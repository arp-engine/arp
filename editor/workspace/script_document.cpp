#include "core/io/filesystem_path.h"
#include "core/text/string_utils.h"
#include "core/text/utf8.h"
#include "editor/workspace/editor_layout.h"
#include "editor/workspace/editor_shell.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace arp::editor {
using namespace layout;

bool EditorShell::initializeScriptPresentation()
{
    pendingScriptFeedback_.clear();
    scriptChromePending_ = false;
    scriptPresentationNotice_ = id("script_presentation_failed");
    scriptPresentationRetry_ = id("script_presentation_retry");
    scriptPresentationStatus_ = id("output_status");
    scriptPresentationOutput_ = id("output_panel");
    for (const auto widget : {scriptPresentationNotice_,
             scriptPresentationRetry_,
             scriptPresentationStatus_,
             scriptPresentationOutput_})
        if (!runtime().canvas().findWidget(widget))
            return false;
    setScriptPresentationFailed(false);
    return true;
}

void EditorShell::setScriptPresentationFailed(bool failed) noexcept
{
    scriptPresentationFailed_ = failed;
    for (const auto widget : {scriptPresentationNotice_, scriptPresentationRetry_})
        if (auto* value = runtime().canvas().findWidget(widget))
            value->setVisible(failed);
    if (auto* value = runtime().canvas().findWidget(scriptPresentationStatus_))
        value->setVisible(!failed);
    if (failed) {
        presentationState_.openOutput();
        if (auto* value = runtime().canvas().findWidget(scriptPresentationOutput_))
            value->setVisible(true);
    }
    ++feedbackVersion_;
}

bool EditorShell::publishScriptPresentation(core::pmr::String message, bool chrome) noexcept
{
    // All callers prepare the owned message before committing document state.
    // Empty messages request a chrome refresh without replacing newer feedback.
    if (!message.empty())
        pendingScriptFeedback_.swap(message);
    scriptChromePending_ |= chrome;
    return retryScriptPresentation();
}

bool EditorShell::retryScriptPresentation() noexcept
{
    try {
        if (scriptChromePending_) {
            updateWorkspaceChrome();
            clampScriptScroll();
            scriptChromePending_ = false;
        }
        if (!pendingScriptFeedback_.empty()) {
            const auto result = runtime().setValue(scriptPresentationStatus_,
                ui::UiControlValue::fromText(pendingScriptFeedback_),
                ui::UiNotifyPolicy::Silent);
            if (!result.ok()) {
                setScriptPresentationFailed(true);
                return false;
            }
            pendingScriptFeedback_.clear();
        }
        setScriptPresentationFailed(false);
        return true;
    } catch (const std::bad_alloc&) {
        // Fixed Widget identities and captions make failure isolation no-alloc.
        setScriptPresentationFailed(true);
        return false;
    }
}

bool EditorShell::isArpPath(const std::filesystem::path& path)
{
    auto extension = core::FilePath::utf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](char c) {
        return core::toLowerAscii(c);
    });
    return extension == ".arp";
}

std::size_t EditorShell::scriptLineCount(const core::pmr::String& text)
{
    return 1u + static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
}

const core::pmr::String& EditorShell::scriptTextView() const
{
    const auto* document = scriptDocuments_.active();
    return document ? document->text : emptyScriptText_;
}

core::pmr::String& EditorShell::scriptTextView()
{
    auto* document = scriptDocuments_.active();
    return document ? document->text : emptyScriptText_;
}

void EditorShell::activateScriptDocument(std::size_t index)
{
    if (index >= scriptDocuments_.size())
        return;
    core::pmr::String workspace("Script", presentationState_.workspace_.get_allocator());
    scriptDocuments_.setActiveIndex(index);
    presentationState_.workspace_.swap(workspace);
    scriptFocused_ = true;
    cancelScriptPointerCapture();
    (void)publishScriptPresentation(core::pmr::String(&core::defaultCpuMemoryResource()), true);
}

