#pragma once

#include "assets/cloud/cloud_asset.h"
#include "renderer/volumetric_cloud/cloud_execution_plan.h"
#include "renderer/volumetric_cloud/cloud_gpu_contract.h"

namespace arp::renderer::cloud {

struct CloudGraphContext;

// CPU metadata for the exact immutable uniform generation submitted by a frame.
// It carries no native state and does not prove GPU completion or buffer content.
struct CloudFrameState {
    CloudGpuConstants parameters{};
    core::Mat4 viewProjection, canonicalProjection;
    core::Vec3 planetCenter;
    std::uint64_t sceneRevision = 0u, cameraId = 0u;
    bool historyUsed = false;
};

// Shared parameter policy for legacy and original prepared graph paths.
// Only successful queue acceptance may advance samplingFrame or supply previous.
class ARP_API CloudFrameParameters final {
public:
    static CloudResult build(const CloudGraphContext& context,
        rhi::Backend backend,
        const CloudRenderConfig& config,
        const CloudExecutionPlan& plan,
        const assets::cloud::CloudCookDesc& asset,
        std::uint32_t samplingFrame,
        const CloudFrameState* previous,
        CloudFrameState& output) noexcept;
};

} // namespace arp::renderer::cloud
