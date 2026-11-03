#include "core/io/file_query_contract.h"
#include "editor/tests/fixtures/lost_receipt_backend.h"
#include "editor/tests/fixtures/source_scene_save_fixture.h"
#include "editor/workspace/editor_project_source_save_task.h"
#include "editor/workspace/editor_source_recovery_capture.h"
#include "editor/workspace/editor_source_recovery_workspace.h"

#include <cassert>

namespace {
using namespace arp;
using namespace editor;
using namespace editor::testfixtures;
using namespace editor::testfixtures::sources;

void checkSceneAt(int line, bool value, const core::pmr::String& diagnostic = {})
{
    if (!value)
        std::fprintf(stderr, "Scene save check at line %d: %s\n", line, diagnostic.c_str());
    assert(value);
}

#define check(...) checkSceneAt(__LINE__, __VA_ARGS__)

void awaitSubmission(Host& host, EditorShell& shell, std::uint64_t id)
{
    unified_io_fixture::wait(host.queue, [&] {
        shell.pollTasks();
        const auto receipt = Host::command(shell, GetScenePersistenceTaskQuery{id}).snapshot;
        check(receipt.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Pending,
            receipt.scenePersistenceTaskDiagnostic);
        return receipt.sceneSaveReceipt.submitted;
    });
}

void external(Host& host, const char* path, std::string_view bytes)
{
    const auto previous = read(host, path);
    check(previous.status == FileIoStatus::Success);
    FileSaveRequest request;
    request.mount = host.mount;
    request.pathScope = FilePathScope::MountedResource;
    request.path = core::pmr::String("workspace/") + path;
    request.bytes.assign(bytes.begin(), bytes.end());
    request.maxWriteBytes = fileBytes;
    request.maxBaselineBytes = fileBytes;
    request.precondition = FileSavePrecondition::ContentSha256;
    request.expectedVersion = previous.version;
    unified_io_fixture::Probe<FileSaveResult> probe;
    check(host.files.save(std::move(request), {decltype(probe)::complete, &probe})
        == FileIoStatus::Accepted);
    unified_io_fixture::wait(host.queue, [&] {
        return probe.calls == 1;
    });
    check(probe.result.commitState == FileCommitState::Committed);
}

void captureScriptedScene(Host& host)
{
    // Reuse the real source bundle and cold Scene codec. No Shell-private
    // getters or test-only access to active workspace owners are required.
    const ProjectInputBudget budget{8, fileBytes, 4 * fileBytes};
    ProjectSourceSession acquire(host.files, {host.mount, "workspace", 1}, budget);
    for (const auto* path : {"project.arp.yaml", "primary.arp", "secondary.arp"}) {
        check(acquire.read(path, fileBytes) == FileIoStatus::Accepted);
        unified_io_fixture::wait(host.queue, [&] {
            return acquire.state() != ProjectSourceState::Pending;
        });
        check(acquire.state() == ProjectSourceState::Ready);
    }
    ProjectSourceContext context{acquire.finish(1), &host.files, 1};
    EditorProjectResources resources;
    check(resources.loadSources(context.snapshot, host.files, 1).success);
    EditorSceneDocument document(false);
    core::pmr::String diagnostic;
    check(document.bindServiceSession(core::makeShared<EditorSceneServiceSession>(
                                          resources.retainManager(), resources.physicsWorld2D()),
              diagnostic),
        diagnostic);
    const auto root = std::filesystem::path("workspace");
    auto policy = EditorSceneScriptPolicy::inputs(root, *resources.manifest());
    policy.sources = &context;
    const auto bytes = read(host, "scene.yaml").bytes;
    check(
        EditorSceneScriptCodec::decode({reinterpret_cast<const char*>(bytes.data()), bytes.size()},
            document,
            policy,
            diagnostic),
        diagnostic);
    document.scene().setName("Captured scripted scene");
    ScriptDocumentSession tabs;
    EditorSourceRecoveryCaptureInput input{context,
        resources,
        *resources.manifest(),
        document,
        tabs,
        "workspace",
        "4305ee93-3897-43ae-93da-ecbb0c569c83",
        "3D",
        1,
        1,
        EditorSceneScriptCodec::contractVersion,
        {},
        {},
        {}};
    const auto submitted = host.files.telemetry().submitted;
    EditorSourceRecoveryCapture capture(input, budget);
    check(host.files.telemetry().submitted == submitted);
    unified_io_fixture::wait(host.queue, [&] {
        capture.advance(context.epoch);
        return capture.state() != SourceRecoveryCaptureState::Pending;
    });
    const char* issue = nullptr;
    const auto* record = capture.snapshot(context.epoch, issue);
    check(record != nullptr, core::pmr::String(capture.diagnostic()));
    core::pmr::String yaml;
    check(EditorSceneScriptCodec::observeSnapshot(document, policy, yaml, diagnostic), diagnostic);
    check(record->scene.yaml == yaml && record->source->inputs.size() == context.snapshot->size()
        && document.scene().name() == "Captured scripted scene");
    // Preserve real cold ScriptComponent fields and object/component references.
    core::UniquePtr<EditorSceneDocument> restored;
    check(
        EditorSceneScriptCodec::prepare(record->scene.yaml, document, policy, restored, diagnostic),
        diagnostic);
    check(EditorSceneScriptCodec::observeSnapshot(*restored, policy, yaml, diagnostic), diagnostic);
    check(yaml == record->scene.yaml);
    EditorSourceRecoveryWorkspace workspace(host.files,
        *record,
        "workspace",
        context.snapshot->location(),
        budget,
        {32, 4, 4096, 8192});
    unified_io_fixture::wait(host.queue, [&] {
        workspace.advance(context.epoch);
        return workspace.state() != SourceRecoveryWorkspaceState::Pending;
    });
    auto view = workspace.inspect(context.epoch, issue);
    check(bool(view), core::pmr::String(workspace.diagnostic()));
    check(view->savedState.sceneAuthoringVersion() == EditorSceneScriptCodec::contractVersion
        && view->savedState.scenePath().empty() && view->scripts.empty());
    check(
        EditorSceneScriptCodec::observeSnapshot(*view->recovery.document, policy, yaml, diagnostic),
        diagnostic);
    check(yaml == record->scene.yaml);
}

void cycle(FixtureSource& fixture, unsigned mode)
{
    fixture.write("plain.yaml", fixture.v3);
    fixture.write("scene.yaml", fixture.scene);
    fixture.write("bom.yaml", core::pmr::String("\xef\xbb\xbf") + fixture.scene);
    for (const auto* path : {"copy.yaml", "script-copy.yaml"})
        std::filesystem::remove(fixture.fixture.files.root / "workspace" / path);
    Host host(fixture, mode, {}, true);
    {
        EditorShell shell;
        host.open(shell);
        check(host.finish(shell, Host::begin(shell, "plain.yaml")).scenePersistenceTaskStatus
            == ScenePersistenceTaskStatus::Succeeded);
        rename(shell, "Saved native scene");
        const auto previous = read(host, "plain.yaml");
        const auto submitted = host.files.telemetry().submitted;
        auto id = save(shell, "plain.yaml");
        check(host.files.telemetry().submitted == submitted);
        check(read(host, "plain.yaml").bytes == previous.bytes);
        awaitSubmission(host, shell, id);
        rename(shell, "Newer local edit");
        auto result = host.finish(shell, id);
        check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded,
            result.scenePersistenceTaskDiagnostic);
        check(result.sceneSaveReceipt.published && result.scenePersistenceTaskCommitted
            && shell.sceneDirty() && shell.sceneDocument().scene().name() == "Newer local edit");
        const auto written = read(host, "plain.yaml");
        check(written.bytes != previous.bytes
            && written.version == result.sceneSaveReceipt.outcome.committedVersion);
        id = save(shell, "plain.yaml");
        check(host.finish(shell, id).sceneSaveReceipt.published && !shell.sceneDirty());

        // Unbound existing targets need explicit overwrite and typed prior-file
        // admission. A v3 save must not silently replace a v4 scripted scene.
        const auto v4 = read(host, "scene.yaml").bytes;
        id = save(shell, "scene.yaml");
        result = host.finish(shell, id);
        check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
            && !result.scenePersistenceTaskCommitted);
        id = save(shell, "scene.yaml", true);
        check(host.finish(shell, id).scenePersistenceTaskStatus
            == ScenePersistenceTaskStatus::Failed);
        check(read(host, "scene.yaml").bytes == v4);

