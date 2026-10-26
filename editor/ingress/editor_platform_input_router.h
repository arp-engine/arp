#pragma once

#include "platform/window.h"
#include "ui/ui.h"

namespace arp::input {
class InputSystem;
}

namespace arp::editor {
class EditorShell;

// Shell admission and UI dispatch run in event order. A captured source down
// and its entire pointer tail are consumed before UI can activate another row.
class EditorPlatformInputRouter final {
public:
    static ui::UiInputFrameDispatchResult dispatch(EditorShell&,
        const platform::WindowInputEvent&,
        input::InputSystem&,
        std::uint64_t windowId);
};
} // namespace arp::editor
