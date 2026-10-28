#include "core/memory/cpu_allocator.h"
#include "renderer/deployment/webgpu_deployment_readiness.h"
#include "rhi/frontend/webgpu_runtime_telemetry.h"
#include "rhi/semantics/webgpu_canvas_lifecycle_bridge.h"

#include <cassert>
#include <string>

namespace {

arp::rhi::semantics::WebGpuRuntimeTelemetryReport buildTelemetryReport(
    const arp::rhi::WebGpuRuntimeProfile& profile,
    const arp::rhi::EngineFallbackPlan& plan,
    const arp::rhi::semantics::NativePlatformLifecycleLoweringDecision& decision)
{
    arp::rhi::semantics::WebGpuRuntimeTelemetryCollector collector({8});
    collector.recordProfile(profile);
    collector.recordFallbackPlan(plan);
    collector.recordLifecycleDecision(decision);
    return collector.buildReport();
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::platform;
    using namespace arp::rhi::semantics;
    using namespace arp::renderer::deployment;

    WebGpuRuntimeProbe promotedProbe;
    promotedProbe.browserRuntime = true;
    promotedProbe.canvasSurface = true;
    promotedProbe.nativeDevice = true;
    promotedProbe.computeDispatch = true;
    promotedProbe.storageTextureBinding = true;
    promotedProbe.maxTexture2DSize = 8192;
    promotedProbe.maxColorAttachments = 4;
    promotedProbe.maxDescriptorBindings = 64;
    promotedProbe.maxSampledTexturesPerShaderStage = 16;
    promotedProbe.uniformBufferOffsetAlignment = 256;
    promotedProbe.storageBufferOffsetAlignment = 256;
    promotedProbe.maxUniformBufferBindingSize = 65536;
    promotedProbe.maxStorageBufferBindingSize = 134217728;
    const auto promotedProfile = buildWebGpuRuntimeProfile(promotedProbe);
    const auto promotedReport = buildTelemetryReport(promotedProfile,
        buildWebGpuRuntimeFallbackPlan(
            promotedProfile, webGpuComputeFrameFeatureRequirements(promotedProfile)),
        lowerWebGpuCanvasLifecycleEvent(promotedProfile,
            makeWebGpuCanvasLifecycleState(promotedProfile),
            PlatformLifecycleEvent::Resize));

    WebGpuDeploymentReadinessGate gate;
    const auto promotedReadiness = gate.evaluate(promotedReport);
    assert(!promotedReadiness.success);
    assert(promotedReadiness.status == WebGpuDeploymentReadinessStatus::DiagnosticFailure);
    assert(!promotedReadiness.contractComplete);
    assert(!promotedReadiness.platformValidated);
    assert(!promotedReadiness.canStartFrame);
    assert(promotedReadiness.swapchainRebuildRequired);
    assert(promotedReadiness.telemetryRequired);
    assert(promotedReadiness.degradedFeatureCount == 0u);
    assert(promotedReadiness.diagnosticFailureCount == 2u);
    assert(promotedReadiness.backendFallbacksHiddenFromUpperLayer);
    assert(!promotedReadiness.nativeHandleExposedToUpperLayer);

    WebGpuRuntimeProbe contractProbe;
    contractProbe.browserRuntime = true;
    contractProbe.canvasSurface = true;
    contractProbe.maxTexture2DSize = 4096;
    contractProbe.maxColorAttachments = 1;
    contractProbe.maxDescriptorBindings = 32;
    contractProbe.maxSampledTexturesPerShaderStage = 16;
    contractProbe.uniformBufferOffsetAlignment = 256;
    contractProbe.storageBufferOffsetAlignment = 256;
    contractProbe.maxUniformBufferBindingSize = 65536;
    contractProbe.maxStorageBufferBindingSize = 134217728;
    const auto contractProfile = buildWebGpuRuntimeProfile(contractProbe);
    const auto contractReport = buildTelemetryReport(contractProfile,
        buildWebGpuRuntimeFallbackPlan(
            contractProfile, webGpuDefaultFrameFeatureRequirements(contractProfile)),
        lowerWebGpuCanvasLifecycleEvent(contractProfile,
            makeWebGpuCanvasLifecycleState(contractProfile),
            PlatformLifecycleEvent::Start));

    const auto contractReadiness = evaluateWebGpuDeploymentReadiness(contractReport);
    assert(contractReadiness.success);
    assert(contractReadiness.status == WebGpuDeploymentReadinessStatus::ContractComplete);
    assert(contractReadiness.contractComplete);
    assert(!contractReadiness.platformValidated);
    assert(contractReadiness.canStartFrame);
    assert(!contractReadiness.telemetryRequired);
    assert(contractReadiness.backendFallbacksHiddenFromUpperLayer);

    WebGpuRuntimeProbe failingProbe = contractProbe;
    failingProbe.canvasSurface = false;
    const auto failingProfile = buildWebGpuRuntimeProfile(failingProbe);
    const auto failingReport = buildTelemetryReport(failingProfile,
        buildWebGpuRuntimeFallbackPlan(
            failingProfile, webGpuComputeFrameFeatureRequirements(failingProfile)),
        lowerWebGpuCanvasLifecycleEvent(failingProfile,
            makeWebGpuCanvasLifecycleState(failingProfile),
            PlatformLifecycleEvent::SurfaceCreated));

    const auto failingReadiness = gate.evaluate(failingReport);
    assert(!failingReadiness.success);
    assert(failingReadiness.status == WebGpuDeploymentReadinessStatus::DiagnosticFailure);
    assert(!failingReadiness.canStartFrame);
    assert(failingReadiness.telemetryRequired);
    assert(failingReadiness.diagnosticFailureCount >= 1u);
    assert(failingReadiness.diagnostics.find("canvas") != std::pmr::string::npos);
    assert(!failingReadiness.nativeHandleExposedToUpperLayer);

    // CPU replay is rejected by the WebGPU semantic telemetry collector. The
    // deployment gate consumes that canonical failure count and must not add
    // the same failure a second time.
    EngineFallbackPlan cpuReplayPlan;
    cpuReplayPlan.success = true;
    cpuReplayPlan.backend = Backend::WebGPU;
    cpuReplayPlan.cpuReplayFeatureCount = 1;
    cpuReplayPlan.backendFallbacksHiddenFromUpperLayer = true;
    cpuReplayPlan.logicalFrameSlots = 2;

    WebGpuRuntimeTelemetryCollector cpuReplayCollector({8});
    cpuReplayCollector.recordProfile(promotedProfile);
    cpuReplayCollector.recordFallbackPlan(cpuReplayPlan);
    const auto cpuReplayReport = cpuReplayCollector.buildReport();
    assert(!cpuReplayReport.success);
    assert(cpuReplayReport.cpuReplayFeatureCount == 1u);
    assert(cpuReplayReport.fallbackDiagnosticFailureCount == 1u);
    assert(cpuReplayReport.diagnosticFailureCount == 1u);

    const auto cpuReplayReadiness = evaluateWebGpuDeploymentReadiness(cpuReplayReport);
    assert(!cpuReplayReadiness.success);
    assert(cpuReplayReadiness.cpuReplayFeatureCount == 1u);
    assert(cpuReplayReadiness.diagnosticFailureCount == 1u);
    assert(cpuReplayReadiness.diagnostics.find("CPU replay") != std::pmr::string::npos);

    return 0;
}
