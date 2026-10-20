#include "metal_completed_graphics_fixture.h"
#include "metal_native_texture_readback_fixture.h"
#include "metal_ordinary_depth_graphics_execution_fixture.h"

namespace arp::test {
void MetalOrdinaryDepthGraphicsBackendFixture::verifyOutput(
    const rhi::OriginalCommandResourceSources& inventory, bool fault)
{
    using namespace rhi;
    std::size_t checked = 0u;
    for (const auto& source : inventory.sources()) {
        const auto* view =
            std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(&source);
        if (!view || (*view)->originalObject()->desc().usage != TextureUsage::RenderAttachment)
            continue;
        const auto& target = *(*view)->originalParentLease()->originalObject();
        const auto content =
            target.observeOriginalContent(defaultTextureSubresourceRange(target.desc()));
        // The shared fixture first cancels its CPU plan and then a genuinely
        // recorded plan. Completion publishes the third reserved generation.
        const auto expectedGeneration = 1u + (captureSubmittedOnly ? 0u : 1u) + 1u;
        assert(content
            && content->state() == common::OriginalTextureContentControl::State::Completed
            && content->contentGeneration() == expectedGeneration);
        semantics::TextureUploadPackedMip mip;
        mip.range = defaultTextureSubresourceRange(target.desc());
        mip.extent = target.desc().extent;
        mip.packedBytesPerRow = kTextureUploadRowPitchAlignment;
        mip.rowCount = 2u;
        const auto bytes = MetalNativeTextureReadbackFixture::read(
            target, std::array{mip}, 2u * kTextureUploadRowPitchAlignment);
        std::array<std::uint8_t, 16u> pixels{};
        for (std::size_t row = 0u; row < 2u; ++row)
            std::memcpy(
                pixels.data() + row * 8u, bytes.data() + row * kTextureUploadRowPitchAlignment, 8u);
        const std::array<std::uint8_t, 16u> expected{
            16, 0, 0, 255, 32, 0, 0, 255, 48, 0, 0, 255, 64, 0, 0, 255};
        assert(pixels == expected);
        char fileName[80u];
        std::snprintf(fileName, sizeof(fileName), "metal-ordinary-depth-fault%d.rgba", fault);
        auto* file = std::fopen(fileName, "wb");
        assert(file && std::fwrite(pixels.data(), 1u, pixels.size(), file) == pixels.size());
        assert(!std::fclose(file));
        std::printf(
            "BINDLESS_METAL_PUBLIC_GRAPHICS_DEPTH2_OK fault=%d numeric40 color16 generation%u less-equal-greater samplers2 real-completion same-journal hot0\n",
            fault,
            expectedGeneration);
        ++checked;
    }
    assert(checked == 1u);
}

namespace {
void verify(const rhi::OriginalCommandBindlessRootSource& root,
    const rhi::GpuTexture& target,
    bool publish,
    bool array,
    bool geometry = false)
{
    using namespace rhi;
    std::uint32_t checked = 0u;
    for (const auto& domain : root.ordinaryDomains())
        for (const auto& element : domain.source->frozenElements()) {
            if (domain.set != 2u || element.declaration.write.binding != 4u)
                continue;
            const auto& lease =
                std::get<std::shared_ptr<const OriginalCommandBufferLease>>(element.resourceOwner);
            std::array<std::uint32_t, 4u> words{};
            core::pmr::String diagnostic;
            assert(rhi_metal::readMetalRhiNativeBuffer(
                rhi_metal::metalRhiNativePayloadFor(*lease->originalObject()),
                words.data(),
                sizeof(words),
                0u,
                lease->originalObject()->desc().sizeBytes,
                diagnostic));
            assert((words == std::array<std::uint32_t, 4u>{98u, array ? 0u : 98u, 98u, 98u}));
            ++checked;
            std::printf(
                "BINDLESS_METAL_SAMPLED_ORDINARY array=%d words %u %u %u %u Root2 same-journal\n",
                array,
                words[0],
                words[1],
                words[2],
                words[3]);
        }
    assert(checked == (geometry ? 0u : 1u));
    // A real draw can finish while logical device retirement withholds public
    // content/fault publication. Observe pixels only for committed live content.
    if (!publish)
        return;
    const auto content =
        target.observeOriginalContent(defaultTextureSubresourceRange(target.desc()));
    assert(target.desc().format == TextureFormat::Rgba8Unorm && target.desc().extent.width == 2u
        && target.desc().extent.height == 2u && content && content->contentGeneration() == 1u);
    semantics::TextureUploadPackedMip mip;
    mip.range = defaultTextureSubresourceRange(target.desc());
    mip.extent = target.desc().extent;
    mip.packedBytesPerRow = kTextureUploadRowPitchAlignment;
    mip.rowCount = 2u;
    const auto bytes = MetalNativeTextureReadbackFixture::read(
        target, std::array{mip}, 2u * kTextureUploadRowPitchAlignment);
    std::array<std::uint8_t, 16u> pixels{};
    for (std::size_t row = 0u; row < 2u; ++row)
        std::memcpy(
            pixels.data() + row * 8u, bytes.data() + row * kTextureUploadRowPitchAlignment, 8u);
    const auto& fault = root.originalSnapshot()->originalBufferSource(
        BindlessSnapshot::OriginalBufferRole::FaultReadback);
    std::uint32_t bits = ~0u;
    core::pmr::String diagnostic;
    assert(rhi_metal::readMetalRhiNativeBuffer(
        rhi_metal::metalRhiNativePayloadFor(*fault->originalObject()),
        &bits,
        sizeof(bits),
        0u,
        sizeof(bits),
        diagnostic));
    assert(bits <= 1u);
    const std::array<std::uint8_t, 16u> expected = geometry && bits
        ? std::array<std::uint8_t, 16u>{}
        : array ? std::array<std::uint8_t,
                      16u>{32, 128, 64, 255, 0, 0, 0, 0, 48, 144, 80, 255, 24, 48, 40, 255}
        : bits
        ? std::array<std::uint8_t, 16u>{32, 128, 64, 255, 0, 0, 0, 0, 32, 128, 64, 255, 0, 0, 0, 0}
        : std::array<std::uint8_t, 16u>{
              32, 128, 64, 255, 16, 40, 32, 255, 32, 128, 64, 255, 16, 40, 32, 255};
    assert(pixels == expected);
    char name[96u];
    std::snprintf(name, sizeof(name), "metal-public-sampled-array%d-fault%u.rgba", array, bits);
    auto* file = std::fopen(name, "wb");
    assert(file);
    assert(std::fwrite(pixels.data(), 1u, pixels.size(), file) == pixels.size());
    assert(!std::fclose(file));
    std::printf("BINDLESS_METAL_SAMPLED_PIXELS array=%d fault=%u rgba", array, bits);
    for (const auto value : pixels)
        std::printf(" %u", value);
    std::puts(" completion=real publication=committed");
}
} // namespace

void verifyMetalCompletedSampledGraphics2D(
    const rhi::OriginalCommandBindlessRootSource& r, const rhi::GpuTexture& t, bool p)
{
    verify(r, t, p, false);
}

void verifyMetalCompletedSampledGraphicsArray(
    const rhi::OriginalCommandBindlessRootSource& r, const rhi::GpuTexture& t, bool p)
{
    verify(r, t, p, true);
}

void verifyMetalInitializedGeometryGraphics(
    const rhi::OriginalCommandBindlessRootSource& r, const rhi::GpuTexture& t, bool p)
{
    verify(r, t, p, false, true);
}
} // namespace arp::test
