#pragma once

#include "core/math/geometry.h"

#include <array>
#include <cmath>
#include <stdexcept>

namespace arp::core {

// Subtract a closed 2D rectangle from a triangle. Barycentric output preserves
// winding and lets callers interpolate arbitrary vertex attributes.
class TriangleRectClipper final {
public:
    using Point = std::array<float, 2>;
    using Weights = std::array<float, 3>;

    template <class Emit>
    static void subtract(
        const std::array<Point, 3>& triangle, const Rect2<float>& rect, Emit&& emit)
    {
        if (!rectHasPositiveArea(rect) || !std::isfinite(rect.x) || !std::isfinite(rect.y)
            || !std::isfinite(rect.width) || !std::isfinite(rect.height)) {
            throw std::invalid_argument("TRIANGLE-RECT-CLIP-RECT-INVALID");
        }

        struct Vertex {
            Point point;
            Weights weights;
        };

        using Polygon = std::array<Vertex, 7>;
        Polygon inside{};
        std::size_t count = 3;
        for (std::size_t i = 0; i < 3; ++i) {
            if (!std::isfinite(triangle[i][0]) || !std::isfinite(triangle[i][1]))
                throw std::invalid_argument("TRIANGLE-RECT-CLIP-POINT-INVALID");
            inside[i].point = triangle[i];
            inside[i].weights[i] = 1.0f;
        }
        const float edges[]{rect.x, rectMaxX(rect), rect.y, rectMaxY(rect)};
        for (std::size_t plane = 0; plane < 4 && count >= 3; ++plane) {
            const std::size_t axis = plane / 2;
            const float sign = (plane % 2 == 0) ? 1.0f : -1.0f;
            const auto distance = [&](const Vertex& v) {
                return sign * (v.point[axis] - edges[plane]);
            };
            Polygon kept{}, outside{};
            std::size_t keptCount = 0, outsideCount = 0;
            Vertex previous = inside[count - 1];
            float previousDistance = distance(previous);
            for (std::size_t i = 0; i < count; ++i) {
                const Vertex current = inside[i];
                const float currentDistance = distance(current);
                const bool previousInside = previousDistance >= 0.0f;
                const bool currentInside = currentDistance >= 0.0f;
                if (previousInside != currentInside) {
                    const float t = previousDistance / (previousDistance - currentDistance);
                    Vertex cut{};
                    for (std::size_t j = 0; j < 2; ++j)
                        cut.point[j] =
                            previous.point[j] + t * (current.point[j] - previous.point[j]);
                    cut.point[axis] = edges[plane];
                    for (std::size_t j = 0; j < 3; ++j)
                        cut.weights[j] =
                            previous.weights[j] + t * (current.weights[j] - previous.weights[j]);
                    kept[keptCount++] = cut;
                    outside[outsideCount++] = cut;
                }
                if (currentInside)
                    kept[keptCount++] = current;
                else
                    outside[outsideCount++] = current;
                previous = current;
                previousDistance = currentDistance;
            }
            for (std::size_t i = 1; i + 1 < outsideCount; ++i) {
                const auto& a = outside[0].point;
                const auto& b = outside[i].point;
                const auto& c = outside[i + 1].point;
                const double area = (double(b[0]) - a[0]) * (double(c[1]) - a[1])
                    - (double(b[1]) - a[1]) * (double(c[0]) - a[0]);
                if (area != 0.0)
                    emit(std::array<Weights, 3>{
                        outside[0].weights, outside[i].weights, outside[i + 1].weights});
            }
            inside = kept;
            count = keptCount;
        }
    }
};

} // namespace arp::core
