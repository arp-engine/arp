#include "editor/command/editor_command.h"

namespace arp::editor {
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
        && !std::holds_alternative<GetScenePersistenceTaskQuery>(command.payload)
        && !std::holds_alternative<GetSceneInspectorQuery>(command.payload)
        && !std::holds_alternative<GetSceneHierarchyQuery>(command.payload)
        && !std::holds_alternative<GetSceneComponentCatalogQuery>(command.payload);
    if (!command.idempotencyKey.empty()) {
        if (command.idempotencyFingerprint.empty()) {
            response.status = EditorResponseStatus::Rejected;
            response.diagnostic = EditorDiagnosticCode::UnsupportedIdempotency;
            return response;
        }
        for (const auto& record : idempotencyRecords_) {
            if (record.key != command.idempotencyKey)
                continue;
            if (record.fingerprint != command.idempotencyFingerprint) {
                response.status = EditorResponseStatus::Rejected;
                response.diagnostic = EditorDiagnosticCode::IdempotencyKeyConflict;
                return response;
            }
            response = record.response;
            response.correlationId = command.correlationId;
            return response;
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
    const auto outcome = executor.execute(command.payload);
    response.stateVersion = stateVersion_;
    response.diagnostic = outcome.diagnostic;
    response.hasSnapshot = outcome.hasSnapshot;
    if (outcome.hasSnapshot)
        response.snapshot = outcome.snapshot;
    response.hasSceneInspector = outcome.hasSceneInspector;
    if (outcome.hasSceneInspector)
        response.sceneInspector = outcome.sceneInspector;
    response.hasSceneHierarchy = outcome.hasSceneHierarchy;
    if (outcome.hasSceneHierarchy)
        response.sceneHierarchy = outcome.sceneHierarchy;
    response.hasSceneComponentCatalog = outcome.hasSceneComponentCatalog;
    if (outcome.hasSceneComponentCatalog)
        response.sceneComponentCatalog = outcome.sceneComponentCatalog;
    if (!outcome.success) {
        response.status = EditorResponseStatus::Failed;
    } else {
        if (outcome.stateChanged)
            response.stateVersion = ++stateVersion_;
        response.status = EditorResponseStatus::Completed;
    }
    if (!command.idempotencyKey.empty() && retainIdempotency) {
        if (idempotencyRecords_.size() >= idempotencyCapacity_)
            idempotencyRecords_.erase(idempotencyRecords_.begin());
        IdempotencyRecord record(&core::defaultCpuMemoryResource());
        record.key = command.idempotencyKey;
        record.fingerprint = command.idempotencyFingerprint;
        record.response = response;
        idempotencyRecords_.push_back(std::move(record));
    }
    return response;
}
} // namespace arp::editor
