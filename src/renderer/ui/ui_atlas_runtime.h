#pragma once

#include "core/memory/cpu_allocator.h"
#include "core/text/utf8.h"
#include "renderer/render_frame.h"
#include "renderer/ui/ui_text_layout.h"
#include "ui/ui_font.h"
#include "ui/ui_font_face_cache.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

namespace arp::renderer::ui {

struct UnifiedUiAtlasRuntimeTelemetry {
    std::size_t glyphHits = 0u;
    std::size_t glyphMisses = 0u;
    std::size_t pageCount = 0u;
    std::size_t uploadBytes = 0u;
    bool atlasFullDiagnostic = false;
    std::size_t unsupportedScriptCount = 0u;
    std::size_t fallbackCount = 0u;
    std::size_t uploadsCompleted = 0u;
};

struct UnifiedUiAtlasRuntimeBuildDesc {
    renderer::GuiAtlasTextureDesc baseAtlas;
    const arp::core::pmr::Vector<UiDrawCommand>* commands = nullptr;
    const arp::core::pmr::Vector<arp::ui::UiFontFaceDesc>* fontFaces = nullptr;
    std::uint32_t maxWidth = 1024u;
    std::uint32_t maxHeight = 1024u;
    std::uint32_t dynamicGlyphBudgetPerFrame = std::numeric_limits<std::uint32_t>::max();
    TextShapingCapability shapingCapability = TextShapingCapability::None;
    std::uint64_t initialGeneration = 1u;
    const arp::core::pmr::Vector<GlyphAtlasEntry>* seedEntries = nullptr;
    const arp::core::pmr::Vector<GlyphAtlasPage>* seedPages = nullptr;
};

struct UiResourceRecoveryRetentionPolicy {
    std::uint64_t maxRecoveryDataBytes = std::numeric_limits<std::uint64_t>::max();
};

class UiResourceRecoveryPlanResult final {
public:
    static UiResourceRecoveryPlanResult success(UnifiedUiAtlasRuntimeBuildDesc plan)
    {
        UiResourceRecoveryPlanResult result;
        result.ok_ = true;
        result.plan_ = std::move(plan);
        return result;
    }

    static UiResourceRecoveryPlanResult failure(arp::core::pmr::String diagnostics)
    {
        UiResourceRecoveryPlanResult result;
        result.ok_ = false;
        result.diagnostics_ = std::move(diagnostics);
        return result;
    }

    bool ok() const { return ok_; }

    const arp::core::pmr::String& diagnostics() const { return diagnostics_; }

    const UnifiedUiAtlasRuntimeBuildDesc& plan() const { return plan_; }

private:
    bool ok_ = false;
    arp::core::pmr::String diagnostics_{&arp::core::defaultCpuMemoryResource()};
    UnifiedUiAtlasRuntimeBuildDesc plan_;
};

class UnifiedUiAtlasResolveResult final {
public:
    static UnifiedUiAtlasResolveResult success(arp::core::pmr::Vector<UiDrawCommand> commands)
    {
        UnifiedUiAtlasResolveResult result;
        result.ok_ = true;
        result.commands_ = std::move(commands);
        return result;
    }

    static UnifiedUiAtlasResolveResult failure(arp::core::pmr::String diagnostics)
    {
        UnifiedUiAtlasResolveResult result;
        result.ok_ = false;
        result.diagnostics_ = std::move(diagnostics);
        return result;
    }

    bool ok() const { return ok_; }

    const arp::core::pmr::String& diagnostics() const { return diagnostics_; }

    const arp::core::pmr::Vector<UiDrawCommand>& commands() const { return commands_; }

    bool exposesNativeBackendHandles() const { return false; }

private:
    bool ok_ = false;
    arp::core::pmr::String diagnostics_{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<UiDrawCommand> commands_{&arp::core::defaultCpuMemoryResource()};
};

class UnifiedUiAtlasRuntime final {
public:
    // Move constructor/assignment are defined inline below build() because
    // moving must re-point textLayout_'s fontFaceCache_ pointer to *this.
    // Copy is deleted: UiFontFaceCache holds FreeType handles that cannot be
    // duplicated.
    UnifiedUiAtlasRuntime(const UnifiedUiAtlasRuntime&) = delete;
    UnifiedUiAtlasRuntime& operator=(const UnifiedUiAtlasRuntime&) = delete;

