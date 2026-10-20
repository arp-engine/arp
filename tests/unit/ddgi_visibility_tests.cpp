#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {

struct Vec3 {
    float x;
    float y;
    float z;
};

struct Aabb {
    Vec3 minimum;
    Vec3 maximum;
};

float dot(const Vec3 a, const Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 normalize(const Vec3 value)
{
    const auto length = std::sqrt(dot(value, value));
    return {value.x / length, value.y / length, value.z / length};
}

bool rayHitsAabb(const Vec3 origin, const Vec3 direction, const Aabb& box)
{
    float nearDistance = 0.0f;
    float farDistance = 100.0f;
    const std::array<float, 3> origins{origin.x, origin.y, origin.z};
    const std::array<float, 3> directions{direction.x, direction.y, direction.z};
    const std::array<float, 3> minimum{box.minimum.x, box.minimum.y, box.minimum.z};
    const std::array<float, 3> maximum{box.maximum.x, box.maximum.y, box.maximum.z};
    for (std::size_t axis = 0u; axis < 3u; ++axis) {
        if (std::abs(directions[axis]) < 1.0e-6f) {
            if (origins[axis] < minimum[axis] || origins[axis] > maximum[axis])
                return false;
            continue;
        }
        const auto inverse = 1.0f / directions[axis];
        auto first = (minimum[axis] - origins[axis]) * inverse;
        auto second = (maximum[axis] - origins[axis]) * inverse;
        if (first > second)
            std::swap(first, second);
        nearDistance = std::max(nearDistance, first);
        farDistance = std::min(farDistance, second);
        if (nearDistance > farDistance)
            return false;
    }
    return farDistance >= nearDistance && farDistance > 1.0e-4f;
}

float visibleFraction(const Vec3 origin, const std::vector<Aabb>& geometry)
{
    constexpr std::array<Vec3, 26> integerDirections{{
        {-1.0f, -1.0f, -1.0f},
        {-1.0f, -1.0f, 0.0f},
        {-1.0f, -1.0f, 1.0f},
        {-1.0f, 0.0f, -1.0f},
        {-1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 1.0f},
        {-1.0f, 1.0f, -1.0f},
        {-1.0f, 1.0f, 0.0f},
        {-1.0f, 1.0f, 1.0f},
        {0.0f, -1.0f, -1.0f},
        {0.0f, -1.0f, 0.0f},
        {0.0f, -1.0f, 1.0f},
        {0.0f, 0.0f, -1.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, -1.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 1.0f},
        {1.0f, -1.0f, -1.0f},
        {1.0f, -1.0f, 0.0f},
        {1.0f, -1.0f, 1.0f},
        {1.0f, 0.0f, -1.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, -1.0f},
        {1.0f, 1.0f, 0.0f},
        {1.0f, 1.0f, 1.0f},
    }};
    std::size_t visible = 0u;
    for (const auto direction : integerDirections) {
        const auto ray = normalize(direction);
        const auto blocked = std::any_of(geometry.begin(), geometry.end(), [&](const Aabb& box) {
            return rayHitsAabb(origin, ray, box);
        });
        visible += blocked ? 0u : 1u;
    }
    return static_cast<float>(visible) / static_cast<float>(integerDirections.size());
}

} // namespace

int main()
{
    const auto outdoorReference = visibleFraction({0.0f, 0.0f, 0.0f}, {});
    assert(outdoorReference == 1.0f);

    const std::vector<Aabb> sealedRoom{
        {{-1.1f, -1.1f, -1.1f}, {-1.0f, 1.1f, 1.1f}},
        {{1.0f, -1.1f, -1.1f}, {1.1f, 1.1f, 1.1f}},
        {{-1.0f, -1.1f, -1.1f}, {1.0f, -1.0f, 1.1f}},
        {{-1.0f, 1.0f, -1.1f}, {1.0f, 1.1f, 1.1f}},
        {{-1.0f, -1.0f, -1.1f}, {1.0f, 1.0f, -1.0f}},
        {{-1.0f, -1.0f, 1.0f}, {1.0f, 1.0f, 1.1f}},
    };
    const auto sealedRoomVisibility = visibleFraction({0.0f, 0.0f, 0.0f}, sealedRoom);
    assert(sealedRoomVisibility <= outdoorReference * 0.02f);

    const std::vector<Aabb> thinWall{{{0.0f, -2.0f, -2.0f}, {0.05f, 2.0f, 2.0f}}};
    const auto thinWallVisibility = visibleFraction({-1.0f, 0.0f, 0.0f}, thinWall);
    assert(thinWallVisibility > 0.25f && thinWallVisibility < 0.75f);

    const std::vector<Aabb> corner{
        {{0.0f, -2.0f, -2.0f}, {0.05f, 2.0f, 2.0f}},
        {{-2.0f, 0.0f, -2.0f}, {2.0f, 0.05f, 2.0f}},
    };
    const auto cornerVisibility = visibleFraction({-1.0f, -1.0f, 0.0f}, corner);
    assert(cornerVisibility < thinWallVisibility);
    assert(cornerVisibility > 0.0f);

    const std::vector<Aabb> containingGeometry{{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}};
    assert(visibleFraction({0.0f, 0.0f, 0.0f}, containingGeometry) == 0.0f);
    return 0;
}
