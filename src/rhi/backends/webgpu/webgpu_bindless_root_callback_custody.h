#pragma once
#include "rhi/backends/webgpu/webgpu_bindless_root_cold_command.h"
#include "rhi/backends/webgpu/webgpu_canonical_callback_custody.h"

namespace arp::rhi_webgpu {
// Reuses the canonical pool/gates/native loss and map receipt adapters. Only
// cold construction grants retention only. Independent live issuers reuse this
// state; no caller receives native handles or a callback-delivery API.
class WebGpuBindlessRootCallbackCustody final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuBindlessRootIssuerPreparation;
    friend class WebGpuCanonicalCallbackCustody;
    using State = WebGpuCanonicalCallbackCustody::RuntimeState<WebGpuBindlessRootColdCommand>;

    class ConstructionKey {
        friend class WebGpuCanonicalCallbackCustody;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Request = rhi::OriginalBindlessRootCallbackCustodyRequest;
    WebGpuBindlessRootCallbackCustody(
        ConstructionKey, std::shared_ptr<State> state, Request request) noexcept;
    ~WebGpuBindlessRootCallbackCustody();
    static std::shared_ptr<const WebGpuBindlessRootCallbackCustody> prepareOriginal(
        std::shared_ptr<const WebGpuBindlessRootColdCommand>, const Request&, const char*&);

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuBindlessRootCallbackCustody>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self)
            ? "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-OWNER"
            : self->validate();
    }

    const char* validate() const noexcept;
    const char* cancelOriginal() const noexcept;
    bool originalNativeLossArmed() const noexcept;
    std::uint32_t pendingOriginalRecords() const noexcept;
    std::uint32_t retainedOwnerCount() const noexcept;

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const std::shared_ptr<State> state_;
    const Request request_;
};
} // namespace arp::rhi_webgpu
