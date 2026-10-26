#pragma once

#include "editor/workspace/editor_shell.h"

namespace arp::editor {

// Controlled normalized window keys against the real native Editor shell.
class EditorSceneKeyboardSmoke final {
public:
    static bool run(EditorShell&, std::string_view scenario, core::pmr::String& diagnostic);
};
} // namespace arp::editor
