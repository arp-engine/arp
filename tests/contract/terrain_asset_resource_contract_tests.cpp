#include "pcg/terrain_scatter.h"
#include "resource/terrain_asset_resource.h"
#include "resource/vegetation_catalog_resource.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

[[noreturn]] void contractAssertionFailed(const char* expression, int line)
{
    std::cerr << "TerrainAsset contract assertion failed at line " << line << ": " << expression
              << '\n';
    std::abort();
}

// Contract tests run in both Debug and Release. Keep all setup expressions and
// assertions active when NDEBUG is set so Release exercises the same contract.
#undef assert
#define assert(expression)                                                                         \
    do {                                                                                           \
        if (!(expression))                                                                         \
            contractAssertionFailed(#expression, __LINE__);                                        \
    } while (false)

constexpr const char* kTerrainId = "3f6fbcca-17d2-46a6-9817-2af357399301";
constexpr const char* kVegetationCatalogId = "3ea4c197-d012-4e64-8f5f-6a415ec2c109";
constexpr const char* kMaterialId = "33c5dcdf-4f06-482a-94eb-26baf7c937fb";

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

arp::resource::TerrainAssetBuildDesc makeAsset()
{
    arp::resource::TerrainAssetBuildDesc desc;
    desc.resourceId = text(kTerrainId);
    desc.alias = text("terrain/contract-meadow");
    desc.bounds = {0.0f, 2.0f, 0.0f, 2.0f};
    desc.tileCountX = 1u;
    desc.tileCountZ = 1u;
    desc.samplesPerEdge = 2u;
    desc.tileWorldSize = 2.0f;
    desc.vegetationCatalogResourceId = text(kVegetationCatalogId);
    constexpr const char* ids[] = {
        "grassMoss",
        "forestFloor",
        "wetMud",
        "rockyPath",
        "mossyRock",
        "dirtySnow",
        "snowOverlay",
        "wetnessOverlay",
    };
    for (std::uint32_t index = 0u; index < desc.layers.size(); ++index) {
        desc.layers[index].layerId = text(ids[index]);
        desc.layers[index].semantic = text(ids[index]);
        desc.layers[index].materialResourceId = text(kMaterialId);
        desc.layers[index].page = index < 4u ? 0u : 1u;
        desc.layers[index].channel = index < 4u ? index : index - 4u;
    }
    arp::resource::TerrainAssetBuildTile tile;
    tile.coord = {0, 0};
    tile.heights = {1.0f, 3.0f, 5.0f, 7.0f};
    tile.weightPageA = {
        255u,
        0u,
        0u,
        0u,
        255u,
        0u,
        0u,
        0u,
        255u,
        0u,
        0u,
        0u,
        255u,
        0u,
        0u,
        0u,
    };
    tile.weightPageB = {
        0u,
        0u,
        32u,
        64u,
        0u,
        0u,
        32u,
        64u,
        0u,
        0u,
        32u,
        64u,
        0u,
        0u,
        32u,
        64u,
    };
    arp::resource::TerrainStaticVegetationInstance instance;
    instance.prototypeId = 9u;
    instance.position = {1.0f, 4.0f, 1.0f};
    instance.variationSeed = 42u;
    tile.vegetation.push_back(instance);
    desc.tiles.push_back(std::move(tile));
    return desc;
}

arp::resource::ResourceReference makeReference()
{
    arp::resource::ResourceReference reference;
    reference.id = text(kTerrainId);
    reference.type = text("TerrainAssetResource");
    reference.alias = text("terrain/contract-meadow");
    reference.status = text("Contract Complete");
    return reference;
}

class TestVegetationCatalogResource final : public arp::resource::VegetationCatalogResource {
public:
    TestVegetationCatalogResource() : arp::resource::VegetationCatalogResource(makeDesc()) {}

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& manager,
        const arp::resource::ResourceLoadContext& context) override
    {
        data = arp::core::makeShared<arp::resource::VegetationCatalogResourceData>();
        arp::resource::VegetationCatalogAssetEntry asset;
        asset.id = text("contract-plant");
        asset.templateId = 9u;
        data->assets.push_back(std::move(asset));
        return arp::resource::ResourceBase::load(manager, context);
    }

private:
    static arp::resource::VegetationCatalogResourceLoadDesc makeDesc()
    {
        arp::resource::VegetationCatalogResourceLoadDesc desc;
        desc.resourceId = text(kVegetationCatalogId);
        desc.alias = text("vegetation/contract-catalog");
        desc.status = text("Contract Complete");
        return desc;
    }
};

void loadCatalog(arp::resource::ResourceManager& manager)
{
    assert(
        manager.load<TestVegetationCatalogResource>(arp::resource::ResourceLoadContext{}).success);
}

