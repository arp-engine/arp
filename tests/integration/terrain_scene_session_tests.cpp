#include "3d/terrain/integration/terrain_scene_session.h"
#include "resource/internal_asset_package.h"
#include "tests/fixtures/flat_terrain_surface.h"
#include "tests/fixtures/terrain_physics_world.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace arp;
using namespace arp::tests::terrain_physics;
using Session = engine::TerrainSceneSession;
constexpr const char* terrainId = "3f6fbcca-17d2-46a6-9817-2af357399301";

Session::Desc description(renderer::MaterialHandle& material)
{
    Session::Desc desc;
    desc.maxObjects = 16u;
    desc.maxTerrains = 1u;
    desc.binding.useAssetTileLayout = true;
    desc.binding.resolveMaterial = resolveMaterial;
    desc.binding.context = &material;
    return desc;
}

scene::SceneUpdateOptions stepOptions()
{
    return {1.0f / 60.0f, 0u, 16u, 64u, 1.0f / 60.0f};
}

class FixedCallback final : public scene::Component {
public:
    enum class Action { None, Translate, ExtraTerrain, Throw };
    Action action = Action::None;
    unsigned calls = 0u;
    unsigned updateCalls = 0u;
    scene::GameObject* terrainObject = nullptr;
    scene::GameObjectId injected;

    void onUpdate(const scene::SceneUpdateContext&) override { ++updateCalls; }

    void onFixedUpdate(const scene::SceneUpdateContext&) override
    {
        ++calls;
        const auto pending = action;
        action = Action::None;
        if (pending == Action::Translate)
            terrainObject->set_local_transform(scene::Transform::fromTranslation({100, 7, 200}));
        else if (pending == Action::ExtraTerrain) {
            auto& extra = gameObject()->scene()->createObject("callback-injected-terrain");
            injected = extra.id();
            extra.addComponent<scene3d::TerrainComponent>().set_resource_id(terrainId);
        } else if (pending == Action::Throw)
            throw std::runtime_error("explicit-fixed-callback-failure");
    }
};

physics::PhysicsBodyHandle fallingBody(physics::PhysicsWorld& world)
{
    physics::RigidBodyDesc body;
    body.motionType = physics::BodyMotionType::Dynamic;
    body.layer = physics::CollisionLayer::Dynamic;
    const auto handle = world.createBody(body, {500, 30, 500}, {});
    assert(handle.valid());
    return handle;
}

core::Vec3 position(physics::PhysicsWorld& world, physics::PhysicsBodyHandle handle)
{
    const float unset = std::numeric_limits<float>::quiet_NaN();
    core::Vec3 result{unset, unset, unset};
    core::Quat rotation;
    world.getBodyTransform(handle, result, rotation);
    assert(std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z));
    return result;
}

