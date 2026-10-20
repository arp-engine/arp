#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
// A frozen original buffer-only Compute command program. WebGPU uses distinct
// native usage scopes for copy/compute hazards and the original CPU state
// journal for initial/final ledger transitions. No execution is granted here.
// Original transfer brackets map to encoder-copy scope; copies never occur
// inside a native compute pass. Original barriers stay outside both pass scopes.
class OriginalBindlessRootProgramLowering final {
public:
    struct Decision {
        bool preparationAvailable = false;
        bool executionAvailable = false;
        bool completionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-ROOT-PROGRAM-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version == 7u) {
            switch (backend) {
            case Backend::Null:
                return {
                    false, false, false, "RHI-BINDLESS-PRODUCED-GEOMETRY-PROGRAM-NULL-VALIDATOR"};
            case Backend::Vulkan:
            case Backend::Metal:
            case Backend::WebGPU:
                return {false,
                    false,
                    false,
                    "RHI-BINDLESS-PRODUCED-GEOMETRY-NATIVE-ADMISSION-REQUIRED"};
            case Backend::WebGL:
                return {false, false, false, "RHI-WEBGL1-BINDLESS-PRODUCED-GEOMETRY-UNSUPPORTED"};
            }
            return {};
        }
        if (version != 1u && version != 2u && version != 3u && version != 4u && version != 5u
            && version != 6u)
            return {false, false, false, "RHI-ORIGINAL-ROOT-PROGRAM-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {false, false, false, "RHI-ORIGINAL-ROOT-PROGRAM-NULL-PROFILE"};
        case Backend::Vulkan:
            return {false, false, false, "RHI-ORIGINAL-ROOT-PROGRAM-VULKAN-PROFILE"};
        case Backend::Metal:
            return {false, false, false, "RHI-ORIGINAL-ROOT-PROGRAM-METAL-UNAVAILABLE"};
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-BINDLESS-ROOT-PROGRAM-UNSUPPORTED"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
