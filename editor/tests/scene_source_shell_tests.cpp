#include "editor/tests/fixtures/source_scene_shell_fixture.h"

namespace {
using namespace arp;
using namespace editor;
using namespace editor::testfixtures;
using namespace editor::testfixtures::sources;
using namespace core::io;

void succeeded(const EditorStateSnapshot& result)
{
    check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded,
        result.scenePersistenceTaskDiagnostic);
}

void loaded(FixtureSource& fixture, EditorShell& shell)
{
    check(shell.sceneDocument().scene().name() == "Shell script root");
    check(!shell.sceneDirty() && fixture.fixture.amount(shell) == 7);
    const auto* body = shell.sceneDocument().scene().findComponent(fixture.fixture.bodyId);
    const auto* child = shell.sceneDocument().scene().findObject(fixture.fixture.child);
    const auto* link = child ? child->findComponent(fixture.fixture.linkId) : nullptr;
    check(body && link
        && link->scriptInstance().ptr<reflection::instance>()->slot("body")
            == body->nativeReflectionObject());
    // Both fixture scripts would rename this Scene if initialization executed.
    check(shell.sceneDocument().scriptCatalog()->entries().size() == 2);
}

void cycle(FixtureSource& fixture, unsigned mode)
{
    Host host(fixture, mode);
    EditorShell shell;
    host.open(shell);
    const auto resources = shell.retainProjectResourceManager();
    const auto first = Host::begin(shell, "scene.yaml");
    const auto rejected = shell.submitCommand(CreateSceneObjectCommand("blocked"));
    check(rejected.diagnostic == EditorDiagnosticCode::WorkspaceBusy);
    succeeded(host.finish(shell, first));
    loaded(fixture, shell);
    check(shell.retainProjectResourceManager() == resources);
    succeeded(host.finish(shell, Host::begin(shell, "plain.yaml")));
    check(shell.sceneDocument().scene().name() == "Mounted sky scene" && !shell.sceneDirty());
    succeeded(host.finish(shell, Host::begin(shell, "bom.yaml")));
    loaded(fixture, shell);
    const auto owner = shell.sceneDocument().ownerTicket();
    const auto task = Host::begin(shell, "scene.yaml");
    shell.pollTasks(); // A real dependency read is pending when cancelled.
    Host::command(shell, CancelScenePersistenceCommand{task});
    check(host.finish(shell, task).scenePersistenceTaskStatus
        == ScenePersistenceTaskStatus::Cancelled);
    unified_io_fixture::wait(host.queue, [&] {
        return host.mounts.telemetry().inFlight == 0;
    });
    check(owner.sameCurrentEpoch(shell.sceneDocument().ownerTicket()));
    loaded(fixture, shell);
    (void)Host::begin(shell, "plain.yaml");
    shell.pollTasks(); // Shell teardown cancels an accepted callback too.
}

