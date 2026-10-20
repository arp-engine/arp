#include "metal_native_texture_readback_fixture.h"
#include "metal_original_storage_execution_fixture.h"

namespace arp::test {
std::array<std::uint16_t, 20u> MetalStorageExecutionBackendFixture::readPixels(
    const rhi::GpuTexture& texture)
{
    using namespace rhi;
    std::array<semantics::TextureUploadPackedMip, 2u> mips;
    for (unsigned i = 0u; i != 2u; ++i) {
        mips[i].range = {i + 1u, 1u, 0u, 1u, TextureAspect::Color};
        mips[i].extent = {i ? 1u : 2u, i ? 1u : 2u, 1u};
        mips[i].packedOffset = i ? 2u * kTextureUploadRowPitchAlignment : 0u;
        mips[i].packedBytesPerRow = kTextureUploadRowPitchAlignment;
        mips[i].rowCount = i ? 1u : 2u;
    }
    const auto pixels = MetalNativeTextureReadbackFixture::read(
        texture, mips, 3u * kTextureUploadRowPitchAlignment);
    std::array<std::uint16_t, 20u> packed{};
    unsigned pixel = 0u;
    for (unsigned mip = 0u; mip != 2u; ++mip)
        for (unsigned y = 0u; y != mips[mip].extent.height; ++y)
            for (unsigned x = 0u; x != mips[mip].extent.width; ++x)
                std::memcpy(packed.data() + pixel++ * 4u,
                    pixels.data() + mips[mip].packedOffset + y * kTextureUploadRowPitchAlignment
                        + x * 8u,
                    8u);
    assert(pixel == 5u);
    return packed;
}
} // namespace arp::test
