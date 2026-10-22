#pragma once

#include "core/math/math.h"
#include "reflection/reflection_macros.h"
#include "scene/transform.h"

namespace arp::scene2d {

ASTRUCT(Construct = Default)

struct Bounds2D {
    APROP(Serialize, Editor)
    arp::core::Vec3 center;
    APROP(Serialize, Editor)
    arp::core::Vec3 extents;
    APROP(Serialize, Editor)
    bool hasExplicitMetadata = false;

    static Bounds2D explicitBounds(arp::core::Vec3 center, arp::core::Vec3 extents);
    bool valid() const;
};

Bounds2D transformBounds(const Bounds2D& localBounds, const arp::scene::Transform& worldTransform);

} // namespace arp::scene2d
