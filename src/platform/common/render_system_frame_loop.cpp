#include "platform/render_system_frame_loop.h"

#include "core/diagnostics/diagnostic_builder.h"
#include "core/memory/cpu_allocator.h"

namespace arp::platform {

namespace {

bool hasMaintenanceWork(const RendererFrameLoopDecision& decision)
{
    return decision.swapchainRebuildRequired || decision.releaseSurfaceResources
        || decision.releaseTransientResources || decision.memoryBudgetRefreshRequired;
}

void disableFrameWork(RendererFrameLoopDecision& decision)
{
    decision.canStartFrame = false;
    decision.shouldAcquireFrame = false;
    decision.shouldRecordFrame = false;
    decision.shouldSubmitFrame = false;
    decision.shouldPresentFrame = false;
}

void enableFrameWork(RendererFrameLoopDecision& decision)
{
    decision.canStartFrame = true;
    decision.shouldAcquireFrame = true;
    decision.shouldRecordFrame = true;
    decision.shouldSubmitFrame = true;
    decision.shouldPresentFrame = true;
}

} // namespace

RendererFrameLoopBridge::RendererFrameLoopBridge(const RendererFrameLoopBridgeDesc& desc)
    : desc_(desc)
{}

RendererFrameLoopDecision RendererFrameLoopBridge::evaluate(
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState) const
{
    return evaluateRendererFrameLoopBridge(desc_, rendererFramePlan, lifecycleState);
}

RendererFrameLoopDecision evaluateRendererFrameLoopBridge(const RendererFrameLoopBridgeDesc& desc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    RendererFrameLoopDecision decision;
    decision.lifecycle = renderer::deployment::evaluateRendererFrameLifecycleBridge(
        desc.lifecycle, rendererFramePlan, lifecycleState);
    decision.selectedBackend = decision.lifecycle.selectedBackend;
    decision.frameSlot = decision.lifecycle.telemetry.frameSlot;
    decision.swapchainRebuildRequired = decision.lifecycle.telemetry.swapchainRebuildRequired;
    decision.releaseSurfaceResources = decision.lifecycle.telemetry.releaseSurfaceResources;
    decision.releaseTransientResources = decision.lifecycle.telemetry.releaseTransientResources;
    decision.memoryBudgetRefreshRequired = decision.lifecycle.telemetry.memoryBudgetRefreshRequired;
    decision.telemetryRequired = decision.lifecycle.telemetryRequired;
    decision.backendFallbacksHiddenFromUpperLayer =
        decision.lifecycle.backendFallbacksHiddenFromUpperLayer;
    decision.nativeHandleExposedToUpperLayer = decision.lifecycle.nativeHandleExposedToUpperLayer;
    decision.diagnostics = decision.lifecycle.diagnostics;

    if (desc.logicalFrameSlots != 2) {
        arp::core::appendDiagnostic(
            decision.diagnostics, "renderer frame loop requires two logical frame slots");
        decision.telemetryRequired = true;
        decision.action = RendererFrameLoopAction::DiagnosticFailure;
        disableFrameWork(decision);
        return decision;
    }

    if (decision.lifecycle.status
            == renderer::deployment::RendererFrameLifecycleReadinessStatus::DiagnosticFailure
        || decision.nativeHandleExposedToUpperLayer
        || !decision.backendFallbacksHiddenFromUpperLayer
        || decision.frameSlot >= desc.logicalFrameSlots) {
        decision.action = RendererFrameLoopAction::DiagnosticFailure;
        decision.success = false;
        decision.telemetryRequired = true;
        disableFrameWork(decision);
        return decision;
    }

    if (hasMaintenanceWork(decision)) {
        decision.action = RendererFrameLoopAction::MaintenanceRequired;
        decision.success = true;
        decision.telemetryRequired = true;
        disableFrameWork(decision);
        arp::core::appendDiagnostic(
            decision.diagnostics, "renderer frame loop requires maintenance before frame start");
        return decision;
    }

    if (decision.lifecycle.status
        == renderer::deployment::RendererFrameLifecycleReadinessStatus::FrameStartBlocked) {
        decision.action = RendererFrameLoopAction::SkipFrame;
        decision.success = true;
        decision.telemetryRequired = true;
        disableFrameWork(decision);
        return decision;
    }

    decision.action = RendererFrameLoopAction::BeginFrame;
    decision.success = true;
    enableFrameWork(decision);
    return decision;
}

namespace {

void disableFrameWork(RenderSystemFrameLoopPlan& plan)
{
    plan.acquireFrame = false;
    plan.recordFrame = false;
    plan.submitFrame = false;
    plan.presentFrame = false;
}

RenderSystemFrameLoopAction mapAction(RendererFrameLoopAction action)
{
    switch (action) {
    case RendererFrameLoopAction::BeginFrame:
        return RenderSystemFrameLoopAction::BeginFrame;
    case RendererFrameLoopAction::SkipFrame:
        return RenderSystemFrameLoopAction::SkipFrame;
    case RendererFrameLoopAction::MaintenanceRequired:
        return RenderSystemFrameLoopAction::MaintenanceRequired;
    case RendererFrameLoopAction::DiagnosticFailure:
        return RenderSystemFrameLoopAction::DiagnosticFailure;
    }
    return RenderSystemFrameLoopAction::DiagnosticFailure;
}

void copyFrameLoopFacts(RenderSystemFrameLoopPlan& plan, const RendererFrameLoopDecision& frameLoop)
{
    plan.action = mapAction(frameLoop.action);
    plan.selectedBackend = frameLoop.selectedBackend;
    plan.frameSlot = frameLoop.frameSlot;
    plan.swapchainRebuildRequired = frameLoop.swapchainRebuildRequired;
    plan.releaseSurfaceResources = frameLoop.releaseSurfaceResources;
    plan.releaseTransientResources = frameLoop.releaseTransientResources;
    plan.memoryBudgetRefreshRequired = frameLoop.memoryBudgetRefreshRequired;
    plan.telemetryRequired = frameLoop.telemetryRequired;
    plan.backendFallbacksHiddenFromUpperLayer = frameLoop.backendFallbacksHiddenFromUpperLayer;
    plan.nativeHandleExposedToUpperLayer = frameLoop.nativeHandleExposedToUpperLayer;
    plan.diagnostics = frameLoop.diagnostics;
}

} // namespace

RenderSystemFrameLoopAdapter::RenderSystemFrameLoopAdapter(
    const RenderSystemFrameLoopAdapterDesc& desc)
    : desc_(desc)
{}

RenderSystemFrameLoopPlan RenderSystemFrameLoopAdapter::plan(
    const RendererFrameLoopDecision& frameLoop, const RenderSystemFrameLoopState& state) const
{
    return planRenderSystemFrameLoop(desc_, frameLoop, state);
}

RenderSystemFrameLoopPlan planRenderSystemFrameLoop(const RenderSystemFrameLoopAdapterDesc& desc,
    const RendererFrameLoopDecision& frameLoop,
    const RenderSystemFrameLoopState& state)
{
    RenderSystemFrameLoopPlan plan;
    copyFrameLoopFacts(plan, frameLoop);
    plan.frameSerial = state.nextFrameSerial;
    plan.nativeHandleExposedToUpperLayer =
        plan.nativeHandleExposedToUpperLayer || state.nativeHandleExposedToUpperLayer;

    if (desc.logicalFrameSlots != 2) {
        arp::core::appendDiagnostic(
            plan.diagnostics, "render system frame loop requires two logical frame slots");
    }
    if (plan.frameSlot >= desc.logicalFrameSlots) {
        arp::core::appendDiagnostic(plan.diagnostics,
            "render system frame loop rejected frame slot outside double-buffer range");
    }
    if (!plan.backendFallbacksHiddenFromUpperLayer) {
        arp::core::appendDiagnostic(
            plan.diagnostics, "render system frame loop rejected visible backend fallbacks");
    }
    if (plan.nativeHandleExposedToUpperLayer) {
        arp::core::appendDiagnostic(
            plan.diagnostics, "render system frame loop rejected native handle exposure");
    }

    if (!frameLoop.success || plan.action == RenderSystemFrameLoopAction::DiagnosticFailure
        || desc.logicalFrameSlots != 2 || plan.frameSlot >= desc.logicalFrameSlots
        || !plan.backendFallbacksHiddenFromUpperLayer || plan.nativeHandleExposedToUpperLayer) {
        plan.success = false;
        plan.action = RenderSystemFrameLoopAction::DiagnosticFailure;
        plan.telemetryRequired = true;
        disableFrameWork(plan);
        return plan;
    }

    if (plan.action == RenderSystemFrameLoopAction::MaintenanceRequired) {
        plan.success = true;
        plan.runMaintenance = true;
        plan.releaseCurrentFrame = true;
        plan.collectCompletedSubmissions = true;
        plan.telemetryRequired = true;
        disableFrameWork(plan);
        return plan;
    }

    if (plan.action == RenderSystemFrameLoopAction::SkipFrame) {
        plan.success = true;
        plan.skipFrame = true;
        plan.collectCompletedSubmissions = true;
        plan.telemetryRequired = true;
        disableFrameWork(plan);
        return plan;
    }

    if (!state.renderPipelineReady) {
        plan.success = false;
        plan.action = RenderSystemFrameLoopAction::DiagnosticFailure;
        plan.telemetryRequired = true;
        disableFrameWork(plan);
        arp::core::appendDiagnostic(
            plan.diagnostics, "render system frame loop requires render pipeline readiness");
        return plan;
    }
    if (state.frameInProgress) {
        plan.success = false;
        plan.action = RenderSystemFrameLoopAction::DiagnosticFailure;
        plan.telemetryRequired = true;
        disableFrameWork(plan);
        arp::core::appendDiagnostic(
            plan.diagnostics, "render system frame loop rejected frame already in progress");
        return plan;
    }

    plan.success = true;
    plan.action = RenderSystemFrameLoopAction::BeginFrame;
    plan.acquireFrame = frameLoop.shouldAcquireFrame;
    plan.recordFrame = frameLoop.shouldRecordFrame;
    plan.submitFrame = frameLoop.shouldSubmitFrame;
    plan.presentFrame = frameLoop.shouldPresentFrame;
    return plan;
}

RenderSystemFrameLoopRuntimeBridge::RenderSystemFrameLoopRuntimeBridge(
    const RenderSystemFrameLoopRuntimeBridgeDesc& desc)
    : desc_(desc)
{}

RenderSystemFrameLoopRuntimePlan RenderSystemFrameLoopRuntimeBridge::plan(
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState,
    const RenderSystemFrameLoopState& renderSystemState) const
{
    return planRenderSystemFrameLoopRuntime(
        desc_, rendererFramePlan, lifecycleState, renderSystemState);
}

RenderSystemFrameLoopRuntimePlan planRenderSystemFrameLoopRuntime(
    const RenderSystemFrameLoopRuntimeBridgeDesc& desc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState,
    const RenderSystemFrameLoopState& renderSystemState)
{
    RenderSystemFrameLoopRuntimePlan runtime;
    runtime.rendererFrameLoop =
        evaluateRendererFrameLoopBridge(desc.rendererFrameLoop, rendererFramePlan, lifecycleState);
    runtime.renderSystemPlan =
        planRenderSystemFrameLoop(desc.renderSystem, runtime.rendererFrameLoop, renderSystemState);

    const auto& plan = runtime.renderSystemPlan;
    runtime.success = plan.success;
    runtime.action = plan.action;
    runtime.selectedBackend = plan.selectedBackend;
    runtime.frameSlot = plan.frameSlot;
    runtime.frameSerial = plan.frameSerial;
    runtime.acquireFrame = plan.acquireFrame;
    runtime.recordFrame = plan.recordFrame;
    runtime.submitFrame = plan.submitFrame;
    runtime.presentFrame = plan.presentFrame;
    runtime.skipFrame = plan.skipFrame;
    runtime.runMaintenance = plan.runMaintenance;
    runtime.releaseCurrentFrame = plan.releaseCurrentFrame;
    runtime.collectCompletedSubmissions = plan.collectCompletedSubmissions;
    runtime.swapchainRebuildRequired = plan.swapchainRebuildRequired;
    runtime.releaseSurfaceResources = plan.releaseSurfaceResources;
    runtime.releaseTransientResources = plan.releaseTransientResources;
    runtime.memoryBudgetRefreshRequired = plan.memoryBudgetRefreshRequired;
    runtime.telemetryRequired =
        runtime.rendererFrameLoop.telemetryRequired || plan.telemetryRequired;
    runtime.backendFallbacksHiddenFromUpperLayer =
        runtime.rendererFrameLoop.backendFallbacksHiddenFromUpperLayer
        && plan.backendFallbacksHiddenFromUpperLayer;
    runtime.nativeHandleExposedToUpperLayer =
        runtime.rendererFrameLoop.nativeHandleExposedToUpperLayer
        || plan.nativeHandleExposedToUpperLayer;
    runtime.diagnostics = plan.diagnostics;
    return runtime;
}

} // namespace arp::platform
