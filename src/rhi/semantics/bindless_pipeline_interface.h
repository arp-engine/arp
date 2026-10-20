#pragma once
#include "rhi/gpu_bindless.h"
#include "rhi/semantics/bindless_ordinary_color_domain.h"
#include "rhi/semantics/bindless_ordinary_depth_pipeline.h"

namespace arp::rhi::semantics {

// PipelineInterface/1 is shared shader ABI and budget inspection. Native
// execution additionally needs typed ordinary sources in the original Root
// inventory, state journal and real completion closure. None is implied here.
class BindlessPipelineInterfaceContract final {
public:
    template <class Reflection>
    static bool explicitWebGpuInactiveBinding(
        const Reflection& reflection, std::uint32_t set, std::uint32_t binding) noexcept
    {
        if (reflection.webGpuBindingSpaceContractVersion != "WebGpuBindingSpace/5"
            || reflection.webGpuBindingSpaceStatus != "Available")
            return false;
        for (const auto& logical : reflection.bindings)
            if (logical.set == set && logical.binding == binding)
                return false;
        std::uint32_t count = 0u;
        for (const auto& map : reflection.webGpuBindingSpace)
            if (map.set == set && map.binding == binding) {
                if (map.active)
                    return false;
                ++count;
            }
        return count == 1u;
    }

    // A full snapshot may contain declarations unused by this selected entry.
    // Only authenticated per-entry reflection can prove absence: the logical
    // binding is absent and both Metal namespaces explicitly mark it inactive.
    // Missing/duplicate/conflicting mappings never mean inactive. This changes
    // validation correspondence only, not snapshot usage or execution authority.
    template <class Reflection>
    static bool explicitMetalInactiveBinding(
        const Reflection& reflection, std::uint32_t set, std::uint32_t binding) noexcept
    {
        if (reflection.metalBindingSpaceContractVersion != "MetalBindingSpace/1"
            || reflection.metalBindingSpaceStatus != "Available"
            || reflection.metalArgumentBufferContractVersion != "MetalArgumentBuffer/1"
            || reflection.metalArgumentBufferStatus != "Available")
            return false;
        for (const auto& logical : reflection.bindings)
            if (logical.set == set && logical.binding == binding)
                return false;
        std::uint32_t direct = 0u, arrays = 0u;
        for (const auto& map : reflection.metalBindingSpace)
            if (map.set == set && map.binding == binding) {
                if (map.active)
                    return false;
                ++direct;
            }
        for (const auto& map : reflection.metalArgumentBuffer)
            if (map.set == set && map.binding == binding) {
                if (map.active)
                    return false;
                ++arrays;
            }
        return direct == 1u && arrays == 1u;
    }

    // Physical binding intervals, including inactive scalar slots, participate
    // in the native pass usage scope. Shader access guards cannot remove aliasing.
    static const char* bufferAliasDiagnostic(Backend backend,
        const DescriptorBindingWrite& lhs,
        std::uint64_t lhsBytes,
        const DescriptorBindingWrite& rhs,
        std::uint64_t rhsBytes) noexcept
    {
        const auto buffer = [](DescriptorType type) {
            return type == DescriptorType::UniformBuffer
                || type == DescriptorType::ReadOnlyStorageBuffer
                || type == DescriptorType::StorageBuffer;
        };
        if (backend != Backend::WebGPU || lhs.resource != rhs.resource || !buffer(lhs.type)
            || !buffer(rhs.type)
            || (lhs.type != DescriptorType::StorageBuffer
                && rhs.type != DescriptorType::StorageBuffer))
            return nullptr;
        DescriptorBufferBindingRangeContract::Resolved a, b;
        if (const auto* id = DescriptorBufferBindingRangeContract::resolve(
                lhs.bufferBindingRange, lhsBytes, 0u, a))
            return id;
        if (const auto* id = DescriptorBufferBindingRangeContract::resolve(
                rhs.bufferBindingRange, rhsBytes, 0u, b))
            return id;
        return core::overlapsUnsignedRanges(a.offsetBytes, a.sizeBytes, b.offsetBytes, b.sizeBytes)
            ? "RHI-WEBGPU-BINDLESS-PIPELINE-WRITABLE-BUFFER-ALIAS"
            : nullptr;
    }

