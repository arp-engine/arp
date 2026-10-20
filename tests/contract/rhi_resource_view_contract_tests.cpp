#include "core/cpu_allocator.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/semantics/native_resource_lowering.h"

#include <cassert>

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    auto device = createNullDevice();
    assert(device);

    TextureDesc sceneColorDesc;
    sceneColorDesc.label = "SceneColorArray";
    sceneColorDesc.extent = {256, 128, 1};
    sceneColorDesc.format = TextureFormat::Rgba8Unorm;
    sceneColorDesc.usage = TextureUsage::RenderAttachment | TextureUsage::Sampled;
    sceneColorDesc.memory = MemoryUsage::GpuOnly;
    sceneColorDesc.initialState = ResourceState::Undefined;
    sceneColorDesc.mipLevelCount = 4;
    sceneColorDesc.arrayLayerCount = 6;

    auto sceneColor = device->createTexture(sceneColorDesc);
    assert(sceneColor);
    assert(sceneColor->state() == ResourceState::Undefined);

    TextureViewDesc sceneColorViewDesc;
    sceneColorViewDesc.label = "SceneColorMipSlice";
    sceneColorViewDesc.format = TextureFormat::Rgba8Unorm;
    sceneColorViewDesc.usage = TextureUsage::Sampled;
    sceneColorViewDesc.dimension = TextureViewDimension::Texture2DArray;
    sceneColorViewDesc.baseMipLevel = 1;
    sceneColorViewDesc.mipLevelCount = 2;
    sceneColorViewDesc.baseArrayLayer = 2;
    sceneColorViewDesc.arrayLayerCount = 3;
    sceneColorViewDesc.aspect = TextureAspect::Color;

    auto sceneColorView = device->createTextureView(*sceneColor, sceneColorViewDesc);
    assert(sceneColorView);
    assert(sceneColorView->textureId() == sceneColor->id());
    assert(sceneColorView->desc().baseMipLevel == 1u);
    assert(sceneColorView->desc().mipLevelCount == 2u);
    assert(sceneColorView->desc().baseArrayLayer == 2u);
    assert(sceneColorView->desc().arrayLayerCount == 3u);
    assert(sceneColorView->desc().usage == TextureUsage::Sampled);

    TextureDesc cubePlacementDesc;
    cubePlacementDesc.extent = {2u, 2u, 1u};
    cubePlacementDesc.format = TextureFormat::Rgba8Unorm;
    cubePlacementDesc.arrayLayerCount = 6u;
    cubePlacementDesc.cubeCompatible = true;
    const auto cubePlacement = estimateTexturePlacementRequirements(cubePlacementDesc);
    assert(cubePlacement.sizeBytes == 2u * 2u * 4u * 6u);

    TextureViewDesc outOfRangeView = sceneColorViewDesc;
    outOfRangeView.baseMipLevel = 3;
    outOfRangeView.mipLevelCount = 2;
    assert(!device->createTextureView(*sceneColor, outOfRangeView));

    TextureViewDesc wrongAspectView = sceneColorViewDesc;
    wrongAspectView.aspect = TextureAspect::Depth;
    assert(!device->createTextureView(*sceneColor, wrongAspectView));

    TextureViewDesc wrongUsageView = sceneColorViewDesc;
    wrongUsageView.usage = TextureUsage::Storage;
    assert(!device->createTextureView(*sceneColor, wrongUsageView));

    auto depthTexture = device->createTexture({
        "DepthTexture",
        {128, 128, 1},
        TextureFormat::D32Float,
        TextureUsage::RenderAttachment | TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
        1,
        1,
    });
    assert(depthTexture);
    TextureViewDesc depthViewDesc;
    depthViewDesc.label = "DepthView";
    depthViewDesc.format = TextureFormat::D32Float;
    depthViewDesc.usage = TextureUsage::Sampled;
    depthViewDesc.aspect = TextureAspect::Depth;
    assert(device->createTextureView(*depthTexture, depthViewDesc));

    TextureDesc depthTextureDesc;
    depthTextureDesc.label = "DepthTexture";
    depthTextureDesc.extent = {128, 128, 1};
    depthTextureDesc.format = TextureFormat::D32Float;
    depthTextureDesc.usage = TextureUsage::RenderAttachment | TextureUsage::Sampled;
    depthTextureDesc.memory = MemoryUsage::GpuOnly;
    depthTextureDesc.initialState = ResourceState::Undefined;
    depthTextureDesc.mipLevelCount = 1;
    depthTextureDesc.arrayLayerCount = 1;

    BufferDesc uniformBufferDesc;
    uniformBufferDesc.label = "FrameUniforms";
    uniformBufferDesc.sizeBytes = 1024;
    uniformBufferDesc.usage = ResourceUsage::UniformBuffer | ResourceUsage::StorageBuffer;
    uniformBufferDesc.memory = MemoryUsage::GpuOnly;
    auto uniformBuffer = device->createBuffer(uniformBufferDesc);
    assert(uniformBuffer);

    BufferViewDesc uniformViewDesc;
    uniformViewDesc.label = "FrameUniformsSlice";
    uniformViewDesc.offsetBytes = 128;
    uniformViewDesc.sizeBytes = 256;
    uniformViewDesc.strideBytes = 16;
    uniformViewDesc.usage = ResourceUsage::UniformBuffer;
    auto uniformView = device->createBufferView(*uniformBuffer, uniformViewDesc);
    assert(uniformView);
    assert(uniformView->bufferId() == uniformBuffer->id());
    assert(uniformView->desc().offsetBytes == 128u);
    assert(uniformView->desc().sizeBytes == 256u);
    assert(uniformView->desc().strideBytes == 16u);

    BufferViewDesc oversizedBufferView = uniformViewDesc;
    oversizedBufferView.offsetBytes = 900;
    oversizedBufferView.sizeBytes = 256;
    assert(!device->createBufferView(*uniformBuffer, oversizedBufferView));

    BufferViewDesc wrongBufferUsage = uniformViewDesc;
    wrongBufferUsage.usage = ResourceUsage::VertexBuffer;
    assert(!device->createBufferView(*uniformBuffer, wrongBufferUsage));

    TextureSubresourceRange mipLayerRange;
    mipLayerRange.baseMipLevel = 1;
    mipLayerRange.mipLevelCount = 1;
    mipLayerRange.baseArrayLayer = 2;
    mipLayerRange.arrayLayerCount = 1;
    mipLayerRange.aspect = TextureAspect::Color;

    auto encoder = device->createCommandEncoder({"SubresourceTransitions"});
    const auto subresourceBarrier = encoder->transition(
        *sceneColor, mipLayerRange, ResourceState::Undefined, ResourceState::ShaderRead);
    assert(subresourceBarrier.success);
    assert(sceneColor->state() == ResourceState::Undefined);
    assert(sceneColor->subresourceState(mipLayerRange) == ResourceState::ShaderRead);

    const auto mismatchedSubresourceBarrier = encoder->transition(
        *sceneColor, mipLayerRange, ResourceState::Undefined, ResourceState::ColorAttachment);
    assert(!mismatchedSubresourceBarrier.success);
    assert(mismatchedSubresourceBarrier.diagnostics.find("subresource") != std::pmr::string::npos);

    const auto commandBuffer = encoder->finish();
    assert(commandBuffer);
    assert(commandBuffer->commandCount() == 1u);

    const auto vulkan = nativeBackendCapabilityContract(Backend::Vulkan);
    const auto vulkanTextureView =
        lowerTextureViewResource(vulkan, sceneColorDesc, sceneColorViewDesc);
    assert(vulkanTextureView.success);
    assert(!vulkanTextureView.nativeHandleExposedToUpperLayer);
    assert(hasNativeResourceSemantic(vulkanTextureView, NativeResourceSemantic::VulkanImageView));
    assert(hasNativeResourceSemantic(
        vulkanTextureView, NativeResourceSemantic::VulkanImageSubresourceRange));

    const auto metal = nativeBackendCapabilityContract(Backend::Metal);
    const auto metalTextureView =
        lowerTextureViewResource(metal, sceneColorDesc, sceneColorViewDesc);
    assert(metalTextureView.success);
    assert(hasNativeResourceSemantic(metalTextureView, NativeResourceSemantic::MetalTextureView));

    const auto webgpu = nativeBackendCapabilityContract(Backend::WebGPU);
    const auto webgpuTextureView =
        lowerTextureViewResource(webgpu, sceneColorDesc, sceneColorViewDesc);
    assert(webgpuTextureView.success);
    assert(hasNativeResourceSemantic(webgpuTextureView, NativeResourceSemantic::WebGpuTextureView));

    const auto vulkanDepthTextureView =
        lowerTextureViewResource(vulkan, depthTextureDesc, depthViewDesc);
    assert(vulkanDepthTextureView.success);
    assert(
        hasNativeResourceSemantic(vulkanDepthTextureView, NativeResourceSemantic::VulkanImageView));

    const auto metalDepthTextureView =
        lowerTextureViewResource(metal, depthTextureDesc, depthViewDesc);
    assert(metalDepthTextureView.success);
    assert(
        hasNativeResourceSemantic(metalDepthTextureView, NativeResourceSemantic::MetalTextureView));

    const auto webgpuDepthTextureView =
        lowerTextureViewResource(webgpu, depthTextureDesc, depthViewDesc);
    assert(webgpuDepthTextureView.success);
    assert(hasNativeResourceSemantic(
        webgpuDepthTextureView, NativeResourceSemantic::WebGpuTextureView));

    const auto nullBackend = nativeBackendCapabilityContract(Backend::Null);
    const auto nullDepthTextureView =
        lowerTextureViewResource(nullBackend, depthTextureDesc, depthViewDesc);
    assert(nullDepthTextureView.success);
    assert(hasNativeResourceSemantic(
        nullDepthTextureView, NativeResourceSemantic::NullRecordedTextureView));

    const auto nullBufferView =
        lowerBufferViewResource(nullBackend, uniformBufferDesc, uniformViewDesc);
    assert(nullBufferView.success);
    assert(
        hasNativeResourceSemantic(nullBufferView, NativeResourceSemantic::NullRecordedBufferView));

    const auto vulkanBufferView =
        lowerBufferViewResource(vulkan, uniformBufferDesc, uniformViewDesc);
    assert(vulkanBufferView.success);
    assert(hasNativeResourceSemantic(vulkanBufferView, NativeResourceSemantic::VulkanBufferView));

    const auto invalidLowering = lowerTextureViewResource(vulkan, sceneColorDesc, outOfRangeView);
    assert(!invalidLowering.success);
    assert(invalidLowering.diagnostics.find("view") != std::pmr::string::npos);

    return 0;
}
