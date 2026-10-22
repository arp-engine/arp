#pragma once

#include "3d/cloud_types.h"
#include "core/api.h"
#include "renderer/volume/medium_integration.h"

#include <array>
#include <cstdint>

namespace arp::renderer::cloud {

inline constexpr std::uint32_t kCloudContractVersion = 1u;

struct CloudResult {
    bool success = true;
    const char* diagnostic = "";

    static CloudResult failure(const char* id) noexcept { return {false, id}; }
};

struct CloudRay {
    core::Vec3 origin{};
    core::Vec3 direction{};
    double maxDistance = 0.0;
};

struct CloudInterval {
    double begin = 0.0;
    double end = 0.0;
};

struct CloudRayIntervals {
    std::array<CloudInterval, 2> intervals{};
    std::uint32_t count = 0u;
};

class ARP_API CloudContract final {
public:
    static CloudResult validate(const scene3d::CloudLayerDesc& layer) noexcept;
    static CloudResult intersect(const scene3d::CloudLayerDesc& layer,
        const CloudRay& ray,
        CloudRayIntervals& output) noexcept;
};

} // namespace arp::renderer::cloud