    // The backend supplies authenticated parent identity and normalized view
    // ranges. Every bound slot participates, including padded scalar slots.
    // Native backends retain their explicit state/hazard validation; this is
    // the additional WebGPU usage-scope restriction, not execution authority.
    static const char* textureAliasDiagnostic(Backend backend,
        bool sameParent,
        const DescriptorBindingDesc& lhs,
        const TextureSubresourceRange& a,
        const DescriptorBindingDesc& rhs,
        const TextureSubresourceRange& b) noexcept
    {
        const auto texture = [](DescriptorType type) {
            return type == DescriptorType::SampledTexture || type == DescriptorType::StorageTexture;
        };
        const auto writable = [](const DescriptorBindingDesc& binding) {
            return binding.type == DescriptorType::StorageTexture
                && binding.storageTextureAccess != StorageTextureAccess::ReadOnly;
        };
        if (backend != Backend::WebGPU || !sameParent || !texture(lhs.type) || !texture(rhs.type)
            || (!writable(lhs) && !writable(rhs)) || a.aspect != b.aspect)
            return nullptr;
        return core::overlapsUnsignedRanges(
                   a.baseMipLevel, a.mipLevelCount, b.baseMipLevel, b.mipLevelCount)
                && core::overlapsUnsignedRanges(
                    a.baseArrayLayer, a.arrayLayerCount, b.baseArrayLayer, b.arrayLayerCount)
            ? "RHI-WEBGPU-BINDLESS-PIPELINE-WRITABLE-TEXTURE-ALIAS"
            : nullptr;
    }

    static const DescriptorLayoutDesc* descriptorLayout(
        const BindlessShaderArtifact& artifact, std::uint32_t set) noexcept
    {
        if (set == artifact.set)
            return &artifact.physicalLayout;
        for (const auto& domain : artifact.additionalDomains)
            if (domain.set == set)
                return &domain.layout;
        return nullptr;
    }

    static bool scalarBufferDomains(
        const BindlessShaderArtifact& artifact, ShaderStage stages) noexcept
    {
        for (const auto& domain : artifact.additionalDomains)
            for (const auto& binding : domain.layout.bindings)
                if (binding.stages == ShaderStage::None
                    || (binding.stages & stages) != binding.stages || binding.arrayCount != 1u
                    || binding.dynamicOffset || binding.tableBacked
                    || (binding.type != DescriptorType::UniformBuffer
                        && binding.type != DescriptorType::ReadOnlyStorageBuffer
                        && binding.type != DescriptorType::StorageBuffer))
                    return false;
        return true;
    }

    // Value-only planning scope. Authentic package provenance, original stream,
    // initialized producer inputs and consumers are checked again by the cold
    // factory; this inspection alone cannot issue an execution or a seal.
    static bool producedGeometryDomains(const BindlessShaderArtifact& artifact) noexcept
    {
        if (artifact.pipelineInterfaceContractVersion != 1u
            || artifact.additionalDomains.size() != 1u || artifact.additionalDomains[0u].set != 2u)
            return false;
        const auto& aliases = artifact.additionalDomains[0u].layout.bindings;
        if (aliases.size() != 4u)
            return false;
        std::uint32_t seen = 0u;
        for (const auto& alias : aliases) {
            if (alias.binding >= 4u || (seen & (1u << alias.binding))
                || alias.stages != ShaderStage::Vertex || alias.arrayCount != 1u
                || alias.dynamicOffset || alias.tableBacked
                || alias.type
                    != (alias.binding == 3u ? DescriptorType::UniformBuffer
                                            : DescriptorType::ReadOnlyStorageBuffer))
                return false;
            seen |= 1u << alias.binding;
        }
        // The closed cull kernel also reads the original color HiZ image.
        // It is a load-only 2D texture, not a depth/comparison/sampling domain.
        const BindlessTableMapping* depth = nullptr;
        for (const auto& table : artifact.tables)
            if (table.tableId == 10u) {
                if (depth || table.capacity != 1u || table.operations != BindlessOperation::Load)
                    return false;
                depth = &table;
            }
        if (!depth)
            return false;
        std::uint32_t images = 0u;
        for (const auto& binding : artifact.physicalLayout.bindings) {
            if (binding.stages != ShaderStage::Compute || binding.dynamicOffset
                || (artifact.execution == BindlessExecution::Emulated
                    && (binding.tableBacked || binding.arrayCount != 1u)))
                return false;
            if (binding.type == DescriptorType::SampledTexture) {
                if (++images != 1u || binding.binding != depth->firstBinding
                    || binding.arrayCount != 1u || binding.depthTexture || binding.comparisonSampler
                    || binding.textureViewDimension != TextureViewDimension::Texture2D)
                    return false;
            } else if (binding.type != DescriptorType::UniformBuffer
                && binding.type != DescriptorType::ReadOnlyStorageBuffer
                && binding.type != DescriptorType::StorageBuffer)
                return false;
        }
        if (images != 1u)
            return false;
        return artifact.snapshotMetadata.contractVersion == 1u
            && artifact.snapshotMetadata.stages == ShaderStage::Compute
            && artifact.faultLatch.contractVersion == 1u
            && artifact.faultLatch.stages == ShaderStage::Compute
            && artifact.faultReportingContractVersion == 1u && !artifact.otherBindGroups
            && !artifact.indirectInstanceBaseContractVersion
            && !artifact.indirectInstanceConsumer.contractVersion
            && !artifact.zeroBaseIndirectProducerContractVersion
            && !artifact.zeroBaseIndirectConsumerContractVersion;
    }

