#pragma once
#include "rhi/semantics/bindless_ordinary_depth_domain.h"

namespace arp::rhi::semantics {
// Declaration-only pipeline creation for an explicitly opted-in BindlessCross
// package with PipelineInterface1. Native layout/shader creation does not admit
// a Root native closure, state/content journal, command or submission. In
// particular the existing ordinary-resource closure validator remains closed.
class OriginalBindlessOrdinaryDepthPipelineDeclaration final {
public:
    struct Decision {
        bool declarationAvailable = false, validatorOnly = false;
        bool nativeClosureAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-ORDINARY-DEPTH-PIPELINE-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {false, false, false, false, "RHI-BINDLESS-ORDINARY-DEPTH-PIPELINE-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, false, nullptr};
        case Backend::Vulkan:
            return {true, false, false, false, nullptr};
        case Backend::Metal:
            return {true, false, false, false, nullptr};
        case Backend::WebGPU:
            return {true, false, false, false, nullptr};
        case Backend::WebGL:
            return {false,
                false,
                false,
                false,
                "RHI-WEBGL1-BINDLESS-ORDINARY-DEPTH-PIPELINE-UNSUPPORTED"};
        }
        return {};
    }

    static const char* inspect(Backend backend, const DescriptorBindingDesc& binding) noexcept
    {
        const auto decision = lowering(backend);
        if (!decision.declarationAvailable)
            return decision.diagnostic;
        return OriginalBindlessOrdinaryDepthDomain::acceptsBinding(binding)
                && ((backend != Backend::Metal && backend != Backend::WebGPU)
                    || binding.stages == ShaderStage::Fragment)
            ? nullptr
            : "RHI-BINDLESS-ORDINARY-DEPTH-PIPELINE-DECLARATION";
    }
};
} // namespace arp::rhi::semantics