        id = save(shell, "copy.yaml");
        result = host.finish(shell, id);
        check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded,
            result.scenePersistenceTaskDiagnostic);
        check(result.scenePath == "copy.yaml" && !shell.sceneDirty());
        rename(shell, "Explicit overwrite");
        id = save(shell, "plain.yaml", true);
        check(host.finish(shell, id).sceneSaveReceipt.published && !shell.sceneDirty());
        const auto beforeUnsafe = host.files.telemetry().submitted;
        check(shell.submitCommand(SaveSceneCommand("../outside.yaml")).status
            != EditorResponseStatus::Completed);
        check(host.files.telemetry().submitted == beforeUnsafe);

        external(host, "plain.yaml", fixture.v3 + "\n# external writer\n");
        rename(shell, "Keep local conflict");
        id = save(shell, "plain.yaml");
        result = host.finish(shell, id);
        check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
            && result.sceneSaveReceipt.outcome.status == FileIoStatus::VersionMismatch
            && shell.sceneDirty() && shell.sceneDocument().scene().name() == "Keep local conflict");
        check(host.finish(shell, Host::begin(shell, "plain.yaml")).scenePersistenceTaskStatus
            == ScenePersistenceTaskStatus::Succeeded);
        rename(shell, "Cancel before write");
        id = save(shell, "plain.yaml");
        Host::command(shell, CancelScenePersistenceCommand{id});
        result = Host::command(shell, GetScenePersistenceTaskQuery{id}).snapshot;
        check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Cancelled
            && !result.sceneSaveReceipt.submitted && !result.scenePersistenceTaskCommitted);
        id = save(shell, "plain.yaml");
        awaitSubmission(host, shell, id);
        // Ensure real native completion is queued before requesting cancellation.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        while (!host.queue.pending() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        check(host.queue.pending() != 0);
        Host::command(shell, CancelScenePersistenceCommand{id});
        result = host.finish(shell, id);
        check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Cancelled
            && result.scenePersistenceTaskCommitted && !result.sceneSaveReceipt.published);
        check(host.finish(shell, Host::begin(shell, "plain.yaml")).scenePersistenceTaskStatus
            == ScenePersistenceTaskStatus::Succeeded);

        // Actual v4 ScriptComponent state survives save/reload; Passive input
        // preparation uses logical mounted paths and executes no user code.
        check(host.finish(shell, Host::begin(shell, "scene.yaml")).scenePersistenceTaskStatus
            == ScenePersistenceTaskStatus::Succeeded);
        rename(shell, "Saved scripted scene");
        captureScriptedScene(host);
        id = save(shell, "scene.yaml");
        result = host.finish(shell, id);
        check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded,
            result.scenePersistenceTaskDiagnostic);
        check(!shell.sceneDirty() && result.sceneSaveReceipt.published);
        check(host.finish(shell, Host::begin(shell, "bom.yaml")).scenePersistenceTaskStatus
            == ScenePersistenceTaskStatus::Succeeded);
        rename(shell, "Saved scripted scene");
        id = save(shell, "bom.yaml");
        result = host.finish(shell, id);
        check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded,
            result.scenePersistenceTaskDiagnostic);
        check(result.sceneSaveReceipt.published);
        id = save(shell, "script-copy.yaml");
        check(host.finish(shell, id).sceneSaveReceipt.published);
        check(host.finish(shell, Host::begin(shell, "script-copy.yaml")).scenePersistenceTaskStatus
            == ScenePersistenceTaskStatus::Succeeded);
        check(shell.sceneDocument().scene().name() == "Saved scripted scene");
        const auto* component =
            shell.sceneDocument().scene().findComponent(fixture.fixture.scriptId);
        check(component
            && component->scriptInstance().ptr<reflection::instance>()->slot("amount").cast<int>()
                == 7);
        const auto state = Host::state(shell);
        check(state.sceneSaveReceipt.terminal && state.sceneSaveReceipt.published);
        Host::command(shell, ActivateControlCommand("save_scene_as"));
        Host::command(shell, SetTextValueCommand{"scene_ui_path", "scene.yaml"});
        id = Host::command(shell, ActivateControlCommand("scene_ui_submit"))
                 .snapshot.scenePersistenceTaskId;
        check(host.finish(shell, id).scenePersistenceTaskStatus
            == ScenePersistenceTaskStatus::Failed);
        const auto* confirm = shell.runtime().canvas().findWidget(shell.id("scene_ui_confirm"));
        check(confirm && confirm->visible());
        id = Host::command(shell, ActivateControlCommand("scene_ui_confirm"))
                 .snapshot.scenePersistenceTaskId;
        check(host.finish(shell, id).sceneSaveReceipt.published);
        check(!shell.runtime().canvas().findWidget(shell.id("scene_ui_modal"))->visible());
        rename(shell, "Abandoned save owner");
        save(shell, "scene.yaml");
        shell.pollTasks();
    }
    while (host.queue.pending())
        host.queue.dispatch(8);
    check(host.mounts.telemetry().inFlight == 0);
    if (mode == 1) {
        Host reopened(fixture, 1, {}, true);
        EditorShell shell;
        reopened.open(shell);
        check(reopened.finish(shell, Host::begin(shell, "script-copy.yaml"))
                  .scenePersistenceTaskStatus
            == ScenePersistenceTaskStatus::Succeeded);
        check(shell.sceneDocument().scene().name() == "Saved scripted scene");
    }
}

