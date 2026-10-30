#include "ui/ui_font.h"

#include "core/text/utf8.h"
#include "ui/ui_font_face_cache.h"
#include "ui/ui_font_source.h"

#include <algorithm>

#if ARP_HAS_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#endif

namespace arp::ui {

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

UiFontFaceDesc defaultFontDescForHost()
{
    UiFontFaceDesc desc;
    desc.tokenId = text("font.engine.default");
    desc.family = text("Arial");
    desc.sizePx = 16u;
    desc.weight = 400u;
    desc.lineHeightPx = 22u;

    const std::filesystem::path candidates[] = {
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    for (const auto& candidate : candidates) {
        core::pmr::String diagnostic;
        auto source = UiFontSource::loadHost(candidate, desc.maxSourceBytes, diagnostic);
        if (source) {
            desc.sourcePath = candidate;
            desc.source = std::move(source);
            return desc;
        }
    }
    return desc;
}

#if ARP_HAS_FREETYPE
// Shared glyph-rasterization loop used by both the cached and legacy paths.
// Takes an already-configured FT_Face (pixel size set) and produces the
// alpha-coverage span list. Does NOT own the FT_Face - caller is responsible
// for init/dispose.
UiFontRasterizeResult rasterizeWithFace(FT_Face face, const UiFontRasterizeDesc& desc)
{
    UiFontRasterizedText output;
    float penX = desc.originX;
    const float fontHeight = static_cast<float>(face->size->metrics.height) / 64.0f;
    const float leading = std::max(0.0f, static_cast<float>(desc.face.lineHeightPx) - fontHeight);
    const float baselineY =
        desc.originY + leading * 0.5f + static_cast<float>(face->size->metrics.ascender) / 64.0f;
    const auto decoded = arp::core::decodeUtf8(desc.text);
    if (!decoded.success) {
        return UiFontRasterizeResult::failure(UiFontDiagnosticCode::InvalidUtf8Text,
            text("UI text rasterization requires valid UTF-8 text."));
    }

    for (const auto codepoint : decoded.codepoints) {
        if (codepoint == static_cast<std::uint32_t>('\n')) {
            break;
        }
        const auto glyphIndex = FT_Get_Char_Index(face, static_cast<FT_ULong>(codepoint));
        if (glyphIndex == 0u) {
            return UiFontRasterizeResult::failure(UiFontDiagnosticCode::MissingGlyph,
                text("UI font does not contain the requested glyph"));
        }
        if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER) != 0) {
            return UiFontRasterizeResult::failure(UiFontDiagnosticCode::FreeTypeLoadFailed,
                text("FreeType failed to render a UI glyph"));
        }

        const auto* glyph = face->glyph;
        const auto& bitmap = glyph->bitmap;
        const float glyphX = penX + static_cast<float>(glyph->bitmap_left);
        const float glyphY = baselineY - static_cast<float>(glyph->bitmap_top);
        for (unsigned int row = 0; row < bitmap.rows; ++row) {
            int spanStart = -1;
            std::uint8_t spanCoverage = 0;
            std::uint32_t spanLength = 0;
            const auto flushSpan = [&]() {
                if (spanStart < 0 || spanLength == 0u) {
                    return;
                }
                const float pixelX = glyphX + static_cast<float>(spanStart);
                const float pixelY = glyphY + static_cast<float>(row);
                if (pixelX >= desc.originX && pixelY >= desc.originY && pixelX < desc.maxX
                    && pixelY < desc.maxY) {
                    const float clippedWidth = std::min(
                        static_cast<float>(spanLength), std::max(0.0f, desc.maxX - pixelX));
                    if (clippedWidth > 0.0f) {
                        output.pixels.push_back(UiFontRasterPixel{
                            pixelX,
                            pixelY,
                            clippedWidth,
                            1.0f,
                            static_cast<float>(spanCoverage) / 255.0f,
                        });
                    }
                }
                spanStart = -1;
                spanCoverage = 0;
                spanLength = 0;
            };

            for (unsigned int column = 0; column < bitmap.width; ++column) {
                const auto coverage = bitmap.buffer[row * bitmap.pitch + column];
                if (coverage == 0u) {
                    flushSpan();
                    continue;
                }
                if (spanStart >= 0 && coverage != spanCoverage) {
                    flushSpan();
                }
                if (spanStart < 0) {
                    spanStart = static_cast<int>(column);
                    spanCoverage = coverage;
                }
                ++spanLength;
            }
            flushSpan();
        }

        penX += static_cast<float>(glyph->advance.x) / 64.0f;
        ++output.glyphCount;
        if (penX >= desc.maxX) {
            break;
        }
    }
    output.advanceX = penX - desc.originX;
    return UiFontRasterizeResult::success(std::move(output));
}

