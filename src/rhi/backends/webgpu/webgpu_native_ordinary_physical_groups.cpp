#include "rhi/backends/webgpu/webgpu_native_ordinary_physical_groups.h"

#include "rhi/backends/webgpu/webgpu_native_string_view.h"

#include <algorithm>
#include <new>
#include <stdexcept>

namespace arp::rhi_webgpu {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
bool initializeWebGpuNativePhysicalDescriptorGroup(WebGpuNativeDescriptorGroupPayload& owner,
    const WebGpuNativeIssuedPipelineContract& issued,
    std::uint32_t group,
    const WGPUBindGroupEntry* entries,
    std::size_t count,
    core::pmr::String& diagnostic)
{
    if (owner.layout || owner.group || !issued.nativeDevice() || !issued.nativeObject()
        || group >= issued.semantics().groupCount || (count && !entries)) {
        diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-NATIVE-INTENT";
        return false;
    }
    owner.layout = issued.graphics().graphics
        ? wgpuRenderPipelineGetBindGroupLayout(
              static_cast<WGPURenderPipeline>(issued.nativeObject()), group)
        : wgpuComputePipelineGetBindGroupLayout(
              static_cast<WGPUComputePipeline>(issued.nativeObject()), group);
    if (!owner.layout) {
        diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-NATIVE-LAYOUT";
        return false;
    }
    auto desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.label = webGpuNativeStringView("RHI WebGPU cold physical group");
    desc.layout = owner.layout;
    desc.entryCount = count;
    desc.entries = entries;
    owner.group = wgpuDeviceCreateBindGroup(static_cast<WGPUDevice>(issued.nativeDevice()), &desc);
    if (!owner.group) {
        diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-NATIVE-CREATE";
        return false;
    }
    return true;
}
#endif
core::UniquePtr<WebGpuOrdinaryPhysicalGroups> WebGpuOrdinaryPhysicalGroups::prepare(
    std::shared_ptr<const WebGpuOrdinaryPipelineObservation> pipeline,
    const WebGpuOrdinaryObservedSet* sets,
    std::size_t count,
    const WebGpuOrdinaryPhysicalGroupBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    diagnostic = validateBudget(budget);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-QUOTA";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)pipeline;
    (void)sets;
    (void)count;
    diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-NATIVE-UNAVAILABLE";
    return {};
#else
    if (!pipeline || !pipeline->hasQuotaBoundSharedOwner(pipeline)) {
        diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-PIPELINE-OWNER";
        return {};
    }
    diagnostic = pipeline->validate();
    if (diagnostic)
        return {};
    const auto& issued = pipeline->contract();
    const auto importedDevice = emscripten_webgpu_get_device();
    const bool sameDevice = importedDevice == issued.nativeDevice();
    if (importedDevice && !webGpuNativeDeviceLost())
        wgpuDeviceRelease(importedDevice);
    if (!sameDevice) {
        diagnostic = "RHI-WEBGPU-ORDINARY-PIPELINE-DEVICE-MISMATCH";
        return {};
    }
    if (issued.semantics().usesPush || !issued.tables().empty()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-PUSH-TABLE-UNSUPPORTED";
        return {};
    }
    if ((count && !sets) || count > budget.maxSets || issued.bindings().size() > budget.maxBindings
        || issued.semantics().groupCount > budget.maxGroups
        || issued.semantics().groupCount > budget.maxNativeGroupLayouts
        || issued.semantics().groupCount > budget.maxNativeGroups) {
        diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-CAPACITY";
        return {};
    }
    for (std::size_t i = 0u; i < count; ++i)
        for (std::size_t j = 0u; j < i; ++j)
            if (sets[j].logicalSet == sets[i].logicalSet) {
                diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-DUPLICATE-SET";
                return {};
            }
    if (count != issued.sets().size()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-SET-COVERAGE";
        return {};
    }
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& set = sets[i];
        if (!set.observation || !set.observation->hasQuotaBoundSharedOwner(set.observation)) {
            diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-DESCRIPTOR-OWNER";
            return {};
        }
        diagnostic = set.observation->validate();
        if (diagnostic)
            return {};
        std::size_t required = 0u;
        for (const auto& fact : issued.facts())
            if (fact.mapping.logicalSet == set.logicalSet) {
                ++required;
                const auto& bindings = set.observation->bindings();
                const auto found =
                    std::find_if(bindings.begin(), bindings.end(), [&](const auto& binding) {
                        return binding.binding.binding == fact.mapping.logicalBinding;
                    });
                if (found == bindings.end()
                    || !rhi::descriptorBindingLayoutMatches(fact.shape, found->binding)) {
                    diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-BINDING-SHAPE";
                    return {};
                }
                if (fact.shape.arrayCount != 1u || fact.shape.tableBacked
                    || fact.shape.type == rhi::DescriptorType::InputAttachment) {
                    diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-ARRAY-INPUT-UNSUPPORTED";
                    return {};
                }
            }
        if (!required || required != set.observation->bindings().size()) {
            diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-BINDING-COVERAGE";
            return {};
        }
        for (const auto& resource : set.observation->resources().resources())
            if (const auto* id = pipeline->validateForDevice(resource.nativeDevice())) {
                diagnostic = id;
                return {};
            }
    }
    const auto limits = webGpuNativeDeviceLimits();
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = core::makeUnique<WebGpuOrdinaryPhysicalGroups>(
            ConstructionKey{}, pipeline, budget.maxSets);
        core::BoundedSequence<const WebGpuOrdinaryResourceObservation*> resources(
            budget.maxResources);
        for (std::size_t i = 0u; i < count; ++i) {
            if (!result->sets_.append(sets[i])) {
                diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-CAPACITY";
                return {};
            }
            for (const auto& resource : sets[i].observation->resources().resources()) {
                bool duplicate = false, sharedAllocation = false;
                for (const auto* earlier : resources.values()) {
                    if (resource.id() == earlier->id()) {
                        if (!resource.sameOwners(*earlier)) {
                            diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-RESOURCE-ALIAS";
                            return {};
                        }
                        duplicate = true;
                    }
                    if (resource.sharesAllocationWith(*earlier)) {
                        if (!resource.sameAllocationBacking(*earlier)) {
                            diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-ALLOCATION-ALIAS";
                            return {};
                        }
                        sharedAllocation = true;
                    }
                }
                if (duplicate)
                    continue;
                if (!resources.append(&resource)) {
                    diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-RESOURCE-CAPACITY";
                    return {};
                }
                if (!sharedAllocation) {
                    const auto bytes = resource.backing().allocationBytes;
                    if (bytes > budget.maxRetainedAllocationBytes - result->allocationBytes_) {
                        diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-ALLOCATION-BYTE-CAPACITY";
                        return {};
                    }
                    result->allocationBytes_ += bytes;
                }
            }
        }
        result->groups_.reserve(budget.maxGroups);
        result->groups_.resize(issued.semantics().groupCount);
        for (auto& group : result->groups_)
            group.owner = core::makeShared<WebGpuNativeDescriptorGroupPayload>();
        for (const auto& fact : issued.facts()) {
            const auto set = std::find_if(result->sets_.values().begin(),
                result->sets_.values().end(),
                [&](const auto& selected) {
                    return selected.logicalSet == fact.mapping.logicalSet;
                });
            if (set == result->sets_.values().end()) {
                diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-SET-COVERAGE";
                return {};
            }
            const auto& bindings = set->observation->bindings();
            const auto binding =
                std::find_if(bindings.begin(), bindings.end(), [&](const auto& candidate) {
                    return candidate.binding.binding == fact.mapping.logicalBinding;
                });
            if (binding == bindings.end()) {
                diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-BINDING-COVERAGE";
                return {};
            }
            auto entry = WGPU_BIND_GROUP_ENTRY_INIT;
            entry.binding = fact.mapping.webGpuBinding;
            switch (fact.physicalType) {
            case rhi::DescriptorType::UniformBuffer:
            case rhi::DescriptorType::StorageBuffer:
            case rhi::DescriptorType::ReadOnlyStorageBuffer: {
                const auto* resource = set->observation->resources().find(binding->write.resource);
                if (!resource || !resource->backing().buffer) {
                    diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-BUFFER";
                    return {};
                }
                const auto bytes = resource->backing().bufferDesc.sizeBytes;
                const auto maximum = fact.physicalType == rhi::DescriptorType::UniformBuffer
                    ? limits.maxUniformBufferBindingSize
                    : limits.maxStorageBufferBindingSize;
                // WebGPU storage binding size is a multiple of four bytes;
                // this native specification constraint is independent of
                // the device's dynamic-offset alignment and shader footprint.
                constexpr std::uint64_t storageBindingSizeAlignment = 4u;
                if (!bytes || !maximum || bytes > maximum || bytes < fact.nativeDeclaredMinimumBytes
                    || (fact.physicalType != rhi::DescriptorType::UniformBuffer
                        && bytes % storageBindingSizeAlignment)) {
                    diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-BUFFER-RANGE";
                    return {};
                }
                if (bytes > budget.maxBufferBindingBytes - result->bufferBytes_) {
                    diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-BINDING-BYTE-CAPACITY";
                    return {};
                }
                result->bufferBytes_ += bytes;
                result->deferredShaderSize_ |= fact.nativeDeclaredMinimumBytes == 0u;
                entry.buffer = resource->backing().buffer->buffer;
                entry.offset = 0u;
                entry.size = bytes;
                break;
            }
            case rhi::DescriptorType::SampledTexture:
            case rhi::DescriptorType::StorageTexture:
                if (!binding->textureView || !binding->textureView->parentTexture) {
                    diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-TEXTURE-PARENT";
                    return {};
                }
                if (const auto* id = binding->textureView->parentTexture->contentAccessPolicy()
                        .ordinaryAccessDiagnostic()) {
                    diagnostic = id;
                    return {};
                }
                entry.textureView = binding->textureView->view;
                break;
            case rhi::DescriptorType::Sampler:
                entry.sampler = binding->sampler->sampler;
                break;
            case rhi::DescriptorType::InputAttachment:
                diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-ARRAY-INPUT-UNSUPPORTED";
                return {};
            }
            result->groups_[fact.mapping.webGpuGroup].entries.push_back(entry);
        }
        for (auto& group : result->groups_) {
            std::sort(group.entries.begin(), group.entries.end(), [](const auto& a, const auto& b) {
                return a.binding < b.binding;
            });
            for (const auto& entry : group.entries) {
                const auto fact = std::find_if(
                    issued.facts().begin(), issued.facts().end(), [&](const auto& candidate) {
                        return candidate.mapping.webGpuBinding == entry.binding
                            && &group == &result->groups_[candidate.mapping.webGpuGroup];
                    });
                if (fact == issued.facts().end()) {
                    diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-BINDING-COVERAGE";
                    return {};
                }
                if (fact->shape.dynamicOffset)
                    group.offsets.push_back(0u);
            }
        }
        // Every core allocation and owner reservation has finished. Native
        // query/create can now fail with all previous references RAII-owned.
        for (std::uint32_t i = 0u; i < result->groups_.size(); ++i) {
            auto& group = result->groups_[i];
            core::pmr::String nativeDiagnostic;
            if (!initializeWebGpuNativePhysicalDescriptorGroup(*group.owner,
                    issued,
                    i,
                    group.entries.empty() ? nullptr : group.entries.data(),
                    group.entries.size(),
                    nativeDiagnostic)) {
                diagnostic = group.owner->layout ? "RHI-WEBGPU-ORDINARY-GROUP-NATIVE-CREATE"
                                                 : "RHI-WEBGPU-ORDINARY-GROUP-NATIVE-LAYOUT";
                return {};
            }
            group.originalLayout = group.owner->layout;
            group.originalGroup = group.owner->group;
        }
        diagnostic = nullptr;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-PREPARATION-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-GROUP-PREPARATION-ALLOCATION";
        return {};
    }
