#pragma once

#include "core/math.h"
#include "reflection/reflection_macros.h"
#include "scene/transform.h"

namespace arp::scene3d {

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct Bounds3D {
    APROP(Serialize, Editor)
    arp::core::Vec3 center;
    APROP(Serialize, Editor)
    arp::core::Vec3 extents;
    APROP(Serialize)
    bool hasExplicitMetadata = false;

    static Bounds3D explicitBounds(arp::core::Vec3 center, arp::core::Vec3 extents);
    bool valid() const;
};

Bounds3D transformBounds(const Bounds3D& localBounds, const arp::scene::Transform& worldTransform);

} // namespace arp::scene3d
