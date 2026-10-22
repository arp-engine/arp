#include "core/memory/cpu_allocator.h"
#include "rhi/semantics/webgpu_canvas_lifecycle_bridge.h"
#include "rhi/semantics/webgpu_runtime_telemetry.h"

#include <cassert>
#include <limits>
#include <string>

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    WebGpuRuntimeProbe promotedProbe;
    promotedProbe.browserRuntime = true;
    promotedProbe.canvasSurface = true;
    promotedProbe.nativeDevice = true;
    promotedProbe.computeDispatch = true;
    promotedProbe.storageTextureBinding = true;
    promotedProbe.bgra8UnormStorage = true;
    promotedProbe.maxTexture2DSize = 8192;
    promotedProbe.maxColorAttachments = 4;
    promotedProbe.maxDescriptorBindings = 64;
    promotedProbe.maxSampledTexturesPerShaderStage = 16;
    promotedProbe.uniformBufferOffsetAlignment = 256;
    promotedProbe.storageBufferOffsetAlignment = 256;
    promotedProbe.maxUniformBufferBindingSize = 65536;
    promotedProbe.maxStorageBufferBindingSize = 134217728;
    const auto promotedProfile = buildWebGpuRuntimeProfile(promotedProbe);
    const auto promotedPlan = buildWebGpuRuntimeFallbackPlan(
        promotedProfile, webGpuComputeFrameFeatureRequirements(promotedProfile));

    auto promotedState = makeWebGpuCanvasLifecycleState(promotedProfile);
    const auto resizeDecision = lowerWebGpuCanvasLifecycleEvent(
        promotedProfile, promotedState, PlatformLifecycleEvent::Resize);

    WebGpuRuntimeTelemetryCollector collector({8});
    collector.recordProfile(promotedProfile);
    collector.recordFallbackPlan(promotedPlan);
    collector.recordLifecycleDecision(resizeDecision);
    const auto promotedReport = collector.buildReport();

    assert(!promotedReport.success);
    assert(promotedReport.backend == Backend::WebGPU);
    assert(promotedReport.profileStatus == WebGpuRuntimeProfileStatus::PlatformValidated);
    assert(promotedReport.logicalFrameSlots == 2u);
    assert(promotedReport.requestedSwapchainImageCount == 2u);
    assert(promotedReport.nativeFeatureCount == 2u);
    assert(promotedReport.emulatedFeatureCount == 1u);
    assert(promotedReport.cpuReplayFeatureCount == 0u);
    assert(promotedReport.degradedFeatureCount == 0u);
    assert(promotedReport.diagnosticFailureCount == 1u);
    assert(promotedReport.telemetryRequired);
    assert(promotedReport.backendFallbacksHiddenFromUpperLayer);
    assert(!promotedReport.nativeHandleExposedToUpperLayer);
    assert(promotedReport.swapchainRebuildRequired);
    assert(promotedReport.acquireAllowed);
    assert(promotedReport.presentAllowed);
    assert(promotedReport.diagnostics.find("descriptor table") != std::pmr::string::npos);

    WebGpuRuntimeProbe browserProbe;
    browserProbe.browserRuntime = true;
    browserProbe.canvasSurface = true;
    browserProbe.maxTexture2DSize = 4096;
    browserProbe.maxColorAttachments = 1;
    browserProbe.maxDescriptorBindings = 32;
    browserProbe.maxSampledTexturesPerShaderStage = 16;
    browserProbe.uniformBufferOffsetAlignment = 256;
    browserProbe.storageBufferOffsetAlignment = 256;
    browserProbe.maxUniformBufferBindingSize = 65536;
    browserProbe.maxStorageBufferBindingSize = 134217728;
    const auto browserProfile = buildWebGpuRuntimeProfile(browserProbe);
    const auto browserPlan = buildWebGpuRuntimeFallbackPlan(
        browserProfile, webGpuComputeFrameFeatureRequirements(browserProfile));
    const auto lostDecision = lowerWebGpuCanvasLifecycleEvent(browserProfile,
        makeWebGpuCanvasLifecycleState(browserProfile),
        PlatformLifecycleEvent::SurfaceLost);

    WebGpuRuntimeTelemetryCollector failureCollector({8});
    failureCollector.recordProfile(browserProfile);
    failureCollector.recordFallbackPlan(browserPlan);
    failureCollector.recordLifecycleDecision(lostDecision);
    const auto failureReport = failureCollector.buildReport();

    assert(!failureReport.success);
    assert(failureReport.fallbackDiagnosticFailureCount == 4u);
    assert(failureReport.diagnosticFailureCount == 4u);
    assert(failureReport.releaseSurfaceResources);
    assert(!failureReport.acquireAllowed);
    assert(!failureReport.presentAllowed);
    assert(failureReport.diagnostics.find("compute dispatch") != std::pmr::string::npos);
    assert(failureReport.diagnostics.find("storage texture") != std::pmr::string::npos);
    assert(!failureReport.nativeHandleExposedToUpperLayer);

    WebGpuRuntimeProbe invalidProbe = browserProbe;
    invalidProbe.canvasSurface = false;
    const auto invalidProfile = buildWebGpuRuntimeProfile(invalidProbe);
    const auto invalidPlan = buildWebGpuRuntimeFallbackPlan(
        invalidProfile, webGpuComputeFrameFeatureRequirements(invalidProfile));

    WebGpuRuntimeTelemetryCollector boundedCollector({1});
    boundedCollector.recordProfile(invalidProfile);
    boundedCollector.recordFallbackPlan(invalidPlan);
    const auto boundedReport = boundedCollector.buildReport();

    assert(!boundedReport.success);
    assert(boundedReport.runtimeDiagnosticFailureCount == 1u);
    assert(boundedReport.diagnosticCapacityExhausted);
    assert(boundedReport.recordedDiagnosticCount == 1u);
    assert(boundedReport.droppedDiagnosticCount > 0u);
    assert(boundedReport.diagnostics.find("telemetry diagnostics capacity exhausted")
        != std::pmr::string::npos);
    assert(!boundedReport.nativeHandleExposedToUpperLayer);

    WebGpuRuntimeTelemetryCollector overflowCollector({4});
    overflowCollector.recordProfile(promotedProfile);

    EngineFallbackPlan saturatedPlan;
    saturatedPlan.success = true;
    saturatedPlan.backend = Backend::WebGPU;
    saturatedPlan.logicalFrameSlots = 2;
    saturatedPlan.nativeFeatureCount = std::numeric_limits<std::uint32_t>::max();
    saturatedPlan.backendFallbacksHiddenFromUpperLayer = true;
    overflowCollector.recordFallbackPlan(saturatedPlan);

    EngineFallbackPlan overflowPlan;
    overflowPlan.success = true;
    overflowPlan.backend = Backend::WebGPU;
    overflowPlan.logicalFrameSlots = 2;
    overflowPlan.nativeFeatureCount = 1u;
    overflowPlan.backendFallbacksHiddenFromUpperLayer = true;
    overflowCollector.recordFallbackPlan(overflowPlan);

    const auto overflowReport = overflowCollector.buildReport();
    assert(!overflowReport.success);
    assert(overflowReport.nativeFeatureCount == std::numeric_limits<std::uint32_t>::max());
    assert(overflowReport.fallbackDiagnosticFailureCount == 1u);
    assert(overflowReport.diagnosticFailureCount == 1u);
    assert(overflowReport.telemetryRequired);
    assert(overflowReport.diagnostics.find("native feature count") != std::pmr::string::npos);
    assert(overflowReport.diagnostics.find("overflow") != std::pmr::string::npos);

    return 0;
}
