#pragma once
#include "rhi/frontend/bindless_completed_image_domain.h"

namespace arp::rhi::semantics {
// The Native public Graphics/1 and Compute/8 adapters share only the completed
// image prerequisite. WebGPU's bounded callback Graphics3/4 remains a separate
// executor and cannot consume this Native prerequisite.
class OriginalBindlessCompletedImageLowering final {
public:
    struct Decision {
        bool preparationAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-COMPLETED-IMAGE-BACKEND";
    };

    static constexpr Decision lowering(Backend backend,
        OriginalBindlessCompletedImageScope scope,
        std::uint32_t version = 1u) noexcept
    {
        if (version != OriginalBindlessCompletedImageDomain::contractVersion)
            return {false, "RHI-BINDLESS-COMPLETED-IMAGE-VERSION"};
        if (scope != OriginalBindlessCompletedImageScope::ComputeColor8
            && scope != OriginalBindlessCompletedImageScope::SampledGraphics1)
            return {false, "RHI-BINDLESS-COMPLETED-IMAGE-SCOPE"};
        switch (backend) {
        case Backend::Null:
            return {false, "RHI-BINDLESS-COMPLETED-IMAGE-NULL-VALIDATOR"};
        case Backend::Vulkan:
        case Backend::Metal:
            return {true, nullptr};
        case Backend::WebGPU:
            return {false, "RHI-BINDLESS-COMPLETED-IMAGE-NATIVE-PROFILE"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
