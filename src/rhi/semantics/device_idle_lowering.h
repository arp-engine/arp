#pragma once

#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {

// DeviceIdle/1 is an explicit cold completion boundary, not capability discovery.
// A native disposition requires an actual successful native wait. Missing support
// and every failed wait must throw; neither may return as if resource reuse is safe.
class DeviceIdleLowering final {
public:
    enum class Disposition { NativeCompletionRequired, ValidatorOnly, DiagnosticFailure };

    struct Decision {
        Disposition disposition = Disposition::DiagnosticFailure;
        const char* diagnostic = "RHI-DEVICE-IDLE-BACKEND-UNKNOWN";
    };

    static constexpr Decision lower(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {Disposition::DiagnosticFailure, "RHI-DEVICE-IDLE-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
            return {Disposition::NativeCompletionRequired, nullptr};
        case Backend::WebGPU:
            return {Disposition::DiagnosticFailure, "RHI-WEBGPU-IDLE-SYNCHRONOUS-UNSUPPORTED"};
        case Backend::WebGL:
            return {Disposition::DiagnosticFailure, "RHI-WEBGL1-GPU-COMPLETION-UNAVAILABLE"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
