#pragma once

#include "renderer/volumetric_cloud/cloud_contract.h"
#include "rhi/gpu_capabilities.h"
#include "rhi/gpu_resource_types.h"

namespace arp::renderer::cloud {

// Every sampling/retention budget is authored, not selected from backend names.
struct CloudRenderConfig {
    std::uint32_t resolutionDivisor = 0u;
    std::uint32_t viewSteps = 0u;
    std::uint32_t lightSteps = 0u;
    std::uint32_t framesInFlight = 0u;
    std::uint64_t gpuBudgetBytes = 0u;
    std::uint64_t maxDensityEvaluations = 0u;
    float maxDistance = 0.0f;
    float historyWeight = 0.0f;
    float historyDepthTolerance = 0.0f;
    bool temporal = false;
};

// CloudSample/1 is two float4s: linear L/T, then representative distance,
// opaque distance and two reserved zero lanes. Asset storage is float4 cells.
inline constexpr std::uint64_t kCloudSampleBytes = 32u;
inline constexpr std::uint32_t kCloudWorkgroupWidth = 8u;
inline constexpr std::uint32_t kCloudWorkgroupHeight = 8u;
inline constexpr std::uint32_t kCloudPipelineCount = 3u;
inline constexpr std::uint32_t kCloudDirectPipelineEntries = kCloudPipelineCount * 2u;
inline constexpr std::uint32_t kCloudDescriptorElementsPerSlot = 12u;

struct CloudExecutionPlan {
    rhi::Extent2D extent{};
    rhi::Extent2D sampleExtent{};
    rhi::Extent3D dispatchGroups{};
    std::uint64_t sampleCount = 0u;
    std::uint64_t sampleBufferBytes = 0u;
    std::uint64_t retainedGpuBytes = 0u;
    std::uint64_t densityEvaluationBound = 0u;
};

class ARP_API CloudExecutionPlanner final {
public:
    static CloudResult plan(const CloudRenderConfig& config,
        rhi::Extent2D extent,
        std::uint64_t assetBytes,
        std::uint32_t uniformBytes,
        const rhi::DeviceCapabilities& capabilities,
        CloudExecutionPlan& output) noexcept;
};

} // namespace arp::renderer::cloud