#endif
}

const char* WebGpuOrdinaryPhysicalGroups::validate() const noexcept
{
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-WEBGPU-ORDINARY-GROUP-NATIVE-UNAVAILABLE";
#else
    if (!pipeline_->hasQuotaBoundSharedOwner(pipeline_))
        return "RHI-WEBGPU-ORDINARY-GROUP-PIPELINE-OWNER";
    if (const auto* id = pipeline_->validate())
        return id;
    for (const auto& set : sets_.values()) {
        if (!set.observation->hasQuotaBoundSharedOwner(set.observation))
            return "RHI-WEBGPU-ORDINARY-GROUP-DESCRIPTOR-OWNER";
        if (const auto* id = set.observation->validate())
            return id;
    }
    for (const auto& group : groups_)
        if (!group.owner || !group.owner->sharedOwnerMatches(group.owner)
            || group.owner->layout != group.originalLayout
            || group.owner->group != group.originalGroup)
            return "RHI-WEBGPU-ORDINARY-GROUP-NATIVE-IDENTITY";
    return nullptr;
#endif
}

const char* WebGpuOrdinaryPhysicalGroups::executionDiagnostic() const noexcept
{
    if (const auto* id = validate())
        return id;
    return deferredShaderSize_ ? "RHI-WEBGPU-ORDINARY-GROUP-SHADER-SIZE-UNPROVEN"
                               : "RHI-WEBGPU-ORDINARY-GROUP-RECORDING-UNPREPARED";
}