void validatesStepContract()
{
    static_assert(!std::is_copy_constructible_v<Session> && !std::is_move_constructible_v<Session>);
    static_assert(physics::PhysicsWorld::maximumStepDeltaSeconds > 0.0f);
    resource::ResourceManager resources;
    renderer::RuntimeScene runtime;
    renderer::MaterialHandle material;
    const auto desc = description(material);
    auto world = makeWorld();
    scene::Scene scene("empty-standard-scene");
    auto& callback = scene.addComponent<FixedCallback>();
    const auto body = fallingBody(*world);
    Session session(resources, runtime, *world);
    const auto initial = position(*world, body);
    auto options = stepOptions();
    for (const float delta : {0.0f,
             -1.0f,
             std::numeric_limits<float>::infinity(),
             std::numeric_limits<float>::quiet_NaN(),
             physics::PhysicsWorld::maximumStepDeltaSeconds * 2}) {
        options.deltaSeconds = delta;
        const auto rejected = session.fixedStep(scene, options, desc);
        assert(!rejected.success && !rejected.fixedUpdateCompleted && !rejected.stepped);
        assert(rejected.failureStage == Session::FailureStage::Configuration);
        assert(callback.calls == 0u && position(*world, body).y == initial.y);
        assert(!session.synchronized() && !session.worldSurface());
    }
    options = stepOptions();
    auto invalid = desc;
    invalid.maxTerrains = 2u;
    assert(!session.fixedStep(scene, options, invalid).success && callback.calls == 0u);
    options.maxComponentCount = 0u;
    assert(!session.fixedStep(scene, options, desc).success && callback.calls == 0u);
    options = stepOptions();
    const auto stepped = session.fixedStep(scene, options, desc);
    assert(stepped.success && stepped.fixedUpdateCompleted && stepped.stepped
        && session.synchronized());
    assert(callback.calls == 1u && position(*world, body).y < initial.y);
    callback.action = FixedCallback::Action::Throw;
    const auto prior = position(*world, body);
    const auto failed = session.fixedStep(scene, options, desc);
    assert(!failed.success && !failed.fixedUpdateCompleted && !failed.stepped);
    assert(failed.failureStage == Session::FailureStage::FixedUpdate);
    assert(failed.diagnostics == "explicit-fixed-callback-failure");
    assert(!session.synchronized() && position(*world, body).y == prior.y);
    assert(session.fixedStep(scene, options, desc).success);
    assert(session.clear().success && session.clear().success);

    // An unrelated existing slot is never selected or silently inherited.
    auto flat = core::makeShared<tests::FlatTerrainSurface>();
    physics::TerrainSurfacePhysicsSource foreign(flat);
    assert(world->setTerrainHeightField(foreign.source(), foreign.bounds()));
    const auto foreignHandle = world->terrainHeightFieldHandle();
    const auto calls = callback.calls;
    const auto conflict = session.fixedStep(scene, options, desc);
    assert(!conflict.success && !conflict.fixedUpdateCompleted && callback.calls == calls);
    assert(conflict.failureStage == Session::FailureStage::PhysicsBinding);
    assert(session.clear().success && world->terrainHeightFieldHandle() == foreignHandle);
    assert(world->clearTerrainHeightField(foreignHandle));

    physics::PhysicsWorldDiagnostic diagnostic;
    auto notInitialized = physics::createPhysicsWorld(diagnostic);
    Session unavailable(resources, runtime, *notInitialized);
    const auto nativeFailure = unavailable.fixedStep(scene, options, desc);
    assert(!nativeFailure.success && nativeFailure.fixedUpdateCompleted && !nativeFailure.stepped);
    assert(nativeFailure.failureStage == Session::FailureStage::PhysicsStep);
    assert(!unavailable.synchronized());
}

