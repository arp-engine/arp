#include "core/cpu_allocator.h"
#include "renderer/deployment/renderer_webgpu_deployment_bridge.h"
#include "rhi/semantics/webgpu_canvas_lifecycle_bridge.h"

#include <cassert>
#include <string>

namespace {

arp::renderer::RendererRhiDrawPlanDesc makeScenePass()
{
    arp::renderer::RendererRhiDrawPlanDesc pass;
    pass.name = "StylizedScene";
    pass.frameSlot = 1;
    pass.drawCommandCount = 24;
    pass.pushConstantSizeBytes = 192;
    pass.pipelinePushConstantCapacityBytes = 192;
    pass.descriptorSetCount = 2;
    pass.descriptorLayoutNameCount = 2;
    pass.descriptorLayoutNames[0] = "SceneFrameSet";
    pass.descriptorLayoutNames[1] = "MaterialSet";
    pass.descriptorTableCount = 1;
    pass.allowSoftSecondaryCommands = true;
    pass.usesTransientTargets = true;
    return pass;
}

arp::renderer::RendererRhiFramePlanBuildResult buildRendererFrame()
{
    arp::renderer::RendererRhiFramePlanBuilder builder({2, 3, 8, 2, 3});
    return builder.setFrameSlot(1).addDrawPlan(makeScenePass()).build();
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    WebGpuRuntimeProbe browserProbe;
    browserProbe.browserRuntime = true;
    browserProbe.canvasSurface = true;
    browserProbe.maxTexture2DSize = 4096;
    browserProbe.maxColorAttachments = 4;
    browserProbe.maxDescriptorBindings = 64;
    browserProbe.maxSampledTexturesPerShaderStage = 16;
    browserProbe.uniformBufferOffsetAlignment = 256;
    browserProbe.storageBufferOffsetAlignment = 256;
    browserProbe.maxUniformBufferBindingSize = 65536;
    browserProbe.maxStorageBufferBindingSize = 134217728;
    const auto browserProfile = buildWebGpuRuntimeProfile(browserProbe);

    RendererWebGpuDeploymentBridge bridge(browserProfile);
    const auto rendererFrame = buildRendererFrame();
    assert(rendererFrame.success);

    const auto readiness = bridge.evaluate(rendererFrame,
        makeWebGpuCanvasLifecycleState(browserProfile),
        PlatformLifecycleEvent::Start);
    assert(readiness.success);
    assert(readiness.status == WebGpuDeploymentReadinessStatus::ContractComplete);
    assert(readiness.canStartFrame);
    assert(readiness.telemetryRequired);
    assert(readiness.degradedFeatureCount == 2u);
    assert(readiness.backendFallbacksHiddenFromUpperLayer);
    assert(!readiness.nativeHandleExposedToUpperLayer);

    arp::renderer::RendererRhiFramePlanBuildResult failedRendererFrame;
    failedRendererFrame.success = false;
    failedRendererFrame.diagnostics = "renderer RHI frame plan draw plan capacity exhausted";
    const auto failedReadiness = evaluateRendererWebGpuDeploymentReadiness(browserProfile,
        failedRendererFrame,
        makeWebGpuCanvasLifecycleState(browserProfile),
        PlatformLifecycleEvent::Start);
    assert(!failedReadiness.success);
    assert(failedReadiness.status == WebGpuDeploymentReadinessStatus::DiagnosticFailure);
    assert(!failedReadiness.canStartFrame);
    assert(failedReadiness.telemetryRequired);
    assert(failedReadiness.diagnosticFailureCount == 1u);
    assert(failedReadiness.diagnostics.find("draw plan capacity") != std::pmr::string::npos);
    assert(!failedReadiness.nativeHandleExposedToUpperLayer);

    auto leakedRendererFrame = rendererFrame;
    leakedRendererFrame.nativeHandleExposedToUpperLayer = true;
    const auto leakedReadiness = bridge.evaluate(leakedRendererFrame,
        makeWebGpuCanvasLifecycleState(browserProfile),
        PlatformLifecycleEvent::Start);
    assert(!leakedReadiness.success);
    assert(leakedReadiness.status == WebGpuDeploymentReadinessStatus::DiagnosticFailure);
    assert(leakedReadiness.nativeHandleExposedToUpperLayer);
    assert(leakedReadiness.diagnostics.find("native handle") != std::pmr::string::npos);

    auto branchingRendererFrame = rendererFrame;
    branchingRendererFrame.backendSpecificBranchingRequired = true;
    const auto branchingReadiness = bridge.evaluate(branchingRendererFrame,
        makeWebGpuCanvasLifecycleState(browserProfile),
        PlatformLifecycleEvent::Start);
    assert(!branchingReadiness.success);
    assert(branchingReadiness.status == WebGpuDeploymentReadinessStatus::DiagnosticFailure);
    assert(branchingReadiness.diagnostics.find("backend branching") != std::pmr::string::npos);

    return 0;
}
