#include "platform/render_system.h"
#if defined(ARP_UI_CONTROL_EVENTS_VISUAL)
#include "examples/ui/control_events.h"
#include "renderer/ui/ui_draw_plan.h"
#endif
#include "renderer/render_graph/render_graph.h"
#include "renderer/ui/gui_renderer.h"
#include "renderer/ui/ui_atlas_runtime.h"
#include "rhi/backends/vulkan/vulkan_device.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>

#if defined(ARP_RENDERDOC_APP_AVAILABLE) && defined(_WIN32)
#include <Windows.h>
#include <renderdoc_app.h>
#endif

#ifndef ARP_SHADER_DIR
#define ARP_SHADER_DIR "shaders"
#endif

namespace {

constexpr std::uint32_t kWidth = 720u;
#if defined(ARP_UI_CONTROL_EVENTS_VISUAL)
constexpr std::uint32_t kHeight = 240u;
#else
constexpr std::uint32_t kHeight = 220u;
#endif

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

class RenderDocCaptureScope final {
public:
    RenderDocCaptureScope()
    {
#if defined(ARP_RENDERDOC_APP_AVAILABLE) && defined(_WIN32)
        const auto module = GetModuleHandleA("renderdoc.dll");
        const auto getApi = module
            ? reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"))
            : nullptr;
        if (!getApi || getApi(eRENDERDOC_API_Version_1_4_0, reinterpret_cast<void**>(&api_)) != 1
            || api_ == nullptr) {
            return;
        }
        if (const auto* path = std::getenv("ARP_RENDERDOC_CAPTURE_PATH")) {
            api_->SetCaptureFilePathTemplate(path);
        }
        api_->MaskOverlayBits(0u, 0u);
        api_->StartFrameCapture(nullptr, nullptr);
        active_ = true;
#endif
    }

    ~RenderDocCaptureScope() { end(); }

    void end()
    {
#if defined(ARP_RENDERDOC_APP_AVAILABLE) && defined(_WIN32)
        if (active_) {
            api_->EndFrameCapture(nullptr, nullptr);
            active_ = false;
        }
#endif
    }

private:
#if defined(ARP_RENDERDOC_APP_AVAILABLE) && defined(_WIN32)
    RENDERDOC_API_1_4_0* api_ = nullptr;
#endif
    bool active_ = false;
};

arp::renderer::GuiAtlasTextureDesc baseAtlas()
{
    arp::renderer::GuiAtlasTextureDesc atlas;
    atlas.resourceId = "texture/ui_text_visual_smoke";
    atlas.extent = {4u, 4u};
    atlas.rgba8.resize(4u * 4u * 4u, 255u);
    atlas.srgb = false;
    return atlas;
}

arp::core::pmr::Vector<arp::ui::UiFontFaceDesc> fontFaces()
{
    arp::core::pmr::Vector<arp::ui::UiFontFaceDesc> fonts{&arp::core::defaultCpuMemoryResource()};
    arp::ui::UiFontFaceDesc alpha;
    alpha.tokenId = text("font.visual.alpha");
    alpha.family = text("Segoe UI");
    alpha.sourcePath = "C:/Windows/Fonts/segoeui.ttf";
    alpha.sizePx = 22u;
    alpha.lineHeightPx = 32u;
    alpha.renderMode = text("alpha");
    fonts.push_back(std::move(alpha));

    arp::ui::UiFontFaceDesc sdf;
    sdf.tokenId = text("font.visual.sdf");
    sdf.family = text("Consolas");
    sdf.sourcePath = "C:/Windows/Fonts/consola.ttf";
    sdf.sizePx = 32u;
    sdf.lineHeightPx = 42u;
    sdf.renderMode = text("sdf");
    fonts.push_back(std::move(sdf));
    for (auto& face : fonts) {
        arp::core::pmr::String diagnostic;
        face.source = arp::ui::acquireUiFontSource(face, diagnostic);
        assert(face.source);
    }
    return fonts;
}

arp::renderer::ui::UiDrawCommand box(
    float x, float y, float width, float height, float red, float green, float blue, float alpha)
{
    arp::renderer::ui::UiDrawCommand command;
    command.kind = arp::renderer::ui::UiDrawCommandKind::Box;
    command.rect = arp::ui::LayoutRect::fromOriginSize(x, y, width, height);
    command.clipRect = command.rect;
    command.color[0] = red;
    command.color[1] = green;
    command.color[2] = blue;
    command.color[3] = alpha;
    return command;
}

arp::renderer::ui::UiDrawCommand label(
    const char* value, const char* fontToken, float x, float y, float width, float height)
{
    arp::renderer::ui::UiDrawCommand command;
    command.kind = arp::renderer::ui::UiDrawCommandKind::Text;
    command.rect = arp::ui::LayoutRect::fromOriginSize(x, y, width, height);
    command.clipRect = arp::ui::LayoutRect::fromOriginSize(x, y, width, height + 16.0f);
    command.text = text(value);
    command.styleId = text("text.visual-smoke");
    command.fontToken = text(fontToken);
    command.color[0] = 0.94f;
    command.color[1] = 0.96f;
    command.color[2] = 0.98f;
    command.color[3] = 1.0f;
    return command;
}

} // namespace

