#include "rhi/backends/webgpu/webgpu_native_ordinary_submission.h"

#include "rhi/backends/webgpu/webgpu_native_command_payload_impl.h"

namespace arp::rhi_webgpu {
WebGpuOrdinarySubmission::~WebGpuOrdinarySubmission()
{
    if (!canonicalMode_ && status_ == Status::Ready)
        recording_->submissionPrepared_ = false;
}

std::shared_ptr<WebGpuOrdinarySubmission> WebGpuOrdinarySubmission::prepare(
    std::shared_ptr<const WebGpuRecordedCommandProof> recording,
    const WebGpuOrdinarySubmissionBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    diagnostic = validateBudget(budget);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-SUBMISSION-QUOTA";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)recording;
    diagnostic = "RHI-WEBGPU-ORDINARY-SUBMISSION-NATIVE-UNAVAILABLE";
    return {};
#else
    if (!recording || !recording->hasQuotaBoundSharedOwner(recording)) {
        diagnostic = "RHI-WEBGPU-ORDINARY-SUBMISSION-RECORDING-OWNER";
        return {};
    }
    if ((diagnostic = recording->validateOriginalCommand()))
        return {};
    if ((diagnostic = recording->diagnostic()))
        return {};
    if (const auto& copies = recording->command_->originalReadbackCopies())
        if ((diagnostic = WebGpuOriginalTextureReadbackCopies::validateNativeRecorded(copies,
                 recording->identity_,
                 recording->completion_->owner()->nativeDevice(),
                 recording->command_->nativeQueue())))
            return {};
    if (recording->submissionPrepared_) {
        diagnostic = "RHI-WEBGPU-ORDINARY-SUBMISSION-DUPLICATE";
        return {};
    }
    if (!recording->hasRecordedCommand()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-SUBMISSION-UNRECORDED";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto unique =
            core::makeUnique<WebGpuOrdinarySubmission>(ConstructionKey{}, std::move(recording));
        for (std::uint32_t i = 0u; i < callbackCount; ++i) {
            unique->callbacks_[i] = core::makeUnique<Callback>();
            unique->callbacks_[i]->participant = i;
        }
        auto result = shareOriginal(std::move(unique));
        result->recording_->submissionPrepared_ = true;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-SUBMISSION-ALLOCATION";
        return {};
    }
#endif
}

const char* WebGpuOrdinarySubmission::submit(
    const std::shared_ptr<WebGpuOrdinarySubmission>& self) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-ORDINARY-SUBMISSION-OWNER";
    if (self->canonicalMode_)
        return "RHI-WEBGPU-CANONICAL-ISSUER-SUBMISSION-UNPROVEN";
    if (self->status() != Status::Ready)
        return "RHI-WEBGPU-ORDINARY-SUBMISSION-STATE";
    const auto& recording = self->recording_;
    if (!recording->hasQuotaBoundSharedOwner(recording))
        return "RHI-WEBGPU-ORDINARY-SUBMISSION-RECORDING-OWNER";
    if (const auto* id = recording->validateOriginalCommand())
        return id;
    if (const auto* id = recording->diagnostic())
        return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-WEBGPU-ORDINARY-SUBMISSION-NATIVE-UNAVAILABLE";
#else
    auto& state = *recording->command_->impl_;
    const auto& reservation = *recording->completion_;
    if (!state.finished || state.submitted || !state.commandBuffer || !state.queue || !state.device)
        return "RHI-WEBGPU-ORDINARY-SUBMISSION-ORIGINAL-COMMAND";
    const auto* originalGraph = reservation.owner()->retained.get(reservation.ticket());
    if (!originalGraph || !originalGraph->matches(reservation.owner()->nativeDevice()))
        return "RHI-WEBGPU-ORDINARY-COMPLETION-ORIGINAL-GRAPH";
    const auto& copies = state.ordinaryOriginalReadbackCopies;
    if (copies)
        if (const auto* id = WebGpuOriginalTextureReadbackCopies::validateNativeRecorded(
                copies, recording->identity_, state.device, state.queue))
            return id;
    const char* commitDiagnostic = nullptr;
    if (!reservation.owner()->retained.submitWith(reservation.ticket(), [&](auto& graph) noexcept {
            if (graph.command() != recording->command_
                || !graph.matches(reservation.owner()->nativeDevice()))
                return false;
            if (state.ordinaryOriginalStateTransaction)
                commitDiagnostic =
                    WebGpuOriginalStateTransaction::commit(state.ordinaryOriginalStateTransaction);
            if (state.ordinaryOriginalTextureTransaction)
                commitDiagnostic = WebGpuOriginalTextureUploadTransaction::markSubmitted(
                    state.ordinaryOriginalTextureTransaction);
            if (!commitDiagnostic && copies) {
                const auto ticket = reservation.ticket();
                commitDiagnostic = WebGpuOriginalTextureReadbackCopies::markNativeSubmitted(copies,
                    recording->identity_,
                    state.device,
                    state.queue,
                    ticket.index,
                    ticket.generation);
                if (commitDiagnostic)
                    graph.quarantineState();
            }
            return !commitDiagnostic;
        }))
        return commitDiagnostic ? commitDiagnostic : "RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED";
    self->callbacksIssued_ = true;
    self->status_ = Status::Pending;
    self->callbacks_[0]->owner = self;
    auto queueInfo = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    queueInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    queueInfo.callback = &completeQueue;
    queueInfo.userdata1 = self->callbacks_[0].release();
    WebGpuNativeErrorScopeBatch::push(state.device);
    wgpuQueueSubmit(state.queue, 1u, &state.commandBuffer);
    state.submitted = true;
    (void)wgpuQueueOnSubmittedWorkDone(state.queue, queueInfo);
    WebGpuNativeErrorScopeBatch::pop(state.device, self, self->callbacks_, 1u, &completeScope);
    return nullptr; // Pending is observable; receipt is not completion proof.
#endif
}

