#include "core/cpu_allocator.h"
#include "platform/render_system_rhi_dry_run.h"
#include "platform/render_system_rhi_startup_readiness.h"
#include "renderer/rhi_draw_plan_bridge.h"
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

arp::platform::RenderSystemRhiFrameLoopDryRunDesc makeDryRunDesc(
    const arp::rhi::WebGpuRuntimeProfile& profile, bool enabled = true)
{
    arp::platform::RenderSystemRhiFrameLoopDryRunDesc desc;
    desc.enabled = enabled;
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

arp::platform::RenderSystemFrameLoopState makeReadyRenderSystemState()
{
    arp::platform::RenderSystemFrameLoopState state;
    state.renderPipelineReady = true;
    state.frameInProgress = false;
    state.nextFrameSerial = 7;
    return state;
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

    RenderSystemRhiFrameLoopDryRunHook hook(makeDryRunDesc(profile));
    const auto begin = hook.evaluate({
        frame,
        makeWebGpuCanvasLifecycleState(profile),
        makeReadyRenderSystemState(),
    });
    assert(begin.enabled);
    assert(begin.dryRunOnly);
    assert(begin.v1RenderPathUnaffected);
    assert(begin.success);
    assert(begin.action == RenderSystemFrameLoopAction::BeginFrame);
    assert(begin.selectedBackend == Backend::WebGPU);
    assert(begin.frameSlot == 1u);
    assert(begin.frameSerial == 7u);
    assert(begin.acquireFrame);
    assert(begin.recordFrame);
    assert(begin.submitFrame);
    assert(begin.presentFrame);
    assert(begin.telemetryRequired);
    assert(begin.runtimePlan.rendererFrameLoop.success);
    assert(begin.runtimePlan.renderSystemPlan.success);

    RenderSystemRhiFrameLoopDryRunSmokeDesc smokeDesc;
    smokeDesc.requireBeginFrame = true;
    const auto beginSmoke = evaluateRenderSystemRhiFrameLoopDryRunSmoke(smokeDesc, begin);
    assert(beginSmoke.success);
    assert(beginSmoke.dryRunOnly);
    assert(beginSmoke.v1RenderPathUnaffected);
    assert(beginSmoke.frameWouldBegin);
    assert(!beginSmoke.frameWouldSkip);
    assert(!beginSmoke.maintenanceWouldRun);
    assert(!beginSmoke.diagnosticFailure);
    assert(beginSmoke.acquireFrame);
    assert(beginSmoke.recordFrame);
    assert(beginSmoke.submitFrame);
    assert(beginSmoke.presentFrame);
    assert(beginSmoke.dryRunReport.frameSerial == begin.frameSerial);
    const auto beginSmokeCaps = summarizeRenderSystemRhiFrameLoopDryRunSmokeCapability(beginSmoke);
    assert(beginSmokeCaps.readyForStartupProbe);
    assert(beginSmokeCaps.frameWouldBegin);
    assert(beginSmokeCaps.selectedBackend == Backend::WebGPU);
    assert(beginSmokeCaps.frameSlot == 1u);
    assert(beginSmokeCaps.frameSerial == 7u);
    const auto startupProbe = evaluateRenderSystemRhiStartupProbe(beginSmokeCaps);
    assert(startupProbe.success);
    assert(startupProbe.readyForStartup);
    assert(startupProbe.dryRunOnly);
    assert(startupProbe.v1RenderPathUnaffected);
    assert(startupProbe.selectedBackend == Backend::WebGPU);
    assert(startupProbe.frameSlot == 1u);
    assert(startupProbe.frameSerial == 7u);
    assert(startupProbe.acquireFrame);
    assert(startupProbe.recordFrame);
    assert(startupProbe.submitFrame);
    assert(startupProbe.presentFrame);
    assert(startupProbe.backendFallbacksHiddenFromUpperLayer);
    assert(!startupProbe.nativeHandleExposedToUpperLayer);
    const auto startupProbeCaps = summarizeRenderSystemRhiStartupProbeCapability(startupProbe);
    assert(startupProbeCaps.available);
    assert(startupProbeCaps.lastEvaluationSucceeded);
    assert(startupProbeCaps.readyForStartup);
    assert(startupProbeCaps.selectedBackend == Backend::WebGPU);
    assert(startupProbeCaps.frameSlot == 1u);
    assert(startupProbeCaps.frameSerial == 7u);
    assert(startupProbeCaps.acquireFrame);
    assert(startupProbeCaps.recordFrame);
    assert(startupProbeCaps.submitFrame);
    assert(startupProbeCaps.presentFrame);
    assert(startupProbeCaps.backendFallbacksHiddenFromUpperLayer);
    assert(!startupProbeCaps.nativeHandleExposedToUpperLayer);
    const auto startupGate = evaluateRenderSystemRhiStartupGate(startupProbeCaps);
    assert(startupGate.success);
    assert(startupGate.allowStartup);
    assert(startupGate.action == RenderSystemRhiStartupGateAction::RunStartupProbe);
    assert(startupGate.selectedBackend == Backend::WebGPU);
    assert(startupGate.frameSlot == 1u);
    assert(startupGate.frameSerial == 7u);
    assert(startupGate.acquireFrame);
    assert(startupGate.recordFrame);
    assert(startupGate.submitFrame);
    assert(startupGate.presentFrame);
    assert(startupGate.dryRunOnly);
    assert(startupGate.v1RenderPathUnaffected);
    assert(startupGate.backendFallbacksHiddenFromUpperLayer);
    assert(!startupGate.nativeHandleExposedToUpperLayer);
    RenderSystemRhiStartupProbeScheduleDesc scheduleDesc;
    scheduleDesc.requireHiddenWindow = true;
    scheduleDesc.probeFrameCount = 1;
    const auto startupSchedule = scheduleRenderSystemRhiStartupProbe(scheduleDesc, startupGate);
    assert(startupSchedule.success);
    assert(startupSchedule.scheduled);
    assert(startupSchedule.hiddenWindowRequired);
    assert(startupSchedule.dryRunOnly);
    assert(startupSchedule.v1RenderPathUnaffected);
    assert(startupSchedule.selectedBackend == Backend::WebGPU);
    assert(startupSchedule.frameSlot == 1u);
    assert(startupSchedule.frameSerial == 7u);
    assert(startupSchedule.logicalFrameSlots == 2u);
    assert(startupSchedule.probeFrameCount == 1u);
    assert(startupSchedule.acquireFrame);
    assert(startupSchedule.recordFrame);
    assert(startupSchedule.submitFrame);
    assert(startupSchedule.presentFrame);
    assert(!startupSchedule.waitForReadiness);
    assert(!startupSchedule.runMaintenance);
    assert(!startupSchedule.diagnosticFailure);
    assert(startupSchedule.backendFallbacksHiddenFromUpperLayer);
    assert(!startupSchedule.nativeHandleExposedToUpperLayer);
    const auto startupBatch = planRenderSystemRhiStartupProbeBatch(startupSchedule);
    assert(startupBatch.success);
    assert(startupBatch.canRunProbeBatch);
    assert(startupBatch.frameCount == 1u);
    assert(startupBatch.firstFrameSerial == 7u);
    assert(startupBatch.lastFrameSerial == 7u);
    assert(startupBatch.logicalFrameSlots == 2u);
    assert(startupBatch.hiddenWindowRequired);
    assert(startupBatch.dryRunOnly);
    assert(startupBatch.v1RenderPathUnaffected);
    assert(startupBatch.nonBlocking);
    assert(startupBatch.backendFallbacksHiddenFromUpperLayer);
    assert(!startupBatch.nativeHandleExposedToUpperLayer);
    RenderSystemRhiStartupProbeRunnerDesc runnerDesc;
    runnerDesc.hiddenWindowAvailable = true;
    runnerDesc.maxFrameCount = 2;
    const auto startupRun = runRenderSystemRhiStartupProbeBatch(runnerDesc, startupBatch);
    assert(startupRun.success);
    assert(startupRun.executed);
    assert(startupRun.completed);
    assert(startupRun.action == RenderSystemRhiStartupProbeRunAction::RunHiddenWindowProbe);
    assert(startupRun.hiddenWindowUsed);
    assert(startupRun.diagnosticsOnly);
    assert(startupRun.v1RenderPathUnaffected);
    assert(startupRun.nonBlocking);
    assert(startupRun.frameCount == 1u);
    assert(startupRun.completedFrameCount == 1u);
    assert(startupRun.firstFrameSerial == 7u);
    assert(startupRun.lastFrameSerial == 7u);
    assert(startupRun.acquireFrame);
    assert(startupRun.recordFrame);
    assert(startupRun.submitFrame);
    assert(startupRun.presentFrame);
    assert(startupRun.backendFallbacksHiddenFromUpperLayer);
    assert(!startupRun.nativeHandleExposedToUpperLayer);
    const auto startupTelemetry = collectRenderSystemRhiStartupTelemetry(startupRun);
    assert(startupTelemetry.success);
    assert(startupTelemetry.probeExecuted);
    assert(startupTelemetry.probeCompleted);
    assert(startupTelemetry.hiddenWindowUsed);
    assert(startupTelemetry.diagnosticsOnly);
    assert(startupTelemetry.nonBlocking);
    assert(startupTelemetry.selectedBackend == Backend::WebGPU);
    assert(startupTelemetry.frameCount == 1u);
    assert(startupTelemetry.completedFrameCount == 1u);
    assert(startupTelemetry.firstFrameSerial == 7u);
    assert(startupTelemetry.lastFrameSerial == 7u);
    assert(startupTelemetry.acquireFrame);
    assert(startupTelemetry.recordFrame);
    assert(startupTelemetry.submitFrame);
    assert(startupTelemetry.presentFrame);
    assert(startupTelemetry.telemetryRequired);
    assert(startupTelemetry.backendFallbacksHiddenFromUpperLayer);
    assert(!startupTelemetry.nativeHandleExposedToUpperLayer);

    const auto startupReadiness = evaluateRenderSystemRhiStartupReadiness(startupTelemetry);
    assert(startupReadiness.status == RenderSystemRhiStartupReadinessStatus::PrototypeComplete);
    assert(startupReadiness.canBeginFormalRhiRefactor);
    assert(startupReadiness.contractComplete);
    assert(startupReadiness.prototypeComplete);
    assert(!startupReadiness.backendValidated);
    assert(!startupReadiness.platformValidated);
    assert(!startupReadiness.diagnosticFailure);
    assert(startupReadiness.hiddenWindowValidated);
    assert(startupReadiness.backendFallbacksHiddenFromUpperLayer);
    assert(!startupReadiness.nativeHandleExposedToUpperLayer);

    const auto formalGate = evaluateRenderSystemRhiFormalRefactorStartGate(startupReadiness);
    assert(formalGate.success);
    assert(formalGate.allowFormalRefactorStart);
    assert(!formalGate.allowRendererMigration);
    assert(!formalGate.allowFrameLoopMigration);
    assert(formalGate.requireTelemetry == startupTelemetry.telemetryRequired);
    assert(!formalGate.requireHiddenWindowValidation);
    assert(formalGate.status == RenderSystemRhiStartupReadinessStatus::PrototypeComplete);
    assert(formalGate.selectedBackend == Backend::WebGPU);
    assert(formalGate.backendFallbacksHiddenFromUpperLayer);
    assert(!formalGate.nativeHandleExposedToUpperLayer);

    auto noHiddenWindowRunnerDesc = runnerDesc;
    noHiddenWindowRunnerDesc.hiddenWindowAvailable = false;
    const auto noHiddenWindowRun =
        runRenderSystemRhiStartupProbeBatch(noHiddenWindowRunnerDesc, startupBatch);
    assert(!noHiddenWindowRun.success);
    assert(!noHiddenWindowRun.executed);
    assert(noHiddenWindowRun.diagnosticFailure);
    assert(noHiddenWindowRun.diagnostics.find("hidden window") != std::pmr::string::npos);
    const auto noHiddenWindowTelemetry = collectRenderSystemRhiStartupTelemetry(noHiddenWindowRun);
    assert(!noHiddenWindowTelemetry.success);
    assert(!noHiddenWindowTelemetry.probeExecuted);
    assert(noHiddenWindowTelemetry.diagnosticFailure);
    const auto noHiddenWindowReadiness =
        evaluateRenderSystemRhiStartupReadiness(noHiddenWindowTelemetry);
    assert(
        noHiddenWindowReadiness.status == RenderSystemRhiStartupReadinessStatus::DiagnosticFailure);
    assert(!noHiddenWindowReadiness.canBeginFormalRhiRefactor);
    const auto noHiddenWindowFormalGate =
        evaluateRenderSystemRhiFormalRefactorStartGate(noHiddenWindowReadiness);
    assert(!noHiddenWindowFormalGate.success);
    assert(!noHiddenWindowFormalGate.allowFormalRefactorStart);
    assert(noHiddenWindowFormalGate.requireHiddenWindowValidation);

    auto pausedLifecycle = makeWebGpuCanvasLifecycleState(profile);
    pausedLifecycle.acquireAllowed = false;
    pausedLifecycle.presentAllowed = false;
    const auto skipped = evaluateRenderSystemRhiFrameLoopDryRun(makeDryRunDesc(profile),
        {
            frame,
            pausedLifecycle,
            makeReadyRenderSystemState(),
        });
    assert(skipped.enabled);
    assert(skipped.success);
    assert(skipped.action == RenderSystemFrameLoopAction::SkipFrame);
    assert(skipped.skipFrame);
    assert(!skipped.acquireFrame);
    assert(skipped.collectCompletedSubmissions);
    assert(skipped.diagnostics.find("acquire") != std::pmr::string::npos);

    auto maintenanceLifecycle = makeWebGpuCanvasLifecycleState(profile);
    maintenanceLifecycle.acquireAllowed = false;
    maintenanceLifecycle.presentAllowed = false;
    maintenanceLifecycle.swapchainRebuildRequired = true;
    maintenanceLifecycle.releaseSurfaceResources = true;
    maintenanceLifecycle.releaseTransientResources = true;
    maintenanceLifecycle.memoryBudgetRefreshRequired = true;
    const auto maintenance = hook.evaluate({
        frame,
        maintenanceLifecycle,
        makeReadyRenderSystemState(),
    });
    assert(maintenance.success);
    assert(maintenance.action == RenderSystemFrameLoopAction::MaintenanceRequired);
    assert(maintenance.runMaintenance);
    assert(maintenance.releaseCurrentFrame);
    assert(maintenance.swapchainRebuildRequired);
    assert(maintenance.releaseSurfaceResources);
    assert(maintenance.releaseTransientResources);
    assert(maintenance.memoryBudgetRefreshRequired);
    RenderSystemRhiFrameLoopDryRunSmokeDesc maintenanceSmokeDesc;
    maintenanceSmokeDesc.requireBeginFrame = false;
    maintenanceSmokeDesc.allowMaintenance = true;
    const auto maintenanceSmoke =
        evaluateRenderSystemRhiFrameLoopDryRunSmoke(maintenanceSmokeDesc, maintenance);
    assert(maintenanceSmoke.success);
    assert(maintenanceSmoke.maintenanceWouldRun);
    const auto maintenanceStartupProbe = evaluateRenderSystemRhiStartupProbe(
        summarizeRenderSystemRhiFrameLoopDryRunSmokeCapability(maintenanceSmoke));
    const auto maintenanceStartupGate = evaluateRenderSystemRhiStartupGate(
        summarizeRenderSystemRhiStartupProbeCapability(maintenanceStartupProbe));
    assert(maintenanceStartupGate.success);
    assert(!maintenanceStartupGate.allowStartup);
    assert(maintenanceStartupGate.action == RenderSystemRhiStartupGateAction::RunMaintenance);
    assert(maintenanceStartupGate.diagnostics.find("maintenance") != std::pmr::string::npos);
    const auto maintenanceSchedule =
        scheduleRenderSystemRhiStartupProbe(scheduleDesc, maintenanceStartupGate);
    assert(maintenanceSchedule.success);
    assert(!maintenanceSchedule.scheduled);
    assert(maintenanceSchedule.runMaintenance);
    assert(!maintenanceSchedule.waitForReadiness);
    assert(!maintenanceSchedule.diagnosticFailure);
    assert(maintenanceSchedule.diagnostics.find("maintenance") != std::pmr::string::npos);
    const auto maintenanceBatch = planRenderSystemRhiStartupProbeBatch(maintenanceSchedule);
    assert(maintenanceBatch.success);
    assert(!maintenanceBatch.canRunProbeBatch);
    assert(maintenanceBatch.runMaintenance);
    assert(!maintenanceBatch.waitForReadiness);
    assert(!maintenanceBatch.diagnosticFailure);
    const auto maintenanceRun = runRenderSystemRhiStartupProbeBatch(runnerDesc, maintenanceBatch);
    assert(maintenanceRun.success);
    assert(!maintenanceRun.executed);
    assert(!maintenanceRun.completed);
    assert(maintenanceRun.action == RenderSystemRhiStartupProbeRunAction::RunMaintenance);
    assert(maintenanceRun.runMaintenance);
    assert(!maintenanceRun.waitForReadiness);

    auto noPipelineState = makeReadyRenderSystemState();
    noPipelineState.renderPipelineReady = false;
    const auto noPipeline = hook.evaluate({
        frame,
        makeWebGpuCanvasLifecycleState(profile),
        noPipelineState,
    });
    assert(!noPipeline.success);
    assert(noPipeline.action == RenderSystemFrameLoopAction::DiagnosticFailure);
    assert(noPipeline.v1RenderPathUnaffected);
    assert(noPipeline.diagnostics.find("render pipeline") != std::pmr::string::npos);

    auto leakedFrame = frame;
    leakedFrame.nativeHandleExposedToUpperLayer = true;
    const auto leaked = hook.evaluate({
        leakedFrame,
        makeWebGpuCanvasLifecycleState(profile),
        makeReadyRenderSystemState(),
    });
    assert(!leaked.success);
    assert(leaked.action == RenderSystemFrameLoopAction::DiagnosticFailure);
    assert(leaked.nativeHandleExposedToUpperLayer);
    assert(leaked.diagnostics.find("native handle") != std::pmr::string::npos);
    const auto leakedSmoke = evaluateRenderSystemRhiFrameLoopDryRunSmoke(smokeDesc, leaked);
    const auto leakedStartupProbe = evaluateRenderSystemRhiStartupProbe(
        summarizeRenderSystemRhiFrameLoopDryRunSmokeCapability(leakedSmoke));
    const auto leakedStartupGate = evaluateRenderSystemRhiStartupGate(
        summarizeRenderSystemRhiStartupProbeCapability(leakedStartupProbe));
    assert(!leakedStartupGate.success);
    assert(!leakedStartupGate.allowStartup);
    assert(leakedStartupGate.action == RenderSystemRhiStartupGateAction::DiagnosticFailure);
    assert(leakedStartupGate.nativeHandleExposedToUpperLayer);
    assert(leakedStartupGate.diagnostics.find("native handle") != std::pmr::string::npos);
    const auto leakedSchedule =
        scheduleRenderSystemRhiStartupProbe(scheduleDesc, leakedStartupGate);
    assert(!leakedSchedule.success);
    assert(!leakedSchedule.scheduled);
    assert(leakedSchedule.diagnosticFailure);
    assert(leakedSchedule.nativeHandleExposedToUpperLayer);
    assert(leakedSchedule.diagnostics.find("native handle") != std::pmr::string::npos);
    const auto leakedBatch = planRenderSystemRhiStartupProbeBatch(leakedSchedule);
    assert(!leakedBatch.success);
    assert(!leakedBatch.canRunProbeBatch);
    assert(leakedBatch.diagnosticFailure);
    assert(leakedBatch.nativeHandleExposedToUpperLayer);
    const auto leakedRun = runRenderSystemRhiStartupProbeBatch(runnerDesc, leakedBatch);
    assert(!leakedRun.success);
    assert(!leakedRun.executed);
    assert(leakedRun.diagnosticFailure);
    assert(leakedRun.nativeHandleExposedToUpperLayer);

    RenderSystemRhiFrameLoopDryRunHook disabledHook(makeDryRunDesc(profile, false));
    const auto disabled = disabledHook.evaluate({
        frame,
        makeWebGpuCanvasLifecycleState(profile),
        makeReadyRenderSystemState(),
    });
    assert(!disabled.enabled);
    assert(disabled.success);
    assert(disabled.dryRunOnly);
    assert(disabled.v1RenderPathUnaffected);
    assert(disabled.action == RenderSystemFrameLoopAction::SkipFrame);
    assert(!disabled.acquireFrame);
    assert(!disabled.recordFrame);
    assert(disabled.diagnostics.find("disabled") != std::pmr::string::npos);
    const auto disabledSmoke = evaluateRenderSystemRhiFrameLoopDryRunSmoke(smokeDesc, disabled);
    assert(!disabledSmoke.success);
    assert(disabledSmoke.frameWouldSkip);
    assert(disabledSmoke.diagnostics.find("enabled") != std::pmr::string::npos);
    const auto disabledSmokeCaps =
        summarizeRenderSystemRhiFrameLoopDryRunSmokeCapability(disabledSmoke);
    assert(!disabledSmokeCaps.readyForStartupProbe);
    const auto disabledStartupProbe = evaluateRenderSystemRhiStartupProbe(disabledSmokeCaps);
    assert(!disabledStartupProbe.success);
    assert(!disabledStartupProbe.readyForStartup);
    assert(disabledStartupProbe.frameWouldSkip);
    assert(disabledStartupProbe.diagnostics.find("not ready") != std::pmr::string::npos);
    const auto disabledStartupProbeCaps =
        summarizeRenderSystemRhiStartupProbeCapability(disabledStartupProbe);
    assert(disabledStartupProbeCaps.available);
    assert(!disabledStartupProbeCaps.lastEvaluationSucceeded);
    assert(!disabledStartupProbeCaps.readyForStartup);
    assert(disabledStartupProbeCaps.frameWouldSkip);
    assert(disabledStartupProbeCaps.diagnostics.find("not ready") != std::pmr::string::npos);
    const auto disabledStartupGate = evaluateRenderSystemRhiStartupGate(disabledStartupProbeCaps);
    assert(disabledStartupGate.success);
    assert(!disabledStartupGate.allowStartup);
    assert(disabledStartupGate.action == RenderSystemRhiStartupGateAction::WaitForReadiness);
    assert(disabledStartupGate.frameWouldSkip);
    assert(disabledStartupGate.diagnostics.find("waiting") != std::pmr::string::npos);
    const auto disabledSchedule =
        scheduleRenderSystemRhiStartupProbe(scheduleDesc, disabledStartupGate);
    assert(disabledSchedule.success);
    assert(!disabledSchedule.scheduled);
    assert(disabledSchedule.waitForReadiness);
    assert(!disabledSchedule.runMaintenance);
    assert(!disabledSchedule.diagnosticFailure);
    assert(disabledSchedule.diagnostics.find("waiting") != std::pmr::string::npos);
    const auto disabledBatch = planRenderSystemRhiStartupProbeBatch(disabledSchedule);
    assert(disabledBatch.success);
    assert(!disabledBatch.canRunProbeBatch);
    assert(disabledBatch.waitForReadiness);
    assert(!disabledBatch.runMaintenance);
    assert(!disabledBatch.diagnosticFailure);
    const auto disabledRun = runRenderSystemRhiStartupProbeBatch(runnerDesc, disabledBatch);
    assert(disabledRun.success);
    assert(!disabledRun.executed);
    assert(!disabledRun.completed);
    assert(disabledRun.action == RenderSystemRhiStartupProbeRunAction::WaitForReadiness);
    assert(disabledRun.waitForReadiness);
    assert(!disabledRun.runMaintenance);

    auto zeroFrameScheduleDesc = scheduleDesc;
    zeroFrameScheduleDesc.probeFrameCount = 0;
    const auto invalidFrameCountSchedule =
        scheduleRenderSystemRhiStartupProbe(zeroFrameScheduleDesc, startupGate);
    assert(!invalidFrameCountSchedule.success);
    assert(!invalidFrameCountSchedule.scheduled);
    assert(invalidFrameCountSchedule.diagnosticFailure);
    assert(invalidFrameCountSchedule.diagnostics.find("frame count") != std::pmr::string::npos);
    const auto invalidFrameCountBatch =
        planRenderSystemRhiStartupProbeBatch(invalidFrameCountSchedule);
    assert(!invalidFrameCountBatch.success);
    assert(!invalidFrameCountBatch.canRunProbeBatch);
    assert(invalidFrameCountBatch.diagnosticFailure);
    const auto invalidFrameCountRun =
        runRenderSystemRhiStartupProbeBatch(runnerDesc, invalidFrameCountBatch);
    assert(!invalidFrameCountRun.success);
    assert(!invalidFrameCountRun.executed);
    assert(invalidFrameCountRun.diagnosticFailure);

    auto oversizedRunnerDesc = runnerDesc;
    oversizedRunnerDesc.maxFrameCount = 0;
    const auto oversizedRun =
        runRenderSystemRhiStartupProbeBatch(oversizedRunnerDesc, startupBatch);
    assert(!oversizedRun.success);
    assert(!oversizedRun.executed);
    assert(oversizedRun.diagnosticFailure);
    assert(oversizedRun.diagnostics.find("frame count") != std::pmr::string::npos);

    return 0;
}
