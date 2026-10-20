#pragma once

#include "rhi/backends/webgpu/webgpu_native_command_preparation.h"
#include "rhi/backends/webgpu/webgpu_original_typed_cold_admission.h"
#include "rhi/frontend/command_shader_validation.h"
#include "rhi/semantics/command_lowering.h"

namespace arp::rhi_webgpu {
// Opaque connection custody; exposes no command/handle/callback or proof API.
class WebGpuOriginalTypedColdCommand final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuNativeCommandPreparation;
    friend class WebGpuCanonicalCallbackCustody;
    friend class WebGpuOrdinaryRecording;
    friend class WebGpuOrdinarySubmission;
    using Admission = WebGpuOriginalTypedColdAdmission;
    using Contract = rhi::OriginalTypedNativeColdCommandContract;

    class ConstructionKey {
        friend class WebGpuNativeCommandPreparation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Request = Contract::Request;

    WebGpuOriginalTypedColdCommand(ConstructionKey,
        std::shared_ptr<const Admission> admission,
        std::shared_ptr<WebGpuNativeCommandPayload> command,
        Request request,
        void* device,
        void* queue,
        std::uint64_t identity) noexcept
        : admission_(std::move(admission)), command_(std::move(command)), request_(request),
          device_(device), queue_(queue), identity_(identity)
    {}

    static std::shared_ptr<const WebGpuOriginalTypedColdCommand> prepareOriginal(
        std::shared_ptr<const Admission> admission, const Request& request, const char*& diagnostic)
    {
        return WebGpuNativeCommandPreparation::prepareTypedOriginal(
            std::move(admission), request, diagnostic);
    }

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuOriginalTypedColdCommand>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self)
            ? "RHI-WEBGPU-TYPED-COLD-COMMAND-OWNER"
            : self->validate();
    }

    const char* validate() const noexcept
    {
        return WebGpuNativeCommandPreparation::validateTypedOriginal(*this);
    }

    const auto& originalAdmission() const noexcept { return admission_; }

    const Request& originalRequest() const noexcept { return request_; }

    std::uint32_t retainedOwnerCount() const noexcept
    {
        return admission_->retainedOwnerCount() + Contract::additionalOwners;
    }

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return arp::rhi::semantics::OriginalTypedNativeColdCommandLowering::lowering(
            rhi::Backend::WebGPU, request_.contractVersion)
            .diagnostic;
    }

private:
    const std::shared_ptr<const Admission> admission_;
    const std::shared_ptr<WebGpuNativeCommandPayload> command_;
    const Request request_;
    void* const device_;
    void* const queue_;
    const std::uint64_t identity_;
};
} // namespace arp::rhi_webgpu
