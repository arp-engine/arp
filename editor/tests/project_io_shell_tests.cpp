#include "core/io/file_io.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "editor/remote/editor_remote_protocol.h"
#include "editor/workspace/editor_shell.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <algorithm>
#include <cassert>
#include <cstdio>

namespace {
using namespace arp;
using namespace editor;
using namespace core::io;
constexpr std::size_t fileBytes = 1024 * 1024;
const auto fixtureRoot = std::filesystem::path(ARP_SOURCE_DIR) / "tests/fixtures/project_resources";

// Seed explicit golden exported bytes, never substitute host reads for missing
// mounted content. The Shell itself only receives the mount, not this path.
core::SharedPtr<MemoryFileBackend> memory(bool malformed = false, bool writable = false)
{
    auto backend = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{64, 4 * fileBytes, 1, 8 * fileBytes, 512, writable});
    core::pmr::Vector<std::filesystem::path> entries;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(fixtureRoot))
        entries.push_back(entry.path());
    std::sort(entries.begin(), entries.end());
    for (const auto& entry : entries) {
        const auto path = core::FilePath::utf8(entry.lexically_relative(fixtureRoot));
        if (std::filesystem::is_directory(entry))
            assert(backend->registerDirectory(path) == FileIoStatus::Success);
        else {
            auto read = core::readBinaryFile(entry, fileBytes);
            assert(read.success);
            if (malformed && path == "workspace/project.arp.yaml")
                read.bytes = {'b', 'r', 'o', 'k', 'e', 'n'};
            assert(backend->registerFile(path, read.bytes.data(), read.bytes.size())
                == FileIoStatus::Success);
        }
    }
    return backend;
}

class Host final {
public:
    explicit Host(bool native = false,
        bool malformed = false,
        const std::filesystem::path& source = fixtureRoot,
        bool writable = false)
    {
        if (native) {
            auto desc = workspace.descriptor();
            if (writable) {
                std::filesystem::copy(source,
                    workspace.root,
                    std::filesystem::copy_options::recursive
                        | std::filesystem::copy_options::overwrite_existing);
            }
            desc.rootPath = core::FilePath::utf8(writable ? workspace.root : source);
            desc.maxInFlight = 1;
            desc.maxBytesInFlight = writable ? 8 * fileBytes : 2 * fileBytes;
            desc.ioChunkBytes = 4096;
            desc.allowObservedBaselineReplace = writable;
            mount = mounts.mount("workspace", core::makeShared<NativeDirectoryFileBackend>(desc));
        } else {
            memoryFiles = memory(malformed, writable);
            mount = mounts.mount("workspace", memoryFiles);
        }
        assert(mount);
    }

    void initialize(EditorShell& shell)
    {
        assert(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                    workspace.root)
                .ok());
        auto invalid = budget();
        invalid.directory.pageSize = 0;
        assert(!shell.bindProjectSourceFiles(files, mount, "workspace", invalid));
        assert(!shell.bindProjectSourceFiles(files, mount, "../workspace", budget()));
        assert(shell.bindProjectSourceFiles(files, mount, "workspace", budget()));
    }

    static EditorProjectIoBudget budget()
    {
        EditorProjectIoBudget result;
        result.inputs = {64, fileBytes, 4 * fileBytes};
        result.directory = {32, 1, 4096, 8192};
        result.maxDecodedBytes = 4 * fileBytes;
        return result;
    }

    static EditorStateSnapshot state(EditorShell& shell)
    {
        const auto result = shell.submitCommand(GetEditorStateQuery{});
        assert(result.status == EditorResponseStatus::Completed && result.hasSnapshot);
        return result.snapshot;
    }

    static EditorResponse completed(EditorShell& shell, EditorCommandPayload payload)
    {
        auto result = shell.submitCommand(std::move(payload));
        if (result.status != EditorResponseStatus::Completed)
            std::fprintf(stderr, "rejected %u\n", unsigned(result.diagnostic));
        assert(result.status == EditorResponseStatus::Completed);
        return result;
    }

    static void rejected(
        EditorShell& shell, EditorCommandPayload payload, EditorDiagnosticCode code)
    {
        const auto before = shell.stateVersion();
        const auto result = shell.submitCommand(std::move(payload));
        assert(result.status != EditorResponseStatus::Completed && result.diagnostic == code);
        assert(shell.stateVersion() == before);
    }

    static EditorResponse start(
        EditorShell& shell, const char* path = "workspace", bool discard = false)
    {
        OpenProjectCommand open{path, discard};
        open.source = "workspace";
        return completed(shell, std::move(open));
    }

    EditorStateSnapshot finish(EditorShell& shell)
    {
        unified_io_fixture::wait(queue, [&] {
            shell.pollTasks();
            return state(shell).projectIoTaskStatus != ProjectIoStatus::Pending;
        });
        return state(shell);
    }

    unified_io_fixture::Workspace workspace;
    core::runtime::CompletionQueue queue{core::runtime::CompletionQueueDesc{8}};
    MountedFileBackend mounts{MountedFileBackendDesc{2, 1, 8 * fileBytes, 128, 512}};
    FileService files{queue, mounts};
    core::PoolHandle mount;
    core::SharedPtr<MemoryFileBackend> memoryFiles;
};