    static UnifiedUiAtlasRuntime build(UnifiedUiAtlasRuntimeBuildDesc desc)
    {
        UnifiedUiAtlasRuntime runtime;
        if (!desc.baseAtlas.valid()) {
            runtime.diagnostics_ =
                text("Unified UI atlas runtime requires a valid base atlas texture.");
            return runtime;
        }
        if (desc.maxWidth == 0u || desc.maxHeight == 0u) {
            runtime.diagnostics_ =
                text("Unified UI atlas runtime requires explicit glyph page dimensions.");
            return runtime;
        }

        runtime.baseAtlas_ = std::move(desc.baseAtlas);
        runtime.logicalAtlasResourceId_ = arp::core::pmr::String(
            runtime.baseAtlas_.resourceId.c_str(), &arp::core::defaultCpuMemoryResource());
        if (desc.fontFaces != nullptr) {
            for (const auto& face : *desc.fontFaces) {
                if (!arp::ui::validatePreparedUiFontFace(face, runtime.diagnostics_)) {
                    return runtime;
                }
            }
            runtime.fontFaces_ = *desc.fontFaces;
            runtime.textLayout_.setFontFaces(runtime.fontFaces_);
        }
        // Wire the persistent FT_Face cache into both the text layout cache
        // (for glyphMetrics() calls) and the atlas runtime (for
        // writeGlyphPixels()). The cache lives on the runtime so it is moved
        // together with it.
        runtime.textLayout_.setFontFaceCache(&runtime.fontFaceCache_);
        runtime.textLayout_.setShapingCapability(desc.shapingCapability);
        GlyphAtlasCacheDesc glyphDesc;
        glyphDesc.pageWidth = desc.maxWidth;
        glyphDesc.pageHeight = desc.maxHeight;
        glyphDesc.maxPages = 1u;
        glyphDesc.insertionPolicy = GlyphAtlasInsertionPolicy::Dynamic;
        glyphDesc.pagePolicy = GlyphAtlasPageAllocationPolicy::MultiPage;
        glyphDesc.dynamicGlyphBudgetPerFrame = desc.dynamicGlyphBudgetPerFrame;
        // If any font face uses SDF rendering, add padding around each glyph
        // entry so bilinear sampling at the edges does not bleed into
        // neighbouring glyphs. The padding equals the SDF spread (8px) so the
        // full distance field gradient is captured without clamping.
        if (desc.fontFaces != nullptr) {
            for (const auto& face : *desc.fontFaces) {
                if (face.renderMode == "sdf") {
                    glyphDesc.entryPadding = kSdfSpreadPx;
                    break;
                }
            }
        }
        runtime.glyphAtlas_ = GlyphAtlasCache(glyphDesc);
        if (desc.seedPages != nullptr) {
            for (const auto& page : *desc.seedPages) {
                runtime.glyphAtlas_.seedPage(page);
            }
        }
        if (desc.seedEntries != nullptr) {
            for (const auto& entry : *desc.seedEntries) {
                runtime.glyphAtlas_.seedEntry(entry);
            }
        }
        runtime.glyphAtlas_.setGeneration(desc.initialGeneration);
        runtime.glyphPageOriginY_ = runtime.baseAtlas_.extent.height;
        runtime.combinedAtlas_ =
            makeCombinedAtlas(runtime.baseAtlas_, desc.maxWidth, desc.maxHeight);
        // Seed entries describe placement, not pixels. Rebuild their CPU atlas
        // from retained sources before cache hits can expose the recovered image.
        for (const auto& entry : runtime.glyphAtlas_.entries()) {
            if (entry.pageIndex != 0u) {
                runtime.diagnostics_ = text("UI-ATLAS-RECOVERY-PAGE-UNSUPPORTED");
                return runtime;
            }
            GlyphQuad quad;
            quad.glyph = entry.key;
            quad.localRect = entry.atlasRect;
            quad.rasterOriginX = entry.bearingX;
            quad.rasterOriginY = entry.bearingY;
            const auto written = runtime.writeGlyphPixels({}, quad, entry);
            if (!written.ok()) {
                runtime.diagnostics_ = written.diagnostics();
                return runtime;
            }
        }
        runtime.ok_ = true;

        if (desc.commands != nullptr) {
            const auto warmed = runtime.resolve(*desc.commands);
            if (!warmed.ok()) {
                runtime.ok_ = false;
                runtime.diagnostics_ = warmed.diagnostics();
                runtime.telemetry_.atlasFullDiagnostic =
                    runtime.diagnostics_.find("budget") != arp::core::pmr::String::npos
                    || runtime.diagnostics_.find("exhausted") != arp::core::pmr::String::npos
                    || runtime.diagnostics_.find("exceeds") != arp::core::pmr::String::npos;
                return runtime;
            }
        }

        runtime.telemetry_.pageCount = runtime.glyphAtlas_.pageCount();
        runtime.telemetry_.uploadBytes = runtime.combinedAtlas_.rgba8.size();
        return runtime;
    }

    UnifiedUiAtlasRuntime() = default;

    UnifiedUiAtlasRuntime(UnifiedUiAtlasRuntime&& other) noexcept
        : ok_(other.ok_), baseAtlas_(std::move(other.baseAtlas_)),
          combinedAtlas_(std::move(other.combinedAtlas_)),
          glyphPageOriginY_(other.glyphPageOriginY_), diagnostics_(std::move(other.diagnostics_)),
          logicalAtlasResourceId_(std::move(other.logicalAtlasResourceId_)),
          telemetry_(other.telemetry_), frameSequence_(other.frameSequence_),
          textLayout_(std::move(other.textLayout_)),
          fontFaceCache_(std::move(other.fontFaceCache_)), fontFaces_(std::move(other.fontFaces_)),
          glyphAtlas_(std::move(other.glyphAtlas_))
    {
        // textLayout_ was moved from the source, so its fontFaceCache_
        // pointer still points at the (now-moved-from) source's
        // fontFaceCache_. Re-point it to *this.
        textLayout_.setFontFaceCache(&fontFaceCache_);
        other.ok_ = false;
    }

