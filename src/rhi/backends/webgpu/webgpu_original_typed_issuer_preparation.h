#pragma once

#include "rhi/backends/webgpu/webgpu_original_typed_callback_custody.h"
#include "rhi/backends/webgpu/webgpu_transfer_retention.h"
#include "rhi/frontend/command_shader_validation.h"
#include "rhi/frontend/command_transfer_validation.h"
#include "rhi/frontend/command_usage_projection.h"
#include "rhi/semantics/command_lowering.h"

namespace arp::test {
class OriginalTypedIssuerPreparationFixture;
}

namespace arp::rhi_webgpu {
class WebGpuOriginalGeneralOutputSnapshot;

class WebGpuOriginalTypedIssuerPreparation final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuCanonicalCallbackCustody;
    friend class arp::test::OriginalTypedIssuerPreparationFixture;
    friend class WebGpuOriginalGeneralOutputSnapshot;

    class ConstructionKey {
        friend class WebGpuCanonicalCallbackCustody;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Request = rhi::OriginalTypedIssuerPreparationRequest;

    WebGpuOriginalTypedIssuerPreparation(ConstructionKey,
        std::shared_ptr<const WebGpuOriginalTypedCallbackCustody> custody,
        Request request,
        std::uint32_t owners,
        std::weak_ptr<const rhi::OriginalCommandStateTransaction> originalRoot) noexcept
        : custody_(std::move(custody)), request_(request), owners_(owners),
          originalRoot_(std::move(originalRoot))
    {}

    static std::shared_ptr<const WebGpuOriginalTypedIssuerPreparation> prepareOriginal(
        std::shared_ptr<const WebGpuOriginalTypedCallbackCustody>, const Request&, const char*&);

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuOriginalTypedIssuerPreparation>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-WEBGPU-TYPED-ISSUER-OWNER"
                                                              : self->validate();
    }

    const char* validate() const noexcept;

    std::uint32_t retainedOwnerCount() const noexcept { return owners_; }

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return arp::rhi::semantics::OriginalTypedIssuerPreparationLowering::lowering(
            rhi::Backend::WebGPU, request_.contractVersion)
            .diagnostic;
    }

    static const char* recordOriginal(
        const std::shared_ptr<const WebGpuOriginalTypedIssuerPreparation>&,
        const rhi::OriginalTypedNativeExecutionRequest&) noexcept;
    static const char* submitOriginal(
        const std::shared_ptr<const WebGpuOriginalTypedIssuerPreparation>&,
        const rhi::OriginalTypedNativeExecutionRequest&) noexcept;
    bool hasOriginalRecordedCommand() const noexcept;
    bool hasOriginalCompletedSubmission() const noexcept;
    const char* recordingDiagnostic() const noexcept;
    const char* submissionDiagnostic() const noexcept;
    bool issuersClosedForInspection() const noexcept;

private:
    std::shared_ptr<const WebGpuOriginalTypedNativePayload>
    preparedGeneralOutputPayload() const noexcept;
    static const char* validateGeneralOutputCompletionEvidence(
        const std::shared_ptr<const WebGpuOriginalTypedIssuerPreparation>&,
        const std::shared_ptr<const rhi::OriginalCommandStateTransaction>&,
        std::uint32_t outputVersion = 1u) noexcept;
    static const char* validateGeneralOutputIssuer(
        const std::shared_ptr<const WebGpuOriginalTypedIssuerPreparation>&,
        const std::shared_ptr<const rhi::OriginalCommandStateTransaction>&,
        bool completed,
        std::uint32_t outputVersion = 1u) noexcept;
    const std::shared_ptr<const WebGpuOriginalTypedCallbackCustody> custody_;
    const Request request_;
    const std::uint32_t owners_;
    const std::weak_ptr<const rhi::OriginalCommandStateTransaction> originalRoot_;
};

// Immutable retained original association. It issues no copy, map or content
// stamp and does not attest whole-texture shader write coverage.
class WebGpuOriginalGeneralOutputSnapshot final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuOriginalGeneralOutputSnapshot;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Budget = rhi::OriginalGeneralOutputSnapshotContract::Budget;
    using Root = rhi::OriginalCommandStateTransaction;
    using Issuer = WebGpuOriginalTypedIssuerPreparation;
    using Access = rhi::common::OriginalCommandDescriptorConsumption::Access;
    using Attachment = rhi::common::OriginalTypedUsageProjection::AttachmentPoint;
    using Capture = std::variant<Access, Attachment>;
    using UsageRequest = rhi::common::OriginalTypedUsageProjection::StateBoundRequest;

    WebGpuOriginalGeneralOutputSnapshot(ConstructionKey,
        std::shared_ptr<const Issuer> issuer,
        std::shared_ptr<const Root> root,
        std::shared_ptr<const rhi::OriginalCommandTextureLease> output,
        Budget budget,
        Capture capture,
        WebGpuTransferResourceObservation native,
        UsageRequest usageRequest = {}) noexcept
        : issuer_(std::move(issuer)), root_(std::move(root)), output_(std::move(output)),
          budget_(budget), capture_(std::move(capture)), native_(std::move(native)),
          usageRequest_(usageRequest)
    {}

    static std::shared_ptr<const WebGpuOriginalGeneralOutputSnapshot> prepareOriginal(
        std::shared_ptr<const Issuer>,
        std::shared_ptr<const Root>,
        std::shared_ptr<const rhi::OriginalCommandTextureLease>,
        const Budget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&);

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuOriginalGeneralOutputSnapshot>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-GENERAL-OUTPUT-SNAPSHOT-OWNER"
                                                              : self->validateIdentity();
    }

    static const char* validateCompletedOriginal(
        const std::shared_ptr<const WebGpuOriginalGeneralOutputSnapshot>&) noexcept;

    const auto& originalRoot() const noexcept { return root_; }

    const auto& originalIssuer() const noexcept { return issuer_; }

    const auto& originalOutput() const noexcept { return output_; }

    const auto& originalBudget() const noexcept { return budget_; }

    std::uint32_t retainedOwnerCount() const noexcept { return issuer_->owners_ + 1u; }

    bool overlapsCpuOwner(const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept;

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const char* validateIdentity() const noexcept;
    const std::shared_ptr<const Issuer> issuer_;
    const std::shared_ptr<const Root> root_;
    const std::shared_ptr<const rhi::OriginalCommandTextureLease> output_;
    const Budget budget_;
    const Capture capture_;
    const WebGpuTransferResourceObservation native_;
    const UsageRequest usageRequest_;
};
} // namespace arp::rhi_webgpu
