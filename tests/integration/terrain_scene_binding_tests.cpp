#include "renderer/gi/ddgi_renderer.h"
#include "renderer/gi/ddgi_terrain_surface.h"
#include "renderer/gi/ddgi_terrain_surface_gpu.h"
#include "renderer/terrain_gpu_rhi_uploader.h"
#include "renderer/terrain_gpu_runtime.h"
#include "renderer/terrain_pbr_page_descriptors.h"
#include "renderer/terrain_scene_binding.h"
#include "renderer/terrain_vegetation_rhi_runtime.h"
#include "renderer/terrain_vegetation_runtime.h"
#include "resource/vegetation_catalog_resource.h"
#include "rhi/backends/null/null_backend.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <string>

namespace {

constexpr const char* kTerrainId = "a33e9a81-42fc-4a58-bc67-7001c1b3a206";
constexpr const char* kCatalogId = "cd1c92aa-f94d-4d74-bdba-9d279d5069f0";
constexpr const char* kMaterialId = "66a905e8-dc1d-4dcf-b503-2d3b185a9705";

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

arp::resource::TerrainAssetBuildDesc makeAsset()
{
    arp::resource::TerrainAssetBuildDesc asset;
    asset.resourceId = text(kTerrainId);
    asset.alias = text("terrain/scene-binding");
    asset.bounds = {10.0f, 12.0f, -4.0f, -2.0f};
    asset.tileCountX = 1u;
    asset.tileCountZ = 1u;
    asset.samplesPerEdge = 2u;
    asset.tileWorldSize = 2.0f;
    asset.vegetationCatalogResourceId = text(kCatalogId);
    for (std::uint32_t index = 0u; index < asset.layers.size(); ++index) {
        asset.layers[index].layerId = text(("layer" + std::to_string(index)).c_str());
        asset.layers[index].semantic = text(("semantic" + std::to_string(index)).c_str());
        asset.layers[index].materialResourceId = text(kMaterialId);
        asset.layers[index].page = index < 4u ? 0u : 1u;
        asset.layers[index].channel = index < 4u ? index : index - 4u;
    }
    arp::resource::TerrainAssetBuildTile tile;
    tile.coord = {0, 0};
    tile.heights = {2.0f, 2.0f, 2.0f, 2.0f};
    tile.weightPageA.resize(16u, 0u);
    tile.weightPageA[0] = tile.weightPageA[4] = tile.weightPageA[8] = tile.weightPageA[12] = 255u;
    tile.weightPageB.resize(16u, 0u);
    arp::resource::TerrainStaticVegetationInstance vegetation;
    vegetation.prototypeId = 9u;
    vegetation.position = {11.0f, 2.0f, -3.0f};
    vegetation.variationSeed = 41u;
    tile.vegetation.push_back(vegetation);
    asset.tiles.push_back(std::move(tile));
    return asset;
}

arp::resource::ResourceReference reference()
{
    arp::resource::ResourceReference result;
    result.id = text(kTerrainId);
    result.type = text("TerrainAssetResource");
    result.alias = text("terrain/scene-binding");
    result.status = text("Contract Complete");
    return result;
}

class TestCatalog final : public arp::resource::VegetationCatalogResource {
public:
    TestCatalog() : arp::resource::VegetationCatalogResource(makeDesc()) {}

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& manager,
        const arp::resource::ResourceLoadContext& context) override
    {
        data = arp::core::makeShared<arp::resource::VegetationCatalogResourceData>();
        arp::resource::VegetationCatalogAssetEntry asset;
        asset.id = text("scene-binding-plant");
        asset.templateId = 9u;
        data->assets.push_back(std::move(asset));
        return arp::resource::ResourceBase::load(manager, context);
    }

private:
    static arp::resource::VegetationCatalogResourceLoadDesc makeDesc()
    {
        arp::resource::VegetationCatalogResourceLoadDesc desc;
        desc.resourceId = text(kCatalogId);
        desc.status = text("Contract Complete");
        return desc;
    }
};

bool resolveMaterial(void* context,
    const arp::resource::TerrainAssetResource&,
    arp::renderer::MaterialHandle& out,
    arp::core::pmr::String&)
{
    out = *static_cast<arp::renderer::MaterialHandle*>(context);
    return true;
}