void unknownAndFailures(FixtureSource& fixture)
{
    fixture.write("plain.yaml", fixture.v3);
    Host host(fixture, 0, {}, true);
    check(host.mounts.unmount(host.mount));
    host.mount = host.mounts.mount(
        "source", core::makeShared<LostReceiptBackend>(fixture.backend(0, {}, true)));
    EditorShell shell;
    host.open(shell);
    check(host.finish(shell, Host::begin(shell, "plain.yaml")).scenePersistenceTaskStatus
        == ScenePersistenceTaskStatus::Succeeded);
    rename(shell, "Unknown saved bytes");
    auto id = save(shell, "plain.yaml");
    auto result = host.finish(shell, id);
    check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
        && result.sceneSaveReceipt.outcome.commitState == FileCommitState::OutcomeUnknown
        && !result.sceneSaveReceipt.published && shell.sceneDirty());
    const auto submissions = host.files.telemetry().submitted;
    check(shell.submitCommand(SaveSceneCommand("plain.yaml")).status
            != EditorResponseStatus::Completed
        && host.files.telemetry().submitted == submissions);
    check(host.finish(shell, Host::begin(shell, "plain.yaml")).scenePersistenceTaskStatus
        == ScenePersistenceTaskStatus::Succeeded);
    check(shell.sceneDocument().scene().name() == "Unknown saved bytes" && !shell.sceneDirty());
    check(Host::state(shell).sceneSaveReceipt.outcome.commitState == FileCommitState::OutcomeUnknown
        && Host::state(shell).sceneSaveReceipt.baselineRefreshed);
    rename(shell, "Known next save");
    id = save(shell, "plain.yaml");
    check(host.finish(shell, id).sceneSaveReceipt.published);
    const auto bytes = read(host, "plain.yaml").bytes;
    rename(shell, "Allocation failure retained");
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope fault(quota);
        check(shell.submitCommand(SaveSceneCommand("plain.yaml")).status
            != EditorResponseStatus::Completed);
    }
    check(quota->liveBytes() == 0 && read(host, "plain.yaml").bytes == bytes && shell.sceneDirty());
    id = save(shell, "plain.yaml");
    shell.sceneDocument().scene().setName("Untracked mutation");
    result = host.finish(shell, id);
    check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
        && !result.scenePersistenceTaskCommitted && read(host, "plain.yaml").bytes == bytes);
    external(host,
        "primary.arp",
        core::pmr::String(Fixture::primarySource) + "\n# changed dependency\n");
    id = save(shell, "plain.yaml");
    check(host.finish(shell, id).scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed);
    check(read(host, "plain.yaml").bytes == bytes);
    external(host, "primary.arp", Fixture::primarySource);
    id = save(shell, "plain.yaml");
    check(host.mounts.unmount(host.mount));
    result = host.finish(shell, id);
    check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Cancelled
        && !result.scenePersistenceTaskCommitted);
}

