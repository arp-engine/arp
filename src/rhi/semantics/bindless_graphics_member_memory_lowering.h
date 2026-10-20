#pragma once
#include "rhi/gpu_common.h"
#include "rhi/semantics/shader_buffer_member_memory.h"

namespace arp::rhi::semantics {
class OriginalBindlessGraphicsMemberMemoryLowering final {
public:
    using Evidence = ShaderBufferMemberMemory::Evidence;

    struct Decision {
        bool available = false, validatorOnly = false, executionAvailable = false;
        Evidence evidence = Evidence::None;
        const char* diagnostic = "RHI-BINDLESS-MEMBER-MEMORY-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u && version != 2u)
            return {false, false, false, Evidence::None, "RHI-BINDLESS-MEMBER-MEMORY-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, Evidence::ValidatedTargetMembers, nullptr};
        case Backend::Vulkan:
            return {true, false, false, Evidence::NativeBinaryIdentity, nullptr};
        case Backend::Metal:
            return {true, false, false, Evidence::ValidatedTargetMembers, nullptr};
        case Backend::WebGPU:
            return {true, false, false, Evidence::ValidatedTargetMembers, nullptr};
        case Backend::WebGL:
            return {false,
                false,
                false,
                Evidence::None,
                "RHI-WEBGL1-BINDLESS-MEMBER-MEMORY-UNSUPPORTED"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
