#pragma once

#include "rhi/semantics/native_platform_lifecycle_lowering.h"
#include "rhi/semantics/webgpu_runtime_profile.h"

namespace arp::rhi::semantics {

class WebGpuCanvasLifecycleBridge {
public:
    explicit WebGpuCanvasLifecycleBridge(rhi::WebGpuRuntimeProfile profile);

    NativePlatformLifecycleLoweringDecision lower(const platform::PlatformLifecycleState& state,
        platform::PlatformLifecycleEvent event) const;

private:
    rhi::WebGpuRuntimeProfile profile_;
};

platform::PlatformLifecycleState makeWebGpuCanvasLifecycleState(
    const rhi::WebGpuRuntimeProfile& profile);

NativePlatformLifecycleLoweringDecision lowerWebGpuCanvasLifecycleEvent(
    const rhi::WebGpuRuntimeProfile& profile,
    const platform::PlatformLifecycleState& state,
    platform::PlatformLifecycleEvent event);

} // namespace arp::rhi::semantics
