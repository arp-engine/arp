#include "platform/render_system_frame_loop.h"
#include "renderer/deployment/renderer_deployment_evidence_admission.h"
#include "renderer/deployment/renderer_frame_lifecycle.h"
#include "renderer/deployment/renderer_webgpu_deployment_bridge.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

#ifdef NDEBUG
#error Deployment evidence tests require assertions
#endif

namespace {

using namespace arp;
using namespace renderer::deployment;
using rhi::Backend;

constexpr std::array identities{Backend::Vulkan,
    Backend::Metal,
    Backend::WebGPU,
    Backend::Null,
    Backend::WebGL,
    static_cast<Backend>(-1),
    static_cast<Backend>(255)};

bool contains(const core::pmr::String& diagnostic, const char* id)
{
    return diagnostic.find(id) != diagnostic.npos;
}

const char* identityDiagnostic(Backend backend, const char* stageDiagnostic)
{
    switch (backend) {
    case Backend::Null:
        return "RHI-RENDERER-DEPLOYMENT-NULL-VALIDATOR-ONLY";
    case Backend::Vulkan:
    case Backend::Metal:
    case Backend::WebGPU:
        return stageDiagnostic;
    case Backend::WebGL:
        return "RHI-WEBGL1-RENDERER-DEPLOYMENT-UNAVAILABLE";
    }
    return "RHI-RENDERER-DEPLOYMENT-BACKEND-UNKNOWN";
}

class DeploymentEvidenceFixture {
public:
    DeploymentEvidenceFixture()
    {
        frame_.success = true; // CPU planning input, never native workload evidence.
        frame_.plan.featureRequirements.logicalFrameSlots = 2u;
        lifecycle_.logicalFrameSlots = 2u;
        lifecycle_.running = true;
        lifecycle_.surfaceValid = true;
        lifecycle_.acquireAllowed = true;
        lifecycle_.presentAllowed = true;
        lifecycle_.surfaceKind = platform::PlatformSurfaceKind::WebCanvas;
    }

