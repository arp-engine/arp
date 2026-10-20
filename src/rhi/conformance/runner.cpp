#include "core/diagnostic_builder.h"
#include "rhi/conformance/matrix.h"
#include "rhi/semantics/conformance_evidence_admission.h"

#include <utility>

namespace arp::rhi {

RhiConformanceRunner::RhiConformanceRunner(RhiConformanceRunnerDesc desc) : desc_(std::move(desc))
{}

RhiConformanceRunnerReport RhiConformanceRunner::run() const
{
    RhiConformanceRunnerReport report;
    const auto scenarios =
        desc_.scenarios.empty() ? defaultRhiConformanceScenarios() : desc_.scenarios;
    report.backendCount = static_cast<std::uint32_t>(desc_.backends.size());
    report.descriptorOnlySyntheticInputRejected = true;
    report.backendValidationGated = true;
    if (desc_.backends.empty()) {
        report.diagnostics = "RHI conformance runner requires at least one backend runtime";
        return report;
    }
    report.backends.reserve(desc_.backends.size());
    for (const auto& backendDesc : desc_.backends) {
        ConformanceBackendExecutionReport backendReport;
        backendReport.backend = backendDesc.backend;
        backendReport.diagnostics =
            semantics::ConformanceEvidenceAdmission::runtimeDiagnostic(backendDesc.backend);
        backendReport.scenarios.reserve(scenarios.size());
        for (const auto scenario : scenarios) {
            ConformanceScenarioResult result;
            result.scenario = scenario;
            result.mode = FallbackMode::DiagnosticFailure;
            result.diagnostics = backendReport.diagnostics;
            backendReport.scenarios.push_back(std::move(result));
            ++backendReport.totalScenarioCount;
            ++backendReport.failedScenarioCount;
        }
        // There is no actual workload/capture source in this facade. Refuse
        // before invoking the factory; caller flags cannot restore the old
        // size-only-copy heartbeat or authorize native readiness.
        ++report.runtimeUnavailableCount;
        report.totalScenarioCount += backendReport.totalScenarioCount;
        report.failedScenarioCount += backendReport.failedScenarioCount;
        core::appendDiagnostic(report.diagnostics, backendReport.diagnostics);
        report.backends.push_back(std::move(backendReport));
    }
    return report;
}

} // namespace arp::rhi
