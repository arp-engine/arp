#pragma once

#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
class MemoryAdmissionLowering final {
public:
    template <class Disposition>
    static constexpr Disposition heapPlacement(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return Disposition::ValidatorOnly;
        case Backend::Vulkan:
        case Backend::Metal:
            return Disposition::Native;
        case Backend::WebGPU:
        case Backend::WebGL:
            return Disposition::DiagnosticFailure;
        }
        return Disposition::DiagnosticFailure;
    }

    static constexpr const char* allocatorBackingDiagnostic(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return "RHI-ALLOCATOR-BACKING-NULL-VALIDATOR";
        case Backend::Vulkan:
            return "RHI-ALLOCATOR-BACKING-VULKAN-UNINSTRUMENTED";
        case Backend::Metal:
            return "RHI-ALLOCATOR-BACKING-METAL-UNINSTRUMENTED";
        case Backend::WebGPU:
            return "RHI-ALLOCATOR-BACKING-WEBGPU-UNAVAILABLE";
        case Backend::WebGL:
            return "RHI-ALLOCATOR-BACKING-WEBGL-UNAVAILABLE";
        }
        return nullptr;
    }

    static constexpr bool knownBackend(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
        case Backend::WebGL:
            return true;
        }
        return false;
    }

    template <class Decision, class Disposition>
    static constexpr Decision stagingColdStorage(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                false,
                "RHI-STAGING-COLD-STORAGE-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {Disposition::Prototype, true, false, nullptr};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-STAGING-COLD-STORAGE-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-STAGING-COLD-STORAGE-WEBGPU-PAYLOAD-MISSING"};
        case Backend::WebGL:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-STAGING-COLD-STORAGE-WEBGL-UNSUPPORTED"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