void validatesResourceSession(const std::filesystem::path& package)
{
    resource::ResourceManager resources;
    assert(resource::InternalAssetPackageReader().load(package, resources).success);
    renderer::RuntimeScene runtime;
    auto material = runtime.addMaterial("explicit-cpu-session-fixture");
    const auto desc = description(material);
    const auto options = stepOptions();
    auto world = makeWorld();
    const auto body = fallingBody(*world);
    scene::Scene first("first", scene::SceneId::fromValue(77u));
    scene::Scene second("second-with-same-authored-id", scene::SceneId::fromValue(77u));
    auto& object = first.createObject("Terrain");
    const auto objectId = object.id();
    auto& component = object.addComponent<scene3d::TerrainComponent>();
    const auto componentId = component.id();
    component.set_resource_id(terrainId);
    object.set_local_transform(scene::Transform::fromTranslation({100, 5, 200}));
    auto& callback = first.addComponent<FixedCallback>();
    callback.terrainObject = &object;
    auto& nextObject = second.createObject("Terrain");
    auto& nextTerrain = nextObject.addComponent<scene3d::TerrainComponent>();
    nextTerrain.set_resource_id(terrainId);
    nextObject.set_local_transform(scene::Transform::fromTranslation({100, 11, 200}));
    Session session(resources, runtime, *world);
    assert(session.synchronize(first, desc).success);
    const auto snapshot = session.worldSurface();
    const auto bounds = snapshot->bounds();
    const float x = (bounds.minX + bounds.maxX) * 0.5f, z = (bounds.minZ + bounds.maxZ) * 0.5f;
    const auto original = hit(*world, x, z, 8.5f).body;
    assert(resources.unload(terrainId).success);
    const auto prior = position(*world, body);
    const auto missing = session.fixedStep(first, options, desc);
    assert(!missing.success && !missing.fixedUpdateCompleted && !missing.stepped
        && callback.calls == 0u);
    assert(!session.worldSurface() && position(*world, body).y == prior.y);
    assert(hit(*world, x, z, 8.5f).body == original);
    assert(resource::InternalAssetPackageReader().load(package, resources).success);
    callback.action = FixedCallback::Action::Translate;
    const auto moved = session.fixedStep(first, options, desc);
    assert(moved.success && moved.fixedUpdateCompleted && moved.stepped && callback.calls == 1u);
    assert(session.worldSurface()->sample(x, z).sample.height == 10.5f);
    assert(hit(*world, x, z, 10.5f).body != original);
    callback.action = FixedCallback::Action::ExtraTerrain;
    const auto beforeMutation = position(*world, body);
    const auto extra = session.fixedStep(first, options, desc);
    assert(!extra.success && extra.fixedUpdateCompleted && !extra.stepped);
    assert(extra.failureStage == Session::FailureStage::Preparation);
    assert(!session.synchronized() && !session.worldSurface());
    assert(position(*world, body).y == beforeMutation.y);
    assert(first.destroyObject(callback.injected).ok());
    assert(session.fixedStep(first, options, desc).success);

    core::pmr::Vector<physics::PhysicsBodyHandle> obstacles;
    physics::RigidBodyDesc obstacle;
    obstacle.motionType = physics::BodyMotionType::Static;
    obstacle.layer = physics::CollisionLayer::Static;
    for (unsigned i = 0; i < 6u; ++i) {
        const auto handle = world->createBody(obstacle, {-500, 10, float(i) * 2}, {});
        assert(handle.valid());
        obstacles.push_back(handle);
    }
    const auto beforeCapacity = position(*world, body);
    const auto priorLease = world->terrainHeightFieldHandle();
    const renderer::TerrainHandle runtimeHandle{component.desc().terrain.id};
    const auto priorRuntime = *runtime.terrain(runtimeHandle);
    const auto priorResolved = component.desc();
    const auto calls = callback.calls;
    object.set_local_transform(scene::Transform::fromTranslation({100, 9, 200}));
    const auto exhausted = session.fixedStep(first, options, desc);
    assert(!exhausted.success && !exhausted.fixedUpdateCompleted && !exhausted.stepped);
    assert(exhausted.failureStage == Session::FailureStage::PhysicsBinding);
    assert(!session.synchronized() && !session.worldSurface() && callback.calls == calls);
    assert(position(*world, body).y == beforeCapacity.y);
    assert(hit(*world, x, z, 10.5f).body == priorLease);
    assert(*runtime.terrain(runtimeHandle) == priorRuntime);
    assert(component.desc().terrain.id == priorResolved.terrain.id
        && component.desc().material.id == priorResolved.material.id);
    for (const auto handle : obstacles)
        world->destroyBody(handle);
    assert(session.fixedStep(first, options, desc).success);
    assert(hit(*world, x, z, 12.5f).body != priorLease);
    assert(*runtime.terrain(runtimeHandle) != priorRuntime);
    component.set_enabled(false);
    assert(session.fixedStep(first, options, desc).success);
    assert(!world->terrainHeightFieldHandle().valid());
    misses(*world, x, z);
    component.set_enabled(true);
    assert(session.fixedStep(first, options, desc).success);
    auto& parent = first.createObject("TerrainParent");
    assert(first.reparent(object.id(), parent.id()).ok());
    parent.set_active(false);
    assert(session.fixedStep(first, options, desc).success);
    misses(*world, x, z);
    parent.set_active(true);
    assert(session.fixedStep(first, options, desc).success);
    assert(object.removeComponent(componentId));
    assert(session.fixedStep(first, options, desc).success);
    misses(*world, x, z);
    object.addComponent<scene3d::TerrainComponent>().set_resource_id(terrainId);
    assert(session.fixedStep(first, options, desc).success);
    assert(session.fixedStep(second, options, desc).success);
    assert(!object.getComponent<scene3d::TerrainComponent>()->desc().terrain.valid());
    assert(hit(*world, x, z, 14.5f).body == world->terrainHeightFieldHandle());
    assert(session.fixedStep(first, options, desc).success);
    assert(!nextTerrain.desc().terrain.valid());
    assert(first.destroyObject(objectId).ok());
    assert(session.fixedStep(first, options, desc).success);
    misses(*world, x, z);
    assert(session.clear().success && session.clear().success);
    {
        Session lifetime(resources, runtime, *world);
        assert(lifetime.synchronize(second, desc).success);
        assert(nextTerrain.desc().terrain.valid());
    }
    assert(!nextTerrain.desc().terrain.valid() && !world->terrainHeightFieldHandle().valid());
    misses(*world, x, z);
    {
        Session replaced(resources, runtime, *world);
        assert(replaced.synchronize(second, desc).success && replaced.synchronized());
        physics::TerrainSurfacePhysicsSource external(
            core::makeShared<tests::FlatTerrainSurface>());
        assert(world->setTerrainHeightField(external.source(), external.bounds()));
        assert(!replaced.synchronized() && !replaced.worldSurface());
        assert(!replaced.clear().success);
    }
    assert(!nextTerrain.desc().terrain.valid());
    const auto externalHandle = world->terrainHeightFieldHandle();
    assert(hit(*world, 0, 0, 0).body == externalHandle);
    assert(world->clearTerrainHeightField(externalHandle));
    assert(snapshot->sample(x, z).sample.height == 8.5f);
    std::puts(
        "Fresh baked package -> engine scene session -> native fixed-step/cleanup gate passed");
}

