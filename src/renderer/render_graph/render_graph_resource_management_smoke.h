#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "renderer/render_graph/frame_graph_resource_pool.h"
#include "rhi/gpu_device.h"
#include "rhi/gpu_swapchain.h"

#include <cstdint>

namespace arp::render_graph {

struct RenderGraphResourceManagementSmokeConfig {
    std::uint32_t logicalFrameSlots = 2;
    rhi::Extent3D extent{64, 64, 1};
    rhi::TextureFormat format = rhi::TextureFormat::Rgba8Unorm;
    std::uint32_t browserCompletionTimeoutMilliseconds = 5000;
};

struct RenderGraphResourceManagementSmokeReport {
    bool success = false;
    std::pmr::string diagnostics;
    rhi::Backend backend = rhi::Backend::Null;
    std::uint32_t requestedFrameCount = 0;
    std::uint32_t completedFrameCount = 0;
    std::uint64_t aliasHandoffCount = 0;
    std::uint64_t prematureReuseCount = 0;
    std::uint64_t barrierFailureCount = 0;
    std::uint64_t fallbackCount = 0;
    bool transientAliasingAdvertised = false;
    bool externalFinalStateCorrect = false;
    bool budgetWithinBounds = false;
    bool graphRebuildExercised = false;
    FrameGraphResourcePoolReport pool;
};

/// Cross-frame executable smoke for the RenderGraph resource-management
/// contract. The runner owns no native objects and observes only L2 state,
/// compiled handoffs, stable diagnostics and pool telemetry.
class ARP_API RenderGraphResourceManagementSmokeRunner final {
public:
    explicit RenderGraphResourceManagementSmokeRunner(
        rhi::GpuDevice& device, rhi::Swapchain* swapchain = nullptr);

    RenderGraphResourceManagementSmokeReport run(
        const RenderGraphResourceManagementSmokeConfig& config);

private:
    rhi::GpuDevice& device_;
    rhi::Swapchain* swapchain_ = nullptr;
};

} // namespace arp::render_graph
