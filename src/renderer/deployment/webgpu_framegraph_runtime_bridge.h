#pragma once

#include "renderer/deployment/webgpu_deployment_readiness.h"
#include "renderer/render_graph/rhi_frame_plan.h"
#include "rhi/semantics/native_platform_lifecycle_lowering.h"
#include "rhi/semantics/webgpu_runtime_profile.h"
#include "rhi/semantics/webgpu_runtime_telemetry.h"

#include <cstdint>

namespace arp::renderer::deployment {

class WebGpuFrameGraphRuntimeBridge {
public:
    explicit WebGpuFrameGraphRuntimeBridge(rhi::WebGpuRuntimeProfile profile);

    rhi::EngineFallbackPlan buildPlan(const render_graph::RhiFramePlan& framePlan) const;

private:
    rhi::WebGpuRuntimeProfile profile_;
};

rhi::EngineFallbackPlan buildWebGpuFrameGraphRuntimeFallbackPlan(
    const rhi::WebGpuRuntimeProfile& profile, const render_graph::RhiFramePlan& framePlan);

class WebGpuFrameGraphDeploymentBridge {
public:
    explicit WebGpuFrameGraphDeploymentBridge(rhi::WebGpuRuntimeProfile profile);

    WebGpuDeploymentReadinessReport evaluate(const render_graph::RhiFramePlan& framePlan,
        const platform::PlatformLifecycleState& state,
        platform::PlatformLifecycleEvent event,
        std::uint32_t maxTelemetryDiagnostics = 8) const;

private:
    rhi::WebGpuRuntimeProfile profile_;
};

WebGpuDeploymentReadinessReport buildWebGpuFrameGraphDeploymentReadiness(
    const rhi::WebGpuRuntimeProfile& profile,
    const render_graph::RhiFramePlan& framePlan,
    const platform::PlatformLifecycleState& state,
    platform::PlatformLifecycleEvent event,
    std::uint32_t maxTelemetryDiagnostics = 8);

} // namespace arp::renderer::deployment
