#pragma once

#include "editor/command/editor_command.h"
#include "platform/window.h"

namespace arp::editor {

// Maps platform logical client coordinates to the authored UI canvas. Framebuffer
// pixels and DPI are intentionally absent: the platform already reports logical units.
class EditorCanvasInputMapping final {
public:
    EditorCanvasInputMapping(
        float windowWidth, float windowHeight, float canvasWidth, float canvasHeight);

    // Null means success. Failure preserves every field; non-spatial events pass
    // through even at zero window extent so focus cancellation remains available.
    const char* apply(platform::WindowInputEvent& event) const;

private:
    float windowWidth_;
    float windowHeight_;
    float canvasWidth_;
    float canvasHeight_;
};

EditorInputCommand normalizePlatformInput(const platform::WindowInputEvent& event);
} // namespace arp::editor
