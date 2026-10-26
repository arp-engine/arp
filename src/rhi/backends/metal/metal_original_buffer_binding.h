#pragma once

#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_original_buffer_limits.h"
#include "rhi/backends/metal/metal_original_pipeline.h"
#include "rhi/frontend/descriptor_graphics_binding_inspection.h"

namespace arp::rhi_metal {
// Backend-private buffer part of ordinary synchronous group preflight. This
// borrows the existing canonical inventory and actual pipeline member proof;
// it does not issue a group, Source, recording or completion authority. The
// complete Metal inspection/publication lowering stays closed until sampled
// resources and the existing transaction are implemented as well.
class MetalOriginalBufferBindingInspection final {
public:
    using Contract = rhi::OriginalGraphicsBindingInspectionContract;

    struct Observation {
        bool available = false;
        std::uint32_t physicalSlot = UINT32_MAX;
        rhi::ShaderStage stages = rhi::ShaderStage::None;
        std::shared_ptr<MetalRhiNativeBufferPayload> buffer;
        MetalRhiNativeResourceObservation nativeIdentity;
        std::uint64_t offsetBytes = 0u, sizeBytes = 0u;
        std::uint64_t alignmentBytes = 0u, requiredBytes = 0u, allocationBytes = 0u;
        Contract::BufferExtent extent;
        std::array<MetalRhiStageBufferRange, 2u> graphicsRanges;
        MetalRhiBufferBindingFact computeFact;

        bool nativeExecutionProven() const noexcept { return false; }
    };

    static const char* inspect(Contract::Request request,
        const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
        std::size_t pipelineIndex,
        std::uint32_t set,
        const rhi::OriginalCommandDescriptorSetElement& element,
        Observation& output) noexcept
    {
        output = {};
        if (const auto* id = Contract::validate(request))
            return id;
        if (device.backend() != rhi::Backend::Metal)
            return "RHI-METAL-ORIGINAL-BUFFER-BINDING-BACKEND";
        return request.contractVersion == 2u
            ? inspectOriginal<true>(device, sources, pipelineIndex, set, element, output)
            : inspectOriginal<false>(device, sources, pipelineIndex, set, element, output);
    }

    template <bool compute>
    static const char* inspectRetained(
        const std::shared_ptr<const Contract::PipelineSource<compute>>& source,
        std::uint32_t set,
        const rhi::OriginalCommandDescriptorSetElement& element,
        const rhi::OriginalCommandDescriptorDependencies::Owner& owner,
        Observation& output) noexcept
    {
        output = {};
        Contract::BindingInputs inputs;
        if (const auto* id =
                Contract::inspectRetainedBinding<compute>(source, set, element, owner, inputs))
            return id;
        return inspectNative<compute>(source, set, element, inputs, output);
    }

private:
    template <bool compute>
    static const char* inspectOriginal(const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
        std::size_t pipelineIndex,
        std::uint32_t set,
        const rhi::OriginalCommandDescriptorSetElement& element,
        Observation& output) noexcept
    {
        std::shared_ptr<const Contract::PipelineSource<compute>> source;
        if (const auto* id =
                Contract::inspectPipeline<compute>(device, sources, pipelineIndex, source))
            return id;
        Contract::BindingInputs inputs;
        if (const auto* id =
                Contract::inspectBinding<compute>(device, sources, source, set, element, inputs))
            return id;
        return inspectNative<compute>(source, set, element, inputs, output);
    }

