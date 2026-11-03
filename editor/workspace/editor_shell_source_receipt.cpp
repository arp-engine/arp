#include "core/io/file_save_receipt_contract.h"
#include "editor/workspace/editor_shell.h"

namespace arp::editor {
using namespace core::io;

bool EditorShell::sourceReceiptPending() const noexcept
{
    return sourceReceiptTaskStatus_ == ProjectIoStatus::Pending;
}

EditorCommandOutcome EditorShell::observeSourceReceipt(const SourceFileReceiptCommand& command)
{
    if (!projectSourceFiles_ || command.source != projectSourceAlias_ || !projectSourceMount_
        || sourceReceiptPending())
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    FileSaveReceiptRequest request;
    request.pathScope = FilePathScope::MountedResource;
    request.mount = projectSourceMount_;
    request.receipt = command.receipt;
    request.operation = command.operation;
    request.maxResultBytes = sizeof(FileSaveReceiptResult);
    if (FileSaveReceiptContract::validate(request).diagnostic)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    if (!projectSourceFiles_->saveCapability(projectSourceMount_).operationReceipts)
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    try {
        auto task = core::makeUnique<ProjectFileSaveTask>(*projectSourceFiles_, request);
        if (task->terminal())
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        const auto id = projectIoIds_.tryAcquire();
        if (!id)
            return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
        EditorCommandOutcome result;
        result.hasSnapshot = true;
        appendProjectSnapshot(result.snapshot);
        result.snapshot.sourceReceiptTaskId = id;
        result.snapshot.sourceReceiptTaskStatus = ProjectIoStatus::Pending;
        result.snapshot.sourceReceiptSubmitted = false;
        result.snapshot.sourceReceiptOperation = command.operation;
        result.snapshot.sourceReceiptIdentity = command.receipt;
        result.snapshot.sourceReceiptResult = {};
        // No IO until a subsequent internal owner-thread advance. All fields
        // below are prepared values; the callback retains no live Shell owner.
        sourceReceiptTask_ = std::move(task);
        sourceReceiptTaskId_ = id;
        sourceReceiptSubmitted_ = false;
        sourceReceiptResult_ = {};
        sourceReceiptTaskStatus_ = ProjectIoStatus::Pending;
        sourceReceiptOperation_ = command.operation;
        sourceReceiptIdentity_ = command.receipt;
        result.stateChanged = true;
        return result;
    } catch (...) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}

EditorCommandOutcome EditorShell::advanceSourceReceipt(std::uint64_t id)
{
    if (!id || id != sourceReceiptTaskId_ || !sourceReceiptPending() || !sourceReceiptTask_)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    const auto submitted = sourceReceiptTask_->submitted();
    sourceReceiptTask_->advance();
    sourceReceiptSubmitted_ = sourceReceiptTask_->submitted();
    sourceReceiptResult_ = sourceReceiptTask_->receiptObservation().result;
    if (sourceReceiptTask_->terminal()) {
        const auto& value = sourceReceiptTask_->receiptObservation().result;
        sourceReceiptTaskStatus_ = sourceReceiptTask_->cancellationRequested()
            ? ProjectIoStatus::Cancelled
            : value.status == FileIoStatus::Success && value.deliveryStatus == FileIoStatus::Success
            ? ProjectIoStatus::Succeeded
            : ProjectIoStatus::Failed;
        return {true, true, EditorDiagnosticCode::None};
    }
    return {true, submitted != sourceReceiptTask_->submitted(), EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::cancelSourceReceipt(std::uint64_t id)
{
    if (!id || id != sourceReceiptTaskId_ || !sourceReceiptPending() || !sourceReceiptTask_)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    const bool changed = !sourceReceiptTask_->cancellationRequested();
    sourceReceiptTask_->cancel();
    auto result = advanceSourceReceipt(id);
    result.stateChanged = result.stateChanged || changed;
    return result;
}
} // namespace arp::editor
