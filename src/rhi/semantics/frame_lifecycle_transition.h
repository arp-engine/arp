#pragma once
#include "rhi/frontend/frame_lifecycle_model.h"

namespace arp::rhi::semantics {
using platform::PlatformLifecycleEvent;
using platform::PlatformLifecycleState;

inline void clearOneShotFlags(PlatformLifecycleState& state)
{
    state.releaseSurfaceResources = false;
    state.releaseTransientResources = false;
    state.memoryBudgetRefreshRequired = false;
    state.nativeHandleExposedToUpperLayer = false;
}

namespace {

inline void applyStart(PlatformLifecycleState& state)
{
    state.running = true;
    state.paused = false;
}

inline void applyPause(PlatformLifecycleState& state)
{
    state.paused = true;
}

inline void applyResume(PlatformLifecycleState& state)
{
    state.running = true;
    state.paused = false;
}

inline void applyStop(PlatformLifecycleState& state)
{
    state.running = false;
    state.paused = false;
    state.backgrounded = false;
    state.surfaceValid = false;
}

inline void applySurfaceCreated(PlatformLifecycleState& state)
{
    state.surfaceValid = rhi::FrameLifecycleAccess::hasDrawableSurface(state.surfaceKind);
    state.swapchainRebuildRequired = true;
}

inline void applySurfaceLost(PlatformLifecycleState& state)
{
    state.surfaceValid = false;
    state.swapchainRebuildRequired = true;
}

inline void applyResize(PlatformLifecycleState& state)
{
    if (state.surfaceValid && rhi::FrameLifecycleAccess::hasDrawableSurface(state.surfaceKind)) {
        state.swapchainRebuildRequired = true;
    }
}

inline void applyEnterBackground(PlatformLifecycleState& state)
{
    state.backgrounded = true;
}

inline void applyEnterForeground(PlatformLifecycleState& state)
{
    state.running = true;
    state.backgrounded = false;
    if (state.surfaceValid && rhi::FrameLifecycleAccess::hasDrawableSurface(state.surfaceKind)) {
        state.swapchainRebuildRequired = true;
    }
}

} // namespace

inline void applyEvent(PlatformLifecycleState& state, PlatformLifecycleEvent event)
{
    ::arp::rhi::semantics::clearOneShotFlags(state);
    switch (event) {
    case PlatformLifecycleEvent::Start:
        applyStart(state);
        break;
    case PlatformLifecycleEvent::Pause:
        applyPause(state);
        break;
    case PlatformLifecycleEvent::Resume:
        applyResume(state);
        break;
    case PlatformLifecycleEvent::Stop:
        applyStop(state);
        break;
    case PlatformLifecycleEvent::SurfaceCreated:
        applySurfaceCreated(state);
        break;
    case PlatformLifecycleEvent::SurfaceLost:
        applySurfaceLost(state);
        break;
    case PlatformLifecycleEvent::Resize:
        applyResize(state);
        break;
    case PlatformLifecycleEvent::MemoryWarning:
        break;
    case PlatformLifecycleEvent::EnterBackground:
        applyEnterBackground(state);
        break;
    case PlatformLifecycleEvent::EnterForeground:
        applyEnterForeground(state);
        break;
    }
    rhi::FrameLifecycleAccess::refresh(state);
}

} // namespace arp::rhi::semantics
