#pragma once

#include "editor/workspace/editor_shell.h"

namespace arp::editor {

// Controlled native input integration, not OS key injection.
class EditorModifierSmoke final {
public:
    static bool run(EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic);
};
} // namespace arp::editor