    UnifiedUiAtlasRuntime& operator=(UnifiedUiAtlasRuntime&& other) noexcept
    {
        if (this != &other) {
            ok_ = other.ok_;
            baseAtlas_ = std::move(other.baseAtlas_);
            combinedAtlas_ = std::move(other.combinedAtlas_);
            glyphPageOriginY_ = other.glyphPageOriginY_;
            diagnostics_ = std::move(other.diagnostics_);
            logicalAtlasResourceId_ = std::move(other.logicalAtlasResourceId_);
            telemetry_ = other.telemetry_;
            frameSequence_ = other.frameSequence_;
            fontFaceCache_ = std::move(other.fontFaceCache_);
            textLayout_ = std::move(other.textLayout_);
            fontFaces_ = std::move(other.fontFaces_);
            glyphAtlas_ = std::move(other.glyphAtlas_);
            textLayout_.setFontFaceCache(&fontFaceCache_);
            other.ok_ = false;
        }
        return *this;
    }

    bool ok() const { return ok_; }

    const arp::core::pmr::String& diagnostics() const { return diagnostics_; }

    const renderer::GuiAtlasTextureDesc& atlas() const { return combinedAtlas_; }

    const arp::core::pmr::Vector<arp::ui::UiFontFaceDesc>& fontFaces() const noexcept
    {
        return fontFaces_;
    }

    const arp::core::pmr::String& logicalAtlasResourceId() const { return logicalAtlasResourceId_; }

    const UnifiedUiAtlasRuntimeTelemetry& telemetry() const { return telemetry_; }