void negatives(FixtureSource& fixture)
{
    Host host(fixture);
    EditorShell shell;
    host.open(shell);
    succeeded(host.finish(shell, Host::begin(shell, "scene.yaml")));
    Host::command(shell, OpenScriptCommand{"primary.arp"});
    unified_io_fixture::wait(host.queue, [&] {
        shell.pollTasks();
        return Host::state(shell).projectIoTaskStatus != ProjectIoStatus::Pending;
    });
    const auto script = Host::state(shell);
    SetSceneObjectNameCommand rename("Keep edited scene");
    rename.objectId = shell.sceneDocument().scene().rootId().value();
    Host::command(shell, rename);
    const auto root = shell.sceneDocument().scene().rootId();
    const auto selected = shell.sceneDocument().selectedObject();
    const auto owner = shell.sceneDocument().ownerTicket();
    const auto resources = shell.retainProjectResourceManager();
    check(shell.submitCommand(LoadSceneCommand("plain.yaml")).diagnostic
        == EditorDiagnosticCode::UnsavedChanges);
    for (const auto* name : {"missing.yaml", "bad.yaml", "utf8.yaml", "nul.yaml", "large.yaml"}) {
        const auto task = Host::begin(shell, name);
        const auto result = host.finish(shell, task);
        check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
            && !result.scenePersistenceTaskDiagnostic.empty());
        const auto state = Host::state(shell);
        check(owner.sameCurrentEpoch(shell.sceneDocument().ownerTicket())
            && root == shell.sceneDocument().scene().rootId()
            && selected == shell.sceneDocument().selectedObject()
            && resources == shell.retainProjectResourceManager()
            && state.activeDocumentId == script.activeDocumentId
            && state.scriptText == script.scriptText
            && shell.sceneDocument().scene().name() == "Keep edited scene");
    }
    for (const auto* path : {"../scene.yaml", "D:/scene.yaml", "primary.arp"}) {
        LoadSceneCommand invalid(path);
        invalid.discardChanges = true;
        check(shell.submitCommand(invalid).status != EditorResponseStatus::Completed);
        check(owner.sameCurrentEpoch(shell.sceneDocument().ownerTicket()));
    }
    // Failed reads must preserve the actual Undo branch, not just its label.
    Host::command(shell, UndoSceneCommand{});
    loaded(fixture, shell);
    const auto stale = Host::begin(shell, "plain.yaml");
    shell.pollTasks();
    // An external native caller can bypass the typed write freeze. Exact
    // content observation must still prevent replacing that caller's edits.
    shell.sceneDocument().scene().setName("External native mutation");
    check(
        host.finish(shell, stale).scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed);
    check(shell.sceneDocument().scene().name() == "External native mutation");
    shell.sceneDocument().scene().setName("Shell script root");
    const auto task = Host::begin(shell, "plain.yaml");
    shell.pollTasks();
    check(host.mounts.unmount(host.mount));
    check(
        host.finish(shell, task).scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed);
    loaded(fixture, shell);
}

void dependencyChange(FixtureSource& fixture)
{
    Host host(fixture, 1);
    EditorShell shell;
    host.open(shell);
    succeeded(host.finish(shell, Host::begin(shell, "scene.yaml")));
    for (const auto* name : {"project.arp.yaml", "primary.arp", "secondary.arp"}) {
        const auto original =
            fixture.fixture.read((core::pmr::String("workspace/") + name).c_str());
        fixture.write(name, original + "\n# External edit\n");
        const auto owner = shell.sceneDocument().ownerTicket();
        const auto task = Host::begin(shell, "plain.yaml");
        const auto result = host.finish(shell, task);
        fixture.write(name, original);
        check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
            && !result.scenePersistenceTaskDiagnostic.empty());
        check(owner.sameCurrentEpoch(shell.sceneDocument().ownerTicket()));
        loaded(fixture, shell);
    }
}

// HTTP has no directory protocol yet. Test the actual file task/transport here;
// do not substitute a Memory directory and label that an HTTP Shell workspace.
void acquisition(FixtureSource& fixture, unsigned mode, std::string_view url = {})
{
    Host host(fixture, mode, url);
    ProjectSourceSession session(
        host.files, {host.mount, "workspace", 71}, {3, fileBytes, 3 * fileBytes});
    for (const auto* name : {"project.arp.yaml", "primary.arp", "secondary.arp"}) {
        check(session.read(name, fileBytes) == FileIoStatus::Accepted);
        unified_io_fixture::wait(host.queue, [&] {
            return session.state() != ProjectSourceState::Pending;
        });
        check(session.state() == ProjectSourceState::Ready,
            session.diagnostic() ? session.diagnostic() : "");
    }
    ProjectSourceContext context{session.finish(71), &host.files, 71};
    check(bool(context.snapshot));
    EditorSceneSourceTask task(context, "scene.yaml", {4, fileBytes, 4 * fileBytes}, fileBytes);
    unified_io_fixture::wait(host.queue, [&] {
        task.advance();
        return task.state() != SceneSourceState::Pending;
    });
    check(task.state() == SceneSourceState::Ready, task.diagnostic() ? task.diagnostic() : "");
    check(task.text() == fixture.scene && task.context().snapshot == context.snapshot);
    check(host.files.telemetry().submitted == 7); // Three initial + three pinned + Scene.
    auto stale = context;
    ++stale.epoch;
    check(
        EditorSceneSourceTask(stale, "scene.yaml", {4, fileBytes, 4 * fileBytes}, fileBytes).state()
        == SceneSourceState::Failed);
    check(EditorSceneSourceTask(context, "scene.yaml", {3, fileBytes, 4 * fileBytes}, fileBytes)
              .state()
        == SceneSourceState::Failed);
    EditorSceneSourceTask constrained(context, "scene.yaml", {4, 1, 4 * fileBytes}, fileBytes);
    check(constrained.state() == SceneSourceState::Failed
        && std::string_view(constrained.diagnostic())
            == "EDITOR-SCENE-SOURCE-DEPENDENCY-OR-BUDGET");
}

