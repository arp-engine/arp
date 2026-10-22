#include "core/memory/cpu_allocator.h"
#include "rhi/semantics/native_resource_lowering.h"

#include <cassert>

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    BufferDesc vertexUpload;
    vertexUpload.label = "StaticMeshVertices";
    vertexUpload.sizeBytes = 4096;
    vertexUpload.usage = ResourceUsage::VertexBuffer | ResourceUsage::CopyDst;
    vertexUpload.memory = MemoryUsage::GpuOnly;

    const auto vulkan = nativeBackendCapabilityContract(Backend::Vulkan);
    const auto vulkanVertex = lowerBufferResource(vulkan, vertexUpload);
    assert(vulkanVertex.success);
    assert(vulkanVertex.backend == Backend::Vulkan);
    assert(vulkanVertex.memoryClass == ResourceMemoryClass::DeviceLocal);
    assert(vulkanVertex.preservesNativePotential);
    assert(!vulkanVertex.nativeHandleExposedToUpperLayer);
    assert(
        hasNativeResourceSemantic(vulkanVertex, NativeResourceSemantic::VulkanDeviceLocalMemory));
    assert(
        hasNativeResourceSemantic(vulkanVertex, NativeResourceSemantic::VulkanBufferVertexUsage));
    assert(hasNativeResourceSemantic(
        vulkanVertex, NativeResourceSemantic::VulkanBufferTransferDstUsage));

    BufferDesc readback;
    readback.label = "Readback";
    readback.sizeBytes = 1024;
    readback.usage = ResourceUsage::CopyDst;
    readback.memory = MemoryUsage::GpuToCpu;

    const auto vulkanReadback = lowerBufferResource(vulkan, readback);
    assert(vulkanReadback.success);
    assert(vulkanReadback.memoryClass == ResourceMemoryClass::Readback);
    assert(
        hasNativeResourceSemantic(vulkanReadback, NativeResourceSemantic::VulkanHostVisibleMemory));
    assert(
        hasNativeResourceSemantic(vulkanReadback, NativeResourceSemantic::VulkanHostCachedMemory));

    TextureDesc transientColor;
    transientColor.label = "TransientSceneColor";
    transientColor.extent = {320, 180, 1};
    transientColor.format = TextureFormat::Bgra8Unorm;
    transientColor.usage = TextureUsage::RenderAttachment | TextureUsage::Sampled;
    transientColor.memory = MemoryUsage::Transient;

    const auto vulkanTransientColor = lowerTextureResource(vulkan, transientColor);
    assert(vulkanTransientColor.success);
    assert(vulkanTransientColor.memoryClass == ResourceMemoryClass::Transient);
    assert(hasNativeResourceSemantic(
        vulkanTransientColor, NativeResourceSemantic::VulkanImageColorAttachmentUsage));
    assert(hasNativeResourceSemantic(
        vulkanTransientColor, NativeResourceSemantic::VulkanImageSampledUsage));
    assert(hasNativeResourceSemantic(
        vulkanTransientColor, NativeResourceSemantic::VulkanTransientAliasingMemory));

    TextureDesc depthStencilTarget;
    depthStencilTarget.label = "DepthStencilTarget";
    depthStencilTarget.extent = {320, 180, 1};
    depthStencilTarget.format = TextureFormat::Depth24Stencil8;
    depthStencilTarget.usage =
        TextureUsage::RenderAttachment | TextureUsage::Sampled | TextureUsage::CopySrc;
    depthStencilTarget.memory = MemoryUsage::GpuOnly;

    const auto vulkanDepthStencil = lowerTextureResource(vulkan, depthStencilTarget);
    assert(vulkanDepthStencil.success);
    assert(hasNativeResourceSemantic(
        vulkanDepthStencil, NativeResourceSemantic::VulkanImageDepthStencilAttachmentUsage));
    assert(!hasNativeResourceSemantic(
        vulkanDepthStencil, NativeResourceSemantic::VulkanImageColorAttachmentUsage));
    assert(hasNativeResourceSemantic(
        vulkanDepthStencil, NativeResourceSemantic::VulkanImageSampledUsage));
    assert(hasNativeResourceSemantic(
        vulkanDepthStencil, NativeResourceSemantic::VulkanImageTransferSrcUsage));

    TextureViewDesc depthView;
    depthView.label = "DepthAspectView";
    depthView.format = TextureFormat::Depth24Stencil8;
    depthView.usage = TextureUsage::Sampled;
    depthView.aspect = TextureAspect::Depth;
    const auto vulkanDepthView = lowerTextureViewResource(vulkan, depthStencilTarget, depthView);
    assert(vulkanDepthView.success);
    assert(hasNativeResourceSemantic(vulkanDepthView, NativeResourceSemantic::VulkanImageView));
    assert(hasNativeResourceSemantic(
        vulkanDepthView, NativeResourceSemantic::VulkanImageSubresourceRange));

    TextureViewDesc stencilView = depthView;
    stencilView.label = "StencilAspectView";
    stencilView.aspect = TextureAspect::Stencil;
    const auto vulkanStencilView =
        lowerTextureViewResource(vulkan, depthStencilTarget, stencilView);
    assert(vulkanStencilView.success);
    assert(hasNativeResourceSemantic(vulkanStencilView, NativeResourceSemantic::VulkanImageView));
    assert(hasNativeResourceSemantic(
        vulkanStencilView, NativeResourceSemantic::VulkanImageSubresourceRange));

    const auto metal = nativeBackendCapabilityContract(Backend::Metal);
    const auto metalColor = lowerTextureResource(metal, transientColor);
    assert(metalColor.success);
    assert(metalColor.backend == Backend::Metal);
    assert(metalColor.memoryClass == ResourceMemoryClass::Transient);
    assert(hasNativeResourceSemantic(metalColor, NativeResourceSemantic::MetalPrivateStorage));
    assert(hasNativeResourceSemantic(
        metalColor, NativeResourceSemantic::MetalMemorylessOrTransientStorage));
    assert(hasNativeResourceSemantic(
        metalColor, NativeResourceSemantic::MetalTextureRenderTargetUsage));

    BufferDesc metalUploadDesc;
    metalUploadDesc.label = "MaterialUpload";
    metalUploadDesc.sizeBytes = 256;
    metalUploadDesc.usage = ResourceUsage::UniformBuffer | ResourceUsage::CopyDst;
    metalUploadDesc.memory = MemoryUsage::CpuToGpu;

    const auto metalUpload = lowerBufferResource(metal, metalUploadDesc);
    assert(metalUpload.success);
    assert(metalUpload.memoryClass == ResourceMemoryClass::Upload);
    assert(hasNativeResourceSemantic(metalUpload, NativeResourceSemantic::MetalSharedStorage));
    assert(hasNativeResourceSemantic(metalUpload, NativeResourceSemantic::MetalBufferUniformUsage));
    assert(hasNativeResourceSemantic(
        metalUpload, NativeResourceSemantic::MetalBufferTransferDstUsage));

    BufferDesc metalReadbackDesc;
    metalReadbackDesc.label = "ReadbackSource";
    metalReadbackDesc.sizeBytes = 512;
    metalReadbackDesc.usage = ResourceUsage::CopySrc;
    metalReadbackDesc.memory = MemoryUsage::GpuToCpu;

    const auto metalReadback = lowerBufferResource(metal, metalReadbackDesc);
    assert(metalReadback.success);
    assert(metalReadback.memoryClass == ResourceMemoryClass::Readback);
    assert(hasNativeResourceSemantic(metalReadback, NativeResourceSemantic::MetalManagedStorage));
    assert(hasNativeResourceSemantic(
        metalReadback, NativeResourceSemantic::MetalBufferTransferSrcUsage));

    TextureDesc metalTransferTexture;
    metalTransferTexture.label = "MetalTransferTexture";
    metalTransferTexture.extent = {64, 64, 1};
    metalTransferTexture.format = TextureFormat::Rgba8Unorm;
    metalTransferTexture.usage =
        TextureUsage::Sampled | TextureUsage::CopySrc | TextureUsage::CopyDst;
    metalTransferTexture.memory = MemoryUsage::GpuOnly;

    const auto metalTransfer = lowerTextureResource(metal, metalTransferTexture);
    assert(metalTransfer.success);
    assert(hasNativeResourceSemantic(
        metalTransfer, NativeResourceSemantic::MetalTextureShaderReadUsage));
    assert(hasNativeResourceSemantic(
        metalTransfer, NativeResourceSemantic::MetalTextureTransferSrcUsage));
    assert(hasNativeResourceSemantic(
        metalTransfer, NativeResourceSemantic::MetalTextureTransferDstUsage));

    const auto nullBackend = nativeBackendCapabilityContract(Backend::Null);
    const auto nullVertex = lowerBufferResource(nullBackend, vertexUpload);
    assert(nullVertex.success);
    assert(nullVertex.backend == Backend::Null);
    assert(nullVertex.memoryClass == ResourceMemoryClass::DeviceLocal);
    assert(nullVertex.fallbackMode == FallbackMode::Native);
    assert(hasNativeResourceSemantic(nullVertex, NativeResourceSemantic::NullRecordedResource));
    assert(!nullVertex.nativeHandleExposedToUpperLayer);

    const auto webgpu = nativeBackendCapabilityContract(Backend::WebGPU);
    BufferDesc webgpuUniformDesc;
    webgpuUniformDesc.label = "FrameUniforms";
    webgpuUniformDesc.sizeBytes = 256;
    webgpuUniformDesc.usage = ResourceUsage::UniformBuffer | ResourceUsage::CopyDst;
    webgpuUniformDesc.memory = MemoryUsage::GpuOnly;

    const auto webgpuUniform = lowerBufferResource(webgpu, webgpuUniformDesc);
    assert(webgpuUniform.success);
    assert(webgpuUniform.backend == Backend::WebGPU);
    assert(webgpuUniform.memoryClass == ResourceMemoryClass::DeviceLocal);
    assert(webgpuUniform.fallbackMode == FallbackMode::Native);
    assert(!webgpuUniform.nativeHandleExposedToUpperLayer);
    assert(
        hasNativeResourceSemantic(webgpuUniform, NativeResourceSemantic::WebGpuBufferUniformUsage));
    assert(
        hasNativeResourceSemantic(webgpuUniform, NativeResourceSemantic::WebGpuBufferCopyDstUsage));

    BufferDesc webgpuUploadDesc;
    webgpuUploadDesc.label = "MaterialUpload";
    webgpuUploadDesc.sizeBytes = 1024;
    webgpuUploadDesc.usage = ResourceUsage::UniformBuffer | ResourceUsage::CopyDst;
    webgpuUploadDesc.memory = MemoryUsage::CpuToGpu;

    const auto webgpuUpload = lowerBufferResource(webgpu, webgpuUploadDesc);
    assert(webgpuUpload.success);
    assert(webgpuUpload.memoryClass == ResourceMemoryClass::Upload);
    assert(webgpuUpload.fallbackMode == FallbackMode::Emulated);
    assert(hasNativeResourceSemantic(webgpuUpload, NativeResourceSemantic::WebGpuQueueWriteUpload));
    assert(
        hasNativeResourceSemantic(webgpuUpload, NativeResourceSemantic::WebGpuBufferUniformUsage));

    const auto webgpuTransientColor = lowerTextureResource(webgpu, transientColor);
    assert(webgpuTransientColor.success);
    assert(webgpuTransientColor.backend == Backend::WebGPU);
    assert(webgpuTransientColor.memoryClass == ResourceMemoryClass::Transient);
    assert(webgpuTransientColor.fallbackMode == FallbackMode::Degraded);
    assert(!webgpuTransientColor.nativeHandleExposedToUpperLayer);
    assert(hasNativeResourceSemantic(
        webgpuTransientColor, NativeResourceSemantic::WebGpuTextureBindingUsage));
    assert(hasNativeResourceSemantic(
        webgpuTransientColor, NativeResourceSemantic::WebGpuTextureRenderAttachmentUsage));

    BufferDesc invalid;
    invalid.label = "Invalid";
    invalid.sizeBytes = 0;
    invalid.usage = ResourceUsage::UniformBuffer;
    invalid.memory = MemoryUsage::GpuOnly;
    const auto invalidDecision = lowerBufferResource(vulkan, invalid);
    assert(!invalidDecision.success);
    assert(invalidDecision.diagnostics.find("buffer") != std::pmr::string::npos);

    return 0;
}
