#include "2d/physics_components.h"
#include "3d/authoring_components.h"
#include "3d/components.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "editor/project/project_identity.h"
#include "editor/project/project_scene_authoring.h"
#include "editor/remote/editor_remote_protocol.h"
#include "editor/tests/fixtures/project_fixture.h"
#include "editor/tests/fixtures/scene_persistence_fixture.h"
#include "editor/workspace/editor_shell.h"
#include "resource/resource_contract.h"
#include "scene/camera.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {
using namespace arp;
using namespace arp::editor;

class WorkflowFixture final {
public:
    WorkflowFixture()
    {
        root = std::filesystem::path(ARP_SOURCE_DIR) / "build"
            / ("project-ui-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        ProjectDiagnostic diagnostic;
        assert(ProjectManifestCodec::decode(
            tests::ProjectFixture::document("assetPackages: []\nphysics2d: null\n"),
            manifest,
            diagnostic));
    }

    ~WorkflowFixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        assert(!error);
    }

    static void initialize(EditorShell& shell, const std::filesystem::path& root)
    {
        assert(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", root)
                .ok());
    }

    static EditorStateSnapshot state(EditorShell& shell)
    {
        const auto result = shell.submitCommand(GetEditorStateQuery{});
        assert(result.status == EditorResponseStatus::Completed && result.hasSnapshot);
        return result.snapshot;
    }

    static void completed(EditorShell& shell, EditorCommandPayload payload)
    {
        const auto result = shell.submitCommand(std::move(payload));
        if (result.status != EditorResponseStatus::Completed)
            std::fprintf(stderr,
                "workflow rejected diagnostic=%u project=%s\n",
                static_cast<unsigned>(result.diagnostic),
                result.snapshot.projectDiagnostic.c_str());
        assert(result.status == EditorResponseStatus::Completed);
    }

    static EditorStateSnapshot finishScene(EditorShell& shell, const EditorResponse& accepted)
    {
        return tests::ScenePersistenceFixture::finish(shell, accepted);
    }

    static void finishProject(EditorShell& shell)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            shell.pollTasks();
            if (!state(shell).projectSavePending)
                return;
            std::this_thread::yield();
        }
        assert(false && "project IO completion timeout");
    }

    static EditorStateSnapshot finishSceneIo(EditorShell& shell)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            shell.pollTasks();
            auto snapshot = state(shell);
            if (!snapshot.sceneIoPending)
                return snapshot;
            std::this_thread::yield();
        }
        assert(false && "scene UI IO completion timeout");
        return {};
    }

    static void finishExportUi(EditorShell& shell)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            shell.pollTasks();
            // The panel retains its returned task ID. Ordinary state only
            // exposes the busy flag, never another task's completion result.
            if (!state(shell).sceneExportPending)
                return;
            std::this_thread::yield();
        }
        assert(false && "scene package UI completion timeout");
    }

    static void rejected(
        EditorShell& shell, EditorCommandPayload payload, EditorDiagnosticCode code)
    {
        const auto before = shell.stateVersion();
        const auto result = shell.submitCommand(std::move(payload));
        assert(result.status != EditorResponseStatus::Completed && result.diagnostic == code);
        assert(shell.stateVersion() == before);
    }

    core::pmr::String path(std::string_view child) const
    {
        return core::FilePath::utf8(root / std::filesystem::u8path(child));
    }

    std::filesystem::path root;
    ProjectManifest manifest;
};

void transactions(WorkflowFixture& fixture)
{
    using F = WorkflowFixture;
    EditorShell shell;
    F::initialize(shell, fixture.root);
    assert(!F::state(shell).hasProject && !shell.sceneDirty());
    F::completed(shell, CreateProjectCommand{fixture.path("first"), fixture.manifest});
    assert(F::state(shell).hasProject && !shell.sceneDirty());
    F::rejected(shell,
        CreateProjectCommand{fixture.path("first"), fixture.manifest},
        EditorDiagnosticCode::ProjectOperationFailed);
    auto invalid = fixture.manifest;
    invalid.id = "invalid-id";
    F::rejected(shell,
        CreateProjectCommand{fixture.path("invalid"), invalid},
        EditorDiagnosticCode::ProjectOperationFailed);
    assert(!std::filesystem::exists(fixture.root / "invalid"));
    invalid = fixture.manifest;
    invalid.physics2d = physics2d::WorldConfig{};
    invalid.physics2d->maxBodies = 0;
    assert(
        shell.submitCommand(CreateProjectCommand{fixture.path("invalid-physics"), invalid}).status
        != EditorResponseStatus::Completed);
    assert(!std::filesystem::exists(fixture.root / "invalid-physics"));
    F::completed(shell, CreateProjectCommand{fixture.path("second"), fixture.manifest});
    F::completed(shell, OpenProjectCommand{fixture.path("first")});
    const auto originalRoot = shell.projectRoot();
    const auto originalSceneRoot = shell.sceneDocument().scene().rootId();
    F::rejected(shell,
        OpenProjectCommand{fixture.path("missing")},
        EditorDiagnosticCode::ProjectOperationFailed);
    F::rejected(
        shell, OpenProjectCommand{fixture.path("")}, EditorDiagnosticCode::ProjectOperationFailed);
    std::filesystem::create_directory(fixture.root / "corrupt");
    std::ofstream(fixture.root / "corrupt/project.arp.yaml") << "type: invalid\n";
    F::rejected(shell,
        OpenProjectCommand{fixture.path("corrupt")},
        EditorDiagnosticCode::ProjectOperationFailed);
    assert(shell.projectRoot() == originalRoot);
    assert(shell.sceneDocument().scene().rootId() == originalSceneRoot);

    // Selection alone is not an edit; Undo restores the content-based clean state.
    F::completed(shell, CreateSceneObjectCommand("kept"));
    const auto kept = shell.sceneDocument().selectedObject();
    const auto save = shell.submitCommand(SaveSceneCommand("main.arp.yaml"));
    const auto beforeSaveCompletion = shell.stateVersion();
    F::rejected(shell,
        CompleteScenePersistenceCommand{save.snapshot.scenePersistenceTaskId},
        EditorDiagnosticCode::InvalidCommandPayload);
    F::rejected(shell, CloseProjectCommand{true}, EditorDiagnosticCode::WorkspaceBusy);
    F::rejected(shell, LoadSceneCommand("main.arp.yaml"), EditorDiagnosticCode::WorkspaceBusy);
    assert(F::finishScene(shell, save).scenePersistenceTaskStatus
        == ScenePersistenceTaskStatus::Succeeded);
    assert(!shell.sceneDirty());
    assert(shell.stateVersion() > beforeSaveCompletion);
    F::completed(shell, SelectSceneObjectCommand{shell.sceneDocument().scene().rootId().value()});
    assert(!shell.sceneDirty());
    F::completed(shell, SelectSceneObjectCommand{kept.value()});
    SetSceneObjectTransformCommand transform;
    transform.translation = {1, 2, 3};
    F::completed(shell, transform);
    assert(shell.sceneDirty());
    F::rejected(shell, CloseProjectCommand{}, EditorDiagnosticCode::UnsavedChanges);
    F::rejected(shell, LoadSceneCommand("main.arp.yaml"), EditorDiagnosticCode::UnsavedChanges);
    F::completed(shell, UndoSceneCommand{});
    assert(!shell.sceneDirty());

    // A save only acknowledges its submitted content; a later edit stays dirty.
    const auto saving = shell.submitCommand(SaveSceneCommand("main.arp.yaml"));
    F::completed(shell, SetSceneObjectTranslationCommand{4, 5, 6});
    assert(F::finishScene(shell, saving).scenePersistenceTaskStatus
        == ScenePersistenceTaskStatus::Succeeded);
    assert(shell.sceneDirty());
    LoadSceneCommand discard("main.arp.yaml");
    discard.discardChanges = true;
    const auto loading = shell.submitCommand(discard);
    F::completed(shell, SetSceneObjectTranslationCommand{7, 8, 9});
    const auto stale = F::finishScene(shell, loading);
    assert(stale.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
        && stale.scenePersistenceTaskDiagnostic == "EDITOR-SCENE-LOAD-STALE-DOCUMENT");
    assert(shell.sceneDocument().scene().findObject(kept));
    assert(F::finishScene(shell, shell.submitCommand(discard)).scenePersistenceTaskStatus
        == ScenePersistenceTaskStatus::Succeeded);
    assert(!shell.sceneDirty());

    // Inactive dirty tabs must also block switching, and old tab IDs cannot be reused.
    std::ofstream(originalRoot / "a.arp") << "// a\n";
    std::ofstream(originalRoot / "b.arp") << "// b\n";
    assert(shell.openScriptFile(originalRoot / "a.arp"));
    const auto oldId = shell.activeScriptDocumentId();
    F::completed(shell, ReplaceScriptTextCommand(oldId, "// edited\n"));
    assert(shell.openScriptFile(originalRoot / "b.arp") && !shell.scriptDirty());
    F::rejected(
        shell, OpenProjectCommand{fixture.path("second")}, EditorDiagnosticCode::UnsavedChanges);
    F::completed(shell, OpenProjectCommand{fixture.path("second"), true});
    assert(shell.openScriptCount() == 0);
    std::ofstream(shell.projectRoot() / "c.arp") << "// c\n";
    assert(shell.openScriptFile(shell.projectRoot() / "c.arp"));
    assert(shell.activeScriptDocumentId() > oldId);
    F::rejected(
        shell, ReplaceScriptTextCommand(oldId, "stale"), EditorDiagnosticCode::StaleDocumentTarget);

    // A name-only save must preserve domain owners and retain edits made while saving.
    auto* manager = &shell.projectResources();
    F::completed(shell, RenameProjectCommand{core::pmr::String("saved name")});
    assert(shell.projectDirty());
    F::rejected(shell, CloseProjectCommand{}, EditorDiagnosticCode::UnsavedChanges);
    F::completed(shell, SaveProjectCommand{});
    const auto acceptedVersion = shell.stateVersion();
    F::rejected(shell, CloseProjectCommand{true}, EditorDiagnosticCode::WorkspaceBusy);
    F::completed(shell, RenameProjectCommand{core::pmr::String("newer draft")});
    F::finishProject(shell);
    assert(shell.stateVersion() > acceptedVersion && shell.projectDirty());
    assert(shell.projectManifest()->name == "newer draft" && &shell.projectResources() == manager);
    const auto disk = core::readTextFile(shell.projectRoot() / "project.arp.yaml");
    assert(disk.success && disk.text.find("saved name") != core::pmr::String::npos);
    F::completed(shell, SaveProjectCommand{});
    F::finishProject(shell);
    assert(!shell.projectDirty());
    F::completed(shell, RenameProjectCommand{core::pmr::String("unsaved draft")});
    std::ofstream(shell.projectRoot() / "project.arp.yaml", std::ios::binary)
        << "external content\n";
    F::rejected(shell, SaveProjectCommand{}, EditorDiagnosticCode::ProjectOperationFailed);
    assert(shell.projectDirty());
    assert(
        core::readTextFile(shell.projectRoot() / "project.arp.yaml").text == "external content\n");
    F::completed(shell, CloseProjectCommand{true});
    assert(!F::state(shell).hasProject && !shell.sceneDirty() && shell.openScriptCount() == 0);

    // Pending requests remain busy even if process-state publication arrives first.
    F::completed(shell, OpenProjectCommand{fixture.path("first")});
    shell.activate("run_play");
    shell.setGamePreviewState(false, false, "test stopped");
    F::rejected(shell, CloseProjectCommand{true}, EditorDiagnosticCode::WorkspaceBusy);
    assert(shell.consumeGamePreviewCommand() == GamePreviewModel::Request::Start);
    F::completed(shell, CloseProjectCommand{});

    auto physical = fixture.manifest;
    physical.physics2d = physics2d::WorldConfig{{0, -9.8f}, 4, 4, 2, 4, 8, 4, 4, 4, 1.f / 60.f};
    F::completed(shell, CreateProjectCommand{fixture.path("physical"), physical});
    F::completed(shell, CreateSceneObjectCommand("body"));
    F::completed(shell, AddSceneComponentCommand("RigidBody2DComponent"));
    F::completed(shell, AddSceneComponentCommand("Collider2DComponent"));
    auto world = shell.sceneDocument().physicsWorld2DProvider()();
    const auto* object =
        shell.sceneDocument().scene().findObject(shell.sceneDocument().selectedObject());
    const auto* authoredBody = object->getComponent<physics2d::RigidBody2DComponent>();
    assert(authoredBody && authoredBody->usage() == physics2d::ComponentUsage::Authoring
        && !authoredBody->handle());
    const auto body = authoredBody->handle();
    assert(world->telemetry().bodies == 0 && world->telemetry().colliders == 0);
    manager = &shell.projectResources();
    F::completed(shell, RenameProjectCommand{core::pmr::String("physical saved")});
    F::completed(shell, SaveProjectCommand{});
    F::finishProject(shell);
    assert(&shell.projectResources() == manager
        && shell.sceneDocument().physicsWorld2DProvider()() == world);
    assert(world->telemetry().bodies == 0 && world->telemetry().colliders == 0);
    F::completed(shell, CloseProjectCommand{true});
    physics2d::BodyState bodyState;
    assert(world->telemetry().bodies == 0 && world->telemetry().colliders == 0
        && !world->state(body, bodyState));
}

