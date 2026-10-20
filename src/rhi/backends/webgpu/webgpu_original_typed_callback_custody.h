#pragma once

#include "rhi/backends/webgpu/webgpu_canonical_callback_custody.h"
#include "rhi/backends/webgpu/webgpu_original_typed_cold_command.h"
#include "rhi/frontend/command_shader_validation.h"
#include "rhi/semantics/command_lowering.h"

namespace arp::test {
class OriginalTypedCallbackCustodyFixture;
class WebGpuPostSuppressionFailureProbeFixture;
} // namespace arp::test

namespace arp::rhi_webgpu {
// Independent typed wrapper over the existing custody State algorithms/pool.
// Exposes no receiver, native handle, command recorder or proof authority.
class WebGpuOriginalTypedCallbackCustody final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuCanonicalCallbackCustody;
    friend class WebGpuOriginalTypedIssuerPreparation;
    friend class arp::test::OriginalTypedCallbackCustodyFixture;
    friend class arp::test::WebGpuPostSuppressionFailureProbeFixture;
    using State = WebGpuCanonicalCallbackCustody::RuntimeState<WebGpuOriginalTypedColdCommand>;

    class ConstructionKey {
        friend class WebGpuCanonicalCallbackCustody;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Request = rhi::OriginalTypedCallbackCustodyRequest;
    WebGpuOriginalTypedCallbackCustody(ConstructionKey, std::shared_ptr<State>, Request) noexcept;
    ~WebGpuOriginalTypedCallbackCustody();
    static std::shared_ptr<const WebGpuOriginalTypedCallbackCustody> prepareOriginal(
        std::shared_ptr<const WebGpuOriginalTypedColdCommand>,
        const Request&,
        const char*& diagnostic,
        std::shared_ptr<const WebGpuCanonicalReadbackResultStorage> results = {},
        std::optional<WebGpuCanonicalCallbackCustody::PublicationIntent> publication =
            std::nullopt);

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuOriginalTypedCallbackCustody>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-WEBGPU-TYPED-CALLBACK-OWNER"
                                                              : self->validate();
    }

    const char* validate() const noexcept;
    const char* cancelOriginal() const noexcept;
    std::uint32_t pendingOriginalRecords() const noexcept;
    bool physicalLossArmed() const noexcept;
    bool physicalLossDelivered() const noexcept;
    bool overlapsCpuOwner(const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept;

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        if (request_.readbackMap.contractVersion)
            return arp::rhi::semantics::OriginalTypedReadbackMapCustodyLowering::lowering(
                rhi::Backend::WebGPU, request_.readbackMap.contractVersion)
                .diagnostic;
        return arp::rhi::semantics::OriginalTypedCallbackCustodyLowering::lowering(
            rhi::Backend::WebGPU, request_.contractVersion)
            .diagnostic;
    }

private:
    std::weak_ptr<const void> stateForInspection() const noexcept { return state_; }

    const std::shared_ptr<State> state_;
    const Request request_;
};
} // namespace arp::rhi_webgpu