EditorStateSnapshot finishProject(Host& host, EditorShell& shell)
{
    unified_io_fixture::wait(host.queue, [&] {
        shell.pollTasks();
        return Host::state(shell).projectIoTaskStatus != ProjectIoStatus::Pending;
    });
    return Host::state(shell);
}

void resetProject(FixtureSource& fixture)
{
    fixture.write("project.arp.yaml", fixture.fixture.manifest);
    fixture.write("primary.arp", Fixture::primarySource);
    fixture.write("secondary.arp", Fixture::secondarySource);
    fixture.write("scene.yaml", fixture.scene);
}

void projectCycle(FixtureSource& fixture, unsigned mode)
{
    resetProject(fixture);
    Host host(fixture, mode, {}, true);
    {
        EditorShell shell;
        host.open(shell);
        check(host.finish(shell, Host::begin(shell, "scene.yaml")).scenePersistenceTaskStatus
            == ScenePersistenceTaskStatus::Succeeded);
        auto* const scene = &shell.sceneDocument().scene();
        const auto catalog = shell.sceneDocument().scriptCatalog();
        const auto services = shell.sceneDocument().sceneServiceSession();
        auto* const resources = &shell.projectResources();
        const auto component = scene->findComponent(fixture.fixture.scriptId);
        check(component != nullptr && catalog && services);
        rename(shell, "History survives project save");
        Host::command(shell, OpenScriptCommand{"primary.arp"});
        check(finishProject(host, shell).projectIoTaskStatus == ProjectIoStatus::Succeeded);
        const auto script = Host::state(shell);
        Host::command(shell,
            ReplaceScriptTextCommand(
                script.activeDocumentId, script.scriptText + "\n# local script edit kept\n"));
        Host::command(shell, SetProjectSettingsCommand{"Submitted project settings", {}});
        const auto submitted = host.files.telemetry().submitted;
        const auto accepted = Host::command(shell, SaveProjectCommand{}).snapshot;
        check(accepted.projectSavePending && accepted.projectSaveReceipt.taskId
            && !accepted.projectSaveReceipt.submitted
            && host.files.telemetry().submitted == submitted);
        Host::command(shell, SetProjectSettingsCommand{"Newer project draft", {}});
        auto result = finishProject(host, shell);
        check(result.projectIoTaskStatus == ProjectIoStatus::Succeeded, result.projectIoDiagnostic);
        check(result.projectSaveReceipt.published && result.projectSaveReceipt.terminal
            && !result.projectSavePending && result.projectDirty
            && result.projectName == "Newer project draft");
        check(&shell.sceneDocument().scene() == scene && &shell.projectResources() == resources
            && shell.sceneDocument().scriptCatalog() == catalog
            && shell.sceneDocument().sceneServiceSession() == services
            && scene->findComponent(fixture.fixture.scriptId) == component);
        check(result.activeDocumentId == script.activeDocumentId && result.scriptDirty
            && result.openDocumentCount == script.openDocumentCount
            && result.scriptText == script.scriptText + "\n# local script edit kept\n");
        auto disk = read(host, "project.arp.yaml");
        check(disk.version == result.projectSaveReceipt.outcome.committedVersion);
        ProjectManifest saved;
        ProjectDiagnostic issue;
        check(ProjectManifestCodec::decode(
            {reinterpret_cast<const char*>(disk.bytes.data()), disk.bytes.size()}, saved, issue));
        check(saved.name == "Submitted project settings");
        Host::command(shell, SaveProjectCommand{});
        result = finishProject(host, shell);
        check(result.projectSaveReceipt.published && !result.projectDirty);
        Host::command(shell, UndoSceneCommand{});
        check(scene->name() != "History survives project save");
        Host::command(shell, RedoSceneCommand{});
        check(scene->name() == "History survives project save");
        SetSceneScriptFieldsCommand fields(primaryId, "Mover", 1, "amount: 9\n", true);
        fields.objectId = scene->rootId().value();
        fields.componentId = fixture.fixture.scriptId.value();
        Host::command(shell, std::move(fields));
        check(finishProject(host, shell).projectIoTaskStatus == ProjectIoStatus::Succeeded);
        const auto sceneSave = save(shell, "scene.yaml");
        check(host.finish(shell, sceneSave).sceneSaveReceipt.published);
        // A tab keeps its own file baseline. Rebinding project provenance must
        // not erase its local edit, undo branch or exact document identity.
        Host::command(shell, SaveScriptCommand{script.activeDocumentId});
        result = finishProject(host, shell);
        check(result.scriptSaveReceipt.published && !result.scriptDirty);
        Host::command(shell,
            NavigateProjectSourceCommand{ProjectBrowserModel::SourceNavigation::Refresh, {}});
        result = finishProject(host, shell);
        check(result.projectIoTaskStatus == ProjectIoStatus::Succeeded);
        check(result.projectSaveReceipt.published && result.projectName == "Newer project draft");
        EditorRemoteProtocol protocol("project-save-token");
        network::http::Request query;
        query.set_method(network::http::Method::Post);
        query.set_target("/api/v1/commands");
        query.add_header("Authorization", "Bearer project-save-token");
        query.set_content_type(network::http::ContentType::Json);
        query.set_body_text(R"({"schemaVersion":1,"command":"editor.state.get"})");
        const auto wire = protocol.handle(query, shell);
        check(wire.status().value() == 200
            && wire.body_text().find("\"projectSaveReceipt\":{") != std::string_view::npos);
    }
    if (mode == 1) {
        Host reopened(fixture, 1);
        EditorShell shell;
        reopened.open(shell);
        check(shell.projectManifest()->name == "Newer project draft" && !shell.projectDirty());
    }
}

