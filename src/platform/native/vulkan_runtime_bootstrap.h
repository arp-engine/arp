#pragma once

#include "platform/window.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"

namespace arp::platform {

// Native platform/backend boundary. Uses the same original surface/device owner
// as RenderSystem; renderer and game code consume RenderSystem instead.
class VulkanSurfaceRuntimeBootstrap final {
public:
    static rhi_vulkan::VulkanRhiRuntimeDesc create(WindowSystem&,
        const PlatformWindow&,
        const core::pmr::String& label,
        core::pmr::String& diagnostics);
};

} // namespace arp::platform
