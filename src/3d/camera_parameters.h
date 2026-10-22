#pragma once

#include "core/math/math.h"

// Historical core-qualified API retained; camera policy belongs to scene 3D.
namespace arp::core {

struct Camera {
    float verticalFovDegrees = 60.0f;
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    Vec3 position;

    void setPerspective(float verticalFov, float aspect, float nearValue, float farValue)
    {
        verticalFovDegrees = verticalFov;
        aspectRatio = aspect;
        nearPlane = nearValue;
        farPlane = farValue;
    }
};

} // namespace arp::core
