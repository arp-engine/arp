#include "renderer/ui/ui_glyph_atlas.h"

#include <cassert>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
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

void repeated_characters_reuse_one_glyph_entry()
{
    arp::renderer::ui::GlyphAtlasCache cache({64u, 64u, 1u});

    auto first = cache.resolveGlyph({glyphKey('A'), 8u, 12u, 9.0f});
    assert(first.ok());
    auto second = cache.resolveGlyph({glyphKey('A'), 8u, 12u, 9.0f});
    assert(second.ok());
    auto third = cache.resolveGlyph({glyphKey('B'), 8u, 12u, 9.0f});
    assert(third.ok());

    assert(first.entry().entryId == second.entry().entryId);
    assert(first.entry().atlasRect == second.entry().atlasRect);
    assert(first.entry().entryId != third.entry().entryId);
    assert(cache.entryCount() == 2u);
    assert(cache.cacheHitCount() == 1u);
    assert(cache.cacheMissCount() == 2u);
    assert(!first.exposesNativeBackendHandles());
}

void glyph_key_includes_font_size_render_mode_and_scale_bucket()
{
    arp::renderer::ui::GlyphAtlasCache cache({128u, 64u, 1u});

    auto body = cache.resolveGlyph({glyphKey('A'), 8u, 12u, 9.0f});
    auto titleKey = glyphKey('A');
    titleKey.fontToken = text("font.ui.title");
    auto title = cache.resolveGlyph({titleKey, 8u, 12u, 9.0f});
    auto sizeKey = glyphKey('A');
    sizeKey.sizePx = 24u;
    auto size = cache.resolveGlyph({sizeKey, 8u, 12u, 9.0f});
    auto scaleKey = glyphKey('A');
    scaleKey.scaleBucket = 2u;
    auto scale = cache.resolveGlyph({scaleKey, 8u, 12u, 9.0f});

    assert(body.ok() && title.ok() && size.ok() && scale.ok());
    assert(cache.entryCount() == 4u);
}

void missing_glyph_and_atlas_full_are_deterministic_failures()
{
    arp::renderer::ui::GlyphAtlasCache cache({8u, 8u, 1u});

    auto missingKey = glyphKey(0u);
    auto missing = cache.resolveGlyph({missingKey, 4u, 4u, 4.0f});
    assert(!missing.ok());
    assert(missing.diagnostics() == text("UI glyph atlas missing glyph id."));

    auto tooLarge = cache.resolveGlyph({glyphKey('W'), 16u, 16u, 16.0f});
    assert(!tooLarge.ok());
    assert(tooLarge.diagnostics() == text("UI glyph atlas entry exceeds page dimensions."));

    auto first = cache.resolveGlyph({glyphKey('A'), 8u, 8u, 8.0f});
    assert(first.ok());
    auto exhausted = cache.resolveGlyph({glyphKey('B'), 8u, 8u, 8.0f});
    assert(!exhausted.ok());
    assert(exhausted.diagnostics() == text("UI glyph atlas page budget exhausted."));
}

void dynamic_glyph_policy_and_upload_budget_are_explicit()
{
    arp::renderer::ui::GlyphAtlasCacheDesc desc;
    desc.pageWidth = 64u;
    desc.pageHeight = 64u;
    desc.maxPages = 1u;
    desc.insertionPolicy = arp::renderer::ui::GlyphAtlasInsertionPolicy::Dynamic;
    desc.dynamicGlyphBudgetPerFrame = 1u;

    arp::renderer::ui::GlyphAtlasCache cache(desc);
    cache.beginFrame(11u);

    auto first = cache.resolveGlyph({glyphKey('A'), 8u, 12u, 9.0f});
    assert(first.ok());
    assert(!first.cacheHit());
    assert(first.uploadScheduled());
    assert(first.upload().frameSequence == 11u);
    assert(first.upload().pageIndex == first.entry().pageIndex);
    assert(first.upload().byteSize == 8u * 12u);
    assert(cache.pendingUploads().size() == 1u);

    auto budgetExceeded = cache.resolveGlyph({glyphKey('B'), 8u, 12u, 9.0f});
    assert(!budgetExceeded.ok());
    assert(budgetExceeded.diagnostics() == text("UI glyph atlas dynamic glyph budget exhausted."));

    auto hit = cache.resolveGlyph({glyphKey('A'), 8u, 12u, 9.0f});
    assert(hit.ok());
    assert(hit.cacheHit());
    assert(!hit.uploadScheduled());

    cache.beginFrame(12u);
    auto second = cache.resolveGlyph({glyphKey('B'), 8u, 12u, 9.0f});
    assert(second.ok());
    assert(second.uploadScheduled());
    assert(second.upload().frameSequence == 12u);
}

void static_glyph_policy_rejects_missing_dynamic_glyphs()
{
    arp::renderer::ui::GlyphAtlasCacheDesc desc;
    desc.pageWidth = 64u;
    desc.pageHeight = 64u;
    desc.maxPages = 1u;
    desc.insertionPolicy = arp::renderer::ui::GlyphAtlasInsertionPolicy::Static;

    arp::renderer::ui::GlyphAtlasCache cache(desc);
    auto missing = cache.resolveGlyph({glyphKey('A'), 8u, 12u, 9.0f});
    assert(!missing.ok());
    assert(missing.diagnostics()
        == text("UI glyph atlas static policy disallows dynamic glyph insertion."));
}

void single_page_policy_rejects_implicit_page_growth()
{
    arp::renderer::ui::GlyphAtlasCacheDesc desc;
    desc.pageWidth = 8u;
    desc.pageHeight = 8u;
    desc.maxPages = 2u;
    desc.pagePolicy = arp::renderer::ui::GlyphAtlasPageAllocationPolicy::SinglePage;

    arp::renderer::ui::GlyphAtlasCache cache(desc);
    auto first = cache.resolveGlyph({glyphKey('A'), 8u, 8u, 8.0f});
    assert(first.ok());
    auto second = cache.resolveGlyph({glyphKey('B'), 8u, 8u, 8.0f});
    assert(!second.ok());
    assert(second.diagnostics() == text("UI glyph atlas single-page policy exhausted."));
}

} // namespace

int main()
{
    repeated_characters_reuse_one_glyph_entry();
    glyph_key_includes_font_size_render_mode_and_scale_bucket();
    missing_glyph_and_atlas_full_are_deterministic_failures();
    dynamic_glyph_policy_and_upload_budget_are_explicit();
    static_glyph_policy_rejects_missing_dynamic_glyphs();
    single_page_policy_rejects_implicit_page_growth();
    return 0;
}