void lifecycle(bool native)
{
    Host host(native);
    {
        EditorShell shell;
        host.initialize(shell);
        const auto owner = shell.retainProjectResourceManager();
        const auto originalScene = shell.sceneDocument().scene().rootId();
        auto accepted = Host::start(shell);
        assert(accepted.snapshot.projectIoTaskStatus == ProjectIoStatus::Pending);
        assert(accepted.snapshot.projectIoTaskId && !Host::state(shell).hasProject);
        assert(shell.retainProjectResourceManager() == owner
            && shell.sceneDocument().scene().rootId() == originalScene);
        Host::rejected(
            shell, CreateSceneObjectCommand("must-not-write"), EditorDiagnosticCode::WorkspaceBusy);
        auto opened = host.finish(shell);
        if (opened.projectIoTaskStatus != ProjectIoStatus::Succeeded)
            std::fprintf(stderr, "project task: %s\n", opened.projectIoDiagnostic.c_str());
        assert(opened.projectIoTaskStatus == ProjectIoStatus::Succeeded && opened.hasProject);
        assert(opened.projectSource == "workspace" && opened.projectRoot == "workspace"
            && opened.projectDirectory == "workspace" && !opened.sceneDirty);
        assert(shell.retainProjectResourceManager() != owner);
        assert(shell.sceneDocument().scene().findObjectByName(core::pmr::String("Camera3D")));
        assert(
            shell.sceneDocument().scene().findObjectByName(core::pmr::String("DirectionalLight")));
        assert(shell.fileEntryCount() == 4);
        const auto publishedOwner = shell.retainProjectResourceManager();
        const auto scene = shell.sceneDocument().scene().rootId();
        const auto check = [&](ProjectBrowserModel::SourceNavigation intent,
                               const char* target,
                               const char* expected) {
            NavigateProjectSourceCommand nav;
            nav.intent = intent;
            nav.directory = target;
            const auto before = Host::state(shell);
            Host::completed(shell, std::move(nav));
            assert(Host::state(shell).projectDirectory == before.projectDirectory);
            const auto after = host.finish(shell);
            assert(after.projectIoTaskStatus == ProjectIoStatus::Succeeded
                && after.projectDirectory == expected
                && shell.retainProjectResourceManager() == publishedOwner
                && shell.sceneDocument().scene().rootId() == scene);
        };
        using N = ProjectBrowserModel::SourceNavigation;
        check(N::Navigate, "scripts", "workspace/scripts");
        assert(shell.fileEntryCount() == 1);
        check(N::Back, "", "workspace");
        check(N::Forward, "", "workspace/scripts");
        check(N::Refresh, "", "workspace/scripts");
        check(N::Navigate, "", "workspace");
        Host::rejected(shell, SaveProjectCommand{}, EditorDiagnosticCode::ProjectOperationFailed);
        Host::rejected(
            shell, SaveSceneCommand("scene.yaml"), EditorDiagnosticCode::ProjectOperationFailed);
        const auto missingScene = Host::completed(shell, LoadSceneCommand("scene.yaml"));
        assert(missingScene.snapshot.scenePersistenceTaskStatus
            == ScenePersistenceTaskStatus::Pending);
        unified_io_fixture::wait(host.queue, [&] {
            shell.pollTasks();
            return !Host::state(shell).sceneIoPending;
        });
        assert(Host::completed(shell,
                   GetScenePersistenceTaskQuery{missingScene.snapshot.scenePersistenceTaskId})
                   .snapshot.scenePersistenceTaskStatus
            == ScenePersistenceTaskStatus::Failed);
        assert(shell.openScriptFile("workspace/scripts/gameplay.arp"));
        assert(host.finish(shell).projectIoTaskStatus == ProjectIoStatus::Succeeded);
        const auto script = Host::state(shell);
        assert(script.hasActiveDocument && script.openDocumentCount == 1
            && script.workspace == "Script" && script.scriptRelativePath == "scripts/gameplay.arp");
        Host::completed(shell, ReloadScriptCommand{script.activeDocumentId, false});
        const auto reloaded = host.finish(shell);
        assert(reloaded.projectIoTaskStatus == ProjectIoStatus::Succeeded
            && reloaded.activeDocumentId == script.activeDocumentId
            && reloaded.scriptText == script.scriptText && !reloaded.scriptDirty);
        Host::rejected(shell,
            SaveScriptCommand{script.activeDocumentId},
            EditorDiagnosticCode::ProjectOperationFailed);
        assert(!std::filesystem::exists(host.workspace.root / "scene.yaml"));
        auto cancel = Host::start(shell);
        shell.pollTasks(); // Accept a real read, cancel before its callback.
        Host::rejected(shell,
            CancelProjectIoCommand{cancel.snapshot.projectIoTaskId + 1},
            EditorDiagnosticCode::InvalidCommandPayload);
        Host::completed(shell, CancelProjectIoCommand{cancel.snapshot.projectIoTaskId});
        unified_io_fixture::wait(host.queue, [&] {
            return host.mounts.telemetry().inFlight == 0;
        });
        assert(Host::state(shell).projectIoTaskStatus == ProjectIoStatus::Cancelled
            && shell.retainProjectResourceManager() == publishedOwner);
        Host::start(shell, "missing");
        const auto missing = host.finish(shell);
        assert(missing.projectIoTaskStatus == ProjectIoStatus::Failed
            && shell.retainProjectResourceManager() == publishedOwner
            && shell.sceneDocument().scene().rootId() == scene);
        Host::completed(shell, CreateSceneObjectCommand("unsaved"));
        OpenProjectCommand protectedOpen{"workspace"};
        protectedOpen.source = "workspace";
        Host::rejected(shell, protectedOpen, EditorDiagnosticCode::UnsavedChanges);
        Host::start(shell, "workspace", true);
        assert(host.finish(shell).projectIoTaskStatus == ProjectIoStatus::Succeeded);
        Host::completed(shell, CloseProjectCommand{});
        assert(!Host::state(shell).hasProject && Host::state(shell).projectSource == "workspace");
        Host::start(shell);
        shell.pollTasks(); // Shell destruction cancels an accepted read.
    }
    host.mounts.shutdown();
    while (host.queue.pending())
        host.queue.dispatch(8);
    assert(host.mounts.telemetry().inFlight == 0);
}

void projectEntrySourceSave()
{
    Host host(false, false, fixtureRoot, true);
    EditorShell shell;
    host.initialize(shell);
    Host::start(shell);
    auto state = host.finish(shell);
    assert(state.projectIoTaskStatus == ProjectIoStatus::Succeeded
        && !state.projectEntryScene.empty());
    const auto entry = state.projectEntryScene;
    const auto resources = shell.retainProjectResourceManager();
    const auto* scene = &shell.sceneDocument().scene();
    Host::completed(shell, SetProjectSettingsCommand{"Entry cleared", {}});
    Host::completed(shell, SaveProjectCommand{});
    state = host.finish(shell);
    assert(state.projectSaveReceipt.published && !state.projectDirty
        && state.projectEntryScene.empty());
    Host::completed(shell, SetProjectSettingsCommand{"Ready startup restored", entry});
    Host::completed(shell, SaveProjectCommand{});
    state = host.finish(shell);
    assert(state.projectSaveReceipt.published && !state.projectDirty
        && state.projectEntryScene == entry && shell.retainProjectResourceManager() == resources
        && &shell.sceneDocument().scene() == scene);
    const auto invalid = shell.submitCommand(
        SetProjectSettingsCommand{"Invalid startup", "00000000-0000-4000-8000-000000000099"});
    assert(invalid.status != EditorResponseStatus::Completed && !shell.projectDirty());
    Host::start(shell, "workspace", true);
    state = host.finish(shell);
    assert(state.projectIoTaskStatus == ProjectIoStatus::Succeeded
        && state.projectName == "Ready startup restored" && state.projectEntryScene == entry);
}

