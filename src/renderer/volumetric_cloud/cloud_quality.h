#pragma once

#include "renderer/volumetric_cloud/cloud_execution_plan.h"

namespace arp::renderer::cloud {

enum class CloudQualityTier { Reference, High, Medium };

// CloudQuality/1 selects sampling only. Names are candidate configurations,
// not claims of quality, timing, physical memory coverage or backend support.
// Callers author retention/work/memory limits and still invoke the planner.
class ARP_API CloudQuality final {
public:
    static constexpr std::uint32_t contractVersion = 1u;

    // Atomic on failure; preserves all caller-authored non-sampling fields.
    static CloudResult apply(
        std::uint32_t version, CloudQualityTier tier, CloudRenderConfig& config) noexcept;
};

} // namespace arp::renderer::cloud
