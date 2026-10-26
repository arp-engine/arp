#include "editor/workspace/editor_scene_panel.h"
#include "editor/workspace/editor_shell.h"
#include "editor/workspace/editor_shell_internal.h"

#include <new>

namespace arp::editor {
void EditorScenePanel::initialize(EditorShell& shell)
{
    constexpr const char* names[] = {"scene_ui_modal",
        "scene_ui_title",
        "scene_ui_description",
        "scene_ui_description_detail",
        "scene_ui_path_label",
        "scene_ui_path_background",
        "scene_ui_path",
        "scene_ui_status",
        "scene_ui_status_detail",
        "scene_ui_submit",
        "scene_ui_confirm",
        "scene_ui_cancel",
        "scene_ui_failed",
        "scene_ui_retry",
        "scene_file_failed",
        "scene_file_retry",
        "output_panel",
        "menu_popup"};
    static_assert(std::size(names) == Count);
    for (std::size_t i = 0; i != Count; ++i)
        ids_[i] = shell.id(names[i]);
    mode_ = Mode::None;
    modal_ = EditorModalSession{};
    pending_.reset();
    taskId_ = 0;
    taskKind_ = TaskKind::None;
    feedbackPending_ = refreshFilesPending_ = false;
    if (auto* root = shell.runtime().canvas().findWidget(ids_[Root]))
        root->setVisible(false);
    clearFailure(shell);
}

bool EditorScenePanel::validBindings(EditorShell& shell) const noexcept
{
    for (std::size_t i = 0; i != Count; ++i) {
        const auto* widget = shell.runtime().canvas().findWidget(ids_[i]);
        if (!widget)
            return false;
        if (i >= Output)
            continue;
        const auto type = i == PathBackground ? ui::UiControlType::Panel
            : i == Path                       ? ui::UiControlType::TextInput
            : i == Root || i == Submit || i == Confirm || i == Cancel || i == Retry
                || i == NoticeRetry
            ? ui::UiControlType::Button
            : ui::UiControlType::Label;
        if (widget->desc().controlType != type
            || (type != ui::UiControlType::Panel && !shell.runtime().findControlObject(ids_[i]))
            || (i > Root && i < Notice
                && !shell.runtime().canvas().isInSubtree(ids_[i], ids_[Root]))
            || ((i == Notice || i == NoticeRetry)
                && !shell.runtime().canvas().isInSubtree(ids_[i], ids_[Output])))
            return false;
    }
    return true;
}

bool EditorScenePanel::setText(EditorShell& shell, Index index, std::string_view text)
{
    auto* widget = shell.runtime().canvas().findWidget(ids_[index]);
    if (widget->desc().controlType == ui::UiControlType::Button) {
        core::pmr::String prepared(text, widget->desc().text.get_allocator().resource());
        widget->setText(std::move(prepared));
        return true;
    }
    return shell.runtime()
        .setValue(ids_[index], ui::UiControlValue::fromText(core::pmr::String(text)))
        .ok();
}

bool EditorScenePanel::renderDraft(EditorShell& shell,
    Mode mode,
    std::string_view path,
    std::string_view status,
    std::string_view detail,
    bool confirming,
    bool overwrite)
{
    const bool pathVisible =
        (mode == Mode::Open || mode == Mode::SaveAs || mode == Mode::Export) && !confirming;
    if (!setText(shell,
            Title,
            confirming                    ? (overwrite ? "Replace Scene File?" : "Unsaved Scene")
                : mode == Mode::New       ? "New Scene"
                : mode == Mode::Open      ? "Open Scene"
                : mode == Mode::Duplicate ? "Duplicate Scene"
                : mode == Mode::Export    ? "Export Scene Package"
                                          : "Save Scene As")
        || !setText(shell,
            Description,
            confirming
                ? (overwrite
                          ? "Replace the existing authoring scene file at the requested path?"
                          : "Discard the current scene edits? Open scripts and project settings are retained.")
                : mode == Mode::New
                ? "Create an empty scene. Open scripts and project settings are retained."
                : mode == Mode::Open
                ? "Open an Editor authoring scene YAML file in the current project folder."
                : mode == Mode::Duplicate
                ? "Keep all current content with a new scene identity. The source file stays unchanged."
                : mode == Mode::Export
                ? "Export the current scene to a new package folder inside this project."
                : "Save the current Editor authoring scene in the current project folder.")
        || !setText(shell,
            DescriptionDetail,
            confirming ? ""
                : mode == Mode::Duplicate
                ? "The save path and old Undo history are cleared. Use Save As to keep this copy."
                : mode == Mode::Export
                ? "The parent folder must exist. The package will not be added to the project."
                : "")
        || !setText(shell,
            PathLabel,
            mode == Mode::Export ? "New project-relative package folder (512 UTF-8 bytes)"
                                 : "Project-relative .arp.yaml path (512 UTF-8 bytes)")
        || !setText(shell, Path, path)
        || !setText(shell, Status, detail::singleLine(confirming ? path : status))
        || !setText(shell, Detail, confirming ? core::pmr::String{} : detail::singleLine(detail))
        || !setText(shell,
            Submit,
            mode == Mode::New             ? "Create Scene"
                : mode == Mode::Open      ? "Open Scene"
                : mode == Mode::Duplicate ? "Create Independent Copy"
                : mode == Mode::Export    ? "Export Package"
                                          : "Save Scene")
        || !setText(shell, Cancel, mode == Mode::Export ? "Close" : "Cancel")
        || !setText(shell, Confirm, overwrite ? "Replace File" : "Discard and Continue"))
        return false;
    for (auto index : {Path, PathLabel, PathBackground})
        shell.runtime().canvas().findWidget(ids_[index])->setVisible(pathVisible);
    shell.runtime().canvas().findWidget(ids_[Submit])->setVisible(!confirming);
    shell.runtime().canvas().findWidget(ids_[Confirm])->setVisible(confirming);
    for (auto index : {Submit, Path, Confirm})
        shell.runtime().canvas().findWidget(ids_[index])->setEnabled(taskId_ == 0);
    for (auto index : {Status, Detail})
        shell.runtime().canvas().findWidget(ids_[index])->setVisible(true);
    return true;
}

bool EditorScenePanel::render(EditorShell& shell)
{
    return renderDraft(shell,
        mode_,
        path_,
        status_,
        detail_,
        pending_.has_value(),
        pending_ && std::holds_alternative<SaveSceneCommand>(*pending_));
}

void EditorScenePanel::clearFailure(EditorShell& shell) noexcept
{
    presentationFailed_ = false;
    for (auto index : {Failed, Retry, Notice, NoticeRetry})
        if (auto* widget = shell.runtime().canvas().findWidget(ids_[index]))
            widget->setVisible(false);
    ++shell.feedbackVersion_;
}

void EditorScenePanel::failureNotice(EditorShell& shell) noexcept
{
    presentationFailed_ = true;
    for (auto index : {Failed, Retry})
        if (auto* widget = shell.runtime().canvas().findWidget(ids_[index]))
            widget->setVisible(active());
    for (auto index : {Notice, NoticeRetry})
        if (auto* widget = shell.runtime().canvas().findWidget(ids_[index]))
            widget->setVisible(!active());
    if (active()) {
        for (auto index : {Status, Detail})
            shell.runtime().canvas().findWidget(ids_[index])->setVisible(false);
        for (auto index : {Path, Submit, Confirm})
            shell.runtime().canvas().findWidget(ids_[index])->setEnabled(false);
    } else {
        shell.presentationState_.openOutput();
        shell.runtime().canvas().findWidget(ids_[Output])->setVisible(true);
    }
    ++shell.feedbackVersion_;
}

bool EditorScenePanel::retryDisplay(EditorShell& shell) noexcept
{
    try {
        if (!validBindings(shell))
            return false;
        if (active() && !render(shell)) {
            failureNotice(shell);
            return false;
        }
        if (refreshFilesPending_) {
            shell.refreshFileSystem();
            refreshFilesPending_ = false;
        }
        if (feedbackPending_) {
            shell.feedback(detail::singleLine(status_ + " " + detail_));
            feedbackPending_ = false;
        }
        clearFailure(shell);
        return true;
    } catch (const std::bad_alloc&) {
        failureNotice(shell);
        return false;
    }
}

EditorCommandOutcome EditorScenePanel::show(EditorShell& shell, Mode mode)
{
    if (active())
        return {true, false, EditorDiagnosticCode::None};
    if (taskId_)
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    const auto gate = shell.sceneReplacementGate(true);
    if (!gate.success)
        return gate;
    const auto* terrain =
        shell.runtime().canvas().findWidget(shell.terrainBrowserWidget("terrain_browser_panel"));
    if (terrain && terrain->visible())
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    if (!validBindings(shell))
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    const auto version = shell.stateVersion();
    try {
        core::pmr::String path(mode == Mode::SaveAs ? shell.activeScenePath_ : core::pmr::String{});
        core::pmr::String status;
        if (path.size() > 512u) {
            path.clear();
            status =
                "The existing path exceeds the UI limit. Enter a path of at most 512 UTF-8 bytes.";
        }
        if (!renderDraft(shell, mode, path, status, {}, false, false)
            || !modal_.open(shell, "scene_ui_modal"))
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        // Hidden preparation and input release succeeded. Publish authority last.
        path_.swap(path);
        status_.swap(status);
        detail_.clear();
        pending_.reset();
        mode_ = mode;
        feedbackPending_ = false;
        clearFailure(shell);
        shell.presentationState_.closeMenu();
        shell.runtime().canvas().findWidget(ids_[Menu])->setVisible(false);
        if (!shell.runtime()
                .requestFocus(ids_[mode == Mode::New || mode == Mode::Duplicate ? Cancel : Path])
                .ok()) {
            failureNotice(shell);
            return {true,
                shell.stateVersion() == version,
                EditorDiagnosticCode::SceneModalPresentationFailed};
        }
        return {true, shell.stateVersion() == version, EditorDiagnosticCode::None};
    } catch (const std::bad_alloc&) {
        if (active()) {
            failureNotice(shell);
            return {true,
                shell.stateVersion() == version,
                EditorDiagnosticCode::SceneModalPresentationFailed};
        }
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}

void EditorScenePanel::closePresentation(EditorShell& shell) noexcept
{
    modal_.closePresentation(shell);
    mode_ = Mode::None;
    pending_.reset();
    clearFailure(shell);
}

EditorCommandOutcome EditorScenePanel::hide(EditorShell& shell)
{
    if (!active())
        return {true, false, EditorDiagnosticCode::None};
    if (!shell.runtime().cancelInput().ok())
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    closePresentation(shell);
    return {true, true, EditorDiagnosticCode::None};
}
} // namespace arp::editor
