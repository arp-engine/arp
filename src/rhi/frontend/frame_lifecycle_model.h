#pragma once
#include <cstdint>

// Shared value ABI. Preserve the existing platform-qualified public type names.
// Platform owns event application; RHI lowering consumes only these value facts.

namespace arp::platform {
enum class PlatformSurfaceKind { DesktopWindow, AndroidSurface, IosLayer, WebCanvas, Headless };

enum class PlatformLifecycleEvent {
    Start,
    Pause,
    Resume,
    Stop,
    SurfaceCreated,
    SurfaceLost,
    Resize,
    MemoryWarning,
    EnterBackground,
    EnterForeground
};

struct PlatformLifecycleState {
    PlatformSurfaceKind surfaceKind = PlatformSurfaceKind::Headless;
    std::uint32_t logicalFrameSlots = 2;
    bool running = false;
    bool paused = false;
    bool backgrounded = false;
    bool surfaceValid = false;
    bool acquireAllowed = false;
    bool presentAllowed = false;
    bool swapchainRebuildRequired = false;
    bool releaseSurfaceResources = false;
    bool releaseTransientResources = false;
    bool memoryBudgetRefreshRequired = false;
    bool nativeHandleExposedToUpperLayer = false;
};

} // namespace arp::platform

namespace arp::rhi {
class FrameLifecycleAccess final {
public:
    static constexpr bool hasDrawableSurface(platform::PlatformSurfaceKind kind) noexcept
    {
        return kind != platform::PlatformSurfaceKind::Headless;
    }

    static constexpr bool allowsFrames(const platform::PlatformLifecycleState& state) noexcept
    {
        return state.running && !state.paused && !state.backgrounded && state.surfaceValid
            && hasDrawableSurface(state.surfaceKind);
    }

    static void refresh(platform::PlatformLifecycleState& state) noexcept
    {
        const bool allowed = allowsFrames(state);
        state.acquireAllowed = allowed;
        state.presentAllowed = allowed;
        state.nativeHandleExposedToUpperLayer = false;
    }
};
} // namespace arp::rhi
