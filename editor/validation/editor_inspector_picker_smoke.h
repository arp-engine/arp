#pragma once

#include "editor/workspace/editor_shell.h"

namespace arp::editor {

// Shared controlled input for CPU contracts and real hidden-window captures.
class EditorInspectorPickerSmoke final {
public:
    static bool run(EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic);
};
} // namespace arp::editor
