#pragma once

#include "core/math/math.h"

#include <cmath>

namespace arp::renderer::volume {

// Inputs are validated at the owning feature boundary. Extinction is m^-1,
// source is linear radiance per metre, length is metres.
inline float integratedLength(float extinction, float distance) noexcept
{
    return extinction == 0.0f ? distance : -std::expm1(-extinction * distance) / extinction;
}

inline float henyeyGreenstein(float cosine, float g) noexcept
{
    const float d = 1.0f + g * g - 2.0f * g * cosine;
    return (1.0f - g * g) / (4.0f * 3.14159265358979323846f * d * std::sqrt(d));
}

struct MediumIntegral {
    core::Vec3 radiance{};
    float transmittance = 1.0f;
};

inline MediumIntegral homogeneous(float extinction, core::Vec3 source, float length) noexcept
{
    return {
        core::scale(source, integratedLength(extinction, length)), std::exp(-extinction * length)};
}

inline MediumIntegral compose(MediumIntegral front, MediumIntegral back) noexcept
{
    return {front.radiance + core::scale(back.radiance, front.transmittance),
        front.transmittance * back.transmittance};
}

inline core::Vec3 composite(MediumIntegral medium, core::Vec3 background) noexcept
{
    return medium.radiance + core::scale(background, medium.transmittance);
}

} // namespace arp::renderer::volume
