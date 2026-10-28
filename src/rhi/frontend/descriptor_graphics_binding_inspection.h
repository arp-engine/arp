#pragma once

#include "rhi/frontend/descriptor_general_binding_validation.h"
#include "rhi/semantics/ordinary_sampled_color.h"

namespace arp::rhi {

// OriginalGraphicsBindingInspection/1 is a cold, borrowed inspection of one
// scalar read-only binding. It neither publishes a descriptor nor authorizes
// recording, state/content changes, submission or completion.
class OriginalGraphicsBindingInspectionContract final {
public:
    template <bool compute>
    using PipelineSource = std::conditional_t<compute,
        OriginalCommandComputePipelineSource,
        OriginalCommandGraphicsPipelineSource>;

    struct BindingInputs {
        const DescriptorBindingDesc* binding = nullptr;
        std::optional<OriginalCommandDescriptorDependencies::Owner> resource;
    };

    // Borrow the existing inventory and canonical owners. These observations
    // allocate no storage and issue no publication or execution authority.
    template <bool compute>
    static const char* inspectPipeline(const GpuDevice& device,
        const std::shared_ptr<const OriginalCommandResourceSources>& sources,
        std::size_t index,
        std::shared_ptr<const PipelineSource<compute>>& output) noexcept
    {
        output.reset();
        if (const auto* id = OriginalCommandResourceSources::validateOriginal(sources))
            return id;
        if (index >= sources->sources().size())
            return "RHI-GRAPHICS-BINDING-INSPECTION-PIPELINE-POSITION";
        const auto* source =
            std::get_if<std::shared_ptr<const PipelineSource<compute>>>(&sources->sources()[index]);
        if (!source || !*source)
            return "RHI-GRAPHICS-BINDING-INSPECTION-PIPELINE-SOURCE";
        if (const auto* id = PipelineSource<compute>::validateOriginal(*source, device))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(
                sources->originalMetadataQuota(), (*source)->originalMetadataQuota()))
            return "RHI-GRAPHICS-BINDING-INSPECTION-PIPELINE-QUOTA";
        output = *source;
        return nullptr;
    }

    template <bool compute>
    static const char* inspectBinding(const GpuDevice& device,
        const std::shared_ptr<const OriginalCommandResourceSources>& sources,
        const std::shared_ptr<const PipelineSource<compute>>& source,
        std::uint32_t logicalSet,
        const OriginalCommandDescriptorSetElement& element,
        BindingInputs& output) noexcept
    {
        output = {};
        // Authenticate the same source in this inventory even if this helper
        // is called independently of inspectPipeline.
        std::shared_ptr<const PipelineSource<compute>> canonical;
        const auto index = sources ? sources->originalLeaseIndex(source) : SIZE_MAX;
        if (const auto* id = inspectPipeline<compute>(device, sources, index, canonical))
            return id;
        const auto& definition = canonical->originalLayout()->desc();
        if (!definition.descriptorTables.empty() || !definition.pushConstantRanges.empty())
            return "RHI-GRAPHICS-BINDING-INSPECTION-LAYOUT-PROFILE";
        const auto* slot = findDescriptorSetSlot(definition, logicalSet);
        const auto* layout = slot ? findDescriptorSetLayout(definition, slot->layoutName) : nullptr;
        const auto* binding =
            layout ? findDescriptorSetBinding(*layout, element.write.binding) : nullptr;
        if (!binding)
            return "RHI-GRAPHICS-BINDING-INSPECTION-LOGICAL-BINDING";
        if (const auto* id = validateShape(*binding, element, compute ? 2u : 1u))
            return id;
        if (const auto* id = OriginalCommandTypedSourceContract::validateSetElements(
                *layout, &element, 1u, sources, device))
            return id;
        using Dependencies = OriginalCommandDescriptorDependencies;
        auto owner = Dependencies::capture(*sources, element.sourceIndex);
        if (!owner)
            return "RHI-GRAPHICS-BINDING-INSPECTION-RESOURCE-SOURCE";
        if (const auto* id = Dependencies::validateOwner(
                *owner, element.registrationGeneration, sources->originalMetadataQuota()))
            return id;
        if (const auto* id = Dependencies::validateRegistered(*owner, device))
            return id;
        if (const auto* id = OriginalGeneralDescriptorBindingContract::validateFootprint(
                *binding, element, *owner, 2u))
            return id;
        output = {binding, std::move(owner)};
        return nullptr;
    }

