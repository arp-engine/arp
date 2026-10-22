#include "3d/terrain/terrain_physics_binding.h"
#include "3d/terrain/terrain_surface_instance.h"
#include "renderer/scene_terrain_preparation.h"
#include "resource/internal_asset_package.h"
#include "tests/fixtures/flat_terrain_surface.h"
#include "tests/fixtures/terrain_physics_world.h"

#include <cassert>
#include <cstdio>
#include <limits>

namespace {
using namespace arp;
using namespace arp::tests::terrain_physics;

std::shared_ptr<const terrain::TerrainSurface> translated(float y)
{
    return core::makeShared<terrain::TerrainSurfaceInstance>(
        core::makeShared<tests::FlatTerrainSurface>(), core::Vec3{100.0f, y, 200.0f});
}

class UnavailableSurface final : public terrain::TerrainSurface {
public:
    explicit UnavailableSurface(
        terrain::TerrainBounds bounds = {0, 2, 0, 2}, std::uint64_t generation = 17u)
        : bounds_(bounds), generation_(generation)
    {}

    terrain::TerrainBounds bounds() const noexcept override { return bounds_; }

    std::uint64_t generation() const noexcept override { return generation_; }

    terrain::TerrainSurfaceQuery sample(float, float) const override { return {}; }

private:
    terrain::TerrainBounds bounds_;
    std::uint64_t generation_;
};

void validatesNativeLifecycle()
{
    auto world = makeWorld();
    assert(!world->terrainHeightFieldHandle().valid());
    assert(!world->clearTerrainHeightField({}));
    physics::TerrainPhysicsBinding binding(*world);
    auto surface = translated(5.0f);
    assert(binding.bind(surface).success && binding.ready());
    const auto first = binding.handle();
    assert(hit(*world, 100.0f, 200.0f, 5.0f).body == first);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned repeat = 0; repeat < 8u; ++repeat)
        assert(binding.bind(surface).reused);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    assert(binding.generation() == surface->generation());
    assert(!binding.bind(nullptr).success);
    assert(!binding.bind(core::makeShared<UnavailableSurface>()).success);
    const auto mismatched = core::makeShared<UnavailableSurface>(
        terrain::TerrainBounds{0, 2, 0, 2}, surface->generation());
    assert(binding.bind(mismatched).diagnostics == "PHYSICS-TERRAIN-GENERATION-BOUNDS-MISMATCH");
    assert(binding.bind(core::makeShared<UnavailableSurface>(surface->bounds(), 0u)).diagnostics
        == "PHYSICS-TERRAIN-SURFACE-INVALID");
    physics::TerrainPhysicsSource sentinel;
    sentinel.trySampleFn = [](void*, float, float, float& height) {
        height = std::numeric_limits<float>::max();
        return true;
    };
    const physics::TerrainSurfacePhysicsSource source(surface);
    // A finite engine height must not silently become a native collider hole.
    assert(!world->setTerrainHeightField(sentinel, source.bounds()));
    assert(binding.handle() == first && binding.surface() == surface);
    assert(hit(*world, 100.0f, 200.0f, 5.0f).body == first);
    physics::TerrainPhysicsBinding foreign(*world);
    assert(!foreign.bind(translated(20.0f)).success);
    assert(foreign.clear().success && binding.ready());
    assert(!world->clearTerrainHeightField({first.id + 1u}));
    assert(binding.bind(translated(7.0f)).success);
    const auto second = binding.handle();
    assert(second != first && hit(*world, 100.0f, 200.0f, 7.0f).body == second);
    assert(!world->clearTerrainHeightField(first));
    assert(hit(*world, 100.0f, 200.0f, 7.0f).body == second);
    assert(binding.clear().success && binding.clear().success);
    assert(!binding.ready() && !binding.surface() && binding.generation() == 0u);
    misses(*world, 100.0f, 200.0f);
    assert(surface->sample(100.0f, 200.0f).sample.height == 5.0f);
    // Destruction releases only a live lease belonging to this binding.
    {
        physics::TerrainPhysicsBinding temporary(*world);
        assert(temporary.bind(surface).success);
    }
    misses(*world, 100.0f, 200.0f);
    {
        physics::TerrainPhysicsBinding replaced(*world);
        assert(replaced.bind(surface).success);
        physics::TerrainSurfacePhysicsSource external(translated(9.0f));
        assert(world->setTerrainHeightField(external.source(), external.bounds()));
        assert(!replaced.ready() && !replaced.bind(surface).success && !replaced.clear().success);
    }
    const auto external = world->terrainHeightFieldHandle();
    assert(hit(*world, 100.0f, 200.0f, 9.0f).body == external);
    world->destroyBody(external);
    assert(!world->terrainHeightFieldHandle().valid());
    misses(*world, 100.0f, 200.0f);
}

