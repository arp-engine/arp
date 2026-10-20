#pragma once

#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_ordinary_resource_retention.h"
#include "rhi/backends/webgpu/webgpu_original_runtime_length_interval_lowering.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"
#include "rhi/frontend/descriptor_general_binding_validation.h"

namespace arp::rhi_webgpu {
// Consumes an accepted original pipeline and registered canonical source.
// Entries borrow native handles; a scoped candidate must retain these owners
// before calling native creation. No group, receipt or execution is issued here.
class WebGpuOriginalGeneralBindingLowering final {
public:
    using Contract = rhi::OriginalGeneralDescriptorBindingContract;
    using Dependencies = rhi::OriginalCommandDescriptorDependencies;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    template <class Pipeline>
    static const char* lower(const Contract::Request& request,
        const rhi::GpuDevice& device,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
        std::uint32_t set,
        const rhi::OriginalCommandDescriptorSetElement& element,
        WGPUBindGroupEntry& output,
        std::uint32_t& group) noexcept
    {
        output = {};
        group = 0u;
        if (const auto* id = Contract::validateRequest(request))
            return id;
        if (!pipeline || !pipeline->originalLayoutOwner())
            return "RHI-ORIGINAL-GENERAL-BINDING-PIPELINE-OWNER";
        const auto& definition = pipeline->originalLayoutOwner()->desc();
        const auto* slot = rhi::findDescriptorSetSlot(definition, set);
        const auto* layout =
            slot ? rhi::findDescriptorSetLayout(definition, slot->layoutName) : nullptr;
        if (!layout)
            return "RHI-ORIGINAL-GENERAL-BINDING-LOGICAL-BINDING";
        if (const auto* id = rhi::OriginalCommandTypedSourceContract::validateSetElements(
                *layout, &element, 1u, sources, device))
            return id;
        const auto owner = Dependencies::capture(*sources, element.sourceIndex);
        if (!owner)
            return "RHI-ORIGINAL-GENERAL-BINDING-SOURCE";
        return lowerFrozen(request, device, pipeline, *owner, set, element, output, group);
    }

