#pragma once
#include "core/math/unsigned_range.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/frontend/transfer_readback_input.h"

#include <cassert>

namespace arp::test {
// Test-only tight seed of the exact validated original mip geometry. GPU writes
// and queue receipts remain genuine; no state/journal/completion proof is minted.
class WebGpuNativeReadbackSeedFixture final {
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE) && defined(WGPU_TEXEL_COPY_BUFFER_INFO_INIT)
    static void seed(WGPUQueue queue,
        WGPUTexture texture,
        const rhi::TextureReadbackLayout& layout,
        const unsigned char* bytes,
        std::uint64_t size)
    {
        assert(queue && texture && bytes && !layout.validate());
        std::uint64_t offset = 0u;
        for (const auto& mip : layout.mipCopies()) {
            assert(mip.sliceCount == 1u && mip.range.arrayLayerCount == 1u
                && mip.rowBytes <= UINT32_MAX);
            assert(mip.rowCount && mip.rowBytes <= UINT64_MAX / mip.rowCount);
            const auto payload = mip.rowBytes * mip.rowCount;
            assert(core::containsUnsignedRange<std::uint64_t>(0u, size, offset, payload));
            auto destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            destination.texture = texture;
            destination.mipLevel = mip.range.baseMipLevel;
            destination.aspect = WGPUTextureAspect_All;
            auto tight = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
            tight.offset = 0u;
            tight.bytesPerRow = static_cast<std::uint32_t>(mip.rowBytes);
            tight.rowsPerImage = mip.rowCount;
            const WGPUExtent3D extent{mip.extent.width, mip.extent.height, mip.extent.depth};
            wgpuQueueWriteTexture(queue, &destination, bytes + offset, payload, &tight, &extent);
            offset += payload;
        }
        assert(offset == size);
    }

    static void verify(const unsigned char* native,
        std::uint64_t nativeSize,
        const rhi::TextureReadbackLayout& layout,
        const unsigned char* expected,
        std::uint64_t expectedSize)
    {
        assert(native && expected && !layout.validate());
        std::uint64_t offset = 0u;
        for (const auto& mip : layout.mipCopies()) {
            assert(mip.sliceCount == 1u);
            for (std::uint32_t row = 0u; row < mip.rowCount; ++row) {
                const auto destination =
                    mip.bufferOffsetBytes + std::uint64_t(row) * mip.bytesPerRow;
                assert(core::containsUnsignedRange<std::uint64_t>(
                           0u, nativeSize, destination, mip.rowBytes)
                    && core::containsUnsignedRange<std::uint64_t>(
                        0u, expectedSize, offset, mip.rowBytes));
                for (std::uint64_t byte = 0u; byte < mip.rowBytes; ++byte)
                    assert(native[destination + byte] == expected[offset + byte]);
                offset += mip.rowBytes;
            }
        }
        assert(offset == expectedSize);
    }
#endif
};
} // namespace arp::test
