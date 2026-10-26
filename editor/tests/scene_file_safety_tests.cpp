#include "core/io/file_io.h"
#include "editor/tests/fixtures/scene_persistence_fixture.h"
#include "editor/workspace/editor_shell.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <future>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using namespace arp;
using namespace arp::editor;
using Task = tests::ScenePersistenceFixture;

// Each test owns its complete disk workspace; external edits use raw bytes so
// the test does not normalize the very revision it is meant to protect.
class SceneFileFixture final {
public:
    SceneFileFixture()
    {
        root = std::filesystem::path(ARP_SOURCE_DIR) / "build"
            / ("scene-file-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
    }

    ~SceneFileFixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        assert(!error);
    }

    void initialize(EditorShell& shell) const
    {
        assert(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", root)
                .ok());
        completed(shell, CreateSceneObjectCommand("Local A"));
    }

    static void completed(EditorShell& shell, EditorCommandPayload command)
    {
        assert(shell.submitCommand(std::move(command)).status == EditorResponseStatus::Completed);
    }

    static EditorStateSnapshot save(
        EditorShell& shell, std::string_view path, bool overwrite = false)
    {
        SaveSceneCommand command(path);
        command.overwriteExisting = overwrite;
        return Task::finish(shell, shell.submitCommand(command));
    }

    static void succeeded(const EditorStateSnapshot& result)
    {
        if (result.scenePersistenceTaskStatus != ScenePersistenceTaskStatus::Succeeded)
            std::fprintf(
                stderr, "scene file failed: %s\n", result.scenePersistenceTaskDiagnostic.c_str());
        assert(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded);
    }

    static void conflict(const EditorStateSnapshot& result)
    {
        assert(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
            && result.scenePersistenceTaskDiagnostic == "EDITOR-SCENE-FILE-EXTERNAL-CHANGE");
    }

    core::pmr::String read(const char* name) const
    {
        const auto result = core::readTextFile(root / name);
        assert(result.success);
        return result.text;
    }

    void write(const char* name, std::string_view bytes) const
    {
        std::ofstream file(root / name, std::ios::binary | std::ios::trunc);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.close();
        assert(file.good());
    }

    static core::pmr::String external(core::pmr::String bytes)
    {
        const auto name = bytes.find("Local A");
        assert(name != core::pmr::String::npos);
        bytes.replace(name, 7, "Outside");
        return bytes;
    }

    static core::pmr::String crlf(std::string_view source)
    {
        core::pmr::String result;
        for (char c : source) {
            if (c == '\n')
                result += '\r';
            result += c;
        }
        return result;
    }

    std::filesystem::path root;
};

