#pragma once

#include "core/cpu_allocator.h"
#include "rhi/gpu_capabilities.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/semantics/native_backend_capabilities.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace arp::rhi::semantics {

enum class CommandRecordingIntent {
    PrimaryCommandBuffer,
    SoftSecondaryCommandBuffer,
    RenderBundle
};

struct CommandWorkloadDesc {
    std::pmr::string label;
    std::uint32_t commandCount = 0;
    /// Authorizes a backend-owned CPU replay lowering for a soft secondary or
    /// render bundle. It is intentionally opt-in because replay changes the
    /// recording/execution boundary and must be visible to the caller's
    /// lowering decision.
    bool allowCpuReplay = false;
    bool usesRenderPass = false;
    bool usesPushConstants = false;
    bool usesRenderBundle = false;
    bool usesBarriers = false;
    std::uint32_t colorAttachmentCount = 0;
    bool usesDepthStencilAttachment = false;
    bool usesResolveTargets = false;
    bool usesTransferCopies = false;
    bool usesUploadStaging = false;
    bool usesReadbackStaging = false;
    bool usesSubresourceBarriers = false;
    bool usesStageAccessBarriers = false;
    /// Workload issues RenderPassEncoder::setViewport / setScissor at record
    /// time. Backends with encoder-side dynamic state (Null/Metal/WebGPU) add
    /// the corresponding runtime command semantic; Vulkan adds the dynamic-
    /// state command semantic (vkCmdSetViewport / vkCmdSetScissor). The
    /// pipeline must have been created with dynamicViewportScissor=true.
    bool usesDynamicViewportScissor = false;
    /// The workload contains a direct or indirect compute dispatch. This is
    /// kept separate from render-pass encoding so the selected device's
    /// ComputeDispatch capability is consumed before a native compute encoder
    /// is created.
    bool usesComputeDispatch = false;
    /// The workload contains a fixed-count indirect draw. GPU-counted indirect
    /// draws set this as well and are additionally gated by their count
    /// lowering contract.
    bool usesIndirectDraw = false;
    /// GPU-controlled draw count is a distinct L2 operation. It must never
    /// be silently lowered to fixed-count indirect execution.
    bool usesGpuCountedIndirectDraw = false;
    /// The selected render pipeline's generated shader explicitly implements
    /// MetalGpuCountedIndirect/1: [[draw_id]] plus a GPU-count mask. This is
    /// required before Metal may issue the bounded fixed-record equivalent.
    bool usesMetalGpuCountedIndirectMaskAbi = false;
    /// The selected render pipeline is PatchList and the command is indirect.
    /// Metal has no native indirect-patch encoder for the L2 argument layout;
    /// this must therefore be rejected by lowering rather than falling back to
    /// triangles or a CPU readback of a GPU-written argument buffer.
    bool usesPatchListIndirectDraw = false;
    /// The workload records a timestamp query against the active native
    /// encoder. Query creation and completion remain capability/lifetime
    /// gated; this bit authorizes the command-side lowering only.
    bool usesTimestampQuery = false;
    /// The workload records one pipeline-statistics query against the active
    /// native encoder. This is optional and must never be inferred from a
    /// generic timestamp capability.
    bool usesPipelineStatisticsQuery = false;
};

/// Metal cannot encode a GPU-written patch argument record directly through
/// MTLRenderCommandEncoder or MTLIndirectCommandBuffer. Keep the unsupported
/// boundary as a versioned lowering fact instead of letting each backend
/// invent a different interpretation of DrawIndexedIndirectCommand.
struct PatchListIndirectLoweringPlan {
    static constexpr std::string_view kContractVersion = "MetalPatchListIndirect/1";
    std::string_view contractVersion = kContractVersion;
    bool executable = false;
    bool preservesGpuWrittenArguments = false;
    bool preservesFactorBufferLifetime = false;
    const char* diagnostic = "RHI-METAL-PATCH-INDIRECT-DRAW-LOWERING-UNAVAILABLE";
};

