#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
// Frozen-byte CPU consumption only. Actual Vulkan push, Metal constant bytes
// and WebGPU uniform-ring commands retain independent execution gates.
class OriginalBindlessPushConsumptionLowering final {
public:
    struct Decision {
        bool projectionAvailable = false, validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-PUSH-CONSUMPTION-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return {false, false, false, "RHI-ORIGINAL-PUSH-CONSUMPTION-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-ORIGINAL-PUSH-CONSUMPTION-UNAVAILABLE"};
        }
        return {};
    }

    // Only the independently requested recording profile may consume frozen
    // bytes through a real native helper. CPU projection /1 stays unchanged.
    static constexpr Decision recording(Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return {false, false, false, "RHI-ORIGINAL-PUSH-RECORDING-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, "RHI-ORIGINAL-PUSH-RECORDING-NULL-VALIDATOR"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, true, nullptr};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-ORIGINAL-PUSH-RECORDING-UNAVAILABLE"};
        }
        return {};
    }

    static constexpr const char* validateNativeSize(
        std::uint32_t bytes, std::uint32_t requested, std::uint32_t queried) noexcept
    {
        if (!queried)
            return "RHI-ORIGINAL-PUSH-RECORDING-LIMIT-UNAVAILABLE";
        if (!bytes || bytes % 4u || bytes > requested || bytes > queried)
            return "RHI-ORIGINAL-PUSH-RECORDING-SIZE-LIMIT";
        return nullptr;
    }

    // Independently budgeted upload sizing has four-backend CPU projection.
    // It grants no new native allocation, recording or completion authority.
    static constexpr Decision upload(Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return {false, false, false, "RHI-ORIGINAL-PUSH-UPLOAD-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-ORIGINAL-PUSH-UPLOAD-UNAVAILABLE"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
