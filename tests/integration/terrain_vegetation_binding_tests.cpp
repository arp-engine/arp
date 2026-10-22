#include "3d/terrain/integration/terrain_scene_session.h"
#include "renderer/terrain_vegetation_draw_preparation.h"
#include "resource/internal_asset_package.h"
#include "rhi/backends/null/null_backend.h"
#include "tests/fixtures/capability_override_device.h"
#include "tests/fixtures/terrain_domain_asset.h"
#include "tests/fixtures/terrain_physics_world.h"
#include "tests/fixtures/terrain_vegetation_delayed_queue.h"
#include "tests/fixtures/terrain_vegetation_draw.h"

#include <cassert>
#include <cstdio>
#include <limits>

namespace {
using namespace arp;
using namespace arp::tests::terrain_physics;
constexpr const char* terrainId = "3f6fbcca-17d2-46a6-9817-2af357399301";
constexpr const char* catalogId = "3ea4c197-d012-4e64-8f5f-6a415ec2c109";

void checkDrawSnapshot(const renderer::TerrainVegetationDrawSnapshot& prepared,
    const std::shared_ptr<const renderer::TerrainVegetationSnapshot>& source)
{
    static_assert(sizeof(vegetation::InstanceDataGpu) == 64u);
    static_assert(renderer::TerrainVegetationDrawSnapshot::positionSpace
        == renderer::TerrainVegetationDrawSnapshot::PositionSpace::World);
    assert(prepared.source == source && prepared.tiles.size() == source->tiles.size());
    std::size_t count = 0u;
    for (std::size_t tileIndex = 0; tileIndex < prepared.tiles.size(); ++tileIndex) {
        const auto& output = prepared.tiles[tileIndex];
        const auto& input = source->tiles[tileIndex];
        assert(output.coord == input.coord);
        std::uint32_t previousPrototype = 0u;
        for (const auto& prototype : output.prototypes) {
            assert(prototype.prototypeId > previousPrototype && !prototype.instances.empty());
            previousPrototype = prototype.prototypeId;
            std::size_t index = 0u;
            for (const auto& instance : input.instances) {
                if (instance.prototypeId != prototype.prototypeId)
                    continue;
                assert(index < prototype.instances.size());
                const auto& gpu = prototype.instances[index];
                // The source is world space: never add translation or tile origin again.
                assert(gpu.positionScale[0] == instance.position.x);
                assert(gpu.positionScale[1] == instance.position.y);
                assert(gpu.positionScale[2] == instance.position.z);
                assert(gpu.positionScale[3] == instance.scale.x);
                assert(gpu.extraTransform[2]
                    == (instance.scale.y == instance.scale.x ? 0.0f : instance.scale.y));
                assert(gpu.extraTransform[3] == 0.0f);
                assert(gpu.yawPhaseIdLod[1]
                    == static_cast<float>(instance.variationSeed) / 4294967296.0f);
                assert(gpu.yawPhaseIdLod[2] == static_cast<float>(index));
                const auto reconstructed = core::quatMul(
                    core::quatMul(core::quatFromAxisAngle(0, 1, 0, gpu.yawPhaseIdLod[0]),
                        core::quatFromAxisAngle(1, 0, 0, gpu.extraTransform[0])),
                    core::quatFromAxisAngle(0, 0, 1, gpu.extraTransform[1]));
                const core::Quat rotation{instance.rotation[0],
                    instance.rotation[1],
                    instance.rotation[2],
                    instance.rotation[3]};
                for (const auto basis :
                    {core::Vec3{1, 0, 0}, core::Vec3{0, 1, 0}, core::Vec3{0, 0, 1}}) {
                    const auto expected = core::rotate(rotation, basis);
                    const auto actual = core::rotate(reconstructed, basis);
                    assert(std::abs(expected.x - actual.x) < 1e-5f);
                    assert(std::abs(expected.y - actual.y) < 1e-5f);
                    assert(std::abs(expected.z - actual.z) < 1e-5f);
                }
                ++index;
                ++count;
            }
            assert(index == prototype.instances.size());
        }
    }
    assert(count == source->instanceCount);
}

void validatesDrawPreparation()
{
    renderer::TerrainVegetationDrawPreparation preparation;
    const renderer::TerrainVegetationDrawPreparation::Desc budget{3u, 4u};
    assert(!preparation.snapshot());
    assert(preparation.prepare(nullptr, budget).diagnostics == "VEGETATION-DRAW-SOURCE-INVALID");
    const auto source = arp::tests::terrainVegetationDrawSourceFixture();
    assert(preparation.prepare(source, budget).success);
    const auto first = preparation.snapshot();
    checkDrawSnapshot(*first, source);
    assert(first->tiles[1].prototypes.empty());
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 8u; ++i)
        assert(preparation.prepare(source, budget).unchanged);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes && before.allocationCount == after.allocationCount);
    assert(preparation.snapshot() == first);
    for (const auto smaller : {renderer::TerrainVegetationDrawPreparation::Desc{2u, 4u},
             renderer::TerrainVegetationDrawPreparation::Desc{3u, 3u},
             renderer::TerrainVegetationDrawPreparation::Desc{0u, 4u},
             renderer::TerrainVegetationDrawPreparation::Desc{3u, 0u}}) {
        assert(preparation.prepare(source, smaller).diagnostics == "VEGETATION-DRAW-BUDGET");
        assert(preparation.snapshot() == first);
    }
    const auto rejects = [&](const auto& mutate, const char* diagnostic) {
        auto bad = core::makeShared<renderer::TerrainVegetationSnapshot>(*source);
        mutate(*bad);
        const auto result = preparation.prepare(bad, budget);
        assert(!result.success && result.diagnostics.find(diagnostic) == 0u);
        assert(preparation.snapshot() == first);
    };
    rejects(
        [](auto& s) {
            s.terrainResourceId = "missing-id";
        },
        "VEGETATION-DRAW-SOURCE-INVALID");
    rejects(
        [](auto& s) {
            s.catalogResourceId.clear();
        },
        "VEGETATION-DRAW-SOURCE-INVALID");
    rejects(
        [](auto& s) {
            s.terrainResourceGeneration = 0u;
        },
        "VEGETATION-DRAW-SOURCE-INVALID");
    rejects(
        [](auto& s) {
            s.catalogResourceGeneration = 0u;
        },
        "VEGETATION-DRAW-SOURCE-INVALID");
    rejects(
        [](auto& s) {
            s.terrainGeneration = 0u;
        },
        "VEGETATION-DRAW-SOURCE-INVALID");
    rejects(
        [](auto& s) {
            s.catalog.reset();
        },
        "VEGETATION-DRAW-SOURCE-INVALID");
    rejects(
        [](auto& s) {
            s.translation.x = std::numeric_limits<float>::infinity();
        },
        "VEGETATION-DRAW-SOURCE-INVALID");
    rejects(
        [](auto& s) {
            s.instanceCount = 3u;
        },
        "VEGETATION-DRAW-COUNT-MISMATCH");
    rejects(
        [](auto& s) {
            s.tiles[1].coord = s.tiles[0].coord;
        },
        "VEGETATION-DRAW-TILE-ORDER-INVALID");
    rejects(
        [](auto& s) {
            s.tiles[0].coord.x = -1;
        },
        "VEGETATION-DRAW-TILE-ORDER-INVALID");
    rejects(
        [](auto& s) {
            std::swap(s.tiles[1], s.tiles[2]);
        },
        "VEGETATION-DRAW-TILE-ORDER-INVALID");
    rejects(
        [](auto& s) {
            s.tiles[0].instances[0].prototypeId = 65536u;
        },
        "VEGETATION-DRAW-PROTOTYPE-UNAVAILABLE");
    rejects(
        [](auto& s) {
            s.tiles[0].instances[0].prototypeId = 3u;
        },
        "VEGETATION-DRAW-PROTOTYPE-UNAVAILABLE");
    rejects(
        [](auto& s) {
            s.tiles[0].instances[0].position.x = std::numeric_limits<float>::quiet_NaN();
        },
        "VEGETATION-DRAW-TRANSFORM-INVALID");
    rejects(
        [](auto& s) {
            s.tiles[0].instances[0].scale.y = 0.0f;
        },
        "VEGETATION-DRAW-TRANSFORM-INVALID");
    rejects(
        [](auto& s) {
            s.tiles[0].instances[0].scale.x = std::numeric_limits<float>::infinity();
        },
        "VEGETATION-DRAW-TRANSFORM-INVALID");
    // Fail late, after earlier tiles were lowered: nothing can be published partially.
    rejects(
        [](auto& s) {
            s.tiles.back().instances[0].scale.z = 4.0f;
        },
        "VEGETATION-DRAW-ABI-UNREPRESENTABLE");
    rejects(
        [](auto& s) {
            s.tiles.back().instances[0].rotation = {};
        },
        "VEGETATION-DRAW-TRANSFORM-INVALID");
    rejects(
        [](auto& s) {
            s.tiles.back().instances[0].rotation = {0, 0, 0, 2};
        },
        "VEGETATION-DRAW-TRANSFORM-INVALID");
    assert(preparation.prepare(source, budget).unchanged);
    // Every distinct immutable source must be rebuilt, including matching generations.
    auto replacement = core::makeShared<renderer::TerrainVegetationSnapshot>(*source);
    assert(preparation.prepare(replacement, budget).success);
    assert(preparation.snapshot() != first && preparation.snapshot()->source == replacement);
    replacement = core::makeShared<renderer::TerrainVegetationSnapshot>(*source);
    replacement->terrainResourceGeneration += 1u;
    replacement->catalogResourceGeneration += 1u;
    replacement->terrainGeneration += 1u;
    replacement->translation.y += 7.0f;
    for (auto& tile : replacement->tiles)
        for (auto& instance : tile.instances)
            instance.position.y += 7.0f;
    assert(preparation.prepare(replacement, budget).success);
    checkDrawSnapshot(*preparation.snapshot(), replacement);
    assert(
        first->source->terrainResourceGeneration == 11u && first->source->terrainGeneration == 23u);
    auto empty = core::makeShared<renderer::TerrainVegetationSnapshot>(*source);
    for (auto& tile : empty->tiles)
        tile.instances.clear();
    empty->instanceCount = 0u;
    assert(preparation.prepare(empty, budget).success);
    for (const auto& tile : preparation.snapshot()->tiles)
        assert(tile.prototypes.empty());
    preparation.clear();
    preparation.clear();
    assert(!preparation.snapshot());
    checkDrawSnapshot(*first, source);
    std::weak_ptr<const renderer::TerrainVegetationSnapshot> sourceLease;
    std::weak_ptr<const resource::VegetationCatalogResourceData> catalogLease;
    std::shared_ptr<const renderer::TerrainVegetationDrawSnapshot> reader;
    {
        const auto temporary = arp::tests::terrainVegetationDrawSourceFixture();
        sourceLease = temporary;
        catalogLease = temporary->catalog;
        assert(preparation.prepare(temporary, budget).success);
        reader = preparation.snapshot();
        preparation.clear();
    }
    assert(!sourceLease.expired() && !catalogLease.expired());
    reader.reset();
    assert(sourceLease.expired() && catalogLease.expired());
}