void projectFailures(FixtureSource& fixture)
{
    resetProject(fixture);
    {
        Host host(fixture);
        EditorShell shell;
        host.open(shell);
        Host::command(shell, RenameProjectCommand{"Readonly local draft"});
        const auto count = host.files.telemetry().submitted;
        check(shell.submitCommand(SaveProjectCommand{}).status != EditorResponseStatus::Completed);
        check(host.files.telemetry().submitted == count && shell.projectDirty());
    }
    Host host(fixture, 0, {}, true);
    EditorShell shell;
    host.open(shell);
    const auto original = read(host, "project.arp.yaml").bytes;
    Host::command(shell, RenameProjectCommand{"Cancelled draft"});
    auto accepted = Host::command(shell, SaveProjectCommand{}).snapshot;
    Host::command(shell, CancelProjectIoCommand{accepted.projectIoTaskId});
    auto result = finishProject(host, shell);
    check(result.projectIoTaskStatus == ProjectIoStatus::Cancelled && shell.projectDirty()
        && !result.projectSaveReceipt.submitted
        && read(host, "project.arp.yaml").bytes == original);
    external(host, "primary.arp", core::pmr::String(Fixture::primarySource) + "\n# external\n");
    Host::command(shell, SaveProjectCommand{});
    result = finishProject(host, shell);
    check(result.projectIoTaskStatus == ProjectIoStatus::Failed
        && !result.projectSaveReceipt.submitted && !result.projectSaveReceipt.published
        && read(host, "project.arp.yaml").bytes == original);
    external(host, "primary.arp", Fixture::primarySource);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope fault(quota);
        check(shell.submitCommand(SaveProjectCommand{}).status != EditorResponseStatus::Completed);
    }
    check(quota->liveBytes() == 0 && read(host, "project.arp.yaml").bytes == original);
    accepted = Host::command(shell, SaveProjectCommand{}).snapshot;
    shell.sceneDocument().setSchemaOnlyComponentCreation(true);
    result = finishProject(host, shell);
    check(!result.projectSaveReceipt.published && !result.projectSaveReceipt.submitted
        && read(host, "project.arp.yaml").bytes == original);
    Host::command(shell, SaveProjectCommand{});
    unified_io_fixture::wait(host.queue, [&] {
        shell.pollTasks();
        return Host::state(shell).projectSaveReceipt.submitted;
    });
    Host::command(shell, CancelProjectIoCommand{Host::state(shell).projectIoTaskId});
    result = finishProject(host, shell);
    check(result.projectIoTaskStatus == ProjectIoStatus::Cancelled
        && result.projectSaveReceipt.outcome.commitState == FileCommitState::Committed
        && !result.projectSaveReceipt.published);
    check(shell.submitCommand(SaveProjectCommand{}).status != EditorResponseStatus::Completed);
    OpenProjectCommand reopen{"workspace", true};
    reopen.source = "workspace";
    Host::command(shell, reopen);
    check(finishProject(host, shell).projectIoTaskStatus == ProjectIoStatus::Succeeded);
    check(Host::state(shell).projectSaveReceipt.baselineRefreshed);
    Host::command(shell, RenameProjectCommand{"After explicit reopen"});
    Host::command(shell, SaveProjectCommand{});
    check(finishProject(host, shell).projectSaveReceipt.published);
    Host::command(shell, RenameProjectCommand{"Unmounted draft retained"});
    Host::command(shell, SaveProjectCommand{});
    const auto count = host.files.telemetry().submitted;
    check(host.mounts.unmount(host.mount));
    result = finishProject(host, shell);
    check(result.projectIoTaskStatus == ProjectIoStatus::Failed
        && !result.projectSaveReceipt.published && !result.projectSaveReceipt.submitted
        && host.files.telemetry().submitted == count);
}

