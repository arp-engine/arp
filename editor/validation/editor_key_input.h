#pragma once

#include "editor/ingress/platform_input_adapter.h"
#include "editor/workspace/editor_shell.h"

namespace arp::editor {

// Controlled native keys cross the same normalization boundary as window events.
class EditorKeyInput final {
public:
    static void send(EditorShell& shell, platform::WindowKey key, bool down = true)
    {
        platform::WindowInputEvent event;
        event.kind = down ? platform::WindowInputEvent::Kind::KeyDown
                          : platform::WindowInputEvent::Kind::KeyUp;
        event.key = key;
        shell.handlePlatformInput(event);
    }

    static void focusLost(EditorShell& shell)
    {
        platform::WindowInputEvent event;
        event.kind = platform::WindowInputEvent::Kind::FocusLost;
        shell.handlePlatformInput(event);
    }
};
} // namespace arp::editor
