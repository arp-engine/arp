#pragma once

#include "rhi/frontend/frame_lifecycle_model.h"

namespace arp::platform {

/// Apply a lifecycle event to the state (state machine migration).
/// Call this BEFORE feeding the state to semantics lowering.
void applyEvent(PlatformLifecycleState& state, PlatformLifecycleEvent event);

/// Utility helpers (used internally by applyEvent, also available directly).
bool hasDrawableSurface(PlatformSurfaceKind kind);
bool lifecycleAllowsFrames(const PlatformLifecycleState& state);
void refreshFrameAccess(PlatformLifecycleState& state);
void clearOneShotFlags(PlatformLifecycleState& state);

} // namespace arp::platform
