#pragma once
#include "rhi/backends/metal/metal_native_pipelines.h"

#include <numeric>

namespace arp::rhi_metal {
// Shared scalar prerequisites. No caller-supplied fact issues native custody.
class MetalOriginalBufferLimitInspection final {
public:
    static const MetalRhiNativeBindingSlot* mapping(const rhi::ShaderReflectionBinding& binding,
        const MetalRhiNativePipelinePayload& native) noexcept
    {
        const MetalRhiNativeBindingSlot* result = nullptr;
        for (const auto& slot : native.metalBindingSpace)
            if (slot.set == binding.set && slot.binding == binding.binding) {
                if (result)
                    return nullptr;
                result = &slot;
            }
        return result && result->active && result->type == binding.type ? result : nullptr;
    }

    static const MetalRhiBufferBindingFact* fact(
        const core::pmr::Vector<MetalRhiBufferBindingFact>& facts, std::uint32_t slot) noexcept
    {
        const MetalRhiBufferBindingFact* result = nullptr;
        for (const auto& candidate : facts)
            if (candidate.slot == slot) {
                if (result)
                    return nullptr;
                result = &candidate;
            }
        return result;
    }

    static bool validFact(const MetalRhiBufferBindingFact* fact, std::uint64_t ceiling) noexcept
    {
        return fact && fact->alignment && fact->alignment <= UINT32_MAX && fact->requiredBytes
            && fact->requiredBytes <= ceiling;
    }