void negatives()
{
    Host host;
    EditorShell shell;
    host.initialize(shell);
    auto unknown = OpenProjectCommand{"workspace"};
    unknown.source = "other";
    Host::rejected(shell, unknown, EditorDiagnosticCode::InvalidCommandPayload);
    unknown.source = "workspace";
    unknown.root = "../workspace";
    Host::rejected(shell, unknown, EditorDiagnosticCode::InvalidCommandPayload);
    const auto owner = shell.retainProjectResourceManager();
    Host::start(shell);
    // Deliberate contract violation: even a direct Scene write outside the
    // dispatcher must not be erased by a stale completion candidate.
    shell.sceneDocument().scene().createObject("external-write");
    auto changed = host.finish(shell);
    assert(changed.projectIoTaskStatus == ProjectIoStatus::Failed
        && changed.projectIoDiagnostic == "EDITOR-PROJECT-IO-DOCUMENT-CHANGED"
        && shell.retainProjectResourceManager() == owner);
    Host::start(shell, "workspace", true);
    shell.pollTasks();
    assert(host.mounts.unmount(host.mount));
    assert(host.finish(shell).projectIoTaskStatus == ProjectIoStatus::Failed
        && shell.retainProjectResourceManager() == owner);
    {
        Host bounded;
        EditorShell target;
        bounded.initialize(target);
        auto budget = Host::budget();
        budget.directory.maxEntries = 1;
        assert(target.bindProjectSourceFiles(bounded.files, bounded.mount, "workspace", budget));
        const auto keep = target.retainProjectResourceManager();
        Host::start(target);
        const auto rejected = bounded.finish(target);
        assert(rejected.projectIoTaskStatus == ProjectIoStatus::Failed
            && target.retainProjectResourceManager() == keep && !rejected.hasProject);
    }
    {
        Host bounded;
        EditorShell target;
        bounded.initialize(target);
        const auto keep = target.retainProjectResourceManager();
        const auto version = target.stateVersion();
        OpenProjectCommand open{"workspace"};
        open.source = "workspace";
        auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        {
            core::CpuMemoryQuotaScope limited(quota);
            try {
                const auto result = target.submitCommand(std::move(open));
                assert(result.status != EditorResponseStatus::Completed);
            } catch (const std::bad_alloc&) {
                // The envelope/response can itself exhaust the injected quota.
            }
        }
        assert(!Host::state(target).projectIoTaskId && target.stateVersion() == version
            && target.retainProjectResourceManager() == keep && quota->liveBytes() == 0);
    }
    {
        Host broken(false, true);
        EditorShell target;
        broken.initialize(target);
        const auto keep = target.retainProjectResourceManager();
        Host::start(target);
        assert(broken.finish(target).projectIoTaskStatus == ProjectIoStatus::Failed
            && target.retainProjectResourceManager() == keep);
    }
}

void nativeEncoding()
{
    unified_io_fixture::Workspace workspace;
    const auto path = workspace.root / "encoding.arp";
    const core::pmr::String original = "\xef\xbb\xbf// original\r\n";
    workspace.write("encoding.arp", original);
    ScriptDocumentSession session;
    const auto opened = session.openFile(workspace.root, path);
    assert(opened && session.setActiveIndex(opened.documentIndex));
    assert(session.active()->text == "// original\n" && session.active()->savedFileBytes == original
        && session.active()->newline == "\r\n");
    const auto id = session.activeId();
    assert(
        session.replaceActiveRange(
            0, session.active()->text.size(), "// edited\n", ScriptDocumentSession::maxScriptBytes)
        == ScriptDocumentSession::EditResult::Applied);
    assert(session.saveActiveFile(workspace.root).code
        == ScriptDocumentSession::FileResultCode::Saved);
    const auto encoded = core::readTextFile(path);
    assert(encoded.success && encoded.text == "\xef\xbb\xbf// edited\r\n"
        && !session.active()->dirty());
    workspace.write("encoding.arp", "// external\n");
    assert(session.reloadActiveFile(workspace.root, true).code
        == ScriptDocumentSession::FileResultCode::Opened);
    assert(session.activeId() == id && session.active()->text == "// external\n"
        && session.active()->savedFileBytes == "// external\n");
    // Input failure and over-budget BOM restoration leave old state/disk intact.
    workspace.write("bad.arp", std::string_view("a\0b", 3));
    workspace.write("bad-utf8.arp", "\xff");
    for (const auto* name : {"bad.arp", "bad-utf8.arp"})
        assert(
            !session.openFile(workspace.root, workspace.root / name) && session.activeId() == id);
    workspace.write("encoding.arp", original);
    assert(session.reloadActiveFile(workspace.root, true));
    core::pmr::String full(ScriptDocumentSession::maxScriptBytes - 1, 'x');
    assert(session.replaceActiveRange(
               0, session.active()->text.size(), full, ScriptDocumentSession::maxScriptBytes)
        == ScriptDocumentSession::EditResult::Applied);
    assert(session.saveActiveFile(workspace.root).code
        == ScriptDocumentSession::FileResultCode::TooLarge);
    assert(core::readTextFile(path).text == original);
}

