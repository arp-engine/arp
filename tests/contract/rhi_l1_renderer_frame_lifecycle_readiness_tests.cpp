#include "core/memory/cpu_allocator.h"
#include "rhi/semantics/renderer_frame_lifecycle_readiness.h"

#include <cassert>
#include <string>

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    RendererFrameLifecycleTelemetryReport readyReport;
    readyReport.success = true;
    readyReport.canStartFrame = true;
    readyReport.selectedBackend = Backend::WebGPU;
    readyReport.frameSlot = 1;
    readyReport.candidateCount = 2;
    readyReport.degradedFeatureCount = 2;
    readyReport.telemetryRequired = true;
    readyReport.backendFallbacksHiddenFromUpperLayer = true;

    RendererFrameLifecycleReadinessGate gate;
    const auto ready = gate.evaluate(readyReport);
    assert(ready.success);
    assert(ready.status == RendererFrameLifecycleReadinessStatus::FrameStartAllowed);
    assert(ready.canStartFrame);
    assert(ready.selectedBackend == Backend::WebGPU);
    assert(ready.frameSlot == 1u);
    assert(ready.telemetryRequired);
    assert(ready.degradedFeatureCount == 2u);
    assert(ready.diagnosticFailureCount == 0u);

    auto blockedReport = readyReport;
    blockedReport.success = false;
    blockedReport.canStartFrame = false;
    blockedReport.frameStartDiagnosticFailureCount = 1;
    blockedReport.diagnosticFailureCount = 1;
    blockedReport.diagnostics = "renderer frame start requires acquire permission";
    const auto blocked = evaluateRendererFrameLifecycleReadiness(blockedReport);
    assert(!blocked.success);
    assert(blocked.status == RendererFrameLifecycleReadinessStatus::FrameStartBlocked);
    assert(!blocked.canStartFrame);
    assert(blocked.telemetryRequired);
    assert(blocked.diagnosticFailureCount == 1u);
    assert(blocked.diagnostics.find("acquire") != std::pmr::string::npos);

    auto diagnosticReport = readyReport;
    diagnosticReport.success = false;
    diagnosticReport.canStartFrame = false;
    diagnosticReport.nativeHandleExposedToUpperLayer = true;
    diagnosticReport.diagnosticFailureCount = 1;
    diagnosticReport.diagnostics = "renderer frame lifecycle rejected native handle exposure";
    const auto diagnostic = gate.evaluate(diagnosticReport);
    assert(!diagnostic.success);
    assert(diagnostic.status == RendererFrameLifecycleReadinessStatus::DiagnosticFailure);
    assert(!diagnostic.canStartFrame);
    assert(diagnostic.nativeHandleExposedToUpperLayer);
    assert(diagnostic.diagnostics.find("native handle") != std::pmr::string::npos);

    auto invalidSlotReport = readyReport;
    invalidSlotReport.frameSlot = 2;
    const auto invalidSlot = gate.evaluate(invalidSlotReport);
    assert(!invalidSlot.success);
    assert(invalidSlot.status == RendererFrameLifecycleReadinessStatus::DiagnosticFailure);
    assert(invalidSlot.diagnostics.find("frame slot") != std::pmr::string::npos);

    auto noCandidateReport = readyReport;
    noCandidateReport.candidateCount = 0;
    const auto noCandidate = gate.evaluate(noCandidateReport);
    assert(!noCandidate.success);
    assert(noCandidate.status == RendererFrameLifecycleReadinessStatus::DiagnosticFailure);
    assert(noCandidate.diagnostics.find("candidate") != std::pmr::string::npos);

    return 0;
}
