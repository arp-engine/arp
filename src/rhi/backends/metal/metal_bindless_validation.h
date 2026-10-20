#pragma once

#include "rhi/backends/metal/metal_native_features.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/semantics/bindless_completed_image_lowering.h"
#include "rhi/semantics/bindless_fragment_store_lowering.h"
#include "rhi/semantics/bindless_ordinary_color_domain.h"
#include "rhi/semantics/bindless_ordinary_depth_pipeline.h"

#include <algorithm>

namespace arp::rhi_metal {

// Consumes published capability facts only; never derives or enables Graphics.
inline bool metalBindlessGraphicsExecutionAvailable(const rhi::BindlessCapabilities& caps) noexcept
{
    return caps.contractVersion == rhi::kBindlessContractVersion && caps.deviceIdentity
        && caps.preparedGraphicsBindingContractVersion == 1u
        && caps.preparedGraphicsPassFaultScopeContractVersion == 1u
        && caps.execution == rhi::BindlessExecution::Native && caps.executablePayload
        && caps.limitsQueried && caps.faultReadbackContractVersion == 1u;
}

inline bool metalBindlessGraphicsDepthExecutionAvailable(
    const rhi::BindlessCapabilities& caps, const MetalRhiNativeFeatureProbe& probe) noexcept
{
    return metalBindlessGraphicsExecutionAvailable(caps)
        && caps.originalGraphicsExecutionContractVersion == 1u && caps.ordinaryDescriptorDomains[0u]
        && caps.ordinaryDescriptorDomains[1u] && probe.sampledDepthTexture
        && probe.argumentBuffersTier2 && probe.nativeSync && probe.maxTextureBindingsPerStage
        && probe.maxSamplerBindingsPerStage && caps.stageLimits[1u].sampledTextures
        && caps.stageLimits[1u].samplers;
}

// Compute and public prepared Graphics have independent opt-ins and operation
// scopes. Compute admits scalar ordinary buffer groups through original Root/2.
// Limits come from the native probe; argument-buffer support is no opt-in.
inline rhi::BindlessCapabilities metalBindlessComputeCapabilities(
    const MetalRhiNativeFeatureProbe& probe,
    bool optedIn,
    bool nativeRuntime,
    std::uint64_t deviceIdentity,
    bool imagesOptedIn = false,
    bool graphicsOptedIn = false,
    bool graphicsImagesOptedIn = false,
    bool originalGraphicsRuntime = false)
{
    rhi::BindlessCapabilities caps;
    caps.deviceIdentity = deviceIdentity;
    if ((!optedIn && !graphicsOptedIn) || !nativeRuntime || !deviceIdentity
        || !probe.argumentBuffersTier2 || !probe.nativeSync
        || !probe.maxStorageBufferBindingsPerStage || !probe.maxStorageBufferBindingSizeBytes)
        return caps;
    caps.execution = rhi::BindlessExecution::Native;
    caps.executablePayload = true;
    caps.limitsQueried = true;
    caps.faultReadbackContractVersion = 1u;
    // Each admitted group consumes at least one buffer argument. Bound the
    // logical set namespace conservatively by the native function slot ceiling;
    // exact sealed compiler mappings and whole-pipeline budgets are also checked.
    caps.maxBindGroups = probe.maxStorageBufferBindingsPerStage;
    caps.maxBindingsPerGroup = probe.maxStorageBufferBindingsPerStage;
    caps.maxUniformBufferBindingSize = probe.maxStorageBufferBindingSizeBytes;
    caps.maxStorageBufferBindingSize = probe.maxStorageBufferBindingSizeBytes;
    caps.workloadResourceLimits = {1u,
        probe.maxStorageBufferBindingSizeBytes,
        probe.maxStorageBufferBindingSizeBytes,
        probe.maxStorageBufferBindingSizeBytes,
        0u,
        0u,
        probe.maxTexture2DDimension,
        probe.maxTexture3DDimension,
        probe.maxTextureArrayLayers};
    caps.ordinaryDescriptorDomains[3u] = true;
    caps.ordinaryDescriptorDomains[4u] = true;
    caps.ordinaryDescriptorDomains[5u] = true;
    using Op = rhi::BindlessOperation;
    if (graphicsOptedIn) {
        caps.preparedGraphicsBindingContractVersion = 1u;
        caps.preparedGraphicsPassFaultScopeContractVersion = 1u;
        for (std::size_t stage = 0u; stage < 2u; ++stage) {
            caps.stageLimits[stage].uniformBuffers = probe.maxStorageBufferBindingsPerStage;
            caps.stageLimits[stage].storageBuffers = probe.maxStorageBufferBindingsPerStage;
            caps.support[stage][3u] = {Op::Load, true};
            caps.support[stage][4u] = {Op::Load | Op::Query, true};
        }
        caps.support[1u][5u] = {Op::Load | Op::Query | Op::Atomic, true};
        if (originalGraphicsRuntime
            && rhi::semantics::OriginalBindlessFragmentStoreLowering::lowering(rhi::Backend::Metal,
                   rhi::ShaderStage::Fragment,
                   rhi::DescriptorType::StorageBuffer)
                    .execution
                == rhi::BindlessExecution::Native)
            caps.support[1u][5u].operations = caps.support[1u][5u].operations | Op::Store;
        if (graphicsImagesOptedIn && originalGraphicsRuntime && probe.maxTextureBindingsPerStage
            && probe.maxSamplerBindingsPerStage) {
            // Ordinary scalar images use the same queried native namespaces in
            // both stages. Indexed image operations below remain Fragment-only.
            caps.ordinaryDescriptorDomains[0u] = true;
            caps.ordinaryDescriptorDomains[1u] = true;
            caps.stageLimits[0u].sampledTextures = probe.maxTextureBindingsPerStage;
            caps.stageLimits[0u].samplers = probe.maxSamplerBindingsPerStage;
            caps.combinedSampledStorageTextureLimits[0u] = probe.maxTextureBindingsPerStage;
            caps.stageLimits[1u].sampledTextures = probe.maxTextureBindingsPerStage;
            caps.stageLimits[1u].samplers = probe.maxSamplerBindingsPerStage;
            caps.combinedSampledStorageTextureLimits[1u] = probe.maxTextureBindingsPerStage;
            caps.support[1u][0u] = {rhi::OriginalBindlessCompletedImageDomain::sampledOperations(
                                        rhi::OriginalBindlessCompletedImageScope::SampledGraphics1),
                true};
            caps.support[1u][1u] = {Op::None, true};
        }
    }
    if (!optedIn)
        return caps;
    caps.preparedComputeBindingContractVersion = 1u;
    caps.originalNativeBufferComputeExecutionContractVersion = 7u;
    auto& limits = caps.stageLimits[2u];
    limits.uniformBuffers = probe.maxStorageBufferBindingsPerStage;
    limits.storageBuffers = probe.maxStorageBufferBindingsPerStage;
    caps.support[2u][3u] = {Op::Load, true};
    caps.support[2u][4u] = {Op::Load | Op::Query, true};
    caps.support[2u][5u] = {Op::Load | Op::Query | Op::Store | Op::Atomic, true};
    if (imagesOptedIn && probe.sampledDepthTexture && probe.maxTextureBindingsPerStage
        && probe.maxSamplerBindingsPerStage) {
        caps.originalNativeDepthRootPreparationContractVersion = 6u;
        caps.originalNativeDepthComputeExecutionContractVersion = 6u;
        limits.sampledTextures = probe.maxTextureBindingsPerStage;
        limits.samplers = probe.maxSamplerBindingsPerStage;
        caps.combinedSampledStorageTextureLimits[2u] = probe.maxTextureBindingsPerStage;
        caps.support[2u][0u] = {Op::Load | Op::Query | Op::SampleLevel, true};
        caps.support[2u][1u] = {Op::None, true};
    }
    if (imagesOptedIn && probe.r32FloatReadWriteTexture && probe.maxTextureBindingsPerStage
        && probe.maxSamplerBindingsPerStage) {
        caps.originalNativeColorComputeExecutionContractVersion = 8u;
        limits.sampledTextures = probe.maxTextureBindingsPerStage;
        limits.storageTextures = probe.maxTextureBindingsPerStage;
        limits.samplers = probe.maxSamplerBindingsPerStage;
        caps.combinedSampledStorageTextureLimits[2u] = probe.maxTextureBindingsPerStage;
        caps.support[2u][0u] = {Op::Load | Op::Query | Op::SampleLevel, true};
        caps.support[2u][1u] = {Op::None, true};
        caps.support[2u][2u] = {Op::Load | Op::Query | Op::Store, true};
        if (probe.rgba16FloatStorageTexture) {
            caps.originalStoragePreparationContractVersion = 7u;
            caps.originalStorageRootPreparationContractVersion = 8u;
            if (caps.originalNativeDepthRootPreparationContractVersion == 6u) {
                caps.originalMixedImageSnapshotPreparationContractVersion = 9u;
                caps.originalMixedImageRootPreparationContractVersion = 10u;
                caps.originalNativeMixedComputeExecutionContractVersion = 11u;
            }
            caps.originalNativeStorageComputeExecutionContractVersion = 9u;
        }
    }
    return caps;
}

// Buffer-domain preflight is independent from authorization. Sealed package
// loading already validates buffer ABI identities and table/layout coherence.
class MetalRhiBindlessDomainValidator final {
public:
    MetalRhiBindlessDomainValidator(const rhi::BindlessShaderArtifact& artifact,
        bool graphics,
        bool images = false,
        bool originalColor = false,
        bool sampledGraphics = false,
        bool storageRoot = false,
        bool originalDepth = false,
        bool mixedImages = false)
        : artifact_(artifact), graphics_(graphics), images_(graphics ? sampledGraphics : images),
          originalColor_((originalColor || sampledGraphics) && images_),
          storageRoot_(storageRoot && !graphics && images_),
          originalDepth_(originalDepth && !graphics && images_),
          mixedImages_(mixedImages && !graphics && images_)
    {}