void validatesBudgetAndNativeReplacementFailure()
{
    physics::PhysicsWorldDiagnostic diagnostic;
    auto uninitialized = physics::createPhysicsWorld(diagnostic);
    assert(uninitialized && !uninitialized->clearTerrainHeightField({1u}));
    assert(uninitialized->lastDiagnostic().message == "PHYSICS-TERRAIN-CLEAR-NOT-INITIALIZED");
    auto surface = translated(5.0f);
    for (const auto resolution : {8u, 3u, std::numeric_limits<std::uint32_t>::max()}) {
        auto world = makeWorld(resolution, 1u);
        physics::TerrainPhysicsBinding binding(*world);
        assert(!binding.bind(surface).success);
        assert(!binding.ready() && !world->terrainHeightFieldHandle().valid());
        assert(world->lastDiagnostic().code
            == physics::PhysicsWorldDiagnostic::Code::TerrainBuildFailed);
    }
    auto world = makeWorld();
    physics::TerrainPhysicsBinding binding(*world);
    assert(binding.bind(surface).success);
    const auto prior = binding.handle();
    physics::RigidBodyDesc obstacle;
    obstacle.motionType = physics::BodyMotionType::Static;
    obstacle.layer = physics::CollisionLayer::Static;
    core::pmr::Vector<physics::PhysicsBodyHandle> obstacles;
    for (unsigned index = 0; index < 7u; ++index) {
        const auto body = world->createBody(obstacle, {-500.0f, 10.0f, float(index) * 2.0f}, {});
        assert(body.valid());
        obstacles.push_back(body);
    }
    // The configured native body capacity is exhausted. No mock or fallback.
    assert(!binding.bind(translated(7.0f)).success);
    assert(binding.handle() == prior && binding.surface() == surface);
    assert(hit(*world, 100.0f, 200.0f, 5.0f).body == prior);
    assert(!world->clearTerrainHeightField(obstacles.front()));
    assert(binding.clear().success);
    assert(hit(*world, -500.0f, 0.0f, 10.5f).body == obstacles.front());
    for (auto body : obstacles)
        world->destroyBody(body);
}

class LatticeSurface final : public terrain::TerrainSurface {
public:
    explicit LatticeSurface(std::uint32_t side, float spacing = 1.0f)
        : side_(side), spacing_(spacing)
    {
        visits_.resize(std::size_t(side) * side);
    }

    terrain::TerrainBounds bounds() const noexcept override
    {
        const float last = static_cast<float>(side_ - 1u) * spacing_;
        return {0, last, 0, last};
    }

    std::uint64_t generation() const noexcept override { return side_; }

    terrain::TerrainSurfaceQuery sample(float x, float z) const override
    {
        assert(bounds().contains(x, z));
        // Each source interval is exactly one unit. A density change or
        // native-padding query would violate this source contract.
        assert(std::floor(x) == x && std::floor(z) == z);
        assert(spacing_ == 1.0f);
        auto& visits = visits_[static_cast<std::size_t>(z) * side_ + static_cast<std::size_t>(x)];
        ++visits;
        assert(visits == 1u);
        ++calls;
        terrain::TerrainSurfaceQuery result;
        result.status = terrain::TerrainSurfaceQueryStatus::Ready;
        result.sample.height = 3.5f;
        return result;
    }

    mutable std::size_t calls = 0u;

private:
    std::uint32_t side_;
    float spacing_;
    mutable core::pmr::Vector<std::uint8_t> visits_{&core::defaultCpuMemoryResource()};
};

