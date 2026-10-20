#pragma once

#include "core/cpu_allocator.h"
#include "rhi/semantics/native_platform_lifecycle_lowering.h"
#include "rhi/semantics/webgpu_runtime_profile.h"

#include <cstdint>
#include <string>

namespace arp::rhi::semantics {

struct WebGpuRuntimeTelemetryCollectorDesc {
    std::uint32_t maxDiagnostics = 8;
};

struct WebGpuRuntimeTelemetryReport {
    bool success = false;
    rhi::Backend backend = rhi::Backend::WebGPU;
    rhi::WebGpuRuntimeProfileStatus profileStatus =
        rhi::WebGpuRuntimeProfileStatus::ContractComplete;
    bool browserRuntime = false;
    bool canvasSurface = false;
    std::uint32_t logicalFrameSlots = 2;
    std::uint32_t requestedSwapchainImageCount = 2;

    std::uint32_t nativeFeatureCount = 0;
    std::uint32_t emulatedFeatureCount = 0;
    std::uint32_t cpuReplayFeatureCount = 0;
    std::uint32_t degradedFeatureCount = 0;
    std::uint32_t runtimeDiagnosticFailureCount = 0;
    std::uint32_t fallbackDiagnosticFailureCount = 0;
    std::uint32_t lifecycleDiagnosticFailureCount = 0;
    std::uint32_t diagnosticFailureCount = 0;

    std::uint32_t recordedDiagnosticCount = 0;
    std::uint32_t droppedDiagnosticCount = 0;
    bool diagnosticCapacityExhausted = false;

    bool telemetryRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;

    bool acquireAllowed = false;
    bool presentAllowed = false;
    bool swapchainRebuildRequired = false;
    bool releaseSurfaceResources = false;
    bool releaseTransientResources = false;
    bool memoryBudgetRefreshRequired = false;

    std::pmr::string diagnostics;
};

class WebGpuRuntimeTelemetryCollector {
public:
    /// Construct with an explicit CPU memory resource. When omitted, the
    /// process-wide default resource is used. Threaded through to the pmr
    /// diagnostics vector so a backend can bind a per-instance intent/budget
    /// instead of silently falling back to the global default.
    explicit WebGpuRuntimeTelemetryCollector(const WebGpuRuntimeTelemetryCollectorDesc& desc,
        std::pmr::memory_resource* memoryResource = &core::defaultCpuMemoryResource());

    void recordProfile(const rhi::WebGpuRuntimeProfile& profile);
    void recordFallbackPlan(const rhi::EngineFallbackPlan& plan);
    void recordLifecycleDecision(const NativePlatformLifecycleLoweringDecision& decision);

    WebGpuRuntimeTelemetryReport buildReport() const;

private:
    void recordDiagnostic(const std::pmr::string& diagnostics);
    void recordRuntimeFailure(const std::pmr::string& diagnostics);
    void recordFallbackFailure(const std::pmr::string& diagnostics);
    void recordLifecycleFailure(const std::pmr::string& diagnostics);

    WebGpuRuntimeTelemetryCollectorDesc desc_;
    WebGpuRuntimeTelemetryReport report_;
    core::pmr::Vector<core::pmr::String> diagnostics_;
    std::uint32_t droppedDiagnosticCount_ = 0;
    bool diagnosticCapacityExhausted_ = false;
    bool profileRecorded_ = false;
};

} // namespace arp::rhi::semantics
