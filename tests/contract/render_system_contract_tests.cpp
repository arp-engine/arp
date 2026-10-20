#include "core/cpu_allocator.h"
#include "platform/render_system.h"
#include "platform/render_system_ordered_profile.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/native_platform_lifecycle_lowering.h"

#include <cassert>

namespace {

arp::platform::RenderSystemRhiFrameLoopDryRunDesc makeDisabledDryRunDesc()
{
    arp::platform::RenderSystemRhiFrameLoopDryRunDesc desc;
    desc.enabled = false;
    desc.runtime.rendererFrameLoop.logicalFrameSlots = 2;
    desc.runtime.renderSystem.logicalFrameSlots = 2;
    return desc;
}

arp::platform::RenderSystemRhiFrameLoopDryRunInput makeDryRunInput()
{
    arp::platform::RenderSystemRhiFrameLoopDryRunInput input;
    input.rendererFramePlan.success = false;
    input.rendererFramePlan.diagnostics =
        "renderer RHI frame plan unavailable in render system contract test";
    input.lifecycleState.logicalFrameSlots = 2;
    input.renderSystemState.renderPipelineReady = false;
    input.renderSystemState.nextFrameSerial = 1;
    return input;
}

arp::rhi::semantics::NativeBackendCapabilityContract makeQueriedVulkan()
{
    auto contract = arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Vulkan);
    contract.queriedFromNativeDevice = true;
    contract.queriedExtendedFeatures = true;
    return contract;
}

arp::platform::RenderSystemRhiFrameLoopDryRunDesc makeEnabledDryRunDesc()
{
    arp::platform::RenderSystemRhiFrameLoopDryRunDesc desc;
    desc.enabled = true;
    desc.runtime.rendererFrameLoop.lifecycle.planner.deployment.preference =
        arp::renderer::deployment::RendererBackendPreference::NativeValidatedFirst;
    desc.runtime.rendererFrameLoop.lifecycle.planner.deployment.candidates.maxCandidates = 1;
    desc.runtime.rendererFrameLoop.lifecycle.planner.deployment.candidates.nativeContracts
        .push_back(makeQueriedVulkan());
    desc.runtime.rendererFrameLoop.lifecycle.planner.frameStart.logicalFrameSlots = 2;
    desc.runtime.rendererFrameLoop.lifecycle.planner.frameStart.requireAcquireAllowed = true;
    desc.runtime.rendererFrameLoop.lifecycle.planner.frameStart.requirePresentAllowed = true;
    desc.runtime.rendererFrameLoop.lifecycle.maxTelemetryDiagnostics = 8;
    desc.runtime.rendererFrameLoop.logicalFrameSlots = 2;
    desc.runtime.renderSystem.logicalFrameSlots = 2;
    return desc;
}

arp::renderer::RendererRhiFramePlanBuildResult makeRendererFramePlan()
{
    arp::renderer::RendererRhiDrawPlanDesc pass;
    pass.name = "RenderSystemContractPass";
    pass.frameSlot = 1;
    pass.drawCommandCount = 1;
    pass.pushConstantSizeBytes = 64;
    pass.descriptorSetCount = 1;
    pass.descriptorLayoutNameCount = 1;
    pass.descriptorLayoutNames[0] = "RenderSystemContractSet";
    pass.allowSoftSecondaryCommands = true;

    arp::renderer::RendererRhiFramePlanBuilder builder({2, 1, 4, 1, 1});
    return builder.setFrameSlot(1).addDrawPlan(pass).build();
}

arp::platform::PlatformLifecycleState makeDesktopReadyState()
{
    arp::platform::PlatformLifecycleState state;
    state.surfaceKind = arp::platform::PlatformSurfaceKind::DesktopWindow;
    state.logicalFrameSlots = 2;
    state.running = true;
    state.surfaceValid = true;
    state.acquireAllowed = true;
    state.presentAllowed = true;
    return state;
}

} // namespace

