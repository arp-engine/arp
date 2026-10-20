#pragma once

#include "rhi/semantics/semantic_report_backend_admission.h"

#include <cstddef>

namespace arp::renderer::deployment {

enum class RendererDeploymentEvidenceStage {
    NativeReport,
    WebGpuReport,
    CandidateSelection,
    FrameStart,
    LifecycleCollection,
    LifecycleReadiness
};

// RendererDeploymentEvidenceAdmission/1. These publicly writable legacy values
// carry structural claims, never authenticated device/workload execution proof.
// A future controlled execution entry must not reopen these aggregate gates.
class RendererDeploymentEvidenceAdmission final {
public:
    static constexpr std::uint32_t contractVersion = 1u;

    static constexpr const char* capacityDiagnostic() noexcept
    {
        return "RHI-RENDERER-DEPLOYMENT-CANDIDATE-CAPACITY";
    }

    // Count admission is metadata validation only, never execution admission.
    static constexpr const char* candidateCountDiagnostic(
        std::size_t count, std::uint32_t capacity) noexcept
    {
        if (count > UINT32_MAX)
            return "RHI-RENDERER-DEPLOYMENT-CANDIDATE-COUNT-UNREPRESENTABLE";
        return capacity == 0u || count > capacity ? capacityDiagnostic() : nullptr;
    }

    static constexpr const char* reject(rhi::Backend backend,
        RendererDeploymentEvidenceStage stage,
        std::uint32_t version = contractVersion) noexcept
    {
        if (version != contractVersion)
            return "RHI-RENDERER-DEPLOYMENT-CONTRACT-VERSION";

        const char* diagnostic = nullptr;
        switch (stage) {
        case RendererDeploymentEvidenceStage::NativeReport:
            diagnostic = "RHI-RENDERER-NATIVE-REPORT-EVIDENCE-UNAUTHENTICATED";
            break;
        case RendererDeploymentEvidenceStage::WebGpuReport:
            diagnostic = "RHI-RENDERER-WEBGPU-REPORT-EVIDENCE-UNAUTHENTICATED";
            break;
        case RendererDeploymentEvidenceStage::CandidateSelection:
            diagnostic = "RHI-RENDERER-CANDIDATE-EVIDENCE-UNAUTHENTICATED";
            break;
        case RendererDeploymentEvidenceStage::FrameStart:
            diagnostic = "RHI-RENDERER-FRAME-START-EVIDENCE-UNAUTHENTICATED";
            break;
        case RendererDeploymentEvidenceStage::LifecycleCollection:
            diagnostic = "RHI-RENDERER-LIFECYCLE-PLAN-EVIDENCE-UNAUTHENTICATED";
            break;
        case RendererDeploymentEvidenceStage::LifecycleReadiness:
            diagnostic = "RHI-RENDERER-LIFECYCLE-TELEMETRY-EVIDENCE-UNAUTHENTICATED";
            break;
        default:
            return "RHI-RENDERER-DEPLOYMENT-STAGE-UNKNOWN";
        }

        const auto identity = rhi::semantics::SemanticReportBackendAdmission::evaluate(backend);
        if (!identity.knownIdentity)
            return "RHI-RENDERER-DEPLOYMENT-BACKEND-UNKNOWN";
        if (identity.validatorOnly)
            return "RHI-RENDERER-DEPLOYMENT-NULL-VALIDATOR-ONLY";
        if (!identity.inspectStructuralContract)
            return "RHI-WEBGL1-RENDERER-DEPLOYMENT-UNAVAILABLE";
        if (stage == RendererDeploymentEvidenceStage::WebGpuReport
            && backend != rhi::Backend::WebGPU)
            return "RHI-RENDERER-WEBGPU-REPORT-BACKEND-MISMATCH";
        return diagnostic;
    }
};

} // namespace arp::renderer::deployment
