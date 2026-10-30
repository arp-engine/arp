#pragma once

#include "ui/ui_font.h"
#include "ui/ui_font_source.h"

#if ARP_HAS_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace arp::ui {

struct UiFontFaceCacheDesc {
    std::size_t maxFaces = 32;
    std::size_t maxResidentSourceBytes = 64u * 1024u * 1024u;
};

// Owner-thread cache. Prepared sources key provenance and content; path-only
// calls capture a HostFilesystem snapshot once until clear().
class ARP_API UiFontFaceCache final {
public:
    struct CachedFace {
        core::SharedPtr<const UiFontSource> source;
#if ARP_HAS_FREETYPE
        FT_Library library = nullptr;
        FT_Face face = nullptr;
#else
        void* library = nullptr;
        void* face = nullptr;
#endif
        std::uint32_t currentSizePx = 0;
        CachedFace() = default;
        ~CachedFace();
        CachedFace(const CachedFace&) = delete;
        CachedFace& operator=(const CachedFace&) = delete;
    };

    explicit UiFontFaceCache(UiFontFaceCacheDesc desc = {});
    ~UiFontFaceCache() = default;
    UiFontFaceCache(UiFontFaceCache&& other) noexcept;
    UiFontFaceCache& operator=(UiFontFaceCache&& other) noexcept;
    UiFontFaceCache(const UiFontFaceCache&) = delete;
    UiFontFaceCache& operator=(const UiFontFaceCache&) = delete;

    const CachedFace* acquire(const UiFontFaceDesc& desc, core::pmr::String& diagnostic);
    const CachedFace* acquire(
        const std::filesystem::path& path, std::uint32_t sizePx, core::pmr::String& diagnostic);
    bool kerningX(const UiFontFaceDesc& desc,
        std::uint32_t leftCodepoint,
        std::uint32_t rightCodepoint,
        float& adjustmentX,
        core::pmr::String& diagnostic);
    bool kerningX(const std::filesystem::path& path,
        std::uint32_t sizePx,
        std::uint32_t leftCodepoint,
        std::uint32_t rightCodepoint,
        float& adjustmentX,
        core::pmr::String& diagnostic);

    void clear() noexcept;

    std::size_t size() const noexcept { return entries_.size(); }

    std::size_t residentSourceBytes() const noexcept { return residentSourceBytes_; }

private:
    UiFontFaceCacheDesc desc_;
    core::pmr::Vector<core::UniquePtr<CachedFace>> entries_{&core::defaultCpuMemoryResource()};
    std::size_t residentSourceBytes_ = 0;
};

} // namespace arp::ui
