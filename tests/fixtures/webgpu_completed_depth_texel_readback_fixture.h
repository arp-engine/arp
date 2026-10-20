#pragma once
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_native_buffer_map_receipt.h"
#include "rhi/frontend/transfer_validation.h"
#include "webgpu_async_wait_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace arp::test {
// Independent diagnostic observer of an already completed original producer.
// The original journal must establish CopySrc first. No Root, submission,
// content or publication authority is issued and the texture is never written.
// Like WebGpuDepthStencilAttachmentInputFixture, this is a native diagnostic,
// not the color-only OriginalTextureReadbackLayout or a public depth readback.
class WebGpuCompletedDepthTexelReadbackFixture final {
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verify(const rhi::GpuTexture& texture, const std::array<float, 5u>& expected)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        const auto& desc = texture.desc();
        assert(desc.extent == (Extent3D{2u, 2u, 1u}) && desc.mipLevelCount == 2u
            && desc.arrayLayerCount == 1u && desc.format == TextureFormat::D32Float
            && desc.sampleCount == 1u
            && (desc.usage & TextureUsage::CopySrc) != TextureUsage::None);
        const auto committed =
            texture.observeOriginalCommittedState(defaultTextureSubresourceRange(desc));
        assert(committed && committed->state() == ResourceState::CopySrc);
        constexpr std::uint32_t pitch = kTextureUploadRowPitchAlignment;
        constexpr std::size_t totalBytes = pitch * 3u;
        const std::array<TextureToBufferCopyDesc::MipLayout, 2u> mips{
            {{0u, {2u, 2u, 1u}, pitch, 2u, 0u}, {1u, {1u, 1u, 1u}, pitch, 1u, pitch * 2u}}};
        for (const auto& mip : mips) {
            auto range = defaultTextureSubresourceRange(desc);
            range.baseMipLevel = mip.mipLevel;
            range.mipLevelCount = 1u;
            assert(range.aspect == TextureAspect::Depth);
            assert(semantics::TextureTransferValidation::copyLayoutValid(desc,
                range,
                mip.extent,
                mip.bytesPerRow,
                mip.rowsPerImage,
                mip.bufferOffsetBytes,
                totalBytes));
        }
        {
            const auto native = webGpuNativePayloadFor(texture);
            assert(native && native->texture && native->ownerDevice);
            const auto queue = wgpuDeviceGetQueue(native->ownerDevice);
            assert(queue);
            auto bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            bufferDesc.size = totalBytes;
            bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
            const auto buffer = wgpuDeviceCreateBuffer(native->ownerDevice, &bufferDesc);
            assert(buffer);
            const auto encoder = wgpuDeviceCreateCommandEncoder(native->ownerDevice, nullptr);
            assert(encoder);
            for (const auto& mip : mips) {
                auto source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
                source.texture = native->texture;
                source.aspect = WGPUTextureAspect_DepthOnly;
                source.mipLevel = mip.mipLevel;
                source.origin = {0u, 0u, 0u};
                auto destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
                destination.buffer = buffer;
                destination.layout.offset = mip.bufferOffsetBytes;
                destination.layout.bytesPerRow = mip.bytesPerRow;
                destination.layout.rowsPerImage = mip.rowsPerImage;
                const WGPUExtent3D extent{mip.extent.width, mip.extent.height, mip.extent.depth};
                wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);
            }
            const auto commands = wgpuCommandEncoderFinish(encoder, nullptr);
            assert(commands);
            wgpuQueueSubmit(queue, 1u, &commands);
            WebGpuNativeBufferMapReceipt receipt;
            bool delivered = false;
            assert(!receipt.prepareCold(buffer,
                totalBytes,
                &delivered,
                [](void* p, WebGpuNativeBufferMapReceipt::Outcome outcome) noexcept {
                    assert(outcome == WebGpuNativeBufferMapReceipt::Outcome::Success);
                    *static_cast<bool*>(p) = true;
                }));
            assert(!receipt.start());
            assert(WebGpuAsyncWaitFixture::until([&] {
                return delivered;
            }));
            std::array<unsigned char, totalBytes> actual{};
            assert(!receipt.readAndUnmap(actual.data(), actual.size()));
            std::size_t texel = 0u;
            for (const auto& mip : mips)
                for (std::uint32_t row = 0u; row < mip.extent.height; ++row) {
                    const auto offset =
                        mip.bufferOffsetBytes + std::uint64_t(row) * mip.bytesPerRow;
                    const auto bytes = semantics::TextureTransferValidation::copyRowBytes(
                        desc.format, mip.extent.width);
                    assert(core::containsUnsignedRange<std::uint64_t>(
                        0u, actual.size(), offset, bytes));
                    assert(core::containsUnsignedRange<std::size_t>(
                        0u, expected.size(), texel, mip.extent.width));
                    assert(!std::memcmp(actual.data() + offset, expected.data() + texel, bytes));
                    texel += mip.extent.width;
                }
            assert(texel == expected.size());
            wgpuCommandBufferRelease(commands);
            wgpuCommandEncoderRelease(encoder);
            wgpuBufferRelease(buffer);
            wgpuQueueRelease(queue);
        }
        std::puts(
            "BINDLESS_WEBGPU_DEPTH_WRITE_RETURN1_TEXELS_OK mip0-four=0.125 mip1-one=0.5 exact-D32 original-CopySrc genuine-map readonly native-diagnostic");
    }
#endif
};
} // namespace arp::test
