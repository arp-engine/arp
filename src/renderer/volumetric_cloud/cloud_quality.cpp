#include "renderer/volumetric_cloud/cloud_quality.h"

namespace arp::renderer::cloud {

CloudResult CloudQuality::apply(
    std::uint32_t version, CloudQualityTier tier, CloudRenderConfig& config) noexcept
{
    if (version != contractVersion)
        return CloudResult::failure("CLOUD-QUALITY-VERSION");
    auto selected = config;
    switch (tier) {
    case CloudQualityTier::Reference:
        selected.resolutionDivisor = 1u;
        selected.viewSteps = 256u;
        selected.lightSteps = 32u;
        selected.temporal = false;
        break;
    case CloudQualityTier::High:
        selected.resolutionDivisor = 1u;
        selected.viewSteps = 128u;
        selected.lightSteps = 16u;
        selected.temporal = true;
        break;
    case CloudQualityTier::Medium:
        selected.resolutionDivisor = 2u;
        selected.viewSteps = 64u;
        selected.lightSteps = 8u;
        selected.temporal = true;
        break;
    default:
        return CloudResult::failure("CLOUD-QUALITY-TIER");
    }
    config = selected;
    return {};
}

} // namespace arp::renderer::cloud
