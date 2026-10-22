#include "3d/cloud_types.h"

#include <cmath>

namespace arp::scene3d {
namespace {
bool finite(core::Vec3 v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool unitRange(float v) noexcept
{
    return std::isfinite(v) && v >= 0.0f && v <= 1.0f;
}

} // namespace

const char* CloudLayerValidator::diagnostic(const CloudLayerDesc& d) noexcept
{
    if (!finite(d.planetCenter) || !std::isfinite(d.planetRadius) || d.planetRadius <= 0.0f
        || !std::isfinite(d.baseAltitude) || d.baseAltitude < 0.0f || !std::isfinite(d.thickness)
        || d.thickness <= 0.0f)
        return "CLOUD-SHELL-INVALID";
    if (!std::isfinite(d.extinction) || d.extinction < 0.0f || !unitRange(d.scatteringAlbedo.x)
        || !unitRange(d.scatteringAlbedo.y) || !unitRange(d.scatteringAlbedo.z)
        || !std::isfinite(d.anisotropy) || std::fabs(d.anisotropy) >= 1.0f)
        return "CLOUD-MEDIUM-INVALID";
    if (!finite(d.windVelocity) || !std::isfinite(d.weatherPeriod) || d.weatherPeriod <= 0.0f
        || !std::isfinite(d.shapePeriod) || d.shapePeriod <= 0.0f || !std::isfinite(d.detailPeriod)
        || d.detailPeriod <= 0.0f || !unitRange(d.coverage) || !unitRange(d.erosion))
        return "CLOUD-DENSITY-INVALID";
    return nullptr;
}

} // namespace arp::scene3d