    /// Fast text expansion that bypasses TextLayoutCache entirely. Iterates
    /// the text's UTF-8 codepoints and queries the glyph atlas directly for
    /// each one. This is dramatically cheaper than resolve() when the glyph
    /// pixels are already cached (e.g. via seed warm-up) but the exact text
    /// string changes every frame (which would cause TextLayoutCache misses
    /// and trigger full FreeType re-rasterization per character in
    /// glyphMetrics()). Requires that all codepoints in the text have been
    /// pre-warmed into the glyph atlas; otherwise returns failure for the
    /// missing glyph. The font face's sizePx is used as the glyph size;
    /// advanceX comes from the cached atlas entry.
    UnifiedUiAtlasResolveResult resolveTextFast(
        const arp::core::pmr::Vector<UiDrawCommand>& commands) const
    {
        if (!ok_) {
            return UnifiedUiAtlasResolveResult::failure(diagnostics_);
        }
        arp::core::pmr::Vector<UiDrawCommand> resolved{&arp::core::defaultCpuMemoryResource()};
        resolved.reserve(commands.size());
        glyphAtlas_.beginFrame(++frameSequence_);

        for (const auto& command : commands) {
            if (command.kind != UiDrawCommandKind::Text) {
                resolved.push_back(command);
                continue;
            }
            // Resolve the font size from the face (mirrors layoutKeyFor).
            const auto* face = fontFaceFor(command.fontToken);
            if (!face) {
                return UnifiedUiAtlasResolveResult::failure(
                    text("Unified UI atlas runtime has no font face for token: ")
                    + command.fontToken);
            }
            const std::uint32_t sizePx = face->sizePx;
            const bool sdfFont = face->renderMode == "sdf";
            // SDF fonts rasterize at a fixed sample size so one glyph entry
            // serves all render sizes. AlphaMask uses the face's sizePx.
            const auto rasterSizePx = sdfFont ? kSdfSampleSizePx : sizePx;
            if (rasterSizePx == 0u) {
                return UnifiedUiAtlasResolveResult::failure(
                    text("resolveTextFast requires a non-zero font size."));
            }
            // Decode all codepoints once (UTF-8) and lay out glyphs left-to-right.
            const auto decoded = arp::core::decodeUtf8(command.text);
            if (!decoded.success) {
                return UnifiedUiAtlasResolveResult::failure(decoded.diagnostics.empty()
                        ? text("resolveTextFast failed to decode UTF-8 text.")
                        : decoded.diagnostics);
            }
            float cursorX = 0.0f;
            for (const auto codepoint : decoded.codepoints) {
                if (codepoint == 0u)
                    continue;
                // Whitespace and control glyphs have no ink (the layout
                // engine sets metrics.visible = false for them) so they are
                // never seeded into the glyph atlas. Skip the atlas lookup
                // for these and advance the cursor by a quarter-em instead,
                // matching the advance used by the shaping fallback path.
                if (codepoint <= 0x20u || codepoint == 0x7Fu) {
                    cursorX += std::max(1.0f, static_cast<float>(sizePx) * 0.25f);
                    continue;
                }
                // Query the pre-warmed glyph atlas (single-character key).
                GlyphKey gk;
                gk.fontToken = command.fontToken;
                gk.glyphId = codepoint;
                gk.sizePx = rasterSizePx;
                gk.renderMode = sdfFont ? GlyphRenderMode::Sdf : GlyphRenderMode::AlphaMask;
                gk.scaleBucket = 1u;
                const auto glyph = glyphAtlas_.resolveGlyph({
                    gk,
                    dimensionFor(static_cast<float>(rasterSizePx)),
                    dimensionFor(static_cast<float>(rasterSizePx)),
                    0.0f,
                });
                if (!glyph.ok()) {
                    char buf[256];
                    std::snprintf(buf,
                        sizeof(buf),
                        "resolveTextFast glyph not in atlas (needs pre-warming): "
                        "fontToken='%s' codepoint=0x%x(%c) rasterSizePx=%u text='%s'",
                        command.fontToken.c_str(),
                        codepoint,
                        codepoint < 128 ? static_cast<char>(codepoint) : '?',
                        rasterSizePx,
                        command.text.c_str());
                    return UnifiedUiAtlasResolveResult::failure(text(buf));
                }
                if (!glyph.cacheHit()) {
                    // Not pre-warmed - fall back to the full resolve path for
                    // this command so the glyph is rasterized + uploaded.
                    char buf[256];
                    std::snprintf(buf,
                        sizeof(buf),
                        "resolveTextFast glyph miss - glyph was not pre-warmed: "
                        "fontToken='%s' codepoint=0x%x(%c) rasterSizePx=%u text='%s'",
                        command.fontToken.c_str(),
                        codepoint,
                        codepoint < 128 ? static_cast<char>(codepoint) : '?',
                        rasterSizePx,
                        command.text.c_str());
                    return UnifiedUiAtlasResolveResult::failure(text(buf));
                }
                ++telemetry_.glyphHits;
                const auto& entry = glyph.entry();
                // SDF glyphs are rasterized at a fixed sample size
                // (kSdfSampleSizePx, e.g. 48px) but rendered at the on-screen
                // font size (sizePx, e.g. 12px). Scale the atlas entry's pixel
                // dimensions and advance down to the render size so the glyph
                // appears at the correct size. AlphaMask glyphs are rasterized
                // at the render size, so the scale factor is 1.0.
                const float renderScale =
                    sdfFont ? static_cast<float>(sizePx) / static_cast<float>(rasterSizePx) : 1.0f;
                // Position the glyph quad using the bearing stored in the atlas
                // entry (bitmap_left and ascender-bitmap_top in raster space).
                // cursorX already tracks the pen advance; bearingX is the
                // additional horizontal offset from the pen to the ink left edge.
                // bearingY is the vertical offset from the text origin (top of
                // the em box) to the ink top edge, so the glyph sits on the
                // baseline rather than flush against the top of command.rect.
                // Build the GlyphQuad command from the atlas entry.
                UiDrawCommand glyphCommand;
                glyphCommand.kind = UiDrawCommandKind::GlyphQuad;
                glyphCommand.widgetId = command.widgetId;
                glyphCommand.rect = arp::ui::LayoutRect::fromOriginSize(
                    command.rect.x + cursorX + entry.bearingX * renderScale,
                    command.rect.y + entry.bearingY * renderScale,
                    static_cast<float>(entry.atlasRect.width) * renderScale,
                    static_cast<float>(entry.atlasRect.height) * renderScale);
                glyphCommand.clipRect = command.clipRect;
                glyphCommand.textureResourceId = logicalAtlasResourceId_;
                glyphCommand.textureSourceRect = normalizedGlyphAtlasRect(entry.atlasRect);
                glyphCommand.hasTextureSourceRect = true;
                glyphCommand.styleId = command.styleId;
                glyphCommand.fontToken = command.fontToken;
                glyphCommand.atlasPageIndex = entry.pageIndex;
                glyphCommand.hasAtlasPageIndex = true;
                glyphCommand.sdfMode = sdfFont;
                glyphCommand.sortKey = command.sortKey;
                glyphCommand.color[0] = command.color[0];
                glyphCommand.color[1] = command.color[1];
                glyphCommand.color[2] = command.color[2];
                glyphCommand.color[3] = command.color[3];
                if (clipGlyphCommand(glyphCommand)) {
                    resolved.push_back(std::move(glyphCommand));
                }
                // The advance moves the pen; it does not include bearingX
                // (bearingX is the ink offset, not the pen step).
                cursorX += (entry.advanceX > 0.0f ? entry.advanceX : 1.0f) * renderScale;
            }
        }

        return UnifiedUiAtlasResolveResult::success(std::move(resolved));
    }

    void recordUploadsCompleted(std::size_t count) const
    {
        telemetry_.uploadsCompleted += count;
        glyphAtlas_.clearPendingUploads();
    }

    const arp::core::pmr::Vector<GlyphAtlasUploadSchedule>& pendingUploads() const
    {
        return glyphAtlas_.pendingUploads();
    }

