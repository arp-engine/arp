#include "renderer/ui/ui_glyph_atlas.h"
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

arp::renderer::ui::GlyphKey glyphKey(std::uint32_t glyphId)
{
    arp::renderer::ui::GlyphKey key;
    key.fontToken = text("font.ui.body");
    key.glyphId = glyphId;
    key.sizePx = 16u;
    key.renderMode = arp::renderer::ui::GlyphRenderMode::AlphaMask;
    key.scaleBucket = 1u;
    return key;
}

// Contract audit: ASCII text produces one shaped glyph per codepoint.
void ascii_text_shapes_to_one_glyph_per_codepoint()
{
    arp::renderer::ui::TextLayoutCache cache(arp::tests::preparedUiFontFaces("font.ui.body"));
    cache.setShapingCapability(arp::renderer::ui::TextShapingCapability::BasicLatinUtf8);

    auto result = cache.resolve(layoutKey("Score"));
    assert(result.ok());
    assert(result.run().glyphQuads.size() == 5u);
    assert(result.run().glyphQuads[0].glyph.glyphId == 'S');
    assert(result.run().glyphQuads[4].glyph.glyphId == 'e');
    assert(result.run().widthPx > 0.0f);
}

// Contract audit: UTF-8 Latin text decodes to codepoints and requires explicit capability.
void utf8_latin_decodes_codepoints_and_requires_capability()
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

// Contract audit: Invalid UTF-8 is rejected with deterministic diagnostics.
void invalid_utf8_rejected_with_deterministic_diagnostics()
{
    arp::renderer::ui::TextLayoutCache cache(arp::tests::preparedUiFontFaces("font.ui.body"));
    cache.setShapingCapability(arp::renderer::ui::TextShapingCapability::BasicLatinUtf8);

    auto invalidLead = layoutKey("\xFF");
    auto result = cache.resolve(invalidLead);
    assert(!result.ok());
    assert(result.diagnostics() == text("UI text layout received invalid UTF-8 text."));

    auto truncated = layoutKey("\xC3");
    auto truncatedResult = cache.resolve(truncated);
    assert(!truncatedResult.ok());
    assert(truncatedResult.diagnostics() == text("UI text layout received truncated UTF-8 text."));
}

// Contract audit: Combining marks are rejected until complex shaping support exists.
void combining_marks_rejected_deterministically()
{
    arp::renderer::ui::TextLayoutCache cache(arp::tests::preparedUiFontFaces("font.ui.body"));
    cache.setShapingCapability(arp::renderer::ui::TextShapingCapability::BasicLatinUtf8);

    auto combining = layoutKey(u8"e\xCC\x81");
    auto result = cache.resolve(combining);
    assert(!result.ok());
    assert(result.diagnostics()
        == text("UI text layout requires complex shaping support for combining marks."));
}

// Contract audit: Unsupported scripts and RTL emit deterministic diagnostics.
void unsupported_scripts_and_rtl_emit_deterministic_diagnostics()
{
    arp::renderer::ui::TextLayoutCache cache(arp::tests::preparedUiFontFaces("font.ui.body"));

    auto rtl = layoutKey("abc");
    rtl.direction = arp::renderer::ui::TextDirection::RightToLeft;
    rtl.script = arp::renderer::ui::TextScript::Arabic;
    auto rtlResult = cache.resolve(rtl);
    assert(!rtlResult.ok());
    assert(rtlResult.diagnostics()
        == text(
            "UI text layout shaping supports only declared Latin and Han scripts at this stage."));

    auto han = layoutKey(u8"\xE4\xBD\xA0");
    han.script = arp::renderer::ui::TextScript::Han;
    auto hanResult = cache.resolve(han);
    assert(!hanResult.ok());
    assert(hanResult.diagnostics()
        == text("UI text layout requires explicit UTF-8 shaping capability for non-ASCII text."));
}

