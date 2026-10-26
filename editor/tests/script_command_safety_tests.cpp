#include "editor/remote/editor_remote_protocol.h"
#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/validation/editor_script_command_smoke.h"
#include "editor/workspace/editor_shell.h"

#include <array>

namespace {
using namespace arp;
using namespace arp::editor;

void load(EditorShell& shell, const tests::RecoveryFixture& files)
{
    tests::RecoveryFixture::write(files.root / "commands.arp", "# old\n");
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    assert(shell.openScriptFile(files.root / "commands.arp"));
}

bool editProbe(const tests::RecoveryFixture& files, std::size_t budget)
{
    EditorShell shell;
    load(shell, files);
    auto command = ReplaceScriptTextCommand(shell.activeScriptDocumentId(), "# edited\n");
    const auto version = shell.stateVersion();
    const auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
    bool threw = false;
    EditorResponse response;
    {
        core::CpuMemoryQuotaScope scope(quota);
        try {
            response = shell.submitCommand(std::move(command));
        } catch (const std::bad_alloc&) {
            threw = true;
        }
    }
    const bool changed = shell.scriptText() == "# edited\n";
    std::printf("edit budget=%zu threw=%u changed=%u versionDelta=%llu\n",
        budget,
        threw,
        changed,
        static_cast<unsigned long long>(shell.stateVersion() - version));
    if (threw)
        return false;
    if (changed)
        return response.status == EditorResponseStatus::Completed
            && shell.stateVersion() == version + 1;
    return response.status == EditorResponseStatus::Failed && shell.stateVersion() == version
        && response.diagnostic == EditorDiagnosticCode::CommandPreparationFailed;
}

bool saveProbe(const tests::RecoveryFixture& files, std::size_t budget)
{
    EditorShell shell;
    load(shell, files);
    const auto id = shell.activeScriptDocumentId();
    const core::pmr::String text(8192, 's');
    assert(shell.submitCommand(ReplaceScriptTextCommand(id, text)).status
        == EditorResponseStatus::Completed);
    // A real control value whose copy fails after the document save commits.
    assert(shell.runtime()
            .setValue(shell.id("output_status"),
                ui::UiControlValue::fromText(core::pmr::String(32768, 'f')))
            .ok());
    const auto version = shell.stateVersion();
    const auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
    bool threw = false;
    EditorResponse response;
    {
        core::CpuMemoryQuotaScope scope(quota);
        try {
            response = shell.submitCommand(SaveScriptCommand{id});
        } catch (const std::bad_alloc&) {
            threw = true;
        }
    }
    const auto disk = core::readTextFile(files.root / "commands.arp");
    assert(disk.success);
    const bool committed = disk.text == text;
    std::printf("save budget=%zu threw=%u committed=%u dirty=%u versionDelta=%llu\n",
        budget,
        threw,
        committed,
        shell.scriptDirty(),
        static_cast<unsigned long long>(shell.stateVersion() - version));
    if (threw)
        return false;
    if (committed) {
        if (shell.scriptPresentationFailed()) {
            assert(response.diagnostic == EditorDiagnosticCode::ScriptPresentationFailed);
            const auto feedback = shell.feedbackVersion();
            {
                const auto retryQuota = core::makeShared<core::CpuMemoryQuota>(1);
                core::CpuMemoryQuotaScope scope(retryQuota);
                assert(!shell.retryScriptPresentation() && shell.scriptPresentationFailed());
            }
            assert(shell.feedbackVersion() > feedback && shell.stateVersion() == version + 1);
            // Retry cannot save again or reload an independent external edit.
            tests::RecoveryFixture::write(files.root / "commands.arp", "# independently changed\n");
            assert(shell.retryScriptPresentation() && !shell.scriptPresentationFailed());
            assert(shell.scriptText() == text && !shell.scriptDirty());
            assert(core::readTextFile(files.root / "commands.arp").text
                == "# independently changed\n");
            assert(shell.stateVersion() == version + 1);
        }
        return response.status == EditorResponseStatus::Completed && !shell.scriptDirty()
            && shell.stateVersion() == version + 1;
    }
    return response.status == EditorResponseStatus::Failed && shell.scriptDirty()
        && shell.stateVersion() == version && disk.text == "# old\n";
}

void historyAndReload(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    load(shell, files);
    const auto id = shell.activeScriptDocumentId();
    assert(shell.submitCommand(ReplaceScriptTextCommand(id, "# history\n")).status
        == EditorResponseStatus::Completed);
    for (bool undo : {true, false}) {
        const auto version = shell.stateVersion();
        const auto text = shell.scriptText();
        EditorResponse result;
        {
            const auto quota = core::makeShared<core::CpuMemoryQuota>(512);
            core::CpuMemoryQuotaScope scope(quota);
            result = undo ? shell.submitCommand(UndoScriptCommand{id})
                          : shell.submitCommand(RedoScriptCommand{id});
        }
        assert(result.status == EditorResponseStatus::Failed
            && result.diagnostic == EditorDiagnosticCode::CommandPreparationFailed
            && shell.stateVersion() == version && shell.scriptText() == text);
        assert((undo ? shell.submitCommand(UndoScriptCommand{id})
                     : shell.submitCommand(RedoScriptCommand{id}))
                   .status
            == EditorResponseStatus::Completed);
    }
    tests::RecoveryFixture::write(files.root / "commands.arp", "# disk reload\n");
    const auto version = shell.stateVersion();
    EditorResponse result;
    {
        const auto quota = core::makeShared<core::CpuMemoryQuota>(512);
        core::CpuMemoryQuotaScope scope(quota);
        result = shell.submitCommand(ReloadScriptCommand{id, true});
    }
    assert(result.diagnostic == EditorDiagnosticCode::CommandPreparationFailed
        && shell.stateVersion() == version && shell.scriptText() == "# history\n"
        && shell.scriptDirty());
    assert(shell.submitCommand(ReloadScriptCommand{id, true}).status
        == EditorResponseStatus::Completed);
    assert(shell.scriptText() == "# disk reload\n" && !shell.scriptDirty());
    assert(shell.submitCommand(UndoScriptCommand{id}).status == EditorResponseStatus::Completed
        && shell.scriptText() == "# disk reload\n");
    std::puts(
        "PASS Undo/Redo/reload preparation failures preserve state/history and retry correctly");
}

void receiptsAndProtocol(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    load(shell, files);
    const auto id = shell.activeScriptDocumentId();
    assert(shell.submitCommand(ReplaceScriptTextCommand(id, "# recorded\n")).status
        == EditorResponseStatus::Completed);
    assert(shell.runtime()
            .setValue(shell.id("output_status"),
                ui::UiControlValue::fromText(core::pmr::String(262144, 'f')))
            .ok());
    EditorCommandEnvelope save{SaveScriptCommand{id}};
    save.commandId = 76;
    save.idempotencyKey = "script-save-receipt";
    save.idempotencyFingerprint = "script.save/1/document/current";
    auto first = save;
    EditorResponse response;
    {
        const auto quota = core::makeShared<core::CpuMemoryQuota>(131072);
        core::CpuMemoryQuotaScope scope(quota);
        response = shell.submitCommand(std::move(first));
    }
    assert(response.status == EditorResponseStatus::Completed
        && response.diagnostic == EditorDiagnosticCode::ScriptPresentationFailed
        && shell.scriptPresentationFailed() && !shell.scriptDirty());
    const auto version = shell.stateVersion();
    tests::RecoveryFixture::write(files.root / "commands.arp", "# external after receipt\n");
    const auto replay = shell.submitCommand(save);
    assert(replay.commandId == 76 && replay.status == EditorResponseStatus::Completed
        && replay.diagnostic == EditorDiagnosticCode::ScriptPresentationFailed
        && shell.stateVersion() == version && shell.scriptPresentationFailed());
    EditorRemoteProtocol protocol("script-command-token");
    const auto request = [&](std::string_view body, bool mcp = false) {
        network::http::Request input;
        input.set_method(network::http::Method::Post);
        input.set_target(mcp ? "/mcp" : "/api/v1/commands");
        input.add_header("Authorization", "Bearer script-command-token");
        input.add_header("Accept", "application/json");
        input.add_header("MCP-Protocol-Version", "2025-11-25");
        input.set_content_type(network::http::ContentType::Json);
        input.set_body_text(body);
        return protocol.handle(input, shell);
    };
    assert(request(R"({"schemaVersion":1,"command":"editor.state.get"})")
               .body_text()
               .find(R"("scriptPresentationFailed":true)")
        != std::string_view::npos);
    const auto retried = request(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"editor.activate","arguments":{"source":"script_presentation_retry"}}})",
        true);
    assert(retried.body_text().find(R"("isError":false)") != std::string_view::npos);
    assert(!shell.scriptPresentationFailed() && shell.stateVersion() == version);
    assert(request(R"({"schemaVersion":1,"command":"editor.state.get"})")
               .body_text()
               .find(R"("scriptPresentationFailed":false)")
        != std::string_view::npos);
    assert(core::readTextFile(files.root / "commands.arp").text == "# external after receipt\n");
    // Cached execution result remains historical even after display recovers.
    assert(shell.submitCommand(save).diagnostic == EditorDiagnosticCode::ScriptPresentationFailed
        && !shell.scriptPresentationFailed() && shell.stateVersion() == version);
    // Project replacement must retire a failed feedback candidate as well.
    assert(shell.runtime()
            .setValue(shell.id("output_status"),
                ui::UiControlValue::fromText(core::pmr::String(262144, 'f')))
            .ok());
    {
        const auto quota = core::makeShared<core::CpuMemoryQuota>(131072);
        core::CpuMemoryQuotaScope scope(quota);
        assert(shell.submitCommand(SaveScriptCommand{id}).diagnostic
            == EditorDiagnosticCode::ScriptExternalConflict);
    }
    assert(shell.scriptPresentationFailed());
    assert(
        shell.submitCommand(CloseProjectCommand{true}).status == EditorResponseStatus::Completed);
    assert(!shell.scriptPresentationFailed() && shell.openScriptCount() == 0);
    const auto replacedVersion = shell.stateVersion();
    assert(shell.retryScriptPresentation() && shell.stateVersion() == replacedVersion
        && shell.openScriptCount() == 0);
    std::puts(
        "PASS retained committed receipt, REST state and MCP display-only retry; disk never replayed and project replacement retires feedback");
}

