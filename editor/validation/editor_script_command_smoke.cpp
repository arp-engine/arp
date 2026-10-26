#include "editor/validation/editor_script_command_smoke.h"

#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/workspace/editor_shell.h"

#include <cstdio>

namespace arp::editor {
bool EditorScriptCommandSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    diagnostic = "script fixture open rejected: project root must contain the isolated fixture";
    tests::RecoveryFixture files;
    const auto path = files.root / "commands.arp";
    tests::RecoveryFixture::write(path, "# old\n");
    if (!shell.openScriptFile(path))
        return false;
    const auto id = shell.activeScriptDocumentId();
    diagnostic = "script fixture text replacement rejected";
    core::pmr::String contents(8192, ' ');
    constexpr std::string_view prefix =
        "# Saved document fixture (not executed).\n\n\nclass ReceiptProbe:\n    pass\n";
    contents.replace(0, prefix.size(), prefix.data(), prefix.size());
    if (shell.submitCommand(ReplaceScriptTextCommand(id, contents)).status
        != EditorResponseStatus::Completed)
        return false;
    diagnostic = "script fixture output control setup failed";
    if (!shell.runtime()
            .setValue(shell.id("output_status"),
                ui::UiControlValue::fromText(core::pmr::String(32768, 'f')))
            .ok())
        return false;
    const auto before = shell.stateVersion();
    diagnostic =
        "script save did not preserve the committed state and presentation failure receipt";
    EditorResponse saved;
    {
        const auto quota = core::makeShared<core::CpuMemoryQuota>(30000);
        core::CpuMemoryQuotaScope scope(quota);
        saved = shell.submitCommand(SaveScriptCommand{id});
    }
    if (saved.status != EditorResponseStatus::Completed
        || saved.diagnostic != EditorDiagnosticCode::ScriptPresentationFailed
        || saved.stateVersion != before + 1 || shell.scriptDirty()
        || !shell.scriptPresentationFailed() || core::readTextFile(path).text != contents)
        return false;
    if (scenario == "script_closed") {
        diagnostic =
            "script close did not preserve the committed state and presentation failure receipt";
        EditorResponse closed;
        {
            const auto quota = core::makeShared<core::CpuMemoryQuota>(512);
            core::CpuMemoryQuotaScope scope(quota);
            closed = shell.submitCommand(CloseScriptCommand{id, false});
        }
        if (closed.status != EditorResponseStatus::Completed
            || closed.diagnostic != EditorDiagnosticCode::ScriptPresentationFailed
            || shell.openScriptCount() || shell.workspace() != "Game"
            || closed.stateVersion != before + 2 || !shell.scriptPresentationFailed())
            return false;
    } else if (scenario == "script_retry") {
        diagnostic = "script display button retry failed or changed document state";
        const auto version = shell.stateVersion();
        ui::UiControlInteraction click;
        click.kind = ui::UiControlInteractionKind::Click;
        click.widgetId = shell.id("script_presentation_retry");
        if (!shell.runtime().interact(click).ok() || shell.scriptPresentationFailed()
            || shell.stateVersion() != version || shell.scriptDirty())
            return false;
    } else if (scenario != "script_failed")
        return false;
    const bool failed = scenario != "script_retry";
    diagnostic = "script presentation visibility does not match the actual failure state";
    if (shell.runtime().canvas().findWidget(shell.id("script_presentation_failed"))->visible()
            != failed
        || shell.runtime().canvas().findWidget(shell.id("script_presentation_retry"))->visible()
            != failed
        || shell.runtime().canvas().findWidget(shell.id("output_status"))->visible() == failed)
        return false;
    const auto query = shell.submitCommand(GetEditorStateQuery{});
    diagnostic = "script presentation snapshot does not match the actual failure state";
    if (!query.hasSnapshot || query.snapshot.scriptPresentationFailed != failed)
        return false;
    diagnostic.clear();
    std::printf(
        "PASS script native scenario=%.*s: committed state retained, failure/retry visible, no operation replay\n",
        static_cast<int>(scenario.size()),
        scenario.data());
    return true;
}
} // namespace arp::editor
