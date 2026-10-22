#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

namespace arp::terrain {
// Runtime signed chunk identity is not a resource tile ID. Asset-origin
// indexing is explicitly selected by TerrainStreamerDesc::worldBounds.
struct ChunkCoord {
    int x = 0;
    int z = 0;

    bool operator==(const ChunkCoord& o) const noexcept { return x == o.x && z == o.z; }
};

} // namespace arp::terrain

// Hash specialization so ChunkCoord can be used as an unordered_map key.
namespace std {
template <> struct hash<arp::terrain::ChunkCoord> {
    std::size_t operator()(const arp::terrain::ChunkCoord& c) const noexcept
    {
        auto imul32 = [](std::uint32_t a, std::uint32_t b) -> std::uint32_t {
            return a * b;
        };
        std::uint32_t h = static_cast<std::uint32_t>(c.x);
        h = imul32(h ^ imul32(static_cast<std::uint32_t>(c.z), 374761393u), 668265263u);
        h ^= h >> 13;
        h = imul32(h, 1274126177u);
        h ^= h >> 16;
        return static_cast<std::size_t>(h);
    }
};
} // namespace std