void validatesConfiguration()
{
    resource::ResourceManager resources;
    renderer::TerrainVegetationBinding binding;
    assert(!binding.snapshot());
    assert(binding.bind(resources, terrainId, 1u, {}, {1u, 1u}).diagnostics
        == "VEGETATION-TERRAIN-RESOURCE-NOT-READY");
    assert(
        binding
            .bind(
                resources, terrainId, 1u, {0, std::numeric_limits<float>::infinity(), 0}, {1u, 1u})
            .diagnostics
        == "VEGETATION-TERRAIN-TRANSLATION-INVALID");
    binding.clear();
    binding.clear();
    assert(!binding.snapshot());
    validatesDrawPreparation();
}

void validatesGpuSession(const resource::ResourceManager& resources,
    const renderer::TerrainVegetationBinding::Desc& budget)
{
    auto device = rhi::createNullDevice();
    tests::DelayedValidatorQueue queue;
    tests::CapabilityOverrideDevice proxy(*device, nullptr);
    proxy.overrideQueue(&queue);
    renderer::TerrainVegetationGpuUpload::Desc upload;
    upload.maxTiles = budget.maxTiles;
    upload.maxInstances = upload.maxPrototypes = budget.maxInstances;
    upload.maxCopyBytes = 64u;
    upload.maxCopyCommands = budget.maxInstances;
    upload.maxCpuBytes = 64u * 1024u * 1024u;
    upload.liveGenerations = core::makeShared<core::AtomicCapacityBudget>(2u);
    upload.liveInstances = core::makeShared<core::AtomicCapacityBudget>(budget.maxInstances * 2u);
    upload.validatorOptIn = true;
    renderer::RuntimeScene runtime;
    auto material = runtime.addMaterial("explicit-gpu-publication-fixture");
    auto world = makeWorld();
    scene::Scene scene("gpu-session");
    scene::Scene next("next-gpu-session");
    auto& object = scene.createObject("terrain");
    auto& terrain = object.addComponent<scene3d::TerrainComponent>();
    terrain.set_resource_id(terrainId);
    const core::Vec3 translation{100, 5, 200};
    object.set_local_transform(scene::Transform::fromTranslation(translation));
    engine::TerrainSceneSession::Desc desc;
    desc.maxObjects = 8u;
    desc.maxTerrains = 1u;
    desc.binding.useAssetTileLayout = true;
    desc.binding.resolveMaterial = resolveMaterial;
    desc.binding.context = &material;
    desc.vegetation = budget;
    engine::TerrainSceneSession session(resources, runtime, *world, proxy, upload);
    auto query = desc;
    query.binding.mode = renderer::TerrainSceneBinding::PreparationMode::QueryOnly;
    assert(session.synchronize(scene, query).failureStage
        == engine::TerrainSceneSession::FailureStage::Configuration);
    auto pending = session.hostStep(scene, 1.0f / 60.0f, desc);
    assert(!pending.success && pending.pending && !pending.stepped
        && pending.failureStage == engine::TerrainSceneSession::FailureStage::VegetationGpuBinding);
    assert(!terrain.desc().terrain.valid() && !world->terrainHeightFieldHandle().valid());
    assert(!session.worldSurface() && !session.vegetationSnapshot()
        && !session.vegetationGpuSnapshot());
    const auto attempts = proxy.bufferCreateAttempts();
    assert(session.synchronize(scene, desc).pending && proxy.bufferCreateAttempts() == attempts);
    queue.complete();
    assert(session.hostStep(scene, 1.0f / 60.0f, desc).stepped);
    auto firstGpu = session.vegetationGpuSnapshot();
    const auto firstCpu = session.vegetationSnapshot();
    assert(firstGpu && firstGpu->validatorOnly() && firstGpu->source()->source == firstCpu);
    const renderer::TerrainHandle handle{terrain.desc().terrain.id};
    const auto oldRuntime = *runtime.terrain(handle);
    const auto lease = world->terrainHeightFieldHandle();
    core::pmr::Vector<physics::PhysicsBodyHandle> blockers{&core::defaultCpuMemoryResource()};
    physics::RigidBodyDesc blocker;
    blocker.motionType = physics::BodyMotionType::Static;
    blocker.layer = physics::CollisionLayer::Static;
    for (unsigned i = 0; i < 7u; ++i) {
        const auto body = world->createBody(blocker, {-500, 10, float(i) * 2}, {});
        assert(body.valid());
        blockers.push_back(body);
    }
    object.set_local_transform(scene::Transform::fromTranslation({100, 9, 200}));
    assert(session.synchronize(scene, desc).pending);
    assert(*runtime.terrain(handle) == oldRuntime && world->terrainHeightFieldHandle() == lease);
    queue.complete();
    const auto rejected = session.synchronize(scene, desc);
    assert(!rejected.success && !rejected.pending
        && rejected.failureStage == engine::TerrainSceneSession::FailureStage::PhysicsBinding);
    assert(!session.vegetationGpuSnapshot() && *runtime.terrain(handle) == oldRuntime
        && world->terrainHeightFieldHandle() == lease);
    object.set_local_transform(scene::Transform::fromTranslation(translation));
    assert(session.synchronize(scene, desc).success && session.vegetationGpuSnapshot() == firstGpu
        && session.vegetationSnapshot() == firstCpu);
    for (const auto body : blockers)
        world->destroyBody(body);
    const auto beforeRetry = proxy.bufferCreateAttempts();
    object.set_local_transform(scene::Transform::fromTranslation({100, 9, 200}));
    assert(session.synchronize(scene, desc).success && proxy.bufferCreateAttempts() == beforeRetry);
    auto secondGpu = session.vegetationGpuSnapshot();
    assert(secondGpu != firstGpu && secondGpu->source()->source == session.vegetationSnapshot());
    object.set_local_transform(scene::Transform::fromTranslation({100, 11, 200}));
    const auto exhausted = session.synchronize(scene, desc);
    assert(!exhausted.success && !exhausted.pending
        && exhausted.diagnostics == "VEGETATION-GPU-LIVE-GENERATION-BUDGET");
    firstGpu.reset();
    assert(session.synchronize(scene, desc).pending);
    terrain.set_enabled(false);
    assert(session.synchronize(scene, desc).success && !session.vegetationGpuSnapshot());
    assert(!world->terrainHeightFieldHandle().valid() && queue.pending);
    assert(session.synchronize(next, desc).success && queue.pending);
    assert(!session.drainAndClear({}).success);
    queue.complete();
    assert(session.drainAndClear({}).success && !session.synchronized());
    assert(upload.liveGenerations->used() == 1u);
    secondGpu.reset();
    assert(!upload.liveGenerations->used() && !upload.liveInstances->used());
    std::puts(
        "Cooked package -> CPU/GPU session publication -> Jolt rollback PASS (Null validator only)");
}

