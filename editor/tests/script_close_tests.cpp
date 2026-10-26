#include "core/io/file_io.h"
#include "editor/remote/editor_remote_protocol.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_shell.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>

namespace {
using namespace arp;
using namespace arp::editor;

class CloseFixture final {
public:
    CloseFixture()
    {
        root = std::filesystem::path(ARP_SOURCE_DIR) / "build"
            / ("script-close-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        std::ofstream(root / "first.arp", std::ios::binary) << "# first\r\n";
        std::ofstream(root / "second.arp", std::ios::binary) << "# second\n";
    }

    ~CloseFixture()
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
    }

    std::uint64_t open(EditorShell& shell, const char* name) const
    {
        assert(shell.openScriptFile(root / name));
        return shell.activeScriptDocumentId();
    }

    static bool modal(EditorShell& shell)
    {
        return shell.runtime().canvas().findWidget(shell.id("script_ui_modal"))->visible();
    }

    static void key(EditorShell& shell, EditorInputKey value, bool down = true)
    {
        EditorInputCommand input;
        input.kind = down ? EditorInputKind::KeyDown : EditorInputKind::KeyUp;
        input.key = value;
        assert(shell.submitCommand(std::move(input)).status == EditorResponseStatus::Completed);
    }

    static void closeFirstTab(EditorShell& shell)
    {
        const auto drawn = shell.drawCommands();
        assert(drawn.ok());
        // Use the rendered close glyph, rather than duplicating tab width math.
        for (const auto& command : drawn.commands()) {
            if (command.widgetId != shell.id("scene_tabs")
                || command.kind != renderer::ui::UiDrawCommandKind::Text || command.text != "x")
                continue;
            EditorInputCommand input;
            input.kind = EditorInputKind::PointerDown;
            input.primaryButton = true;
            input.button = EditorPointerButton::Left;
            input.x = command.rect.x + command.rect.width * 0.5f;
            input.y = command.rect.y + command.rect.height * 0.5f;
            assert(shell.submitCommand(input).status == EditorResponseStatus::Completed);
            input.kind = EditorInputKind::PointerUp;
            assert(shell.submitCommand(input).status == EditorResponseStatus::Completed);
            return;
        }
        assert(false && "Missing script tab close glyph");
    }

