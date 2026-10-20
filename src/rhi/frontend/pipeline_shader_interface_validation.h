#pragma once
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/frontend/pipeline_depth_table_lowering_validation.h"
#include "rhi/frontend/pipeline_vertex_slots.h"
#include "rhi/gpu_capabilities.h"
#include "rhi/gpu_common.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/pipeline_creation_lowering.h"

// region pipeline_shader_declaration
namespace arp::rhi {
// Reused cold selected-entry L2 contract. Declaration validation cannot issue
// immutable artifact, native pipeline or canonical Source authority.
class PipelineShaderDeclarationContract final {
public:
    enum class Kind { GraphicsPipeline, ComputePipeline };

    struct TableExpansionRequest {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
    };

    static const char* validateRegisteredDepthTable(const DescriptorTableDesc& table) noexcept
    {
        // Exact logical Inputs declaration of OriginalDepthTableScalarExpansion/1.
        return table.label == "Inputs" && table.type == DescriptorType::SampledTexture
                && table.capacity == 3u && table.depthTexture
                && table.textureViewDimensionConstrained
                && table.textureViewDimension == TextureViewDimension::Texture2D
                && (table.stages == ShaderStage::Compute || table.stages == ShaderStage::Fragment)
            ? nullptr
            : "RHI-ORIGINAL-TABLE-REGISTRATION-SHAPE";
    }

    // Complete ordered logical candidate only. Resource custody, successful
    // writes, scalar native group creation and command authority remain separate.
    static const char* validateDepthTableExpansionWrites(TableExpansionRequest request,
        const PipelineLayoutDesc& layout,
        const DescriptorTableDesc& table,
        const DescriptorTableWrite* writes,
        std::size_t count) noexcept
    {
        if (request.contractVersion != 1u)
            return "RHI-ORIGINAL-TABLE-WRITE-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-ORIGINAL-TABLE-WRITE-OPT-IN";
        if (const auto* id = validateRegisteredDepthTable(table))
            return id;
        if (const auto* id = DescriptorSlotShapeContract::validateTable(
                layout, 0u, table, DescriptorSlotShapeContract::TableMatch::ExactOriginal))
            return id;
        if (!writes || count != table.capacity)
            return "RHI-ORIGINAL-TABLE-WRITE-COVERAGE";
        for (std::size_t index = 0u; index < count; ++index)
            if (writes[index].index != index || writes[index].type != table.type
                || writes[index].resourceKind != DescriptorResourceKind::TextureView
                || !writes[index].resource.isValid())
                return "RHI-ORIGINAL-TABLE-WRITE-ELEMENT";
        return nullptr;
    }