enum class NativeCommandSemantic {
    NullRecordedCommandBuffer,
    VulkanPrimaryCommandBuffer,
    VulkanSecondaryCommandBuffer,
    VulkanRenderPassEncoding,
    VulkanNativePushConstants,
    VulkanPipelineBarrier,
    MetalCommandBuffer,
    MetalRenderCommandEncoder,
    MetalComputeCommandEncoder,
    /// Primary command-buffer indirect draw lowering. Metal implements this
    /// with the bounded render-encoder indirect-record loop; it is distinct
    /// from the ICB semantic used by native secondary/render-bundle replay.
    MetalIndirectDrawCommand,
    MetalIndirectCommandBuffer,
    MetalRenderBundleIndirectCommandBuffer,
    MetalRenderBundleReplay,
    MetalSecondaryCommandReplay,
    MetalPushConstantUpload,
    MetalGpuCountedIndirectMask,
    WebGpuCommandEncoder,
    WebGpuRenderPassEncoder,
    WebGpuRenderBundle,
    WebGpuSecondaryCommandReplay,
    WebGpuPushConstantUpload,
    NullPassDescriptorValidation,
    NullTransferCommandValidation,
    NullBackendOwnedStaging,
    NullStageAccessBarrierTracking,
    VulkanMrtDepthStencilRenderPass,
    VulkanResolveAttachment,
    VulkanCopyCommands,
    VulkanBackendOwnedStagingBuffer,
    VulkanStageAccessPipelineBarrier,
    MetalMrtDepthStencilRenderEncoder,
    MetalResolveAttachment,
    MetalBlitCopyCommands,
    MetalBackendOwnedStagingBuffer,
    MetalResourceBarrier,
    WebGpuMrtDepthStencilRenderPass,
    WebGpuResolveAttachment,
    WebGpuCopyCommands,
    WebGpuBackendOwnedStagingBuffer,
    WebGpuUsageTransition,
    // Dynamic viewport/scissor commands (RenderPassEncoder::setViewport /
    // setScissor). Each backend records one semantic per command so the
    // parity test can assert uniform runtime-command lowering.
    NullSetViewportCommand,
    NullSetScissorCommand,
    VulkanDynamicViewportCommand, // vkCmdSetViewport (Vulkan 1.0 core)
    VulkanDynamicScissorCommand,  // vkCmdSetScissor  (Vulkan 1.0 core)
    MetalSetViewportCommand,      // [encoder setViewport:]
    MetalSetScissorCommand,       // [encoder setScissorRect:]
    WebGpuSetViewportCommand,     // wgpuRenderPassEncoderSetViewport (WebGPU core)
    WebGpuSetScissorCommand,      // wgpuRenderPassEncoderSetScissorRect (WebGPU core)
    NullGpuCountedIndirectValidation,
    VulkanDrawIndirectCount,
    NullTimestampQueryValidation,
    NullPipelineStatisticsQueryValidation,
    VulkanTimestampQuery,
    MetalTimestampQuery,
    MetalPipelineStatisticsQuery,
    WebGpuTimestampQuery
};

struct NativeCommandLoweringDecision {
    bool success = false;
    rhi::Backend backend = rhi::Backend::Null;
    CommandRecordingIntent intent = CommandRecordingIntent::PrimaryCommandBuffer;
    rhi::FallbackMode fallbackMode = rhi::FallbackMode::Native;
    bool nativeHandleExposedToUpperLayer = false;
    bool backendVocabularyHiddenFromUpperLayer = true;
    bool renderBundleSelected = false;
    std::uint32_t commandCount = 0;
    PatchListIndirectLoweringPlan patchListIndirect;
    std::pmr::vector<NativeCommandSemantic> semantics;
    std::pmr::string diagnostics;
};

NativeCommandLoweringDecision lowerCommandWorkload(const NativeBackendCapabilityContract& contract,
    const CommandWorkloadDesc& workload,
    CommandRecordingIntent intent);

bool hasNativeCommandSemantic(
    const NativeCommandLoweringDecision& decision, NativeCommandSemantic semantic);

/// Validates the semantic that the caller is about to execute against the
/// lowering decision.  A successful decision is not sufficient: the selected
/// native/replay path must consume the semantic and, when requested, the
/// declared fallback mode.
rhi::ValidationResult validateNativeCommandLoweringUse(
    const NativeCommandLoweringDecision& decision,
    NativeCommandSemantic semantic,
    rhi::FallbackMode expectedFallback,
    std::string_view operation);

} // namespace arp::rhi::semantics