    // Revalidate the exact owners already admitted from an original inventory.
    // This retains no inventory, creates no Source and grants no new authority.
    template <bool compute>
    static const char* inspectRetainedBinding(
        const std::shared_ptr<const PipelineSource<compute>>& source,
        std::uint32_t logicalSet,
        const OriginalCommandDescriptorSetElement& element,
        const OriginalCommandDescriptorDependencies::Owner& owner,
        BindingInputs& output) noexcept
    {
        output = {};
        if (const auto* id = PipelineSource<compute>::validateOriginal(source))
            return id;
        const auto& definition = source->originalLayout()->desc();
        const auto* slot = findDescriptorSetSlot(definition, logicalSet);
        const auto* layout = slot ? findDescriptorSetLayout(definition, slot->layoutName) : nullptr;
        const auto* binding =
            layout ? findDescriptorSetBinding(*layout, element.write.binding) : nullptr;
        if (!binding || !definition.descriptorTables.empty()
            || !definition.pushConstantRanges.empty())
            return "RHI-GRAPHICS-BINDING-INSPECTION-LAYOUT-PROFILE";
        if (const auto* id = validateShape(*binding, element, compute ? 2u : 1u))
            return id;
        using Dependencies = OriginalCommandDescriptorDependencies;
        if (const auto* id = Dependencies::validateOwner(
                owner, element.registrationGeneration, source->originalMetadataQuota()))
            return id;
        if (const auto* id = std::visit(
                [&](const auto& resource) -> const char* {
                    if (resource->resource() != element.write.resource)
                        return "RHI-GRAPHICS-BINDING-INSPECTION-RESOURCE-IDENTITY";
                    return resource->originalObject()->originalResourceRegistration().diagnostic();
                },
                owner))
            return id;
        if (const auto* id = OriginalGeneralDescriptorBindingContract::validateFootprint(
                *binding, element, owner, 2u))
            return id;
        output = {binding, owner};
        return nullptr;
    }

