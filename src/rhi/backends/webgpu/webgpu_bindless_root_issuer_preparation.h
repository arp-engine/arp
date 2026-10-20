#pragma once
#include "rhi/backends/webgpu/webgpu_bindless_root_callback_custody.h"
#include "rhi/frontend/bindless_root_execution.h"

namespace arp::rhi_webgpu {
// Existing issuers retained by the same RuntimeState. Cold construction grants
// no execution; independent OriginalExecution/1 requires an executable plan,
// Lifecycle11 and the original genuine queue/map completion boundary.
class WebGpuBindlessRootIssuerPreparation final : public rhi::OriginalBindlessRootExecution {
    friend class WebGpuCanonicalCallbackCustody;

    class ConstructionKey {
        friend class WebGpuCanonicalCallbackCustody;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Request = rhi::OriginalBindlessRootIssuerRequest;

    WebGpuBindlessRootIssuerPreparation(ConstructionKey,
        std::shared_ptr<const WebGpuBindlessRootCallbackCustody> custody,
        Request request,
        std::uint32_t owners) noexcept
        : custody_(std::move(custody)), request_(request), owners_(owners)
    {}

    static std::shared_ptr<const WebGpuBindlessRootIssuerPreparation> prepareOriginal(
        std::shared_ptr<const WebGpuBindlessRootCallbackCustody> custody,
        const Request& request,
        const char*& diagnostic)
    {
        return WebGpuCanonicalCallbackCustody::prepareRootIssuers(
            std::move(custody), request, diagnostic);
    }

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuBindlessRootIssuerPreparation>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-WEBGPU-ROOT-ISSUER-OWNER"
                                                              : self->validate();
    }

    const char* validate() const noexcept
    {
        return WebGpuCanonicalCallbackCustody::validateRootIssuers(*this);
    }

    bool issuersClosedForInspection() const noexcept
    {
        return WebGpuCanonicalCallbackCustody::rootIssuersClosed(*this);
    }

    std::uint32_t retainedOwnerCount() const noexcept { return owners_; }

    bool nativeExecutionProven() const noexcept { return false; }

    static const char* recordOriginal(
        const std::shared_ptr<const WebGpuBindlessRootIssuerPreparation>&,
        const rhi::BindlessRequirements::OriginalExecution&) noexcept;
    static const char* submitOriginal(
        const std::shared_ptr<const WebGpuBindlessRootIssuerPreparation>&,
        const rhi::BindlessRequirements::OriginalExecution&) noexcept;
    bool recordingProven() const noexcept override;
    bool submissionCompleted() const noexcept override;
    const char* executionDiagnostic() const noexcept override;

    const char* cancelExecution() const noexcept override { return custody_->cancelOriginal(); }

    std::uint32_t pendingRecords() const noexcept override
    {
        return custody_->pendingOriginalRecords();
    }

private:
    const char* recordExecution(
        const rhi::BindlessRequirements::OriginalExecution&) const noexcept override;
    const char* submitExecution(
        const rhi::BindlessRequirements::OriginalExecution&) const noexcept override;
    const std::shared_ptr<const WebGpuBindlessRootCallbackCustody> custody_;
    const Request request_;
    const std::uint32_t owners_;
};
} // namespace arp::rhi_webgpu