EditorCommandOutcome EditorShell::closeScriptDocument(std::uint64_t documentId, bool discardChanges)
{
    const auto& documents = scriptDocuments_.documents();
    const auto found =
        std::find_if(documents.begin(), documents.end(), [documentId](const auto& document) {
            return documentId != 0 && document.id == documentId;
        });
    if (found == documents.end())
        return {false, false, EditorDiagnosticCode::StaleDocumentTarget};
    try {
        if (found->dirty() && !discardChanges) {
            (void)publishScriptPresentation(core::pmr::String(
                "EDITOR-SCRIPT-UNSAVED: save or explicitly discard this script before closing.",
                &core::defaultCpuMemoryResource()));
            return {false, false, EditorDiagnosticCode::UnsavedChanges};
        }
        core::pmr::String message(
            "Closed script document; its disk file was kept.", &core::defaultCpuMemoryResource());
        core::pmr::String game("Game", presentationState_.workspace_.get_allocator());
        const auto index = static_cast<std::size_t>(found - documents.begin());
        const bool closingActive = index == scriptDocuments_.activeIndex();
        const bool scriptWorkspace = presentationState_.workspace() == "Script";
        scriptDocuments_.erase(index);
        if (closingActive)
            cancelScriptPointerCapture();
        if (scriptDocuments_.size() == 0) {
            emptyScriptText_.clear();
            scriptFocused_ = false;
            scriptDraggingSelection_ = false;
            if (scriptWorkspace)
                presentationState_.workspace_.swap(game);
        } else if (closingActive) {
            const auto next = std::min(index, scriptDocuments_.size() - 1);
            scriptDocuments_.setActiveIndex(next);
            if (scriptWorkspace) {
                scriptFocused_ = true;
                scriptDraggingSelection_ = false;
            }
        }
        const bool ready = publishScriptPresentation(std::move(message), true);
        return {true,
            true,
            ready ? EditorDiagnosticCode::None : EditorDiagnosticCode::ScriptPresentationFailed};
    } catch (const std::bad_alloc&) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}

bool EditorShell::openScriptFile(const std::filesystem::path& path)
{
    if (projectBrowser_.sourceSnapshot()) {
        return submitCommand(OpenScriptCommand{core::FilePath::utf8(
                                 path.lexically_relative(projectBrowser_.root()))})
                   .status
            == EditorResponseStatus::Completed;
    }
    auto message = "Opened " + core::FilePath::utf8(path.filename());
    core::pmr::String workspace("Script", presentationState_.workspace_.get_allocator());
    const auto result = scriptDocuments_.openFile(projectBrowser_.root(), path);
    using Code = ScriptDocumentSession::FileResultCode;
    if (result.code == Code::Opened || result.code == Code::AlreadyOpen) {
        scriptDocuments_.setActiveIndex(result.documentIndex);
        presentationState_.workspace_.swap(workspace);
        scriptFocused_ = true;
        cancelScriptPointerCapture();
        (void)publishScriptPresentation(std::move(message), true);
        return true;
    }
    if (result.code == Code::TooLarge)
        feedback("Script open failed: file exceeds the 2 MiB editor limit");
    else if (result.code == Code::IdentityExhausted)
        feedback("EDITOR-SCRIPT-IDENTITY-EXHAUSTED: no document identity remains; session kept.");
    else if (result.code == Code::ReadFailed)
        feedback("Script open failed: cannot read " + core::FilePath::utf8(path.filename()));
    else
        feedback("Script open failed: choose an .arp file inside res://");
    return false;
}

void EditorShell::appendScriptSnapshot(EditorStateSnapshot& snapshot) const
{
    snapshot.workspace = presentationState_.workspace();
    snapshot.scene = presentationState_.scene();
    snapshot.selection = presentationState_.selection();
    snapshot.activeDocumentId = scriptDocuments_.activeId();
    snapshot.openDocumentCount = static_cast<std::uint32_t>(
        std::min<std::size_t>(scriptDocuments_.size(), std::numeric_limits<std::uint32_t>::max()));
    snapshot.hasActiveDocument = scriptDocuments_.active() != nullptr;
    snapshot.scriptDirty = scriptDocuments_.active() && scriptDocuments_.active()->dirty();
    snapshot.scriptPresentationFailed = scriptPresentationFailed_;
    if (const auto* document = scriptDocuments_.active()) {
        snapshot.scriptText = document->text;
        const auto relative = document->path.lexically_relative(projectBrowser_.root());
        if (!relative.empty() && !relative.is_absolute())
            snapshot.scriptRelativePath = core::FilePath::utf8(relative);
    }
}

bool EditorShell::saveScript()
{
    return submitCommand(SaveScriptCommand{activeScriptDocumentId()}).status
        == EditorResponseStatus::Completed;
}

EditorCommandOutcome EditorShell::saveScriptDocument()
{
    try {
        auto message = "Saved " + core::FilePath::utf8(scriptPath().filename());
        const bool wasDirty = scriptDirty();
        const auto result = scriptDocuments_.saveActiveFile(projectBrowser_.root());
        using Code = ScriptDocumentSession::FileResultCode;
        if (result.code == Code::Saved) {
            const bool ready = publishScriptPresentation(std::move(message));
            return {true,
                wasDirty,
                ready ? EditorDiagnosticCode::None
                      : EditorDiagnosticCode::ScriptPresentationFailed};
        }
        if (result.code == Code::ExternalConflict)
            message =
                "EDITOR-SCRIPT-EXTERNAL-CONFLICT: disk changed/unreadable; edits kept. Use Reload Disk to discard.";
        else if (result.code == Code::TooLarge)
            message = "EDITOR-SCRIPT-BYTE-LIMIT: encoded file exceeds 2 MiB; original file kept.";
        else
            message = "EDITOR-SCRIPT-SAVE-FAILED: original file and local edits kept.";
        (void)publishScriptPresentation(std::move(message));
        presentationState_.openOutput();
        if (auto* output = runtime().canvas().findWidget(scriptPresentationOutput_))
            output->setVisible(true);
        return {false,
            false,
            result.code == Code::ExternalConflict
                ? EditorDiagnosticCode::ScriptExternalConflict
                : EditorDiagnosticCode::ScriptFileOperationFailed};
    } catch (const std::bad_alloc&) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}

