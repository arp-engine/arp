#include "core/memory/cpu_allocator.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "rhi/semantics/renderer_frame_lifecycle_planner.h"
#include "rhi/semantics/renderer_frame_lifecycle_telemetry.h"
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

arp::renderer::RendererRhiFramePlanBuildResult buildSceneFrame()
{
    arp::renderer::RendererRhiFramePlanBuilder builder({2, 2, 8, 2, 2});
    return builder.setFrameSlot(1).addDrawPlan(makeScenePass()).build();
}

arp::rhi::semantics::NativeBackendCapabilityContract makeQueriedVulkan()
{
    auto contract = arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Vulkan);
    contract.queriedFromNativeDevice = true;
    contract.queriedExtendedFeatures = true;
    return contract;
}

arp::rhi::WebGpuRuntimeProfile makeBrowserProfile()
{
    arp::rhi::WebGpuRuntimeProbe probe;
    probe.browserRuntime = true;
    probe.canvasSurface = true;
    probe.maxTexture2DSize = 4096;
    probe.maxColorAttachments = 4;
    probe.maxDescriptorBindings = 64;
    probe.maxSampledTexturesPerShaderStage = 16;
    probe.uniformBufferOffsetAlignment = 256;
    probe.storageBufferOffsetAlignment = 256;
    probe.maxUniformBufferBindingSize = 65536;
    probe.maxStorageBufferBindingSize = 134217728;
    return arp::rhi::buildWebGpuRuntimeProfile(probe);
}

arp::rhi::semantics::RendererFrameLifecyclePlanner makePlanner(
    const arp::rhi::WebGpuRuntimeProfile& profile)
{
    return arp::rhi::semantics::RendererFrameLifecyclePlanner({
        {
            arp::renderer::deployment::RendererBackendPreference::WebFirst,
            {
                2,
                {makeQueriedVulkan()},
                true,
                profile,
                arp::rhi::semantics::makeWebGpuCanvasLifecycleState(profile),
                arp::platform::PlatformLifecycleEvent::Start,
                8,
            },
        },
        {2, true, true},
    });
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    const auto frame = buildSceneFrame();
    assert(frame.success);
    const auto profile = makeBrowserProfile();
    auto planner = makePlanner(profile);
    const auto lifecycle = planner.plan(frame, makeWebGpuCanvasLifecycleState(profile));
    assert(lifecycle.success);

    RendererFrameLifecycleTelemetryCollector collector({4});
    collector.record(lifecycle);
    const auto report = collector.buildReport();
    assert(report.success);
    assert(report.canStartFrame);
    assert(report.selectedBackend == Backend::WebGPU);
    assert(report.frameSlot == 1u);
    assert(report.candidateCount == 2u);
    assert(report.degradedFeatureCount == 2u);
    assert(report.telemetryRequired);
    assert(report.backendFallbacksHiddenFromUpperLayer);
    assert(!report.nativeHandleExposedToUpperLayer);
    assert(report.recordedDiagnosticCount == 0u);

    auto pausedState = makeWebGpuCanvasLifecycleState(profile);
    pausedState.acquireAllowed = false;
    pausedState.presentAllowed = false;
    const auto pausedLifecycle = planner.plan(frame, pausedState);
    assert(!pausedLifecycle.success);

    RendererFrameLifecycleTelemetryCollector pausedCollector({2});
    pausedCollector.record(pausedLifecycle);
    const auto pausedReport = pausedCollector.buildReport();
    assert(!pausedReport.success);
    assert(!pausedReport.canStartFrame);
    assert(pausedReport.frameStartDiagnosticFailureCount == 1u);
    assert(pausedReport.diagnosticFailureCount == 1u);
    assert(pausedReport.recordedDiagnosticCount == 1u);
    assert(pausedReport.diagnostics.find("acquire") != std::pmr::string::npos);

    RendererFrameLifecycleTelemetryCollector boundedCollector({1});
    boundedCollector.record(pausedLifecycle);
    boundedCollector.record(pausedLifecycle);
    const auto boundedReport = boundedCollector.buildReport();
    assert(!boundedReport.success);
    assert(boundedReport.diagnosticCapacityExhausted);
    assert(boundedReport.droppedDiagnosticCount == 1u);
    assert(boundedReport.diagnostics.find("capacity") != std::pmr::string::npos);

    RendererFrameLifecycleTelemetryCollector emptyCollector({1});
    const auto emptyReport = emptyCollector.buildReport();
    assert(!emptyReport.success);
    assert(emptyReport.diagnosticFailureCount == 1u);
    assert(emptyReport.diagnostics.find("lifecycle plan") != std::pmr::string::npos);

    return 0;
}
