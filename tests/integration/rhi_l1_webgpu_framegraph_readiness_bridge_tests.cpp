#include "core/memory/cpu_allocator.h"
#include "renderer/deployment/webgpu_framegraph_runtime_bridge.h"
#include "renderer/render_graph/rhi_frame_plan.h"
#include "rhi/semantics/webgpu_canvas_lifecycle_bridge.h"

#include <cassert>
#include <string>

namespace {

arp::render_graph::RhiFramePlan buildComputePreviewFrame()
{
    using namespace arp::rhi;
    using namespace arp::render_graph;

    RhiFramePlanBuilder builder({2, 6, 2, 2});
    auto result = builder.setFrameSlot(1)
                      .requireFeature(EngineFeature::PushConstants,
                          FeatureCorrectnessImpact::PreservesCorrectness,
                          "material constants")
                      .requireFeature(EngineFeature::SecondaryCommandBuffer,
                          FeatureCorrectnessImpact::PreservesCorrectness,
                          "renderer draw bundles")
                      .requireFeature(EngineFeature::ComputeDispatch,
                          FeatureCorrectnessImpact::PreservesCorrectness,
                          "browser-side compute preview")
                      .requireFeature(EngineFeature::StorageTextureBinding,
                          FeatureCorrectnessImpact::PreservesCorrectness,
                          "compute output texture")
                      .requireFeature(EngineFeature::DescriptorTable,
                          FeatureCorrectnessImpact::PerformanceOnly,
                          "bounded material descriptor table")
                      .addPass({"WebComputePreview"})
                      .build();
    assert(result.success);
    return result.plan;
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;
    using namespace arp::renderer::deployment;

    const auto framePlan = buildComputePreviewFrame();

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

    WebGpuFrameGraphDeploymentBridge bridge(promotedProfile);
    const auto promotedReadiness = bridge.evaluate(
        framePlan, makeWebGpuCanvasLifecycleState(promotedProfile), PlatformLifecycleEvent::Resize);
    assert(!promotedReadiness.success);
    assert(promotedReadiness.status == WebGpuDeploymentReadinessStatus::DiagnosticFailure);
    assert(!promotedReadiness.canStartFrame);
    assert(promotedReadiness.swapchainRebuildRequired);
    assert(promotedReadiness.telemetryRequired);
    assert(promotedReadiness.degradedFeatureCount == 0u);
    assert(promotedReadiness.diagnosticFailureCount >= 2u);
    assert(promotedReadiness.diagnostics.find("secondary command") != std::pmr::string::npos);
    assert(promotedReadiness.backendFallbacksHiddenFromUpperLayer);
    assert(!promotedReadiness.nativeHandleExposedToUpperLayer);

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
    const auto browserReadiness = buildWebGpuFrameGraphDeploymentReadiness(browserProfile,
        framePlan,
        makeWebGpuCanvasLifecycleState(browserProfile),
        PlatformLifecycleEvent::Start,
        8);
    assert(!browserReadiness.success);
    assert(browserReadiness.status == WebGpuDeploymentReadinessStatus::DiagnosticFailure);
    assert(!browserReadiness.canStartFrame);
    assert(browserReadiness.diagnostics.find("secondary command") != std::pmr::string::npos);
    assert(browserReadiness.diagnostics.find("compute dispatch") != std::pmr::string::npos);
    assert(browserReadiness.diagnostics.find("storage texture") != std::pmr::string::npos);

    auto invalidState = makeWebGpuCanvasLifecycleState(promotedProfile);
    invalidState.logicalFrameSlots = 3;
    const auto invalidLifecycleReadiness =
        bridge.evaluate(framePlan, invalidState, PlatformLifecycleEvent::Resize);
    assert(!invalidLifecycleReadiness.success);
    assert(invalidLifecycleReadiness.status == WebGpuDeploymentReadinessStatus::DiagnosticFailure);
    assert(invalidLifecycleReadiness.diagnostics.find("frame slot") != std::pmr::string::npos);
    assert(!invalidLifecycleReadiness.nativeHandleExposedToUpperLayer);

    return 0;
}