void diskConflict(SceneFileFixture& fixture)
{
    using F = SceneFileFixture;
    EditorShell shell;
    fixture.initialize(shell);
    F::succeeded(F::save(shell, "scene.arp.yaml"));
    const auto original = fixture.read("scene.arp.yaml");
    const auto rootId = shell.sceneDocument().scene().rootId();
    const auto selected = shell.sceneDocument().selectedObject();
    fixture.write("rules.arp", "# disk\n");
    assert(shell.openScriptFile(fixture.root / "rules.arp"));
    const auto script = shell.activeScriptDocumentId();
    F::completed(shell, ReplaceScriptTextCommand(script, "# local script\n"));
    F::completed(shell, SetSceneObjectNameCommand("Local B"));
    const auto timestamp = std::filesystem::last_write_time(fixture.root / "scene.arp.yaml");
    const auto changed = F::external(original);
    assert(changed.size() == original.size());
    fixture.write("scene.arp.yaml", changed);
    std::filesystem::last_write_time(fixture.root / "scene.arp.yaml", timestamp);
    const auto accepted = shell.submitCommand(SaveSceneCommand("scene.arp.yaml"));
    assert(shell.submitCommand(NewSceneCommand{true}).diagnostic
        == EditorDiagnosticCode::WorkspaceBusy);
    assert(shell.submitCommand(RequestEditorExitCommand{true}).diagnostic
        == EditorDiagnosticCode::WorkspaceBusy);
    assert(shell.submitCommand(SaveSceneCommand("other.arp.yaml")).diagnostic
        == EditorDiagnosticCode::WorkspaceBusy);
    assert(shell
               .submitCommand(
                   CompleteScenePersistenceCommand{accepted.snapshot.scenePersistenceTaskId})
               .diagnostic
        == EditorDiagnosticCode::InvalidCommandPayload);
    F::conflict(Task::finish(shell, accepted));
    const auto* output = shell.runtime().canvas().findWidget(shell.id("output_status"));
    assert(output && output->desc().text.find("File changed on disk. Local edits kept.") == 0
        && output->desc().text.find_first_of("\r\n") == core::pmr::String::npos);
    assert(shell.sceneDocument().scene().rootId() == rootId
        && shell.sceneDocument().selectedObject() == selected && shell.sceneDirty());
    assert(fixture.read("scene.arp.yaml") == changed && shell.scriptText() == "# local script\n"
        && shell.scriptDirty());
    F::completed(shell, UndoSceneCommand{});
    assert(!shell.sceneDirty());
    F::completed(shell, RedoSceneCommand{});
    F::conflict(F::save(shell, "scene.arp.yaml", true));
    fixture.write("scene.arp.yaml", original);
    const auto saving = shell.submitCommand(SaveSceneCommand("scene.arp.yaml"));
    F::completed(shell, SetSceneObjectNameCommand("Local C"));
    F::succeeded(Task::finish(shell, saving));
    assert(shell.sceneDirty()
        && fixture.read("scene.arp.yaml").find("Local B") != core::pmr::String::npos);
    F::completed(shell, UndoSceneCommand{});
    assert(!shell.sceneDirty());
    F::completed(shell, RedoSceneCommand{});
    F::succeeded(F::save(shell, "scene.arp.yaml"));
    assert(!shell.sceneDirty() && shell.scriptText() == "# local script\n");
    assert(shell.drawCommands().ok());
    std::puts(
        "PASS same-length/same-time external conflict, exact task/busy guards, explicit overwrite cannot bypass, scene/script history retained, retry and submitted save point");
}

void revisionsAndReset(SceneFileFixture& fixture)
{
    using F = SceneFileFixture;
    EditorShell shell;
    fixture.initialize(shell);
    F::succeeded(F::save(shell, "revision.arp.yaml"));
    const auto original = fixture.read("revision.arp.yaml");
    fixture.write("revision.arp.yaml", F::crlf(original));
    F::conflict(F::save(shell, "revision.arp.yaml"));
    LoadSceneCommand load("revision.arp.yaml");
    F::succeeded(Task::finish(shell, shell.submitCommand(load)));
    F::completed(shell, SetSceneObjectNameCommand("Local D"));
    F::succeeded(F::save(shell, "revision.arp.yaml"));
    assert(fixture.read("revision.arp.yaml").find("Local D") != core::pmr::String::npos);
    fixture.write("broken.arp.yaml", "type: broken\ncontractVersion: 99\n");
    const auto bad = F::save(shell, "broken.arp.yaml", true);
    assert(bad.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
        && bad.scenePersistenceTaskDiagnostic == "ARP-SAVE-UNSUPPORTED-CONTRACT"
        && bad.scenePath == "revision.arp.yaml");
    assert(fixture.read("broken.arp.yaml") == "type: broken\ncontractVersion: 99\n");
    // Failed/stale loads must not replace the old disk binding.
    const auto loading = shell.submitCommand(load);
    F::completed(shell, SetSceneObjectNameCommand("Local E"));
    const auto stale = Task::finish(shell, loading);
    assert(stale.scenePersistenceTaskDiagnostic == "EDITOR-SCENE-LOAD-STALE-DOCUMENT");
    F::succeeded(F::save(shell, "revision.arp.yaml"));
    F::completed(shell, DuplicateSceneCommand{});
    assert(shell.submitCommand(SaveSceneCommand("revision.arp.yaml")).diagnostic
        == EditorDiagnosticCode::SceneOverwriteRequired);
    F::succeeded(F::save(shell, "independent.arp.yaml"));
    F::completed(shell, NewSceneCommand{});
    assert(shell.submitCommand(SaveSceneCommand("independent.arp.yaml")).diagnostic
        == EditorDiagnosticCode::SceneOverwriteRequired);
    std::puts(
        "PASS raw CRLF load baseline, newline-only conflict, unsupported file preserved, stale load binding, Save As/duplicate/new revision isolation");
}

