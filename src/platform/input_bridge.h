#pragma once

// WindowSystem/1 input adapter. Translates neutral WindowInputEvents from the
// platform event pump into the engine's InputSystem event vocabulary, in the
// same bridge pattern as audio_lifecycle_bridge: a pure, stateless conversion
// surface with deterministic drop accounting for keys the InputSystem does
// not model. The InputSystem keeps its push-based beginFrame contract; this
// bridge never reaches into platform internals.

#include "input/input.h"
#include "platform/window.h"

#include <cstdint>

namespace arp::platform {

struct WindowInputBridgeStats {
    /// Events appended to the input frame.
    std::uint32_t forwardedEvents = 0;
    /// Focus loss, minimize and close barriers at their native input order.
    std::uint32_t forwardedLifecycleEvents = 0;
    /// Key events dropped because the neutral key has no InputKey equivalent.
    std::uint32_t droppedKeyEvents = 0;
    /// Mouse button events dropped because the button has no InputMouseButton
    /// equivalent (X1 / X2).
    std::uint32_t droppedMouseButtonEvents = 0;
    /// Window events not requiring an input cancellation barrier.
    std::uint32_t windowEventsIgnored = 0;
};

/// Append input events from the batch onto the frame. Window lifecycle events
/// requiring cancellation are forwarded as explicit barriers; other lifecycle
/// events are counted as ignored input. Text, pointer, wheel, mouse and mapped key
/// events are forwarded in batch order.
WindowInputBridgeStats applyWindowInputToInputFrame(
    const WindowEventBatch& batch, arp::input::InputFrame& outFrame);

/// Neutral key -> InputSystem key mapping. Returns false for keys without an
/// InputSystem equivalent so callers can drop them deterministically.
bool windowKeyToInputKey(WindowKey key, arp::input::InputKey& outKey);

/// Neutral mouse button -> InputSystem button mapping. Returns false for
/// X1 / X2.
bool windowMouseButtonToInputMouseButton(
    WindowMouseButton button, arp::input::InputMouseButton& outButton);

} // namespace arp::platform
