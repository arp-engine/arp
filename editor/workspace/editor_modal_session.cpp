#include "editor/workspace/editor_modal_session.h"

#include "editor/workspace/editor_shell.h"

namespace arp::editor {

bool EditorModalSession::open(EditorShell& shell, const char* root)
{
    if (root_.valid())
        return true;
    const auto rootId = shell.id(root);
    if (!shell.runtime().canvas().findWidget(rootId))
        return false;
    const auto authoringRoot = shell.id("root");
    const auto canvasRoot = shell.runtime().canvas().rootId();
    const auto traversal = shell.runtime().canvas().traversal();
    decltype(background_) prepared;
    prepared.reserve(traversal.size());
    // Complete the bounded background snapshot before changing any UI flags.
    for (auto child : traversal) {
        const auto* widget = shell.runtime().canvas().findWidget(child);
        if (!widget || child == authoringRoot || child == canvasRoot || child == rootId
            || (widget->parentId() != authoringRoot && widget->parentId() != canvasRoot))
            continue;
        prepared.push_back({child, widget->enabled()});
    }
    EditorInputCommand cancel;
    cancel.kind = EditorInputKind::FocusLost;
    (void)shell.submitCommand(std::move(cancel));
    if (!shell.runtime().cancelInput().ok())
        return false;
    // No focused/captured input remains. Flag publication needs no callbacks.
    background_.swap(prepared);
    root_ = rootId;
    if (auto* widget = shell.runtime().canvas().findWidget(root_))
        widget->setVisible(true);
    for (const auto& [id, enabled] : background_)
        if (auto* widget = shell.runtime().canvas().findWidget(id))
            widget->setEnabled(false);
    return true;
}

void EditorModalSession::close(EditorShell& shell)
{
    shell.runtime().cancelInput();
    closePresentation(shell);
}

void EditorModalSession::closePresentation(EditorShell& shell) noexcept
{
    if (auto* widget = shell.runtime().canvas().findWidget(root_))
        widget->setVisible(false);
    root_ = {};
    for (const auto& [id, enabled] : background_)
        if (auto* widget = shell.runtime().canvas().findWidget(id))
            widget->setEnabled(enabled);
    background_.clear();
    confirmationPending_ = false;
    confirmationVersion_ = 0;
}

void EditorModalSession::armConfirmation()
{
    // The outer UI command commits its presentation version after the callback.
    confirmationPending_ = true;
}

bool EditorModalSession::confirmationValid(std::uint64_t stateVersion) const
{
    return !confirmationPending_ && confirmationVersion_ == stateVersion;
}

void EditorModalSession::commandCompleted(
    std::uint64_t previous, std::uint64_t current, bool blockedInput)
{
    if (confirmationPending_) {
        confirmationVersion_ = current;
        confirmationPending_ = false;
    } else if (root_.valid() && blockedInput && confirmationVersion_ == previous) {
        confirmationVersion_ = current;
    }
}

} // namespace arp::editor