    rhi::ValidationResult validate() const
    {
        using Stage = rhi::ShaderStage;
        const auto stages = artifact_.snapshotMetadata.stages;
        const bool validStages = graphics_ ? stages == Stage::Vertex || stages == Stage::Fragment
                || stages == (Stage::Vertex | Stage::Fragment)
                                           : stages == Stage::Compute;
        if (artifact_.contractVersion != rhi::kBindlessContractVersion
            || artifact_.execution != rhi::BindlessExecution::Native
            || (artifact_.set != 0u && artifact_.additionalDomains.empty()
                && (!graphics_ || artifact_.pipelineInterfaceContractVersion != 1u))
            || artifact_.otherBindGroups || artifact_.activePrefixContractVersion != 2u
            || artifact_.snapshotMetadata.contractVersion != 1u || !validStages
            || artifact_.faultReportingContractVersion != 1u
            || artifact_.faultLatch.contractVersion != 1u || artifact_.faultLatch.stages != stages
            || artifact_.indirectInstanceBaseContractVersion
            || artifact_.indirectInstanceConsumer.contractVersion
            || artifact_.zeroBaseIndirectProducerContractVersion)
            return {false,
                graphics_ ? "RHI-METAL-BINDLESS-GRAPHICS-ABI-UNSUPPORTED"
                          : "RHI-METAL-BINDLESS-COMPUTE-ABI-UNSUPPORTED"};
        if (!artifact_.additionalDomains.empty()) {
            if (artifact_.pipelineInterfaceContractVersion != 1u)
                return domainFailure();
            for (const auto& domain : artifact_.additionalDomains)
                for (const auto& binding : domain.layout.bindings)
                    if ((graphics_ ? binding.stages == Stage::None
                                    || (binding.stages & (Stage::Vertex | Stage::Fragment))
                                        != binding.stages
                                    || (binding.type == rhi::DescriptorType::StorageBuffer
                                        && binding.stages != Stage::Fragment)
                                   : binding.stages != Stage::Compute)
                        || binding.arrayCount != 1u || binding.dynamicOffset || binding.tableBacked
                        || (binding.type != rhi::DescriptorType::UniformBuffer
                            && binding.type != rhi::DescriptorType::ReadOnlyStorageBuffer
                            && binding.type != rhi::DescriptorType::StorageBuffer
                            && !(graphics_ && images_
                                && (rhi::semantics::OriginalBindlessOrdinaryColorDomain::
                                        acceptsGraphicsBinding(binding)
                                    || (binding.stages == Stage::Fragment
                                        && !rhi::semantics::
                                            OriginalBindlessOrdinaryDepthPipelineDeclaration::
                                                inspect(rhi::Backend::Metal, binding))))))
                        return domainFailure();
        }
        for (const auto& reserved : artifact_.reservedPerStage)
            if (reserved.sampledTextures || reserved.samplers || reserved.storageTextures
                || reserved.uniformBuffers || reserved.storageBuffers)
                return domainFailure();
        for (const auto& binding : artifact_.physicalLayout.bindings) {
            if (binding.stages == Stage::None || (binding.stages & stages) != binding.stages
                || binding.dynamicOffset
                || (binding.type != rhi::DescriptorType::UniformBuffer
                    && binding.type != rhi::DescriptorType::ReadOnlyStorageBuffer
                    && binding.type != rhi::DescriptorType::StorageBuffer
                    && !(images_
                        && (binding.type == rhi::DescriptorType::SampledTexture
                            || binding.type == rhi::DescriptorType::Sampler
                            || (!graphics_
                                && binding.type == rhi::DescriptorType::StorageTexture)))))
                return {false,
                    graphics_ ? "RHI-METAL-BINDLESS-GRAPHICS-RESOURCE-UNSUPPORTED"
                              : "RHI-METAL-BINDLESS-COMPUTE-RESOURCE-UNSUPPORTED"};
            if (images_) {
                if (graphics_
                    && !rhi::OriginalBindlessCompletedImageDomain::acceptsStages(
                        rhi::OriginalBindlessCompletedImageScope::SampledGraphics1,
                        binding.type,
                        binding.stages))
                    return domainFailure();
                if ((binding.type == rhi::DescriptorType::SampledTexture
                        || binding.type == rhi::DescriptorType::StorageTexture)
                    && (!binding.textureViewDimensionConstrained
                        || (binding.textureViewDimension != rhi::TextureViewDimension::Texture2D
                            && (!originalColor_
                                || binding.type != rhi::DescriptorType::SampledTexture
                                || binding.textureViewDimension
                                    != rhi::TextureViewDimension::Texture2DArray))
                        || (mixedImages_
                                ? (binding.depthTexture
                                      && binding.type != rhi::DescriptorType::SampledTexture)
                                : binding.depthTexture
                                    != (originalDepth_
                                        && binding.type == rhi::DescriptorType::SampledTexture))))
                    return domainFailure();
                if (binding.type == rhi::DescriptorType::StorageTexture
                    && (originalDepth_
                        || binding.storageTextureFormat
                            != ((storageRoot_ || mixedImages_) ? rhi::TextureFormat::Rgba16Float
                                                               : rhi::TextureFormat::R32Float)
                        || binding.storageTextureAccess
                            != ((storageRoot_ || mixedImages_)
                                    ? rhi::StorageTextureAccess::WriteOnly
                                    : rhi::StorageTextureAccess::ReadWrite)))
                    return domainFailure();
                if (binding.type == rhi::DescriptorType::Sampler && binding.comparisonSampler)
                    return domainFailure();
            }
            bool declared = binding.binding == artifact_.snapshotMetadata.binding
                || binding.binding == artifact_.faultLatch.binding;
            for (const auto& table : artifact_.tables)
                declared = declared || table.firstBinding == binding.binding;
            if (!declared)
                return domainFailure();
        }
        if (graphics_)
            for (const auto& table : artifact_.tables) {
                const auto binding = std::find_if(artifact_.physicalLayout.bindings.begin(),
                    artifact_.physicalLayout.bindings.end(),
                    [&](const auto& candidate) {
                        return candidate.binding == table.firstBinding;
                    });
                if (binding == artifact_.physicalLayout.bindings.end()
                    || (!images_ && table.samplerTableId))
                    return domainFailure();
                using Op = rhi::BindlessOperation;
                const auto allowed = images_ && binding->type == rhi::DescriptorType::SampledTexture
                    ? rhi::OriginalBindlessCompletedImageDomain::sampledOperations(
                          rhi::OriginalBindlessCompletedImageScope::SampledGraphics1)
                    : binding->type == rhi::DescriptorType::UniformBuffer ? Op::Load
                    : binding->type == rhi::DescriptorType::ReadOnlyStorageBuffer
                    ? Op::Load | Op::Query
                    : binding->type == rhi::DescriptorType::StorageBuffer
                        && binding->stages == Stage::Fragment
                    ? Op::Load | Op::Query | Op::Store | Op::Atomic
                    : Op::None;
                if ((binding->type != rhi::DescriptorType::Sampler && table.operations == Op::None)
                    || (static_cast<std::uint32_t>(table.operations)
                        & ~static_cast<std::uint32_t>(allowed)))
                    return {false, "RHI-METAL-BINDLESS-GRAPHICS-OPERATION-UNSUPPORTED"};
            }
        if (images_)
            for (const auto& table : artifact_.tables) {
                const auto binding = std::find_if(artifact_.physicalLayout.bindings.begin(),
                    artifact_.physicalLayout.bindings.end(),
                    [&](const auto& candidate) {
                        return candidate.binding == table.firstBinding;
                    });
                if (binding == artifact_.physicalLayout.bindings.end())
                    return domainFailure();
                using Op = rhi::BindlessOperation;
                const auto type = binding->type;
                const auto allowed = type == rhi::DescriptorType::SampledTexture
                    ? rhi::OriginalBindlessCompletedImageDomain::sampledOperations(graphics_
                              ? rhi::OriginalBindlessCompletedImageScope::SampledGraphics1
                              : rhi::OriginalBindlessCompletedImageScope::ComputeColor8)
                    : type == rhi::DescriptorType::StorageTexture
                    ? ((storageRoot_ || mixedImages_) ? Op::Query | Op::Store
                                                      : Op::Load | Op::Query | Op::Store)
                    : type == rhi::DescriptorType::UniformBuffer         ? Op::Load
                    : type == rhi::DescriptorType::ReadOnlyStorageBuffer ? Op::Load | Op::Query
                    : type == rhi::DescriptorType::StorageBuffer
                    ? Op::Load | Op::Query | Op::Store | Op::Atomic
                    : Op::None;
                if ((type != rhi::DescriptorType::Sampler && table.operations == Op::None)
                    || (static_cast<std::uint32_t>(table.operations)
                        & ~static_cast<std::uint32_t>(allowed)))
                    return {false, "RHI-METAL-BINDLESS-COMPUTE-OPERATION-UNSUPPORTED"};
                if (type != rhi::DescriptorType::SampledTexture && table.samplerTableId)
                    return domainFailure();
                if (static_cast<std::uint32_t>(table.operations)
                    & static_cast<std::uint32_t>(Op::SampleLevel | Op::SampleGrad)) {
                    const auto sampler = std::find_if(artifact_.tables.begin(),
                        artifact_.tables.end(),
                        [&](const auto& candidate) {
                            return candidate.tableId == table.samplerTableId;
                        });
                    if (sampler == artifact_.tables.end())
                        return domainFailure();
                    const auto samplerBinding =
                        std::find_if(artifact_.physicalLayout.bindings.begin(),
                            artifact_.physicalLayout.bindings.end(),
                            [&](const auto& candidate) {
                                return candidate.binding == sampler->firstBinding;
                            });
                    if (samplerBinding == artifact_.physicalLayout.bindings.end()
                        || samplerBinding->type != rhi::DescriptorType::Sampler)
                        return domainFailure();
                }
            }
        return {true, {}};
    }

private:
    rhi::ValidationResult domainFailure() const
    {
        return {false,
            graphics_ ? "RHI-METAL-BINDLESS-GRAPHICS-DOMAIN-UNSUPPORTED"
                      : "RHI-METAL-BINDLESS-COMPUTE-DOMAIN-UNSUPPORTED"};
    }

