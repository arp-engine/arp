#include "3d/terrain/integration/terrain_scene_session.h"

#include <cmath>

namespace arp::engine {
TerrainSceneSession::TerrainSceneSession(const resource::ResourceManager& resources,
    renderer::RuntimeScene& runtime,
    physics::PhysicsWorld& world)
    : resources_(resources), runtime_(runtime), world_(world), collider_(world)
{}

TerrainSceneSession::TerrainSceneSession(const resource::ResourceManager& resources,
    renderer::RuntimeScene& runtime,
    physics::PhysicsWorld& world,
    rhi::GpuDevice& device,
    const renderer::TerrainVegetationGpuUpload::Desc& upload)
    : TerrainSceneSession(resources, runtime, world)
{
    gpu_ = core::makeUnique<renderer::TerrainVegetationGpuPublication>(device, upload);
}

TerrainSceneSession::~TerrainSceneSession()
{
    if (scene_)
        preparation_.clear(*scene_, runtime_);
}

TerrainSceneSession::Result TerrainSceneSession::clear()
{
    synchronized_ = false;
    Result result;
    if (gpu_)
        (void)gpu_->stage(nullptr);
    const auto cleared = collider_.clear();
    if (!cleared.success) {
        result.failureStage = FailureStage::PhysicsBinding;
        result.diagnostics = cleared.diagnostics;
        return result;
    }
    if (scene_)
        preparation_.clear(*scene_, runtime_);
    if (gpu_)
        gpu_->publishPrepared();
    vegetation_.clear();
    scene_ = nullptr;
    result.success = true;
    return result;
}

TerrainSceneSession::Result TerrainSceneSession::drainAndClear(
    const rhi::semantics::ShutdownDrainRequest& request)
{
    auto result = clear();
    if (!result.success || !gpu_)
        return result;
    const auto drained = gpu_->drainAndClear(request);
    if (!drained.success) {
        result.success = false;
        result.failureStage = FailureStage::VegetationGpuBinding;
        result.diagnostics = drained.diagnostics;
    }
    return result;
}

TerrainSceneSession::Result TerrainSceneSession::synchronize(scene::Scene& scene, const Desc& desc)
{
    synchronized_ = false;
    Result result;
    if (desc.maxTerrains != 1u || !desc.maxObjects || !desc.binding.valid()
        || (gpu_
            && desc.binding.mode == renderer::TerrainSceneBinding::PreparationMode::QueryOnly)) {
        result.failureStage = FailureStage::Configuration;
        result.diagnostics =
            "ENGINE-TERRAIN-SESSION-CONFIG: explicit single-slot preparation required";
        return result;
    }
    if (scene_ != &scene) {
        result = clear();
        if (!result.success)
            return result;
        scene_ = &scene;
    }
    result = {};
    PublicationContext publication{*this, desc};
    const auto prepared = preparation_.synchronizeWithGate(scene,
        resources_,
        runtime_,
        {desc.maxObjects, desc.maxTerrains, desc.binding},
        publishConsumers,
        &publication);
    if (!prepared.success) {
        result.failureStage = publication.failureStage == FailureStage::None
            ? FailureStage::Preparation
            : publication.failureStage;
        result.diagnostics = prepared.diagnostics;
        result.pending = publication.pending;
        return result;
    }
    result.success = synchronized_ = true;
    return result;
}

bool TerrainSceneSession::publishConsumers(void* context,
    const renderer::SceneTerrainPreparation& candidate,
    core::pmr::String& diagnostics)
{
    auto& publication = *static_cast<PublicationContext*>(context);
    auto& session = publication.session;
    auto vegetation = session.vegetation_;
    if (!candidate.terrainCount()
        || publication.desc.binding.mode
            == renderer::TerrainSceneBinding::PreparationMode::QueryOnly)
        vegetation.clear();
    else {
        const auto source = candidate.singleSource();
        const auto bound = vegetation.bind(session.resources_,
            source.resourceId,
            source.generation,
            source.translation,
            publication.desc.vegetation);
        if (!bound.success) {
            publication.failureStage = FailureStage::VegetationBinding;
            diagnostics = bound.diagnostics;
            return false;
        }
    }
    if (session.gpu_) {
        const auto staged = session.gpu_->stage(vegetation.snapshot());
        if (staged.status != renderer::TerrainVegetationGpuPublication::Status::Ready) {
            publication.failureStage = FailureStage::VegetationGpuBinding;
            publication.pending =
                staged.status == renderer::TerrainVegetationGpuPublication::Status::Pending;
            diagnostics = staged.diagnostics;
            return false;
        }
        if (!vegetation.retainEquivalentSnapshot(session.gpu_->preparedSource())) {
            publication.failureStage = FailureStage::VegetationGpuBinding;
            diagnostics = "VEGETATION-GPU-PUBLICATION-SOURCE-MISMATCH";
            return false;
        }
    }
    if (session.world_.terrainHeightFieldHandle().valid() && !session.collider_.ready()) {
        publication.failureStage = FailureStage::PhysicsBinding;
        diagnostics = "PHYSICS-TERRAIN-OWNERSHIP-CONFLICT";
        return false;
    }
    const auto bound = candidate.terrainCount() == 0u
        ? session.collider_.clear()
        : session.collider_.bind(candidate.singleWorldSurface());
    if (!bound.success) {
        publication.failureStage = FailureStage::PhysicsBinding;
        diagnostics = bound.diagnostics;
        return false;
    }
    if (session.gpu_)
        session.gpu_->publishPrepared();
    session.vegetation_ = std::move(vegetation);
    return true;
}

TerrainSceneSession::Result TerrainSceneSession::fixedStep(
    scene::Scene& scene, const scene::SceneUpdateOptions& options, const Desc& desc)
{
    synchronized_ = false;
    if (!std::isfinite(options.deltaSeconds) || options.deltaSeconds <= 0.0f
        || options.deltaSeconds > physics::PhysicsWorld::maximumStepDeltaSeconds
        || !std::isfinite(options.unscaledDeltaSeconds) || options.unscaledDeltaSeconds < 0.0f
        || !options.maxObjectCount || !options.maxComponentCount) {
        Result result;
        result.failureStage = FailureStage::Configuration;
        result.diagnostics = "ENGINE-TERRAIN-SESSION-STEP-INVALID";
        return result;
    }
    auto result = synchronize(scene, desc);
    if (!result.success)
        return result;
    // Callbacks can mutate terrain authoring; revalidate before native simulation.
    synchronized_ = false;
    const auto updated = scene.fixedUpdate(options);
    if (!updated.ok()) {
        result.success = false;
        result.failureStage = FailureStage::FixedUpdate;
        result.diagnostics = updated.diagnostic().message;
        return result;
    }
    result = synchronize(scene, desc);
    result.fixedUpdateCompleted = true;
    if (!result.success)
        return result;
    synchronized_ = false;
    if (!world_.step(options.deltaSeconds)) {
        result.success = false;
        result.failureStage = FailureStage::PhysicsStep;
        result.diagnostics = world_.lastDiagnostic().message;
        return result;
    }
    result.stepped = synchronized_ = true;
    return result;
}

TerrainSceneSession::Result TerrainSceneSession::hostStep(
    scene::Scene& scene, float deltaSeconds, const Desc& desc)
{
    synchronized_ = false;
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0f
        || deltaSeconds > physics::PhysicsWorld::maximumStepDeltaSeconds) {
        Result result;
        result.failureStage = FailureStage::Configuration;
        result.diagnostics = "ENGINE-TERRAIN-SESSION-HOST-STEP-INVALID";
        return result;
    }
    auto result = synchronize(scene, desc);
    if (!result.success || deltaSeconds == 0.0f)
        return result;
    synchronized_ = false;
    if (!world_.step(deltaSeconds)) {
        result.success = false;
        result.failureStage = FailureStage::PhysicsStep;
        result.diagnostics = world_.lastDiagnostic().message;
        return result;
    }
    result.stepped = synchronized_ = true;
    return result;
}
} // namespace arp::engine
