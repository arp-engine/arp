#include "editor/app/editor_project_source_host.h"
#include "editor/remote/editor_workspace_server.h"
#include "editor/tests/fixtures/source_scene_shell_fixture.h"
#include "editor/tests/fixtures/workspace_wire_host.h"
#include "tests/fixtures/workspace_backend_fixture.h"

#include <chrono>
#include <thread>

namespace {
using namespace arp;
using namespace editor;
using namespace editor::testfixtures;
using namespace editor::testfixtures::sources;
using S = EditorProjectSourceHost::ConnectionStatus;

template <class Predicate>
void wait(EditorWorkspaceServer& server, EditorProjectSourceHost& host, Predicate&& predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        server.pump();
        host.dispatch();
        std::this_thread::yield();
    }
    check(predicate());
}

EditorProjectIoBudget budget()
{
    EditorProjectIoBudget result;
    result.inputs = {64, fileBytes, 4 * fileBytes};
    result.directory = {32, 4, 4096, 8192};
    result.maxDecodedBytes = 4 * fileBytes;
    return result;
}

core::io::WorkspaceFileBackendDesc descriptor(EditorWorkspaceServer& server)
{
    auto d = workspace_backend_fixture::descriptor(
        "http://127.0.0.1:" + core::formatUnsigned(server.port()));
    d.maxFileBytes = fileBytes;
    d.maxRequestBytes = d.maxResponseBytes = 2 * fileBytes;
    d.preparationBytes = 32 * fileBytes;
    d.maxDirectoryEntries = 32;
    d.maxDirectoryNameBytes = 4096;
    d.timeoutMs = 5000;
    return d;
}

EditorStateSnapshot finish(
    EditorWorkspaceServer& server, EditorProjectSourceHost& host, EditorShell& shell)
{
    wait(server, host, [&] {
        shell.pollTasks();
        return Host::state(shell).projectIoTaskStatus != ProjectIoStatus::Pending;
    });
    const auto result = Host::state(shell);
    if (result.projectIoTaskStatus != ProjectIoStatus::Succeeded)
        std::fprintf(stderr,
            "Project task failed: %s; script save=%u, project save=%u\n",
            result.projectIoDiagnostic.c_str(),
            unsigned(result.scriptSaveReceipt.outcome.status),
            unsigned(result.projectSaveReceipt.outcome.status));
    check(result.projectIoTaskStatus == ProjectIoStatus::Succeeded, result.projectIoDiagnostic);
    return result;
}

EditorStateSnapshot sceneFinish(EditorWorkspaceServer& server,
    EditorProjectSourceHost& host,
    EditorShell& shell,
    std::uint64_t id)
{
    wait(server, host, [&] {
        shell.pollTasks();
        return Host::command(shell, GetScenePersistenceTaskQuery{id})
                   .snapshot.scenePersistenceTaskStatus
            != ScenePersistenceTaskStatus::Pending;
    });
    const auto result = Host::command(shell, GetScenePersistenceTaskQuery{id}).snapshot;
    check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded,
        result.scenePersistenceTaskDiagnostic);
    return result;
}

void nativeReopen(FixtureSource& source, bool scriptChanged)
{
    EditorShell native;
    check(native
            .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                source.fixture.files.root / "workspace")
            .ok());
    check(native.projectManifest() && native.projectManifest()->name == "Remote IO project");
    const auto loaded = Host::command(native, LoadSceneCommand("remote.yaml")).snapshot;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (Host::state(native).sceneIoPending && std::chrono::steady_clock::now() < deadline) {
        native.pollTasks();
        std::this_thread::yield();
    }
    const auto result =
        Host::command(native, GetScenePersistenceTaskQuery{loaded.scenePersistenceTaskId}).snapshot;
    if (scriptChanged) {
        // A v4 archive binds exact module bytes. Source IO does not authorize
        // silently replacing that identity; explicit module reload remains open.
        check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
            && result.scenePersistenceTaskDiagnostic
                == "EDITOR-SCRIPT-ARCHIVE-SOURCE-OR-IMPORT-CONFLICT");
        check(native.openScriptFile(source.fixture.files.root / "workspace/primary.arp"));
        check(native.scriptText().find("# saved through workspace FileService")
            != core::pmr::String::npos);
    } else {
        check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded,
            result.scenePersistenceTaskDiagnostic);
        check(native.sceneDocument().scene().name() == "Remote IO scene");
    }
}

