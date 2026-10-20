#pragma once
#include <cstdint>

namespace arp::editor::editor_atlas {
struct Region {
    std::uint32_t x, y, width, height;
};

inline constexpr std::uint32_t kWidth = 2048;
inline constexpr std::uint32_t kBaseHeight = 1024;
inline constexpr std::uint32_t kGlyphPageHeight = 2048;
inline constexpr std::uint32_t kHeight = kBaseHeight + kGlyphPageHeight;
inline constexpr std::uint32_t kCellSize = 32;
inline constexpr std::uint32_t kGutter = 4;
inline constexpr std::uint32_t kRoundedCorner = 6;
inline constexpr Region kCheck{1988, 4, 24, 24};
inline constexpr Region kRoundedSelection{1508, 68, 24, 24};
} // namespace arp::editor::editor_atlas
