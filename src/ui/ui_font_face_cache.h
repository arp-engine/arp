#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"

#include <cstdint>
#include <filesystem>

#if ARP_HAS_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace arp::ui {

/// Caches FreeType FT_Library + FT_Face handles + on-disk font bytes, keyed by
/// the font source path. Without this cache, UiFontTextRasterizer re-inits
/// FreeType, re-reads the .ttf file from disk, and re-parses the face on
/// *every* rasterize() call (twice per call, due to the validation step) -
/// which is the root cause of the ~2.5s stutter when TextLayoutCache misses.
///
/// Thread safety: NOT thread-safe. Callers must use it from a single thread
/// (the logic/main thread). The FT_Face handles are kept alive for the
/// lifetime of the cache, so repeated rasterize() calls only pay the cost of
/// FT_Load_Char (the actual glyph rasterization), not FT_Init_FreeType +
/// readFontBytes + FT_New_Memory_Face + FT_Done_Face.
class ARP_API UiFontFaceCache final {
public:
    struct CachedFace {
        arp::core::pmr::Vector<std::uint8_t> fontBytes{&arp::core::defaultCpuMemoryResource()};
#if ARP_HAS_FREETYPE
        FT_Library library = nullptr;
        FT_Face face = nullptr;
#else
        void* library = nullptr;
        void* face = nullptr;
#endif
        std::uint32_t currentSizePx = 0;
    };

    UiFontFaceCache() = default;
    ~UiFontFaceCache();

    // Movable but not copyable: the FT_Face/FT_Library handles are unique
    // resources. Move transfers ownership; the source is left empty.
    UiFontFaceCache(UiFontFaceCache&& other) noexcept;
    UiFontFaceCache& operator=(UiFontFaceCache&& other) noexcept;
    UiFontFaceCache(const UiFontFaceCache&) = delete;
    UiFontFaceCache& operator=(const UiFontFaceCache&) = delete;

    /// Acquire a cached (or newly-created) FT_Face for the given source path.
    /// On first call for a path: reads the font file, inits FreeType, creates
    /// the face, and caches it. On subsequent calls: returns the cached face,
    /// only calling FT_Set_Pixel_Sizes if sizePx changed.
    /// Returns nullptr on failure (with diagnostics populated).
    const CachedFace* acquire(const std::filesystem::path& sourcePath,
        std::uint32_t sizePx,
        arp::core::pmr::String& diagnostics);

    /// Resolve the FreeType kerning adjustment for a Unicode codepoint pair.
    /// Returns false only when the face or pair cannot be queried.
    bool kerningX(const std::filesystem::path& sourcePath,
        std::uint32_t sizePx,
        std::uint32_t leftCodepoint,
        std::uint32_t rightCodepoint,
        float& adjustmentX,
        arp::core::pmr::String& diagnostics);

    /// Release all cached faces and font bytes.
    void clear();

private:
    struct Entry {
        std::filesystem::path path;
        CachedFace face;
    };

    arp::core::pmr::Vector<Entry> entries_{&arp::core::defaultCpuMemoryResource()};
};

} // namespace arp::ui
