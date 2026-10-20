#pragma once

#include "editor/command/editor_command.h"
#include "platform/window.h"

namespace arp::editor {
EditorInputCommand normalizePlatformInput(const platform::WindowInputEvent& event);
} // namespace arp::editor
