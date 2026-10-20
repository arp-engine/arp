#include "ui/ui_font.h"
#include "ui/ui_font_face_cache.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

std::filesystem::path systemFontPath()
{
    const std::filesystem::path candidates[] = {
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

arp::ui::UiFontFaceDesc validDesc()
{
    arp::ui::UiFontFaceDesc desc;
    desc.tokenId = text("font.ui.body");
    desc.family = text("Arial");
    desc.sourcePath = systemFontPath();
    desc.sizePx = 16u;
    desc.weight = 500u;
    desc.lineHeightPx = 22u;
    return desc;
}

void ui_font_loader_requires_explicit_metadata()
{
    const auto result = arp::ui::UiFontFaceLoader().load({});
    if (result.ok()) {
        std::cerr << "Expected invalid metadata to fail\n";
    }
    assert(!result.ok());
    assert(result.diagnostic().code == arp::ui::UiFontDiagnosticCode::InvalidFontMetadata);
}

void ui_font_loader_reports_missing_font_file()
{
    auto desc = validDesc();
    if (desc.sourcePath.empty()) {
        std::cerr << "No system font candidate was found\n";
    }
    desc.sourcePath = "assets/pika/ui/fonts/missing.ttf";
    const auto result = arp::ui::UiFontFaceLoader().load(desc);
    if (result.ok()) {
        std::cerr << "Expected missing font file to fail\n";
    }
    assert(!result.ok());
    assert(result.diagnostic().code == arp::ui::UiFontDiagnosticCode::MissingFontFile);
}

void ui_font_loader_uses_freetype_when_available()
{
    const auto fontPath = systemFontPath();
    assert(!fontPath.empty());

    const auto result = arp::ui::UiFontFaceLoader().load(validDesc());
    if (arp::ui::uiFreeTypeBackendAvailable()) {
        if (!result.ok()) {
            std::cerr << "Expected FreeType UI font load to succeed, code="
                      << static_cast<int>(result.diagnostic().code)
                      << " message=" << result.diagnostic().message << '\n';
        }
        assert(result.ok());
        assert(result.face().validatedByFreeType);
    } else {
        assert(!result.ok());
        assert(result.diagnostic().code == arp::ui::UiFontDiagnosticCode::FreeTypeUnavailable);
    }
}

void engine_default_ui_font_uses_freetype_backend()
{
    const auto result = arp::ui::loadDefaultUiFontFace();
    if (arp::ui::uiFreeTypeBackendAvailable()) {
        if (!result.ok()) {
            std::cerr << "Expected default UI font to load through FreeType, code="
                      << static_cast<int>(result.diagnostic().code)
                      << " message=" << result.diagnostic().message << '\n';
        }
        assert(result.ok());
        assert(result.face().validatedByFreeType);
        assert(result.face().desc.tokenId == text("font.engine.default"));
    } else {
        assert(!result.ok());
        assert(result.diagnostic().code == arp::ui::UiFontDiagnosticCode::FreeTypeUnavailable);
    }
}

void ui_font_rasterizer_decodes_utf8_codepoints()
{
    arp::ui::UiFontRasterizeDesc desc;
    desc.face = validDesc();
    desc.text = text(u8"Caf\xC3\xA9");
    desc.originX = 0.0f;
    desc.originY = 0.0f;
    desc.maxX = 256.0f;
    desc.maxY = 64.0f;

    const auto result = arp::ui::UiFontTextRasterizer().rasterize(desc);
    if (arp::ui::uiFreeTypeBackendAvailable()) {
        if (!result.ok()) {
            std::cerr << "Expected UTF-8 rasterization to succeed, code="
                      << static_cast<int>(result.diagnostic().code)
                      << " message=" << result.diagnostic().message << '\n';
        }
        assert(result.ok());
        assert(result.text().glyphCount == 4u);
        assert(result.text().advanceX > 0.0f);
    } else {
        assert(!result.ok());
        assert(result.diagnostic().code == arp::ui::UiFontDiagnosticCode::FreeTypeUnavailable);
    }
}

void ui_font_face_defaults_to_alpha_mask_rendering()
{
    const arp::ui::UiFontFaceDesc desc;
    assert(desc.renderMode == text("alpha"));
}

void alpha_rasterization_preserves_freetype_coverage()
{
    auto desc = validDesc();
    desc.sizePx = 24u;
    desc.lineHeightPx = 32u;
    desc.renderMode = text("alpha");

    arp::ui::UiFontRasterizeDesc rasterDesc;
    rasterDesc.face = std::move(desc);
    rasterDesc.text = text("SgAa");
    rasterDesc.maxX = 256.0f;
    rasterDesc.maxY = 96.0f;

    const auto result = arp::ui::UiFontTextRasterizer().rasterize(rasterDesc);
    if (!arp::ui::uiFreeTypeBackendAvailable()) {
        assert(!result.ok());
        return;
    }
    assert(result.ok());

    std::set<int> coverageValues;
    for (const auto& pixel : result.text().pixels) {
        coverageValues.insert(static_cast<int>(std::lround(pixel.alpha * 255.0f)));
    }
    assert(coverageValues.size() > 8u);
}

void missing_codepoint_reports_missing_glyph()
{
    arp::ui::UiFontRasterizeDesc desc;
    desc.face = validDesc();
    desc.face.renderMode = text("alpha");
    desc.text = text("\xF4\x8F\xBF\xBF"); // U+10FFFF is valid Unicode but absent from UI fonts.
    desc.maxX = 128.0f;
    desc.maxY = 64.0f;

    const auto result = arp::ui::UiFontTextRasterizer().rasterize(desc);
    if (!arp::ui::uiFreeTypeBackendAvailable()) {
        assert(!result.ok());
        return;
    }
    assert(!result.ok());
    assert(result.diagnostic().code == arp::ui::UiFontDiagnosticCode::MissingGlyph);
}

void line_height_deterministically_positions_the_baseline()
{
    const auto minimumInkY = [](std::uint32_t lineHeightPx) {
        arp::ui::UiFontRasterizeDesc desc;
        desc.face = validDesc();
        desc.face.renderMode = text("alpha");
        desc.face.lineHeightPx = lineHeightPx;
        desc.text = text("H");
        desc.maxX = 128.0f;
        desc.maxY = 128.0f;
        const auto result = arp::ui::UiFontTextRasterizer().rasterize(desc);
        assert(result.ok());
        assert(!result.text().pixels.empty());
        return std::min_element(result.text().pixels.begin(),
            result.text().pixels.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.y < rhs.y;
            })
            ->y;
    };

    if (!arp::ui::uiFreeTypeBackendAvailable()) {
        return;
    }
    const auto compactY = minimumInkY(24u);
    const auto spaciousY = minimumInkY(40u);
    assert(std::abs((spaciousY - compactY) - 8.0f) < 0.01f);
}

void freetype_face_cache_exposes_pair_kerning()
{
    if (!arp::ui::uiFreeTypeBackendAvailable()) {
        return;
    }
    arp::ui::UiFontFaceCache cache;
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
    float adjustmentX = 0.0f;
    const auto ok = cache.kerningX(systemFontPath(),
        32u,
        static_cast<std::uint32_t>('A'),
        static_cast<std::uint32_t>('V'),
        adjustmentX,
        diagnostics);
    assert(ok);
    assert(diagnostics.empty());
    assert(adjustmentX < 0.0f);
}

} // namespace

int main()
{
    ui_font_loader_requires_explicit_metadata();
    ui_font_loader_reports_missing_font_file();
    ui_font_loader_uses_freetype_when_available();
    engine_default_ui_font_uses_freetype_backend();
    ui_font_rasterizer_decodes_utf8_codepoints();
    ui_font_face_defaults_to_alpha_mask_rendering();
    alpha_rasterization_preserves_freetype_coverage();
    missing_codepoint_reports_missing_glyph();
    line_height_deterministically_positions_the_baseline();
    freetype_face_cache_exposes_pair_kerning();
    return 0;
}
