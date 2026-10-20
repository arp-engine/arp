#include "metal_sampled_material_readback_fixture.h"

#include "metal_native_texture_readback_fixture.h"
#include "original_graphics_color_gi_data_fixture.h"
#include "original_graphics_hdr_gi_data_fixture.h"
#include "original_graphics_pbr_material_data_fixture.h"
#include "original_graphics_terrain27_data_fixture.h"
#include "original_graphics_wocc_ibl_data_fixture.h"

#include <type_traits>

namespace arp::test {
namespace {
template <class Data, int ColorTolerance = 1, bool DirectReference = false>
void verify(
    const rhi::OriginalCommandBindlessRootSource& root, const rhi::GpuTexture& target, bool publish)
{
    if (!publish)
        return;
    core::pmr::String diagnostic;
    std::size_t count = 0u;
    typename Data::Numeric observed{};
    const auto observe = [&](const std::shared_ptr<const rhi::OriginalCommandBufferLease>& lease) {
        typename Data::Numeric actual;
        assert(lease->layout().sizeBytes == sizeof(actual));
        assert(rhi_metal::readMetalRhiNativeBuffer(
            rhi_metal::metalRhiNativePayloadFor(*lease->originalObject()),
            actual.data(),
            sizeof(actual),
            0u,
            sizeof(actual),
            diagnostic));
        if constexpr (DirectReference)
            Data::verifyReference(actual);
        else
            Data::verify(actual);
        observed = actual;
        ++count;
    };
    if constexpr (DirectReference) {
        for (const auto& domain : root.ordinaryDomains())
            for (const auto& element : domain.source->frozenElements())
                if (domain.set == 0u && element.declaration.write.binding == 4u)
                    observe(std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                        element.resourceOwner));
    } else
        for (const auto& element : root.elements())
            if (element.declaration.write.binding == Data::resultBinding)
                observe(std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                    element.owner));
    assert(count == 1u);
    const auto content =
        target.observeOriginalContent(rhi::defaultTextureSubresourceRange(target.desc()));
    assert(target.desc().format == rhi::TextureFormat::Rgba8Unorm
        && target.desc().extent.width == 2u && target.desc().extent.height == 2u && content
        && content->contentGeneration() == 1u);
    rhi::semantics::TextureUploadPackedMip mip;
    mip.range = rhi::defaultTextureSubresourceRange(target.desc());
    mip.extent = target.desc().extent;
    mip.packedBytesPerRow = rhi::kTextureUploadRowPitchAlignment;
    mip.rowCount = 2u;
    const auto bytes = MetalNativeTextureReadbackFixture::read(
        target, std::array{mip}, 2u * rhi::kTextureUploadRowPitchAlignment);
    std::array<std::uint8_t, 16u> pixels{};
    for (std::size_t y = 0u; y < 2u; ++y)
        for (std::size_t x = 0u; x < 2u; ++x) {
            const auto expected = Data::expectedColor(x);
            for (std::size_t c = 0u; c < 4u; ++c) {
                const auto actual = std::to_integer<std::uint8_t>(
                    bytes[y * rhi::kTextureUploadRowPitchAlignment + x * 4u + c]);
                assert(std::abs(int(actual) - int(expected[c])) <= ColorTolerance);
                pixels[y * 8u + x * 4u + c] = actual;
            }
        }
    const auto& fault = root.originalSnapshot()->originalBufferSource(
        rhi::BindlessSnapshot::OriginalBufferRole::FaultReadback);
    std::uint32_t bits = ~0u;
    assert(rhi_metal::readMetalRhiNativeBuffer(
        rhi_metal::metalRhiNativePayloadFor(*fault->originalObject()),
        &bits,
        sizeof(bits),
        0u,
        sizeof(bits),
        diagnostic));
    assert(bits <= 1u);
    const auto writeSnapshot = [](const char* name, const void* data, std::size_t bytes) {
        auto* file = std::fopen(name, "wb");
        assert(file && std::fwrite(data, 1u, bytes, file) == bytes);
        assert(!std::fclose(file));
    };
    char name[128u];
    std::snprintf(name, sizeof(name), "metal-public-%s-fault%u.rgba", Data::packageName, bits);
    writeSnapshot(name, pixels.data(), pixels.size());
    if constexpr (std::is_same_v<Data, OriginalGraphicsWoccIblDataFixture>) {
        // Cold independent evidence export after actual GPU completion. Keep
        // reference/clean/fault separate; never turn output into runtime proof.
        std::snprintf(name,
            sizeof(name),
            "metal-public-%s-%s.numeric",
            Data::packageName,
            DirectReference ? "reference"
                : bits      ? "fault1"
                            : "fault0");
        writeSnapshot(name, observed.data(), sizeof(observed));
    }
    std::printf(
        "BINDLESS_METAL_SAMPLED_MATERIAL_READBACK_OK workload%s fault%u lanes%zu color2x2 completion-real\n",
        Data::packageName,
        bits,
        std::tuple_size<typename Data::Numeric>::value);
}
} // namespace

void MetalSampledMaterialReadbackFixture::colorGi(
    const rhi::OriginalCommandBindlessRootSource& root, const rhi::GpuTexture& target, bool publish)
{
    verify<OriginalGraphicsColorGiDataFixture, 0>(root, target, publish);
}

void MetalSampledMaterialReadbackFixture::hdrGi(
    const rhi::OriginalCommandBindlessRootSource& root, const rhi::GpuTexture& target, bool publish)
{
    verify<OriginalGraphicsHdrGiDataFixture, 0>(root, target, publish);
}

void MetalSampledMaterialReadbackFixture::woccIbl(
    const rhi::OriginalCommandBindlessRootSource& root, const rhi::GpuTexture& target, bool publish)
{
    verify<OriginalGraphicsWoccIblDataFixture, 0>(root, target, publish);
}

void MetalSampledMaterialReadbackFixture::woccIblReference(
    const rhi::OriginalCommandBindlessRootSource& root, const rhi::GpuTexture& target, bool publish)
{
    verify<OriginalGraphicsWoccIblDataFixture, 0, true>(root, target, publish);
}

void MetalSampledMaterialReadbackFixture::pbr(
    const rhi::OriginalCommandBindlessRootSource& root, const rhi::GpuTexture& target, bool publish)
{
    verify<OriginalGraphicsPbrMaterialDataFixture>(root, target, publish);
}

void MetalSampledMaterialReadbackFixture::terrain(
    const rhi::OriginalCommandBindlessRootSource& root, const rhi::GpuTexture& target, bool publish)
{
    verify<OriginalGraphicsTerrain27DataFixture>(root, target, publish);
}
} // namespace arp::test
