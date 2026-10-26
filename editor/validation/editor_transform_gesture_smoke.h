#pragma once

#include "core/cpu_allocator.h"

#include <string_view>

namespace arp::editor {
class EditorShell;

// Explicit native hidden-window validation input. This drives the ordinary
// platform-input/typed-command path; it never writes scene transforms itself.
class EditorTransformGestureSmoke final {
public:
    static bool run(EditorShell& shell,
        std::string_view scenario,
        core::pmr::String& diagnostic,
        float windowWidth,
        float windowHeight);
};
} // namespace arp::editor
