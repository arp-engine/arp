#pragma once
#include "core/memory/cpu_allocator.h"

#include <string_view>

namespace arp::editor {
class EditorShell;

class EditorScriptInspectorSmoke final {
public:
    static bool run(EditorShell&, std::string_view, core::pmr::String& diagnostic);
};
} // namespace arp::editor