    // OriginalDepthTableExpansionPipelineDeclaration/1. Logical metadata only;
    // ordinary package/native admission remains separate and closed.
    template <class Input, class EntryAt>
    static const char* validateTableExpansion(TableExpansionRequest request,
        const std::shared_ptr<const Input>& original,
        const PipelineLayoutDesc& layout,
        const ShaderPackageDesc& package,
        std::size_t count,
        Kind kind,
        EntryAt&& entryAt)
    {
        if (request.contractVersion != 1u)
            return "RHI-ORIGINAL-TABLE-DECLARATION-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-ORIGINAL-TABLE-DECLARATION-OPT-IN";
        if (const auto* id = Input::tableExpansionPackageDiagnostic(original, package))
            return id;
        const auto& artifact = *original->tableExpansion();
        const bool compute =
            artifact.profile() == shader::OriginalDepthTableExpansionArtifact::Profile::Compute;
        if (kind != (compute ? Kind::ComputePipeline : Kind::GraphicsPipeline))
            return "RHI-ORIGINAL-TABLE-DECLARATION-KIND";
        if (count != artifact.entryCount())
            return "RHI-ORIGINAL-TABLE-DECLARATION-ENTRIES";
        if (layout.descriptorTables.size() != 1u || !layout.pushConstantRanges.empty())
            return "RHI-ORIGINAL-TABLE-DECLARATION-LAYOUT";
        const auto& table = artifact.logicalBindings().front();
        DescriptorTableDesc expected{
            core::pmr::String(table.name, &core::defaultCpuMemoryResource()),
            DescriptorType::SampledTexture,
            static_cast<ShaderStage>(table.stageMask),
            table.arrayCount,
            true,
            TextureViewDimension::Texture2D,
            true};
        if (const auto* id = DescriptorSlotShapeContract::validateTable(layout,
                table.set,
                expected,
                DescriptorSlotShapeContract::TableMatch::ExactOriginal))
            return id;
        if (compute) {
            if (layout.descriptorLayouts.size() != 1u || layout.descriptorSets.size() != 1u
                || layout.descriptorSets[0].set != 1u)
                return "RHI-ORIGINAL-TABLE-DECLARATION-OUTPUT";
            const auto* output =
                findDescriptorSetLayout(layout, layout.descriptorSets[0].layoutName);
            if (!output || output->bindings.size() != 1u)
                return "RHI-ORIGINAL-TABLE-DECLARATION-OUTPUT";
            const auto& binding = output->bindings.front();
            if (binding.binding || binding.type != DescriptorType::StorageBuffer
                || binding.stages != ShaderStage::Compute || binding.arrayCount != 1u
                || binding.dynamicOffset || binding.tableBacked || binding.depthTexture)
                return "RHI-ORIGINAL-TABLE-DECLARATION-OUTPUT";
        } else if (!layout.descriptorLayouts.empty() || !layout.descriptorSets.empty())
            return "RHI-ORIGINAL-TABLE-DECLARATION-OUTPUT";
        return validateSelectedInterface(
            layout, package, count, kind, std::forward<EntryAt>(entryAt));
    }

    template <class EntryAt>
    static const char* validate(const PipelineLayoutDesc& layout,
        const ShaderPackageDesc& package,
        std::size_t count,
        Kind kind,
        EntryAt&& entryAt)
    {
        if (kind != Kind::GraphicsPipeline && kind != Kind::ComputePipeline)
            return "RHI-ORIGINAL-TYPED-PIPELINE-KIND";
        if (!count)
            return "RHI-ORIGINAL-TYPED-PIPELINE-ENTRY";
        if (!validateShaderPackage(package).success)
            return "RHI-ORIGINAL-TYPED-SHADER-DECLARATION";
        return validateSelectedInterface(
            layout, package, count, kind, std::forward<EntryAt>(entryAt));
    }

