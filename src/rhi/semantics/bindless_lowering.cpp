#include "core/stable_hash.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_commands.h"
#include "rhi/semantics/bindless_original_execution_lowering.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"
#include "rhi/semantics/bindless_pipeline_interface.h"

#include <limits>

namespace arp::rhi {

bool bindlessPlansMatch(const BindlessLoweringPlan& a, const BindlessLoweringPlan& b) noexcept
{
    return a.success && b.success && a.identity == b.identity
        && a.contractVersion == b.contractVersion && a.execution == b.execution
        && a.bindingModel == b.bindingModel && a.executable == b.executable
        && a.originalPreparationContractVersion == b.originalPreparationContractVersion
        && a.originalExecutionContractVersion == b.originalExecutionContractVersion
        && a.deviceIdentity == b.deviceIdentity
        && a.shaderPackageIdentity == b.shaderPackageIdentity && a.snapshotBytes == b.snapshotBytes
        && a.descriptorCount == b.descriptorCount && a.budget.maxSnapshots == b.budget.maxSnapshots
        && a.budget.maxUpdatesPerSnapshot == b.budget.maxUpdatesPerSnapshot
        && a.budget.maxResidentBytes == b.budget.maxResidentBytes
        && a.budget.maxSwitchCases == b.budget.maxSwitchCases;
}

ValidationResult validateBindlessFaultStatus(BindlessFaultValidationStatus status) noexcept
{
    switch (status) {
    case BindlessFaultValidationStatus::Clean:
        return {true, {}};
    case BindlessFaultValidationStatus::Faulted:
        return {false, "RHI-BINDLESS-GPU-INDEX-FAULT"};
    case BindlessFaultValidationStatus::ReadbackFailed:
        return {false, "RHI-BINDLESS-GPU-FAULT-READBACK-FAILED"};
    case BindlessFaultValidationStatus::Abandoned:
        return {false, "RHI-BINDLESS-GPU-VALIDATION-ABANDONED"};
    case BindlessFaultValidationStatus::Pending:
        return {false, "RHI-BINDLESS-GPU-COMPLETION-PENDING"};
    case BindlessFaultValidationStatus::NotExecuted:
        return {false, "RHI-BINDLESS-GPU-EXECUTION-NOT-PROVIDED"};
    case BindlessFaultValidationStatus::Unavailable:
        return {false, "RHI-BINDLESS-GPU-FAULT-REPORT-UNAVAILABLE"};
    }
    return {false, "RHI-BINDLESS-GPU-VALIDATION-INVALID"};
}

BindlessFaultReport mergeBindlessFaultReports(
    BindlessFaultReport consumer, BindlessFaultReport producer) noexcept
{
    const auto bits = consumer.faultBits | producer.faultBits;
    if (!consumer.snapshotGeneration || consumer.snapshotGeneration != producer.snapshotGeneration)
        return {consumer.snapshotGeneration, BindlessFaultValidationStatus::Unavailable, bits};
    if (bits || consumer.status == BindlessFaultValidationStatus::Faulted
        || producer.status == BindlessFaultValidationStatus::Faulted)
        return {consumer.snapshotGeneration, BindlessFaultValidationStatus::Faulted, bits};
    return {consumer.snapshotGeneration,
        consumer.status == BindlessFaultValidationStatus::Clean ? producer.status : consumer.status,
        bits};
}

ValidationResult BindlessFaultReport::validate() const noexcept
{
    if (!snapshotGeneration)
        return {false, "RHI-BINDLESS-GPU-FAULT-REPORT-GENERATION-INVALID"};
    if ((status == BindlessFaultValidationStatus::Clean && faultBits != 0u)
        || (status == BindlessFaultValidationStatus::Faulted && faultBits == 0u)
        || ((status != BindlessFaultValidationStatus::Clean
                && status != BindlessFaultValidationStatus::Faulted)
            && faultBits != 0u))
        return {false, "RHI-BINDLESS-GPU-FAULT-REPORT-INCONSISTENT"};
    return validateBindlessFaultStatus(status);
}

ValidationResult validateBindlessRenderPass(
    const RenderPipelineDesc& pipeline, const RenderPassDesc& pass)
{
    if (!pass.subpasses.empty() || !pass.subpassDependencies.empty() || pass.layeredRendering
        || pipeline.subpassIndex || pipeline.renderPassCompatibility.enabled()
        || pipeline.multisample.sampleCount != 1)
        return {false, "RHI-BINDLESS-RENDER-PASS-ABI-UNAVAILABLE"};
    if (effectiveColorTargetCount(pipeline) != pass.colorAttachments.size()
        || effectiveDepthStencilFormat(pipeline)
            != (pass.hasDepthStencilAttachment ? pass.depthStencilAttachment.format
                                               : TextureFormat::Unknown))
        return {false, "RHI-BINDLESS-RENDER-PASS-FORMAT-MISMATCH"};
    for (std::size_t i = 0; i < pass.colorAttachments.size(); ++i) {
        const auto& attachment = pass.colorAttachments[i];
        if (!attachment.target.isValid()
            || attachment.format != effectiveColorTargetAt(pipeline, i).format)
            return {false, "RHI-BINDLESS-RENDER-PASS-FORMAT-MISMATCH"};
        if (attachment.hasResolveTarget)
            return {false, "RHI-BINDLESS-RENDER-PASS-ABI-UNAVAILABLE"};
    }
    if (pass.hasDepthStencilAttachment && !pass.depthStencilAttachment.target.isValid())
        return {false, "RHI-BINDLESS-RENDER-PASS-FORMAT-MISMATCH"};
    if (pass.colorAttachments.empty() && !pass.hasDepthStencilAttachment)
        return {false, "RHI-BINDLESS-RENDER-PASS-FORMAT-MISMATCH"};
    return {true, {}};
}

std::size_t bindlessResourceClass(DescriptorType type) noexcept
{
    switch (type) {
    case DescriptorType::SampledTexture:
        return 0;
    case DescriptorType::Sampler:
        return 1;
    case DescriptorType::StorageTexture:
        return 2;
    case DescriptorType::UniformBuffer:
        return 3;
    case DescriptorType::ReadOnlyStorageBuffer:
        return 4;
    case DescriptorType::StorageBuffer:
        return 5;
    default:
        return kBindlessResourceClasses;
    }
}

namespace semantics {
namespace {
constexpr std::uint32_t stageBits[] = {
    static_cast<std::uint32_t>(ShaderStage::Vertex),
    static_cast<std::uint32_t>(ShaderStage::Fragment),
    static_cast<std::uint32_t>(ShaderStage::Compute),
};
constexpr std::uint32_t knownStages = stageBits[0] | stageBits[1] | stageBits[2];

bool sameShape(const DescriptorBindingDesc& a, const DescriptorBindingDesc& b)
{
    return a.type == b.type && a.stages == b.stages
        && a.textureViewDimensionConstrained == b.textureViewDimensionConstrained
        && a.textureViewDimension == b.textureViewDimension && a.depthTexture == b.depthTexture
        && a.storageTextureFormat == b.storageTextureFormat
        && a.storageTextureAccess == b.storageTextureAccess
        && a.sampledTextureFilterable == b.sampledTextureFilterable
        && a.comparisonSampler == b.comparisonSampler && !a.dynamicOffset && !b.dynamicOffset;
}

const DescriptorBindingDesc* findBinding(const DescriptorLayoutDesc& layout, std::uint32_t slot)
{
    for (const auto& binding : layout.bindings)
        if (binding.binding == slot)
            return &binding;
    return nullptr;
}

std::uint32_t limitFor(const BindlessStageLimits& limits, std::size_t type)
{
    switch (type) {
    case 0:
        return limits.sampledTextures;
    case 1:
        return limits.samplers;
    case 2:
        return limits.storageTextures;
    case 3:
        return limits.uniformBuffers;
    default:
        return limits.storageBuffers;
    }
}

std::uint32_t operationMask(DescriptorType type)
{
    using O = BindlessOperation;
    switch (type) {
    case DescriptorType::SampledTexture:
        return static_cast<std::uint32_t>(
            O::Load | O::Query | O::SampleLevel | O::SampleGrad | O::SampleCompare);
    case DescriptorType::StorageTexture:
        return static_cast<std::uint32_t>(O::Load | O::Query | O::Store);
    case DescriptorType::UniformBuffer:
        return static_cast<std::uint32_t>(O::Load);
    case DescriptorType::ReadOnlyStorageBuffer:
        return static_cast<std::uint32_t>(O::Load | O::Query);
    case DescriptorType::StorageBuffer:
        return static_cast<std::uint32_t>(O::Load | O::Query | O::Store | O::Atomic);
    default:
        return 0;
    }
}

std::uint64_t planIdentity(const BindlessCapabilities& caps,
    const BindlessRequirements& request,
    const BindlessShaderArtifact& artifact)
{
    core::StableHash64 hash;
    const auto append = [&](auto value) {
        hash.appendU64(static_cast<std::uint64_t>(value));
    };
    append(request.contractVersion);
    append(request.deviceIdentity);
    append(request.shaderPackageIdentity);
    append(caps.execution);
    append(caps.faultReadbackContractVersion);
    append(caps.indirectInstanceConsumerContractVersion);
    append(caps.zeroBaseIndirectProducerContractVersion);
    append(caps.indirectPacketStorageContractVersion);
    append(request.optIn);
    append(request.allowEmulated);
    if (!request.originalPreparation.empty()) {
        hash.appendString("OriginalBindlessPreparation/1");
        append(request.originalPreparation.contractVersion);
        append(request.originalPreparation.prototypeOptIn);
        append(caps.originalPreparationContractVersion);
        append(caps.originalGraphicsPreparationContractVersion);
        append(caps.originalSampledGraphicsPreparationContractVersion);
        if (caps.originalOrdinaryColorGraphicsPreparationContractVersion)
            append(caps.originalOrdinaryColorGraphicsPreparationContractVersion);
        if (request.originalPreparation.contractVersion == 12u)
            append(caps.originalOrdinaryDepthGraphicsPreparationContractVersion);
        append(caps.originalDepthSnapshotPreparationContractVersion);
        append(caps.originalDepthRootPreparationContractVersion);
        append(caps.originalNativeDepthRootPreparationContractVersion);
        append(caps.originalStoragePreparationContractVersion);
        append(caps.originalStorageRootPreparationContractVersion);
        if (request.originalPreparation.contractVersion >= 9u) {
            append(caps.originalMixedImageSnapshotPreparationContractVersion);
            append(caps.originalMixedImageRootPreparationContractVersion);
        }
        append(caps.originalNativeDepthComputeExecutionContractVersion);
    }
    if (!request.originalExecution.empty()) {
        hash.appendString("OriginalBindlessExecution/1");
        append(request.originalExecution.contractVersion);
        append(request.originalExecution.prototypeOptIn);
        append(caps.originalExecutionContractVersion);
        append(caps.originalFragmentGraphicsExecutionContractVersion);
        append(caps.originalSampledFragmentGraphicsExecutionContractVersion);
        append(caps.originalArrayFragmentGraphicsExecutionContractVersion);
        if (caps.originalOrdinaryColorGraphicsExecutionContractVersion)
            append(caps.originalOrdinaryColorGraphicsExecutionContractVersion);
        if (caps.originalOrdinaryDepthGraphicsExecutionContractVersion)
            append(caps.originalOrdinaryDepthGraphicsExecutionContractVersion);
        append(caps.originalDepthComputeExecutionContractVersion);
        append(caps.originalNativeBufferComputeExecutionContractVersion);
        append(caps.originalNativeColorComputeExecutionContractVersion);
        append(caps.originalNativeStorageComputeExecutionContractVersion);
        append(caps.originalEmulatedStorageComputeExecutionContractVersion);
        if (request.originalExecution.contractVersion == 11u
            || request.originalExecution.contractVersion == 12u) {
            append(caps.originalNativeMixedComputeExecutionContractVersion);
            append(caps.originalEmulatedMixedComputeExecutionContractVersion);
        }
        if (request.originalExecution.contractVersion == 15u
            || request.originalExecution.contractVersion == 16u) {
            append(caps.originalNativeProducedGeometryExecutionContractVersion);
            append(caps.originalEmulatedProducedGeometryExecutionContractVersion);
        }
    }
    append(request.budget.maxSnapshots);
    append(request.budget.maxUpdatesPerSnapshot);
    append(request.budget.maxResidentBytes);
    append(request.budget.maxSwitchCases);
    append(artifact.execution);
    append(artifact.set);
    append(artifact.snapshotBytes);
    append(artifact.switchCases);
    append(artifact.otherBindGroups);
    append(artifact.pipelineInterfaceContractVersion);
    append(artifact.activePrefixContractVersion);
    append(artifact.faultReportingContractVersion);
    append(artifact.indirectInstanceBaseContractVersion);
    append(artifact.indirectInstanceConsumer.contractVersion);
    append(artifact.indirectInstanceConsumer.binding);
    append(artifact.indirectInstanceConsumer.recordIndex);
    append(artifact.indirectInstanceConsumer.instanceCapacity);
    append(artifact.zeroBaseIndirectProducerContractVersion);
    append(artifact.zeroBaseIndirectConsumerContractVersion);
    append(artifact.snapshotMetadata.contractVersion);
    append(artifact.snapshotMetadata.binding);
    append(artifact.snapshotMetadata.wordCount);
    append(artifact.snapshotMetadata.stages);
    append(artifact.faultLatch.contractVersion);
    append(artifact.faultLatch.binding);
    append(artifact.faultLatch.stages);
    for (const auto& reserved : artifact.reservedPerStage) {
        append(reserved.sampledTextures);
        append(reserved.samplers);
        append(reserved.storageTextures);
        append(reserved.uniformBuffers);
        append(reserved.storageBuffers);
    }
    const auto appendLayout = [&](const DescriptorLayoutDesc& layout) {
        append(layout.bindings.size());
        for (const auto& binding : layout.bindings) {
            append(binding.binding);
            append(binding.type);
            append(binding.stages);
            append(binding.arrayCount);
            append(binding.tableBacked);
            append(binding.dynamicOffset);
            append(binding.textureViewDimensionConstrained);
            append(binding.textureViewDimension);
            append(binding.depthTexture);
            append(binding.storageTextureFormat);
            append(binding.storageTextureAccess);
            append(binding.sampledTextureFilterable);
            append(binding.comparisonSampler);
        }
    };
    appendLayout(artifact.physicalLayout);
    append(artifact.additionalDomains.size());
    for (const auto& domain : artifact.additionalDomains) {
        append(domain.set);
        appendLayout(domain.layout);
    }
    append(artifact.tables.size());
    for (const auto& mapping : artifact.tables) {
        append(mapping.tableId);
        append(mapping.firstBinding);
        append(mapping.capacity);
        append(mapping.operations);
        append(mapping.nonUniform);
        append(mapping.samplerTableId);
        append(mapping.metadataWordOffset);
        append(mapping.bufferLayout.identity);
        append(mapping.bufferLayout.elementStrideBytes);
        append(mapping.bufferLayout.elementAlignmentBytes);
        append(mapping.bufferLayout.minBindingSizeBytes);
    }
    append(request.tables.size());
    for (const auto& table : request.tables) {
        append(table.tableId);
        append(table.resourceCount);
    }
    return hash.value();
}
} // namespace

BindlessLoweringPlan planBindless(const BindlessCapabilities& caps,
    const BindlessRequirements& request,
    const BindlessShaderArtifact& artifact)
{
    BindlessLoweringPlan result;
    auto fail = [&](const char* id) {
        result.diagnosticId = id;
        return result;
    };
    if (caps.contractVersion != kBindlessContractVersion
        || request.contractVersion != kBindlessContractVersion
        || artifact.contractVersion != kBindlessContractVersion)
        return fail("RHI-BINDLESS-CONTRACT-VERSION");
    if (!request.optIn)
        return fail("RHI-BINDLESS-OPT-IN-REQUIRED");
    if (artifact.pipelineInterfaceContractVersion > 1u
        || (!artifact.pipelineInterfaceContractVersion && !artifact.additionalDomains.empty()))
        return fail("RHI-BINDLESS-PIPELINE-INTERFACE-VERSION");
    if (caps.execution != BindlessExecution::Validator)
        for (const auto& domain : artifact.additionalDomains)
            for (const auto& binding : domain.layout.bindings) {
                const auto kind = bindlessResourceClass(binding.type);
                const bool coldOrdinaryImage = request.originalPreparation.contractVersion == 11u
                    && request.originalPreparation.prototypeOptIn
                    && request.originalExecution.empty()
                    && OriginalBindlessPreparationLowering::capabilityAvailable(caps, 11u)
                    && OriginalBindlessOrdinaryColorDomain::acceptsGraphicsBinding(binding);
                const bool executableOrdinaryImage =
                    request.originalExecution.contractVersion == 13u
                    && request.originalExecution.prototypeOptIn
                    && request.originalPreparation.empty()
                    && OriginalBindlessExecutionLowering::capabilityAvailable(caps, 13u)
                    && OriginalBindlessOrdinaryColorDomain::acceptsGraphicsBinding(binding);
                const bool coldOrdinaryDepthImage =
                    request.originalPreparation.contractVersion == 12u
                    && request.originalPreparation.prototypeOptIn
                    && request.originalExecution.empty()
                    && OriginalBindlessPreparationLowering::capabilityAvailable(caps, 12u)
                    && (OriginalBindlessOrdinaryColorDomain::acceptsGraphicsBinding(binding)
                        || !OriginalBindlessOrdinaryDepthPipelineDeclaration::inspect(
                            Backend::WebGPU, binding));
                const bool executableOrdinaryDepthImage =
                    request.originalExecution.contractVersion == 14u
                    && request.originalExecution.prototypeOptIn
                    && request.originalPreparation.empty()
                    && OriginalBindlessExecutionLowering::capabilityAvailable(caps, 14u)
                    && (OriginalBindlessOrdinaryColorDomain::acceptsGraphicsBinding(binding)
                        || !OriginalBindlessOrdinaryDepthPipelineDeclaration::inspect(
                            Backend::WebGPU, binding));
                if (kind >= kBindlessResourceClasses
                    || (!caps.ordinaryDescriptorDomains[kind] && !coldOrdinaryImage
                        && !executableOrdinaryImage && !coldOrdinaryDepthImage
                        && !executableOrdinaryDepthImage))
                    return fail("RHI-BINDLESS-PIPELINE-DOMAINS-EXECUTION-UNAVAILABLE");
            }
    const bool preparationOnly = !request.originalPreparation.empty();
    const bool ordinaryColorPreparation =
        preparationOnly && request.originalPreparation.contractVersion == 11u;
    const bool ordinaryDepthPreparation =
        preparationOnly && request.originalPreparation.contractVersion == 12u;
    const bool originalExecution = !request.originalExecution.empty();
    const bool producedNativeExecution =
        originalExecution && request.originalExecution.contractVersion == 15u;
    const bool producedEmulatedExecution =
        originalExecution && request.originalExecution.contractVersion == 16u;
    const bool producedExecution = producedNativeExecution || producedEmulatedExecution;
    const bool ordinaryDepthExecution =
        originalExecution && request.originalExecution.contractVersion == 14u;
    const bool ordinaryColorExecution =
        originalExecution && request.originalExecution.contractVersion == 13u;
    const bool nativeDepthPreparation =
        preparationOnly && request.originalPreparation.contractVersion == 6u;
    const bool mixedNativeExecution =
        originalExecution && request.originalExecution.contractVersion == 11u;
    const bool mixedEmulatedExecution =
        originalExecution && request.originalExecution.contractVersion == 12u;
    const bool mixedImageDomain = mixedNativeExecution || mixedEmulatedExecution
        || (preparationOnly
            && (request.originalPreparation.contractVersion == 9u
                || request.originalPreparation.contractVersion == 10u));
    const bool mixedImagePreparation = preparationOnly && mixedImageDomain;
    const bool storagePreparation = preparationOnly
        && (request.originalPreparation.contractVersion == 7u
            || request.originalPreparation.contractVersion == 8u || mixedImagePreparation);
    const bool storageExecution =
        originalExecution && request.originalExecution.contractVersion == 9u;
    const bool emulatedStorageExecution =
        originalExecution && request.originalExecution.contractVersion == 10u;
    const bool storageDomain =
        storagePreparation || storageExecution || emulatedStorageExecution || mixedImageDomain;
    const bool nativeDepthExecution =
        originalExecution && request.originalExecution.contractVersion == 6u;
    const bool nativeDepth = nativeDepthPreparation || nativeDepthExecution;
    const bool nativeBufferExecution =
        originalExecution && request.originalExecution.contractVersion == 7u;
    const bool nativeColorExecution =
        originalExecution && request.originalExecution.contractVersion == 8u;
    const bool nativeOriginal = nativeDepth || nativeBufferExecution || nativeColorExecution
        || storageExecution || mixedNativeExecution || producedNativeExecution
        || (storagePreparation && caps.execution == BindlessExecution::Native);
    const bool depthPreparation = preparationOnly
        && (request.originalPreparation.contractVersion == 4u
            || request.originalPreparation.contractVersion == 5u || nativeDepthPreparation);
    const bool depthExecution = originalExecution
        && (request.originalExecution.contractVersion == 5u || nativeDepthExecution);
    const bool depthDomain = depthPreparation || depthExecution;
    const bool arrayExecution =
        originalExecution && request.originalExecution.contractVersion == 4u;
    const bool sampledExecution =
        originalExecution && (request.originalExecution.contractVersion == 3u || arrayExecution);
    const bool sampledPreparation =
        (preparationOnly && request.originalPreparation.contractVersion == 3u) || depthDomain
        || sampledExecution || nativeColorExecution || storageDomain;
    const bool graphicsPreparation =
        (preparationOnly
            && (ordinaryColorPreparation || ordinaryDepthPreparation
                || request.originalPreparation.contractVersion == 2u
                || request.originalPreparation.contractVersion == 3u
                || (depthPreparation && artifact.snapshotMetadata.stages == ShaderStage::Fragment)))
        || (originalExecution
            && (request.originalExecution.contractVersion == 2u || sampledExecution
                || ordinaryColorExecution || ordinaryDepthExecution));
    if (preparationOnly && originalExecution)
        return fail("RHI-BINDLESS-ORIGINAL-PROFILE-CONFLICT");
    if (preparationOnly) {
        if (request.originalPreparation.contractVersion != 1u
            && request.originalPreparation.contractVersion != 2u
            && request.originalPreparation.contractVersion != 3u
            && request.originalPreparation.contractVersion != 4u
            && request.originalPreparation.contractVersion != 5u && !nativeDepthPreparation
            && !storageDomain && !ordinaryColorPreparation && !ordinaryDepthPreparation)
            return fail("RHI-BINDLESS-ORIGINAL-PREPARATION-VERSION");
        if (!request.originalPreparation.prototypeOptIn)
            return fail("RHI-BINDLESS-ORIGINAL-PREPARATION-OPT-IN");
        if (!OriginalBindlessPreparationLowering::capabilityAvailable(
                caps, request.originalPreparation.contractVersion))
            return fail("RHI-BINDLESS-ORIGINAL-PREPARATION-UNAVAILABLE");
    }
    if (originalExecution) {
        if (request.originalExecution.contractVersion != 1u
            && request.originalExecution.contractVersion != 2u
            && request.originalExecution.contractVersion != 3u
            && request.originalExecution.contractVersion != 4u
            && request.originalExecution.contractVersion != 5u && !nativeDepthExecution
            && !nativeBufferExecution && !nativeColorExecution && !storageExecution
            && !emulatedStorageExecution && !mixedNativeExecution && !mixedEmulatedExecution
            && !ordinaryColorExecution && !ordinaryDepthExecution && !producedExecution)
            return fail("RHI-BINDLESS-ORIGINAL-EXECUTION-VERSION");
        if (!request.originalExecution.prototypeOptIn)
            return fail("RHI-BINDLESS-ORIGINAL-EXECUTION-OPT-IN");
        if (!OriginalBindlessExecutionLowering::capabilityAvailable(
                caps, request.originalExecution.contractVersion))
            return fail("RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE");
    }
    if (preparationOnly || originalExecution) {
        const auto* scopeDiagnostic = preparationOnly ? "RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE"
                                                      : "RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE";
        if (producedExecution) {
            if (!BindlessPipelineInterfaceContract::producedGeometryDomains(artifact)
                || artifact.execution
                    != (producedNativeExecution ? BindlessExecution::Native
                                                : BindlessExecution::Emulated))
                return fail(scopeDiagnostic);
        } else {
            if (storageDomain && !artifact.additionalDomains.empty())
                return fail(scopeDiagnostic);
            if ((nativeBufferExecution || nativeColorExecution)
                && !BindlessPipelineInterfaceContract::scalarBufferDomains(
                    artifact, ShaderStage::Compute))
                return fail(scopeDiagnostic);
            if (!nativeOriginal && (ordinaryDepthPreparation || !artifact.additionalDomains.empty())
                && !BindlessPipelineInterfaceContract::originalEmulatedBufferDomains(artifact,
                    preparationOnly ? request.originalPreparation.contractVersion
                                    : request.originalExecution.contractVersion,
                    preparationOnly))
                return fail(scopeDiagnostic);
            if (artifact.execution
                    != (nativeOriginal ? BindlessExecution::Native : BindlessExecution::Emulated)
                || artifact.otherBindGroups
                || (depthPreparation && artifact.snapshotMetadata.stages != ShaderStage::Compute
                    && artifact.snapshotMetadata.stages != ShaderStage::Fragment)
                || artifact.snapshotMetadata.contractVersion != 1u
                || artifact.snapshotMetadata.stages
                    != (graphicsPreparation ? ShaderStage::Fragment : ShaderStage::Compute)
                || artifact.faultLatch.contractVersion != 1u
                || artifact.faultLatch.stages
                    != (graphicsPreparation ? ShaderStage::Fragment : ShaderStage::Compute)
                || artifact.faultReportingContractVersion != 1u
                || artifact.indirectInstanceBaseContractVersion
                || artifact.indirectInstanceConsumer.contractVersion
                || artifact.zeroBaseIndirectProducerContractVersion
                || artifact.zeroBaseIndirectConsumerContractVersion)
                return fail(scopeDiagnostic);
            for (const auto& binding : artifact.physicalLayout.bindings)
                if (binding.stages
                        != (graphicsPreparation ? ShaderStage::Fragment : ShaderStage::Compute)
                    || binding.dynamicOffset || (!nativeOriginal && binding.tableBacked)
                    || (!nativeOriginal
                        && (originalExecution || graphicsPreparation || depthPreparation)
                        && binding.arrayCount != 1u)
                    || (binding.type != DescriptorType::UniformBuffer
                        && binding.type != DescriptorType::ReadOnlyStorageBuffer
                        && binding.type != DescriptorType::StorageBuffer
                        && !(sampledPreparation
                            && (binding.type == DescriptorType::SampledTexture
                                || (!storageDomain && binding.type == DescriptorType::Sampler)))
                        && !(storageDomain && binding.type == DescriptorType::StorageTexture))
                    || (storageDomain && binding.type == DescriptorType::StorageTexture
                        && (binding.storageTextureFormat != TextureFormat::Rgba16Float
                            || binding.storageTextureAccess != StorageTextureAccess::WriteOnly
                            || binding.depthTexture
                            || binding.textureViewDimension != TextureViewDimension::Texture2D))
                    || (sampledPreparation && binding.type == DescriptorType::SampledTexture
                        && ((!mixedImageDomain && binding.depthTexture != depthDomain)
                            || !binding.sampledTextureFilterable
                            || (binding.textureViewDimension != TextureViewDimension::Texture2D
                                && (depthDomain || storageDomain
                                    || binding.textureViewDimension
                                        != TextureViewDimension::Texture2DArray))))
                    || (sampledPreparation && binding.type == DescriptorType::Sampler
                        && (binding.comparisonSampler
                            || ((request.originalPreparation.contractVersion == 5u
                                    || nativeDepthPreparation || depthExecution)
                                && binding.sampledTextureFilterable))))
                    return fail(scopeDiagnostic);
            if (storageDomain) {
                bool storage = false, depth = false, color = false;
                for (const auto& table : request.tables) {
                    const bool image = table.binding.type == DescriptorType::StorageTexture;
                    if (!image && table.binding.type != DescriptorType::SampledTexture)
                        continue;
                    const auto operations = static_cast<std::uint32_t>(table.operations);
                    const auto allowed = image ? BindlessOperation::Store | BindlessOperation::Query
                                               : BindlessOperation::Load | BindlessOperation::Query;
                    if ((operations & ~static_cast<std::uint32_t>(allowed))
                        || (image
                            && !(
                                operations & static_cast<std::uint32_t>(BindlessOperation::Store))))
                        return fail(scopeDiagnostic);
                    storage = storage || image;
                    depth = depth || (!image && table.binding.depthTexture);
                    color = color || (!image && !table.binding.depthTexture);
                }
                if (!storage || (mixedImageDomain && (!depth || !color)))
                    return fail(scopeDiagnostic);
            }
            if (depthDomain) {
                bool depth = false;
                for (const auto& table : request.tables)
                    if (table.binding.type == DescriptorType::SampledTexture) {
                        depth = true;
                        const auto allowed = BindlessOperation::Load | BindlessOperation::Query
                            | BindlessOperation::SampleLevel;
                        if ((static_cast<std::uint32_t>(table.operations)
                                & ~static_cast<std::uint32_t>(allowed))
                            != 0u)
                            return fail(scopeDiagnostic);
                    }
                if (!depth)
                    return fail(scopeDiagnostic);
            }
            if (nativeColorExecution) {
                bool sampled = false;
                for (const auto& table : request.tables) {
                    if (table.binding.type != DescriptorType::SampledTexture)
                        continue;
                    sampled = true;
                    const auto allowed = BindlessOperation::Load | BindlessOperation::Query
                        | BindlessOperation::SampleLevel;
                    if ((static_cast<std::uint32_t>(table.operations)
                            & ~static_cast<std::uint32_t>(allowed))
                        != 0u)
                        return fail(scopeDiagnostic);
                }
                if (!sampled)
                    return fail(scopeDiagnostic);
            }
            // The first sampled execution domain consumes authentic canonical
            // whole 2D uploads. Cold array preparation cannot grant array execution.
            if (sampledExecution && !arrayExecution)
                for (const auto& binding : artifact.physicalLayout.bindings)
                    if (binding.type == DescriptorType::SampledTexture
                        && binding.textureViewDimension != TextureViewDimension::Texture2D)
                        return fail(scopeDiagnostic);
            for (const auto& reserved : artifact.reservedPerStage)
                if (reserved.sampledTextures || reserved.samplers || reserved.storageTextures
                    || reserved.uniformBuffers || reserved.storageBuffers)
                    return fail(scopeDiagnostic);
        }
    }
    // SampledDepth/1 publishes compiler/package ABI only. Neither a generic
    // executable payload nor an older color Root profile proves control2 depth
    // content, state, completion or retirement. Keep this gate independent.
    for (const auto& binding : artifact.physicalLayout.bindings)
        if (binding.depthTexture && !depthDomain && !mixedImageDomain)
            return fail("RHI-BINDLESS-SAMPLED-DEPTH-EXECUTION-UNAVAILABLE");
    const auto execution = nativeOriginal        ? BindlessExecution::Native
        : (preparationOnly || originalExecution) ? BindlessExecution::Emulated
                                                 : caps.execution;
    if (execution == BindlessExecution::DiagnosticFailure || !caps.limitsQueried
        || (!preparationOnly && !originalExecution && execution != BindlessExecution::Validator
            && !caps.executablePayload))
        return fail("RHI-BINDLESS-EXECUTION-UNAVAILABLE");
    if (execution != BindlessExecution::Validator && execution != BindlessExecution::Native
        && execution != BindlessExecution::Emulated)
        return fail("RHI-BINDLESS-EXECUTION-UNAVAILABLE");
    if (artifact.faultReportingContractVersion > 1u)
        return fail("RHI-BINDLESS-FAULT-REPORTING-VERSION");
    if (artifact.indirectInstanceBaseContractVersion > 1u)
        return fail("RHI-BINDLESS-INDIRECT-INSTANCE-BASE-VERSION");
    if (artifact.indirectInstanceBaseContractVersion
        && artifact.faultReportingContractVersion != 1u)
        return fail("RHI-BINDLESS-INDIRECT-INSTANCE-BASE-FAULT-LATCH-MISSING");
    if (artifact.zeroBaseIndirectProducerContractVersion > 1u)
        return fail("RHI-BINDLESS-ZERO-BASE-INDIRECT-PRODUCER-VERSION");
    if (artifact.zeroBaseIndirectConsumerContractVersion > 1u)
        return fail("RHI-BINDLESS-ZERO-BASE-INDIRECT-CONSUMER-VERSION");
    if (artifact.zeroBaseIndirectConsumerContractVersion
        && (artifact.indirectInstanceBaseContractVersion != 1u
            || artifact.indirectInstanceConsumer.contractVersion != 1u
            || artifact.zeroBaseIndirectProducerContractVersion))
        return fail("RHI-BINDLESS-ZERO-BASE-INDIRECT-CONSUMER-ABI");
    // Compiler provenance is not native execution evidence. This new closed
    // specialization has no published GPU lowering payload on any backend.
    if (artifact.zeroBaseIndirectConsumerContractVersion
        && execution != BindlessExecution::Validator)
        return fail("RHI-BINDLESS-ZERO-BASE-INDIRECT-CONSUMER-EXECUTION-UNAVAILABLE");
    if (artifact.zeroBaseIndirectProducerContractVersion
        && (artifact.indirectInstanceBaseContractVersion != 1u
            || artifact.indirectInstanceConsumer.contractVersion))
        return fail("RHI-BINDLESS-ZERO-BASE-INDIRECT-PRODUCER-ABI");
    if (artifact.zeroBaseIndirectProducerContractVersion
        && caps.zeroBaseIndirectProducerContractVersion
            != artifact.zeroBaseIndirectProducerContractVersion)
        return fail("RHI-BINDLESS-ZERO-BASE-INDIRECT-PRODUCER-UNAVAILABLE");
    if (execution != BindlessExecution::Validator && artifact.faultReportingContractVersion != 1u)
        return fail("RHI-BINDLESS-FAULT-REPORTING-REQUIRED");
    if (!preparationOnly && execution != BindlessExecution::Validator
        && caps.faultReadbackContractVersion != 1u)
        return fail("RHI-BINDLESS-FAULT-READBACK-UNAVAILABLE");
    if (artifact.execution == BindlessExecution::Emulated && !request.allowEmulated)
        return fail("RHI-BINDLESS-EMULATED-NOT-ALLOWED");
    if (!request.deviceIdentity || request.deviceIdentity != caps.deviceIdentity)
        return fail("RHI-BINDLESS-DEVICE-MISMATCH");
    if (!request.shaderPackageIdentity
        || request.shaderPackageIdentity != artifact.shaderPackageIdentity
        || (artifact.execution != execution && execution != BindlessExecution::Validator)
        || (artifact.execution != BindlessExecution::Validator
            && artifact.execution != BindlessExecution::Native
            && artifact.execution != BindlessExecution::Emulated))
        return fail("RHI-BINDLESS-SHADER-MISMATCH");
    const auto& budget = request.budget;
    if (!budget.maxSnapshots || !budget.maxUpdatesPerSnapshot || !budget.maxResidentBytes
        || !artifact.snapshotBytes
        || artifact.snapshotBytes > budget.maxResidentBytes / budget.maxSnapshots)
        return fail("RHI-BINDLESS-SNAPSHOT-BUDGET");
    if (artifact.switchCases > budget.maxSwitchCases
        || (artifact.execution == BindlessExecution::Emulated && !artifact.switchCases))
        return fail("RHI-BINDLESS-SWITCH-BUDGET");
    if (request.tables.empty() || request.tables.size() != artifact.tables.size()
        || artifact.physicalLayout.name.empty() || artifact.physicalLayout.bindings.empty()
        || !caps.maxBindGroups || artifact.set >= caps.maxBindGroups || !caps.maxBindingsPerGroup
        || artifact.otherBindGroups >= caps.maxBindGroups
        || artifact.physicalLayout.bindings.size() > caps.maxBindingsPerGroup)
        return fail("RHI-BINDLESS-LAYOUT-INVALID");

    BindlessPipelineInterfaceSummary pipelineInterface;
    if (artifact.pipelineInterfaceContractVersion) {
        if (const auto* id = inspectBindlessPipelineInterface(caps, artifact, pipelineInterface))
            return fail(id);
    }

    // Count the WHOLE physical domain, not just indexed tables. RO/RW storage
    // bindings share a limit. uint64 counters make cumulative overflow explicit.
    std::array<std::array<std::uint64_t, 5>, kBindlessShaderStages> totals{};
    std::uint64_t descriptorCount = 0;
    for (std::size_t stage = 0; stage < kBindlessShaderStages; ++stage) {
        const auto& reserved = artifact.pipelineInterfaceContractVersion
            ? pipelineInterface.additionalPerStage[stage]
            : artifact.reservedPerStage[stage];
        totals[stage] = {reserved.sampledTextures,
            reserved.samplers,
            reserved.storageTextures,
            reserved.uniformBuffers,
            reserved.storageBuffers};
        for (std::size_t type = 0; type < 5; ++type)
            if (totals[stage][type] > limitFor(caps.stageLimits[stage], type))
                return fail("RHI-BINDLESS-STAGE-LIMIT");
    }
    for (std::size_t i = 0; i < artifact.physicalLayout.bindings.size(); ++i) {
        const auto& binding = artifact.physicalLayout.bindings[i];
        const auto type = bindlessResourceClass(binding.type);
        const auto stages = static_cast<std::uint32_t>(binding.stages);
        if (type == kBindlessResourceClasses || !binding.arrayCount || !stages
            || (stages & ~knownStages))
            return fail("RHI-BINDLESS-LAYOUT-INVALID");
        // WebGPU bounds the binding identifier itself, not just entry count.
        if (artifact.execution == BindlessExecution::Emulated
            && binding.binding >= caps.maxBindingsPerGroup)
            return fail("RHI-BINDLESS-LAYOUT-INVALID");
        for (std::size_t j = 0; j < i; ++j)
            if (artifact.physicalLayout.bindings[j].binding == binding.binding)
                return fail("RHI-BINDLESS-PHYSICAL-BINDING-COLLISION");
        descriptorCount += binding.arrayCount;
        const auto bucket = type > 4 ? 4 : type;
        for (std::size_t stage = 0; stage < kBindlessShaderStages; ++stage) {
            if (!(stages & stageBits[stage]))
                continue;
            totals[stage][bucket] += binding.arrayCount;
            if (totals[stage][bucket] > limitFor(caps.stageLimits[stage], type))
                return fail("RHI-BINDLESS-STAGE-LIMIT");
        }
    }
    for (std::size_t stage = 0u; stage < kBindlessShaderStages; ++stage) {
        const auto combined = caps.combinedSampledStorageTextureLimits[stage];
        if (combined && totals[stage][0u] + totals[stage][2u] > *combined)
            return fail("RHI-BINDLESS-STAGE-LIMIT");
    }
    if (descriptorCount > budget.maxUpdatesPerSnapshot)
        return fail("RHI-BINDLESS-UPDATE-BUDGET");

    if (artifact.activePrefixContractVersion == 2u) {
        const auto& metadata = artifact.snapshotMetadata;
        const auto* binding = findBinding(artifact.physicalLayout, metadata.binding);
        if (metadata.contractVersion != 1 || !binding || binding->arrayCount != 1
            || binding->type != DescriptorType::ReadOnlyStorageBuffer || binding->dynamicOffset
            || binding->stages != metadata.stages)
            return fail("RHI-BINDLESS-METADATA-ABI");
        std::uint64_t words = 4;
        ShaderStage stages = ShaderStage::None;
        for (const auto& mapping : artifact.tables) {
            const auto* first = findBinding(artifact.physicalLayout, mapping.firstBinding);
            const auto count =
                artifact.execution == BindlessExecution::Emulated ? mapping.capacity : 1u;
            if (!first || mapping.metadataWordOffset != words
                || (metadata.binding >= mapping.firstBinding
                    && metadata.binding - mapping.firstBinding < count))
                return fail("RHI-BINDLESS-METADATA-ABI");
            stages = stages | first->stages;
            words += 4ull + mapping.capacity;
        }
        if (words > UINT32_MAX / 4u || words != metadata.wordCount || stages != metadata.stages
            || words * 4u > caps.maxStorageBufferBindingSize || words * 4u > artifact.snapshotBytes)
            return fail("RHI-BINDLESS-METADATA-ABI");
    } else if (artifact.snapshotMetadata.contractVersion || artifact.snapshotMetadata.wordCount
        || artifact.snapshotMetadata.binding
        || artifact.snapshotMetadata.stages != ShaderStage::None) {
        return fail("RHI-BINDLESS-METADATA-ABI");
    }
    if (artifact.faultLatch.contractVersion) {
        const auto& latch = artifact.faultLatch;
        const auto* binding = findBinding(artifact.physicalLayout, latch.binding);
        if (latch.contractVersion != 1u || !binding || binding->arrayCount != 1u
            || binding->type != DescriptorType::StorageBuffer || binding->dynamicOffset
            || binding->stages != latch.stages || latch.stages == ShaderStage::None
            || (static_cast<std::uint32_t>(latch.stages) & ~7u)
            || latch.binding == artifact.snapshotMetadata.binding
            || caps.maxStorageBufferBindingSize < sizeof(std::uint32_t))
            return fail("RHI-BINDLESS-FAULT-LATCH-ABI");
        for (const auto& mapping : artifact.tables) {
            const auto count =
                artifact.execution == BindlessExecution::Emulated ? mapping.capacity : 1u;
            if (latch.binding >= mapping.firstBinding
                && latch.binding - mapping.firstBinding < count)
                return fail("RHI-BINDLESS-FAULT-LATCH-ABI");
        }
        const auto metadataBytes =
            std::uint64_t(artifact.snapshotMetadata.wordCount) * sizeof(std::uint32_t);
        if (artifact.snapshotBytes < metadataBytes + sizeof(std::uint32_t))
            return fail("RHI-BINDLESS-FAULT-LATCH-BUDGET");
        if (caps.faultReadbackContractVersion == 1u
            && artifact.snapshotBytes < metadataBytes + 2u * sizeof(std::uint32_t))
            return fail("RHI-BINDLESS-FAULT-READBACK-BUDGET");
    } else if (artifact.faultLatch.binding || artifact.faultLatch.stages != ShaderStage::None) {
        return fail("RHI-BINDLESS-FAULT-LATCH-ABI");
    }
    if (artifact.faultReportingContractVersion && artifact.faultLatch.contractVersion != 1u)
        return fail("RHI-BINDLESS-FAULT-LATCH-REQUIRED");

    const auto& consumer = artifact.indirectInstanceConsumer;
    if (consumer.contractVersion) {
        if (caps.indirectInstanceConsumerContractVersion != consumer.contractVersion)
            return fail("RHI-BINDLESS-INDIRECT-INSTANCE-CONSUMER-UNAVAILABLE");
        const auto* binding = findBinding(artifact.physicalLayout, consumer.binding);
        if (consumer.contractVersion != 1u || artifact.indirectInstanceBaseContractVersion != 1u
            || !consumer.instanceCapacity || !binding || binding->arrayCount != 1u
            || binding->type != DescriptorType::ReadOnlyStorageBuffer || binding->dynamicOffset
            || binding->stages != ShaderStage::Vertex
            || (std::uint64_t(consumer.recordIndex) + 1u) * 16u > caps.maxStorageBufferBindingSize
            || (artifact.snapshotMetadata.contractVersion
                && consumer.binding == artifact.snapshotMetadata.binding)
            || (artifact.faultLatch.contractVersion
                && consumer.binding == artifact.faultLatch.binding)
            || (artifact.faultLatch.stages & ShaderStage::Fragment) == ShaderStage::None)
            return fail("RHI-BINDLESS-INDIRECT-INSTANCE-CONSUMER-ABI");
        for (const auto& mapping : artifact.tables) {
            const auto count =
                artifact.execution == BindlessExecution::Emulated ? mapping.capacity : 1u;
            if (consumer.binding >= mapping.firstBinding
                && consumer.binding - mapping.firstBinding < count)
                return fail("RHI-BINDLESS-INDIRECT-INSTANCE-CONSUMER-ABI");
        }
    } else if (consumer.binding || consumer.recordIndex || consumer.instanceCapacity) {
        return fail("RHI-BINDLESS-INDIRECT-INSTANCE-CONSUMER-ABI");
    }

    std::uint64_t updateCount = 0;
    ShaderStage indexedStages = ShaderStage::None;
    for (std::size_t i = 0; i < request.tables.size(); ++i) {
        const auto& table = request.tables[i];
        indexedStages = indexedStages | table.binding.stages;
        const auto type = bindlessResourceClass(table.binding.type);
        const auto stages = static_cast<std::uint32_t>(table.binding.stages);
        const auto operations = static_cast<std::uint32_t>(table.operations);
        if (type == kBindlessResourceClasses || !table.tableId || !table.resourceCount
            || (!operations && table.binding.type != DescriptorType::Sampler) || !stages
            || (stages & ~knownStages))
            return fail("RHI-BINDLESS-TABLE-INVALID");
        if (operations & ~operationMask(table.binding.type))
            return fail("RHI-BINDLESS-OPERATION-UNSUPPORTED");
        if (table.binding.type == DescriptorType::StorageTexture) {
            const auto access = table.binding.storageTextureAccess;
            if ((operations & static_cast<std::uint32_t>(BindlessOperation::Load))
                && access == StorageTextureAccess::WriteOnly)
                return fail("RHI-BINDLESS-STORAGE-ACCESS-MISMATCH");
            if ((operations & static_cast<std::uint32_t>(BindlessOperation::Store))
                && access == StorageTextureAccess::ReadOnly)
                return fail("RHI-BINDLESS-STORAGE-ACCESS-MISMATCH");
        }
        // WebGPU vertex shaders have no writable storage fault sink. Until an
        // upstream bounded-index proof is part of the sealed workload, an
        // indexed vertex operation cannot satisfy validation-frame readback.
        if (execution == BindlessExecution::Emulated
            && artifact.execution == BindlessExecution::Emulated
            && (stages & static_cast<std::uint32_t>(ShaderStage::Vertex)) && operations)
            return fail("RHI-BINDLESS-VERTEX-FAULT-PROVENANCE-REQUIRED");
        if ((operations & static_cast<std::uint32_t>(BindlessOperation::SampleGrad))
            && stages != static_cast<std::uint32_t>(ShaderStage::Fragment))
            return fail("RHI-BINDLESS-OPERATION-UNSUPPORTED");
        for (std::size_t j = 0; j < i; ++j)
            if (request.tables[j].tableId == table.tableId)
                return fail("RHI-BINDLESS-TABLE-ID-COLLISION");
        for (std::size_t stage = 0; stage < kBindlessShaderStages; ++stage) {
            if (!(stages & stageBits[stage]))
                continue;
            const auto& support = caps.support[stage][type];
            if ((operations & ~static_cast<std::uint32_t>(support.operations))
                || (table.nonUniform && !support.nonUniform))
                return fail("RHI-BINDLESS-OPERATION-UNSUPPORTED");
        }
        const BindlessTableMapping* mapping = nullptr;
        for (const auto& candidate : artifact.tables) {
            if (candidate.tableId != table.tableId)
                continue;
            if (mapping)
                return fail("RHI-BINDLESS-TABLE-ID-COLLISION");
            mapping = &candidate;
        }
        if (!mapping || !mapping->capacity || table.resourceCount > mapping->capacity)
            return fail("RHI-BINDLESS-TABLE-CAPACITY");
        if (artifact.activePrefixContractVersion > 2u
            || (table.resourceCount < mapping->capacity && !artifact.activePrefixContractVersion))
            return fail("RHI-BINDLESS-ACTIVE-PREFIX-ABI");
        if (mapping->operations != table.operations || mapping->nonUniform != table.nonUniform)
            return fail("RHI-BINDLESS-SHADER-OPERATIONS-MISMATCH");
        const bool buffer = table.binding.type == DescriptorType::UniformBuffer
            || table.binding.type == DescriptorType::ReadOnlyStorageBuffer
            || table.binding.type == DescriptorType::StorageBuffer;
        const auto& shape = table.bufferLayout;
        const auto& compiledShape = mapping->bufferLayout;
        if (shape.identity != compiledShape.identity
            || shape.elementStrideBytes != compiledShape.elementStrideBytes
            || shape.elementAlignmentBytes != compiledShape.elementAlignmentBytes
            || shape.minBindingSizeBytes != compiledShape.minBindingSizeBytes)
            return fail("RHI-BINDLESS-BUFFER-LAYOUT-MISMATCH");
        if (buffer) {
            if (!shape.identity || !shape.elementStrideBytes || !shape.elementAlignmentBytes
                || shape.elementStrideBytes % shape.elementAlignmentBytes
                || !shape.minBindingSizeBytes
                || shape.minBindingSizeBytes % shape.elementStrideBytes)
                return fail("RHI-BINDLESS-BUFFER-LAYOUT-INVALID");
            const auto maxSize = table.binding.type == DescriptorType::UniformBuffer
                ? caps.maxUniformBufferBindingSize
                : caps.maxStorageBufferBindingSize;
            if (!maxSize || shape.minBindingSizeBytes > maxSize)
                return fail("RHI-BINDLESS-BUFFER-SIZE-LIMIT");
        } else if (shape.identity || shape.elementStrideBytes || shape.elementAlignmentBytes
            || shape.minBindingSizeBytes) {
            return fail("RHI-BINDLESS-BUFFER-LAYOUT-INVALID");
        }
        updateCount += mapping->capacity;
        if (updateCount > budget.maxUpdatesPerSnapshot)
            return fail("RHI-BINDLESS-UPDATE-BUDGET");
        const bool expanded = artifact.execution == BindlessExecution::Emulated;
        const auto bindingCount = expanded ? mapping->capacity : 1u;
        if (bindingCount - 1u > std::numeric_limits<std::uint32_t>::max() - mapping->firstBinding)
            return fail("RHI-BINDLESS-LAYOUT-INVALID");
        for (const auto& other : artifact.tables) {
            if (&other == mapping)
                continue;
            const std::uint64_t otherEnd =
                std::uint64_t(other.firstBinding) + (expanded ? other.capacity : 1u);
            const std::uint64_t end = std::uint64_t(mapping->firstBinding) + bindingCount;
            if (mapping->firstBinding < otherEnd && other.firstBinding < end)
                return fail("RHI-BINDLESS-PHYSICAL-BINDING-COLLISION");
        }
        for (std::uint32_t element = 0; element < bindingCount; ++element) {
            const auto* binding =
                findBinding(artifact.physicalLayout, mapping->firstBinding + element);
            if (!binding || !sameShape(table.binding, *binding)
                || binding->arrayCount != (expanded ? 1u : mapping->capacity))
                return fail("RHI-BINDLESS-MAPPING-MISMATCH");
        }
    }
    if (artifact.faultLatch.contractVersion && artifact.faultLatch.stages != indexedStages)
        return fail("RHI-BINDLESS-FAULT-LATCH-STAGES");
    result.success = true;
    result.execution = execution;
    result.bindingModel = artifact.execution == BindlessExecution::Emulated
        ? BindlessBindingModel::ScalarExpansion
        : BindlessBindingModel::DescriptorArrays;
    result.executable = !preparationOnly && execution != BindlessExecution::Validator;
    result.originalPreparationContractVersion =
        preparationOnly ? request.originalPreparation.contractVersion : 0u;
    result.originalExecutionContractVersion =
        originalExecution ? request.originalExecution.contractVersion : 0u;
    result.deviceIdentity = caps.deviceIdentity;
    result.shaderPackageIdentity = artifact.shaderPackageIdentity;
    result.identity = planIdentity(caps, request, artifact);
    result.budget = budget;
    result.snapshotBytes = artifact.snapshotBytes;
    result.descriptorCount = descriptorCount;
    result.diagnosticId = "RHI-BINDLESS-PLAN-VALID";
    result.telemetryLabel = preparationOnly        ? "OriginalPreparation"
        : originalExecution                        ? "OriginalExecution"
        : execution == BindlessExecution::Native   ? "Native"
        : execution == BindlessExecution::Emulated ? "Emulated"
                                                   : "NullValidator";
    return result;
}

BindlessSelection selectBindlessVariant(BindlessPolicy policy,
    const BindlessCapabilities& caps,
    const BindlessRequirements& request,
    const BindlessShaderArtifact* artifacts,
    std::size_t artifactCount)
{
    BindlessSelection result;
    if (policy == BindlessPolicy::Disabled) {
        result.path = BindlessSelectionPath::Traditional;
        result.diagnosticId = "RHI-BINDLESS-DISABLED";
        return result;
    }
    if ((policy != BindlessPolicy::Auto && policy != BindlessPolicy::Required)
        || request.shaderPackageIdentity || (!artifacts && artifactCount)) {
        result.diagnosticId = "RHI-BINDLESS-SELECTION-INVALID";
        return result;
    }
    // Selection occurs at startup/reload, outside recording. The request copy
    // uses the core memory resource and is never retained by a backend.
    auto resolved = request;
    std::uint64_t bestCapacity = std::numeric_limits<std::uint64_t>::max();
    std::uint32_t bestSwitchCases = std::numeric_limits<std::uint32_t>::max();
    for (std::size_t i = 0; i < artifactCount; ++i) {
        resolved.shaderPackageIdentity = artifacts[i].shaderPackageIdentity;
        auto plan = planBindless(caps, resolved, artifacts[i]);
        if (!plan.success || !plan.executable) {
            if (result.path != BindlessSelectionPath::Indexed)
                result.diagnosticId = !plan.success ? plan.diagnosticId
                    : plan.originalPreparationContractVersion
                    ? "RHI-BINDLESS-ORIGINAL-PREPARATION-NOT-EXECUTABLE"
                    : "RHI-BINDLESS-VALIDATOR-NOT-EXECUTABLE";
            continue;
        }
        std::uint64_t capacity = 0;
        for (const auto& table : artifacts[i].tables)
            capacity += table.capacity;
        if (result.path == BindlessSelectionPath::Indexed
            && (capacity > bestCapacity
                || (capacity == bestCapacity
                    && (artifacts[i].switchCases > bestSwitchCases
                        || (artifacts[i].switchCases == bestSwitchCases
                            && plan.shaderPackageIdentity >= result.plan.shaderPackageIdentity)))))
            continue;
        bestCapacity = capacity;
        bestSwitchCases = artifacts[i].switchCases;
        result.path = BindlessSelectionPath::Indexed;
        result.artifactIndex = i;
        result.plan = plan;
        result.diagnosticId = plan.diagnosticId;
    }
    if (result.path != BindlessSelectionPath::Indexed && policy == BindlessPolicy::Auto)
        result.path = BindlessSelectionPath::Traditional;
    return result;
}

} // namespace semantics

namespace {
const char* inspectPipelineInterface(const BindlessCapabilities* caps,
    const BindlessShaderArtifact& artifact,
    BindlessPipelineInterfaceSummary& output,
    BindlessPipelineInterfaceDemand& demand) noexcept
{
    output = {};
    demand = {};
    if ((caps && caps->contractVersion != kBindlessContractVersion)
        || artifact.contractVersion != kBindlessContractVersion)
        return "RHI-BINDLESS-CONTRACT-VERSION";
    if (artifact.pipelineInterfaceContractVersion != 1u)
        return "RHI-BINDLESS-PIPELINE-INTERFACE-VERSION";
    if (caps && (!caps->limitsQueried || !caps->maxBindGroups || !caps->maxBindingsPerGroup))
        return "RHI-BINDLESS-PIPELINE-LIMITS-UNAVAILABLE";
    if ((caps && artifact.additionalDomains.size() >= caps->maxBindGroups)
        || artifact.otherBindGroups)
        return "RHI-BINDLESS-PIPELINE-DOMAINS-MISMATCH";
    if (artifact.additionalDomains.size() >= std::numeric_limits<std::uint32_t>::max())
        return "RHI-BINDLESS-PIPELINE-DEMAND-OVERFLOW";

    BindlessPipelineInterfaceSummary result;
    BindlessPipelineInterfaceDemand required;
    std::array<std::array<std::uint64_t, 5>, kBindlessShaderStages> totals{};
    const auto inspectLayout =
        [&](std::uint32_t set, const DescriptorLayoutDesc& layout, bool additional) -> const char* {
        if ((caps
                && (set >= caps->maxBindGroups
                    || layout.bindings.size() > caps->maxBindingsPerGroup))
            || layout.name.empty() || layout.bindings.empty())
            return "RHI-BINDLESS-PIPELINE-DOMAIN-INVALID";
        if (set == std::numeric_limits<std::uint32_t>::max()
            || layout.bindings.size() > std::numeric_limits<std::uint32_t>::max())
            return "RHI-BINDLESS-PIPELINE-DEMAND-OVERFLOW";
        if (required.bindGroupSlots < set + 1u)
            required.bindGroupSlots = set + 1u;
        if (required.bindingsPerGroup < layout.bindings.size())
            required.bindingsPerGroup = static_cast<std::uint32_t>(layout.bindings.size());
        for (std::size_t i = 0; i < layout.bindings.size(); ++i) {
            const auto& binding = layout.bindings[i];
            const auto resourceClass = bindlessResourceClass(binding.type);
            const auto stages = static_cast<std::uint32_t>(binding.stages);
            if (resourceClass == kBindlessResourceClasses || !binding.arrayCount || !stages
                || (stages & ~semantics::knownStages)
                || (artifact.execution == BindlessExecution::Emulated
                    && ((caps && binding.binding >= caps->maxBindingsPerGroup)
                        || (additional && binding.arrayCount != 1u))))
                return "RHI-BINDLESS-PIPELINE-BINDING-INVALID";
            if (artifact.execution == BindlessExecution::Emulated) {
                if (binding.binding == std::numeric_limits<std::uint32_t>::max())
                    return "RHI-BINDLESS-PIPELINE-DEMAND-OVERFLOW";
                if (required.bindingsPerGroup < binding.binding + 1u)
                    required.bindingsPerGroup = binding.binding + 1u;
            }
            for (std::size_t previous = 0; previous < i; ++previous)
                if (layout.bindings[previous].binding == binding.binding)
                    return "RHI-BINDLESS-PHYSICAL-BINDING-COLLISION";
            if (additional) {
                const bool image = binding.type == DescriptorType::SampledTexture
                    || binding.type == DescriptorType::StorageTexture;
                // /1 has no dynamic-offset/resource-table closure. An image's
                // dimension, depth and storage semantics must be explicit.
                if (binding.tableBacked || binding.dynamicOffset
                    || image != binding.textureViewDimensionConstrained
                    || (binding.depthTexture && binding.type != DescriptorType::SampledTexture)
                    || (binding.comparisonSampler && binding.type != DescriptorType::Sampler)
                    || (binding.type == DescriptorType::StorageTexture
                        && binding.storageTextureFormat == TextureFormat::Unknown)
                    || (binding.type != DescriptorType::StorageTexture
                        && binding.storageTextureFormat != TextureFormat::Unknown))
                    return "RHI-BINDLESS-PIPELINE-BINDING-SHAPE";
                if (binding.type == DescriptorType::StorageTexture
                    && binding.storageTextureAccess != StorageTextureAccess::WriteOnly
                    && binding.storageTextureAccess != StorageTextureAccess::ReadOnly
                    && binding.storageTextureAccess != StorageTextureAccess::ReadWrite)
                    return "RHI-BINDLESS-PIPELINE-BINDING-SHAPE";
                if (image) {
                    switch (binding.textureViewDimension) {
                    case TextureViewDimension::Texture2D:
                    case TextureViewDimension::Texture2DArray:
                    case TextureViewDimension::Texture3D:
                    case TextureViewDimension::TextureCube:
                        break;
                    default:
                        return "RHI-BINDLESS-PIPELINE-BINDING-SHAPE";
                    }
                }
            }
            if (result.descriptorCount
                > std::numeric_limits<std::uint64_t>::max() - binding.arrayCount)
                return "RHI-BINDLESS-PIPELINE-DEMAND-OVERFLOW";
            result.descriptorCount += binding.arrayCount;
            const auto bucket = resourceClass > 4u ? 4u : resourceClass;
            for (std::size_t stage = 0; stage < kBindlessShaderStages; ++stage) {
                if (!(stages & semantics::stageBits[stage]))
                    continue;
                totals[stage][bucket] += binding.arrayCount;
                if (caps
                    && totals[stage][bucket]
                        > semantics::limitFor(caps->stageLimits[stage], resourceClass))
                    return "RHI-BINDLESS-STAGE-LIMIT";
                if (totals[stage][bucket] > std::numeric_limits<std::uint32_t>::max())
                    return "RHI-BINDLESS-PIPELINE-DEMAND-OVERFLOW";
                auto& all = required.perStage[stage];
                auto& total = bucket == 0u ? all.sampledTextures
                    : bucket == 1u         ? all.samplers
                    : bucket == 2u         ? all.storageTextures
                    : bucket == 3u         ? all.uniformBuffers
                                           : all.storageBuffers;
                total += binding.arrayCount;
                if (additional) {
                    auto& sum = result.additionalPerStage[stage];
                    auto& count = bucket == 0u ? sum.sampledTextures
                        : bucket == 1u         ? sum.samplers
                        : bucket == 2u         ? sum.storageTextures
                        : bucket == 3u         ? sum.uniformBuffers
                                               : sum.storageBuffers;
                    count += binding.arrayCount; // Checked total never exceeds uint32 limits.
                }
            }
        }
        return nullptr;
    };
    if (const auto* id = inspectLayout(artifact.set, artifact.physicalLayout, false))
        return id;
    for (std::size_t i = 0; i < artifact.additionalDomains.size(); ++i) {
        const auto& domain = artifact.additionalDomains[i];
        if (domain.set == artifact.set)
            return "RHI-BINDLESS-PIPELINE-DOMAIN-COLLISION";
        for (std::size_t previous = 0; previous < i; ++previous)
            if (artifact.additionalDomains[previous].set == domain.set)
                return "RHI-BINDLESS-PIPELINE-DOMAIN-COLLISION";
        if (const auto* id = inspectLayout(domain.set, domain.layout, true))
            return id;
    }
    for (std::size_t stage = 0; stage < kBindlessShaderStages; ++stage) {
        const auto& declared = artifact.reservedPerStage[stage];
        if (declared.sampledTextures || declared.samplers || declared.storageTextures
            || declared.uniformBuffers || declared.storageBuffers)
            return "RHI-BINDLESS-PIPELINE-RESERVATIONS-MISMATCH";
        const auto combined =
            caps ? caps->combinedSampledStorageTextureLimits[stage] : std::nullopt;
        if (combined && totals[stage][0u] + totals[stage][2u] > *combined)
            return "RHI-BINDLESS-STAGE-LIMIT";
    }
    result.bindGroupCount = static_cast<std::uint32_t>(artifact.additionalDomains.size()) + 1u;
    required.domainCount = result.bindGroupCount;
    required.descriptorCount = result.descriptorCount;
    output = result;
    demand = required;
    return nullptr;
}
} // namespace

const char* inspectBindlessPipelineInterface(const BindlessCapabilities& caps,
    const BindlessShaderArtifact& artifact,
    BindlessPipelineInterfaceSummary& output) noexcept
{
    BindlessPipelineInterfaceDemand demand;
    return inspectPipelineInterface(&caps, artifact, output, demand);
}

const char* inspectBindlessPipelineInterfaceDemand(
    const BindlessShaderArtifact& artifact, BindlessPipelineInterfaceDemand& output) noexcept
{
    BindlessPipelineInterfaceSummary summary;
    return inspectPipelineInterface(nullptr, artifact, summary, output);
}
} // namespace arp::rhi
