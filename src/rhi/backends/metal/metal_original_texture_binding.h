#pragma once

#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_original_buffer_limits.h"
#include "rhi/backends/metal/metal_original_pipeline.h"
#include "rhi/backends/metal/metal_resource_ownership.h"
#include "rhi/frontend/descriptor_graphics_binding_inspection.h"
#include "shader/spirv_image_declaration.h"

namespace arp::rhi_metal {
// Borrowed prerequisite for the existing ordinary group transaction. A fresh
// uninitialized original texture may be inspected; no content, publication,
// recording or completion authority is issued by this native observation.
class MetalOriginalTextureBindingInspection final {
public:
    using Contract = rhi::OriginalGraphicsBindingInspectionContract;

    struct Observation {
        bool available = false;
        std::uint32_t physicalSlot = UINT32_MAX;
        rhi::ShaderStage stages = rhi::ShaderStage::None;
        std::shared_ptr<MetalRhiNativeTexturePayload> parent;
        std::shared_ptr<MetalRhiNativeTextureViewPayload> view;
        MetalRhiNativeResourceObservation parentIdentity, viewIdentity;
        std::array<MetalRhiTextureBindingFact, 2u> facts;
        std::uint64_t allocationBytes = 0u;

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
            return "RHI-METAL-ORIGINAL-TEXTURE-BINDING-BACKEND";
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
        const auto* originalView =
            std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                &*inputs.resource);
        if (originalView && *originalView
            && sources->originalLeaseIndex((*originalView)->originalParentLease()) == SIZE_MAX)
            return "RHI-GRAPHICS-BINDING-INSPECTION-VIEW-PARENT-SOURCE";
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
        const auto* lease =
            std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                &*inputs.resource);
        if (!lease || !*lease || inputs.binding->type != rhi::DescriptorType::SampledTexture)
            return "RHI-METAL-ORIGINAL-TEXTURE-BINDING-KIND";
        const auto& parentLease = (*lease)->originalParentLease();
        if (const auto* id =
                parentLease->originalObject()->originalResourceRegistration().diagnostic())
            return id;
        const auto& pipeline = source->originalPipeline();
        const auto input = MetalOriginalPipeline::input(*pipeline);
        const auto native = metalRhiNativePayloadFor(*pipeline);
        if (!input || !native || MetalOriginalPipeline::diagnostic(*native))
            return "RHI-METAL-ORIGINAL-TEXTURE-BINDING-PIPELINE";
        const auto parent = metalRhiNativePayloadFor(*parentLease->originalObject());
        const auto view = metalRhiNativePayloadFor(*(*lease)->originalObject());
        if (!parent || !view || !parent->resource.allocationReleaseToken
            || !core::SharedOwnerProjection::sameOwner(
                parent->resource.nativeDevice, native->nativeDevice))
            return "RHI-METAL-ORIGINAL-TEXTURE-BINDING-DEVICE";
        const auto& td = parentLease->originalObject()->desc();
        const auto& vd = (*lease)->originalObject()->desc();
        const auto& binding = *inputs.binding;
        if (!(*lease)->originalViewLease()->layout().matches(vd))
            return "RHI-METAL-ORIGINAL-TEXTURE-BINDING-VIEW-LAYOUT";
        if (const auto* id = Contract::validateTexture(td, vd, binding.depthTexture))
            return id;
        if (const auto* id = binding.depthTexture
                ? validateMetalRhiNativeCompletedDepthView(*parent, *view, td, vd)
                : validateMetalRhiNativeCompletedTextureView(*parent, *view, td, vd))
            return id;
        const rhi::ShaderReflectionBinding* reflected = nullptr;
        for (const auto& candidate : input->package().reflection.descriptorBindings)
            if (candidate.set == set && candidate.binding == binding.binding) {
                if (reflected)
                    return "RHI-METAL-ORIGINAL-TEXTURE-BINDING-MAPPING";
                reflected = &candidate;
            }
        const auto* mapped =
            reflected ? MetalOriginalBufferLimitInspection::mapping(*reflected, *native) : nullptr;
        if (!mapped || mapped->type != binding.type)
            return "RHI-METAL-ORIGINAL-TEXTURE-BINDING-MAPPING";
        const auto version =
            compute ? native->computeTextureFactsVersion : native->graphicsTextureFactsVersion;
        if (version != 1u)
            return "RHI-METAL-ORIGINAL-TEXTURE-BINDING-FACTS";
        const auto& facts = compute ? native->computeTextureFacts : native->graphicsTextureFacts;
        const auto stages = native->nativeBindingStages.stages(mapped->type, mapped->metalSlot);
        const auto entries = [&] {
            if constexpr (compute)
                return std::array{
                    input->artifact(), std::shared_ptr<const shader::SingleEntryShaderArtifact>{}};
            else
                return std::array{input->artifact()->vertex(), input->artifact()->fragment()};
        }();
        const auto shaderStages = compute
            ? std::array{rhi::ShaderStage::Compute, rhi::ShaderStage::None}
            : std::array{rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment};
        Observation observed;
        auto active = rhi::ShaderStage::None;
        auto queried = rhi::ShaderStage::None;
        for (const auto& fact : facts) {
            if (fact.slot != mapped->metalSlot)
                continue;
            if ((fact.stage != shaderStages[0] && fact.stage != shaderStages[1])
                || fact.stage == rhi::ShaderStage::None
                || (queried & fact.stage) != rhi::ShaderStage::None)
                return "RHI-METAL-ORIGINAL-TEXTURE-BINDING-FACT-STAGE";
            if (const auto* id =
                    MetalRhiOriginalSampledTexture2DFloatAbi::inspect(fact, binding.depthTexture))
                return id;
            queried = queried | fact.stage;
            if (fact.active)
                active = active | fact.stage;
            observed.facts[fact.stage == shaderStages[0] ? 0u : 1u] = fact;
        }
        if (queried != stages
            || !rhi::semantics::metalOriginalBindingStagesMatch(
                binding.stages, stages, active, compute))
            return "RHI-METAL-ORIGINAL-TEXTURE-BINDING-STAGES";
        for (std::size_t i = 0u; i < entries.size(); ++i) {
            if ((binding.stages & shaderStages[i]) == rhi::ShaderStage::None)
                continue;
            if (const auto* id = shader::SingleEntryShaderArtifact::validateOriginal(entries[i]))
                return id;
            const auto& bytes = entries[i]->bytes(shader::ShaderArtifactRole::Spirv);
            shader::SpirvImageDeclaration declaration;
            if (const auto* id = shader::SpirvImageDeclarationVerifier::inspect(
                    bytes.data(), bytes.size(), set, binding.binding, declaration))
                return id;
            if (declaration.descriptorCount != 1u || declaration.dimension != 1u
                || declaration.arrayed || declaration.multisampled || declaration.sampled != 1u
                || !declaration.floatScalar || declaration.scalarWidth != 32u
                || (binding.depthTexture ? declaration.depth != 1u : declaration.depth == 1u))
                return "RHI-METAL-ORIGINAL-TEXTURE-BINDING-DECLARATION";
        }
        const char* id = nullptr;
        observed.allocationBytes = observeMetalRhiOriginalContentAllocation(parent->resource,
            td,
            parentLease->originalObject()->allocation().estimatedSizeBytes,
            id);
        if (id)
            return id;
        if (!observed.allocationBytes)
            return "RHI-METAL-ORIGINAL-TEXTURE-BINDING-ALLOCATION";
        observed.parent = parent;
        observed.view = view;
        observed.parentIdentity = MetalRhiNativeResourceObservation(parent->resource);
        observed.viewIdentity = MetalRhiNativeResourceObservation(view->resource);
        observed.physicalSlot = mapped->metalSlot;
        observed.stages = stages;
        observed.available = true;
        output = std::move(observed);
        return nullptr;
    }
};
} // namespace arp::rhi_metal
