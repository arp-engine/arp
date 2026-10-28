#pragma once

#include "core/cpu_allocator.h"

#include <string_view>

namespace arp::editor {
class EditorShell;

// Formal v4 file load and the existing Inspector Remove control, followed by
// exact history commands. The resulting ordinary UI is captured by RenderDoc.

class EditorScriptPresenceSmoke final {
public:
    static bool run(EditorShell&, std::string_view scenario, core::pmr::String& diagnostic);
};
} // namespace arp::editor
