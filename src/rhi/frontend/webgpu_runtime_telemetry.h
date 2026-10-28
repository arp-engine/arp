#pragma once

#include "rhi/semantics/webgpu_runtime_telemetry_values.h"

namespace arp::rhi::semantics {

class WebGpuRuntimeTelemetryCollector {
public:
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