// SDF (Signed Distance Field) glyph rasterization. Uses FreeType's
// FT_RENDER_MODE_SDF to produce a per-pixel distance bitmap. Each byte in the
// output bitmap is 128 * (dist / spread + 1), clamped [0..255]: edge ~= 128,
// inside > 128, outside < 128. We emit one UiFontRasterPixel per pixel (width=1)
// so writeGlyphPixels can copy the distance value into the atlas alpha channel.
// The fragment shader reconstructs sharp edges via smoothstep at render time.
UiFontRasterizeResult rasterizeWithFaceSdf(
    FT_Face face, const UiFontRasterizeDesc& desc, FT_Library library)
{
    // Set the SDF spread (default 8). We set it every call because the face may
    // be shared across different sizePx rasterizations (in the legacy path),
    // and FT_Property_Set is cheap. The cached path benefits from this too
    // since the same library is reused.
    FT_Int spread = 8;
    FT_Property_Set(library, "sdf", "spread", &spread);

    UiFontRasterizedText output;
    float penX = desc.originX;
    const float fontHeight = static_cast<float>(face->size->metrics.height) / 64.0f;
    const float leading = std::max(0.0f, static_cast<float>(desc.face.lineHeightPx) - fontHeight);
    const float baselineY =
        desc.originY + leading * 0.5f + static_cast<float>(face->size->metrics.ascender) / 64.0f;
    const auto decoded = arp::core::decodeUtf8(desc.text);
    if (!decoded.success) {
        return UiFontRasterizeResult::failure(UiFontDiagnosticCode::InvalidUtf8Text,
            text("UI text SDF rasterization requires valid UTF-8 text."));
    }

    for (const auto codepoint : decoded.codepoints) {
        if (codepoint == static_cast<std::uint32_t>('\n')) {
            break;
        }
        const auto glyphIndex = FT_Get_Char_Index(face, static_cast<FT_ULong>(codepoint));
        if (glyphIndex == 0u) {
            return UiFontRasterizeResult::failure(UiFontDiagnosticCode::MissingGlyph,
                text("UI font does not contain the requested glyph"));
        }
        // FT_LOAD_DEFAULT (no FT_LOAD_RENDER) so we can call FT_Render_Glyph
        // with FT_RENDER_MODE_SDF explicitly.
        if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT) != 0) {
            return UiFontRasterizeResult::failure(UiFontDiagnosticCode::FreeTypeLoadFailed,
                text("FreeType failed to load a UI glyph for SDF rasterization"));
        }
        if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_SDF) != 0) {
            return UiFontRasterizeResult::failure(UiFontDiagnosticCode::FreeTypeLoadFailed,
                text("FreeType failed to render a UI glyph in SDF mode"));
        }

        const auto* glyph = face->glyph;
        const auto& bitmap = glyph->bitmap;
        // SDF bitmaps are FT_PIXEL_MODE_GRAY (8-bit, one byte per pixel).
        if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY || bitmap.buffer == nullptr) {
            // Glyph has no outline (e.g. space) - advance only.
            penX += static_cast<float>(glyph->advance.x) / 64.0f;
            ++output.glyphCount;
            continue;
        }
        const float glyphX = penX + static_cast<float>(glyph->bitmap_left);
        const float glyphY = baselineY - static_cast<float>(glyph->bitmap_top);

        // SDF bitmaps include `spread` pixels of padding on every side and may
        // extend beyond the em box (descenders drop below the baseline, tall
        // glyphs rise above the ascender). Every pixel of the distance field is
        // needed for correct smoothstep edge reconstruction at render time, so
        // unlike the AlphaMask path we do NOT clip pixels to [origin, max).
        // The atlas allocator already reserves entryPadding gutters so the full
        // bitmap (ink + spread) is the natural unit to store.
        for (unsigned int row = 0; row < bitmap.rows; ++row) {
            for (unsigned int column = 0; column < bitmap.width; ++column) {
                const auto distanceByte = bitmap.buffer[row * bitmap.pitch + column];
                output.pixels.push_back(UiFontRasterPixel{
                    glyphX + static_cast<float>(column),
                    glyphY + static_cast<float>(row),
                    1.0f,
                    1.0f,
                    static_cast<float>(distanceByte) / 255.0f,
                });
            }
        }

        penX += static_cast<float>(glyph->advance.x) / 64.0f;
        ++output.glyphCount;
    }
    output.advanceX = penX - desc.originX;
    return UiFontRasterizeResult::success(std::move(output));
}
#endif // ARP_HAS_FREETYPE

} // namespace

UiFontLoadResult UiFontLoadResult::success(UiFontFace face)
{
    UiFontLoadResult result;
    result.face_ = std::move(face);
    return result;
}

UiFontLoadResult UiFontLoadResult::failure(
    UiFontDiagnosticCode code, arp::core::pmr::String message)
{
    UiFontLoadResult result;
    result.diagnostic_ = UiFontDiagnostic{code, std::move(message)};
    return result;
}

