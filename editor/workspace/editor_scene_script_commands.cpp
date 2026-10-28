#include "editor/workspace/editor_scene_script_policy.h"
#include "editor/workspace/editor_shell.h"

#include <algorithm>
#include <stdexcept>

namespace arp::editor {
namespace {
void require(bool condition)
{
    if (!condition)
        throw std::invalid_argument("EDITOR-SCENE-SCRIPT-COMMAND-REJECTED");
}
} // namespace

// Cold command admission only. Ordinary frame/hash/Inspector observation must
// never call this disk-source check. Presence and field commands share it.
void EditorShell::requireSceneScriptContext() const
{
    require(sceneAuthoringVersion_ == EditorSceneScriptCodec::contractVersion && projectManifest_);
    const auto catalog = sceneDocument_.scriptCatalog();
    require(catalog && catalog->modules().matchesManifest(*projectManifest_));
    core::pmr::String diagnostic;
    if (!catalog->modules().verifySources(projectRoot(), diagnostic))
        throw std::invalid_argument(diagnostic.c_str());
}

EditorCommandOutcome EditorShell::addSceneScriptComponentWithHistory(
    const AddSceneScriptComponentCommand& command)
{
    try {
        require(command.objectId);
        requireSceneScriptContext();
        const auto ownerId = scene::GameObjectId::fromValue(command.objectId);
        auto* owner = sceneDocument_.scene().findObject(ownerId);
        require(owner);
        EditorScriptComponentArchive archive(sceneDocument_, EditorSceneScriptPolicy::fields());
        const auto state =
            archive.prepare({command.resourceId, command.scriptType, command.schemaVersion},
                command.enabled,
                command.fields);
        core::pmr::Vector<SceneHistoryEntry> preparedUndo, retiredRedo;
        preparedUndo.reserve(std::min(sceneUndoHistory_.size() + 1, kMaxSceneHistoryEntries));
        scene::Scene::IdentityRestoration transaction(
            sceneDocument_.scene(), {0, 1}, sceneDocument_.identityRestorationFactory());
        const auto index = owner->components().size();
        auto& candidate = archive.stage(transaction, ownerId, {}, index, state, true);
        archive.apply(candidate, state, transaction);
        // Capture the unpublished candidate through its exact resolver. The
        // shared presence archive owns storage charge and future restore rules.
        auto snapshot = core::makeShared<EditorScenePresence::Component>(
            EditorScenePresence::captureComponent(sceneDocument_, candidate, index, &transaction));
        SceneHistoryEntry history;
        history.kind = SceneHistoryEntry::Kind::ComponentPresence;
        history.objectId = command.objectId;
        history.componentId = candidate.id().value();
        history.componentPresentAfter = true;
        history.cost += snapshot->cost;
        history.componentSnapshot = std::move(snapshot);
        if (!EditorScenePresence::fits(history.charge(), EditorScenePresence::operationLimit))
            return {false, false, EditorDiagnosticCode::SceneHistoryBudgetExceeded};
        transaction.prepare();
        transaction.commit();
        publishPreparedSceneHistory(preparedUndo, retiredRedo, std::move(history));
    } catch (const std::bad_alloc&) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    } catch (const std::length_error&) {
        return {false, false, EditorDiagnosticCode::SceneHistoryBudgetExceeded};
    } catch (const core::YamlContractError&) {
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    } catch (const std::exception&) {
        return {false, false, EditorDiagnosticCode::SceneHistoryRestoreRejected};
    }
    sceneInspectorWidgetOwner_ = {};
    refreshSceneInspector();
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::setSceneScriptFieldsWithHistory(
    const SetSceneScriptFieldsCommand& command)
{
    try {
        require(command.objectId && command.componentId);
        requireSceneScriptContext();
        auto* owner =
            sceneDocument_.scene().findObject(scene::GameObjectId::fromValue(command.objectId));
        auto* component = owner
            ? owner->findComponent(scene::ComponentId::fromValue(command.componentId))
            : nullptr;
        require(component);
        EditorScriptComponentArchive archive(sceneDocument_, EditorSceneScriptPolicy::fields());
        auto before = core::makeShared<EditorScriptComponentState>(archive.capture(*component));
        require(before->identity.scriptResourceId == command.resourceId
            && before->identity.scriptType == command.scriptType
            && before->identity.moduleSchemaVersion == command.schemaVersion);
        auto after = core::makeShared<EditorScriptComponentState>(
            archive.prepare(before->identity, command.enabled, command.fields));
        if (EditorScriptComponentArchive::equal(*before, *after))
            return {true, false, EditorDiagnosticCode::None};
        SceneHistoryEntry history;
        history.kind = SceneHistoryEntry::Kind::ScriptFields;
        history.objectId = command.objectId;
        history.componentId = command.componentId;
        history.cost.bytes += EditorScriptComponentArchive::storageBytes(*before)
            + EditorScriptComponentArchive::storageBytes(*after);
        history.cost.components = 1;
        history.cost.references = before->references.size() + after->references.size();
        history.beforeScriptFields = std::move(before);
        history.afterScriptFields = std::move(after);
        if (!EditorScenePresence::fits(history.charge(), EditorScenePresence::operationLimit))
            return {false, false, EditorDiagnosticCode::SceneHistoryBudgetExceeded};
        core::pmr::Vector<SceneHistoryEntry> preparedUndo, retiredRedo;
        preparedUndo.reserve(std::min(sceneUndoHistory_.size() + 1, kMaxSceneHistoryEntries));
        require(archive.replaceFields(
            *component, *history.beforeScriptFields, *history.afterScriptFields));
        publishPreparedSceneHistory(preparedUndo, retiredRedo, std::move(history));
    } catch (const std::bad_alloc&) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    } catch (const std::length_error&) {
        return {false, false, EditorDiagnosticCode::SceneHistoryBudgetExceeded};
    } catch (const core::YamlContractError&) {
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    } catch (const std::exception&) {
        return {false, false, EditorDiagnosticCode::SceneHistoryRestoreRejected};
    }
    sceneInspectorWidgetOwner_ = {};
    refreshSceneInspector();
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::applyScriptFieldHistory(const SceneHistoryEntry& entry, bool undo)
{
    try {
        requireSceneScriptContext();
        require(entry.beforeScriptFields && entry.afterScriptFields);
        auto* owner =
            sceneDocument_.scene().findObject(scene::GameObjectId::fromValue(entry.objectId));
        auto* component = owner
            ? owner->findComponent(scene::ComponentId::fromValue(entry.componentId))
            : nullptr;
        require(component);
        const auto& expected = undo ? *entry.afterScriptFields : *entry.beforeScriptFields;
        const auto& replacement = undo ? *entry.beforeScriptFields : *entry.afterScriptFields;
        EditorScriptComponentArchive archive(sceneDocument_, EditorSceneScriptPolicy::fields());
        require(archive.replaceFields(*component, expected, replacement));
    } catch (const std::bad_alloc&) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    } catch (const std::length_error&) {
        return {false, false, EditorDiagnosticCode::SceneHistoryBudgetExceeded};
    } catch (const core::YamlContractError&) {
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    } catch (const std::exception&) {
        return {false, false, EditorDiagnosticCode::SceneHistoryRestoreRejected};
    }
    return {true, true, EditorDiagnosticCode::None};
}
} // namespace arp::editor