void validatesExactNativeLattice()
{
    {
        auto world = makeWorld(2u, 4u * sizeof(float));
        physics::TerrainPhysicsBinding binding(*world);
        auto source =
            core::makeShared<LatticeSurface>(2u, std::numeric_limits<float>::max() / 2.0f);
        assert(!binding.bind(source).success && source->calls == 0u);
        assert(world->lastDiagnostic().message == "PHYSICS-TERRAIN-GRID-UNSUPPORTED");
    }
    for (const auto side : {2u, 3u, 5u, 513u}) {
        auto world = makeWorld(side, std::size_t(side) * side * sizeof(float));
        physics::TerrainPhysicsBinding binding(*world);
        auto source = core::makeShared<LatticeSurface>(side);
        assert(binding.bind(source).success);
        assert(source->calls == std::size_t(side) * side);
        const float last = static_cast<float>(side - 1u), center = last * 0.5f;
        hit(*world, 0.25f, center, 3.5f);
        hit(*world, last - 0.25f, center, 3.5f);
        hit(*world, center, 0.25f, 3.5f);
        hit(*world, center, last - 0.25f, 3.5f);
        misses(*world, -0.25f, center);
        misses(*world, last + 0.25f, center);
        misses(*world, center, -0.25f);
        misses(*world, center, last + 0.25f);
        misses(*world, last + 0.25f, last + 0.25f);
        assert(binding.bind(source).reused && source->calls == std::size_t(side) * side);
    }
}

void sceneSmoke(const std::filesystem::path& package)
{
    constexpr const char* id = "3f6fbcca-17d2-46a6-9817-2af357399301";
    resource::ResourceManager resources;
    assert(resource::InternalAssetPackageReader().load(package, resources).success);
    auto world = makeWorld();
    physics::TerrainPhysicsBinding collider(*world);
    renderer::RuntimeScene runtime;
    auto material = runtime.addMaterial("explicit-cpu-binding-fixture");
    scene::Scene scene("terrain-physics");
    auto& object = scene.createObject("Terrain");
    const auto objectId = object.id();
    auto& component = object.addComponent<scene3d::TerrainComponent>();
    const auto componentId = component.id();
    component.set_resource_id(id);
    object.set_local_transform(scene::Transform::fromTranslation({100, 5, 200}));
    renderer::SceneTerrainPreparation preparation;
    renderer::SceneTerrainPreparation::Desc desc;
    desc.maxObjects = 8u;
    desc.maxTerrains = 1u;
    desc.binding.useAssetTileLayout = true;
    desc.binding.resolveMaterial = resolveMaterial;
    desc.binding.context = &material;
    assert(preparation.synchronize(scene, resources, runtime, desc).success);
    const auto surface = preparation.worldSurface(componentId);
    assert(collider.bind(surface).success);
    const auto bounds = surface->bounds();
    const float x = (bounds.minX + bounds.maxX) * 0.5f, z = (bounds.minZ + bounds.maxZ) * 0.5f;
    assert(hit(*world, x, z, 8.5f).body == collider.handle());
    const auto first = collider.handle();
    assert(resources.unload(id).success);
    assert(!preparation.synchronize(scene, resources, runtime, desc).success);
    assert(preparation.worldSurface(componentId) == surface && collider.bind(surface).reused);
    assert(resource::InternalAssetPackageReader().load(package, resources).success);
    object.set_local_transform(scene::Transform::fromTranslation({100, 7, 200}));
    assert(preparation.synchronize(scene, resources, runtime, desc).success);
    assert(
        collider.bind(preparation.worldSurface(componentId)).success && collider.handle() != first);
    assert(hit(*world, x, z, 10.5f).body == collider.handle());
    component.set_enabled(false);
    assert(preparation.synchronize(scene, resources, runtime, desc).released == 1u);
    assert(!preparation.worldSurface(componentId) && collider.clear().success);
    misses(*world, x, z);
    component.set_enabled(true);
    assert(preparation.synchronize(scene, resources, runtime, desc).success);
    assert(collider.bind(preparation.worldSurface(componentId)).success);
    assert(scene.destroyObject(objectId).ok());
    assert(preparation.synchronize(scene, resources, runtime, desc).released == 1u);
    assert(collider.clear().success);
    misses(*world, x, z);
    preparation.clear(scene, runtime);
    assert(surface->sample(x, z).sample.height == 8.5f);
    std::puts("Baked resource -> standard scene world surface -> native Jolt lifecycle passed");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2)
        sceneSmoke(argv[1]);
    else {
        validatesNativeLifecycle();
        validatesBudgetAndNativeReplacementFailure();
        validatesExactNativeLattice();
        std::puts("Native terrain physics binding, lease, generation, budget and rollback passed");
    }
}
