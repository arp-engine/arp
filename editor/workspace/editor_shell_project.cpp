#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/io/staged_directory.h"
#include "editor/project/project_scene_authoring.h"
#include "editor/workspace/editor_scene_file_format.h"
#include "editor/workspace/editor_scene_service_session.h"
#include "editor/workspace/editor_scene_transaction.h"
#include "editor/workspace/editor_shell.h"

#include <algorithm>
#include <system_error>

namespace arp::editor {

bool EditorShell::observeSceneHash(core::pmr::String& hash, core::pmr::String& diagnostic) const
{
    return EditorSceneFileFormat::observeHash(sceneDocument_,
        sceneAuthoringVersion_,
        projectRoot(),
        projectManifest_ ? &*projectManifest_ : nullptr,
        hash,
        diagnostic);
}

bool EditorShell::sceneDirty() const
{
    core::pmr::String hash, diagnostic;
    return !observeSceneHash(hash, diagnostic) || hash != savedSceneHash_;
}

void EditorShell::appendProjectSnapshot(EditorStateSnapshot& snapshot) const
{
    snapshot.sceneTransformSettings = sceneTransformSettings();
    snapshot.sceneCameraTarget = sceneViewportCamera_.target();
    snapshot.sceneCameraYaw = sceneViewportCamera_.yawDegrees();
    snapshot.sceneCameraPitch = sceneViewportCamera_.pitchDegrees();
    snapshot.sceneCameraDistance = sceneViewportCamera_.distance();
    snapshot.exitRequested = exitRequested_;
    snapshot.hasProject = bool(projectManifest_);
    snapshot.projectDirty = projectManifestDirty_;
    snapshot.sceneDirty = sceneDirty();
    snapshot.scenePath = activeScenePath_;
    snapshot.sceneRuntimeId = sceneDocument_.runtimeSceneId();
    snapshot.sceneIoPending = std::any_of(
        scenePersistenceTasks_.begin(), scenePersistenceTasks_.end(), [](const auto& task) {
            return task.status == ScenePersistenceTaskStatus::Pending;
        });
    snapshot.projectSavePending = bool(projectSaveRequest_);
    snapshot.sceneExportPending = sceneExport_.pending();
    snapshot.projectRoot = core::FilePath::utf8(projectRoot());
    snapshot.projectDiagnostic = projectDiagnostic_;
    if (projectManifest_) {
        snapshot.projectId = projectManifest_->id;
        snapshot.projectName = projectManifest_->name;
        snapshot.projectEntryScene = projectManifest_->entryScene;
    }
}

EditorCommandOutcome EditorShell::projectReplacementGate(bool discardChanges) const
{
    const auto gate = sceneReplacementGate(true);
    if (!gate.success)
        return gate;
    const bool scriptDirty = std::any_of(scriptDocuments_.documents().begin(),
        scriptDocuments_.documents().end(),
        [](const auto& document) {
            return document.dirty();
        });
    if (!discardChanges && (projectManifestDirty_ || sceneDirty() || scriptDirty))
        return {false, false, EditorDiagnosticCode::UnsavedChanges};
    return {true, false, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::requestEditorExit(bool discardChanges)
{
    if (exitRequested_)
        return {true, false, EditorDiagnosticCode::None};
    if (projectPanel_.modalActive() || scenePanel_.active() || scriptFilePanel_.active()) {
        feedback("EDITOR-EXIT-DIALOG-ACTIVE: finish the open dialog before exiting.");
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    }
    const auto gate = projectReplacementGate(discardChanges);
    if (!gate.success) {
        feedback(gate.diagnostic == EditorDiagnosticCode::UnsavedChanges
                ? "EDITOR-EXIT-UNSAVED: save your work or explicitly discard before exiting."
                : "EDITOR-EXIT-BUSY: finish pending tasks and stop Play before exiting.");
        presentationState_.openOutput();
        visible("output_panel", true);
        return gate;
    }
    exitRequested_ = true;
    return {true, true, EditorDiagnosticCode::None};
}

bool EditorShell::prepareProjectScene(
    EditorProjectResources& resources, EditorSceneDocument& document, core::pmr::String& diagnostic)
{
    if (!resources.manifest() || resources.manifest()->entryScene.empty())
        return true;
    const auto factory = ProjectSceneAuthoring::factory(resources.manager(),
        resources.physicsWorld2D(),
        diagnostic,
        document.resourceManagerProvider(),
        document.physicsWorld2DProvider());
    const auto asset =
        resources.manager().find<scene::SceneAssetResource>(resources.manifest()->entryScene);
    if (!factory || !asset.ready()) {
        if (diagnostic.empty())
            diagnostic = "EDITOR-PROJECT-ENTRY-SCENE-PREPARE";
        return false;
    }
    return document.loadRuntimeScene(*asset.get(), resources.manager(), *factory, diagnostic);
}

bool EditorShell::publishProject(EditorProjectResources&& resources, ProjectBrowserModel&& browser)
{
    // Prepare every value before touching the live workspace.
    auto empty = core::makeUnique<EditorSceneDocument>(false);
    core::pmr::String hash, diagnostic;
    auto services = core::makeShared<EditorSceneServiceSession>(
        resources.retainManager(), resources.physicsWorld2D());
    if (!empty->bindServiceSession(std::move(services), diagnostic)
        || !prepareProjectScene(resources, *empty, diagnostic)) {
        projectDiagnostic_ = diagnostic;
        return false;
    }
    if (!EditorSceneAuthoringCodec::contentHash(*empty, hash, diagnostic))
        return false;
    std::optional<ProjectManifest> manifest;
    if (resources.manifest())
        manifest = *resources.manifest();
    // The old scene must die while its ResourceManager is still alive.
    auto prepared = prepareSceneReplacement(
        *empty, diagnostic, EditorSceneDocument::ServicePolicy::AdoptCandidate);
    if (!prepared || commitSceneReplacement(*prepared, *empty) != SceneReplacementResult::Committed)
        return false;
    // Release reflection history on its owner thread, then old components,
    // while the original project resources are still alive.
    prepared.reset();
    empty.reset();
    while (scriptDocuments_.size())
        scriptDocuments_.erase(scriptDocuments_.size() - 1);
    pendingScriptFeedback_.clear();
    scriptChromePending_ = false;
    setScriptPresentationFailed(false);
    projectResources_ = std::move(resources);
    ++resourceRevision_;
    projectManifest_ = std::move(manifest);
    projectManifestDirty_ = false;
    projectDiagnostic_.clear();
    projectBrowser_ = std::move(browser);
    sceneAuthoringVersion_ = EditorSceneAuthoringCodec::contractVersion;
    savedSceneHash_ = std::move(hash);
    activeScenePath_.clear();
    savedSceneFileTarget_.clear();
    savedSceneFileBytes_.clear();
    ++sceneDocumentEpoch_;
    selectedFilePath_.clear();
    lastClickedScriptPath_.clear();
    cancelScriptPointerCapture();
    scriptFocused_ = false;
    (void)showTerrainBrowser(false);
    terrainBrowser_ = {};
    refreshFileSystem();
    refreshSceneInspector();
    return true;
}

EditorCommandOutcome EditorShell::openProject(const OpenProjectCommand& command)
{
    const auto gate = projectReplacementGate(command.discardChanges);
    if (!gate.success)
        return gate;
    if (command.root.empty() || command.root.find('\0') != core::pmr::String::npos)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    std::error_code error;
    const auto root = std::filesystem::canonical(std::filesystem::u8path(command.root), error);
    EditorProjectResources candidate;
    if (error || !candidate.load(root).success || !candidate.manifest()) {
        projectDiagnostic_ = error                     ? "EDITOR-PROJECT-OPEN-ROOT"
            : candidate.lastLoad().diagnostics.empty() ? "EDITOR-PROJECT-OPEN-MANIFEST-REQUIRED"
                                                       : candidate.lastLoad().diagnostics;
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    ProjectBrowserModel browser;
    browser.setRoot(root);
    if (!browser.refresh() || !publishProject(std::move(candidate), std::move(browser))) {
        projectDiagnostic_ = "EDITOR-PROJECT-OPEN-PUBLISH";
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::closeProject(bool discardChanges)
{
    const auto gate = projectReplacementGate(discardChanges);
    if (!gate.success)
        return gate;
    // Keep the same directory visible, with no live project identity/resources.
    ProjectBrowserModel browser;
    browser.setRoot(projectRoot());
    if (!browser.refresh() || !publishProject(EditorProjectResources{}, std::move(browser)))
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::createProject(const CreateProjectCommand& command)
{
    const auto gate = projectReplacementGate(command.discardChanges);
    if (!gate.success)
        return gate;
    // Bare-project creation has no inferred assets, sources or editor metadata.
    // Template bundles add their explicit files in the next authoring subphase.
    if (command.root.empty() || command.root.find('\0') != core::pmr::String::npos
        || !command.manifest.assetPackages.empty() || !command.manifest.scriptModules.empty()
        || !command.manifest.editorMetadata.empty()
        || (command.templateKind == ProjectTemplateKind::Empty
            && !command.manifest.entryScene.empty()))
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    auto manifest = command.manifest;
    core::pmr::String yaml;
    ProjectDiagnostic diagnostic;
    if (!ProjectManifestCodec::encode(command.manifest, yaml, diagnostic)) {
        projectDiagnostic_ = diagnostic.code;
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    core::io::StagedDirectory staging;
    const auto destination = std::filesystem::u8path(command.root);
    if (!staging.begin(destination)) {
        projectDiagnostic_ = staging.diagnostic();
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    if (!ProjectSceneAuthoring::prepareTemplate(
            staging.path(), manifest, command.templateKind, projectDiagnostic_))
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    if (!ProjectManifestCodec::encode(manifest, yaml, diagnostic)) {
        projectDiagnostic_ = diagnostic.code;
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    const auto written = core::io::replaceFileAtomically(
        core::FilePath::utf8(staging.path() / "project.arp.yaml"), {yaml.begin(), yaml.end()});
    EditorProjectResources validated;
    if (!written.success || !validated.load(staging.path()).success) {
        projectDiagnostic_ =
            written.success ? validated.lastLoad().diagnostics : written.diagnostic;
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    if (!staging.publish()) {
        projectDiagnostic_ = staging.diagnostic();
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    // The committed directory remains valid even if opening it subsequently fails.
    auto opened = openProject({command.root, command.discardChanges});
    if (!opened.success)
        projectDiagnostic_ = "EDITOR-PROJECT-CREATED-OPEN-FAILED: " + projectDiagnostic_;
    return opened;
}

EditorCommandOutcome EditorShell::renameProject(std::string_view name)
{
    if (!projectManifest_)
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    return setProjectSettings({core::pmr::String(name), projectManifest_->entryScene});
}

EditorCommandOutcome EditorShell::setProjectSettings(const SetProjectSettingsCommand& command)
{
    if (terrainBuild_.pending() || sceneExport_.pending())
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    if (!projectManifest_)
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    auto candidate = *projectManifest_;
    candidate.name = command.name;
    candidate.entryScene = command.entryScene;
    ProjectDiagnostic diagnostic;
    core::pmr::String yaml;
    if (!ProjectManifestCodec::encode(candidate, yaml, diagnostic))
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    if (!candidate.entryScene.empty()
        && !projectResources_.manager()
            .find<scene::SceneAssetResource>(candidate.entryScene)
            .ready()) {
        projectDiagnostic_ = "EDITOR-PROJECT-ENTRY-SCENE-NOT-READY";
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    if (candidate.name == projectManifest_->name
        && candidate.entryScene == projectManifest_->entryScene)
        return {true, false, EditorDiagnosticCode::None};
    core::pmr::String saved;
    if (!ProjectManifestCodec::encode(*projectResources_.manifest(), saved, diagnostic))
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    projectManifest_ = std::move(candidate);
    projectManifestDirty_ = yaml != saved;
    projectDiagnostic_.clear();
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::saveProject()
{
    if (!projectManifest_)
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    if (projectSaveRequest_ || terrainBuild_.pending() || sceneExport_.pending())
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    if (!projectManifest_->entryScene.empty()
        && !projectResources_.manager()
            .find<scene::SceneAssetResource>(projectManifest_->entryScene)
            .ready()) {
        projectDiagnostic_ = "EDITOR-PROJECT-ENTRY-SCENE-NOT-READY";
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    ProjectDiagnostic diagnostic;
    std::filesystem::path path;
    if (!ProjectManifestCodec::resolvePath(
            projectRoot(), "project.arp.yaml", ProjectPathIntent::ExistingFile, path, diagnostic)) {
        projectDiagnostic_ = diagnostic.code;
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    const auto disk = core::readTextFile(path, EditorProjectResources::maxManifestBytes);
    if (!disk.success || disk.text != projectResources_.manifestSnapshot()) {
        projectDiagnostic_ = "EDITOR-PROJECT-MANIFEST-EXTERNALLY-CHANGED";
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    core::pmr::String yaml;
    if (!ProjectManifestCodec::encode(*projectManifest_, yaml, diagnostic))
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    if (!projectManifestDirty_)
        return {true, false, EditorDiagnosticCode::None};
    projectSaveManifest_ = *projectManifest_;
    projectSaveSnapshot_ = yaml;
    projectSaveRequest_ =
        projectFiles_.replace(core::FilePath::utf8(path), {yaml.begin(), yaml.end()});
    if (!projectSaveRequest_) {
        projectSaveManifest_.reset();
        projectSaveSnapshot_.clear();
        projectDiagnostic_ = projectFiles_.diagnostic();
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    projectDiagnostic_.clear();
    return {true, true, EditorDiagnosticCode::None};
}

void EditorShell::pollProjectSave()
{
    if (!projectSaveRequest_ || projectSaveResult_)
        return;
    core::io::AtomicFileResult result;
    if (!projectFiles_.take(projectSaveRequest_, result))
        return;
    projectSaveResult_ = std::move(result);
    (void)submitCommand(CompleteProjectSaveCommand{}, EditorCommandOrigin::Internal);
}

EditorCommandOutcome EditorShell::completeProjectSave()
{
    if (!projectSaveRequest_ || !projectSaveResult_)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    auto result = std::move(*projectSaveResult_);
    projectSaveResult_.reset();
    projectSaveRequest_ = {};
    if (result.success) {
        core::pmr::String current;
        ProjectDiagnostic diagnostic;
        const bool encoded = ProjectManifestCodec::encode(*projectManifest_, current, diagnostic);
        projectManifestDirty_ = !encoded || current != projectSaveSnapshot_;
        projectResources_.publishSavedSettings(
            std::move(*projectSaveManifest_), std::move(projectSaveSnapshot_));
        projectSaveManifest_.reset();
        projectDiagnostic_.clear();
    } else {
        projectDiagnostic_ = result.diagnostic;
        projectSaveManifest_.reset();
        projectSaveSnapshot_.clear();
    }
    // Completion itself changes task/dirty state, including a failed IO result.
    return {true, true, EditorDiagnosticCode::None};
}

} // namespace arp::editor
