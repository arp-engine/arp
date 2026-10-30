#pragma once

#include "core/memory/cpu_allocator.h"
#include "core/text/utf8.h"
#include "renderer/ui/ui_glyph_atlas.h"
#include "ui/ui.h"
#include "ui/ui_font.h"
#include "ui/ui_font_face_cache.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace arp::renderer::ui {

enum class TextDirection {
    LeftToRight,
    RightToLeft,
};

enum class TextScript {
    Latin,
    Arabic,
    Han,
    Unknown,
};

enum class TextShapingCapability {
    None,
    BasicLatinUtf8,
    BasicUnicodeUtf8,
};

struct TextLayoutKey {
    arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String styleId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String fontToken{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String locale{&arp::core::defaultCpuMemoryResource()};
    std::uint32_t sizePx = 0u;
    std::uint32_t maxWidthPx = 0u;
    std::uint32_t scaleBucket = 0u;
    TextDirection direction = TextDirection::LeftToRight;
    TextScript script = TextScript::Latin;
    GlyphRenderMode renderMode = GlyphRenderMode::AlphaMask;

    friend bool operator==(const TextLayoutKey& lhs, const TextLayoutKey& rhs)
    {
        return lhs.text == rhs.text && lhs.styleId == rhs.styleId && lhs.fontToken == rhs.fontToken
            && lhs.locale == rhs.locale && lhs.sizePx == rhs.sizePx
            && lhs.maxWidthPx == rhs.maxWidthPx && lhs.scaleBucket == rhs.scaleBucket
            && lhs.direction == rhs.direction && lhs.script == rhs.script
            && lhs.renderMode == rhs.renderMode;
    }
};

struct GlyphQuad {
    GlyphKey glyph;
    arp::ui::LayoutRect localRect;
    float advanceX = 0.0f;
    float rasterOriginX = 0.0f;
    float rasterOriginY = 0.0f;
};

struct TextLayoutRun {
    std::uint64_t runId = 0u;
    TextLayoutKey key;
    float widthPx = 0.0f;
    float heightPx = 0.0f;
    arp::core::pmr::Vector<GlyphQuad> glyphQuads{&arp::core::defaultCpuMemoryResource()};
};

class TextLayoutResolveResult final {
public:
    static TextLayoutResolveResult success(TextLayoutRun run, bool cacheHit)
    {
        TextLayoutResolveResult result;
        result.ok_ = true;
        result.run_ = std::move(run);
        result.cacheHit_ = cacheHit;
        return result;
    }

    static TextLayoutResolveResult failure(arp::core::pmr::String diagnostics)
    {
        TextLayoutResolveResult result;
        result.ok_ = false;
        result.diagnostics_ = std::move(diagnostics);
        return result;
    }

    bool ok() const { return ok_; }

    const arp::core::pmr::String& diagnostics() const { return diagnostics_; }

    const TextLayoutRun& run() const { return run_; }

    bool cacheHit() const { return cacheHit_; }