std::uint32_t floatBits(float value)
{
    std::uint32_t result = 0u;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

void validatesDdgiBakedTerrainHierarchy()
{
    arp::renderer::DdgiBakedTerrainGrid grid;
    grid.bounds = {-7.0f, 3.0f, 8.0f, 11.0f};
    grid.samplesX = 3u;
    grid.samplesZ = 2u;
    grid.heights = {5.0f, -4.0f, 8.0f, 3.0f, 12.0f, 1.0f};
    assert(grid.valid());

    const auto hierarchy = arp::renderer::buildDdgiTerrainMinMaxHierarchy(grid);
    const arp::renderer::DdgiTerrainMinMaxHierarchyLayout layout;
    assert(hierarchy.size() == layout.totalRecordCount());
    assert(hierarchy[0][0] == -7.0f && hierarchy[0][1] == 8.0f);
    assert(hierarchy[0][2] == 3.0f && hierarchy[0][3] == 11.0f);
    assert(floatBits(hierarchy[1][0]) == 3u);
    assert(floatBits(hierarchy[1][1]) == 2u);
    assert(floatBits(hierarchy[1][2]) == layout.levelCount);
    assert(floatBits(hierarchy[1][3]) == layout.totalRecordCount());

    const auto baseOffset = layout.levelOffset(0u);
    for (std::uint32_t z = 0u; z < layout.levelResolution(0u); ++z) {
        for (std::uint32_t x = 0u; x < layout.levelResolution(0u); ++x) {
            const auto& record = hierarchy[baseOffset + z * layout.levelResolution(0u) + x];
            assert(record[0] <= -4.0f);
            assert(record[1] >= 12.0f);
        }
    }
    const auto topOffset = layout.levelOffset(layout.levelCount - 1u);
    assert(hierarchy[topOffset][0] == -4.0f);
    assert(hierarchy[topOffset][1] == 12.0f);

    grid.heights.pop_back();
    assert(!grid.valid());
    assert(arp::renderer::buildDdgiTerrainMinMaxHierarchy(grid).empty());
}

struct UploadTelemetry final {
    std::size_t count = 0u;
};

bool uploadVegetationPage(
    void* context, const arp::renderer::TerrainVegetationPage& page, arp::core::pmr::String&)
{
    auto& telemetry = *static_cast<UploadTelemetry*>(context);
    ++telemetry.count;
    return page.prototypes.size() == 1u && page.prototypes.front().prototypeId == 9u
        && page.prototypes.front().instances.size() == 1u;
}

void bindsOnlyResolvedRuntimeState()
{
    const auto path = std::filesystem::temp_directory_path() / "arp_terrain_scene_binding";
    assert(arp::resource::writeTerrainAsset(makeAsset(), path).success);

    arp::resource::ResourceManager resources;
    assert(resources.load<TestCatalog>(arp::resource::ResourceLoadContext{}).success);
    assert(resources.declareResource(kMaterialId, "MaterialResource").success);
    assert(
        arp::resource::loadTerrainAssetResource(resources, reference(), path / "terrain.asset.yaml")
            .success);
    const auto terrainResource = resources.find<arp::resource::TerrainAssetResource>(kTerrainId);
    assert(terrainResource.ready());

    arp::resource::TerrainAssetTileReadView tileView;
    assert(terrainResource.get()->tileReadView({0, 0}, tileView));
    assert(tileView.valid() && tileView.heights[0] == 2.0f);
    arp::renderer::DdgiTerrainSurfaceInput ddgiSurfaceInput;
    ddgiSurfaceInput.surface = terrainResource.get()->snapshotSurface();
    ddgiSurfaceInput.samplesX = 2u;
    ddgiSurfaceInput.samplesZ = 2u;
    arp::core::pmr::Vector<float> ddgiSurfaceGrid{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String ddgiSurfaceDiagnostics{&arp::core::defaultCpuMemoryResource()};
    assert(arp::renderer::buildDdgiTerrainSurfaceGrid(
        ddgiSurfaceInput, ddgiSurfaceGrid, ddgiSurfaceDiagnostics));
    assert(ddgiSurfaceGrid.size() == 4u && ddgiSurfaceGrid.front() == 2.0f);
    arp::renderer::DdgiBakedTerrainGrid ddgiBakedGrid;
    assert(arp::renderer::buildDdgiBakedTerrainGrid(
        ddgiSurfaceInput, ddgiBakedGrid, ddgiSurfaceDiagnostics));
    assert(ddgiBakedGrid.valid());
    assert(ddgiBakedGrid.bounds.minX == 10.0f && ddgiBakedGrid.bounds.maxZ == -2.0f);
    assert(ddgiBakedGrid.samplesX == 2u && ddgiBakedGrid.samplesZ == 2u);
    assert(ddgiBakedGrid.heights == ddgiSurfaceGrid);
    arp::renderer::TerrainGpuTilePayload tilePayload;
    assert(
        arp::renderer::terrainGpuTilePayloadFromAsset(*terrainResource.get(), {0, 0}, tilePayload));
    assert(tilePayload.valid());
    assert(tilePayload.worldBounds.minX == 10.0f && tilePayload.worldBounds.maxZ == -2.0f);
    arp::renderer::TerrainGpuRuntime gpuRuntime;
    std::size_t tileUploads = 0u;
    const auto uploadTile = [&tileUploads](const arp::renderer::TerrainGpuTilePayload& payload,
                                arp::renderer::TerrainGpuTileBackendPayload& pages) {
        ++tileUploads;
        if (!payload.valid())
            return false;
        pages = {101u, 102u, 103u};
        return true;
    };
    const auto replaceTile = [&tileUploads](const arp::renderer::TerrainGpuTilePayload& payload,
                                 arp::renderer::TerrainGpuTileBackendPayload& pages) {
        ++tileUploads;
        if (!payload.valid())
            return false;
        pages = {201u, 202u, 203u};
        return true;
    };
    assert(gpuRuntime.syncTile(tilePayload, 7u, uploadTile, replaceTile));
    assert(gpuRuntime.activeTileCount() == 1u && tileUploads == 1u);
    assert(gpuRuntime.pageTable().front().pages.heightPage == 101u);
    assert(gpuRuntime.syncTile(tilePayload, 7u, uploadTile, replaceTile));
    assert(tileUploads == 1u);
    auto reloadedPayload = tilePayload;
    ++reloadedPayload.resourceGeneration;
    assert(gpuRuntime.syncTile(reloadedPayload, 7u, uploadTile, replaceTile));
    assert(tileUploads == 2u && gpuRuntime.pageTable().front().pages.heightPage == 201u);
    assert(gpuRuntime.retiredSlotCount() == 1u);
    assert(gpuRuntime.reapCompleted(7u) == 0u);
    assert(gpuRuntime.reapCompleted(8u) == 1u);

    auto nullDevice = arp::rhi::createNullDevice();
    assert(nullDevice);
    arp::renderer::DdgiTerrainSurfaceGpuPayload ddgiTerrainPayload;
    const auto ddgiTerrainUploaded =
        ddgiTerrainPayload.create(*nullDevice, ddgiBakedGrid, terrainResource.get()->generation());
    assert(ddgiTerrainUploaded.success);
    assert(
        ddgiTerrainUploaded.status == arp::renderer::DdgiTerrainSurfaceGpuStatus::NullValidation);
    assert(ddgiTerrainPayload.ready() && ddgiTerrainPayload.view() != nullptr);
    arp::renderer::TerrainGpuRhiUploader uploader;
    arp::renderer::TerrainGpuRhiUploaderDesc uploaderDesc;
    uploaderDesc.samplesPerEdge = 2u;
    uploaderDesc.maxTilePages = 2u;
    uploaderDesc.maxPageTableSnapshots = 2u;
    const auto createdUploader = uploader.create(*nullDevice, uploaderDesc);
    assert(createdUploader.success);
    assert(createdUploader.status == arp::renderer::TerrainGpuPageBackendStatus::NullValidation);
    assert(uploader.heightPagesView() != nullptr && uploader.weightPageAView() != nullptr
        && uploader.weightPageBView() != nullptr);
    const auto pageUploaded = uploader.upload(tilePayload, 0u, 7u);
    assert(pageUploaded.success && pageUploaded.pages.valid());
    assert(pageUploaded.pages.weightArrayLayer == 0u);
    arp::core::pmr::Vector<arp::renderer::TerrainGpuPageTableEntry> uploadedEntries{
        &arp::core::defaultCpuMemoryResource()};
    uploadedEntries.push_back({tilePayload.coord,
        tilePayload.worldBounds,
        tilePayload.resourceGeneration,
        pageUploaded.pages});
    const auto pageTableWritten = uploader.writePageTable(uploadedEntries, 7u);
    assert(pageTableWritten.success && pageTableWritten.pageTableGeneration == 1u);
    assert(uploader.pageTableBuffer() != nullptr && uploader.pageTableParamsBuffer() != nullptr);
    const auto firstPageTableSnapshot = uploader.pageTableSnapshot();
    assert(firstPageTableSnapshot.valid() && firstPageTableSnapshot.generation == 1u);
    auto terrainDescriptorArena =
        nullDevice->createDescriptorArena({"TerrainAssetDescriptorArena", 1u, 4u});
    assert(terrainDescriptorArena);
    arp::rhi::DescriptorLayoutDesc terrainPageLayout;
    terrainPageLayout.name = "TerrainAssetPbrPages";
    terrainPageLayout.bindings = {
        {arp::renderer::TerrainPbrPageBindings::weightPageA,
            arp::rhi::DescriptorType::SampledTexture,
            arp::rhi::ShaderStage::Fragment},
        {arp::renderer::TerrainPbrPageBindings::weightPageB,
            arp::rhi::DescriptorType::SampledTexture,
            arp::rhi::ShaderStage::Fragment},
        {arp::renderer::TerrainPbrPageBindings::pageTable,
            arp::rhi::DescriptorType::ReadOnlyStorageBuffer,
            arp::rhi::ShaderStage::Fragment},
        {arp::renderer::TerrainPbrPageBindings::pageTableParams,
            arp::rhi::DescriptorType::UniformBuffer,
            arp::rhi::ShaderStage::Fragment},
    };
    terrainPageLayout.bindings[0].textureViewDimensionConstrained = true;
    terrainPageLayout.bindings[0].textureViewDimension =
        arp::rhi::TextureViewDimension::Texture2DArray;
    terrainPageLayout.bindings[1].textureViewDimensionConstrained = true;
    terrainPageLayout.bindings[1].textureViewDimension =
        arp::rhi::TextureViewDimension::Texture2DArray;
    auto terrainPageSet = terrainDescriptorArena->allocateDescriptorSet(
        {"TerrainAssetPbrPageSet", terrainPageLayout});
    assert(terrainPageSet);
    const auto firstDescriptorWrite = arp::renderer::writeTerrainPbrPageDescriptors(
        *terrainPageSet, uploader, firstPageTableSnapshot);
    assert(firstDescriptorWrite.success && firstDescriptorWrite.pageTableGeneration == 1u);
    assert(terrainPageSet->hasWrittenBinding(arp::renderer::TerrainPbrPageBindings::weightPageA));
    assert(terrainPageSet->hasWrittenBinding(arp::renderer::TerrainPbrPageBindings::weightPageB));
    assert(terrainPageSet->hasWrittenBinding(arp::renderer::TerrainPbrPageBindings::pageTable));
    assert(
        terrainPageSet->hasWrittenBinding(arp::renderer::TerrainPbrPageBindings::pageTableParams));

    // Page-table descriptors bind an immutable snapshot, not whichever
    // generation happens to be active by the time a reload is published.
    auto reloadedEntries = uploadedEntries;
    reloadedEntries.front().resourceGeneration = tilePayload.resourceGeneration + 1u;
    const auto secondPageTableWritten = uploader.writePageTable(reloadedEntries, 8u);
    assert(secondPageTableWritten.success && secondPageTableWritten.pageTableGeneration == 2u);
    const auto secondPageTableSnapshot = uploader.pageTableSnapshot();
    assert(secondPageTableSnapshot.valid() && secondPageTableSnapshot.generation == 2u);
    assert(firstPageTableSnapshot.entries != secondPageTableSnapshot.entries);
    assert(firstPageTableSnapshot.params != secondPageTableSnapshot.params);
    const auto retainedFirstDescriptorWrite = arp::renderer::writeTerrainPbrPageDescriptors(
        *terrainPageSet, uploader, firstPageTableSnapshot);
    assert(retainedFirstDescriptorWrite.success
        && retainedFirstDescriptorWrite.pageTableGeneration == firstPageTableSnapshot.generation);
    const auto exhaustedPageTableWrite = uploader.writePageTable(uploadedEntries, 8u);
    assert(!exhaustedPageTableWrite.success
        && exhaustedPageTableWrite.status
            == arp::renderer::TerrainGpuPageBackendStatus::DiagnosticFailure);
    assert(uploader.reapCompletedPageTableSnapshots(8u) == 0u);
    assert(uploader.reapCompletedPageTableSnapshots(9u) == 1u);
    const auto thirdPageTableWritten = uploader.writePageTable(uploadedEntries, 9u);
    assert(thirdPageTableWritten.success && thirdPageTableWritten.pageTableGeneration == 3u);

    // RHI publication owns array-layer leases. A replacement must receive a
    // distinct layer until the old frame's strict retirement serial completes;
    // otherwise an old draw could observe new terrain data through its old
    // page-table entry.
    arp::renderer::TerrainGpuRhiUploader tileUploader;
    auto tileUploaderDesc = uploaderDesc;
    tileUploaderDesc.maxPageTableSnapshots = 3u;
    assert(tileUploader.create(*nullDevice, tileUploaderDesc).success);
    arp::renderer::TerrainGpuRuntime rhiGpuRuntime;
    arp::core::pmr::String rhiDiagnostics{&arp::core::defaultCpuMemoryResource()};
    assert(rhiGpuRuntime.syncTileWithRhi(tilePayload, 7u, tileUploader, rhiDiagnostics));
    assert(rhiGpuRuntime.pageTable().front().pages.weightArrayLayer == 0u);
    assert(rhiGpuRuntime.syncTileWithRhi(tilePayload, 7u, tileUploader, rhiDiagnostics));
    assert(rhiGpuRuntime.activeTileCount() == 1u);
    assert(rhiGpuRuntime.syncTileWithRhi(reloadedPayload, 7u, tileUploader, rhiDiagnostics));
    assert(rhiGpuRuntime.pageTable().front().pages.weightArrayLayer == 1u);
    auto thirdGenerationPayload = reloadedPayload;
    ++thirdGenerationPayload.resourceGeneration;
    assert(
        !rhiGpuRuntime.syncTileWithRhi(thirdGenerationPayload, 7u, tileUploader, rhiDiagnostics));
    assert(rhiGpuRuntime.reapCompleted(7u) == 0u);
    assert(rhiGpuRuntime.reapCompleted(8u) == 1u);
    assert(tileUploader.reapCompletedPageTableSnapshots(8u) == 1u);
    assert(rhiGpuRuntime.syncTileWithRhi(thirdGenerationPayload, 8u, tileUploader, rhiDiagnostics));
    assert(rhiGpuRuntime.pageTable().front().pages.weightArrayLayer == 0u);
    assert(tileUploader.reapCompletedPageTableSnapshots(9u) == 1u);
    arp::core::pmr::Vector<arp::terrain::TerrainTileCoord> noActiveTerrainTiles{
        &arp::core::defaultCpuMemoryResource()};
    assert(rhiGpuRuntime.retireInactiveTilesWithRhi(
        noActiveTerrainTiles, 9u, tileUploader, rhiDiagnostics));
    assert(rhiGpuRuntime.activeTileCount() == 0u);
    assert(rhiGpuRuntime.reapCompleted(10u) == 2u);

    // Production streaming stages every visible tile before publishing one
    // complete table. Two staged tiles must consume only one snapshot
    // generation; per-tile publication previously exhausted the snapshot
    // budget and left later visible chunks absent.
    arp::renderer::TerrainGpuRhiUploader batchUploader;
    assert(batchUploader.create(*nullDevice, uploaderDesc).success);
    arp::renderer::TerrainGpuRuntime batchRuntime;
    auto adjacentPayload = tilePayload;
    adjacentPayload.coord = {1, 0};
    adjacentPayload.worldBounds = {1.0f, 2.0f, 0.0f, 1.0f};
    assert(batchRuntime.syncTileWithRhi(tilePayload, 11u, batchUploader, rhiDiagnostics, false));
    assert(
        batchRuntime.syncTileWithRhi(adjacentPayload, 11u, batchUploader, rhiDiagnostics, false));
    assert(!batchUploader.pageTableSnapshot().valid());
    assert(batchRuntime.activeTileCount() == 2u);
    assert(batchRuntime.publishPageTableWithRhi(11u, batchUploader, rhiDiagnostics));
    assert(batchUploader.pageTableSnapshot().valid());
    assert(batchUploader.pageTableSnapshot().generation == 1u);

    arp::renderer::TerrainVegetationRuntime vegetationRuntime;
    UploadTelemetry uploadTelemetry;
    const auto catalog = resources.find<arp::resource::VegetationCatalogResource>(kCatalogId);
    assert(catalog.ready());
    arp::resource::VegetationCatalogResourceLoadDesc wrongCatalogDesc;
    wrongCatalogDesc.resourceId = text("2a39d9f4-245d-4dac-8b75-cbd856544a5a");
    wrongCatalogDesc.status = text("Contract Complete");
    arp::resource::VegetationCatalogResource wrongCatalog(wrongCatalogDesc);
    wrongCatalog.data = arp::core::makeShared<arp::resource::VegetationCatalogResourceData>();
    arp::resource::VegetationCatalogAssetEntry wrongCatalogAsset;
    wrongCatalogAsset.id = text("wrong-catalog-plant");
    wrongCatalogAsset.templateId = 9u;
    wrongCatalog.data->assets.push_back(std::move(wrongCatalogAsset));
    const auto rejectedCatalog = vegetationRuntime.activate(
        *terrainResource.get(), wrongCatalog, {0, 0}, 7u, uploadVegetationPage, &uploadTelemetry);
    assert(!rejectedCatalog.success);
    assert(rejectedCatalog.diagnostics.find("declared ready") != arp::core::pmr::String::npos);
    const auto activated = vegetationRuntime.activate(
        *terrainResource.get(), *catalog.get(), {0, 0}, 7u, uploadVegetationPage, &uploadTelemetry);
    assert(activated.success && !activated.unchanged && uploadTelemetry.count == 1u);
    assert(vegetationRuntime.activePageCount() == 1u);
    assert(vegetationRuntime.page({0, 0}) != nullptr);
    arp::core::pmr::Vector<arp::renderer::TerrainVegetationGpuInstance> gpuVegetationInstances{
        &arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String gpuVegetationDiagnostics{&arp::core::defaultCpuMemoryResource()};
    const auto* activeVegetationPage = vegetationRuntime.page({0, 0});
    assert(activeVegetationPage != nullptr && !activeVegetationPage->prototypes.empty());
    assert(
        arp::renderer::buildTerrainVegetationGpuInstances(activeVegetationPage->prototypes.front(),
            gpuVegetationInstances,
            gpuVegetationDiagnostics));
    assert(!gpuVegetationInstances.empty()
        && gpuVegetationInstances.front().prototypeId
            == activeVegetationPage->prototypes.front().prototypeId);
    arp::core::pmr::Vector<arp::pcg::InstanceDataGpu> cullVegetationInstances{
        &arp::core::defaultCpuMemoryResource()};
    assert(arp::renderer::lowerTerrainVegetationInstancesForCull(
        activeVegetationPage->prototypes.front(),
        cullVegetationInstances,
        gpuVegetationDiagnostics));
    assert(cullVegetationInstances.size() == 1u);
    assert(cullVegetationInstances.front().positionScale[0] == 11.0f);
    assert(cullVegetationInstances.front().positionScale[3] == 1.0f);
    assert(cullVegetationInstances.front().yawPhaseIdLod[2] == 0.0f);
    auto nonRepresentableVegetation = activeVegetationPage->prototypes.front();
    nonRepresentableVegetation.instances.front().scale.z = 2.0f;
    assert(!arp::renderer::lowerTerrainVegetationInstancesForCull(
        nonRepresentableVegetation, cullVegetationInstances, gpuVegetationDiagnostics));
    assert(gpuVegetationDiagnostics.find("cannot be represented") != arp::core::pmr::String::npos);
    arp::renderer::TerrainVegetationRhiRuntime vegetationGpuRuntime;
    const auto vegetationGpuActivated =
        vegetationGpuRuntime.activate(*nullDevice, *activeVegetationPage, 7u);
    assert(vegetationGpuActivated.success && !vegetationGpuActivated.unchanged);
    const auto* uploadedVegetationPage = vegetationGpuRuntime.page({0, 0});
    assert(uploadedVegetationPage != nullptr && !uploadedVegetationPage->prototypes.empty());
    assert(uploadedVegetationPage->prototypes.front().instanceBuffer != nullptr);
    assert(vegetationGpuRuntime.deactivate({0, 0}, 7u));
    assert(vegetationGpuRuntime.reapCompleted(8u) == 1u);
    const auto unchanged = vegetationRuntime.activate(
        *terrainResource.get(), *catalog.get(), {0, 0}, 7u, uploadVegetationPage, &uploadTelemetry);
    assert(unchanged.success && unchanged.unchanged && uploadTelemetry.count == 1u);
    assert(vegetationRuntime.deactivate({0, 0}, 7u));
    assert(vegetationRuntime.retiredPageCount() == 1u);
    assert(vegetationRuntime.reapCompleted(7u) == 0u);
    assert(vegetationRuntime.reapCompleted(8u) == 1u);

    arp::scene3d::TerrainComponent component;
    component.set_resource_id(text(kTerrainId));
    arp::renderer::RuntimeScene runtimeScene;
    auto material = runtimeScene.addMaterial("terrain/pbr");
    arp::renderer::TerrainSceneBinding::Desc desc;
    desc.runtime.worldHalfExtent = 1.0f;
    desc.runtime.chunkSize = 1.0f;
    desc.runtime.samplesPerChunk = 2u;
    desc.runtime.activeRadius = 1u;
    desc.useAssetTileLayout = true;
    desc.resolveMaterial = resolveMaterial;
    desc.context = &material;

    const auto bound =
        arp::renderer::TerrainSceneBinding().bind(component, resources, runtimeScene, desc);
    assert(bound.success);
    assert(component.resource_id() == text(kTerrainId));
    assert(component.desc().terrain.valid());
    assert(component.desc().material == material);
    assert(component.desc().metadata.valid());
    assert(component.desc().localBounds.valid());
    const auto* runtime = runtimeScene.terrain({component.desc().terrain.id});
    assert(runtime != nullptr && (*runtime)->terrainSurface() != nullptr);
    assert((*runtime)->desc().chunkSize == 2.0f);
    assert((*runtime)->desc().samplesPerChunk == 2u);
    assert((*runtime)->update(11.0f, -3.0f, 0.0f, 1.0f, 0.016f));
    assert((*runtime)->visibleChunkCount() > 0u);
    for (const auto& [coord, chunk] : (*runtime)->streamer().activeChunks()) {
        (void)coord;
        for (const auto& vertex : chunk.mesh.vertices) {
            assert(vertex.position[0] >= 10.0f && vertex.position[0] <= 12.0f);
            assert(vertex.position[2] >= -4.0f && vertex.position[2] <= -2.0f);
        }
    }

    const auto previous = component.desc().terrain;
    const auto rebound =
        arp::renderer::TerrainSceneBinding().bind(component, resources, runtimeScene, desc);
    assert(rebound.success);
    assert(component.desc().terrain == previous);
    arp::renderer::TerrainSceneBinding::Desc incomplete;
    incomplete.runtime = desc.runtime;
    const auto rejected =
        arp::renderer::TerrainSceneBinding().bind(component, resources, runtimeScene, incomplete);
    assert(!rejected.success);
    assert(component.desc().terrain == previous);

    arp::pcg::TerrainStreamer invalidGrid;
    arp::pcg::TerrainStreamerDesc invalidGridDesc;
    invalidGridDesc.worldBounds = {0.0f, 2.5f, 0.0f, 2.0f};
    invalidGridDesc.chunkSize = 1.0f;
    invalidGridDesc.heightSampler = [](float, float) {
        return 0.0f;
    };
    assert(!invalidGrid.initialize(invalidGridDesc));
}

} // namespace

int main()
{
    validatesDdgiBakedTerrainHierarchy();
    bindsOnlyResolvedRuntimeState();
    return 0;
}