void sceneTemplates(WorkflowFixture& fixture)
{
    using F = WorkflowFixture;
    EditorShell shell;
    F::initialize(shell, fixture.root);
    auto manifest = fixture.manifest;
    manifest.entryScene = "00000000-0000-4000-8000-000000000091";
    F::completed(shell,
        CreateProjectCommand{fixture.path("game2d"), manifest, false, ProjectTemplateKind::Game2D});
    assert(shell.projectResources().find<scene::SceneAssetResource>(manifest.entryScene).ready());
    assert(shell.sceneDocument().runtimeSceneId() == manifest.entryScene);
    auto* camera = shell.sceneDocument().scene().findObjectByName("Camera2D");
    assert(
        camera && camera->getComponent<scene::CameraComponent>()->desc().visibleQueues.bits() == 1);
    assert(!shell.sceneDirty() && shell.projectManifest()->assetPackages.size() == 1);
    F::completed(shell, CloseProjectCommand{});
    F::completed(shell, OpenProjectCommand{fixture.path("game2d")});
    assert(shell.sceneDocument().scene().findObjectByName("Camera2D"));
    assert(shell.sceneDocument().runtimeSceneId() == manifest.entryScene);
    F::completed(shell, CloseProjectCommand{});
    manifest.entryScene = "00000000-0000-4000-8000-000000000092";
    F::completed(shell,
        CreateProjectCommand{fixture.path("game3d"), manifest, false, ProjectTemplateKind::Game3D});
    assert(shell.sceneDocument().scene().executionMode() == scene::SceneExecutionMode::Passive);
    assert(shell.sceneDocument().runtimeSceneId() == manifest.entryScene);
    assert(shell.sceneDocument()
            .scene()
            .findObjectByName("DirectionalLight")
            ->getComponent<scene3d::LightComponent>());
    assert(shell.sceneDocument()
            .scene()
            .findObjectByName("Sky")
            ->getComponent<scene3d::SkyComponent>());
    assert(!shell.sceneDirty());
    core::pmr::String diagnostic;
    const auto codecs = ProjectSceneAuthoring::factory(shell.projectResources(), {}, diagnostic);
    assert(codecs);
    assert(ProjectSceneAuthoring::exportScene(shell.sceneDocument().scene(),
        *codecs,
        manifest.entryScene,
        fixture.root / "exported-scene",
        diagnostic));
    assert(!ProjectSceneAuthoring::exportScene(shell.sceneDocument().scene(),
        *codecs,
        manifest.entryScene,
        fixture.root / "exported-scene",
        diagnostic));
    shell.sceneDocument()
        .scene()
        .createObject("unsupported")
        .addComponent<scene3d::TerrainAuthoringComponent>();
    assert(!ProjectSceneAuthoring::exportScene(shell.sceneDocument().scene(),
        *codecs,
        manifest.entryScene,
        fixture.root / "unsupported-export",
        diagnostic));
    assert(!std::filesystem::exists(fixture.root / "unsupported-export"));
    F::completed(shell, CloseProjectCommand{true});
    F::rejected(shell,
        CreateProjectCommand{
            fixture.path("missing-entry"), fixture.manifest, false, ProjectTemplateKind::Game3D},
        EditorDiagnosticCode::ProjectOperationFailed);
    assert(!std::filesystem::exists(fixture.root / "missing-entry"));
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        F::completed(shell, OpenProjectCommand{fixture.path(cycle % 2 ? "game2d" : "game3d")});
        F::completed(shell, CloseProjectCommand{});
        const auto released = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = released;
        assert(released.liveBytes == baseline.liveBytes
            && released.allocationCount - released.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    EditorShell startup;
    F::initialize(startup, fixture.root / "game3d");
    assert(startup.sceneDocument().scene().findObjectByName("Camera3D") && !startup.sceneDirty());
    std::puts(
        "PASS Editor 2D/3D template create/open/export: 20 cycles; core live bytes/count delta = 0");
}

void sceneDocuments(WorkflowFixture& fixture)
{
    using F = WorkflowFixture;
    EditorShell shell;
    F::initialize(shell, fixture.root);
    auto manifest = fixture.manifest;
    manifest.physics2d = physics2d::WorldConfig{{0, -9.8f}, 4, 4, 2, 4, 8, 4, 4, 4, 1.f / 60.f};
    F::completed(shell, CreateProjectCommand{fixture.path("scene-documents"), manifest});
    const auto world = shell.sceneDocument().physicsWorld2DProvider()();
    const auto root = shell.projectRoot();
    std::ofstream(root / "rules.arp") << "// rules\n";
    assert(shell.openScriptFile(root / "rules.arp"));
    const auto scriptId = shell.activeScriptDocumentId();
    F::completed(shell, ReplaceScriptTextCommand(scriptId, "// unsaved rules\n"));
    F::completed(shell, RenameProjectCommand{"unsaved project name"});
    F::completed(shell, CreateSceneObjectCommand("old world"));
    F::completed(shell, AddSceneComponentCommand("RigidBody2DComponent"));
    F::completed(shell, AddSceneComponentCommand("Collider2DComponent"));
    F::completed(shell, SetSceneObjectTranslationCommand{2, 3, 0});
    const auto body = shell.sceneDocument()
                          .scene()
                          .findObject(shell.sceneDocument().selectedObject())
                          ->getComponent<physics2d::RigidBody2DComponent>()
                          ->handle();
    F::rejected(shell, NewSceneCommand{}, EditorDiagnosticCode::UnsavedChanges);
    F::completed(shell, NewSceneCommand{true});
    const auto state = F::state(shell);
    assert(state.scene == "Untitled" && state.selection == "Untitled" && state.sceneDirty
        && state.scenePath.empty() && !state.sceneIoPending);
    assert(shell.sceneDocument().scene().executionMode() == scene::SceneExecutionMode::Passive
        && shell.sceneDocument().scene().objects().empty()
        && shell.sceneDocument().selectedObject() == shell.sceneDocument().scene().rootId());
    assert(shell.sceneDocument().physicsWorld2DProvider()() == world && state.projectDirty
        && shell.activeScriptDocumentId() == scriptId && shell.scriptDirty()
        && shell.scriptText() == "// unsaved rules\n");
    physics2d::BodyState staleBody;
    assert(!world->state(body, staleBody) && world->telemetry().bodies == 0
        && world->telemetry().colliders == 0);
    F::completed(shell, UndoSceneCommand{});
    assert(shell.sceneDocument().scene().objects().empty() && shell.sceneDirty());
    // Empty untitled documents are protected too; replacement does not close scripts.
    F::rejected(shell, NewSceneCommand{}, EditorDiagnosticCode::UnsavedChanges);
    F::completed(shell, CreateSceneObjectCommand("saved object"));
    const auto save = shell.submitCommand(SaveSceneCommand("first.arp.yaml"));
    assert(F::state(shell).sceneIoPending && F::state(shell).scenePath.empty());
    F::rejected(shell, NewSceneCommand{true}, EditorDiagnosticCode::WorkspaceBusy);
    const auto saved = F::finishScene(shell, save);
    assert(saved.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded
        && !saved.sceneIoPending && !saved.sceneDirty && saved.scenePath == "first.arp.yaml");
    F::completed(shell, SetSceneObjectTranslationCommand{4, 5, 0});
    const auto savedAs =
        F::finishScene(shell, shell.submitCommand(SaveSceneCommand("copy.arp.yaml")));
    assert(savedAs.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded
        && savedAs.scenePath == "copy.arp.yaml" && !shell.sceneDirty());
    const auto copy = core::readTextFile(root / "copy.arp.yaml");
    assert(copy.success && copy.text != core::readTextFile(root / "first.arp.yaml").text);
    F::rejected(shell,
        SaveSceneCommand("missing/copy.arp.yaml"),
        EditorDiagnosticCode::InvalidCommandPayload);
    assert(F::state(shell).scenePath == "copy.arp.yaml" && !shell.sceneDirty());
    std::ofstream(root / "broken.arp.yaml") << "type: broken\ncontractVersion: 99\n";
    const auto failed =
        F::finishScene(shell, shell.submitCommand(LoadSceneCommand("broken.arp.yaml")));
    assert(failed.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
        && !failed.scenePersistenceTaskDiagnostic.empty() && failed.scenePath == "copy.arp.yaml"
        && !shell.sceneDirty() && shell.sceneDocument().scene().findObjectByName("saved object"));
    F::completed(shell, NewSceneCommand{});
    LoadSceneCommand load("copy.arp.yaml");
    F::rejected(shell, load, EditorDiagnosticCode::UnsavedChanges);
    load.discardChanges = true;
    const auto reopened = F::finishScene(shell, shell.submitCommand(load));
    assert(reopened.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded
        && reopened.scene == "Untitled" && reopened.scenePath == "copy.arp.yaml"
        && !reopened.sceneDirty && !reopened.sceneIoPending);
    assert(shell.sceneDocument()
               .scene()
               .findObjectByName("saved object")
               ->transform()
               .localTransform()
               .translation.x
        == 4);
    shell.activate("run_play");
    shell.setGamePreviewState(false, false, "request remains queued");
    F::rejected(shell, NewSceneCommand{true}, EditorDiagnosticCode::WorkspaceBusy);
    F::rejected(shell, load, EditorDiagnosticCode::WorkspaceBusy);
    assert(shell.consumeGamePreviewCommand() == GamePreviewModel::Request::Start);
    shell.setGamePreviewState(true, false, "running");
    F::rejected(shell, NewSceneCommand{true}, EditorDiagnosticCode::WorkspaceBusy);
    shell.setGamePreviewState(false, true, "starting");
    F::rejected(shell, NewSceneCommand{true}, EditorDiagnosticCode::WorkspaceBusy);
    shell.setGamePreviewState(false, false, "stopped");
    F::completed(shell, SaveProjectCommand{});
    F::rejected(shell, NewSceneCommand{true}, EditorDiagnosticCode::WorkspaceBusy);
    F::rejected(shell, load, EditorDiagnosticCode::WorkspaceBusy);
    F::finishProject(shell);
    F::completed(shell, NewSceneCommand{});
    assert(shell.projectRoot() == root && shell.scriptDirty());
    std::puts(
        "PASS scene document commands: new/discard, owner retention, save-as, load failure, busy guards");
}

// Ordinary create/save/load/new/destruction lifecycle; no rendering or stress load.
void sceneDocumentLifetime(WorkflowFixture& fixture)
{
    using F = WorkflowFixture;
    auto manifest = fixture.manifest;
    manifest.physics2d = physics2d::WorldConfig{{0, -9.8f}, 4, 4, 2, 4, 8, 4, 4, 4, 1.f / 60.f};
    const auto root = fixture.root / "scene-lifetime";
    {
        EditorShell shell;
        F::initialize(shell, fixture.root);
        F::completed(shell, CreateProjectCommand{core::FilePath::utf8(root), manifest});
    }
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            EditorShell shell;
            F::initialize(shell, root);
            const auto world = shell.sceneDocument().physicsWorld2DProvider()();
            F::completed(shell, NewSceneCommand{});
            F::completed(shell, CreateSceneObjectCommand("lifetime body"));
            F::completed(shell, AddSceneComponentCommand("RigidBody2DComponent"));
            F::completed(shell, AddSceneComponentCommand("Collider2DComponent"));
            SaveSceneCommand save("loop.arp.yaml");
            save.overwriteExisting = true;
            const auto saved = F::finishScene(shell, shell.submitCommand(save));
            assert(saved.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded);
            F::completed(shell, NewSceneCommand{});
            assert(world->telemetry().bodies == 0 && world->telemetry().colliders == 0);
            LoadSceneCommand load("loop.arp.yaml");
            load.discardChanges = true;
            const auto restored = F::finishScene(shell, shell.submitCommand(load));
            assert(restored.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded
                && world->telemetry().bodies == 0 && world->telemetry().colliders == 0);
            F::completed(shell, NewSceneCommand{});
            assert(world->telemetry().bodies == 0 && world->telemetry().colliders == 0);
        }
        const auto released = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = released;
        assert(released.liveBytes == baseline.liveBytes
            && released.allocationCount - released.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS scene document CPU smoke: 20 warmed save/load/new/destruction cycles; core live bytes/count delta = 0, physics owners = 0");
}

void repeatedWorkspaceLifetime(WorkflowFixture& fixture)
{
    using F = WorkflowFixture;
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            EditorShell shell;
            F::initialize(shell, fixture.root);
            shell.activate("menu_Project");
            shell.activate("project_ui_new");
            shell.activate("project_ui_cancel");
            F::completed(shell, OpenProjectCommand{fixture.path("physical")});
            F::completed(shell, CreateSceneObjectCommand("lifetime"));
            F::completed(shell, AddSceneComponentCommand("RigidBody2DComponent"));
            F::completed(shell, AddSceneComponentCommand("Collider2DComponent"));
            auto world = shell.sceneDocument().physicsWorld2DProvider()();
            assert(world->telemetry().bodies == 0 && world->telemetry().colliders == 0);
            F::completed(shell, CloseProjectCommand{true});
            assert(world->telemetry().bodies == 0 && world->telemetry().colliders == 0);
            assert(!shell.sceneDirty() && shell.sceneDocument().scene().objects().empty());
        }
        const auto released = core::defaultCpuAllocator().stats();
        if (cycle == 0)
            baseline = released;
        assert(released.liveBytes == baseline.liveBytes);
        assert(released.allocationCount - released.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts("PASS Editor workspace: 20 cycles; core live bytes/count delta = 0");
    {
        EditorShell shell;
        F::initialize(shell, fixture.root);
        for (unsigned cycle = 0; cycle <= 20; ++cycle) {
            F::completed(shell, OpenProjectCommand{fixture.path("physical")});
            F::completed(shell, CreateSceneObjectCommand("in-place lifetime"));
            F::completed(shell, AddSceneComponentCommand("RigidBody2DComponent"));
            F::completed(shell, AddSceneComponentCommand("Collider2DComponent"));
            {
                const auto world = shell.sceneDocument().physicsWorld2DProvider()();
                assert(world->telemetry().bodies == 0 && world->telemetry().colliders == 0);
                F::completed(shell,
                    BeginSceneTransformEditCommand{shell.sceneDocument().selectedObject().value()});
                F::completed(shell, CloseProjectCommand{true});
                assert(world->telemetry().bodies == 0 && world->telemetry().colliders == 0);
            }
            const auto released = core::defaultCpuAllocator().stats();
            if (cycle == 0)
                baseline = released;
            assert(released.liveBytes == baseline.liveBytes);
            assert(released.allocationCount - released.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
        }
    }
    std::puts("PASS Editor in-place switching: 20 cycles; core live bytes/count delta = 0");
}

// Drive the loaded YAML controls through real hit-testing and UI events.
class ProjectUiFixture final {
public:
    static ui::WidgetId find(EditorShell& shell, const char* source)
    {
        auto id = shell.id(source);
        if (id.valid())
            return id;
        for (const auto candidate : shell.runtime().canvas().traversal())
            if (shell.runtime().canvas().findWidget(candidate)->name() == source)
                return candidate;
        return {};
    }

    static void click(EditorShell& shell, const char* source)
    {
        const auto widgetId = find(shell, source);
        const auto layout = shell.runtime().canvas().resolveLayout();
        assert(layout.ok());
        const auto* target = layout.findWidget(widgetId);
        assert(target);
        const float x = target->rect.x + target->rect.width * 0.5f;
        const float y = target->rect.y + target->rect.height * 0.5f;
        assert(shell.runtime().canvas().hitTest(layout, {x, y}).widgetId() == widgetId);
        assert(shell.runtime().dispatch(ui::UiInputEvent::pointerDown(x, y)).ok());
        assert(shell.runtime().dispatch(ui::UiInputEvent::pointerUp(x, y)).ok());
    }

    static bool visible(EditorShell& shell, const char* source)
    {
        return shell.runtime().canvas().findWidget(shell.id(source))->visible();
    }

    static void edit(EditorShell& shell, const char* source, core::pmr::String value)
    {
        click(shell, source);
        const auto result = shell.runtime().interact(
            ui::UiControlInteraction::editText(shell.id(source), std::move(value)));
        if (!result.ok())
            std::fprintf(stderr,
                "UI edit failed source=%s diagnostic=%s\n",
                source,
                result.diagnostic().message.c_str());
        assert(result.ok());
    }

    static void open(EditorShell& shell, const char* action)
    {
        click(shell, "menu_Project");
        assert(visible(shell, "project_ui_menu"));
        click(shell, action);
        assert(visible(shell, "project_ui_modal"));
    }
};

std::filesystem::path settingsProject(WorkflowFixture& fixture)
{
    const auto root = fixture.root / "settings";
    std::filesystem::create_directories(root / "assets");
    auto manifest = fixture.manifest;
    manifest.engineContract = "arp.scene-asset/2";
    manifest.entryScene = resource::deriveResourceUuid("settings", "initial");
    core::pmr::String diagnostic;
    assert(ProjectSceneAuthoring::prepareTemplate(
        root, manifest, ProjectTemplateKind::Game3D, diagnostic));
    resource::ResourceManager resources;
    const auto factory = ProjectSceneAuthoring::factory(resources, {}, diagnostic);
    assert(factory);
    // Enough ordinary scene assets to verify the bounded selector's scroll path.
    for (unsigned i = 0; i < 7; ++i) {
        core::pmr::String name("Scene ");
        core::appendUnsigned(name, i);
        const auto id = resource::deriveResourceUuid("settings", name);
        scene::Scene authored(
            name, scene::SceneId::fromValue(i + 1), scene::SceneExecutionMode::Passive);
        core::pmr::String path("assets/scene-");
        core::appendUnsigned(path, i);
        assert(ProjectSceneAuthoring::exportScene(
            authored, *factory, id, root / path.c_str(), diagnostic));
        manifest.assetPackages.push_back(path);
        if (id < manifest.entryScene)
            manifest.entryScene = id;
    }
    core::pmr::String yaml;
    ProjectDiagnostic error;
    assert(ProjectManifestCodec::encode(manifest, yaml, error));
    std::ofstream stream(root / "project.arp.yaml", std::ios::binary);
    stream.write(yaml.data(), yaml.size());
    assert(stream.good());
    return root;
}

void projectSettings(WorkflowFixture& fixture)
{
    using F = WorkflowFixture;
    using U = ProjectUiFixture;
    EditorShell shell;
    const auto root = settingsProject(fixture);
    F::initialize(shell, root);
    const auto name = shell.projectManifest()->name;
    const auto original = shell.projectManifest()->entryScene;
    auto* owner = &shell.projectResources();
    const auto sceneRoot = shell.sceneDocument().scene().rootId();
    const auto choices = owner->snapshotReferences("SceneAssetResource", 64);
    assert(choices.success && choices.entries.size() == 8);
    const auto target = choices.entries.back().reference.id;
    assert(target != original);
    const auto targetYaml =
        shell.projectResources().find<scene::SceneAssetResource>(target).get()->yaml();
    const auto targetName =
        core::YamlContract::text(core::YamlContract::parseSingleDocument(targetYaml), "name");
    F::rejected(shell,
        SetProjectSettingsCommand{"wrong name", "invalid"},
        EditorDiagnosticCode::InvalidCommandPayload);
    F::rejected(shell,
        SetProjectSettingsCommand{
            "wrong name", resource::deriveResourceUuid("settings", "missing")},
        EditorDiagnosticCode::ProjectOperationFailed);
    assert(shell.projectManifest()->name == name && !shell.projectDirty());
    assert(owner
            ->declareResource(
                resource::deriveResourceUuid("settings", "wrong-type"), "MaterialResource")
            .success);
    F::rejected(shell,
        SetProjectSettingsCommand{
            "wrong name", resource::deriveResourceUuid("settings", "wrong-type")},
        EditorDiagnosticCode::ProjectOperationFailed);

    U::open(shell, "project_ui_settings");
    U::click(shell, "project_ui_entry");
    const auto popup = U::find(shell, "project_ui_entry_popup");
    assert(popup.valid());
    const auto last = U::find(shell, "project_ui_entry_option_8");
    auto layout = shell.runtime().canvas().resolveLayout();
    assert(layout.ok() && layout.clipRect(last).height == 0);
    const auto rect = layout.findWidget(popup)->rect;
    assert(
        shell.runtime().dispatch(ui::UiInputEvent::scrollWheel(rect.x + 10, rect.y + 10, -1)).ok());
    layout = shell.runtime().canvas().resolveLayout();
    assert(shell.runtime().findControl(popup)->value().scroll.y == 40);
    unsigned visibleChoices = 0;
    for (auto child : shell.runtime().canvas().findWidget(popup)->childIds()) {
        const auto* row = layout.findWidget(child);
        if (!row->visibleInHierarchy)
            continue;
        ++visibleChoices;
        const auto clip = layout.clipRect(child);
        assert(clip.y == row->rect.y && clip.height == row->rect.height);
    }
    assert(visibleChoices == 6);
    assert(shell.runtime()
            .dispatch(ui::UiInputEvent::scrollWheel(rect.x + 10, rect.y + 10, -10000))
            .ok());
    layout = shell.runtime().canvas().resolveLayout();
    assert(layout.clipRect(last).height > 0);
    assert(shell.runtime().findControl(popup)->value().scroll.y == 120);
    U::click(shell, "project_ui_entry_option_8");
    assert(!U::find(shell, "project_ui_entry_popup").valid());
    // Selection is a local draft until submitting settings.
    assert(shell.projectManifest()->entryScene == original && !shell.projectDirty());
    U::click(shell, "project_ui_submit");
    F::finishProject(shell);
    assert(!shell.projectDirty() && shell.projectManifest()->entryScene == target);
    assert(
        &shell.projectResources() == owner && shell.sceneDocument().scene().rootId() == sceneRoot);
    U::click(shell, "project_ui_cancel");
    F::completed(shell, OpenProjectCommand{core::FilePath::utf8(root)});
    assert(shell.projectManifest()->entryScene == target
        && F::state(shell).projectEntryScene == target);
    assert(shell.sceneDocument().scene().name() == targetName);

    // A submitted snapshot acknowledges both fields; a later draft survives.
    owner = &shell.projectResources();
    F::completed(shell, SetProjectSettingsCommand{"submitted", original});
    F::completed(shell, SaveProjectCommand{});
    F::completed(shell, SetProjectSettingsCommand{"newer", target});
    F::finishProject(shell);
    assert(shell.projectDirty() && shell.projectManifest()->name == "newer"
        && shell.projectManifest()->entryScene == target && &shell.projectResources() == owner);
    ProjectManifest disk;
    ProjectDiagnostic error;
    assert(ProjectManifestCodec::decode(
        core::readTextFile(root / "project.arp.yaml").text, disk, error));
    assert(disk.name == "submitted" && disk.entryScene == original);
    F::completed(shell, SetProjectSettingsCommand{"submitted", original});
    assert(!shell.projectDirty());
    F::completed(shell, SetProjectSettingsCommand{"no entry", {}});
    F::completed(shell, SaveProjectCommand{});
    F::finishProject(shell);
    F::completed(shell, OpenProjectCommand{core::FilePath::utf8(root)});
    assert(shell.projectManifest()->entryScene.empty());
    // Ready is checked again at save, never inferred from an old selection.
    F::completed(shell, SetProjectSettingsCommand{"unloaded", target});
    assert(shell.projectResources().unload(target).success);
    F::rejected(shell, SaveProjectCommand{}, EditorDiagnosticCode::ProjectOperationFailed);
    U::open(shell, "project_ui_settings");
    assert(!U::find(shell, "project_ui_entry").valid()
        && !shell.runtime().canvas().findWidget(shell.id("project_ui_submit"))->enabled());
    U::click(shell, "project_ui_cancel");
    std::puts(
        "PASS project settings: exact Ready references, bounded scroll choice, draft/submit/reopen, snapshot isolation and owners");
}

void projectSettingsLifetime(WorkflowFixture& fixture)
{
    using F = WorkflowFixture;
    using U = ProjectUiFixture;
    const auto root = settingsProject(fixture);
    {
        // Clearing the entry uses the ordinary default scene on the next open.
        // Warm its process-lifetime reflection metadata as well as the packaged
        // entry below, so both explicit startup paths precede the baseline.
        EditorShell noEntry;
        F::initialize(noEntry, fixture.root);
    }
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            EditorShell shell;
            F::initialize(shell, root);
            U::open(shell, "project_ui_settings");
            core::pmr::String name("Settings lifetime ");
            core::appendUnsigned(name, cycle);
            U::edit(shell, "project_ui_name", name);
            U::click(shell, "project_ui_entry");
            U::click(shell, "project_ui_entry_option_0");
            U::click(shell, "project_ui_submit");
            F::finishProject(shell);
            U::click(shell, "project_ui_cancel");
        }
        const auto stats = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = stats;
        if (stats.liveBytes != baseline.liveBytes
            || stats.allocationCount - stats.deallocationCount
                != baseline.allocationCount - baseline.deallocationCount)
            std::fprintf(stderr,
                "settings cycle=%u live=%llu baseline=%llu count=%llu baselineCount=%llu\n",
                cycle,
                static_cast<unsigned long long>(stats.liveBytes),
                static_cast<unsigned long long>(baseline.liveBytes),
                static_cast<unsigned long long>(stats.allocationCount - stats.deallocationCount),
                static_cast<unsigned long long>(
                    baseline.allocationCount - baseline.deallocationCount));
        assert(stats.liveBytes == baseline.liveBytes
            && stats.allocationCount - stats.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS project settings CPU smoke: 20 warmed open/choose/save/destroy cycles; core live bytes/count delta = 0");
}

void sceneIdentityContract()
{
    using Y = core::YamlContract;
    core::pmr::String yaml, diagnostic, firstHash, secondHash;
    const auto identity = resource::deriveResourceUuid("identity-contract", "scene");
    EditorSceneDocument source(false), restored(false);
    assert(source.runtimeSceneId().empty());
    assert(EditorSceneAuthoringCodec::encode(source, yaml, diagnostic));
    assert(Y::required(Y::parseSingleDocument(yaml), "runtimeSceneId").is_null());
    assert(EditorSceneAuthoringCodec::contentHash(source, firstHash, diagnostic));
    assert(source.setRuntimeSceneId(identity));
    assert(!source.setRuntimeSceneId("filename.scene.yaml") && source.runtimeSceneId() == identity);
    assert(EditorSceneAuthoringCodec::contentHash(source, secondHash, diagnostic));
    assert(firstHash != secondHash);
    const auto first = source.createObject("First");
    const auto second = source.createObject("Second");
    assert(source.select(first));
    assert(EditorSceneAuthoringCodec::contentHash(source, firstHash, diagnostic));
    assert(source.select(second));
    assert(EditorSceneAuthoringCodec::contentHash(source, secondHash, diagnostic));
    assert(firstHash == secondHash);
    assert(EditorSceneAuthoringCodec::encode(source, yaml, diagnostic));
    assert(EditorSceneAuthoringCodec::decode(yaml, restored, nullptr, nullptr, diagnostic));
    assert(restored.runtimeSceneId() == identity);
    assert(EditorSceneAuthoringCodec::contentHash(restored, secondHash, diagnostic));
    assert(firstHash == secondHash);
    assert(!EditorSceneAuthoringCodec::decode(
        yaml + "runtimeSceneId: null\n", restored, nullptr, nullptr, diagnostic));
    assert(restored.runtimeSceneId() == identity);
    auto root = Y::parseSingleDocument(yaml);
    for (const auto invalid : {Y::Node(""), Y::Node("bad"), Y::Node(3), Y::Node::mapping()}) {
        root["runtimeSceneId"] = invalid;
        assert(!EditorSceneAuthoringCodec::decode(
            Y::serialize(root), restored, nullptr, nullptr, diagnostic));
        assert(diagnostic == "EDITOR-SCENE-RUNTIME-IDENTITY-INVALID");
        assert(restored.runtimeSceneId() == identity);
    }
    auto legacy = Y::Node::mapping();
    for (auto key : {"type", "name", "objects", "selected"})
        legacy[key] = Y::required(root, key);
    legacy["contractVersion"] = 2;
    const auto v2 = Y::serialize(legacy);
    assert(!EditorSceneAuthoringCodec::decode(v2, restored, nullptr, nullptr, diagnostic));
    assert(diagnostic.find("arp_editor_scene_migrate") != core::pmr::String::npos);
    core::pmr::String migrated;
    assert(EditorSceneAuthoringCodec::migrateVersion2(v2, identity, migrated, diagnostic));
    assert(EditorSceneAuthoringCodec::decode(migrated, restored, nullptr, nullptr, diagnostic));
    assert(restored.runtimeSceneId() == identity);
    assert(EditorSceneAuthoringCodec::migrateVersion2(v2, {}, migrated, diagnostic));
    assert(EditorSceneAuthoringCodec::decode(migrated, restored, nullptr, nullptr, diagnostic));
    assert(restored.runtimeSceneId().empty());
    migrated = "retained";
    assert(!EditorSceneAuthoringCodec::migrateVersion2(v2, "bad", migrated, diagnostic));
    assert(migrated == "retained");
    core::pmr::String generated = "retained";
    assert(!ProjectIdentity::create({}, generated, diagnostic) && generated == "retained");
    assert(ProjectIdentity::create("EditorScene/1", generated, diagnostic));
    assert(resource::isResourceUuid(generated));
    std::puts(
        "PASS scene identity contract: null/UUID, round-trip, selection/hash, rejection atomicity and explicit v2 migration");
}

void sceneIdentityLifetime(WorkflowFixture& fixture)
{
    using F = WorkflowFixture;
    auto baseline = core::defaultCpuAllocator().stats();
    core::pmr::String previous;
    // Keep the comparison identity outside the measured scope at fixed capacity.
    previous.reserve(36);
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            EditorShell shell;
            F::initialize(shell, fixture.root);
            F::completed(shell, NewSceneCommand{});
            const auto identity = F::state(shell).sceneRuntimeId;
            assert(resource::isResourceUuid(identity) && identity != previous);
            previous = identity;
            assert(shell.sceneDocument().runtimeSceneId() == identity);
            F::completed(shell, CreateSceneObjectCommand("Identity object"));
            const auto object = shell.sceneDocument().selectedObject();
            SetSceneObjectTransformCommand transform;
            transform.objectId = object.value();
            transform.translation = {1, 2, 3};
            F::completed(shell, transform);
            F::completed(shell, UndoSceneCommand{});
            F::completed(shell, RedoSceneCommand{});
            assert(shell.sceneDocument().runtimeSceneId() == identity);
            SaveSceneCommand save("identity.arp.yaml");
            save.overwriteExisting = cycle != 0;
            auto saved = shell.submitCommand(save);
            assert(saved.status == EditorResponseStatus::Completed && saved.hasSnapshot);
            F::rejected(shell, NewSceneCommand{true}, EditorDiagnosticCode::WorkspaceBusy);
            F::rejected(shell, DuplicateSceneCommand{}, EditorDiagnosticCode::WorkspaceBusy);
            transform.translation = {4, 5, 6};
            F::completed(shell, transform);
            assert(F::finishScene(shell, saved).scenePersistenceTaskStatus
                == ScenePersistenceTaskStatus::Succeeded);
            assert(shell.sceneDirty() && shell.sceneDocument().runtimeSceneId() == identity);
            // Save As is another authoring path for the same resource identity.
            SaveSceneCommand saveAs("identity-copy.arp.yaml");
            saveAs.overwriteExisting = cycle != 0;
            saved = shell.submitCommand(saveAs);
            assert(F::finishScene(shell, saved).scenePersistenceTaskStatus
                == ScenePersistenceTaskStatus::Succeeded);
            assert(shell.sceneDocument().runtimeSceneId() == identity && !shell.sceneDirty());
            const auto sourceFile = core::readTextFile(fixture.root / "identity-copy.arp.yaml");
            assert(sourceFile.success);
            auto* contents = shell.sceneDocument().scene().findObjectByName("Identity object");
            auto* resources = &shell.projectResources();
            F::completed(shell, SetSceneObjectTranslationCommand{7, 8, 9});
            F::completed(shell, DuplicateSceneCommand{});
            const auto independent = F::state(shell);
            assert(resource::isResourceUuid(independent.sceneRuntimeId)
                && independent.sceneRuntimeId != identity && independent.scenePath.empty()
                && independent.sceneDirty && &shell.projectResources() == resources);
            assert(shell.sceneDocument().scene().findObjectByName("Identity object") == contents
                && contents->transform().localTransform().translation.x == 7);
            F::completed(shell, UndoSceneCommand{});
            F::completed(shell, RedoSceneCommand{});
            assert(shell.sceneDocument().runtimeSceneId() == independent.sceneRuntimeId
                && contents->transform().localTransform().translation.x == 7);
            SaveSceneCommand independentSave("identity-independent.arp.yaml");
            independentSave.overwriteExisting = cycle != 0;
            assert(F::finishScene(shell, shell.submitCommand(independentSave))
                       .scenePersistenceTaskStatus
                == ScenePersistenceTaskStatus::Succeeded);
            assert(core::readTextFile(fixture.root / "identity-copy.arp.yaml").text
                == sourceFile.text);
            F::rejected(shell,
                SaveSceneCommand("missing/identity.arp.yaml"),
                EditorDiagnosticCode::InvalidCommandPayload);
            assert(shell.sceneDocument().runtimeSceneId() == independent.sceneRuntimeId
                && F::state(shell).scenePath == "identity-independent.arp.yaml");
            F::completed(shell, NewSceneCommand{});
            assert(F::state(shell).sceneRuntimeId != identity);
            LoadSceneCommand load("identity-copy.arp.yaml");
            load.discardChanges = true;
            const auto loaded = shell.submitCommand(load);
            assert(F::finishScene(shell, loaded).scenePersistenceTaskStatus
                == ScenePersistenceTaskStatus::Succeeded);
            assert(shell.sceneDocument().runtimeSceneId() == identity && !shell.sceneDirty());
        }
        const auto stats = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = stats;
        assert(stats.liveBytes == baseline.liveBytes);
        assert(stats.allocationCount - stats.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS identity CPU smoke: 20 warmed new/save/save-as/duplicate/undo/reopen/destroy cycles; core live bytes/count delta = 0");
}

void projectUi(WorkflowFixture& fixture)
{
    using F = WorkflowFixture;
    using U = ProjectUiFixture;
    EditorShell shell;
    F::initialize(shell, fixture.root);
    const auto initialRoot = shell.sceneDocument().scene().rootId();
    U::open(shell, "project_ui_new");
    // Modal consumes background pointer input and confines keyboard focus.
    auto layout = shell.runtime().canvas().resolveLayout();
    assert(shell.runtime().canvas().hitTest(layout, {105, 95}).widgetId()
        == shell.id("project_ui_modal"));
    assert(shell.runtime().dispatch(ui::UiInputEvent::pointerDown(105, 95)).ok());
    assert(shell.runtime().dispatch(ui::UiInputEvent::pointerUp(105, 95)).ok());
    const auto modalDraws = shell.drawCommands();
    assert(modalDraws.ok());
    bool hasInputBackground = false, hasShade = false;
    for (const auto& draw : modalDraws.commands()) {
        if (draw.widgetId == shell.id("project_ui_name_background"))
            hasInputBackground = true;
        if (draw.widgetId == shell.id("project_ui_modal")) {
            hasShade = true;
            assert(draw.color[3] > 0.71f && draw.color[3] < 0.73f);
        }
    }
    assert(hasInputBackground && hasShade);
    for (unsigned i = 0; i < 24; ++i) {
        assert(shell.runtime().dispatch(ui::UiInputEvent::keyboardNext()).ok());
        const auto* focused = shell.runtime().canvas().findWidget(shell.runtime().focusedControl());
        while (focused && focused->id() != shell.id("project_ui_modal"))
            focused = shell.runtime().canvas().findWidget(focused->parentId());
        assert(focused);
    }
    U::click(shell, "project_ui_submit");
    assert(!shell.projectManifest() && shell.sceneDocument().scene().rootId() == initialRoot);
    U::edit(shell, "project_ui_name", "UI project");
    const auto overflow = shell.runtime().interact(ui::UiControlInteraction::editText(
        shell.id("project_ui_name"), core::pmr::String(129, 'x')));
    assert(!overflow.ok() && overflow.diagnostic().code == ui::UiDiagnosticCode::TextEditOverflow);
    assert(shell.runtime().findControl(shell.id("project_ui_name"))->value().text == "UI project");
    // Real text input follows the same draft path.
    U::click(shell, "project_ui_path");
    assert(shell.runtime().dispatch(ui::UiInputEvent::textInput(fixture.path("ui-3d"))).ok());
    U::click(shell, "project_ui_3d");
    U::click(shell, "project_ui_cancel");
    assert(!shell.projectManifest() && !std::filesystem::exists(fixture.root / "ui-3d"));
    assert(shell.runtime().canvas().findWidget(shell.id("menu_Project"))->enabled());

    for (const auto* kind : {"3d", "2d", "empty"}) {
        U::open(shell, "project_ui_new");
        U::edit(shell, "project_ui_name", "UI project");
        U::edit(shell, "project_ui_path", fixture.path(core::pmr::String("ui-") + kind));
        const auto choice = core::pmr::String("project_ui_") + kind;
        U::click(shell, choice.c_str());
        assert(shell.runtime().canvas().findWidget(shell.id(choice))->desc().text.find("selected")
            != core::pmr::String::npos);
        U::click(shell, "project_ui_submit");
        assert(!U::visible(shell, "project_ui_modal") && shell.projectManifest());
        assert(shell.projectManifest()->engineContract == "arp.scene-asset/2");
        assert(shell.projectManifest()->entryScene.empty() == (std::string_view(kind) == "empty"));
        if (std::string_view(kind) == "3d")
            assert(shell.sceneDocument().scene().findObjectByName("Sky"));
        if (std::string_view(kind) == "2d")
            assert(shell.sceneDocument().scene().findObjectByName("Camera2D"));
        U::open(shell, "project_ui_close");
        U::click(shell, "project_ui_submit");
        assert(!shell.projectManifest());
    }
    U::open(shell, "project_ui_open");
    U::edit(shell, "project_ui_path", fixture.path("missing-ui"));
    U::click(shell, "project_ui_submit");
    assert(U::visible(shell, "project_ui_modal") && !shell.projectManifest());
    U::edit(shell, "project_ui_path", fixture.path("ui-3d"));
    U::click(shell, "project_ui_submit");
    assert(shell.projectManifest() && !U::visible(shell, "project_ui_modal"));
    const auto stableId = shell.projectManifest()->id;
    U::open(shell, "project_ui_settings");
    assert(shell.runtime().canvas().findWidget(shell.id("project_ui_submit"))->desc().text
        == "Save Settings");
    U::edit(shell, "project_ui_name", "Renamed in UI");
    U::click(shell, "project_ui_submit");
    F::finishProject(shell);
    assert(!shell.projectDirty() && shell.projectManifest()->name == "Renamed in UI");
    U::click(shell, "project_ui_cancel");
    F::completed(shell, OpenProjectCommand{fixture.path("ui-3d")});
    assert(shell.projectManifest()->name == "Renamed in UI"
        && shell.projectManifest()->id == stableId);

    F::completed(shell, CreateSceneObjectCommand("unsaved UI"));
    const auto editing = shell.sceneDocument().selectedObject();
    F::completed(shell, BeginSceneTransformEditCommand{editing.value()});
    U::open(shell, "project_ui_close");
    F::rejected(shell,
        EndSceneTransformEditCommand{editing.value()},
        EditorDiagnosticCode::InvalidCommandPayload);
    U::click(shell, "project_ui_submit");
    assert(U::visible(shell, "project_ui_discard"));
    // Background keyboard commands cannot delete scene objects while confirming.
    const auto selected = shell.sceneDocument().selectedObject();
    EditorInputCommand key;
    key.kind = EditorInputKind::KeyDown;
    key.key = EditorInputKey::Delete;
    F::completed(shell, key);
    assert(shell.sceneDocument().scene().findObject(selected));
    F::completed(shell, CreateSceneObjectCommand("new edit after confirmation"));
    U::click(shell, "project_ui_discard");
    assert(
        shell.projectManifest() && shell.sceneDirty() && !U::visible(shell, "project_ui_discard"));
    U::click(shell, "project_ui_submit");
    assert(U::visible(shell, "project_ui_discard"));
    const auto saving = shell.submitCommand(SaveSceneCommand("ui-save.arp.yaml"));
    // Re-enter confirmation while IO is pending; explicit discard cannot bypass busy.
    U::click(shell, "project_ui_discard");
    U::click(shell, "project_ui_submit");
    assert(shell.projectManifest() && U::visible(shell, "project_ui_modal"));
    F::finishScene(shell, saving);
    U::click(shell, "project_ui_submit");
    assert(!shell.projectManifest());

    F::completed(shell, OpenProjectCommand{fixture.path("ui-2d")});
    F::completed(shell, CreateSceneObjectCommand("discard after benign input"));
    U::open(shell, "project_ui_close");
    U::click(shell, "project_ui_submit");
    F::completed(shell, key);
    U::click(shell, "project_ui_discard");
    assert(!shell.projectManifest() && !U::visible(shell, "project_ui_modal"));

    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        U::open(shell, "project_ui_new");
        U::click(shell, "project_ui_cancel");
        const auto released = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = released;
        assert(released.liveBytes == baseline.liveBytes);
        assert(released.allocationCount - released.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS Project UI: pointer/keyboard, templates, settings, stale confirmation, busy gate, 20 modal cycles; core live bytes/count delta = 0");
}

void sceneUi(WorkflowFixture& fixture)
{
    using F = WorkflowFixture;
    using U = ProjectUiFixture;
    EditorShell shell;
    F::initialize(shell, fixture.root);
    F::completed(shell, CreateProjectCommand{fixture.path("scene-ui"), fixture.manifest});
    const auto root = shell.projectRoot();
    const auto menu = [&](const char* action) {
        U::click(shell, "menu_Scene");
        assert(U::visible(shell, "menu_popup"));
        U::click(shell, action);
    };
    menu("scene_ui_new");
    assert(U::visible(shell, "scene_ui_modal"));
    // Background pointer/key and cross-panel activation cannot edit behind a modal.
    shell.activate("project_ui_new");
    assert(!U::visible(shell, "project_ui_modal") && U::visible(shell, "scene_ui_modal"));
    const auto layout = shell.runtime().canvas().resolveLayout();
    assert(shell.runtime().canvas().hitTest(layout, {105, 95}).widgetId()
        == shell.id("scene_ui_modal"));
    for (unsigned i = 0; i < 16; ++i) {
        assert(shell.runtime().dispatch(ui::UiInputEvent::keyboardNext()).ok());
        const auto* focused = shell.runtime().canvas().findWidget(shell.runtime().focusedControl());
        while (focused && focused->id() != shell.id("scene_ui_modal"))
            focused = shell.runtime().canvas().findWidget(focused->parentId());
        assert(focused);
    }
    U::click(shell, "scene_ui_submit");
    assert(!U::visible(shell, "scene_ui_modal") && shell.sceneDirty());
    F::completed(shell, CreateSceneObjectCommand("authored object"));
    menu("save_scene");
    assert(U::visible(shell, "scene_ui_modal") && U::visible(shell, "scene_ui_path"));
    U::click(shell, "scene_ui_submit");
    assert(U::visible(shell, "scene_ui_modal") && !std::filesystem::exists(root / "main.arp.yaml"));
    U::edit(shell, "scene_ui_path", "main.arp.yaml");
    U::click(shell, "scene_ui_submit");
    const auto saved = F::finishSceneIo(shell);
    assert(saved.scenePath == "main.arp.yaml" && !saved.sceneDirty
        && !U::visible(shell, "scene_ui_modal"));

    F::completed(shell, SetSceneObjectTranslationCommand{1, 2, 3});
    // Scene Ctrl+S saves the current file, while Ctrl+Shift+S opens Save As.
    EditorInputCommand key;
    key.kind = EditorInputKind::KeyDown;
    key.key = EditorInputKey::LeftControl;
    F::completed(shell, key);
    key.key = EditorInputKey::S;
    F::completed(shell, key);
    assert(F::finishSceneIo(shell).scenePath == "main.arp.yaml" && !shell.sceneDirty());
    key.key = EditorInputKey::LeftShift;
    F::completed(shell, key);
    key.key = EditorInputKey::S;
    F::completed(shell, key);
    assert(U::visible(shell, "scene_ui_modal")
        && shell.runtime().findControl(shell.id("scene_ui_path"))->value().text == "main.arp.yaml");
    U::edit(shell, "scene_ui_path", "copy.arp.yaml");
    U::click(shell, "scene_ui_submit");
    assert(F::finishSceneIo(shell).scenePath == "copy.arp.yaml");
    const auto first = core::readTextFile(root / "main.arp.yaml");
    assert(first.success);
    F::completed(shell, SetSceneObjectTranslationCommand{4, 5, 6});
    menu("save_scene_as");
    U::edit(shell, "scene_ui_path", "main.arp.yaml");
    U::click(shell, "scene_ui_submit");
    assert(U::visible(shell, "scene_ui_confirm")
        && shell.runtime().canvas().findWidget(shell.id("scene_ui_confirm"))->desc().text
            == "Replace File");
    assert(core::readTextFile(root / "main.arp.yaml").text == first.text);
    F::completed(shell, CreateSceneObjectCommand("edit after overwrite prompt"));
    U::click(shell, "scene_ui_confirm");
    assert(!U::visible(shell, "scene_ui_confirm")
        && core::readTextFile(root / "main.arp.yaml").text == first.text);
    U::click(shell, "scene_ui_submit");
    U::click(shell, "scene_ui_confirm");
    assert(F::finishSceneIo(shell).scenePath == "main.arp.yaml"
        && core::readTextFile(root / "main.arp.yaml").text != first.text);

    // Explicit overwrite still cannot replace another schema or corrupt YAML.
    const auto manifest = core::readTextFile(root / "project.arp.yaml");
    menu("save_scene_as");
    U::edit(shell, "scene_ui_path", "project.arp.yaml");
    U::click(shell, "scene_ui_submit");
    U::click(shell, "scene_ui_confirm");
    const auto refused = F::finishSceneIo(shell);
    assert(U::visible(shell, "scene_ui_modal") && refused.scenePath == "main.arp.yaml"
        && core::readTextFile(root / "project.arp.yaml").text == manifest.text
        && shell.runtime()
                .canvas()
                .findWidget(shell.id("scene_ui_status"))
                ->desc()
                .text.find("ARP-SAVE-UNSUPPORTED-CONTRACT")
            != core::pmr::String::npos);
    U::click(shell, "scene_ui_cancel");
    std::ofstream(root / "broken-ui.arp.yaml", std::ios::binary) << "type: [broken\n";
    menu("save_scene_as");
    U::edit(shell, "scene_ui_path", "broken-ui.arp.yaml");
    U::click(shell, "scene_ui_submit");
    U::click(shell, "scene_ui_confirm");
    F::finishSceneIo(shell);
    assert(core::readTextFile(root / "broken-ui.arp.yaml").text == "type: [broken\n");
    U::click(shell, "scene_ui_cancel");

    // Cancel pending IO only closes presentation; the actual worker result is still observed.
    menu("save_scene_as");
    U::edit(shell, "scene_ui_path", "cancel-view.arp.yaml");
    U::click(shell, "scene_ui_submit");
    U::click(shell, "scene_ui_cancel");
    assert(!U::visible(shell, "scene_ui_modal"));
    assert(F::finishSceneIo(shell).scenePath == "cancel-view.arp.yaml");
    menu("open_scene");
    U::edit(shell, "scene_ui_path", "missing-ui.arp.yaml");
    U::click(shell, "scene_ui_submit");
    assert(U::visible(shell, "scene_ui_modal") && !F::state(shell).sceneIoPending);
    U::click(shell, "scene_ui_cancel");

    F::completed(shell, CreateSceneObjectCommand("discard scene only"));
    std::ofstream(root / "ui-rules.arp") << "// rules\n";
    assert(shell.openScriptFile(root / "ui-rules.arp"));
    const auto scriptId = shell.activeScriptDocumentId();
    F::completed(shell, ReplaceScriptTextCommand(scriptId, "// unsaved script\n"));
    F::completed(shell, RenameProjectCommand{"unsaved project"});
    menu("open_scene");
    U::edit(shell, "scene_ui_path", "main.arp.yaml");
    U::click(shell, "scene_ui_submit");
    assert(U::visible(shell, "scene_ui_confirm"));
    EditorInputCommand blocked;
    blocked.kind = EditorInputKind::KeyDown;
    blocked.key = EditorInputKey::Delete;
    F::completed(shell, blocked);
    U::click(shell, "scene_ui_confirm");
    const auto loaded = F::finishSceneIo(shell);
    assert(loaded.scenePath == "main.arp.yaml" && !loaded.sceneDirty && loaded.scriptDirty
        && loaded.projectDirty && shell.activeScriptDocumentId() == scriptId
        && !shell.sceneDocument().scene().findObjectByName("discard scene only"));
    menu("scene_ui_new");
    U::click(shell, "scene_ui_submit");
    assert(shell.sceneDirty() && shell.scriptDirty() && shell.projectDirty());
    menu("scene_ui_new");
    U::click(shell, "scene_ui_submit");
    assert(U::visible(shell, "scene_ui_confirm"));
    F::completed(shell, CreateSceneObjectCommand("confirm must expire"));
    U::click(shell, "scene_ui_confirm");
    assert(!U::visible(shell, "scene_ui_confirm")
        && shell.sceneDocument().scene().findObjectByName("confirm must expire"));
    U::click(shell, "scene_ui_cancel");
    assert(shell.runtime().canvas().findWidget(shell.id("menu_Project"))->enabled());
    std::puts(
        "PASS Scene UI: pointer/keyboard, save-as/overwrite, schema preservation, dirty guards, async completion and cancel");
}

// Long drafts use the same controls and commands as ordinary file operations.
void sceneExportUi(WorkflowFixture& fixture)
{
    using F = WorkflowFixture;
    using U = ProjectUiFixture;
    EditorShell shell;
    F::initialize(shell, fixture.root);
    auto manifest = fixture.manifest;
    manifest.entryScene = resource::deriveResourceUuid("export-ui", "entry");
    F::completed(shell,
        CreateProjectCommand{
            fixture.path("export-ui"), manifest, false, ProjectTemplateKind::Game2D});
    const auto root = shell.projectRoot();
    const auto sourcePackage = core::readTextFile(root / "assets/main/scene.yaml").text;
    const auto sourceManifest = core::readTextFile(root / "project.arp.yaml").text;
    const auto menu = [&](const char* action) {
        U::click(shell, "menu_Scene");
        assert(U::visible(shell, "menu_popup"));
        U::click(shell, action);
        assert(U::visible(shell, "scene_ui_modal"));
    };
    const auto text = [&](const char* source) -> const core::pmr::String& {
        return shell.runtime().canvas().findWidget(shell.id(source))->desc().text;
    };
    const auto status = [&]() -> const core::pmr::String& {
        // The native TextLayoutCache rejects CR/LF. CPU hit tests alone do not
        // exercise that renderer gate; guard every modal presentation state.
        for (const auto* source : {"scene_ui_description",
                 "scene_ui_description_detail",
                 "scene_ui_status",
                 "scene_ui_status_detail",
                 "output_status"})
            assert(text(source).find_first_of("\r\n") == core::pmr::String::npos);
        return text("scene_ui_status");
    };
    // Registered identity cannot silently become another registered package.
    menu("export_scene");
    assert(shell.runtime()
               .canvas()
               .findWidget(shell.id("scene_ui_path_label"))
               ->desc()
               .text.find("package folder")
        != core::pmr::String::npos);
    U::edit(shell, "scene_ui_path", "assets/registered");
    U::click(shell, "scene_ui_submit");
    assert(!F::state(shell).sceneExportPending
        && status().find("already registered") != core::pmr::String::npos
        && !std::filesystem::exists(root / "assets/registered"));
    U::click(shell, "scene_ui_cancel");
    assert(F::finishScene(shell, shell.submitCommand(SaveSceneCommand("source.arp.yaml")))
               .scenePersistenceTaskStatus
        == ScenePersistenceTaskStatus::Succeeded);
    const auto sourceFile = core::readTextFile(root / "source.arp.yaml").text;
    F::completed(shell, CreateSceneObjectCommand("unsaved copy content"));
    const auto* contents = shell.sceneDocument().scene().findObjectByName("unsaved copy content");
    auto* resources = &shell.projectResources();
    const auto before = F::state(shell);
    menu("duplicate_scene");
    (void)status();
    assert(text("scene_ui_description_detail").find("Undo history") != core::pmr::String::npos);
    assert(!U::visible(shell, "scene_ui_path") && !U::visible(shell, "scene_ui_confirm"));
    U::click(shell, "scene_ui_cancel");
    assert(F::state(shell).sceneRuntimeId == before.sceneRuntimeId
        && F::state(shell).scenePath == before.scenePath && shell.sceneDirty());
    menu("duplicate_scene");
    U::click(shell, "scene_ui_submit");
    const auto copied = F::state(shell);
    assert(!U::visible(shell, "scene_ui_modal") && copied.scenePath.empty() && copied.sceneDirty
        && copied.sceneRuntimeId != before.sceneRuntimeId && &shell.projectResources() == resources
        && shell.sceneDocument().scene().findObjectByName("unsaved copy content") == contents);
    F::completed(shell, UndoSceneCommand{});
    assert(shell.sceneDocument().scene().findObjectByName("unsaved copy content") == contents);

    menu("export_scene");
    U::click(shell, "scene_ui_submit");
    assert(status().find("Enter a new") != core::pmr::String::npos);
    for (const auto path : {"../outside", "missing/output", "assets/main"}) {
        U::edit(shell, "scene_ui_path", path);
        U::click(shell, "scene_ui_submit");
        assert(status().find("Export rejected") != core::pmr::String::npos
            && !F::state(shell).sceneExportPending);
    }
    F::completed(shell, SetTextValueCommand{"scene_ui_path", core::pmr::String(512, 'p')});
    F::rejected(shell,
        SetTextValueCommand{"scene_ui_path", core::pmr::String(513, 'p')},
        EditorDiagnosticCode::InvalidCommandPayload);
    U::edit(shell, "scene_ui_path", "assets/copied-package");
    U::click(shell, "scene_ui_submit");
    assert(F::state(shell).sceneExportPending
        && status().find("Exporting") != core::pmr::String::npos
        && text("scene_ui_status_detail").find("keeps the export running")
            != core::pmr::String::npos
        && !shell.runtime().canvas().findWidget(shell.id("scene_ui_submit"))->enabled()
        && !shell.runtime().canvas().findWidget(shell.id("scene_ui_path"))->enabled());
    F::rejected(shell, DuplicateSceneCommand{}, EditorDiagnosticCode::WorkspaceBusy);
    U::click(shell, "scene_ui_cancel");
    assert(!U::visible(shell, "scene_ui_modal") && F::state(shell).sceneExportPending);
    F::completed(shell, CreateSceneObjectCommand("later edit during export"));
    F::finishExportUi(shell);
    assert(!U::visible(shell, "scene_ui_modal") && shell.sceneDirty()
        && F::state(shell).scenePath.empty());
    const auto exported = core::readTextFile(root / "assets/copied-package/scene.yaml").text;
    assert(exported.find("unsaved copy content") != core::pmr::String::npos
        && exported.find("later edit during export") == core::pmr::String::npos);
    assert(!shell.projectResources().recordSnapshot(copied.sceneRuntimeId));
    // A deterministic real worker failure must leave its unrelated staging owner.
    const auto blocker = root / "assets/blocked.preparation.arp-staging";
    std::filesystem::create_directory(blocker);
    std::ofstream(blocker / "owner.txt") << "other writer";
    menu("export_scene");
    U::edit(shell, "scene_ui_path", "assets/blocked");
    U::click(shell, "scene_ui_submit");
    F::finishExportUi(shell);
    assert(status().find("export failed") != core::pmr::String::npos
        && !text("scene_ui_status_detail").empty() && U::visible(shell, "scene_ui_modal")
        && shell.runtime().canvas().findWidget(shell.id("scene_ui_submit"))->enabled()
        && core::readTextFile(blocker / "owner.txt").text == "other writer"
        && !std::filesystem::exists(root / "assets/blocked"));
    U::edit(shell, "scene_ui_path", "assets/retried");
    U::click(shell, "scene_ui_submit");
    F::finishExportUi(shell);
    assert(U::visible(shell, "scene_ui_modal")
        && status().find("not added to project") != core::pmr::String::npos
        && text("scene_ui_status_detail") == "assets/retried");
    U::click(shell, "scene_ui_cancel");
    assert(core::readTextFile(root / "source.arp.yaml").text == sourceFile
        && core::readTextFile(root / "assets/main/scene.yaml").text == sourcePackage
        && core::readTextFile(root / "project.arp.yaml").text == sourceManifest
        && shell.projectManifest()->entryScene == manifest.entryScene
        && shell.projectManifest()->assetPackages.size() == 1 && shell.sceneDirty());
    std::puts(
        "PASS scene export UI: real menu/pointer/text, copy/cancel/unsaved content, registered identity refusal, bounded paths, accepted task close/busy/edit, worker failure/retry, exported-unregistered and source/manifest preservation");
}

void sceneExportUiLifetime(WorkflowFixture& fixture)
{
    using F = WorkflowFixture;
    using U = ProjectUiFixture;
    auto manifest = fixture.manifest;
    manifest.entryScene = resource::deriveResourceUuid("export-ui-lifetime", "entry");
    const auto root = fixture.root / "export-ui-lifetime";
    {
        EditorShell shell;
        F::initialize(shell, fixture.root);
        F::completed(shell,
            CreateProjectCommand{
                core::FilePath::utf8(root), manifest, false, ProjectTemplateKind::Game2D});
    }
    const auto original = core::readTextFile(root / "project.arp.yaml").text;
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            EditorShell shell;
            F::initialize(shell, root);
            U::click(shell, "menu_Scene");
            U::click(shell, "duplicate_scene");
            U::click(shell, "scene_ui_cancel");
            U::click(shell, "menu_Scene");
            U::click(shell, "duplicate_scene");
            U::click(shell, "scene_ui_submit");
            U::click(shell, "menu_Scene");
            U::click(shell, "export_scene");
            core::pmr::String path("assets/export-");
            core::appendUnsigned(path, cycle);
            U::edit(shell, "scene_ui_path", path);
            U::click(shell, "scene_ui_submit");
            assert(F::state(shell).sceneExportPending);
            if (cycle % 2u)
                U::click(shell, "scene_ui_cancel");
            F::finishExportUi(shell);
            assert(std::filesystem::is_regular_file(root / path.c_str() / "scene.yaml"));
            if (U::visible(shell, "scene_ui_modal"))
                U::click(shell, "scene_ui_cancel");
            assert(!shell.projectResources().recordSnapshot(F::state(shell).sceneRuntimeId));
        }
        const auto stats = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = stats;
        assert(stats.liveBytes == baseline.liveBytes
            && stats.allocationCount - stats.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
        assert(core::readTextFile(root / "project.arp.yaml").text == original);
    }
    std::puts(
        "PASS scene export UI CPU smoke: 20 warmed loaded-project/copy/cancel/export/task-close-or-complete/destroy cycles; core live bytes/count delta = 0");
}

void longTextUi(WorkflowFixture& fixture)
{
    using F = WorkflowFixture;
    using U = ProjectUiFixture;
    EditorShell shell;
    F::initialize(shell, fixture.root);
    const core::pmr::String name(128, 'N');
    const auto path = fixture.path(core::pmr::String(80, 'p'));
    assert(path.size() > 64 && path.size() < 240);
    U::open(shell, "project_ui_new");
    U::click(shell, "project_ui_name");
    assert(shell.runtime().dispatch(ui::UiInputEvent::textInput(name)).ok());
    U::click(shell, "project_ui_path");
    assert(shell.runtime().dispatch(ui::UiInputEvent::textInput(path)).ok());
    U::click(shell, "project_ui_submit");
    assert(shell.projectManifest() && shell.projectManifest()->name == name);
    U::open(shell, "project_ui_settings");
    const core::pmr::String renamed(128, 'R');
    U::edit(shell, "project_ui_name", renamed);
    U::click(shell, "project_ui_submit");
    F::finishProject(shell);
    assert(!shell.projectDirty());
    U::click(shell, "project_ui_cancel");
    U::open(shell, "project_ui_close");
    U::click(shell, "project_ui_submit");
    U::open(shell, "project_ui_open");
    U::edit(shell, "project_ui_path", path);
    U::click(shell, "project_ui_submit");
    assert(shell.projectManifest() && shell.projectManifest()->name == renamed);

    shell.activate("save_scene_as");
    const core::pmr::String scenePath = core::pmr::String(80, 's') + ".arp.yaml";
    U::edit(shell, "scene_ui_path", scenePath);
    U::click(shell, "scene_ui_submit");
    assert(F::finishSceneIo(shell).scenePath == scenePath && !shell.sceneDirty());
    shell.activate("open_scene");
    U::edit(shell, "scene_ui_path", scenePath);
    U::click(shell, "scene_ui_submit");
    assert(F::finishSceneIo(shell).scenePath == scenePath && !shell.sceneDirty());

    // 512 is an input budget, independent of filesystem long-path policy.
    shell.activate("save_scene_as");
    U::edit(shell, "scene_ui_path", core::pmr::String(512, 'x'));
    assert(!shell.runtime().dispatch(ui::UiInputEvent::textInput("x")).ok());
    assert(shell.runtime().findControl(shell.id("scene_ui_path"))->value().text.size() == 512);
    U::click(shell, "scene_ui_cancel");
    std::puts(
        "PASS long text UI: 128-byte name saved/reopened, >64-byte project/scene paths, 512-byte draft boundary");
}

void sceneUiLifetime(WorkflowFixture& fixture)
{
    using F = WorkflowFixture;
    using U = ProjectUiFixture;
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            EditorShell shell;
            F::initialize(shell, fixture.root);
            for (const auto* action : {"scene_ui_new", "open_scene", "save_scene_as"}) {
                U::click(shell, "menu_Scene");
                U::click(shell, action);
                if (std::string_view(action) != "scene_ui_new")
                    U::edit(shell, "scene_ui_path", core::pmr::String(512, 'x'));
                U::click(shell, "scene_ui_cancel");
            }
        }
        const auto released = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = released;
        assert(released.liveBytes == baseline.liveBytes
            && released.allocationCount - released.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS Scene UI CPU smoke: 20 warmed modal/shell destruction cycles; core live bytes/count delta = 0");
}

// Exercise the real REST/MCP grammar and idempotency dispatcher without a socket.
class ProtocolClient final {
public:
    ProtocolClient(EditorShell& shell) : shell_(shell), protocol_("workflow-token") {}

    static core::pmr::String quoted(std::string_view value)
    {
        core::pmr::String result("\"");
        for (const char c : value) {
            if (c == '\\' || c == '"') {
                result += '\\';
                result += c;
            } else if (c == '\n')
                result += "\\n";
            else if (c == '\r')
                result += "\\r";
            else
                result += c;
        }
        return result + '"';
    }

    network::http::Response send(std::string_view body, bool mcp = false)
    {
        network::http::Request request;
        request.set_method(network::http::Method::Post);
        request.set_target(mcp ? "/mcp" : "/api/v1/commands");
        request.add_header("Authorization", "Bearer workflow-token");
        request.add_header("Accept", "application/json");
        request.add_header("MCP-Protocol-Version", "2025-11-25");
        request.set_content_type(network::http::ContentType::Json);
        request.set_body_text(body);
        return protocol_.handle(request, shell_);
    }

private:
    EditorShell& shell_;
    EditorRemoteProtocol protocol_;
};

void remoteSceneExport(WorkflowFixture& fixture)
{
    using F = WorkflowFixture;
    EditorShell shell;
    F::initialize(shell, fixture.root);
    F::completed(shell, CreateProjectCommand{fixture.path("remote-export"), fixture.manifest});
    F::completed(shell, NewSceneCommand{true});
    F::completed(shell, CreateSceneObjectCommand("remote submitted"));
    ProtocolClient client(shell);
    const auto version = shell.stateVersion();
    for (const auto body :
        {R"({"schemaVersion":1,"command":"scene.export","path":"out","overwriteExisting":true})",
            R"({"schemaVersion":1,"command":"scene.export","path":"out","taskId":1})",
            R"({"schemaVersion":1,"command":"scene.export","path":"out","expectedStateVersion":0})",
            R"({"schemaVersion":1,"command":"scene.export.task.get","taskId":1,"path":"out"})",
            R"({"schemaVersion":1,"command":"scene.export.task.get","taskId":0})"}) {
        assert(client.send(body).status().value() != 200 && shell.stateVersion() == version);
    }
    const auto request =
        R"({"schemaVersion":1,"command":"scene.export","path":"output","correlationId":93,"idempotencyKey":"export-once"})";
    const auto accepted = client.send(request);
    assert(accepted.status().value() == 200
        && accepted.body_text().find("\"sceneExportTask\":{\"taskId\":1")
            != std::string_view::npos);
    const auto replay = client.send(request);
    assert(replay.status().value() == 200 && replay.body_text() == accepted.body_text()
        && shell.stateVersion() == version + 1);
    assert(
        client
            .send(
                R"({"schemaVersion":1,"command":"scene.export","path":"different","idempotencyKey":"export-once"})")
            .status()
            .value()
        == 409);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool finished = false;
    while (std::chrono::steady_clock::now() < deadline) {
        shell.pollTasks();
        if (!F::state(shell).sceneExportPending) {
            finished = true;
            break;
        }
        std::this_thread::yield();
    }
    assert(finished);
    const auto result =
        client.send(R"({"schemaVersion":1,"command":"scene.export.task.get","taskId":1})");
    assert(result.status().value() == 200
        && result.body_text().find("exported_unregistered") != std::string_view::npos
        && result.body_text().find("\"registered\":false") != std::string_view::npos);
    const auto mcp = client.send(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"scene.export","arguments":{"path":"mcp-output","idempotencyKey":"mcp-export"}}})",
        true);
    assert(mcp.body_text().find("\"isError\":false") != std::string_view::npos);
    while (F::state(shell).sceneExportPending) {
        assert(std::chrono::steady_clock::now() < deadline);
        shell.pollTasks();
        std::this_thread::yield();
    }
    const auto query = client.send(
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"scene.export.task.get","arguments":{"taskId":2}}})",
        true);
    assert(query.body_text().find("exported_unregistered") != std::string_view::npos);
    assert(
        client
            .send(
                R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"scene.export","arguments":{"path":"bad","overwriteExisting":true}}})",
                true)
            .body_text()
            .find("unknown_tool_argument")
        != std::string_view::npos);
    // Explicit single-result retention: accepting task 2 retired task 1.
    assert(client.send(R"({"schemaVersion":1,"command":"scene.export.task.get","taskId":1})")
               .status()
               .value()
        != 200);
    assert(shell.sceneDirty() && shell.projectManifest()->assetPackages.empty());
}

