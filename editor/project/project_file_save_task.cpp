#include "editor/project/project_file_save_task.h"

#include "core/io/file_save_receipt_contract.h"

namespace arp::editor {
using namespace core::io;

struct ProjectFileSaveTask::State {
    core::io::FileSaveRequest request;
    core::runtime::CancellationSource cancellation;
    core::io::FileSaveResult result;
    bool submitted = false;
    bool terminal = false;
    bool cancelled = false;
    bool saveDone = false, receiptPending = false;
    bool observationOnly = false, observationSubmitted = false;
    FileSaveReceiptRequest receiptRequest;
    ProjectFileReceiptObservation observation;

    static void observed(void* context, FileSaveReceiptResult value) noexcept
    {
        using Retained = core::SharedPtr<State>;
        core::UniquePtr<Retained> owner{static_cast<Retained*>(context),
            core::CpuAllocatedDeleter<Retained>{
                context, &core::destroyCpuAllocatedUnique<Retained>}};
        auto& state = **owner;
        state.receiptPending = false;
        state.observation.result = value;
        if (state.observationOnly) {
            if (value.state == FileSaveReceiptState::Terminal)
                state.result = value.outcome;
            state.terminal = true;
            return;
        }
        if (state.receiptRequest.operation == FileSaveReceiptOperation::Query
            && value.status == FileIoStatus::Success
            && value.state == FileSaveReceiptState::Terminal
            && value.outcome.commitState != FileCommitState::OutcomeUnknown) {
            const auto originalDelivery = state.result.deliveryStatus;
            state.result = value.outcome;
            if (originalDelivery != FileIoStatus::Success)
                state.result.deliveryStatus = originalDelivery;
            else if (value.deliveryStatus != FileIoStatus::Success)
                state.result.deliveryStatus = value.deliveryStatus;
            // Only the next owner-thread advance can submit Release.
            return;
        }
        // Failed/unavailable/pending observations cannot prove a rollback. A
        // Release response never replaces the already observed save outcome.
        state.terminal = true;
    }
};

ProjectFileSaveTask::ProjectFileSaveTask(
    core::io::FileService& files, core::io::FileSaveRequest request)
    : files_(files), state_(core::makeShared<State>())
{
    state_->receiptRequest.pathScope = request.pathScope;
    state_->receiptRequest.mount = request.mount;
    state_->receiptRequest.maxResultBytes = sizeof(FileSaveReceiptResult);
    state_->request = std::move(request);
    state_->request.cancellation = state_->cancellation.token();
}

ProjectFileSaveTask::ProjectFileSaveTask(FileService& files, FileSaveReceiptRequest request)
    : files_(files), state_(core::makeShared<State>())
{
    state_->observationOnly = state_->submitted = state_->saveDone = true;
    state_->result.commitState = FileCommitState::OutcomeUnknown;
    state_->result.receipt = request.receipt;
    request.cancellation = state_->cancellation.token();
    state_->receiptRequest = std::move(request);
    const auto validation = FileSaveReceiptContract::validate(state_->receiptRequest);
    if (validation.diagnostic) {
        state_->observation.result.status = validation.status;
        state_->observation.result.diagnosticId = validation.diagnostic;
        state_->terminal = true;
    }
}

ProjectFileSaveTask::~ProjectFileSaveTask()
{
    if (!state_->terminal)
        cancel();
}

void ProjectFileSaveTask::complete(void* context, core::io::FileSaveResult result) noexcept
{
    using Retained = core::SharedPtr<State>;
    core::UniquePtr<Retained> owner{static_cast<Retained*>(context),
        core::CpuAllocatedDeleter<Retained>{context, &core::destroyCpuAllocatedUnique<Retained>}};
    (*owner)->result = result;
    (*owner)->saveDone = true;
    (*owner)->terminal = !result.receipt.valid();
}

void ProjectFileSaveTask::advance() noexcept
{
    if (state_->terminal)
        return;
    if (state_->submitted) {
        auto& state = *state_;
        if (!state.saveDone || state.receiptPending)
            return;
        const bool unknown = state.result.commitState == FileCommitState::OutcomeUnknown;
        if (state.observationOnly && state.cancelled) {
            state.observation.result.status = FileIoStatus::Cancelled;
            state.observation.result.diagnosticId = "EDITOR-FILE-RECEIPT-CANCELLED";
            state.terminal = true;
            return;
        }
        if (!state.observationOnly
            && ((unknown && (state.observation.queryAttempted || state.cancelled))
                || (!unknown && state.observation.releaseAttempted))) {
            state.terminal = true;
            return;
        }
        auto& observed = state.observation;
        state.receiptRequest.receipt = state.result.receipt;
        if (!state.observationOnly)
            state.receiptRequest.operation =
                unknown ? FileSaveReceiptOperation::Query : FileSaveReceiptOperation::Release;
        if (state.receiptRequest.operation == FileSaveReceiptOperation::Query)
            observed.queryAttempted = true;
        else
            observed.releaseAttempted = true;
        try {
            auto retained = core::makeUnique<core::SharedPtr<State>>(state_);
            const auto status =
                files_.receipt(state.receiptRequest, {State::observed, retained.get()});
            if (status == FileIoStatus::Accepted) {
                state.receiptPending = true;
                state.observationSubmitted = true;
                retained.release();
                return;
            }
            observed.result.status = status;
            observed.result.diagnosticId = "EDITOR-FILE-SAVE-RECEIPT-NOT-ACCEPTED";
        } catch (...) {
            observed.result.status = FileIoStatus::BudgetExceeded;
            observed.result.diagnosticId = "EDITOR-FILE-SAVE-RECEIPT-PREPARATION";
        }
        state.terminal = true;
        return;
    }
    if (state_->cancelled) {
        reject(core::io::FileIoStatus::Cancelled, "EDITOR-FILE-SAVE-CANCELLED");
        return;
    }
    try {
        auto retained = core::makeUnique<core::SharedPtr<State>>(state_);
        const auto status = files_.save(std::move(state_->request), {complete, retained.get()});
        if (status == core::io::FileIoStatus::Accepted) {
            state_->submitted = true;
            retained.release();
        } else
            reject(status, "EDITOR-FILE-SAVE-REQUEST-REJECTED");
    } catch (...) {
        reject(core::io::FileIoStatus::BudgetExceeded, "EDITOR-FILE-SAVE-PREPARATION");
    }
}

void ProjectFileSaveTask::cancel() noexcept
{
    state_->cancelled = true;
    state_->cancellation.cancel();
}

void ProjectFileSaveTask::reject(core::io::FileIoStatus status, const char* diagnostic) noexcept
{
    if (state_->terminal)
        return;
    if (state_->submitted) {
        cancel();
        return;
    }
    state_->result.fail(status, diagnostic);
    state_->terminal = true;
    core::io::FileSaveRequest{}.bytes.swap(state_->request.bytes);
}

bool ProjectFileSaveTask::submitted() const noexcept
{
    return state_->observationOnly ? state_->observationSubmitted : state_->submitted;
}

bool ProjectFileSaveTask::terminal() const noexcept
{
    return state_->terminal;
}

bool ProjectFileSaveTask::cancellationRequested() const noexcept
{
    return state_->cancelled;
}

const core::io::FileSaveResult& ProjectFileSaveTask::result() const noexcept
{
    return state_->result;
}

const ProjectFileReceiptObservation& ProjectFileSaveTask::receiptObservation() const noexcept
{
    return state_->observation;
}
} // namespace arp::editor