void nativePreparation(std::string_view scenario)
{
    EditorShell shell;
    assert(shell
            .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                std::filesystem::path(ARP_SOURCE_DIR))
            .ok());
    core::pmr::String diagnostic;
    assert(EditorScriptCommandSmoke::run(shell, scenario, diagnostic));
    if (scenario == "script_closed") {
        const auto version = shell.stateVersion();
        assert(shell.retryScriptPresentation() && !shell.openScriptCount()
            && shell.stateVersion() == version);
    }
}

void wireReceipt(const tests::RecoveryFixture& files, bool mcp)
{
    EditorShell shell;
    load(shell, files);
    const auto id = shell.activeScriptDocumentId();
    assert(shell.submitCommand(ReplaceScriptTextCommand(id, "# remote saved\n")).status
        == EditorResponseStatus::Completed);
    assert(shell.runtime()
            .setValue(shell.id("output_status"),
                ui::UiControlValue::fromText(core::pmr::String(262144, 'f')))
            .ok());
    EditorRemoteProtocol protocol("script-wire-token");
    network::http::Request input;
    input.set_method(network::http::Method::Post);
    input.set_target(mcp ? "/mcp" : "/api/v1/commands");
    input.add_header("Authorization", "Bearer script-wire-token");
    input.add_header("Accept", "application/json");
    input.add_header("MCP-Protocol-Version", "2025-11-25");
    input.set_content_type(network::http::ContentType::Json);
    const auto number = core::pmr::String(std::to_string(id));
    input.set_body_text(mcp
            ? core::pmr::String(
                  R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"script.save","arguments":{"documentId":)")
                + number + "}}}"
            : core::pmr::String(R"({"schemaVersion":1,"command":"script.save","documentId":)")
                + number + "}");
    const auto version = shell.stateVersion();
    network::http::Response response;
    {
        const auto quota = core::makeShared<core::CpuMemoryQuota>(131072);
        core::CpuMemoryQuotaScope scope(quota);
        response = protocol.handle(input, shell);
    }
    assert(response.status().value() == 200 && shell.stateVersion() == version + 1
        && shell.scriptPresentationFailed() && !shell.scriptDirty());
    assert(response.body_text().find("script_presentation_failed") != std::string_view::npos);
    if (mcp)
        assert(response.body_text().find(R"("isError":false)") != std::string_view::npos);
    assert(core::readTextFile(files.root / "commands.arp").text == "# remote saved\n");
    // A newer non-script notification supersedes the queued historical message.
    shell.reportViewportDiagnostic("newer feedback");
    assert(shell.retryScriptPresentation() && !shell.scriptPresentationFailed());
    assert(shell.runtime().canvas().findWidget(shell.id("output_status"))->desc().text
        == "3D viewport: newer feedback");
    assert(shell.stateVersion() == version + 1);
    std::puts(
        "PASS real REST/MCP committed save receipt carries presentation diagnostic; newer feedback supersedes retry candidate");
}
} // namespace

