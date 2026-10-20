#pragma once

#include "renderer/deployment/webgpu_deployment_readiness.h"
#include "renderer/rhi_draw_plan_bridge.h"

#include <cstdint>

namespace arp::renderer::deployment {

class RendererWebGpuDeploymentBridge {
public:
    explicit RendererWebGpuDeploymentBridge(rhi::WebGpuRuntimeProfile profile);

    WebGpuDeploymentReadinessReport evaluate(
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
        const platform::PlatformLifecycleState& state,
        platform::PlatformLifecycleEvent event,
        std::uint32_t maxTelemetryDiagnostics = 8) const;

private:
    rhi::WebGpuRuntimeProfile profile_;
};

WebGpuDeploymentReadinessReport evaluateRendererWebGpuDeploymentReadiness(
    const rhi::WebGpuRuntimeProfile& profile,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& state,
    platform::PlatformLifecycleEvent event,
    std::uint32_t maxTelemetryDiagnostics = 8);

} // namespace arp::renderer::deployment
