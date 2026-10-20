#pragma once
#include "rhi/backends/webgpu/webgpu_bindless_root_native_program.h"
#include "rhi/backends/webgpu/webgpu_canonical_readback_result_storage.h"
#include "rhi/backends/webgpu/webgpu_native_command_preparation.h"

namespace arp::rhi_webgpu {
// Opaque authentic shell for the original Root. Preparation opens no native
// encoder and grants no record/submit/map/completion authority.
class WebGpuBindlessRootColdCommand final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuNativeCommandPreparation;
    friend class WebGpuCanonicalCallbackCustody;
    friend class WebGpuOrdinaryRecording;
    friend class WebGpuOrdinarySubmission;
    using Admission = WebGpuBindlessRootColdAdmission;
    using Storage = WebGpuCanonicalReadbackResultStorage;
    using Contract = rhi::OriginalBindlessRootColdCommandContract;

    class ConstructionKey {
        friend class WebGpuNativeCommandPreparation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Request = Contract::Request;

    WebGpuBindlessRootColdCommand(ConstructionKey,
        std::shared_ptr<const Admission> admission,
        std::shared_ptr<const Storage> results,
        std::shared_ptr<WebGpuNativeCommandPayload> command,
        Request request,
        void* device,
        void* queue,
        std::uint64_t identity)
        : admission_(std::move(admission)), results_(std::move(results)),
          command_(std::move(command)), request_(request), device_(device), queue_(queue),
          identity_(identity)
    {
        if (request_.contractVersion == 2u)
            program_.emplace(
                admission_->originalReadbackBinding()->originalProgram()->operations().size());
    }

    static std::shared_ptr<const WebGpuBindlessRootColdCommand> prepareOriginal(
        std::shared_ptr<const Admission> admission,
        std::shared_ptr<const Storage> results,
        const Request& request,
        const char*& diagnostic,
        WebGpuRhiAllocator* allocator = nullptr)
    {
        return WebGpuNativeCommandPreparation::prepareBindlessRoot(
            std::move(admission), std::move(results), request, diagnostic, allocator);
    }

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuBindlessRootColdCommand>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-WEBGPU-ROOT-COLD-COMMAND-OWNER"
                                                              : self->validate();
    }

    const char* validate() const noexcept
    {
        return WebGpuNativeCommandPreparation::validateBindlessRoot(*this);
    }

    const auto& originalAdmission() const noexcept { return admission_; }

    const auto& originalResultStorage() const noexcept { return results_; }

    const auto& originalRequest() const noexcept { return request_; }

    const auto* originalNativeProgram() const noexcept { return program_ ? &*program_ : nullptr; }

    std::uint32_t retainedOwnerCount() const noexcept
    {
        return results_->retainedOwnerCount() + Contract::additionalOwners
            + (request_.program.pushUpload.empty()
                    ? 0u
                    : WebGpuBindlessRootNativeProgram::pushRetainedOwners);
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const std::shared_ptr<const Admission> admission_;
    const std::shared_ptr<const Storage> results_;
    const std::shared_ptr<WebGpuNativeCommandPayload> command_;
    const Request request_;
    void* const device_;
    void* const queue_;
    const std::uint64_t identity_;
    std::optional<WebGpuBindlessRootNativeProgram> program_;
    mutable bool callbackCustodyClaimed_ = false;
};
} // namespace arp::rhi_webgpu