void remote(FixtureSource& fixture)
{
    Host host(fixture);
    EditorShell shell;
    host.open(shell);
    EditorRemoteProtocol protocol("source-token");
    const auto send = [&](std::string_view body, bool mcp = false) {
        network::http::Request request;
        request.set_method(network::http::Method::Post);
        request.set_target(mcp ? "/mcp" : "/api/v1/commands");
        request.add_header("Authorization", "Bearer source-token");
        request.add_header("Accept", "application/json");
        request.add_header("MCP-Protocol-Version", "2025-11-25");
        request.set_content_type(network::http::ContentType::Json);
        request.set_body_text(body);
        return protocol.handle(request, shell);
    };
    const auto load = send(
        R"({"schemaVersion":1,"command":"scene.load","path":"scene.yaml","idempotencyKey":"scene-source","correlationId":57})");
    check(load.status().value() == 200);
    const auto version = shell.stateVersion();
    check(
        send(
            R"({"schemaVersion":1,"command":"scene.load","path":"scene.yaml","idempotencyKey":"scene-source","correlationId":57})")
                .body_text()
            == load.body_text()
        && version == shell.stateVersion());
    const auto payload = core::YamlContract::parseSingleDocument(load.body_text());
    const auto& snapshot = core::YamlContract::required(payload, "snapshot");
    const auto task = core::YamlContract::uint64(core::YamlContract::required(
        core::YamlContract::required(snapshot, "scenePersistenceTask"), "taskId"));
    check(task != 0);
    const auto cancelBody =
        core::pmr::String("{\"schemaVersion\":1,\"command\":\"scene.task.cancel\",\"taskId\":")
        + std::to_string(task).c_str() + ",\"idempotencyKey\":\"scene-cancel\"}";
    check(send(cancelBody).status().value() == 200);
    const auto after = shell.stateVersion();
    check(send(cancelBody).status().value() == 200 && shell.stateVersion() == after);
    check(send(R"({"schemaVersion":1,"command":"scene.task.cancel","taskId":1,"path":"x"})")
              .status()
              .value()
        == 400);
    check(
        send(
            R"({"schemaVersion":1,"command":"scene.task.cancel","taskId":1,"discardChanges":true})")
            .status()
            .value()
        == 400);
    const auto query = send(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"scene.task.get","arguments":{"taskId":1}}})",
        true);
    check(query.body_text().find("cancelled") != std::string_view::npos);
    const auto next = Host::begin(shell, "plain.yaml");
    const auto cancel = send(
        core::pmr::String(
            R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"scene.task.cancel","arguments":{"taskId":)")
            + std::to_string(next).c_str() + "}}}",
        true);
    check(cancel.body_text().find("\"isError\":false") != std::string_view::npos);
    check(host.finish(shell, next).scenePersistenceTaskStatus
        == ScenePersistenceTaskStatus::Cancelled);
}
} // namespace

int main(int argc, char** argv)
{
    FixtureSource fixture;
    const std::string_view option = argc > 1 ? argv[1] : "";
    if (option == "--emit") {
        check(argc == 3);
        std::filesystem::copy(fixture.fixture.files.root / "workspace",
            std::filesystem::path(argv[2]) / "workspace",
            std::filesystem::copy_options::recursive);
        return 0;
    }
    if (option.empty()) {
        cycle(fixture, 0);
        negatives(fixture);
#if !defined(__EMSCRIPTEN__)
        dependencyChange(fixture);
#endif
        acquisition(fixture, 0);
        remote(fixture);
    } else {
        const auto run = [&] {
            if (option == "--http") {
                check(argc == 3);
                acquisition(fixture, 2, argv[2]);
            } else {
                check(option == "--smoke" || option == "--native");
                cycle(fixture, option == "--native" ? 1 : 0);
            }
        };
        for (unsigned i = 0; i < 20; ++i)
            run();
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            run();
        const auto after = core::defaultCpuAllocator().stats();
        std::printf("Scene source 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)after.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        check(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
    }
    std::puts("PASS source Scene acquisition/publication; no user initialization");
}