void remoteSceneExportUi(WorkflowFixture& fixture)
{
    using F = WorkflowFixture;
    EditorShell shell;
    F::initialize(shell, fixture.root);
    auto manifest = fixture.manifest;
    manifest.entryScene = resource::deriveResourceUuid("export-ui-remote", "entry");
    F::completed(shell,
        CreateProjectCommand{
            fixture.path("export-ui-remote"), manifest, false, ProjectTemplateKind::Game2D});
    ProtocolClient client(shell);
    const auto activate = [&](const char* source) {
        const auto body =
            core::pmr::String("{\"schemaVersion\":1,\"command\":\"editor.activate\",\"source\":")
            + ProtocolClient::quoted(source) + "}";
        assert(client.send(body).status().value() == 200);
    };
    const auto sourceId = shell.sceneDocument().runtimeSceneId();
    activate("duplicate_scene");
    assert(ProjectUiFixture::visible(shell, "scene_ui_modal"));
    activate("scene_ui_cancel");
    assert(shell.sceneDocument().runtimeSceneId() == sourceId);
    activate("duplicate_scene");
    activate("scene_ui_submit");
    assert(shell.sceneDocument().runtimeSceneId() != sourceId && shell.sceneDirty());
    const auto opened = client.send(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"editor.activate","arguments":{"source":"export_scene"}}})",
        true);
    assert(opened.body_text().find("\"isError\":false") != std::string_view::npos
        && ProjectUiFixture::visible(shell, "scene_ui_modal"));
    assert(
        client
            .send(
                R"({"schemaVersion":1,"command":"editor.text.set","source":"scene_ui_path","value":"assets/remote-copy"})")
            .status()
            .value()
        == 200);
    activate("scene_ui_submit");
    F::finishExportUi(shell);
    assert(shell.runtime()
               .canvas()
               .findWidget(shell.id("scene_ui_status"))
               ->desc()
               .text.find("not added to project")
        != core::pmr::String::npos);
    assert(std::filesystem::is_regular_file(
               shell.projectRoot() / "assets/remote-copy/manifest.arpkg.yaml")
        && shell.projectManifest()->assetPackages.size() == 1);
    activate("scene_ui_cancel");
    assert(
        client.send(R"({"schemaVersion":1,"command":"editor.activate","source":"register_scene"})")
            .status()
            .value()
        == 400);
    std::puts(
        "PASS scene export UI REST/MCP: explicit source whitelist, duplicate/cancel/submit, bounded text and real exported-unregistered result");
}

