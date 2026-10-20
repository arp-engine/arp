#pragma once
#include "rhi/backends/metal/metal_backend_objects.h"

#include <Metal/Metal.h>
#include <cassert>
#include <cstring>

namespace arp::test {
// Independent observation after genuine native completion. No ordinary L2
// access, original state transition or publication authority is introduced.
class MetalNativeTextureReadbackFixture final {
public:
    template <class Mips>
    static core::pmr::Vector<std::byte> read(
        const rhi::GpuTexture& texture, const Mips& mips, std::size_t byteCount)
    {
        const auto payload = rhi_metal::metalRhiNativePayloadFor(texture);
        assert(payload);
        auto image = reinterpret_cast<id<MTLTexture>>(payload->resource.nativeObject.get());
        assert(image);
        auto queue = [image.device newCommandQueue];
        assert(queue);
        auto output = [image.device newBufferWithLength:byteCount
                                                options:MTLResourceStorageModeShared |
            MTLResourceHazardTrackingModeTracked];
        assert(output);
        std::memset(output.contents, 0u, byteCount);
        auto command = [queue commandBuffer];
        auto encoder = [command blitCommandEncoder];
        assert(encoder);
        for (const auto& mip : mips) {
            const auto sliceBytes = std::uint64_t(mip.packedBytesPerRow) * mip.rowCount;
            for (std::uint32_t layer = 0u; layer < mip.range.arrayLayerCount; ++layer)
                [encoder copyFromTexture:image
                                 sourceSlice:mip.range.baseArrayLayer + layer
                                 sourceLevel:mip.range.baseMipLevel
                                sourceOrigin:MTLOriginMake(0u, 0u, 0u)
                                  sourceSize:MTLSizeMake(mip.extent.width, mip.extent.height, 1u)
                                    toBuffer:output
                           destinationOffset:mip.packedOffset + layer * sliceBytes
                      destinationBytesPerRow:mip.packedBytesPerRow
                    destinationBytesPerImage:sliceBytes];
        }
        [encoder endEncoding];
        [command commit];
        [command waitUntilCompleted];
        assert(command.status == MTLCommandBufferStatusCompleted);
        core::pmr::Vector<std::byte> bytes(byteCount);
        std::memcpy(bytes.data(), output.contents, byteCount);
        [output release];
        [queue release];
        return bytes;
    }
};
} // namespace arp::test
