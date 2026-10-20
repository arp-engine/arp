#include "rhi/semantics/webgpu_runtime_telemetry.h"

#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"

#include <limits>

namespace arp::rhi::semantics {

namespace {

bool addUint32(std::uint32_t& target, std::uint32_t value)
{
    if (value > std::numeric_limits<std::uint32_t>::max() - target) {
        return false;
    }
    target += value;
    return true;
}

} // namespace

WebGpuRuntimeTelemetryCollector::WebGpuRuntimeTelemetryCollector(
    const WebGpuRuntimeTelemetryCollectorDesc& desc, std::pmr::memory_resource* memoryResource)
    : desc_(desc), diagnostics_(memoryResource ? memoryResource : &core::defaultCpuMemoryResource())
{
    diagnostics_.reserve(desc_.maxDiagnostics);
}

void WebGpuRuntimeTelemetryCollector::recordProfile(const rhi::WebGpuRuntimeProfile& profile)
{
    profileRecorded_ = true;
    report_.backend = profile.backend;
    report_.profileStatus = profile.status;
    report_.browserRuntime = profile.browserRuntime;
    report_.canvasSurface = profile.canvasSurface;
    report_.logicalFrameSlots = profile.logicalFrameSlots;
    report_.requestedSwapchainImageCount = profile.requestedSwapchainImageCount;
    report_.nativeHandleExposedToUpperLayer =
        report_.nativeHandleExposedToUpperLayer || profile.nativeHandleExposedToUpperLayer;

    if (!profile.diagnostics.empty()) {
        recordDiagnostic(profile.diagnostics);
    }

    if (profile.status == rhi::WebGpuRuntimeProfileStatus::DiagnosticFailure) {
        recordRuntimeFailure("");
    }
    if (profile.logicalFrameSlots != 2) {
        recordRuntimeFailure("WebGPU telemetry requires two logical frame slots");
    }
    if (profile.requestedSwapchainImageCount != 2) {
        recordRuntimeFailure("WebGPU telemetry requires a double-buffer swapchain");
    }
    if (profile.nativeHandleExposedToUpperLayer) {
        recordRuntimeFailure("WebGPU telemetry rejected native handle exposure");
    }
}

void WebGpuRuntimeTelemetryCollector::recordFallbackPlan(const rhi::EngineFallbackPlan& plan)
{
    report_.backend = plan.backend;
    report_.logicalFrameSlots = plan.logicalFrameSlots;

    if (!addUint32(report_.nativeFeatureCount, plan.nativeFeatureCount)) {
        recordFallbackFailure("WebGPU fallback telemetry native feature count overflow");
    }
    if (!addUint32(report_.emulatedFeatureCount, plan.emulatedFeatureCount)) {
        recordFallbackFailure("WebGPU fallback telemetry emulated feature count overflow");
    }
    if (!addUint32(report_.cpuReplayFeatureCount, plan.cpuReplayFeatureCount)) {
        recordFallbackFailure("WebGPU fallback telemetry CPU replay feature count overflow");
    }
    if (plan.cpuReplayFeatureCount > 0) {
        recordFallbackFailure(
            "WebGPU deployment rejected CPU replay because it has no executable native lowering");
    }
    if (!addUint32(report_.degradedFeatureCount, plan.degradedFeatureCount)) {
        recordFallbackFailure("WebGPU fallback telemetry degraded feature count overflow");
    }
    if (!addUint32(report_.fallbackDiagnosticFailureCount, plan.diagnosticFailureCount)) {
        recordFallbackFailure("WebGPU fallback telemetry diagnostic failure count overflow");
    }

    report_.telemetryRequired = report_.telemetryRequired || plan.telemetryRequired;
    report_.backendFallbacksHiddenFromUpperLayer = report_.backendFallbacksHiddenFromUpperLayer
        && plan.backendFallbacksHiddenFromUpperLayer && !plan.upperLayerVisibleFallbacks;

    if (!plan.diagnostics.empty()) {
        recordDiagnostic(plan.diagnostics);
    }
    if (plan.degradedFeatureCount > 0 || plan.diagnosticFailureCount > 0) {
        report_.telemetryRequired = true;
    }
    if (plan.logicalFrameSlots != 2) {
        recordRuntimeFailure("WebGPU fallback telemetry requires two logical frame slots");
    }
    if (!plan.backendFallbacksHiddenFromUpperLayer || plan.upperLayerVisibleFallbacks) {
        recordRuntimeFailure("WebGPU fallback details became visible to upper layers");
    }
}

void WebGpuRuntimeTelemetryCollector::recordLifecycleDecision(
    const NativePlatformLifecycleLoweringDecision& decision)
{
    report_.backend = decision.backend;
    report_.logicalFrameSlots = decision.state.logicalFrameSlots;
    report_.acquireAllowed = decision.state.acquireAllowed;
    report_.presentAllowed = decision.state.presentAllowed;
    report_.swapchainRebuildRequired =
        report_.swapchainRebuildRequired || decision.state.swapchainRebuildRequired;
    report_.releaseSurfaceResources =
        report_.releaseSurfaceResources || decision.state.releaseSurfaceResources;
    report_.releaseTransientResources =
        report_.releaseTransientResources || decision.state.releaseTransientResources;
    report_.memoryBudgetRefreshRequired =
        report_.memoryBudgetRefreshRequired || decision.state.memoryBudgetRefreshRequired;
    report_.nativeHandleExposedToUpperLayer =
        report_.nativeHandleExposedToUpperLayer || decision.state.nativeHandleExposedToUpperLayer;

    if (!decision.diagnostics.empty()) {
        recordDiagnostic(decision.diagnostics);
    }
    if (!decision.success || decision.fallbackMode == rhi::FallbackMode::DiagnosticFailure) {
        recordLifecycleFailure("");
    }
    if (decision.state.logicalFrameSlots != 2) {
        recordLifecycleFailure("WebGPU lifecycle telemetry requires two logical frame slots");
    }
    if (decision.state.nativeHandleExposedToUpperLayer) {
        recordLifecycleFailure("WebGPU lifecycle rejected native handle exposure");
    }
}

WebGpuRuntimeTelemetryReport WebGpuRuntimeTelemetryCollector::buildReport() const
{
    auto report = report_;
    report.recordedDiagnosticCount = static_cast<std::uint32_t>(diagnostics_.size());
    report.droppedDiagnosticCount = droppedDiagnosticCount_;
    report.diagnosticCapacityExhausted = diagnosticCapacityExhausted_;

    for (const auto& diagnostics : diagnostics_) {
        arp::core::appendDiagnostic(report.diagnostics, diagnostics);
    }

    if (diagnosticCapacityExhausted_) {
        arp::core::appendDiagnostic(report.diagnostics, "telemetry diagnostics capacity exhausted");
        report.telemetryRequired = true;
    }

    if (!profileRecorded_) {
        arp::core::appendDiagnostic(
            report.diagnostics, "WebGPU telemetry requires a runtime profile");
        ++report.runtimeDiagnosticFailureCount;
        ++report.diagnosticFailureCount;
        report.telemetryRequired = true;
    }

    report.diagnosticFailureCount = 0;
    bool diagnosticFailureCountOverflow = false;
    if (!addUint32(report.diagnosticFailureCount, report.runtimeDiagnosticFailureCount)) {
        diagnosticFailureCountOverflow = true;
    }
    if (!addUint32(report.diagnosticFailureCount, report.fallbackDiagnosticFailureCount)) {
        diagnosticFailureCountOverflow = true;
    }
    if (!addUint32(report.diagnosticFailureCount, report.lifecycleDiagnosticFailureCount)) {
        diagnosticFailureCountOverflow = true;
    }
    if (diagnosticFailureCountOverflow) {
        report.diagnosticFailureCount = std::numeric_limits<std::uint32_t>::max();
        arp::core::appendDiagnostic(
            report.diagnostics, "WebGPU telemetry diagnostic failure count overflow");
        report.telemetryRequired = true;
    }

    report.success = report.diagnosticFailureCount == 0 && report.logicalFrameSlots == 2
        && report.requestedSwapchainImageCount == 2 && report.backendFallbacksHiddenFromUpperLayer
        && !report.nativeHandleExposedToUpperLayer;

    if (report.degradedFeatureCount > 0) {
        report.telemetryRequired = true;
    }

    return report;
}

void WebGpuRuntimeTelemetryCollector::recordDiagnostic(const std::pmr::string& diagnostics)
{
    if (diagnostics.empty()) {
        return;
    }

    report_.telemetryRequired = true;
    if (diagnostics_.size() < desc_.maxDiagnostics) {
        diagnostics_.push_back(diagnostics);
        return;
    }

    diagnosticCapacityExhausted_ = true;
    ++droppedDiagnosticCount_;
}

void WebGpuRuntimeTelemetryCollector::recordRuntimeFailure(const std::pmr::string& diagnostics)
{
    if (!addUint32(report_.runtimeDiagnosticFailureCount, 1u)) {
        recordDiagnostic("WebGPU runtime diagnostic failure count overflow");
        return;
    }
    report_.telemetryRequired = true;
    recordDiagnostic(diagnostics);
}

void WebGpuRuntimeTelemetryCollector::recordFallbackFailure(const std::pmr::string& diagnostics)
{
    if (!addUint32(report_.fallbackDiagnosticFailureCount, 1u)) {
        recordDiagnostic("WebGPU fallback diagnostic failure count overflow");
        return;
    }
    report_.telemetryRequired = true;
    recordDiagnostic(diagnostics);
}

void WebGpuRuntimeTelemetryCollector::recordLifecycleFailure(const std::pmr::string& diagnostics)
{
    if (!addUint32(report_.lifecycleDiagnosticFailureCount, 1u)) {
        recordDiagnostic("WebGPU lifecycle diagnostic failure count overflow");
        return;
    }
    report_.telemetryRequired = true;
    recordDiagnostic(diagnostics);
}

} // namespace arp::rhi::semantics