    template <bool compute>
    static const char* inspectNative(
        const std::shared_ptr<const Contract::PipelineSource<compute>>& source,
        std::uint32_t set,
        const rhi::OriginalCommandDescriptorSetElement& element,
        const Contract::BindingInputs& inputs,
        Observation& output) noexcept
    {
        const auto& binding = *inputs.binding;
        const auto* lease =
            std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(&*inputs.resource);
        if (!lease || !*lease)
            return "RHI-METAL-ORIGINAL-BUFFER-BINDING-KIND";
        const auto& pipeline = source->originalPipeline();
        const auto input = MetalOriginalPipeline::input(*pipeline);
        const auto native = metalRhiNativePayloadFor(*pipeline);
        if (!input || !native || MetalOriginalPipeline::diagnostic(*native))
            return "RHI-METAL-ORIGINAL-BUFFER-BINDING-PIPELINE";
        const auto buffer = metalRhiNativePayloadFor(*(*lease)->originalObject());
        if (!buffer
            || !core::SharedOwnerProjection::sameOwner(
                buffer->resource.nativeDevice, native->nativeDevice))
            return "RHI-METAL-ORIGINAL-BUFFER-BINDING-DEVICE";
        if (const auto* id = validateMetalRhiTrackedNativeBufferObject(
                buffer->resource, (*lease)->layout().sizeBytes))
            return id;
        if (binding.type == rhi::DescriptorType::StorageBuffer
            && (*lease)->originalObject()->gpuWritesSealed())
            return "RHI-BUFFER-GPU-WRITES-SEALED";
        rhi::DescriptorBufferBindingRangeContract::Resolved range;
        if (const auto* id = rhi::DescriptorBufferBindingRangeContract::resolve(
                element.write.bufferBindingRange, (*lease)->layout().sizeBytes, 0u, range))
            return id;
        if (const auto* id = pipeline->originalDescriptorBufferBindingDiagnostic(
                {1u, true}, binding.type, range.sizeBytes, element.dynamicOffset))
            return id;
        const rhi::ShaderReflectionBinding* reflected = nullptr;
        for (const auto& candidate : input->package().reflection.descriptorBindings)
            if (candidate.set == set && candidate.binding == binding.binding) {
                if (reflected)
                    return "RHI-METAL-ORIGINAL-BUFFER-BINDING-MAPPING";
                reflected = &candidate;
            }
        const auto* mapped =
            reflected ? MetalOriginalBufferLimitInspection::mapping(*reflected, *native) : nullptr;
        if (!mapped || mapped->type != binding.type)
            return "RHI-METAL-ORIGINAL-BUFFER-BINDING-MAPPING";
        const auto stages = native->nativeBindingStages.stages(mapped->type, mapped->metalSlot);
        const auto entries = [&] {
            const auto& artifact = input->artifact();
            if constexpr (compute)
                return std::array{
                    artifact, std::shared_ptr<const shader::SingleEntryShaderArtifact>{}};
            else
                return std::array{artifact->vertex(), artifact->fragment()};
        }();
        const auto shaderStages = compute
            ? std::array{rhi::ShaderStage::Compute, rhi::ShaderStage::None}
            : std::array{rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment};
        Observation observed;
        if (const auto* id = Contract::inspectBufferExtent(
                entries, shaderStages, set, binding, range.sizeBytes, observed.extent))
            return id;
        const bool readOnlyStorage = binding.type == rhi::DescriptorType::ReadOnlyStorageBuffer;
        if (readOnlyStorage)
            for (std::size_t i = 0u; i < entries.size(); ++i)
                if ((binding.stages & shaderStages[i]) != rhi::ShaderStage::None)
                    if (const auto* id =
                            shader::SingleEntryShaderArtifact::originalReadOnlyStorageDiagnostic(
                                entries[i], set, binding.binding))
                        return id;
        auto activeStages = rhi::ShaderStage::None;
        for (std::size_t i = 0u; i < entries.size(); ++i) {
            if ((stages & shaderStages[i]) == rhi::ShaderStage::None)
                continue;
            if (!compute && native->graphicsBufferFacts[i].stage != shaderStages[i])
                return "RHI-METAL-ORIGINAL-BUFFER-BINDING-MEMBER-STAGE";
            const auto& member =
                compute ? native->computeBufferMemberMemory : native->graphicsBufferMemberMemory[i];
            const auto& facts =
                compute ? native->computeBufferFacts : native->graphicsBufferFacts[i].entries;
            const auto version = compute ? native->computeBufferFactsVersion
                                         : native->graphicsBufferFacts[i].version;
            const MetalRhiBufferBindingFact* fact = nullptr;
            if (const auto* id = validateMetalRhiBufferOffset(facts,
                    version,
                    mapped->metalSlot,
                    range.offsetBytes,
                    (*lease)->layout().sizeBytes,
                    fact))
                return id;
            if (fact->active > 1u)
                return "RHI-METAL-ORIGINAL-BUFFER-BINDING-ACTIVITY-UNKNOWN";
            if (fact->active)
                activeStages = activeStages | shaderStages[i];
            if (const auto* id = rhi::semantics::metalOriginalBufferAccessDiagnostic(
                    binding.type, fact->access, readOnlyStorage))
                return id;
            // Type comes from the actual member graph, not a blanket scalar uint.
            if (fact->elementType == UINT32_MAX || fact->requiredBytes > range.sizeBytes)
                return "RHI-METAL-ORIGINAL-BUFFER-BINDING-ACCESS-RANGE";
            std::uint32_t alignment = static_cast<std::uint32_t>(observed.alignmentBytes);
            if (!MetalOriginalBufferLimitInspection::accumulate(alignment, fact->alignment))
                return "RHI-METAL-ORIGINAL-BUFFER-BINDING-ALIGNMENT";
            observed.alignmentBytes = alignment;
            observed.requiredBytes = std::max(observed.requiredBytes, fact->requiredBytes);
            if constexpr (compute)
                observed.computeFact = *fact;
            else
                observed.graphicsRanges[i] = {shaderStages[i], *fact, range.offsetBytes};
            if ((binding.stages & shaderStages[i]) == rhi::ShaderStage::None)
                continue;
            if (member.version != 1u || member.stage != shaderStages[i])
                return "RHI-METAL-ORIGINAL-BUFFER-BINDING-MEMBER-STAGE";
            const MetalRhiNativeBufferMemberMemory* memory = nullptr;
            for (const auto& candidate : member.entries)
                if (candidate.set == set && candidate.binding == binding.binding) {
                    if (memory)
                        return "RHI-METAL-ORIGINAL-BUFFER-BINDING-MEMBER-DUPLICATE";
                    memory = &candidate;
                }
            const shader::NativeBufferMemoryDeclarations* declarations = nullptr;
            if (const auto* id = shader::SingleEntryShaderArtifact::originalNativeBufferMemory(
                    entries[i], declarations))
                return id;
            const auto* declared =
                declarations ? declarations->lookup(set, binding.binding) : nullptr;
            if (!declared || !memory || memory->slot != mapped->metalSlot
                || memory->targetType >= memory->target.nodes().size()
                || !memory->relation.comparedPairs)
                return "RHI-METAL-ORIGINAL-BUFFER-BINDING-MEMBER-PROOF";
            const auto& extent = observed.extent.declaration;
            const auto& relation = memory->relation;
            if (relation.hasRuntimeArray != extent.hasRuntimeArray
                || relation.occupiedBytes != extent.staticOccupiedExtentBytes
                || (extent.hasRuntimeArray
                        ? relation.sourceRuntimeStrideBytes != extent.runtimeArrayStrideBytes
                            || relation.targetRuntimeStrideBytes != extent.runtimeArrayStrideBytes
                            || memory->element.strideBytes != extent.runtimeArrayStrideBytes
                            || !memory->element.alignmentBytes
                            || range.offsetBytes % memory->element.alignmentBytes
                            || memory->target.nodes()[memory->targetType].occupiedExtentBytes
                                != extent.runtimeElementOccupiedExtentBytes
                        : relation.occupiedBytes != extent.staticOccupiedExtentBytes))
                return "RHI-METAL-ORIGINAL-BUFFER-BINDING-MEMBER-EXTENT";
            if (!extent.hasRuntimeArray
                && memory->target.nodes()[memory->targetType].occupiedExtentBytes
                    != relation.occupiedBytes)
                return "RHI-METAL-ORIGINAL-BUFFER-BINDING-MEMBER-EXTENT";
        }
        if (const auto* id = rhi::semantics::metalOriginalBufferStagesDiagnostic(
                binding.stages, stages, activeStages, compute))
            return id;
        const char* id = nullptr;
        observed.allocationBytes = observeMetalRhiNativeBufferAllocation(
            buffer->resource, (*lease)->layout().sizeBytes, id);
        if (id)
            return id;
        if (!observed.allocationBytes)
            return "RHI-METAL-ORIGINAL-BUFFER-BINDING-ALLOCATION";
        observed.buffer = buffer;
        observed.nativeIdentity = MetalRhiNativeResourceObservation(buffer->resource);
        observed.physicalSlot = mapped->metalSlot;
        observed.stages = stages;
        observed.offsetBytes = range.offsetBytes;
        observed.sizeBytes = range.sizeBytes;
        observed.available = true;
        output = std::move(observed);
        return nullptr;
    }
};
} // namespace arp::rhi_metal
