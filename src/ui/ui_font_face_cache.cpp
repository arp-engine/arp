#include "ui/ui_font_face_cache.h"

#include "core/io/filesystem_path.h"
#include "ui/detail/ui_freetype_memory.h"

#include <limits>
#include <utility>

#if ARP_HAS_FREETYPE
#include FT_MODULE_H
#endif

namespace arp::ui {

UiFontFaceCache::CachedFace::~CachedFace()
{
#if ARP_HAS_FREETYPE
    if (face)
        FT_Done_Face(face);
    if (library)
        FT_Done_Library(library);
#endif
}

UiFontFaceCache::UiFontFaceCache(UiFontFaceCacheDesc desc) : desc_(desc)
{
    if (desc_.maxFaces && desc_.maxFaces != SIZE_MAX)
        entries_.reserve(desc_.maxFaces);
}

UiFontFaceCache::UiFontFaceCache(UiFontFaceCache&& other) noexcept
    : desc_(other.desc_), entries_(std::move(other.entries_)),
      residentSourceBytes_(std::exchange(other.residentSourceBytes_, 0))
{}

UiFontFaceCache& UiFontFaceCache::operator=(UiFontFaceCache&& other) noexcept
{
    if (this != &other) {
        clear();
        desc_ = other.desc_;
        entries_ = std::move(other.entries_);
        residentSourceBytes_ = std::exchange(other.residentSourceBytes_, 0);
    }
    return *this;
}

const UiFontFaceCache::CachedFace* UiFontFaceCache::acquire(
    const std::filesystem::path& path, std::uint32_t sizePx, core::pmr::String& diagnostic)
{
    UiFontFaceDesc desc;
    desc.sourcePath = path;
    desc.sizePx = sizePx;
    return acquire(desc, diagnostic);
}

const UiFontFaceCache::CachedFace* UiFontFaceCache::acquire(
    const UiFontFaceDesc& desc, core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!desc_.maxFaces || desc_.maxFaces == SIZE_MAX || !desc_.maxResidentSourceBytes
        || desc_.maxResidentSourceBytes == SIZE_MAX || !desc.sizePx || !desc.maxSourceBytes
        || desc.maxSourceBytes == SIZE_MAX) {
        diagnostic = "UI-FONT-CACHE-BUDGET-OR-SIZE";
        return nullptr;
    }
#if !ARP_HAS_FREETYPE
    diagnostic = "UI-FONT-FREETYPE-UNAVAILABLE";
    return nullptr;
#else
    auto source = desc.source;
    // The legacy host-path surface captures exactly once per cache lifetime.
    if (!source) {
        const auto path = core::FilePath::utf8(desc.sourcePath);
        for (const auto& entry : entries_)
            if (entry->source->desc().pathScope == core::io::FilePathScope::HostFilesystem
                && entry->source->desc().path == path) {
                source = entry->source;
                break;
            }
    }
    if (desc.source || !source)
        source = acquireUiFontSource(desc, diagnostic);
    if (!source)
        return nullptr;
    if (source->bytes().size() > desc.maxSourceBytes) {
        diagnostic = "UI-FONT-SOURCE-BUDGET";
        return nullptr;
    }
    for (const auto& entry : entries_)
        if (entry->source->matches(*source)) {
            if (entry->currentSizePx != desc.sizePx) {
                if (FT_Set_Pixel_Sizes(entry->face, 0, desc.sizePx)) {
                    diagnostic = "UI-FONT-CACHE-PIXEL-SIZE";
                    return nullptr;
                }
                entry->currentSizePx = desc.sizePx;
            }
            return entry.get();
        }
    if (entries_.size() >= desc_.maxFaces
        || source->bytes().size() > desc_.maxResidentSourceBytes - residentSourceBytes_) {
        diagnostic = "UI-FONT-CACHE-CAPACITY";
        return nullptr;
    }
    if (source->bytes().size() > static_cast<std::size_t>(std::numeric_limits<FT_Long>::max())) {
        diagnostic = "UI-FONT-CACHE-ADDRESS-RANGE";
        return nullptr;
    }
    auto entry = core::makeUnique<CachedFace>();
    entry->source = std::move(source);
    if (!detail::initializeFreeTypeLibrary(entry->library)) {
        diagnostic = "UI-FONT-CACHE-LIBRARY";
        return nullptr;
    }
    const auto bytes = entry->source->bytes();
    if (FT_New_Memory_Face(
            entry->library, bytes.data(), static_cast<FT_Long>(bytes.size()), 0, &entry->face)
        || !entry->face) {
        diagnostic = "UI-FONT-CACHE-FACE";
        return nullptr;
    }
    if (FT_Set_Pixel_Sizes(entry->face, 0, desc.sizePx)) {
        diagnostic = "UI-FONT-CACHE-PIXEL-SIZE";
        return nullptr;
    }
    entry->currentSizePx = desc.sizePx;
    const auto byteCount = bytes.size();
    entries_.push_back(std::move(entry));
    residentSourceBytes_ += byteCount;
    return entries_.back().get();
#endif
}

bool UiFontFaceCache::kerningX(const std::filesystem::path& path,
    std::uint32_t sizePx,
    std::uint32_t leftCodepoint,
    std::uint32_t rightCodepoint,
    float& adjustmentX,
    core::pmr::String& diagnostic)
{
    UiFontFaceDesc desc;
    desc.sourcePath = path;
    desc.sizePx = sizePx;
    return kerningX(desc, leftCodepoint, rightCodepoint, adjustmentX, diagnostic);
}

bool UiFontFaceCache::kerningX(const UiFontFaceDesc& desc,
    std::uint32_t leftCodepoint,
    std::uint32_t rightCodepoint,
    float& adjustmentX,
    core::pmr::String& diagnostic)
{
    adjustmentX = 0;
    const auto* cached = acquire(desc, diagnostic);
    if (!cached)
        return false;
#if !ARP_HAS_FREETYPE
    return false;
#else
    if (!FT_HAS_KERNING(cached->face))
        return true;
    const auto left = FT_Get_Char_Index(cached->face, static_cast<FT_ULong>(leftCodepoint));
    const auto right = FT_Get_Char_Index(cached->face, static_cast<FT_ULong>(rightCodepoint));
    if (!left || !right) {
        diagnostic = "UI-FONT-KERNING-MISSING-GLYPH";
        return false;
    }
    FT_Vector adjustment{};
    if (FT_Get_Kerning(cached->face, left, right, FT_KERNING_DEFAULT, &adjustment)) {
        diagnostic = "UI-FONT-KERNING-QUERY";
        return false;
    }
    adjustmentX = static_cast<float>(adjustment.x) / 64.0f;
    return true;
#endif
}

void UiFontFaceCache::clear() noexcept
{
    entries_.clear();
    residentSourceBytes_ = 0;
}

} // namespace arp::ui
