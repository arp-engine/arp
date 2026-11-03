#include "editor/app/editor_project_source_host.h"
#include "editor/remote/editor_file_receipt_wire.h"
#include "editor/remote/editor_remote_protocol.h"
#include "editor/tests/fixtures/source_scene_shell_fixture.h"
#include "editor/tests/fixtures/workspace_wire_host.h"
#include "tests/fixtures/workspace_backend_fixture.h"

namespace {
using namespace arp;
using namespace editor;
using namespace editor::testfixtures;
using namespace core::io;
using Wire = WorkspaceFileWire;
using Y = core::YamlContract;

void verify(bool value, int line)
{
    if (!value)
        std::fprintf(stderr, "Receipt command check failed at line %d\n", line);
    editor::testfixtures::check(value);
}

#define check(value) verify(value, __LINE__)

network::http::Response send(
    EditorRemoteProtocol& protocol, EditorShell& shell, std::string_view body, bool mcp = false)
{
    network::http::Request request;
    request.set_method(network::http::Method::Post);
    request.set_target(mcp ? "/mcp" : "/api/v1/commands");
    request.add_header("Authorization", "Bearer receipt-command-token");
    request.add_header("Accept", "application/json");
    request.add_header("MCP-Protocol-Version", "2025-11-25");
    request.set_content_type(network::http::ContentType::Json);
    request.set_body_text(body);
    return protocol.handle(request, shell);
}

core::pmr::String body(const FileSaveReceipt& ticket, const char* operation, const char* extra = "")
{
    return core::pmr::String(R"({"schemaVersion":1,"command":"source.receipt.)") + operation
        + R"(","source":"workspace","receipt":)"
        + EditorFileReceiptWire::encodeIdentity(ticket, EditorRemoteProtocol::maxRequestBytes)
        + extra + "}";
}
} // namespace