void assertRoundTrip(const std::filesystem::path& root)
{
    const auto written = arp::resource::writeTerrainAsset(makeAsset(), root);
    assert(written.success);

    arp::resource::ResourceManager manager;
    loadCatalog(manager);
    assert(manager.declareResource(kMaterialId, "MaterialResource").success);
    const auto loaded = arp::resource::loadTerrainAssetResource(
        manager, makeReference(), root / "terrain.asset.yaml");
    if (!loaded.success) {
        std::cerr << loaded.diagnostics << '\n';
    }
    assert(loaded.success);

    const auto handle = manager.find<arp::resource::TerrainAssetResource>(kTerrainId);
    assert(handle.ready());
    const auto* terrain = handle.get();
    assert(terrain != nullptr);
    const auto centre = terrain->sample(1.0f, 1.0f);
    assert(centre.ready());
    assert(std::fabs(centre.sample.height - 4.0f) < 1e-5f);
    assert(std::fabs(centre.sample.baseWeights[0] - 1.0f) < 1e-5f);
    assert(std::fabs(centre.sample.snowOverlay - 32.0f / 255.0f) < 1e-5f);
    assert(std::fabs(centre.sample.wetnessOverlay - 64.0f / 255.0f) < 1e-5f);
    assert(terrain->sample(-0.01f, 1.0f).status
        == arp::terrain::TerrainSurfaceQueryStatus::OutOfBounds);
    const auto initialSnapshot = terrain->snapshotSurface();
    assert(initialSnapshot != nullptr);
    for (std::uint32_t index = 0u; index < 64u; ++index) {
        const auto snapshot = terrain->snapshotSurface();
        assert(snapshot.get() == initialSnapshot.get());
        const auto sample = snapshot->sample(1.0f, 1.0f);
        assert(sample.ready() && std::fabs(sample.sample.height - 4.0f) < 1e-5f);
    }
    const auto* vegetation = terrain->vegetationFor({0, 0});
    assert(vegetation != nullptr && vegetation->size() == 1u);
    assert(vegetation->front().prototypeId == 9u);
    assert(vegetation->front().variationSeed == 42u);

    arp::core::ScatterDesc scatter;
    scatter.minX = 0.5f;
    scatter.maxX = 1.5f;
    scatter.minZ = 0.5f;
    scatter.maxZ = 1.5f;
    scatter.step = 0.25f;
    scatter.density = 1.0f;
    assert(arp::core::countScatter(*terrain, scatter) > 0u);
}

void assertCorruptBlockFails(const std::filesystem::path& root)
{
    const auto written = arp::resource::writeTerrainAsset(makeAsset(), root);
    assert(written.success);
    std::ofstream corrupt(root / "tiles/0_0.weight-a.bin", std::ios::binary | std::ios::app);
    corrupt.put(static_cast<char>(0xff));
    corrupt.close();

    arp::resource::ResourceManager manager;
    loadCatalog(manager);
    assert(manager.declareResource(kMaterialId, "MaterialResource").success);
    const auto loaded = arp::resource::loadTerrainAssetResource(
        manager, makeReference(), root / "terrain.asset.yaml");
    assert(!loaded.success);
    assert(loaded.diagnostics.find("TerrainAsset block") != arp::core::pmr::String::npos);
}

void assertInvalidAuthoringFails()
{
    auto duplicateLayer = makeAsset();
    duplicateLayer.layers[1].semantic = duplicateLayer.layers[0].semantic;
    assert(!arp::resource::writeTerrainAsset(duplicateLayer,
        std::filesystem::temp_directory_path() / "arp_terrain_asset_duplicate_layer")
            .success);

    auto outsideVegetation = makeAsset();
    outsideVegetation.tiles.front().vegetation.front().position.x = 3.0f;
    assert(!arp::resource::writeTerrainAsset(outsideVegetation,
        std::filesystem::temp_directory_path() / "arp_terrain_asset_outside_vegetation")
            .success);

    auto duplicateTile = makeAsset();
    duplicateTile.bounds.maxX = 4.0f;
    duplicateTile.tileCountX = 2u;
    duplicateTile.tiles.push_back(duplicateTile.tiles.front());
    assert(!arp::resource::writeTerrainAsset(
        duplicateTile, std::filesystem::temp_directory_path() / "arp_terrain_asset_duplicate_tile")
            .success);

    auto discontinuous = makeAsset();
    discontinuous.bounds.maxX = 4.0f;
    discontinuous.tileCountX = 2u;
    auto rightTile = discontinuous.tiles.front();
    rightTile.coord = {1, 0};
    rightTile.vegetation.clear();
    rightTile.heights = {0.0f, 2.0f, 0.0f, 2.0f};
    discontinuous.tiles.push_back(std::move(rightTile));
    assert(!arp::resource::writeTerrainAsset(
        discontinuous, std::filesystem::temp_directory_path() / "arp_terrain_asset_discontinuous")
            .success);
}

