#pragma once

#include "core/memory/cpu_allocator.h"
#include "ui/ui.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace arp::renderer::ui {

enum class GlyphRenderMode {
    AlphaMask,
    ColorBitmap,
    /// Signed Distance Field: the glyph is rasterized once at a fixed sample
    /// size (e.g. 48px) and the distance field is stored in the atlas alpha
    /// channel. The fragment shader reconstructs sharp edges at any render
    /// size via smoothstep, so one atlas entry serves all sizes.
    Sdf,
};

struct GlyphKey {
    arp::core::pmr::String fontToken{&arp::core::defaultCpuMemoryResource()};
    std::uint32_t glyphId = 0u;
    std::uint32_t sizePx = 0u;
    GlyphRenderMode renderMode = GlyphRenderMode::AlphaMask;
    std::uint32_t scaleBucket = 0u;

    friend bool operator==(const GlyphKey& lhs, const GlyphKey& rhs)
    {
        return lhs.fontToken == rhs.fontToken && lhs.glyphId == rhs.glyphId
            && lhs.sizePx == rhs.sizePx && lhs.renderMode == rhs.renderMode
            && lhs.scaleBucket == rhs.scaleBucket;
    }
};

struct GlyphAtlasEntry {
    std::uint64_t entryId = 0u;
    GlyphKey key;
    std::uint32_t pageIndex = 0u;
    arp::ui::LayoutRect atlasRect;
    float advanceX = 0.0f;
    /// Horizontal offset from the pen origin to the glyph's left edge, in
    /// raster-space pixels. Equals FreeType's bitmap_left. Used to position
    /// the glyph quad when bypassing TextLayoutCache (resolveTextFast).
    float bearingX = 0.0f;
    /// Vertical offset from the pen origin (top of the em box) to the glyph's
    /// top edge, in raster-space pixels. Equals (ascender - bitmap_top), i.e.
    /// how far down from the origin the glyph ink starts. Used to position the
    /// glyph quad on the baseline in resolveTextFast.
    float bearingY = 0.0f;
    std::uint64_t generation = 0u;
};

struct GlyphAtlasPage {
    std::uint32_t pageIndex = 0u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t cursorX = 0u;
    std::uint32_t cursorY = 0u;
    std::uint32_t rowHeight = 0u;
};

enum class GlyphAtlasInsertionPolicy {
    Static,
    Dynamic,
};

enum class GlyphAtlasPageAllocationPolicy {
    SinglePage,
    MultiPage,
};

struct GlyphAtlasUploadSchedule {
    std::uint64_t frameSequence = 0u;
    std::uint32_t pageIndex = 0u;
    arp::ui::LayoutRect atlasRect;
    std::uint32_t byteSize = 0u;
    std::uint64_t generation = 0u;
};

struct GlyphAtlasCacheDesc {
    std::uint32_t pageWidth = 0u;
    std::uint32_t pageHeight = 0u;
    std::uint32_t maxPages = 0u;
    GlyphAtlasInsertionPolicy insertionPolicy = GlyphAtlasInsertionPolicy::Dynamic;
    GlyphAtlasPageAllocationPolicy pagePolicy = GlyphAtlasPageAllocationPolicy::MultiPage;
    std::uint32_t dynamicGlyphBudgetPerFrame = std::numeric_limits<std::uint32_t>::max();
    /// Padding (in pixels) added around each glyph entry in the atlas. Required
    /// for SDF glyphs so bilinear sampling at the edges does not bleed into
    /// neighbouring glyphs. AlphaMask glyphs can use 0 (tightly packed).
    std::uint32_t entryPadding = 0u;
};

struct GlyphAtlasRequest {
    GlyphKey key;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    float advanceX = 0.0f;
    /// Pen-origin-to-ink offset (raster-space). See GlyphAtlasEntry::bearingX/Y.
    float bearingX = 0.0f;
    float bearingY = 0.0f;
};

class GlyphAtlasResolveResult final {
public:
    static GlyphAtlasResolveResult success(
        GlyphAtlasEntry entry, bool cacheHit, GlyphAtlasUploadSchedule upload = {})
    {
        GlyphAtlasResolveResult result;
        result.ok_ = true;
        result.entry_ = std::move(entry);
        result.cacheHit_ = cacheHit;
        result.upload_ = upload;
        result.uploadScheduled_ = !cacheHit && upload.byteSize > 0u;
        return result;
    }

