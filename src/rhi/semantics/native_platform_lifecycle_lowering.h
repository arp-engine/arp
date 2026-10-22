#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/frame_lifecycle_model.h"
#include "rhi/gpu_capabilities.h"
#include "rhi/semantics/native_backend_capabilities.h"

#include <cstdint>
#include <string>
#include <vector>

namespace arp::rhi::semantics {

enum class NativePlatformLifecycleSemantic {
    NullRecordedLifecycle,
    DoubleBufferedFrameSlotsPreserved,
    AndroidPauseStopsAcquirePresent,
    AndroidSurfaceInvalidatesSwapchain,
    AndroidResumeRequiresValidSurface,
    IosBackgroundStopsDrawableAcquire,
    IosLayerInvalidatesDrawableResources,
    IosLayerResizeRebuildsDrawableResources,
    DesktopResizeRebuildsSwapchain,
    WebCanvasConfigureSurface,
    ReleaseSurfaceFrameResources,
    ReleaseTransientResources,
    MemoryBudgetRefresh
};

struct NativePlatformLifecycleLoweringDecision {
    bool success = false;
    rhi::Backend backend = rhi::Backend::Null;
    rhi::FallbackMode fallbackMode = rhi::FallbackMode::Native;
    platform::PlatformLifecycleState state;
    std::pmr::vector<NativePlatformLifecycleSemantic> semantics;
    std::pmr::string diagnostics;
};

class NativePlatformLifecycleLowerer {
public:
    explicit NativePlatformLifecycleLowerer(NativeBackendCapabilityContract contract);

    NativePlatformLifecycleLoweringDecision lower(const platform::PlatformLifecycleState& state,
        platform::PlatformLifecycleEvent event) const;

private:
    NativeBackendCapabilityContract contract_;
};

NativePlatformLifecycleLoweringDecision lowerPlatformLifecycleEvent(
    const NativeBackendCapabilityContract& contract,
    const platform::PlatformLifecycleState& state,
    platform::PlatformLifecycleEvent event);

bool hasNativePlatformLifecycleSemantic(const NativePlatformLifecycleLoweringDecision& decision,
    NativePlatformLifecycleSemantic semantic);

} // namespace arp::rhi::semantics