void scriptDocuments()
{
    Host host;
    const auto add = [&](const char* path, std::string_view text) {
        assert(host.memoryFiles->registerFile(
                   path, reinterpret_cast<const std::uint8_t*>(text.data()), text.size())
            == FileIoStatus::Success);
    };
    add("workspace/scripts/bad.arp", "\xff");
    add("workspace/scripts/nul.arp", std::string_view("a\0b", 3));
    add("workspace/scripts/pending.arp", "// pending\r\n");
    core::pmr::String large(fileBytes + 1, 'a');
    add("workspace/scripts/large.arp", large);
    EditorShell shell;
    host.initialize(shell);
    Host::start(shell);
    assert(host.finish(shell).projectIoTaskStatus == ProjectIoStatus::Succeeded);
    const auto owner = shell.retainProjectResourceManager();
    const auto scene = shell.sceneDocument().scene().rootId();
    const auto before = Host::state(shell);
    const auto accepted = Host::completed(shell, OpenScriptCommand{"scripts/gameplay.arp"});
    assert(accepted.hasSnapshot && accepted.snapshot.projectIoTaskStatus == ProjectIoStatus::Pending
        && !Host::state(shell).hasActiveDocument
        && Host::state(shell).workspace == before.workspace);
    auto opened = host.finish(shell);
    const auto raw = core::readBinaryFile(fixtureRoot / "workspace/scripts/gameplay.arp");
    assert(raw.success);
    const std::string_view rawText(
        reinterpret_cast<const char*>(raw.bytes.data()), raw.bytes.size());
    assert(opened.hasActiveDocument && opened.openDocumentCount == 1 && !opened.scriptDirty
        && opened.scriptSourceMounted && !opened.scriptSourceMissing && !opened.scriptSourceConflict
        && opened.scriptText == ScriptDocumentSession::normalizeFileBytes(rawText)
        && shell.retainProjectResourceManager() == owner
        && shell.sceneDocument().scene().rootId() == scene);
    Host::completed(shell, ReplaceScriptTextCommand(opened.activeDocumentId, "// keep dirty\n"));
    const auto reads = host.files.telemetry().submitted;
    Host::completed(shell, OpenScriptCommand{"scripts/gameplay.arp"});
    assert(host.files.telemetry().submitted == reads && Host::state(shell).scriptDirty
        && Host::state(shell).scriptText == "// keep dirty\n"
        && Host::state(shell).activeDocumentId == opened.activeDocumentId);
    Host::completed(shell, UndoScriptCommand{opened.activeDocumentId});
    assert(Host::state(shell).scriptText == opened.scriptText && !Host::state(shell).scriptDirty);
    Host::completed(shell, RedoScriptCommand{opened.activeDocumentId});
    for (const auto* file :
        {"scripts/bad.arp", "scripts/nul.arp", "scripts/large.arp", "scripts/missing.arp"}) {
        Host::completed(shell, OpenScriptCommand{file});
        const auto failed = host.finish(shell);
        assert(failed.projectIoTaskStatus == ProjectIoStatus::Failed && failed.scriptDirty
            && failed.activeDocumentId == opened.activeDocumentId && failed.openDocumentCount == 1
            && failed.scriptText == "// keep dirty\n");
    }
    for (const auto* file :
        {"../gameplay.arp", "/gameplay.arp", "scripts/../gameplay.arp", "scripts/a.txt"})
        Host::rejected(shell, OpenScriptCommand{file}, EditorDiagnosticCode::InvalidCommandPayload);
    EditorInputCommand control;
    control.kind = EditorInputKind::KeyDown;
    control.key = EditorInputKey::LeftControl;
    Host::completed(shell, control);
    const auto cancel = Host::completed(shell, OpenScriptCommand{"scripts/pending.arp"});
    shell.pollTasks();
    control.kind = EditorInputKind::KeyUp;
    Host::completed(shell, control);
    EditorInputCommand textInput;
    textInput.kind = EditorInputKind::Text;
    textInput.text = "x";
    Host::completed(shell, textInput);
    assert(Host::state(shell).scriptText == "// keep dirty\n");
    Host::completed(shell, CancelProjectIoCommand{cancel.snapshot.projectIoTaskId});
    unified_io_fixture::wait(host.queue, [&] {
        return host.mounts.telemetry().inFlight == 0;
    });
    assert(Host::state(shell).openDocumentCount == 1 && Host::state(shell).scriptDirty);
    Host::completed(shell, textInput);
    assert(Host::state(shell).scriptText.size() == std::string_view("// keep dirty\n").size() + 1);
    Host::completed(shell, UndoScriptCommand{opened.activeDocumentId});
    assert(Host::state(shell).scriptText == "// keep dirty\n");
    Host::completed(shell, OpenScriptCommand{"scripts/pending.arp"});
    auto second = host.finish(shell);
    assert(second.projectIoTaskStatus == ProjectIoStatus::Succeeded && second.openDocumentCount == 2
        && second.activeDocumentId != opened.activeDocumentId
        && second.scriptText == "// pending\n");
    Host::completed(shell, CloseScriptCommand{second.activeDocumentId});
    assert(shell.openScriptCount() == 1);

    const auto readsBeforeReload = host.files.telemetry().submitted;
    Host::rejected(shell,
        ReloadScriptCommand{opened.activeDocumentId, false},
        EditorDiagnosticCode::UnsavedChanges);
    assert(host.files.telemetry().submitted == readsBeforeReload);
    const auto reload = Host::completed(shell, ReloadScriptCommand{opened.activeDocumentId, true});
    assert(reload.snapshot.projectIoTaskStatus == ProjectIoStatus::Pending
        && Host::state(shell).scriptText == "// keep dirty\n");
    auto reloaded = host.finish(shell);
    assert(reloaded.projectIoTaskStatus == ProjectIoStatus::Succeeded
        && reloaded.activeDocumentId == opened.activeDocumentId && reloaded.openDocumentCount == 1
        && !reloaded.scriptDirty && reloaded.scriptText == opened.scriptText);
    const auto cleanVersion = shell.stateVersion();
    Host::completed(shell, UndoScriptCommand{opened.activeDocumentId});
    Host::completed(shell, RedoScriptCommand{opened.activeDocumentId});
    assert(
        shell.stateVersion() == cleanVersion && Host::state(shell).scriptText == opened.scriptText);

    Host::completed(shell, ReplaceScriptTextCommand(opened.activeDocumentId, "// cancel reload\n"));
    const auto cancelled =
        Host::completed(shell, ReloadScriptCommand{opened.activeDocumentId, true});
    shell.pollTasks();
    Host::completed(shell, CancelProjectIoCommand{cancelled.snapshot.projectIoTaskId});
    unified_io_fixture::wait(host.queue, [&] {
        return host.mounts.telemetry().inFlight == 0;
    });
    assert(Host::state(shell).scriptText == "// cancel reload\n" && Host::state(shell).scriptDirty);
    Host::completed(shell, UndoScriptCommand{opened.activeDocumentId});
    assert(Host::state(shell).scriptText == opened.scriptText);

    // A bound source tab limit is checked before another read is accepted.
    Host limited;
    EditorShell one;
    limited.initialize(one);
    auto budget = Host::budget();
    budget.maxScriptDocuments = 1;
    assert(one.bindProjectSourceFiles(limited.files, limited.mount, "workspace", budget));
    Host::start(one);
    assert(limited.finish(one).projectIoTaskStatus == ProjectIoStatus::Succeeded);
    Host::completed(one, OpenScriptCommand{"scripts/gameplay.arp"});
    assert(limited.finish(one).projectIoTaskStatus == ProjectIoStatus::Succeeded);
    const auto submitted = limited.files.telemetry().submitted;
    Host::rejected(one,
        OpenScriptCommand{"scripts/other.arp"},
        EditorDiagnosticCode::CommandPreparationFailed);
    assert(limited.files.telemetry().submitted == submitted && one.openScriptCount() == 1);
    Host::completed(one, ReloadScriptCommand{Host::state(one).activeDocumentId, false});
    assert(limited.finish(one).projectIoTaskStatus == ProjectIoStatus::Succeeded
        && one.openScriptCount() == 1);
}

#if !defined(__EMSCRIPTEN__)
void nativeSourceReload()
{
    unified_io_fixture::Workspace copy;
    const auto root = copy.root / "source";
    std::filesystem::copy(fixtureRoot, root, std::filesystem::copy_options::recursive);
    Host host(true, false, root);
    EditorShell shell;
    host.initialize(shell);
    Host::start(shell);
    assert(host.finish(shell).projectIoTaskStatus == ProjectIoStatus::Succeeded);
    Host::completed(shell, OpenScriptCommand{"scripts/gameplay.arp"});
    const auto opened = host.finish(shell);
    const auto id = opened.activeDocumentId;
    Host::completed(shell, ReplaceScriptTextCommand(id, "// local edits\n"));
    const auto path = root / "workspace/scripts/gameplay.arp";
    const auto write = [&](std::string_view contents) {
        copy.write("source/workspace/scripts/gameplay.arp", contents);
    };
    for (const auto bad : {std::string_view("\xff"), std::string_view("x\0y", 3)}) {
        write(bad);
        Host::completed(shell, ReloadScriptCommand{id, true});
        const auto failed = host.finish(shell);
        assert(failed.projectIoTaskStatus == ProjectIoStatus::Failed && failed.scriptDirty
            && failed.activeDocumentId == id && failed.scriptText == "// local edits\n");
    }
    std::filesystem::remove(path);
    Host::completed(shell, ReloadScriptCommand{id, true});
    assert(host.finish(shell).projectIoTaskStatus == ProjectIoStatus::Failed
        && Host::state(shell).scriptText == "// local edits\n");
    Host::completed(shell, UndoScriptCommand{id});
    assert(Host::state(shell).scriptText == opened.scriptText);
    Host::completed(shell, RedoScriptCommand{id});
    write("\xef\xbb\xbf// external source\r\n");
    Host::completed(shell, ReloadScriptCommand{id, true});
    const auto reloaded = host.finish(shell);
    assert(reloaded.projectIoTaskStatus == ProjectIoStatus::Succeeded
        && reloaded.activeDocumentId == id && !reloaded.scriptDirty
        && reloaded.scriptText == "// external source\n");
    Host::completed(shell, ReloadScriptCommand{id, false});
    shell.pollTasks();
    assert(host.mounts.unmount(host.mount));
    assert(host.finish(shell).projectIoTaskStatus == ProjectIoStatus::Failed
        && Host::state(shell).scriptText == reloaded.scriptText);
}
#endif

