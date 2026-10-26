#include "editor/workspace/editor_script_file_panel.h"

#include "core/io/filesystem_path.h"
#include "editor/workspace/editor_shell.h"

#include <new>

namespace arp::editor {
void EditorScriptFilePanel::initialize(EditorShell& shell)
{
    constexpr const char* names[] = {"script_ui_modal",
        "script_ui_title",
        "script_ui_description",
        "script_ui_detail",
        "script_ui_status",
        "script_ui_confirm",
        "script_ui_cancel",
        "script_ui_failed",
        "script_ui_stale"};
    for (std::size_t i = 0; i != Count; ++i)
        ids_[i] = shell.id(names[i]);
    if (auto* root = shell.runtime().canvas().findWidget(ids_[Root]))
        root->setVisible(false);
    documentId_ = 0;
    synchronize(shell);
}

void EditorScriptFilePanel::synchronize(EditorShell& shell)
{
    const bool scriptMode = shell.presentationState_.workspace() == "Script";
    shell.visible("script_ui_reload", scriptMode);
    if (const auto* viewport = shell.runtime().canvas().findWidget(shell.id("viewport"))) {
        if (auto* button = shell.runtime().canvas().findWidget(shell.id("script_ui_reload")))
            button->setLocalRect(ui::LayoutRect::fromOriginSize(
                viewport->desc().localRect.x + viewport->desc().localRect.width - 168.0f,
                viewport->desc().localRect.y + 3.0f,
                160.0f,
                24.0f));
    }
    (void)shell.runtime().setEnabled(
        shell.id("script_ui_reload"), shell.activeScriptDocumentId() != 0);
}

bool EditorScriptFilePanel::validBindings(EditorShell& shell) const noexcept
{
    for (std::size_t i = 0; i != Count; ++i) {
        const auto* widget = shell.runtime().canvas().findWidget(ids_[i]);
        const auto expected = i == Root || i == Confirm || i == Cancel ? ui::UiControlType::Button
                                                                       : ui::UiControlType::Label;
        if (!widget || widget->desc().controlType != expected
            || !shell.runtime().findControlObject(ids_[i])
            || (i != Root && !shell.runtime().canvas().isInSubtree(ids_[i], ids_[Root])))
            return false;
    }
    return true;
}

void EditorScriptFilePanel::closePresentation(EditorShell& shell) noexcept
{
    modal_.closePresentation(shell);
    documentId_ = 0;
}

EditorCommandOutcome EditorScriptFilePanel::close(EditorShell& shell)
{
    if (!active())
        return {true, false, EditorDiagnosticCode::None};
    if (!shell.runtime().cancelInput().ok())
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    closePresentation(shell);
    return {true, true, EditorDiagnosticCode::None};
}

void EditorScriptFilePanel::failureNotice(EditorShell& shell, bool stale) noexcept
{
    // All captions and identities are mounted up front; rejection cannot depend
    // on another string allocation while the confirmation is live.
    shell.runtime().canvas().findWidget(ids_[Status])->setVisible(false);
    shell.runtime().canvas().findWidget(ids_[Failed])->setVisible(!stale);
    shell.runtime().canvas().findWidget(ids_[Stale])->setVisible(stale);
    shell.runtime().canvas().findWidget(ids_[Confirm])->setEnabled(false);
}

EditorCommandOutcome EditorScriptFilePanel::open(
    EditorShell& shell, Mode mode, std::uint64_t documentId)
{
    if (active())
        return {true, false, EditorDiagnosticCode::None};
    const auto* document = shell.scriptDocuments_.find(documentId);
    if (!document)
        return {false, false, EditorDiagnosticCode::StaleDocumentTarget};
    if (!validBindings(shell))
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    const auto version = shell.stateVersion();
    try {
        const bool closing = mode == Mode::Close;
        const auto menuId = shell.id("menu_popup");
        // Hidden draft only. Any allocation failure here leaves the document,
        // live modal authority, focus and background flags untouched.
        const core::pmr::String captions[] = {
            core::pmr::String(
                closing ? "Close Script Without Saving?" : "Reload Script From Disk?"),
            core::pmr::String(closing ? "Close script: " : "Reload script: ")
                + core::FilePath::utf8(document->path.filename()),
            core::pmr::String(closing
                    ? "Discard this script's local edits. Its disk file and other documents are kept."
                    : "Discard local edits, load the latest disk version and clear this document's Undo history."),
            core::pmr::String("Cancel to keep editing, or explicitly discard local changes.")};
        for (std::size_t i = 0; i != 4; ++i)
            if (!shell.runtime()
                    .setValue(ids_[Title + i], ui::UiControlValue::fromText(captions[i]))
                    .ok())
                return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        auto* confirm = shell.runtime().canvas().findWidget(ids_[Confirm]);
        core::pmr::String confirmCaption(closing ? "Discard and Close" : "Discard and Reload",
            confirm->desc().text.get_allocator().resource());
        confirm->setText(std::move(confirmCaption));
        if (!modal_.open(shell, "script_ui_modal"))
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        // ModalSession publishes only after preparing its background snapshot
        // and releasing input. Everything up to requesting initial focus is fixed.
        mode_ = mode;
        documentId_ = documentId;
        confirm->setEnabled(true);
        shell.runtime().canvas().findWidget(ids_[Status])->setVisible(true);
        shell.runtime().canvas().findWidget(ids_[Failed])->setVisible(false);
        shell.runtime().canvas().findWidget(ids_[Stale])->setVisible(false);
        shell.presentationState_.closeMenu();
        if (auto* menu = shell.runtime().canvas().findWidget(menuId))
            menu->setVisible(false);
        modal_.armConfirmation();
        if (!shell.runtime().requestFocus(ids_[Cancel]).ok()) {
            failureNotice(shell, false);
            return {true,
                shell.stateVersion() == version,
                EditorDiagnosticCode::ScriptModalPresentationFailed};
        }
        return {true, shell.stateVersion() == version, EditorDiagnosticCode::None};
    } catch (const std::bad_alloc&) {
        if (active()) {
            failureNotice(shell, false);
            return {true,
                shell.stateVersion() == version,
                EditorDiagnosticCode::ScriptModalPresentationFailed};
        }
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}

EditorCommandOutcome EditorScriptFilePanel::requestClose(
    EditorShell& shell, std::uint64_t documentId)
{
    if (active())
        return {true, false, EditorDiagnosticCode::None};
    const auto result = shell.submitCommand(CloseScriptCommand{documentId, false});
    if (result.diagnostic == EditorDiagnosticCode::UnsavedChanges)
        return open(shell, Mode::Close, documentId);
    return {result.status == EditorResponseStatus::Completed, false, result.diagnostic};
}

bool EditorScriptFilePanel::handles(std::string_view source) noexcept
{
    return source.substr(0, 10) == "script_ui_";
}

EditorCommandOutcome EditorScriptFilePanel::activate(EditorShell& shell, std::string_view source)
{
    try {
        if (source == "script_ui_reload") {
            if (shell.presentationState_.workspace() != "Script")
                return {false, false, EditorDiagnosticCode::WorkspaceBusy};
            return open(shell, Mode::Reload, shell.activeScriptDocumentId());
        }
        if (source == "script_ui_close") {
            if (shell.presentationState_.workspace() != "Script")
                return {false, false, EditorDiagnosticCode::WorkspaceBusy};
            return requestClose(shell, shell.activeScriptDocumentId());
        }
        if (source == "script_ui_cancel")
            return close(shell);
        if (source == "script_ui_modal")
            return {true, false, EditorDiagnosticCode::None};
        if (source != "script_ui_confirm" || !active() || !validBindings(shell))
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        if (!shell.scriptDocuments_.find(documentId_)
            || (mode_ == Mode::Reload && shell.activeScriptDocumentId() != documentId_)
            || !modal_.confirmationValid(shell.stateVersion())
            || !shell.runtime().canvas().findWidget(ids_[Confirm])->enabled()) {
            failureNotice(shell, true);
            return {false, false, EditorDiagnosticCode::StaleDocumentTarget};
        }
        // Complete input callbacks before document IO. Once the nested command
        // commits, retiring this modal is an allocation-free flag publication.
        if (!shell.runtime().cancelInput().ok()) {
            failureNotice(shell, false);
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        }
        const auto result = mode_ == Mode::Close
            ? shell.submitCommand(CloseScriptCommand{documentId_, true})
            : shell.submitCommand(ReloadScriptCommand{documentId_, true});
        if (result.status == EditorResponseStatus::Completed)
            closePresentation(shell);
        else
            failureNotice(shell, false);
        return {result.status == EditorResponseStatus::Completed, false, result.diagnostic};
    } catch (const std::bad_alloc&) {
        if (active() && validBindings(shell))
            failureNotice(shell, false);
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}

void EditorScriptFilePanel::commandCompleted(
    std::uint64_t previous, std::uint64_t current, bool blockedInput)
{
    modal_.commandCompleted(previous, current, blockedInput);
}
} // namespace arp::editor
