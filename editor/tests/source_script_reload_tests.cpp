#include "editor/tests/fixtures/source_scene_shell_fixture.h"
#include "editor/workspace/editor_scene_script_reload.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace editor;
using namespace editor::testfixtures;
using namespace editor::testfixtures::sources;

void reloadCheck(int line, bool value, const core::pmr::String& diagnostic = {})
{
    if (!value)
        std::fprintf(stderr, "Reload check at line %d: %s\n", line, diagnostic.c_str());
    editor::testfixtures::check(value, diagnostic);
}

#define check(...) reloadCheck(__LINE__, __VA_ARGS__)

EditorStateSnapshot projectFinish(Host& host, EditorShell& shell, bool success = true)
{
    unified_io_fixture::wait(host.queue, [&] {
        shell.pollTasks();
        return Host::state(shell).projectIoTaskStatus != ProjectIoStatus::Pending;
    });
    auto result = Host::state(shell);
    check(result.projectIoTaskStatus
            == (success ? ProjectIoStatus::Succeeded : ProjectIoStatus::Failed),
        result.projectIoDiagnostic);
    return result;
}

void saveScript(Host& host, EditorShell& shell, std::string_view text)
{
    Host::command(shell, ReplaceScriptTextCommand(shell.activeScriptDocumentId(), text));
    Host::command(shell, SaveScriptCommand{shell.activeScriptDocumentId()});
    const auto result = projectFinish(host, shell);
    check(result.scriptSaveReceipt.published && !shell.scriptDirty());
}

