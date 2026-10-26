#include "editor/workspace/editor_exit_panel.h"

#include "editor/workspace/editor_shell.h"

namespace arp::editor {
void EditorExitPanel::initialize(EditorShell& shell)
{
    shell.visible("exit_ui_modal", false);
}

void EditorExitPanel::close(EditorShell& shell)
{
    active_ = false;
    modal_.close(shell);
}

bool EditorExitPanel::activate(EditorShell& shell, std::string_view source)
{
    if (source == "exit_ui_request") {
        // Repeated title-bar close requests must not re-authorize a stale dialog.
        if (active_ || shell.exitRequested())
            return true;
        const auto result = shell.submitCommand(RequestEditorExitCommand{});
        if (result.diagnostic == EditorDiagnosticCode::UnsavedChanges) {
            shell.presentationState_.closeMenu();
            shell.visible("menu_popup", false);
            active_ = true;
            shell.text(
                "exit_ui_status", "Cancel to save your work, or explicitly discard and exit.");
            (void)shell.runtime().setEnabled(shell.id("exit_ui_confirm"), true);
            modal_.open(shell, "exit_ui_modal");
            modal_.armConfirmation();
            (void)shell.runtime().requestFocus(shell.id("exit_ui_cancel"));
        }
    } else if (source == "exit_ui_cancel") {
        close(shell);
    } else if (source == "exit_ui_confirm" && active_) {
        if (!modal_.confirmationValid(shell.stateVersion())) {
            shell.text("exit_ui_status", "The workspace changed. Cancel and request exit again.");
            (void)shell.runtime().setEnabled(shell.id("exit_ui_confirm"), false);
            return true;
        }
        const auto result = shell.submitCommand(RequestEditorExitCommand{true});
        if (result.status == EditorResponseStatus::Completed)
            close(shell);
        else {
            shell.text(
                "exit_ui_status", "Tasks or Play are still active. Cancel and finish them first.");
            (void)shell.runtime().setEnabled(shell.id("exit_ui_confirm"), false);
        }
    } else if (source != "exit_ui_modal")
        return false;
    return true;
}

void EditorExitPanel::commandCompleted(
    std::uint64_t previous, std::uint64_t current, bool blockedInput)
{
    modal_.commandCompleted(previous, current, blockedInput);
}
} // namespace arp::editor