    struct Request {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        semantics::OriginalOrdinarySampledColorContract::Intent sampledColor;
    };

    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, inspectionAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-GRAPHICS-BINDING-INSPECTION-BACKEND";
    };

    static constexpr const char* validate(Request request) noexcept
    {
        if (const auto* id =
                semantics::OriginalOrdinarySampledColorContract::validate(request.sampledColor))
            return id;
        if (!request.sampledColor.empty() && request.contractVersion != 1u)
            return "RHI-ORDINARY-SAMPLED-COLOR-INSPECTION-PROFILE";
        if (request.contractVersion != 1u && request.contractVersion != 2u)
            return "RHI-GRAPHICS-BINDING-INSPECTION-VERSION";
        return request.prototypeOptIn ? nullptr : "RHI-GRAPHICS-BINDING-INSPECTION-OPT-IN";
    }

    static constexpr Decision lowering(Backend backend, Request request) noexcept
    {
        if (const auto* id = validate(request))
            return {CapabilityMode::DiagnosticFailure, false, false, false, id};
        if (!request.sampledColor.empty()) {
            const auto decision = semantics::OriginalOrdinarySampledColorContract::lowering(
                backend, request.sampledColor);
            if (!decision.nativeAvailable)
                return {CapabilityMode::DiagnosticFailure,
                    decision.validatorOnly,
                    false,
                    false,
                    decision.diagnostic};
        }
        return semantics::DescriptorLowering::OriginalGraphicsBindingInspection_lowering<Decision>(
            backend);
    }

    static const char* validateShape(const DescriptorBindingDesc& binding,
        const OriginalCommandDescriptorSetElement& element,
        std::uint32_t version = 1u) noexcept
    {
        if (version == 2u) {
            if (binding.stages != ShaderStage::Compute || binding.dynamicOffset
                || element.dynamicOffset
                || (binding.type != DescriptorType::UniformBuffer
                    && binding.type != DescriptorType::ReadOnlyStorageBuffer
                    && binding.type != DescriptorType::StorageBuffer
                    && binding.type != DescriptorType::SampledTexture)
                || (binding.type == DescriptorType::SampledTexture
                    && (!binding.textureViewDimensionConstrained
                        || binding.textureViewDimension != TextureViewDimension::Texture2D)))
                return "RHI-COMPUTE-BINDING-INSPECTION-PROFILE";
            return OriginalGeneralDescriptorBindingContract::validateShape(binding, element, 2u);
        }
        if (version != 1u)
            return "RHI-GRAPHICS-BINDING-INSPECTION-VERSION";
        if (binding.dynamicOffset || element.dynamicOffset
            || (binding.type != DescriptorType::UniformBuffer
                && binding.type != DescriptorType::ReadOnlyStorageBuffer
                && binding.type != DescriptorType::SampledTexture
                && binding.type != DescriptorType::Sampler)
            || (binding.stages != ShaderStage::Vertex && binding.stages != ShaderStage::Fragment
                && binding.stages != (ShaderStage::Vertex | ShaderStage::Fragment))
            || (binding.type == DescriptorType::SampledTexture
                && (!binding.textureViewDimensionConstrained
                    || binding.textureViewDimension != TextureViewDimension::Texture2D)))
            return "RHI-GRAPHICS-BINDING-INSPECTION-READ-ONLY-PROFILE";
        return OriginalGeneralDescriptorBindingContract::validateShape(binding, element, 2u);
    }

    // A declaration extent is not a shader access proof. This finite profile
    // requires at least one complete runtime element and no partial tail.
    // Facts must come from the original selected backend artifact.
    static constexpr const char* validateBufferExtent(std::uint64_t bytes,
        std::uint64_t staticExtent,
        bool runtime,
        std::uint64_t runtimeOffset,
        std::uint64_t runtimeStride,
        std::uint64_t elementExtent,
        std::uint64_t& elementCount) noexcept
    {
        elementCount = 0u;
        if (!bytes || bytes < staticExtent || (!runtime && !staticExtent))
            return "RHI-GRAPHICS-BINDING-INSPECTION-BUFFER-EXTENT";
        if (!runtime)
            return runtimeOffset || runtimeStride || elementExtent
                ? "RHI-GRAPHICS-BINDING-INSPECTION-BUFFER-ABI"
                : nullptr;
        if (!runtimeStride || !elementExtent || elementExtent > runtimeStride
            || bytes < runtimeOffset || (bytes - runtimeOffset) % runtimeStride
            || (bytes - runtimeOffset) / runtimeStride == 0u)
            return "RHI-GRAPHICS-BINDING-INSPECTION-BUFFER-RUNTIME-ELEMENTS";
        elementCount = (bytes - runtimeOffset) / runtimeStride;
        return nullptr;
    }

    static const char* validateTexture(const TextureDesc& parent,
        const TextureViewDesc& view,
        bool depth,
        semantics::OriginalOrdinarySampledColorContract::Intent sampledColor = {}) noexcept
    {
        if (const auto* id =
                semantics::OriginalOrdinarySampledColorContract::validate(sampledColor))
            return id;
        if (!sampledColor.empty())
            return depth
                ? "RHI-ORDINARY-SAMPLED-COLOR-DEPTH-PROFILE"
                : semantics::OriginalOrdinarySampledColorContract::validateView(parent, view);
        if (parent.sampleCount != 1u || parent.extent.depth != 1u || view.format != parent.format
            || view.dimension != TextureViewDimension::Texture2D || view.arrayLayerCount != 1u
            || (depth
                    ? view.format != TextureFormat::D32Float || view.aspect != TextureAspect::Depth
                    : (view.format != TextureFormat::Rgba8Unorm
                          && view.format != TextureFormat::Rgba16Float)
                        || view.aspect != TextureAspect::Color))
            return "RHI-GRAPHICS-BINDING-INSPECTION-TEXTURE-PROFILE";
        return nullptr;
    }

    struct BufferExtent {
        shader::SpirvBufferDeclarationLayout declaration;
        std::uint64_t runtimeElementCount = 0u;
    };

    static const char* inspectBufferExtent(
        const std::array<std::shared_ptr<const shader::SingleEntryShaderArtifact>, 2u>& entries,
        const std::array<ShaderStage, 2u>& stages,
        std::uint32_t set,
        const DescriptorBindingDesc& binding,
        std::uint64_t bytes,
        BufferExtent& output) noexcept
    {
        output = {};
        BufferExtent observed;
        bool seen = false;
        for (std::size_t i = 0u; i < entries.size(); ++i) {
            if ((binding.stages & stages[i]) == ShaderStage::None)
                continue;
            shader::SpirvBufferDeclarationLayout facts;
            if (const auto* id = shader::SingleEntryShaderArtifact::originalBufferDeclarationLayout(
                    entries[i], set, binding.binding, facts))
                return id;
            const auto& first = observed.declaration;
            if (facts.descriptorCount != 1u
                || (seen
                    && (facts.staticOccupiedExtentBytes != first.staticOccupiedExtentBytes
                        || facts.hasRuntimeArray != first.hasRuntimeArray
                        || facts.runtimeArrayOffsetBytes != first.runtimeArrayOffsetBytes
                        || facts.runtimeArrayStrideBytes != first.runtimeArrayStrideBytes
                        || facts.runtimeElementOccupiedExtentBytes
                            != first.runtimeElementOccupiedExtentBytes)))
                return "RHI-GRAPHICS-BINDING-INSPECTION-BUFFER-ABI";
            observed.declaration = facts;
            seen = true;
        }
        if (!seen)
            return "RHI-GRAPHICS-BINDING-INSPECTION-BUFFER-EXTENT";
        const auto& facts = observed.declaration;
        if (const auto* id = validateBufferExtent(bytes,
                facts.staticOccupiedExtentBytes,
                facts.hasRuntimeArray,
                facts.runtimeArrayOffsetBytes,
                facts.runtimeArrayStrideBytes,
                facts.runtimeElementOccupiedExtentBytes,
                observed.runtimeElementCount))
            return id;
        output = observed;
        return nullptr;
    }
};

} // namespace arp::rhi