    void policyAndDefaults()
    {
        static_assert(RendererDeploymentEvidenceAdmission::contractVersion == 1u);
        constexpr std::array stages{RendererDeploymentEvidenceStage::NativeReport,
            RendererDeploymentEvidenceStage::WebGpuReport,
            RendererDeploymentEvidenceStage::CandidateSelection,
            RendererDeploymentEvidenceStage::FrameStart,
            RendererDeploymentEvidenceStage::LifecycleCollection,
            RendererDeploymentEvidenceStage::LifecycleReadiness};
        constexpr const char* expected[] = {"RHI-RENDERER-NATIVE-REPORT-EVIDENCE-UNAUTHENTICATED",
            "RHI-RENDERER-WEBGPU-REPORT-EVIDENCE-UNAUTHENTICATED",
            "RHI-RENDERER-CANDIDATE-EVIDENCE-UNAUTHENTICATED",
            "RHI-RENDERER-FRAME-START-EVIDENCE-UNAUTHENTICATED",
            "RHI-RENDERER-LIFECYCLE-PLAN-EVIDENCE-UNAUTHENTICATED",
            "RHI-RENDERER-LIFECYCLE-TELEMETRY-EVIDENCE-UNAUTHENTICATED"};
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        for (const auto backend : identities) {
            for (std::size_t index = 0; index < stages.size(); ++index) {
                const auto* stageDiagnostic = expected[index];
                if (stages[index] == RendererDeploymentEvidenceStage::WebGpuReport
                    && backend != Backend::WebGPU)
                    stageDiagnostic = "RHI-RENDERER-WEBGPU-REPORT-BACKEND-MISMATCH";
                assert(
                    std::strcmp(RendererDeploymentEvidenceAdmission::reject(backend, stages[index]),
                        identityDiagnostic(backend, stageDiagnostic))
                    == 0);
                for (const auto version : {0u, 2u, UINT32_MAX})
                    assert(std::strcmp(RendererDeploymentEvidenceAdmission::reject(
                                           backend, stages[index], version),
                               "RHI-RENDERER-DEPLOYMENT-CONTRACT-VERSION")
                        == 0);
            }
            assert(std::strcmp(RendererDeploymentEvidenceAdmission::reject(
                                   backend, static_cast<RendererDeploymentEvidenceStage>(255)),
                       "RHI-RENDERER-DEPLOYMENT-STAGE-UNKNOWN")
                == 0);
        }
        assert(RendererDeploymentEvidenceAdmission::candidateCountDiagnostic(0u, 1u) == nullptr);
        assert(RendererDeploymentEvidenceAdmission::candidateCountDiagnostic(1u, 1u) == nullptr);
        assert(std::strcmp(RendererDeploymentEvidenceAdmission::candidateCountDiagnostic(0u, 0u),
                   "RHI-RENDERER-DEPLOYMENT-CANDIDATE-CAPACITY")
            == 0);
        assert(std::strcmp(RendererDeploymentEvidenceAdmission::candidateCountDiagnostic(2u, 1u),
                   "RHI-RENDERER-DEPLOYMENT-CANDIDATE-CAPACITY")
            == 0);
        if constexpr (sizeof(std::size_t) > sizeof(std::uint32_t)) {
            assert(std::strcmp(RendererDeploymentEvidenceAdmission::candidateCountDiagnostic(
                                   static_cast<std::size_t>(UINT32_MAX) + 1u, UINT32_MAX),
                       "RHI-RENDERER-DEPLOYMENT-CANDIDATE-COUNT-UNREPRESENTABLE")
                == 0);
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
        const NativeRendererDeploymentReadinessReport native;
        const WebGpuDeploymentReadinessReport webgpu;
        const RendererBackendSelectionResult selection;
        const RendererBackendDeploymentPlan plan;
        assert(native.status == NativeRendererDeploymentReadinessStatus::DiagnosticFailure);
        assert(webgpu.status == WebGpuDeploymentReadinessStatus::DiagnosticFailure);
        assert(!native.success && !webgpu.success && !selection.success && !plan.canStartFrame);
        assert(selection.selectedCandidateIndex == UINT32_MAX
            && plan.selectedCandidateIndex == UINT32_MAX);
        assert(native.diagnostics.get_allocator().resource() == &core::defaultCpuMemoryResource());
        assert(webgpu.diagnostics.get_allocator().resource() == &core::defaultCpuMemoryResource());
        assert(
            selection.diagnostics.get_allocator().resource() == &core::defaultCpuMemoryResource());
        assert(RendererBackendSelectionDesc().candidates.get_allocator().resource()
            == &core::defaultCpuMemoryResource());
        assert(RendererBackendCandidateBuilderDesc().nativeContracts.get_allocator().resource()
            == &core::defaultCpuMemoryResource());
        const auto report = RendererFrameLifecycleTelemetryCollector({}).buildReport();
        assert(!report.success && !report.canStartFrame && report.telemetryRequired);
        assert(report.diagnostics.get_allocator().resource() == &core::defaultCpuMemoryResource());
    }

    void nativeReports()
    {
        for (const auto backend : identities) {
            for (unsigned flags = 0; flags != 16u; ++flags) {
                auto contract = rhi::semantics::nativeBackendCapabilityContract(backend);
                contract.queriedFromNativeDevice = (flags & 1u) != 0;
                contract.queriedExtendedFeatures = (flags & 2u) != 0;
                contract.preservesNativePotential = (flags & 4u) == 0;
                contract.flattenedToPortableSubset = (flags & 8u) != 0;
                const auto report = NativeRendererDeploymentBridge(contract).evaluate(frame_);
                assert(!report.success && !report.canStartFrame);
                assert(!report.contractComplete && !report.backendValidated);
                assert(report.status == NativeRendererDeploymentReadinessStatus::DiagnosticFailure);
                assert(report.backend == backend && report.telemetryRequired);
                assert(report.diagnostics.get_allocator().resource()
                    == &core::defaultCpuMemoryResource());
                assert(report.diagnosticFailureCount != 0u);
                assert(contains(report.diagnostics,
                    identityDiagnostic(
                        backend, "RHI-RENDERER-NATIVE-REPORT-EVIDENCE-UNAUTHENTICATED")));
            }
        }
    }

    void webGpuReports()
    {
        constexpr std::array order{Backend::WebGPU,
            Backend::Vulkan,
            Backend::Metal,
            Backend::Null,
            Backend::WebGL,
            static_cast<Backend>(-1),
            static_cast<Backend>(255)};
        for (const auto backend : order) {
            for (const auto status : {rhi::WebGpuRuntimeProfileStatus::PlatformValidated,
                     rhi::WebGpuRuntimeProfileStatus::ContractComplete,
                     rhi::WebGpuRuntimeProfileStatus::DiagnosticFailure,
                     static_cast<rhi::WebGpuRuntimeProfileStatus>(255)}) {
                rhi::semantics::WebGpuRuntimeTelemetryReport source;
                source.backend = backend;
                source.success = true;
                source.profileStatus = status;
                source.browserRuntime = source.canvasSurface = true;
                source.acquireAllowed = source.presentAllowed = true;
                source.diagnostics = "retained-source-diagnostic";
                const auto report = WebGpuDeploymentReadinessGate().evaluate(source);
                assert(!report.success && !report.canStartFrame);
                assert(!report.contractComplete && !report.platformValidated);
                assert(report.status == WebGpuDeploymentReadinessStatus::DiagnosticFailure);
                assert(report.backend == backend && report.telemetryRequired);
                assert(report.diagnosticFailureCount != 0u);
                const auto* stage = backend == Backend::WebGPU
                    ? "RHI-RENDERER-WEBGPU-REPORT-EVIDENCE-UNAUTHENTICATED"
                    : "RHI-RENDERER-WEBGPU-REPORT-BACKEND-MISMATCH";
                assert(contains(report.diagnostics, identityDiagnostic(backend, stage)));
                assert(contains(report.diagnostics, "retained-source-diagnostic"));
            }
        }
        rhi::semantics::WebGpuRuntimeTelemetryReport maintenance;
        maintenance.swapchainRebuildRequired = maintenance.releaseSurfaceResources = true;
        maintenance.releaseTransientResources = maintenance.memoryBudgetRefreshRequired = true;
        const auto report = evaluateWebGpuDeploymentReadiness(maintenance);
        assert(report.swapchainRebuildRequired && report.releaseSurfaceResources);
        assert(report.releaseTransientResources && report.memoryBudgetRefreshRequired);
        assert(!report.success && !report.canStartFrame);
    }

    void selection(bool webGlOnly = false)
    {
        for (const auto preference : {static_cast<RendererBackendPreference>(255),
                 static_cast<RendererBackendPreference>(-1),
                 RendererBackendPreference::NativeValidatedFirst,
                 RendererBackendPreference::WebFirst}) {
            if (webGlOnly && static_cast<int>(preference) < 0)
                continue;
            if (webGlOnly && preference != RendererBackendPreference::NativeValidatedFirst
                && preference != RendererBackendPreference::WebFirst)
                continue;
            for (const auto backend : identities) {
                if (webGlOnly && backend != Backend::WebGL)
                    continue;
                RendererBackendSelectionDesc desc;
                desc.maxCandidates = 1u;
                NativeRendererDeploymentReadinessReport claim;
                claim.backend = backend;
                claim.success = claim.canStartFrame = claim.contractComplete =
                    claim.backendValidated = true;
                claim.status = NativeRendererDeploymentReadinessStatus::BackendValidated;
                claim.diagnostics = "untrusted-candidate-detail";
                desc.candidates.push_back(claim);
                const auto result = RendererBackendSelectionPolicy(preference).select(desc);
                assert(!result.success);
                assert(result.selectedCandidateIndex == std::numeric_limits<std::uint32_t>::max());
                assert(result.selectedStatus
                    == NativeRendererDeploymentReadinessStatus::DiagnosticFailure);
                assert(result.telemetryRequired && result.diagnosticFailureUserVisible);
                if (preference != RendererBackendPreference::NativeValidatedFirst
                    && preference != RendererBackendPreference::WebFirst) {
                    assert(result.diagnosticFailureReason
                        == "RHI-RENDERER-DEPLOYMENT-PREFERENCE-UNKNOWN");
                    assert(!contains(result.diagnostics, "untrusted-candidate-detail"));
                } else {
                    assert(result.candidateCount == 1u && result.diagnosticCandidateCount == 1u);
                    assert(contains(result.diagnostics,
                        identityDiagnostic(
                            backend, "RHI-RENDERER-CANDIDATE-EVIDENCE-UNAUTHENTICATED")));
                    assert(contains(result.diagnostics, "untrusted-candidate-detail"));
                    assert(contains(
                        result.diagnostics, "RHI-RENDERER-DEPLOYMENT-NO-AUTHENTICATED-CANDIDATE"));
                }
                desc.maxCandidates = 0u;
                const auto capacity = selectRendererBackend(preference, desc);
                assert(!capacity.success);
                if (preference == RendererBackendPreference::NativeValidatedFirst
                    || preference == RendererBackendPreference::WebFirst)
                    assert(contains(
                        capacity.diagnostics, "RHI-RENDERER-DEPLOYMENT-CANDIDATE-CAPACITY"));
            }
        }
    }

    void frameStart()
    {
        for (const auto backend : identities) {
            RendererBackendDeploymentPlan claim;
            claim.success = claim.canStartFrame = true;
            claim.selectedBackend = backend;
            claim.selectedStatus = NativeRendererDeploymentReadinessStatus::BackendValidated;
            auto lifecycle = lifecycle_;
            lifecycle.swapchainRebuildRequired = lifecycle.releaseSurfaceResources = true;
            lifecycle.releaseTransientResources = lifecycle.memoryBudgetRefreshRequired = true;
            const auto result = RendererFrameStartGate({}).evaluate(claim, frame_.plan, lifecycle);
            assert(!result.success && !result.canStartFrame && result.telemetryRequired);
            assert(contains(result.diagnostics,
                identityDiagnostic(backend, "RHI-RENDERER-FRAME-START-EVIDENCE-UNAUTHENTICATED")));
            assert(result.swapchainRebuildRequired && result.releaseSurfaceResources);
            assert(result.releaseTransientResources && result.memoryBudgetRefreshRequired);
        }
    }

    void collection()
    {
        for (const auto backend : identities) {
            RendererFrameLifecyclePlan claim;
            claim.success = claim.canStartFrame = true;
            claim.selectedBackend = backend;
            claim.deployment.candidateCount = 1u;
            const auto report = buildRendererFrameLifecycleTelemetryReport(claim);
            assert(!report.success && !report.canStartFrame && report.telemetryRequired);
            assert(report.diagnosticFailureCount != 0u);
            assert(contains(report.diagnostics,
                identityDiagnostic(
                    backend, "RHI-RENDERER-LIFECYCLE-PLAN-EVIDENCE-UNAUTHENTICATED")));
        }
    }

    void readiness()
    {
        for (const auto backend : identities) {
            RendererFrameLifecycleTelemetryReport claim;
            claim.success = claim.canStartFrame = true;
            claim.selectedBackend = backend;
            claim.candidateCount = 1u;
            const auto report = RendererFrameLifecycleReadinessGate().evaluate(claim);
            assert(!report.success && !report.canStartFrame && report.telemetryRequired);
            assert(report.status == RendererFrameLifecycleReadinessStatus::DiagnosticFailure);
            assert(report.diagnosticFailureCount != 0u);
            assert(contains(report.diagnostics,
                identityDiagnostic(
                    backend, "RHI-RENDERER-LIFECYCLE-TELEMETRY-EVIDENCE-UNAUTHENTICATED")));
        }
    }

    void chain()
    {
        RendererFrameLifecycleBridgeDesc desc;
        desc.planner.deployment.candidates.maxCandidates = 1u;
        auto claim = rhi::semantics::nativeBackendCapabilityContract(Backend::Vulkan);
        claim.queriedFromNativeDevice = claim.queriedExtendedFeatures = true;
        desc.planner.deployment.candidates.nativeContracts.push_back(claim);
        const auto decision = RendererFrameLifecycleBridge(desc).evaluate(frame_, lifecycle_);
        assert(!decision.success && !decision.canStartFrame);
        assert(decision.status == RendererFrameLifecycleReadinessStatus::DiagnosticFailure);
        assert(!decision.lifecycle.deployment.success && !decision.lifecycle.frameStart.success);
        assert(decision.lifecycle.deployment.selectedCandidateIndex == UINT32_MAX);
        assert(contains(decision.diagnostics, "RHI-RENDERER-CANDIDATE-EVIDENCE-UNAUTHENTICATED"));
        assert(contains(decision.diagnostics,
            identityDiagnostic(decision.selectedBackend,
                "RHI-RENDERER-LIFECYCLE-TELEMETRY-EVIDENCE-UNAUTHENTICATED")));
        platform::RendererFrameLoopBridgeDesc loop;
        loop.lifecycle = desc;
        const auto execution = platform::RendererFrameLoopBridge(loop).evaluate(frame_, lifecycle_);
        assert(!execution.success && !execution.canStartFrame);
        assert(!execution.shouldAcquireFrame && !execution.shouldRecordFrame);
        assert(!execution.shouldSubmitFrame && !execution.shouldPresentFrame);
        assert(execution.action == platform::RendererFrameLoopAction::DiagnosticFailure);

        desc.planner.deployment.candidates.nativeContracts.clear();
        desc.planner.deployment.candidates.includeWebGpuRuntimeProfile = true;
        desc.planner.deployment.candidates.webGpuProfile.status =
            rhi::WebGpuRuntimeProfileStatus::PlatformValidated;
        const auto candidates =
            RendererBackendCandidateBuilder(desc.planner.deployment.candidates).build(frame_);
        assert(candidates.success && candidates.selection.candidates.size() == 1u);
        const auto& mapped = candidates.selection.candidates.front();
        assert(mapped.backend == Backend::WebGPU);
        assert(!mapped.success && !mapped.canStartFrame && !mapped.backendValidated
            && !mapped.contractComplete);
        assert(mapped.status == NativeRendererDeploymentReadinessStatus::DiagnosticFailure);
        assert(contains(mapped.diagnostics, "RHI-RENDERER-NATIVE-REPORT-EVIDENCE-UNAUTHENTICATED"));
        const auto web = RendererFrameLifecycleBridge(desc).evaluate(frame_, lifecycle_);
        assert(!web.success && !web.canStartFrame);
        assert(web.status == RendererFrameLifecycleReadinessStatus::DiagnosticFailure);
        assert(!web.lifecycle.deployment.selectedReadiness.backendValidated);
        assert(!web.lifecycle.deployment.selectedReadiness.canStartFrame);
        assert(web.lifecycle.deployment.selectedCandidateIndex == UINT32_MAX);
        assert(contains(web.diagnostics, "RHI-RENDERER-CANDIDATE-EVIDENCE-UNAUTHENTICATED"));
        assert(contains(web.diagnostics,
            identityDiagnostic(
                web.selectedBackend, "RHI-RENDERER-LIFECYCLE-TELEMETRY-EVIDENCE-UNAUTHENTICATED")));

        desc.planner.deployment.candidates.maxCandidates = 0u;
        const auto capacity = RendererFrameLifecycleBridge(desc).evaluate(frame_, lifecycle_);
        assert(!capacity.success && !capacity.canStartFrame);
        assert(capacity.lifecycle.deployment.capacityExceeded);
    }

private:
    renderer::RendererRhiFramePlanBuildResult frame_;
    platform::PlatformLifecycleState lifecycle_;
};

} // namespace

int main(int argc, char** argv)
{
    DeploymentEvidenceFixture fixture;
    const char* selected = argc == 2 ? argv[1] : "all";
    bool matched = false;
    const auto run = [selected, &matched](const char* name) {
        const bool selectedCase =
            std::strcmp(selected, "all") == 0 || std::strcmp(selected, name) == 0;
        matched = matched || selectedCase;
        return selectedCase;
    };
    if (run("policy"))
        fixture.policyAndDefaults();
    if (run("native"))
        fixture.nativeReports();
    if (run("webgpu"))
        fixture.webGpuReports();
    if (run("selection"))
        fixture.selection();
    if (run("webgl"))
        fixture.selection(true);
    if (run("frame-start"))
        fixture.frameStart();
    if (run("collection"))
        fixture.collection();
    if (run("readiness"))
        fixture.readiness();
    if (run("chain"))
        fixture.chain();
    if (!matched)
        return 2;
    std::puts("RendererDeploymentEvidenceAdmission/1: all requested legacy gates refused");
}
