#include "core/memory/cpu_allocator.h"
#include "core/ownership/lifetime_epoch.h"
#include "core/ownership/mutation_journal.h"
#include "null_backend_internal.h"
#include "rhi/frontend/descriptor_state_copy.h"

#include <utility>
#include <vector>

namespace arp::rhi::null_backend {

namespace {

DescriptorResourceKind expectedResourceKind(DescriptorType type)
{
    switch (type) {
    case DescriptorType::SampledTexture:
    case DescriptorType::StorageTexture:
    case DescriptorType::InputAttachment:
        return DescriptorResourceKind::TextureView;
    case DescriptorType::Sampler:
        return DescriptorResourceKind::Sampler;
    case DescriptorType::UniformBuffer:
    case DescriptorType::StorageBuffer:
    case DescriptorType::ReadOnlyStorageBuffer:
        return DescriptorResourceKind::Buffer;
    }

    return DescriptorResourceKind::TextureView;
}

template <class Write> struct StoredDescriptorBinding {
    bool written = false;
    Write write;
};

bool descriptorCapacityExceeded(const DescriptorArenaStats& stats,
    std::uint32_t requestedDescriptors,
    std::uint32_t maxDescriptors)
{
    return stats.allocatedDescriptors > maxDescriptors
        || requestedDescriptors > maxDescriptors - stats.allocatedDescriptors;
}

class NullDescriptorSet final : public DescriptorSet {
public:
    NullDescriptorSet(DescriptorSetDesc desc,
        DescriptorSetAllocation allocation,
        ResourceRegistryPtr registry,
        const core::LifetimeEpoch& generation)
        : desc_(std::move(desc)), allocation_(std::move(allocation)),
          registry_(std::move(registry)), generation_(generation.ticket()),
          bindings_(allocation_.descriptorCapacity, &core::defaultCpuMemoryResource())
    {}

    const DescriptorSetAllocation& allocation() const override { return allocation_; }

    const DescriptorLayoutDesc& layout() const override { return desc_.layout; }

    std::shared_ptr<const OriginalCompletedDepthTextureSource> originalCompletedDepthPublication(
        std::uint32_t, std::uint32_t) const noexcept override
    {
        // Null validates declarations but cannot issue a native publication.
        return {};
    }

    ValidationResult writeOriginalBufferRangeImpl(const DescriptorBindingWrite& write,
        const std::shared_ptr<const OriginalCommandBufferLease>&) override
    {
        return writeImpl(write);
    }

    ValidationResult writeImpl(const DescriptorBindingWrite& write) override
    {
        if (!isCurrentAllocation()) {
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: descriptor set write rejected after descriptor "
                "arena reset"};
        }
        if (!write.resource.isValid()) {
            return {false, "descriptor binding write requires a valid resource id"};
        }
        const auto loweringValidation = validateDescriptorWriteLowering(write);
        if (!loweringValidation.success) {
            return loweringValidation;
        }
        const auto elementIndex =
            descriptorElementIndex(desc_.layout, write.binding, write.arrayElement);
        if (elementIndex == UINT32_MAX || elementIndex >= bindings_.size()) {
            return {false,
                "RHI-DESCRIPTOR-ELEMENT-RANGE: descriptor binding or array element is outside "
                "layout"};
        }

        for (std::size_t index = 0; index < desc_.layout.bindings.size(); ++index) {
            const auto& binding = desc_.layout.bindings[index];
            if (binding.binding != write.binding) {
                continue;
            }
            if (binding.type != write.type) {
                return {false, "descriptor binding write type does not match layout"};
            }
            if (write.resourceKind != expectedResourceKind(binding.type)) {
                return {false, "descriptor binding write resource kind does not match layout"};
            }
            const auto resourceValidation =
                registry_->validateDescriptorResource(physicalDescriptorType(write),
                    write.resourceKind,
                    write.resource,
                    binding.textureViewDimensionConstrained,
                    binding.textureViewDimension,
                    binding.depthTexture);
            if (!resourceValidation.success) {
                return resourceValidation;
            }
            bindings_[elementIndex] = {true, write};
            return {true, ""};
        }

        return {false, "descriptor binding write target does not exist"};
    }

    bool isCurrentAllocation() const override
    {
        return allocation_.success && generation_.isCurrent();
    }

