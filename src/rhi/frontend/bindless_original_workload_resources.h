#pragma once
#include "rhi/frontend/command_sources.h"
#include "rhi/semantics/bindless_workload_resources.h"

namespace arp::rhi {
struct OriginalBindlessWorkloadResourceInspectionBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxSourceVisits = 0u, maxFrozenDescriptorElements = 0u,
                  maxRetainedResourceVisits = 0u;
};

struct OriginalBindlessWorkloadResourceReport {
    semantics::BindlessWorkloadResourceDemand demand;
    std::uint64_t sourceVisits = 0u, bufferVisits = 0u, textureVisits = 0u, samplerSources = 0u,
                  frozenDescriptorElements = 0u;
    bool validatorOnly = false;
};

// Read-only projection over the one original sealed inventory. No copies of
// its resource table or shared owners, no execution or reservation authority.
class OriginalBindlessWorkloadResourceInspection final {
public:
    // The closed /1 source algebra retains at most source+destination.
    static constexpr std::uint32_t maxRetainedResourcesPerSource = 2u;

    static const char* inspect(
        const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
        const GpuDevice& device,
        const OriginalBindlessWorkloadResourceInspectionBudget& budget,
        OriginalBindlessWorkloadResourceReport& output) noexcept
    {
        output = {};
        if (budget.contractVersion != 1u)
            return "RHI-ORIGINAL-WORKLOAD-RESOURCES-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-ORIGINAL-WORKLOAD-RESOURCES-OPT-IN";
        const auto decision =
            semantics::BindlessWorkloadResourceContract::lowering(device.backend());
        if (!decision.planningAvailable)
            return decision.diagnostic;
        if (!inventory || !inventory->hasQuotaBoundSharedOwner(inventory))
            return "RHI-ORIGINAL-SOURCES-OWNER";
        if (!inventory->inventorySealed())
            return "RHI-ORIGINAL-WORKLOAD-RESOURCES-UNSEALED";
        if (inventory->sources().size() > budget.maxSourceVisits)
            return "RHI-ORIGINAL-WORKLOAD-RESOURCES-SOURCE-CAPACITY";
        if (const auto* id =
                OriginalCommandResourceSources::validateRegisteredLifetime(inventory, device))
            return id;
        auto remaining = budget.maxFrozenDescriptorElements;
        auto resourceRemaining = budget.maxRetainedResourceVisits;
        // Preflight the full bounded projection, including inactive physical
        // slots. Existing canonical validation retains its independent budgets.
        for (const auto& source : inventory->sources()) {
            const auto resources = retainedResourceVisits(source);
            if (resources > resourceRemaining)
                return "RHI-ORIGINAL-WORKLOAD-RESOURCES-RESOURCE-CAPACITY";
            resourceRemaining -= resources;
            const auto count = std::visit(
                [](const auto& value) -> std::size_t {
                    using T =
                        std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                    if constexpr (T::domain == CommandReferenceDomain::BindlessRoot)
                        return value->elements().size();
                    else if constexpr (T::domain == CommandReferenceDomain::Descriptor
                        || T::domain == CommandReferenceDomain::DescriptorTable)
                        return value->frozenElements().size();
                    else
                        return 0u;
                },
                source);
            if (count > remaining)
                return "RHI-ORIGINAL-WORKLOAD-RESOURCES-DESCRIPTOR-CAPACITY";
            remaining -= count;
        }
        OriginalBindlessWorkloadResourceReport result;
        result.validatorOnly = decision.validatorOnly;
        result.sourceVisits = inventory->sources().size();
        result.frozenDescriptorElements = budget.maxFrozenDescriptorElements - remaining;
        for (const auto& source : inventory->sources()) {
            const auto* id = std::visit(
                [&](const auto& value) -> const char* {
                    using T =
                        std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                    if constexpr (std::is_same_v<T, OriginalCommandBufferLease>) {
                        return inspectBuffer(value->layout(), result);
                    } else if constexpr (std::is_same_v<T, OriginalCommandTextureLease>) {
                        return inspectTexture(value->layout(), result);
                    } else if constexpr (std::is_same_v<T, OriginalCommandTextureViewSource>) {
                        return inspectTexture(value->originalParentLease()->layout(), result);
                    } else if constexpr (std::is_same_v<T, OriginalCommandTextureUploadSource>) {
                        return inspectTexture(value->originalDestinationLease()->layout(), result);
                    } else if constexpr (std::is_same_v<T, OriginalCommandBufferUploadSource>) {
                        if (const auto* error =
                                inspectBuffer(value->originalSourceLease()->layout(), result))
                            return error;
                        return inspectBuffer(value->originalDestinationLease()->layout(), result);
                    } else if constexpr (std::is_same_v<T, OriginalCommandReadbackTextureSource>) {
                        return inspectTexture(value->layout(), result);
                    } else if constexpr (std::is_same_v<T, OriginalCommandTextureReadbackSource>) {
                        if (const auto* error =
                                inspectTexture(value->originalSource()->layout(), result))
                            return error;
                        return inspectBuffer(value->originalDestinationLease()->layout(), result);
                    } else if constexpr (std::is_same_v<T, OriginalCommandSamplerLease>) {
                        ++result.samplerSources;
                    } else if constexpr (T::domain == CommandReferenceDomain::BindlessRoot) {
                        for (const auto& element : value->elements())
                            if (const auto* error = inspectBinding(element.declaration.write.type,
                                    element.declaration.write.bufferBindingRange,
                                    element.declaration.dynamicOffset != 0u,
                                    element.owner,
                                    result.demand))
                                return error;
                    } else if constexpr (T::domain == CommandReferenceDomain::Descriptor) {
                        for (const auto& element : value->frozenElements()) {
                            const auto& declaration = element.declaration;
                            const auto* binding = findDescriptorSetBinding(
                                value->frozenDefinition(), declaration.write.binding);
                            if (!binding)
                                return "RHI-ORIGINAL-WORKLOAD-RESOURCES-DESCRIPTOR-BINDING";
                            if (const auto* error = inspectBinding(declaration.write.type,
                                    declaration.write.bufferBindingRange,
                                    binding->dynamicOffset || declaration.dynamicOffset,
                                    element.resourceOwner,
                                    result.demand))
                                return error;
                        }
                    } else if constexpr (T::domain == CommandReferenceDomain::DescriptorTable) {
                        for (const auto& element : value->frozenElements())
                            if (const auto* error = inspectBinding(element.declaration.write.type,
                                    DescriptorBufferBindingRange{},
                                    false,
                                    element.resourceOwner,
                                    result.demand))
                                return error;
                    } else {
                        static_assert(T::domain == CommandReferenceDomain::Pipeline,
                            "Every new canonical source needs an explicit resource projection");
                    }
                    return nullptr;
                },
                source);
            if (id)
                return id;
        }
        if (result.bufferVisits + result.textureVisits
            != budget.maxRetainedResourceVisits - resourceRemaining)
            return "RHI-ORIGINAL-WORKLOAD-RESOURCES-PROJECTION";
        output = result;
        return nullptr;
    }

private:
    static std::uint32_t retainedResourceVisits(
        const OriginalCommandResourceSources::Source& source) noexcept
    {
        return std::visit(
            [](const auto& value) -> std::uint32_t {
                using T = std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                if constexpr (std::is_same_v<T, OriginalCommandBufferLease>
                    || std::is_same_v<T, OriginalCommandTextureLease>
                    || std::is_same_v<T, OriginalCommandTextureViewSource>
                    || std::is_same_v<T, OriginalCommandTextureUploadSource>
                    || std::is_same_v<T, OriginalCommandReadbackTextureSource>)
                    return 1u;
                else if constexpr (std::is_same_v<T, OriginalCommandBufferUploadSource>
                    || std::is_same_v<T, OriginalCommandTextureReadbackSource>)
                    return 2u;
                else {
                    static_assert(T::domain == CommandReferenceDomain::BindlessRoot
                            || T::domain == CommandReferenceDomain::Descriptor
                            || T::domain == CommandReferenceDomain::DescriptorTable
                            || T::domain == CommandReferenceDomain::Pipeline
                            || std::is_same_v<T, OriginalCommandSamplerLease>,
                        "Every new canonical source needs explicit retained resource accounting");
                    return 0u;
                }
            },
            source);
    }