void cycle(bool failures)
{
    FixtureSource source;
    EditorWorkspaceServer server;
    EditorWorkspaceProtocolDesc protocol;
    protocol.workspaceId = "fixture";
    protocol.sessionId = "editor-host-session";
    protocol.readToken = workspace_backend_fixture::readToken;
    protocol.writeToken = workspace_backend_fixture::writeToken;
    protocol.maxFileBytes = fileBytes;
    protocol.maxRequestBytes = protocol.maxResponseBytes = 2 * fileBytes;
    protocol.preparationBytes = 32 * fileBytes;
    protocol.maxQueryBytes = 8192;
    protocol.maxDirectoryEntries = 32;
    protocol.maxDirectoryNameBytes = 4096;
    // Three ordinary saves must reuse the bounded journal instead of filling it.
    protocol.maxReceipts = 1;
    check(server.start(core::FilePath::utf8(source.fixture.files.root), protocol));
    if (failures) {
        EditorProjectSourceHost failed;
        auto config = descriptor(server);
        auto wrongBudget = budget();
        wrongBudget.inputs.maxFileBytes = 2 * fileBytes;
        check(failed.connectRemote(config, wrongBudget) == core::io::FileIoStatus::InvalidRequest);
        check(failed.connectionStatus() == S::Unconfigured);
        config.token = "wrong-012345678901234567890123456789";
        check(failed.connectRemote(config, budget()) == core::io::FileIoStatus::Accepted);
        wait(server, failed, [&] {
            return failed.connectionStatus() != S::Connecting;
        });
        check(failed.connectionStatus() == S::Failed && failed.diagnostic());
        check(failed.connectRemote(descriptor(server), budget())
            == core::io::FileIoStatus::InvalidRequest);
    }
    {
        EditorProjectSourceHost host;
        EditorShell shell;
        check(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                    source.workspace.root)
                .ok());
        check(host.connectRemote(descriptor(server), budget()) == core::io::FileIoStatus::Accepted);
        check(host.connectionStatus() == S::Connecting && !host.bindRemote(shell));
        wait(server, host, [&] {
            return host.connectionStatus() != S::Connecting;
        });
        check(host.connectionStatus() == S::Ready && !host.diagnostic());
        check(!host.bindRemote(shell, "../invalid") && host.connectionStatus() == S::Ready);
        check(host.bindRemote(shell) && host.connectionStatus() == S::Bound && !host.diagnostic());
        OpenProjectCommand open{"workspace"};
        open.source = "workspace";
        Host::command(shell, open);
        auto state = finish(server, host, shell);
        check(state.projectIoDiagnostic.empty() && !shell.filesPresentationFailed()
            && std::string_view(shell.inspectorFailure()).empty());
        check(state.hasProject && state.projectSource == "workspace"
            && state.projectRoot == "workspace");
        const auto loaded = Host::command(shell, LoadSceneCommand("scene.yaml")).snapshot;
        sceneFinish(server, host, shell, loaded.scenePersistenceTaskId);
        SetSceneObjectNameCommand rename("Remote IO scene");
        rename.objectId = shell.sceneDocument().scene().rootId().value();
        Host::command(shell, rename);
        const auto saved = Host::command(shell, SaveSceneCommand("remote.yaml")).snapshot;
        const auto committed = sceneFinish(server, host, shell, saved.scenePersistenceTaskId);
        check(committed.sceneSaveReceipt.outcome.commitState == core::io::FileCommitState::Committed
            && !shell.sceneDirty() && committed.sceneSaveReceipt.maintenance.releaseAttempted
            && committed.sceneSaveReceipt.maintenance.result.state
                == core::io::FileSaveReceiptState::Released);
        const auto reopened = Host::command(shell, LoadSceneCommand("remote.yaml")).snapshot;
        sceneFinish(server, host, shell, reopened.scenePersistenceTaskId);
        check(shell.sceneDocument().scene().name() == "Remote IO scene");
        Host::command(shell,
            SetProjectSettingsCommand{"Remote IO project", shell.projectManifest()->entryScene});
        Host::command(shell, SaveProjectCommand{});
        state = finish(server, host, shell);
        check(state.projectSaveReceipt.outcome.commitState == core::io::FileCommitState::Committed
            && state.projectSaveReceipt.published && !state.projectDirty
            && state.projectSaveReceipt.maintenance.releaseAttempted
            && state.projectSaveReceipt.maintenance.result.state
                == core::io::FileSaveReceiptState::Released);
        nativeReopen(source, false);
        OpenScriptCommand script;
        script.relativePath = "primary.arp";
        Host::command(shell, script);
        state = finish(server, host, shell);
        check(state.scriptSourceMounted && state.hasActiveDocument);
        const auto text = state.scriptText + "\n# saved through workspace FileService\n";
        Host::command(shell, ReplaceScriptTextCommand(state.activeDocumentId, text));
        Host::command(shell, SaveScriptCommand{state.activeDocumentId});
        state = finish(server, host, shell);
        check(state.scriptSaveReceipt.outcome.commitState == core::io::FileCommitState::Committed
            && !state.scriptDirty && state.scriptSaveReceipt.maintenance.releaseAttempted
            && state.scriptSaveReceipt.maintenance.result.state
                == core::io::FileSaveReceiptState::Released);
        Host::command(shell, ReloadScriptCommand{state.activeDocumentId, false});
        state = finish(server, host, shell);
        check(state.scriptText == text && !state.scriptDirty);
        // Destruction cancels a real accepted source read. Host outlives Shell.
        Host::command(shell, ReloadScriptCommand{state.activeDocumentId, false});
        check(Host::state(shell).projectIoTaskStatus == ProjectIoStatus::Pending);
        shell.pollTasks();
    }
    // Reopen actual persisted bytes through the ordinary native Editor path.
    nativeReopen(source, true);
    if (failures) {
        // No Shell or callback target may be required after a connecting host dies.
        EditorProjectSourceHost pending;
        check(pending.connectRemote(descriptor(server), budget())
            == core::io::FileIoStatus::Accepted);
    }
    server.stop();
    check(server.stopped());
}