void aliasesAndFailures(SceneFileFixture& fixture)
{
    using F = SceneFileFixture;
    EditorShell shell;
    fixture.initialize(shell);
    F::succeeded(F::save(shell, "bound.arp.yaml"));
    const auto original = fixture.read("bound.arp.yaml");
    std::error_code error;
    std::filesystem::create_hard_link(
        fixture.root / "bound.arp.yaml", fixture.root / "alias.arp.yaml", error);
    assert(!error);
    F::completed(shell, SetSceneObjectNameCommand("Local B"));
    fixture.write("bound.arp.yaml", F::external(original));
    F::conflict(F::save(shell, "alias.arp.yaml"));
#ifdef _WIN32
    F::conflict(F::save(shell, "BOUND.ARP.YAML"));
#endif
    fixture.write("bound.arp.yaml", original);
#ifdef _WIN32
    const auto unreadable = CreateFileW((fixture.root / "bound.arp.yaml").c_str(),
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    assert(unreadable != INVALID_HANDLE_VALUE);
    const auto readAttempt = shell.submitCommand(SaveSceneCommand("bound.arp.yaml"));
    if (readAttempt.status == EditorResponseStatus::Completed) {
        const auto failedRead = Task::finish(shell, readAttempt);
        assert(failedRead.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
            && failedRead.scenePersistenceTaskDiagnostic.find("EDITOR-SCENE-FILE-READ-FAILED")
                == 0);
    } else
        assert(readAttempt.diagnostic == EditorDiagnosticCode::InvalidCommandPayload);
    assert(CloseHandle(unreadable));
    assert(fixture.read("bound.arp.yaml") == original);
    const auto file = CreateFileW((fixture.root / "bound.arp.yaml").c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    assert(file != INVALID_HANDLE_VALUE);
    const auto locked = F::save(shell, "bound.arp.yaml");
    assert(locked.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
        && locked.scenePersistenceTaskDiagnostic == "ARP-FILE-ATOMIC-REPLACE");
    assert(CloseHandle(file));
    assert(fixture.read("bound.arp.yaml") == original);
    for (const auto& entry : std::filesystem::directory_iterator(fixture.root))
        assert(entry.path().filename().string().find(".arp-tmp-") == std::string::npos);
#endif
    const core::pmr::String oversized(EditorSceneAuthoringCodec::maxDocumentBytes + 1u, 'x');
    fixture.write("bound.arp.yaml", oversized);
    const auto limited = F::save(shell, "bound.arp.yaml");
    assert(limited.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
        && limited.scenePersistenceTaskDiagnostic.find("EDITOR-SCENE-FILE-READ-FAILED") == 0
        && std::filesystem::file_size(fixture.root / "bound.arp.yaml") == oversized.size());
    std::filesystem::remove(fixture.root / "bound.arp.yaml");
    const auto missing = F::save(shell, "bound.arp.yaml");
    assert(missing.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
        && missing.scenePersistenceTaskDiagnostic == "EDITOR-SCENE-FILE-MISSING");
    assert(!std::filesystem::exists(fixture.root / "bound.arp.yaml") && shell.sceneDirty());
    std::filesystem::create_directory(fixture.root / "bound.arp.yaml");
    assert(shell.submitCommand(SaveSceneCommand("bound.arp.yaml")).diagnostic
        == EditorDiagnosticCode::InvalidCommandPayload);
    std::filesystem::remove(fixture.root / "bound.arp.yaml");
    fixture.write("bound.arp.yaml", original);
    F::succeeded(F::save(shell, "bound.arp.yaml"));
    fixture.write("source-a.arp.yaml", original);
    const auto redirected = F::external(original);
    fixture.write("source-b.arp.yaml", redirected);
    std::filesystem::create_symlink("source-a.arp.yaml", fixture.root / "route.arp.yaml", error);
    if (!error) {
        F::succeeded(Task::finish(shell, shell.submitCommand(LoadSceneCommand("route.arp.yaml"))));
        F::completed(shell, SetSceneObjectNameCommand("Local B"));
        std::filesystem::remove(fixture.root / "route.arp.yaml");
        std::filesystem::create_symlink("source-b.arp.yaml", fixture.root / "route.arp.yaml");
        const auto version = shell.stateVersion();
        SaveSceneCommand retargeted("route.arp.yaml");
        retargeted.overwriteExisting = true;
        assert(shell.submitCommand(retargeted).diagnostic
                == EditorDiagnosticCode::InvalidCommandPayload
            && shell.stateVersion() == version && shell.sceneDirty());
        assert(fixture.read("source-a.arp.yaml") == original
            && fixture.read("source-b.arp.yaml") == redirected);
        F::succeeded(F::save(shell, "routed-copy.arp.yaml"));
        std::puts(
            "PASS admitted symlink retarget rejects even explicit overwrite; distinct Save As remains available");
    } else
        std::fprintf(stderr,
            "SKIP symlink retarget: filesystem denied creation (%s)\n",
            error.message().c_str());
    std::puts(
        "PASS filesystem hard-link/case aliases cannot bypass conflict, unreadable/oversized file, atomic replace failure/temp cleanup, missing/directory target and safe retry");
}

void lifetime(SceneFileFixture& fixture)
{
    using F = SceneFileFixture;
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i <= 20; ++i) {
        {
            EditorShell shell;
            fixture.initialize(shell);
            F::succeeded(F::save(shell, "cycle.arp.yaml", true));
            const auto original = fixture.read("cycle.arp.yaml");
            F::completed(shell, SetSceneObjectNameCommand("Local B"));
            fixture.write("cycle.arp.yaml", F::external(original));
            F::conflict(F::save(shell, "cycle.arp.yaml"));
            fixture.write("cycle.arp.yaml", original);
            F::succeeded(F::save(shell, "cycle.arp.yaml"));
            assert(shell.drawCommands().ok());
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!i)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary scene preflight/conflict/retry/destroy cycles; core live bytes/count delta=0");
}

// The worker owns only immutable bytes and its cold validation state; the
// foreground owns the live document. Their only shared payload is registered
// immutable reflection metadata, exactly the supported cross-owner boundary.
void metadataLifetime(SceneFileFixture& fixture)
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            EditorShell shell;
            fixture.initialize(shell);
            SceneFileFixture::completed(shell, AddSceneComponentCommand("LightComponent"));
            core::pmr::String bytes, diagnostic;
            assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), bytes, diagnostic));
            assert(EditorSceneAuthoringCodec::validateStructure(bytes, diagnostic));
            const auto target = shell.sceneDocument().selectedObject();
            assert(!shell.sceneDocument().inspectorProperties(target).empty());
            std::promise<void> admission;
            auto start = admission.get_future();
            auto worker =
                std::async(std::launch::async, [input = bytes, start = std::move(start)]() mutable {
                    start.wait();
                    core::pmr::String workerDiagnostic;
                    for (unsigned round = 0; round < 4; ++round)
                        assert(
                            EditorSceneAuthoringCodec::validateStructure(input, workerDiagnostic));
                });
            admission.set_value();
            for (unsigned round = 0; round < 4; ++round) {
                core::pmr::String encoded, hash;
                assert(
                    EditorSceneAuthoringCodec::encode(shell.sceneDocument(), encoded, diagnostic));
                assert(encoded == bytes);
                assert(EditorSceneAuthoringCodec::contentHash(
                    shell.sceneDocument(), hash, diagnostic));
                assert(!shell.sceneDocument().inspectorProperties(target).empty());
            }
            worker.get();
            SceneFileFixture::succeeded(SceneFileFixture::save(shell, "metadata.arp.yaml", true));
            assert(shell.drawCommands().ok());
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes);
        assert(current.allocationCount - current.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary background schema validation with foreground encode/hash/Inspector, save/draw/destruction; core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    SceneFileFixture fixture;
    if (argc == 2 && std::string_view(argv[1]) == "--metadata-smoke")
        metadataLifetime(fixture);
    else if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifetime(fixture);
    else {
        diskConflict(fixture);
        revisionsAndReset(fixture);
        aliasesAndFailures(fixture);
    }
}