// Transport outcome fixture. Read/query bytes come from the real registered
// memory backend; this fixture never claims to have committed a remote write.
class UnknownSaveBackend final : public ManagedFileBackend {
public:
    explicit UnknownSaveBackend(core::SharedPtr<ManagedFileBackend> read) : read_(std::move(read))
    {}

    FileBackendCapability capability() const noexcept override { return read_->capability(); }

    FileReadResult validateRead(const FileReadRequest& request) const override
    {
        return read_->validateRead(request);
    }

    FileIoStatus readAsync(const FileReadRequest& request,
        FileReadCompletion completion,
        core::runtime::CompletionQueue& queue) noexcept override
    {
        return read_->readAsync(request, completion, queue);
    }

    FileIoStatus queryAsync(const FileQueryRequest& request,
        FileQueryCompletion completion,
        core::runtime::CompletionQueue& queue) noexcept override
    {
        return read_->queryAsync(request, completion, queue);
    }

    FileSaveCapability saveCapability(core::PoolHandle) const noexcept override
    {
        return {true, false, false};
    }

    FileIoStatus saveAsync(FileSaveRequest,
        FileSaveCompletion completion,
        core::runtime::CompletionQueue& queue) noexcept override
    {
        struct Pending {
            FileSaveCompletion completion;

            static void deliver(void* context) noexcept
            {
                core::UniquePtr<Pending> pending{static_cast<Pending*>(context),
                    core::CpuAllocatedDeleter<Pending>{
                        context, &core::destroyCpuAllocatedUnique<Pending>}};
                FileSaveResult result;
                result.fail(FileIoStatus::OutcomeUnknown, "TEST-SAVE-RESPONSE-LOST");
                result.commitState = FileCommitState::OutcomeUnknown;
                pending->completion.complete(pending->completion.userData, result);
            }
        };

        try {
            auto reserved = queue.reserve();
            if (!reserved.valid())
                return FileIoStatus::QueueFull;
            auto pending = core::makeUnique<Pending>();
            pending->completion = completion;
            if (!queue.postReserved(std::move(reserved), {Pending::deliver, pending.get()})
                    .accepted())
                return FileIoStatus::ServiceClosed;
            pending.release();
            ++saveCalls;
            return FileIoStatus::Accepted;
        } catch (...) {
            return FileIoStatus::BudgetExceeded;
        }
    }

    void shutdown() noexcept override { read_->shutdown(); }

    unsigned saveCalls = 0;

private:
    core::SharedPtr<ManagedFileBackend> read_;
};

void unknownSave()
{
    Host host;
    assert(host.mounts.unmount(host.mount));
    auto backend = core::makeShared<UnknownSaveBackend>(host.memoryFiles);
    host.mount = host.mounts.mount("workspace", backend);
    EditorShell shell;
    host.initialize(shell);
    Host::start(shell);
    assert(host.finish(shell).projectIoTaskStatus == ProjectIoStatus::Succeeded);
    Host::completed(shell, OpenScriptCommand{"scripts/gameplay.arp"});
    const auto id = host.finish(shell).activeDocumentId;
    Host::completed(shell, ReplaceScriptTextCommand{id, "// pending unknown outcome\n"});
    const auto accepted = Host::completed(shell, SaveScriptCommand{id});
    assert(accepted.snapshot.projectIoTaskStatus == ProjectIoStatus::Pending);
    assert(backend->saveCalls == 0 && !accepted.snapshot.scriptSaveReceipt.submitted);
    const auto unknown = host.finish(shell);
    assert(unknown.projectIoTaskStatus == ProjectIoStatus::Failed && unknown.scriptDirty);
    assert(unknown.scriptSaveReceipt.terminal && !unknown.scriptSaveReceipt.published
        && unknown.scriptSaveReceipt.outcome.commitState == FileCommitState::OutcomeUnknown);
    Host::rejected(shell, SaveScriptCommand{id}, EditorDiagnosticCode::WorkspaceBusy);
    assert(backend->saveCalls == 1);
    NavigateProjectSourceCommand refresh;
    refresh.intent = ProjectBrowserModel::SourceNavigation::Refresh;
    Host::completed(shell, refresh);
    const auto afterRead = host.finish(shell);
    assert(afterRead.scriptSaveReceipt.taskId == unknown.scriptSaveReceipt.taskId
        && afterRead.scriptSaveReceipt.outcome.commitState == FileCommitState::OutcomeUnknown);
    Host::rejected(shell, SaveScriptCommand{id}, EditorDiagnosticCode::WorkspaceBusy);
    Host::completed(shell, ReloadScriptCommand{id, true});
    const auto reloaded = host.finish(shell);
    assert(!reloaded.scriptDirty && reloaded.scriptSaveReceipt.baselineRefreshed);
    assert(reloaded.scriptSaveReceipt.outcome.commitState == FileCommitState::OutcomeUnknown);
    Host::completed(shell, SaveScriptCommand{id});
    (void)host.finish(shell);
    assert(backend->saveCalls == 2);
}

