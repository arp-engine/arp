#include "renderer/terrain_pbr_page_descriptors.h"

namespace arp::renderer {
namespace {

TerrainPbrPageDescriptorResult failure(const char* diagnostics)
{
    TerrainPbrPageDescriptorResult result;
    result.diagnostics = core::pmr::String(diagnostics, &core::defaultCpuMemoryResource());
    return result;
}

bool write(rhi::DescriptorSet& descriptorSet,
    std::uint32_t binding,
    rhi::DescriptorType type,
    rhi::GpuResourceId resource,
    rhi::DescriptorResourceKind resourceKind,
    core::pmr::String& diagnostics)
{
    const auto result = descriptorSet.write({binding, type, resource, resourceKind});
    if (result.success)
        return true;
    diagnostics.assign(result.diagnostics.data(), result.diagnostics.size());
    return false;
}

} // namespace

TerrainPbrPageDescriptorResult writeTerrainPbrPageDescriptors(rhi::DescriptorSet& descriptorSet,
    const TerrainGpuRhiUploader& uploader,
    const TerrainGpuRhiUploader::PageTableSnapshot& pageTableSnapshot)
{
    if (!uploader.ready() || uploader.heightPagesView() == nullptr
        || uploader.weightPageAView() == nullptr || uploader.weightPageBView() == nullptr
        || !pageTableSnapshot.valid()) {
        return failure(
            "TERRAIN-PBR-PAGE-DESCRIPTORS-UNAVAILABLE: TerrainAsset RHI pages must be created before descriptor binding");
    }

    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    if (!write(descriptorSet,
            TerrainPbrPageBindings::weightPageA,
            rhi::DescriptorType::SampledTexture,
            uploader.weightPageAView()->id(),
            rhi::DescriptorResourceKind::TextureView,
            diagnostics)
        || !write(descriptorSet,
            TerrainPbrPageBindings::weightPageB,
            rhi::DescriptorType::SampledTexture,
            uploader.weightPageBView()->id(),
            rhi::DescriptorResourceKind::TextureView,
            diagnostics)
        || !write(descriptorSet,
            TerrainPbrPageBindings::pageTable,
            rhi::DescriptorType::ReadOnlyStorageBuffer,
            pageTableSnapshot.entries->id(),
            rhi::DescriptorResourceKind::Buffer,
            diagnostics)
        || !write(descriptorSet,
            TerrainPbrPageBindings::pageTableParams,
            rhi::DescriptorType::UniformBuffer,
            pageTableSnapshot.params->id(),
            rhi::DescriptorResourceKind::Buffer,
            diagnostics)) {
        TerrainPbrPageDescriptorResult result;
        result.diagnostics = "TERRAIN-PBR-PAGE-DESCRIPTOR-WRITE-FAILED: ";
        result.diagnostics += diagnostics;
        return result;
    }

    TerrainPbrPageDescriptorResult result;
    result.success = true;
    result.pageTableGeneration = pageTableSnapshot.generation;
    return result;
}

TerrainPbrPageDescriptorResult writeTerrainPbrPageDescriptors(
    rhi::DescriptorSet& descriptorSet, const TerrainGpuRhiUploader& uploader)
{
    return writeTerrainPbrPageDescriptors(descriptorSet, uploader, uploader.pageTableSnapshot());
}

} // namespace arp::renderer