void projectUnknown(FixtureSource& fixture)
{
    resetProject(fixture);
    Host host(fixture, 0, {}, true);
    check(host.mounts.unmount(host.mount));
    host.mount = host.mounts.mount(
        "source", core::makeShared<LostReceiptBackend>(fixture.backend(0, {}, true)));
    EditorShell shell;
    host.open(shell);
    Host::command(shell, RenameProjectCommand{"Unknown committed project"});
    Host::command(shell, SaveProjectCommand{});
    auto result = finishProject(host, shell);
    check(result.projectIoTaskStatus == ProjectIoStatus::Failed && shell.projectDirty()
        && result.projectSaveReceipt.outcome.commitState == FileCommitState::OutcomeUnknown
        && !result.projectSaveReceipt.published);
    check(shell.submitCommand(SaveProjectCommand{}).status != EditorResponseStatus::Completed);
    OpenProjectCommand reopen{"workspace", true};
    reopen.source = "workspace";
    Host::command(shell, reopen);
    result = finishProject(host, shell);
    check(result.projectIoTaskStatus == ProjectIoStatus::Succeeded
        && result.projectName == "Unknown committed project"
        && result.projectSaveReceipt.baselineRefreshed
        && result.projectSaveReceipt.outcome.commitState == FileCommitState::OutcomeUnknown);
    Host::command(shell, RenameProjectCommand{"Known project after reopen"});
    Host::command(shell, SaveProjectCommand{});
    check(finishProject(host, shell).projectSaveReceipt.published);
}