int main()
{
    arp::platform::RenderSystemOrderedProfile orderedProfile;
    arp::platform::RenderSystemDesc orderedCreation;
    orderedCreation.orderedGraphics = &orderedProfile;
    auto orderedRejected = arp::platform::createRenderSystem(orderedCreation);
    assert(!orderedRejected.valid());
    assert(orderedRejected.diagnostics() == "PLATFORM-ORDERED-PROFILE-BACKEND-UNAVAILABLE");
    assert(!orderedRejected.orderedGraphicsCapabilities().orderedGraphics);
    assert(!orderedRejected.orderedGraphicsExecution());
    arp::platform::OrderedPipelineLoadDesc orderedPipeline;
    arp::core::pmr::String orderedDiagnostic;
    assert(!orderedRejected.createOrderedPipeline(orderedPipeline, orderedDiagnostic));
    assert(orderedDiagnostic == "PLATFORM-ORDERED-PROFILE-BACKEND-UNAVAILABLE");
    orderedCreation.backendPreference = static_cast<arp::platform::BackendPreference>(255);
    assert(arp::platform::createRenderSystem(orderedCreation).diagnostics()
        == "RHI-PLATFORM-BACKEND-PREFERENCE-UNKNOWN");
    // Invalid preferences must be rejected before any platform/default factory
    // or surface validation. They are never an implicit Default request.
    arp::platform::RenderSystemDesc unknownBackend;
    unknownBackend.backendPreference = static_cast<arp::platform::BackendPreference>(255);
    auto unknownRuntime = arp::platform::createRenderSystem(unknownBackend);
    assert(!unknownRuntime.valid());
    assert(unknownRuntime.diagnostics() == "RHI-PLATFORM-BACKEND-PREFERENCE-UNKNOWN");
    assert(unknownRuntime.gpuDevice() == nullptr && unknownRuntime.swapchain() == nullptr);
    arp::platform::RenderSystemDesc webGlBackend;
    webGlBackend.backendPreference = arp::platform::BackendPreference::WebGL;
    auto webGlRuntime = arp::platform::createRenderSystem(webGlBackend);
    assert(!webGlRuntime.valid() && !webGlRuntime.capabilities().canRender);
    assert(webGlRuntime.diagnostics() == "RHI-WEBGL1-RENDERSYSTEM-UNAVAILABLE");
    assert(webGlRuntime.gpuDevice() == nullptr && webGlRuntime.swapchain() == nullptr);

    arp::platform::PlatformSurfaceDesc surface;
    assert(surface.windowSystem == nullptr);
    assert(surface.window == nullptr);

    arp::platform::RenderSystemDesc desc;
    assert(desc.backendPreference == arp::platform::BackendPreference::Default);
    assert(desc.surface.windowSystem == nullptr);
    assert(desc.surface.window == nullptr);
    assert(desc.applicationName == "AnimeRenderPipeline");

    auto runtime = arp::platform::createRenderSystem(desc);
    assert(!runtime.valid());
    assert(runtime.backend() == arp::rhi::Backend::Null);
    assert(!runtime.capabilities().canRender);
    assert(runtime.capabilities().backend == arp::rhi::Backend::Null);
    assert(runtime.capabilities().rhiFrameLoopDryRun.available);
    assert(!runtime.capabilities().rhiFrameLoopDryRun.enabled);
    assert(!runtime.capabilities().rhiFrameLoopDryRun.lastEvaluationSucceeded);
    assert(runtime.capabilities().rhiFrameLoopDryRun.dryRunOnly);
    assert(runtime.capabilities().rhiFrameLoopDryRun.v1RenderPathUnaffected);
    assert(runtime.capabilities().rhiFrameLoopDryRunSmoke.available);
    assert(!runtime.capabilities().rhiFrameLoopDryRunSmoke.lastEvaluationSucceeded);
    assert(runtime.capabilities().rhiFrameLoopDryRunSmoke.dryRunOnly);
    assert(runtime.capabilities().rhiFrameLoopDryRunSmoke.v1RenderPathUnaffected);
    assert(runtime.capabilities().rhiStartupProbe.available);
    assert(!runtime.capabilities().rhiStartupProbe.lastEvaluationSucceeded);
    assert(!runtime.capabilities().rhiStartupProbe.readyForStartup);
    assert(runtime.capabilities().rhiStartupProbe.dryRunOnly);
    assert(runtime.capabilities().rhiStartupProbe.v1RenderPathUnaffected);
    assert(runtime.capabilities().rhiStartupGate.success);
    assert(!runtime.capabilities().rhiStartupGate.allowStartup);
    assert(runtime.capabilities().rhiStartupGate.action
        == arp::platform::RenderSystemRhiStartupGateAction::WaitForReadiness);
    assert(runtime.capabilities().rhiStartupGate.dryRunOnly);
    assert(runtime.capabilities().rhiStartupGate.v1RenderPathUnaffected);
    assert(!runtime.resize(arp::rhi::Extent2D{64, 64}).success);

    arp::platform::RenderSystemDesc webDesc;
    webDesc.backendPreference = arp::platform::BackendPreference::WebGPU;
    auto webRuntime = arp::platform::createRenderSystem(webDesc);
    assert(!webRuntime.valid());
    assert(webRuntime.backend() == arp::rhi::Backend::Null);
    assert(webRuntime.diagnostics().find("WebGPU") != std::pmr::string::npos);
    assert(webRuntime.capabilities().diagnostics == webRuntime.diagnostics());
    assert(!webRuntime.resize(arp::rhi::Extent2D{64, 64}).success);

    const auto invalidAcquire = runtime.acquireFrame();
    assert(!invalidAcquire.success);
    assert(invalidAcquire.diagnostics == "render system is not valid");
    const auto invalidPresent =
        runtime.presentFrame(arp::rhi::AcquiredFrame{false, "not acquired", 0, 0});
    assert(!invalidPresent.success);
    assert(invalidPresent.diagnostics == "present failure: render system is not valid");

    const auto initialDryRun = runtime.lastRhiFrameLoopDryRunReport();
    assert(!initialDryRun.enabled);
    assert(initialDryRun.dryRunOnly);
    assert(initialDryRun.v1RenderPathUnaffected);
    const auto initialSmoke = runtime.lastRhiFrameLoopDryRunSmokeReport();
    assert(!initialSmoke.success);
    assert(initialSmoke.dryRunOnly);
    assert(initialSmoke.v1RenderPathUnaffected);
    const auto initialStartupProbe = runtime.lastRhiStartupProbeReport();
    assert(!initialStartupProbe.success);
    assert(!initialStartupProbe.readyForStartup);
    assert(initialStartupProbe.dryRunOnly);
    assert(initialStartupProbe.v1RenderPathUnaffected);
    const auto initialStartupGate = runtime.lastRhiStartupGateReport();
    assert(!initialStartupGate.success);
    assert(!initialStartupGate.allowStartup);
    assert(initialStartupGate.dryRunOnly);
    assert(initialStartupGate.v1RenderPathUnaffected);
    const auto initialStartupSchedule = runtime.lastRhiStartupProbeScheduleReport();
    assert(!initialStartupSchedule.success);
    assert(!initialStartupSchedule.scheduled);
    assert(initialStartupSchedule.dryRunOnly);
    assert(initialStartupSchedule.v1RenderPathUnaffected);
    const auto initialStartupBatch = runtime.lastRhiStartupProbeBatchReport();
    assert(!initialStartupBatch.success);
    assert(!initialStartupBatch.canRunProbeBatch);
    assert(initialStartupBatch.dryRunOnly);
    assert(initialStartupBatch.v1RenderPathUnaffected);
    const auto initialStartupRun = runtime.lastRhiStartupProbeRunReport();
    assert(!initialStartupRun.success);
    assert(!initialStartupRun.executed);
    assert(initialStartupRun.diagnosticsOnly);
    assert(initialStartupRun.v1RenderPathUnaffected);
    const auto initialStartupTelemetry = runtime.lastRhiStartupTelemetryReport();
    assert(!initialStartupTelemetry.success);
    assert(!initialStartupTelemetry.probeExecuted);
    assert(initialStartupTelemetry.diagnosticsOnly);
    assert(initialStartupTelemetry.v1RenderPathUnaffected);
    const auto initialStartupReadiness = runtime.lastRhiStartupReadinessReport();
    assert(!initialStartupReadiness.canBeginFormalRhiRefactor);
    assert(initialStartupReadiness.status
        == arp::platform::RenderSystemRhiStartupReadinessStatus::DiagnosticFailure);
    const auto initialFormalGate = runtime.lastRhiFormalRefactorStartGateReport();
    assert(!initialFormalGate.success);
    assert(!initialFormalGate.allowFormalRefactorStart);

    const auto configuredDryRun = runtime.configureRhiFrameLoopDryRun(makeDisabledDryRunDesc());
    assert(configuredDryRun.success);
    assert(runtime.capabilities().rhiFrameLoopDryRun.available);
    assert(!runtime.capabilities().rhiFrameLoopDryRun.enabled);
    const auto dryRun = runtime.evaluateRhiFrameLoopDryRun(makeDryRunInput());
    assert(dryRun.success);
    assert(!dryRun.enabled);
    assert(dryRun.dryRunOnly);
    assert(dryRun.v1RenderPathUnaffected);
    assert(dryRun.action == arp::platform::RenderSystemFrameLoopAction::SkipFrame);
    assert(dryRun.diagnostics.find("disabled") != std::pmr::string::npos);

    const auto lastDryRun = runtime.lastRhiFrameLoopDryRunReport();
    assert(lastDryRun.success == dryRun.success);
    assert(lastDryRun.enabled == dryRun.enabled);
    assert(lastDryRun.diagnostics == dryRun.diagnostics);
    const auto dryRunCaps = runtime.capabilities().rhiFrameLoopDryRun;
    assert(dryRunCaps.available);
    assert(!dryRunCaps.enabled);
    assert(dryRunCaps.lastEvaluationSucceeded);
    assert(dryRunCaps.dryRunOnly);
    assert(dryRunCaps.v1RenderPathUnaffected);
    assert(dryRunCaps.action == dryRun.action);
    assert(dryRunCaps.selectedBackend == dryRun.selectedBackend);
    assert(!dryRunCaps.nativeHandleExposedToUpperLayer);
    assert(dryRunCaps.diagnostics == dryRun.diagnostics);

    const auto configuredEnabledDryRun =
        runtime.configureRhiFrameLoopDryRun(makeEnabledDryRunDesc());
    assert(configuredEnabledDryRun.success);
    const auto currentStateDryRun = runtime.evaluateRhiFrameLoopDryRunForCurrentState(
        makeRendererFramePlan(), makeDesktopReadyState());
    assert(!currentStateDryRun.success);
    assert(currentStateDryRun.enabled);
    assert(currentStateDryRun.frameSerial == 1u);
    assert(
        currentStateDryRun.action == arp::platform::RenderSystemFrameLoopAction::DiagnosticFailure);
    assert(!currentStateDryRun.diagnostics.empty());
    const auto currentStateCaps = runtime.capabilities().rhiFrameLoopDryRun;
    assert(currentStateCaps.enabled);
    assert(!currentStateCaps.lastEvaluationSucceeded);
    assert(currentStateCaps.diagnostics == currentStateDryRun.diagnostics);

    arp::platform::RenderSystemRhiFrameLoopDryRunSmokeDesc smokeDesc;
    smokeDesc.requireBeginFrame = true;
    const auto currentStateSmoke = runtime.evaluateRhiFrameLoopDryRunSmokeForCurrentState(
        smokeDesc, makeRendererFramePlan(), makeDesktopReadyState());
    assert(!currentStateSmoke.success);
    assert(currentStateSmoke.dryRunOnly);
    assert(currentStateSmoke.v1RenderPathUnaffected);
    assert(currentStateSmoke.diagnosticFailure);
    assert(!currentStateSmoke.frameWouldBegin);
    assert(!currentStateSmoke.diagnostics.empty());
    assert(runtime.lastRhiFrameLoopDryRunReport().diagnostics
        == currentStateSmoke.dryRunReport.diagnostics);
    const auto lastSmoke = runtime.lastRhiFrameLoopDryRunSmokeReport();
    assert(lastSmoke.success == currentStateSmoke.success);
    assert(lastSmoke.diagnosticFailure == currentStateSmoke.diagnosticFailure);
    assert(lastSmoke.dryRunReport.diagnostics == currentStateSmoke.dryRunReport.diagnostics);
    assert(lastSmoke.diagnostics == currentStateSmoke.diagnostics);
    const auto smokeCaps = runtime.capabilities().rhiFrameLoopDryRunSmoke;
    assert(smokeCaps.available);
    assert(!smokeCaps.lastEvaluationSucceeded);
    assert(smokeCaps.dryRunOnly);
    assert(smokeCaps.v1RenderPathUnaffected);
    assert(!smokeCaps.readyForStartupProbe);
    assert(smokeCaps.diagnosticFailure);
    assert(!smokeCaps.frameWouldBegin);
    assert(smokeCaps.selectedBackend == currentStateSmoke.dryRunReport.selectedBackend);
    assert(smokeCaps.frameSlot == currentStateSmoke.dryRunReport.frameSlot);
    assert(smokeCaps.frameSlot == 1u);
    assert(smokeCaps.frameSerial == currentStateSmoke.dryRunReport.frameSerial);
    assert(smokeCaps.frameSerial == 1u);
    assert(smokeCaps.acquireFrame == currentStateSmoke.acquireFrame);
    assert(smokeCaps.recordFrame == currentStateSmoke.recordFrame);
    assert(smokeCaps.submitFrame == currentStateSmoke.submitFrame);
    assert(smokeCaps.presentFrame == currentStateSmoke.presentFrame);
    assert(!smokeCaps.nativeHandleExposedToUpperLayer);
    assert(smokeCaps.diagnostics == currentStateSmoke.diagnostics);
    const auto startupProbe = runtime.evaluateRhiStartupProbeForCurrentState(
        smokeDesc, makeRendererFramePlan(), makeDesktopReadyState());
    assert(!startupProbe.success);
    assert(!startupProbe.readyForStartup);
    assert(startupProbe.dryRunOnly);
    assert(startupProbe.v1RenderPathUnaffected);
    assert(startupProbe.diagnosticFailure);
    assert(startupProbe.frameSlot == 1u);
    assert(startupProbe.frameSerial == 1u);
    assert(!startupProbe.diagnostics.empty());
    assert(startupProbe.diagnostics.find(runtime.lastRhiFrameLoopDryRunSmokeReport().diagnostics)
        != std::pmr::string::npos);
    const auto lastStartupProbe = runtime.lastRhiStartupProbeReport();
    assert(lastStartupProbe.success == startupProbe.success);
    assert(lastStartupProbe.readyForStartup == startupProbe.readyForStartup);
    assert(lastStartupProbe.selectedBackend == startupProbe.selectedBackend);
    assert(lastStartupProbe.frameSlot == startupProbe.frameSlot);
    assert(lastStartupProbe.frameSerial == startupProbe.frameSerial);
    assert(lastStartupProbe.diagnostics == startupProbe.diagnostics);
    const auto startupCaps = runtime.capabilities().rhiStartupProbe;
    assert(startupCaps.available);
    assert(startupCaps.lastEvaluationSucceeded == startupProbe.success);
    assert(startupCaps.readyForStartup == startupProbe.readyForStartup);
    assert(startupCaps.dryRunOnly == startupProbe.dryRunOnly);
    assert(startupCaps.v1RenderPathUnaffected == startupProbe.v1RenderPathUnaffected);
    assert(startupCaps.diagnosticFailure == startupProbe.diagnosticFailure);
    assert(startupCaps.selectedBackend == startupProbe.selectedBackend);
    assert(startupCaps.frameSlot == startupProbe.frameSlot);
    assert(startupCaps.frameSerial == startupProbe.frameSerial);
    assert(startupCaps.acquireFrame == startupProbe.acquireFrame);
    assert(startupCaps.recordFrame == startupProbe.recordFrame);
    assert(startupCaps.submitFrame == startupProbe.submitFrame);
    assert(startupCaps.presentFrame == startupProbe.presentFrame);
    assert(startupCaps.telemetryRequired == startupProbe.telemetryRequired);
    assert(startupCaps.backendFallbacksHiddenFromUpperLayer
        == startupProbe.backendFallbacksHiddenFromUpperLayer);
    assert(startupCaps.nativeHandleExposedToUpperLayer
        == startupProbe.nativeHandleExposedToUpperLayer);
    assert(startupCaps.diagnostics == startupProbe.diagnostics);
    const auto startupGate = runtime.capabilities().rhiStartupGate;
    assert(!startupGate.success);
    assert(!startupGate.allowStartup);
    assert(
        startupGate.action == arp::platform::RenderSystemRhiStartupGateAction::DiagnosticFailure);
    assert(startupGate.diagnosticFailure == startupProbe.diagnosticFailure);
    assert(startupGate.selectedBackend == startupProbe.selectedBackend);
    assert(startupGate.frameSlot == startupProbe.frameSlot);
    assert(startupGate.frameSerial == startupProbe.frameSerial);
    assert(startupGate.nativeHandleExposedToUpperLayer
        == startupProbe.nativeHandleExposedToUpperLayer);
    assert(startupGate.diagnostics == startupProbe.diagnostics);
    const auto currentStateStartupGate = runtime.evaluateRhiStartupGateForCurrentState(
        smokeDesc, makeRendererFramePlan(), makeDesktopReadyState());
    assert(!currentStateStartupGate.success);
    assert(!currentStateStartupGate.allowStartup);
    assert(currentStateStartupGate.action
        == arp::platform::RenderSystemRhiStartupGateAction::DiagnosticFailure);
    assert(currentStateStartupGate.diagnosticFailure);
    assert(currentStateStartupGate.frameSlot == 1u);
    assert(currentStateStartupGate.frameSerial == 1u);
    assert(!currentStateStartupGate.diagnostics.empty());
    const auto lastStartupGate = runtime.lastRhiStartupGateReport();
    assert(lastStartupGate.success == currentStateStartupGate.success);
    assert(lastStartupGate.allowStartup == currentStateStartupGate.allowStartup);
    assert(lastStartupGate.action == currentStateStartupGate.action);
    assert(lastStartupGate.selectedBackend == currentStateStartupGate.selectedBackend);
    assert(lastStartupGate.frameSlot == currentStateStartupGate.frameSlot);
    assert(lastStartupGate.frameSerial == currentStateStartupGate.frameSerial);
    assert(lastStartupGate.diagnostics == currentStateStartupGate.diagnostics);
    const auto updatedStartupGateCaps = runtime.capabilities().rhiStartupGate;
    assert(updatedStartupGateCaps.action == currentStateStartupGate.action);
    assert(updatedStartupGateCaps.selectedBackend == currentStateStartupGate.selectedBackend);
    assert(updatedStartupGateCaps.frameSlot == currentStateStartupGate.frameSlot);
    assert(updatedStartupGateCaps.frameSerial == currentStateStartupGate.frameSerial);
    assert(updatedStartupGateCaps.diagnostics == currentStateStartupGate.diagnostics);
    arp::platform::RenderSystemRhiStartupProbeScheduleDesc startupScheduleDesc;
    startupScheduleDesc.requireHiddenWindow = true;
    startupScheduleDesc.probeFrameCount = 1;
    const auto currentStateStartupSchedule = runtime.scheduleRhiStartupProbeForCurrentState(
        startupScheduleDesc, smokeDesc, makeRendererFramePlan(), makeDesktopReadyState());
    assert(!currentStateStartupSchedule.success);
    assert(!currentStateStartupSchedule.scheduled);
    assert(currentStateStartupSchedule.diagnosticFailure);
    assert(currentStateStartupSchedule.frameSlot == 1u);
    assert(currentStateStartupSchedule.frameSerial == 1u);
    assert(currentStateStartupSchedule.logicalFrameSlots == 2u);
    assert(currentStateStartupSchedule.hiddenWindowRequired);
    assert(currentStateStartupSchedule.probeFrameCount == 1u);
    assert(!currentStateStartupSchedule.diagnostics.empty());
    const auto lastStartupSchedule = runtime.lastRhiStartupProbeScheduleReport();
    assert(lastStartupSchedule.success == currentStateStartupSchedule.success);
    assert(lastStartupSchedule.scheduled == currentStateStartupSchedule.scheduled);
    assert(lastStartupSchedule.selectedBackend == currentStateStartupSchedule.selectedBackend);
    assert(lastStartupSchedule.frameSlot == currentStateStartupSchedule.frameSlot);
    assert(lastStartupSchedule.frameSerial == currentStateStartupSchedule.frameSerial);
    assert(lastStartupSchedule.diagnostics == currentStateStartupSchedule.diagnostics);
    const auto updatedStartupScheduleCaps = runtime.capabilities().rhiStartupProbeSchedule;
    assert(updatedStartupScheduleCaps.success == currentStateStartupSchedule.success);
    assert(updatedStartupScheduleCaps.scheduled == currentStateStartupSchedule.scheduled);
    assert(updatedStartupScheduleCaps.diagnosticFailure
        == currentStateStartupSchedule.diagnosticFailure);
    assert(
        updatedStartupScheduleCaps.selectedBackend == currentStateStartupSchedule.selectedBackend);
    assert(updatedStartupScheduleCaps.frameSlot == currentStateStartupSchedule.frameSlot);
    assert(updatedStartupScheduleCaps.frameSerial == currentStateStartupSchedule.frameSerial);
    assert(updatedStartupScheduleCaps.diagnostics == currentStateStartupSchedule.diagnostics);
    const auto currentStateStartupBatch = runtime.planRhiStartupProbeBatchForCurrentState(
        startupScheduleDesc, smokeDesc, makeRendererFramePlan(), makeDesktopReadyState());
    assert(!currentStateStartupBatch.success);
    assert(!currentStateStartupBatch.canRunProbeBatch);
    assert(currentStateStartupBatch.diagnosticFailure);
    assert(currentStateStartupBatch.logicalFrameSlots == 2u);
    assert(currentStateStartupBatch.frameCount == 1u);
    assert(currentStateStartupBatch.firstFrameSerial == 1u);
    assert(currentStateStartupBatch.lastFrameSerial == 1u);
    assert(currentStateStartupBatch.hiddenWindowRequired);
    assert(currentStateStartupBatch.dryRunOnly);
    assert(currentStateStartupBatch.v1RenderPathUnaffected);
    assert(currentStateStartupBatch.nonBlocking);
    assert(!currentStateStartupBatch.diagnostics.empty());
    const auto lastStartupBatch = runtime.lastRhiStartupProbeBatchReport();
    assert(lastStartupBatch.success == currentStateStartupBatch.success);
    assert(lastStartupBatch.canRunProbeBatch == currentStateStartupBatch.canRunProbeBatch);
    assert(lastStartupBatch.selectedBackend == currentStateStartupBatch.selectedBackend);
    assert(lastStartupBatch.firstFrameSerial == currentStateStartupBatch.firstFrameSerial);
    assert(lastStartupBatch.lastFrameSerial == currentStateStartupBatch.lastFrameSerial);
    assert(lastStartupBatch.diagnostics == currentStateStartupBatch.diagnostics);
    const auto updatedStartupBatchCaps = runtime.capabilities().rhiStartupProbeBatch;
    assert(updatedStartupBatchCaps.success == currentStateStartupBatch.success);
    assert(updatedStartupBatchCaps.canRunProbeBatch == currentStateStartupBatch.canRunProbeBatch);
    assert(updatedStartupBatchCaps.diagnosticFailure == currentStateStartupBatch.diagnosticFailure);
    assert(updatedStartupBatchCaps.selectedBackend == currentStateStartupBatch.selectedBackend);
    assert(updatedStartupBatchCaps.firstFrameSerial == currentStateStartupBatch.firstFrameSerial);
    assert(updatedStartupBatchCaps.lastFrameSerial == currentStateStartupBatch.lastFrameSerial);
    assert(updatedStartupBatchCaps.diagnostics == currentStateStartupBatch.diagnostics);
    arp::platform::RenderSystemRhiStartupProbeRunnerDesc startupRunnerDesc;
    startupRunnerDesc.hiddenWindowAvailable = true;
    startupRunnerDesc.maxFrameCount = 2;
    const auto currentStateStartupRun = runtime.runRhiStartupProbeForCurrentState(startupRunnerDesc,
        startupScheduleDesc,
        smokeDesc,
        makeRendererFramePlan(),
        makeDesktopReadyState());
    assert(!currentStateStartupRun.success);
    assert(!currentStateStartupRun.executed);
    assert(!currentStateStartupRun.completed);
    assert(currentStateStartupRun.diagnosticFailure);
    assert(currentStateStartupRun.frameCount == 1u);
    assert(currentStateStartupRun.firstFrameSerial == 1u);
    assert(currentStateStartupRun.lastFrameSerial == 1u);
    assert(currentStateStartupRun.hiddenWindowRequired);
    assert(!currentStateStartupRun.hiddenWindowUsed);
    assert(currentStateStartupRun.diagnosticsOnly);
    assert(currentStateStartupRun.v1RenderPathUnaffected);
    assert(currentStateStartupRun.nonBlocking);
    assert(!currentStateStartupRun.diagnostics.empty());
    const auto lastStartupRun = runtime.lastRhiStartupProbeRunReport();
    assert(lastStartupRun.success == currentStateStartupRun.success);
    assert(lastStartupRun.executed == currentStateStartupRun.executed);
    assert(lastStartupRun.completed == currentStateStartupRun.completed);
    assert(lastStartupRun.selectedBackend == currentStateStartupRun.selectedBackend);
    assert(lastStartupRun.firstFrameSerial == currentStateStartupRun.firstFrameSerial);
    assert(lastStartupRun.lastFrameSerial == currentStateStartupRun.lastFrameSerial);
    assert(lastStartupRun.diagnostics == currentStateStartupRun.diagnostics);
    const auto updatedStartupRunCaps = runtime.capabilities().rhiStartupProbeRun;
    assert(updatedStartupRunCaps.success == currentStateStartupRun.success);
    assert(updatedStartupRunCaps.executed == currentStateStartupRun.executed);
    assert(updatedStartupRunCaps.completed == currentStateStartupRun.completed);
    assert(updatedStartupRunCaps.diagnosticFailure == currentStateStartupRun.diagnosticFailure);
    assert(updatedStartupRunCaps.selectedBackend == currentStateStartupRun.selectedBackend);
    assert(updatedStartupRunCaps.firstFrameSerial == currentStateStartupRun.firstFrameSerial);
    assert(updatedStartupRunCaps.lastFrameSerial == currentStateStartupRun.lastFrameSerial);
    assert(updatedStartupRunCaps.diagnostics == currentStateStartupRun.diagnostics);
    const auto currentStateStartupTelemetry =
        runtime.evaluateRhiStartupTelemetryForCurrentState(startupRunnerDesc,
            startupScheduleDesc,
            smokeDesc,
            makeRendererFramePlan(),
            makeDesktopReadyState());
    assert(!currentStateStartupTelemetry.success);
    assert(!currentStateStartupTelemetry.probeExecuted);
    assert(currentStateStartupTelemetry.diagnosticFailure);
    assert(currentStateStartupTelemetry.firstFrameSerial == 1u);
    assert(currentStateStartupTelemetry.lastFrameSerial == 1u);
    assert(!currentStateStartupTelemetry.diagnostics.empty());
    const auto lastStartupTelemetry = runtime.lastRhiStartupTelemetryReport();
    assert(lastStartupTelemetry.success == currentStateStartupTelemetry.success);
    assert(lastStartupTelemetry.probeExecuted == currentStateStartupTelemetry.probeExecuted);
    assert(lastStartupTelemetry.selectedBackend == currentStateStartupTelemetry.selectedBackend);
    assert(lastStartupTelemetry.diagnostics == currentStateStartupTelemetry.diagnostics);
    const auto currentStateStartupReadiness =
        runtime.evaluateRhiStartupReadinessForCurrentState(startupRunnerDesc,
            startupScheduleDesc,
            smokeDesc,
            makeRendererFramePlan(),
            makeDesktopReadyState());
    assert(currentStateStartupReadiness.status
        == arp::platform::RenderSystemRhiStartupReadinessStatus::DiagnosticFailure);
    assert(!currentStateStartupReadiness.canBeginFormalRhiRefactor);
    assert(currentStateStartupReadiness.diagnosticFailure);
    assert(!currentStateStartupReadiness.diagnostics.empty());
    const auto lastStartupReadiness = runtime.lastRhiStartupReadinessReport();
    assert(lastStartupReadiness.status == currentStateStartupReadiness.status);
    assert(lastStartupReadiness.canBeginFormalRhiRefactor
        == currentStateStartupReadiness.canBeginFormalRhiRefactor);
    assert(lastStartupReadiness.diagnostics == currentStateStartupReadiness.diagnostics);
    const auto currentStateFormalGate =
        runtime.evaluateRhiFormalRefactorStartGateForCurrentState(startupRunnerDesc,
            startupScheduleDesc,
            smokeDesc,
            makeRendererFramePlan(),
            makeDesktopReadyState());
    assert(!currentStateFormalGate.success);
    assert(!currentStateFormalGate.allowFormalRefactorStart);
    assert(!currentStateFormalGate.allowRendererMigration);
    assert(!currentStateFormalGate.allowFrameLoopMigration);
    assert(currentStateFormalGate.status
        == arp::platform::RenderSystemRhiStartupReadinessStatus::DiagnosticFailure);
    assert(!currentStateFormalGate.diagnostics.empty());
    const auto lastFormalGate = runtime.lastRhiFormalRefactorStartGateReport();
    assert(lastFormalGate.success == currentStateFormalGate.success);
    assert(
        lastFormalGate.allowFormalRefactorStart == currentStateFormalGate.allowFormalRefactorStart);
    assert(lastFormalGate.diagnostics == currentStateFormalGate.diagnostics);
    const auto updatedReadinessCaps = runtime.capabilities().rhiStartupReadiness;
    assert(updatedReadinessCaps.status == currentStateStartupReadiness.status);
    assert(updatedReadinessCaps.canBeginFormalRhiRefactor
        == currentStateStartupReadiness.canBeginFormalRhiRefactor);
    const auto updatedFormalGateCaps = runtime.capabilities().rhiFormalRefactorStartGate;
    assert(updatedFormalGateCaps.success == currentStateFormalGate.success);
    assert(updatedFormalGateCaps.allowFormalRefactorStart
        == currentStateFormalGate.allowFormalRefactorStart);

    assert(!runtime.valid());
    assert(runtime.backend() == arp::rhi::Backend::Null);
    return 0;
}
