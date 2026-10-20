#pragma once

#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/gpu_pipeline.h"

namespace arp::rhi {

inline bool descriptorSetStageCoverage(ShaderStage available, ShaderStage required)
{
    return (available & required) == required;
}

inline const DescriptorLayoutDesc* findDescriptorSetLayout(
    const PipelineLayoutDesc& layout, const std::pmr::string& name)
{
    for (const auto& descriptorLayout : layout.descriptorLayouts) {
        if (descriptorLayout.name == name)
            return &descriptorLayout;
    }
    return nullptr;
}

inline const DescriptorSetSlotDesc* findDescriptorSetSlot(
    const PipelineLayoutDesc& layout, std::uint32_t set)
{
    for (const auto& slot : layout.descriptorSets) {
        if (slot.set == set)
            return &slot;
    }
    return nullptr;
}

inline const DescriptorBindingDesc* findDescriptorSetBinding(
    const DescriptorLayoutDesc& layout, std::uint32_t binding)
{
    for (const auto& descriptor : layout.bindings) {
        if (descriptor.binding == binding)
            return &descriptor;
    }
    return nullptr;
}

// Exact authored binding shape, with the existing visibility coverage rule.
// Shared by prepared binding validators; no backend-specific type guessing.
inline bool descriptorBindingLayoutMatches(
    const DescriptorBindingDesc& expected, const DescriptorBindingDesc& actual) noexcept
{
    return expected.binding == actual.binding && expected.type == actual.type
        && descriptorSetStageCoverage(actual.stages, expected.stages)
        && expected.arrayCount == actual.arrayCount && expected.tableBacked == actual.tableBacked
        && expected.dynamicOffset == actual.dynamicOffset
        && expected.textureViewDimensionConstrained == actual.textureViewDimensionConstrained
        && expected.textureViewDimension == actual.textureViewDimension
        && expected.depthTexture == actual.depthTexture
        && expected.storageTextureFormat == actual.storageTextureFormat
        && expected.storageTextureAccess == actual.storageTextureAccess
        && expected.sampledTextureFilterable == actual.sampledTextureFilterable
        && expected.comparisonSampler == actual.comparisonSampler;
}

inline bool descriptorBindingWriteMatches(
    const DescriptorBindingWrite& expected, const DescriptorBindingWrite& actual) noexcept
{
    return expected.binding == actual.binding && expected.type == actual.type
        && expected.resource == actual.resource && expected.resourceKind == actual.resourceKind
        && expected.resourceState == actual.resourceState
        && expected.arrayElement == actual.arrayElement
        && expected.bufferBindingRange.matches(actual.bufferBindingRange)
        && expected.physicalLowering.enabled == actual.physicalLowering.enabled
        && expected.physicalLowering.physicalType == actual.physicalLowering.physicalType
        && expected.physicalLowering.logicalAttachmentIndex
        == actual.physicalLowering.logicalAttachmentIndex
        && expected.physicalLowering.shaderPackageHash == actual.physicalLowering.shaderPackageHash;
}

inline bool descriptorTableWriteMatches(
    const DescriptorTableWrite& expected, const DescriptorTableWrite& actual) noexcept
{
    return expected.index == actual.index && expected.type == actual.type
        && expected.resource == actual.resource && expected.resourceKind == actual.resourceKind;
}

inline bool descriptorTableDefinitionMatches(
    const DescriptorTableDesc& expected, const DescriptorTableDesc& actual) noexcept
{
    return expected.label == actual.label && expected.type == actual.type
        && expected.stages == actual.stages && expected.capacity == actual.capacity
        && expected.textureViewDimensionConstrained == actual.textureViewDimensionConstrained
        && expected.textureViewDimension == actual.textureViewDimension
        && expected.depthTexture == actual.depthTexture;
}

inline bool descriptorLayoutDefinitionMatches(
    const DescriptorLayoutDesc& expected, const DescriptorLayoutDesc& actual) noexcept
{
    if (expected.name != actual.name || expected.bindings.size() != actual.bindings.size())
        return false;
    for (const auto& required : expected.bindings) {
        const auto* binding = findDescriptorSetBinding(actual, required.binding);
        if (!binding || required.stages != binding->stages
            || !descriptorBindingLayoutMatches(required, *binding))
            return false;
    }
    return true;
}

// Shape only: no descriptor content, original custody, range or native proof.
// ExactOriginal/1 is an explicitly selected positional prerequisite; generic
// table validation retains its existing Coverage rules and diagnostics.
class DescriptorSlotShapeContract final {
public:
    enum class TableMatch { Coverage, ExactOriginal };

    static const DescriptorTableSlotDesc* findTableSlot(
        const PipelineLayoutDesc& layout, std::uint32_t set) noexcept
    {
        for (const auto& slot : layout.descriptorTables)
            if (slot.set == set)
                return &slot;
        return nullptr;
    }

    static const char* validateSet(const PipelineLayoutDesc& layout,
        std::uint32_t set,
        const DescriptorLayoutDesc& actual) noexcept
    {
        const auto* slot = findDescriptorSetSlot(layout, set);
        const auto* expected = slot ? findDescriptorSetLayout(layout, slot->layoutName) : nullptr;
        return expected && descriptorLayoutDefinitionMatches(*expected, actual)
            ? nullptr
            : "RHI-DESCRIPTOR-LAYOUT-CONTRACT-MISMATCH";
    }