void memorySourceSaveCycle()
{
    Host host(false, false, fixtureRoot, true);
    EditorShell shell;
    host.initialize(shell);
    Host::start(shell);
    assert(host.finish(shell).projectIoTaskStatus == ProjectIoStatus::Succeeded);
    Host::completed(shell, OpenScriptCommand{"scripts/gameplay.arp"});
    const auto id = host.finish(shell).activeDocumentId;
    const auto read = [&] {
        FileReadRequest request;
        request.contractVersion = 1;
        request.pathScope = FilePathScope::MountedResource;
        request.path = "workspace/scripts/gameplay.arp";
        request.mount = host.mount;
        request.maxBytes = fileBytes;
        unified_io_fixture::Probe<FileReadResult> result;
        assert(host.files.read(request, {decltype(result)::complete, &result})
            == FileIoStatus::Accepted);
        host.queue.dispatch(8);
        assert(result.calls == 1 && result.result.status == FileIoStatus::Success);
        return result.result;
    };
    const auto original = read();
    Host::completed(shell, ReplaceScriptTextCommand{id, "// memory first\n"});
    const auto submitted = host.files.telemetry().submitted;
    const auto pending = Host::completed(shell, SaveScriptCommand{id});
    assert(!pending.snapshot.scriptSaveReceipt.submitted
        && host.files.telemetry().submitted == submitted);
    assert(read().bytes == original.bytes);
    Host::completed(shell, ReplaceScriptTextCommand{id, "// memory newer\n"});
    Host::completed(shell, UndoScriptCommand{id});
    Host::completed(shell, RedoScriptCommand{id});
    auto state = host.finish(shell);
    assert(state.projectIoTaskStatus == ProjectIoStatus::Succeeded && state.scriptDirty
        && state.scriptSaveReceipt.published);
    const auto saved = read();
    assert(std::string_view(reinterpret_cast<const char*>(saved.bytes.data()), saved.bytes.size())
        == "\xef\xbb\xbf// memory first\r\n");
    Host::completed(shell, SaveScriptCommand{id});
    state = host.finish(shell);
    assert(state.projectIoTaskStatus == ProjectIoStatus::Succeeded && !state.scriptDirty);
    assert(state.scriptSaveReceipt.outcome.backendKind == FileBackendKind::MemoryFileBackend);

    // Simulate a separate authorized writer through the same production IO
    // contract, rather than bypassing the backend or fabricating a receipt.
    FileSaveRequest external;
    external.mount = host.mount;
    external.pathScope = FilePathScope::MountedResource;
    external.path = "workspace/scripts/gameplay.arp";
    const std::string_view edited = "// external memory\n";
    external.bytes.assign(edited.begin(), edited.end());
    external.precondition = FileSavePrecondition::ContentSha256;
    external.expectedVersion = state.scriptSaveReceipt.outcome.committedVersion;
    external.maxBaselineBytes = fileBytes;
    external.maxWriteBytes = fileBytes;
    unified_io_fixture::Probe<FileSaveResult> receipt;
    assert(host.files.save(std::move(external), {decltype(receipt)::complete, &receipt})
        == FileIoStatus::Accepted);
    host.queue.dispatch(8);
    assert(receipt.result.commitState == FileCommitState::Committed);
    Host::completed(shell, ReplaceScriptTextCommand{id, "// keep local\n"});
    Host::completed(shell, SaveScriptCommand{id});
    state = host.finish(shell);
    assert(state.projectIoTaskStatus == ProjectIoStatus::Failed && state.scriptDirty
        && state.scriptSaveReceipt.outcome.status == FileIoStatus::VersionMismatch);
    Host::completed(shell, ReloadScriptCommand{id, true});
    state = host.finish(shell);
    assert(!state.scriptDirty && state.scriptText == edited);

    Host::completed(shell, ReplaceScriptTextCommand{id, "// before cancellation\n"});
    auto task = Host::completed(shell, SaveScriptCommand{id});
    Host::completed(shell, CancelProjectIoCommand{task.snapshot.projectIoTaskId});
    state = host.finish(shell);
    assert(state.projectIoTaskStatus == ProjectIoStatus::Cancelled
        && !state.scriptSaveReceipt.submitted
        && state.scriptSaveReceipt.outcome.commitState == FileCommitState::NotCommitted);
    Host::completed(shell, SaveScriptCommand{id});
    shell.pollTasks();
    assert(host.queue.pending() == 1);
    Host::completed(shell, CancelProjectIoCommand{Host::state(shell).projectIoTaskId});
    state = host.finish(shell);
    assert(state.projectIoTaskStatus == ProjectIoStatus::Cancelled && state.scriptDirty
        && state.scriptSaveReceipt.outcome.commitState == FileCommitState::Committed
        && state.scriptSaveReceipt.outcome.deliveryStatus == FileIoStatus::Cancelled);
    Host::completed(shell, ReloadScriptCommand{id, true});
    assert(!host.finish(shell).scriptDirty);
    Host::completed(shell, ReplaceScriptTextCommand{id, "// closed before submit\n"});
    Host::completed(shell, SaveScriptCommand{id});
    Host::completed(shell, CloseScriptCommand{id, true});
    state = host.finish(shell);
    assert(state.projectIoTaskStatus == ProjectIoStatus::Cancelled && !state.hasActiveDocument
        && !state.scriptSaveReceipt.submitted);
    Host::completed(shell, OpenScriptCommand{"scripts/gameplay.arp"});
    state = host.finish(shell);
    assert(state.activeDocumentId != id && state.scriptText == "// before cancellation\n");
    assert(host.memoryFiles->telemetry().inFlight == 0
        && host.memoryFiles->telemetry().queuedBytes == 0);
}

