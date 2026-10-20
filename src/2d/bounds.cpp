#include "2d/bounds.h"

#include <cmath>

namespace arp::scene2d {

Bounds2D Bounds2D::explicitBounds(arp::core::Vec3 center, arp::core::Vec3 extents)
{
    Bounds2D bounds;
    bounds.center = center;
    bounds.extents = extents;
    bounds.hasExplicitMetadata = true;
    return bounds;
}

bool Bounds2D::valid() const
{
    return hasExplicitMetadata && std::isfinite(center.x) && std::isfinite(center.y)
        && std::isfinite(extents.x) && std::isfinite(extents.y) && extents.x >= 0.0f
        && extents.y >= 0.0f;
}

Bounds2D transformBounds(const Bounds2D& localBounds, const arp::scene::Transform& worldTransform)
{
    Bounds2D bounds;
    bounds.hasExplicitMetadata = localBounds.hasExplicitMetadata;
    const auto center = arp::core::rotate(worldTransform.rotation,
        {localBounds.center.x * worldTransform.scale.x,
            localBounds.center.y * worldTransform.scale.y,
            0});
    bounds.center = worldTransform.translation + center;
    const auto x = arp::core::rotate(
        worldTransform.rotation, {localBounds.extents.x * worldTransform.scale.x, 0, 0});
    const auto y = arp::core::rotate(
        worldTransform.rotation, {0, localBounds.extents.y * worldTransform.scale.y, 0});
    bounds.extents = {
        std::fabs(x.x) + std::fabs(y.x),
        std::fabs(x.y) + std::fabs(y.y),
        0.0f,
    };
    return bounds;
}

} // namespace arp::scene2d