UiFontRasterizeResult UiFontRasterizeResult::success(UiFontRasterizedText text)
{
    UiFontRasterizeResult result;
    result.text_ = std::move(text);
    return result;
}

UiFontRasterizeResult UiFontRasterizeResult::failure(
    UiFontDiagnosticCode code, arp::core::pmr::String message)
{
    UiFontRasterizeResult result;
    result.diagnostic_ = UiFontDiagnostic{code, std::move(message)};
    return result;
}

bool uiFreeTypeBackendAvailable()
{
#if ARP_HAS_FREETYPE
    return true;
#else
    return false;
#endif
}

UiFontLoadResult loadDefaultUiFontFace()
{
    const auto desc = defaultFontDescForHost();
    if (desc.sourcePath.empty()) {
        return UiFontLoadResult::failure(UiFontDiagnosticCode::DefaultFontUnavailable,
            text("Engine default UI font requires an explicit system font source path"));
    }
    return UiFontFaceLoader().load(desc);
}

UiFontRasterizeResult UiFontTextRasterizer::rasterize(const UiFontRasterizeDesc& desc) const
{
#if !ARP_HAS_FREETYPE
    return UiFontRasterizeResult::failure(UiFontDiagnosticCode::FreeTypeUnavailable,
        text("UI text rasterization requires the FreeType backend"));
#else
    if (desc.face.sourcePath.empty() || desc.face.sizePx == 0u || desc.face.lineHeightPx == 0u
        || (desc.face.renderMode != "alpha" && desc.face.renderMode != "sdf")) {
        return UiFontRasterizeResult::failure(UiFontDiagnosticCode::InvalidFontMetadata,
            text(
                "UI text rasterization requires sourcePath, sizePx, lineHeightPx, and a valid renderMode"));
    }
    const bool sdfMode = (desc.face.renderMode == "sdf");

    // Prepared source ownership is shared with the cache; no file is reopened.
    if (cache_ != nullptr) {
        arp::core::pmr::String acquireDiagnostics(&arp::core::defaultCpuMemoryResource());
        const auto cached = cache_->acquire(desc.face, acquireDiagnostics);
        if (cached == nullptr) {
            return UiFontRasterizeResult::failure(
                UiFontDiagnosticCode::FreeTypeLoadFailed, std::move(acquireDiagnostics));
        }
        if (sdfMode) {
            return rasterizeWithFaceSdf(cached->face, desc, cached->library);
        }
        return rasterizeWithFace(cached->face, desc);
    }

    // Uncached calls retain a temporary owner through glyph extraction.
    UiFontFaceCache temporary({1, desc.face.maxSourceBytes});
    core::pmr::String diagnostic;
    const auto* cached = temporary.acquire(desc.face, diagnostic);
    if (!cached)
        return UiFontRasterizeResult::failure(
            UiFontDiagnosticCode::FreeTypeLoadFailed, std::move(diagnostic));
    return sdfMode ? rasterizeWithFaceSdf(cached->face, desc, cached->library)
                   : rasterizeWithFace(cached->face, desc);
#endif
}

UiFontLoadResult UiFontFaceLoader::load(const UiFontFaceDesc& desc) const
{
    if (desc.tokenId.empty() || desc.family.empty() || desc.sourcePath.empty() || desc.sizePx == 0u
        || desc.lineHeightPx == 0u || desc.weight == 0u) {
        return UiFontLoadResult::failure(UiFontDiagnosticCode::InvalidFontMetadata,
            text("UI font requires tokenId, family, sourcePath, sizePx, weight, and lineHeightPx"));
    }
    if (desc.renderMode != "alpha" && desc.renderMode != "sdf") {
        return UiFontLoadResult::failure(UiFontDiagnosticCode::InvalidFontMetadata,
            text("UI font renderMode must be 'alpha' or 'sdf'"));
    }
    core::pmr::String diagnostic;
    auto source = acquireUiFontSource(desc, diagnostic);
    if (!source)
        return UiFontLoadResult::failure(
            UiFontDiagnosticCode::MissingFontFile, std::move(diagnostic));

#if !ARP_HAS_FREETYPE
    return UiFontLoadResult::failure(UiFontDiagnosticCode::FreeTypeUnavailable,
        text("UI font source is explicit but FreeType backend is not available"));
#else
    UiFontFace loaded;
    loaded.desc = desc;
    loaded.desc.source = std::move(source);
    UiFontFaceCache validation({1, desc.maxSourceBytes});
    if (!validation.acquire(loaded.desc, diagnostic))
        return UiFontLoadResult::failure(
            UiFontDiagnosticCode::FreeTypeLoadFailed, std::move(diagnostic));
    loaded.validatedByFreeType = true;
    return UiFontLoadResult::success(std::move(loaded));
#endif
}

} // namespace arp::ui
