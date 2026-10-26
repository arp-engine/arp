#include "editor/command/editor_command.h"

#include <new>
#include <type_traits>

namespace arp::editor {
namespace {
// Keep the original result in the cache even when copying its dynamic payload
// is refused. This receipt reports the actual outcome, never a fake rollback.
EditorResponse copyReceipt(const EditorResponse& stored, std::uint64_t correlationId)
{
    try {
        auto response = stored;
        response.correlationId = correlationId;
        return response;
    } catch (const std::bad_alloc&) {
        EditorResponse receipt;
        receipt.schemaVersion = stored.schemaVersion;
        receipt.commandId = stored.commandId;
        receipt.correlationId = correlationId;
        receipt.stateVersion = stored.stateVersion;
        receipt.hasStateVersion = stored.hasStateVersion;
        receipt.status = stored.status;
        receipt.diagnostic = EditorDiagnosticCode::ResponsePayloadUnavailable;
        return receipt;
    }
}

// Aggregate initialization move-constructs PMR owners, preserving even a
// nondefault resource. Move assignment could allocate for unequal resources.
EditorResponse completedResponse(std::uint64_t commandId,
    std::uint64_t correlationId,
    std::uint64_t stateVersion,
    EditorCommandOutcome&& outcome) noexcept
{
    static_assert(std::is_nothrow_move_constructible_v<EditorResponse>);
    static_assert(std::is_nothrow_move_constructible_v<EditorCommandOutcome>);
    return {1,
        commandId,
        correlationId,
        stateVersion,
        true,
        outcome.success ? EditorResponseStatus::Completed : EditorResponseStatus::Failed,
        outcome.diagnostic,
        outcome.hasSnapshot,
        std::move(outcome.snapshot),
        outcome.hasSceneTransformSettings,
        outcome.sceneTransformSettings,
        outcome.hasSceneInspector,
        std::move(outcome.sceneInspector),
        outcome.hasSceneInspectorSelection,
        std::move(outcome.sceneInspectorSelection),
        outcome.hasSceneHierarchy,
        std::move(outcome.sceneHierarchy),
        outcome.hasSceneComponentCatalog,
        std::move(outcome.sceneComponentCatalog)};
}
} // namespace

EditorCommandOutcome EditorResponse::intoNestedOutcome() && noexcept
{
    static_assert(std::is_nothrow_move_constructible_v<EditorCommandOutcome>);
    return {status == EditorResponseStatus::Completed,
        false,
        diagnostic,
        hasSnapshot,
        std::move(snapshot),
        hasSceneTransformSettings,
        sceneTransformSettings,
        hasSceneInspector,
        std::move(sceneInspector),
        hasSceneInspectorSelection,
        std::move(sceneInspectorSelection),
        hasSceneHierarchy,
        std::move(sceneHierarchy),
        hasSceneComponentCatalog,
        std::move(sceneComponentCatalog)};
}

EditorCommandDispatcher::EditorCommandDispatcher(std::size_t idempotencyCapacity)
    : idempotencyCapacity_(idempotencyCapacity)
{}

EditorResponse EditorCommandDispatcher::dispatch(
    const EditorCommandEnvelope& command, IEditorCommandExecutor& executor)
{
    EditorResponse response;
    response.commandId = command.commandId != 0 ? command.commandId : nextCommandId_.fetch_add(1);
    response.correlationId = command.correlationId;

    if (std::this_thread::get_id() != ownerThread_) {
        response.status = EditorResponseStatus::Rejected;
        response.diagnostic = EditorDiagnosticCode::WrongOwnerThread;
        return response;
    }
    response.stateVersion = stateVersion_;
    response.hasStateVersion = true;
    if (command.schemaVersion != EditorCommandEnvelope::currentVersion) {
        response.status = EditorResponseStatus::Rejected;
        response.diagnostic = EditorDiagnosticCode::UnsupportedCommandVersion;
        return response;
    }
    const bool retainIdempotency = !std::holds_alternative<GetEditorStateQuery>(command.payload)
        && !std::holds_alternative<GetSceneTransformSettingsQuery>(command.payload)
        && !std::holds_alternative<GetScenePersistenceTaskQuery>(command.payload)
        && !std::holds_alternative<GetSceneExportTaskQuery>(command.payload)
        && !std::holds_alternative<GetSceneInspectorQuery>(command.payload)
        && !std::holds_alternative<GetSceneInspectorSelectionQuery>(command.payload)
        && !std::holds_alternative<GetSceneHierarchyQuery>(command.payload)
        && !std::holds_alternative<GetSceneComponentCatalogQuery>(command.payload);
    if (!command.idempotencyKey.empty()) {
        if (command.idempotencyFingerprint.empty()) {
            response.status = EditorResponseStatus::Rejected;
            response.diagnostic = EditorDiagnosticCode::UnsupportedIdempotency;
            return response;
        }
        for (const auto& record : idempotencyRecords_) {
            if (record->key != command.idempotencyKey)
                continue;
            if (record->fingerprint != command.idempotencyFingerprint) {
                response.status = EditorResponseStatus::Rejected;
                response.diagnostic = EditorDiagnosticCode::IdempotencyKeyConflict;
                return response;
            }
            return copyReceipt(*record->response, command.correlationId);
        }
        if (retainIdempotency && idempotencyCapacity_ == 0) {
            response.status = EditorResponseStatus::Rejected;
            response.diagnostic = EditorDiagnosticCode::IdempotencyCapacityExceeded;
            return response;
        }
    }
    if (command.hasExpectedStateVersion && command.expectedStateVersion != stateVersion_) {
        response.status = EditorResponseStatus::Rejected;
        response.diagnostic = EditorDiagnosticCode::StaleStateVersion;
        return response;
    }
    core::UniquePtr<IdempotencyRecord> pending;
    if (!command.idempotencyKey.empty() && retainIdempotency) {
        try {
            // Reserve the explicit whole bound: nested owner-thread commands
            // may consume slots while the outer executor is running.
            if (idempotencyCapacity_ > idempotencyRecords_.max_size())
                throw std::bad_alloc();
            idempotencyRecords_.reserve(idempotencyCapacity_);
            pending = core::makeUnique<IdempotencyRecord>(&core::defaultCpuMemoryResource());
            pending->key = command.idempotencyKey;
            pending->fingerprint = command.idempotencyFingerprint;
        } catch (const std::bad_alloc&) {
            response.status = EditorResponseStatus::Rejected;
            response.diagnostic = EditorDiagnosticCode::CommandPreparationFailed;
            return response;
        }
    }
    auto outcome = executor.execute(command.payload);
    if (outcome.success && outcome.stateChanged)
        ++stateVersion_;
    auto completed = completedResponse(
        response.commandId, command.correlationId, stateVersion_, std::move(outcome));
    if (pending) {
        pending->response.emplace(std::move(completed));
        if (idempotencyRecords_.size() >= idempotencyCapacity_)
            idempotencyRecords_.erase(idempotencyRecords_.begin());
        idempotencyRecords_.push_back(std::move(pending));
        return copyReceipt(*idempotencyRecords_.back()->response, command.correlationId);
    }
    return completed;
}
} // namespace arp::editor