    static GlyphAtlasResolveResult failure(arp::core::pmr::String diagnostics)
    {
        GlyphAtlasResolveResult result;
        result.ok_ = false;
        result.diagnostics_ = std::move(diagnostics);
        return result;
    }

    bool ok() const { return ok_; }

    const arp::core::pmr::String& diagnostics() const { return diagnostics_; }

    const GlyphAtlasEntry& entry() const { return entry_; }

    bool cacheHit() const { return cacheHit_; }

    bool uploadScheduled() const { return uploadScheduled_; }

    const GlyphAtlasUploadSchedule& upload() const { return upload_; }

    bool exposesNativeBackendHandles() const { return false; }

private:
    bool ok_ = false;
    bool cacheHit_ = false;
    bool uploadScheduled_ = false;
    GlyphAtlasEntry entry_;
    GlyphAtlasUploadSchedule upload_;
    arp::core::pmr::String diagnostics_{&arp::core::defaultCpuMemoryResource()};
};

class GlyphAtlasCache final {
public:
    explicit GlyphAtlasCache(GlyphAtlasCacheDesc desc) : desc_(desc) {}

    void beginFrame(std::uint64_t frameSequence)
    {
        currentFrameSequence_ = frameSequence;
        dynamicGlyphsThisFrame_ = 0u;
        pendingUploads_.clear();
    }

    void setGeneration(std::uint64_t generation) { currentGeneration_ = generation; }

    std::uint64_t generation() const { return currentGeneration_; }

    void seedPage(const GlyphAtlasPage& page) { pages_.push_back(page); }

    void seedEntry(const GlyphAtlasEntry& entry)
    {
        entries_.push_back(entry);
        nextEntryId_ = std::max(nextEntryId_, entry.entryId + 1u);
    }

    GlyphAtlasResolveResult resolveGlyph(const GlyphAtlasRequest& request)
    {
        if (request.key.fontToken.empty()) {
            return GlyphAtlasResolveResult::failure(text("UI glyph atlas missing font token."));
        }
        if (request.key.glyphId == 0u) {
            return GlyphAtlasResolveResult::failure(text("UI glyph atlas missing glyph id."));
        }
        if (request.key.sizePx == 0u || request.width == 0u || request.height == 0u) {
            return GlyphAtlasResolveResult::failure(
                text("UI glyph atlas request requires explicit non-zero size."));
        }
        if (desc_.pageWidth == 0u || desc_.pageHeight == 0u || desc_.maxPages == 0u) {
            return GlyphAtlasResolveResult::failure(
                text("UI glyph atlas cache requires explicit page dimensions and budget."));
        }
        if (request.width > desc_.pageWidth || request.height > desc_.pageHeight) {
            return GlyphAtlasResolveResult::failure(
                text("UI glyph atlas entry exceeds page dimensions."));
        }

        const auto found =
            std::find_if(entries_.begin(), entries_.end(), [&](const GlyphAtlasEntry& entry) {
                return entry.key == request.key;
            });
        if (found != entries_.end()) {
            ++cacheHitCount_;
            return GlyphAtlasResolveResult::success(*found, true);
        }
        if (desc_.insertionPolicy == GlyphAtlasInsertionPolicy::Static) {
            return GlyphAtlasResolveResult::failure(
                text("UI glyph atlas static policy disallows dynamic glyph insertion."));
        }
        if (dynamicGlyphsThisFrame_ >= desc_.dynamicGlyphBudgetPerFrame) {
            return GlyphAtlasResolveResult::failure(
                text("UI glyph atlas dynamic glyph budget exhausted."));
        }

        // Account for entry padding when allocating space in the page. The
        // padding forms a gutter around each glyph so bilinear sampling at the
        // edges does not bleed into neighbouring glyphs (required for SDF).
        const auto pad = desc_.entryPadding;
        const auto allocWidth = request.width + pad * 2u;
        const auto allocHeight = request.height + pad * 2u;
        auto* page = writablePageFor(allocWidth, allocHeight);
        if (page == nullptr) {
            if (desc_.pagePolicy == GlyphAtlasPageAllocationPolicy::SinglePage) {
                return GlyphAtlasResolveResult::failure(
                    text("UI glyph atlas single-page policy exhausted."));
            }
            return GlyphAtlasResolveResult::failure(text("UI glyph atlas page budget exhausted."));
        }

        GlyphAtlasEntry entry;
        entry.entryId = nextEntryId_++;
        entry.key = request.key;
        entry.pageIndex = page->pageIndex;
        // The atlasRect covers only the glyph pixels (padding is outside),
        // so UVs map 1:1 to the glyph. The cursor starts at (cursorX + pad,
        // cursorY + pad) so the padding gutter surrounds the glyph.
        entry.atlasRect =
            arp::ui::LayoutRect::fromOriginSize(static_cast<float>(page->cursorX + pad),
                static_cast<float>(page->cursorY + pad),
                static_cast<float>(request.width),
                static_cast<float>(request.height));
        entry.advanceX = request.advanceX;
        entry.bearingX = request.bearingX;
        entry.bearingY = request.bearingY;
        entry.generation = currentGeneration_;

        page->cursorX += allocWidth;
        page->rowHeight = std::max(page->rowHeight, allocHeight);

        entries_.push_back(entry);
        ++cacheMissCount_;
        ++dynamicGlyphsThisFrame_;

        GlyphAtlasUploadSchedule upload;
        upload.frameSequence = currentFrameSequence_;
        upload.pageIndex = entry.pageIndex;
        upload.atlasRect = entry.atlasRect;
        upload.byteSize = request.width * request.height;
        upload.generation = currentGeneration_;
        pendingUploads_.push_back(upload);
        return GlyphAtlasResolveResult::success(std::move(entry), false, upload);
    }