std::uint64_t WebGpuOrdinaryPhysicalGroups::bindingRangeBytes(
    std::uint32_t group, std::uint32_t binding) const noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (group < groups_.size())
        for (const auto& entry : groups_[group].entries)
            if (entry.binding == binding && entry.buffer)
                return entry.size;
#else
    (void)group;
    (void)binding;
#endif
    return 0u;
}

const char* WebGpuOrdinaryPhysicalGroups::originalBufferBinding(
    const std::shared_ptr<const WebGpuOrdinaryPhysicalGroups>& original,
    std::uint32_t group,
    std::uint32_t binding,
    OriginalBufferBinding& output) noexcept
{
    if (!original || !original->hasQuotaBoundSharedOwner(original))
        return "RHI-WEBGPU-ORIGINAL-BUFFER-GROUP-OWNER";
    if (const auto* id = original->validate())
        return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)group;
    (void)binding;
    (void)output;
    return "RHI-WEBGPU-ORIGINAL-BUFFER-NATIVE-UNAVAILABLE";
#else
    const WebGpuNativePipelineBindingFact* selected = nullptr;
    for (const auto& fact : original->pipeline_->contract().facts())
        if (fact.mapping.webGpuGroup == group && fact.mapping.webGpuBinding == binding) {
            if (selected)
                return "RHI-WEBGPU-ORIGINAL-BUFFER-MAPPING-ALIAS";
            selected = &fact;
        }
    if (!selected || group >= original->groups_.size())
        return "RHI-WEBGPU-ORIGINAL-BUFFER-SLOT";
    if (selected->physicalType != rhi::DescriptorType::UniformBuffer
        && selected->physicalType != rhi::DescriptorType::StorageBuffer
        && selected->physicalType != rhi::DescriptorType::ReadOnlyStorageBuffer)
        return "RHI-WEBGPU-ORIGINAL-BUFFER-TYPE";
    if (selected->shape.arrayCount != 1u || selected->shape.tableBacked)
        return "RHI-WEBGPU-ORIGINAL-BUFFER-LOGICAL-MAPPING";
    const WebGpuOrdinaryResourceObservation* resource = nullptr;
    for (const auto& set : original->sets_.values())
        if (set.logicalSet == selected->mapping.logicalSet)
            for (const auto& logical : set.observation->bindings())
                if (logical.binding.binding == selected->mapping.logicalBinding) {
                    if (resource
                        || !rhi::descriptorBindingLayoutMatches(selected->shape, logical.binding))
                        return "RHI-WEBGPU-ORIGINAL-BUFFER-LOGICAL-MAPPING";
                    resource = set.observation->resources().find(logical.write.resource);
                    if (!resource || logical.arrayElement || !resource->backing().buffer
                        || !core::SharedOwnerProjection::sameOwner(
                            logical.buffer, resource->backing().buffer)
                        || logical.textureView || logical.sampler)
                        return "RHI-WEBGPU-ORIGINAL-BUFFER-LOGICAL-MAPPING";
                }
    if (!resource || !resource->backing().buffer || !resource->matches())
        return "RHI-WEBGPU-ORIGINAL-BUFFER-RESOURCE";
    const WGPUBindGroupEntry* entry = nullptr;
    for (const auto& candidate : original->groups_[group].entries)
        if (candidate.binding == binding) {
            if (entry)
                return "RHI-WEBGPU-ORIGINAL-BUFFER-MAPPING-ALIAS";
            entry = &candidate;
        }
    if (!entry || entry->buffer != resource->backing().buffer->buffer || entry->offset != 0u
        || entry->size != resource->backing().bufferDesc.sizeBytes || !entry->size
        || entry->textureView || entry->sampler)
        return "RHI-WEBGPU-ORIGINAL-BUFFER-NATIVE-MAPPING";
    for (const auto offset : original->groups_[group].offsets)
        if (offset)
            return "RHI-WEBGPU-ORIGINAL-BUFFER-DYNAMIC-OFFSET";
    output = {resource, selected->mapping, selected->physicalType, entry->offset, entry->size};
    return nullptr;
#endif
}

std::size_t WebGpuOrdinaryPhysicalGroups::dynamicOffsetCount(std::uint32_t group) const noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return group < groups_.size() ? groups_[group].offsets.size() : 0u;
#else
    (void)group;
    return 0u;
#endif
}
} // namespace arp::rhi_webgpu
