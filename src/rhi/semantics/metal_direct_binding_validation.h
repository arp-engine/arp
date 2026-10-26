#pragma once

#include "rhi/gpu_descriptors.h"

#include <array>

namespace arp::rhi::semantics {

// MetalOriginalBufferStages/1: native formal signatures may retain inactive
// arguments outside the original visibility. Active native use may not widen it.
inline constexpr bool metalOriginalBindingStagesMatch(
    ShaderStage logical, ShaderStage formal, ShaderStage active, bool compute) noexcept
{
    const auto domain =
        compute ? ShaderStage::Compute : ShaderStage::Vertex | ShaderStage::Fragment;
    return logical != ShaderStage::None && (logical & domain) == logical
        && (formal & domain) == formal && (formal & logical) == logical
        && (active & logical) == active;
}

inline constexpr const char* metalOriginalBufferStagesDiagnostic(
    ShaderStage logical, ShaderStage formal, ShaderStage active, bool compute) noexcept
{
    return metalOriginalBindingStagesMatch(logical, formal, active, compute)
        ? nullptr
        : "RHI-METAL-ORIGINAL-BUFFER-BINDING-STAGES";
}

// MetalOriginalBufferAccess/1. MSL device pointer permission can exceed the
// original Slang/SPIR-V read-only declaration. That declaration must be proved
// independently; native permission never upgrades the L2 access footprint.
inline constexpr const char* metalOriginalBufferAccessDiagnostic(
    DescriptorType type, std::uint32_t access, bool originalNonWritable) noexcept
{
    const bool valid = type == DescriptorType::UniformBuffer ? access == 0u
        : type == DescriptorType::StorageBuffer              ? access == 1u
                                                : type == DescriptorType::ReadOnlyStorageBuffer
            && originalNonWritable && (access == 0u || access == 1u);
    return valid ? nullptr : "RHI-METAL-ORIGINAL-BUFFER-BINDING-ACCESS";
}

// MetalSecondaryHostStorage/1: ICB inheritance contains buffer bindings,
// including argument-buffer roots, but no direct texture/sampler bindings.
inline constexpr bool metalIndirectCommandSupportsDirectBinding(DescriptorType type)
{
    return type == DescriptorType::UniformBuffer || type == DescriptorType::StorageBuffer
        || type == DescriptorType::ReadOnlyStorageBuffer;
}

// MetalBindingSpace/1 direct-slot domain (Metal Feature Set Tables, resource
// limits): buffer [0,31), texture [0,128), sampler [0,16). These are separate
// namespaces, not logical L2 binding limits or a device capability promise.
// Native pipeline creation must still enforce the actual device's limits.
// InputAttachment uses framebuffer-fetch, never a direct resource slot.
// Null validates logical bindings; Vulkan retains set/binding; WebGPU uses
// its group/binding lowering. None inherits Metal's physical slot limits.
inline constexpr bool validMetalDirectBindingSlot(DescriptorType type, std::uint32_t slot)
{
    switch (type) {
    case DescriptorType::UniformBuffer:
    case DescriptorType::StorageBuffer:
    case DescriptorType::ReadOnlyStorageBuffer:
        return slot < 31u;
    case DescriptorType::SampledTexture:
    case DescriptorType::StorageTexture:
        return slot < 128u;
    case DescriptorType::Sampler:
        return slot < 16u;
    case DescriptorType::InputAttachment:
        return false;
    }
    return false;
}

// MetalPhysicalStageBinding/1: populated only by target reflection, never by
// widening the semantic L2 stage declaration. All storage is pipeline-owned.
class MetalPhysicalBindingStages final {
public:
    bool operator==(const MetalPhysicalBindingStages& other) const noexcept
    {
        return buffers_ == other.buffers_ && textures_ == other.textures_
            && samplers_ == other.samplers_;
    }

    bool record(DescriptorType type, std::uint32_t slot, ShaderStage stage)
    {
        if (stage != ShaderStage::Vertex && stage != ShaderStage::Fragment
            && stage != ShaderStage::Compute)
            return false;
        auto* entry = lookup(type, slot);
        if (!entry)
            return false;
        *entry = *entry | stage;
        return true;
    }

    ShaderStage stages(DescriptorType type, std::uint32_t slot) const
    {
        const auto* entry = lookup(type, slot);
        return entry ? *entry : ShaderStage::None;
    }

private:
    ShaderStage* lookup(DescriptorType type, std::uint32_t slot)
    {
        return const_cast<ShaderStage*>(
            static_cast<const MetalPhysicalBindingStages*>(this)->lookup(type, slot));
    }

    const ShaderStage* lookup(DescriptorType type, std::uint32_t slot) const
    {
        if (!validMetalDirectBindingSlot(type, slot))
            return nullptr;
        switch (type) {
        case DescriptorType::UniformBuffer:
        case DescriptorType::StorageBuffer:
        case DescriptorType::ReadOnlyStorageBuffer:
            return &buffers_[slot];
        case DescriptorType::SampledTexture:
        case DescriptorType::StorageTexture:
            return &textures_[slot];
        case DescriptorType::Sampler:
            return &samplers_[slot];
        case DescriptorType::InputAttachment:
            return nullptr;
        }
        return nullptr;
    }

    std::array<ShaderStage, 31> buffers_{};
    std::array<ShaderStage, 128> textures_{};
    std::array<ShaderStage, 16> samplers_{};
};

} // namespace arp::rhi::semantics
