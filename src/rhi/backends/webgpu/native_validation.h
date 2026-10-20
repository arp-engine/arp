#pragma once

#include "rhi/backends/webgpu/webgpu_pipeline_ownership.h"
#include "rhi/frontend/pipeline_depth_table_lowering_validation.h"
#include "rhi/semantics/webgpu_error_scope.h"

#include <cstdint>

// region webgpu_descriptor_array_abi
namespace arp::rhi_webgpu {
// WebGpuDescriptorArrayAbi/1: pinned emdawnwebgpu browser bridge has scalar
// resource entries and does not forward bindingArraySize. Layout metadata or
// browser shader acceptance cannot supply the missing resource-array payload.
class WebGpuDescriptorArrayAbiContract final {
public:
    static constexpr std::uint32_t version = 1u;
    static constexpr bool nativeArraysAvailable = false;

    static constexpr const char* validateCount(std::uint32_t count) noexcept
    {
        if (!count)
            return "RHI-WEBGPU-DESCRIPTOR-ARRAY-COUNT";
        if (count > 1u)
            return "RHI-WEBGPU-DESCRIPTOR-ARRAY-ABI-UNSUPPORTED";
        return nullptr;
    }

    template <class Bindings>
    static constexpr const char* validateBindings(const Bindings& bindings) noexcept
    {
        for (const auto& binding : bindings)
            if (const auto* diagnostic = validateCount(binding.arrayCount))
                return diagnostic;
        return nullptr;
    }
};
} // namespace arp::rhi_webgpu

// endregion webgpu_descriptor_array_abi

// region webgpu_original_depth_table_expansion_native

namespace arp::rhi_webgpu {
// The exact original projection is the sole element identity source. This join
// is shared by layout emission and immutable issued-payload validation.
class WebGpuOriginalDepthTableExpansionNativeContract final {
public:
    using Definition = WebGpuOriginalDepthTableExpansionPipelineDefinition;

    static rhi::DescriptorBindingDesc physicalShape(const Definition& definition, std::size_t i)
    {
        const auto& e = definition.space()->entry(i);
        const auto& layout = definition.layoutDefinition();
        rhi::DescriptorBindingDesc shape;
        if (e.logical.type == rhi::DescriptorType::SampledTexture) {
            const auto& table = layout.descriptorTables.at(0u);
            shape.type = table.type;
            shape.stages = table.stages;
            shape.depthTexture = table.depthTexture;
            shape.tableBacked = true;
            shape.textureViewDimensionConstrained = table.textureViewDimensionConstrained;
            shape.textureViewDimension = table.textureViewDimension;
        } else {
            const auto& slot = layout.descriptorSets.at(0u);
            shape = rhi::findPipelineDescriptorLayout(layout, slot.layoutName)->bindings.at(0u);
        }
        shape.binding = e.logical.binding;
        shape.arrayCount = e.physicalCardinality;
        return shape;
    }

    static const char* validateJoin(const WebGpuNativeIssuedPipelineContract& issued) noexcept
    {
        const auto& definition = issued.expansionDefinition();
        if (issued.semantics().contractVersion != 2u)
            return "RHI-ORIGINAL-TABLE-EMISSION-SOURCE-VERSION";
        if (const auto* id = Definition::validateOriginal(definition))
            return id;
        const auto& space = *definition->space();
        if (definition->backend() != rhi::Backend::WebGPU || issued.semantics().usesPush
            || issued.semantics().validatedInputAttachment
            || issued.semantics().pushGroup != UINT32_MAX
            || issued.semantics().pushBinding != UINT32_MAX
            || issued.semantics().groupCount != space.physicalGroupCount()
            || issued.facts().size() != space.entryCount()
            || issued.bindings().size() != space.entryCount()
            || issued.sets().size() != space.physicalGroupCount() || issued.tables().size() != 1u)
            return "RHI-ORIGINAL-TABLE-EMISSION-BINDINGS";
        for (std::size_t i = 0u; i < space.entryCount(); ++i) {
            const auto joined = issued.expandedBindingFact(i);
            const auto& e = joined.original;
            const auto& f = joined.native;
            const WebGpuNativeDescriptorBindingLowering expected{
                e.logical.set, e.logical.binding, e.group, e.binding};
            if (!(f.mapping == expected) || !(issued.bindings()[i] == expected)
                || !rhi::PipelineDefinitionIdentity::matches(f.shape, physicalShape(*definition, i))
                || f.physicalType != e.logical.type || f.nativeDeclaredMinimumBytes)
                return "RHI-ORIGINAL-TABLE-EMISSION-BINDINGS";
        }
        for (const auto& set : issued.sets()) {
            bool found = false;
            for (std::size_t i = 0u; i < space.entryCount(); ++i) {
                const auto& e = space.entry(i);
                if (e.logical.set == set.logicalSet && e.group == set.webGpuGroup)
                    found = true;
            }
            if (!found)
                return "RHI-ORIGINAL-TABLE-EMISSION-BINDINGS";
        }
        const auto& e = space.entry(0u);
        const WebGpuNativeDescriptorTableLowering table{e.logical.set,
            e.logical.binding,
            e.group,
            e.binding,
            e.logical.arrayCount,
            e.logical.type};
        if (!(issued.tables()[0] == table))
            return "RHI-ORIGINAL-TABLE-EMISSION-BINDINGS";
        const auto& artifact = space.computeInput() ? space.computeInput()->tableExpansion()
                                                    : space.graphicsInput()->tableExpansion();
        const auto& stages = issued.stages();
        if (stages.size() != artifact->entryCount())
            return "RHI-ORIGINAL-TABLE-EMISSION-STAGES";
        const auto& package = space.computeInput() ? space.computeInput()->package()
                                                   : space.graphicsInput()->package();
        if (issued.semantics().shaderPackageHash != rhi::stableShaderPackageHash(package))
            return "RHI-ORIGINAL-TABLE-EMISSION-STAGES";
        for (std::size_t i = 0u; i < stages.size(); ++i) {
            const auto& file =
                artifact->entry(i)
                    .files[static_cast<std::size_t>(shader::ShaderArtifactRole::Wgsl)];
            const auto& stage = stages[i];
            if (stage.stage != package.entrypoints[i].stage
                || stage.entry != package.entrypoints[i].entryPoint || !stage.moduleIdentity
                || stage.sourceBytes != file.bytes.size()
                || !core::Sha256::matchesRawHexDigest(stage.sourceSha256, file.sha256))
                return "RHI-ORIGINAL-TABLE-EMISSION-STAGES";
        }
        if (stages.size() == 2u && stages[0].moduleIdentity == stages[1].moduleIdentity)
            return "RHI-ORIGINAL-TABLE-EMISSION-STAGES";
        return nullptr;
    }
};
} // namespace arp::rhi_webgpu

// endregion webgpu_original_depth_table_expansion_native