void receiptCycle()
{
    using namespace core::io;
    {
        editor::tests::WorkspaceWireHost badWire;
        badWire.corruptLostReceipt = true;
        workspace_backend_fixture::Client badClient(
            workspace_backend_fixture::descriptor(badWire.origin()));
        badClient.connect([&] {
            badWire.pump();
        });
        ProjectFileSaveTask unresolved(badClient.files,
            badClient.save("folder/lost.arp", "# committed with corrupt receipt response\n"));
        workspace_backend_fixture::wait(
            badClient.queue,
            [&] {
                return unresolved.terminal();
            },
            [&] {
                badWire.pump();
                unresolved.advance();
            });
        check(unresolved.result().commitState == FileCommitState::OutcomeUnknown
            && unresolved.result().receipt.valid() && unresolved.receiptObservation().queryAttempted
            && !unresolved.receiptObservation().releaseAttempted
            && unresolved.receiptObservation().result.status == FileIoStatus::IntegrityMismatch
            && badWire.dropped == 1 && badWire.corrupted == 1);
        const auto actual =
            core::readBinaryFile(badWire.host.workspace.root / "folder/lost.arp", 65536);
        check(actual.success
            && std::string_view(
                   reinterpret_cast<const char*>(actual.bytes.data()), actual.bytes.size())
                == "# committed with corrupt receipt response\n");
    }
    editor::tests::WorkspaceWireHost wire;
    workspace_backend_fixture::Client client(workspace_backend_fixture::descriptor(wire.origin()));
    client.connect([&] {
        wire.pump();
    });
    ProjectFileSaveTask lost(client.files, client.save("folder/lost.arp", "# actual committed\n"));
    workspace_backend_fixture::wait(
        client.queue,
        [&] {
            return lost.terminal();
        },
        [&] {
            wire.pump();
            lost.advance();
        });
    check(lost.result().commitState == FileCommitState::Committed && wire.dropped == 1);
    const auto& observed = lost.receiptObservation();
    check(observed.queryAttempted && observed.releaseAttempted
        && observed.result.state == FileSaveReceiptState::Released);
    unified_io_fixture::Probe<FileSaveReceiptResult> retired;
    check(client.files.receipt(
              client.receipt(lost.result().receipt), {decltype(retired)::complete, &retired})
        == FileIoStatus::Accepted);
    workspace_backend_fixture::wait(
        client.queue,
        [&] {
            return retired.calls == 1;
        },
        [&] {
            wire.pump();
        });
    check(retired.result.state == FileSaveReceiptState::Unavailable
        && retired.result.status == FileIoStatus::StaleLocation);
    auto late = core::makeUnique<ProjectFileSaveTask>(
        client.files, client.save("folder/late.arp", "# owner retires after release accepted\n"));
    late->advance();
    workspace_backend_fixture::wait(
        client.queue,
        [&] {
            return late->result().commitState == FileCommitState::Committed;
        },
        [&] {
            wire.pump();
        });
    const auto lateTicket = late->result().receipt;
    late->advance();
    check(late->receiptObservation().releaseAttempted && !late->terminal());
    late.reset();
    workspace_backend_fixture::wait(
        client.queue,
        [&] {
            return client.backend->telemetry().inFlight == 0;
        },
        [&] {
            wire.pump();
        });
    unified_io_fixture::Probe<FileSaveReceiptResult> lateRetired;
    check(client.files.receipt(
              client.receipt(lateTicket), {decltype(lateRetired)::complete, &lateRetired})
        == FileIoStatus::Accepted);
    workspace_backend_fixture::wait(
        client.queue,
        [&] {
            return lateRetired.calls == 1;
        },
        [&] {
            wire.pump();
        });
    check(lateRetired.result.state == FileSaveReceiptState::Unavailable);
    ProjectFileSaveTask known(client.files, client.save("folder/known.arp", "# known committed\n"));
    known.advance();
    workspace_backend_fixture::wait(
        client.queue,
        [&] {
            return known.result().commitState == FileCommitState::Committed;
        },
        [&] {
            wire.pump();
        });
    check(!known.terminal());
    // Failure while preparing Release must retain the successful disk outcome.
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope denyAllocation(quota);
        known.advance();
    }
    check(known.terminal() && known.result().commitState == FileCommitState::Committed
        && known.receiptObservation().releaseAttempted
        && known.receiptObservation().result.status == FileIoStatus::BudgetExceeded
        && quota->liveBytes() == 0);
    ProjectFileSaveTask stale(client.files, client.save("folder/stale.arp", "# stale delivery\n"));
    stale.advance();
    workspace_backend_fixture::wait(
        client.queue,
        [&] {
            return stale.result().commitState == FileCommitState::Committed;
        },
        [&] {
            wire.pump();
        });
    check(client.mounts.unmount(client.mount));
    stale.advance();
    check(stale.terminal() && stale.result().commitState == FileCommitState::Committed
        && stale.receiptObservation().result.status == FileIoStatus::StaleLocation);
}
} // namespace