void validatesPackage(const std::filesystem::path& package)
{
    resource::ResourceManager resources;
    assert(resource::InternalAssetPackageReader().load(package, resources).success);
    renderer::TerrainVegetationBinding binding;
    const auto asset = resources.find<resource::TerrainAssetResource>(terrainId);
    assert(asset.ready() && asset.get()->hasVegetation());
    const auto catalogDesc =
        resources.find<resource::VegetationCatalogResource>(catalogId).get()->loadDesc;
    const auto nx = asset.get()->tileCountX(), nz = asset.get()->tileCountZ();
    renderer::TerrainVegetationBinding::Desc budget{static_cast<std::size_t>(nx) * nz, 0u};
    for (std::uint32_t z = 0; z < nz; ++z)
        for (std::uint32_t x = 0; x < nx; ++x)
            budget.maxInstances += asset.get()->vegetationFor({int(x), int(z)})->size();
    assert(budget.maxInstances > 0u);
    validatesGpuSession(resources, budget);
    const core::Vec3 translation{100, 5, 200};
    const auto first = binding.bind(resources, terrainId, asset.generation(), translation, budget);
    assert(first.success && !first.unchanged);
    const auto retained = binding.snapshot();
    assert(retained && retained->instanceCount == budget.maxInstances);
    assert(retained->tiles.size() == budget.maxTiles && retained->catalog);
    renderer::TerrainVegetationDrawPreparation drawPreparation;
    const renderer::TerrainVegetationDrawPreparation::Desc drawBudget{
        budget.maxTiles, budget.maxInstances};
    assert(drawPreparation.prepare(retained, drawBudget).success);
    const auto initialDrawSnapshot = drawPreparation.snapshot();
    checkDrawSnapshot(*initialDrawSnapshot, retained);
    for (const auto& tile : retained->tiles) {
        const auto& local = *asset.get()->vegetationFor(tile.coord);
        assert(local.size() == tile.instances.size());
        for (std::size_t i = 0; i < local.size(); ++i) {
            const auto& world = tile.instances[i];
            assert(world.position.x == local[i].position.x + translation.x);
            assert(world.position.y == local[i].position.y + translation.y);
            assert(world.position.z == local[i].position.z + translation.z);
            assert(world.rotation == local[i].rotation && world.scale.x == local[i].scale.x
                && world.scale.y == local[i].scale.y && world.scale.z == local[i].scale.z);
            assert(world.prototypeId == local[i].prototypeId
                && world.variationSeed == local[i].variationSeed);
        }
    }
    const auto liveBytes = core::defaultCpuAllocator().stats().liveBytes;
    for (unsigned i = 0; i < 8u; ++i)
        assert(
            binding.bind(resources, terrainId, asset.generation(), translation, budget).unchanged);
    assert(core::defaultCpuAllocator().stats().liveBytes == liveBytes);
    assert(binding.snapshot() == retained);
    auto small = budget;
    small.maxInstances = budget.maxInstances - 1u;
    assert(!binding.bind(resources, terrainId, asset.generation(), translation, small).success);
    small = budget;
    small.maxTiles = budget.maxTiles - 1u;
    assert(!binding.bind(resources, terrainId, asset.generation(), translation, small).success);
    assert(
        !binding.bind(resources, terrainId, asset.generation() + 1u, translation, budget).success);
    assert(binding.snapshot() == retained);

    assert(resources.unload(catalogId).success);
    assert(!binding.bind(resources, terrainId, asset.generation(), translation, budget).success);
    assert(binding.snapshot() == retained && retained->catalog->findTemplate("grass-a"));
    {
        // An imported scene explicitly owns vegetation elsewhere. Query-only
        // consumes terrain even with no catalog or vegetation snapshot budget.
        renderer::RuntimeScene queryRuntime;
        auto queryWorld = makeWorld();
        scene::Scene queryScene("query-only-with-independent-vegetation");
        auto& component =
            queryScene.createObject("Terrain").addComponent<scene3d::TerrainComponent>();
        component.set_resource_id(terrainId);
        engine::TerrainSceneSession::Desc queryDesc;
        queryDesc.maxObjects = 8u;
        queryDesc.maxTerrains = 1u;
        queryDesc.binding.mode = renderer::TerrainSceneBinding::PreparationMode::QueryOnly;
        engine::TerrainSceneSession query(resources, queryRuntime, *queryWorld);
        assert(query.synchronize(queryScene, queryDesc).success);
        assert(query.worldSurface() && !query.vegetationSnapshot());
        assert(!component.desc().terrain.valid() && !component.desc().material.valid());
        assert(query.clear().success && !queryWorld->terrainHeightFieldHandle().valid());
    }
    assert(resource::loadVegetationCatalogResource(resources, catalogDesc).success);
    const auto refreshed = resources.find<resource::TerrainAssetResource>(terrainId);
    assert(binding.bind(resources, terrainId, refreshed.generation(), translation, budget).success);
    assert(binding.snapshot() != retained);
    assert(binding.snapshot()->catalogResourceGeneration != retained->catalogResourceGeneration);
    const auto reloadedCatalog = binding.snapshot();
    assert(drawPreparation.prepare(reloadedCatalog, drawBudget).success);
    assert(drawPreparation.snapshot() != initialDrawSnapshot);
    checkDrawSnapshot(*drawPreparation.snapshot(), reloadedCatalog);
    auto terrainCopy = core::makeUnique<resource::TerrainAssetResource>(*refreshed.get());
    assert(resources.unload(terrainId).success);
    assert(
        !binding.bind(resources, terrainId, refreshed.generation(), translation, budget).success);
    assert(binding.snapshot() == reloadedCatalog);
    assert(resources.loadResource(std::move(terrainCopy)).success);
    assert(
        binding.bind(resources, terrainId, resources.generationOf(terrainId), {100, 7, 200}, budget)
            .success);
    assert(binding.snapshot() != reloadedCatalog);
    assert(drawPreparation.prepare(binding.snapshot(), drawBudget).success);
    checkDrawSnapshot(*drawPreparation.snapshot(), binding.snapshot());
    const auto beforeMissingPrototype = binding.snapshot();
    assert(resources.unload(catalogId).success);
    assert(resources.loadResource(core::makeUnique<tests::EmptyTerrainCatalogFixture>()).success);
    const auto missingPrototype =
        binding.bind(resources, terrainId, resources.generationOf(terrainId), translation, budget);
    assert(!missingPrototype.success
        && missingPrototype.diagnostics == "VEGETATION-TERRAIN-PROTOTYPE-UNAVAILABLE");
    assert(binding.snapshot() == beforeMissingPrototype);
    assert(resources.unload(catalogId).success);
    assert(resource::loadVegetationCatalogResource(resources, catalogDesc).success);
    binding.clear();
    assert(!binding.snapshot() && retained->tiles.size() == budget.maxTiles);
    // Draw readers pin the exact catalog/source epoch independently of manager unload.
    checkDrawSnapshot(*initialDrawSnapshot, retained);
    drawPreparation.clear();
    assert(!drawPreparation.snapshot());

    renderer::RuntimeScene runtime;
    auto material = runtime.addMaterial("explicit-cpu-vegetation-fixture");
    auto world = makeWorld();
    scene::Scene scene("vegetation-first", scene::SceneId::fromValue(33u));
    scene::Scene next("vegetation-next-same-id", scene::SceneId::fromValue(33u));
    auto& parent = scene.createObject("parent");
    auto& object = scene.createObject("terrain");
    assert(scene.reparent(object.id(), parent.id()).ok());
    auto& terrain = object.addComponent<scene3d::TerrainComponent>();
    terrain.set_resource_id(terrainId);
    object.set_local_transform(scene::Transform::fromTranslation(translation));
    engine::TerrainSceneSession::Desc desc;
    desc.maxObjects = 8u;
    desc.maxTerrains = 1u;
    desc.binding.useAssetTileLayout = true;
    desc.binding.resolveMaterial = resolveMaterial;
    desc.binding.context = &material;
    desc.vegetation = budget;
    engine::TerrainSceneSession session(resources, runtime, *world);
    assert(session.synchronize(scene, desc).success);
    const auto active = session.vegetationSnapshot();
    assert(drawPreparation.prepare(active, drawBudget).success);
    checkDrawSnapshot(*drawPreparation.snapshot(), active);
    assert(active
        && active->tiles.front().instances.front().position.y
            == retained->tiles.front().instances.front().position.y);
    const auto lease = world->terrainHeightFieldHandle();
    const renderer::TerrainHandle initialHandle{terrain.desc().terrain.id};
    const auto initialRuntime = *runtime.terrain(initialHandle);
    const scene::SceneUpdateOptions step{1.0f / 60.0f, 0u, 8u, 32u, 1.0f / 60.0f};
    auto rejected = desc;
    rejected.vegetation.maxInstances = 0u;
    object.set_local_transform(scene::Transform::fromTranslation({100, 9, 200}));
    const auto blocked = session.fixedStep(scene, step, rejected);
    assert(!blocked.success && !blocked.fixedUpdateCompleted && !blocked.stepped);
    assert(blocked.failureStage == engine::TerrainSceneSession::FailureStage::VegetationBinding);
    assert(!session.synchronized() && !session.vegetationSnapshot() && !session.worldSurface());
    assert(world->terrainHeightFieldHandle() == lease);
    assert(*runtime.terrain(initialHandle) == initialRuntime);
    object.set_local_transform(scene::Transform::fromTranslation(translation));
    assert(session.fixedStep(scene, step, desc).stepped && session.vegetationSnapshot() == active);
    terrain.set_resource_id("3f6fbcca-17d2-46a6-9817-2af357399302");
    assert(!session.fixedStep(scene, step, desc).success && !session.vegetationSnapshot());
    terrain.set_resource_id(terrainId);
    assert(session.synchronize(scene, desc).success && session.vegetationSnapshot() == active);
    assert(resources.unload(catalogId).success);
    const auto missingCatalog = session.fixedStep(scene, step, desc);
    assert(!missingCatalog.success && !missingCatalog.fixedUpdateCompleted
        && !missingCatalog.stepped
        && missingCatalog.failureStage
            == engine::TerrainSceneSession::FailureStage::VegetationBinding);
    assert(!session.vegetationSnapshot() && world->terrainHeightFieldHandle() == lease);
    assert(resource::loadVegetationCatalogResource(resources, catalogDesc).success);
    assert(session.synchronize(scene, desc).success && session.vegetationSnapshot() != active);
    const auto priorVegetation = session.vegetationSnapshot();
    const renderer::TerrainHandle runtimeHandle{terrain.desc().terrain.id};
    const auto priorRuntime = *runtime.terrain(runtimeHandle);
    const auto priorLease = world->terrainHeightFieldHandle();
    core::pmr::Vector<physics::PhysicsBodyHandle> blockers{&core::defaultCpuMemoryResource()};
    physics::RigidBodyDesc blocker;
    blocker.motionType = physics::BodyMotionType::Static;
    blocker.layer = physics::CollisionLayer::Static;
    // The shared fixture declares exactly 8 native body slots, one owns terrain.
    for (unsigned i = 0; i < 7u; ++i) {
        const auto handle = world->createBody(blocker, {-500, 10, float(i) * 2}, {});
        assert(handle.valid());
        blockers.push_back(handle);
    }
    object.set_local_transform(scene::Transform::fromTranslation({100, 9, 200}));
    const auto nativeFailure = session.synchronize(scene, desc);
    assert(!nativeFailure.success
        && nativeFailure.failureStage == engine::TerrainSceneSession::FailureStage::PhysicsBinding);
    assert(!session.vegetationSnapshot() && !session.worldSurface());
    assert(*runtime.terrain(runtimeHandle) == priorRuntime);
    assert(world->terrainHeightFieldHandle() == priorLease);
    // Restore authored transform while native capacity remains exhausted. Reuse
    // succeeds only if all consumers retained the old generation.
    object.set_local_transform(scene::Transform::fromTranslation({100, 5, 200}));
    assert(session.synchronize(scene, desc).success);
    assert(session.vegetationSnapshot() == priorVegetation);
    assert(*runtime.terrain(runtimeHandle) == priorRuntime);
    for (const auto handle : blockers)
        world->destroyBody(handle);
    object.set_local_transform(scene::Transform::fromTranslation({100, 7, 200}));
    assert(session.synchronize(scene, desc).success && session.vegetationSnapshot() != active);
    assert(session.vegetationSnapshot()->tiles.front().instances.front().position.y
        == active->tiles.front().instances.front().position.y + 2.0f);
    terrain.set_enabled(false);
    assert(session.synchronize(scene, desc).success && !session.vegetationSnapshot());
    terrain.set_enabled(true);
    assert(session.synchronize(scene, desc).success && session.vegetationSnapshot());
    parent.set_active(false);
    assert(session.synchronize(scene, desc).success && !session.vegetationSnapshot());
    parent.set_active(true);
    assert(session.synchronize(scene, desc).success);
    const auto componentId = terrain.id();
    assert(object.removeComponent(componentId));
    assert(session.synchronize(scene, desc).success && !session.vegetationSnapshot());
    object.addComponent<scene3d::TerrainComponent>().set_resource_id(terrainId);
    assert(session.synchronize(scene, desc).success && session.vegetationSnapshot());
    assert(session.synchronize(next, desc).success && !session.vegetationSnapshot());
    assert(!object.getComponent<scene3d::TerrainComponent>()->desc().terrain.valid());
    assert(session.synchronize(scene, desc).success && session.vegetationSnapshot());
    assert(scene.destroyObject(object.id()).ok());
    assert(session.synchronize(scene, desc).success && !session.vegetationSnapshot());
    assert(session.clear().success && session.clear().success);
    auto& nextTerrain =
        next.createObject("scoped-terrain").addComponent<scene3d::TerrainComponent>();
    nextTerrain.set_resource_id(terrainId);
    std::weak_ptr<const renderer::TerrainVegetationSnapshot> scopedSnapshot;
    {
        engine::TerrainSceneSession scoped(resources, runtime, *world);
        assert(scoped.synchronize(next, desc).success);
        scopedSnapshot = scoped.vegetationSnapshot();
        assert(!scopedSnapshot.expired());
    }
    assert(scopedSnapshot.expired() && !nextTerrain.desc().terrain.valid()
        && !world->terrainHeightFieldHandle().valid());
    // Geometry-only replacement releases placements without requiring vegetation budgets.
    resource::TerrainAssetBuildDesc geometry;
    geometry.resourceId = terrainId;
    geometry.version = resource::TerrainAssetVersion::V2;
    geometry.hasSurfaceWeights = geometry.hasVegetation = false;
    geometry.vegetationCatalogResourceId.clear();
    geometry.bounds = {0, 2, 0, 2};
    geometry.tileCountX = geometry.tileCountZ = 1u;
    geometry.samplesPerEdge = 2u;
    geometry.tileWorldSize = 2.0f;
    resource::TerrainAssetBuildTile geometryTile;
    geometryTile.heights = {0, 0, 0, 0};
    geometry.tiles.push_back(std::move(geometryTile));
    const auto geometryRoot = package.parent_path() / "geometry-only";
    const auto geometryWritten = resource::TerrainAssetWriter().write(geometry, geometryRoot);
    if (!geometryWritten.success)
        std::fprintf(stderr, "%s\n", geometryWritten.diagnostics.c_str());
    assert(geometryWritten.success);
    assert(session.synchronize(next, desc).success && session.vegetationSnapshot());
    assert(resources.unload(terrainId).success);
    assert(resource::loadTerrainAssetResource(resources,
        {terrainId, "TerrainAssetResource", {}, "Contract Complete", {}},
        geometryRoot / "terrain.asset.yaml")
            .success);
    auto geometryDesc = desc;
    geometryDesc.vegetation = {};
    assert(session.synchronize(next, geometryDesc).success && !session.vegetationSnapshot());
    assert(session.clear().success);
    assert(active->catalog && active->instanceCount == budget.maxInstances);
    std::puts("Cooked package -> CPU vegetation snapshot/session lifecycle -> Jolt step passed");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2)
        validatesPackage(argv[1]);
    else
        validatesConfiguration();
}
