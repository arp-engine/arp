#include "core/cpu_allocator.h"
#include "rhi/semantics/render_system_frame_loop_adapter.h"

#include <cassert>
#include <string>

namespace {

arp::platform::RendererFrameLoopDecision makeBeginDecision()
{
    arp::platform::RendererFrameLoopDecision decision;
    decision.success = true;
    decision.canStartFrame = true;
    decision.action = arp::platform::RendererFrameLoopAction::BeginFrame;
    decision.selectedBackend = arp::rhi::Backend::WebGPU;
    decision.frameSlot = 1;
    decision.shouldAcquireFrame = true;
    decision.shouldRecordFrame = true;
    decision.shouldSubmitFrame = true;
    decision.shouldPresentFrame = true;
    decision.telemetryRequired = true;
    decision.backendFallbacksHiddenFromUpperLayer = true;
    return decision;
}

arp::platform::RendererFrameLoopDecision makeSkipDecision()
{
    auto decision = makeBeginDecision();
    decision.success = true;
    decision.canStartFrame = false;
    decision.action = arp::platform::RendererFrameLoopAction::SkipFrame;
    decision.shouldAcquireFrame = false;
    decision.shouldRecordFrame = false;
    decision.shouldSubmitFrame = false;
    decision.shouldPresentFrame = false;
    decision.diagnostics = "renderer frame start requires acquire permission";
    return decision;
}

arp::platform::RendererFrameLoopDecision makeMaintenanceDecision()
{
    auto decision = makeSkipDecision();
    decision.action = arp::platform::RendererFrameLoopAction::MaintenanceRequired;
    decision.swapchainRebuildRequired = true;
    decision.releaseSurfaceResources = true;
    decision.releaseTransientResources = true;
    decision.memoryBudgetRefreshRequired = true;
    decision.diagnostics = "renderer frame loop requires maintenance before frame start";
    return decision;
}

arp::platform::RendererFrameLoopDecision makeDiagnosticDecision()
{
    auto decision = makeBeginDecision();
    decision.success = false;
    decision.canStartFrame = false;
    decision.action = arp::platform::RendererFrameLoopAction::DiagnosticFailure;
    decision.shouldAcquireFrame = false;
    decision.shouldRecordFrame = false;
    decision.shouldSubmitFrame = false;
    decision.shouldPresentFrame = false;
    decision.nativeHandleExposedToUpperLayer = true;
    decision.diagnostics = "renderer frame loop rejected native handle exposure";
    return decision;
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    RenderSystemFrameLoopAdapter adapter({2});

    RenderSystemFrameLoopState readyState;
    readyState.renderPipelineReady = true;
    readyState.frameInProgress = false;
    readyState.nextFrameSerial = 42;
    const auto beginPlan = adapter.plan(makeBeginDecision(), readyState);
    assert(beginPlan.success);
    assert(beginPlan.action == RenderSystemFrameLoopAction::BeginFrame);
    assert(beginPlan.selectedBackend == Backend::WebGPU);
    assert(beginPlan.frameSlot == 1u);
    assert(beginPlan.frameSerial == 42u);
    assert(beginPlan.acquireFrame);
    assert(beginPlan.recordFrame);
    assert(beginPlan.submitFrame);
    assert(beginPlan.presentFrame);
    assert(!beginPlan.runMaintenance);
    assert(!beginPlan.releaseCurrentFrame);
    assert(beginPlan.telemetryRequired);
    assert(beginPlan.backendFallbacksHiddenFromUpperLayer);
    assert(!beginPlan.nativeHandleExposedToUpperLayer);

    const auto skippedPlan = adapter.plan(makeSkipDecision(), readyState);
    assert(skippedPlan.success);
    assert(skippedPlan.action == RenderSystemFrameLoopAction::SkipFrame);
    assert(!skippedPlan.acquireFrame);
    assert(!skippedPlan.recordFrame);
    assert(!skippedPlan.submitFrame);
    assert(!skippedPlan.presentFrame);
    assert(skippedPlan.skipFrame);
    assert(skippedPlan.collectCompletedSubmissions);
    assert(skippedPlan.diagnostics.find("acquire") != std::pmr::string::npos);

    const auto maintenancePlan =
        planRenderSystemFrameLoop({2}, makeMaintenanceDecision(), readyState);
    assert(maintenancePlan.success);
    assert(maintenancePlan.action == RenderSystemFrameLoopAction::MaintenanceRequired);
    assert(maintenancePlan.runMaintenance);
    assert(maintenancePlan.releaseCurrentFrame);
    assert(maintenancePlan.collectCompletedSubmissions);
    assert(maintenancePlan.swapchainRebuildRequired);
    assert(maintenancePlan.releaseSurfaceResources);
    assert(maintenancePlan.releaseTransientResources);
    assert(maintenancePlan.memoryBudgetRefreshRequired);
    assert(maintenancePlan.diagnostics.find("maintenance") != std::pmr::string::npos);

    auto noPipelineState = readyState;
    noPipelineState.renderPipelineReady = false;
    const auto noPipelinePlan = adapter.plan(makeBeginDecision(), noPipelineState);
    assert(!noPipelinePlan.success);
    assert(noPipelinePlan.action == RenderSystemFrameLoopAction::DiagnosticFailure);
    assert(!noPipelinePlan.acquireFrame);
    assert(noPipelinePlan.diagnostics.find("render pipeline") != std::pmr::string::npos);

    auto overlappingState = readyState;
    overlappingState.frameInProgress = true;
    const auto overlappingPlan = adapter.plan(makeBeginDecision(), overlappingState);
    assert(!overlappingPlan.success);
    assert(overlappingPlan.action == RenderSystemFrameLoopAction::DiagnosticFailure);
    assert(overlappingPlan.diagnostics.find("frame already in progress") != std::pmr::string::npos);

    const auto diagnosticPlan = adapter.plan(makeDiagnosticDecision(), readyState);
    assert(!diagnosticPlan.success);
    assert(diagnosticPlan.action == RenderSystemFrameLoopAction::DiagnosticFailure);
    assert(!diagnosticPlan.acquireFrame);
    assert(diagnosticPlan.nativeHandleExposedToUpperLayer);
    assert(diagnosticPlan.diagnostics.find("native handle") != std::pmr::string::npos);

    RenderSystemFrameLoopAdapter badSlotsAdapter({3});
    const auto badSlotsPlan = badSlotsAdapter.plan(makeBeginDecision(), readyState);
    assert(!badSlotsPlan.success);
    assert(badSlotsPlan.action == RenderSystemFrameLoopAction::DiagnosticFailure);
    assert(badSlotsPlan.diagnostics.find("two logical frame slots") != std::pmr::string::npos);

    auto invalidSlotDecision = makeBeginDecision();
    invalidSlotDecision.frameSlot = 2;
    const auto invalidSlotPlan = adapter.plan(invalidSlotDecision, readyState);
    assert(!invalidSlotPlan.success);
    assert(invalidSlotPlan.action == RenderSystemFrameLoopAction::DiagnosticFailure);
    assert(invalidSlotPlan.diagnostics.find("frame slot") != std::pmr::string::npos);

    return 0;
}