    std::filesystem::path root;
};

void isolation(CloseFixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    assert(shell.submitCommand(CreateSceneObjectCommand("preserve scene history")).status
        == EditorResponseStatus::Completed);
    const auto objectId = shell.sceneDocument().selectedObject();
    assert(shell.submitCommand(SetSceneObjectNameCommand("retained rename")).status
        == EditorResponseStatus::Completed);
    const auto first = fixture.open(shell, "first.arp");
    assert(shell.submitCommand(ReplaceScriptTextCommand(first, "# dirty first\n")).status
        == EditorResponseStatus::Completed);
    const auto second = fixture.open(shell, "second.arp");
    core::pmr::String manyLines;
    for (unsigned i = 0; i < 120; ++i)
        manyLines += "# retained\n";
    assert(shell.submitCommand(ReplaceScriptTextCommand(second, manyLines)).status
        == EditorResponseStatus::Completed);
    CloseFixture::key(shell, EditorInputKey::LeftControl);
    CloseFixture::key(shell, EditorInputKey::A);
    CloseFixture::key(shell, EditorInputKey::LeftControl, false);
    EditorInputCommand wheel;
    wheel.kind = EditorInputKind::Wheel;
    const auto layout = shell.runtime().canvas().resolveLayout();
    const auto* viewport = layout.findWidget(shell.id("viewport"));
    assert(viewport);
    wheel.x = viewport->rect.x + 100;
    wheel.y = viewport->rect.y + 100;
    wheel.wheelDelta = -5;
    assert(shell.submitCommand(wheel).status == EditorResponseStatus::Completed);
    const auto selectionStart = shell.scriptSelectionStart(),
               selectionEnd = shell.scriptSelectionEnd();
    const auto scroll = shell.scriptScrollLine();
    assert(scroll > 0);
    assert(selectionEnd == manyLines.size());
    const auto version = shell.stateVersion();
    assert(shell.submitCommand(CloseScriptCommand{first}).diagnostic
        == EditorDiagnosticCode::UnsavedChanges);
    assert(shell.stateVersion() == version && shell.activeScriptDocumentId() == second);
    CloseFixture::closeFirstTab(shell);
    assert(CloseFixture::modal(shell) && shell.activeScriptDocumentId() == second);
    assert(shell.runtime()
               .canvas()
               .findWidget(shell.id("script_ui_description"))
               ->desc()
               .text.find("first.arp")
        != core::pmr::String::npos);
    shell.activate("script_ui_cancel");
    assert(shell.openScriptCount() == 2 && shell.scriptText() == manyLines);
    CloseFixture::closeFirstTab(shell);
    shell.activate("script_ui_confirm");
    assert(shell.openScriptCount() == 1 && shell.activeScriptDocumentId() == second);
    assert(shell.scriptText() == manyLines && shell.scriptDirty());
    assert(shell.scriptSelectionStart() == selectionStart
        && shell.scriptSelectionEnd() == selectionEnd && shell.scriptScrollLine() == scroll);
    assert(shell.submitCommand(UndoScriptCommand{second}).status == EditorResponseStatus::Completed
        && !shell.scriptDirty());
    assert(shell.submitCommand(RedoScriptCommand{second}).status == EditorResponseStatus::Completed
        && shell.scriptDirty());
    assert(shell.submitCommand(CloseScriptCommand{first, true}).diagnostic
        == EditorDiagnosticCode::StaleDocumentTarget);
    const auto reopened = fixture.open(shell, "first.arp");
    assert(reopened != first && shell.scriptText() == "# first\n" && !shell.scriptDirty());
    assert(shell.submitCommand(CloseScriptCommand{first, true}).diagnostic
        == EditorDiagnosticCode::StaleDocumentTarget);
    assert(shell.submitCommand(CloseScriptCommand{reopened}).status
        == EditorResponseStatus::Completed);
    assert(shell.activeScriptDocumentId() == second);
    assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
    assert(shell.sceneDocument().scene().findObject(objectId)->name() == "preserve scene history");
    shell.activate("workspace_3D");
    assert(shell.submitCommand(CloseScriptCommand{second, true}).status
        == EditorResponseStatus::Completed);
    assert(shell.openScriptCount() == 0 && shell.workspace() == "3D");
    assert(core::readTextFile(fixture.root / "first.arp").text == "# first\r\n");
    assert(core::readTextFile(fixture.root / "second.arp").text == "# second\n");
    std::puts(
        "PASS exact non-active pointer close, cancel, other document buffer/selection/scroll/history and scene history isolation, disk unchanged and stale ID rejection");
}

void confirmation(CloseFixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    const auto id = fixture.open(shell, "first.arp");
    assert(shell.submitCommand(ReplaceScriptTextCommand(id, "# local\n")).status
        == EditorResponseStatus::Completed);
    CloseFixture::key(shell, EditorInputKey::LeftControl);
    CloseFixture::key(shell, EditorInputKey::W);
    assert(CloseFixture::modal(shell));
    assert(shell.submitCommand(ReplaceScriptTextCommand(id, "# newer\n")).status
        == EditorResponseStatus::Completed);
    shell.activate("script_ui_confirm");
    shell.activate("script_ui_close");
    shell.activate("script_ui_reload");
    shell.activate("script_ui_confirm");
    assert(shell.openScriptCount() == 1 && shell.scriptText() == "# newer\n");
    assert(!shell.runtime().canvas().findWidget(shell.id("script_ui_confirm"))->enabled());
    shell.activate("script_ui_cancel");
    shell.activate("script_ui_close");
    shell.activate("script_ui_close");
    shell.activate("script_ui_confirm");
    assert(
        shell.openScriptCount() == 0 && shell.workspace() == "Game" && !CloseFixture::modal(shell));
    const auto next = fixture.open(shell, "first.arp");
    shell.activate("script_ui_reload");
    assert(shell.runtime().canvas().findWidget(shell.id("script_ui_confirm"))->desc().text
        == "Discard and Reload");
    assert(shell.runtime().canvas().findWidget(shell.id("script_ui_title"))->desc().text
        == "Reload Script From Disk?");
    shell.activate("script_ui_close");
    shell.activate("script_ui_confirm");
    assert(shell.openScriptCount() == 1 && shell.activeScriptDocumentId() == next
        && !CloseFixture::modal(shell));
    assert(shell.submitCommand(CloseScriptCommand{0, true}).diagnostic
        == EditorDiagnosticCode::StaleDocumentTarget);
    assert(shell.submitCommand(CloseScriptCommand{next}).status == EditorResponseStatus::Completed);
    std::puts(
        "PASS Ctrl+W, stale/repeated confirmation and immutable reload/close mode, last Script tab and clean close");
}

void protocol(CloseFixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    const auto id = fixture.open(shell, "first.arp");
    assert(shell.submitCommand(ReplaceScriptTextCommand(id, "# local\n")).status
        == EditorResponseStatus::Completed);
    EditorRemoteProtocol protocol("close-test-token");
    const auto request = [&](std::string_view body, bool mcp = false) {
        network::http::Request input;
        input.set_method(network::http::Method::Post);
        input.set_target(mcp ? "/mcp" : "/api/v1/commands");
        input.add_header("Authorization", "Bearer close-test-token");
        input.add_header("Accept", "application/json");
        input.add_header("MCP-Protocol-Version", "2025-11-25");
        input.set_content_type(network::http::ContentType::Json);
        input.set_body_text(body);
        return protocol.handle(input, shell);
    };
    const auto prefix =
        core::pmr::String(R"({"schemaVersion":1,"command":"script.close","documentId":)")
        + core::pmr::String(std::to_string(id));
    assert(request(prefix + "}").body_text().find("unsaved_changes") != std::string_view::npos);
    for (const auto suffix : {R"(,"discardChanges":"true"})",
             R"(,"discardChanges":true,"path":"x"})",
             R"(,"discardChanges":true,"text":"x"})",
             R"(,"discardChanges":true,"unknown":1})"})
        assert(request(prefix + suffix).status().value() != 200 && shell.openScriptCount() == 1);
    const auto version = shell.stateVersion();
    assert(request(prefix + R"(,"discardChanges":true,"expectedStateVersion":0})")
               .body_text()
               .find("stale_state_version")
        != std::string_view::npos);
    assert(shell.stateVersion() == version);
    const auto mcp =
        core::pmr::String(
            R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"script.close","arguments":{"documentId":)")
        + core::pmr::String(std::to_string(id))
        + R"(,"discardChanges":true,"idempotencyKey":"close-1"}}})";
    assert(request(mcp, true).body_text().find(R"("isError":false)") != std::string_view::npos
        && shell.openScriptCount() == 0);
    const auto body = prefix + R"(,"discardChanges":true,"idempotencyKey":"close-1"})";
    const auto committed = shell.stateVersion();
    assert(request(body).status().value() == 200 && shell.stateVersion() == committed);
    assert(request(prefix + R"(,"discardChanges":false,"idempotencyKey":"close-1"})")
               .body_text()
               .find("idempotency_key_conflict")
        != std::string_view::npos);
    assert(request(prefix + ",\"discardChanges\":true}").body_text().find("stale_document_target")
        != std::string_view::npos);
    std::puts(
        "PASS REST/MCP close strict fields, dirty/state guard, explicit discard, idempotency and stale document");
}

void lifetime(CloseFixture& fixture)
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i <= 20; ++i) {
        {
            EditorShell shell;
            fixture.initialize(shell);
            const auto first = fixture.open(shell, "first.arp");
            assert(shell.submitCommand(ReplaceScriptTextCommand(first, "# dirty\n")).status
                == EditorResponseStatus::Completed);
            const auto second = fixture.open(shell, "second.arp");
            CloseFixture::closeFirstTab(shell);
            shell.activate("script_ui_cancel");
            CloseFixture::closeFirstTab(shell);
            assert(shell.drawCommands().ok());
            shell.activate("script_ui_confirm");
            assert(shell.openScriptCount() == 1 && shell.activeScriptDocumentId() == second);
            assert(shell.submitCommand(CloseScriptCommand{second}).status
                == EditorResponseStatus::Completed);
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!i)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary pointer-close/cancel/confirm/destroy cycles; core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    CloseFixture fixture;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifetime(fixture);
    else {
        isolation(fixture);
        confirmation(fixture);
        protocol(fixture);
    }
}
