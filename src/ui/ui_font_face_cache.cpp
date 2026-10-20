#include "ui/ui_font_face_cache.h"

#include <algorithm>
#include <fstream>

namespace arp::ui {

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

arp::core::pmr::Vector<std::uint8_t> readFontBytes(const std::filesystem::path& path)
{
    arp::core::pmr::Vector<std::uint8_t> bytes(&arp::core::defaultCpuMemoryResource());
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        return bytes;
    }
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size <= 0) {
        return bytes;
    }
    input.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input.good()) {
        bytes.clear();
    }
    return bytes;
}

// Resolve a relative font sourcePath by walking up from the current working
// directory looking for a project root (a directory containing CMakeLists.txt).
std::filesystem::path resolveFontSourcePath(const std::filesystem::path& declaredPath)
{
    if (declaredPath.empty() || declaredPath.is_absolute()) {
        return declaredPath;
    }
    std::error_code ec;
    if (std::filesystem::exists(declaredPath, ec)) {
        return declaredPath;
    }
    auto current = std::filesystem::current_path(ec);
    if (ec) {
        return declaredPath;
    }
    for (;;) {
        if (std::filesystem::exists(current / "CMakeLists.txt", ec)) {
            const auto resolved = current / declaredPath;
            if (std::filesystem::exists(resolved, ec)) {
                return resolved;
            }
        }
        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    return declaredPath;
}

} // namespace

UiFontFaceCache::~UiFontFaceCache()
{
    clear();
}

UiFontFaceCache::UiFontFaceCache(UiFontFaceCache&& other) noexcept
    : entries_(std::move(other.entries_))
{}

UiFontFaceCache& UiFontFaceCache::operator=(UiFontFaceCache&& other) noexcept
{
    if (this != &other) {
        clear();
        entries_ = std::move(other.entries_);
    }
    return *this;
}

const UiFontFaceCache::CachedFace* UiFontFaceCache::acquire(const std::filesystem::path& sourcePath,
    std::uint32_t sizePx,
    arp::core::pmr::String& diagnostics)
{
    const auto resolvedPath = resolveFontSourcePath(sourcePath);

    // Linear search for existing entry (typical cache size: 1-5 fonts).
    for (auto& entry : entries_) {
        if (entry.path == resolvedPath) {
#if ARP_HAS_FREETYPE
            if (entry.face.currentSizePx != sizePx) {
                if (FT_Set_Pixel_Sizes(entry.face.face, 0, sizePx) != 0) {
                    diagnostics = text("FreeType failed to set UI font pixel size (cached)");
                    return nullptr;
                }
                entry.face.currentSizePx = sizePx;
            }
#else
            diagnostics = text("FreeType backend is not available");
            return nullptr;
#endif
            return &entry.face;
        }
    }

#if !ARP_HAS_FREETYPE
    diagnostics = text("FreeType backend is not available");
    return nullptr;
#else
    // Create new entry: read font bytes, init FreeType, create face.
    auto bytes = readFontBytes(resolvedPath);
    if (bytes.empty()) {
        auto message = text("UI font source file is empty or unreadable: ");
        message += resolvedPath.generic_string().c_str();
        diagnostics = std::move(message);
        return nullptr;
    }

    Entry entry;
    entry.path = resolvedPath;
    entry.face.fontBytes = std::move(bytes);

    if (FT_Init_FreeType(&entry.face.library) != 0 || entry.face.library == nullptr) {
        diagnostics = text("FreeType failed to initialize for cached UI font face");
        return nullptr;
    }

    const auto faceLoaded = FT_New_Memory_Face(entry.face.library,
        entry.face.fontBytes.data(),
        static_cast<FT_Long>(entry.face.fontBytes.size()),
        0,
        &entry.face.face);
    if (faceLoaded != 0 || entry.face.face == nullptr) {
        FT_Done_FreeType(entry.face.library);
        entry.face.library = nullptr;
        diagnostics = text("FreeType failed to load cached UI font face");
        return nullptr;
    }

    if (FT_Set_Pixel_Sizes(entry.face.face, 0, sizePx) != 0) {
        FT_Done_Face(entry.face.face);
        FT_Done_FreeType(entry.face.library);
        entry.face.face = nullptr;
        entry.face.library = nullptr;
        diagnostics = text("FreeType failed to set UI font pixel size (cached)");
        return nullptr;
    }
    entry.face.currentSizePx = sizePx;

    entries_.push_back(std::move(entry));
    return &entries_.back().face;
#endif
}

bool UiFontFaceCache::kerningX(const std::filesystem::path& sourcePath,
    std::uint32_t sizePx,
    std::uint32_t leftCodepoint,
    std::uint32_t rightCodepoint,
    float& adjustmentX,
    arp::core::pmr::String& diagnostics)
{
    adjustmentX = 0.0f;
#if !ARP_HAS_FREETYPE
    (void)sourcePath;
    (void)sizePx;
    (void)leftCodepoint;
    (void)rightCodepoint;
    diagnostics = text("FreeType backend is not available");
    return false;
#else
    const auto* cached = acquire(sourcePath, sizePx, diagnostics);
    if (cached == nullptr) {
        return false;
    }
    if (!FT_HAS_KERNING(cached->face)) {
        return true;
    }
    const auto leftGlyph = FT_Get_Char_Index(cached->face, static_cast<FT_ULong>(leftCodepoint));
    const auto rightGlyph = FT_Get_Char_Index(cached->face, static_cast<FT_ULong>(rightCodepoint));
    if (leftGlyph == 0u || rightGlyph == 0u) {
        diagnostics = text("FreeType kerning pair references a missing UI glyph");
        return false;
    }
    FT_Vector adjustment{};
    if (FT_Get_Kerning(cached->face, leftGlyph, rightGlyph, FT_KERNING_DEFAULT, &adjustment) != 0) {
        diagnostics = text("FreeType failed to query UI glyph kerning");
        return false;
    }
    adjustmentX = static_cast<float>(adjustment.x) / 64.0f;
    return true;
#endif
}

void UiFontFaceCache::clear()
{
#if ARP_HAS_FREETYPE
    for (auto& entry : entries_) {
        if (entry.face.face != nullptr) {
            FT_Done_Face(entry.face.face);
            entry.face.face = nullptr;
        }
        if (entry.face.library != nullptr) {
            FT_Done_FreeType(entry.face.library);
            entry.face.library = nullptr;
        }
    }
#endif
    entries_.clear();
}

} // namespace arp::ui