void assertManifestIdentityAndPrototypeFail(const std::filesystem::path& root)
{
    assert(arp::resource::writeTerrainAsset(makeAsset(), root).success);
    const auto manifest = root / "terrain.asset.yaml";
    std::ifstream input(manifest, std::ios::binary);
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto marker = std::string("resourceId: ") + kTerrainId;
    const auto replacement = std::string("resourceId: 11111111-1111-4111-8111-111111111111");
    assert(contents.find(marker) != std::string::npos);
    contents.replace(contents.find(marker), marker.size(), replacement);
    std::ofstream output(manifest, std::ios::binary | std::ios::trunc);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    arp::resource::ResourceManager mismatchedManager;
    const auto mismatched =
        arp::resource::loadTerrainAssetResource(mismatchedManager, makeReference(), manifest);
    assert(!mismatched.success);
    assert(mismatched.diagnostics.find("resourceId") != arp::core::pmr::String::npos);

    const auto prototypeRoot = root.string() + "_unknown_prototype";
    auto invalidPrototype = makeAsset();
    invalidPrototype.tiles.front().vegetation.front().prototypeId = 17u;
    assert(arp::resource::writeTerrainAsset(invalidPrototype, prototypeRoot).success);
    arp::resource::ResourceManager prototypeManager;
    loadCatalog(prototypeManager);
    assert(prototypeManager.declareResource(kMaterialId, "MaterialResource").success);
    const auto prototypeLoad = arp::resource::loadTerrainAssetResource(prototypeManager,
        makeReference(),
        std::filesystem::path(prototypeRoot) / "terrain.asset.yaml");
    assert(!prototypeLoad.success);
    assert(prototypeLoad.diagnostics.find("vegetation instance") != arp::core::pmr::String::npos);
}

void assertReloadPublishesNewSurfaceGeneration(const std::filesystem::path& root)
{
    auto original = makeAsset();
    assert(arp::resource::writeTerrainAsset(original, root).success);

    arp::resource::ResourceManager manager;
    loadCatalog(manager);
    assert(manager.declareResource(kMaterialId, "MaterialResource").success);
    assert(arp::resource::loadTerrainAssetResource(
        manager, makeReference(), root / "terrain.asset.yaml")
            .success);
    const auto first = manager.find<arp::resource::TerrainAssetResource>(kTerrainId);
    assert(first.ready());
    const auto oldSurface = first.get()->snapshotSurface();
    const auto oldValue = oldSurface->sample(1.0f, 1.0f).sample.height;
    const auto oldGeneration = manager.generationOf(kTerrainId);

    auto updated = makeAsset();
    for (auto& height : updated.tiles.front().heights)
        height += 10.0f;
    assert(arp::resource::writeTerrainAsset(updated, root).success);
    auto reload = arp::core::makeUnique<arp::resource::TerrainAssetResource>(
        makeReference(), root / "terrain.asset.yaml");
    const auto reloaded = manager.reloadResource(std::move(reload));
    assert(reloaded.success && reloaded.telemetry.committed);
    assert(manager.generationOf(kTerrainId) == oldGeneration + 1u);
    const auto second = manager.find<arp::resource::TerrainAssetResource>(kTerrainId);
    assert(second.ready());
    assert(std::fabs(second.get()->sample(1.0f, 1.0f).sample.height - (oldValue + 10.0f)) < 1e-5f);
    assert(std::fabs(oldSurface->sample(1.0f, 1.0f).sample.height - oldValue) < 1e-5f);

    auto vegetationOnly = updated;
    ++vegetationOnly.tiles.front().vegetation.front().variationSeed;
    assert(arp::resource::writeTerrainAsset(vegetationOnly, root).success);
    auto vegetationReload = arp::core::makeUnique<arp::resource::TerrainAssetResource>(
        makeReference(), root / "terrain.asset.yaml");
    const auto vegetationReloaded = manager.reloadResource(std::move(vegetationReload));
    assert(vegetationReloaded.success && vegetationReloaded.telemetry.committed);
    assert(manager.generationOf(kTerrainId) == oldGeneration + 2u);
}

} // namespace

int main()
{
    const auto root =
        std::filesystem::temp_directory_path() / "arp_terrain_asset_resource_contract";
    const auto corruptRoot =
        std::filesystem::temp_directory_path() / "arp_terrain_asset_resource_contract_corrupt";
    const auto reloadRoot =
        std::filesystem::temp_directory_path() / "arp_terrain_asset_resource_contract_reload";
    assertRoundTrip(root);
    assertCorruptBlockFails(corruptRoot);
    assertInvalidAuthoringFails();
    assertManifestIdentityAndPrototypeFail(root.string() + "_manifest");
    assertReloadPublishesNewSurfaceGeneration(reloadRoot);
    return 0;
}
