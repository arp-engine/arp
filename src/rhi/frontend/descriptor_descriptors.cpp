#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/gpu_descriptors.h"

namespace arp::rhi {

namespace {

bool rangeEndOverflows(const PushConstantRange& range)
{
    return range.offsetBytes + range.sizeBytes < range.offsetBytes;
}

bool rangesOverlap(const PushConstantRange& lhs, const PushConstantRange& rhs)
{
    const auto lhsEnd = lhs.offsetBytes + lhs.sizeBytes;
    const auto rhsEnd = rhs.offsetBytes + rhs.sizeBytes;
    return lhs.offsetBytes < rhsEnd && rhs.offsetBytes < lhsEnd;
}

} // namespace

ValidationResult validateSampledDepthView(
    bool depthRequired, TextureFormat format, TextureAspect aspect)
{
    if (const auto* id = sampledDepthViewDiagnostic(depthRequired, format, aspect))
        return {false, id};
    return {true, ""};
}

ValidationResult countDescriptorElements(const DescriptorLayoutDesc& desc, std::uint32_t& count)
{
    count = 0u;
    for (const auto& binding : desc.bindings) {
        if (binding.arrayCount == 0u || binding.arrayCount > UINT32_MAX - count) {
            count = 0u;
            return {
                false, "RHI-DESCRIPTOR-ELEMENT-CAPACITY: invalid or overflowing array capacity"};
        }
        count += binding.arrayCount;
    }
    return {true, {}};
}

std::uint32_t descriptorElementIndex(
    const DescriptorLayoutDesc& desc, std::uint32_t binding, std::uint32_t arrayElement)
{
    std::uint32_t offset = 0u;
    for (const auto& candidate : desc.bindings) {
        if (candidate.arrayCount == 0u || candidate.arrayCount > UINT32_MAX - offset)
            return UINT32_MAX;
        if (candidate.binding == binding)
            return arrayElement < candidate.arrayCount ? offset + arrayElement : UINT32_MAX;
        offset += candidate.arrayCount;
    }
    return UINT32_MAX;
}

ValidationResult validateDescriptorTextureRequirement(
    DescriptorType type, bool dimensionConstrained, bool depthTexture)
{
    if (depthTexture && type != DescriptorType::SampledTexture
        && type != DescriptorType::InputAttachment)
        return {false,
            "RHI-SAMPLED-DEPTH-LAYOUT: depth requirement needs a sampled texture or input "
            "attachment"};
    if (dimensionConstrained && type != DescriptorType::SampledTexture
        && type != DescriptorType::StorageTexture)
        return {
            false, "descriptor texture view dimension contract requires a texture descriptor type"};
    return {true, {}};
}

ValidationResult validateDescriptorTableSlot(
    const PipelineLayoutDesc& layout, std::uint32_t set, const DescriptorTableDesc& table)
{
    const auto* diagnostic = DescriptorSlotShapeContract::validateTable(
        layout, set, table, DescriptorSlotShapeContract::TableMatch::Coverage);
    return {diagnostic == nullptr, diagnostic ? diagnostic : ""};
}

ValidationResult validateDescriptorLayout(const DescriptorLayoutDesc& desc)
{
    if (desc.name.empty()) {
        return {false, "descriptor layout has no name"};
    }
    for (std::size_t index = 0; index < desc.bindings.size(); ++index) {
        const auto& binding = desc.bindings[index];
        if (binding.stages == ShaderStage::None) {
            return {false, "descriptor binding has no shader stage visibility"};
        }
        if (binding.arrayCount == 0) {
            return {false, "descriptor binding array count must be greater than zero"};
        }
        if (binding.type == DescriptorType::StorageTexture
            && binding.storageTextureAccess != StorageTextureAccess::WriteOnly
            && binding.storageTextureAccess != StorageTextureAccess::ReadOnly
            && binding.storageTextureAccess != StorageTextureAccess::ReadWrite) {
            return {false, "RHI-STORAGE-TEXTURE-ACCESS-INVALID"};
        }
        if (binding.type != DescriptorType::StorageTexture
            && binding.storageTextureAccess != StorageTextureAccess::WriteOnly) {
            return {false, "RHI-STORAGE-TEXTURE-ACCESS-INVALID"};
        }
        const auto textureRequirement = validateDescriptorTextureRequirement(
            binding.type, binding.textureViewDimensionConstrained, binding.depthTexture);
        if (!textureRequirement.success)
            return textureRequirement;
        if (binding.dynamicOffset && binding.type != DescriptorType::UniformBuffer
            && binding.type != DescriptorType::StorageBuffer
            && binding.type != DescriptorType::ReadOnlyStorageBuffer) {
            return {false, "descriptor binding dynamic offset requires a buffer descriptor type"};
        }
        if (binding.dynamicOffset && binding.arrayCount != 1u) {
            return {false, "descriptor binding dynamic offset does not support descriptor arrays"};
        }
        for (std::size_t other = index + 1; other < desc.bindings.size(); ++other) {
            if (desc.bindings[other].binding == binding.binding) {
                return {false, "descriptor binding index is duplicated"};
            }
        }
    }
    std::uint32_t count = 0u;
    return countDescriptorElements(desc, count);
}

ValidationResult validatePipelineLayout(const PipelineLayoutDesc& desc)
{
    for (std::size_t index = 0; index < desc.descriptorLayouts.size(); ++index) {
        const auto& descriptorLayout = desc.descriptorLayouts[index];
        if (descriptorLayout.name.empty()) {
            return {false, "descriptor layout registry entry has no name"};
        }
        const auto descriptorValidation = validateDescriptorLayout(descriptorLayout);
        if (!descriptorValidation.success) {
            return descriptorValidation;
        }
        for (std::size_t other = index + 1; other < desc.descriptorLayouts.size(); ++other) {
            if (desc.descriptorLayouts[other].name == descriptorLayout.name) {
                return {false, "descriptor layout registry name is duplicated"};
            }
        }
    }

    for (std::size_t index = 0; index < desc.descriptorSets.size(); ++index) {
        const auto& slot = desc.descriptorSets[index];
        if (slot.layoutName.empty()) {
            return {false, "descriptor set slot has no layout name"};
        }
        if (!desc.descriptorLayouts.empty()) {
            bool layoutFound = false;
            for (const auto& descriptorLayout : desc.descriptorLayouts) {
                if (descriptorLayout.name == slot.layoutName) {
                    layoutFound = true;
                    break;
                }
            }
            if (!layoutFound) {
                return {
                    false, "pipeline layout descriptor set references unknown descriptor layout"};
            }
        }
        for (std::size_t other = index + 1; other < desc.descriptorSets.size(); ++other) {
            if (desc.descriptorSets[other].set == slot.set) {
                return {false, "pipeline layout descriptor set index is duplicated"};
            }
        }
    }

    for (std::size_t index = 0; index < desc.descriptorTables.size(); ++index) {
        const auto& slot = desc.descriptorTables[index];
        if (slot.tableName.empty()) {
            return {false, "descriptor table slot has no table name"};
        }
        if (slot.stages == ShaderStage::None) {
            return {false, "descriptor table slot has no shader stage visibility"};
        }
        if (slot.capacity == 0) {
            return {false, "descriptor table slot has no capacity"};
        }
        const auto textureRequirement = validateDescriptorTextureRequirement(
            slot.type, slot.textureViewDimensionConstrained, slot.depthTexture);
        if (!textureRequirement.success)
            return textureRequirement;
        for (const auto& setSlot : desc.descriptorSets) {
            if (setSlot.set == slot.set) {
                return {false, "pipeline layout descriptor slot index is duplicated"};
            }
        }
        for (std::size_t other = index + 1; other < desc.descriptorTables.size(); ++other) {
            if (desc.descriptorTables[other].set == slot.set) {
                return {false, "pipeline layout descriptor table index is duplicated"};
            }
        }
    }

    for (std::size_t index = 0; index < desc.pushConstantRanges.size(); ++index) {
        const auto& range = desc.pushConstantRanges[index];
        if (range.stages == ShaderStage::None) {
            return {false, "push constant range has no shader stage visibility"};
        }
        if (range.sizeBytes == 0) {
            return {false, "push constant range is empty"};
        }
        if (rangeEndOverflows(range)) {
            return {false, "push constant range offset and size overflow"};
        }
        for (std::size_t other = index + 1; other < desc.pushConstantRanges.size(); ++other) {
            const auto& otherRange = desc.pushConstantRanges[other];
            if ((range.stages & otherRange.stages) != ShaderStage::None
                && rangesOverlap(range, otherRange)) {
                return {false, "push constant ranges overlap for the same shader stage"};
            }
        }
    }
    return {true, ""};
}

} // namespace arp::rhi

// Canonical original descriptor dependency definitions share this L2 module.

// Inline definitions follow the complete public declarations.
#include "rhi/frontend/command_descriptor_dependencies.inl"
