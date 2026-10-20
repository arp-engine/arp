#include "renderer/ui/gui_renderer.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_resources.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace {

constexpr std::uint32_t kExtent = 32u;
constexpr std::uint32_t kRowPitch = 256u;

void requireSuccess(const arp::renderer::RendererResult& result)
{
    if (!result.success)
        std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
    assert(result.success);
}

void requireSuccess(const arp::rhi::CommandValidationResult& result)
{
    if (!result.success)
        std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
    assert(result.success);
}

void requireSuccess(const arp::rhi::QueueSubmitResult& result)
{
    if (!result.success)
        std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
    assert(result.success);
}

void requireSuccess(const arp::rhi::ResourceBarrierResult& result)
{
    if (!result.success)
        std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
    assert(result.success);
}

bool near(std::uint8_t actual, std::uint8_t expected)
{
    const auto delta = static_cast<int>(actual) - static_cast<int>(expected);
    return delta >= -1 && delta <= 1;
}

} // namespace

int main()
{
    using namespace arp;
    using namespace rhi;
    using namespace rhi_metal;

    MetalRhiDeviceDesc deviceDesc;
    deviceDesc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
    deviceDesc.capabilityContract.queriedFromNativeDevice = true;
    deviceDesc.capabilityContract.queriedExtendedFeatures = true;
    deviceDesc.memoryBudgetBytes = 32u * 1024u * 1024u;
    deviceDesc.uploadStagingPageSizeBytes = 2u * 1024u * 1024u;
    deviceDesc.uploadStagingMaxPageCount = 1u;
    auto device = createMetalRhiDevice(deviceDesc);
    assert(device);

    renderer::GuiAtlasTextureDesc atlas;
    atlas.resourceId = "GuiMetalOrientationAtlas";
    atlas.extent = {2u, 2u};
    atlas.srgb = false;
    // CPU raster data is row-major and top-row first: red must remain at the
    // top of the rendered UI quad, and blue must remain at the bottom.
    atlas.rgba8 = {
        255u,
        0u,
        0u,
        255u,
        255u,
        0u,
        0u,
        255u,
        0u,
        0u,
        255u,
        255u,
        0u,
        0u,
        255u,
        255u,
    };
    assert(atlas.valid());

    renderer::GuiRenderer gui(*device);
    renderer::GuiRendererDesc guiDesc;
    guiDesc.shaderPath = ARP_SHADER_DIR "/gui_overlay.slang";
    guiDesc.colorFormat = TextureFormat::Rgba8Unorm;
    guiDesc.extent = {kExtent, kExtent};
    guiDesc.atlasTexture = atlas;
    guiDesc.hasAtlasTexture = true;
    requireSuccess(gui.create(guiDesc));

    auto target = device->createTexture({
        "GuiMetalOrientationTarget",
        {kExtent, kExtent, 1u},
        TextureFormat::Rgba8Unorm,
        TextureUsage::RenderAttachment | TextureUsage::CopySrc,
        MemoryUsage::GpuOnly,
        ResourceState::ColorAttachment,
    });
    auto readback = device->createBuffer({
        "GuiMetalOrientationReadback",
        kRowPitch * kExtent,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst,
    });
    assert(target && readback);

    gui::DebugDrawData drawData;
    gui::DebugDrawQuad background;
    background.nodeId = 1u;
    background.type = gui::NodeType::Panel;
    background.rect = {0.0f, 0.0f, static_cast<float>(kExtent), static_cast<float>(kExtent)};
    background.color = {0.0f, 0.0f, 0.0f, 1.0f};
    background.customColor = true;
    drawData.quads.push_back(std::move(background));

    gui::DebugDrawQuad atlasQuad;
    atlasQuad.nodeId = 2u;
    atlasQuad.type = gui::NodeType::Image;
    atlasQuad.rect = {0.0f, 0.0f, static_cast<float>(kExtent), 16.0f};
    atlasQuad.color = {1.0f, 1.0f, 1.0f, 1.0f};
    atlasQuad.customColor = true;
    atlasQuad.textureResourceId = atlas.resourceId;
    atlasQuad.textureUvRect = {0.0f, 0.0f, 1.0f, 1.0f};
    atlasQuad.textured = true;
    drawData.quads.push_back(std::move(atlasQuad));

    gui::DebugDrawQuad positionQuad;
    positionQuad.nodeId = 3u;
    positionQuad.type = gui::NodeType::Panel;
    positionQuad.rect = {0.0f, 20.0f, static_cast<float>(kExtent), 8.0f};
    positionQuad.color = {0.0f, 1.0f, 0.0f, 1.0f};
    positionQuad.customColor = true;
    drawData.quads.push_back(std::move(positionQuad));

    renderer::RenderFrameContext frame{
        AcquiredFrame{true, "", 1u, 0u},
        RenderTargetDesc{
            "GuiMetalOrientationTarget",
            target->id(),
            {kExtent, kExtent},
            TextureFormat::Rgba8Unorm,
            ResourceState::Present,
        },
        nullptr,
        target.get(),
        nullptr,
    };
    requireSuccess(gui.drawFrame(frame, drawData));

    const TextureSubresourceRange colorRange{0u, 1u, 0u, 1u, TextureAspect::Color};
    auto encoder = device->createCommandEncoder({"GuiMetalOrientationReadback"});
    assert(encoder);
    requireSuccess(encoder->transition(
        *target, colorRange, ResourceState::ColorAttachment, ResourceState::CopySrc));
    auto transfer = encoder->beginTransferPass({"GuiMetalOrientationReadback"});
    assert(transfer);
    requireSuccess(transfer->copyTextureToBuffer({
        target->id(),
        readback->id(),
        colorRange,
        0u,
        kRowPitch,
        kExtent,
        target->desc().extent,
    }));
    requireSuccess(transfer->end());
    auto commands = encoder->finish();
    assert(commands);
    requireSuccess(device->queue().submit(*commands));
    device->waitIdle();

    std::array<std::uint8_t, kRowPitch * kExtent> bytes{};
    core::pmr::String diagnostic;
    assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*readback),
        bytes.data(),
        bytes.size(),
        0u,
        bytes.size(),
        diagnostic));

    const auto pixel = [&](std::uint32_t x, std::uint32_t y) {
        return bytes.data() + y * kRowPitch + x * 4u;
    };
    const auto* atlasTop = pixel(kExtent / 2u, 4u);
    const auto* atlasBottom = pixel(kExtent / 2u, 12u);
    const auto* gap = pixel(kExtent / 2u, 18u);
    const auto* position = pixel(kExtent / 2u, 22u);
    std::fprintf(stderr,
        "GUI samples: atlasTop=(%u,%u,%u) atlasBottom=(%u,%u,%u) gap=(%u,%u,%u) position=(%u,%u,%u)\n",
        atlasTop[0],
        atlasTop[1],
        atlasTop[2],
        atlasBottom[0],
        atlasBottom[1],
        atlasBottom[2],
        gap[0],
        gap[1],
        gap[2],
        position[0],
        position[1],
        position[2]);
    // The production atlas sampler is linear, so samples near the two-row
    // boundary may contain a small amount of the adjacent color. Direction,
    // rather than exact nearest-filter bytes, is the orientation invariant.
    assert(atlasTop[0] > 200u && atlasTop[1] < 8u && atlasTop[2] < 64u);
    assert(atlasBottom[0] < 64u && atlasBottom[1] < 8u && atlasBottom[2] > 200u);
    assert(near(gap[0], 0u) && near(gap[1], 0u) && near(gap[2], 0u));
    assert(near(position[0], 0u) && near(position[1], 255u) && near(position[2], 0u));
    std::puts("Metal GUI top-left coordinates and CPU atlas orientation: PASS");
}
