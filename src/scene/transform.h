#pragma once

#include "core/math.h"
#include "reflection/reflection_macros.h"

namespace arp::scene {

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct Transform {
    APROP(Serialize, Editor)
    arp::core::Vec3 translation{0.0f, 0.0f, 0.0f};
    APROP(Serialize, Editor)
    arp::core::Vec3 scale{1.0f, 1.0f, 1.0f};
    APROP(Serialize, Editor)
    arp::core::Quat rotation{};

    AFUNC()

    void set_translation(float x, float y, float z) { translation = {x, y, z}; }

    static Transform identity();
    static Transform fromTranslation(arp::core::Vec3 translation);
};

AFUNC()
Transform composeTransforms(const Transform& parentWorld, const Transform& local);
AFUNC()
Transform deriveLocalTransform(const Transform& parentWorld, const Transform& desiredWorld);

} // namespace arp::scene