    // Returns the pending glyph uploads translated into combined-atlas absolute
    // coordinates (atlasRect.y offset by glyphPageOriginY_), which is the
    // coordinate space expected by GuiRenderer::updateAtlasTexture / the
    // RenderSystem::updateGuiAtlasTexture entry point. The glyph cache stores
    // page-local rects (origin 0,0 within the glyph page); the combined atlas
    // places the glyph page at the glyphPageOriginY_ row offset, so a direct
    // hand-off of pendingUploads() would make updateAtlasTexture read from the
    // base-atlas region instead of the glyph-page region and upload blank
    // pixels. Callers that drive dynamic glyph uploads must use this accessor
    // (or apply the offset themselves) — never pendingUploads() directly.
    arp::core::pmr::Vector<GlyphAtlasUploadSchedule> combinedAtlasPendingUploads() const
    {
        arp::core::pmr::Vector<GlyphAtlasUploadSchedule> result{
            &arp::core::defaultCpuMemoryResource()};
        result.reserve(glyphAtlas_.pendingUploads().size());
        for (const auto& upload : glyphAtlas_.pendingUploads()) {
            auto adjusted = upload;
            adjusted.atlasRect = arp::ui::LayoutRect::fromOriginSize(upload.atlasRect.x,
                upload.atlasRect.y + static_cast<float>(glyphPageOriginY_),
                upload.atlasRect.width,
                upload.atlasRect.height);
            result.push_back(adjusted);
        }
        return result;
    }

    const GlyphAtlasCache& glyphAtlas() const { return glyphAtlas_; }

    bool exposesNativeBackendHandles() const { return false; }

    UnifiedUiAtlasRuntimeBuildDesc makeRecoveryPlan() const { return makeRecoveryPlan({}).plan(); }

    UiResourceRecoveryPlanResult makeRecoveryPlan(
        const UiResourceRecoveryRetentionPolicy& policy) const
    {
        UnifiedUiAtlasRuntimeBuildDesc plan;
        plan.baseAtlas = baseAtlas_;
        plan.fontFaces = &fontFaces_;
        plan.maxWidth = glyphAtlas_.desc().pageWidth;
        plan.maxHeight = glyphAtlas_.desc().pageHeight;
        plan.dynamicGlyphBudgetPerFrame = glyphAtlas_.desc().dynamicGlyphBudgetPerFrame;
        plan.shapingCapability = textLayout_.shapingCapability();
        plan.initialGeneration = glyphAtlas_.generation();
        plan.seedEntries = &glyphAtlas_.entries();
        plan.seedPages = &glyphAtlas_.pages();

        auto estimatedBytes = static_cast<std::uint64_t>(plan.baseAtlas.rgba8.size())
            + static_cast<std::uint64_t>(glyphAtlas_.entries().size() * sizeof(GlyphAtlasEntry))
            + static_cast<std::uint64_t>(glyphAtlas_.pages().size() * sizeof(GlyphAtlasPage));
        if (estimatedBytes > policy.maxRecoveryDataBytes) {
            return UiResourceRecoveryPlanResult::failure(
                text("UI resource recovery plan exceeds retained data budget."));
        }
        for (std::size_t i = 0; i < fontFaces_.size(); ++i) {
            const auto& source = fontFaces_[i].source;
            const auto duplicate = std::find_if(fontFaces_.begin(),
                fontFaces_.begin() + static_cast<std::ptrdiff_t>(i),
                [&](const auto& face) {
                    return face.source == source;
                });
            if (!source || duplicate != fontFaces_.begin() + static_cast<std::ptrdiff_t>(i))
                continue;
            if (source->bytes().size() > policy.maxRecoveryDataBytes - estimatedBytes) {
                return UiResourceRecoveryPlanResult::failure(
                    text("UI resource recovery plan exceeds retained data budget."));
            }
            estimatedBytes += source->bytes().size();
        }
        return UiResourceRecoveryPlanResult::success(std::move(plan));
    }

