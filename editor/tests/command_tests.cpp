#include "editor/command/editor_command.h"
#include "editor/ingress/platform_input_adapter.h"
#include "editor/remote/editor_remote_protocol.h"
#include "editor/workspace/editor_shell.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {
class TestExecutor final : public arp::editor::IEditorCommandExecutor {
public:
    arp::editor::EditorCommandOutcome execute(const arp::editor::EditorCommandPayload&) override
    {
        ++calls;
        return nextOutcome;
    }

    unsigned calls = 0;
    arp::editor::EditorCommandOutcome nextOutcome{};
};

arp::editor::EditorCommandEnvelope command(std::uint64_t correlationId = 0)
{
    arp::editor::EditorCommandEnvelope envelope{arp::editor::ActivateControlCommand("node_test")};
    envelope.correlationId = correlationId;
    return envelope;
}
} // namespace

int main()
{
    using namespace arp::editor;
    EditorCommandDispatcher dispatcher;
    TestExecutor executor;

    auto firstCommand = command(41);
    auto first = dispatcher.dispatch(firstCommand, executor);
    assert(first.status == EditorResponseStatus::Completed);
    assert(first.commandId != 0 && first.correlationId == 41);
    assert(first.hasStateVersion && first.stateVersion == 1);

    auto staleCommand = command();
    staleCommand.hasExpectedStateVersion = true;
    staleCommand.expectedStateVersion = 0;
    const auto stale = dispatcher.dispatch(staleCommand, executor);
    assert(stale.status == EditorResponseStatus::Rejected);
    assert(stale.diagnostic == EditorDiagnosticCode::StaleStateVersion);
    assert(executor.calls == 1 && dispatcher.stateVersion() == 1);

    auto unsupportedCommand = command();
    unsupportedCommand.schemaVersion = EditorCommandEnvelope::currentVersion + 1;
    const auto unsupported = dispatcher.dispatch(unsupportedCommand, executor);
    assert(unsupported.status == EditorResponseStatus::Rejected);
    assert(unsupported.diagnostic == EditorDiagnosticCode::UnsupportedCommandVersion);
    assert(executor.calls == 1);

    auto retryCommand = command();
    retryCommand.idempotencyKey = "retry-key";
    const auto retry = dispatcher.dispatch(retryCommand, executor);
    assert(retry.status == EditorResponseStatus::Rejected);
    assert(retry.diagnostic == EditorDiagnosticCode::UnsupportedIdempotency);
    assert(executor.calls == 1);

    retryCommand.idempotencyFingerprint = "activate:node_test";
    const auto firstRetry = dispatcher.dispatch(retryCommand, executor);
    assert(firstRetry.status == EditorResponseStatus::Completed);
    assert(firstRetry.stateVersion == 2 && executor.calls == 2);
    retryCommand.correlationId = 42;
    const auto duplicateRetry = dispatcher.dispatch(retryCommand, executor);
    assert(duplicateRetry.commandId == firstRetry.commandId);
    assert(duplicateRetry.correlationId == 42);
    assert(duplicateRetry.stateVersion == 2 && executor.calls == 2);
    retryCommand.idempotencyFingerprint = "activate:node_other";
    const auto conflictingRetry = dispatcher.dispatch(retryCommand, executor);
    assert(conflictingRetry.status == EditorResponseStatus::Rejected);
    assert(conflictingRetry.diagnostic == EditorDiagnosticCode::IdempotencyKeyConflict);
    assert(executor.calls == 2);

    EditorCommandDispatcher boundedDispatcher(1);
    TestExecutor boundedExecutor;
    auto boundedFirst = command();
    boundedFirst.idempotencyKey = "first";
    boundedFirst.idempotencyFingerprint = "payload-first";
    assert(boundedDispatcher.dispatch(boundedFirst, boundedExecutor).status
        == EditorResponseStatus::Completed);
    auto boundedSecond = command();
    boundedSecond.idempotencyKey = "second";
    boundedSecond.idempotencyFingerprint = "payload-second";
    assert(boundedDispatcher.dispatch(boundedSecond, boundedExecutor).status
        == EditorResponseStatus::Completed);
    boundedFirst.correlationId = 99;
    assert(boundedDispatcher.dispatch(boundedFirst, boundedExecutor).status
        == EditorResponseStatus::Completed);
    assert(boundedExecutor.calls == 3);

    executor.nextOutcome = {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    const auto failed = dispatcher.dispatch(command(), executor);
    assert(failed.status == EditorResponseStatus::Failed);
    assert(failed.stateVersion == 2 && dispatcher.stateVersion() == 2);

    EditorResponse wrongThreadResponse;
    std::thread wrongThread([&] {
        wrongThreadResponse = dispatcher.dispatch(command(), executor);
    });
    wrongThread.join();
    assert(wrongThreadResponse.status == EditorResponseStatus::Rejected);
    assert(wrongThreadResponse.diagnostic == EditorDiagnosticCode::WrongOwnerThread);
    assert(wrongThreadResponse.commandId != 0);
    assert(!wrongThreadResponse.hasStateVersion);
    assert(executor.calls == 3 && dispatcher.stateVersion() == 2);

    arp::platform::WindowInputEvent platformEvent;
    platformEvent.kind = arp::platform::WindowInputEvent::Kind::KeyDown;
    platformEvent.key = arp::platform::WindowKey::LeftControl;
    platformEvent.windowId = 99;
    const auto normalizedKey = normalizePlatformInput(platformEvent);
    assert(normalizedKey.kind == EditorInputKind::KeyDown);
    assert(normalizedKey.key == EditorInputKey::LeftControl);
    for (const auto [platformKey, editorKey] : {
             std::pair{arp::platform::WindowKey::Q, EditorInputKey::Q},
             std::pair{arp::platform::WindowKey::W, EditorInputKey::W},
             std::pair{arp::platform::WindowKey::E, EditorInputKey::E},
             std::pair{arp::platform::WindowKey::R, EditorInputKey::R}}) {
        platformEvent.key = platformKey;
        const auto normalizedToolKey = normalizePlatformInput(platformEvent);
        assert(normalizedToolKey.kind == EditorInputKind::KeyDown
            && normalizedToolKey.key == editorKey);
    }

    platformEvent.kind = arp::platform::WindowInputEvent::Kind::TextInput;
    platformEvent.text = "utf8 text";
    const auto normalizedText = normalizePlatformInput(platformEvent);
    assert(normalizedText.kind == EditorInputKind::Text);
    assert(normalizedText.text == "utf8 text");

    platformEvent.kind = arp::platform::WindowInputEvent::Kind::FocusLost;
    const auto normalizedBarrier = normalizePlatformInput(platformEvent);
    assert(normalizedBarrier.kind == EditorInputKind::FocusLost);

    const auto tempRoot = std::filesystem::temp_directory_path()
        / ("arp_editor_command_history_"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tempRoot);
    const auto scriptPath = tempRoot / "history.arp";
    const auto secondScriptPath = tempRoot / "second.arp";
    {
        std::ofstream script(scriptPath, std::ios::binary);
        script << "let value = 1;\n";
    }
    {
        std::ofstream script(secondScriptPath, std::ios::binary);
        script << "let second = 2;\n";
    }
    EditorShell shell;
    const auto loaded = shell.load(
        std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", tempRoot);
    assert(loaded.ok());
    const auto versionBeforeQuery = shell.stateVersion();
    const auto snapshot = shell.submitCommand(GetEditorStateQuery{}, EditorCommandOrigin::Mcp);
    assert(snapshot.status == EditorResponseStatus::Completed && snapshot.hasSnapshot);
    assert(
        snapshot.stateVersion == versionBeforeQuery && shell.stateVersion() == versionBeforeQuery);
    assert(snapshot.snapshot.workspace == shell.workspace());
    assert(!snapshot.snapshot.hasActiveDocument && snapshot.snapshot.openDocumentCount == 0);
    assert(shell.openScriptFile(scriptPath));

    constexpr std::string_view token = "test-token-0123456789-0123456789-0123456789";
    EditorRemoteProtocol protocol(token);
    arp::network::http::Request unauthorized;
    unauthorized.set_method(arp::network::http::Method::Get);
    unauthorized.set_target("/api/v1/discovery");
    assert(protocol.handle(unauthorized, shell).status().value() == 401);

    arp::network::http::Request discovery;
    discovery.set_method(arp::network::http::Method::Get);
    discovery.set_target("/api/v1/discovery");
    discovery.add_header("Authorization", "Bearer " + std::string(token));
    const auto discoveryResponse = protocol.handle(discovery, shell);
    assert(discoveryResponse.status().value() == 200);
    assert(discoveryResponse.body_text().find("editor.state.get") != std::string_view::npos);

    arp::network::http::Request replaceRequest;
    replaceRequest.set_method(arp::network::http::Method::Post);
    replaceRequest.set_target("/api/v1/commands");
    replaceRequest.add_header("Authorization", "Bearer " + std::string(token));
    replaceRequest.add_header("Content-Type", "application/json");
    replaceRequest.set_body_text(
        "{\"schemaVersion\":1,\"command\":\"script.replace\",\"documentId\":"
        + std::to_string(shell.activeScriptDocumentId())
        + ",\"text\":\"remote text\\n\",\"idempotencyKey\":\"replace-once\"}");
    const auto replaceResponse = protocol.handle(replaceRequest, shell);
    assert(replaceResponse.status().value() == 200);
    const auto replacedVersion = shell.stateVersion();
    assert(shell.scriptText() == "remote text\n");
    const auto replayResponse = protocol.handle(replaceRequest, shell);
    assert(replayResponse.status().value() == 200);
    assert(shell.stateVersion() == replacedVersion && shell.scriptText() == "remote text\n");

    arp::platform::WindowInputEvent textEvent;
    textEvent.kind = arp::platform::WindowInputEvent::Kind::TextInput;
    textEvent.text = "# edit";
    shell.handlePlatformInput(textEvent);
    const auto documentId = shell.activeScriptDocumentId();
    const auto editedText = shell.scriptText();
    assert(documentId != 0 && shell.scriptDirty());

    const auto undoResponse = shell.submitCommand(UndoScriptCommand{documentId});
    assert(undoResponse.status == EditorResponseStatus::Completed);
    assert(shell.scriptText().find("# edit") == arp::core::pmr::String::npos);
    const auto redoResponse = shell.submitCommand(RedoScriptCommand{documentId});
    assert(redoResponse.status == EditorResponseStatus::Completed);
    assert(shell.scriptText() == editedText);

    const auto staleHistory = shell.submitCommand(UndoScriptCommand{documentId + 1});
    assert(staleHistory.status == EditorResponseStatus::Failed);
    assert(staleHistory.diagnostic == EditorDiagnosticCode::StaleDocumentTarget);
    assert(shell.scriptText() == editedText);

    assert(shell.openScriptFile(secondScriptPath));
    textEvent.text = "# second";
    shell.handlePlatformInput(textEvent);
    const auto secondDocumentId = shell.activeScriptDocumentId();
    const auto secondEditedText = shell.scriptText();
    assert(secondDocumentId != documentId);
    const auto inactiveUndo = shell.submitCommand(UndoScriptCommand{documentId});
    assert(inactiveUndo.status == EditorResponseStatus::Failed);
    assert(inactiveUndo.diagnostic == EditorDiagnosticCode::StaleDocumentTarget);

    assert(shell.openScriptFile(scriptPath));
    const auto isolatedUndo = shell.submitCommand(UndoScriptCommand{documentId});
    assert(isolatedUndo.status == EditorResponseStatus::Completed);
    assert(shell.scriptText().find("# edit") == arp::core::pmr::String::npos);
    assert(shell.openScriptFile(secondScriptPath));
    assert(shell.scriptText() == secondEditedText);

    std::filesystem::remove(scriptPath);
    std::filesystem::remove(secondScriptPath);
    std::filesystem::remove(tempRoot);
}
