#pragma once

#include "editor/workspace/editor_shell.h"

namespace arp::editor {
class EditorInspectorSelectionSmoke final {
public:
    static bool run(EditorShell&, std::string_view scenario, core::pmr::String& diagnostic);
};
} // namespace arp::editor
