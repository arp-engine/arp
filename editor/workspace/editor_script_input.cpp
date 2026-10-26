#include <cwchar>
#include <limits>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include "core/text/utf8_cursor.h"
#include "editor/workspace/editor_shell.h"

#include <cstring>
#include <new>

namespace arp::editor {
namespace {
#if defined(_WIN32)
// Reading text can allocate. Release the OS lock and clipboard even when that
// preparation throws, before the input command reports its failure.
class ClipboardReadScope final {
public:
    explicit ClipboardReadScope(HGLOBAL data) noexcept : data_(data) {}

    ~ClipboardReadScope()
    {
        GlobalUnlock(data_);
        CloseClipboard();
    }

    ClipboardReadScope(const ClipboardReadScope&) = delete;
    ClipboardReadScope& operator=(const ClipboardReadScope&) = delete;

private:
    HGLOBAL data_;
};
#endif

bool writeSystemClipboardText(std::string_view utf8)
{
#if defined(_WIN32)
    if (utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return false;
    const int wideLength = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (wideLength < 0 || (!utf8.empty() && wideLength == 0))
        return false;
    core::pmr::Vector<wchar_t> wide(
        static_cast<std::size_t>(wideLength), L'\0', &core::defaultCpuMemoryResource());
    if (wideLength > 0
        && MultiByteToWideChar(CP_UTF8,
               MB_ERR_INVALID_CHARS,
               utf8.data(),
               static_cast<int>(utf8.size()),
               wide.data(),
               wideLength)
            != wideLength)
        return false;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
    if (!memory)
        return false;
    auto* target = static_cast<wchar_t*>(GlobalLock(memory));
    if (!target) {
        GlobalFree(memory);
        return false;
    }
    if (!wide.empty())
        std::memcpy(target, wide.data(), wide.size() * sizeof(wchar_t));
    target[wide.size()] = L'\0';
    GlobalUnlock(memory);
    if (!OpenClipboard(nullptr)) {
        GlobalFree(memory);
        return false;
    }
    const bool ok =
        EmptyClipboard() != FALSE && SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    CloseClipboard();
    if (!ok)
        GlobalFree(memory);
    return ok;
#else
    (void)utf8;
    return false;
#endif
}

bool readSystemClipboardText(core::pmr::String& utf8)
{
#if defined(_WIN32)
    if (!OpenClipboard(nullptr))
        return false;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (!data) {
        CloseClipboard();
        return false;
    }
    const auto* source = static_cast<const wchar_t*>(GlobalLock(data));
    if (!source) {
        CloseClipboard();
        return false;
    }
    const ClipboardReadScope cleanup(data);
    const auto wideLength = std::wcslen(source);
    bool ok = wideLength <= static_cast<std::size_t>(std::numeric_limits<int>::max());
    int bytes = 0;
    if (ok && wideLength > 0) {
        bytes = WideCharToMultiByte(CP_UTF8,
            WC_ERR_INVALID_CHARS,
            source,
            static_cast<int>(wideLength),
            nullptr,
            0,
            nullptr,
            nullptr);
        ok = bytes > 0;
    }
    if (ok) {
        utf8.resize(static_cast<std::size_t>(bytes));
        if (bytes > 0)
            ok = WideCharToMultiByte(CP_UTF8,
                     WC_ERR_INVALID_CHARS,
                     source,
                     static_cast<int>(wideLength),
                     utf8.data(),
                     bytes,
                     nullptr,
                     nullptr)
                == bytes;
        if (!ok)
            utf8.clear();
    }
    return ok;
#else
    (void)utf8;
    return false;
#endif
}

} // namespace

EditorScriptInputState EditorShell::captureScriptInputState() const noexcept
{
    return {scriptDocuments_.stateTicket(),
        stateVersion(),
        modifiers_,
        scriptFocused_,
        scriptDraggingSelection_,
        scriptDraggingScrollbar_,
        lastScriptClickOffset_,
        lastEditorClickTime_,
        scriptScrollbarGrabOffset_};
}

EditorCommandOutcome EditorShell::scriptInputOutcome(
    const EditorScriptInputState& before) const noexcept
{
    // A nested document command already owns its version increment.
    if (stateVersion() != before.version)
        return {true, false, EditorDiagnosticCode::None};
    const bool changed = !before.document.isCurrent() || !(before.modifiers == modifiers_)
        || before.focused != scriptFocused_ || before.selectionCaptured != scriptDraggingSelection_
        || before.scrollCaptured != scriptDraggingScrollbar_
        || before.clickOffset != lastScriptClickOffset_ || before.clickTime != lastEditorClickTime_
        || before.scrollGrabOffset != scriptScrollbarGrabOffset_;
    return {true, changed, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::applyScriptKeyboardInput(const EditorInputCommand& event)
{
    using Kind = EditorInputKind;
    const auto before = captureScriptInputState();
    // The panel owns preparation, publication and the nested close receipt.
    if (scriptEditorCapturesKeyboard() && event.kind == Kind::KeyDown && modifiers_.control()
        && event.key == EditorInputKey::W) {
        return scriptFilePanel_.requestClose(*this, activeScriptDocumentId());
    }
    try {
        if (!scriptEditorCapturesKeyboard())
            return scriptInputOutcome(before);
        if (event.kind == Kind::Text) {
            if (!modifiers_.control() && !event.text.empty())
                return insertScriptText(event.text.data(), event.text.size());
            return scriptInputOutcome(before);
        }
        if (event.kind != Kind::KeyDown)
            return scriptInputOutcome(before);
        if (modifiers_.control() && event.key == EditorInputKey::S) {
            const auto response = submitCommand(
                SaveScriptCommand{activeScriptDocumentId()}, EditorCommandOrigin::PlatformInput);
            return {response.status == EditorResponseStatus::Completed, false, response.diagnostic};
        }
        if (modifiers_.control() && event.key == EditorInputKey::Z && !modifiers_.shift()) {
            const auto response = submitCommand(
                UndoScriptCommand{activeScriptDocumentId()}, EditorCommandOrigin::PlatformInput);
            return {response.status == EditorResponseStatus::Completed, false, response.diagnostic};
        }
        if (modifiers_.control()
            && (event.key == EditorInputKey::Y
                || (modifiers_.shift() && event.key == EditorInputKey::Z))) {
            const auto response = submitCommand(
                RedoScriptCommand{activeScriptDocumentId()}, EditorCommandOrigin::PlatformInput);
            return {response.status == EditorResponseStatus::Completed, false, response.diagnostic};
        }
        if (modifiers_.control() && event.key == EditorInputKey::A) {
            scriptDocuments_.setActiveSelection(scriptTextView().size(), 0);
            return scriptInputOutcome(before);
        }
        if (modifiers_.control() && event.key == EditorInputKey::C) {
            if (scriptSelectionStart() != scriptSelectionEnd())
                writeSystemClipboardText(
                    std::string_view(scriptTextView().data() + scriptSelectionStart(),
                        scriptSelectionEnd() - scriptSelectionStart()));
            return scriptInputOutcome(before);
        }
        if (modifiers_.control() && event.key == EditorInputKey::X) {
            if (scriptSelectionStart() != scriptSelectionEnd()
                && writeSystemClipboardText(
                    std::string_view(scriptTextView().data() + scriptSelectionStart(),
                        scriptSelectionEnd() - scriptSelectionStart())))
                return replaceScriptRange(
                    scriptSelectionStart(), scriptSelectionEnd() - scriptSelectionStart(), "", 0);
            return scriptInputOutcome(before);
        }
        if (modifiers_.control() && event.key == EditorInputKey::V) {
            core::pmr::String pasted(&core::defaultCpuMemoryResource());
            if (!readSystemClipboardText(pasted))
                return scriptInputOutcome(before);
            core::pmr::String normalized(&core::defaultCpuMemoryResource());
            normalized.reserve(pasted.size());
            for (std::size_t i = 0; i < pasted.size(); ++i) {
                if (pasted[i] == '\r') {
                    if (i + 1 < pasted.size() && pasted[i + 1] == '\n')
                        ++i;
                    normalized.push_back('\n');
                } else
                    normalized.push_back(pasted[i]);
            }
            return insertScriptText(normalized.data(), normalized.size());
        }
        if (event.key == EditorInputKey::Backspace) {
            if (scriptSelectionStart() != scriptSelectionEnd())
                return replaceScriptRange(
                    scriptSelectionStart(), scriptSelectionEnd() - scriptSelectionStart(), "", 0);
            else if (scriptDocuments_.activeCursor() > 0) {
                const auto cursor = scriptDocuments_.activeCursor();
                const auto start = core::Utf8Cursor::previous(scriptTextView(), cursor);
                return replaceScriptRange(start, cursor - start, "", 0);
            }
            return scriptInputOutcome(before);
        }
        if (event.key == EditorInputKey::Delete) {
            if (scriptSelectionStart() != scriptSelectionEnd())
                return replaceScriptRange(
                    scriptSelectionStart(), scriptSelectionEnd() - scriptSelectionStart(), "", 0);
            else if (scriptDocuments_.activeCursor() < scriptTextView().size()) {
                const auto cursor = scriptDocuments_.activeCursor();
                const auto end = core::Utf8Cursor::next(scriptTextView(), cursor);
                return replaceScriptRange(cursor, end - cursor, "", 0);
            }
            return scriptInputOutcome(before);
        }
        if (event.key == EditorInputKey::Enter || event.key == EditorInputKey::NumpadEnter) {
            return insertScriptText("\n", 1);
        }
        if (event.key == EditorInputKey::Tab) {
            return insertScriptText("    ", 4);
        }
        if (event.key == EditorInputKey::ArrowLeft) {
            auto target = scriptDocuments_.activeCursor();
            if (!modifiers_.shift() && scriptSelectionStart() != scriptSelectionEnd())
                target = scriptSelectionStart();
            else
                target = core::Utf8Cursor::previous(scriptTextView(), target);
            moveScriptCursor(target, modifiers_.shift());
            return scriptInputOutcome(before);
        }
        if (event.key == EditorInputKey::ArrowRight) {
            auto target = scriptDocuments_.activeCursor();
            if (!modifiers_.shift() && scriptSelectionStart() != scriptSelectionEnd())
                target = scriptSelectionEnd();
            else
                target = core::Utf8Cursor::next(scriptTextView(), target);
            moveScriptCursor(target, modifiers_.shift());
            return scriptInputOutcome(before);
        }
        if (event.key == EditorInputKey::Home || event.key == EditorInputKey::End) {
            const auto cursor = scriptDocuments_.activeCursor();
            const auto previous = scriptTextView().rfind('\n', cursor == 0 ? 0 : cursor - 1);
            const auto lineStart = previous == core::pmr::String::npos ? 0 : previous + 1;
            const auto next = scriptTextView().find('\n', cursor);
            const auto lineEnd = next == core::pmr::String::npos ? scriptTextView().size() : next;
            moveScriptCursor(
                event.key == EditorInputKey::Home ? lineStart : lineEnd, modifiers_.shift());
            return scriptInputOutcome(before);
        }
        if (event.key == EditorInputKey::ArrowUp || event.key == EditorInputKey::ArrowDown) {
            moveScriptCursorVertical(event.key == EditorInputKey::ArrowUp ? -1 : 1);
            return scriptInputOutcome(before);
        }
        if (event.key == EditorInputKey::PageUp || event.key == EditorInputKey::PageDown) {
            for (int i = 0; i < 30; ++i)
                moveScriptCursorVertical(event.key == EditorInputKey::PageUp ? -1 : 1);
            return scriptInputOutcome(before);
        }
        return scriptInputOutcome(before);
    } catch (const std::bad_alloc&) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}
} // namespace arp::editor
