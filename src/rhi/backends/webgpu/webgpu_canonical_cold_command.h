#pragma once

#include "rhi/backends/webgpu/webgpu_canonical_cold_admission.h"
#include "rhi/backends/webgpu/webgpu_native_command_preparation.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/semantics/command_lowering.h"

namespace arp::rhi_webgpu {
// Runtime-owned opaque native command custody. No native handle, mutable command
// payload, callback receiver, record/submit API or recorded proof is exposed.
// Cold publication and destruction are externally serialized per Source/seal.
class WebGpuCanonicalColdCommand final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuNativeCommandPreparation;
    friend class WebGpuCanonicalCallbackCustody;
    friend class WebGpuOrdinaryRecording;
    friend class WebGpuOrdinarySubmission;
    using Admission = WebGpuCanonicalColdAdmission;
    using Contract = rhi::OriginalCommandNativeColdCommandContract;

    class ConstructionKey {
        friend class WebGpuNativeCommandPreparation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Budget = rhi::OriginalCommandNativeColdCommandBudget;

    WebGpuCanonicalColdCommand(ConstructionKey,
        std::shared_ptr<const Admission> admission,
        std::shared_ptr<WebGpuNativeCommandPayload> command,
        Budget budget,
        void* device,
        void* queue,
        std::uint64_t identity) noexcept
        : admission_(std::move(admission)), command_(std::move(command)), budget_(budget),
          device_(device), queue_(queue), identity_(identity)
    {}

    static std::shared_ptr<const WebGpuCanonicalColdCommand> prepareOriginal(
        std::shared_ptr<const Admission> admission, const Budget& budget, const char*& diagnostic)
    {
        return WebGpuNativeCommandPreparation::prepareCanonical(
            std::move(admission), budget, diagnostic);
    }

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuCanonicalColdCommand>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self)
            ? "RHI-WEBGPU-CANONICAL-COLD-COMMAND-OWNER"
            : self->validate();
    }

    const char* validate() const noexcept;

    const auto& originalAdmission() const noexcept { return admission_; }

    std::uint32_t retainedOwnerCount() const noexcept
    {
        return admission_->retainedOwnerCount() + Contract::additionalOwnerCount;
    }

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return arp::rhi::semantics::OriginalCommandNativeColdCommandLowering::lowering(
            rhi::Backend::WebGPU)
            .diagnostic;
    }

private:
    // Command and its native queue/device references die before the root/Source.
    const std::shared_ptr<const Admission> admission_;
    const std::shared_ptr<WebGpuNativeCommandPayload> command_;
    const Budget budget_;
    void* const device_;
    void* const queue_;
    const std::uint64_t identity_;
    mutable bool callbackCustodyClaimed_ = false;
};
} // namespace arp::rhi_webgpu
