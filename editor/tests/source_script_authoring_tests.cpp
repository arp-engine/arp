#include "editor/tests/fixtures/script_inspector_fixture.h"
#include "editor/tests/fixtures/source_scene_shell_fixture.h"

namespace {
using namespace arp;
using namespace editor;
using namespace editor::testfixtures;
using namespace editor::testfixtures::sources;
using namespace core::io;
using Controls = ScriptInspectorFixture;

EditorStateSnapshot finish(Host& host, EditorShell& shell)
{
    unified_io_fixture::wait(host.queue, [&] {
        shell.pollTasks();
        return Host::state(shell).projectIoTaskStatus != ProjectIoStatus::Pending;
    });
    return Host::state(shell);
}

void applied(Host& host, EditorShell& shell, EditorCommandPayload command)
{
    const auto accepted = Host::command(shell, std::move(command));
    check(accepted.snapshot.projectIoTaskId
        && accepted.snapshot.projectIoTaskStatus == ProjectIoStatus::Pending);
    const auto result = finish(host, shell);
    check(result.projectIoTaskStatus == ProjectIoStatus::Succeeded, result.projectIoDiagnostic);
}

int amount(const EditorShell& shell, scene::ComponentId id)
{
    const auto* component = shell.sceneDocument().scene().findComponent(id);
    check(component != nullptr);
    return component->scriptInstance().ptr<reflection::instance>()->slot("amount").cast<int>();
}

void cycle(FixtureSource& fixture, unsigned mode)
{
    Host host(fixture, mode);
    EditorShell shell;
    host.open(shell);
    const auto load = host.finish(shell, Host::begin(shell, "plain.yaml"));
    check(load.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded);
    const auto root = shell.sceneDocument().scene().rootId();
    const auto resources = shell.retainProjectResourceManager();
    SetSceneObjectNameCommand rename("Before enabling");
    rename.objectId = root.value();
    Host::command(shell, rename);
    const auto old = shell.sceneDocument().ownerTicket();
    check(shell.sceneHistoryUsage().undoEntries == 1 && !shell.sceneDocument().scriptCatalog());
    applied(host, shell, ActivateControlCommand("scene_script_prepare"));
    check(!old.isCurrent() && shell.sceneDocument().scriptCatalog()
        && shell.sceneDocument().scene().name() == "Before enabling"
        && shell.retainProjectResourceManager() == resources
        && shell.sceneHistoryUsage().undoEntries == 0 && shell.sceneDirty());
    AddSceneScriptComponentCommand add(primaryId, "Mover", 1, "amount: 11\n", false);
    add.objectId = root.value();
    const auto count = shell.sceneDocument().scene().components().size();
    Host::command(shell, add);
    check(shell.sceneDocument().scene().components().size() == count);
    check(shell.submitCommand(CreateSceneObjectCommand("blocked")).diagnostic
        == EditorDiagnosticCode::WorkspaceBusy);
    check(finish(host, shell).projectIoTaskStatus == ProjectIoStatus::Succeeded);
    const auto id = shell.sceneDocument().scene().componentIds().back();
    check(amount(shell, id) == 11 && !shell.sceneDocument().scene().findComponent(id)->enabled());
    SetSceneScriptFieldsCommand fields(primaryId, "Mover", 1, "amount: 19\n", true);
    fields.objectId = root.value();
    fields.componentId = id.value();
    Host::command(shell, fields);
    check(amount(shell, id) == 11);
    check(finish(host, shell).projectIoTaskStatus == ProjectIoStatus::Succeeded
        && amount(shell, id) == 19);
    applied(host, shell, UndoSceneCommand{});
    check(amount(shell, id) == 11);
    applied(host, shell, RedoSceneCommand{});
    check(amount(shell, id) == 19);
    const auto receiver = shell.sceneDocument().scene().findComponent(id)->scriptInstance();
    const auto native = shell.sceneDocument().scene().findComponent(id)->nativeReflectionObject();
    RemoveSceneComponentCommand remove;
    remove.objectId = root.value();
    remove.componentId = id.value();
    applied(host, shell, remove);
    check(!shell.sceneDocument().scene().findComponent(id)
        && !receiver.ptr<reflection::instance>()->alive());
    applied(host, shell, UndoSceneCommand{});
    check(amount(shell, id) == 19 && !native.native_reference_alive()
        && shell.sceneDocument()
                .scene()
                .findComponent(id)
                ->nativeReflectionObject()
                .storageIdentity()
            != native.storageIdentity());
    applied(host, shell, RedoSceneCommand{});
    check(!shell.sceneDocument().scene().findComponent(id));
    applied(host, shell, UndoSceneCommand{});
    check(amount(shell, id) == 19 && shell.sceneDocument().scene().name() == "Before enabling");
    // An accepted intent is private data until dependency verification completes.
    fields.fields = "amount: 77\n";
    const auto accepted = Host::command(shell, fields);
    shell.pollTasks();
    Host::command(shell, CancelProjectIoCommand{accepted.snapshot.projectIoTaskId});
    unified_io_fixture::wait(host.queue, [&] {
        return host.mounts.telemetry().inFlight == 0;
    });
    check(amount(shell, id) == 19
        && Host::state(shell).projectIoTaskStatus == ProjectIoStatus::Cancelled);
    Host::command(shell, fields);
    shell.pollTasks(); // Teardown with a real pending verification callback.
}

void inspector(FixtureSource& fixture)
{
    Host host(fixture);
    EditorShell shell;
    host.open(shell);
    check(host.finish(shell, Host::begin(shell, "scene.yaml")).scenePersistenceTaskStatus
        == ScenePersistenceTaskStatus::Succeeded);
    Host::command(shell, SelectSceneObjectCommand{shell.sceneDocument().scene().rootId().value()});
    Controls::edit(shell, "amount", "29");
    const auto apply = [&] {
        return Host::command(shell,
            ActivateControlCommand(Controls::name(
                shell, Controls::control(shell, {}, ui::UiControlType::Button, "Apply fields"))));
    };
    const auto accepted = apply();
    check(amount(shell, fixture.fixture.scriptId) == 7);
    Host::command(shell, CancelProjectIoCommand{accepted.snapshot.projectIoTaskId});
    check(amount(shell, fixture.fixture.scriptId) == 7
        && shell.runtime()
                .findControlObject(Controls::control(shell, "amount", ui::UiControlType::TextInput))
                ->value()
                .text
            == "29");
    unified_io_fixture::wait(host.queue, [&] {
        return host.mounts.telemetry().inFlight == 0;
    });
    apply();
    check(finish(host, shell).projectIoTaskStatus == ProjectIoStatus::Succeeded);
    check(amount(shell, fixture.fixture.scriptId) == 29);
    const auto history = shell.sceneHistoryUsage().undoEntries;
    // The committed form must be clean, so the next Apply is a harmless no-op.
    apply();
    check(finish(host, shell).projectIoTaskStatus == ProjectIoStatus::Succeeded
        && shell.sceneHistoryUsage().undoEntries == history);
    SetSceneScriptFieldsCommand invalid(primaryId, "Mover", 1, "amount: wrong-type\n", true);
    invalid.objectId = shell.sceneDocument().scene().rootId().value();
    invalid.componentId = fixture.fixture.scriptId.value();
    Host::command(shell, invalid);
    check(finish(host, shell).projectIoTaskStatus == ProjectIoStatus::Failed
        && amount(shell, fixture.fixture.scriptId) == 29
        && shell.sceneHistoryUsage().undoEntries == history);
    auto oversized = invalid;
    oversized.fields.assign(EditorSceneScriptPolicy::fields().maxYamlBytes + 1, 'x');
    const auto unchanged = shell.stateVersion();
    const auto reads = host.files.telemetry().submitted;
    check(shell.submitCommand(oversized).status != EditorResponseStatus::Completed
        && shell.stateVersion() == unchanged && host.files.telemetry().submitted == reads
        && amount(shell, fixture.fixture.scriptId) == 29
        && shell.sceneHistoryUsage().undoEntries == history);
    applied(host, shell, UndoSceneCommand{});
    check(amount(shell, fixture.fixture.scriptId) == 7);
    SetSceneScriptFieldsCommand clear(secondaryId, "PhysicsLink", 1, "body: null\n", true);
    clear.objectId = fixture.fixture.child.value();
    clear.componentId = fixture.fixture.linkId.value();
    applied(host, shell, clear);
    const auto link = [&]() -> reflection::object& {
        return shell.sceneDocument()
            .scene()
            .findObject(fixture.fixture.child)
            ->findComponent(fixture.fixture.linkId)
            ->scriptInstance()
            .ptr<reflection::instance>()
            ->slot("body");
    };
    check(link().is_undefined());
    applied(host, shell, UndoSceneCommand{});
    check(link()
        == shell.sceneDocument()
            .scene()
            .findComponent(fixture.fixture.bodyId)
            ->nativeReflectionObject());
    // A typed task cannot overwrite a direct external native edit.
    invalid.fields = "amount: 42\n";
    Host::command(shell, invalid);
    shell.pollTasks();
    shell.sceneDocument().scene().setName("External edit");
    check(finish(host, shell).projectIoTaskStatus == ProjectIoStatus::Failed
        && shell.sceneDocument().scene().name() == "External edit"
        && amount(shell, fixture.fixture.scriptId) == 7);
}

void changed(FixtureSource& fixture)
{
    Host host(fixture, 1);
    EditorShell shell;
    host.open(shell);
    check(host.finish(shell, Host::begin(shell, "scene.yaml")).scenePersistenceTaskStatus
        == ScenePersistenceTaskStatus::Succeeded);
    for (const auto* name : {"project.arp.yaml", "primary.arp", "secondary.arp"}) {
        const auto bytes = fixture.fixture.read((core::pmr::String("workspace/") + name).c_str());
        fixture.write(name, bytes + "\n# changed\n");
        SetSceneScriptFieldsCommand fields(primaryId, "Mover", 1, "amount: 91\n", true);
        fields.objectId = shell.sceneDocument().scene().rootId().value();
        fields.componentId = fixture.fixture.scriptId.value();
        Host::command(shell, fields);
        const auto result = finish(host, shell);
        fixture.write(name, bytes);
        check(result.projectIoTaskStatus == ProjectIoStatus::Failed
            && amount(shell, fixture.fixture.scriptId) == 7);
    }
    SetSceneScriptFieldsCommand fields(primaryId, "Mover", 1, "amount: 8\n", true);
    fields.objectId = shell.sceneDocument().scene().rootId().value();
    fields.componentId = fixture.fixture.scriptId.value();
    Host::command(shell, fields);
    shell.pollTasks();
    check(host.mounts.unmount(host.mount));
    check(finish(host, shell).projectIoTaskStatus == ProjectIoStatus::Failed
        && amount(shell, fixture.fixture.scriptId) == 7);
}

void badPreparation(FixtureSource& fixture)
{
    // The manifest can acquire valid UTF-8 before a cold compiler sees it.
    // A preparation failure must not publish the v4 owner or clear v3 history.
    const auto original = fixture.fixture.read("workspace/primary.arp");
    fixture.write("primary.arp", "from arp_scene import main_scene\n\nclass Broken(:\n");
    {
        Host host(fixture);
        EditorShell shell;
        host.open(shell);
        SetSceneObjectNameCommand rename("Before failed preparation");
        rename.objectId = shell.sceneDocument().scene().rootId().value();
        Host::command(shell, rename);
        const auto owner = shell.sceneDocument().ownerTicket();
        const auto resources = shell.retainProjectResourceManager();
        Host::command(shell, PrepareSceneScriptAuthoringCommand{});
        const auto failed = finish(host, shell);
        check(failed.projectIoTaskStatus == ProjectIoStatus::Failed
            && !failed.projectIoDiagnostic.empty() && !shell.sceneDocument().scriptCatalog()
            && owner.sameCurrentEpoch(shell.sceneDocument().ownerTicket())
            && resources == shell.retainProjectResourceManager()
            && shell.sceneDocument().scene().name() == "Before failed preparation"
            && shell.sceneHistoryUsage().undoEntries == 1);
        const auto undo = Host::command(shell, UndoSceneCommand{});
        check(!undo.hasSnapshot
            && shell.sceneDocument().scene().name() != "Before failed preparation");
    }
    fixture.write("primary.arp", original);
}

// HTTP directory admission is separate. This verifies the exact common source
// task used by authoring, over the real transport, without fabricating a Shell.
void http(FixtureSource& fixture, std::string_view url)
{
    Host host(fixture, 2, url);
    ProjectSourceSession source(
        host.files, {host.mount, "workspace", 93}, {3, fileBytes, 3 * fileBytes});
    for (const auto* name : {"project.arp.yaml", "primary.arp", "secondary.arp"}) {
        check(source.read(name, fileBytes) == FileIoStatus::Accepted);
        unified_io_fixture::wait(host.queue, [&] {
            return source.state() != ProjectSourceState::Pending;
        });
        check(source.state() == ProjectSourceState::Ready);
    }
    EditorProjectIoBudget budget;
    EditorProjectIoTask task(ProjectSourceContext{source.finish(93), &host.files, 93}, budget);
    unified_io_fixture::wait(host.queue, [&] {
        task.advance();
        return task.ready() || task.status() != ProjectIoStatus::Pending;
    });
    check(task.ready() && task.verifyingSources() && host.files.telemetry().submitted == 6);
    task.published();
}

void remote(FixtureSource& fixture)
{
    Host host(fixture);
    EditorShell shell;
    host.open(shell);
    EditorRemoteProtocol protocol("source-authoring-token");
    const auto send = [&](std::string_view body, bool mcp = false) {
        network::http::Request request;
        request.set_method(network::http::Method::Post);
        request.set_target(mcp ? "/mcp" : "/api/v1/commands");
        request.add_header("Authorization", "Bearer source-authoring-token");
        request.add_header("Accept", "application/json");
        request.add_header("MCP-Protocol-Version", "2025-11-25");
        request.set_content_type(network::http::ContentType::Json);
        request.set_body_text(body);
        return protocol.handle(request, shell);
    };
    check(send(R"({"schemaVersion":1,"command":"scene.script.prepare","path":"forbidden"})")
              .status()
              .value()
        == 400);
    const auto* prepare =
        R"({"schemaVersion":1,"command":"scene.script.prepare","correlationId":57,"idempotencyKey":"prepare-source"})";
    const auto accepted = send(prepare);
    check(accepted.status().value() == 200);
    const auto version = shell.stateVersion();
    check(send(prepare).body_text() == accepted.body_text() && shell.stateVersion() == version);
    check(send(R"({"schemaVersion":1,"command":"scene.undo","idempotencyKey":"prepare-source"})")
              .status()
              .value()
        == 409);
    check(finish(host, shell).projectIoTaskStatus == ProjectIoStatus::Succeeded);
    const auto invalid = send(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"scene.script.prepare","arguments":{"path":"x"}}})",
        true);
    check(invalid.body_text().find("unknown_tool_argument") != std::string_view::npos);
    const auto added = send(
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"scene.script.add","arguments":{"objectId":1,"resourceId":"00000000-0000-4000-8000-000000000096","scriptType":"Mover","moduleSchemaVersion":1,"fieldsYaml":"amount: 25\n","enabled":true}}})",
        true);
    check(added.status().value() == 200
        && added.body_text().find("\"isError\":false") != std::string_view::npos);
    check(finish(host, shell).projectIoTaskStatus == ProjectIoStatus::Succeeded);
    const auto id = shell.sceneDocument().scene().componentIds().back();
    check(amount(shell, id) == 25);
    SetSceneScriptFieldsCommand fields(primaryId, "Mover", 1, "amount: 77\n", true);
    fields.objectId = shell.sceneDocument().scene().rootId().value();
    fields.componentId = id.value();
    const auto pending = Host::command(shell, fields).snapshot.projectIoTaskId;
    const auto cancel =
        core::pmr::String("{\"schemaVersion\":1,\"command\":\"project.io.cancel\",\"taskId\":")
        + std::to_string(pending).c_str() + '}';
    check(send(cancel).status().value() == 200 && amount(shell, id) == 25);
    check(Host::state(shell).projectIoTaskStatus == ProjectIoStatus::Cancelled);
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
        inspector(fixture);
        remote(fixture);
        badPreparation(fixture);
#if !defined(__EMSCRIPTEN__)
        changed(fixture);
#endif
    } else {
        const auto run = [&] {
            if (option == "--http") {
                check(argc == 3);
                http(fixture, argv[2]);
            } else if (option == "--failure-smoke") {
                badPreparation(fixture);
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
        std::printf("Source script authoring 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)after.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        check(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
    }
    std::puts(
        "PASS source cold preparation, component authoring/history and retained Inspector drafts");
}
