#include "core/cpu_allocator.h"
#include "platform/render_system_rhi_active_frame_loop.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "rhi/semantics/webgpu_canvas_lifecycle_bridge.h"

#include <cassert>
#include <string>

namespace {

arp::renderer::RendererRhiDrawPlanDesc makeScenePass()
{
    arp::renderer::RendererRhiDrawPlanDesc pass;
    pass.name = "ActiveStylizedScene";
    pass.frameSlot = 1;
    pass.drawCommandCount = 24;
    pass.pushConstantSizeBytes = 192;
    pass.pipelinePushConstantCapacityBytes = 256;
    pass.pipelineLayoutName = "ActiveScenePipelineLayout";
    pass.expectedPipelineLayoutName = "ActiveScenePipelineLayout";
    pass.descriptorSetCount = 2;
    pass.descriptorTableCount = 1;
    pass.descriptorLayoutNameCount = 2;
    pass.descriptorLayoutNames[0] = "ActiveSceneFrameSet";
    pass.descriptorLayoutNames[1] = "ActiveMaterialSet";
    pass.requiredMaterialResourceCount = 3;
    pass.materialResourceCount = 3;
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

arp::platform::RenderSystemRhiActiveFrameLoopDesc makeActiveDesc(
    const arp::rhi::WebGpuRuntimeProfile& profile, bool enabled, bool guardEnabled)
{
    arp::platform::RenderSystemRhiActiveFrameLoopDesc desc;
    desc.enabled = enabled;
    desc.guardEnabled = guardEnabled;
    desc.v1FallbackAvailable = true;
    desc.requireStartupReadiness = true;
    desc.runtime.rendererFrameLoop.lifecycle.planner.deployment.preference =
        arp::renderer::deployment::RendererBackendPreference::WebFirst;
    desc.runtime.rendererFrameLoop.lifecycle.planner.deployment.candidates.maxCandidates = 2;
    desc.runtime.rendererFrameLoop.lifecycle.planner.deployment.candidates.nativeContracts = {
        makeQueriedVulkan()};
    desc.runtime.rendererFrameLoop.lifecycle.planner.deployment.candidates
        .includeWebGpuRuntimeProfile = true;
    desc.runtime.rendererFrameLoop.lifecycle.planner.deployment.candidates.webGpuProfile = profile;
    desc.runtime.rendererFrameLoop.lifecycle.planner.deployment.candidates.webGpuLifecycleState =
        arp::rhi::semantics::makeWebGpuCanvasLifecycleState(profile);
    desc.runtime.rendererFrameLoop.lifecycle.planner.deployment.candidates.webGpuLifecycleEvent =
        arp::platform::PlatformLifecycleEvent::Start;
    desc.runtime.rendererFrameLoop.lifecycle.planner.deployment.candidates.maxTelemetryDiagnostics =
        8;
    desc.runtime.rendererFrameLoop.lifecycle.planner.frameStart = {2, true, true};
    desc.runtime.rendererFrameLoop.lifecycle.maxTelemetryDiagnostics = 8;
    desc.runtime.rendererFrameLoop.logicalFrameSlots = 2;
    desc.runtime.renderSystem.logicalFrameSlots = 2;
    return desc;
}

arp::platform::RenderSystemRhiFrameLoopDryRunDesc makeDryRunDesc(
    const arp::rhi::WebGpuRuntimeProfile& profile)
{
    arp::platform::RenderSystemRhiFrameLoopDryRunDesc desc;
    desc.enabled = true;
    desc.runtime = makeActiveDesc(profile, true, true).runtime;
    return desc;
}

arp::platform::RenderSystemFrameLoopState makeReadyRenderSystemState()
{
    arp::platform::RenderSystemFrameLoopState state;
    state.renderPipelineReady = true;
    state.frameInProgress = false;
    state.nextFrameSerial = 11;
    return state;
}

arp::platform::RenderSystemRhiStartupReadinessReport makePrototypeReadiness()
{
    arp::platform::RenderSystemRhiStartupReadinessReport readiness;
    readiness.status = arp::platform::RenderSystemRhiStartupReadinessStatus::PrototypeComplete;
    readiness.canBeginFormalRhiRefactor = true;
    readiness.contractComplete = true;
    readiness.prototypeComplete = true;
    readiness.hiddenWindowValidated = true;
    readiness.diagnosticsOnly = true;
    readiness.nonBlocking = true;
    readiness.v1RenderPathUnaffected = true;
    readiness.selectedBackend = arp::rhi::Backend::WebGPU;
    readiness.acquireFrame = true;
    readiness.recordFrame = true;
    readiness.submitFrame = true;
    readiness.presentFrame = true;
    readiness.backendFallbacksHiddenFromUpperLayer = true;
    readiness.nativeHandleExposedToUpperLayer = false;
    readiness.diagnosticFailure = false;
    return readiness;
}

arp::platform::RenderSystemRhiProductionCutoverEvidence makeCompleteProductionCutoverEvidence()
{
    arp::platform::RenderSystemRhiProductionCutoverEvidence evidence;
    evidence.nullHiddenFrameSmokePassed = true;
    evidence.vulkanHiddenWindowSmokePassed = true;
    evidence.webGpuBrowserRuntimeSmokePassed = true;
    evidence.requiredPlatformSmokesPassed = true;
    evidence.nativeHeaderIsolationVerified = true;
    evidence.hotPathBounded = true;
    evidence.nonBlocking = true;
    evidence.backendFallbacksHiddenFromUpperLayer = true;
    evidence.nativeHandleExposedToUpperLayer = false;
    return evidence;
}

arp::platform::RenderSystemRhiStartupTelemetryReport buildStartupTelemetry(
    const arp::renderer::RendererRhiFramePlanBuildResult& frame,
    const arp::rhi::WebGpuRuntimeProfile& profile,
    bool hiddenWindowAvailable)
{
    arp::platform::RenderSystemRhiFrameLoopDryRunHook dryRun(makeDryRunDesc(profile));
    const auto dryRunReport = dryRun.evaluate({
        frame,
        arp::rhi::semantics::makeWebGpuCanvasLifecycleState(profile),
        makeReadyRenderSystemState(),
    });
    arp::platform::RenderSystemRhiFrameLoopDryRunSmokeDesc smokeDesc;
    smokeDesc.requireBeginFrame = true;
    const auto smoke =
        arp::platform::evaluateRenderSystemRhiFrameLoopDryRunSmoke(smokeDesc, dryRunReport);
    const auto startupProbe = arp::platform::evaluateRenderSystemRhiStartupProbe(
        arp::platform::summarizeRenderSystemRhiFrameLoopDryRunSmokeCapability(smoke));
    const auto startupGate = arp::platform::evaluateRenderSystemRhiStartupGate(
        arp::platform::summarizeRenderSystemRhiStartupProbeCapability(startupProbe));
    arp::platform::RenderSystemRhiStartupProbeScheduleDesc scheduleDesc;
    scheduleDesc.requireHiddenWindow = true;
    scheduleDesc.probeFrameCount = 1;
    const auto schedule =
        arp::platform::scheduleRenderSystemRhiStartupProbe(scheduleDesc, startupGate);
    const auto batch = arp::platform::planRenderSystemRhiStartupProbeBatch(schedule);
    arp::platform::RenderSystemRhiStartupProbeRunnerDesc runnerDesc;
    runnerDesc.hiddenWindowAvailable = hiddenWindowAvailable;
    runnerDesc.maxFrameCount = 2;
    return arp::platform::collectRenderSystemRhiStartupTelemetry(
        arp::platform::runRenderSystemRhiStartupProbeBatch(runnerDesc, batch));
}

arp::platform::RenderSystemRhiActiveFrameLoopInput makeInput(
    const arp::renderer::RendererRhiFramePlanBuildResult& frame,
    const arp::rhi::WebGpuRuntimeProfile& profile,
    const arp::platform::RenderSystemRhiStartupReadinessReport& readiness,
    const arp::platform::RenderSystemRhiStartupTelemetryReport& telemetry = {})
{
    arp::platform::RenderSystemRhiActiveFrameLoopInput input{
        frame,
        arp::rhi::semantics::makeWebGpuCanvasLifecycleState(profile),
        makeReadyRenderSystemState(),
        readiness,
    };
    input.startupTelemetry = telemetry;
    return input;
}

} // namespace

int main()
{
    using namespace arp::platform;
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    const auto frame = buildSceneFrame();
    assert(frame.success);
    const auto profile = makeBrowserProfile();
    const auto readyInput = makeInput(frame, profile, makePrototypeReadiness());

    RenderSystemRhiActiveFrameLoopHook disabledHook(makeActiveDesc(profile, false, false));
    const auto disabled = disabledHook.evaluate(readyInput);
    assert(disabled.success);
    assert(!disabled.enabled);
    assert(!disabled.guardEnabled);
    assert(disabled.v1RenderPathAvailable);
    assert(disabled.fallbackToV1);
    assert(!disabled.usedRhiFrameLoop);
    assert(disabled.action == RenderSystemFrameLoopAction::SkipFrame);
    assert(disabled.diagnostics.find("disabled") != std::pmr::string::npos);

    RenderSystemRhiActiveFrameLoopHook guardedOffHook(makeActiveDesc(profile, true, false));
    const auto guardedOff = guardedOffHook.evaluate(readyInput);
    assert(guardedOff.success);
    assert(guardedOff.enabled);
    assert(!guardedOff.guardEnabled);
    assert(guardedOff.v1RenderPathAvailable);
    assert(guardedOff.fallbackToV1);
    assert(!guardedOff.usedRhiFrameLoop);
    assert(guardedOff.diagnostics.find("guard") != std::pmr::string::npos);

    RenderSystemRhiActiveFrameLoopHook activeHook(makeActiveDesc(profile, true, true));
    const RenderSystemRhiStartupReadinessReport missingReadiness;
    const auto waiting = activeHook.evaluate(makeInput(frame, profile, missingReadiness));
    assert(waiting.success);
    assert(waiting.enabled);
    assert(waiting.guardEnabled);
    assert(!waiting.startupReadinessAccepted);
    assert(waiting.fallbackToV1);
    assert(!waiting.usedRhiFrameLoop);
    assert(waiting.action == RenderSystemFrameLoopAction::SkipFrame);
    assert(waiting.diagnostics.find("startup readiness") != std::pmr::string::npos);

    const auto begin = activeHook.evaluate(readyInput);
    assert(begin.success);
    assert(begin.activeMode);
    assert(begin.startupReadinessAccepted);
    assert(begin.v1RenderPathAvailable);
    assert(!begin.fallbackToV1);
    assert(begin.usedRhiFrameLoop);
    assert(begin.action == RenderSystemFrameLoopAction::BeginFrame);
    assert(begin.selectedBackend == Backend::WebGPU);
    assert(begin.frameSlot == 1u);
    assert(begin.frameSerial == 11u);
    assert(begin.acquireFrame);
    assert(begin.recordFrame);
    assert(begin.submitFrame);
    assert(begin.presentFrame);
    assert(begin.nonBlocking);
    assert(begin.backendFallbacksHiddenFromUpperLayer);
    assert(!begin.nativeHandleExposedToUpperLayer);

    auto leakedFrame = frame;
    leakedFrame.nativeHandleExposedToUpperLayer = true;
    const auto leaked =
        activeHook.evaluate(makeInput(leakedFrame, profile, makePrototypeReadiness()));
    assert(!leaked.success);
    assert(leaked.guardEnabled);
    assert(!leaked.usedRhiFrameLoop);
    assert(leaked.fallbackToV1);
    assert(leaked.nativeHandleExposedToUpperLayer);
    assert(leaked.diagnostics.find("native handle") != std::pmr::string::npos);

    auto smokeRequiredDesc = makeActiveDesc(profile, true, true);
    smokeRequiredDesc.requireStartupSmokeCoverage = true;
    RenderSystemRhiActiveFrameLoopHook smokeRequiredHook(smokeRequiredDesc);
    const auto startupTelemetry = buildStartupTelemetry(frame, profile, true);
    assert(startupTelemetry.success);
    assert(startupTelemetry.probeExecuted);
    assert(startupTelemetry.probeCompleted);
    const auto telemetryReadiness = evaluateRenderSystemRhiStartupReadiness(startupTelemetry);
    assert(telemetryReadiness.prototypeComplete);
    const auto smokeCoveredBegin =
        smokeRequiredHook.evaluate(makeInput(frame, profile, telemetryReadiness, startupTelemetry));
    assert(smokeCoveredBegin.success);
    assert(smokeCoveredBegin.startupSmokeCoverageAccepted);
    assert(smokeCoveredBegin.usedRhiFrameLoop);
    assert(smokeCoveredBegin.action == RenderSystemFrameLoopAction::BeginFrame);
    assert(smokeCoveredBegin.frameSerial == startupTelemetry.firstFrameSerial);

    const auto noSmokeCoverage =
        smokeRequiredHook.evaluate(makeInput(frame, profile, makePrototypeReadiness()));
    assert(noSmokeCoverage.success);
    assert(!noSmokeCoverage.startupSmokeCoverageAccepted);
    assert(noSmokeCoverage.fallbackToV1);
    assert(!noSmokeCoverage.usedRhiFrameLoop);
    assert(noSmokeCoverage.action == RenderSystemFrameLoopAction::SkipFrame);
    assert(noSmokeCoverage.diagnostics.find("smoke coverage") != std::pmr::string::npos);

    const auto failedTelemetry = buildStartupTelemetry(frame, profile, false);
    assert(!failedTelemetry.success);
    const auto failedReadiness = evaluateRenderSystemRhiStartupReadiness(failedTelemetry);
    const auto failedSmokeCoverage =
        smokeRequiredHook.evaluate(makeInput(frame, profile, failedReadiness, failedTelemetry));
    assert(failedSmokeCoverage.success);
    assert(!failedSmokeCoverage.startupSmokeCoverageAccepted);
    assert(failedSmokeCoverage.fallbackToV1);
    assert(!failedSmokeCoverage.usedRhiFrameLoop);
    assert(failedSmokeCoverage.diagnostics.find("smoke coverage") != std::pmr::string::npos);

    auto productionDefaultDesc = makeActiveDesc(profile, false, false);
    productionDefaultDesc.requestProductionDefault = true;
    productionDefaultDesc.requireStartupSmokeCoverage = true;
    productionDefaultDesc.productionCutover = makeCompleteProductionCutoverEvidence();
    RenderSystemRhiActiveFrameLoopHook productionDefaultHook(productionDefaultDesc);
    const auto productionDefault = productionDefaultHook.evaluate(
        makeInput(frame, profile, telemetryReadiness, startupTelemetry));
    assert(productionDefault.success);
    assert(productionDefault.enabled);
    assert(productionDefault.guardEnabled);
    assert(productionDefault.productionDefaultRequested);
    assert(productionDefault.productionCutoverAccepted);
    assert(productionDefault.promotedToProductionDefault);
    assert(productionDefault.usedRhiFrameLoop);
    assert(!productionDefault.fallbackToV1);
    assert(productionDefault.action == RenderSystemFrameLoopAction::BeginFrame);
    assert(productionDefault.selectedBackend == Backend::WebGPU);
    assert(productionDefault.backendFallbacksHiddenFromUpperLayer);
    assert(!productionDefault.nativeHandleExposedToUpperLayer);

    auto incompleteProductionDefaultDesc = productionDefaultDesc;
    incompleteProductionDefaultDesc.productionCutover.webGpuBrowserRuntimeSmokePassed = false;
    RenderSystemRhiActiveFrameLoopHook incompleteProductionDefaultHook(
        incompleteProductionDefaultDesc);
    const auto incompleteProductionDefault = incompleteProductionDefaultHook.evaluate(
        makeInput(frame, profile, telemetryReadiness, startupTelemetry));
    assert(incompleteProductionDefault.success);
    assert(!incompleteProductionDefault.enabled);
    assert(!incompleteProductionDefault.guardEnabled);
    assert(incompleteProductionDefault.productionDefaultRequested);
    assert(!incompleteProductionDefault.productionCutoverAccepted);
    assert(!incompleteProductionDefault.promotedToProductionDefault);
    assert(incompleteProductionDefault.fallbackToV1);
    assert(!incompleteProductionDefault.usedRhiFrameLoop);
    assert(incompleteProductionDefault.action == RenderSystemFrameLoopAction::SkipFrame);
    assert(incompleteProductionDefault.diagnostics.find("production cutover")
        != std::pmr::string::npos);

    return 0;
}