    static bool accumulate(std::uint32_t& alignment, std::uint64_t next) noexcept
    {
        if (!next || next > UINT32_MAX)
            return false;
        const auto reduced = alignment ? alignment / std::gcd<std::uint64_t>(alignment, next) : 1u;
        if (next > UINT32_MAX / reduced)
            return false;
        alignment = static_cast<std::uint32_t>(reduced * next);
        return true;
    }
};

class MetalOriginalGraphicsBufferLimits final {
public:
    static const char* inspect(const rhi::ShaderReflectionDesc& reflection,
        const MetalRhiNativePipelinePayload& native,
        const rhi::ComputeResourceCapabilities& queried,
        rhi::OriginalDescriptorBufferBindingContract::Limits& output) noexcept
    {
        output = {};
        using Policy = rhi::semantics::DescriptorWholeBufferBinding;
        using Shared = MetalOriginalBufferLimitInspection;
        // Both stage queries are required even for empty coverage. A missing
        // query cannot be interpreted as a stage without buffer arguments.
        for (std::size_t i = 0u; i < native.graphicsBufferFacts.size(); ++i)
            if (native.graphicsBufferFacts[i].version != 1u
                || native.graphicsBufferFacts[i].stage
                    != (i ? rhi::ShaderStage::Fragment : rhi::ShaderStage::Vertex))
                return "RHI-METAL-ACCEPTED-GRAPHICS-BUFFER-FACTS";
        rhi::OriginalDescriptorBufferBindingContract::Limits limits{2u, 0u, 0u, 0u, 0u, 1u, 0u};
        for (std::uint32_t slot = 0u; slot < 31u; ++slot) {
            if (native.nativeBindingStages.stages(rhi::DescriptorType::UniformBuffer, slot)
                == rhi::ShaderStage::None)
                continue;
            bool matched = false;
            for (const auto& binding : reflection.descriptorBindings) {
                if (binding.type != rhi::DescriptorType::UniformBuffer
                    && binding.type != rhi::DescriptorType::StorageBuffer
                    && binding.type != rhi::DescriptorType::ReadOnlyStorageBuffer)
                    continue;
                const auto* mapped = Shared::mapping(binding, native);
                matched |= mapped && mapped->metalSlot == slot;
            }
            if (!matched)
                return "RHI-METAL-ACCEPTED-GRAPHICS-BUFFER-MAPPING";
        }
        for (const auto& stage : native.graphicsBufferFacts)
            for (const auto& fact : stage.entries) {
                bool matched = false;
                for (const auto& binding : reflection.descriptorBindings) {
                    if (binding.type != rhi::DescriptorType::UniformBuffer
                        && binding.type != rhi::DescriptorType::StorageBuffer
                        && binding.type != rhi::DescriptorType::ReadOnlyStorageBuffer)
                        continue;
                    const auto* mapped = Shared::mapping(binding, native);
                    if (mapped && mapped->metalSlot == fact.slot
                        && (native.nativeBindingStages.stages(mapped->type, mapped->metalSlot)
                               & stage.stage)
                            != rhi::ShaderStage::None)
                        matched = true;
                }
                if (!matched)
                    return "RHI-METAL-ACCEPTED-GRAPHICS-BUFFER-FACTS";
            }
        for (const auto& binding : reflection.descriptorBindings) {
            const bool uniform = binding.type == rhi::DescriptorType::UniformBuffer;
            if (!uniform && binding.type != rhi::DescriptorType::StorageBuffer
                && binding.type != rhi::DescriptorType::ReadOnlyStorageBuffer)
                continue;
            const auto* mapped = Shared::mapping(binding, native);
            if (!mapped)
                return "RHI-METAL-ACCEPTED-GRAPHICS-BUFFER-MAPPING";
            // Exact logical and physical uniqueness in the buffer namespace.
            for (const auto& other : reflection.descriptorBindings) {
                if (&other == &binding)
                    continue;
                if (other.set == binding.set && other.binding == binding.binding)
                    return "RHI-METAL-ACCEPTED-GRAPHICS-BUFFER-MAPPING";
            }
            for (const auto& other : native.metalBindingSpace)
                if (&other != mapped && other.active && other.metalSlot == mapped->metalSlot
                    && (other.type == rhi::DescriptorType::UniformBuffer
                        || other.type == rhi::DescriptorType::StorageBuffer
                        || other.type == rhi::DescriptorType::ReadOnlyStorageBuffer))
                    return "RHI-METAL-ACCEPTED-GRAPHICS-BUFFER-MAPPING";
            const auto stages = native.nativeBindingStages.stages(mapped->type, mapped->metalSlot);
            const auto graphics = rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment;
            if (stages == rhi::ShaderStage::None || (stages & graphics) != stages)
                return "RHI-METAL-ACCEPTED-GRAPHICS-BUFFER-MAPPING";
            const auto ceiling = uniform ? queried.maxUniformBufferBindingSizeBytes
                                         : queried.maxStorageBufferBindingSizeBytes;
            auto& alignment =
                uniform ? limits.uniformOffsetAlignment : limits.storageOffsetAlignment;
            for (const auto& stage : native.graphicsBufferFacts) {
                const bool required = (stages & stage.stage) != rhi::ShaderStage::None;
                const auto* fact = Shared::fact(stage.entries, mapped->metalSlot);
                // Reject even duplicate facts in an inactive stage.
                bool present = false;
                for (const auto& candidate : stage.entries)
                    present |= candidate.slot == mapped->metalSlot;
                if ((!required && present) || (required && !Shared::validFact(fact, ceiling)))
                    return "RHI-METAL-ACCEPTED-GRAPHICS-BUFFER-FACTS";
                if (required && !Shared::accumulate(alignment, fact->alignment))
                    return "RHI-METAL-ACCEPTED-GRAPHICS-BUFFER-ALIGNMENT-OVERFLOW";
            }
            limits.bufferKindMask |= uniform ? Policy::uniformKind : Policy::storageKind;
            (uniform ? limits.maxUniformBindingBytes : limits.maxStorageBindingBytes) = ceiling;
        }
        if (const auto* id = rhi::OriginalDescriptorBufferBindingContract::validateLimits(limits))
            return id;
        output = limits;
        return nullptr;
    }
};
} // namespace arp::rhi_metal
