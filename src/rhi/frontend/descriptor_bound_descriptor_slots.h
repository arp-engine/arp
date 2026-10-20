#pragma once

#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/frontend/descriptor_validation.h"

#include <array>

namespace arp::rhi {

// Shared, bounded recording observations. Native callers preflight capacity,
// perform native binding, then commit the observation in the same serialized call.
class BoundDescriptorSlots final {
public:
    static constexpr std::uint32_t kMaxTrackedSlots = 32u;

    ValidationResult validateSetCapacity(std::uint32_t set) const
    {
        return findSet(set) || setCount_ < kMaxTrackedSlots
            ? ValidationResult{true, {}}
            : ValidationResult{false,
                  "RHI-DESCRIPTOR-TRACKING-CAPACITY: descriptor set tracking capacity exhausted"};
    }

    ValidationResult validateTableCapacity(std::uint32_t set) const
    {
        return findTable(set) || tableCount_ < kMaxTrackedSlots
            ? ValidationResult{true, {}}
            : ValidationResult{false,
                  "RHI-DESCRIPTOR-TRACKING-CAPACITY: descriptor table tracking capacity exhausted"};
    }

    ValidationResult validateLiveBindings() const
    {
        for (std::uint32_t i = 0; i < setCount_; ++i) {
            const auto* object = sets_[i].descriptorSet.get();
            if (!object)
                return {false, "RHI-DESCRIPTOR-OBJECT-NOT-LIVE: set consumed"};
            const auto result =
                validateDescriptorResources(*object, DescriptorContentRequirement::WrittenElements);
            if (!result.success)
                return result;
            if (object->contentRevision() != sets_[i].revision)
                return {false, "RHI-DESCRIPTOR-CONTENT-CHANGED: rebind set before consumption"};
        }
        for (std::uint32_t i = 0; i < tableCount_; ++i) {
            const auto* object = tables_[i].descriptorTable.get();
            if (!object)
                return {false, "RHI-DESCRIPTOR-OBJECT-NOT-LIVE: table consumed"};
            const auto result =
                validateDescriptorResources(*object, DescriptorContentRequirement::WrittenElements);
            if (!result.success)
                return result;
            if (object->contentRevision() != tables_[i].revision)
                return {false, "RHI-DESCRIPTOR-CONTENT-CHANGED: rebind table before consumption"};
        }
        return {true, {}};
    }

    void reset()
    {
        for (std::uint32_t i = 0; i < setCount_; ++i)
            sets_[i] = {};
        for (std::uint32_t i = 0; i < tableCount_; ++i)
            tables_[i] = {};
        setCount_ = 0;
        tableCount_ = 0;
    }

    ValidationResult bindSet(std::uint32_t set,
        const DescriptorSet& descriptorSet,
        const std::uint32_t* dynamicOffsets,
        std::uint32_t dynamicOffsetCount)
    {
        if (auto* binding = findSet(set)) {
            binding->descriptorSet = {&descriptorSet, descriptorSet.lifetimeTicket()};
            binding->revision = descriptorSet.contentRevision();
            return {true, ""};
        }
        if (setCount_ >= kMaxTrackedSlots) {
            return {false, "descriptor set tracking capacity exhausted"};
        }
        sets_[setCount_] = {
            set, {&descriptorSet, descriptorSet.lifetimeTicket()}, descriptorSet.contentRevision()};
        ++setCount_;
        return {true, ""};
    }

    ValidationResult bindTable(std::uint32_t set, const DescriptorTable& descriptorTable)
    {
        if (auto* binding = findTable(set)) {
            binding->descriptorTable = {&descriptorTable, descriptorTable.lifetimeTicket()};
            binding->revision = descriptorTable.contentRevision();
            return {true, ""};
        }
        if (tableCount_ >= kMaxTrackedSlots) {
            return {false, "descriptor table tracking capacity exhausted"};
        }
        tables_[tableCount_] = {set,
            {&descriptorTable, descriptorTable.lifetimeTicket()},
            descriptorTable.contentRevision()};
        ++tableCount_;
        return {true, ""};
    }

    ValidationResult validateComplete(const PipelineLayout* pipelineLayout,
        const ShaderReflectionDesc* shaderReflection,
        ShaderStage requiredStages) const
    {
        const auto live = validateLiveBindings();
        if (!live.success)
            return live;
        if (!pipelineLayout) {
            return {false, "pipeline must be bound before descriptor completeness validation"};
        }
        if (!shaderReflection) {
            return {false,
                "pipeline shader reflection is required for descriptor completeness validation"};
        }

        return validateComplete(pipelineLayout->desc(), *shaderReflection, requiredStages);
    }