    bool hasWrittenBinding(std::uint32_t binding) const override
    {
        if (!isCurrentAllocation()) {
            return false;
        }
        for (const auto& candidate : desc_.layout.bindings) {
            if (candidate.binding == binding) {
                const auto offset = descriptorElementIndex(desc_.layout, binding, 0u);
                for (std::uint32_t element = 0; element < candidate.arrayCount; ++element)
                    if (bindings_[offset + element].written)
                        return true;
                return false;
            }
        }
        return false;
    }

    bool hasWrittenBindingElement(std::uint32_t binding, std::uint32_t element) const override
    {
        if (!isCurrentAllocation())
            return false;
        const auto offset = descriptorElementIndex(desc_.layout, binding, element);
        return offset != UINT32_MAX && offset < bindings_.size() && bindings_[offset].written;
    }

    ValidationResult validateBinding(std::uint32_t binding) const override
    {
        if (!isCurrentAllocation()) {
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: descriptor set validation rejected after "
                "descriptor arena reset"};
        }
        for (std::size_t index = 0; index < desc_.layout.bindings.size(); ++index) {
            const auto& layoutBinding = desc_.layout.bindings[index];
            if (layoutBinding.binding != binding) {
                continue;
            }
            if (!hasWrittenBinding(binding)) {
                return {false, "descriptor binding is not written before draw or dispatch"};
            }
            for (std::uint32_t element = 0u; element < layoutBinding.arrayCount; ++element) {
                if (!hasWrittenBindingElement(binding, element))
                    continue;
                const auto result = validateBindingElement(binding, element);
                if (!result.success)
                    return result;
            }
            return {true, {}};
        }
        return {false, "descriptor binding is not declared by descriptor set layout"};
    }

    ValidationResult validateBindingElement(
        std::uint32_t binding, std::uint32_t element) const override
    {
        if (!isCurrentAllocation())
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: descriptor array validation after arena reset"};
        const auto offset = descriptorElementIndex(desc_.layout, binding, element);
        if (offset == UINT32_MAX || offset >= bindings_.size() || !bindings_[offset].written)
            return {false, "descriptor array element is not written"};
        const auto& stored = bindings_[offset];
        for (const auto& candidate : desc_.layout.bindings) {
            if (candidate.binding != binding)
                continue;
            return registry_->validateDescriptorResource(physicalDescriptorType(stored.write),
                stored.write.resourceKind,
                stored.write.resource,
                candidate.textureViewDimensionConstrained,
                candidate.textureViewDimension,
                candidate.depthTexture);
        }
        return {false, "descriptor binding is not declared by descriptor set layout"};
    }

    DescriptorSetStats stats() const override
    {
        DescriptorSetStats result;
        result.bindingCount = static_cast<std::uint32_t>(desc_.layout.bindings.size());
        if (isCurrentAllocation()) {
            for (const auto& binding : bindings_) {
                if (binding.written) {
                    ++result.writtenBindings;
                }
            }
        }
        result.backendFallbacksHiddenFromUpperLayer = true;
        result.nativeHandleExposedToUpperLayer = false;
        return result;
    }

    core::LifetimeEpoch::Ticket allocationEpochTicket() const noexcept override
    {
        return generation_;
    }

    std::optional<DescriptorBindingWrite> successfulBindingWrite(
        std::uint32_t binding, std::uint32_t element) const noexcept override
    {
        const auto index = descriptorElementIndex(desc_.layout, binding, element);
        return isCurrentAllocation() && index != UINT32_MAX && index < bindings_.size()
                && bindings_[index].written
            ? std::optional<DescriptorBindingWrite>(bindings_[index].write)
            : std::nullopt;
    }

private:
    DescriptorSetDesc desc_;
    DescriptorSetAllocation allocation_;
    ResourceRegistryPtr registry_;
    core::LifetimeEpoch::Ticket generation_;
    core::pmr::Vector<StoredDescriptorBinding<DescriptorBindingWrite>> bindings_;
};

class NullDescriptorTable final : public DescriptorTable {
public:
    NullDescriptorTable(DescriptorTableDesc desc,
        DescriptorTableAllocation allocation,
        ResourceRegistryPtr registry,
        const core::LifetimeEpoch& generation)
        : desc_(std::move(desc)), allocation_(std::move(allocation)),
          registry_(std::move(registry)), generation_(generation.ticket()),
          descriptors_(desc_.capacity, &core::defaultCpuMemoryResource())
    {}