    const GlyphAtlasCacheDesc& desc() const { return desc_; }

    const arp::core::pmr::Vector<GlyphAtlasEntry>& entries() const { return entries_; }

    const arp::core::pmr::Vector<GlyphAtlasPage>& pages() const { return pages_; }

    const arp::core::pmr::Vector<GlyphAtlasUploadSchedule>& pendingUploads() const
    {
        return pendingUploads_;
    }

    void clearPendingUploads() { pendingUploads_.clear(); }

    std::size_t entryCount() const { return entries_.size(); }

    std::size_t pageCount() const { return pages_.size(); }

    std::size_t cacheHitCount() const { return cacheHitCount_; }

    std::size_t cacheMissCount() const { return cacheMissCount_; }

    bool exposesNativeBackendHandles() const { return false; }

private:
    static arp::core::pmr::String text(const char* value)
    {
        return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
    }

    GlyphAtlasPage* writablePageFor(std::uint32_t width, std::uint32_t height)
    {
        if (pages_.empty()) {
            if (!appendPage()) {
                return nullptr;
            }
        }

        GlyphAtlasPage* page = &pages_.back();
        if (page->cursorX + width > page->width) {
            page->cursorX = 0u;
            page->cursorY += page->rowHeight;
            page->rowHeight = 0u;
        }
        if (page->cursorY + height <= page->height) {
            return page;
        }
        if (desc_.pagePolicy == GlyphAtlasPageAllocationPolicy::SinglePage) {
            return nullptr;
        }
        if (!appendPage()) {
            return nullptr;
        }
        return &pages_.back();
    }

    bool appendPage()
    {
        if (pages_.size() >= desc_.maxPages) {
            return false;
        }
        GlyphAtlasPage page;
        page.pageIndex = static_cast<std::uint32_t>(pages_.size());
        page.width = desc_.pageWidth;
        page.height = desc_.pageHeight;
        pages_.push_back(std::move(page));
        return true;
    }

    GlyphAtlasCacheDesc desc_;
    arp::core::pmr::Vector<GlyphAtlasPage> pages_{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<GlyphAtlasEntry> entries_{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<GlyphAtlasUploadSchedule> pendingUploads_{
        &arp::core::defaultCpuMemoryResource()};
    std::uint64_t nextEntryId_ = 1u;
    std::uint64_t currentFrameSequence_ = 0u;
    std::uint64_t currentGeneration_ = 1u;
    std::uint32_t dynamicGlyphsThisFrame_ = 0u;
    std::size_t cacheHitCount_ = 0u;
    std::size_t cacheMissCount_ = 0u;
};

} // namespace arp::renderer::ui
