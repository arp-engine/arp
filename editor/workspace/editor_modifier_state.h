#pragma once

#include "editor/command/editor_command.h"

namespace arp::editor {

// Editor-owned physical sides survive workspace and modal routing. Aggregate
// shortcuts must not release until both sides release; FocusLost is the reset.
class EditorModifierState final {
public:
    static bool handles(EditorInputKey key) noexcept
    {
        return key == EditorInputKey::LeftControl || key == EditorInputKey::RightControl
            || key == EditorInputKey::LeftShift || key == EditorInputKey::RightShift;
    }

    bool update(EditorInputKey key, bool pressed) noexcept
    {
        bool* side = nullptr;
        switch (key) {
        case EditorInputKey::LeftControl:
            side = &leftControl_;
            break;
        case EditorInputKey::RightControl:
            side = &rightControl_;
            break;
        case EditorInputKey::LeftShift:
            side = &leftShift_;
            break;
        case EditorInputKey::RightShift:
            side = &rightShift_;
            break;
        default:
            return false;
        }
        const bool changed = *side != pressed;
        *side = pressed;
        return changed;
    }

    bool control() const noexcept { return leftControl_ || rightControl_; }

    bool shift() const noexcept { return leftShift_ || rightShift_; }

    void clear() noexcept { *this = {}; }

    bool operator==(const EditorModifierState& other) const noexcept
    {
        return leftControl_ == other.leftControl_ && rightControl_ == other.rightControl_
            && leftShift_ == other.leftShift_ && rightShift_ == other.rightShift_;
    }

private:
    bool leftControl_ = false;
    bool rightControl_ = false;
    bool leftShift_ = false;
    bool rightShift_ = false;
};
} // namespace arp::editor
