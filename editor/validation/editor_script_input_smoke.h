#pragma once

#include "core/cpu_allocator.h"

#include <string_view>

namespace arp::editor {
class EditorShell;

// Controlled window input uses the production normalization and command path.
class EditorScriptInputSmoke final {
public:
    static bool run(EditorShell&, std::string_view scenario, core::pmr::String& diagnostic);
};
} // namespace arp::editor
