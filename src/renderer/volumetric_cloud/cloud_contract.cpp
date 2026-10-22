#include "renderer/volumetric_cloud/cloud_contract.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace arp::renderer::cloud {
namespace {

bool finite(core::Vec3 v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

double dotDouble(core::Vec3 a, core::Vec3 b) noexcept
{
    return double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z;
}

struct SphereHit {
    double begin = 0.0;
    double end = 0.0;
    bool hit = false;
};

SphereHit sphere(double a, double b, double radiusSquaredDifference) noexcept
{
    const double discriminant = b * b - a * radiusSquaredDifference;
    if (discriminant <= 0.0)
        return {};
    const double root = std::sqrt(discriminant);
    // Stable quadratic roots avoid cancellation for rays near a planet surface.
    const double q = -b - std::copysign(root, b);
    const double first = q / a;
    const double second = radiusSquaredDifference / q;
    return {std::min(first, second), std::max(first, second), true};
}

} // namespace

CloudResult CloudContract::validate(const scene3d::CloudLayerDesc& d) noexcept
{
    if (const auto* diagnostic = scene3d::CloudLayerValidator::diagnostic(d))
        return CloudResult::failure(diagnostic);
    return {};
}

CloudResult CloudContract::intersect(
    const scene3d::CloudLayerDesc& layer, const CloudRay& ray, CloudRayIntervals& output) noexcept
{
    output = {};
    const auto valid = validate(layer);
    if (!valid.success)
        return valid;
    const double a = dotDouble(ray.direction, ray.direction);
    // Directions originate from float camera math. This tolerance validates unit
    // input, while the quadratic still uses the actual a rather than assuming 1.
    constexpr double unitDirectionTolerance = 0.0001;
    if (!finite(ray.origin) || !finite(ray.direction) || !std::isfinite(ray.maxDistance)
        || ray.maxDistance < 0.0 || std::fabs(a - 1.0) > unitDirectionTolerance)
        return CloudResult::failure("CLOUD-RAY-INVALID");
    const double x = double(ray.origin.x) - layer.planetCenter.x;
    const double y = double(ray.origin.y) - layer.planetCenter.y;
    const double z = double(ray.origin.z) - layer.planetCenter.z;
    const double originSquared = x * x + y * y + z * z;
    const double b = x * ray.direction.x + y * ray.direction.y + z * ray.direction.z;
    const double radius = layer.planetRadius;
    if (originSquared < radius * radius)
        return CloudResult::failure("CLOUD-CAMERA-INSIDE-PLANET");
    double limit = ray.maxDistance;
    const auto ground = sphere(a, b, originSquared - radius * radius);
    if (ground.hit && ground.begin >= 0.0)
        limit = std::min(limit, ground.begin);
    const double innerRadius = radius + layer.baseAltitude;
    const double outerRadius = innerRadius + layer.thickness;
    const auto outer = sphere(a, b, originSquared - outerRadius * outerRadius);
    if (!outer.hit)
        return {};
    const double begin = std::max(0.0, outer.begin);
    const double end = std::min(limit, outer.end);
    if (end <= begin)
        return {};
    const auto append = [&](double first, double last) {
        if (last > first)
            output.intervals[output.count++] = {first, last};
    };
    const auto inner = sphere(a, b, originSquared - innerRadius * innerRadius);
    if (!inner.hit || inner.end <= begin || inner.begin >= end) {
        append(begin, end);
    } else {
        append(begin, std::min(end, inner.begin));
        append(std::max(begin, inner.end), end);
    }
    return {};
}

} // namespace arp::renderer::cloud
