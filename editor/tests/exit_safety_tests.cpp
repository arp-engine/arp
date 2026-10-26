#include "core/io/file_io.h"
#include "editor/remote/editor_remote_protocol.h"
#include "editor/tests/fixtures/project_fixture.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_shell.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

namespace {
using namespace arp;
using namespace arp::editor;

class ExitFixture final {
public:
    ExitFixture()
    {
        root = std::filesystem::path(ARP_SOURCE_DIR) / "build"
            / ("editor-exit-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        std::ofstream(root / "first.arp", std::ios::binary) << "# first\r\n";
        std::ofstream(root / "second.arp", std::ios::binary) << "# second\n";
    }

    ~ExitFixture()
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
        assert(!shell.sceneDirty() && !shell.exitRequested());
    }

    static core::pmr::String contents(EditorShell& shell)
    {
        core::pmr::String text, diagnostic;
        assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), text, diagnostic));
        return text;
    }

    static bool modal(EditorShell& shell)
    {
        return shell.runtime().canvas().findWidget(shell.id("exit_ui_modal"))->visible();
    }

    std::filesystem::path root;
};

void contract(ExitFixture& fixture)
{
    {
        EditorShell shell;
        fixture.initialize(shell);
        const auto before = ExitFixture::contents(shell);
        assert(shell.submitCommand(RequestEditorExitCommand{}).status
            == EditorResponseStatus::Completed);
        const auto version = shell.stateVersion();
        assert(shell.exitRequested()
            && shell.submitCommand(GetEditorStateQuery{}).snapshot.exitRequested);
        assert(shell.submitCommand(RequestEditorExitCommand{true}).status
            == EditorResponseStatus::Completed);
        assert(shell.stateVersion() == version);
        assert(shell.submitCommand(CreateSceneObjectCommand("after exit")).diagnostic
            == EditorDiagnosticCode::WorkspaceBusy);
        assert(ExitFixture::contents(shell) == before && shell.stateVersion() == version);
    }
    {
        EditorShell shell;
        fixture.initialize(shell);
        assert(shell.submitCommand(CreateSceneObjectCommand("unsaved")).status
            == EditorResponseStatus::Completed);
        const auto before = ExitFixture::contents(shell);
        const auto version = shell.stateVersion();
        assert(shell.submitCommand(RequestEditorExitCommand{}).diagnostic
            == EditorDiagnosticCode::UnsavedChanges);
        assert(!shell.exitRequested() && shell.stateVersion() == version);
        shell.activate("exit_ui_request");
        assert(ExitFixture::modal(shell));
        shell.activate("exit_ui_cancel");
        assert(!ExitFixture::modal(shell) && !shell.exitRequested());
        assert(ExitFixture::contents(shell) == before);
        shell.activate("exit_ui_request");
        assert(shell.submitCommand(CreateSceneObjectCommand("newer edit")).status
            == EditorResponseStatus::Completed);
        shell.activate("exit_ui_confirm");
        assert(!shell.exitRequested() && ExitFixture::modal(shell));
        // A second native close request must not re-arm an invalid confirmation.
        shell.activate("exit_ui_request");
        shell.activate("exit_ui_confirm");
        assert(!shell.exitRequested());
        assert(!shell.runtime().canvas().findWidget(shell.id("exit_ui_confirm"))->enabled());
        shell.activate("exit_ui_cancel");
        shell.activate("exit_ui_request");
        assert(shell.drawCommands().ok());
        shell.activate("exit_ui_confirm");
        assert(shell.exitRequested() && !ExitFixture::modal(shell));
    }
    {
        EditorShell shell;
        fixture.initialize(shell);
        assert(shell.openScriptFile(fixture.root / "first.arp"));
        const auto firstId = shell.activeScriptDocumentId();
        assert(shell.submitCommand(ReplaceScriptTextCommand(firstId, "# unsaved first\n")).status
            == EditorResponseStatus::Completed);
        assert(shell.openScriptFile(fixture.root / "second.arp") && !shell.scriptDirty());
        assert(shell.submitCommand(RequestEditorExitCommand{}).diagnostic
            == EditorDiagnosticCode::UnsavedChanges);
        shell.activate("exit_ui_request");
        shell.activate("exit_ui_cancel");
        assert(shell.openScriptCount() == 2 && !shell.exitRequested());
        assert(shell.openScriptFile(fixture.root / "first.arp"));
        assert(shell.scriptText() == "# unsaved first\n" && shell.scriptDirty());
        assert(shell.submitCommand(UndoScriptCommand{firstId}).status
            == EditorResponseStatus::Completed);
        assert(!shell.scriptDirty());
        assert(core::readTextFile(fixture.root / "first.arp").text == "# first\r\n");
        // Another modal retains sole ownership of input; exit cannot stack over it.
        shell.activate("script_ui_reload");
        assert(shell.submitCommand(RequestEditorExitCommand{true}).diagnostic
            == EditorDiagnosticCode::WorkspaceBusy);
        shell.activate("exit_ui_request");
        assert(!ExitFixture::modal(shell));
        shell.activate("script_ui_cancel");
        assert(shell.submitCommand(RequestEditorExitCommand{}).status
            == EditorResponseStatus::Completed);
    }
    std::puts(
        "PASS clean/dirty/non-active script exit, cancellation, stale/repeated confirmation, modal ownership and terminal authoring gate");
}

