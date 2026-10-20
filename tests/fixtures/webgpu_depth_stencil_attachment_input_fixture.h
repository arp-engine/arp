#pragma once

#include "depth_stencil_attachment_input_fixture.h"
#include "render_pass_arguments_fixture.h"
#include "rhi/backends/webgpu/webgpu_attachment_operation_lowering.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"

#include <cstdio>
#include <cstring>

namespace arp::test {
// Diagnostic native lowering fixture only; it issues no general recording proof.
class WebGpuDepthStencilAttachmentInputFixture final {
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    template <class QueueWait, class ReadMap>
    static bool verify(QueueWait&& waitForQueue, ReadMap&& mapRead)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        DepthStencilAttachmentInputFixture::verifyContract();
        constexpr std::uint32_t width = 3u, height = 2u;
        const auto aligned = core::alignCpuAllocationOffsetUp(
            width * bytesPerPixel(TextureFormat::D32Float), kTextureUploadRowPitchAlignment);
        assert(aligned.success && aligned.alignedValue <= UINT32_MAX);
        const auto pitch = static_cast<std::uint32_t>(aligned.alignedValue);
        const BufferDesc bufferDesc{"depth clear diagnostic readback",
            pitch * height,
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu,
            ResourceState::CopyDst};
        const TextureDesc textureDesc{"depth clear diagnostic target",
            {width, height, 1u},
            TextureFormat::D32Float,
            TextureUsage::RenderAttachment | TextureUsage::CopySrc,
            MemoryUsage::GpuOnly};
        const auto textureBytes = estimateWebGpuRhiTextureBytes(textureDesc);
        const auto ledgerBytes = bufferDesc.sizeBytes + textureBytes;
        WebGpuRhiAllocator allocator(
            {"depth input diagnostic original ledger", ledgerBytes, false});
        {
            auto registry = core::makeShared<WebGpuRhiResourceRegistry>();
            auto bufferAllocation =
                allocator.allocate({bufferDesc.label, bufferDesc.sizeBytes, bufferDesc.memory});
            auto textureAllocation =
                allocator.allocate({textureDesc.label, textureBytes, textureDesc.memory});
            assert(bufferAllocation.success && textureAllocation.success);
            auto buffer = createWebGpuRhiBuffer(
                allocateGpuResourceId(), bufferDesc, bufferAllocation, {true, true}, registry);
            auto texture = createWebGpuRhiTexture(
                allocateGpuResourceId(), textureDesc, textureAllocation, {true, true}, registry);
            assert(buffer && texture);
            registry->registerBuffer(*buffer);
            registry->registerTexture(*texture);
            TextureViewDesc viewDesc{"depth clear diagnostic view",
                TextureFormat::D32Float,
                TextureUsage::RenderAttachment};
            viewDesc.aspect = TextureAspect::Depth;
            auto view = createWebGpuRhiTextureView(
                allocateGpuResourceId(), texture->id(), *texture, viewDesc, {true, true}, registry);
            assert(view);
            registry->registerTextureView(*view);
            auto nativeBuffer = webGpuNativePayloadFor(*buffer);
            auto nativeTexture = webGpuNativePayloadFor(*texture);
            auto nativeView = webGpuNativePayloadFor(*view);
            assert(nativeBuffer && nativeTexture && nativeView && nativeView->view);
            const auto device = nativeTexture->ownerDevice;
            assert(device && nativeBuffer->ownerDevice == device);
            const auto queue = wgpuDeviceGetQueue(device);
            assert(queue);
            RenderPassDepthStencilAttachmentDesc intent;
            intent.target = view->id();
            intent.format = textureDesc.format;
            intent.depthLoadOp = LoadOp::Clear;
            intent.depthStoreOp = StoreOp::Store;
            for (const auto expected : {0.125f, 0.375f, 0.875f}) {
                intent.depthClearValue = expected;
                intent.stencilClearValue = 73u;
                RenderPassDesc authored;
                authored.hasDepthStencilAttachment = true;
                authored.depthStencilAttachment = intent;
                const auto frozen = RenderPassArgumentsFixture::freezeDepthPass(authored);
                authored.depthStencilAttachment.depthClearValue = 1.0f;
                assert(!frozen->matches(authored));
                auto attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
                const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
                assert(!WebGpuAttachmentOperationLowering::depthStencil(
                    frozen->desc().depthStencilAttachment,
                    textureDesc.format,
                    nativeView->view,
                    attachment));
                assert(attachment.depthClearValue == expected
                    && attachment.depthLoadOp == WGPULoadOp_Clear
                    && attachment.depthStoreOp == WGPUStoreOp_Store && !attachment.depthReadOnly
                    && attachment.stencilLoadOp == WGPULoadOp_Undefined
                    && attachment.stencilStoreOp == WGPUStoreOp_Undefined
                    && !attachment.stencilClearValue && attachment.stencilReadOnly);
                const auto original = attachment;
                const auto reject = [&](const char* id) {
                    assert(std::string_view(WebGpuAttachmentOperationLowering::depthStencil(
                               intent, textureDesc.format, nativeView->view, attachment))
                        == id);
                    assert(std::memcmp(&original, &attachment, sizeof(original)) == 0);
                };
                intent.depthReadOnly = true;
                reject("RHI-RENDER-PASS-DEPTH-STENCIL-READ-ONLY-UNPROVEN");
                intent.depthReadOnly = false;
                intent.stencilReadOnly = true;
                reject("RHI-RENDER-PASS-DEPTH-STENCIL-READ-ONLY-UNPROVEN");
                intent.stencilReadOnly = false;
                intent.depthClearValue = std::numeric_limits<float>::quiet_NaN();
                reject("RHI-RENDER-PASS-DEPTH-CLEAR-INVALID");
                intent.depthClearValue = expected;
                intent.stencilClearValue = 256u;
                reject("RHI-RENDER-PASS-STENCIL-CLEAR-INVALID");
                intent.stencilClearValue = 73u;
                assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
                auto encoderDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
                const auto encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
                assert(encoder);
                auto passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
                passDesc.depthStencilAttachment = &attachment;
                const auto pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
                assert(pass);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
                auto source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
                source.texture = nativeTexture->texture;
                source.aspect = WGPUTextureAspect_DepthOnly;
                auto destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
                destination.buffer = nativeBuffer->buffer;
                destination.layout.bytesPerRow = pitch;
                destination.layout.rowsPerImage = height;
                const WGPUExtent3D extent{width, height, 1u};
                wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);
                auto commandDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
                const auto commands = wgpuCommandEncoderFinish(encoder, &commandDesc);
                assert(commands);
                wgpuCommandEncoderRelease(encoder);
                wgpuQueueSubmit(queue, 1u, &commands);
                wgpuCommandBufferRelease(commands);
                assert(waitForQueue(queue) && mapRead(nativeBuffer->buffer, bufferDesc.sizeBytes));
                const auto* bytes = static_cast<const std::uint8_t*>(
                    wgpuBufferGetConstMappedRange(nativeBuffer->buffer, 0u, bufferDesc.sizeBytes));
                assert(bytes);
                for (unsigned y = 0u; y < height; ++y)
                    for (unsigned x = 0u; x < width; ++x) {
                        float actual = 0.0f;
                        std::memcpy(
                            &actual, bytes + y * pitch + x * sizeof(actual), sizeof(actual));
                        assert(actual == expected);
                    }
                wgpuBufferUnmap(nativeBuffer->buffer);
            }
            wgpuQueueRelease(queue);
        }
        assert(
            !allocator.memoryStats().allocatedBytes && !allocator.memoryStats().liveResourceCount);
        std::puts("BINDLESS_RHI_DEPTH_STENCIL_INPUT_NATIVE_OK 0.125 0.375 0.875 allocation0");
        std::puts("BINDLESS_RHI_FROZEN_PASS_NATIVE_DEPTH_OK 0.125 0.375 0.875");
        return true;
    }
#endif
};
} // namespace arp::test
