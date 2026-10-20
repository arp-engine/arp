#include "hud_content.h"

#include <algorithm>
#include <cstdio>

namespace arp::build_a_queen {
namespace {

constexpr const char* kBodyStyle = "queen.body";
constexpr const char* kPanelStyle = "queen.panel";
constexpr const char* kPreviewPanelStyle = "queen.preview_panel";
constexpr const char* kAccentStyle = "queen.accent";
constexpr const char* kMutedStyle = "queen.muted";

renderer::ui::UiDrawStyleColor styleColor(const char* id, core::Vec4 color)
{
    renderer::ui::UiDrawStyleColor style;
    style.styleId = id;
    style.color[0] = color.x;
    style.color[1] = color.y;
    style.color[2] = color.z;
    style.color[3] = color.w;
    return style;
}

ui::WidgetDesc widgetDesc(const char* name,
    ui::LayoutRect rect,
    std::uint64_t order,
    const char* style,
    ui::UiControlType type,
    core::pmr::String text = {})
{
    ui::WidgetDesc desc;
    desc.name = name;
    desc.localRect = rect;
    desc.order = order;
    desc.styleId = style;
    desc.fontToken = kQueenFontToken;
    desc.controlType = type;
    desc.text = std::move(text);
    return desc;
}

} // namespace

HudContent::HudContent() : canvas_("build_a_queen_hud", makeCanvasDesc({1280, 720}))
{}

renderer::RendererResult HudContent::createHud(rhi::Extent2D extent)
{
    if (!styleColors_.empty())
        return {false, "ARP-QUEEN-HUD-ALREADY-CREATED"};
    styleColors_.push_back(styleColor(kBodyStyle, {1.0f, 0.98f, 0.97f, 1.0f}));
    styleColors_.push_back(styleColor(kPanelStyle, {0.20f, 0.08f, 0.15f, 0.78f}));
    styleColors_.push_back(styleColor(kPreviewPanelStyle, {0.20f, 0.08f, 0.15f, 0.14f}));
    styleColors_.push_back(styleColor(kAccentStyle, {1.0f, 0.76f, 0.34f, 0.98f}));
    styleColors_.push_back(styleColor(kMutedStyle, {1.0f, 0.90f, 0.94f, 0.82f}));
    renderer::ui::UiDrawStyleFont fontStyle;
    fontStyle.styleId = kBodyStyle;
    fontStyle.fontToken = kQueenFontToken;
    styleFonts_.push_back(std::move(fontStyle));

    return resizeHud(extent);
}

renderer::RendererResult HudContent::resizeHud(rhi::Extent2D extent)
{
    if (!extent.width || !extent.height)
        return {false, "ARP-QUEEN-HUD-EXTENT"};
    extent_ = extent;
    canvas_ = ui::Canvas("build_a_queen_hud", makeCanvasDesc(extent));
    soundButton_ = levelLabel_ = progressLabel_ = targetTitle_ = instructionLabel_ = nullptr;
    createWidgets();

    return {true, {}};
}

renderer::RendererResult HudContent::prepareHud(const Snapshot& state, const LevelDesc& level)
{
    updateWidgets(state, level);
    return buildCommands();
}

ui::CanvasDesc HudContent::makeCanvasDesc(rhi::Extent2D extent)
{
    ui::CanvasDesc desc;
    desc.viewport = ui::UiViewport::explicitPixels(
        static_cast<float>(extent.width), static_cast<float>(extent.height));
    desc.safeArea = ui::UiSafeArea::none();
    desc.scalePolicy = ui::UiScalePolicy::fixed(1.0f);
    desc.maxWidgetCount = 32;
    return desc;
}

void HudContent::createWidgets()
{
    const auto add = [&](ui::WidgetDesc desc) -> ui::Widget* {
        auto& widget = canvas_.createWidget(std::move(desc));
        return canvas_.reparent(widget.id(), canvas_.rootId()).ok() ? &widget : nullptr;
    };
    add(widgetDesc("top_panel",
        ui::LayoutRect::fromOriginSize(24, 20, 380, 84),
        1,
        kPanelStyle,
        ui::UiControlType::Panel));
    add(widgetDesc("brand_label",
        ui::LayoutRect::fromOriginSize(40, 27, 250, 28),
        2,
        kBodyStyle,
        ui::UiControlType::Label,
        "BUILD A QUEEN"));
    levelLabel_ = add(widgetDesc("level_label",
        ui::LayoutRect::fromOriginSize(40, 62, 240, 26),
        3,
        kMutedStyle,
        ui::UiControlType::Label,
        "LEVEL 01"));
    add(widgetDesc("sound_button_bg",
        ui::LayoutRect::fromOriginSize(286, 62, 108, 30),
        4,
        kPanelStyle,
        ui::UiControlType::Button));
    soundButton_ = add(widgetDesc("sound_button",
        ui::LayoutRect::fromOriginSize(294, 65, 96, 24),
        5,
        kAccentStyle,
        ui::UiControlType::Label,
        "SOUND ON"));
    add(widgetDesc("progress_panel",
        ui::LayoutRect::fromOriginSize(504, 26, 270, 34),
        6,
        kPanelStyle,
        ui::UiControlType::Panel));
    progressLabel_ = add(widgetDesc("progress_label",
        ui::LayoutRect::fromOriginSize(520, 30, 245, 26),
        7,
        kBodyStyle,
        ui::UiControlType::Label,
        "RUNWAY 000%"));
    add(widgetDesc("target_card",
        ui::LayoutRect::fromOriginSize(956, 20, 300, 236),
        5,
        kPreviewPanelStyle,
        ui::UiControlType::Panel,
        ""));
    targetTitle_ = add(widgetDesc("target_title",
        ui::LayoutRect::fromOriginSize(978, 34, 256, 30),
        8,
        kAccentStyle,
        ui::UiControlType::Label,
        "MATCH THIS LOOK"));
    add(widgetDesc("instruction_panel",
        ui::LayoutRect::fromOriginSize(378, 594, 524, 52),
        20,
        kPanelStyle,
        ui::UiControlType::Panel));
    instructionLabel_ = add(widgetDesc("instruction",
        ui::LayoutRect::fromOriginSize(394, 604, 492, 30),
        21,
        kBodyStyle,
        ui::UiControlType::Label,
        "HOLD AND DRAG TO STEER"));
    add(widgetDesc("footer",
        ui::LayoutRect::fromOriginSize(28, 660, 360, 34),
        21,
        kMutedStyle,
        ui::UiControlType::Label,
        "COLLECT THE MATCHING OUTFIT"));
}

void HudContent::updateWidgets(const Snapshot& state, const LevelDesc& level)
{
    char label[64]{};
    std::snprintf(label, sizeof(label), "LEVEL %02u  /  %02u", state.level, Game::kLevelCount);
    levelLabel_->setText(label);
    const auto percent =
        static_cast<unsigned>(std::clamp(state.distance / Game::kRunDistance, 0.0f, 1.0f) * 100.0f);
    std::snprintf(label, sizeof(label), "RUNWAY   %03u%%", percent);
    progressLabel_->setText(label);
    if (soundButton_)
        soundButton_->setText("SOUND");
    if (targetTitle_)
        targetTitle_->setText(state.phase == Phase::Result ? "FINAL LOOK" : "MATCH THIS LOOK");
    if (instructionLabel_) {
        const char* instruction = "HOLD AND DRAG TO STEER";
        if (state.phase == Phase::Menu)
            instruction = "TAP OR PRESS ENTER TO START";
        if (state.phase == Phase::RunwayBattle)
            instruction = "RUNWAY SHOWDOWN";
        if (state.phase == Phase::Result)
            instruction =
                state.won ? "YOU WIN - TAP FOR NEXT LEVEL" : "TRY AGAIN - TAP TO CONTINUE";
        instructionLabel_->setText(instruction);
    }
}

rhi::ViewportState HudContent::targetPreviewViewport() const
{
    return {956.0f, 68.0f, 300.0f, 184.0f, 0.0f, 1.0f};
}

rhi::ScissorState HudContent::targetPreviewScissor() const
{
    return {956, 68, 300u, 184u};
}

renderer::RendererResult HudContent::buildCommands()
{
    const auto layout = canvas_.resolveLayout();
    if (!layout.ok())
        return {false, layout.diagnostic().message};
    const auto extracted = ui::RenderExtractor().extract(canvas_, layout);
    if (!extracted.ok())
        return {false, extracted.diagnostic().message};
    const auto plan = ui::DrawPlanner().plan(extracted.queue());
    if (!plan.ok())
        return {false, plan.diagnostic().message};

    renderer::ui::UiDrawPlanLoweringDesc lowering;
    lowering.extent = extent_;
    lowering.styleColors = styleColors_;
    lowering.styleFonts = styleFonts_;
    const auto lowered = renderer::ui::UiDrawPlanLowerer().lower(plan.plan(), lowering);
    if (!lowered.ok())
        return {false, lowered.diagnostics()};
    drawCommands_ = lowered.commands();
    lastDrawCount_ = static_cast<std::uint32_t>(drawCommands_.size());
    return {true, {}};
}

} // namespace arp::build_a_queen
