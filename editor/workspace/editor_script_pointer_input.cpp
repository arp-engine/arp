#include "core/text/utf8_cursor.h"
#include "editor/workspace/editor_layout.h"
#include "editor/workspace/editor_shell.h"
#include "ui/scrollbar_geometry.h"

#include <cctype>
#include <cmath>
#include <limits>
#include <new>

namespace arp::editor {
using namespace layout;

void EditorShell::cancelScriptPointerCapture() noexcept
{
    scriptPointerSuppressRelease_ |= scriptDraggingSelection_ || scriptDraggingScrollbar_;
    scriptDraggingSelection_ = scriptDraggingScrollbar_ = false;
}

std::optional<EditorCommandOutcome> EditorShell::applyScriptPointerInput(
    const EditorInputCommand& event)
{
    using Kind = EditorInputKind;
    if (scriptPointerSuppressRelease_) {
        if (event.kind == Kind::PointerDown && event.primaryButton)
            scriptPointerSuppressRelease_ = false;
        else if (event.kind == Kind::PointerUp && event.primaryButton) {
            scriptPointerSuppressRelease_ = false;
            platformPointerConsumed_ = true;
            return EditorCommandOutcome{true, false, EditorDiagnosticCode::None};
        } else if (event.kind == Kind::PointerMoved || event.kind == Kind::PointerDelta
            || event.kind == Kind::Wheel) {
            platformPointerConsumed_ = true;
            return EditorCommandOutcome{true, false, EditorDiagnosticCode::None};
        }
    }
    if (presentationState_.workspace() != "Script")
        return std::nullopt;
    const auto before = captureScriptInputState();
    const bool captured = scriptDraggingSelection_ || scriptDraggingScrollbar_;
    // Releasing an owned sequence never resolves layout or allocates. A failed
    // move retains capture, so the following release still reaches this path.
    if (event.kind == Kind::PointerUp && event.primaryButton && captured) {
        platformPointerConsumed_ = true;
        scriptDraggingSelection_ = scriptDraggingScrollbar_ = false;
        return scriptInputOutcome(before);
    }
    const bool moving = event.kind == Kind::PointerMoved && captured;
    const bool pressing = event.kind == Kind::PointerDown && event.primaryButton;
    if (!moving && !pressing && event.kind != Kind::Wheel)
        return std::nullopt;
    platformPointerConsumed_ = moving;
    if (!std::isfinite(event.x) || !std::isfinite(event.y)
        || (event.kind == Kind::Wheel && !std::isfinite(event.wheelDelta)))
        return EditorCommandOutcome{false, false, EditorDiagnosticCode::InvalidCommandPayload};
    try {
        const auto layout = runtime().canvas().resolveLayout();
        const auto* viewport = layout.findWidget(id("viewport"));
        if (!viewport)
            return moving ? std::optional<EditorCommandOutcome>(scriptInputOutcome(before))
                          : std::nullopt;
        const auto& rect = viewport->rect;
        const bool inside = event.x >= rect.x && event.x < rect.x + rect.width && event.y >= rect.y
            && event.y < rect.y + rect.height;
        if (event.kind == Kind::Wheel) {
            if (!inside)
                return std::nullopt;
            const double deltaValue = double(event.wheelDelta) * 3.0;
            if (deltaValue > double(std::numeric_limits<int>::max())
                || deltaValue < double(std::numeric_limits<int>::min()))
                return EditorCommandOutcome{
                    false, false, EditorDiagnosticCode::InvalidCommandPayload};
            const auto delta = static_cast<int>(std::lround(deltaValue));
            const auto visible =
                static_cast<std::size_t>(std::max(0.0f, rect.height - 42.0f) / kCodeLineHeight);
            const auto lines = scriptLineCount(scriptTextView());
            const auto maximum = lines > visible ? lines - visible : 0u;
            const auto current = std::min(scriptDocuments_.activeScrollLine(), maximum);
            const auto next = delta > 0
                ? current - std::min(current, static_cast<std::size_t>(delta))
                : current
                    + std::min(maximum - current, static_cast<std::size_t>(-std::int64_t(delta)));
            scriptDocuments_.setActiveScrollLine(next);
            platformPointerConsumed_ = true;
            return scriptInputOutcome(before);
        }
        const float trackX = rect.x + rect.width - kScriptScrollbarWidth - 2.0f;
        const float trackY = rect.y + 34.0f, trackHeight = rect.height - 58.0f;
        const bool onTrack = inside && event.x >= trackX && event.x < trackX + kScriptScrollbarWidth
            && event.y >= trackY && event.y < trackY + trackHeight;
        if (scriptDraggingScrollbar_ || (pressing && onTrack)) {
            const auto visible =
                static_cast<std::size_t>(std::max(0.0f, rect.height - 42.0f) / kCodeLineHeight);
            const auto thumb = ui::ScrollbarGeometry::thumb(trackY,
                trackHeight,
                scriptLineCount(scriptTextView()),
                visible,
                scriptDocuments_.activeScrollLine(),
                32.0f);
            if (thumb.visible) {
                const float grab = pressing
                    ? ((event.y >= thumb.y && event.y < thumb.y + thumb.height)
                              ? event.y - thumb.y
                              : thumb.height * 0.5f)
                    : scriptScrollbarGrabOffset_;
                const float travel = trackHeight - thumb.height;
                const float fraction = travel > 0.0f
                    ? std::clamp((event.y - trackY - grab) / travel, 0.0f, 1.0f)
                    : 0.0f;
                const auto offset = static_cast<std::size_t>(
                    std::lround(fraction * static_cast<float>(thumb.maxOffset)));
                scriptDocuments_.setActiveScrollLine(offset);
                if (pressing) {
                    scriptFocused_ = true;
                    scriptDraggingScrollbar_ = true;
                    scriptScrollbarGrabOffset_ = grab;
                }
            } else if (pressing)
                scriptPointerSuppressRelease_ = true;
            platformPointerConsumed_ = true;
            return scriptInputOutcome(before);
        }
        const bool onCode = inside && event.x >= rect.x + 54.0f
            && event.x < rect.x + rect.width - 8.0f
            && event.y >= rect.y + kScriptEditorHeaderHeight + 4.0f
            && event.y < rect.y + rect.height - 24.0f;
        if (!scriptDraggingSelection_ && (!pressing || !onCode))
            return std::nullopt;
        const auto offset = scriptOffsetAt(event.x, event.y, rect);
        if (moving) {
            moveScriptCursor(offset, true);
        } else {
            const auto now = std::chrono::steady_clock::now();
            const bool doubleClick = now - lastEditorClickTime_ < std::chrono::milliseconds(450)
                && (offset == lastScriptClickOffset_
                    || offset == core::Utf8Cursor::next(scriptTextView(), lastScriptClickOffset_)
                    || core::Utf8Cursor::next(scriptTextView(), offset) == lastScriptClickOffset_);
            // Everything below is non-allocating. Publish focus/capture only
            // after layout and offset preparation have both succeeded.
            scriptFocused_ = true;
            lastEditorClickTime_ = now;
            lastScriptClickOffset_ = offset;
            if (doubleClick && !scriptTextView().empty()) {
                std::size_t probe = offset;
                if (probe == scriptTextView().size()
                    || (probe < scriptTextView().size() && scriptTextView()[probe] == '\n')) {
                    if (probe > 0)
                        probe = core::Utf8Cursor::previous(scriptTextView(), probe);
                }
                auto isWordByte = [&](std::size_t at) {
                    if (at >= scriptTextView().size() || scriptTextView()[at] == '\n')
                        return false;
                    const auto ch = static_cast<unsigned char>(scriptTextView()[at]);
                    return ch >= 0x80u || std::isalnum(ch) || ch == '_';
                };
                if (isWordByte(probe)) {
                    auto begin = probe, end = core::Utf8Cursor::next(scriptTextView(), probe);
                    while (begin > 0) {
                        const auto previous = core::Utf8Cursor::previous(scriptTextView(), begin);
                        if (!isWordByte(previous))
                            break;
                        begin = previous;
                    }
                    while (end < scriptTextView().size() && isWordByte(end))
                        end = core::Utf8Cursor::next(scriptTextView(), end);
                    scriptDocuments_.setActiveSelection(end, begin);
                } else
                    scriptDocuments_.setActiveSelection(offset, offset);
                scriptDraggingSelection_ = false;
                scriptPointerSuppressRelease_ = true;
            } else {
                moveScriptCursor(offset, modifiers_.shift());
                scriptDraggingSelection_ = true;
            }
        }
        platformPointerConsumed_ = true;
        return scriptInputOutcome(before);
    } catch (const std::bad_alloc&) {
        platformPointerConsumed_ = true;
        return EditorCommandOutcome{false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}
} // namespace arp::editor
