#pragma once

#include "renderer/volumetric_cloud/cloud_contract.h"
#include "renderer/volumetric_cloud/cloud_gpu_contract.h"

namespace arp::renderer::cloud {

// CloudSample/2 retains the six meaningful IEEE float32 lanes of /1 exactly.
// Scalar fields give all three shader targets a 24-byte storage stride. This
// is a distinct publication/selection, never inferred from a buffer's size.
struct CloudGpuCompactSample {
    float red, green, blue, transmittance, distance, opaqueDistance;
};

class CloudSampleStorage final {
public:
    static constexpr std::uint64_t stride(std::uint32_t version) noexcept
    {
        return version == 1u ? sizeof(CloudGpuSample)
            : version == 2u  ? sizeof(CloudGpuCompactSample)
                             : 0u;
    }

    static CloudResult pack(const CloudGpuSample& input, CloudGpuCompactSample& output) noexcept
    {
        output = {};
        if (input.metadata.z != 0.f || input.metadata.w != 0.f)
            return CloudResult::failure("CLOUD-SAMPLE-COMPACT-RESERVED");
        const auto& v = input.radianceTransmittance;
        output = {v.x, v.y, v.z, v.w, input.metadata.x, input.metadata.y};
        return {};
    }

    static CloudGpuSample unpack(const CloudGpuCompactSample& input) noexcept
    {
        return {{input.red, input.green, input.blue, input.transmittance},
            {input.distance, input.opaqueDistance, 0.f, 0.f}};
    }
};

static_assert(sizeof(CloudGpuCompactSample) == 24u);
static_assert(offsetof(CloudGpuCompactSample, transmittance) == 12u);
static_assert(offsetof(CloudGpuCompactSample, distance) == 16u);
static_assert(offsetof(CloudGpuCompactSample, opaqueDistance) == 20u);
} // namespace arp::renderer::cloud
