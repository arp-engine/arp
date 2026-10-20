#include "renderer/ui/ui_draw_plan.h"

#include <cassert>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

arp::ui::DrawRecord textRecord(const char* styleId)
{
    arp::ui::DrawRecord record;
    record.kind = arp::ui::DrawRecordKind::Text;
    record.widgetId = arp::ui::WidgetId::fromValue(42);
    record.rect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 96.0f, 20.0f);
    record.clipRect = record.rect;
    record.text = text("Score");
    record.styleId = text(styleId);
    return record;
}

arp::renderer::ui::UiDrawPlanLoweringDesc loweringDesc()
{
    arp::renderer::ui::UiDrawPlanLoweringDesc desc;
    desc.extent = {320u, 180u};

    arp::renderer::ui::UiDrawStyleFont font;
    font.styleId = text("text.hud");
    font.fontToken = text("font.ui.body");
    desc.styleFonts.push_back(std::move(font));
    return desc;
}

void text_draw_record_carries_resolved_font_token_to_renderer_command()
{
    arp::ui::DrawPlan plan;
    auto record = textRecord("text.hud");
    record.fontToken = text("font.ui.body");
    plan.addRecord(std::move(record));

    const auto lowered = arp::renderer::ui::UiDrawPlanLowerer().lower(plan, loweringDesc());
    assert(lowered.ok());
    assert(lowered.commands().size() == 1u);
    assert(lowered.commands()[0].kind == arp::renderer::ui::UiDrawCommandKind::Text);
    assert(lowered.commands()[0].styleId == text("text.hud"));
    assert(lowered.commands()[0].fontToken == text("font.ui.body"));
}

void text_draw_lowering_resolves_font_token_from_style_binding()
{
    arp::ui::DrawPlan plan;
    plan.addRecord(textRecord("text.hud"));

    const auto lowered = arp::renderer::ui::UiDrawPlanLowerer().lower(plan, loweringDesc());
    assert(lowered.ok());
    assert(lowered.commands()[0].fontToken == text("font.ui.body"));
}

void text_draw_lowering_fails_without_font_token()
{
    arp::ui::DrawPlan plan;
    plan.addRecord(textRecord("text.missing"));

    auto desc = loweringDesc();
    const auto lowered = arp::renderer::ui::UiDrawPlanLowerer().lower(plan, desc);
    assert(!lowered.ok());
    assert(lowered.diagnostics()
        == text("UI text draw lowering requires an explicit font token binding."));
}

void text_draw_lowering_fails_for_missing_style_token()
{
    arp::ui::DrawPlan plan;
    auto record = textRecord("");
    plan.addRecord(std::move(record));

    const auto lowered = arp::renderer::ui::UiDrawPlanLowerer().lower(plan, loweringDesc());
    assert(!lowered.ok());
    assert(lowered.diagnostics()
        == text("UI text draw record requires style id before font token resolution."));
}

} // namespace

int main()
{
    text_draw_record_carries_resolved_font_token_to_renderer_command();
    text_draw_lowering_resolves_font_token_from_style_binding();
    text_draw_lowering_fails_without_font_token();
    text_draw_lowering_fails_for_missing_style_token();
    return 0;
}
