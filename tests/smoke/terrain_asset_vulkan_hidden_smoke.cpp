#include "platform/render_system.h"
#include "renderer/terrain_gpu_rhi_uploader.h"
#include "renderer/terrain_pbr_page_descriptors.h"
#include "rhi/backends/vulkan/vulkan_device.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace {

arp::core::UniquePtr<arp::rhi::DescriptorSet> makeTerrainPageSet(
    arp::rhi::GpuDevice& device, arp::core::UniquePtr<arp::rhi::DescriptorArena>& arena)
{
    arena = device.createDescriptorArena({"TerrainAssetVulkanDescriptorArena", 1u, 4u});
    if (!arena)
        return {};
    arp::rhi::DescriptorLayoutDesc layout;
    layout.name = "TerrainAssetVulkanPbrPages";
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
    return arena->allocateDescriptorSet({"TerrainAssetVulkanPbrPageSet", layout});
}

} // namespace

int main()
{
    arp::platform::WindowSystem platform;
    const auto initialized = platform.initialize();
    if (!initialized.success) {
        std::fprintf(stderr,
            "TerrainAsset Vulkan hidden smoke unavailable: %s\n",
            initialized.diagnostics.c_str());
        return 0;
    }
    auto window = platform.createWindow({"TerrainAssetVulkanHiddenSmoke", 320, 180, true, true});
    if (!window.valid()) {
        std::fprintf(
            stderr, "TerrainAsset Vulkan hidden smoke unavailable: window creation failed\n");
        return 0;
    }
    auto renderSystem =
        arp::platform::createDefaultRenderSystem(platform, window, "TerrainAssetVulkanHiddenSmoke");
    if (!renderSystem.valid() || renderSystem.backend() != arp::rhi::Backend::Vulkan) {
        std::fprintf(stderr,
            "TerrainAsset Vulkan hidden smoke unavailable: %s\n",
            renderSystem.diagnostics().c_str());
        return 0;
    }
    auto* device = renderSystem.gpuDevice();
    assert(device != nullptr);
    const auto before = arp::rhi_vulkan::inspectVulkanRhiNativeResourceStats(*device);

    arp::renderer::TerrainGpuRhiUploader uploader;
    arp::renderer::TerrainGpuRhiUploaderDesc desc;
    desc.samplesPerEdge = 2u;
    desc.maxTilePages = 2u;
    desc.maxPageTableSnapshots = 2u;
    const auto created = uploader.create(*device, desc);
    assert(created.success);
    assert(created.status == arp::renderer::TerrainGpuPageBackendStatus::Native);

    const std::array<float, 4> heights{0.0f, 1.0f, 2.0f, 3.0f};
    const std::array<std::uint8_t, 16> weightPageA{
        255u, 0u, 0u, 0u, 0u, 255u, 0u, 0u, 0u, 0u, 255u, 0u, 0u, 0u, 0u, 255u};
    const std::array<std::uint8_t, 16> weightPageB{
        0u, 255u, 0u, 0u, 0u, 0u, 255u, 0u, 0u, 0u, 0u, 255u, 255u, 0u, 0u, 0u};
    const arp::renderer::TerrainGpuTilePayload tile{{0, 0},
        {0.0f, 2.0f, 0.0f, 2.0f},
        2u,
        0x100000001ull,
        heights.data(),
        heights.size(),
        weightPageA.data(),
        weightPageA.size(),
        weightPageB.data(),
        weightPageB.size()};
    const auto uploaded = uploader.upload(tile, 0u, 1u);
    if (!uploaded.success) {
        std::fprintf(
            stderr, "TerrainAsset Vulkan upload failed: %s\n", uploaded.diagnostics.c_str());
    }
    assert(uploaded.success && uploaded.pages.valid());
    // A texture-array layer begins Undefined independently of layer zero.
    // This second upload proves the Vulkan barrier tracker lowers each
    // TerrainAsset page range with its own old layout.
    auto secondTile = tile;
    secondTile.coord = {1, 0};
    secondTile.resourceGeneration = 0x100000002ull;
    const auto secondUploaded = uploader.upload(secondTile, 1u, 2u);
    if (!secondUploaded.success) {
        std::fprintf(stderr,
            "TerrainAsset Vulkan second-layer upload failed: %s\n",
            secondUploaded.diagnostics.c_str());
    }
    assert(secondUploaded.success && secondUploaded.pages.valid());
    arp::core::pmr::Vector<arp::renderer::TerrainGpuPageTableEntry> entries{
        &arp::core::defaultCpuMemoryResource()};
    entries.push_back({tile.coord, tile.worldBounds, tile.resourceGeneration, uploaded.pages});
    const auto pageTableWritten = uploader.writePageTable(entries, 1u);
    assert(pageTableWritten.success && pageTableWritten.pageTableGeneration == 1u);
    const auto pageTableSnapshot = uploader.pageTableSnapshot();
    assert(pageTableSnapshot.valid());

    arp::core::UniquePtr<arp::rhi::DescriptorArena> descriptorArena;
    auto pageSet = makeTerrainPageSet(*device, descriptorArena);
    assert(pageSet);
    const auto descriptorWritten =
        arp::renderer::writeTerrainPbrPageDescriptors(*pageSet, uploader, pageTableSnapshot);
    assert(descriptorWritten.success
        && descriptorWritten.pageTableGeneration == pageTableSnapshot.generation);
    assert(pageSet->hasWrittenBinding(arp::renderer::TerrainPbrPageBindings::weightPageA));
    assert(pageSet->hasWrittenBinding(arp::renderer::TerrainPbrPageBindings::weightPageB));
    assert(pageSet->hasWrittenBinding(arp::renderer::TerrainPbrPageBindings::pageTable));
    assert(pageSet->hasWrittenBinding(arp::renderer::TerrainPbrPageBindings::pageTableParams));

    renderSystem.waitGpuIdle();
    const auto after = arp::rhi_vulkan::inspectVulkanRhiNativeResourceStats(*device);
    assert(after.nativeImageCreateCount >= before.nativeImageCreateCount + 3u);
    assert(after.nativeBufferCreateCount >= before.nativeBufferCreateCount + 2u);
    assert(
        after.nativeBufferToImageCopyRecordCount >= before.nativeBufferToImageCopyRecordCount + 6u);
    assert(after.nativeDescriptorWriteCount >= before.nativeDescriptorWriteCount + 4u);
    return 0;
}