    UnifiedUiAtlasResolveResult resolve(const arp::core::pmr::Vector<UiDrawCommand>& commands) const
    {
        if (!ok_) {
            return UnifiedUiAtlasResolveResult::failure(diagnostics_);
        }
        arp::core::pmr::Vector<UiDrawCommand> resolved{&arp::core::defaultCpuMemoryResource()};
        resolved.reserve(commands.size());
        glyphAtlas_.beginFrame(++frameSequence_);

        for (const auto& command : commands) {
            if (command.kind != UiDrawCommandKind::Text) {
                resolved.push_back(command);
                continue;
            }

            auto layout = textLayout_.resolve(layoutKeyFor(command));
            if (!layout.ok()) {
                if (layout.diagnostics().find("shaping") != arp::core::pmr::String::npos) {
                    ++telemetry_.unsupportedScriptCount;
                }
                return UnifiedUiAtlasResolveResult::failure(layout.diagnostics());
            }

            for (const auto& quad : layout.run().glyphQuads) {
                const auto glyph = glyphAtlas_.resolveGlyph({
                    quad.glyph,
                    dimensionFor(quad.localRect.width),
                    dimensionFor(quad.localRect.height),
                    quad.advanceX,
                    quad.rasterOriginX,
                    quad.rasterOriginY,
                });
                if (!glyph.ok()) {
                    return UnifiedUiAtlasResolveResult::failure(glyph.diagnostics());
                }
                if (glyph.cacheHit()) {
                    ++telemetry_.glyphHits;
                } else {
                    ++telemetry_.glyphMisses;
                    const auto written = writeGlyphPixels(command, quad, glyph.entry());
                    if (!written.ok()) {
                        return UnifiedUiAtlasResolveResult::failure(written.diagnostics());
                    }
                    // TEMP DIAG: dump miss-glyph atlasRect + normalized UV to
                    // diagnose the "i i rrior" symptom (W/a rendering as i).
                    if (std::getenv("WOCC_ATLAS_DIAG") != nullptr) {
                        const auto& e = glyph.entry();
                        const auto nuv = normalizedGlyphAtlasRect(e.atlasRect);
                        std::fprintf(stderr,
                            "[atlas-diag] miss glyph=0x%x sizePx=%u entryRect=(%g,%g,%gx%g) "
                            "pageIdx=%u nuv=(%g,%g,%gx%g) glyphPageOriginY=%u atlasExtent=(%u,%u)\n",
                            quad.glyph.glyphId,
                            quad.glyph.sizePx,
                            e.atlasRect.x,
                            e.atlasRect.y,
                            e.atlasRect.width,
                            e.atlasRect.height,
                            e.pageIndex,
                            nuv.x,
                            nuv.y,
                            nuv.width,
                            nuv.height,
                            glyphPageOriginY_,
                            combinedAtlas_.extent.width,
                            combinedAtlas_.extent.height);
                    }
                }
                telemetry_.pageCount = glyphAtlas_.pageCount();

                // SDF glyphs: the quad geometry (localRect, advanceX) is in
                // raster-space pixels (kSdfSampleSizePx, e.g. 48px) because
                // layoutKeyFor set key.sizePx to the SDF sample size. Scale
                // down to the on-screen render size (face->sizePx, e.g. 12px)
                // so the glyph appears at the correct size. AlphaMask glyphs
                // are rasterized at the render size, so renderScale = 1.0.
                const bool sdfGlyph = (quad.glyph.renderMode == GlyphRenderMode::Sdf);
                float renderScale = 1.0f;
                if (sdfGlyph) {
                    std::uint32_t renderSizePx = dimensionFor(command.rect.height);
                    if (const auto* face = fontFaceFor(quad.glyph.fontToken)) {
                        renderSizePx = face->sizePx;
                    }
                    renderScale =
                        static_cast<float>(renderSizePx) / static_cast<float>(quad.glyph.sizePx);
                }

                UiDrawCommand glyphCommand;
                glyphCommand.kind = UiDrawCommandKind::GlyphQuad;
                glyphCommand.widgetId = command.widgetId;
                glyphCommand.rect = arp::ui::LayoutRect::fromOriginSize(
                    command.rect.x + quad.localRect.x * renderScale,
                    command.rect.y + quad.localRect.y * renderScale,
                    quad.localRect.width * renderScale,
                    quad.localRect.height * renderScale);
                glyphCommand.clipRect = command.clipRect;
                glyphCommand.textureResourceId = logicalAtlasResourceId_;
                glyphCommand.textureSourceRect = normalizedGlyphAtlasRect(glyph.entry().atlasRect);
                glyphCommand.hasTextureSourceRect = true;
                glyphCommand.styleId = command.styleId;
                glyphCommand.fontToken = quad.glyph.fontToken;
                glyphCommand.atlasPageIndex = glyph.entry().pageIndex;
                glyphCommand.hasAtlasPageIndex = true;
                glyphCommand.sdfMode = sdfGlyph;
                glyphCommand.sortKey = command.sortKey;
                glyphCommand.color[0] = command.color[0];
                glyphCommand.color[1] = command.color[1];
                glyphCommand.color[2] = command.color[2];
                glyphCommand.color[3] = command.color[3];
                if (!clipGlyphCommand(glyphCommand)) {
                    continue;
                }
                resolved.push_back(std::move(glyphCommand));
            }
        }

        return UnifiedUiAtlasResolveResult::success(std::move(resolved));
    }

private:
    static arp::core::pmr::String text(const char* value)
    {
        return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
    }

    struct WriteGlyphResult {
        bool success = true;
        arp::core::pmr::String message{&arp::core::defaultCpuMemoryResource()};

        bool ok() const { return success; }

        const arp::core::pmr::String& diagnostics() const { return message; }
    };

    static renderer::GuiAtlasTextureDesc makeCombinedAtlas(
        const renderer::GuiAtlasTextureDesc& baseAtlas,
        std::uint32_t glyphPageWidth,
        std::uint32_t glyphPageHeight)
    {
        renderer::GuiAtlasTextureDesc atlas;
        atlas.resourceId = baseAtlas.resourceId;
        atlas.extent = {
            std::max(baseAtlas.extent.width, glyphPageWidth),
            baseAtlas.extent.height + glyphPageHeight,
        };
        atlas.srgb = baseAtlas.srgb;
        atlas.rgba8.resize(static_cast<std::size_t>(atlas.extent.width)
                * static_cast<std::size_t>(atlas.extent.height) * 4u,
            0u);
        for (std::uint32_t y = 0u; y < baseAtlas.extent.height; ++y) {
            const auto sourceOffset = (static_cast<std::size_t>(y) * baseAtlas.extent.width) * 4u;
            const auto targetOffset = (static_cast<std::size_t>(y) * atlas.extent.width) * 4u;
            std::copy(baseAtlas.rgba8.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
                baseAtlas.rgba8.begin()
                    + static_cast<std::ptrdiff_t>(
                        sourceOffset + static_cast<std::size_t>(baseAtlas.extent.width) * 4u),
                atlas.rgba8.begin() + static_cast<std::ptrdiff_t>(targetOffset));
        }
        return atlas;
    }

