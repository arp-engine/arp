#include "3d/bounds.h"

#include <cmath>

namespace arp::scene3d {

Bounds3D Bounds3D::explicitBounds(arp::core::Vec3 center, arp::core::Vec3 extents)
{
    Bounds3D bounds;
    bounds.center = center;
    bounds.extents = extents;
    bounds.hasExplicitMetadata = true;
    return bounds;
}

bool Bounds3D::valid() const
{
    return hasExplicitMetadata && extents.x >= 0.0f && extents.y >= 0.0f && extents.z >= 0.0f;
}

Bounds3D transformBounds(const Bounds3D& localBounds, const arp::scene::Transform& worldTransform)
{
    Bounds3D bounds;
    bounds.hasExplicitMetadata = localBounds.hasExplicitMetadata;
    bounds.center = {
        worldTransform.translation.x + localBounds.center.x * worldTransform.scale.x,
        worldTransform.translation.y + localBounds.center.y * worldTransform.scale.y,
        worldTransform.translation.z + localBounds.center.z * worldTransform.scale.z,
    };
    bounds.extents = {
        std::fabs(localBounds.extents.x * worldTransform.scale.x),
        std::fabs(localBounds.extents.y * worldTransform.scale.y),
        std::fabs(localBounds.extents.z * worldTransform.scale.z),
    };
    return bounds;
}

} // namespace arp::scene3d
