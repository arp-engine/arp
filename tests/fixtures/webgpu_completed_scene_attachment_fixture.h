#pragma once
#include "bindless_scene_attachment_fixture.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_native_buffer_map_receipt.h"
#include "webgpu_async_wait_fixture.h"

namespace arp::test {
// Independent output observation only, after the public scene journal commits.
// Genuine copy/map completion retains all diagnostic objects until map drain.
// This grants no public texture-readback/lifecycle capability.
class WebGpuCompletedSceneAttachmentFixture final {
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void observe(const rhi::GpuTexture& texture,
        const char* entry,
        bool terrain,
        const void* geometry,
        bool fault = false)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        const auto native = webGpuNativePayloadFor(texture);
        assert(native && native->texture && native->ownerDevice);
        const auto& desc = texture.desc();
        const bool depth = desc.format == TextureFormat::D32Float;
        assert(desc.extent == (Extent3D{2u, 2u, 1u}) && desc.mipLevelCount == 1u
            && desc.arrayLayerCount == 1u && desc.sampleCount == 1u
            && (desc.usage & TextureUsage::CopySrc) != TextureUsage::None);
        assert(depth || desc.format == TextureFormat::Rgba16Float);
        const auto committed =
            texture.observeOriginalCommittedState(defaultTextureSubresourceRange(desc));
        assert(committed
            && committed->state()
                == (depth ? ResourceState::DepthAttachment : ResourceState::ColorAttachment));
        constexpr auto pitch = kTextureUploadRowPitchAlignment;
        constexpr auto total = 2u * pitch;
        const auto queue = wgpuDeviceGetQueue(native->ownerDevice);
        auto bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.size = total;
        bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        const auto buffer = wgpuDeviceCreateBuffer(native->ownerDevice, &bufferDesc);
        const auto encoder = wgpuDeviceCreateCommandEncoder(native->ownerDevice, nullptr);
        assert(queue && buffer && encoder);
        auto source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        source.texture = native->texture;
        source.aspect = depth ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;
        auto destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        destination.buffer = buffer;
        destination.layout.bytesPerRow = pitch;
        destination.layout.rowsPerImage = 2u;
        const WGPUExtent3D extent{2u, 2u, 1u};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);
        const auto command = wgpuCommandEncoderFinish(encoder, nullptr);
        assert(command);
        wgpuQueueSubmit(queue, 1u, &command);
        WebGpuNativeBufferMapReceipt receipt;
        bool delivered = false;
        assert(!receipt.prepareCold(buffer,
            total,
            &delivered,
            [](void* context, WebGpuNativeBufferMapReceipt::Outcome outcome) noexcept {
                assert(outcome == WebGpuNativeBufferMapReceipt::Outcome::Success);
                *static_cast<bool*>(context) = true;
            }));
        assert(!receipt.start());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return delivered;
        }));
        std::array<std::byte, total> padded{};
        assert(!receipt.readAndUnmap(padded.data(), padded.size()));
        std::array<std::byte, 32u> packed{};
        const auto rowBytes = depth ? 8u : 16u;
        for (std::size_t row = 0u; row < 2u; ++row)
            std::memcpy(packed.data() + row * rowBytes, padded.data() + row * pitch, rowBytes);
        BindlessSceneAttachmentFixture::inspectAndReport(
            texture, entry, terrain, geometry, packed.data(), 2u * rowBytes, fault);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        wgpuBufferRelease(buffer);
        wgpuQueueRelease(queue);
    }
#endif
};
} // namespace arp::test