int main(int argc, char** argv)
{
    tests::RecoveryFixture files;
    const bool probe = argc == 2 && std::string_view(argv[1]) == "--probe";
    if (probe) {
        const bool edit = editProbe(files, 512);
        const bool save = saveProbe(files, 30000);
        std::printf("%s script command receipt follows real edit/save state\n",
            edit && save ? "PASS" : "FAIL");
        return edit && save ? 0 : 2;
    }
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= (smoke ? 20u : 0u); ++cycle) {
        if (!smoke)
            for (const auto budget : std::array<std::size_t, 5>{1, 512, 30000, 65536, 131072}) {
                assert(editProbe(files, budget));
                assert(saveProbe(files, budget));
            }
        if (smoke) {
            assert(editProbe(files, 512));
            assert(saveProbe(files, 30000));
        }
        historyAndReload(files);
        receiptsAndProtocol(files);
        if (!smoke) {
            wireReceipt(files, false);
            wireReceipt(files, true);
        }
        for (const auto scenario : {"script_failed", "script_retry", "script_closed"})
            nativePreparation(scenario);
        const auto now = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = now;
        assert(now.liveBytes == baseline.liveBytes);
        assert(now.allocationCount - now.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts("PASS script command failure boundaries and ordinary lifecycle core baseline");
    if (smoke)
        std::puts(
            "PASS 20 warmed ordinary failure/retry/history/receipt/close/destruction cycles: core live bytes/count delta=0");
}
