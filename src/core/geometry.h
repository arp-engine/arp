#pragma once

#include <algorithm>

namespace arp::core {

template <typename T> struct Rect2 {
    T x{};
    T y{};
    T width{};
    T height{};
};

template <typename T> constexpr T rectMinX(const Rect2<T>& rect)
{
    return rect.x;
}

template <typename T> constexpr T rectMinY(const Rect2<T>& rect)
{
    return rect.y;
}

template <typename T> constexpr T rectMaxX(const Rect2<T>& rect)
{
    return rect.x + rect.width;
}

template <typename T> constexpr T rectMaxY(const Rect2<T>& rect)
{
    return rect.y + rect.height;
}

template <typename T> constexpr bool rectHasPositiveArea(const Rect2<T>& rect)
{
    return rect.width > T{} && rect.height > T{};
}

template <typename T> constexpr bool rectsIntersect(const Rect2<T>& lhs, const Rect2<T>& rhs)
{
    return rectHasPositiveArea(lhs) && rectHasPositiveArea(rhs) && rectMinX(lhs) < rectMaxX(rhs)
        && rectMaxX(lhs) > rectMinX(rhs) && rectMinY(lhs) < rectMaxY(rhs)
        && rectMaxY(lhs) > rectMinY(rhs);
}

template <typename T> constexpr Rect2<T> intersectRect(const Rect2<T>& lhs, const Rect2<T>& rhs)
{
    const auto x0 = std::max(rectMinX(lhs), rectMinX(rhs));
    const auto y0 = std::max(rectMinY(lhs), rectMinY(rhs));
    const auto x1 = std::min(rectMaxX(lhs), rectMaxX(rhs));
    const auto y1 = std::min(rectMaxY(lhs), rectMaxY(rhs));
    return Rect2<T>{
        x0,
        y0,
        std::max(T{}, x1 - x0),
        std::max(T{}, y1 - y0),
    };
}

template <typename T> constexpr bool rectContainsPoint(const Rect2<T>& rect, T x, T y)
{
    return rectHasPositiveArea(rect) && x >= rectMinX(rect) && y >= rectMinY(rect)
        && x <= rectMaxX(rect) && y <= rectMaxY(rect);
}

} // namespace arp::core