void cycle(FixtureSource& source, unsigned mode)
{
    // Native writes affect the disposable project. Each cycle starts from the
    // same explicit corpus, independent of the previous cycle's saved bindings.
    source.write("primary.arp", Fixture::primarySource);
    source.write("scene.yaml", source.scene);
    Host host(source, mode, {}, true);
    EditorShell shell;
    host.open(shell);
    ScriptSceneShellFixture::succeeded(host.finish(shell, Host::begin(shell, "scene.yaml")));
    auto oldReceiver =
        shell.sceneDocument().scene().findComponent(source.fixture.scriptId)->scriptInstance();
    const auto selected = shell.sceneDocument().selectedObject();
    SetSceneObjectNameCommand rename("Reload retained child");
    rename.objectId = source.fixture.child.value();
    Host::command(shell, rename);
    Host::command(shell, OpenScriptCommand{"primary.arp"});
    projectFinish(host, shell);
    core::pmr::String changed(Fixture::primarySource);
    changed += "\n# explicit reload one\n";
    saveScript(host, shell, changed);
    // Ordinary disk load must keep its strict source guard and retain the live
    // document even though the registered file has already been saved.
    const auto oldOwner = shell.sceneDocument().ownerTicket();
    const auto failed = host.finish(shell, Host::begin(shell, "scene.yaml"));
    check(failed.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed);
    check(oldOwner.sameCurrentEpoch(shell.sceneDocument().ownerTicket()));
    // Exercise the authored button through the real REST allowlist and panel
    // dispatcher, in addition to the typed command used by subsequent probes.
    Host::command(shell, ActivateControlCommand("workspace_Script"));
    EditorRemoteProtocol protocol("script-reload-token");
    network::http::Request reloadRequest;
    reloadRequest.set_method(network::http::Method::Post);
    reloadRequest.set_target("/api/v1/commands");
    reloadRequest.add_header("Authorization", "Bearer script-reload-token");
    reloadRequest.set_content_type(network::http::ContentType::Json);
    reloadRequest.set_body_text(
        R"({"schemaVersion":1,"command":"editor.activate","source":"script_ui_reload_modules"})");
    const auto beforeTask = Host::state(shell).projectIoTaskId;
    const auto activated = protocol.handle(reloadRequest, shell);
    check(activated.status().value() == 200, core::pmr::String(activated.body_text()));
    check(Host::state(shell).projectIoTaskId > beforeTask);
    const auto first = projectFinish(host, shell);
    check(first.sceneDirty && !oldOwner.isCurrent());
    bool hasScriptFields = false;
    for (const auto id :
        shell.runtime().canvas().findWidget(shell.id("inspector_content"))->childIds()) {
        const auto* widget = shell.runtime().canvas().findWidget(id);
        if (widget && EditorScriptInspector::handles(widget->name())) {
            hasScriptFields = true;
            check(!widget->visible());
        }
    }
    check(hasScriptFields);
    check(!oldReceiver.ptr<reflection::instance>()->alive());
    check(shell.sceneDocument().selectedObject() == selected);
    check(shell.sceneDocument().scene().name() == "Shell script root");
    check(shell.sceneDocument().scene().findObject(source.fixture.child)->name()
        == "Reload retained child");
    auto* body = shell.sceneDocument().scene().findComponent(source.fixture.bodyId);
    auto* link = shell.sceneDocument()
                     .scene()
                     .findObject(source.fixture.child)
                     ->findComponent(source.fixture.linkId);
    auto* script = shell.sceneDocument().scene().findComponent(source.fixture.scriptId);
    check(body && link && script);
    check(script->scriptInstance().ptr<reflection::instance>()->slot("amount").cast<int>() == 7);
    check(link->scriptInstance().ptr<reflection::instance>()->slot("body")
        == body->nativeReflectionObject());
    // Repeated reload before saving composes a single original-file proof.
    changed += "# explicit reload two\n";
    saveScript(host, shell, changed);
    Host::command(shell, ReloadSceneScriptsCommand{});
    projectFinish(host, shell);
    const auto owner = shell.sceneDocument().ownerTicket();
    // A valid new source with an extra field must not invent a default.
    auto incompatible = changed;
    const auto field = incompatible.find("    amount: int");
    check(field != core::pmr::String::npos);
    incompatible.insert(field, "    added: int\n");
    saveScript(host, shell, incompatible);
    Host::command(shell, ReloadSceneScriptsCommand{});
    projectFinish(host, shell, false);
    check(owner.sameCurrentEpoch(shell.sceneDocument().ownerTicket()));
    auto scalarChanged = changed;
    scalarChanged.replace(
        scalarChanged.find("amount: int"), std::string_view("amount: int").size(), "amount: float");
    saveScript(host, shell, scalarChanged);
    Host::command(shell, ReloadSceneScriptsCommand{});
    projectFinish(host, shell, false);
    check(owner.sameCurrentEpoch(shell.sceneDocument().ownerTicket()));
    saveScript(host, shell, "class Broken\n");
    Host::command(shell, ReloadSceneScriptsCommand{});
    projectFinish(host, shell, false);
    check(owner.sameCurrentEpoch(shell.sceneDocument().ownerTicket()));
    saveScript(host, shell, changed);
    Host::command(shell, ReloadSceneScriptsCommand{});
    projectFinish(host, shell);
    // The proof is scoped to the original saved target, never another archive.
    SaveSceneCommand other("bom.yaml");
    other.overwriteExisting = true;
    const auto otherId = Host::command(shell, other).snapshot.scenePersistenceTaskId;
    const auto rejected = host.finish(shell, otherId);
    check(rejected.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
        && !rejected.sceneSaveReceipt.submitted && !rejected.scenePersistenceTaskCommitted);
    if (mode == 1) {
        source.write("scene.yaml", source.scene + "\n# outside writer\n");
        const auto id =
            Host::command(shell, SaveSceneCommand("scene.yaml")).snapshot.scenePersistenceTaskId;
        const auto conflict = host.finish(shell, id);
        check(conflict.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
            && !conflict.scenePersistenceTaskCommitted && shell.sceneDirty());
        source.write("scene.yaml", source.scene);
    }
    const auto saved = Host::command(shell, SaveSceneCommand("scene.yaml")).snapshot;
    const auto completed = host.finish(shell, saved.scenePersistenceTaskId);
    ScriptSceneShellFixture::succeeded(completed, true);
    check(completed.sceneSaveReceipt.published && !shell.sceneDirty());
    ScriptSceneShellFixture::succeeded(host.finish(shell, Host::begin(shell, "scene.yaml")));
    check(shell.sceneDocument().scene().findObject(source.fixture.child)->name()
        == "Reload retained child");
    const auto stable = shell.sceneDocument().ownerTicket();
    const auto submissions = host.files.telemetry().submitted;
    const auto cancelId =
        Host::command(shell, ReloadSceneScriptsCommand{}).snapshot.projectIoTaskId;
    unified_io_fixture::wait(host.queue, [&] {
        shell.pollTasks();
        return host.files.telemetry().submitted > submissions;
    });
    Host::command(shell, CancelProjectIoCommand{cancelId});
    check(Host::state(shell).projectIoTaskStatus == ProjectIoStatus::Cancelled);
    unified_io_fixture::wait(host.queue, [&] {
        return host.mounts.telemetry().inFlight == 0;
    });
    check(stable.sameCurrentEpoch(shell.sceneDocument().ownerTicket()) && !shell.sceneDirty());
    if (mode == 1) {
        const auto manifest = source.fixture.read("workspace/project.arp.yaml");
        source.write("project.arp.yaml", manifest + "\n# external project change\n");
        Host::command(shell, ReloadSceneScriptsCommand{});
        const auto failed = projectFinish(host, shell, false);
        check(failed.projectIoDiagnostic == "EDITOR-SCRIPT-RELOAD-NONMODULE-CHANGE");
        check(stable.sameCurrentEpoch(shell.sceneDocument().ownerTicket()));
        source.write("project.arp.yaml", manifest);
    }
    // A failed private preparation preserves a previously prepared candidate.
    auto input = EditorSceneScriptPolicy::inputs(shell.projectRoot(), *shell.projectManifest());
    input.sources = shell.sceneDocument().retainedScriptSources();
    core::pmr::String diagnostic;
    auto prepared =
        EditorSceneScriptReload::prepare(shell.sceneDocument(), input, "", "", nullptr, diagnostic);
    check(bool(prepared), diagnostic);
    auto denied = input;
    denied.preparationBytes = 1;
    check(!EditorSceneScriptReload::prepare(
        shell.sceneDocument(), denied, "", "", nullptr, diagnostic));
    check(prepared->candidate().scene().findObject(source.fixture.child) != nullptr);
}
} // namespace

int runSourceScriptReloadTests(int argc, char** argv)
{
    const auto option = argc > 1 ? std::string_view(argv[1]) : std::string_view{};
    if (option != "--reload" && option != "--reload-smoke" && option != "--reload-native"
        && option != "--reload-native-smoke")
        return -1;
    const unsigned mode = option.find("native") != std::string_view::npos ? 1 : 0;
    const unsigned cycles = option.find("smoke") != std::string_view::npos ? 20 : 1;
    FixtureSource source;
    cycle(source, mode);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < cycles; ++i)
        cycle(source, mode);
    const auto after = core::defaultCpuAllocator().stats();
    check(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::printf("Explicit script reload %u cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
        cycles,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
    return 0;
}
