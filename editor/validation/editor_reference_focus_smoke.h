#pragma once

#include "core/cpu_allocator.h"

#include <string_view>

namespace arp::editor {
class EditorShell;

// Controlled real controls shared by CPU smoke and native RenderDoc validation.
class EditorReferenceFocusSmoke final {
public:
    static bool run(EditorShell&, std::string_view scenario, core::pmr::String& diagnostic);
};
} // namespace arp::editor