void remoteDialogText(WorkflowFixture& fixture)
{
    EditorShell shell;
    WorkflowFixture::initialize(shell, fixture.root);
    ProtocolClient client(shell);
    const auto setPath =
        R"({"schemaVersion":1,"command":"editor.text.set","source":"scene_ui_path","value":"remote-ui.arp.yaml"})";
    const auto hiddenVersion = shell.stateVersion();
    const auto hidden = client.send(setPath);
    assert(hidden.status().value() == 500
        && hidden.body_text().find("invalid_command_payload") != std::string_view::npos
        && shell.stateVersion() == hiddenVersion);
    shell.activate("save_scene_as");
    assert(client.send(setPath).status().value() == 200);
    assert(shell.runtime().findControl(shell.id("scene_ui_path"))->value().text
        == "remote-ui.arp.yaml");
    const auto draftVersion = shell.stateVersion();
    for (const auto body :
        {R"({"schemaVersion":1,"command":"editor.text.set","source":"scene_search","value":"x"})",
            R"({"schemaVersion":1,"command":"editor.text.set","source":"scene_ui_path","value":"line\nbreak"})",
            R"({"schemaVersion":1,"command":"editor.text.set","source":"scene_ui_path","value":"nul\u0000byte"})",
            R"({"schemaVersion":1,"command":"editor.text.set","source":"scene_ui_path","value":"x","path":"unused"})"})
        assert(client.send(body).status().value() == 400 && shell.stateVersion() == draftVersion);
    const auto oversized =
        "{\"schemaVersion\":1,\"command\":\"editor.text.set\",\"source\":\"scene_ui_path\",\"value\":"
        + ProtocolClient::quoted(core::pmr::String(513, 'x')) + "}";
    assert(client.send(oversized).status().value() == 400 && shell.stateVersion() == draftVersion);
    const auto mcp = client.send(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"editor.text.set","arguments":{"source":"scene_ui_path","value":"mcp-ui.arp.yaml"}}})",
        true);
    assert(mcp.body_text().find("\"isError\":false") != std::string_view::npos);
    assert(
        shell.runtime().findControl(shell.id("scene_ui_path"))->value().text == "mcp-ui.arp.yaml");
    shell.activate("scene_ui_submit");
    assert(WorkflowFixture::finishSceneIo(shell).scenePath == "mcp-ui.arp.yaml");
    assert(!ProjectUiFixture::visible(shell, "scene_ui_modal"));
    shell.activate("project_ui_new");
    assert(
        client
            .send(
                R"({"schemaVersion":1,"command":"editor.text.set","source":"project_ui_name","value":"Remote draft"})")
            .status()
            .value()
        == 200);
    assert(
        shell.runtime().findControl(shell.id("project_ui_name"))->value().text == "Remote draft");
    const auto setName = [](std::uint32_t size) {
        return core::pmr::String(
                   "{\"schemaVersion\":1,\"command\":\"editor.text.set\",\"source\":\"project_ui_name\",\"value\":")
            + ProtocolClient::quoted(core::pmr::String(size, 'N')) + "}";
    };
    assert(client.send(setName(128)).status().value() == 200);
    const auto nameVersion = shell.stateVersion();
    assert(client.send(setName(129)).status().value() == 500);
    assert(shell.stateVersion() == nameVersion
        && shell.runtime().findControl(shell.id("project_ui_name"))->value().text.size() == 128);
    assert(client.send(setPath).status().value() == 500);
    shell.activate("project_ui_cancel");
    std::puts(
        "PASS dialog text REST/MCP: visible enabled drafts, bounded grammar, hidden/cross-modal rejection and actual save");
}

