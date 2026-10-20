#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_binding_space_lowering.h"
#include "rhi/gpu_pipeline.h"
#include "tests/fixtures/sampled_depth_descriptor_contract.h"
#if defined(ARP_TEST_DEPTH_METAL)
#include "rhi/backends/metal/metal_device.h"
#endif
#include <cstdio>

namespace {
// This exercises WebGPU's descriptor validator without claiming browser/native
// execution. Explicit metadata is registered in the backend contract fixture.
class FixtureView final : public arp::rhi::GpuTextureView {
public:
    FixtureView(arp::rhi::GpuResourceId parent,
        arp::rhi::TextureFormat format,
        arp::rhi::TextureAspect aspect,
        arp::rhi::TextureViewDimension dimension = arp::rhi::TextureViewDimension::Texture2D,
        arp::rhi::TextureUsage usage = arp::rhi::TextureUsage::Sampled)
        : id_{arp::rhi::allocateGpuResourceId()}, parent_(parent)
    {
        desc_.format = format;
        desc_.aspect = aspect;
        desc_.usage = usage;
        desc_.dimension = dimension;
    }

    arp::rhi::GpuResourceId id() const override { return id_; }

    arp::rhi::GpuResourceId textureId() const override { return parent_; }

    const arp::rhi::TextureViewDesc& desc() const override { return desc_; }

private:
    arp::rhi::GpuResourceId id_;
    arp::rhi::GpuResourceId parent_;
    arp::rhi::TextureViewDesc desc_;
};
} // namespace

