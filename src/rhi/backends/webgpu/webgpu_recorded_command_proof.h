#pragma once

#include "core/quota_bound_shared_identity.h"
#include "rhi/backends/webgpu/webgpu_ordinary_completion_retention.h"

namespace arp::rhi_webgpu {

// WebRecordedCommandProof/1. No public construction or metadata-to-proof path.
// Concrete recorders validate original owners and real native recording scopes.
class WebGpuRecordedCommandProof : public core::QuotaBoundSharedObjectIdentity {
public:
    virtual ~WebGpuRecordedCommandProof() = default;
    virtual const char* diagnostic() const noexcept = 0;
    virtual bool hasRecordedCommand() const noexcept = 0;
    WebGpuRecordedCommandProof(const WebGpuRecordedCommandProof&) = delete;
    WebGpuRecordedCommandProof& operator=(const WebGpuRecordedCommandProof&) = delete;

protected:
    const char* validateOriginalCommand() const noexcept
    {
        if (!completion_ || !completion_->owner()
            || !completion_->owner()->sharedOwnerMatches(completion_->owner())
            || !completion_->owner()->retained.accepting())
            return "RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED";
        const auto* graph = completion_->owner()->retained.get(completion_->ticket());
        if (!command_ || !command_->sharedOwnerMatches(command_) || !graph
            || graph->command() != command_ || !graph->matches(completion_->owner()->nativeDevice())
            || command_->recordingIdentity() != identity_)
            return "RHI-WEBGPU-ORDINARY-RECORDING-ORIGINAL-COMMAND";
        return nullptr;
    }

    const std::shared_ptr<WebGpuNativeCommandPayload> command_;
    const std::uint64_t identity_;
    core::UniquePtr<WebGpuOrdinaryCompletionReservation> completion_;

private:
    friend class WebGpuOrdinaryRecording;
    friend class WebGpuOrdinarySubmission;

    explicit WebGpuRecordedCommandProof(std::shared_ptr<WebGpuNativeCommandPayload> command)
        : command_(std::move(command)), identity_(command_->recordingIdentity())
    {}

    mutable bool submissionPrepared_ = false;
};
} // namespace arp::rhi_webgpu