void remoteProjectSettings(WorkflowFixture& fixture)
{
    EditorShell shell;
    const auto root = fixture.root / "settings";
    WorkflowFixture::initialize(shell, root);
    ProtocolClient client(shell);
    const auto choices = shell.projectResources().snapshotReferences("SceneAssetResource", 64);
    assert(choices.success && !choices.entries.empty());
    const auto entry = choices.entries.back().reference.id;
    const auto body =
        core::pmr::String(
            "{\"schemaVersion\":1,\"command\":\"project.settings.set\",\"name\":\"Remote settings\",\"entryScene\":")
        + ProtocolClient::quoted(entry) + ",\"idempotencyKey\":\"settings-test\"}";
    assert(client.send(body).status().value() == 200);
    const auto version = shell.stateVersion();
    assert(client.send(body).status().value() == 200 && shell.stateVersion() == version);
    auto conflicting = body;
    conflicting.replace(conflicting.find(entry), entry.size(), "");
    assert(client.send(conflicting).status().value() == 409 && shell.stateVersion() == version);
    for (const auto* invalid :
        {R"({"schemaVersion":1,"command":"project.settings.set","name":"x"})",
            R"({"schemaVersion":1,"command":"project.settings.set","name":"x","entryScene":null})",
            R"({"schemaVersion":1,"command":"project.settings.set","name":"x","entryScene":"","path":"x"})",
            R"({"schemaVersion":1,"command":"project.rename","name":"x","entryScene":""})"})
        assert(client.send(invalid).status().value() == 400 && shell.stateVersion() == version);
    const auto mcp = client.send(
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"project.settings.set","arguments":{"name":"MCP settings","entryScene":""}}})",
        true);
    assert(mcp.body_text().find("\"isError\":false") != std::string_view::npos);
    assert(shell.projectManifest()->entryScene.empty()
        && shell.projectManifest()->name == "MCP settings");
    WorkflowFixture::completed(shell, SaveProjectCommand{});
    WorkflowFixture::finishProject(shell);
    WorkflowFixture::completed(shell, OpenProjectCommand{core::FilePath::utf8(root)});
    assert(shell.projectManifest()->name == "MCP settings"
        && shell.projectManifest()->entryScene.empty());
    assert(client.send(R"({"schemaVersion":1,"command":"editor.state.get"})")
               .body_text()
               .find("\"projectEntryScene\":\"\"")
        != std::string_view::npos);
    std::puts(
        "PASS project settings REST/MCP: complete payload, entry fingerprint, strict fields and saved startup snapshot");
}