    static const char* inspectBuffer(
        const BufferResourceLayout& layout, OriginalBindlessWorkloadResourceReport& result) noexcept
    {
        BufferDesc desc;
        layout.write(desc);
        const semantics::BindlessWorkloadResources resources{
            1u, &desc, 1u, nullptr, 0u, nullptr, 0u};
        semantics::BindlessWorkloadResourceDemand part;
        if (const auto* id = semantics::BindlessWorkloadResourceContract::inspect(resources, part))
            return id;
        result.demand = semantics::BindlessWorkloadResourceContract::merge(result.demand, part);
        ++result.bufferVisits;
        return nullptr;
    }

    static const char* inspectTexture(const TextureResourceLayout& layout,
        OriginalBindlessWorkloadResourceReport& result) noexcept
    {
        TextureDesc desc;
        layout.write(desc);
        const semantics::BindlessWorkloadResources resources{
            1u, nullptr, 0u, nullptr, 0u, &desc, 1u};
        semantics::BindlessWorkloadResourceDemand part;
        if (const auto* id = semantics::BindlessWorkloadResourceContract::inspect(resources, part))
            return id;
        result.demand = semantics::BindlessWorkloadResourceContract::merge(result.demand, part);
        ++result.textureVisits;
        return nullptr;
    }

    static const char* inspectBinding(DescriptorType type,
        const DescriptorBufferBindingRange& range,
        bool dynamic,
        const OriginalCommandDescriptorDependencies::Owner& owner,
        semantics::BindlessWorkloadResourceDemand& output) noexcept
    {
        if (type != DescriptorType::UniformBuffer && type != DescriptorType::ReadOnlyStorageBuffer
            && type != DescriptorType::StorageBuffer)
            return nullptr;
        if (dynamic)
            return "RHI-ORIGINAL-WORKLOAD-RESOURCES-DYNAMIC-RANGE";
        const auto* buffer = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&owner);
        if (!buffer || !*buffer)
            return "RHI-ORIGINAL-WORKLOAD-RESOURCES-BUFFER-OWNER";
        BufferDesc desc;
        (*buffer)->layout().write(desc);
        const semantics::BindlessWorkloadBufferBinding binding{0u, type, range};
        const semantics::BindlessWorkloadResources resources{
            1u, &desc, 1u, &binding, 1u, nullptr, 0u};
        semantics::BindlessWorkloadResourceDemand part;
        if (const auto* id = semantics::BindlessWorkloadResourceContract::inspect(resources, part))
            return id;
        output = semantics::BindlessWorkloadResourceContract::merge(output, part);
        return nullptr;
    }
};
} // namespace arp::rhi
