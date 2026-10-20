#pragma once
#include "rhi/gpu_descriptors.h"

namespace arp::rhi::semantics {
// ES100 descriptor state is backend-owned metadata, never native descriptor
// handles or an advertised executable command payload. Record must snapshot it.
class WebGlDescriptorCreationContract final {
public:
    static const char* arena(Backend backend, const DescriptorArenaDesc& d) noexcept
    {
        if (!d.maxSets || !d.maxDescriptors)
            return "RHI-WEBGL1-DESCRIPTOR-ARENA-BUDGET";
        switch (backend) {
        case Backend::WebGL:
            return nullptr;
        case Backend::Null:
            return "RHI-WEBGL1-DESCRIPTOR-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return "RHI-WEBGL1-DESCRIPTOR-BACKEND-NOT-APPLICABLE";
        }
        return "RHI-WEBGL1-DESCRIPTOR-BACKEND-UNKNOWN";
    }

    static const char* binding(const DescriptorBindingDesc& b) noexcept
    {
        if ((b.type != DescriptorType::UniformBuffer && b.type != DescriptorType::SampledTexture
                && b.type != DescriptorType::Sampler)
            || b.arrayCount != 1 || b.tableBacked || b.depthTexture || b.comparisonSampler
            || (b.textureViewDimensionConstrained
                && b.textureViewDimension != TextureViewDimension::Texture2D)
            || (b.dynamicOffset && b.type != DescriptorType::UniformBuffer)
            || b.stages == ShaderStage::None
            || (static_cast<std::uint32_t>(b.stages)
                & ~static_cast<std::uint32_t>(ShaderStage::Vertex | ShaderStage::Fragment)))
            return "RHI-WEBGL1-PIPELINE-LAYOUT-BINDING";
        return nullptr;
    }
};
} // namespace arp::rhi::semantics