#if defined(_WIN32)
void sourceSaveCycle()
{
    Host host(true, false, fixtureRoot, true);
    const auto diskPath = host.workspace.root / "workspace/scripts/gameplay.arp";
    const auto original = core::readBinaryFile(diskPath, fileBytes);
    assert(original.success);
    {
        EditorShell shell;
        host.initialize(shell);
        Host::start(shell);
        assert(host.finish(shell).projectIoTaskStatus == ProjectIoStatus::Succeeded);
        Host::completed(shell, OpenScriptCommand{"scripts/gameplay.arp"});
        const auto opened = host.finish(shell);
        const auto id = opened.activeDocumentId;
        Host::completed(shell, ReplaceScriptTextCommand{id, "// save first\n"});
        const auto accepted = Host::completed(shell, SaveScriptCommand{id});
        assert(accepted.snapshot.projectIoTaskStatus == ProjectIoStatus::Pending);
        assert(accepted.snapshot.scriptSaveReceipt.documentId == id
            && !accepted.snapshot.scriptSaveReceipt.submitted);
        assert(core::readBinaryFile(diskPath, fileBytes).bytes == original.bytes);
        Host::completed(shell, ReplaceScriptTextCommand{id, "// newer local edit\n"});
        Host::completed(shell, UndoScriptCommand{id});
        Host::completed(shell, RedoScriptCommand{id});
        Host::rejected(shell, SaveScriptCommand{id}, EditorDiagnosticCode::WorkspaceBusy);
        auto saved = host.finish(shell);
        assert(saved.projectIoTaskStatus == ProjectIoStatus::Succeeded && saved.scriptDirty);
        assert(saved.scriptText == "// newer local edit\n" && saved.scriptSaveReceipt.published);
        assert(saved.scriptSaveReceipt.outcome.commitState == FileCommitState::Committed);
        const auto encoded = std::string_view("\xef\xbb\xbf"
                                              "// save first\r\n");
        const auto written = core::readBinaryFile(diskPath, fileBytes);
        assert(written.success
            && std::string_view(
                   reinterpret_cast<const char*>(written.bytes.data()), written.bytes.size())
                == encoded);
        Host::completed(shell, SaveScriptCommand{id});
        saved = host.finish(shell);
        assert(saved.projectIoTaskStatus == ProjectIoStatus::Succeeded && !saved.scriptDirty);
        const auto savedTask = saved.scriptSaveReceipt.taskId;
        NavigateProjectSourceCommand refresh;
        refresh.intent = ProjectBrowserModel::SourceNavigation::Refresh;
        Host::completed(shell, refresh);
        assert(host.finish(shell).scriptSaveReceipt.taskId == savedTask);

        Host::completed(shell, ReplaceScriptTextCommand{id, "// conflicting\n"});
        Host::completed(shell, SaveScriptCommand{id});
        host.workspace.write("workspace/scripts/gameplay.arp", "// external\r\n");
        saved = host.finish(shell);
        assert(saved.projectIoTaskStatus == ProjectIoStatus::Failed && saved.scriptDirty);
        assert(saved.scriptSaveReceipt.outcome.status == FileIoStatus::VersionMismatch
            && saved.scriptSaveReceipt.outcome.commitState == FileCommitState::NotCommitted);
        Host::completed(shell, ReloadScriptCommand{id, true});
        assert(!host.finish(shell).scriptDirty);

        Host::completed(shell, ReplaceScriptTextCommand{id, "// cancelled after commit\n"});
        auto task = Host::completed(shell, SaveScriptCommand{id});
        shell.pollTasks();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        while (!host.queue.pending() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        assert(host.queue.pending() == 1);
        Host::completed(shell, CancelProjectIoCommand{task.snapshot.projectIoTaskId});
        assert(Host::state(shell).projectIoTaskStatus == ProjectIoStatus::Pending);
        saved = host.finish(shell);
        assert(saved.projectIoTaskStatus == ProjectIoStatus::Cancelled && saved.scriptDirty);
        assert(saved.scriptSaveReceipt.outcome.commitState == FileCommitState::Committed
            && !saved.scriptSaveReceipt.published && saved.scriptSaveReceipt.cancellationRequested);
        assert(core::readTextFile(diskPath).text == "// cancelled after commit\r\n");

        task = Host::completed(shell, SaveScriptCommand{id});
        Host::completed(shell, CloseScriptCommand{id, true});
        saved = host.finish(shell);
        assert(saved.projectIoTaskStatus == ProjectIoStatus::Cancelled && !saved.hasActiveDocument);
        assert(!saved.scriptSaveReceipt.submitted
            && saved.scriptSaveReceipt.outcome.commitState == FileCommitState::NotCommitted);
        assert(core::readTextFile(diskPath).text == "// cancelled after commit\r\n");
        Host::completed(shell, OpenScriptCommand{"scripts/gameplay.arp"});
        const auto reopened = host.finish(shell);
        assert(reopened.activeDocumentId != id);
        const auto before = core::readBinaryFile(diskPath, fileBytes);
        auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        {
            core::CpuMemoryQuotaScope failure(quota);
            const auto refused = shell.submitCommand(SaveScriptCommand{reopened.activeDocumentId});
            assert(refused.status != EditorResponseStatus::Completed);
        }
        assert(core::readBinaryFile(diskPath, fileBytes).bytes == before.bytes);
        assert(Host::state(shell).projectIoTaskStatus != ProjectIoStatus::Pending
            && quota->liveBytes() == 0);
        Host::completed(
            shell, ReplaceScriptTextCommand{reopened.activeDocumentId, "// abandoned owner\n"});
        Host::completed(shell, SaveScriptCommand{reopened.activeDocumentId});
        shell.pollTasks(); // Destruction abandons an accepted save's publication.
    }
    host.mounts.shutdown();
    while (host.queue.pending())
        host.queue.dispatch(8);
    assert(host.mounts.telemetry().inFlight == 0 && host.mounts.telemetry().reservedReadBytes == 0);
}
#endif

void scriptInputCycle(core::SharedPtr<ManagedFileBackend> backend)
{
    core::runtime::CompletionQueue queue(core::runtime::CompletionQueueDesc{8});
    MountedFileBackend mounts({1, 1, 2 * fileBytes, 128, 512});
    const auto mount = mounts.mount("workspace", std::move(backend));
    assert(mount);
    FileService files(queue, mounts);
    ProjectSourceSession scope(files, {mount, "workspace", 17}, {1, fileBytes, fileBytes});
    assert(scope.read("project.arp.yaml", fileBytes) == FileIoStatus::Accepted);
    unified_io_fixture::wait(queue, [&] {
        return scope.state() != ProjectSourceState::Pending;
    });
    const auto snapshot = scope.finish(17);
    assert(snapshot);
    EditorProjectIoTask task(
        files, EditorProjectIoTask::ScriptInput{snapshot, "scripts/gameplay.arp"}, Host::budget());
    unified_io_fixture::wait(queue, [&] {
        task.advance();
        return task.ready() || task.status() != ProjectIoStatus::Pending;
    });
    assert(task.ready());
    const auto& document = task.script();
    const auto raw = core::readBinaryFile(fixtureRoot / "workspace/scripts/gameplay.arp");
    assert(raw.success);
    assert(document.sourceSnapshot && document.sourceSnapshot->size() == 1);
    const auto* acquired = document.sourceSnapshot->entryAt(0);
    assert(acquired && acquired->receipt.bytes == raw.bytes);
    const std::string_view text(reinterpret_cast<const char*>(raw.bytes.data()), raw.bytes.size());
    assert(std::string_view(document.savedFileBytes.data(), document.savedFileBytes.size()) == text
        && document.newline == "\r\n"
        && document.text == ScriptDocumentSession::normalizeFileBytes(text) && !document.dirty());
    ScriptDocumentSession native;
    const auto nativeFile =
        native.openFile(fixtureRoot / "workspace", fixtureRoot / "workspace/scripts/gameplay.arp");
    assert(nativeFile && native.setActiveIndex(nativeFile.documentIndex));
    assert(native.active()->savedFileBytes == document.savedFileBytes
        && native.active()->text == document.text && native.active()->newline == document.newline);
    ScriptDocumentSession session;
    const auto index = session.append(std::move(task.script()));
    assert(index != ScriptDocumentSession::noActiveDocument && session.setActiveIndex(index));
    task.published();
    assert(session.active()->sourceSnapshot);
    assert(session.saveActiveFile(fixtureRoot).code
        == ScriptDocumentSession::FileResultCode::InvalidPath);
    assert(session.reloadActiveFile(fixtureRoot, true).code
        == ScriptDocumentSession::FileResultCode::InvalidPath);
    const auto documentId = session.activeId();
    assert(session.replaceActiveRange(0, 0, "// edited\n", ScriptDocumentSession::maxScriptBytes)
        == ScriptDocumentSession::EditResult::Applied);
    const char* issue = nullptr;
    assert(!session.prepareReload(documentId, false, {}, issue)
        && std::string_view(issue) == "EDITOR-SCRIPT-UNSAVED");
    auto budget = ScriptDocumentSession::ReloadBudget{};
    budget.maxHistoryBytes = 1;
    assert(!session.prepareReload(documentId, true, budget, issue)
        && std::string_view(issue) == "EDITOR-SCRIPT-RELOAD-BUDGET");
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope allocation(quota);
        assert(!session.prepareReload(documentId, true, {}, issue));
    }
    assert(quota->liveBytes() == 0 && session.active()->dirty());
    auto reload = session.prepareReload(documentId, true, {}, issue);
    assert(reload);
    EditorProjectIoTask reloadTask(files,
        EditorProjectIoTask::ScriptInput{snapshot, "scripts/gameplay.arp", std::move(reload)},
        Host::budget());
    unified_io_fixture::wait(queue, [&] {
        reloadTask.advance();
        return reloadTask.ready() || reloadTask.status() != ProjectIoStatus::Pending;
    });
    assert(reloadTask.ready() && reloadTask.scriptReload());
    auto& replacement = reloadTask.script();
    const auto edited = session.active()->text;
    // Direct edits bypass the epoch, but cannot bypass exact target validation.
    session.active()->text[0] = 'x';
    assert(!session.commitSourceReload(*reloadTask.scriptReload(), replacement)
        && replacement.text == native.active()->text);
    session.active()->text = edited;
    session.active()->undo.back().offset += 1;
    assert(!session.commitSourceReload(*reloadTask.scriptReload(), replacement));
    session.active()->undo.back().offset -= 1;
    {
        core::CpuMemoryQuotaScope allocation(quota);
        assert(session.commitSourceReload(*reloadTask.scriptReload(), replacement));
    }
    assert(session.activeId() == documentId && session.active()->text == native.active()->text
        && !session.active()->dirty() && session.active()->undo.empty()
        && session.active()->redo.empty() && replacement.text == edited && quota->liveBytes() == 0);
    assert(!session.commitSourceReload(*reloadTask.scriptReload(), replacement));
    reloadTask.published();
    assert(!reloadTask.scriptReload());
    auto stale = session.prepareReload(documentId, false, {}, issue);
    assert(stale);
    core::UniquePtr<ScriptDocumentSession::Document> staleCandidate;
    assert(ScriptDocumentSession::prepareSourceFile(session.active()->sourceSnapshot,
        "scripts/gameplay.arp",
        files,
        17,
        staleCandidate,
        issue));
    session.setActiveSelection(1, 1);
    session.setActiveSelection(0, 0);
    assert(!session.commitSourceReload(*stale, *staleCandidate)
        && session.active()->text == native.active()->text && staleCandidate->id == 0);
    auto kept = core::makeUnique<ScriptDocumentSession::Document>();
    kept->text = "unchanged";
    {
        core::CpuMemoryQuotaScope allocation(quota);
        assert(!ScriptDocumentSession::prepareSourceFile(
            session.active()->sourceSnapshot, "scripts/gameplay.arp", files, 17, kept, issue));
    }
    assert(kept->text == "unchanged" && quota->liveBytes() == 0);
    assert(mounts.unmount(mount));
    assert(!ScriptDocumentSession::prepareSourceFile(
               session.active()->sourceSnapshot, "scripts/gameplay.arp", files, 17, kept, issue)
        && kept->text == "unchanged");
}

