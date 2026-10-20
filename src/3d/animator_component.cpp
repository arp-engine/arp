#include "3d/animator_component.h"

#include "3d/components.h" // Scene3DAnimationExtractPayload (extract contract)
#include "animation/animator_controller.h"
#include "animation/skinned_mesh_player.h"
#include "assets/glb_animated_mesh_importer.h"
#include "resource/prefab_asset_resources.h"

#include <utility>

namespace arp::scene3d {

namespace {

arp::core::pmr::String makeString(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

const arp::core::pmr::String* requestedStateName(
    const AnimatorComponentDesc& desc, const AnimatorEvaluationRequest& request)
{
    if (!request.stateName.empty()) {
        return &request.stateName;
    }
    if (!desc.defaultStateName.empty()) {
        return &desc.defaultStateName;
    }
    if (desc.controller != nullptr && !desc.controller->defaultStateName.empty()) {
        return &desc.controller->defaultStateName;
    }
    return nullptr;
}

Bounds3D boundsFromMesh(const arp::assets::MeshAsset& mesh)
{
    if (mesh.vertices.empty()) {
        return {};
    }

    auto minX = mesh.vertices.front().position[0];
    auto minY = mesh.vertices.front().position[1];
    auto minZ = mesh.vertices.front().position[2];
    auto maxX = minX;
    auto maxY = minY;
    auto maxZ = minZ;

    for (const auto& vertex : mesh.vertices) {
        const auto x = vertex.position[0];
        const auto y = vertex.position[1];
        const auto z = vertex.position[2];
        minX = x < minX ? x : minX;
        minY = y < minY ? y : minY;
        minZ = z < minZ ? z : minZ;
        maxX = x > maxX ? x : maxX;
        maxY = y > maxY ? y : maxY;
        maxZ = z > maxZ ? z : maxZ;
    }

    return Bounds3D::explicitBounds(
        {
            (minX + maxX) * 0.5f,
            (minY + maxY) * 0.5f,
            (minZ + maxZ) * 0.5f,
        },
        {
            (maxX - minX) * 0.5f,
            (maxY - minY) * 0.5f,
            (maxZ - minZ) * 0.5f,
        });
}

float clampedDeltaSeconds(const float deltaSeconds)
{
    return deltaSeconds < 0.0f ? 0.0f : deltaSeconds;
}

} // namespace

AnimatorEvaluationFrame::AnimatorEvaluationFrame(Bounds3D bounds,
    const std::size_t materialCount,
    arp::core::pmr::String stateName,
    arp::core::pmr::String clipName,
    const float clipTimeSeconds,
    const std::size_t jointCount)
    : bounds_(bounds), materialCount_(materialCount), stateName_(std::move(stateName)),
      clipName_(std::move(clipName)), clipTimeSeconds_(clipTimeSeconds), jointCount_(jointCount)
{}

AnimatorEvaluationResult AnimatorEvaluationResult::success(AnimatorEvaluationFrame frame)
{
    AnimatorEvaluationResult result;
    result.frame_ = std::move(frame);
    return result;
}

AnimatorEvaluationResult AnimatorEvaluationResult::failure(
    const AnimatorEvaluationDiagnosticCode code, const char* message)
{
    AnimatorEvaluationResult result;
    result.diagnostic_.code = code;
    result.diagnostic_.message = makeString(message);
    return result;
}

AnimatorComponent::AnimatorComponent(AnimatorComponentDesc desc,
    arp::core::SharedCallback<arp::resource::ResourceManager*()> resourceManagerProvider)
    : desc_(std::move(desc)), resourceManagerProvider_(std::move(resourceManagerProvider))
{
    if (desc_.controller != nullptr) {
        animator_ = arp::core::makeUnique<animation::AnimatorControllerRuntime>(*desc_.controller);
    }
    if (desc_.character != nullptr) {
        player_ = arp::core::makeUnique<animation::SkinnedMeshPlayer>(*desc_.character);
    }
}

AnimatorComponent::~AnimatorComponent() = default;

arp::reflection::object AnimatorComponent::editorAuthoringProperties() const
{
    return arp::reflection::object(editorProperties_);
}

bool AnimatorComponent::applyEditorAuthoringProperties(const arp::reflection::object& value)
{
    if (!value.is<AnimatorEditorProperties>())
        return false;
    const auto& properties = value.cast<const AnimatorEditorProperties&>();
    if (properties.prefabResourceId.size() > 512u || properties.defaultStateName.size() > 128u)
        return false;

    arp::core::SharedPtr<const arp::assets::AnimatedMeshAsset> meshOwner;
    arp::core::SharedPtr<const animation::AnimatorControllerAsset> controllerOwner;
    arp::core::pmr::String diagnostics(&arp::core::defaultCpuMemoryResource());
    if (!resolveEditorBinding(properties, meshOwner, controllerOwner, diagnostics)) {
        editorBindingDiagnostic_ = std::move(diagnostics);
        return false;
    }

    editorProperties_ = properties;
    installEditorBinding(std::move(meshOwner), std::move(controllerOwner));
    editorBindingDiagnostic_.clear();
    return true;
}

bool AnimatorComponent::resolveEditorBinding(const AnimatorEditorProperties& properties,
    arp::core::SharedPtr<const arp::assets::AnimatedMeshAsset>& meshOwner,
    arp::core::SharedPtr<const animation::AnimatorControllerAsset>& controllerOwner,
    arp::core::pmr::String& diagnostics) const
{
    if (properties.prefabResourceId.empty())
        return true;
    auto* manager = resourceManagerProvider_ ? resourceManagerProvider_() : nullptr;
    if (!manager) {
        diagnostics = "ANIMATOR-RESOURCE-MANAGER-UNAVAILABLE";
        return false;
    }
    const auto prefab = manager->find<arp::resource::PrefabResource>(
        {properties.prefabResourceId.data(), properties.prefabResourceId.size()});
    if (!prefab.ready()) {
        diagnostics = prefab.diagnostics().empty() ? "ANIMATOR-PREFAB-NOT-READY"
                                                   : prefab.diagnostics();
        return false;
    }
    if (prefab.get()->runtimePayloadId.empty()) {
        diagnostics = "ANIMATOR-RUNTIME-PAYLOAD-ID-MISSING";
        return false;
    }
    const auto runtime = manager->find<arp::resource::PrefabRuntimePayloadResource>(
        {prefab.get()->runtimePayloadId.data(), prefab.get()->runtimePayloadId.size()});
    if (!runtime.ready()) {
        diagnostics = runtime.diagnostics().empty() ? "ANIMATOR-RUNTIME-PAYLOAD-NOT-READY"
                                                    : runtime.diagnostics();
        return false;
    }
    meshOwner = runtime.get()->runtimeAsset;
    controllerOwner = runtime.get()->controllerAsset;
    if (!meshOwner || !controllerOwner || meshOwner->skeleton.jointCount() == 0u) {
        diagnostics = "ANIMATOR-RUNTIME-PAYLOAD-INCOMPLETE";
        return false;
    }

    if (!runtime.get()->defaultStateName.empty()
        && !controllerOwner->defaultStateName.empty()
        && runtime.get()->defaultStateName != controllerOwner->defaultStateName) {
        diagnostics = "ANIMATOR-DEFAULT-STATE-MISMATCH";
        meshOwner.reset();
        controllerOwner.reset();
        return false;
    }
    const auto& stateName = properties.defaultStateName.empty()
        ? controllerOwner->defaultStateName
        : properties.defaultStateName;
    const auto state = std::find_if(controllerOwner->states.begin(),
        controllerOwner->states.end(), [&](const auto& candidate) {
            return candidate.name == stateName;
        });
    if (stateName.empty() || state == controllerOwner->states.end()) {
        diagnostics = "ANIMATOR-DEFAULT-STATE-UNKNOWN";
        meshOwner.reset();
        controllerOwner.reset();
        return false;
    }
    const auto clip = std::find_if(meshOwner->clips.begin(), meshOwner->clips.end(),
        [&](const auto& candidate) { return candidate.name == state->clipName; });
    if (clip == meshOwner->clips.end()) {
        diagnostics = "ANIMATOR-STATE-CLIP-MISSING";
        meshOwner.reset();
        controllerOwner.reset();
        return false;
    }
    const animation::SkinnedMeshPlayer candidatePlayer(*meshOwner);
    if (!candidatePlayer.valid()) {
        diagnostics = "ANIMATOR-SKINNED-MESH-INVALID";
        meshOwner.reset();
        controllerOwner.reset();
        return false;
    }
    return true;
}

void AnimatorComponent::installEditorBinding(
    arp::core::SharedPtr<const arp::assets::AnimatedMeshAsset> meshOwner,
    arp::core::SharedPtr<const animation::AnimatorControllerAsset> controllerOwner)
{
    animator_.reset();
    player_.reset();
    meshOwner_ = std::move(meshOwner);
    controllerOwner_ = std::move(controllerOwner);
    boundResourceManager_ = nullptr;
    boundPrefabGeneration_ = 0u;
    boundRuntimeGeneration_ = 0u;
    boundPrefabResourceId_.clear();
    desc_.character = meshOwner_.get();
    desc_.controller = controllerOwner_.get();
    desc_.clip = nullptr;
    desc_.materialCount = meshOwner_ ? meshOwner_->materials.size() : 0u;
    desc_.defaultStateName = editorProperties_.defaultStateName;
    if (!meshOwner_ || !controllerOwner_)
        return;

    if (desc_.defaultStateName.empty())
        desc_.defaultStateName = controllerOwner_->defaultStateName;
    const auto state = std::find_if(controllerOwner_->states.begin(),
        controllerOwner_->states.end(), [&](const auto& candidate) {
            return candidate.name == desc_.defaultStateName;
        });
    if (state != controllerOwner_->states.end()) {
        const auto clip = std::find_if(meshOwner_->clips.begin(), meshOwner_->clips.end(),
            [&](const auto& candidate) { return candidate.name == state->clipName; });
        if (clip != meshOwner_->clips.end())
            desc_.clip = &*clip;
    }
    animator_ = arp::core::makeUnique<animation::AnimatorControllerRuntime>(*controllerOwner_);
    player_ = arp::core::makeUnique<animation::SkinnedMeshPlayer>(*meshOwner_);
}

bool AnimatorComponent::refreshEditorBinding()
{
    if (editorProperties_.prefabResourceId.empty())
        return true;
    auto* manager = resourceManagerProvider_ ? resourceManagerProvider_() : nullptr;
    if (!manager) {
        installEditorBinding({}, {});
        editorBindingDiagnostic_ = "ANIMATOR-RESOURCE-MANAGER-UNAVAILABLE";
        return false;
    }
    if (manager != boundResourceManager_)
        installEditorBinding({}, {});

    const auto prefab = manager->find<arp::resource::PrefabResource>(
        {editorProperties_.prefabResourceId.data(), editorProperties_.prefabResourceId.size()});
    if (!prefab.ready() || prefab.get()->runtimePayloadId.empty()) {
        installEditorBinding({}, {});
        editorBindingDiagnostic_ = prefab.ready() ? "ANIMATOR-RUNTIME-PAYLOAD-ID-MISSING"
                                                  : prefab.diagnostics();
        return false;
    }
    const auto runtime = manager->find<arp::resource::PrefabRuntimePayloadResource>(
        {prefab.get()->runtimePayloadId.data(), prefab.get()->runtimePayloadId.size()});
    if (!runtime.ready()) {
        installEditorBinding({}, {});
        editorBindingDiagnostic_ = runtime.diagnostics();
        return false;
    }
    if (boundResourceManager_ == manager
        && boundPrefabResourceId_ == editorProperties_.prefabResourceId
        && boundPrefabGeneration_ == prefab.generation()
        && boundRuntimeGeneration_ == runtime.generation()
        && (editorProperties_.defaultStateName.empty()
            || editorProperties_.defaultStateName == desc_.defaultStateName)) {
        editorBindingDiagnostic_.clear();
        return true;
    }

    installEditorBinding({}, {});
    arp::core::SharedPtr<const arp::assets::AnimatedMeshAsset> meshOwner;
    arp::core::SharedPtr<const animation::AnimatorControllerAsset> controllerOwner;
    arp::core::pmr::String diagnostics(&arp::core::defaultCpuMemoryResource());
    if (!resolveEditorBinding(editorProperties_, meshOwner, controllerOwner, diagnostics)) {
        installEditorBinding({}, {});
        editorBindingDiagnostic_ = std::move(diagnostics);
        return false;
    }
    installEditorBinding(std::move(meshOwner), std::move(controllerOwner));
    boundResourceManager_ = manager;
    boundPrefabGeneration_ = prefab.generation();
    boundRuntimeGeneration_ = runtime.generation();
    boundPrefabResourceId_ = editorProperties_.prefabResourceId;
    editorBindingDiagnostic_.clear();
    return true;
}

void AnimatorComponent::extract(
    const arp::scene::RenderExtractContext& ctx, arp::scene::RenderExtractSink& sink) const
{
    if (!ctx.activeInHierarchy || !enabled()) {
        arp::scene::RenderExtractPayload marker;
        marker.kind = arp::scene::RenderExtractKind::Custom;
        marker.typeId = "scene3d.disabled";
        sink.accept(ctx, marker);
        return;
    }
    if (!editorProperties_.prefabResourceId.empty()
        && !const_cast<AnimatorComponent*>(this)->refreshEditorBinding()) {
        arp::scene::RenderExtractPayload marker;
        marker.kind = arp::scene::RenderExtractKind::Custom;
        marker.typeId = "scene3d.animation.unavailable";
        sink.accept(ctx, marker);
        return;
    }
    Scene3DAnimationExtractPayload domain{desc_.clip, desc_.controller};
    arp::scene::RenderExtractPayload payload;
    payload.kind = arp::scene::RenderExtractKind::Custom;
    payload.typeId = "scene3d.animation";
    payload.data = &domain;
    sink.accept(ctx, payload);
}

AnimatorEvaluationResult AnimatorComponent::evaluate(const AnimatorEvaluationRequest& request)
{
    if (!editorProperties_.prefabResourceId.empty() && !refreshEditorBinding()) {
        return AnimatorEvaluationResult::failure(AnimatorEvaluationDiagnosticCode::MissingAsset,
            "AnimatorComponent editor resource binding is unavailable.");
    }
    if (desc_.character == nullptr) {
        return AnimatorEvaluationResult::failure(AnimatorEvaluationDiagnosticCode::MissingAsset,
            "AnimatorComponent requires an animated mesh asset.");
    }
    if (desc_.controller == nullptr || !animator_) {
        return AnimatorEvaluationResult::failure(AnimatorEvaluationDiagnosticCode::MissingAnimator,
            "AnimatorComponent requires animator controller data.");
    }
    if (!player_ || !player_->valid()) {
        return AnimatorEvaluationResult::failure(
            AnimatorEvaluationDiagnosticCode::MissingSkinnedMesh,
            "AnimatorComponent requires valid skinned mesh data.");
    }

    const auto* stateName = requestedStateName(desc_, request);
    if (stateName == nullptr || stateName->empty()) {
        return AnimatorEvaluationResult::failure(
            AnimatorEvaluationDiagnosticCode::MissingAnimationState,
            "AnimatorComponent requires an explicit animation state.");
    }

    if (*stateName != currentStateName_) {
        if (!animator_->playState(*stateName)) {
            return AnimatorEvaluationResult::failure(
                AnimatorEvaluationDiagnosticCode::MissingAnimationState,
                "AnimatorComponent could not find the requested animation state.");
        }
        currentStateName_ = *stateName;
    }

    animator_->advance(clampedDeltaSeconds(request.deltaSeconds));
    const auto& clipName = animator_->activeClipName();
    if (clipName.empty()) {
        return AnimatorEvaluationResult::failure(
            AnimatorEvaluationDiagnosticCode::MissingActiveClip,
            "AnimatorComponent active animation state has no clip.");
    }

    if (!player_->update(clipName, animator_->clipTimeSeconds())) {
        return AnimatorEvaluationResult::failure(AnimatorEvaluationDiagnosticCode::SkinningFailed,
            "AnimatorComponent failed to evaluate skinned mesh pose.");
    }

    // GPU-skinning-only path (Phase 3): the frame carries animation-state
    // metadata only. Bounds come from the immutable bind-pose mesh -- the
    // renderer consumes the bind-pose vertex stream plus the GPU skinning
    // palette (built from SkinnedMeshPlayer::skinMatrices()) so no CPU-baked
    // mesh is exposed here.
    const auto& bindPoseMesh = desc_.character->mesh;
    return AnimatorEvaluationResult::success(AnimatorEvaluationFrame(boundsFromMesh(bindPoseMesh),
        desc_.materialCount,
        *stateName,
        clipName,
        animator_->clipTimeSeconds(),
        desc_.character->skeleton.jointCount()));
}

} // namespace arp::scene3d
