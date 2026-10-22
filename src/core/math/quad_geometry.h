#pragma once

#include "core/math/math.h"

#include <array>
#include <cstdint>

namespace arp::core {

// A fixed topology shared by CPU-expanded sprite/UI geometry. The two
// triangles have the same winding and duplicate their common edge exactly.
class QuadTriangleTopology final {
public:
    static constexpr std::uint32_t vertexCount = 6;
    static constexpr std::array<Vec2, vertexCount> corners{
        Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 0}, Vec2{1, 1}, Vec2{0, 1}};

    static constexpr Vec2 affinePoint(Vec2 origin, Vec2 axisX, Vec2 axisY, Vec2 corner) noexcept
    {
        return {origin.x + axisX.x * corner.x + axisY.x * corner.y,
            origin.y + axisX.y * corner.x + axisY.y * corner.y};
    }
};
} // namespace arp::core
