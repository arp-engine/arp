#include "core/io/filesystem_path.h"
#include "core/text/string_utils.h"
#include "editor/workspace/editor_layout.h"
#include "editor/workspace/editor_scene_service_session.h"
#include "editor/workspace/editor_shell.h"
#include "editor/workspace/editor_shell_internal.h"

#include <algorithm>
#include <cmath>
#include <system_error>
#include <type_traits>

namespace arp::editor::detail {
resource::ResourceManager* sceneResourceManager(const EditorSceneDocument& document)
{
    const auto provider = document.resourceManagerProvider();
    return provider ? provider() : nullptr;
}
} // namespace arp::editor::detail

namespace arp::editor {
using namespace layout;
using detail::sceneResourceManager;

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
    appendProjectSnapshot(result.snapshot);
    result.snapshot.workspace = presentationState_.workspace();
    result.snapshot.scene = presentationState_.scene();
    result.snapshot.selection = presentationState_.selection();
    result.snapshot.scenePersistenceTaskId = found->id;
    result.snapshot.scenePersistenceTaskStatus = found->status;
    result.snapshot.scenePersistenceTaskDiagnostic = found->diagnostic;
    return result;
}

EditorCommandOutcome EditorShell::beginScenePersistence(
    bool loading, std::string_view relativePath, bool discardChanges, bool overwriteExisting)
{
    if (sceneExport_.pending())
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    if (loading) {
        const auto gate = sceneReplacementGate(discardChanges);
        if (!gate.success)
            return gate;
    }
    // Serialize document IO: completion order must never decide the clean baseline.
    if (std::any_of(
            scenePersistenceTasks_.begin(), scenePersistenceTasks_.end(), [](const auto& task) {
                return task.status == ScenePersistenceTaskStatus::Pending;
            }))
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    core::pmr::String contentHash, contentDiagnostic;
    if (!EditorSceneAuthoringCodec::contentHash(sceneDocument_, contentHash, contentDiagnostic))
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
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

    // The worker may commit immediately after submission. Prepare all owned
    // receipt/task strings before accepting it; publication below only swaps.
    core::pmr::String preparedPath(relativePath);
    EditorCommandOutcome result;
    result.stateChanged = false;
    result.hasSnapshot = true;
    appendProjectSnapshot(result.snapshot);
    result.snapshot.workspace = presentationState_.workspace();
    result.snapshot.scene = presentationState_.scene();
    result.snapshot.selection = presentationState_.selection();
    EditorScenePersistence::RequestHandle handle;
    std::filesystem::path resolvedTarget;
    core::pmr::String saveYaml;
    bool checkingFile = false;
    if (loading) {
        handle = scenePersistence_.load(projectBrowser_.root(), relativePath, &resolvedTarget);
    } else {
        for (const auto& object : sceneDocument_.scene().objects()) {
            const auto properties = sceneDocument_.inspectorProperties(object->id());
            for (const auto& property : properties) {
                if (property.kind != SceneInspectorPropertyKind::ResourceReference
                    || property.stringValue.empty())
                    continue;
                if (property.resourceType.empty()
                    || !EditorSceneAuthoringCodec::isResourceReady(
                        sceneResourceManager(sceneDocument_),
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
        const char* pathDiagnostic = nullptr;
        if (!EditorScenePersistence::resolvePath(
                projectBrowser_.root(), relativePath, false, resolvedTarget, pathDiagnostic)) {
            feedback(pathDiagnostic ? pathDiagnostic : "EDITOR-SCENE-PATH-INVALID");
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        }
        std::error_code error;
        const bool exists = std::filesystem::exists(resolvedTarget, error);
        if (error)
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        bool bound = !savedSceneFileTarget_.empty() && resolvedTarget == savedSceneFileTarget_;
        if (!bound && exists && !savedSceneFileTarget_.empty()) {
            const bool previousExists = std::filesystem::exists(savedSceneFileTarget_, error);
            if (!error && previousExists)
                bound = std::filesystem::equivalent(savedSceneFileTarget_, resolvedTarget, error);
            if (error) {
                feedback("EDITOR-SCENE-FILE-IDENTITY-UNAVAILABLE");
                return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
            }
        }
        if (relativePath == activeScenePath_ && !bound) {
            feedback("EDITOR-SCENE-FILE-TARGET-CHANGED: reload or save to a different path.");
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        }
        if (exists && !bound && !overwriteExisting)
            return {false, false, EditorDiagnosticCode::SceneOverwriteRequired};
        checkingFile = bound;
        if (checkingFile) {
            handle = scenePersistence_.readResolved(resolvedTarget);
            saveYaml = std::move(yaml);
        } else
            handle = scenePersistence_.saveResolved(resolvedTarget, yaml);
    }
    if (!handle) {
        const auto* diagnostic = scenePersistence_.diagnostic();
        return {false,
            false,
            diagnostic && std::string_view(diagnostic) == "EDITOR-SCENE-OVERWRITE-REQUIRED"
                ? EditorDiagnosticCode::SceneOverwriteRequired
                : EditorDiagnosticCode::InvalidCommandPayload};
    }

    available->id = nextScenePersistenceTaskId_++;
    available->request = handle;
    available->status = ScenePersistenceTaskStatus::Pending;
    available->loading = loading;
    available->checkingFile = checkingFile;
    available->saveYaml.swap(saveYaml);
    available->resolvedTarget.swap(resolvedTarget);
    available->contentHash.swap(contentHash);
    available->documentEpoch = sceneDocumentEpoch_;
    available->relativePath.swap(preparedPath);
    available->diagnostic.clear();
    available->result.reset();
    result.snapshot.sceneIoPending = true;
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
        task.result = std::move(fileResult);
        (void)submitCommand(
            CompleteScenePersistenceCommand{task.id}, EditorCommandOrigin::Internal);
    }
}

void EditorShell::pollTasks()
{
    pollProjectSave();
    pollScenePersistence();
    if (sceneExport_.ready())
        (void)submitCommand(CompleteSceneExportCommand{}, EditorCommandOrigin::Internal);
    if (terrainBuild_.ready())
        (void)submitCommand(CompleteTerrainResourceBuildCommand{}, EditorCommandOrigin::Internal);
    projectPanel_.synchronize(*this);
    scenePanel_.synchronize(*this);
}

EditorCommandOutcome EditorShell::execute(const EditorCommandPayload& payload)
{
    return std::visit(
        [this](const auto& command) -> EditorCommandOutcome {
            using Command = std::decay_t<decltype(command)>;
            // Any independent command cancels a pending local reference drag.
            // Retain capture until release so UI cannot receive an orphan up.
            if constexpr (!std::is_same_v<Command, EditorInputCommand>
                && !std::is_same_v<Command, GetEditorStateQuery>
                && !std::is_same_v<Command, GetSceneTransformSettingsQuery>
                && !std::is_same_v<Command, GetSceneInspectorQuery>
                && !std::is_same_v<Command, GetSceneInspectorSelectionQuery>
                && !std::is_same_v<Command, GetSceneHierarchyQuery>
                && !std::is_same_v<Command, GetSceneComponentCatalogQuery>
                && !std::is_same_v<Command, GetScenePersistenceTaskQuery>
                && !std::is_same_v<Command, GetSceneExportTaskQuery>) {
                sceneReferenceDrag_.cancel();
                sceneViewPointer_.cancel();
            }
            // A gesture owns a prepared history branch. Other writes cannot
            // invalidate it halfway through a group operation.
            if (activeSceneSelectionTransformEditId() != 0
                && !std::is_same_v<Command, EditorInputCommand>
                && !std::is_same_v<Command, UpdateSceneSelectionTransformEditCommand>
                && !std::is_same_v<Command, EndSceneSelectionTransformEditCommand>
                && !std::is_same_v<Command, GetEditorStateQuery>
                && !std::is_same_v<Command, GetSceneTransformSettingsQuery>
                && !std::is_same_v<Command, GetSceneInspectorQuery>
                && !std::is_same_v<Command, GetSceneInspectorSelectionQuery>
                && !std::is_same_v<Command, GetSceneHierarchyQuery>
                && !std::is_same_v<Command, GetSceneComponentCatalogQuery>
                && !std::is_same_v<Command, GetScenePersistenceTaskQuery>
                && !std::is_same_v<Command, GetSceneExportTaskQuery>)
                return {false, false, EditorDiagnosticCode::WorkspaceBusy};
            // A single-object drag owns its prepared history branch just as a
            // group drag does. Queries and exact-target pose updates may proceed.
            if (activeTransformEditObjectId_ != 0 && !std::is_same_v<Command, EditorInputCommand>
                && !std::is_same_v<Command, ActivateControlCommand>
                && !std::is_same_v<Command, CloseProjectCommand>
                && !std::is_same_v<Command, SetSceneObjectTransformCommand>
                && !std::is_same_v<Command, SetSceneObjectTranslationCommand>
                && !std::is_same_v<Command, EndSceneTransformEditCommand>
                && !std::is_same_v<Command, GetEditorStateQuery>
                && !std::is_same_v<Command, GetSceneTransformSettingsQuery>
                && !std::is_same_v<Command, GetSceneInspectorQuery>
                && !std::is_same_v<Command, GetSceneInspectorSelectionQuery>
                && !std::is_same_v<Command, GetSceneHierarchyQuery>
                && !std::is_same_v<Command, GetSceneComponentCatalogQuery>
                && !std::is_same_v<Command, GetScenePersistenceTaskQuery>
                && !std::is_same_v<Command, GetSceneExportTaskQuery>)
                return {false, false, EditorDiagnosticCode::WorkspaceBusy};
            // Freeze authoring after acceptance, while the application drains its response.
            if (exitRequested_ && !std::is_same_v<Command, GetEditorStateQuery>
                && !std::is_same_v<Command, GetSceneTransformSettingsQuery>
                && !std::is_same_v<Command, RequestEditorExitCommand>)
                return {false, false, EditorDiagnosticCode::WorkspaceBusy};
            // All frontends reach the same object write barrier, including
            // nested property/reference and history commands. Hidden is view
            // policy only; locked protects both direct and inherited changes.
            if constexpr (std::is_same_v<Command, SetSceneObjectNameCommand>
                || std::is_same_v<Command, SetSceneObjectActiveCommand>
                || std::is_same_v<Command, SetSceneObjectTransformCommand>
                || std::is_same_v<Command, BeginSceneTransformEditCommand>
                || std::is_same_v<Command, AddSceneComponentCommand>
                || std::is_same_v<Command, RemoveSceneComponentCommand>
                || std::is_same_v<Command, SetSceneComponentPropertyCommand>) {
                const auto gate = sceneObjectWriteGate(
                    command.objectId, !std::is_same_v<Command, SetSceneObjectNameCommand>);
                if (!gate.success)
                    return gate;
            } else if constexpr (std::is_same_v<Command, SetSceneObjectTranslationCommand>
                || std::is_same_v<Command, DestroySceneObjectCommand>) {
                const auto gate = sceneObjectWriteGate(0, true);
                if (!gate.success)
                    return gate;
            } else if constexpr (std::is_same_v<Command, CreateSceneObjectCommand>) {
                const auto gate =
                    sceneObjectWriteGate(sceneDocument_.scene().rootId().value(), false);
                if (!gate.success)
                    return gate;
            } else if constexpr (std::is_same_v<Command, ReparentSceneObjectCommand>) {
                const auto* object = sceneDocument_.scene().findObject(
                    scene::GameObjectId::fromValue(command.objectId));
                if (!object)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                for (const auto id : {command.parentId, object->parentId().value()}) {
                    const auto gate = sceneObjectWriteGate(id, false);
                    if (!gate.success)
                        return gate;
                }
                const auto gate = sceneObjectWriteGate(command.objectId, true);
                if (!gate.success)
                    return gate;
            }
            if constexpr (std::is_same_v<Command, RequestEditorExitCommand>)
                return requestEditorExit(command.discardChanges);
            else if constexpr (std::is_same_v<Command, GetSceneTransformSettingsQuery>) {
                EditorCommandOutcome result;
                result.stateChanged = false;
                result.hasSceneTransformSettings = true;
                result.sceneTransformSettings = sceneTransformSettings();
                return result;
            } else if constexpr (std::is_same_v<Command, SetSceneTransformSettingsCommand>)
                return setSceneTransformSettings(command.settings);
            else if constexpr (std::is_same_v<Command, OpenProjectCommand>)
                return openProject(command);
            else if constexpr (std::is_same_v<Command, CreateProjectCommand>)
                return createProject(command);
            else if constexpr (std::is_same_v<Command, CloseProjectCommand>)
                return closeProject(command.discardChanges);
            else if constexpr (std::is_same_v<Command, SaveProjectCommand>)
                return saveProject();
            else if constexpr (std::is_same_v<Command, CompleteProjectSaveCommand>)
                return completeProjectSave();
            else if constexpr (std::is_same_v<Command, RenameProjectCommand>)
                return renameProject(command.name);
            else if constexpr (std::is_same_v<Command, SetProjectSettingsCommand>)
                return setProjectSettings(command);
            else if constexpr (std::is_same_v<Command, NewSceneCommand>)
                return newScene(command.discardChanges);
            else if constexpr (std::is_same_v<Command, DuplicateSceneCommand>)
                return duplicateScene();
            else if constexpr (std::is_same_v<Command, ExportSceneCommand>)
                return exportScene(command.relativeDirectory);
            else if constexpr (std::is_same_v<Command, GetSceneExportTaskQuery>)
                return sceneExportTaskSnapshot(command.taskId);
            else if constexpr (std::is_same_v<Command, CompleteSceneExportCommand>)
                return completeSceneExport();
            else if constexpr (std::is_same_v<Command, ShowTerrainBrowserCommand>) {
                if (!showTerrainBrowser(command.visible))
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
            } else if constexpr (std::is_same_v<Command, SelectTerrainPreviewCommand>) {
                EditorTerrainBrowser currentBindings;
                if (!currentBindings.refresh(projectResources()))
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                for (std::size_t i = 0; i < terrainBrowser_.entries().size(); ++i) {
                    const auto& entry = terrainBrowser_.entries()[i];
                    const auto current =
                        projectResources().find<resource::MaterialResource>(entry.previewId);
                    if (entry.previewId == command.previewId && current.ready()
                        && entry.previewGeneration == command.expectedGeneration
                        && current.generation() == command.expectedGeneration) {
                        terrainBrowserSelection_ = i;
                        updateTerrainBrowserDetails();
                        return {true, true, EditorDiagnosticCode::None};
                    }
                }
                return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
            } else if constexpr (std::is_same_v<Command, OpenTerrainPreviewCommand>) {
                return openTerrainPreview(command);
            } else if constexpr (std::is_same_v<Command, BuildTerrainResourceCommand>) {
                if (projectSaveRequest_ || projectManifestDirty_ || sceneExport_.pending())
                    return {false, false, EditorDiagnosticCode::WorkspaceBusy};
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
                ++resourceRevision_;
                sceneServiceSession_->publishOwners(
                    projectResources_.retainManager(), projectResources_.physicsWorld2D());
                projectManifest_ = *projectResources_.manifest();
                refreshFileSystem();
                sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                refreshSceneInspector();
                feedback("Terrain resource built and added to project");
                return {true, true, EditorDiagnosticCode::None};
            } else if constexpr (std::is_same_v<Command, ActivateControlCommand>) {
                if (command.source.empty())
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                if (EditorScriptFilePanel::handles(command.source)) {
                    if (modalBlocksActivation(command.source))
                        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
                    return scriptFilePanel_.activate(*this, command.source);
                }
                if (EditorScenePanel::handles(command.source)) {
                    if (modalBlocksActivation(command.source) && !scenePanel_.active())
                        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
                    return scenePanel_.activate(*this, command.source);
                }
                // Remote activation and native buttons must reach the same
                // presence command and preserve its actual nested receipt.
                if (command.source == "scene_object_add")
                    return submitCommand(CreateSceneObjectCommand("GameObject"))
                        .intoNestedOutcome();
                if (command.source == "scene_object_delete")
                    return submitCommand(DestroySceneObjectCommand{}).intoNestedOutcome();
                if (command.source == "script_presentation_retry") {
                    if (authoringUiBlocked())
                        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
                    const bool ready = retryScriptPresentation();
                    return {ready,
                        false,
                        ready ? EditorDiagnosticCode::None
                              : EditorDiagnosticCode::ScriptPresentationFailed};
                }
                if (EditorSceneTransformPanel::handles(command.source))
                    return sceneTransformPanel_.activate(*this, command.source);
                if (command.source == "tool_1" || command.source == "tool_2"
                    || command.source == "tool_3" || command.source == "tool_4") {
                    auto settings = sceneTransformSettings();
                    settings.tool = command.source == "tool_1" ? SceneTransformTool::Move
                        : command.source == "tool_2"           ? SceneTransformTool::Rotate
                        : command.source == "tool_3"           ? SceneTransformTool::Scale
                                                               : SceneTransformTool::Select;
                    return setSceneTransformSettings(settings);
                }
                if (command.source == "scene_transform_space")
                    return setSceneTransformSpace(
                        sceneTransformSpace_ == EditorSceneTransformSpace::World
                            ? EditorSceneTransformSpace::Local
                            : EditorSceneTransformSpace::World);
                if (command.source == "scene_transform_pivot")
                    return setSceneTransformPivot(
                        sceneTransformPivot_ == EditorSceneTransformPivot::Active
                            ? EditorSceneTransformPivot::Center
                            : sceneTransformPivot_ == EditorSceneTransformPivot::Center
                            ? EditorSceneTransformPivot::Individual
                            : EditorSceneTransformPivot::Active);
                if (command.source == "scene_transform_snap") {
                    auto settings = sceneTransformSnap_;
                    settings.enabled = !settings.enabled;
                    return setSceneTransformSnap(settings);
                }
                applyActivation(command.source);
            } else if constexpr (std::is_same_v<Command, SetTextValueCommand>) {
                if (command.source == "scene_ui_path")
                    return scenePanel_.edit(*this, command.value);
                if (EditorSceneTransformPanel::textSource(command.source))
                    return sceneTransformPanel_.edit(*this, command.source, command.value);
                if (command.source.empty() || command.source == "scene_name")
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                if (command.source == "scene_ui_path" || command.source == "project_ui_path"
                    || command.source == "project_ui_name") {
                    const auto layout = runtime().canvas().resolveLayout();
                    const auto widgetId = id(command.source);
                    const auto* widget = layout.findWidget(widgetId);
                    if (!layout.ok() || !widget || !widget->visibleInHierarchy
                        || !widget->enabledInHierarchy
                        || command.value.find_first_of("\r\n") != core::pmr::String::npos
                        || command.value.find('\0') != core::pmr::String::npos
                        || !runtime()
                            .setValue(widgetId, ui::UiControlValue::fromText(command.value))
                            .ok())
                        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                }
                applyEntryChanged(command.source, command.value);
            } else if constexpr (std::is_same_v<Command, EditorInputCommand>) {
                return applyPlatformInput(command);
            } else if constexpr (std::is_same_v<Command, SetInspectorScrollCommand>)
                applyInspectorScrollChanged(command.offset);
            else if constexpr (std::is_same_v<Command, DragInspectorThumbCommand>)
                applyDragInspectorThumb(command.top);
            else if constexpr (std::is_same_v<Command, SetGamePreviewStateCommand>)
                applyGamePreviewState(command.running, command.starting, command.status);
            else if constexpr (std::is_same_v<Command, SetSceneObjectViewCommand>) {
                return setSceneObjectView(command);
            } else if constexpr (std::is_same_v<Command, SelectSceneObjectCommand>) {
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
                if (!hasPrimary)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                core::pmr::Vector<scene::GameObjectId> additional{
                    &core::defaultCpuMemoryResource()};
                try {
                    additional.reserve(command.objectIds.size() - 1);
                } catch (const std::bad_alloc&) {
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                }
                for (const auto objectId : command.objectIds) {
                    if (objectId != command.primaryObjectId)
                        additional.push_back(scene::GameObjectId::fromValue(objectId));
                }
                if (!sceneDocument_.select(primaryId))
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                sceneViewportAdditionalSelection_.swap(additional);
                refreshSceneInspector();
            } else if constexpr (std::is_same_v<Command, FocusSceneSelectionCommand>) {
                return focusSceneSelection();
            } else if constexpr (std::is_same_v<Command, FocusSceneReferenceCommand>) {
                return focusSceneReference(command);
            } else if constexpr (std::is_same_v<Command, LocateSceneResourceCommand>) {
                return locateSceneResource(command);
            } else if constexpr (std::is_same_v<Command, AssignSceneReferenceCommand>) {
                return assignSceneReference(command);
            } else if constexpr (std::is_same_v<Command, SetSceneTransformSpaceCommand>) {
                return setSceneTransformSpace(command.space);
            } else if constexpr (std::is_same_v<Command, SetSceneTransformSnapCommand>) {
                return setSceneTransformSnap(command.settings);
            } else if constexpr (std::is_same_v<Command, SetSceneTransformPivotCommand>) {
                return setSceneTransformPivot(command.pivot);
            } else if constexpr (std::is_same_v<Command, TransformSceneSelectionCommand>) {
                return transformSceneSelection(command);
            } else if constexpr (std::is_same_v<Command, BeginSceneSelectionTransformEditCommand>) {
                return beginSceneSelectionTransformEdit(command.operation);
            } else if constexpr (std::is_same_v<Command,
                                     UpdateSceneSelectionTransformEditCommand>) {
                return updateSceneSelectionTransformEdit(command.gestureId, command.amount);
            } else if constexpr (std::is_same_v<Command, EndSceneSelectionTransformEditCommand>) {
                return endSceneSelectionTransformEdit(command.gestureId);
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
                try {
                    SceneHistoryEntry history;
                    history.kind = SceneHistoryEntry::Kind::Reparent;
                    history.objectId = objectId.value();
                    history.beforeParentId = beforeParentId.value();
                    history.afterParentId = parentId.value();
                    history.beforeChildIndex = beforeChildIndex;
                    history.afterChildIndex = command.childIndex;
                    core::pmr::Vector<SceneHistoryEntry> preparedUndo{
                        &core::defaultCpuMemoryResource()};
                    core::pmr::Vector<SceneHistoryEntry> retiredRedo{
                        &core::defaultCpuMemoryResource()};
                    preparedUndo.reserve(
                        std::min(sceneUndoHistory_.size() + 1, kMaxSceneHistoryEntries));
                    if (!sceneDocument_.reparentObject(objectId, parentId, command.childIndex))
                        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                    publishPreparedSceneHistory(preparedUndo, retiredRedo, std::move(history));
                } catch (const std::bad_alloc&) {
                    return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
                }
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
                return createSceneObjectWithHistory(command);
            } else if constexpr (std::is_same_v<Command, DestroySceneObjectCommand>) {
                return destroySceneObjectWithHistory();
            } else if constexpr (std::is_same_v<Command, SetSceneObjectNameCommand>) {
                const auto targetId = command.objectId != 0u
                    ? scene::GameObjectId::fromValue(command.objectId)
                    : sceneDocument_.selectedObject();
                auto* object = sceneDocument_.scene().findObject(targetId);
                if (!object || !EditorSceneDocument::validObjectName(command.name))
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                if (object->name() == command.name)
                    return {true, false, EditorDiagnosticCode::None};
                try {
                    // Reflection values in the history entry also allocate.
                    // Construct them inside the preparation failure boundary.
                    SceneHistoryEntry history;
                    history.kind = SceneHistoryEntry::Kind::Name;
                    history.objectId = targetId.value();
                    core::pmr::Vector<SceneHistoryEntry> preparedUndo{
                        &core::defaultCpuMemoryResource()};
                    core::pmr::Vector<SceneHistoryEntry> retiredRedo{
                        &core::defaultCpuMemoryResource()};
                    core::pmr::String sceneLabel{&core::defaultCpuMemoryResource()};
                    history.beforeName = object->name();
                    history.afterName = command.name;
                    preparedUndo.reserve(
                        std::min(sceneUndoHistory_.size() + 1, kMaxSceneHistoryEntries));
                    if (targetId == sceneDocument_.scene().rootId())
                        sceneLabel = command.name;
                    // basic_string assignment retains the old value on failure.
                    // All remaining content/history publication is allocation-free.
                    object->setName(command.name);
                    publishPreparedSceneHistory(preparedUndo, retiredRedo, std::move(history));
                    if (targetId == sceneDocument_.scene().rootId())
                        presentationState_.scene_.swap(sceneLabel);
                } catch (const std::bad_alloc&) {
                    return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
                }
                if (targetId == sceneDocument_.selectedObject())
                    refreshSceneInspector();
            } else if constexpr (std::is_same_v<Command, SetSceneObjectActiveCommand>) {
                const auto targetId = command.objectId
                    ? scene::GameObjectId::fromValue(command.objectId)
                    : sceneDocument_.selectedObject();
                auto* object = sceneDocument_.scene().findObject(targetId);
                if (!object)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                // The authoring schema stores active only for child GameObjects.
                if (targetId == sceneDocument_.scene().rootId())
                    return {false, false, EditorDiagnosticCode::SceneRootActiveUnsupported};
                if (object->active() == command.active)
                    return {true, false, EditorDiagnosticCode::None};
                try {
                    SceneHistoryEntry history;
                    history.kind = SceneHistoryEntry::Kind::Active;
                    history.objectId = targetId.value();
                    history.beforeActive = object->active();
                    history.afterActive = command.active;
                    core::pmr::Vector<SceneHistoryEntry> preparedUndo{
                        &core::defaultCpuMemoryResource()};
                    core::pmr::Vector<SceneHistoryEntry> retiredRedo{
                        &core::defaultCpuMemoryResource()};
                    preparedUndo.reserve(
                        std::min(sceneUndoHistory_.size() + 1, kMaxSceneHistoryEntries));
                    object->setActive(command.active);
                    publishPreparedSceneHistory(preparedUndo, retiredRedo, std::move(history));
                } catch (const std::bad_alloc&) {
                    return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
                }
                if (targetId == sceneDocument_.selectedObject())
                    refreshSceneInspector();
            } else if constexpr (std::is_same_v<Command, SetSceneObjectTranslationCommand>) {
                if (!std::isfinite(command.x) || !std::isfinite(command.y)
                    || !std::isfinite(command.z) || std::abs(command.x) > 100000.0f
                    || std::abs(command.y) > 100000.0f || std::abs(command.z) > 100000.0f)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                const auto id = sceneDocument_.selectedObject();
                const auto* object = sceneDocument_.scene().findObject(id);
                if (!object)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                auto transform = object->transform().localTransform();
                transform.translation = {command.x, command.y, command.z};
                return applySceneTransform(id, transform);
            } else if constexpr (std::is_same_v<Command, SetSceneObjectTransformCommand>) {
                scene::Transform requested;
                requested.translation = command.translation;
                requested.rotation = command.rotation;
                requested.scale = command.scale;
                scene::Transform transform;
                if (!EditorSceneTransformFrame::canonicalize(requested, transform))
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                const auto targetId = command.objectId != 0u
                    ? scene::GameObjectId::fromValue(command.objectId)
                    : sceneDocument_.selectedObject();
                const auto* object = sceneDocument_.scene().findObject(targetId);
                if (!object)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                return applySceneTransform(targetId, transform);
            } else if constexpr (std::is_same_v<Command, BeginSceneTransformEditCommand>) {
                return beginSingleSceneTransformEdit(command.objectId);
            } else if constexpr (std::is_same_v<Command, EndSceneTransformEditCommand>) {
                return endSingleSceneTransformEdit(command.objectId);
            } else if constexpr (std::is_same_v<Command, UndoSceneCommand>) {
                const bool batch = !sceneUndoHistory_.empty()
                    && (sceneUndoHistory_.back().kind == SceneHistoryEntry::Kind::TransformBatch
                        || sceneUndoHistory_.back().kind
                            == SceneHistoryEntry::Kind::ObjectPresence);
                const auto result = applySceneHistory(true);
                if (result.success && result.stateChanged && !batch)
                    sceneViewportAdditionalSelection_.clear();
                return result;
            } else if constexpr (std::is_same_v<Command, RedoSceneCommand>) {
                const bool batch = !sceneRedoHistory_.empty()
                    && (sceneRedoHistory_.back().kind == SceneHistoryEntry::Kind::TransformBatch
                        || sceneRedoHistory_.back().kind
                            == SceneHistoryEntry::Kind::ObjectPresence);
                const auto result = applySceneHistory(false);
                if (result.success && result.stateChanged && !batch)
                    sceneViewportAdditionalSelection_.clear();
                return result;
            } else if constexpr (std::is_same_v<Command, AddSceneComponentCommand>) {
                return addSceneComponentWithHistory(command);
            } else if constexpr (std::is_same_v<Command, RemoveSceneComponentCommand>) {
                return removeSceneComponentWithHistory(command);
            } else if constexpr (std::is_same_v<Command, SetSceneComponentPropertyCommand>) {
                SceneInspectorPropertyKind valueKind;
                if (!sceneInspectorKindForValueKind(command.valueKind, valueKind))
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                SceneHistoryEntry history;
                core::pmr::Vector<SceneHistoryEntry> preparedUndo{
                    &core::defaultCpuMemoryResource()};
                core::pmr::Vector<SceneHistoryEntry> retiredRedo{&core::defaultCpuMemoryResource()};
                const bool captureHistory = !applyingSceneHistory_;
                ui::WidgetId booleanControl;
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
                    if (valueKind == SceneInspectorPropertyKind::Boolean
                        && sceneDocument_.selectedObject().value() == command.objectId) {
                        const auto source = scenePropertyControlSource(*target);
                        for (const auto widgetId : sceneInspectorWidgetIds_) {
                            const auto* widget = runtime().canvas().findWidget(widgetId);
                            if (widget && widget->desc().name == source) {
                                booleanControl = widgetId;
                                break;
                            }
                        }
                    }
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
                    // Prepare capacity while the Scene and both history branches
                    // are intact. Publication below only moves owned entries.
                    preparedUndo.reserve(
                        std::min(sceneUndoHistory_.size() + 1, kMaxSceneHistoryEntries));
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
                const auto previousLinkCount =
                    history.beforeProperty.sceneObjectLinkListValue.size();
                if (result == SceneInspectorWriteResult::Changed && captureHistory)
                    publishPreparedSceneHistory(preparedUndo, retiredRedo, std::move(history));
                // Native widgets already contain user input; remote commands do
                // not. Project the accepted boolean without rebuilding controls
                // or interrupting input. Its source ID was prepared pre-mutation.
                if (result == SceneInspectorWriteResult::Changed && booleanControl.valid()
                    && runtime().findControl(booleanControl)
                    && !runtime()
                        .setValue(
                            booleanControl, ui::UiControlValue::fromBool(command.booleanValue))
                        .ok())
                    failSceneInspector("EDITOR-INSPECTOR-BOOLEAN-PROJECTION");
                if (result == SceneInspectorWriteResult::Changed
                    && (valueKind == SceneInspectorPropertyKind::ResourceReference
                        || valueKind == SceneInspectorPropertyKind::SceneObjectReference)
                    && sceneDocument_.selectedObject().value() == command.objectId) {
                    sceneInspectorReferenceFailureOwner_ = {};
                    sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                    refreshSceneInspector();
                }
                if (result == SceneInspectorWriteResult::Changed
                    && valueKind == SceneInspectorPropertyKind::SceneObjectLinkList
                    && previousLinkCount != command.sceneObjectLinkListValue.size()
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
                return replaceScriptText(command);
            } else if constexpr (std::is_same_v<Command, CloseScriptCommand>) {
                return closeScriptDocument(command.documentId, command.discardChanges);
            } else if constexpr (std::is_same_v<Command, SaveScriptCommand>) {
                if (scriptDocuments_.activeId() != command.documentId || command.documentId == 0)
                    return {false, false, EditorDiagnosticCode::StaleDocumentTarget};
                return saveScriptDocument();
            } else if constexpr (std::is_same_v<Command, ReloadScriptCommand>) {
                if (scriptDocuments_.activeId() != command.documentId || command.documentId == 0)
                    return {false, false, EditorDiagnosticCode::StaleDocumentTarget};
                return reloadScriptDocument(command.discardChanges);
            } else if constexpr (std::is_same_v<Command, SaveSceneCommand>) {
                return beginScenePersistence(false,
                    std::string_view(command.relativePath.data(), command.relativePath.size()),
                    false,
                    command.overwriteExisting);
            } else if constexpr (std::is_same_v<Command, LoadSceneCommand>) {
                return beginScenePersistence(true,
                    std::string_view(command.relativePath.data(), command.relativePath.size()),
                    command.discardChanges);
            } else if constexpr (std::is_same_v<Command, GetScenePersistenceTaskQuery>) {
                return scenePersistenceTaskSnapshot(command.taskId);
            } else if constexpr (std::is_same_v<Command, CompleteScenePersistenceCommand>) {
                const auto task = std::find_if(scenePersistenceTasks_.begin(),
                    scenePersistenceTasks_.end(),
                    [&](const auto& record) {
                        return record.id == command.taskId;
                    });
                if (task == scenePersistenceTasks_.end() || !task->result
                    || task->status != ScenePersistenceTaskStatus::Pending)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                auto fileResult = std::move(*task->result);
                task->result.reset();
                if (task->checkingFile) {
                    task->checkingFile = false;
                    if (!fileResult.success) {
                        fileResult.diagnostic = fileResult.missing
                            ? "EDITOR-SCENE-FILE-MISSING"
                            : "EDITOR-SCENE-FILE-READ-FAILED: " + fileResult.diagnostic;
                    } else if (fileResult.bytes != savedSceneFileBytes_) {
                        fileResult.success = false;
                        fileResult.diagnostic = "EDITOR-SCENE-FILE-EXTERNAL-CHANGE";
                    } else {
                        auto snapshot = std::move(task->saveYaml);
                        task->request =
                            scenePersistence_.saveResolved(task->resolvedTarget, snapshot);
                        if (task->request)
                            return {true, false, EditorDiagnosticCode::None};
                        fileResult.success = false;
                        fileResult.diagnostic = "EDITOR-SCENE-SAVE-SUBMIT-FAILED";
                    }
                    core::pmr::String{}.swap(task->saveYaml);
                }
                if (!fileResult.success) {
                    task->status = ScenePersistenceTaskStatus::Failed;
                    task->diagnostic = std::move(fileResult.diagnostic);
                    feedback(detail::sceneFileFailureMessage(task->diagnostic));
                    presentationState_.openOutput();
                    visible("output_panel", true);
                    return {true, true, EditorDiagnosticCode::None};
                }
                if (!task->loading) {
                    if (task->documentEpoch == sceneDocumentEpoch_) {
                        savedSceneHash_ = std::move(task->contentHash);
                        activeScenePath_ = task->relativePath;
                        savedSceneFileTarget_ = std::move(task->resolvedTarget);
                        savedSceneFileBytes_ = std::move(fileResult.bytes);
                    }
                    task->status = ScenePersistenceTaskStatus::Succeeded;
                    return {true, true, EditorDiagnosticCode::None};
                }
                core::pmr::String beforeHash, hashDiagnostic;
                if (task->documentEpoch != sceneDocumentEpoch_
                    || !EditorSceneAuthoringCodec::contentHash(
                        sceneDocument_, beforeHash, hashDiagnostic)
                    || beforeHash != task->contentHash) {
                    task->status = ScenePersistenceTaskStatus::Failed;
                    task->diagnostic = "EDITOR-SCENE-LOAD-STALE-DOCUMENT";
                    return {true, true, EditorDiagnosticCode::None};
                }
                EditorSceneDocument candidate(false);
                candidate.copySceneServicesFrom(sceneDocument_);
                core::pmr::String diagnostic(&core::defaultCpuMemoryResource());
                const auto yaml =
                    std::string_view(reinterpret_cast<const char*>(fileResult.bytes.data()),
                        fileResult.bytes.size());
                if (!EditorSceneAuthoringCodec::decode(yaml,
                        candidate,
                        &EditorSceneAuthoringCodec::isResourceReady,
                        sceneResourceManager(sceneDocument_),
                        diagnostic)
                    || !EditorSceneAuthoringCodec::contentHash(candidate, beforeHash, diagnostic)
                    || !sceneDocument_.replaceWith(std::move(candidate))) {
                    task->status = ScenePersistenceTaskStatus::Failed;
                    task->diagnostic =
                        diagnostic.empty() ? "EDITOR-SCENE-REPLACE-FAILED" : diagnostic;
                    return {true, true, EditorDiagnosticCode::None};
                }
                savedSceneHash_ = std::move(beforeHash);
                activeScenePath_ = task->relativePath;
                savedSceneFileTarget_ = std::move(task->resolvedTarget);
                savedSceneFileBytes_ = std::move(fileResult.bytes);
                ++sceneDocumentEpoch_;
                task->status = ScenePersistenceTaskStatus::Succeeded;
                task->diagnostic.clear();
                resetSceneDocumentInteraction();
                presentationState_.setScene(sceneDocument_.scene().name());
                presentationState_.select(
                    sceneDocument_.scene().findObject(sceneDocument_.selectedObject())->name());
                refreshSceneInspector();
                return {true, true, EditorDiagnosticCode::None};
            } else if constexpr (std::is_same_v<Command, GetEditorStateQuery>) {
                EditorCommandOutcome result;
                result.stateChanged = false;
                result.hasSnapshot = true;
                appendProjectSnapshot(result.snapshot);
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
                result.snapshot.scriptPresentationFailed = scriptPresentationFailed_;
                if (const auto* document = scriptDocuments_.active()) {
                    result.snapshot.scriptText = document->text;
                    const auto relative = document->path.lexically_relative(projectBrowser_.root());
                    if (!relative.empty() && !relative.is_absolute())
                        result.snapshot.scriptRelativePath = core::FilePath::utf8(relative);
                }
                return result;
            } else if constexpr (std::is_same_v<Command, GetSceneInspectorSelectionQuery>) {
                try {
                    EditorCommandOutcome result;
                    result.stateChanged = false;
                    result.hasSceneInspectorSelection = true;
                    result.sceneInspectorSelection = inspectorSelectionSnapshot();
                    return result;
                } catch (const std::bad_alloc&) {
                    return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
                }
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
                result.sceneInspector.properties = sceneInspectorProperties(objectId);
                return result;
            } else if constexpr (std::is_same_v<Command, GetSceneHierarchyQuery>) {
                EditorCommandOutcome result;
                result.stateChanged = false;
                result.hasSceneHierarchy = true;
                result.sceneHierarchy.documentEpoch = sceneDocumentEpoch_;
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
                    const auto view = sceneObjectView(entry.objectId);
                    if (!view.valid)
                        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
                    snapshotEntry.hidden = view.local.hidden;
                    snapshotEntry.locked = view.local.locked;
                    snapshotEntry.effectiveHidden = view.effective.hidden;
                    snapshotEntry.effectiveLocked = view.effective.locked;
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