    const rhi::BindlessShaderArtifact& artifact_;
    bool graphics_;
    bool images_;
    bool originalColor_;
    bool storageRoot_;
    bool originalDepth_;
    bool mixedImages_;
};

inline rhi::ValidationResult validateMetalBindlessComputeArtifact(
    const rhi::BindlessShaderArtifact& artifact,
    bool images = false,
    bool originalColor = false,
    bool storageRoot = false,
    bool originalDepth = false,
    bool mixedImages = false)
{
    return MetalRhiBindlessDomainValidator(
        artifact, false, images, originalColor, false, storageRoot, originalDepth, mixedImages)
        .validate();
}

// Uniform/storage arguments share Metal's function buffer slot namespace.
// Counting all array elements is conservative for Tier2 array-pointer lowering.
inline rhi::ValidationResult validateMetalBindlessBufferSlotBudget(
    const rhi::BindlessShaderArtifact& artifact, std::uint32_t slots)
{
    std::array<std::uint64_t, rhi::kBindlessShaderStages> totals{};
    const auto inspect = [&](const rhi::DescriptorLayoutDesc& layout) {
        for (const auto& binding : layout.bindings) {
            if (binding.type != rhi::DescriptorType::UniformBuffer
                && binding.type != rhi::DescriptorType::ReadOnlyStorageBuffer
                && binding.type != rhi::DescriptorType::StorageBuffer)
                continue;
            for (std::size_t stage = 0u; stage < totals.size(); ++stage)
                if (static_cast<std::uint32_t>(binding.stages) & (1u << stage)) {
                    if (binding.arrayCount > slots || totals[stage] > slots - binding.arrayCount)
                        return false;
                    totals[stage] += binding.arrayCount;
                }
        }
        return true;
    };
    if (!slots || !inspect(artifact.physicalLayout))
        return {false, "RHI-METAL-BINDLESS-BUFFER-SLOT-BUDGET"};
    for (const auto& domain : artifact.additionalDomains)
        if (!inspect(domain.layout))
            return {false, "RHI-METAL-BINDLESS-BUFFER-SLOT-BUDGET"};
    return {true, {}};
}

inline rhi::ValidationResult validateMetalBindlessGraphicsArtifact(
    const rhi::BindlessShaderArtifact& artifact, bool sampledImages = false)
{
    return MetalRhiBindlessDomainValidator(artifact, true, false, false, sampledImages).validate();
}

// Pipeline declarations have no native Source/closure or execution authority.
// The independent image opt-in is consumed by artifact admission above; the
// original device probe must also support the actual depth substrate.
inline rhi::ValidationResult validateMetalBindlessOrdinaryDepthPipelinePrerequisites(
    const rhi::BindlessShaderArtifact& artifact, const MetalRhiNativeFeatureProbe& probe)
{
    for (const auto& domain : artifact.additionalDomains)
        for (const auto& binding : domain.layout.bindings)
            if ((binding.type == rhi::DescriptorType::SampledTexture && binding.depthTexture)
                || (binding.type == rhi::DescriptorType::Sampler && binding.comparisonSampler))
                if (!probe.sampledDepthTexture)
                    return {false, "RHI-METAL-BINDLESS-ORDINARY-DEPTH-PIPELINE-CAPABILITY"};
    return {true, {}};
}

class MetalRhiBindlessComputeImageContract final {
public:
    static rhi::ValidationResult validateTexture(rhi::DescriptorType type,
        rhi::TextureFormat format,
        rhi::TextureViewDimension dimension,
        rhi::TextureAspect aspect,
        std::uint32_t sampleCount,
        std::uint32_t layers)
    {
        if (!rhi::BindlessScalarImageResourceDomain::accepts(
                type, format, dimension, aspect, sampleCount, layers))
            return {false, "RHI-METAL-BINDLESS-COMPUTE-IMAGE-VIEW-UNSUPPORTED"};
        return {true, {}};
    }

    static rhi::ValidationResult validateSampler(bool comparison, bool argumentCompatible)
    {
        return !comparison && argumentCompatible
            ? rhi::ValidationResult{true, {}}
            : rhi::ValidationResult{false, "RHI-METAL-BINDLESS-COMPUTE-IMAGE-SAMPLER-UNSUPPORTED"};
    }
};

inline rhi::ValidationResult validateMetalBindlessGraphicsPackage(
    const rhi::BindlessShaderPackage& package, bool sampledImages = false)
{
    const auto artifact = validateMetalBindlessGraphicsArtifact(package.artifact(), sampledImages);
    if (!artifact.success)
        return artifact;
    for (const auto& module : package.modules()) {
        const auto& entries = module.reflection.reflection.entryPoints;
        if (entries.size() != 1u
            || (entries.front().stage != shader::ShaderStage::Vertex
                && entries.front().stage != shader::ShaderStage::Fragment))
            return {false, "RHI-METAL-BINDLESS-GRAPHICS-STAGE-UNSUPPORTED"};
    }
    return {true, {}};
}

} // namespace arp::rhi_metal