int main()
{
    if (!std::filesystem::exists("C:/Windows/Fonts/segoeui.ttf")
        || !std::filesystem::exists("C:/Windows/Fonts/consola.ttf")) {
        std::fprintf(
            stderr, "UI text Vulkan smoke unavailable: required contract fonts are missing\n");
#if defined(ARP_UI_CONTROL_EVENTS_VISUAL)
        return 1;
#else
        return 0;
#endif
    }

    auto fonts = fontFaces();
    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> authored{
        &arp::core::defaultCpuMemoryResource()};
#if defined(ARP_UI_CONTROL_EVENTS_VISUAL)
    arp::examples::ControlEventsExample example;
    const auto loaded =
        example.load(std::filesystem::path(ARP_SOURCE_DIR) / "examples/ui/control_events.arpui");
    if (!loaded.ok()) {
        std::fprintf(stderr, "UI example load failed: %s\n", loaded.diagnostic().message.c_str());
        return 1;
    }
    if (!std::getenv("ARP_UI_EVENTS_BEFORE")) {
        const auto routed = example.runScenario();
        if (!routed.ok()) {
            std::fprintf(
                stderr, "UI example events failed: %s\n", routed.diagnostic.message.c_str());
            return 1;
        }
        assert(example.button()->clicks == 1 && example.slider()->lastValue == 0.75f);
        assert(example.input()->value().text == "Typed through InputFrame");
    }
    const auto layout = example.runtime().canvas().resolveLayout();
    const auto extracted = arp::ui::RenderExtractor().extract(example.runtime().canvas(), layout);
    assert(extracted.ok());
    const auto planned = arp::ui::DrawPlanner().plan(extracted.queue());
    assert(planned.ok());
    arp::renderer::ui::UiDrawPlanLoweringDesc lowering;
    lowering.extent = {kWidth, kHeight};
    arp::renderer::ui::UiDrawStyleColor background;
    background.styleId = "background";
    background.color[0] = 0.035f;
    background.color[1] = 0.045f;
    background.color[2] = 0.055f;
    background.color[3] = 1;
    lowering.styleColors.push_back(background);
    arp::renderer::ui::UiDrawStyleColor control;
    control.styleId = "control";
    control.color[0] = 0.12f;
    control.color[1] = 0.18f;
    control.color[2] = 0.25f;
    control.color[3] = 1;
    lowering.styleColors.push_back(control);
    arp::renderer::ui::UiDrawStyleColor foreground;
    foreground.styleId = "text";
    foreground.color[0] = foreground.color[1] = foreground.color[2] = foreground.color[3] = 1;
    lowering.styleColors.push_back(foreground);
    const auto lowered = arp::renderer::ui::UiDrawPlanLowerer().lower(planned.plan(), lowering);
    if (!lowered.ok()) {
        std::fprintf(stderr, "UI example lowering failed: %s\n", lowered.diagnostics().c_str());
        return 1;
    }
    authored = lowered.commands();
    // The example uses one style for a button's box and text; give text the
    // explicit foreground color in this verification scene.
    for (auto& command : authored)
        if (command.kind == arp::renderer::ui::UiDrawCommandKind::Text) {
            command.color[0] = command.color[1] = command.color[2] = command.color[3] = 1.0f;
        }
#else
    authored.push_back(box(0.0f,
        0.0f,
        static_cast<float>(kWidth),
        static_cast<float>(kHeight),
        0.035f,
        0.045f,
        0.055f,
        1.0f));
    authored.push_back(box(18.0f, 18.0f, 684.0f, 70.0f, 0.08f, 0.11f, 0.14f, 1.0f));
    authored.push_back(box(18.0f, 112.0f, 684.0f, 76.0f, 0.12f, 0.09f, 0.07f, 1.0f));
    authored.push_back(label("Alpha 22px: AVATAR, crisp curves & thin strokes",
        "font.visual.alpha",
        36.0f,
        36.0f,
        640.0f,
        32.0f));
    authored.push_back(
        label("SDF 32px: AVATAR 0123456789", "font.visual.sdf", 36.0f, 124.0f, 640.0f, 42.0f));

#endif

    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc atlasDesc;
    atlasDesc.baseAtlas = baseAtlas();
    atlasDesc.commands = &authored;
    atlasDesc.fontFaces = &fonts;
    atlasDesc.maxWidth = 1024u;
    atlasDesc.maxHeight = 256u;
    atlasDesc.dynamicGlyphBudgetPerFrame = 256u;
    auto atlasRuntime = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(atlasDesc));
    if (!atlasRuntime.ok()) {
        std::fprintf(
            stderr, "UI text atlas build failed: %s\n", atlasRuntime.diagnostics().c_str());
        return 1;
    }
    const auto recovery = atlasRuntime.makeRecoveryPlan();
    auto recovered = arp::renderer::ui::UnifiedUiAtlasRuntime::build(recovery);
    assert(recovered.ok() && recovered.atlas().rgba8 == atlasRuntime.atlas().rgba8);
    atlasRuntime = std::move(recovered);
    const auto resolved = atlasRuntime.resolve(authored);
    if (!resolved.ok()) {
        std::fprintf(stderr, "UI text atlas resolve failed: %s\n", resolved.diagnostics().c_str());
        return 1;
    }
    const auto alphaGlyphCount = std::count_if(
        resolved.commands().begin(), resolved.commands().end(), [](const auto& command) {
            return command.kind == arp::renderer::ui::UiDrawCommandKind::GlyphQuad
                && !command.sdfMode;
        });
    const auto sdfGlyphCount = std::count_if(
        resolved.commands().begin(), resolved.commands().end(), [](const auto& command) {
            return command.kind == arp::renderer::ui::UiDrawCommandKind::GlyphQuad
                && command.sdfMode;
        });
    assert(alphaGlyphCount > 20);