    template <class Input, class EntryAt>
    static const char* validateOriginal(const std::shared_ptr<const Input>& original,
        const PipelineLayoutDesc& layout,
        const ShaderPackageDesc& package,
        std::size_t count,
        Kind kind,
        EntryAt&& entryAt)
    {
        if (kind != Kind::GraphicsPipeline && kind != Kind::ComputePipeline)
            return "RHI-ORIGINAL-TYPED-PIPELINE-KIND";
        if (!count)
            return "RHI-ORIGINAL-TYPED-PIPELINE-ENTRY";
        if (const auto* id = Input::packageDiagnostic(original, package))
            return id;
        return validateSelectedInterface(
            layout, package, count, kind, std::forward<EntryAt>(entryAt));
    }

private:
    template <class EntryAt>
    static const char* validateSelectedInterface(const PipelineLayoutDesc& layout,
        const ShaderPackageDesc& package,
        std::size_t count,
        Kind kind,
        EntryAt&& entryAt)
    {
        if (!validatePipelineLayoutAgainstShaderReflection(layout, package.reflection).success)
            return "RHI-ORIGINAL-TYPED-PIPELINE-REFLECTION";
        ShaderStage stages = ShaderStage::None;
        for (std::size_t i = 0u; i < count; ++i) {
            const auto& entry = entryAt(i);
            if ((stages & entry.stage) != ShaderStage::None)
                return "RHI-ORIGINAL-TYPED-PIPELINE-ENTRY";
            bool matched = false;
            for (const auto& original : package.entrypoints)
                if (original.matchesDefinition(entry))
                    matched = true;
            if (!matched)
                return "RHI-ORIGINAL-TYPED-PIPELINE-ENTRY";
            stages = stages | entry.stage;
        }
        if (kind == Kind::ComputePipeline) {
            if (stages != ShaderStage::Compute || count != 1u)
                return "RHI-ORIGINAL-TYPED-PIPELINE-STAGES";
            const auto& entry = entryAt(0u);
            const auto& reflection = package.reflection;
            if (!entry.computeWorkgroupSizeX || !entry.computeWorkgroupSizeY
                || !entry.computeWorkgroupSizeZ
                || entry.computeWorkgroupSizeX != reflection.computeWorkgroupSizeX
                || entry.computeWorkgroupSizeY != reflection.computeWorkgroupSizeY
                || entry.computeWorkgroupSizeZ != reflection.computeWorkgroupSizeZ)
                return "RHI-ORIGINAL-TYPED-WORKGROUP-REFLECTION";
        } else {
            const auto base = ShaderStage::Vertex;
            const auto tessellation =
                ShaderStage::TessellationControl | ShaderStage::TessellationEvaluation;
            const auto raster = stages & (ShaderStage::Vertex | ShaderStage::Fragment);
            if ((raster & base) != base || (stages != raster && stages != (raster | tessellation)))
                return "RHI-ORIGINAL-TYPED-PIPELINE-STAGES";
        }
        return nullptr;
    }
};

// endregion pipeline_shader_declaration

// region original_graphics_artifact_consumption

class OriginalGraphicsArtifactConsumptionContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxTargetBytes = 0u, maxCpuMetadataBytes = 0u;
    };

    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-BACKEND";
    };

    static constexpr const char* validateBudget(Budget budget) noexcept
    {
        if (budget.contractVersion != 1u && budget.contractVersion != 2u)
            return "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-OPT-IN";
        if (!budget.maxTargetBytes || !budget.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-BUDGET";
        return nullptr;
    }

    static constexpr const char* creationValidationDiagnostic() noexcept
    {
        return "RHI-ORIGINAL-GRAPHICS-CREATION-RECEIPT-MISSING";
    }

    // Selected combined-module profile. Unsupported state rejects before any
    // native call; the old emitter cannot silently define this L2 profile.
    static const char* validateProfile(
        const RenderPipelineDesc& desc, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u && version != 2u)
            return "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-VERSION";
        if (desc.topology != PrimitiveTopology::TriangleList)
            return "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-TOPOLOGY-PROFILE";
        if (desc.renderPassCompatibility.enabled() || desc.subpassIndex)
            return "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-SUBPASS-PROFILE";
        if (desc.depthStencil.depthBiasEnabled)
            return "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-DEPTH-BIAS-PROFILE";
        // WebGPU's core specification defines rasterization sample counts 1/4.
        if (desc.multisample.sampleCount != 1u && desc.multisample.sampleCount != 4u)
            return "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-MSAA-PROFILE";
        for (std::size_t i = 0u; i < desc.vertexBuffers.size(); ++i)
            if (version == 1u && desc.vertexBuffers[i].slot != i)
                return "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-VERTEX-SLOT-PROFILE";
        if (version == 2u) {
            std::uint32_t slots = 0;
            if (const auto* id =
                    VertexStreamSlotLayout::count(desc.vertexBuffers, UINT32_MAX, slots))
                return id;
        }
        return nullptr;
    }

    static constexpr Decision lowering(Backend backend, bool liveNativeDevice = false) noexcept
    {
        return semantics::PipelineCreationLowering::OriginalGraphicsArtifactConsumption_lowering<
            Decision>(backend, liveNativeDevice);
    }
};
} // namespace arp::rhi

// endregion original_graphics_artifact_consumption
