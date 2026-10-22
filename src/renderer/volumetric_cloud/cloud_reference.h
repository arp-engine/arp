#pragma once

#include "assets/cloud/cloud_asset.h"
#include "renderer/volumetric_cloud/cloud_contract.h"

namespace arp::renderer::cloud {

// Independent CPU reference for CloudDensity/1 and fixed midpoint integration.
// Borrowed validated asset must outlive this evaluator. No allocations per ray.
class ARP_API CloudReference final {
public:
    CloudResult configure(
        const assets::cloud::CloudAsset& asset, const scene3d::CloudLayerDesc& layer);
    float density(core::Vec3 worldPosition, float time) const noexcept;
    CloudResult integrate(const CloudRay& ray,
        std::uint32_t viewSteps,
        std::uint32_t lightSteps,
        core::Vec3 sunDirection,
        core::Vec3 sunRadiance,
        core::Vec3 ambientRadiance,
        float time,
        volume::MediumIntegral& output) const noexcept;

private:
    const assets::cloud::CloudAsset* asset_ = nullptr;
    scene3d::CloudLayerDesc layer_;
};

} // namespace arp::renderer::cloud