void runReceiptCommandCycle()
{
    editor::tests::WorkspaceWireHost wire;
    workspace_backend_fixture::Client peer(workspace_backend_fixture::descriptor(wire.origin()));
    peer.connect([&] {
        wire.pump();
    });
    unified_io_fixture::Probe<FileSaveResult> saved;
    check(peer.files.save(peer.save("folder/lost.arp", "# receipt command real commit\n"),
              {decltype(saved)::complete, &saved})
        == FileIoStatus::Accepted);
    workspace_backend_fixture::wait(
        peer.queue,
        [&] {
            return saved.calls == 1;
        },
        [&] {
            wire.pump();
        });
    check(saved.result.commitState == FileCommitState::OutcomeUnknown
        && saved.result.receipt.valid());
    EditorProjectSourceHost host;
    EditorShell shell;
    check(shell
            .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                wire.host.workspace.root)
            .ok());
    EditorProjectIoBudget budget;
    budget.inputs = {16, 65536, 262144};
    budget.directory = {16, 4, 1024, 8192};
    check(host.connectRemote(workspace_backend_fixture::descriptor(wire.origin()), budget)
        == FileIoStatus::Accepted);
    const auto pump = [&] {
        wire.pump();
        host.dispatch();
        shell.pollTasks();
    };
    workspace_backend_fixture::wait(
        peer.queue,
        [&] {
            return host.connectionStatus() != EditorProjectSourceHost::ConnectionStatus::Connecting;
        },
        pump);
    check(host.bindRemote(shell));
    const auto before = shell.submitCommand(GetEditorStateQuery{}).snapshot;
    EditorRemoteProtocol protocol("receipt-command-token");
    const auto ticket = saved.result.receipt;
    for (const auto& bad : {body(ticket, "query", R"(,"receipt":{})"),
             body(ticket, "query", R"(,"path":"local.txt")"),
             body(ticket, "query", R"(,"mount":1)")}) {
        const auto version = shell.stateVersion();
        check(
            send(protocol, shell, bad).status().value() == 400 && shell.stateVersion() == version);
    }
    auto zero = ticket;
    zero.scope = 0;
    check(send(protocol, shell, body(zero, "query")).status().value() == 400);
    for (unsigned i = 0; i < 6; ++i) {
        core::json::JsonDocument malformed;
        const auto original = body(ticket, "query");
        malformed.Parse(original.data(), original.size());
        auto& value = malformed["receipt"];
        switch (i) {
        case 0:
            value["contractVersion"].SetInt(2);
            break;
        case 1:
            value["scope"].SetString("1");
            break;
        case 2:
            value["operationId"].SetInt(0);
            break;
        case 3:
            value["sessionVersion"]["digest"].SetString("!!!!");
            break;
        case 4:
            value.AddMember("unknown", true, malformed.GetAllocator());
            break;
        case 5:
            value.AddMember("scope", 1, malformed.GetAllocator());
            break;
        }
        const auto version = shell.stateVersion();
        check(send(protocol, shell, Wire::encode(malformed, EditorRemoteProtocol::maxRequestBytes))
                    .status()
                    .value()
                == 400
            && shell.stateVersion() == version);
    }
    const auto query =
        body(ticket, "query", R"(,"idempotencyKey":"original-query","correlationId":55)");
    const auto accepted = send(protocol, shell, query);
    check(accepted.status().value() == 200);
    const auto acceptedNode = Y::parseSingleDocument(accepted.body_text());
    const auto id = Y::uint64(Y::required(acceptedNode["snapshot"]["sourceReceiptTask"], "taskId"));
    check(id && Y::text(acceptedNode["snapshot"]["sourceReceiptTask"], "status") == "pending"
        && !Y::boolean(acceptedNode["snapshot"]["sourceReceiptTask"], "submitted"));
    check(send(protocol, shell, query).body_text() == accepted.body_text());
    check(shell.submitCommand(CreateSceneObjectCommand("blocked")).diagnostic
        == EditorDiagnosticCode::WorkspaceBusy);
    workspace_backend_fixture::wait(
        peer.queue,
        [&] {
            return shell.submitCommand(GetEditorStateQuery{}).snapshot.sourceReceiptTaskStatus
                != ProjectIoStatus::Pending;
        },
        pump);
    auto observed = shell.submitCommand(GetEditorStateQuery{}).snapshot;
    check(observed.sourceReceiptTaskId == id
        && observed.sourceReceiptTaskStatus == ProjectIoStatus::Succeeded
        && observed.sourceReceiptResult.state == FileSaveReceiptState::Terminal
        && observed.sourceReceiptResult.outcome.commitState == FileCommitState::Committed
        && observed.sourceReceiptIdentity == ticket && observed.sceneDirty == before.sceneDirty
        && observed.sceneRuntimeId == before.sceneRuntimeId
        && observed.sceneSaveReceipt.taskId == before.sceneSaveReceipt.taskId);
    const auto identity =
        EditorFileReceiptWire::encodeIdentity(ticket, EditorRemoteProtocol::maxRequestBytes);
    const auto release =
        core::pmr::String(
            R"({"jsonrpc":"2.0","id":42,"method":"tools/call","params":{"name":"source.receipt.release","arguments":{"source":"workspace","receipt":)")
        + identity + "}}}";
    const auto mcp = send(protocol, shell, release, true);
    check(mcp.status().value() == 200
        && !Y::boolean(Y::parseSingleDocument(mcp.body_text())["result"], "isError"));
    workspace_backend_fixture::wait(
        peer.queue,
        [&] {
            return shell.submitCommand(GetEditorStateQuery{}).snapshot.sourceReceiptTaskStatus
                != ProjectIoStatus::Pending;
        },
        pump);
    observed = shell.submitCommand(GetEditorStateQuery{}).snapshot;
    check(observed.sourceReceiptTaskStatus == ProjectIoStatus::Succeeded
        && observed.sourceReceiptResult.state == FileSaveReceiptState::Released
        && observed.sourceReceiptResult.outcome.commitState == FileCommitState::OutcomeUnknown);
    const auto afterRelease = send(protocol, shell, body(ticket, "query"));
    check(afterRelease.status().value() == 200);
    const auto pending = shell.submitCommand(GetEditorStateQuery{}).snapshot;
    const auto cancelled =
        shell.submitCommand(CancelSourceReceiptCommand{pending.sourceReceiptTaskId});
    check(cancelled.status == EditorResponseStatus::Completed);
    observed = shell.submitCommand(GetEditorStateQuery{}).snapshot;
    check(observed.sourceReceiptTaskStatus == ProjectIoStatus::Cancelled
        && !observed.sourceReceiptSubmitted
        && observed.sourceReceiptResult.status == FileIoStatus::Cancelled);
    // A stale identity is rejected by the actual backend. No source save is replayed.
    check(send(protocol, shell, body(ticket, "query")).status().value() == 200);
    workspace_backend_fixture::wait(
        peer.queue,
        [&] {
            return shell.submitCommand(GetEditorStateQuery{}).snapshot.sourceReceiptTaskStatus
                != ProjectIoStatus::Pending;
        },
        pump);
    observed = shell.submitCommand(GetEditorStateQuery{}).snapshot;
    check(observed.sourceReceiptTaskStatus == ProjectIoStatus::Failed
        && observed.sourceReceiptResult.state == FileSaveReceiptState::Unavailable
        && observed.sourceReceiptResult.outcome.commitState == FileCommitState::OutcomeUnknown
        && wire.dropped == 1);
}
