#include "renderer/ui/ui_text_layout.h"
#include "tests/fixtures/ui_prepared_fonts.h"

#include <cassert>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

arp::renderer::ui::TextLayoutKey layoutKey(const char* value)
{
    arp::renderer::ui::TextLayoutKey key;
    key.text = text(value);
    key.styleId = text("text.hud");
    key.fontToken = text("font.ui.body");
    key.locale = text("en-US");
    key.sizePx = 16u;
    key.maxWidthPx = 256u;
    key.scaleBucket = 1u;
    key.direction = arp::renderer::ui::TextDirection::LeftToRight;
    key.script = arp::renderer::ui::TextScript::Latin;
    return key;
}

void repeated_layout_requests_hit_cache()
{
    arp::renderer::ui::TextLayoutCache cache(arp::tests::preparedUiFontFaces("font.ui.body"));

    auto first = cache.resolve(layoutKey("Score"));
    auto second = cache.resolve(layoutKey("Score"));

    assert(first.ok());
    assert(second.ok());
    assert(first.run().runId == second.run().runId);
    assert(first.run().glyphQuads.size() == 5u);
    assert(cache.cacheHitCount() == 1u);
    assert(cache.cacheMissCount() == 1u);
    assert(!first.exposesNativeBackendHandles());
}

void style_width_and_text_changes_produce_distinct_runs()
{
    arp::renderer::ui::TextLayoutCache cache(arp::tests::preparedUiFontFaces("font.ui.body"));

    auto base = cache.resolve(layoutKey("Score"));
    auto styleKey = layoutKey("Score");
    styleKey.styleId = text("text.menu");
    auto style = cache.resolve(styleKey);
    auto widthKey = layoutKey("Score");
    widthKey.maxWidthPx = 128u;
    auto width = cache.resolve(widthKey);
    auto textChanged = cache.resolve(layoutKey("HP"));

    assert(base.ok() && style.ok() && width.ok() && textChanged.ok());
    assert(cache.runCount() == 4u);
    assert(base.run().runId != style.run().runId);
    assert(base.run().runId != width.run().runId);
    assert(base.run().runId != textChanged.run().runId);
}

void font_metrics_drive_advance_without_clipping_cell_width()
{
    arp::renderer::ui::TextLayoutCache cache(arp::tests::preparedUiFontFaces("font.ui.body"));

    auto result = cache.resolve(layoutKey("PIKA"));

    assert(result.ok());
    assert(result.run().glyphQuads.size() == 4u);
    assert(
        result.run().glyphQuads[0].localRect.width < static_cast<float>(result.run().key.sizePx));
    assert(result.run().glyphQuads[1].localRect.x < static_cast<float>(result.run().key.sizePx));
    assert(result.run().widthPx < static_cast<float>(result.run().key.sizePx) * 4.0f);
}

void unsupported_text_layout_semantics_fail_deterministically()
{
    arp::renderer::ui::TextLayoutCache cache(arp::tests::preparedUiFontFaces("font.ui.body"));

    auto missingFont = layoutKey("Score");
    missingFont.fontToken.clear();
    auto missing = cache.resolve(missingFont);
    assert(!missing.ok());
    assert(missing.diagnostics() == text("UI text layout requires an explicit font token."));

    auto newline = layoutKey("Score\nNext");
    auto unsupportedNewline = cache.resolve(newline);
    assert(!unsupportedNewline.ok());
    assert(unsupportedNewline.diagnostics()
        == text("UI text layout cache supports single-line text only."));

    auto missingLocale = layoutKey("Score");
    missingLocale.locale.clear();
    auto localeFailure = cache.resolve(missingLocale);
    assert(!localeFailure.ok());
    assert(localeFailure.diagnostics() == text("UI text layout requires an explicit locale."));

    auto nonAscii = layoutKey(u8"Caf\xC3\xA9");
    auto unsupportedScript = cache.resolve(nonAscii);
    assert(!unsupportedScript.ok());
    assert(unsupportedScript.diagnostics()
        == text("UI text layout requires explicit UTF-8 shaping capability for non-ASCII text."));

    auto rtl = layoutKey("abc");
    rtl.direction = arp::renderer::ui::TextDirection::RightToLeft;
    rtl.script = arp::renderer::ui::TextScript::Arabic;
    auto rtlFailure = cache.resolve(rtl);
    assert(!rtlFailure.ok());
    assert(rtlFailure.diagnostics()
        == text(
            "UI text layout shaping supports only declared Latin and Han scripts at this stage."));

    auto clippedSingleLine = layoutKey("This text is too wide");
    clippedSingleLine.maxWidthPx = 12u;
    auto clipped = cache.resolve(clippedSingleLine);
    assert(clipped.ok());
    assert(clipped.run().widthPx > static_cast<float>(clippedSingleLine.maxWidthPx));
}

void utf8_latin_requires_explicit_capability_and_decodes_to_codepoints()
{
    arp::renderer::ui::TextLayoutCache cache(arp::tests::preparedUiFontFaces("font.ui.body"));

    auto cafe = layoutKey(u8"Caf\xC3\xA9");
    auto unsupported = cache.resolve(cafe);
    assert(!unsupported.ok());
    assert(unsupported.diagnostics()
        == text("UI text layout requires explicit UTF-8 shaping capability for non-ASCII text."));

    cache.setShapingCapability(arp::renderer::ui::TextShapingCapability::BasicLatinUtf8);
    auto supported = cache.resolve(cafe);
    assert(supported.ok());
    assert(supported.run().glyphQuads.size() == 4u);
    assert(supported.run().glyphQuads[3].glyph.glyphId == 0x00E9u);
}

void combining_marks_and_complex_scripts_fail_deterministically()
{
    arp::renderer::ui::TextLayoutCache cache(arp::tests::preparedUiFontFaces("font.ui.body"));
    cache.setShapingCapability(arp::renderer::ui::TextShapingCapability::BasicLatinUtf8);

    auto combining = layoutKey(u8"e\xCC\x81");
    auto combiningFailure = cache.resolve(combining);
    assert(!combiningFailure.ok());
    assert(combiningFailure.diagnostics()
        == text("UI text layout requires complex shaping support for combining marks."));

    auto han = layoutKey(u8"\xE4\xBD\xA0\xE5\xA5\xBD");
    han.script = arp::renderer::ui::TextScript::Han;
    auto hanFailure = cache.resolve(han);
    assert(!hanFailure.ok());
    assert(hanFailure.diagnostics()
        == text(
            "UI text layout fallback chain exhausted for font token: font.ui.body; last diagnostic: UI font does not contain the requested glyph"));
}

} // namespace

int main()
{
    repeated_layout_requests_hit_cache();
    style_width_and_text_changes_produce_distinct_runs();
    font_metrics_drive_advance_without_clipping_cell_width();
    unsupported_text_layout_semantics_fail_deterministically();
    utf8_latin_requires_explicit_capability_and_decodes_to_codepoints();
    combining_marks_and_complex_scripts_fail_deterministically();
    return 0;
}
