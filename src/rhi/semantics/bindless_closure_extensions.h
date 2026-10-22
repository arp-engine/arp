#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
// Pure value decisions. These extensions grant preparation, never execution.
class BindlessClosureExtensionLowering final {
public:
    template <class Decision>
    static constexpr Decision produced_attachments(Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return {false, false, false, "RHI-BINDLESS-PRODUCED-ATTACHMENT-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {false, true, false, "RHI-BINDLESS-PRODUCED-ATTACHMENT-NULL-VALIDATOR"};
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
            return {false, false, false, "RHI-BINDLESS-PRODUCED-ATTACHMENT-NATIVE-PASS-REQUIRED"};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-BINDLESS-PRODUCED-ATTACHMENT-UNSUPPORTED"};
        }
        return {};
    }

    template <class Decision>
    static constexpr Decision produced_image(Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return {false, false, false, "RHI-BINDLESS-PRODUCED-IMAGE-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-BINDLESS-PRODUCED-IMAGE-UNSUPPORTED"};
        }
        return {};
    }

    template <class Decision>
    static constexpr Decision snapshot_scope(Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return {false, false, false, "RHI-BINDLESS-SNAPSHOT-SCOPE-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-BINDLESS-SNAPSHOT-SCOPE-UNSUPPORTED"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