// A corrupt primary snapshot may only use its explicitly declared prepared fallback.
void missing_glyph_traverses_fallback_chain()
{
    auto primary = arp::tests::preparedUiFontFace("font.ui.body");
    arp::tests::setCorruptUiFontSource(primary);
    primary.fallbackChain.push_back(text("font.ui.fallback"));
    arp::core::pmr::Vector<arp::ui::UiFontFaceDesc> faces;
    faces.push_back(std::move(primary));
    faces.push_back(arp::tests::preparedUiFontFace("font.ui.fallback"));
    arp::renderer::ui::TextLayoutCache cache(faces);
    const auto result = cache.resolve(layoutKey("A"));
    assert(result.ok() && result.run().glyphQuads.size() == 1);
    assert(result.run().glyphQuads[0].glyph.fontToken == "font.ui.fallback");
}

void empty_fallback_chain_fails_deterministically()
{
    arp::core::pmr::Vector<arp::ui::UiFontFaceDesc> faces{&arp::core::defaultCpuMemoryResource()};

    arp::ui::UiFontFaceDesc primary;
    primary.tokenId = text("font.ui.body");
    primary.family = text("Primary");
    primary.sourcePath = "assets/pika/ui/fonts/nonexistent.ttf";
    arp::tests::setCorruptUiFontSource(primary);
    primary.supportedScripts.push_back(text("Latin"));
    primary.sizePx = 16u;
    // No fallbackChain entries.
    faces.push_back(std::move(primary));

    arp::renderer::ui::TextLayoutCache cache(arp::tests::preparedUiFontFaces("font.ui.body"));
    cache.setFontFaces(faces);
    cache.setShapingCapability(arp::renderer::ui::TextShapingCapability::BasicLatinUtf8);

    auto key = layoutKey("A");
    key.fontToken = text("font.ui.body");
    auto result = cache.resolve(key);

    // Expected contract: without a fallback chain, the layout must return a
    // deterministic diagnostic that the glyph cannot be satisfied.
    //
    // Current behavior: returns the raw FreeType missing-file diagnostic;
    // no explicit empty-fallback-chain diagnostic is produced.
    assert(!result.ok());
    const bool mentionsFallback =
        result.diagnostics().find(text("fallback")) != arp::core::pmr::String::npos;
    assert(mentionsFallback
        && "UI text layout must report a deterministic empty-fallback-chain failure;"
           " returning a raw FreeType diagnostic is a contract gap.");
}

// Contract audit: Cache keys must distinguish all contract inputs so that
// text, style, font, locale, size, scale bucket, direction, script, and render
// mode each produce a distinct cached run.
void shaping_cache_keys_distinguish_all_contract_inputs()
{
    arp::renderer::ui::TextLayoutCache cache(arp::tests::preparedUiFontFaces("font.ui.body"));
    cache.setShapingCapability(arp::renderer::ui::TextShapingCapability::BasicLatinUtf8);

    auto base = cache.resolve(layoutKey("Score"));
    assert(base.ok());

    auto textKey = layoutKey("Points");
    auto textChanged = cache.resolve(textKey);

    auto styleKey = layoutKey("Score");
    styleKey.styleId = text("text.menu");
    auto styleChanged = cache.resolve(styleKey);

    auto fontKey = layoutKey("Score");
    fontKey.fontToken = text("font.ui.title");
    auto fontChanged = cache.resolve(fontKey);

    auto localeKey = layoutKey("Score");
    localeKey.locale = text("de-DE");
    auto localeChanged = cache.resolve(localeKey);

    auto sizeKey = layoutKey("Score");
    sizeKey.sizePx = 24u;
    auto sizeChanged = cache.resolve(sizeKey);

    auto scaleKey = layoutKey("Score");
    scaleKey.scaleBucket = 2u;
    auto scaleChanged = cache.resolve(scaleKey);

    auto dirKey = layoutKey("Score");
    dirKey.direction = arp::renderer::ui::TextDirection::RightToLeft;
    auto dirChanged = cache.resolve(dirKey);

    auto scriptKey = layoutKey("Score");
    scriptKey.script = arp::renderer::ui::TextScript::Arabic;
    auto scriptChanged = cache.resolve(scriptKey);

    auto modeKey = layoutKey("Score");
    modeKey.renderMode = arp::renderer::ui::GlyphRenderMode::ColorBitmap;
    auto modeChanged = cache.resolve(modeKey);

    assert(base.run().runId != textChanged.run().runId);
    assert(base.run().runId != styleChanged.run().runId);
    assert(base.run().runId != fontChanged.run().runId);
    assert(base.run().runId != localeChanged.run().runId);
    assert(base.run().runId != sizeChanged.run().runId);
    assert(base.run().runId != scaleChanged.run().runId);
    assert(base.run().runId != dirChanged.run().runId);
    assert(base.run().runId != scriptChanged.run().runId);
    assert(base.run().runId != modeChanged.run().runId);
}

