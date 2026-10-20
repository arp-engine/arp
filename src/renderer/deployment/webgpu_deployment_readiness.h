#pragma once

#include "rhi/semantics/webgpu_runtime_telemetry.h"

#include <cstdint>
#include <memory_resource>

namespace arp::renderer::deployment {

enum class WebGpuDeploymentReadinessStatus {
    ContractComplete,
    PlatformValidated,
    DiagnosticFailure
};

struct WebGpuDeploymentReadinessReport {
    bool success = false;
    rhi::Backend backend = rhi::Backend::WebGPU;
    WebGpuDeploymentReadinessStatus status = WebGpuDeploymentReadinessStatus::DiagnosticFailure;
    bool contractComplete = false;
    bool platformValidated = false;
    bool canStartFrame = false;
    bool telemetryRequired = false;
    bool swapchainRebuildRequired = false;
    bool releaseSurfaceResources = false;
    bool releaseTransientResources = false;
    bool memoryBudgetRefreshRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    std::uint32_t cpuReplayFeatureCount = 0;
    std::uint32_t degradedFeatureCount = 0;
    std::uint32_t diagnosticFailureCount = 0;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

class WebGpuDeploymentReadinessGate {
public:
    WebGpuDeploymentReadinessReport evaluate(
        const rhi::semantics::WebGpuRuntimeTelemetryReport& telemetry) const;
};

WebGpuDeploymentReadinessReport evaluateWebGpuDeploymentReadiness(
    const rhi::semantics::WebGpuRuntimeTelemetryReport& telemetry);

} // namespace arp::renderer::deployment
