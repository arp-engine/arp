#pragma once

#include "editor/ingress/editor_platform_input_router.h"
#include "editor/validation/editor_control_input.h"
#include "input/input.h"

#include <cassert>
#include <cstdio>

namespace arp::editor {

// Same ordered ingress as the native app. Positions come from actual hierarchy
// metadata and resolved Widget geometry, not screenshots or guessed numeric IDs.
class EditorReferenceDragInput final {
public:
    explicit EditorReferenceDragInput(EditorShell& shell) : shell_(shell), input_({}) {}

    bool send(platform::WindowInputEvent::Kind kind,
        ui::UiPoint point = {},
        platform::WindowKey key = platform::WindowKey::Unknown)
    {
        platform::WindowInputEvent event;
        event.kind = kind;
        event.windowId = 1;
        event.x = point.x;
        event.y = point.y;
        event.hasPosition = kind != platform::WindowInputEvent::Kind::KeyDown
            && kind != platform::WindowInputEvent::Kind::KeyUp
            && kind != platform::WindowInputEvent::Kind::FocusLost;
        event.mouseButton = platform::WindowMouseButton::Left;
        event.key = key;
        return EditorPlatformInputRouter::dispatch(shell_, event, input_, 1).ok();
    }

    bool down(ui::UiPoint p) { return send(platform::WindowInputEvent::Kind::MouseButtonDown, p); }

    bool move(ui::UiPoint p) { return send(platform::WindowInputEvent::Kind::PointerMoved, p); }

    bool up(ui::UiPoint p) { return send(platform::WindowInputEvent::Kind::MouseButtonUp, p); }

    static ui::UiPoint center(EditorShell& shell, ui::WidgetId id)
    {
        const auto layout = shell.runtime().canvas().resolveLayout();
        const auto* widget = layout.findWidget(id);
        if (!widget || !widget->visibleInHierarchy || !widget->enabledInHierarchy)
            std::fprintf(stderr,
                "reference drag center unavailable: widget=%llu selected=%llu inspector=%u visible=%d enabled=%d\n",
                (unsigned long long)id.value(),
                (unsigned long long)shell.sceneDocument().selectedObject().value(),
                unsigned(shell.inspectorState()),
                widget ? int(widget->visibleInHierarchy) : -1,
                widget ? int(widget->enabledInHierarchy) : -1);
        assert(widget && widget->visibleInHierarchy && widget->enabledInHierarchy);
        const ui::UiPoint result{
            widget->rect.x + widget->rect.width / 2, widget->rect.y + widget->rect.height / 2};
        assert(shell.runtime().canvas().hitTest(layout, result).widgetId() == id);
        return result;
    }

    static ui::WidgetId field(EditorShell& shell, std::string_view name)
    {
        for (const auto& property :
            shell.sceneDocument().inspectorProperties(shell.sceneDocument().selectedObject()))
            if (property.name == name) {
                const auto source = scenePropertyControlSource(property);
                const auto found = EditorControlInput::find(shell, source);
                if (!found.valid())
                    std::fprintf(
                        stderr, "reference drag missing field widget: %s\n", source.c_str());
                return found;
            }
        std::fprintf(
            stderr, "reference drag missing field metadata: %.*s\n", int(name.size()), name.data());
        return {};
    }

    // Fixtures use an unfolded, unscrolled bounded tree. This is deliberately
    // rejected when the projected painted icon cannot be found in the draw plan.
    static ui::UiPoint objectIcon(EditorShell& shell, scene::GameObjectId object)
    {
        const auto tree = shell.sceneDocument().hierarchyEntries();
        const auto draws = shell.drawCommands();
        assert(draws.ok());
        for (std::size_t row = 0; row < tree.size(); ++row) {
            if (tree[row].objectId != object)
                continue;
            assert(row < layout::kSceneHierarchyVisibleRows);
            const auto rect = layout::sceneHierarchyIconRect(row, tree[row].depth);
            bool painted = false;
            for (const auto& draw : draws.commands())
                if (draw.kind == renderer::ui::UiDrawCommandKind::Image && draw.rect.x == rect.x
                    && draw.rect.y == rect.y && draw.rect.width == rect.width
                    && draw.rect.height == rect.height)
                    painted = true;
            assert(painted);
            return {rect.x + rect.width / 2, rect.y + rect.height / 2};
        }
        assert(false);
        return {};
    }

    static bool feedback(EditorShell& shell, std::string_view text)
    {
        const auto draws = shell.drawCommands();
        if (!draws.ok())
            std::fprintf(
                stderr, "reference drag feedback draw failed: %s\n", draws.diagnostics().c_str());
        assert(draws.ok());
        for (const auto& draw : draws.commands())
            if (draw.kind == renderer::ui::UiDrawCommandKind::Text && draw.text == text)
                return true;
        return false;
    }

private:
    EditorShell& shell_;
    input::InputSystem input_;
};
} // namespace arp::editor