WebGpuOrdinarySubmission::Status WebGpuOrdinarySubmission::status() const noexcept
{
    if (canonicalMode_)
        return canonicalStatus();
    if (status_ == Status::Completed)
        return status_;
    return recording_->completion_->owner()->retained.closed() ? Status::DeviceLost : status_;
}

const char* WebGpuOrdinarySubmission::diagnostic() const noexcept
{
    if (canonicalMode_)
        return canonicalDiagnostic();
    switch (status()) {
    case Status::Ready:
        if (const auto* id = recording_->diagnostic())
            return id;
        return "RHI-WEBGPU-ORDINARY-SUBMISSION-UNSUBMITTED";
    case Status::Pending:
        return "RHI-WEBGPU-ORDINARY-SUBMISSION-PENDING";
    case Status::Completed:
        return nullptr;
    case Status::Unproven:
        return "RHI-WEBGPU-ORDINARY-COMPLETION-UNPROVEN";
    case Status::DeviceLost:
        return "RHI-WEBGPU-ORDINARY-VALIDATION-DEVICE-LOST";
    }
    return "RHI-WEBGPU-ORDINARY-SUBMISSION-STATE";
}

void WebGpuOrdinarySubmission::observe(std::uint32_t participant, bool proven) noexcept
{
    if (!callbacksIssued_)
        return;
    const bool accepted = evidence_.observe(participant, proven);
    if (status() != Status::Pending)
        return;
    if (!accepted || !proven) {
        const auto& reservation = *recording_->completion_;
        if (auto* graph = reservation.owner()->retained.get(reservation.ticket()))
            graph->quarantineState();
        reservation.owner()->retained.resolve(reservation.ticket(), false);
        status_ = Status::Unproven;
        return;
    }
    if (!evidence_.allReceived())
        return;
    const auto& reservation = *recording_->completion_;
    auto& pool = reservation.owner()->retained;
    auto* graph = pool.get(reservation.ticket());
    bool success =
        evidence_.proven() && graph && graph->matches(reservation.owner()->nativeDevice());
    // Only this branch owns actual queue plus all three successful submission
    // scopes, the exact retained ticket and its original finished native command.
    if (success)
        if (const auto& transaction = recording_->command_->originalTextureTransaction())
            success = !WebGpuOriginalTextureUploadTransaction::markCompleted(transaction);
    if (success)
        if (const auto& copies = recording_->command_->originalReadbackCopies()) {
            const auto ticket = reservation.ticket();
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            const auto& state = *recording_->command_->impl_;
            success = state.finished && state.submitted && state.commandBuffer
                && !WebGpuOriginalTextureReadbackCopies::markNativeCompleted(copies,
                    recording_->identity_,
                    state.device,
                    state.queue,
                    ticket.index,
                    ticket.generation);
#else
            (void)ticket;
            success = false;
#endif
        }
    success = success && graph->publishSignal();
    if (!success && graph)
        graph->quarantineState();
    if (!pool.resolve(reservation.ticket(), success)) {
        status_ = Status::Unproven;
        return;
    }
    status_ = success ? Status::Completed : Status::Unproven;
}
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
void WebGpuOrdinarySubmission::completeQueue(
    WGPUQueueWorkDoneStatus status, WGPUStringView, void* data, void*) noexcept
{
    auto* original = static_cast<Callback*>(data);
    core::UniquePtr<Callback> callback(original,
        core::CpuAllocatedDeleter<Callback>{original, &core::destroyCpuAllocatedUnique<Callback>});
    if (callback && callback->owner) {
        callback->owner->observe(callback->participant, status == WGPUQueueWorkDoneStatus_Success);
        core::runtime::CompletionDrainConsumer::consumeOnce(
            callback->owner->callbacksDrained(), callback->owner->readbackDrainConsumer_);
    }
}

void WebGpuOrdinarySubmission::completeScope(
    WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView, void* data, void*) noexcept
{
    auto* original = static_cast<Callback*>(data);
    core::UniquePtr<Callback> callback(original,
        core::CpuAllocatedDeleter<Callback>{original, &core::destroyCpuAllocatedUnique<Callback>});
    if (callback && callback->owner) {
        callback->owner->observe(callback->participant,
            status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_NoError);
        core::runtime::CompletionDrainConsumer::consumeOnce(
            callback->owner->callbacksDrained(), callback->owner->readbackDrainConsumer_);
    }
}
#endif
} // namespace arp::rhi_webgpu