    const DescriptorTableAllocation& allocation() const override { return allocation_; }

    const DescriptorTableDesc& desc() const override { return desc_; }

    ValidationResult writeImpl(const DescriptorTableWrite& write) override
    {
        if (!isCurrentAllocation()) {
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: descriptor table write rejected after descriptor "
                "arena reset"};
        }
        if (!write.resource.isValid()) {
            return {false, "descriptor table write requires a valid resource id"};
        }
        if (write.index >= desc_.capacity) {
            return {false, "descriptor table write index exceeds capacity"};
        }
        if (write.type != desc_.type) {
            return {false, "descriptor table write type does not match table"};
        }
        if (write.resourceKind != expectedResourceKind(desc_.type)) {
            return {false, "descriptor table write resource kind does not match table"};
        }
        const auto resourceValidation = registry_->validateDescriptorResource(desc_.type,
            write.resourceKind,
            write.resource,
            desc_.textureViewDimensionConstrained,
            desc_.textureViewDimension,
            desc_.depthTexture);
        if (!resourceValidation.success) {
            return resourceValidation;
        }

        descriptors_[write.index] = {true, write};
        return {true, ""};
    }

    bool isCurrentAllocation() const override
    {
        return allocation_.success && generation_.isCurrent();
    }

    bool hasWrittenDescriptor(std::uint32_t index) const override
    {
        if (!isCurrentAllocation()) {
            return false;
        }
        if (index >= descriptors_.size()) {
            return false;
        }
        return descriptors_[index].written;
    }

    ValidationResult validateDescriptor(std::uint32_t index) const override
    {
        if (!isCurrentAllocation()) {
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: descriptor table validation rejected after "
                "descriptor arena reset"};
        }
        if (index >= descriptors_.size()) {
            return {false, "descriptor table entry exceeds capacity"};
        }
        const auto& stored = descriptors_[index];
        if (!stored.written) {
            return {false, "descriptor table entry is not written before draw or dispatch"};
        }
        return registry_->validateDescriptorResource(desc_.type,
            stored.write.resourceKind,
            stored.write.resource,
            desc_.textureViewDimensionConstrained,
            desc_.textureViewDimension,
            desc_.depthTexture);
    }

    DescriptorTableStats stats() const override
    {
        DescriptorTableStats result;
        result.capacity = desc_.capacity;
        if (isCurrentAllocation()) {
            for (const auto& descriptor : descriptors_) {
                if (descriptor.written) {
                    ++result.writtenDescriptors;
                }
            }
        }
        result.backendFallbacksHiddenFromUpperLayer = true;
        result.nativeHandleExposedToUpperLayer = false;
        return result;
    }

    core::LifetimeEpoch::Ticket allocationEpochTicket() const noexcept override
    {
        return generation_;
    }

    std::optional<DescriptorTableWrite> successfulDescriptorWrite(
        std::uint32_t index) const noexcept override
    {
        return hasWrittenDescriptor(index)
            ? std::optional<DescriptorTableWrite>(descriptors_[index].write)
            : std::nullopt;
    }

private:
    DescriptorTableDesc desc_;
    DescriptorTableAllocation allocation_;
    ResourceRegistryPtr registry_;
    core::LifetimeEpoch::Ticket generation_;
    core::pmr::Vector<StoredDescriptorBinding<DescriptorTableWrite>> descriptors_;
};

class NullDescriptorArena final : public DescriptorArena {
public:
    NullDescriptorArena(DescriptorArenaDesc desc, ResourceRegistryPtr registry)
        : desc_(std::move(desc)), registry_(std::move(registry))
    {}