void runReceiptCommandCycle();

int main(int argc, char** argv)
{
    if (argc == 3
        && (std::string_view(argv[1]) == "--verify-web-project"
            || std::string_view(argv[1]) == "--verify-web-module-project")) {
        // Reopen exact browser-written files through the native Editor loader.
        // This is document/IO evidence, not a native GPU smoke.
        const auto root = std::filesystem::u8path(argv[2]);
        EditorShell shell;
        check(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", root)
                .ok());
        check(shell.projectManifest() && shell.projectManifest()->name == "Connected Web project");
        const auto accepted =
            Host::command(shell, LoadSceneCommand("connected.scene.yaml")).snapshot;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (Host::state(shell).sceneIoPending && std::chrono::steady_clock::now() < deadline) {
            shell.pollTasks();
            std::this_thread::yield();
        }
        const auto loaded =
            Host::command(shell, GetScenePersistenceTaskQuery{accepted.scenePersistenceTaskId})
                .snapshot;
        check(loaded.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded,
            loaded.scenePersistenceTaskDiagnostic);
        const auto* object =
            shell.sceneDocument().scene().findObject(scene::GameObjectId::fromValue(2));
        check(object && object->name() == "Web source object");
        if (std::string_view(argv[1]) == "--verify-web-module-project") {
            const scene::Component* greeting = nullptr;
            for (const auto& component : object->components())
                if (std::string_view(component->scriptTypeName()) == "HelloWorld")
                    greeting = component.get();
            check(greeting != nullptr, "Native reader did not find HelloWorld component");
            check(greeting->scriptInstance()
                      .ptr<reflection::instance>()
                      ->slot("message")
                      .cast<core::pmr::String>()
                == "Hello World!");
        }
        check(shell.openScriptFile(root / "notes.arp"));
        check(shell.scriptText().find("# saved through product workspace")
            != core::pmr::String::npos);
        std::puts("PASS native Editor reopened actual Web-written project, v4 scene and script");
        return 0;
    }
    const auto loops = argc > 1 && std::string_view(argv[1]) == "--smoke" ? 20u : 1u;
    cycle(true);
    receiptCycle();
    runReceiptCommandCycle();
    const auto before = arp::core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < loops; ++i) {
        cycle(false);
        receiptCycle();
        runReceiptCommandCycle();
    }
    const auto after = arp::core::defaultCpuAllocator().stats();
    std::printf("Editor actual remote host %u cycles: core bytes %llu -> %llu, live %llu -> %llu\n",
        loops,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
    check(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
