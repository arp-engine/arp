#include "core/filesystem_path.h"
#include "core/string_utils.h"
#include "editor/workspace/editor_layout.h"
#include "editor/workspace/editor_shell.h"

#include <algorithm>
#include <cmath>

namespace arp::editor {
using namespace layout;

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
    scriptDocuments_.setActiveIndex(index);
    presentationState_.setWorkspace("Script");
    scriptFocused_ = true;
    scriptDraggingSelection_ = false;
    updateWorkspaceChrome();
    clampScriptScroll();
}

bool EditorShell::closeScriptDocument(std::size_t index)
{
    if (index >= scriptDocuments_.size())
        return false;
    if (scriptDocuments_.documents()[index].dirty()) {
        feedback("Save " + core::FilePath::utf8(scriptDocuments_.documents()[index].path.filename())
            + " before closing");
        return false;
    }
    const bool closingActive = index == scriptDocuments_.activeIndex();
    scriptDocuments_.erase(index);
    if (scriptDocuments_.size() == 0) {
        emptyScriptText_.clear();
        scriptFocused_ = false;
        presentationState_.setWorkspace("Game");
        updateWorkspaceChrome();
    } else if (closingActive) {
        activateScriptDocument(std::min(index, scriptDocuments_.size() - 1));
    }
    return true;
}

bool EditorShell::openScriptFile(const std::filesystem::path& path)
{
    const auto result = scriptDocuments_.openFile(projectBrowser_.root(), path);
    using Code = ScriptDocumentSession::FileResultCode;
    if (result.code == Code::Opened || result.code == Code::AlreadyOpen) {
        activateScriptDocument(result.documentIndex);
        feedback("Opened " + core::FilePath::utf8(scriptPath().filename()));
        return true;
    }
    if (result.code == Code::TooLarge)
        feedback("Script open failed: file exceeds the 2 MiB editor limit");
    else if (result.code == Code::ReadFailed)
        feedback("Script open failed: cannot read " + core::FilePath::utf8(path.filename()));
    else
        feedback("Script open failed: choose an .arp file inside res://");
    return false;
}

bool EditorShell::saveScript()
{
    const auto result = scriptDocuments_.saveActiveFile(projectBrowser_.root());
    using Code = ScriptDocumentSession::FileResultCode;
    if (result.code == Code::Saved) {
        feedback("Saved " + core::FilePath::utf8(scriptPath().filename()));
        return true;
    }
    if (result.code == Code::NoActiveDocument)
        feedback("Script save failed: no project script is open");
    else if (result.code == Code::InvalidPath)
        feedback("Script save failed: script path is outside res://");
    else
        feedback(
            "Script save failed: cannot write " + core::FilePath::utf8(scriptPath().filename()));
    return false;
}

void EditorShell::replaceScriptRange(
    std::size_t start, std::size_t length, const char* value, std::size_t size)
{
    const auto result = scriptDocuments_.replaceActiveRange(
        start, length, std::string_view(value, size), ScriptDocumentSession::maxScriptBytes);
    if (result != ScriptDocumentSession::EditResult::Applied)
        return;
    clampScriptScroll();
}

void EditorShell::insertScriptText(const char* value, std::size_t size)
{
    const auto begin = scriptSelectionStart();
    const auto length = scriptSelectionEnd() - begin;
    replaceScriptRange(begin, length, value, size);
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
    const bool changed = undo ? scriptDocuments_.undoActive() : scriptDocuments_.redoActive();
    if (!changed)
        return {true, false, EditorDiagnosticCode::None};
    clampScriptScroll();
    return {true, true, EditorDiagnosticCode::None};
}

void EditorShell::moveScriptCursor(std::size_t cursor, bool extendSelection)
{
    cursor = std::min(cursor, scriptTextView().size());
    const auto anchor = extendSelection ? scriptDocuments_.activeAnchor() : cursor;
    scriptDocuments_.setActiveSelection(cursor, anchor);
}

void EditorShell::clampScriptScroll()
{
    const auto layout = runtime().canvas().resolveLayout();
    const auto* viewport = layout.findWidget(id("viewport"));
    if (!viewport) {
        scriptDocuments_.setActiveScrollLine(0);
        return;
    }
    const auto visible =
        static_cast<std::size_t>((viewport->rect.height - 42.0f) / kCodeLineHeight);
    const auto total = scriptLineCount(scriptTextView());
    const auto maxOffset = total > visible ? total - visible : 0u;
    scriptDocuments_.setActiveScrollLine(std::min(scriptDocuments_.activeScrollLine(), maxOffset));
}

std::size_t EditorShell::scriptOffsetAt(float x, float y) const
{
    const auto layout = runtime().canvas().resolveLayout();
    const auto* viewport = layout.findWidget(id("viewport"));
    if (!viewport)
        return 0;
    const float codeTop = viewport->rect.y + kScriptEditorHeaderHeight + 4.0f;
    const auto line = scriptDocuments_.activeScrollLine()
        + static_cast<std::size_t>(std::max(0.0f, std::floor((y - codeTop) / kCodeLineHeight)));
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
            std::max(0.0f, x - (viewport->rect.x + 67.0f)),
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
        target, shiftHeld_ ? scriptDocuments_.activeAnchor() : target);
}

} // namespace arp::editor
