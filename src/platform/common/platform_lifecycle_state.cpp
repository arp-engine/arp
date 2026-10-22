#include "platform/platform_lifecycle_state.h"

#include "rhi/semantics/frame_lifecycle_transition.h"

namespace arp::platform {

bool hasDrawableSurface(PlatformSurfaceKind kind)
{
    return rhi::FrameLifecycleAccess::hasDrawableSurface(kind);
}

bool lifecycleAllowsFrames(const PlatformLifecycleState& state)
{
    return rhi::FrameLifecycleAccess::allowsFrames(state);
}

void refreshFrameAccess(PlatformLifecycleState& state)
{
    rhi::FrameLifecycleAccess::refresh(state);
}

void clearOneShotFlags(PlatformLifecycleState& state)
{
    rhi::semantics::clearOneShotFlags(state);
}

void applyEvent(PlatformLifecycleState& state, PlatformLifecycleEvent event)
{
    rhi::semantics::applyEvent(state, event);
}
} // namespace arp::platform