    bool exposesNativeBackendHandles() const { return false; }

private:
    bool ok_ = false;
    bool cacheHit_ = false;
    TextLayoutRun run_;
    arp::core::pmr::String diagnostics_{&arp::core::defaultCpuMemoryResource()};
};

struct ShapedGlyph {
    std::uint32_t codepoint = 0u;
    std::uint32_t cluster = 0u;
    float advanceX = 0.0f;
};

struct TextShapingResult {
    bool success = false;
    arp::core::pmr::Vector<ShapedGlyph> glyphs{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

class TextShapingService final {
public:
    TextShapingService() = default;

    explicit TextShapingService(TextShapingCapability capability) : capability_(capability) {}

    void setCapability(TextShapingCapability capability) { capability_ = capability; }

    TextShapingCapability capability() const noexcept { return capability_; }

    TextShapingResult shape(const TextLayoutKey& key) const
    {
        TextShapingResult result;
        if (key.script != TextScript::Latin && key.script != TextScript::Han) {
            result.diagnostics = text(
                "UI text layout shaping supports only declared Latin and Han scripts at this stage.");
            return result;
        }
        if (key.direction != TextDirection::LeftToRight) {
            result.diagnostics =
                text("UI text layout shaping does not support right-to-left text yet.");
            return result;
        }

        auto decoded = arp::core::decodeUtf8(key.text);
        if (!decoded.success) {
            if (decoded.diagnostics == "Invalid UTF-8 text.") {
                result.diagnostics = text("UI text layout received invalid UTF-8 text.");
            } else if (decoded.diagnostics == "Truncated UTF-8 text.") {
                result.diagnostics = text("UI text layout received truncated UTF-8 text.");
            } else if (decoded.diagnostics == "Invalid UTF-8 continuation byte.") {
                result.diagnostics =
                    text("UI text layout received invalid UTF-8 continuation byte.");
            } else if (decoded.diagnostics == "Non-canonical UTF-8 text.") {
                result.diagnostics = text("UI text layout rejected non-canonical UTF-8 text.");
            } else {
                result.diagnostics = decoded.diagnostics;
            }
            return result;
        }

        const bool containsNonAscii = std::any_of(decoded.codepoints.begin(),
            decoded.codepoints.end(),
            [](const std::uint32_t codepoint) {
                return codepoint >= 128u;
            });
        const bool hanText = key.script == TextScript::Han;
        if (containsNonAscii && capability_ != TextShapingCapability::BasicLatinUtf8
            && capability_ != TextShapingCapability::BasicUnicodeUtf8) {
            result.diagnostics = text(
                "UI text layout requires explicit UTF-8 shaping capability for non-ASCII text.");
            return result;
        }

        std::uint32_t cluster = 0u;
        for (const auto codepoint : decoded.codepoints) {
            if (isCombiningMark(codepoint)) {
                result.diagnostics =
                    text("UI text layout requires complex shaping support for combining marks.");
                return result;
            }
            if (hanText ? !isHanCodepoint(codepoint) : codepoint > 0x024Fu) {
                result.diagnostics = hanText
                    ? text("UI text layout shaping supports Han codepoints only.")
                    : text("UI text layout shaping supports declared Latin text only.");
                return result;
            }

            ShapedGlyph glyph;
            glyph.codepoint = codepoint;
            glyph.cluster = cluster++;
            glyph.advanceX = 0.0f;
            result.glyphs.push_back(glyph);
        }

        result.success = true;
        return result;
    }

private:
    static arp::core::pmr::String text(const char* value)
    {
        return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
    }

    static bool isHanCodepoint(std::uint32_t codepoint) noexcept
    {
        return (codepoint >= 0x2E80u && codepoint <= 0xA4CFu)
            || (codepoint >= 0x3000u && codepoint <= 0x303Fu)
            || (codepoint >= 0x3040u && codepoint <= 0x30FFu)
            || (codepoint >= 0x3400u && codepoint <= 0x4DBFu)
            || (codepoint >= 0x4E00u && codepoint <= 0x9FFFu)
            || (codepoint >= 0xF900u && codepoint <= 0xFAFFu)
            || (codepoint >= 0xFE10u && codepoint <= 0xFE6Fu)
            || (codepoint >= 0xFF00u && codepoint <= 0xFFEFu)
            || (codepoint >= 0xAC00u && codepoint <= 0xD7AFu);
    }

    static bool isCombiningMark(std::uint32_t codepoint) noexcept
    {
        return (codepoint >= 0x0300u && codepoint <= 0x036Fu)
            || (codepoint >= 0x1AB0u && codepoint <= 0x1AFFu)
            || (codepoint >= 0x1DC0u && codepoint <= 0x1DFFu)
            || (codepoint >= 0x20D0u && codepoint <= 0x20FFu)
            || (codepoint >= 0xFE20u && codepoint <= 0xFE2Fu);
    }

    TextShapingCapability capability_ = TextShapingCapability::None;
};

class TextLayoutCache final {
public:
    TextLayoutCache() = default;

    explicit TextLayoutCache(const arp::core::pmr::Vector<arp::ui::UiFontFaceDesc>& fontFaces)
        : fontFaces_(fontFaces)
    {}

    void setFontFaces(const arp::core::pmr::Vector<arp::ui::UiFontFaceDesc>& fontFaces)
    {
        fontFaces_ = fontFaces;
        runs_.clear();
        cacheHitCount_ = 0u;
        cacheMissCount_ = 0u;
    }

    void setShapingCapability(TextShapingCapability capability)
    {
        shaping_.setCapability(capability);
        runs_.clear();
        cacheHitCount_ = 0u;
        cacheMissCount_ = 0u;
    }

    /// Attach a persistent FT_Face cache so glyphMetrics() rasterization calls
    /// reuse cached faces instead of re-initializing FreeType per call.
    /// The cache must outlive the TextLayoutCache. Pass nullptr to detach.
    void setFontFaceCache(arp::ui::UiFontFaceCache* cache)
    {
        fontFaceCache_ = cache;
        runs_.clear();
        cacheHitCount_ = 0u;
        cacheMissCount_ = 0u;
    }

    TextShapingCapability shapingCapability() const noexcept { return shaping_.capability(); }

    TextLayoutResolveResult resolve(const TextLayoutKey& key)
    {
        if (key.fontToken.empty()) {
            return TextLayoutResolveResult::failure(
                text("UI text layout requires an explicit font token."));
        }
        if (key.styleId.empty()) {
            return TextLayoutResolveResult::failure(
                text("UI text layout requires an explicit style id."));
        }
        if (key.sizePx == 0u || key.maxWidthPx == 0u || key.scaleBucket == 0u) {
            return TextLayoutResolveResult::failure(
                text("UI text layout requires explicit size, width, and scale bucket."));
        }
        if (key.locale.empty()) {
            return TextLayoutResolveResult::failure(
                text("UI text layout requires an explicit locale."));
        }
        if (key.text.find('\n') != arp::core::pmr::String::npos
            || key.text.find('\r') != arp::core::pmr::String::npos) {
            return TextLayoutResolveResult::failure(
                text("UI text layout cache supports single-line text only."));
        }

        const auto found = std::find_if(runs_.begin(), runs_.end(), [&](const TextLayoutRun& run) {
            return run.key == key;
        });
        if (found != runs_.end()) {
            ++cacheHitCount_;
            return TextLayoutResolveResult::success(*found, true);
        }

        TextLayoutRun run;
        run.runId = nextRunId_++;
        run.key = key;
        run.heightPx = static_cast<float>(key.sizePx);
        run.glyphQuads.reserve(key.text.size());

        const auto shaped = shaping_.shape(key);
        if (!shaped.success) {
            return TextLayoutResolveResult::failure(shaped.diagnostics);
        }

        float cursorX = 0.0f;
        std::uint32_t previousCodepoint = 0u;
        arp::core::pmr::String previousFontToken{&arp::core::defaultCpuMemoryResource()};
        for (const auto& glyph : shaped.glyphs) {
            const auto metrics = glyphMetrics(key, glyph.codepoint);
            if (!metrics.ok) {
                return TextLayoutResolveResult::failure(metrics.diagnostics);
            }
            if (fontFaceCache_ != nullptr && previousCodepoint != 0u
                && previousFontToken == metrics.fontToken) {
                float kerningX = 0.0f;
                arp::core::pmr::String kerningDiagnostics{&arp::core::defaultCpuMemoryResource()};
                if (!fontFaceCache_->kerningX(metrics.face,
                        previousCodepoint,
                        glyph.codepoint,
                        kerningX,
                        kerningDiagnostics)) {
                    return TextLayoutResolveResult::failure(std::move(kerningDiagnostics));
                }
                cursorX += kerningX;
            }
            if (metrics.visible) {
                GlyphQuad quad;
                quad.glyph.fontToken =
                    metrics.fontToken.empty() ? key.fontToken : metrics.fontToken;
                quad.glyph.glyphId = glyph.codepoint;
                quad.glyph.sizePx = key.sizePx;
                quad.glyph.renderMode = key.renderMode;
                quad.glyph.scaleBucket = key.scaleBucket;
                quad.localRect = arp::ui::LayoutRect::fromOriginSize(cursorX + metrics.inkRect.x,
                    metrics.inkRect.y,
                    metrics.inkRect.width,
                    metrics.inkRect.height);
                quad.advanceX = metrics.advanceX;
                quad.rasterOriginX = metrics.inkRect.x;
                quad.rasterOriginY = metrics.inkRect.y;
                run.glyphQuads.push_back(std::move(quad));
            }
            cursorX += metrics.advanceX > 0.0f ? metrics.advanceX : std::max(1.0f, glyph.advanceX);
            previousCodepoint = glyph.codepoint;
            previousFontToken = metrics.fontToken;
        }
        run.widthPx = cursorX;

        runs_.push_back(run);
        ++cacheMissCount_;
        return TextLayoutResolveResult::success(std::move(run), false);
    }

    const arp::core::pmr::Vector<TextLayoutRun>& runs() const { return runs_; }

    std::size_t runCount() const { return runs_.size(); }

    std::size_t cacheHitCount() const { return cacheHitCount_; }

    std::size_t cacheMissCount() const { return cacheMissCount_; }

    bool exposesNativeBackendHandles() const { return false; }

private:
    struct GlyphMetrics {
        bool ok = false;
        bool visible = false;
        float advanceX = 0.0f;
        arp::core::pmr::String fontToken{&arp::core::defaultCpuMemoryResource()};
        arp::ui::UiFontFaceDesc face;
        arp::ui::LayoutRect inkRect;
        arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
    };

    static arp::core::pmr::String text(const char* value)
    {
        return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
    }

    const arp::ui::UiFontFaceDesc* fontFaceFor(const arp::core::pmr::String& fontToken) const
    {
        const auto found = std::find_if(
            fontFaces_.begin(), fontFaces_.end(), [&](const arp::ui::UiFontFaceDesc& face) {
                return face.tokenId == fontToken;
            });
        return found == fontFaces_.end() ? nullptr : &*found;
    }

    arp::core::pmr::Vector<arp::ui::UiFontFaceDesc> resolveFontFaceCandidates(
        const TextLayoutKey& key, arp::core::pmr::String& diagnostics) const
    {
        arp::core::pmr::Vector<arp::ui::UiFontFaceDesc> candidates{
            &arp::core::defaultCpuMemoryResource()};
        if (const auto* face = fontFaceFor(key.fontToken)) {
            candidates.push_back(*face);
            for (const auto& fallbackToken : face->fallbackChain) {
                if (fallbackToken.empty()) {
                    continue;
                }
                if (fallbackToken == face->tokenId) {
                    continue;
                }
                if (const auto* fallback = fontFaceFor(fallbackToken)) {
                    const auto duplicate = std::find_if(candidates.begin(),
                        candidates.end(),
                        [&](const arp::ui::UiFontFaceDesc& candidate) {
                            return candidate.tokenId == fallback->tokenId;
                        });
                    if (duplicate == candidates.end()) {
                        candidates.push_back(*fallback);
                    }
                } else {
                    if (!diagnostics.empty()) {
                        diagnostics += text("; ");
                    }
                    diagnostics += text("fallback font token not declared: ");
                    diagnostics += fallbackToken;
                }
            }
            return candidates;
        }
        diagnostics = text("UI text layout has no font face for token: ");
        diagnostics += key.fontToken;
        return candidates;
    }

    GlyphMetrics glyphMetrics(const TextLayoutKey& key, std::uint32_t codepoint) const
    {
        GlyphMetrics metrics;
        arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
        auto candidates = resolveFontFaceCandidates(key, diagnostics);
        if (candidates.empty()) {
            metrics.diagnostics = std::move(diagnostics);
            return metrics;
        }

        arp::ui::UiFontRasterizeDesc rasterDesc;
        rasterDesc.text = arp::core::pmr::String(&arp::core::defaultCpuMemoryResource());
        if (!arp::core::appendUtf8Codepoint(rasterDesc.text, codepoint, metrics.diagnostics)) {
            metrics.diagnostics =
                text("UI text layout cannot encode an invalid Unicode codepoint.");
            return metrics;
        }
        rasterDesc.originX = 0.0f;
        rasterDesc.originY = 0.0f;
        rasterDesc.maxX = static_cast<float>(key.sizePx);

        arp::ui::UiFontRasterizeResult rasterized;
        arp::core::pmr::String lastDiagnostic{&arp::core::defaultCpuMemoryResource()};
        arp::core::pmr::String resolvedToken{&arp::core::defaultCpuMemoryResource()};
        for (auto face : candidates) {
            if (!arp::ui::validatePreparedUiFontFace(face, lastDiagnostic)) {
                rasterized = arp::ui::UiFontRasterizeResult::failure(
                    arp::ui::UiFontDiagnosticCode::InvalidFontMetadata, lastDiagnostic);
                continue;
            }
            face.sizePx = key.sizePx;
            face.lineHeightPx = std::max(key.sizePx, face.lineHeightPx);
            face.renderMode = key.renderMode == GlyphRenderMode::Sdf ? "sdf" : "alpha";
            rasterDesc.face = std::move(face);
            rasterDesc.maxY = static_cast<float>(rasterDesc.face.lineHeightPx);
            if (fontFaceCache_ != nullptr) {
                rasterized = arp::ui::UiFontTextRasterizer(*fontFaceCache_).rasterize(rasterDesc);
            } else {
                rasterized = arp::ui::UiFontTextRasterizer().rasterize(rasterDesc);
            }
            if (rasterized.ok()) {
                resolvedToken = rasterDesc.face.tokenId;
                break;
            }
            lastDiagnostic = rasterized.diagnostic().message;
        }
        if (!rasterized.ok()) {
            metrics.diagnostics = text("UI text layout fallback chain exhausted for font token: ");
            metrics.diagnostics += key.fontToken;
            if (!diagnostics.empty()) {
                metrics.diagnostics += text("; ");
                metrics.diagnostics += diagnostics;
            }
            if (!lastDiagnostic.empty()) {
                metrics.diagnostics += text("; last diagnostic: ");
                metrics.diagnostics += lastDiagnostic;
            }
            return metrics;
        }

        metrics.ok = true;
        metrics.fontToken = std::move(resolvedToken);
        metrics.face = std::move(rasterDesc.face);
        metrics.advanceX = std::max(1.0f, rasterized.text().advanceX);
        if (rasterized.text().pixels.empty()) {
            return metrics;
        }

        float left = std::numeric_limits<float>::max();
        float top = std::numeric_limits<float>::max();
        float right = 0.0f;
        float bottom = 0.0f;
        for (const auto& pixel : rasterized.text().pixels) {
            left = std::min(left, pixel.x);
            top = std::min(top, pixel.y);
            right = std::max(right, pixel.x + pixel.width);
            bottom = std::max(bottom, pixel.y + pixel.height);
        }
        if (right <= left || bottom <= top) {
            return metrics;
        }
        metrics.visible = true;
        metrics.inkRect =
            arp::ui::LayoutRect::fromOriginSize(left, top, right - left, bottom - top);
        return metrics;
    }

    arp::core::pmr::Vector<TextLayoutRun> runs_{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<arp::ui::UiFontFaceDesc> fontFaces_{
        &arp::core::defaultCpuMemoryResource()};
    TextShapingService shaping_{};
    // Optional persistent FT_Face cache (owned by UnifiedUiAtlasRuntime).
    // When non-null, glyphMetrics() passes it to UiFontTextRasterizer to
    // avoid re-initializing FreeType on every cache miss.
    arp::ui::UiFontFaceCache* fontFaceCache_ = nullptr;
    std::uint64_t nextRunId_ = 1u;
    std::size_t cacheHitCount_ = 0u;
    std::size_t cacheMissCount_ = 0u;
};

} // namespace arp::renderer::ui
