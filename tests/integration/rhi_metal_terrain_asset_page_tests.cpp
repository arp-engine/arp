#include "renderer/terrain_gpu_rhi_uploader.h"
#include "renderer/terrain_pbr_page_descriptors.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/semantics/native_backend_capabilities.h"

#include <array>
#include <cassert>
#include <cstdint>

namespace {

arp::core::UniquePtr<arp::rhi::DescriptorSet> makePageSet(
    arp::rhi::GpuDevice& device, arp::core::UniquePtr<arp::rhi::DescriptorArena>& arena)
{
    arena = device.createDescriptorArena({"TerrainAssetMetalDescriptorArena", 1u, 4u});
    if (!arena)
        return {};
    arp::rhi::DescriptorLayoutDesc layout;
    layout.name = "TerrainAssetMetalPbrPages";
    layout.bindings = {
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
    layout.bindings[0].textureViewDimensionConstrained = true;
    layout.bindings[0].textureViewDimension = arp::rhi::TextureViewDimension::Texture2DArray;
    layout.bindings[1].textureViewDimensionConstrained = true;
    layout.bindings[1].textureViewDimension = arp::rhi::TextureViewDimension::Texture2DArray;
    return arena->allocateDescriptorSet({"TerrainAssetMetalPbrPageSet", layout});
}

} // namespace

int main()
{
    arp::rhi_metal::MetalRhiDeviceDesc deviceDesc;
    deviceDesc.capabilityContract =
        arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Metal);
    deviceDesc.capabilityContract.queriedFromNativeDevice = true;
    deviceDesc.capabilityContract.queriedExtendedFeatures = true;
    deviceDesc.memoryBudgetBytes = 4u * 1024u * 1024u;
    deviceDesc.uploadStagingPageSizeBytes = 4096u;
    deviceDesc.uploadStagingMaxPageCount = 2u;
    auto device = arp::rhi_metal::createMetalRhiDevice(deviceDesc);
    assert(device);

    arp::renderer::TerrainGpuRhiUploader uploader;
    arp::renderer::TerrainGpuRhiUploaderDesc uploaderDesc;
    uploaderDesc.samplesPerEdge = 2u;
    uploaderDesc.maxTilePages = 2u;
    uploaderDesc.maxPageTableSnapshots = 2u;
    uploaderDesc.label = "TerrainAssetMetal";
    const auto created = uploader.create(*device, uploaderDesc);
    assert(created.success);
    assert(created.status == arp::renderer::TerrainGpuPageBackendStatus::Native);
    assert(uploader.heightPagesView() && uploader.weightPageAView() && uploader.weightPageBView());
    assert(arp::rhi_metal::inspectMetalRhiNativePayload(*uploader.heightPagesView())
            .nativeRuntimeBacked);
    assert(arp::rhi_metal::inspectMetalRhiNativePayload(*uploader.weightPageAView())
            .nativeRuntimeBacked);
    assert(arp::rhi_metal::inspectMetalRhiNativePayload(*uploader.weightPageBView())
            .nativeRuntimeBacked);
    assert(uploader.retirementDemandForPageTablePublication() == 0u);

    const std::array<float, 4> heights{0.0f, 0.25f, 0.5f, 0.75f};
    const std::array<std::uint8_t, 16> weightsA{
        255u, 0u, 0u, 0u, 255u, 0u, 0u, 0u, 255u, 0u, 0u, 0u, 255u, 0u, 0u, 0u};
    const std::array<std::uint8_t, 16> weightsB{};
    const arp::renderer::TerrainGpuTilePayload tile{{0, 0},
        {-1.0f, 1.0f, -1.0f, 1.0f},
        2u,
        1u,
        heights.data(),
        heights.size(),
        weightsA.data(),
        weightsA.size(),
        weightsB.data(),
        weightsB.size()};
    const auto uploaded = uploader.upload(tile, 0u, 1u);
    assert(
        uploaded.success && uploaded.status == arp::renderer::TerrainGpuPageBackendStatus::Native);
    assert(uploaded.pages.valid() && uploaded.pages.weightArrayLayer == 0u);

    arp::core::pmr::Vector<arp::renderer::TerrainGpuPageTableEntry> entries{
        &arp::core::defaultCpuMemoryResource()};
    entries.push_back({tile.coord, tile.worldBounds, tile.resourceGeneration, uploaded.pages});
    const auto tableWritten = uploader.writePageTable(entries, 1u);
    assert(tableWritten.success && tableWritten.pageTableGeneration == 1u);
    const auto snapshot = uploader.pageTableSnapshot();
    assert(snapshot.valid());
    assert(uploader.retirementDemandForPageTablePublication() == 1u);
    assert(arp::rhi_metal::inspectMetalRhiNativePayload(*snapshot.entries).nativeRuntimeBacked);
    assert(arp::rhi_metal::inspectMetalRhiNativePayload(*snapshot.params).nativeRuntimeBacked);

    arp::core::UniquePtr<arp::rhi::DescriptorArena> arena;
    auto set = makePageSet(*device, arena);
    assert(set);
    const auto bound = arp::renderer::writeTerrainPbrPageDescriptors(*set, uploader, snapshot);
    assert(bound.success && bound.pageTableGeneration == snapshot.generation);
    assert(set->hasWrittenBinding(arp::renderer::TerrainPbrPageBindings::weightPageA));
    assert(set->hasWrittenBinding(arp::renderer::TerrainPbrPageBindings::weightPageB));
    assert(set->hasWrittenBinding(arp::renderer::TerrainPbrPageBindings::pageTable));
    assert(set->hasWrittenBinding(arp::renderer::TerrainPbrPageBindings::pageTableParams));

    device->waitIdle();
    return 0;
}