void projectPublicationContract(FixtureSource& fixture)
{
    resetProject(fixture);
    Host host(fixture, 0, {}, true);
    ProjectSourceSession acquire(
        host.files, {host.mount, "workspace", 7}, {4, fileBytes, 4 * fileBytes});
    for (const auto* name : {"project.arp.yaml", "primary.arp", "secondary.arp"}) {
        check(acquire.read(name, fileBytes) == FileIoStatus::Accepted);
        unified_io_fixture::wait(host.queue, [&] {
            return acquire.state() != ProjectSourceState::Pending;
        });
    }
    auto scope = acquire.finish(7);
    check(scope != nullptr);
    ProjectSourceContext context{scope, &host.files, 7};
    EditorProjectResources resources;
    check(resources.loadSources(scope, host.files, 7).success);
    core::pmr::String diagnostic;
    EditorSceneDocument document(false);
    check(document.bindServiceSession(core::makeShared<EditorSceneServiceSession>(
                                          resources.retainManager(), resources.physicsWorld2D()),
              diagnostic),
        diagnostic);
    const auto root = std::filesystem::path("workspace");
    auto inputs = EditorSceneScriptPolicy::inputs(root, *resources.manifest());
    inputs.sources = &context;
    check(EditorSceneScriptCodec::decode(fixture.scene, document, inputs, diagnostic), diagnostic);
    const auto catalog = document.scriptCatalog();
    const auto services = document.sceneServiceSession();
    auto* manager = &resources.manager();
    const auto physics = resources.physicsWorld2D();
    ProjectDirectorySession listing(host.files, scope, "", {32, 32, 4096, 8192});
    check(listing.next(7) == FileIoStatus::Accepted);
    host.queue.dispatch(8);
    auto directory = listing.finish(7);
    check(directory != nullptr);
    ProjectBrowserModel empty, browser;
    check(!empty.prepareSources(
        directory, host.files, 7, ProjectBrowserModel::SourceNavigation::Navigate, browser));
    auto settings = *resources.manifest();
    settings.name = "Direct transaction project";
    const char* issue = nullptr;
    auto changed = settings;
    changed.scriptModules.front().schemaVersion += 1;
    check(!EditorProjectSourceSaveTask::prepare(
        context, {4, fileBytes, 4 * fileBytes}, resources, browser, document, changed, issue));
    check(std::string_view(issue) == "EDITOR-PROJECT-SAVE-DEPENDENCY-CHANGE");
    auto task = EditorProjectSourceSaveTask::prepare(
        context, {4, fileBytes, 4 * fileBytes}, resources, browser, document, settings, issue);
    check(task && !issue && !task->submitted());
    unified_io_fixture::wait(host.queue, [&] {
        task->advance();
        return task->terminal();
    });
    check(task->result().commitState == FileCommitState::Committed);
    EditorSceneDocument foreign(false);
    check(!task->publish(resources, browser, foreign));
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    core::SharedPtr<const ProjectSourceSnapshot> committed;
    {
        core::CpuMemoryQuotaScope fault(quota);
        committed = task->publish(resources, browser, document);
    }
    check(committed && quota->liveBytes() == 0 && task->published());
    check(!task->publish(resources, browser, document));
    check(resources.sourceSnapshot() == committed
        && browser.sourceSnapshot()->project() == committed
        && resources.manifest()->name == "Direct transaction project"
        && &resources.manager() == manager && resources.physicsWorld2D() == physics
        && document.scriptCatalog() == catalog && document.sceneServiceSession() == services);
    context.snapshot = committed;
    auto savedInputs = EditorSceneScriptPolicy::inputs(root, *resources.manifest());
    savedInputs.sources = &context;
    core::pmr::String hash;
    check(EditorSceneScriptCodec::contentHash(document, savedInputs, hash, diagnostic), diagnostic);
    task->releasePreparation();
}
} // namespace

