#pragma once

#include "core/cpu_allocator.h"
#include "renderer/deployment/native_renderer_deployment_bridge.h"
#include "renderer/deployment/renderer_webgpu_deployment_bridge.h"

#include <cstdint>
#include <string>
#include <vector>

namespace arp::renderer::deployment {

enum class RendererBackendPreference { NativeValidatedFirst, WebFirst };

struct RendererBackendSelectionDesc {
    std::uint32_t maxCandidates = 0;
    core::pmr::Vector<NativeRendererDeploymentReadinessReport> candidates{
        &core::defaultCpuMemoryResource()};
};

struct RendererBackendSelectionResult {
    bool success = false;
    rhi::Backend selectedBackend = rhi::Backend::Null;
    NativeRendererDeploymentReadinessStatus selectedStatus =
        NativeRendererDeploymentReadinessStatus::DiagnosticFailure;
    std::uint32_t selectedCandidateIndex = UINT32_MAX;
    std::uint32_t candidateCount = 0;
    std::uint32_t diagnosticCandidateCount = 0;
    bool telemetryRequired = false;
    bool diagnosticFailureUserVisible = false;
    bool diagnosticFailureSilent = false;
    core::pmr::String diagnosticFailureReason{&core::defaultCpuMemoryResource()};
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

class RendererBackendSelectionPolicy {
public:
    explicit RendererBackendSelectionPolicy(RendererBackendPreference preference);

    RendererBackendSelectionResult select(const RendererBackendSelectionDesc& desc) const;

private:
    RendererBackendPreference preference_ = RendererBackendPreference::NativeValidatedFirst;
};

RendererBackendSelectionResult selectRendererBackend(
    RendererBackendPreference preference, const RendererBackendSelectionDesc& desc);

struct RendererBackendCandidateBuilderDesc {
    std::uint32_t maxCandidates = 0;
    core::pmr::Vector<rhi::semantics::NativeBackendCapabilityContract> nativeContracts{
        &core::defaultCpuMemoryResource()};
    bool includeWebGpuRuntimeProfile = false;
    rhi::WebGpuRuntimeProfile webGpuProfile;
    platform::PlatformLifecycleState webGpuLifecycleState;
    platform::PlatformLifecycleEvent webGpuLifecycleEvent = platform::PlatformLifecycleEvent::Start;
    std::uint32_t maxTelemetryDiagnostics = 8;
};

struct RendererBackendCandidateBuildResult {
    bool success = false;
    bool capacityExceeded = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    RendererBackendSelectionDesc selection;
};

class RendererBackendCandidateBuilder {
public:
    explicit RendererBackendCandidateBuilder(const RendererBackendCandidateBuilderDesc& desc);

    RendererBackendCandidateBuildResult build(
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan) const;

private:
    RendererBackendCandidateBuilderDesc desc_;
};

RendererBackendCandidateBuildResult buildRendererBackendCandidates(
    const RendererBackendCandidateBuilderDesc& desc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan);

struct RendererBackendDeploymentPlannerDesc {
    RendererBackendPreference preference = RendererBackendPreference::NativeValidatedFirst;
    RendererBackendCandidateBuilderDesc candidates;
};

struct RendererBackendDeploymentPlan {
    bool success = false;
    bool canStartFrame = false;
    bool capacityExceeded = false;
    rhi::Backend selectedBackend = rhi::Backend::Null;
    NativeRendererDeploymentReadinessStatus selectedStatus =
        NativeRendererDeploymentReadinessStatus::DiagnosticFailure;
    std::uint32_t selectedCandidateIndex = UINT32_MAX;
    std::uint32_t candidateCount = 0;
    std::uint32_t diagnosticCandidateCount = 0;
    bool telemetryRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    NativeRendererDeploymentReadinessReport selectedReadiness;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

class RendererBackendDeploymentPlanner {
public:
    explicit RendererBackendDeploymentPlanner(const RendererBackendDeploymentPlannerDesc& desc);

    RendererBackendDeploymentPlan plan(
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan) const;

private:
    RendererBackendDeploymentPlannerDesc desc_;
};

RendererBackendDeploymentPlan planRendererBackendDeployment(
    const RendererBackendDeploymentPlannerDesc& desc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan);

} // namespace arp::renderer::deployment