    static const char* validateTable(const PipelineLayoutDesc& layout,
        std::uint32_t set,
        const DescriptorTableDesc& table,
        TableMatch match) noexcept
    {
        if (match != TableMatch::Coverage && match != TableMatch::ExactOriginal)
            return "RHI-DESCRIPTOR-TABLE-SHAPE-MATCH-MODE";
        const auto* slot = findTableSlot(layout, set);
        if (!slot)
            return "descriptor table slot is not declared by pipeline layout";
        if (slot->tableName != table.label)
            return "descriptor table name does not match pipeline layout";
        if (slot->type != table.type)
            return "descriptor table type does not match pipeline layout";
        if ((table.stages & slot->stages) != slot->stages)
            return "descriptor table shader visibility does not cover pipeline layout";
        if (table.capacity < slot->capacity)
            return "descriptor table capacity does not cover pipeline layout";
        if (slot->depthTexture != table.depthTexture
            || slot->textureViewDimensionConstrained != table.textureViewDimensionConstrained
            || (slot->textureViewDimensionConstrained
                && slot->textureViewDimension != table.textureViewDimension))
            return "RHI-DESCRIPTOR-TABLE-SHAPE-MISMATCH: table texture shape does not match "
                   "pipeline layout";
        if (match == TableMatch::ExactOriginal
            && (table.stages != slot->stages || table.capacity != slot->capacity))
            return "RHI-DESCRIPTOR-TABLE-EXACT-SLOT-MISMATCH";
        return nullptr;
    }
};

inline bool descriptorSetLayoutMatches(
    const DescriptorLayoutDesc& expected, const DescriptorLayoutDesc& actual)
{
    if (expected.name != actual.name || expected.bindings.size() != actual.bindings.size()) {
        return false;
    }
    for (const auto& expectedBinding : expected.bindings) {
        const auto* actualBinding = findDescriptorSetBinding(actual, expectedBinding.binding);
        if (!actualBinding || actualBinding->type != expectedBinding.type
            || !descriptorSetStageCoverage(actualBinding->stages, expectedBinding.stages)) {
            return false;
        }
    }
    return true;
}

// Complete authored shape for preparation, beyond legacy type/stage coverage.
inline ValidationResult validateExactDescriptorSetLayout(
    const DescriptorLayoutDesc& expected, const DescriptorSet& actual)
{
    if (!descriptorLayoutDefinitionMatches(expected, actual.layout()))
        return {false, "RHI-DESCRIPTOR-LAYOUT-CONTRACT-MISMATCH"};
    return validateDescriptorResources(actual, DescriptorContentRequirement::WrittenElements);
}

// Owns only L2 descriptor-set contracts; no caller layout/native references.
class DescriptorSetLayoutContract final {
    struct Set {
        std::uint32_t index = 0u;
        DescriptorLayoutDesc layout{core::pmr::String{&core::defaultCpuMemoryResource()},
            core::pmr::Vector<DescriptorBindingDesc>{&core::defaultCpuMemoryResource()}};
    };

public:
    explicit DescriptorSetLayoutContract(const PipelineLayoutDesc& source)
    {
        if (!validatePipelineLayout(source).success) {
            valid_ = false;
            return;
        }
        sets_.reserve(source.descriptorSets.size());
        for (const auto& slot : source.descriptorSets) {
            const auto* expected = findDescriptorSetLayout(source, slot.layoutName);
            if (!expected) {
                valid_ = false;
                return;
            }
            Set set;
            set.index = slot.set;
            set.layout.name = expected->name;
            set.layout.bindings = expected->bindings;
            sets_.push_back(std::move(set));
        }
    }

    DescriptorSetLayoutContract(const DescriptorSetLayoutContract&) = delete;
    DescriptorSetLayoutContract& operator=(const DescriptorSetLayoutContract&) = delete;

    bool valid() const noexcept { return valid_; }

    const DescriptorLayoutDesc* find(std::uint32_t index) const noexcept
    {
        for (const auto& set : sets_)
            if (set.index == index)
                return &set.layout;
        return nullptr;
    }

    ValidationResult validate(std::uint32_t index, const DescriptorSet& actual) const
    {
        if (!valid_)
            return {false, "RHI-DESCRIPTOR-LAYOUT-CONTRACT-INVALID"};
        const auto* expected = find(index);
        if (!expected)
            return {false, "descriptor set slot is not declared by pipeline layout"};
        return validateExactDescriptorSetLayout(*expected, actual);
    }

private:
    core::pmr::Vector<Set> sets_{&core::defaultCpuMemoryResource()};
    bool valid_ = true;
};

inline ValidationResult validateDescriptorSetBinding(
    const PipelineLayout* pipelineLayout, std::uint32_t set, const DescriptorSet& descriptorSet)
{
    if (!pipelineLayout)
        return {false, "pipeline must be bound before descriptor set"};
    if (descriptorSet.hasOriginalFiniteContent())
        return {false, "RHI-ORIGINAL-FINITE-WRITE-EMISSION-CLOSED"};
    if (!descriptorSet.isCurrentAllocation()) {
        return {false, "descriptor set bind rejected after descriptor arena reset"};
    }

    const auto& layout = pipelineLayout->desc();
    const auto* slot = findDescriptorSetSlot(layout, set);
    if (!slot)
        return {false, "descriptor set slot is not declared by pipeline layout"};
    if (slot->layoutName != descriptorSet.layout().name) {
        return {false, "descriptor set layout name does not match pipeline layout"};
    }
    if (!layout.descriptorLayouts.empty()) {
        const auto* expected = findDescriptorSetLayout(layout, slot->layoutName);
        if (!expected || !descriptorSetLayoutMatches(*expected, descriptorSet.layout())) {
            return {false, "descriptor set layout does not match pipeline layout"};
        }
    }
    return validateDescriptorResources(
        descriptorSet, DescriptorContentRequirement::WrittenElements);
}

} // namespace arp::rhi