// Contract audit: Glyph atlas page budget exhaustion must be deterministic.
void glyph_atlas_page_budget_exhaustion_is_deterministic()
{
    arp::renderer::ui::GlyphAtlasCache cache({8u, 8u, 1u});
    cache.beginFrame(1u);

    auto first = cache.resolveGlyph({glyphKey('A'), 8u, 8u, 8.0f});
    assert(first.ok());

    auto second = cache.resolveGlyph({glyphKey('B'), 8u, 8u, 8.0f});
    assert(!second.ok());
    assert(second.diagnostics() == text("UI glyph atlas page budget exhausted."));
}

// Contract audit: Glyph atlas upload schedule must be deterministic across
// frames, carrying the correct frame sequence, page index, atlas rect, and
// byte size.
void glyph_upload_schedule_is_deterministic_across_frames()
{
    arp::renderer::ui::GlyphAtlasCacheDesc desc;
    desc.pageWidth = 64u;
    desc.pageHeight = 64u;
    desc.maxPages = 1u;
    desc.insertionPolicy = arp::renderer::ui::GlyphAtlasInsertionPolicy::Dynamic;
    desc.dynamicGlyphBudgetPerFrame = 2u;

    arp::renderer::ui::GlyphAtlasCache cache(desc);

    cache.beginFrame(7u);
    auto first = cache.resolveGlyph({glyphKey('A'), 8u, 12u, 9.0f});
    assert(first.ok());
    assert(!first.cacheHit());
    assert(first.uploadScheduled());
    assert(first.upload().frameSequence == 7u);
    assert(first.upload().pageIndex == first.entry().pageIndex);
    assert(first.upload().byteSize == 8u * 12u);
    assert(first.upload().atlasRect == first.entry().atlasRect);

    auto hit = cache.resolveGlyph({glyphKey('A'), 8u, 12u, 9.0f});
    assert(hit.ok());
    assert(hit.cacheHit());
    assert(!hit.uploadScheduled());

    cache.beginFrame(8u);
    auto second = cache.resolveGlyph({glyphKey('B'), 8u, 12u, 9.0f});
    assert(second.ok());
    assert(!second.cacheHit());
    assert(second.uploadScheduled());
    assert(second.upload().frameSequence == 8u);
    assert(second.upload().byteSize == 8u * 12u);
}

} // namespace

int main()
{
    ascii_text_shapes_to_one_glyph_per_codepoint();
    utf8_latin_decodes_codepoints_and_requires_capability();
    invalid_utf8_rejected_with_deterministic_diagnostics();
    combining_marks_rejected_deterministically();
    unsupported_scripts_and_rtl_emit_deterministic_diagnostics();
    missing_glyph_traverses_fallback_chain();
    empty_fallback_chain_fails_deterministically();
    shaping_cache_keys_distinguish_all_contract_inputs();
    glyph_atlas_page_budget_exhaustion_is_deterministic();
    glyph_upload_schedule_is_deterministic_across_frames();
    return 0;
}