void remoteSceneDocuments(WorkflowFixture& fixture)
{
    using F = WorkflowFixture;
    EditorShell shell;
    F::initialize(shell, fixture.root);
    ProtocolClient client(shell);
    const auto command =
        R"({"schemaVersion":1,"command":"scene.new","idempotencyKey":"new-scene"})";
    assert(client.send(command).status().value() == 200 && shell.sceneDirty());
    const auto identity = shell.sceneDocument().runtimeSceneId();
    assert(resource::isResourceUuid(identity));
    const auto snapshot = client.send(R"({"schemaVersion":1,"command":"editor.state.get"})");
    assert(snapshot.body_text().find("\"sceneRuntimeId\":" + ProtocolClient::quoted(identity))
        != std::string_view::npos);
    const auto version = shell.stateVersion();
    assert(client.send(command).status().value() == 200 && shell.stateVersion() == version);
    assert(client.send(R"({"schemaVersion":1,"command":"scene.new"})").status().value() == 409);
    assert(
        client
            .send(
                R"({"schemaVersion":1,"command":"scene.new","discardChanges":true,"idempotencyKey":"new-scene"})")
            .status()
            .value()
        == 409);
    assert(shell.stateVersion() == version);
    for (const auto body : {R"({"schemaVersion":1,"command":"scene.new","discardChanges":1})",
             R"({"schemaVersion":1,"command":"scene.new","name":"unused"})",
             R"({"schemaVersion":1,"command":"scene.new","path":"unused"})",
             R"({"schemaVersion":1,"command":"scene.new","source":"unused"})",
             R"({"schemaVersion":1,"command":"scene.new","documentId":1})"})
        assert(client.send(body).status().value() == 400 && shell.stateVersion() == version);
    const auto stale = client.send(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"scene.new","arguments":{"discardChanges":true,"expectedStateVersion":0}}})",
        true);
    assert(stale.body_text().find("stale_state_version") != std::string_view::npos
        && shell.stateVersion() == version);
    const auto invalid = client.send(
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"scene.new","arguments":{"discardChanges":true,"path":"unused"}}})",
        true);
    assert(invalid.body_text().find("unknown_tool_argument") != std::string_view::npos);
    const auto mcp =
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"scene.new","arguments":{"discardChanges":true,"idempotencyKey":"mcp-new-scene"}}})";
    assert(client.send(mcp, true).body_text().find("\"isError\":false") != std::string_view::npos);
    const auto mcpVersion = shell.stateVersion();
    assert(client.send(mcp, true).body_text().find("\"isError\":false") != std::string_view::npos
        && shell.stateVersion() == mcpVersion);
    const auto saved =
        F::finishScene(shell, shell.submitCommand(SaveSceneCommand("remote-scene.arp.yaml")));
    assert(saved.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded);
    const auto copy =
        F::finishScene(shell, shell.submitCommand(SaveSceneCommand("remote-scene-copy.arp.yaml")));
    assert(copy.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded);
    const auto guardedSave =
        R"({"schemaVersion":1,"command":"scene.save","path":"remote-scene.arp.yaml","idempotencyKey":"guarded-save"})";
    const auto guardVersion = shell.stateVersion();
    assert(
        client.send(guardedSave).status().value() == 409 && shell.stateVersion() == guardVersion);
    assert(
        client
            .send(
                R"({"schemaVersion":1,"command":"scene.save","path":"remote-scene.arp.yaml","overwriteExisting":true,"idempotencyKey":"guarded-save"})")
            .status()
            .value()
        == 409);
    for (const auto body :
        {R"({"schemaVersion":1,"command":"scene.save","path":"remote-scene.arp.yaml","overwriteExisting":1})",
            R"({"schemaVersion":1,"command":"scene.new","overwriteExisting":true})"})
        assert(client.send(body).status().value() == 400);
    const auto replaced = client.send(
        R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"scene.save","arguments":{"path":"remote-scene.arp.yaml","overwriteExisting":true}}})",
        true);
    assert(replaced.body_text().find("\"isError\":false") != std::string_view::npos);
    F::finishSceneIo(shell);
    const auto state = client.send(R"({"schemaVersion":1,"command":"editor.state.get"})");
    assert(
        state.body_text().find("\"scenePath\":\"remote-scene.arp.yaml\"") != std::string_view::npos
        && state.body_text().find("\"sceneIoPending\":false") != std::string_view::npos);
    const auto tools = client.send(R"({"jsonrpc":"2.0","id":4,"method":"tools/list"})", true);
    assert(tools.body_text().find("scene.new") != std::string_view::npos);
    assert(tools.body_text().find("scene.duplicate") != std::string_view::npos);
    const auto beforeDuplicate = shell.sceneDocument().runtimeSceneId();
    const auto duplicate =
        R"({"schemaVersion":1,"command":"scene.duplicate","idempotencyKey":"duplicate-scene"})";
    assert(client.send(duplicate).status().value() == 200);
    const auto duplicated = shell.sceneDocument().runtimeSceneId();
    assert(resource::isResourceUuid(duplicated) && duplicated != beforeDuplicate);
    const auto duplicatedVersion = shell.stateVersion();
    assert(client.send(duplicate).status().value() == 200
        && shell.stateVersion() == duplicatedVersion
        && shell.sceneDocument().runtimeSceneId() == duplicated);
    for (const auto body :
        {R"({"schemaVersion":1,"command":"scene.duplicate","discardChanges":true})",
            R"({"schemaVersion":1,"command":"scene.duplicate","path":"copy.arp.yaml"})",
            R"({"schemaVersion":1,"command":"scene.duplicate","name":"copy"})",
            R"({"schemaVersion":1,"command":"scene.duplicate","expectedStateVersion":0})"})
        assert(client.send(body).status().value() != 200
            && shell.sceneDocument().runtimeSceneId() == duplicated);
    const auto mcpDuplicate =
        R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"scene.duplicate","arguments":{"idempotencyKey":"mcp-duplicate"}}})";
    assert(client.send(mcpDuplicate, true).body_text().find("\"isError\":false")
        != std::string_view::npos);
    const auto mcpIdentity = shell.sceneDocument().runtimeSceneId();
    assert(mcpIdentity != duplicated);
    assert(client.send(mcpDuplicate, true).body_text().find("\"isError\":false")
            != std::string_view::npos
        && shell.sceneDocument().runtimeSceneId() == mcpIdentity);
    assert(
        client
            .send(
                R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"scene.duplicate","arguments":{"documentId":1}}})",
                true)
            .body_text()
            .find("unknown_tool_argument")
        != std::string_view::npos);
    std::puts(
        "PASS scene.new REST/MCP: strict grammar, dirty guards, state versions, idempotency and path snapshots");
}