void remote()
{
    Host host;
    EditorShell shell;
    host.initialize(shell);
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
    for (const auto* body :
        {R"({"schemaVersion":1,"command":"project.open","source":"","path":"workspace"})",
            R"({"schemaVersion":1,"command":"project.open","source":"workspace","source":"other","path":"workspace"})",
            R"({"schemaVersion":1,"command":"project.directory.refresh","path":"workspace"})",
            R"({"schemaVersion":1,"command":"project.io.cancel","taskId":0})",
            R"({"schemaVersion":1,"command":"project.open","source":"workspace","path":"workspace","mount":42})",
            R"({"schemaVersion":1,"command":"script.open","path":"scripts/gameplay.arp","source":"workspace"})",
            R"({"schemaVersion":1,"command":"script.open","path":"scripts/gameplay.arp","path":"scripts/other.arp"})",
            R"({"schemaVersion":1,"command":"script.open","path":"../gameplay.arp"})"}) {
        const auto version = shell.stateVersion();
        assert(send(body).status().value() != 200 && shell.stateVersion() == version);
    }
    const auto* open =
        R"({"schemaVersion":1,"command":"project.open","source":"workspace","path":"workspace","idempotencyKey":"source-open","correlationId":52})";
    const auto accepted = send(open);
    assert(accepted.status().value() == 200
        && accepted.body_text().find("\"status\":\"pending\"") != std::string_view::npos);
    const auto version = shell.stateVersion();
    assert(send(open).body_text() == accepted.body_text() && shell.stateVersion() == version);
    assert(
        send(
            R"({"schemaVersion":1,"command":"project.open","source":"other","path":"workspace","idempotencyKey":"source-open"})")
            .status()
            .value()
        != 200);
    assert(host.finish(shell).projectIoTaskStatus == ProjectIoStatus::Succeeded);
    const auto nav = send(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"project.directory.open","arguments":{"path":"scripts"}}})",
        true);
    assert(nav.status().value() == 200
        && nav.body_text().find("\"isError\":false") != std::string_view::npos);
    assert(host.finish(shell).projectDirectory == "workspace/scripts");
    const auto* openScript =
        R"({"schemaVersion":1,"command":"script.open","path":"scripts/gameplay.arp","idempotencyKey":"script-source-open","correlationId":57})";
    const auto scriptAccepted = send(openScript);
    assert(scriptAccepted.status().value() == 200);
    const auto scriptVersion = shell.stateVersion();
    assert(send(openScript).body_text() == scriptAccepted.body_text()
        && shell.stateVersion() == scriptVersion);
    assert(
        send(
            R"({"schemaVersion":1,"command":"script.open","path":"scripts/other.arp","idempotencyKey":"script-source-open"})")
            .status()
            .value()
        == 409);
    assert(host.finish(shell).hasActiveDocument);
    const auto mcpScript = send(
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"script.open","arguments":{"path":"scripts/gameplay.arp"}}})",
        true);
    assert(mcpScript.body_text().find("\"isError\":false") != std::string_view::npos
        && shell.openScriptCount() == 1);
    const auto query = send(R"({"schemaVersion":1,"command":"editor.state.get"})");
    assert(query.body_text().find("\"projectSource\":\"workspace\"") != std::string_view::npos
        && query.body_text().find("\"projectDirectory\":\"workspace/scripts\"")
            != std::string_view::npos);
}
} // namespace

int main(int argc, char** argv)
{
    const bool saveContracts = argc == 2 && std::string_view(argv[1]) == "--source-save-contracts";
    const bool saveSmoke = argc == 2 && std::string_view(argv[1]) == "--source-save-smoke";
    if (saveContracts || saveSmoke) {
        const auto run = [] {
            unknownSave();
            memorySourceSaveCycle();
#if defined(_WIN32)
            sourceSaveCycle();
#endif
        };
        run();
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < (saveSmoke ? 20u : 1u); ++i)
            run();
        const auto after = core::defaultCpuAllocator().stats();
        std::printf("Shell source save %u cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
            saveSmoke ? 20u : 1u,
            (unsigned long long)before.liveBytes,
            (unsigned long long)after.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        assert(before.liveBytes == after.liveBytes);
        assert(before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
        return 0;
    }
    const bool scriptHttp = argc == 3 && std::string_view(argv[1]) == "--script-http";
    const bool scriptSmoke = argc == 2 && std::string_view(argv[1]) == "--script-input-smoke";
    if (scriptHttp || scriptSmoke) {
        const auto run = [&] {
            core::SharedPtr<ManagedFileBackend> backend;
            if (scriptHttp) {
                HttpFileBackendDesc desc;
                desc.baseUrl = argv[2];
                desc.maxInFlight = 1;
                desc.maxBytesInFlight = 2 * fileBytes;
                desc.maxPathBytes = 512;
                desc.maxHeaderBytes = 8192;
                desc.ioChunkBytes = 4096;
                desc.timeoutMs = 3000;
                desc.cancellationPollMs = 2;
                backend = core::makeShared<HttpFileBackend>(desc);
            } else
                backend = memory();
            scriptInputCycle(std::move(backend));
        };
        for (unsigned i = 0; i < 20; ++i)
            run();
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            run();
        const auto after = core::defaultCpuAllocator().stats();
        std::printf("Script source input 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)after.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        return 0;
    }
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    const bool native = argc == 2 && std::string_view(argv[1]) == "--native";
    assert(argc == 1 || smoke || native);
    if (!smoke && !native) {
        lifecycle(false);
        projectEntrySourceSave();
        negatives();
        scriptDocuments();
        nativeEncoding();
#if !defined(__EMSCRIPTEN__)
        nativeSourceReload();
#endif
        scriptInputCycle(memory());
        unknownSave();
#if defined(_WIN32)
        sourceSaveCycle();
#endif
        remote();
    } else {
        for (unsigned i = 0; i < 20; ++i)
            lifecycle(native);
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            lifecycle(native);
        const auto after = core::defaultCpuAllocator().stats();
        std::printf("Shell source IO 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)after.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
    }
    std::puts(
        "PASS Shell typed source publication, directory history, cancellation and owner lifetime");
}