int main()
{
    using namespace arp::rhi;
    assert(validateSampledDepthView(true, TextureFormat::D32Float, TextureAspect::Depth).success);
    assert(!validateSampledDepthView(true, TextureFormat::R32Float, TextureAspect::Color).success);
    assert(!validateSampledDepthView(true, TextureFormat::D32Float, TextureAspect::Color).success);
    assert(
        !validateSampledDepthView(true, TextureFormat::D32Float, TextureAspect::Stencil).success);
    assert(!validateSampledDepthView(true, TextureFormat::Unknown, TextureAspect::Depth).success);
    assert(
        validateSampledDepthView(false, TextureFormat::Rgba8Unorm, TextureAspect::Color).success);
    DescriptorLayoutDesc layout;
    layout.name = "InvalidDepthRequirement";
    layout.bindings.push_back({0, DescriptorType::UniformBuffer, ShaderStage::Fragment});
    layout.bindings[0].depthTexture = true;
    assert(!validateDescriptorLayout(layout).success);
    PipelineLayoutDesc tableLayout;
    tableLayout.descriptorTables.push_back(
        {0u, "DepthTable", DescriptorType::SampledTexture, ShaderStage::Fragment, 3u});
    auto reflected = makeShaderReflectionBinding(
        0u, {0u, DescriptorType::SampledTexture, ShaderStage::Fragment, 3u});
    reflected.depthTexture = true;
    assert(!validateDescriptorTableBindingAgainstReflection(tableLayout, reflected).success);
    tableLayout.descriptorTables[0].depthTexture = true;
    assert(validateDescriptorTableBindingAgainstReflection(tableLayout, reflected).success);
    DescriptorTableDesc tableDesc{
        "DepthTable", DescriptorType::SampledTexture, ShaderStage::Fragment, 3u};
    assert(!validateDescriptorTableSlot(tableLayout, 0u, tableDesc).success);
    tableDesc.depthTexture = true;
    assert(validateDescriptorTableSlot(tableLayout, 0u, tableDesc).success);
    tableLayout.descriptorTables[0].textureViewDimensionConstrained = true;
    assert(!validateDescriptorTableSlot(tableLayout, 0u, tableDesc).success);
    tableDesc.textureViewDimensionConstrained = true;
    assert(validateDescriptorTableSlot(tableLayout, 0u, tableDesc).success);
    tableDesc.textureViewDimension = TextureViewDimension::Texture2DArray;
    assert(!validateDescriptorTableSlot(tableLayout, 0u, tableDesc).success);
    const auto depthKey = stablePipelineLayoutKey(tableLayout);
    tableLayout.descriptorTables[0].depthTexture = false;
    assert(stablePipelineLayoutKey(tableLayout) != depthKey);
    tableLayout.descriptorTables[0].depthTexture = true;
    tableLayout.descriptorTables[0].textureViewDimension = TextureViewDimension::Texture2DArray;
    assert(stablePipelineLayoutKey(tableLayout) != depthKey);
    tableLayout.descriptorTables[0].type = DescriptorType::Sampler;
    assert(!validatePipelineLayout(tableLayout).success);
    tableLayout.descriptorTables[0].type = DescriptorType::SampledTexture;
    tableLayout.descriptorTables[0].textureViewDimension = TextureViewDimension::Texture2D;
    ShaderReflectionDesc depthReflection;
    depthReflection.descriptorBindings.push_back(reflected);
    arp::rhi_webgpu::WebGpuBindingSpaceLowering lowering;
    lowering.contractVersion = arp::rhi_webgpu::kWebGpuBindingSpaceContractVersion;
    lowering.physicalGroupCount = 1u;
    lowering.artifactBindings = depthReflection.descriptorBindings;
    arp::rhi_webgpu::WebGpuBindingSpaceLoweringEntry entry;
    entry.name = "Inputs";
    entry.active = true;
    entry.capacity = 3u;
    entry.physicalType = DescriptorType::SampledTexture;
    entry.depthTexture = true;
    lowering.entries.push_back(entry);
    const auto validateLowering = [&]() {
        return arp::rhi_webgpu::validateWebGpuBindingSpaceLowering(
            tableLayout, depthReflection, lowering, UINT32_MAX);
    };
    assert(validateLowering().success);
    lowering.entries[0].depthTexture = false;
    assert(!validateLowering().success);
    lowering.entries[0].depthTexture = true;
    tableLayout.descriptorTables[0].depthTexture = false;
    assert(!validateLowering().success);
    auto nullDevice = createNullDevice();
    assert(nullDevice);
    arp::tests::verifySampledDepthDescriptors(*nullDevice);
    std::puts("SampledDepthView/1 Null: PASS");
    auto registry = arp::core::makeShared<arp::rhi_webgpu::WebGpuRhiResourceRegistry>();
    arp::rhi_webgpu::WebGpuRhiAllocation allocation;
    allocation.success = true;
    TextureDesc parentDesc{"DepthParent",
        {8u, 8u, 1u},
        TextureFormat::D32Float,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead};
    auto depthParent = arp::rhi_webgpu::createWebGpuRhiTexture(allocateGpuResourceId(),
        parentDesc,
        allocation,
        arp::rhi_webgpu::WebGpuRhiNativePayloadPolicy{},
        registry);
    parentDesc.format = TextureFormat::Rgba8Unorm;
    auto colorParent = arp::rhi_webgpu::createWebGpuRhiTexture(allocateGpuResourceId(),
        parentDesc,
        allocation,
        arp::rhi_webgpu::WebGpuRhiNativePayloadPolicy{},
        registry);
    assert(depthParent && colorParent);
    registry->registerTexture(*depthParent);
    registry->registerTexture(*colorParent);
    FixtureView color(colorParent->id(), TextureFormat::Rgba8Unorm, TextureAspect::Color);
    FixtureView depth(depthParent->id(), TextureFormat::D32Float, TextureAspect::Depth);
    FixtureView stencil(depthParent->id(), TextureFormat::D32Float, TextureAspect::Stencil);
    FixtureView dimension(depthParent->id(),
        TextureFormat::D32Float,
        TextureAspect::Depth,
        TextureViewDimension::Texture2DArray);
    FixtureView usage(depthParent->id(),
        TextureFormat::D32Float,
        TextureAspect::Depth,
        TextureViewDimension::Texture2D,
        TextureUsage::RenderAttachment);
    registry->registerTextureView(color);
    registry->registerTextureView(depth);
    registry->registerTextureView(stencil);
    registry->registerTextureView(dimension);
    registry->registerTextureView(usage);
    auto webArena = arp::rhi_webgpu::createWebGpuRhiDescriptorArena(
        {"WebDepthContractArena", 1, 3}, registry, {});
    assert(webArena);
    arp::tests::verifySampledDepthWrites(*webArena, color, depth);
    arp::tests::verifySampledDepthWrites(*webArena, stencil, depth);
    arp::tests::verifySampledDepthTableWrites(*webArena, color, depth);
    arp::tests::verifySampledDepthTableWrites(*webArena, stencil, depth);
    arp::tests::verifySampledDepthTableWrites(*webArena, dimension, depth);
    arp::tests::verifySampledDepthTableWrites(*webArena, usage, depth);
    std::puts("SampledDepthView/1 WebGPU descriptor contract (no native runtime): PASS");
#if defined(ARP_TEST_DEPTH_METAL)
    arp::rhi_metal::MetalRhiDeviceDesc desc;
    desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
    desc.capabilityContract.queriedFromNativeDevice = true;
    desc.capabilityContract.queriedExtendedFeatures = true;
    desc.memoryBudgetBytes = 1024u * 1024u;
    desc.uploadStagingPageSizeBytes = 4096u;
    desc.uploadStagingMaxPageCount = 1u;
    auto metalDevice = arp::rhi_metal::createMetalRhiDevice(desc);
    assert(metalDevice);
    arp::tests::verifySampledDepthDescriptors(*metalDevice);
    std::puts("SampledDepthView/1 native Metal resources: PASS");
#endif
}
