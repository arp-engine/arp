#pragma once
#include "rhi/gpu_bindless.h"

namespace arp::rhi::semantics {
// Fragment scalar storage-buffer Store/1 preserves the existing table-index,
// element bounds, fault and write-visibility semantics. This is an operation
// mapping only; device opt-in, queried limits, original Root, state journal and
// genuine execution/completion authority remain independent prerequisites.
class OriginalBindlessFragmentStoreLowering final {
public:
    struct Decision {
        BindlessExecution execution = BindlessExecution::DiagnosticFailure;
        const char* diagnostic = "RHI-BINDLESS-FRAGMENT-STORE-BACKEND";
    };

    static constexpr Decision lowering(Backend backend,
        ShaderStage stage,
        DescriptorType type,
        std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {BindlessExecution::DiagnosticFailure, "RHI-BINDLESS-FRAGMENT-STORE-VERSION"};
        if (stage != ShaderStage::Fragment || type != DescriptorType::StorageBuffer)
            return {BindlessExecution::DiagnosticFailure, "RHI-BINDLESS-FRAGMENT-STORE-DOMAIN"};
        switch (backend) {
        case Backend::Null:
            return {BindlessExecution::Validator, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
            return {BindlessExecution::Native, nullptr};
        case Backend::WebGPU:
            return {BindlessExecution::Emulated, nullptr};
        case Backend::WebGL:
            return {BindlessExecution::DiagnosticFailure,
                "RHI-WEBGL1-BINDLESS-FRAGMENT-STORE-UNSUPPORTED"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
