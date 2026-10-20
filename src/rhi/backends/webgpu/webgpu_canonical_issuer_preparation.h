#pragma once

#include "rhi/backends/webgpu/webgpu_canonical_copy_pass_lowering.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/semantics/command_lowering.h"

namespace arp::test {
class WebGpuCanonicalIssuerPreparationFixture;
class WebGpuCanonicalScopedRecordingFixture;
class WebGpuCanonicalSubmissionFixture;
} // namespace arp::test

namespace arp::rhi_webgpu {
// Opaque storage over real issuer types. Preparation is not a recorded proof;
// a separate ScopedRecording/1 opt-in issues recording only. Callers cannot
// obtain either issuer or mutable native payload. QueueSubmission/1 separately
// opts into the selected Prototype payload; general execution remains closed.
class WebGpuCanonicalIssuerPreparation final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuCanonicalCallbackCustody;
    friend class WebGpuOrdinaryRecording;
    friend class WebGpuOrdinarySubmission;
    friend class arp::test::WebGpuCanonicalIssuerPreparationFixture;
    friend class arp::test::WebGpuCanonicalScopedRecordingFixture;
    friend class arp::test::WebGpuCanonicalSubmissionFixture;

    class ConstructionKey {
        friend class WebGpuCanonicalCallbackCustody;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Budget = rhi::OriginalCommandIssuerPreparationBudget;

    WebGpuCanonicalIssuerPreparation(ConstructionKey,
        std::shared_ptr<const WebGpuCanonicalCopyPassLowering> lowering,
        Budget budget,
        std::uint32_t owners,
        std::weak_ptr<const rhi::OriginalCommandStateTransaction> originalRoot) noexcept
        : lowering_(std::move(lowering)), budget_(budget), owners_(owners),
          originalRoot_(std::move(originalRoot))
    {}

    static std::shared_ptr<const WebGpuCanonicalIssuerPreparation> prepareOriginal(
        std::shared_ptr<const WebGpuCanonicalCopyPassLowering>, const Budget&, const char*&);

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuCanonicalIssuerPreparation>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-WEBGPU-CANONICAL-ISSUER-OWNER"
                                                              : self->validate();
    }

    const char* validate() const noexcept;
    static const char* recordOriginal(
        const std::shared_ptr<const WebGpuCanonicalIssuerPreparation>&,
        const rhi::OriginalCommandScopedRecordingIntent&) noexcept;
    static const char* submitOriginal(
        const std::shared_ptr<const WebGpuCanonicalIssuerPreparation>&,
        const rhi::OriginalCommandQueueSubmissionIntent&) noexcept;
    bool hasOriginalCompletedSubmission() const noexcept;
    const char* submissionDiagnostic() const noexcept;
    bool hasOriginalRecordedCommand() const noexcept;
    const char* recordingDiagnostic() const noexcept;
    // Correlates the existing completed issuer with its original upload root.
    // Borrows initialized input facts only; no new receipt or shader authority.
    static const char* validateOriginalCompletedShaderTextureInput(
        const std::shared_ptr<const WebGpuCanonicalIssuerPreparation>&,
        const std::shared_ptr<const WebGpuCanonicalTextureContentUploadSource>&,
        const rhi::GpuTexture::ContentExpectation&,
        const rhi::OriginalCompletedShaderTextureInputContract::Budget&,
        const std::shared_ptr<const rhi::OriginalCommandStateTransaction>& originalRoot =
            {}) noexcept;

    const auto& originalLowering() const noexcept { return lowering_; }

    std::uint32_t retainedOwnerCount() const noexcept { return owners_; }

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return arp::rhi::semantics::OriginalCommandIssuerPreparationLowering::lowering(
            rhi::Backend::WebGPU)
            .diagnostic;
    }

private:
    std::uint32_t submissionSlotsAtLossForInspection() const noexcept;
    bool submissionSlotsDrainedForInspection() const noexcept;
    bool recordingSlotsDrainedForInspection() const noexcept;
    std::uint32_t recordingScopesAtLossForInspection() const noexcept;
    bool issuersClosedForInspection() const noexcept;
    const std::shared_ptr<const WebGpuCanonicalCopyPassLowering> lowering_;
    const Budget budget_;
    const std::uint32_t owners_;
    const std::weak_ptr<const rhi::OriginalCommandStateTransaction> originalRoot_;
};
} // namespace arp::rhi_webgpu
