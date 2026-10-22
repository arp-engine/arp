#include "bindless_scene_attachment_fixture.h"

#include "metal_native_texture_readback_fixture.h"
#include "rhi/frontend/transfer_upload_packing.h"

namespace arp::test {
void BindlessSceneAttachmentFixture::observeMetal(const rhi::GpuTexture& texture,
    const char* entry,
    bool terrain,
    const void* geometry,
    bool fault)
{
    rhi::semantics::TextureUploadPackedMip mip;
    mip.range = rhi::defaultTextureSubresourceRange(texture.desc());
    mip.extent = texture.desc().extent;
    mip.packedBytesPerRow = rhi::kTextureUploadRowPitchAlignment;
    mip.rowCount = 2u;
    const auto bytes = MetalNativeTextureReadbackFixture::read(
        texture, std::array{mip}, 2u * mip.packedBytesPerRow);
    const auto rowBytes = texture.desc().format == rhi::TextureFormat::D32Float ? 8u : 16u;
    std::array<std::byte, 32u> packed{};
    for (std::size_t row = 0u; row < 2u; ++row)
        std::memcpy(
            packed.data() + row * rowBytes, bytes.data() + row * mip.packedBytesPerRow, rowBytes);
    inspectAndReport(texture, entry, terrain, geometry, packed.data(), 2u * rowBytes, fault);
}
} // namespace arp::test