void validatesHostBoundary()
{
    resource::ResourceManager resources;
    renderer::RuntimeScene runtime;
    auto world = makeWorld();
    scene::Scene scene("host-boundary");
    auto& callback = scene.addComponent<FixedCallback>();
    const auto body = fallingBody(*world);
    const float initial = position(*world, body).y;
    Session::Desc desc;
    desc.maxObjects = 16u;
    desc.maxTerrains = 1u;
    desc.binding.mode = renderer::TerrainSceneBinding::PreparationMode::QueryOnly;
    Session session(resources, runtime, *world);
    auto zero = session.hostStep(scene, 0.0f, desc);
    assert(zero.success && !zero.stepped && !zero.fixedUpdateCompleted);
    assert(position(*world, body).y == initial && session.synchronized());
    for (const float delta : {-1.0f,
             std::numeric_limits<float>::infinity(),
             std::numeric_limits<float>::quiet_NaN(),
             physics::PhysicsWorld::maximumStepDeltaSeconds * 2}) {
        const auto invalid = session.hostStep(scene, delta, desc);
        assert(!invalid.success && !invalid.stepped && !invalid.fixedUpdateCompleted);
        assert(invalid.failureStage == Session::FailureStage::Configuration);
        assert(!session.synchronized() && position(*world, body).y == initial);
    }
    auto invalidMode = desc;
    invalidMode.binding.mode = static_cast<renderer::TerrainSceneBinding::PreparationMode>(999);
    assert(!session.hostStep(scene, 0.0f, invalidMode).success);
    const auto stepped =
        session.hostStep(scene, physics::PhysicsWorld::maximumStepDeltaSeconds, desc);
    assert(stepped.success && stepped.stepped && !stepped.fixedUpdateCompleted);
    assert(position(*world, body).y < initial);
    auto referenceWorld = makeWorld();
    const auto referenceBody = fallingBody(*referenceWorld);
    assert(referenceWorld->step(physics::PhysicsWorld::maximumStepDeltaSeconds));
    assert(position(*world, body).y == position(*referenceWorld, referenceBody).y);
    assert(callback.calls == 0u && callback.updateCalls == 0u);
    assert(scene.update(stepOptions()).ok() && callback.updateCalls == 1u);
    assert(session.hostStep(scene, 0.0f, desc).success);
    assert(callback.calls == 0u && callback.updateCalls == 1u);
    assert(session.clear().success);

    physics::PhysicsWorldDiagnostic diagnostic;
    auto unavailableWorld = physics::createPhysicsWorld(diagnostic);
    Session unavailable(resources, runtime, *unavailableWorld);
    const auto failed = unavailable.hostStep(scene, 1.0f / 60.0f, desc);
    assert(!failed.success && !failed.stepped && !failed.fixedUpdateCompleted);
    assert(
        failed.failureStage == Session::FailureStage::PhysicsStep && !unavailable.synchronized());
}

