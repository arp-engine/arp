#pragma once

#include "core/memory/cpu_allocator.h"
#include "renderer/deployment/renderer_fallback_bridge.h"

#include <cstdint>
#include <string>

namespace arp::renderer::deployment {

enum class NativeRendererDeploymentReadinessStatus {
    ContractComplete,
    BackendValidated,
    DiagnosticFailure
};

struct NativeRendererDeploymentReadinessReport {
    bool success = false;
    rhi::Backend backend = rhi::Backend::Null;
    NativeRendererDeploymentReadinessStatus status =
        NativeRendererDeploymentReadinessStatus::DiagnosticFailure;
    bool contractComplete = false;
    bool backendValidated = false;
    bool canStartFrame = false;
    bool telemetryRequired = false;
    std::uint32_t nativeFeatureCount = 0;
    std::uint32_t emulatedFeatureCount = 0;
    std::uint32_t cpuReplayFeatureCount = 0;
    std::uint32_t degradedFeatureCount = 0;
    std::uint32_t diagnosticFailureCount = 0;
    bool upperLayerVisibleFallbacks = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

class NativeRendererDeploymentBridge {
public:
    explicit NativeRendererDeploymentBridge(
        rhi::semantics::NativeBackendCapabilityContract contract);

    NativeRendererDeploymentReadinessReport evaluate(
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan) const;

private:
    rhi::semantics::NativeBackendCapabilityContract contract_;
};

NativeRendererDeploymentReadinessReport evaluateNativeRendererDeploymentReadiness(
    const rhi::semantics::NativeBackendCapabilityContract& contract,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan);

/// Builds a structural fallback plan and only restricts an existing device
/// capability snapshot. Caller-set contract/query claims cannot grant features.
void evaluateAndApplyFallback(const rhi::semantics::NativeBackendCapabilityContract& contract,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    rhi::DeviceCapabilities& caps);

} // namespace arp::renderer::deployment
