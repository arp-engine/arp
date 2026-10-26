#include "editor/validation/editor_script_modal_smoke.h"

#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/validation/editor_key_input.h"

#include <cstdio>

namespace arp::editor {
bool EditorScriptModalSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    diagnostic = "EDITOR-SCRIPT-MODAL-SMOKE";
    tests::RecoveryFixture files;
    const auto path = files.root / "modal_workflow.arp";
    tests::RecoveryFixture::write(path, "# disk retained\n");
    if (!shell.openScriptFile(path))
        return false;
    const auto document = shell.activeScriptDocumentId();
    if (shell.submitCommand(ReplaceScriptTextCommand(document, "# local unsaved changes\n")).status
        != EditorResponseStatus::Completed)
        return false;
    EditorKeyInput::send(shell, platform::WindowKey::LeftControl);
    const auto version = shell.stateVersion();
    EditorKeyInput::send(shell, platform::WindowKey::W);
    const auto modal = shell.id("script_ui_modal");
    if (!shell.runtime().canvas().findWidget(modal)->visible()
        || shell.stateVersion() != version + 1 || !shell.scriptDirty())
        return false;
    if (scenario != "script_modal_open") {
        EditorCommandEnvelope confirm(ActivateControlCommand("script_ui_confirm"));
        const auto failingVersion = shell.stateVersion();
        EditorResponse response;
        {
            const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
            core::CpuMemoryQuotaScope scope(quota);
            response = shell.submitCommand(std::move(confirm));
        }
        if (response.status != EditorResponseStatus::Failed
            || response.diagnostic != EditorDiagnosticCode::CommandPreparationFailed
            || shell.stateVersion() != failingVersion || shell.openScriptCount() != 1
            || !shell.runtime().canvas().findWidget(shell.id("script_ui_failed"))->visible()
            || shell.runtime().canvas().findWidget(shell.id("script_ui_confirm"))->enabled())
            return false;
        if (scenario == "script_modal_retry" || scenario == "script_modal_closed") {
            if (shell.submitCommand(ActivateControlCommand("script_ui_cancel")).status
                != EditorResponseStatus::Completed)
                return false;
            if (shell.submitCommand(ActivateControlCommand("script_ui_close")).status
                != EditorResponseStatus::Completed)
                return false;
            if (shell.runtime().canvas().findWidget(shell.id("script_ui_failed"))->visible()
                || !shell.runtime().canvas().findWidget(shell.id("script_ui_confirm"))->enabled())
                return false;
            if (scenario == "script_modal_closed") {
                const auto closing = shell.stateVersion();
                if (shell.submitCommand(ActivateControlCommand("script_ui_confirm")).status
                        != EditorResponseStatus::Completed
                    || shell.stateVersion() != closing + 1 || shell.openScriptCount() != 0
                    || shell.runtime().canvas().findWidget(modal)->visible())
                    return false;
            }
        } else if (scenario != "script_modal_failed")
            return false;
    }
    if (core::readTextFile(path).text != "# disk retained\n")
        return false;
    diagnostic.clear();
    std::printf(
        "PASS controlled native script modal scenario=%.*s: real failure/cancel/retry/close and exact version, disk retained\n",
        static_cast<int>(scenario.size()),
        scenario.data());
    return true;
}
} // namespace arp::editor
