#pragma once

#include "rhi/gpu_descriptors.h"

namespace arp::rhi {

// A non-filtering layout is an input restriction. Never rewrite an authored
// sampler to fit it: callers must choose the matching sampler before binding.
inline const char* validateNonFilteringSamplerBinding(
    bool filteringAllowed, const SamplerDesc& sampler) noexcept
{
    return !filteringAllowed
            && (sampler.minFilter != SamplerFilter::Nearest
                || sampler.magFilter != SamplerFilter::Nearest
                || sampler.mipFilter == SamplerMipFilter::Linear || sampler.maxAnisotropy > 1.0f)
        ? "RHI-DESCRIPTOR-NONFILTERING-SAMPLER"
        : nullptr;
}

inline const char* sampledDepthViewDiagnostic(
    bool depthRequired, TextureFormat format, TextureAspect aspect) noexcept
{
    return depthRequired && (!isDepthTextureFormat(format) || aspect != TextureAspect::Depth)
        ? "RHI-SAMPLED-DEPTH-VIEW-MISMATCH: depth binding requires a depth-format, depth-aspect view"
        : nullptr;
}

// Exact authored storage-format correspondence, independent of native format
// support. Callers must separately validate usage/dimension/liveness/capability.
inline const char* validateStorageTextureViewFormat(
    TextureFormat declaredFormat, TextureFormat viewFormat) noexcept
{
    if (declaredFormat == TextureFormat::Unknown || viewFormat == TextureFormat::Unknown)
        return "RHI-DESCRIPTOR-STORAGE-FORMAT-MISSING";
    return declaredFormat == viewFormat ? nullptr : "RHI-DESCRIPTOR-STORAGE-FORMAT-MISMATCH";
}

inline ValidationResult validateDescriptorTextureParent(bool parentLive)
{
    return parentLive ? ValidationResult{true, {}}
                      : ValidationResult{false,
                            "RHI-DESCRIPTOR-PARENT-NOT-LIVE: descriptor texture view parent "
                            "texture is not registered"};
}

// Binding liveness and content completeness are distinct contracts. Callers
// retain their existing completeness requirement rather than forcing unused
// layout slots to become mandatory merely to validate written identities.
enum class DescriptorContentRequirement {
    WrittenElements,
    CompleteLayout,
};

inline ValidationResult validateDescriptorResources(
    const DescriptorSet& set, DescriptorContentRequirement content)
{
    if (!set.isCurrentAllocation()) {
        return {false, "RHI-DESCRIPTOR-ALLOCATION-STALE: descriptor set bind"};
    }
    for (const auto& binding : set.layout().bindings) {
        for (std::uint32_t element = 0u; element < binding.arrayCount; ++element) {
            if (content == DescriptorContentRequirement::WrittenElements
                && !set.hasWrittenBindingElement(binding.binding, element))
                continue;
            const auto result = set.validateBindingElement(binding.binding, element);
            if (!result.success)
                return result;
        }
    }
    return {true, {}};
}

inline ValidationResult validateDescriptorResources(
    const DescriptorTable& table, DescriptorContentRequirement content)
{
    if (!table.isCurrentAllocation()) {
        return {false, "RHI-DESCRIPTOR-ALLOCATION-STALE: descriptor table bind"};
    }
    for (std::uint32_t index = 0u; index < table.desc().capacity; ++index) {
        if (content == DescriptorContentRequirement::WrittenElements
            && !table.hasWrittenDescriptor(index))
            continue;
        const auto result = table.validateDescriptor(index);
        if (!result.success)
            return result;
    }
    return {true, {}};
}

} // namespace arp::rhi