#if defined(ARP_UI_CONTROL_EVENTS_VISUAL)
    assert(sdfGlyphCount == 0);
#else
    assert(sdfGlyphCount > 10);
#endif

    std::set<std::uint8_t> alphaCoverage;
    for (std::size_t offset = 3u; offset < atlasRuntime.atlas().rgba8.size(); offset += 4u) {
        const auto alpha = atlasRuntime.atlas().rgba8[offset];
        if (alpha != 0u) {
            alphaCoverage.insert(alpha);
        }
    }
    assert(alphaCoverage.size() > 16u);

    arp::platform::WindowSystem platform;
    const auto initialized = platform.initialize();
    if (!initialized.success) {
        std::fprintf(
            stderr, "UI text Vulkan smoke unavailable: %s\n", initialized.diagnostics.c_str());
#if defined(ARP_UI_CONTROL_EVENTS_VISUAL)
        return 1;
#else
        return 0;
#endif
    }
    auto window = platform.createWindow({"UiTextVulkanSmoke", kWidth, kHeight, true, true});
    if (!window.valid()) {
        std::fprintf(stderr, "UI text Vulkan smoke unavailable: window creation failed\n");
#if defined(ARP_UI_CONTROL_EVENTS_VISUAL)
        return 1;
#else
        return 0;
#endif
    }
    auto renderSystem =
        arp::platform::createDefaultRenderSystem(platform, window, "UiTextVulkanSmoke");
    if (!renderSystem.valid() || renderSystem.backend() != arp::rhi::Backend::Vulkan) {
        std::fprintf(
            stderr, "UI text Vulkan smoke unavailable: %s\n", renderSystem.diagnostics().c_str());
#if defined(ARP_UI_CONTROL_EVENTS_VISUAL)
        return 1;
#else
        return 0;
#endif
    }
    auto* device = renderSystem.gpuDevice();
    auto* swapchain = renderSystem.swapchain();
    assert(device != nullptr);
    assert(swapchain != nullptr);

    arp::renderer::GuiRenderer renderer(*device);
    arp::renderer::GuiRendererDesc rendererDesc;
    rendererDesc.shaderPath =
        std::filesystem::path(ARP_SHADER_DIR).append("gui_overlay.slang").generic_string();
    rendererDesc.colorFormat = renderSystem.colorFormat();
    rendererDesc.extent = {kWidth, kHeight};
    rendererDesc.atlasTexture = atlasRuntime.atlas();
    rendererDesc.hasAtlasTexture = true;
    const auto rendererCreated = renderer.create(rendererDesc);
    if (!rendererCreated.success) {
        std::fprintf(
            stderr, "UI text renderer creation failed: %s\n", rendererCreated.diagnostics.c_str());
        return 1;
    }

    const auto nativeBefore = arp::rhi_vulkan::inspectVulkanRhiNativeResourceStats(*device);
    RenderDocCaptureScope capture;
    auto acquired = swapchain->acquireNextFrame();
    if (!acquired.success) {
        std::fprintf(stderr, "UI text frame acquire failed: %s\n", acquired.diagnostics.c_str());
        return 1;
    }
    auto acquiredImage = swapchain->acquiredImage(acquired);
    if (!acquiredImage.success || !acquiredImage.texture) {
        swapchain->discardFrame(acquired);
        std::fprintf(
            stderr, "UI text swapchain image failed: %s\n", acquiredImage.diagnostics.c_str());
        return 1;
    }

    arp::render_graph::FrameGraph graph(*device);
    const auto backbuffer =
        graph.importSwapchainTexture(std::move(acquiredImage.texture), acquiredImage.ref);
    arp::renderer::RendererResult recorded;
    graph.addPass("UiTextVisualSmoke")
        .writeAttachment(backbuffer,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store,
            {},
            {0.035f, 0.045f, 0.055f, 1.0f})
        .execute([&](arp::render_graph::RenderContext& context) {
            arp::renderer::RenderFrameContext frame;
            frame.frame = acquired;
            frame.colorTarget = {
                "UiTextSwapchain",
                graph.texture(backbuffer).id(),
                {kWidth, kHeight},
                renderSystem.colorFormat(),
                arp::rhi::ResourceState::Present,
            };
            frame.colorTexture = &graph.texture(backbuffer);
            recorded = renderer.recordPass(context, frame, resolved.commands());
        });
    graph.execute();
    if (!recorded.success) {
        swapchain->discardFrame(acquired);
        std::fprintf(stderr, "UI text GUI pass failed: %s\n", recorded.diagnostics.c_str());
        return 1;
    }
    assert(graph.texture(backbuffer).state() == arp::rhi::ResourceState::Present);
    const auto presented = swapchain->present(acquired);
    if (!presented.success) {
        std::fprintf(stderr, "UI text frame present failed: %s\n", presented.diagnostics.c_str());
        return 1;
    }
    renderSystem.waitGpuIdle();
    capture.end();

    const auto nativeAfter = arp::rhi_vulkan::inspectVulkanRhiNativeResourceStats(*device);
    const auto expectedDraws = resolved.commands().size();
    assert(nativeAfter.nativeIndexedDrawRecordCount
        >= nativeBefore.nativeIndexedDrawRecordCount + expectedDraws);
    std::printf(
        "PASS: UI text Vulkan hidden smoke drew %zu commands (%td alpha glyphs, %td SDF glyphs)\n",
        expectedDraws,
        alphaGlyphCount,
        sdfGlyphCount);
    return 0;
}
