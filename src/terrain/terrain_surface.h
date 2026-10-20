#pragma once

#include "core/api.h"
#include "core/math.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace arp::terrain {

inline constexpr std::uint32_t kTerrainBaseLayerCount = 6u;

struct TerrainBounds final {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;

    bool valid() const noexcept { return minX < maxX && minZ < maxZ; }

    bool contains(float x, float z) const noexcept
    {
        return valid() && x >= minX && x <= maxX && z >= minZ && z <= maxZ;
    }
};

struct TerrainTileCoord final {
    std::int32_t x = 0;
    std::int32_t z = 0;

    friend bool operator==(TerrainTileCoord lhs, TerrainTileCoord rhs) noexcept
    {
        return lhs.x == rhs.x && lhs.z == rhs.z;
    }
};

struct TerrainTileCoordHash final {
    std::size_t operator()(TerrainTileCoord coord) const noexcept
    {
        const auto x = static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.x));
        const auto z = static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.z));
        const std::uint64_t packed = (x << 32u) | z;
        return static_cast<std::size_t>(packed ^ (packed >> 33u));
    }
};

enum class TerrainSurfaceQueryStatus : std::uint8_t {
    Ready,
    OutOfBounds,
    TileUnavailable,
    InvalidData,
};

struct TerrainSurfaceSample final {
    float height = 0.0f;
    core::Vec3 normal{0.0f, 1.0f, 0.0f};
    std::array<float, kTerrainBaseLayerCount> baseWeights{};
    float snowOverlay = 0.0f;
    float wetnessOverlay = 0.0f;
};

struct TerrainSurfaceQuery final {
    TerrainSurfaceQueryStatus status = TerrainSurfaceQueryStatus::TileUnavailable;
    TerrainSurfaceSample sample{};

    bool ready() const noexcept { return status == TerrainSurfaceQueryStatus::Ready; }
};

/// Immutable terrain query boundary shared by rendering, physics and PCG.
/// Implementations must never manufacture values for unavailable tiles.
class ARP_API TerrainSurface {
public:
    virtual ~TerrainSurface() = default;

    virtual TerrainBounds bounds() const noexcept = 0;
    virtual std::uint64_t generation() const noexcept = 0;
    virtual TerrainSurfaceQuery sample(float worldX, float worldZ) const = 0;
};

} // namespace arp::terrain
