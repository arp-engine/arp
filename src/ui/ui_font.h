#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"

#include <cstdint>
#include <filesystem>

namespace arp::ui {

enum class UiFontDiagnosticCode {
    None,
    InvalidFontMetadata,
    InvalidUtf8Text,
    MissingFontFile,
    FreeTypeUnavailable,
    FreeTypeLoadFailed,
    DefaultFontUnavailable,
    MissingGlyph,
};

struct UiFontDiagnostic {
    UiFontDiagnosticCode code = UiFontDiagnosticCode::None;
    arp::core::pmr::String message{&arp::core::defaultCpuMemoryResource()};
};

struct UiFontFaceDesc {
    arp::core::pmr::String tokenId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String family{&arp::core::defaultCpuMemoryResource()};
    std::filesystem::path sourcePath;
    arp::core::pmr::Vector<arp::core::pmr::String> fallbackChain{
        &arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<arp::core::pmr::String> supportedScripts{
        &arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String charset{&arp::core::defaultCpuMemoryResource()};
    std::uint32_t sizePx = 0;
    std::uint32_t weight = 400;
    std::uint32_t lineHeightPx = 0;
    /// Rasterization mode: "alpha" (default, full-coverage AlphaMask at
    /// sizePx) or "sdf" (Signed Distance Field for explicitly scalable text).
    arp::core::pmr::String renderMode{"alpha", &arp::core::defaultCpuMemoryResource()};
};

struct UiFontFace {
    UiFontFaceDesc desc;
    bool validatedByFreeType = false;
};

struct UiFontRasterPixel {
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
    float alpha = 1.0f;
};

struct UiFontRasterizeDesc {
    UiFontFaceDesc face;
    arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};
    float originX = 0.0f;
    float originY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
};

struct UiFontRasterizedText {
    arp::core::pmr::Vector<UiFontRasterPixel> pixels{&arp::core::defaultCpuMemoryResource()};
    float advanceX = 0.0f;
    std::uint32_t glyphCount = 0u;
};

class UiFontLoadResult final {
public:
    static UiFontLoadResult success(UiFontFace face);
    static UiFontLoadResult failure(UiFontDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == UiFontDiagnosticCode::None; }

    const UiFontDiagnostic& diagnostic() const { return diagnostic_; }

    const UiFontFace& face() const { return face_; }

private:
    UiFontDiagnostic diagnostic_;
    UiFontFace face_;
};

class UiFontRasterizeResult final {
public:
    static UiFontRasterizeResult success(UiFontRasterizedText text);
    static UiFontRasterizeResult failure(UiFontDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == UiFontDiagnosticCode::None; }

    const UiFontDiagnostic& diagnostic() const { return diagnostic_; }

    const UiFontRasterizedText& text() const { return text_; }

private:
    UiFontDiagnostic diagnostic_;
    UiFontRasterizedText text_;
};

class ARP_API UiFontFaceLoader final {
public:
    UiFontLoadResult load(const UiFontFaceDesc& desc) const;
};

class ARP_API UiFontFaceCache;

class ARP_API UiFontTextRasterizer final {
public:
    /// Construct a stateless rasterizer (legacy path: re-inits FreeType every
    /// call). Existing tests and standalone callers use this default.
    UiFontTextRasterizer() = default;

    /// Construct a rasterizer backed by a persistent FT_Face cache. When
    /// cache is non-null, rasterize() skips FT_Init/FT_Done and re-reads of
    /// the font file, reducing miss cost from ~12ms to ~0.1ms per glyph.
    explicit UiFontTextRasterizer(UiFontFaceCache& cache) : cache_(&cache) {}

    UiFontRasterizeResult rasterize(const UiFontRasterizeDesc& desc) const;

private:
    UiFontFaceCache* cache_ = nullptr;
};

ARP_API bool uiFreeTypeBackendAvailable();
ARP_API UiFontLoadResult loadDefaultUiFontFace();

} // namespace arp::ui