    static bool originalEmulatedBufferDomains(const BindlessShaderArtifact& artifact,
        std::uint32_t version,
        bool preparation = false) noexcept
    {
        const bool ordinaryDepth =
            (preparation && version == 12u) || (!preparation && version == 14u);
        if ((preparation && version == 11u) || (!preparation && version == 13u) || ordinaryDepth) {
            bool image = false, depth = false, comparison = false;
            for (const auto& domain : artifact.additionalDomains)
                for (const auto& binding : domain.layout.bindings) {
                    if (binding.type == DescriptorType::UniformBuffer
                        || binding.type == DescriptorType::ReadOnlyStorageBuffer
                        || binding.type == DescriptorType::StorageBuffer) {
                        if (binding.arrayCount != 1u || binding.dynamicOffset || binding.tableBacked
                            || binding.stages == ShaderStage::None
                            || (binding.stages & (ShaderStage::Vertex | ShaderStage::Fragment))
                                != binding.stages
                            || (binding.type == DescriptorType::StorageBuffer
                                && (binding.stages & ShaderStage::Vertex) != ShaderStage::None))
                            return false;
                    } else {
                        const bool depthBinding = binding.depthTexture || binding.comparisonSampler;
                        if (depthBinding
                                ? !ordinaryDepth
                                    || OriginalBindlessOrdinaryDepthPipelineDeclaration::inspect(
                                        Backend::WebGPU, binding)
                                : !OriginalBindlessOrdinaryColorDomain::acceptsGraphicsBinding(
                                      binding))
                            return false;
                        image = image || binding.type == DescriptorType::SampledTexture;
                        depth = depth || binding.depthTexture;
                        comparison = comparison || binding.comparisonSampler;
                    }
                }
            return ordinaryDepth ? depth && comparison : image;
        }
        if (version == 1u)
            return scalarBufferDomains(artifact, ShaderStage::Compute);
        const bool graphics = version == 2u || version == 3u || (!preparation && version == 4u);
        if (!graphics
            || !scalarBufferDomains(artifact, ShaderStage::Vertex | ShaderStage::Fragment))
            return false;
        for (const auto& domain : artifact.additionalDomains)
            for (const auto& binding : domain.layout.bindings)
                if (binding.type == DescriptorType::StorageBuffer
                    && (binding.stages & ShaderStage::Vertex) != ShaderStage::None)
                    return false;
        return true;
    }

    // A standalone snapshot owns its indexed set only. Composed domains enter
    // via Root/2 and retain their Sources in the existing canonical journal.
    static const char* standaloneSnapshotDiagnostic(const BindlessShaderArtifact& artifact) noexcept
    {
        return artifact.additionalDomains.empty()
            ? nullptr
            : "RHI-BINDLESS-PIPELINE-DOMAINS-ORIGINAL-ROOT-REQUIRED";
    }

    struct Lowering {
        bool interfaceInspectionAvailable = false;
        bool validatorOnly = false;
        bool executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-PIPELINE-DOMAINS-EXECUTION-UNAVAILABLE";
    };

    static Lowering lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, true, false, "RHI-BINDLESS-PIPELINE-DOMAINS-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            // Ordinary scalar buffers/color images require the original Root2
            // custody and per-resource queried limits. Declaration inspection
            // alone does not authorize format, state or execution.
            return {true, false, true, nullptr};
        case Backend::Metal:
            // Original Root/2 scalar buffers in Compute and Graphics; exact
            // stage mappings and whole-pipeline slots are independently checked.
            // Ordinary images, arrays and dynamic offsets remain refused.
            return {true, false, true, nullptr};
        case Backend::WebGPU:
            // Original Compute1/Graphics2 with Root2 scalar buffers. The planner,
            // authentic native closure and callback lifecycle remain required.
            return {true, false, true, nullptr};
        case Backend::WebGL:
        default:
            return {};
        }
    }
};

} // namespace arp::rhi::semantics
