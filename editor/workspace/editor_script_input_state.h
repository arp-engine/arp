#pragma once

#include "core/ownership/lifetime_epoch.h"
#include "editor/workspace/editor_modifier_state.h"

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace arp::editor {

// A fixed-size observation of script input state. The Session ticket includes
// text, saved baselines, selection and scroll; no document text is copied.
struct EditorScriptInputState {
    core::LifetimeEpoch::Ticket document;
    std::uint64_t version = 0;
    EditorModifierState modifiers;
    bool focused = false;
    bool selectionCaptured = false;
    bool scrollCaptured = false;
    std::size_t clickOffset = 0;
    std::chrono::steady_clock::time_point clickTime{};
    float scrollGrabOffset = 0.0f;
};
} // namespace arp::editor
