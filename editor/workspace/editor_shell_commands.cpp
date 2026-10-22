#include "core/io/filesystem_path.h"
#include "core/text/string_utils.h"
#include "editor/workspace/editor_layout.h"
#include "editor/workspace/editor_shell.h"
#include "editor/workspace/editor_shell_internal.h"

#include <algorithm>
#include <cmath>
#include <system_error>
#include <type_traits>

namespace arp::editor::detail {
bool sceneResourceReady(void* context, std::string_view type, std::string_view id)
{
    auto* resources = static_cast<resource::ResourceManager*>(context);
    if (!resources)
        return false;
    const auto* record = resources->recordSnapshot(id);
    return record && record->state == resource::ResourceState::Ready
        && std::string_view(record->type.data(), record->type.size()) == type;
}

resource::ResourceManager* sceneResourceManager(const EditorSceneDocument& document)
{
    const auto provider = document.resourceManagerProvider();
    return provider ? provider() : nullptr;
}
} // namespace arp::editor::detail

namespace arp::editor {
using namespace layout;
using detail::sceneResourceManager;
using detail::sceneResourceReady;

bool sceneInspectorKindForValueKind(
    ScenePropertyValueKind valueKind, SceneInspectorPropertyKind& inspectorKind)
{
    switch (valueKind) {
    case ScenePropertyValueKind::Boolean:
        inspectorKind = SceneInspectorPropertyKind::Boolean;
        return true;
    case ScenePropertyValueKind::Integer:
        inspectorKind = SceneInspectorPropertyKind::Integer;
        return true;
    case ScenePropertyValueKind::UnsignedInteger:
        inspectorKind = SceneInspectorPropertyKind::UnsignedInteger;
        return true;
    case ScenePropertyValueKind::Number:
        inspectorKind = SceneInspectorPropertyKind::Number;
        return true;
    case ScenePropertyValueKind::Enumeration:
        inspectorKind = SceneInspectorPropertyKind::Enumeration;
        return true;
    case ScenePropertyValueKind::Vector2:
        inspectorKind = SceneInspectorPropertyKind::Vector2;
        return true;
    case ScenePropertyValueKind::Vector2List:
        inspectorKind = SceneInspectorPropertyKind::Vector2List;
        return true;
    case ScenePropertyValueKind::SceneObjectLinkList:
        inspectorKind = SceneInspectorPropertyKind::SceneObjectLinkList;
        return true;
    case ScenePropertyValueKind::Vector3:
        inspectorKind = SceneInspectorPropertyKind::Vector3;
        return true;
    case ScenePropertyValueKind::Color4:
        inspectorKind = SceneInspectorPropertyKind::Color4;
        return true;
    case ScenePropertyValueKind::String:
        inspectorKind = SceneInspectorPropertyKind::String;
        return true;
    case ScenePropertyValueKind::ResourceReference:
        inspectorKind = SceneInspectorPropertyKind::ResourceReference;
        return true;
    case ScenePropertyValueKind::SceneObjectReference:
        inspectorKind = SceneInspectorPropertyKind::SceneObjectReference;
        return true;
    }
    return false;
}

void EditorShell::recordSceneHistory(const SceneHistoryEntry& entry)
{
    sceneRedoHistory_.clear();
    if (sceneUndoHistory_.size() == kMaxSceneHistoryEntries)
        sceneUndoHistory_.erase(sceneUndoHistory_.begin());
    sceneUndoHistory_.push_back(entry);
}

EditorCommandOutcome EditorShell::applySceneHistory(bool undo)
{
    auto& source = undo ? sceneUndoHistory_ : sceneRedoHistory_;
    auto& destination = undo ? sceneRedoHistory_ : sceneUndoHistory_;
    if (source.empty())
        return {true, false, EditorDiagnosticCode::None};
    auto entry = source.back();
    auto* object =
        sceneDocument_.scene().findObject(scene::GameObjectId::fromValue(entry.objectId));
    if (!object)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    if (entry.kind == SceneHistoryEntry::Kind::Transform) {
        object->transform().setLocalTransform(undo ? entry.beforeTransform : entry.afterTransform);
        if (sceneDocument_.selectedObject().value() == entry.objectId) {
            sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
            refreshSceneInspector();
        }
    } else if (entry.kind == SceneHistoryEntry::Kind::Reparent) {
        const auto parentId = undo ? entry.beforeParentId : entry.afterParentId;
        const auto childIndex = undo ? entry.beforeChildIndex : entry.afterChildIndex;
        if (!sceneDocument_.reparentObject(
                object->id(), scene::GameObjectId::fromValue(parentId), childIndex))
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    } else if (entry.kind == SceneHistoryEntry::Kind::ComponentPresence) {
        const bool shouldExist = undo ? entry.componentPresentBefore : entry.componentPresentAfter;
        if (shouldExist) {
            auto* restored = sceneDocument_.addComponent(entry.componentType, object->id());
            if (!restored)
                return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
            const auto& editorProperties =
                undo ? entry.beforeComponentProperties : entry.afterComponentProperties;
            const auto& authoringProperties = undo ? entry.beforeComponentAuthoringProperties
                                                   : entry.afterComponentAuthoringProperties;
            bool valid = editorProperties.is_undefined()
                || restored->applyEditorProperties(editorProperties);
            valid = valid
                && (authoringProperties.is_undefined()
                    || restored->applyEditorAuthoringProperties(authoringProperties));
            if (valid) {
                for (const auto& property : sceneDocument_.inspectorProperties(object->id())) {
                    if (property.componentId != restored->id().value()
                        || property.kind != SceneInspectorPropertyKind::ResourceReference
                        || property.stringValue.empty())
                        continue;
                    if (property.resourceType.empty()
                        || !sceneResourceReady(sceneResourceManager(sceneDocument_),
                            std::string_view(
                                property.resourceType.data(), property.resourceType.size()),
                            std::string_view(
                                property.stringValue.data(), property.stringValue.size()))) {
                        valid = false;
                        break;
                    }
                }
            }
            if (!valid) {
                (void)sceneDocument_.removeComponent(object->id(), restored->id());
                return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
            }
            entry.componentId = restored->id().value();
        } else {
            const auto componentId = scene::ComponentId::fromValue(entry.componentId);
            if (!object->findComponent(componentId)
                || !sceneDocument_.removeComponent(object->id(), componentId))
                return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        }
        sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
        refreshSceneInspector();
    } else {
        applyingSceneHistory_ = true;
        const auto response =
            execute(EditorCommandPayload{undo ? entry.beforeProperty : entry.afterProperty});
        applyingSceneHistory_ = false;
        if (!response.success || !response.stateChanged)
            return {false,
                false,
                response.diagnostic == EditorDiagnosticCode::None
                    ? EditorDiagnosticCode::InvalidCommandPayload
                    : response.diagnostic};
        if (sceneDocument_.selectedObject().value() == entry.objectId) {
            sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
            refreshSceneInspector();
        }
    }
    source.pop_back();
    if (destination.size() == kMaxSceneHistoryEntries)
        destination.erase(destination.begin());
    destination.push_back(entry);
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::scenePersistenceTaskSnapshot(std::uint64_t taskId) const
{
    const auto found = std::find_if(
        scenePersistenceTasks_.begin(), scenePersistenceTasks_.end(), [taskId](const auto& task) {
            return task.id == taskId;
        });
    if (taskId == 0 || found == scenePersistenceTasks_.end())
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    EditorCommandOutcome result;
    result.stateChanged = false;
    result.hasSnapshot = true;
    result.snapshot.workspace = presentationState_.workspace();
    result.snapshot.scene = presentationState_.scene();
    result.snapshot.selection = presentationState_.selection();
    result.snapshot.scenePersistenceTaskId = found->id;
    result.snapshot.scenePersistenceTaskStatus = found->status;
    result.snapshot.scenePersistenceTaskDiagnostic = found->diagnostic;
    return result;
}

EditorCommandOutcome EditorShell::beginScenePersistence(bool loading, std::string_view relativePath)
{
    auto available = std::find_if(
        scenePersistenceTasks_.begin(), scenePersistenceTasks_.end(), [](const auto& task) {
            return task.id == 0;
        });
    if (available == scenePersistenceTasks_.end()) {
        available = std::min_element(scenePersistenceTasks_.begin(),
            scenePersistenceTasks_.end(),
            [](const auto& lhs, const auto& rhs) {
                const bool lhsDone = lhs.status == ScenePersistenceTaskStatus::Succeeded
                    || lhs.status == ScenePersistenceTaskStatus::Failed;
                const bool rhsDone = rhs.status == ScenePersistenceTaskStatus::Succeeded
                    || rhs.status == ScenePersistenceTaskStatus::Failed;
                if (lhsDone != rhsDone)
                    return lhsDone;
                return lhs.id < rhs.id;
            });
        if (available == scenePersistenceTasks_.end()
            || (available->status != ScenePersistenceTaskStatus::Succeeded
                && available->status != ScenePersistenceTaskStatus::Failed))
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    }

    EditorScenePersistence::RequestHandle handle;
    if (loading) {
        handle = scenePersistence_.load(projectBrowser_.root(), relativePath);
    } else {
        for (const auto& object : sceneDocument_.scene().objects()) {
            const auto properties = sceneDocument_.inspectorProperties(object->id());
            for (const auto& property : properties) {
                if (property.kind != SceneInspectorPropertyKind::ResourceReference
                    || property.stringValue.empty())
                    continue;
                if (property.resourceType.empty()
                    || !sceneResourceReady(sceneResourceManager(sceneDocument_),
                        std::string_view(
                            property.resourceType.data(), property.resourceType.size()),
                        std::string_view(property.stringValue.data(), property.stringValue.size())))
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
            }
        }
        core::pmr::String yaml(&core::defaultCpuMemoryResource());
        core::pmr::String diagnostic(&core::defaultCpuMemoryResource());
        if (!EditorSceneAuthoringCodec::encode(sceneDocument_, yaml, diagnostic))
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        handle = scenePersistence_.save(projectBrowser_.root(), relativePath, yaml);
    }
    if (!handle)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};

    available->id = nextScenePersistenceTaskId_++;
    available->request = handle;
    available->status = ScenePersistenceTaskStatus::Pending;
    available->loading = loading;
    available->diagnostic.clear();
    EditorCommandOutcome result;
    result.stateChanged = false;
    result.hasSnapshot = true;
    result.snapshot.workspace = presentationState_.workspace();
    result.snapshot.scene = presentationState_.scene();
    result.snapshot.selection = presentationState_.selection();
    result.snapshot.scenePersistenceTaskId = available->id;
    result.snapshot.scenePersistenceTaskStatus = available->status;
    return result;
}

void EditorShell::pollScenePersistence()
{
    for (auto& task : scenePersistenceTasks_) {
        if (task.status != ScenePersistenceTaskStatus::Pending)
            continue;
        core::io::AtomicFileResult fileResult;
        if (!scenePersistence_.take(task.request, fileResult))
            continue;
        task.request = {};
        if (!fileResult.success) {
            task.status = ScenePersistenceTaskStatus::Failed;
            task.diagnostic = fileResult.diagnostic;
            continue;
        }
        if (!task.loading) {
            task.status = ScenePersistenceTaskStatus::Succeeded;
            continue;
        }
        const std::string_view yaml(
            reinterpret_cast<const char*>(fileResult.bytes.data()), fileResult.bytes.size());
        (void)submitCommand(CompleteSceneLoadCommand(task.id, yaml), EditorCommandOrigin::Internal);
    }
}

void EditorShell::pollTasks()
{
    pollScenePersistence();
    if (terrainBuild_.ready())
        (void)submitCommand(CompleteTerrainResourceBuildCommand{}, EditorCommandOrigin::Internal);
}

EditorCommandOutcome EditorShell::execute(const EditorCommandPayload& payload)
{
    return std::visit(
        [this](const auto& command) -> EditorCommandOutcome {
            using Command = std::decay_t<decltype(command)>;
            if constexpr (std::is_same_v<Command, BuildTerrainResourceCommand>) {
                if (!terrainBuild_.start(projectRoot(),
                        command.authoringPath,
                        command.packagePath,
                        projectResources_.manifestSnapshot()))
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                feedback("Building terrain resource...");
                return {true, false, EditorDiagnosticCode::None};
            } else if constexpr (std::is_same_v<Command, CompleteTerrainResourceBuildCommand>) {
                auto built = terrainBuild_.finish(projectRoot());
                if (!built.success) {
                    feedback(built.diagnostic);
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                }
                projectResources_.publishAssetPackages(std::move(*built.resources));
                refreshFileSystem();
                sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                refreshSceneInspector();
                feedback("Terrain resource built and added to project");
                return {true, true, EditorDiagnosticCode::None};
            } else if constexpr (std::is_same_v<Command, ActivateControlCommand>) {
                if (command.source.empty())
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                applyActivation(command.source);
            } else if constexpr (std::is_same_v<Command, SetTextValueCommand>) {
                if (command.source.empty() || command.source == "scene_name")
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                applyEntryChanged(command.source, command.value);
            } else if constexpr (std::is_same_v<Command, EditorInputCommand>) {
                applyPlatformInput(command);
                return {true, true, EditorDiagnosticCode::None};
            } else if constexpr (std::is_same_v<Command, SetInspectorScrollCommand>)
                applyInspectorScrollChanged(command.offset);
            else if constexpr (std::is_same_v<Command, DragInspectorThumbCommand>)
                applyDragInspectorThumb(command.top);
            else if constexpr (std::is_same_v<Command, SetGamePreviewStateCommand>)
                applyGamePreviewState(command.running, command.starting, command.status);
            else if constexpr (std::is_same_v<Command, SelectSceneObjectCommand>) {
                if (!sceneDocument_.select(scene::GameObjectId::fromValue(command.objectId)))
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                sceneViewportAdditionalSelection_.clear();
                refreshSceneInspector();
            } else if constexpr (std::is_same_v<Command, SelectSceneObjectsCommand>) {
                if (command.objectIds.empty()
                    || command.objectIds.size() > EditorSceneAuthoringCodec::maxObjects
                    || command.primaryObjectId == 0u)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                bool hasPrimary = false;
                for (std::size_t i = 0u; i < command.objectIds.size(); ++i) {
                    const auto id = scene::GameObjectId::fromValue(command.objectIds[i]);
                    if (!id.valid() || sceneDocument_.scene().findObject(id) == nullptr
                        || std::find(command.objectIds.begin(),
                               command.objectIds.begin() + static_cast<std::ptrdiff_t>(i),
                               command.objectIds[i])
                            != command.objectIds.begin() + static_cast<std::ptrdiff_t>(i))
                        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                    hasPrimary = hasPrimary || command.objectIds[i] == command.primaryObjectId;
                }
                const auto primaryId = scene::GameObjectId::fromValue(command.primaryObjectId);
                if (!hasPrimary || !sceneDocument_.select(primaryId))
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                sceneViewportAdditionalSelection_.clear();
                for (const auto objectId : command.objectIds) {
                    if (objectId != command.primaryObjectId)
                        sceneViewportAdditionalSelection_.push_back(
                            scene::GameObjectId::fromValue(objectId));
                }
                refreshSceneInspector();
            } else if constexpr (std::is_same_v<Command, ReparentSceneObjectCommand>) {
                const auto objectId = scene::GameObjectId::fromValue(command.objectId);
                const auto parentId = scene::GameObjectId::fromValue(command.parentId);
                const auto* object = sceneDocument_.scene().findObject(objectId);
                const auto* parent = sceneDocument_.scene().findObject(parentId);
                if (!object || !parent)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                if (object->parentId() == parentId && command.childIndex < parent->childIds().size()
                    && parent->childIds()[command.childIndex] == objectId)
                    return {true, false, EditorDiagnosticCode::None};
                const auto beforeParentId = object->parentId();
                const auto* beforeParent = beforeParentId.valid()
                    ? sceneDocument_.scene().findObject(beforeParentId)
                    : nullptr;
                if (!beforeParent)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                const auto beforeChild = std::find(
                    beforeParent->childIds().begin(), beforeParent->childIds().end(), objectId);
                if (beforeChild == beforeParent->childIds().end())
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                const auto beforeChildIndex =
                    static_cast<std::uint32_t>(beforeChild - beforeParent->childIds().begin());
                if (!sceneDocument_.reparentObject(objectId, parentId, command.childIndex))
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                SceneHistoryEntry history;
                history.kind = SceneHistoryEntry::Kind::Reparent;
                history.objectId = objectId.value();
                history.beforeParentId = beforeParentId.value();
                history.afterParentId = parentId.value();
                history.beforeChildIndex = beforeChildIndex;
                history.afterChildIndex = command.childIndex;
                recordSceneHistory(history);
            } else if constexpr (std::is_same_v<Command, SceneViewportCameraCommand>) {
                constexpr float maxNavigationDelta = 100000.0f;
                if (!std::isfinite(command.deltaX) || !std::isfinite(command.deltaY)
                    || !std::isfinite(command.viewportHeight) || command.viewportHeight <= 0.0f
                    || !std::isfinite(command.viewportWidth) || command.viewportWidth <= 0.0f
                    || std::abs(command.deltaX) > maxNavigationDelta
                    || std::abs(command.deltaY) > maxNavigationDelta)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                switch (command.operation) {
                case SceneViewportCameraOperation::Orbit:
                    sceneViewportCamera_.orbit(command.deltaX, command.deltaY);
                    break;
                case SceneViewportCameraOperation::OrbitAround:
                    sceneViewportCamera_.orbitAround(command.deltaX, command.deltaY, command.pivot);
                    break;
                case SceneViewportCameraOperation::Look:
                    sceneViewportCamera_.look(command.deltaX, command.deltaY);
                    break;
                case SceneViewportCameraOperation::Pan:
                    sceneViewportCamera_.pan(
                        command.deltaX, command.deltaY, command.viewportHeight);
                    break;
                case SceneViewportCameraOperation::Move:
                    sceneViewportCamera_.move(command.deltaX, command.deltaY);
                    break;
                case SceneViewportCameraOperation::Zoom:
                    sceneViewportCamera_.zoom(command.deltaY);
                    break;
                default:
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                }
                return {true, true, EditorDiagnosticCode::None};
            } else if constexpr (std::is_same_v<Command, CreateSceneObjectCommand>) {
                if (!sceneDocument_
                        .createObject(std::string_view(command.name.data(), command.name.size()))
                        .valid())
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                sceneViewportAdditionalSelection_.clear();
                refreshSceneInspector();
            } else if constexpr (std::is_same_v<Command, DestroySceneObjectCommand>) {
                if (!sceneDocument_.destroySelected())
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                sceneViewportAdditionalSelection_.clear();
                refreshSceneInspector();
            } else if constexpr (std::is_same_v<Command, SetSceneObjectNameCommand>) {
                if (!sceneDocument_.renameSelected(
                        std::string_view(command.name.data(), command.name.size())))
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                refreshSceneInspector();
            } else if constexpr (std::is_same_v<Command, SetSceneObjectTranslationCommand>) {
                if (!std::isfinite(command.x) || !std::isfinite(command.y)
                    || !std::isfinite(command.z) || std::abs(command.x) > 100000.0f
                    || std::abs(command.y) > 100000.0f || std::abs(command.z) > 100000.0f
                    || !sceneDocument_.setSelectedTranslation(command.x, command.y, command.z))
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                refreshSceneInspector();
            } else if constexpr (std::is_same_v<Command, SetSceneObjectTransformCommand>) {
                const auto finiteVec3 = [](core::Vec3 value) {
                    return std::isfinite(value.x) && std::isfinite(value.y)
                        && std::isfinite(value.z);
                };
                const auto& rotation = command.rotation;
                const float rotationLengthSquared = rotation.x * rotation.x
                    + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
                if (!finiteVec3(command.translation) || !finiteVec3(command.scale)
                    || !std::isfinite(rotation.x) || !std::isfinite(rotation.y)
                    || !std::isfinite(rotation.z) || !std::isfinite(rotation.w)
                    || std::abs(command.translation.x) > 100000.0f
                    || std::abs(command.translation.y) > 100000.0f
                    || std::abs(command.translation.z) > 100000.0f
                    || std::abs(command.scale.x) > 100000.0f
                    || std::abs(command.scale.y) > 100000.0f
                    || std::abs(command.scale.z) > 100000.0f
                    || !std::isfinite(rotationLengthSquared) || rotationLengthSquared < 1.0e-12f)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                const auto targetId = command.objectId != 0u
                    ? scene::GameObjectId::fromValue(command.objectId)
                    : sceneDocument_.selectedObject();
                const auto* object = sceneDocument_.scene().findObject(targetId);
                if (!object)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                auto transform = object->transform().localTransform();
                const auto beforeTransform = transform;
                transform.translation = command.translation;
                transform.rotation = core::normalize(command.rotation);
                transform.scale = command.scale;
                if (targetId == sceneDocument_.selectedObject()) {
                    if (!sceneDocument_.setSelectedTransform(transform))
                        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                } else {
                    auto* mutableObject = sceneDocument_.scene().findObject(targetId);
                    if (!mutableObject)
                        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                    mutableObject->transform().setLocalTransform(transform);
                }
                const auto sameVec3 = [](core::Vec3 lhs, core::Vec3 rhs) {
                    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
                };
                const auto sameQuat = [](core::Quat lhs, core::Quat rhs) {
                    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
                };
                if (!sameVec3(beforeTransform.translation, transform.translation)
                    || !sameQuat(beforeTransform.rotation, transform.rotation)
                    || !sameVec3(beforeTransform.scale, transform.scale)) {
                    if (activeTransformEditObjectId_ != targetId.value()) {
                        SceneHistoryEntry history;
                        history.kind = SceneHistoryEntry::Kind::Transform;
                        history.objectId = targetId.value();
                        history.beforeTransform = beforeTransform;
                        history.afterTransform = transform;
                        recordSceneHistory(history);
                    }
                }
                sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                refreshSceneInspector();
            } else if constexpr (std::is_same_v<Command, BeginSceneTransformEditCommand>) {
                const auto objectId = scene::GameObjectId::fromValue(command.objectId);
                const auto* object = sceneDocument_.scene().findObject(objectId);
                if (!object || activeTransformEditObjectId_ != 0u)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                activeTransformEditObjectId_ = objectId.value();
                activeTransformEditStart_ = object->transform().localTransform();
                return {true, false, EditorDiagnosticCode::None};
            } else if constexpr (std::is_same_v<Command, EndSceneTransformEditCommand>) {
                if (activeTransformEditObjectId_ == 0u
                    || activeTransformEditObjectId_ != command.objectId)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                auto* object = sceneDocument_.scene().findObject(
                    scene::GameObjectId::fromValue(activeTransformEditObjectId_));
                if (!object) {
                    activeTransformEditObjectId_ = 0u;
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                }
                const auto after = object->transform().localTransform();
                const auto before = activeTransformEditStart_;
                const auto sameVec3 = [](core::Vec3 lhs, core::Vec3 rhs) {
                    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
                };
                const auto sameQuat = [](core::Quat lhs, core::Quat rhs) {
                    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
                };
                if (!sameVec3(before.translation, after.translation)
                    || !sameQuat(before.rotation, after.rotation)
                    || !sameVec3(before.scale, after.scale)) {
                    SceneHistoryEntry history;
                    history.kind = SceneHistoryEntry::Kind::Transform;
                    history.objectId = activeTransformEditObjectId_;
                    history.beforeTransform = before;
                    history.afterTransform = after;
                    recordSceneHistory(history);
                }
                activeTransformEditObjectId_ = 0u;
                return {true, false, EditorDiagnosticCode::None};
            } else if constexpr (std::is_same_v<Command, UndoSceneCommand>) {
                const auto result = applySceneHistory(true);
                if (result.success)
                    sceneViewportAdditionalSelection_.clear();
                return result;
            } else if constexpr (std::is_same_v<Command, RedoSceneCommand>) {
                const auto result = applySceneHistory(false);
                if (result.success)
                    sceneViewportAdditionalSelection_.clear();
                return result;
            } else if constexpr (std::is_same_v<Command, AddSceneComponentCommand>) {
                if (command.initialProperties.size() > 64u)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                for (std::size_t index = 0; index < command.initialProperties.size(); ++index) {
                    const auto& property = command.initialProperties[index];
                    if (property.name.empty() || property.name.size() > 256u)
                        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                    for (std::size_t other = index + 1u; other < command.initialProperties.size();
                        ++other)
                        if (property.name == command.initialProperties[other].name)
                            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                }
                const auto objectId = command.objectId != 0
                    ? scene::GameObjectId::fromValue(command.objectId)
                    : sceneDocument_.selectedObject();
                const auto* object = sceneDocument_.scene().findObject(objectId);
                auto* added = sceneDocument_.addComponent(
                    std::string_view(command.type.data(), command.type.size()), objectId);
                if (!object || !added)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                for (const auto& property : command.initialProperties) {
                    SceneInspectorPropertyKind kind;
                    if (!sceneInspectorKindForValueKind(property.valueKind, kind)) {
                        (void)sceneDocument_.removeComponent(objectId, added->id());
                        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                    }
                    const auto write = sceneDocument_.setComponentProperty(objectId,
                        added->id(),
                        std::string_view(property.name.data(), property.name.size()),
                        kind,
                        property.booleanValue,
                        property.integerValue,
                        property.unsignedIntegerValue,
                        property.numberValue,
                        property.vector3Value,
                        property.color4Value,
                        std::string_view(property.stringValue.data(), property.stringValue.size()),
                        property.vector2Value,
                        property.vector2ListValue,
                        property.sceneObjectReferenceValue,
                        property.sceneObjectLinkListValue);
                    if (write == SceneInspectorWriteResult::Rejected) {
                        (void)sceneDocument_.removeComponent(objectId, added->id());
                        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                    }
                }
                if (!applyingSceneHistory_) {
                    SceneHistoryEntry history;
                    history.kind = SceneHistoryEntry::Kind::ComponentPresence;
                    history.objectId = objectId.value();
                    history.componentId = added->id().value();
                    history.componentPresentAfter = true;
                    history.componentType = added->scriptTypeName();
                    history.afterComponentProperties = added->editorProperties();
                    history.afterComponentAuthoringProperties = added->editorAuthoringProperties();
                    recordSceneHistory(history);
                }
                sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                refreshSceneInspector();
            } else if constexpr (std::is_same_v<Command, RemoveSceneComponentCommand>) {
                const auto objectId = command.objectId != 0
                    ? scene::GameObjectId::fromValue(command.objectId)
                    : sceneDocument_.selectedObject();
                auto* object = sceneDocument_.scene().findObject(objectId);
                scene::ComponentId componentId = scene::ComponentId::fromValue(command.componentId);
                if (object && !componentId.valid()) {
                    const auto& components = object->components();
                    for (auto it = components.rbegin(); it != components.rend(); ++it) {
                        if ((*it)->scriptTypeName() != std::string_view("TransformComponent")) {
                            componentId = (*it)->id();
                            break;
                        }
                    }
                }
                auto* removed = object ? object->findComponent(componentId) : nullptr;
                if (!removed || removed->scriptTypeName() == std::string_view("TransformComponent"))
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                SceneHistoryEntry history;
                history.kind = SceneHistoryEntry::Kind::ComponentPresence;
                history.objectId = objectId.value();
                history.componentId = componentId.value();
                history.componentPresentBefore = true;
                history.componentType = removed->scriptTypeName();
                history.beforeComponentProperties = removed->editorProperties();
                history.beforeComponentAuthoringProperties = removed->editorAuthoringProperties();
                if (!sceneDocument_.removeComponent(objectId, componentId))
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                if (!applyingSceneHistory_)
                    recordSceneHistory(history);
                sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                refreshSceneInspector();
            } else if constexpr (std::is_same_v<Command, SetSceneComponentPropertyCommand>) {
                SceneInspectorPropertyKind valueKind;
                if (!sceneInspectorKindForValueKind(command.valueKind, valueKind))
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                SceneHistoryEntry history;
                const bool captureHistory = !applyingSceneHistory_;
                if (captureHistory) {
                    const auto properties = sceneDocument_.inspectorProperties(
                        scene::GameObjectId::fromValue(command.objectId));
                    const auto target = std::find_if(
                        properties.begin(), properties.end(), [&](const auto& property) {
                            return property.componentId == command.componentId
                                && property.name == command.name;
                        });
                    if (target == properties.end() || !target->editable
                        || target->kind != valueKind)
                        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                    history.kind = SceneHistoryEntry::Kind::Property;
                    history.objectId = command.objectId;
                    history.beforeProperty.objectId = command.objectId;
                    history.beforeProperty.componentId = command.componentId;
                    history.beforeProperty.name = command.name;
                    history.beforeProperty.valueKind = command.valueKind;
                    history.beforeProperty.booleanValue = target->booleanValue;
                    history.beforeProperty.integerValue = target->integerValue;
                    history.beforeProperty.unsignedIntegerValue = target->unsignedIntegerValue;
                    history.beforeProperty.sceneObjectReferenceValue =
                        target->sceneObjectReferenceValue;
                    history.beforeProperty.numberValue = target->numberValue;
                    history.beforeProperty.vector2Value = target->vector2Value;
                    history.beforeProperty.vector2ListValue = target->vector2ListValue;
                    history.beforeProperty.sceneObjectLinkListValue =
                        target->sceneObjectLinkListValue;
                    history.beforeProperty.vector3Value = target->vector3Value;
                    history.beforeProperty.color4Value = target->color4Value;
                    history.beforeProperty.stringValue = target->stringValue;
                    history.afterProperty = command;
                }
                if (valueKind == SceneInspectorPropertyKind::ResourceReference) {
                    const auto properties = sceneDocument_.inspectorProperties(
                        scene::GameObjectId::fromValue(command.objectId));
                    const auto target = std::find_if(
                        properties.begin(), properties.end(), [&](const auto& property) {
                            return property.componentId == command.componentId
                                && property.name == command.name
                                && property.kind == SceneInspectorPropertyKind::ResourceReference;
                        });
                    if (target == properties.end())
                        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                    if (command.stringValue.empty()) {
                        if (!target->resourceOptional)
                            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                    } else {
                        const auto* resources = sceneResourceManager(sceneDocument_);
                        const auto* record = resources
                            ? resources->recordSnapshot(std::string_view(
                                  command.stringValue.data(), command.stringValue.size()))
                            : nullptr;
                        if (!record || record->state != resource::ResourceState::Ready
                            || std::string_view(record->type.data(), record->type.size())
                                != std::string_view(
                                    target->resourceType.data(), target->resourceType.size()))
                            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                    }
                }
                const auto result = sceneDocument_.setComponentProperty(
                    scene::GameObjectId::fromValue(command.objectId),
                    scene::ComponentId::fromValue(command.componentId),
                    std::string_view(command.name.data(), command.name.size()),
                    valueKind,
                    command.booleanValue,
                    command.integerValue,
                    command.unsignedIntegerValue,
                    command.numberValue,
                    command.vector3Value,
                    command.color4Value,
                    std::string_view(command.stringValue.data(), command.stringValue.size()),
                    command.vector2Value,
                    command.vector2ListValue,
                    command.sceneObjectReferenceValue,
                    command.sceneObjectLinkListValue);
                if (result == SceneInspectorWriteResult::Rejected)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                if (result == SceneInspectorWriteResult::Changed && captureHistory)
                    recordSceneHistory(history);
                if (result == SceneInspectorWriteResult::Changed
                    && valueKind == SceneInspectorPropertyKind::SceneObjectLinkList
                    && history.beforeProperty.sceneObjectLinkListValue.size()
                        != command.sceneObjectLinkListValue.size()
                    && sceneDocument_.selectedObject().value() == command.objectId) {
                    sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                    refreshSceneInspector();
                }
                return {
                    true, result == SceneInspectorWriteResult::Changed, EditorDiagnosticCode::None};
            } else if constexpr (std::is_same_v<Command, UndoScriptCommand>)
                return applyScriptHistory(command.documentId, true);
            else if constexpr (std::is_same_v<Command, RedoScriptCommand>)
                return applyScriptHistory(command.documentId, false);
            else if constexpr (std::is_same_v<Command, ReplaceScriptTextCommand>) {
                const auto* document = scriptDocuments_.active();
                if (!document || document->id != command.documentId)
                    return {false, false, EditorDiagnosticCode::StaleDocumentTarget};
                if (command.text.size() > ScriptDocumentSession::maxScriptBytes)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                if (document->text == command.text)
                    return {true, false, EditorDiagnosticCode::None};
                const auto edit = scriptDocuments_.replaceActiveRange(0,
                    document->text.size(),
                    std::string_view(command.text.data(), command.text.size()),
                    ScriptDocumentSession::maxScriptBytes);
                if (edit != ScriptDocumentSession::EditResult::Applied)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                clampScriptScroll();
                return {true, true, EditorDiagnosticCode::None};
            } else if constexpr (std::is_same_v<Command, SaveScriptCommand>) {
                if (scriptDocuments_.activeId() != command.documentId || command.documentId == 0)
                    return {false, false, EditorDiagnosticCode::StaleDocumentTarget};
                const bool saved = saveScript();
                return {saved,
                    saved,
                    saved ? EditorDiagnosticCode::None
                          : EditorDiagnosticCode::InvalidCommandPayload};
            } else if constexpr (std::is_same_v<Command, SaveSceneCommand>) {
                return beginScenePersistence(false,
                    std::string_view(command.relativePath.data(), command.relativePath.size()));
            } else if constexpr (std::is_same_v<Command, LoadSceneCommand>) {
                return beginScenePersistence(true,
                    std::string_view(command.relativePath.data(), command.relativePath.size()));
            } else if constexpr (std::is_same_v<Command, GetScenePersistenceTaskQuery>) {
                return scenePersistenceTaskSnapshot(command.taskId);
            } else if constexpr (std::is_same_v<Command, CompleteSceneLoadCommand>) {
                const auto task = std::find_if(scenePersistenceTasks_.begin(),
                    scenePersistenceTasks_.end(),
                    [&](const auto& record) {
                        return record.id == command.taskId;
                    });
                if (task == scenePersistenceTasks_.end() || !task->loading
                    || task->status != ScenePersistenceTaskStatus::Pending)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                EditorSceneDocument candidate(false);
                candidate.setResourceManagerProvider(sceneDocument_.resourceManagerProvider());
                candidate.setPhysicsWorld2DProvider(sceneDocument_.physicsWorld2DProvider());
                core::pmr::String diagnostic(&core::defaultCpuMemoryResource());
                const auto yaml = std::string_view(command.yaml.data(), command.yaml.size());
                if (!EditorSceneAuthoringCodec::decode(yaml,
                        candidate,
                        &sceneResourceReady,
                        sceneResourceManager(sceneDocument_),
                        diagnostic)
                    || !sceneDocument_.replaceWith(std::move(candidate))) {
                    task->status = ScenePersistenceTaskStatus::Failed;
                    task->diagnostic =
                        diagnostic.empty() ? "EDITOR-SCENE-REPLACE-FAILED" : diagnostic;
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                }
                task->status = ScenePersistenceTaskStatus::Succeeded;
                task->diagnostic.clear();
                sceneUndoHistory_.clear();
                sceneRedoHistory_.clear();
                sceneViewportAdditionalSelection_.clear();
                sceneHierarchyCollapsedObjects_.clear();
                sceneHierarchyScrollOffset_ = 0u;
                sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                refreshSceneInspector();
                return {true, true, EditorDiagnosticCode::None};
            } else if constexpr (std::is_same_v<Command, GetEditorStateQuery>) {
                EditorCommandOutcome result;
                result.stateChanged = false;
                result.hasSnapshot = true;
                result.snapshot.workspace = presentationState_.workspace();
                result.snapshot.scene = presentationState_.scene();
                result.snapshot.selection = presentationState_.selection();
                result.snapshot.activeDocumentId = scriptDocuments_.activeId();
                result.snapshot.openDocumentCount =
                    static_cast<std::uint32_t>(std::min<std::size_t>(
                        scriptDocuments_.size(), std::numeric_limits<std::uint32_t>::max()));
                result.snapshot.hasActiveDocument = scriptDocuments_.active() != nullptr;
                result.snapshot.scriptDirty =
                    scriptDocuments_.active() && scriptDocuments_.active()->dirty();
                if (const auto* document = scriptDocuments_.active()) {
                    result.snapshot.scriptText = document->text;
                    const auto relative = document->path.lexically_relative(projectBrowser_.root());
                    if (!relative.empty() && !relative.is_absolute())
                        result.snapshot.scriptRelativePath = core::FilePath::utf8(relative);
                }
                return result;
            } else if constexpr (std::is_same_v<Command, GetSceneInspectorQuery>) {
                const auto objectId = command.objectId != 0
                    ? scene::GameObjectId::fromValue(command.objectId)
                    : sceneDocument_.selectedObject();
                const auto* object = sceneDocument_.scene().findObject(objectId);
                if (!object)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                EditorCommandOutcome result;
                result.stateChanged = false;
                result.hasSceneInspector = true;
                result.sceneInspector.objectId = objectId.value();
                result.sceneInspector.objectName = object->name();
                result.sceneInspector.properties = buildSceneInspectorProperties(
                    sceneDocument_, sceneResourceManager(sceneDocument_), objectId);
                return result;
            } else if constexpr (std::is_same_v<Command, GetSceneHierarchyQuery>) {
                EditorCommandOutcome result;
                result.stateChanged = false;
                result.hasSceneHierarchy = true;
                result.sceneHierarchy.selectedObjectId = sceneDocument_.selectedObject().value();
                const auto hierarchy = sceneDocument_.hierarchyEntries();
                result.sceneHierarchy.entries.reserve(hierarchy.size());
                for (const auto& entry : hierarchy) {
                    const auto* object = sceneDocument_.scene().findObject(entry.objectId);
                    if (!object)
                        continue;
                    SceneHierarchySnapshotEntry snapshotEntry;
                    snapshotEntry.objectId = entry.objectId.value();
                    snapshotEntry.parentId = object->parentId().value();
                    snapshotEntry.depth = static_cast<std::uint32_t>(entry.depth);
                    snapshotEntry.name = object->name();
                    if (const auto* parent =
                            sceneDocument_.scene().findObject(object->parentId())) {
                        const auto& siblings = parent->childIds();
                        const auto sibling =
                            std::find(siblings.begin(), siblings.end(), entry.objectId);
                        if (sibling != siblings.end())
                            snapshotEntry.childIndex =
                                static_cast<std::uint32_t>(sibling - siblings.begin());
                    }
                    result.sceneHierarchy.entries.push_back(std::move(snapshotEntry));
                }
                return result;
            } else if constexpr (std::is_same_v<Command, GetSceneComponentCatalogQuery>) {
                EditorCommandOutcome result;
                result.stateChanged = false;
                result.hasSceneComponentCatalog = true;
                const auto& catalog = EditorSceneDocument::componentCatalog();
                result.sceneComponentCatalog.entries.reserve(catalog.size());
                for (const auto& entry : catalog) {
                    SceneComponentCatalogSnapshotEntry snapshotEntry;
                    snapshotEntry.typeName.assign(entry.typeName.data(), entry.typeName.size());
                    snapshotEntry.displayName.assign(
                        entry.displayName.data(), entry.displayName.size());
                    result.sceneComponentCatalog.entries.push_back(std::move(snapshotEntry));
                }
                return result;
            }
            return {true, true, EditorDiagnosticCode::None};
        },
        payload);
}

} // namespace arp::editor
