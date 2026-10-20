#pragma once

#include "rhi/gpu_common.h"

namespace arp::platform {

// Public ordinals are stable. This is not the rhi::Backend enumeration.
enum class BackendPreference {
    Default = 0,
    Vulkan = 1,
    Metal = 2,
    WebGPU = 3,
    Null = 4,
    WebGL = 5,
};

struct RenderSystemBackendRoute {
    rhi::Backend requestedBackend = rhi::Backend::Null;
    bool routeToFactory = false;
    const char* diagnostic = "RHI-PLATFORM-BACKEND-PREFERENCE-UNKNOWN";
};

// RenderSystemBackendSelection/1 validates identity and compiled factory
// routing only. A route never grants capabilities or native execution.
class RenderSystemBackendSelection final {
public:
    static constexpr unsigned contractVersion = 1u;

    static constexpr RenderSystemBackendRoute forBuild(BackendPreference preference,
        rhi::Backend compiledBackend,
        bool orderedPrototypeOptIn = false) noexcept
    {
        using rhi::Backend;
        Backend requested = Backend::Null;
        switch (preference) {
        case BackendPreference::Default:
            requested = compiledBackend;
            break;
        case BackendPreference::Null:
            requested = Backend::Null;
            break;
        case BackendPreference::Vulkan:
            requested = Backend::Vulkan;
            break;
        case BackendPreference::Metal:
            requested = Backend::Metal;
            break;
        case BackendPreference::WebGPU:
            requested = Backend::WebGPU;
            break;
        case BackendPreference::WebGL:
            requested = Backend::WebGL;
            break;
        default:
            return {};
        }
        switch (compiledBackend) {
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
        case Backend::WebGL:
            break;
        default:
            return {requested, false, "RHI-PLATFORM-BACKEND-BUILD-UNKNOWN"};
        }
        if (requested == Backend::WebGL && compiledBackend == Backend::WebGL
            && orderedPrototypeOptIn)
            return {requested, true, nullptr};
        if (requested == Backend::WebGL || compiledBackend == Backend::WebGL)
            return {requested, false, "RHI-WEBGL1-RENDERSYSTEM-UNAVAILABLE"};
        if (requested == Backend::Null || compiledBackend == Backend::Null)
            return {requested,
                false,
                "requested RenderSystem backend is not available in this platform build"};
        if (requested == compiledBackend)
            return {requested, true, nullptr};
        if (compiledBackend == Backend::Metal)
            return {requested,
                false,
                "requested backend is unavailable in Apple desktop build; use Metal"};
        if (requested == Backend::Vulkan)
            return {requested,
                false,
                "Vulkan RenderSystem requires the native RHI Vulkan platform implementation"};
        if (requested == Backend::WebGPU)
            return {requested,
                false,
                "WebGPU RenderSystem requires the RHI WebGPU platform implementation"};
        return {requested,
            false,
            "requested RenderSystem backend is not available in this platform build"};
    }
};

} // namespace arp::platform