    DescriptorSetAllocation allocateSet(const DescriptorLayoutDesc& layout) override
    {
        if (!generation_.isActive()) {
            return {false, "RHI-DESCRIPTOR-GENERATION-EXHAUSTED", 0u, 0u};
        }
        const auto validation = validateDescriptorLayout(layout);
        if (!validation.success) {
            return {false, validation.diagnostics, 0, 0};
        }

        std::uint32_t descriptorCount = 0u;
        const auto count = countDescriptorElements(layout, descriptorCount);
        if (!count.success)
            return {false, count.diagnostics, 0u, 0u};
        if (stats_.allocatedSets >= desc_.maxSets
            || descriptorCapacityExceeded(stats_, descriptorCount, desc_.maxDescriptors)) {
            return {false, "descriptor arena capacity exhausted", 0, 0};
        }

        const auto setIndex = stats_.allocatedSets;
        ++stats_.allocatedSets;
        stats_.allocatedDescriptors += descriptorCount;
        return {true, "", setIndex, descriptorCount};
    }

    arp::core::UniquePtr<DescriptorSet> allocateDescriptorSet(
        const DescriptorSetDesc& desc) override
    {
        auto copied = arp::rhi::DescriptorStateCopy::copy(desc);
        return core::withMutationRollback(
            [&]() -> core::UniquePtr<DescriptorSet> {
                auto allocation = allocateSet(copied.layout);
                if (!allocation.success) {
                    return nullptr;
                }
                return arp::core::makeUnique<NullDescriptorSet>(
                    std::move(copied), std::move(allocation), registry_, generation_);
            },
            stats_);
    }

    DescriptorTableAllocation allocateTable(const DescriptorTableDesc& desc) override
    {
        if (!generation_.isActive()) {
            return {false, "RHI-DESCRIPTOR-GENERATION-EXHAUSTED", 0u, 0u};
        }
        if (desc.label.empty()) {
            return {false, "descriptor table label must not be empty", 0, 0};
        }
        if (desc.capacity == 0) {
            return {false, "descriptor table capacity must be greater than zero", 0, 0};
        }
        if (desc.stages == ShaderStage::None) {
            return {false, "descriptor table requires shader visibility", 0, 0};
        }
        const auto shape = validateDescriptorTextureRequirement(
            desc.type, desc.textureViewDimensionConstrained, desc.depthTexture);
        if (!shape.success)
            return {false, shape.diagnostics, 0u, 0u};
        if (stats_.allocatedSets >= desc_.maxSets
            || descriptorCapacityExceeded(stats_, desc.capacity, desc_.maxDescriptors)) {
            return {false, "descriptor arena capacity exhausted", 0, 0};
        }

        const auto tableIndex = stats_.allocatedSets;
        ++stats_.allocatedSets;
        ++stats_.allocatedTables;
        stats_.allocatedDescriptors += desc.capacity;
        return {true, "", tableIndex, desc.capacity};
    }

    arp::core::UniquePtr<DescriptorTable> allocateDescriptorTable(
        const DescriptorTableDesc& desc) override
    {
        auto copied = arp::rhi::DescriptorStateCopy::copy(desc);
        return core::withMutationRollback(
            [&]() -> core::UniquePtr<DescriptorTable> {
                auto allocation = allocateTable(copied);
                if (!allocation.success) {
                    return nullptr;
                }
                return arp::core::makeUnique<NullDescriptorTable>(
                    std::move(copied), std::move(allocation), registry_, generation_);
            },
            stats_);
    }

    DescriptorArenaStats stats() const override
    {
        DescriptorArenaStats result = stats_;
        result.maxSets = desc_.maxSets;
        result.maxDescriptors = desc_.maxDescriptors;
        return result;
    }

    void reset() override
    {
        stats_.allocatedSets = 0;
        stats_.allocatedTables = 0;
        stats_.allocatedDescriptors = 0;
        generation_.advance();
    }

    core::LifetimeEpoch::Ticket allocationEpochTicket() const noexcept override
    {
        return generation_.ticket();
    }

private:
    DescriptorArenaDesc desc_;
    ResourceRegistryPtr registry_;
    core::LifetimeEpoch generation_;
    DescriptorArenaStats stats_;
};

} // namespace

arp::core::UniquePtr<DescriptorArena> createDescriptorArena(
    const DescriptorArenaDesc& desc, const ResourceRegistryPtr& registry)
{
    if (desc.maxSets == 0 || desc.maxDescriptors == 0) {
        return nullptr;
    }
    return arp::core::makeUnique<NullDescriptorArena>(
        arp::rhi::DescriptorStateCopy::copy(desc), registry);
}

} // namespace arp::rhi::null_backend