EditorCommandOutcome EditorShell::reloadScriptDocument(bool discardChanges)
{
    if (projectBrowser_.sourceSnapshot())
        return reloadSourceScript(discardChanges);
    try {
        core::pmr::String message(
            "Reloaded script from disk; local history cleared.", &core::defaultCpuMemoryResource());
        const auto rows = scriptVisibleRows();
        const auto result =
            scriptDocuments_.reloadActiveFile(projectBrowser_.root(), discardChanges);
        using Code = ScriptDocumentSession::FileResultCode;
        if (result.code == Code::Opened) {
            clampScriptScroll(rows);
            const bool ready = publishScriptPresentation(std::move(message));
            return {true,
                true,
                ready ? EditorDiagnosticCode::None
                      : EditorDiagnosticCode::ScriptPresentationFailed};
        }
        if (result.code == Code::UnsavedChanges) {
            message = "EDITOR-SCRIPT-UNSAVED: explicit discard is required to reload.";
            (void)publishScriptPresentation(std::move(message));
            return {false, false, EditorDiagnosticCode::UnsavedChanges};
        }
        message = "EDITOR-SCRIPT-RELOAD-FAILED: disk could not be loaded; local edits kept.";
        (void)publishScriptPresentation(std::move(message));
        presentationState_.openOutput();
        if (auto* output = runtime().canvas().findWidget(scriptPresentationOutput_))
            output->setVisible(true);
        return {false, false, EditorDiagnosticCode::ScriptFileOperationFailed};
    } catch (const std::bad_alloc&) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}