    static std::uint32_t dimensionFor(float value)
    {
        return std::max(1u, static_cast<std::uint32_t>(std::ceil(value)));
    }

    TextLayoutKey layoutKeyFor(const UiDrawCommand& command) const
    {
        TextLayoutKey key;
        key.text = command.text;
        key.styleId = command.styleId;
        key.fontToken = command.fontToken;
        key.locale = "en-US";
        key.sizePx = dimensionFor(command.rect.height);
        if (const auto* face = fontFaceFor(command.fontToken)) {
            key.sizePx = face->sizePx;
            // SDF fonts: the GlyphKey (and thus the atlas entry) must use the
            // fixed SDF sample size so one glyph serves all render sizes.
            // The renderMode is propagated to the GlyphQuad -> GlyphKey.
            if (face->renderMode == "sdf") {
                key.sizePx = kSdfSampleSizePx;
                key.renderMode = GlyphRenderMode::Sdf;
            }
        }
        key.maxWidthPx = dimensionFor(command.rect.width);
        key.scaleBucket = 1u;
        key.script = command.fontToken == "font.code.cjk" ? TextScript::Han : TextScript::Latin;
        key.direction = TextDirection::LeftToRight;
        return key;
    }

    const arp::ui::UiFontFaceDesc* fontFaceFor(const arp::core::pmr::String& fontToken) const
    {
        const auto found = std::find_if(
            fontFaces_.begin(), fontFaces_.end(), [&](const arp::ui::UiFontFaceDesc& face) {
                return face.tokenId == fontToken;
            });
        return found == fontFaces_.end() ? nullptr : &*found;
    }

    /// Fixed sample size (in pixels) for SDF glyphs. One SDF glyph is rasterized
    /// at this size and the distance field is stored once per character. The
    /// fragment shader reconstructs sharp edges at any render size via
    /// smoothstep, so this is independent of the on-screen font size.
    static constexpr std::uint32_t kSdfSampleSizePx = 48u;

    /// SDF spread (in pixels) used for FreeType's FT_RENDER_MODE_SDF. Matches the
    /// default FreeType spread and the value set in rasterizeWithFaceSdf(). Also
    /// used as the atlas entry padding so the full distance gradient is captured.
    static constexpr std::uint32_t kSdfSpreadPx = 8u;

    /// Returns true if the font face with the given token is configured for SDF
    /// rendering (renderMode == "sdf"). Falls back to the first font face if the
    /// token is not found.
    bool isSdfFont(const arp::core::pmr::String& fontToken) const
    {
        const auto* face = fontFaceFor(fontToken);
        if (face != nullptr) {
            return face->renderMode == "sdf";
        }
        return !fontFaces_.empty() && fontFaces_.front().renderMode == "sdf";
    }

    arp::ui::UiFontFaceDesc resolveFontFace(const GlyphQuad& quad, WriteGlyphResult& result) const
    {
        if (const auto* face = fontFaceFor(quad.glyph.fontToken)) {
            auto resolved = *face;
            resolved.renderMode = quad.glyph.renderMode == GlyphRenderMode::Sdf ? "sdf" : "alpha";
            // SDF fonts rasterize at a fixed sample size (kSdfSampleSizePx)
            // so one glyph entry serves all render sizes. AlphaMask fonts
            // rasterize at the actual on-screen size.
            const auto rasterSizePx =
                (resolved.renderMode == "sdf") ? kSdfSampleSizePx : quad.glyph.sizePx;
            resolved.sizePx = rasterSizePx;
            resolved.lineHeightPx = std::max(rasterSizePx, resolved.lineHeightPx);
            return resolved;
        }
        result.success = false;
        result.message = text("Unified UI atlas runtime has no font face for token: ");
        result.message += quad.glyph.fontToken;
        return {};
    }

    arp::ui::LayoutRect normalizedGlyphAtlasRect(const arp::ui::LayoutRect& rect) const
    {
        return arp::ui::LayoutRect::fromOriginSize(
            rect.x / static_cast<float>(combinedAtlas_.extent.width),
            (static_cast<float>(glyphPageOriginY_) + rect.y)
                / static_cast<float>(combinedAtlas_.extent.height),
            rect.width / static_cast<float>(combinedAtlas_.extent.width),
            rect.height / static_cast<float>(combinedAtlas_.extent.height));
    }

    static bool clipGlyphCommand(UiDrawCommand& command)
    {
        if (!command.clipRect.valid()) {
            return true;
        }

        // Clip horizontally (line wrapping) but NOT vertically: glyph ink
        // (ascenders/descenders) legitimately extends above/below the text
        // widget's bounding box. Vertical clipping would truncate the tops of
        // tall glyphs (T, l, capitals) and the bottoms of descenders (g, p, y).
        // This matches Unity/TextMeshPro behaviour where the text box only
        // constrains horizontal extent, not vertical.
        const auto left = std::max(command.rect.x, command.clipRect.x);
        const auto right = std::min(
            command.rect.x + command.rect.width, command.clipRect.x + command.clipRect.width);
        if (right <= left || command.rect.width <= 0.0f || command.rect.height <= 0.0f) {
            return false;
        }

        const auto u0 = (left - command.rect.x) / command.rect.width;
        const auto u1 = (right - command.rect.x) / command.rect.width;
        command.textureSourceRect = arp::ui::LayoutRect::fromOriginSize(
            command.textureSourceRect.x + command.textureSourceRect.width * u0,
            command.textureSourceRect.y,
            command.textureSourceRect.width * (u1 - u0),
            command.textureSourceRect.height);
        command.rect = arp::ui::LayoutRect::fromOriginSize(
            left, command.rect.y, right - left, command.rect.height);
        return command.textureSourceRect.valid();
    }

