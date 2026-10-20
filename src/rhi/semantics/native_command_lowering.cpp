#include "rhi/semantics/native_command_lowering.h"

#include <utility>

namespace arp::rhi::semantics {

namespace {

void addUnique(NativeCommandLoweringDecision& decision, NativeCommandSemantic semantic)
{
    if (!hasNativeCommandSemantic(decision, semantic)) {
        decision.semantics.push_back(semantic);
    }
}

NativeCommandLoweringDecision makeDecision(const NativeBackendCapabilityContract& contract,
    const CommandWorkloadDesc& workload,
    CommandRecordingIntent intent)
{
    NativeCommandLoweringDecision decision;
    decision.success = true;
    decision.backend = contract.backend;
    decision.intent = intent;
    decision.fallbackMode = rhi::FallbackMode::Native;
    decision.nativeHandleExposedToUpperLayer = false;
    decision.backendVocabularyHiddenFromUpperLayer = true;
    decision.commandCount = workload.commandCount;
    return decision;
}

NativeCommandLoweringDecision makeFailure(const NativeBackendCapabilityContract& contract,
    const CommandWorkloadDesc& workload,
    CommandRecordingIntent intent,
    const char* diagnostics)
{
    auto decision = makeDecision(contract, workload, intent);
    decision.success = false;
    decision.fallbackMode = rhi::FallbackMode::DiagnosticFailure;
    decision.diagnostics = diagnostics;
    return decision;
}

bool usesMrtDepthStencil(const CommandWorkloadDesc& workload)
{
    return workload.colorAttachmentCount > 1 || workload.usesDepthStencilAttachment;
}

bool usesBackendOwnedStaging(const CommandWorkloadDesc& workload)
{
    return workload.usesUploadStaging || workload.usesReadbackStaging;
}

bool usesStageAccessBarriers(const CommandWorkloadDesc& workload)
{
    return workload.usesSubresourceBarriers || workload.usesStageAccessBarriers;
}

void addNullSemantics(NativeCommandLoweringDecision& decision, const CommandWorkloadDesc& workload)
{
    addUnique(decision, NativeCommandSemantic::NullRecordedCommandBuffer);
    if (workload.usesRenderPass || usesMrtDepthStencil(workload) || workload.usesResolveTargets) {
        addUnique(decision, NativeCommandSemantic::NullPassDescriptorValidation);
    }
    if (workload.usesTransferCopies) {
        addUnique(decision, NativeCommandSemantic::NullTransferCommandValidation);
    }
    if (usesBackendOwnedStaging(workload)) {
        addUnique(decision, NativeCommandSemantic::NullBackendOwnedStaging);
    }
    if (usesStageAccessBarriers(workload)) {
        addUnique(decision, NativeCommandSemantic::NullStageAccessBarrierTracking);
    }
    if (workload.usesDynamicViewportScissor) {
        addUnique(decision, NativeCommandSemantic::NullSetViewportCommand);
        addUnique(decision, NativeCommandSemantic::NullSetScissorCommand);
    }
    if (workload.usesGpuCountedIndirectDraw) {
        addUnique(decision, NativeCommandSemantic::NullGpuCountedIndirectValidation);
    }
    if (workload.usesTimestampQuery) {
        addUnique(decision, NativeCommandSemantic::NullTimestampQueryValidation);
    }
    if (workload.usesPipelineStatisticsQuery) {
        addUnique(decision, NativeCommandSemantic::NullPipelineStatisticsQueryValidation);
    }
}

void addVulkanSemantics(NativeCommandLoweringDecision& decision,
    const CommandWorkloadDesc& workload,
    const NativeBackendCapabilityContract& contract)
{
    if (decision.intent == CommandRecordingIntent::SoftSecondaryCommandBuffer
        && hasExecutableNativeFeature(contract, NativeBackendFeature::SecondaryCommandBuffers)) {
        addUnique(decision, NativeCommandSemantic::VulkanSecondaryCommandBuffer);
    } else {
        addUnique(decision, NativeCommandSemantic::VulkanPrimaryCommandBuffer);
    }

    if (workload.usesRenderPass) {
        addUnique(decision, NativeCommandSemantic::VulkanRenderPassEncoding);
    }
    if (usesMrtDepthStencil(workload)) {
        addUnique(decision, NativeCommandSemantic::VulkanMrtDepthStencilRenderPass);
    }
    if (workload.usesResolveTargets) {
        addUnique(decision, NativeCommandSemantic::VulkanResolveAttachment);
    }
    if (workload.usesTransferCopies) {
        addUnique(decision, NativeCommandSemantic::VulkanCopyCommands);
    }
    if (usesBackendOwnedStaging(workload)) {
        addUnique(decision, NativeCommandSemantic::VulkanBackendOwnedStagingBuffer);
    }
    if (workload.usesPushConstants
        && hasExecutableNativeFeature(contract, NativeBackendFeature::PushConstants)) {
        addUnique(decision, NativeCommandSemantic::VulkanNativePushConstants);
    }
    if (workload.usesBarriers) {
        addUnique(decision, NativeCommandSemantic::VulkanPipelineBarrier);
    }
    if (usesStageAccessBarriers(workload)) {
        addUnique(decision, NativeCommandSemantic::VulkanStageAccessPipelineBarrier);
    }
    // vkCmdSetViewport / vkCmdSetScissor are Vulkan 1.0 core - no extension
    // or feature gate required. The pipeline must have been created with
    // dynamicViewportScissor=true (validated at pipeline-state lowering).
    if (workload.usesDynamicViewportScissor) {
        addUnique(decision, NativeCommandSemantic::VulkanDynamicViewportCommand);
        addUnique(decision, NativeCommandSemantic::VulkanDynamicScissorCommand);
    }
    if (workload.usesGpuCountedIndirectDraw) {
        addUnique(decision, NativeCommandSemantic::VulkanDrawIndirectCount);
    }
    if (workload.usesTimestampQuery) {
        addUnique(decision, NativeCommandSemantic::VulkanTimestampQuery);
    }
}

void addMetalSemantics(NativeCommandLoweringDecision& decision,
    const CommandWorkloadDesc& workload,
    const NativeBackendCapabilityContract& contract)
{
    addUnique(decision, NativeCommandSemantic::MetalCommandBuffer);
    if (decision.intent == CommandRecordingIntent::SoftSecondaryCommandBuffer
        && workload.usesPushConstants && !workload.allowCpuReplay) {
        decision = makeFailure(contract,
            workload,
            decision.intent,
            "[PC-E014] Metal native secondary command buffers cannot lower push constants");
        return;
    }
    if (decision.intent == CommandRecordingIntent::RenderBundle || workload.usesRenderBundle) {
        decision.renderBundleSelected = true;
        if (workload.allowCpuReplay) {
            decision.fallbackMode = rhi::FallbackMode::CpuReplay;
            addUnique(decision, NativeCommandSemantic::MetalRenderBundleReplay);
            return;
        }
        if (!hasExecutableNativeFeature(contract, NativeBackendFeature::IndirectCommandBuffers)) {
            decision = makeFailure(contract,
                workload,
                decision.intent,
                "Metal render bundles have no executable indirect-command lowering");
            return;
        }
        addUnique(decision, NativeCommandSemantic::MetalRenderBundleIndirectCommandBuffer);
        return;
    }
    if (decision.intent == CommandRecordingIntent::SoftSecondaryCommandBuffer
        && workload.allowCpuReplay) {
        decision.fallbackMode = rhi::FallbackMode::CpuReplay;
        addUnique(decision, NativeCommandSemantic::MetalSecondaryCommandReplay);
    } else if (decision.intent == CommandRecordingIntent::SoftSecondaryCommandBuffer
        && !hasExecutableNativeFeature(contract, NativeBackendFeature::IndirectCommandBuffers)) {
        decision = makeFailure(contract,
            workload,
            decision.intent,
            "Metal soft secondary command buffers have no executable replay lowering");
        return;
    }
    if (usesBackendOwnedStaging(workload)) {
        addUnique(decision, NativeCommandSemantic::MetalBackendOwnedStagingBuffer);
    }
    if (workload.usesBarriers || usesStageAccessBarriers(workload)) {
        addUnique(decision, NativeCommandSemantic::MetalResourceBarrier);
    }
    if (workload.usesRenderPass) {
        addUnique(decision, NativeCommandSemantic::MetalRenderCommandEncoder);
    }
    if (workload.usesComputeDispatch) {
        addUnique(decision, NativeCommandSemantic::MetalComputeCommandEncoder);
    }
    if (usesMrtDepthStencil(workload)) {
        addUnique(decision, NativeCommandSemantic::MetalMrtDepthStencilRenderEncoder);
    }
    if (workload.usesResolveTargets) {
        addUnique(decision, NativeCommandSemantic::MetalResolveAttachment);
    }
    if (workload.usesTransferCopies) {
        addUnique(decision, NativeCommandSemantic::MetalBlitCopyCommands);
    }
    if (workload.usesIndirectDraw) {
        addUnique(decision, NativeCommandSemantic::MetalIndirectDrawCommand);
    }
    if (decision.intent == CommandRecordingIntent::SoftSecondaryCommandBuffer
        && !workload.allowCpuReplay
        && hasExecutableNativeFeature(contract, NativeBackendFeature::IndirectCommandBuffers)) {
        addUnique(decision, NativeCommandSemantic::MetalIndirectCommandBuffer);
    }
    if (workload.usesPushConstants) {
        const auto pushPlan = buildPushConstantLoweringPlan(rhi::Backend::Metal, contract);
        if (!pushPlan.hasExecutablePayload) {
            decision = makeFailure(contract,
                workload,
                decision.intent,
                "[PC-E011] Metal command workload uses push constants but the selected "
                "Metal lowering has no executable payload");
            return;
        }
        addUnique(decision, NativeCommandSemantic::MetalPushConstantUpload);
    }
    if (workload.usesGpuCountedIndirectDraw && workload.usesMetalGpuCountedIndirectMaskAbi) {
        decision.fallbackMode = rhi::FallbackMode::Emulated;
        addUnique(decision, NativeCommandSemantic::MetalGpuCountedIndirectMask);
    }
    // Metal viewport/scissor state is lowered directly on the active render
    // command encoder.
    if (workload.usesDynamicViewportScissor) {
        addUnique(decision, NativeCommandSemantic::MetalSetViewportCommand);
        addUnique(decision, NativeCommandSemantic::MetalSetScissorCommand);
    }
    if (workload.usesTimestampQuery) {
        addUnique(decision, NativeCommandSemantic::MetalTimestampQuery);
    }
    if (workload.usesPipelineStatisticsQuery) {
        addUnique(decision, NativeCommandSemantic::MetalPipelineStatisticsQuery);
    }
}

void addWebGpuSemantics(NativeCommandLoweringDecision& decision,
    const CommandWorkloadDesc& workload,
    const NativeBackendCapabilityContract& contract)
{
    if (decision.intent == CommandRecordingIntent::SoftSecondaryCommandBuffer) {
        decision.fallbackMode = rhi::FallbackMode::CpuReplay;
        addUnique(decision, NativeCommandSemantic::WebGpuSecondaryCommandReplay);
        return;
    }
    if (decision.intent == CommandRecordingIntent::RenderBundle || workload.usesRenderBundle) {
        if (!hasExecutableNativeFeature(contract, NativeBackendFeature::RenderBundles)) {
            decision = makeFailure(contract,
                workload,
                decision.intent,
                "RHI-WEBGPU-RENDER-BUNDLE-NATIVE-CAPABILITY-UNAVAILABLE");
            return;
        }
        decision.renderBundleSelected = true;
        addUnique(decision, NativeCommandSemantic::WebGpuRenderBundle);
        return;
    }

    addUnique(decision, NativeCommandSemantic::WebGpuCommandEncoder);
    if (workload.usesRenderPass) {
        addUnique(decision, NativeCommandSemantic::WebGpuRenderPassEncoder);
    }
    if (usesMrtDepthStencil(workload)) {
        addUnique(decision, NativeCommandSemantic::WebGpuMrtDepthStencilRenderPass);
    }
    if (workload.usesResolveTargets) {
        addUnique(decision, NativeCommandSemantic::WebGpuResolveAttachment);
    }
    if (workload.usesTransferCopies) {
        addUnique(decision, NativeCommandSemantic::WebGpuCopyCommands);
    }
    if (usesBackendOwnedStaging(workload)) {
        addUnique(decision, NativeCommandSemantic::WebGpuBackendOwnedStagingBuffer);
    }
    if (workload.usesPushConstants) {
        const auto pushPlan = buildPushConstantLoweringPlan(rhi::Backend::WebGPU, contract);
        if (!pushPlan.hasExecutablePayload) {
            decision = makeFailure(contract,
                workload,
                decision.intent,
                "[PC-E011] WebGPU command workload uses push constants but the selected "
                "WebGPU lowering has no queried executable runtime payload");
            return;
        }
        addUnique(decision, NativeCommandSemantic::WebGpuPushConstantUpload);
    }
    if (workload.usesBarriers || usesStageAccessBarriers(workload)) {
        addUnique(decision, NativeCommandSemantic::WebGpuUsageTransition);
    }
    // wgpuRenderPassEncoderSetViewport / SetScissorRect are WebGPU core API.
    if (workload.usesDynamicViewportScissor) {
        addUnique(decision, NativeCommandSemantic::WebGpuSetViewportCommand);
        addUnique(decision, NativeCommandSemantic::WebGpuSetScissorCommand);
    }
    if (workload.usesTimestampQuery) {
        addUnique(decision, NativeCommandSemantic::WebGpuTimestampQuery);
    }
}

void addBackendSemantics(NativeCommandLoweringDecision& decision,
    const CommandWorkloadDesc& workload,
    const NativeBackendCapabilityContract& contract)
{
    switch (contract.backend) {
    case rhi::Backend::WebGL:
        decision = makeFailure(
            contract, workload, decision.intent, "RHI-WEBGL1-COMMAND-EXECUTION-UNAVAILABLE");
        break;
    case rhi::Backend::Null:
        addNullSemantics(decision, workload);
        break;
    case rhi::Backend::Vulkan:
        addVulkanSemantics(decision, workload, contract);
        break;
    case rhi::Backend::Metal:
        addMetalSemantics(decision, workload, contract);
        break;
    case rhi::Backend::WebGPU:
        addWebGpuSemantics(decision, workload, contract);
        break;
    }
}

} // namespace

NativeCommandLoweringDecision lowerCommandWorkload(const NativeBackendCapabilityContract& contract,
    const CommandWorkloadDesc& workload,
    CommandRecordingIntent intent)
{
    if (workload.commandCount == 0) {
        return makeFailure(contract, workload, intent, "command workload is empty");
    }

    // These flags refine the fixed-count indirect substrate; they are not
    // alternate command kinds. Reject malformed callers before backend
    // semantic selection can accidentally authorize a partial lowering.
    if (workload.usesGpuCountedIndirectDraw && !workload.usesIndirectDraw) {
        return makeFailure(
            contract, workload, intent, "RHI GPU-counted indirect draw requires usesIndirectDraw");
    }
    if (workload.usesPatchListIndirectDraw && !workload.usesIndirectDraw) {
        return makeFailure(
            contract, workload, intent, "RHI patch-list indirect draw requires usesIndirectDraw");
    }
    if (workload.usesRenderBundle && intent != CommandRecordingIntent::RenderBundle) {
        return makeFailure(contract,
            workload,
            intent,
            "RHI render-bundle workload requires RenderBundle recording intent");
    }

    if (workload.usesComputeDispatch && contract.backend != rhi::Backend::Null
        && !hasExecutableNativeFeature(contract, NativeBackendFeature::ComputeDispatch)) {
        return makeFailure(contract,
            workload,
            intent,
            "RHI compute dispatch has no executable lowering for this backend");
    }

    if (workload.usesIndirectDraw && contract.backend != rhi::Backend::Null
        && !hasExecutableNativeFeature(contract, NativeBackendFeature::IndirectDraw)) {
        return makeFailure(contract,
            workload,
            intent,
            "RHI indirect draw has no executable lowering for this backend");
    }

    if (contract.backend == rhi::Backend::WebGPU
        && intent == CommandRecordingIntent::SoftSecondaryCommandBuffer
        && !workload.allowCpuReplay) {
        return makeFailure(contract,
            workload,
            intent,
            "WebGPU soft secondary command recording requires explicit CPU replay authorization");
    }

    if (workload.usesPatchListIndirectDraw && contract.backend == rhi::Backend::Metal) {
        auto decision = makeFailure(
            contract, workload, intent, "RHI-METAL-PATCH-INDIRECT-DRAW-LOWERING-UNAVAILABLE");
        decision.patchListIndirect.contractVersion =
            PatchListIndirectLoweringPlan::kContractVersion;
        decision.patchListIndirect.executable = false;
        decision.patchListIndirect.preservesGpuWrittenArguments = false;
        decision.patchListIndirect.preservesFactorBufferLifetime = false;
        decision.patchListIndirect.diagnostic =
            "RHI-METAL-PATCH-INDIRECT-DRAW-LOWERING-UNAVAILABLE";
        return decision;
    }

    if (workload.usesGpuCountedIndirectDraw) {
        // A counted indirect operation always consumes the same indirect
        // argument substrate as a fixed-count indirect draw.
        if (contract.backend == rhi::Backend::Null) {
            // Null validates the shape of the operation but does not pretend
            // to execute a GPU-written range.
        } else if (contract.backend == rhi::Backend::Vulkan
            && hasExecutableNativeFeature(contract, NativeBackendFeature::DrawIndirectCount)) {
            // Vulkan's vkCmdDrawIndexedIndirectCount is the only currently
            // approved native lowering for this L2 operation.
        } else if (contract.backend == rhi::Backend::Metal
            && workload.usesMetalGpuCountedIndirectMaskAbi
            && hasExecutableNativeFeature(contract, NativeBackendFeature::IndirectDraw)) {
            // Metal has no GPU-controlled NSRange for ICB execution. The
            // approved equivalent lowering issues the caller's bounded
            // records and relies on the reflected vertex ABI to suppress
            // every draw whose [[draw_id]] is >= the GPU-written count.
        } else {
            return makeFailure(contract,
                workload,
                intent,
                "RHI GPU-counted indirect draw has no executable lowering for this backend");
        }
    }

    if (workload.usesTimestampQuery && contract.backend != rhi::Backend::Null
        && !((contract.backend == rhi::Backend::Metal || contract.backend == rhi::Backend::WebGPU)
                ? hasExecutableOptionalNativeFeature(
                      contract, NativeBackendFeature::TimestampQueries)
                : hasExecutableNativeFeature(contract, NativeBackendFeature::TimestampQueries))) {
        return makeFailure(contract,
            workload,
            intent,
            contract.backend == rhi::Backend::Metal
                ? "RHI-METAL-TIMESTAMP-QUERY-LOWERING-UNAVAILABLE"
                : contract.backend == rhi::Backend::WebGPU
                ? "RHI-WEBGPU-TIMESTAMP-QUERY-LOWERING-UNAVAILABLE"
                : "RHI-VULKAN-TIMESTAMP-QUERY-LOWERING-UNAVAILABLE");
    }
    if (contract.backend == rhi::Backend::Metal && workload.usesPipelineStatisticsQuery
        && !hasExecutableOptionalNativeFeature(
            contract, NativeBackendFeature::PipelineStatisticsQueries)) {
        return makeFailure(
            contract, workload, intent, "RHI-METAL-PIPELINE-STATISTICS-QUERY-LOWERING-UNAVAILABLE");
    }

    auto decision = makeDecision(contract, workload, intent);
    addBackendSemantics(decision, workload, contract);
    return decision;
}

bool hasNativeCommandSemantic(
    const NativeCommandLoweringDecision& decision, NativeCommandSemantic semantic)
{
    for (const auto entry : decision.semantics) {
        if (entry == semantic) {
            return true;
        }
    }
    return false;
}

rhi::ValidationResult validateNativeCommandLoweringUse(
    const NativeCommandLoweringDecision& decision,
    NativeCommandSemantic semantic,
    rhi::FallbackMode expectedFallback,
    std::string_view operation)
{
    if (!decision.success) {
        return {false, decision.diagnostics};
    }
    if (!hasNativeCommandSemantic(decision, semantic)) {
        std::pmr::string diagnostics;
        diagnostics += operation;
        diagnostics += " lowering decision omitted the required native command semantic";
        return {false, std::move(diagnostics)};
    }
    if (decision.fallbackMode != expectedFallback) {
        std::pmr::string diagnostics;
        diagnostics += operation;
        diagnostics += " lowering decision selected an unexpected fallback mode";
        return {false, std::move(diagnostics)};
    }
    return {true, {}};
}

} // namespace arp::rhi::semantics