    // Prepared backend bindings own immutable L2 metadata rather than a raw
    // public pipeline wrapper. Share the same completeness/liveness algorithm.
    ValidationResult validateComplete(const PipelineLayoutDesc& layout,
        const ShaderReflectionDesc& shaderReflection,
        ShaderStage requiredStages) const
    {
        return validateCompleteBindings(
            shaderReflection.descriptorBindings,
            requiredStages,
            [&](std::uint32_t set) {
                return declaresDescriptorSetSlot(layout, set);
            },
            [&](std::uint32_t set) {
                return declaresDescriptorTableSlot(layout, set);
            });
    }

    ValidationResult validateComplete(
        const DescriptorConsumptionContract& contract, ShaderStage stages) const
    {
        return validateCompleteBindings(
            contract.bindings(),
            stages,
            [&](std::uint32_t set) {
                return contract.declaresSet(set);
            },
            [&](std::uint32_t set) {
                return contract.declaresTable(set);
            });
    }

private:
    template <class DeclaresSet, class DeclaresTable>
    ValidationResult validateCompleteBindings(
        const core::pmr::Vector<ShaderReflectionBinding>& bindings,
        ShaderStage requiredStages,
        DeclaresSet declaresSet,
        DeclaresTable declaresTable) const
    {
        const auto live = validateLiveBindings();
        if (!live.success)
            return live;
        for (const auto& binding : bindings) {
            if ((binding.stages & requiredStages) == ShaderStage::None) {
                continue;
            }
            if (declaresTable(binding.set)) {
                const auto* table = findTable(binding.set);
                if (!table) {
                    return {false, "descriptor table slot is not bound before draw or dispatch"};
                }
                const auto* object = table->descriptorTable.get();
                if (!object)
                    return {false, "RHI-DESCRIPTOR-OBJECT-NOT-LIVE: table consumed"};
                const auto descriptorValidation = object->validateDescriptor(binding.binding);
                if (!descriptorValidation.success) {
                    return descriptorValidation;
                }
                continue;
            }
            if (declaresSet(binding.set)) {
                const auto* set = findSet(binding.set);
                if (!set) {
                    return {false, "descriptor set slot is not bound before draw or dispatch"};
                }
                const auto* object = set->descriptorSet.get();
                if (!object)
                    return {false, "RHI-DESCRIPTOR-OBJECT-NOT-LIVE: set consumed"};
                const auto descriptorValidation = object->validateBinding(binding.binding);
                if (!descriptorValidation.success) {
                    return descriptorValidation;
                }
                continue;
            }
            return {
                false, "shader reflection descriptor binding is not declared by pipeline layout"};
        }
        return {true, ""};
    }

private:
    struct BoundSet {
        std::uint32_t set = 0;
        core::LifetimeObserver<const DescriptorSet> descriptorSet;
        std::uint64_t revision = 0u;
    };

    struct BoundTable {
        std::uint32_t set = 0;
        core::LifetimeObserver<const DescriptorTable> descriptorTable;
        std::uint64_t revision = 0u;
    };

    using SetArray = std::array<BoundSet, kMaxTrackedSlots>;
    using TableArray = std::array<BoundTable, kMaxTrackedSlots>;

    BoundSet* findSet(std::uint32_t set)
    {
        for (std::uint32_t index = 0; index < setCount_; ++index) {
            if (sets_[index].set == set) {
                return &sets_[index];
            }
        }
        return nullptr;
    }

    const BoundSet* findSet(std::uint32_t set) const
    {
        for (std::uint32_t index = 0; index < setCount_; ++index) {
            if (sets_[index].set == set) {
                return &sets_[index];
            }
        }
        return nullptr;
    }

    BoundTable* findTable(std::uint32_t set)
    {
        for (std::uint32_t index = 0; index < tableCount_; ++index) {
            if (tables_[index].set == set) {
                return &tables_[index];
            }
        }
        return nullptr;
    }

    const BoundTable* findTable(std::uint32_t set) const
    {
        for (std::uint32_t index = 0; index < tableCount_; ++index) {
            if (tables_[index].set == set) {
                return &tables_[index];
            }
        }
        return nullptr;
    }

    static bool declaresDescriptorSetSlot(const PipelineLayoutDesc& layout, std::uint32_t set)
    {
        for (const auto& slot : layout.descriptorSets) {
            if (slot.set == set) {
                return true;
            }
        }
        return false;
    }

    static bool declaresDescriptorTableSlot(const PipelineLayoutDesc& layout, std::uint32_t set)
    {
        for (const auto& slot : layout.descriptorTables) {
            if (slot.set == set) {
                return true;
            }
        }
        return false;
    }

    SetArray sets_{};
    TableArray tables_{};
    std::uint32_t setCount_ = 0;
    std::uint32_t tableCount_ = 0;
};

} // namespace arp::rhi
