#pragma once

#include "core/cpu_allocator.h"

#include <string_view>

namespace arp::editor {
class EditorShell;

class EditorReferenceDragSmoke final {
public:
    static bool run(EditorShell&, std::string_view scenario, core::pmr::String& diagnostic);
};
} // namespace arp::editor
