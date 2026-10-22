#pragma once
#include "core/math/math.h"

namespace arp::core {
inline double signedTurn(Vec2 a, Vec2 b, Vec2 c)
{
    return (double(b.x) - a.x) * (double(c.y) - a.y) - (double(b.y) - a.y) * (double(c.x) - a.x);
}

inline bool pointOnSegment(Vec2 a, Vec2 b, Vec2 p)
{
    return signedTurn(a, b, p) == 0 && p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x)
        && p.y >= std::min(a.y, b.y) && p.y <= std::max(a.y, b.y);
}

inline bool segmentsTouch(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
{
    const auto ac = signedTurn(a, b, c), ad = signedTurn(a, b, d), ca = signedTurn(c, d, a),
               cb = signedTurn(c, d, b);
    if (((ac > 0 && ad < 0) || (ac < 0 && ad > 0)) && ((ca > 0 && cb < 0) || (ca < 0 && cb > 0)))
        return true;
    return pointOnSegment(a, b, c) || pointOnSegment(a, b, d) || pointOnSegment(c, d, a)
        || pointOnSegment(c, d, b);
}

template <class Range> bool simplePolyline(const Range& points, bool closed, float minimumSpacing)
{
    if (points.size() < 2 || !std::isfinite(minimumSpacing) || minimumSpacing < 0)
        return false;
    for (const auto& p : points)
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
            return false;
    const auto edges = closed ? points.size() : points.size() - 1;
    for (std::size_t i = 0; i < edges; ++i) {
        auto a = points[i], b = points[(i + 1) % points.size()];
        const auto delta = b - a;
        if (double(delta.x) * delta.x + double(delta.y) * delta.y
            <= double(minimumSpacing) * minimumSpacing)
            return false;
        for (std::size_t j = i + 1; j < edges; ++j) {
            if (j == i + 1 || (closed && i == 0 && j + 1 == edges))
                continue;
            if (segmentsTouch(a, b, points[j], points[(j + 1) % points.size()]))
                return false;
        }
    }
    return true;
}

template <class Range> bool strictlyConvexCcw(const Range& points)
{
    if (points.size() < 3 || !simplePolyline(points, true, 0))
        return false;
    for (std::size_t i = 0; i < points.size(); ++i)
        if (signedTurn(points[i], points[(i + 1) % points.size()], points[(i + 2) % points.size()])
            <= 0)
            return false;
    return true;
}
} // namespace arp::core
