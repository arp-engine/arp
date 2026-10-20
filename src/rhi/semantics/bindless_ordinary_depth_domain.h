#pragma once
#include "rhi/gpu_bindless.h"

namespace arp::rhi::semantics {
// OrdinaryDepthDomain/1 describes scalar D32/comparison declarations and the
// selected stage of an explicit pair. It does not infer shader resource-use
// edges, authenticate a package/Root, or authorize native descriptor creation.
class OriginalBindlessOrdinaryDepthDomain final {
public:
    static bool acceptsStages(ShaderStage stages) noexcept
    {
        const auto supported = ShaderStage::Fragment | ShaderStage::Compute;
        return stages != ShaderStage::None && (stages & supported) == stages;
    }

    static bool acceptsBinding(const DescriptorBindingDesc& binding) noexcept
    {
        if (binding.arrayCount != 1u || binding.dynamicOffset || binding.tableBacked
            || !acceptsStages(binding.stages))
            return false;
        if (binding.type == DescriptorType::Sampler)
            return binding.comparisonSampler;
        return binding.type == DescriptorType::SampledTexture && binding.depthTexture
            && binding.textureViewDimensionConstrained
            && binding.textureViewDimension == TextureViewDimension::Texture2D;
    }

    // Both declarations must come from queries of the same authenticated Root
    // and selected entry. This value check alone cannot establish that origin.
    static const char* inspectSelectedDeclarations(const DescriptorBindingDesc& depth,
        const DescriptorBindingDesc& sampler,
        ShaderStage selected) noexcept
    {
        if (depth.type != DescriptorType::SampledTexture || !acceptsBinding(depth)
            || sampler.type != DescriptorType::Sampler || !acceptsBinding(sampler))
            return "RHI-BINDLESS-ORDINARY-DEPTH-DECLARATION";
        if ((selected != ShaderStage::Fragment && selected != ShaderStage::Compute)
            || (depth.stages & selected) != selected || (sampler.stages & selected) != selected)
            return "RHI-BINDLESS-ORDINARY-DEPTH-SELECTED-STAGE";
        return nullptr;
    }

    struct Decision {
        bool inspectionAvailable = false, validatorOnly = false, nativeClosureAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-ORDINARY-DEPTH-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {false, false, false, "RHI-BINDLESS-ORDINARY-DEPTH-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, "RHI-BINDLESS-ORDINARY-DEPTH-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {true, false, false, "RHI-VULKAN-BINDLESS-ORDINARY-DEPTH-CLOSURE-UNAVAILABLE"};
        case Backend::Metal:
            return {true, false, false, "RHI-METAL-BINDLESS-ORDINARY-DEPTH-CLOSURE-UNAVAILABLE"};
        case Backend::WebGPU:
            return {true, false, false, "RHI-WEBGPU-BINDLESS-ORDINARY-DEPTH-CLOSURE-UNAVAILABLE"};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-BINDLESS-ORDINARY-DEPTH-UNSUPPORTED"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
