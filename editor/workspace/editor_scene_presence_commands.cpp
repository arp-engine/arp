#include "editor/workspace/editor_scene_script_codec.h"
#include "editor/workspace/editor_shell.h"
#include "editor/workspace/editor_shell_internal.h"

#include <algorithm>
#include <stdexcept>

namespace arp::editor {
namespace {
using Presence = EditorScenePresence;

EditorCommandOutcome preparationFailure()
{
    return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
}

EditorCommandOutcome restorationFailure()
{
    return {false, false, EditorDiagnosticCode::SceneHistoryRestoreRejected};
}

EditorCommandOutcome budgetFailure()
{
    return {false, false, EditorDiagnosticCode::SceneHistoryBudgetExceeded};
}

void require(bool condition)
{
    if (!condition)
        throw std::invalid_argument("EDITOR-SCENE-HISTORY-REJECTED");
}

bool containsScripts(const Presence::Subtree& subtree)
{
    return std::any_of(subtree.objects.begin(), subtree.objects.end(), [](const auto& object) {
        return std::any_of(
            object.components.begin(), object.components.end(), [](const auto& component) {
                return bool(component.script);
            });
    });
}

SceneInspectorProperty initialValue(const SetSceneComponentPropertyCommand& command,
    const core::pmr::Vector<SceneInspectorProperty>& schema)
{
    const auto found = std::find_if(schema.begin(), schema.end(), [&](const auto& field) {
        return field.name == command.name;
    });
    SceneInspectorPropertyKind kind;
    require(found != schema.end() && sceneInspectorKindForValueKind(command.valueKind, kind)
        && found->kind == kind && found->editable);
    auto value = *found;
    value.booleanValue = command.booleanValue;
    value.integerValue = command.integerValue;
    value.unsignedIntegerValue = command.unsignedIntegerValue;
    value.numberValue = command.numberValue;
    value.vector3Value = command.vector3Value;
    value.color4Value = command.color4Value;
    value.stringValue = command.stringValue;
    value.vector2Value = command.vector2Value;
    value.vector2ListValue = command.vector2ListValue;
    value.sceneObjectReferenceValue = command.sceneObjectReferenceValue;
    value.sceneObjectLinkListValue = command.sceneObjectLinkListValue;
    return value;
}
} // namespace

EditorCommandOutcome EditorShell::createSceneObjectWithHistory(
    const CreateSceneObjectCommand& command)
{
    if (!EditorSceneDocument::validObjectName(command.name))
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    try {
        SceneHistoryEntry history;
        history.kind = SceneHistoryEntry::Kind::ObjectPresence;
        history.objectPresentAfter = true;
        history.selectionBefore = sceneDocument_.selectedObject().value();
        history.additionalSelectionBefore = sceneViewportAdditionalSelection_;
        core::pmr::Vector<SceneHistoryEntry> preparedUndo, retiredRedo;
        preparedUndo.reserve(std::min(sceneUndoHistory_.size() + 1, kMaxSceneHistoryEntries));
        auto snapshot = core::makeShared<Presence::Subtree>();
        scene::Scene::IdentityRestoration transaction(sceneDocument_.scene(), {1, 1});
        auto& object = transaction.createObject(command.name,
            sceneDocument_.scene().rootId(),
            sceneDocument_.scene().childIds().size());
        Presence::Object value;
        value.id = object.id();
        value.parent = object.parentId();
        value.transformId = object.transform().id();
        value.name = object.name();
        value.childIndex = sceneDocument_.scene().childIds().size();
        value.transform = object.local_transform();
        snapshot->cost = {
            sizeof(Presence::Subtree) + sizeof(Presence::Object) + value.name.capacity() + 1,
            1,
            1,
            0};
        snapshot->objects.push_back(std::move(value));
        snapshot->cost.bytes += snapshot->objects.capacity() * sizeof(Presence::Object);
        history.objectId = object.id().value();
        history.selectionAfter = history.objectId;
        history.cost += snapshot->cost;
        history.cost.bytes += history.additionalSelectionBefore.capacity() * sizeof(std::uint64_t);
        history.subtree = std::move(snapshot);
        require(Presence::fits(history.cost, Presence::operationLimit));
        transaction.prepare();
        transaction.commit();
        (void)sceneDocument_.select(object.id());
        sceneViewportAdditionalSelection_.clear();
        publishPreparedSceneHistory(preparedUndo, retiredRedo, std::move(history));
    } catch (const std::bad_alloc&) {
        return preparationFailure();
    } catch (const std::length_error&) {
        return budgetFailure();
    } catch (const std::logic_error&) {
        return restorationFailure();
    }
    sceneInspectorWidgetOwner_ = {};
    refreshSceneInspector();
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::destroySceneObjectWithHistory()
{
    try {
        SceneHistoryEntry history;
        history.kind = SceneHistoryEntry::Kind::ObjectPresence;
        history.objectPresentBefore = true;
        history.objectId = sceneDocument_.selectedObject().value();
        history.selectionBefore = history.objectId;
        history.selectionAfter = sceneDocument_.scene().rootId().value();
        history.additionalSelectionBefore = sceneViewportAdditionalSelection_;
        auto snapshot = core::makeShared<Presence::Subtree>(Presence::captureSubtree(sceneDocument_,
            sceneDocument_.selectedObject(),
            sceneAuthoringVersion_ == EditorSceneScriptCodec::contractVersion
                ? Presence::ScriptPolicy::Archive
                : Presence::ScriptPolicy::Reject));
        if (containsScripts(*snapshot))
            requireSceneScriptContext();
        for (const auto& links : snapshot->externalLinks) {
            const auto gate = sceneObjectWriteGate(links.owner.value(), false);
            if (!gate.success)
                return gate;
        }
        for (const auto& joint : snapshot->externalJoints) {
            const auto gate = sceneObjectWriteGate(joint.owner.value(), false);
            if (!gate.success)
                return gate;
        }
        history.cost += snapshot->cost;
        history.cost.bytes += history.additionalSelectionBefore.capacity() * sizeof(std::uint64_t);
        if (!Presence::fits(history.cost, Presence::operationLimit))
            return budgetFailure();
        history.subtree = std::move(snapshot);
        core::pmr::Vector<SceneHistoryEntry> preparedUndo, retiredRedo;
        preparedUndo.reserve(std::min(sceneUndoHistory_.size() + 1, kMaxSceneHistoryEntries));
        Presence::destroySubtree(sceneDocument_, *history.subtree);
        (void)sceneDocument_.select(sceneDocument_.scene().rootId());
        sceneViewportAdditionalSelection_.clear();
        publishPreparedSceneHistory(preparedUndo, retiredRedo, std::move(history));
    } catch (const std::bad_alloc&) {
        return preparationFailure();
    } catch (const std::length_error&) {
        return budgetFailure();
    } catch (const std::logic_error&) {
        return restorationFailure();
    }
    sceneInspectorWidgetOwner_ = {};
    refreshSceneInspector();
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::addSceneComponentWithHistory(
    const AddSceneComponentCommand& command)
{
    if (command.initialProperties.size() > 64u)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    for (std::size_t i = 0; i < command.initialProperties.size(); ++i) {
        const auto& field = command.initialProperties[i];
        if (field.name.empty() || field.name.size() > EditorSceneDocument::maxPropertyNameBytes)
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        for (std::size_t j = 0; j < i; ++j)
            if (command.initialProperties[j].name == field.name)
                return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    }
    const auto id = command.objectId ? scene::GameObjectId::fromValue(command.objectId)
                                     : sceneDocument_.selectedObject();
    auto* object = sceneDocument_.scene().findObject(id);
    if (!object || command.type.empty() || command.type.size() > 64u)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    // The authoring format keys component records by type; duplicate types
    // cannot be persisted deterministically by its existing v3 contract.
    for (const auto& component : object->components())
        if (component->scriptTypeName() == command.type)
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    try {
        SceneHistoryEntry history;
        history.kind = SceneHistoryEntry::Kind::ComponentPresence;
        history.objectId = id.value();
        history.componentPresentAfter = true;
        core::pmr::Vector<SceneHistoryEntry> preparedUndo, retiredRedo;
        preparedUndo.reserve(std::min(sceneUndoHistory_.size() + 1, kMaxSceneHistoryEntries));
        scene::Scene::IdentityRestoration transaction(
            sceneDocument_.scene(), {0, 1}, sceneDocument_.identityRestorationFactory());
        auto& component =
            transaction.createComponent(id, command.type, object->components().size());
        const auto schema = EditorSceneDocument::componentProperties(component);
        core::pmr::Vector<SceneInspectorProperty> initial;
        for (const auto& field : command.initialProperties)
            initial.push_back(initialValue(field, schema));
        require(EditorSceneDocument::applyComponentProperties(component, initial, transaction));
        auto snapshot = core::makeShared<Presence::Component>(
            Presence::captureComponent(component, object->components().size()));
        Presence::applyComponent(component, *snapshot, sceneDocument_, transaction);
        history.componentId = component.id().value();
        history.cost += snapshot->cost;
        history.componentSnapshot = std::move(snapshot);
        if (!Presence::fits(history.cost, Presence::operationLimit))
            return budgetFailure();
        transaction.prepare();
        transaction.commit();
        publishPreparedSceneHistory(preparedUndo, retiredRedo, std::move(history));
    } catch (const std::bad_alloc&) {
        return preparationFailure();
    } catch (const std::length_error&) {
        return budgetFailure();
    } catch (const std::logic_error&) {
        return restorationFailure();
    }
    sceneInspectorWidgetOwner_ = {};
    refreshSceneInspector();
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::removeSceneComponentWithHistory(
    const RemoveSceneComponentCommand& command)
{
    const auto objectId = command.objectId ? scene::GameObjectId::fromValue(command.objectId)
                                           : sceneDocument_.selectedObject();
    auto* object = sceneDocument_.scene().findObject(objectId);
    auto id = scene::ComponentId::fromValue(command.componentId);
    if (object && !id.valid() && object->components().size() > 1)
        id = object->components().back()->id();
    const auto* component = object ? object->findComponent(id) : nullptr;
    if (!component || id == object->transform().id())
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    try {
        if (!component->scriptInstance().is_undefined())
            requireSceneScriptContext();
        Presence::validateRemoval(sceneDocument_, *component);
        const auto index = static_cast<std::size_t>(
            std::find(object->componentIds().begin(), object->componentIds().end(), id)
            - object->componentIds().begin());
        auto snapshot = core::makeShared<Presence::Component>(
            sceneAuthoringVersion_ == EditorSceneScriptCodec::contractVersion
                ? Presence::captureComponent(sceneDocument_, *component, index)
                : Presence::captureComponent(*component, index));
        SceneHistoryEntry history;
        history.kind = SceneHistoryEntry::Kind::ComponentPresence;
        history.objectId = objectId.value();
        history.componentId = id.value();
        history.componentPresentBefore = true;
        history.cost += snapshot->cost;
        history.componentSnapshot = std::move(snapshot);
        if (!Presence::fits(history.cost, Presence::operationLimit))
            return budgetFailure();
        core::pmr::Vector<SceneHistoryEntry> preparedUndo, retiredRedo;
        preparedUndo.reserve(std::min(sceneUndoHistory_.size() + 1, kMaxSceneHistoryEntries));
        Presence::validateRestoration(sceneDocument_, objectId, *history.componentSnapshot);
        require(sceneDocument_.removeComponent(objectId, id));
        publishPreparedSceneHistory(preparedUndo, retiredRedo, std::move(history));
    } catch (const std::bad_alloc&) {
        return preparationFailure();
    } catch (const std::length_error&) {
        return budgetFailure();
    } catch (const std::logic_error&) {
        return restorationFailure();
    }
    sceneInspectorWidgetOwner_ = {};
    refreshSceneInspector();
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::applyPresenceHistory(const SceneHistoryEntry& entry, bool undo)
{
    try {
        if (entry.kind == SceneHistoryEntry::Kind::ComponentPresence) {
            require(bool(entry.componentSnapshot));
            const auto& snapshot = *entry.componentSnapshot;
            if (snapshot.script)
                requireSceneScriptContext();
            auto* owner =
                sceneDocument_.scene().findObject(scene::GameObjectId::fromValue(entry.objectId));
            require(owner);
            if (undo ? entry.componentPresentBefore : entry.componentPresentAfter) {
                scene::Scene::IdentityRestoration transaction(
                    sceneDocument_.scene(), {0, 1}, sceneDocument_.identityRestorationFactory());
                auto& restored =
                    Presence::stageComponent(sceneDocument_, transaction, owner->id(), snapshot);
                Presence::applyComponent(restored, snapshot, sceneDocument_, transaction);
                transaction.prepare();
                transaction.commit();
            } else {
                const auto* component = owner->findComponent(snapshot.id);
                require(component && snapshot.index < owner->components().size()
                    && owner->components()[snapshot.index].get() == component
                    && Presence::matchesComponent(sceneDocument_, *component, snapshot));
                Presence::validateRemoval(sceneDocument_, *component);
                require(sceneDocument_.removeComponent(owner->id(), snapshot.id));
            }
        } else {
            require(bool(entry.subtree) && !entry.subtree->objects.empty());
            if (containsScripts(*entry.subtree))
                requireSceneScriptContext();
            const bool restoring = undo ? entry.objectPresentBefore : entry.objectPresentAfter;
            const auto selection =
                scene::GameObjectId::fromValue(undo ? entry.selectionBefore : entry.selectionAfter);
            auto additional =
                undo ? entry.additionalSelectionBefore : entry.additionalSelectionAfter;
            const auto resolves = [&](scene::GameObjectId id) {
                if (restoring
                    && std::any_of(entry.subtree->objects.begin(),
                        entry.subtree->objects.end(),
                        [id](const auto& object) {
                            return object.id == id;
                        }))
                    return true;
                if (!restoring
                    && std::any_of(entry.subtree->objects.begin(),
                        entry.subtree->objects.end(),
                        [id](const auto& object) {
                            return object.id == id;
                        }))
                    return false;
                return sceneDocument_.scene().findObject(id) != nullptr;
            };
            require(resolves(selection));
            for (auto id : additional)
                require(resolves(id));
            for (const auto& links : entry.subtree->externalLinks) {
                const auto gate = sceneObjectWriteGate(links.owner.value(), false);
                if (!gate.success)
                    return gate;
            }
            for (const auto& joint : entry.subtree->externalJoints) {
                const auto gate = sceneObjectWriteGate(joint.owner.value(), false);
                if (!gate.success)
                    return gate;
            }

            if (restoring)
                Presence::restoreSubtree(sceneDocument_, *entry.subtree);
            else
                Presence::destroySubtree(sceneDocument_, *entry.subtree);
            (void)sceneDocument_.select(selection);
            sceneViewportAdditionalSelection_.swap(additional);
        }
    } catch (const std::bad_alloc&) {
        return preparationFailure();
    } catch (const std::length_error&) {
        return budgetFailure();
    } catch (const std::logic_error&) {
        return restorationFailure();
    }
    return {true, true, EditorDiagnosticCode::None};
}
} // namespace arp::editor