EditorCommandOutcome EditorShell::replaceScriptRange(
    std::size_t start, std::size_t length, const char* value, std::size_t size)
{
    const auto* document = scriptDocuments_.active();
    if (!document || size > ScriptDocumentSession::maxScriptBytes
        || core::Utf8Decoder::validate(std::string_view(value, size)))
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    start = std::min(start, document->text.size());
    length = std::min(length, document->text.size() - start);
    if (document->text.size() - length > ScriptDocumentSession::maxScriptBytes - size)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    if (length == size && document->text.compare(start, length, value, size) == 0)
        return {true, false, EditorDiagnosticCode::None};
    const auto rows = scriptVisibleRows();
    const auto result = scriptDocuments_.replaceActiveRange(
        start, length, std::string_view(value, size), ScriptDocumentSession::maxScriptBytes);
    if (result == ScriptDocumentSession::EditResult::Rejected)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    if (result == ScriptDocumentSession::EditResult::Unchanged)
        return {true, false, EditorDiagnosticCode::None};
    clampScriptScroll(rows);
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::replaceScriptText(const ReplaceScriptTextCommand& command)
{
    const auto* document = scriptDocuments_.active();
    if (!document || document->id != command.documentId)
        return {false, false, EditorDiagnosticCode::StaleDocumentTarget};
    if (command.text.size() > ScriptDocumentSession::maxScriptBytes)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    if (document->text == command.text)
        return {true, false, EditorDiagnosticCode::None};
    try {
        const auto rows = scriptVisibleRows();
        const auto edit = scriptDocuments_.replaceActiveRange(
            0, document->text.size(), command.text, ScriptDocumentSession::maxScriptBytes);
        if (edit != ScriptDocumentSession::EditResult::Applied)
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        clampScriptScroll(rows);
        return {true, true, EditorDiagnosticCode::None};
    } catch (const std::bad_alloc&) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}

EditorCommandOutcome EditorShell::insertScriptText(const char* value, std::size_t size)
{
    const auto begin = scriptSelectionStart();
    const auto length = scriptSelectionEnd() - begin;
    return replaceScriptRange(begin, length, value, size);
}

std::uint64_t EditorShell::activeScriptDocumentId() const
{
    return scriptDocuments_.activeId();
}

EditorCommandOutcome EditorShell::applyScriptHistory(std::uint64_t documentId, bool undo)
{
    if (documentId == 0)
        return {true, false, EditorDiagnosticCode::None};
    if (scriptDocuments_.activeId() != documentId)
        return {false, false, EditorDiagnosticCode::StaleDocumentTarget};
    const auto* document = scriptDocuments_.active();
    if (undo ? document->undo.empty() : document->redo.empty())
        return {true, false, EditorDiagnosticCode::None};
    try {
        const auto rows = scriptVisibleRows();
        const bool changed = undo ? scriptDocuments_.undoActive() : scriptDocuments_.redoActive();
        if (!changed)
            return {true, false, EditorDiagnosticCode::None};
        clampScriptScroll(rows);
        return {true, true, EditorDiagnosticCode::None};
    } catch (const std::bad_alloc&) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}

void EditorShell::moveScriptCursor(std::size_t cursor, bool extendSelection)
{
    cursor = std::min(cursor, scriptTextView().size());
    const auto anchor = extendSelection ? scriptDocuments_.activeAnchor() : cursor;
    scriptDocuments_.setActiveSelection(cursor, anchor);
}

void EditorShell::clampScriptScroll()
{
    clampScriptScroll(scriptVisibleRows());
}

std::size_t EditorShell::scriptVisibleRows() const
{
    const auto layout = runtime().canvas().resolveLayout();
    const auto* viewport = layout.findWidget(id("viewport"));
    if (!viewport)
        return 0;
    return static_cast<std::size_t>(
        std::max(0.0f, viewport->rect.height - 42.0f) / kCodeLineHeight);
}

void EditorShell::clampScriptScroll(std::size_t visible) noexcept
{
    if (!visible) {
        scriptDocuments_.setActiveScrollLine(0);
        return;
    }
    const auto total = scriptLineCount(scriptTextView());
    const auto maxOffset = total > visible ? total - visible : 0u;
    scriptDocuments_.setActiveScrollLine(std::min(scriptDocuments_.activeScrollLine(), maxOffset));
}

std::size_t EditorShell::scriptOffsetAt(
    float x, float y, const ui::LayoutRect& viewport) const noexcept
{
    const float codeTop = viewport.y + kScriptEditorHeaderHeight + 4.0f;
    const auto total = scriptLineCount(scriptTextView());
    const auto first = std::min(scriptDocuments_.activeScrollLine(), total);
    const double row = std::max(0.0, std::floor((double(y) - double(codeTop)) / kCodeLineHeight));
    if (row >= double(total - first))
        return scriptTextView().size();
    const auto line = first + static_cast<std::size_t>(row);
    std::size_t offset = 0, current = 0;
    while (current < line && offset < scriptTextView().size()) {
        const auto newline = scriptTextView().find('\n', offset);
        if (newline == core::pmr::String::npos)
            return scriptTextView().size();
        offset = newline + 1;
        ++current;
    }
    const auto end = scriptTextView().find('\n', offset);
    const auto lineEnd = end == core::pmr::String::npos ? scriptTextView().size() : end;
    const std::string_view codeLine(scriptTextView().data() + offset, lineEnd - offset);
    return offset
        + utf8OffsetAtAdvance(codeLine,
            std::max(0.0f, x - (viewport.x + 67.0f)),
            codeColumnWidth_,
            codeCjkColumnWidth_);
}

void EditorShell::moveScriptCursorVertical(int direction)
{
    const auto cursor = scriptDocuments_.activeCursor();
    const auto currentStartPos = scriptTextView().rfind('\n', cursor == 0 ? 0 : cursor - 1);
    const auto currentStart = currentStartPos == core::pmr::String::npos ? 0 : currentStartPos + 1;
    const std::string_view currentLine(
        scriptTextView().data() + currentStart, cursor - currentStart);
    const float visualAdvance =
        utf8AdvancePixels(currentLine, currentLine.size(), codeColumnWidth_, codeCjkColumnWidth_);
    std::size_t targetStart = 0, targetEnd = 0;
    if (direction < 0) {
        if (currentStart == 0)
            return;
        targetEnd = currentStart - 1;
        const auto previousStart = scriptTextView().rfind('\n', targetEnd == 0 ? 0 : targetEnd - 1);
        targetStart = previousStart == core::pmr::String::npos ? 0 : previousStart + 1;
    } else {
        const auto currentEnd = scriptTextView().find('\n', cursor);
        if (currentEnd == core::pmr::String::npos)
            return;
        targetStart = currentEnd + 1;
        targetEnd = scriptTextView().find('\n', targetStart);
        if (targetEnd == core::pmr::String::npos)
            targetEnd = scriptTextView().size();
    }
    const std::string_view targetLine(
        scriptTextView().data() + targetStart, targetEnd - targetStart);
    const auto target = targetStart
        + utf8OffsetAtAdvance(targetLine, visualAdvance, codeColumnWidth_, codeCjkColumnWidth_);
    scriptDocuments_.setActiveSelection(
        target, modifiers_.shift() ? scriptDocuments_.activeAnchor() : target);
}

} // namespace arp::editor
