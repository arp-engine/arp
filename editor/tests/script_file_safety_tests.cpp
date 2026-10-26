#include "core/io/atomic_file_service.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "editor/remote/editor_remote_protocol.h"
#include "editor/workspace/editor_shell.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using namespace arp;
using namespace arp::editor;
using Session = ScriptDocumentSession;
using Code = Session::FileResultCode;

class Fixture final {
public:
    Fixture()
    {
        root = std::filesystem::path(ARP_SOURCE_DIR) / "build"
            / ("script-file-safety-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        path = root / "script.arp";
    }

    ~Fixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        assert(!error);
    }

    void write(std::string_view text) const
    {
        core::pmr::Vector<std::uint8_t> bytes(text.begin(), text.end());
        assert(core::io::replaceFileAtomically(core::FilePath::utf8(path), bytes).success);
    }

    core::pmr::String read() const { return core::readTextFile(path).text; }

    void open(Session& session) const
    {
        const auto result = session.openFile(root, path);
        assert(result.code == Code::Opened && session.setActiveIndex(result.documentIndex));
    }

    void shell(EditorShell& shell) const
    {
        assert(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", root)
                .ok());
        assert(shell.openScriptFile(path));
    }

    std::filesystem::path root, path;
};

void contract(Fixture& fixture)
{
    fixture.write("# alpha\r\n");
    Session session;
    fixture.open(session);
    const auto id = session.activeId();
    assert(session.replaceActiveRange(
               0, session.active()->text.size(), "# local\n", Session::maxScriptBytes)
        == Session::EditResult::Applied);
    const auto timestamp = std::filesystem::last_write_time(fixture.path);
    fixture.write("# bravo\r\n");
    std::filesystem::last_write_time(fixture.path, timestamp);
    assert(session.saveActiveFile(fixture.root).code == Code::ExternalConflict);
    assert(fixture.read() == "# bravo\r\n" && session.active()->text == "# local\n");
    assert(session.activeId() == id && session.activeCursor() == 8
        && session.active()->undo.size() == 1);
    assert(session.undoActive() && !session.active()->dirty());
    assert(session.saveActiveFile(fixture.root).code == Code::ExternalConflict);
    assert(session.redoActive() && session.active()->dirty());
    assert(session.reloadActiveFile(fixture.root, false).code == Code::UnsavedChanges);
    assert(session.reloadActiveFile(fixture.root, true).code == Code::Opened);
    assert(session.activeId() == id && session.active()->text == "# bravo\n"
        && !session.active()->dirty());
    assert(session.active()->undo.empty() && session.active()->redo.empty()
        && session.activeCursor() == 0);
    assert(session.replaceActiveRange(
               0, session.active()->text.size(), "# saved\n", Session::maxScriptBytes)
        == Session::EditResult::Applied);
#ifdef _WIN32
    // Deny rename/delete while permitting the preflight read: replacement fails
    // after staging, and must neither truncate the original nor mark it clean.
    const auto locked = CreateFileW(fixture.path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    assert(locked != INVALID_HANDLE_VALUE);
    assert(session.saveActiveFile(fixture.root).code == Code::WriteFailed);
    assert(fixture.read() == "# bravo\r\n" && session.active()->dirty());
    assert(CloseHandle(locked));
#endif
    assert(session.saveActiveFile(fixture.root).code == Code::Saved);
    assert(fixture.read() == "# saved\r\n" && !session.active()->dirty());
    for (const auto& entry : std::filesystem::directory_iterator(fixture.root))
        assert(entry.path().filename().string().find(".arp-tmp-") == std::string::npos);
    assert(session.undoActive() && session.active()->dirty());
    assert(session.redoActive() && !session.active()->dirty());
    // A newline-only external edit conflicts, even with equal normalized text.
    fixture.write("# saved\n");
    assert(session.saveActiveFile(fixture.root).code == Code::ExternalConflict);
    std::filesystem::remove(fixture.path);
    assert(session.saveActiveFile(fixture.root).code == Code::ExternalConflict);
    assert(session.reloadActiveFile(fixture.root, true).code == Code::InvalidPath);
    assert(session.active()->text == "# saved\n" && session.activeId() == id);
    fixture.write(core::pmr::String(Session::maxScriptBytes + 1, 'x'));
    assert(session.reloadActiveFile(fixture.root, true).code == Code::TooLarge);
    assert(session.active()->text == "# saved\n");

    fixture.write("# mixed\r\n# second\n");
    Session mixed;
    fixture.open(mixed);
    assert(mixed.saveActiveFile(fixture.root).code == Code::Saved);
    assert(fixture.read() == "# mixed\r\n# second\n");
    core::pmr::String full(Session::maxScriptBytes, '\n');
    assert(mixed.replaceActiveRange(0, mixed.active()->text.size(), full, Session::maxScriptBytes)
        == Session::EditResult::Applied);
    assert(mixed.saveActiveFile(fixture.root).code == Code::TooLarge);
    assert(fixture.read() == "# mixed\r\n# second\n" && mixed.active()->dirty());
    std::puts(
        "PASS exact disk conflict, same size/mtime, newline/deletion, failed atomic replacement, encoded byte budget and explicit reload");
}

void shellContract(Fixture& fixture)
{
    fixture.write("# original\n");
    EditorShell shell;
    fixture.shell(shell);
    const auto id = shell.activeScriptDocumentId();
    assert(shell.submitCommand(ReplaceScriptTextCommand(id, "# local\n")).status
        == EditorResponseStatus::Completed);
    fixture.write("# external\r\n");
    const auto version = shell.stateVersion();
    const auto feedbackVersion = shell.feedbackVersion();
    assert(shell.submitCommand(SaveScriptCommand{id}).diagnostic
        == EditorDiagnosticCode::ScriptExternalConflict);
    assert(shell.stateVersion() == version && shell.scriptDirty());
    assert(shell.feedbackVersion() > feedbackVersion);
    const auto repeatedFeedbackVersion = shell.feedbackVersion();
    assert(shell.submitCommand(SaveScriptCommand{id}).diagnostic
        == EditorDiagnosticCode::ScriptExternalConflict);
    assert(shell.stateVersion() == version && shell.feedbackVersion() > repeatedFeedbackVersion);
    assert(shell.submitCommand(ReloadScriptCommand{id}).diagnostic
        == EditorDiagnosticCode::UnsavedChanges);
    assert(shell.submitCommand(ReloadScriptCommand{id + 1, true}).diagnostic
        == EditorDiagnosticCode::StaleDocumentTarget);
    shell.activate("script_ui_reload");
    assert(shell.runtime().canvas().findWidget(shell.id("script_ui_modal"))->visible());
    shell.activate("script_ui_cancel");
    assert(shell.scriptText() == "# local\n" && shell.scriptDirty());
    shell.activate("script_ui_reload");
    // A command arriving while the dialog is open invalidates its confirmation.
    assert(shell.submitCommand(ReplaceScriptTextCommand(id, "# later\n")).status
        == EditorResponseStatus::Completed);
    shell.activate("script_ui_confirm");
    assert(shell.scriptText() == "# later\n" && shell.scriptDirty());
    shell.activate("script_ui_cancel");
    shell.activate("script_ui_reload");
    shell.activate("script_ui_confirm");
    assert(shell.scriptText() == "# external\n" && !shell.scriptDirty());
    assert(!shell.runtime().canvas().findWidget(shell.id("script_ui_modal"))->visible());
    assert(shell.submitCommand(UndoScriptCommand{id}).status == EditorResponseStatus::Completed);
    assert(shell.scriptText() == "# external\n");
    assert(shell.drawCommands().ok());
    std::puts(
        "PASS typed conflict and reload, stale document, modal cancel/stale confirmation/reload and history reset");
}

void documentIsolation(Fixture& fixture)
{
    fixture.write("# first\n");
    Session session;
    fixture.open(session);
    const auto firstId = session.activeId();
    assert(session.replaceActiveRange(
               0, session.active()->text.size(), "# first local\n", Session::maxScriptBytes)
        == Session::EditResult::Applied);
    session.setActiveSelection(4, 2);
    session.setActiveScrollLine(3);
    const auto secondPath = fixture.root / "second.arp";
    const core::pmr::String secondText = "# second\n";
    const core::pmr::Vector<std::uint8_t> secondBytes(secondText.begin(), secondText.end());
    assert(core::io::replaceFileAtomically(core::FilePath::utf8(secondPath), secondBytes).success);
    const auto opened = session.openFile(fixture.root, secondPath);
    assert(opened.code == Code::Opened && session.setActiveIndex(opened.documentIndex));
    const auto secondId = session.activeId();
    assert(session.reloadActiveFile(fixture.root, true).code == Code::Opened);
    assert(session.activeId() == secondId && !session.active()->dirty());
    const auto* first = session.find(firstId);
    assert(first && first->text == "# first local\n" && first->dirty());
    assert(first->cursor == 4 && first->anchor == 2 && first->scrollLine == 3);
    assert(first->undo.size() == 1 && first->redo.empty());
    assert(session.setActiveIndex(0) && session.undoActive() && !session.active()->dirty());
    assert(session.find(secondId)->text == "# second\n");
    std::puts(
        "PASS reload preserves other documents, selection, scroll, history and disk baseline");
}

void remoteContract(Fixture& fixture)
{
    fixture.write("# original\n");
    EditorShell shell;
    fixture.shell(shell);
    const auto id = shell.activeScriptDocumentId();
    assert(shell.submitCommand(ReplaceScriptTextCommand(id, "# local\n")).status
        == EditorResponseStatus::Completed);
    fixture.write("# external\n");
    EditorRemoteProtocol protocol("script-file-token");
    const auto request = [&](std::string_view text, bool mcp = false) {
        network::http::Request input;
        input.set_method(network::http::Method::Post);
        input.set_target(mcp ? "/mcp" : "/api/v1/commands");
        input.add_header("Authorization", "Bearer script-file-token");
        input.add_header("Accept", "application/json");
        input.add_header("MCP-Protocol-Version", "2025-11-25");
        input.set_content_type(network::http::ContentType::Json);
        input.set_body_text(text);
        return protocol.handle(input, shell);
    };
    const auto prefix =
        core::pmr::String(R"({"schemaVersion":1,"command":"script.reload","documentId":)")
        + core::pmr::String(std::to_string(id));
    assert(request(prefix + "}").body_text().find("unsaved_changes") != std::string_view::npos);
    for (auto suffix : {R"(,"discardChanges":"true"})",
             R"(,"discardChanges":true,"active":false})",
             R"(,"discardChanges":true,"unknown":1})"})
        assert(request(prefix + suffix).status().value() != 200);
    assert(shell.scriptText() == "# local\n" && shell.scriptDirty());
    const auto body = prefix + R"(,"discardChanges":true,"idempotencyKey":"reload-1"})";
    assert(request(body).status().value() == 200 && shell.scriptText() == "# external\n");
    const auto version = shell.stateVersion();
    assert(request(body).status().value() == 200 && shell.stateVersion() == version);
    assert(request(prefix + R"(,"discardChanges":false,"idempotencyKey":"reload-1"})")
               .body_text()
               .find("idempotency_key_conflict")
        != std::string_view::npos);
    assert(shell.submitCommand(ReplaceScriptTextCommand(id, "# mcp local\n")).status
        == EditorResponseStatus::Completed);
    const auto mcp =
        core::pmr::String(
            R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"script.reload","arguments":{"documentId":)")
        + core::pmr::String(std::to_string(id)) + R"(,"discardChanges":true}}})";
    assert(request(mcp, true).body_text().find(R"("isError":false)") != std::string_view::npos);
    assert(shell.scriptText() == "# external\n" && !shell.scriptDirty());
    std::puts(
        "PASS REST/MCP reload strict fields, dirty protection, idempotency and discard fingerprint");
}

void lifetime(Fixture& fixture)
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i <= 20; ++i) {
        fixture.write("# baseline\r\n");
        {
            EditorShell shell;
            fixture.shell(shell);
            const auto id = shell.activeScriptDocumentId();
            assert(shell.submitCommand(ReplaceScriptTextCommand(id, "# local\n")).status
                == EditorResponseStatus::Completed);
            fixture.write("# changed\r\n");
            assert(shell.submitCommand(SaveScriptCommand{id}).diagnostic
                == EditorDiagnosticCode::ScriptExternalConflict);
            shell.activate("script_ui_reload");
            shell.activate("script_ui_confirm");
            assert(!shell.scriptDirty() && shell.scriptText() == "# changed\n");
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
        "PASS 20 warmed ordinary script conflict/reload/destroy cycles; core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    Fixture fixture;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifetime(fixture);
    else {
        contract(fixture);
        documentIsolation(fixture);
        shellContract(fixture);
        remoteContract(fixture);
    }
}