void remoteTransactions(WorkflowFixture& fixture)
{
    using F = WorkflowFixture;
    EditorShell shell;
    F::initialize(shell, fixture.root);
    ProtocolClient client(shell);
    assert(client.send(R"({"schemaVersion":1,"command":"editor.activate","source":"workspace_3D"})")
               .status()
               .value()
        == 200);
    assert(shell.workspace() == "3D");
    assert(
        client
            .send(R"({"schemaVersion":1,"command":"editor.activate","source":"workspace_unknown"})")
            .status()
            .value()
        == 400);
    const auto open = "{\"schemaVersion\":1,\"command\":\"project.open\",\"path\":"
        + ProtocolClient::quoted(fixture.path("first")) + ",\"idempotencyKey\":\"project-open\"}";
    assert(client.send(open).status().value() == 200);
    const auto openedVersion = shell.stateVersion();
    assert(client.send(open).status().value() == 200 && shell.stateVersion() == openedVersion);
    const auto differentDiscard = "{\"schemaVersion\":1,\"command\":\"project.open\",\"path\":"
        + ProtocolClient::quoted(fixture.path("first"))
        + ",\"discardChanges\":true,\"idempotencyKey\":\"project-open\"}";
    auto conflict = client.send(differentDiscard);
    assert(conflict.status().value() == 409
        && conflict.body_text().find("idempotency_key_conflict") != std::string_view::npos);
    const auto differentPath = "{\"schemaVersion\":1,\"command\":\"project.open\",\"path\":"
        + ProtocolClient::quoted(fixture.path("missing")) + ",\"idempotencyKey\":\"project-open\"}";
    assert(client.send(differentPath).status().value() == 409);
    const auto state = client.send(R"({"schemaVersion":1,"command":"editor.state.get"})");
    assert(state.status().value() == 200);
    assert(state.body_text().find("\"hasProject\":true") != std::string_view::npos);
    assert(state.body_text().find("\"sceneDirty\":false") != std::string_view::npos);
    const auto rename = client.send(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"project.rename","arguments":{"name":"remote name"}}})",
        true);
    assert(rename.status().value() == 200 && shell.projectManifest()->name == "remote name");
    const auto unsaved = client.send(R"({"schemaVersion":1,"command":"project.close"})");
    assert(unsaved.status().value() == 409
        && unsaved.body_text().find("unsaved_changes") != std::string_view::npos);
    assert(client.send(R"({"schemaVersion":1,"command":"project.save"})").status().value() == 200);
    const auto busy =
        client.send(R"({"schemaVersion":1,"command":"project.close","discardChanges":true})");
    assert(busy.status().value() == 409
        && busy.body_text().find("workspace_busy") != std::string_view::npos);
    F::finishProject(shell);
    assert(!shell.projectDirty());

    for (const auto body :
        {R"({"schemaVersion":1,"command":"project.close","discardChanges":"true"})",
            R"({"schemaVersion":1,"command":"project.save","discardChanges":true})",
            R"({"schemaVersion":1,"command":"project.rename","name":"x","path":"unused"})",
            R"({"schemaVersion":1,"command":"scene.save","path":"x.arp.yaml","discardChanges":true})",
            R"({"schemaVersion":1,"command":"script.save","documentId":1,"manifestYaml":"unused"})"})
        assert(client.send(body).status().value() == 400);
    const auto invalidMcp = client.send(
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"project.close","arguments":{"discardChanges":1}}})",
        true);
    assert(invalidMcp.body_text().find("\"isError\":true") != std::string_view::npos);
    const auto tools = client.send(R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})", true);
    assert(tools.body_text().find("project.create") != std::string_view::npos
        && tools.body_text().find("manifestYaml") != std::string_view::npos);

    F::completed(shell, CreateSceneObjectCommand("unsaved remote"));
    const auto load =
        client.send(R"({"schemaVersion":1,"command":"scene.load","path":"main.arp.yaml"})");
    assert(load.status().value() == 409);
    const auto mcpLoad = client.send(
        R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"scene.load","arguments":{"path":"main.arp.yaml","discardChanges":true}}})",
        true);
    assert(mcpLoad.body_text().find("\"isError\":false") != std::string_view::npos);
    // Drain this actual task before attempting project replacement.
    const auto taskScope = mcpLoad.body_text().find("\"scenePersistenceTask\":{");
    assert(taskScope != std::string_view::npos);
    const auto taskKey = mcpLoad.body_text().find("\"taskId\":", taskScope);
    assert(taskKey != std::string_view::npos);
    const auto taskText = mcpLoad.body_text().substr(taskKey + 9);
    const auto taskId = std::stoull(std::string(taskText));
    EditorResponse accepted;
    accepted.status = EditorResponseStatus::Completed;
    accepted.hasSnapshot = true;
    accepted.snapshot.scenePersistenceTaskId = taskId;
    assert(F::finishScene(shell, accepted).scenePersistenceTaskStatus
        == ScenePersistenceTaskStatus::Succeeded);
    assert(!shell.sceneDirty());
    assert(client.send(R"({"schemaVersion":1,"command":"project.close"})").status().value() == 200);

    core::pmr::String yaml;
    ProjectDiagnostic diagnostic;
    assert(ProjectManifestCodec::encode(fixture.manifest, yaml, diagnostic));
    const auto create = "{\"schemaVersion\":1,\"command\":\"project.create\",\"path\":"
        + ProtocolClient::quoted(fixture.path("remote-created")) + ",\"manifestYaml\":"
        + ProtocolClient::quoted(yaml) + ",\"idempotencyKey\":\"create-project\"}";
    assert(client.send(create).status().value() == 200);
    fixture.manifest.name = "different manifest";
    assert(ProjectManifestCodec::encode(fixture.manifest, yaml, diagnostic));
    const auto changed = "{\"schemaVersion\":1,\"command\":\"project.create\",\"path\":"
        + ProtocolClient::quoted(fixture.path("remote-created")) + ",\"manifestYaml\":"
        + ProtocolClient::quoted(yaml) + ",\"idempotencyKey\":\"create-project\"}";
    assert(client.send(changed).status().value() == 409);
    auto templateManifest = fixture.manifest;
    templateManifest.entryScene = "00000000-0000-4000-8000-000000000093";
    assert(ProjectManifestCodec::encode(templateManifest, yaml, diagnostic));
    const auto templateBody = "{\"schemaVersion\":1,\"command\":\"project.create\",\"path\":"
        + ProtocolClient::quoted(fixture.path("remote-template"))
        + ",\"manifestYaml\":" + ProtocolClient::quoted(yaml)
        + ",\"template\":\"3d\",\"idempotencyKey\":\"create-template\"}";
    assert(client.send(templateBody).status().value() == 200);
    assert(shell.sceneDocument().scene().findObjectByName("Sky"));
    auto changedTemplate = templateBody;
    const auto at = changedTemplate.find("\"template\":\"3d\"");
    changedTemplate.replace(at, 15, "\"template\":\"2d\"");
    assert(client.send(changedTemplate).status().value() == 409);
    assert(
        client
            .send(
                R"({"schemaVersion":1,"command":"project.create","path":"x","manifestYaml":"x","template":true})")
            .status()
            .value()
        == 400);
}
} // namespace

int main(int argc, char** argv)
{
    WorkflowFixture fixture;
    if (argc == 2 && std::string_view(argv[1]) == "--scene-document-smoke") {
        sceneDocumentLifetime(fixture);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--scene-ui-smoke") {
        sceneUiLifetime(fixture);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--settings-smoke") {
        projectSettingsLifetime(fixture);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--identity-contract") {
        sceneIdentityContract();
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--identity-smoke") {
        sceneIdentityLifetime(fixture);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--export-ui-contract") {
        sceneExportUi(fixture);
        remoteSceneExportUi(fixture);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--export-ui-smoke") {
        sceneExportUiLifetime(fixture);
        return 0;
    }
    assert(argc == 1);
    transactions(fixture);
    sceneDocuments(fixture);
    remoteSceneDocuments(fixture);
    remoteSceneExport(fixture);
    remoteDialogText(fixture);
    remoteTransactions(fixture);
    projectUi(fixture);
    projectSettings(fixture);
    remoteProjectSettings(fixture);
    longTextUi(fixture);
    sceneUi(fixture);
    sceneTemplates(fixture);
    repeatedWorkspaceLifetime(fixture);
    return 0;
}
