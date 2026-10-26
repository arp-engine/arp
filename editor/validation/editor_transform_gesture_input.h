#pragma once

#include "editor/ingress/platform_input_adapter.h"
#include "editor/workspace/editor_shell.h"

#include <cmath>
#include <cstdio>

namespace arp::editor {

// Controlled native input uses the same pivot frame and ingress mapping as the UI.
class EditorTransformGestureInput final {
public:
    EditorTransformGestureInput(EditorShell& shell, float width, float height)
        : shell_(shell), width_(width), height_(height)
    {}

    bool linear(bool scale, float direction = 1)
    {
        shell_.activate(scale ? "tool_3" : "tool_1");
        core::Vec3 origin;
        EditorSceneTransformFrame frame;
        if (shell_.sceneTransformSelectionView(origin, frame))
            return false;
        core::Vec3 center, end;
        if (!project(origin, center) || !project(origin + frame.axes[0], end))
            return false;
        const auto axis = end - center;
        pointer_ = center + core::scale(axis, 0.65f);
        event(platform::WindowInputEvent::Kind::MouseButtonDown);
        delta(core::scale(axis, direction * 0.25f));
        delta(core::scale(axis, direction * 0.25f));
        event(platform::WindowInputEvent::Kind::MouseButtonUp);
        return true;
    }

    bool rotate()
    {
        shell_.activate("tool_2");
        core::Vec3 origin;
        EditorSceneTransformFrame frame;
        if (shell_.sceneTransformSelectionView(origin, frame))
            return false;
        core::Vec3 center;
        if (!project(origin, center)
            || !project(origin
                    + core::scale(
                        frame.planeU(2), EditorSceneTransformFrame::ringRadius * std::cos(0.47f))
                    + core::scale(
                        frame.planeV(2), EditorSceneTransformFrame::ringRadius * std::sin(0.47f)),
                pointer_))
            return false;
        event(platform::WindowInputEvent::Kind::MouseButtonDown);
        auto radius = pointer_ - center;
        for (unsigned step = 0; step != 2; ++step) {
            const core::Vec3 next{radius.x * std::cos(0.15f) - radius.y * std::sin(0.15f),
                radius.x * std::sin(0.15f) + radius.y * std::cos(0.15f),
                0};
            delta(next - radius);
            radius = next;
        }
        event(platform::WindowInputEvent::Kind::FocusLost);
        event(platform::WindowInputEvent::Kind::MouseButtonUp);
        return true;
    }

private:
    bool project(core::Vec3 world, core::Vec3& screen) const
    {
        const auto layout = shell_.runtime().canvas().resolveLayout();
        const auto* viewport = layout.ok() ? layout.findWidget(shell_.id("viewport")) : nullptr;
        if (!viewport
            || !shell_.sceneViewportCamera().project(
                world, viewport->rect.width, viewport->rect.height, screen.x, screen.y))
            return false;
        screen.x += viewport->rect.x;
        screen.y += viewport->rect.y;
        return true;
    }

    void event(platform::WindowInputEvent::Kind kind)
    {
        platform::WindowInputEvent input;
        input.kind = kind;
        input.mouseButton = platform::WindowMouseButton::Left;
        input.x = pointer_.x;
        input.y = pointer_.y;
        submit(input);
    }

    void delta(core::Vec3 value)
    {
        pointer_ = pointer_ + value;
        platform::WindowInputEvent input;
        input.kind = platform::WindowInputEvent::Kind::MouseMoved;
        input.x = pointer_.x;
        input.y = pointer_.y;
        input.dx = value.x;
        input.dy = value.y;
        submit(input);
    }

    void submit(platform::WindowInputEvent& input)
    {
        const auto& canvas = shell_.runtime().canvas().desc().viewport;
        // Author the controlled input in logical window units, then exercise the
        // exact ingress mapping used by the native application event loop.
        input.x *= width_ / canvas.width;
        input.y *= height_ / canvas.height;
        input.dx *= width_ / canvas.width;
        input.dy *= height_ / canvas.height;
        input.hasPosition = input.kind == platform::WindowInputEvent::Kind::MouseButtonDown
            || input.kind == platform::WindowInputEvent::Kind::MouseButtonUp;
        const EditorCanvasInputMapping mapping(width_, height_, canvas.width, canvas.height);
        if (const auto* diagnostic = mapping.apply(input)) {
            std::fprintf(stderr, "%s\n", diagnostic);
            return;
        }
        shell_.handlePlatformInput(input);
    }

    EditorShell& shell_;
    core::Vec3 pointer_{};
    float width_;
    float height_;
};

} // namespace arp::editor