    WriteGlyphResult writeGlyphPixels(
        const UiDrawCommand& command, const GlyphQuad& quad, const GlyphAtlasEntry& entry) const
    {
        arp::ui::UiFontRasterizeDesc rasterDesc;
        WriteGlyphResult result;
        rasterDesc.face = resolveFontFace(quad, result);
        if (!result.ok()) {
            return result;
        }
        rasterDesc.text = arp::core::pmr::String(&arp::core::defaultCpuMemoryResource());
        if (!arp::core::appendUtf8Codepoint(rasterDesc.text, quad.glyph.glyphId, result.message)) {
            result.success = false;
            result.message =
                text("Unified UI atlas runtime cannot encode an invalid glyph codepoint.");
            return result;
        }
        rasterDesc.originX = 0.0f;
        rasterDesc.originY = 0.0f;
        rasterDesc.maxX = std::max(
            static_cast<float>(quad.glyph.sizePx), quad.rasterOriginX + quad.localRect.width);
        rasterDesc.maxY = std::max(
            static_cast<float>(quad.glyph.sizePx), quad.rasterOriginY + quad.localRect.height);

        const auto rasterized = arp::ui::UiFontTextRasterizer(fontFaceCache_).rasterize(rasterDesc);
        if (!rasterized.ok()) {
            return {false, rasterized.diagnostic().message};
        }
        for (const auto& pixel : rasterized.text().pixels) {
            const auto alpha = static_cast<std::uint8_t>(
                std::lround(std::clamp(pixel.alpha, 0.0f, 1.0f) * 255.0f));
            if (alpha == 0u) {
                continue;
            }
            const auto localX = pixel.x - quad.rasterOriginX;
            const auto localY = pixel.y - quad.rasterOriginY;
            if (localX + pixel.width <= 0.0f || localY + pixel.height <= 0.0f
                || localX >= entry.atlasRect.width || localY >= entry.atlasRect.height) {
                continue;
            }
            const auto x0 =
                static_cast<std::uint32_t>(std::max(0.0f, std::floor(entry.atlasRect.x + localX)));
            const auto y0 = static_cast<std::uint32_t>(std::max(0.0f,
                std::floor(static_cast<float>(glyphPageOriginY_) + entry.atlasRect.y + localY)));
            const auto x1 =
                static_cast<std::uint32_t>(std::min(static_cast<float>(combinedAtlas_.extent.width),
                    std::ceil(static_cast<float>(x0) + std::max(1.0f, pixel.width))));
            const auto y1 = static_cast<std::uint32_t>(
                std::min(static_cast<float>(combinedAtlas_.extent.height),
                    std::ceil(static_cast<float>(y0) + std::max(1.0f, pixel.height))));
            for (std::uint32_t y = y0; y < y1; ++y) {
                for (std::uint32_t x = x0; x < x1; ++x) {
                    const auto offset =
                        (static_cast<std::size_t>(y) * combinedAtlas_.extent.width + x) * 4u;
                    combinedAtlas_.rgba8[offset + 0u] = 255u;
                    combinedAtlas_.rgba8[offset + 1u] = 255u;
                    combinedAtlas_.rgba8[offset + 2u] = 255u;
                    combinedAtlas_.rgba8[offset + 3u] = alpha;
                }
            }
        }
        return {};
    }

    bool ok_ = false;
    renderer::GuiAtlasTextureDesc baseAtlas_;
    mutable renderer::GuiAtlasTextureDesc combinedAtlas_;
    std::uint32_t glyphPageOriginY_ = 0u;
    arp::core::pmr::String diagnostics_{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String logicalAtlasResourceId_{&arp::core::defaultCpuMemoryResource()};
    mutable UnifiedUiAtlasRuntimeTelemetry telemetry_;
    mutable std::uint64_t frameSequence_ = 0u;
    // Persistent FreeType face cache: must be declared BEFORE textLayout_
    // because textLayout_ holds a raw pointer to it. On move, members are
    // initialized in declaration order, so fontFaceCache_ is moved first,
    // keeping its address stable for textLayout_'s pointer (which is then
    // re-pointed in the move constructor / build()). Mutable because
    // rasterize (a const concept) may need to warm a previously-unseen face.
    mutable arp::ui::UiFontFaceCache fontFaceCache_;
    mutable TextLayoutCache textLayout_;
    arp::core::pmr::Vector<arp::ui::UiFontFaceDesc> fontFaces_{
        &arp::core::defaultCpuMemoryResource()};
    mutable GlyphAtlasCache glyphAtlas_{{1u, 1u, 1u}};
};

} // namespace arp::renderer::ui