void busy(ExitFixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    SetGamePreviewStateCommand playing("fixture running");
    playing.running = true;
    assert(shell.submitCommand(std::move(playing)).status == EditorResponseStatus::Completed);
    assert(shell.submitCommand(RequestEditorExitCommand{true}).diagnostic
        == EditorDiagnosticCode::WorkspaceBusy);
    shell.activate("exit_ui_request");
    assert(!shell.exitRequested() && !ExitFixture::modal(shell));
    assert(shell.submitCommand(SetGamePreviewStateCommand("stopped")).status
        == EditorResponseStatus::Completed);
    assert(shell.submitCommand(SaveSceneCommand("saved.arp.yaml")).status
        == EditorResponseStatus::Completed);
    assert(shell.submitCommand(RequestEditorExitCommand{true}).diagnostic
        == EditorDiagnosticCode::WorkspaceBusy);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (shell.submitCommand(GetEditorStateQuery{}).snapshot.sceneIoPending
        && std::chrono::steady_clock::now() < deadline) {
        shell.pollTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(!shell.submitCommand(GetEditorStateQuery{}).snapshot.sceneIoPending);
    assert(
        shell.submitCommand(RequestEditorExitCommand{}).status == EditorResponseStatus::Completed);
    std::puts("PASS Play and pending scene IO reject explicit discard exit until complete");
}

void protocol(ExitFixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    assert(shell.submitCommand(CreateSceneObjectCommand("dirty")).status
        == EditorResponseStatus::Completed);
    EditorRemoteProtocol protocol("exit-test-token");
    const auto request = [&](std::string_view body, bool mcp = false) {
        network::http::Request input;
        input.set_method(network::http::Method::Post);
        input.set_target(mcp ? "/mcp" : "/api/v1/commands");
        input.add_header("Authorization", "Bearer exit-test-token");
        input.add_header("Accept", "application/json");
        input.add_header("MCP-Protocol-Version", "2025-11-25");
        input.set_content_type(network::http::ContentType::Json);
        input.set_body_text(body);
        return protocol.handle(input, shell);
    };
    assert(request(R"({"schemaVersion":1,"command":"editor.exit"})")
               .body_text()
               .find("unsaved_changes")
        != std::string_view::npos);
    for (const auto suffix : {R"(,"discardChanges":"true"})",
             R"(,"discardChanges":true,"path":"x"})",
             R"(,"unknown":true})"}) {
        assert(request(core::pmr::String(R"({"schemaVersion":1,"command":"editor.exit")") + suffix)
                   .status()
                   .value()
            != 200);
        assert(!shell.exitRequested());
    }
    const auto result = request(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"editor.exit","arguments":{"discardChanges":true,"idempotencyKey":"exit-1"}}})",
        true);
    assert(result.body_text().find(R"("isError":false)") != std::string_view::npos
        && shell.exitRequested());
    const auto version = shell.stateVersion();
    assert(
        request(
            R"({"schemaVersion":1,"command":"editor.exit","discardChanges":true,"idempotencyKey":"exit-1"})")
            .status()
            .value()
        == 200);
    assert(shell.stateVersion() == version);
    assert(
        request(
            R"({"schemaVersion":1,"command":"editor.exit","discardChanges":false,"idempotencyKey":"exit-1"})")
            .body_text()
            .find("idempotency_key_conflict")
        != std::string_view::npos);
    std::puts(
        "PASS REST/MCP exit dirty guard, strict fields, explicit discard and idempotency fingerprint");
}

void projectDirty(ExitFixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    ProjectManifest manifest;
    ProjectDiagnostic diagnostic;
    assert(ProjectManifestCodec::decode(
        tests::ProjectFixture::document("assetPackages: []\nphysics2d: null\n"),
        manifest,
        diagnostic));
    const auto root = fixture.root / "project";
    assert(
        shell
            .submitCommand(CreateProjectCommand{core::pmr::String(root.generic_string()), manifest})
            .status
        == EditorResponseStatus::Completed);
    const auto disk = core::readTextFile(root / "project.arp.yaml").text;
    assert(shell.submitCommand(RenameProjectCommand{core::pmr::String("unsaved settings")}).status
        == EditorResponseStatus::Completed);
    assert(shell.projectDirty() && !shell.sceneDirty());
    assert(shell.submitCommand(RequestEditorExitCommand{}).diagnostic
        == EditorDiagnosticCode::UnsavedChanges);
    shell.activate("exit_ui_request");
    shell.activate("exit_ui_cancel");
    assert(shell.projectManifest()->name == "unsaved settings" && shell.projectDirty());
    assert(core::readTextFile(root / "project.arp.yaml").text == disk);
    assert(shell.submitCommand(SaveProjectCommand{}).status == EditorResponseStatus::Completed);
    assert(shell.submitCommand(RequestEditorExitCommand{true}).diagnostic
        == EditorDiagnosticCode::WorkspaceBusy);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (shell.submitCommand(GetEditorStateQuery{}).snapshot.projectSavePending
        && std::chrono::steady_clock::now() < deadline) {
        shell.pollTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(!shell.projectDirty());
    assert(
        shell.submitCommand(RequestEditorExitCommand{}).status == EditorResponseStatus::Completed);
    std::puts(
        "PASS project-only unsaved settings, cancellation preserves manifest and pending save blocks exit");
}

void lifetime(ExitFixture& fixture)
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i <= 20; ++i) {
        {
            EditorShell shell;
            fixture.initialize(shell);
            assert(shell.submitCommand(CreateSceneObjectCommand("dirty")).status
                == EditorResponseStatus::Completed);
            shell.activate("exit_ui_request");
            shell.activate("exit_ui_cancel");
            shell.activate("exit_ui_request");
            shell.activate("exit_ui_request");
            assert(shell.drawCommands().ok());
            shell.activate("exit_ui_confirm");
            assert(shell.exitRequested());
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!i)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary request/cancel/confirm/destroy cycles; core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    ExitFixture fixture;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifetime(fixture);
    else {
        contract(fixture);
        busy(fixture);
        projectDirty(fixture);
        protocol(fixture);
    }
}