int runSourceScriptReloadTests(int argc, char** argv);

int main(int argc, char** argv)
{
    if (const auto reload = runSourceScriptReloadTests(argc, argv); reload >= 0)
        return reload;
    const bool smoke = argc > 1 && std::string_view(argv[1]) == "--smoke";
    const unsigned mode = argc > 1 && std::string_view(argv[1]) == "--native" ? 1 : 0;
    FixtureSource fixture;
    if (argc > 1 && std::string_view(argv[1]) == "--emit") {
        check(argc == 3);
        std::filesystem::create_directories(argv[2]);
        std::filesystem::copy(fixture.fixture.files.root / "workspace",
            std::filesystem::path(argv[2]) / "workspace",
            std::filesystem::copy_options::recursive);
        return 0;
    }
    const bool project = argc > 1 && std::string_view(argv[1]).find("--project") == 0;
    if (project) {
        const unsigned projectMode = std::string_view(argv[1]) == "--project-native" ? 1 : 0;
        const unsigned loops = std::string_view(argv[1]) == "--project-smoke" ? 20 : 1;
        projectCycle(fixture, projectMode);
        projectFailures(fixture);
        projectUnknown(fixture);
        projectPublicationContract(fixture);
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < loops; ++i) {
            projectCycle(fixture, projectMode);
            projectFailures(fixture);
            projectUnknown(fixture);
            projectPublicationContract(fixture);
        }
        const auto after = core::defaultCpuAllocator().stats();
        std::printf("Project source save %u cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
            loops,
            (unsigned long long)before.liveBytes,
            (unsigned long long)after.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        check(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        return 0;
    }
    cycle(fixture, mode);
    unknownAndFailures(fixture);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < (smoke ? 20u : 1u); ++i) {
        cycle(fixture, mode);
        unknownAndFailures(fixture);
    }
    const auto after = core::defaultCpuAllocator().stats();
    std::printf("Scene source save %u cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
        smoke ? 20u : 1u,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
    check(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}

#undef check
