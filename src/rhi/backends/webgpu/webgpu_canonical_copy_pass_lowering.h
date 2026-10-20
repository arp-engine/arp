#pragma once

#include "rhi/backends/webgpu/webgpu_canonical_callback_custody.h"
#include "rhi/frontend/command_transfer_validation.h"
#include "rhi/semantics/command_lowering.h"

namespace arp::test {
class WebGpuCanonicalCopyPassLoweringFixture;
}

namespace arp::rhi_webgpu {
// Runtime-owned immutable projection. Native input data remains backend-private
// and expires before its exact retained original graph is released.
class WebGpuCanonicalCopyPassLowering final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuCanonicalCallbackCustody;
    friend class WebGpuOrdinaryRecording;
    friend class WebGpuOrdinarySubmission;
    friend class arp::test::WebGpuCanonicalCopyPassLoweringFixture;
    class Payload;

    class ConstructionKey {
        friend class WebGpuCanonicalCallbackCustody;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Budget = rhi::OriginalCommandCopyPassLoweringBudget;

    WebGpuCanonicalCopyPassLowering(ConstructionKey,
        std::shared_ptr<const WebGpuCanonicalCallbackCustody> custody,
        Budget budget,
        std::uint32_t copies,
        std::uint32_t owners) noexcept
        : custody_(std::move(custody)), budget_(budget), copies_(copies), owners_(owners)
    {}

    static std::shared_ptr<const WebGpuCanonicalCopyPassLowering> prepareOriginal(
        std::shared_ptr<const WebGpuCanonicalCallbackCustody>,
        const Budget&,
        const char*& diagnostic);

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuCanonicalCopyPassLowering>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self)
            ? "RHI-WEBGPU-CANONICAL-LOWERING-OWNER"
            : self->validate();
    }

    const char* validate() const noexcept;

    const auto& originalCustody() const noexcept { return custody_; }

    std::uint32_t projectedCopyCount() const noexcept { return copies_; }

    std::uint32_t projectedUploadCopyCount() const noexcept;
    std::uint32_t projectedReadbackCopyCount() const noexcept;

    std::uint32_t retainedOwnerCount() const noexcept { return owners_; }

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return arp::rhi::semantics::OriginalCommandCopyPassLowering::lowering(
            rhi::Backend::WebGPU, budget_.contractVersion)
            .diagnostic;
    }

private:
    bool matchesProjectedCopyForInspection(
        std::size_t, const rhi::TransferCopyArguments&) const noexcept;
    bool matchesProjectedUploadForInspection(
        std::size_t, std::size_t, std::size_t, const rhi::TransferCopyArguments&) const noexcept;
    bool matchesProjectedReadbackForInspection(
        std::size_t, std::size_t, std::size_t, const rhi::TransferCopyArguments&) const noexcept;
    const std::shared_ptr<const WebGpuCanonicalCallbackCustody> custody_;
    const Budget budget_;
    const std::uint32_t copies_, owners_;
};
} // namespace arp::rhi_webgpu