void validatesQueryOnlyPackage(const std::filesystem::path& package)
{
    resource::ResourceManager resources;
    assert(resource::InternalAssetPackageReader().load(package, resources).success);
    renderer::RuntimeScene runtime;
    auto world = makeWorld();
    scene::Scene scene("independent-render-owner");
    auto& object = scene.createObject("Terrain");
    auto& terrain = object.addComponent<scene3d::TerrainComponent>();
    terrain.set_resource_id(terrainId);
    object.set_local_transform(scene::Transform::fromTranslation({100, 5, 200}));
    // An independent CPU renderer fixture already owns these resolved handles.
    // Query-only preparation must never replace, clear or unload them.
    const auto independent = core::makeShared<terrain::TerrainRuntime>();
    const auto independentHandle = runtime.registerTerrain(independent);
    scene3d::TerrainComponentDesc resolved;
    resolved.terrain = scene3d::TerrainHandle{independentHandle.id};
    resolved.material = runtime.addMaterial("independent-cpu-fixture");
    terrain.bindResolvedTerrain(resolved);
    Session::Desc desc;
    desc.maxObjects = 16u;
    desc.maxTerrains = 1u;
    desc.binding.mode = renderer::TerrainSceneBinding::PreparationMode::QueryOnly;
    // Deliberately invalid render runtime settings and resolver: neither is
    // consumed by this explicit query/collider contract.
    desc.binding.runtime.samplesPerChunk = 0u;
    desc.binding.resolveMaterial = [](void*,
                                       const resource::TerrainAssetResource&,
                                       renderer::MaterialHandle&,
                                       core::pmr::String&) -> bool {
        assert(false && "query-only must not resolve a material");
        return false;
    };
    const auto body = fallingBody(*world);
    const float before = position(*world, body).y;
    std::shared_ptr<const terrain::TerrainSurface> retained;
    {
        Session session(resources, runtime, *world);
        assert(session.hostStep(scene, 0.0f, desc).success);
        retained = session.worldSurface();
        assert(retained && !session.vegetationSnapshot());
        const auto b = retained->bounds();
        const float x = (b.minX + b.maxX) * 0.5f, z = (b.minZ + b.maxZ) * 0.5f;
        const float height = retained->sample(x, z).sample.height;
        const auto lease = hit(*world, x, z, height).body;
        assert(session.synchronize(scene, desc).success && session.worldSurface() == retained);
        assert(world->terrainHeightFieldHandle() == lease);
        auto renderDesc = desc;
        renderDesc.binding.mode = renderer::TerrainSceneBinding::PreparationMode::RenderAndQuery;
        const auto switched = session.hostStep(scene, 1.0f / 60.0f, renderDesc);
        assert(!switched.success && switched.diagnostics == "TERRAIN-SCENE-MODE-CLEAR-REQUIRED");
        assert(position(*world, body).y == before && !session.worldSurface());
        assert(world->terrainHeightFieldHandle() == lease);
        assert(resources.unload(terrainId).success);
        assert(!session.hostStep(scene, 1.0f / 60.0f, desc).success);
        assert(position(*world, body).y == before && hit(*world, x, z, height).body == lease);
        assert(resource::InternalAssetPackageReader().load(package, resources).success);
        assert(session.synchronize(scene, desc).success);
        assert(world->terrainHeightFieldHandle() == lease);
        core::pmr::Vector<physics::PhysicsBodyHandle> blockers{&core::defaultCpuMemoryResource()};
        physics::RigidBodyDesc blocker;
        blocker.motionType = physics::BodyMotionType::Static;
        blocker.layer = physics::CollisionLayer::Static;
        for (unsigned i = 0u; i < 6u; ++i) {
            const auto handle = world->createBody(blocker, {-500, 10, float(i) * 2}, {});
            assert(handle.valid());
            blockers.push_back(handle);
        }
        object.set_local_transform(scene::Transform::fromTranslation({100, 7, 200}));
        const auto exhausted = session.hostStep(scene, 1.0f / 60.0f, desc);
        assert(!exhausted.success && !exhausted.stepped);
        assert(exhausted.failureStage == Session::FailureStage::PhysicsBinding);
        assert(!session.worldSurface() && position(*world, body).y == before);
        assert(hit(*world, x, z, height).body == lease);
        assert(*runtime.terrain(independentHandle) == independent);
        object.set_local_transform(scene::Transform::fromTranslation({100, 5, 200}));
        assert(session.synchronize(scene, desc).success && session.worldSurface() == retained);
        for (const auto handle : blockers)
            world->destroyBody(handle);
        terrain.setEnabled(false);
        assert(session.synchronize(scene, desc).success && !session.worldSurface());
        assert(!world->terrainHeightFieldHandle().valid());
        assert(!session.synchronize(scene, renderDesc).success);
        assert(terrain.desc().terrain.id == independentHandle.id);
        assert(terrain.desc().material.id == resolved.material.id);
        assert(*runtime.terrain(independentHandle) == independent);
        terrain.setEnabled(true);
        assert(session.synchronize(scene, desc).success);
        object.set_local_transform(scene::Transform::fromTranslation({100, 7, 200}));
        assert(session.hostStep(scene, 0.0f, desc).success);
        assert(hit(*world, x, z, height + 2.0f).body != lease);
        assert(session.clear().success);
        // Explicit detach allows a different consumer policy, on a fresh scene
        // that does not share the independently owned renderer fixture.
        scene::Scene empty("detached");
        assert(session.synchronize(empty, renderDesc).success);
        assert(session.clear().success);
        assert(session.synchronize(scene, desc).success);
    }
    assert(!world->terrainHeightFieldHandle().valid());
    assert(*runtime.terrain(independentHandle) == independent);
    assert(terrain.desc().terrain.id == independentHandle.id);
    assert(terrain.desc().material.id == resolved.material.id);
    // Query-only did not consume any renderer registration ID.
    assert(runtime.registerTerrain(independent).id == independentHandle.id + 1u);
    assert(retained && retained->generation());
    std::puts("Query-only package/host boundary preserves independent renderer ownership");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2) {
        validatesQueryOnlyPackage(argv[1]);
        validatesResourceSession(argv[1]);
    } else {
        validatesHostBoundary();
        validatesStepContract();
        std::puts(
            "Engine terrain session configuration, callback/native-step and ownership gates passed");
    }
}