    // The transaction retains individual canonical owners, never mutable Sources.
    template <class Pipeline>
    static const char* lowerFrozen(const Contract::Request& request,
        const rhi::GpuDevice& device,
        const std::shared_ptr<Pipeline>& pipeline,
        const Dependencies::Owner& owner,
        std::uint32_t set,
        const rhi::OriginalCommandDescriptorSetElement& element,
        WGPUBindGroupEntry& output,
        std::uint32_t& group) noexcept
    {
        output = {};
        group = 0u;
        if (const auto* id = Contract::validateRequest(request))
            return id;
        if (const auto* id = Contract::lowering(device.backend()).diagnostic)
            return id;
        if (!pipeline || !pipeline->sharedOwnerMatches(pipeline)
            || !pipeline->originalAcceptedSharedQuotaBound() || !pipeline->originalLayoutOwner())
            return "RHI-ORIGINAL-GENERAL-BINDING-PIPELINE-OWNER";
        if (const auto* id = device.validateOriginalResourceRegistration(*pipeline))
            return id;
        const auto& definition = pipeline->originalLayoutOwner()->desc();
        if (!definition.descriptorTables.empty() || !definition.pushConstantRanges.empty())
            return "RHI-ORIGINAL-GENERAL-BINDING-PIPELINE-PROFILE";
        const auto* slot = rhi::findDescriptorSetSlot(definition, set);
        const auto* layout =
            slot ? rhi::findDescriptorSetLayout(definition, slot->layoutName) : nullptr;
        const auto* binding =
            layout ? rhi::findDescriptorSetBinding(*layout, element.write.binding) : nullptr;
        if (!binding)
            return "RHI-ORIGINAL-GENERAL-BINDING-LOGICAL-BINDING";
        const auto* ownerId = std::visit(
            [&](const auto& source) -> const char* {
                using T =
                    std::remove_const_t<typename std::decay_t<decltype(source)>::element_type>;
                if (const auto* id = T::validateOriginal(source))
                    return id;
                if (source->originalObject()->id() != element.write.resource)
                    return "RHI-ORIGINAL-GENERAL-BINDING-SOURCE";
                return Dependencies::validateOwner(
                    owner, element.registrationGeneration, source->originalMetadataQuota());
            },
            owner);
        if (ownerId)
            return ownerId;
        if (const auto* id = Dependencies::validateRegistered(owner, device))
            return id;
        if (const auto* id =
                Contract::validateFootprint(*binding, element, owner, request.contractVersion))
            return id;
        const auto native = webGpuNativePayloadFor(*pipeline);
        const auto* nativeId = [&]() {
            if constexpr (std::is_same_v<Pipeline, rhi::ComputePipeline>)
                return WebGpuOriginalArtifactComputeFactory::validateConsumedOriginal(native);
            else
                return WebGpuOriginalArtifactGraphicsFactory::validateConsumedOriginal(native);
        }();
        if (nativeId)
            return nativeId;
        const auto& issued = *native->issuedContract();
        if (!native->pipeline || !issued.nativeDevice() || issued.semantics().usesPush
            || issued.semantics().contractVersion != 1u || !issued.tables().empty())
            return "RHI-ORIGINAL-GENERAL-BINDING-PHYSICAL-PROFILE";
        const WebGpuNativePipelineBindingFact* selected = nullptr;
        for (const auto& fact : issued.facts())
            if (fact.mapping.logicalSet == set
                && fact.mapping.logicalBinding == element.write.binding) {
                if (selected)
                    return "RHI-ORIGINAL-GENERAL-BINDING-PHYSICAL-DUPLICATE";
                selected = &fact;
            }
        if (!selected || selected->physicalType != binding->type
            || selected->shape.stages != binding->stages
            || !rhi::descriptorBindingLayoutMatches(selected->shape, *binding)
            || selected->mapping.webGpuGroup >= issued.semantics().groupCount)
            return "RHI-ORIGINAL-GENERAL-BINDING-PHYSICAL-SHAPE";
        auto entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = selected->mapping.webGpuBinding;
        // WGPU's initializer uses WHOLE_SIZE even for non-buffer entries.
        // Every physical field is explicit; only a buffer branch sets a size.
        entry.buffer = nullptr;
        entry.textureView = nullptr;
        entry.sampler = nullptr;
        entry.offset = 0u;
        entry.size = 0u;
        const auto* id = std::visit(
            [&](const auto& source) -> const char* {
                using T =
                    std::remove_const_t<typename std::decay_t<decltype(source)>::element_type>;
                const auto* object = source->originalObject();
                if constexpr (std::is_same_v<T, rhi::OriginalCommandBufferLease>) {
                    if (element.write.bufferBindingRange.contractVersion == 2u) {
                        WGPUBindGroupEntry finite{};
                        std::uint32_t finiteGroup = 0u, displacement = 0u;
                        if (const auto* failure = WebGpuOriginalRuntimeLengthIntervalLowering::
                                lowerTargetMemoryBindingDiagnostic({2u, true},
                                    device,
                                    pipeline,
                                    source,
                                    set,
                                    element.write.binding,
                                    element.write.bufferBindingRange,
                                    element.dynamicOffset,
                                    finite,
                                    finiteGroup,
                                    displacement))
                            return failure;
                        if (finiteGroup != selected->mapping.webGpuGroup
                            || finite.binding != entry.binding
                            || displacement != element.dynamicOffset
                            || finite.size < selected->nativeDeclaredMinimumBytes)
                            return "RHI-ORIGINAL-GENERAL-BINDING-TARGET-MEMBER";
                        entry = finite;
                        return nullptr;
                    }
                    if (const auto* failure = pipeline->originalDescriptorBufferBindingDiagnostic(
                            {1u, true}, binding->type, source->layout().sizeBytes, 0u))
                        return failure;
                    WebGpuTransferResourceObservation observed;
                    if (const auto* failure = observeOriginalWebGpuResource(*object, observed))
                        return failure;
                    if (!observed.buffer || observed.buffer->ownerDevice != issued.nativeDevice()
                        || observed.buffer->sizeBytes != source->layout().sizeBytes)
                        return "RHI-ORIGINAL-GENERAL-BINDING-BUFFER-IDENTITY";
                    const auto input = [&] {
                        if constexpr (std::is_same_v<Pipeline, rhi::ComputePipeline>)
                            return issued.artifactInput();
                        else
                            return issued.graphicsInput();
                    }();
                    using Artifact = std::remove_const_t<
                        typename std::decay_t<decltype(input->artifact())>::element_type>;
                    using Memory =
                        std::conditional_t<std::is_same_v<Pipeline, rhi::ComputePipeline>,
                            shader::TargetBufferMemoryLayout,
                            shader::GraphicsTargetBufferMemoryLayout>;
                    Memory memory;
                    if (const auto* failure = Artifact::originalTargetBufferMemory(
                            input->artifact(), set, element.write.binding, memory))
                        return failure;
                    const auto type = rhi::descriptorTypeFromShaderBinding(memory.type);
                    if (!type || *type != binding->type
                        || memory.target.group != selected->mapping.webGpuGroup
                        || memory.target.binding != entry.binding
                        || !memory.target.minimumBindingBytes
                        || observed.buffer->sizeBytes < memory.target.minimumBindingBytes
                        || observed.buffer->sizeBytes < selected->nativeDeclaredMinimumBytes)
                        return "RHI-ORIGINAL-GENERAL-BINDING-TARGET-MEMBER";
                    entry.buffer = observed.buffer->buffer;
                    entry.offset = 0u;
                    entry.size = observed.buffer->sizeBytes;
                } else {
                    WebGpuOrdinaryResourceObservation observed;
                    if (const auto* failure = observeOriginalWebGpuResource(*object, observed))
                        return failure;
                    if (observed.nativeDevice() != issued.nativeDevice())
                        return "RHI-ORIGINAL-GENERAL-BINDING-NATIVE-DEVICE";
                    if constexpr (std::is_same_v<T, rhi::OriginalCommandTextureViewSource>) {
                        const auto parent = webGpuNativePayloadFor(
                            *source->originalParentLease()->originalObject());
                        if (!observed.view()
                            || !core::SharedOwnerProjection::sameOwner(
                                observed.backing().texture, parent))
                            return "RHI-ORIGINAL-GENERAL-BINDING-VIEW-PARENT";
                        entry.textureView = observed.view()->view;
                    } else {
                        if (!observed.sampler())
                            return "RHI-ORIGINAL-GENERAL-BINDING-SAMPLER-IDENTITY";
                        entry.sampler = observed.sampler()->sampler;
                    }
                }
                return nullptr;
            },
            owner);
        if (id)
            return id;
        output = entry;
        group = selected->mapping.webGpuGroup;
        return nullptr;
    }
#endif
};
} // namespace arp::rhi_webgpu
