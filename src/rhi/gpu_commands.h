#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "rhi/frontend/indirect_sequence_range.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_pipeline.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arp::rhi {

class QueryPool;
class BindlessPreparedGraphicsBinding;
class BindlessAccessDeclarations;

struct CommandEncoderDesc {
    std::pmr::string label;
    // Preparation-time reservations. Zero disables indexed command retention.
    std::uint32_t maxBindlessPipelines = 0;
    std::uint32_t maxBindlessSnapshots = 0;
    // Number of indirect access declarations retained by an indexed render pass.
    std::uint32_t maxBindlessIndirectAccesses = 0;
    // Prepared closed processors and their distinct fault reports per command.
    std::uint32_t maxBindlessIndirectProcessors = 0;
    // Complete retained intent for PreparedGraphicsPassFaultScope/1.
    std::uint32_t maxBindlessGraphicsAccesses = 0;
    // Explicit optional contract. Absence grants no ordinary retention bound.
    std::optional<CommandRetentionBudget> retentionBudget;
};

enum class LoadOp { Load, Clear, DontCare };

enum class StoreOp { Store, DontCare };

struct RenderPassColorAttachmentDesc {
    std::pmr::string label;
    GpuResourceId target;
    TextureFormat format = TextureFormat::Unknown;
    LoadOp loadOp = LoadOp::Load;
    StoreOp storeOp = StoreOp::Store;
    bool hasResolveTarget = false;
    GpuResourceId resolveTarget;
    Color clearColor;
};

struct RenderPassDepthStencilAttachmentDesc {
    std::pmr::string label;
    GpuResourceId target;
    TextureFormat format = TextureFormat::Unknown;
    LoadOp depthLoadOp = LoadOp::Load;
    StoreOp depthStoreOp = StoreOp::Store;
    LoadOp stencilLoadOp = LoadOp::Load;
    StoreOp stencilStoreOp = StoreOp::Store;
    // DepthStencilAttachmentInput/1. Clear values are L2 intent, including
    // when the selected load operation does not consume them. Read-only
    // enforcement requires a complete pass/pipeline proof and currently fails
    // closed on every backend.
    float depthClearValue = 1.0f;
    std::uint32_t stencilClearValue = 0u;
    bool depthReadOnly = false;
    bool stencilReadOnly = false;
};

struct RenderPassDesc {
    std::pmr::string colorTargetName;
    std::pmr::string label;
    std::pmr::vector<RenderPassColorAttachmentDesc> colorAttachments;
    bool hasDepthStencilAttachment = false;
    RenderPassDepthStencilAttachmentDesc depthStencilAttachment;

    /// Multi-subpass layout. Empty (default) = single subpass with all color
    /// attachments + depth (existing behavior). Non-empty = explicit subpass
    /// layout; the backend creates a render pass with subpassCount > 1 and
    /// the executor calls nextSubpass() between subpass recordings.
    std::pmr::vector<SubpassDesc> subpasses;
    /// Dependencies between subpasses (tile-local barriers). Ignored when
    /// `subpasses` is empty.
    std::pmr::vector<SubpassDependency> subpassDependencies;
    /// Explicit opt-in for rendering to a 2D texture-array view. Every color,
    /// resolve, and depth attachment must expose the same layer count greater
    /// than one, and the selected fragment entry point must declare the
    /// render-target-array-index ABI. A multi-layer view is never interpreted
    /// as layered rendering implicitly.
    bool layeredRendering = false;
};

struct ComputePassDesc {
    std::pmr::string label;
};

struct TransferPassDesc {
    std::pmr::string label;
};

struct RenderBundleDesc {
    std::pmr::string label;
    /// Render bundles have no pipeline or descriptor state of their own. A
    /// backend may therefore use an explicit CPU replay lowering against the
    /// consuming render pass, but only when the caller opts into that cost and
    /// ordering model. The default remains false so a recording-only object
    /// cannot be mistaken for an executable bundle.
    bool allowCpuReplay = false;
    /// Native bundle encodings need the primitive type at record time. The
    /// consuming render pipeline must use the same topology.
    PrimitiveTopology primitiveTopology = PrimitiveTopology::TriangleList;
    /// Fixed native bundle capacity. Backends reject recordings beyond it.
    std::uint32_t maxCommandCount = 4096u;
    /// Required only for PatchList topology. A bundle does not have a
    /// pipeline of its own, so the control-point count is part of the
    /// recording contract and must match the consuming pipeline.
    std::uint32_t patchControlPointCount = 0u;
    /// Native render-bundle encoders are compatibility-bound to the render
    /// pass attachment formats at recording time. An empty list is valid only
    /// for a bundle that is never submitted to a native encoder; executable
    /// backends must reject that form rather than infer formats later.
    std::pmr::vector<TextureFormat> colorFormats;
    TextureFormat depthStencilFormat = TextureFormat::Unknown;
    std::uint32_t sampleCount = 1u;
};

enum class IndexFormat { Uint16, Uint32 };

// PushConstant command descriptor — L2 contract entry point.
//
// L2 -> L0 contract chain:
//   L2  AtomicFullBlockPush profile (gpu_common.h kL2AtomicPushConstantLimitBytes)
//       enforced at encoder level by semantics::validatePushConstantCommand
//   L1  semantics::validatePushConstantBinding checks pipeline layout
//       (exactly one range, offset 0, exact size match, stage coverage)
//   L0  Backend-specific lowering:
//       Vulkan  -> vkCmdPushConstants          (native)
//       Metal   -> setVertexBytes/setFragmentBytes/setBytes using reflected slots
//       WebGPU  -> uniform ring + persistent dynamic-offset BGL bind-group
//       Null    -> reference validator (contract test fixture)
//
// All four backends call the shared L1 validators before lowering.
// No native handle/vocabulary is exposed to renderer/game layers.
struct PushConstantsDesc {
    ShaderStage stages = ShaderStage::None;
    std::uint32_t offsetBytes = 0;
    std::uint32_t sizeBytes = 0;
    const void* data = nullptr;
};

struct SecondaryCommandEncoderDesc {
    std::pmr::string label;
    /// CPU replay changes command recording/execution behavior and must be
    /// explicitly requested by a feature-specific lowering contract.
    bool allowCpuReplay = false;
    /// Native secondary lowerings need the primitive topology at ICB record
    /// time because a Metal indirect command stores it in the command.
    PrimitiveTopology primitiveTopology = PrimitiveTopology::TriangleList;
    /// Maximum number of executable records reserved by the selected native
    /// secondary lowering. Fixed-capacity native command stores must reject
    /// recordings beyond this budget instead of silently truncating them.
    std::uint32_t maxCommandCount = 4096u;
    /// Required only for PatchList topology. Zero is invalid for a patch
    /// secondary and is never inferred from draw counts.
    std::uint32_t patchControlPointCount = 0u;
};

enum class PipelineStage : std::uint32_t {
    None = 0,
    TopOfPipe = 1u << 0,
    Transfer = 1u << 1,
    VertexShader = 1u << 2,
    FragmentShader = 1u << 3,
    ComputeShader = 1u << 4,
    ColorAttachmentOutput = 1u << 5,
    DepthStencil = 1u << 6,
    Present = 1u << 7,
    DrawIndirect = 1u << 8,
    // Fixed-function vertex/instance fetch and index fetch, not shader execution.
    VertexInput = 1u << 9
};

inline PipelineStage operator|(PipelineStage lhs, PipelineStage rhs)
{
    return static_cast<PipelineStage>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

inline PipelineStage operator&(PipelineStage lhs, PipelineStage rhs)
{
    return static_cast<PipelineStage>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

enum class ResourceAccess : std::uint32_t {
    None = 0,
    TransferRead = 1u << 0,
    TransferWrite = 1u << 1,
    ShaderRead = 1u << 2,
    ShaderWrite = 1u << 3,
    ColorAttachmentRead = 1u << 4,
    ColorAttachmentWrite = 1u << 5,
    DepthStencilRead = 1u << 6,
    DepthStencilWrite = 1u << 7,
    PresentRead = 1u << 8,
    IndirectCommandRead = 1u << 9,
    VertexAttributeRead = 1u << 10,
    IndexRead = 1u << 11
};

inline ResourceAccess operator|(ResourceAccess lhs, ResourceAccess rhs)
{
    return static_cast<ResourceAccess>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

inline ResourceAccess operator&(ResourceAccess lhs, ResourceAccess rhs)
{
    return static_cast<ResourceAccess>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

struct ResourceBarrierResult {
    bool success = false;
    std::pmr::string diagnostics;
    GpuResourceId resource;
    ResourceState before = ResourceState::Undefined;
    ResourceState after = ResourceState::Undefined;
    bool subresourceAware = false;
    bool stageAccessAware = false;
    PipelineStage sourceStages = PipelineStage::None;
    PipelineStage destinationStages = PipelineStage::None;
    ResourceAccess sourceAccess = ResourceAccess::None;
    ResourceAccess destinationAccess = ResourceAccess::None;
};

struct ResourceBarrierDesc {
    GpuResourceId resource;
    TextureSubresourceRange textureSubresources;
    ResourceState before = ResourceState::Undefined;
    ResourceState after = ResourceState::Undefined;
    PipelineStage sourceStages = PipelineStage::None;
    PipelineStage destinationStages = PipelineStage::None;
    ResourceAccess sourceAccess = ResourceAccess::None;
    ResourceAccess destinationAccess = ResourceAccess::None;
};

/// An access-only dependency for one resource. Unlike ResourceBarrierDesc it
/// deliberately does not carry before/after states and never changes logical
/// resource-state metadata.
struct ResourceMemoryBarrierDesc {
    GpuResourceId resource;
    PipelineStage sourceStages = PipelineStage::None;
    PipelineStage destinationStages = PipelineStage::None;
    ResourceAccess sourceAccess = ResourceAccess::None;
    ResourceAccess destinationAccess = ResourceAccess::None;
};

struct AliasingBarrierDesc {
    GpuResourceId beforeResource;
    GpuResourceId afterResource;
    std::uint32_t aliasGroup = 0;
    PipelineStage sourceStages = PipelineStage::None;
    PipelineStage destinationStages = PipelineStage::None;
    ResourceAccess sourceAccess = ResourceAccess::None;
    ResourceAccess destinationAccess = ResourceAccess::None;
    std::uint32_t sourceQueueRole = 0;
    std::uint32_t destinationQueueRole = 0;
    bool ownershipHandoff = false;
};

enum class StagingIntent { None, Upload, Readback };

struct BufferCopyDesc {
    GpuResourceId sourceBuffer;
    GpuResourceId destinationBuffer;
    std::uint64_t sourceOffsetBytes = 0;
    std::uint64_t destinationOffsetBytes = 0;
    std::uint64_t sizeBytes = 0;
};

struct BufferToTextureCopyDesc {
    GpuResourceId sourceBuffer;
    GpuResourceId destinationTexture;
    std::uint64_t bufferOffsetBytes = 0;
    std::uint32_t bytesPerRow = 0;
    std::uint32_t rowsPerImage = 0;
    TextureSubresourceRange textureSubresources;
    Extent3D extent;

    /// Optional per-mip packing contract. Empty is the legacy single-mip
    /// form. When non-empty it must contain exactly mipLevelCount entries in
    /// ascending mip order; bufferOffsetBytes is the base of the first entry
    /// and each entry's bufferOffsetBytes is relative to that base. Array
    /// layers within one mip are packed by bytesPerImage.
    struct MipLayout {
        std::uint32_t mipLevel = 0;
        Extent3D extent;
        std::uint32_t bytesPerRow = 0;
        std::uint32_t rowsPerImage = 0;
        std::uint64_t bufferOffsetBytes = 0;
    };

    std::pmr::vector<MipLayout> mipLayouts;
};

struct TextureToBufferCopyDesc {
    GpuResourceId sourceTexture;
    GpuResourceId destinationBuffer;
    TextureSubresourceRange textureSubresources;
    std::uint64_t bufferOffsetBytes = 0;
    std::uint32_t bytesPerRow = 0;
    std::uint32_t rowsPerImage = 0;
    Extent3D extent;

    struct MipLayout {
        std::uint32_t mipLevel = 0;
        Extent3D extent;
        std::uint32_t bytesPerRow = 0;
        std::uint32_t rowsPerImage = 0;
        std::uint64_t bufferOffsetBytes = 0;
    };

    std::pmr::vector<MipLayout> mipLayouts;
};

struct TextureToTextureCopyDesc {
    GpuResourceId sourceTexture;
    GpuResourceId destinationTexture;
    TextureSubresourceRange sourceSubresources;
    TextureSubresourceRange destinationSubresources;
    Extent3D extent;

    /// Optional per-mip extents. Empty is the legacy single-mip form; a
    /// non-empty vector must match both mip ranges and is ordered by mip.
    struct MipLayout {
        std::uint32_t mipLevel = 0;
        Extent3D extent;
    };

    std::pmr::vector<MipLayout> mipLayouts;
};

struct UploadBufferDesc {
    std::pmr::string label;
    GpuResourceId sourceUploadBuffer;
    GpuResourceId destinationResource;
    std::uint64_t sizeBytes = 0;
    StagingIntent stagingIntent = StagingIntent::Upload;
    bool backendOwnedStaging = true;
    std::uint64_t frameSerial = 0;
};

/// Explicit L2 staging row-pitch contract shared by Null, Vulkan, Metal and
/// WebGPU texture upload lowerings. Callers pack staging rows to this value;
/// backend-owned copies then consume the same declared layout.
inline constexpr std::uint32_t kTextureUploadRowPitchAlignment = 256u;

struct UploadTextureDesc {
    std::pmr::string label;
    GpuResourceId destinationTexture;
    Extent3D extent;
    // Destination origin (top-left) within the destination texture, in texels.
    // (0,0,0) means a full-texture or top-left upload. Sub-rect uploads (e.g.
    // dynamic glyph cache updates) set this to the atlas slot's absolute
    // offset so the pixel data lands in the correct region instead of
    // overwriting the top-left corner. Backends must honor this via the
    // native copy origin (VkOffset3D / MTLOrigin / WGPUOrigin3D).
    // Note: Extent3D defaults depth=1 (sensible for an extent), so the
    // default initializer here must zero all three components explicitly.
    Extent3D destinationOffset{0u, 0u, 0u};
    TextureFormat format = TextureFormat::Unknown;
    std::uint32_t bytesPerRow = 0;
    std::uint32_t rowsPerImage = 0;
    std::uint32_t mipLevel = 0;
    std::uint32_t arrayLayer = 0;
    TextureSubresourceRange subresources;

    /// Optional per-mip packing contract. Empty is the legacy single-mip
    /// form. When non-empty it must contain exactly subresources.mipLevelCount
    /// entries in ascending mip order. Each bufferOffsetBytes is relative to
    /// pixelData and layers within that mip are packed by bytesPerImage.
    /// Backends must reject this form until they implement the same lowering;
    /// silently uploading only the first mip violates the subresource contract.
    struct MipLayout {
        std::uint32_t mipLevel = 0;
        Extent3D extent;
        std::uint32_t bytesPerRow = 0;
        std::uint32_t rowsPerImage = 0;
        std::uint64_t bufferOffsetBytes = 0;
    };

    std::pmr::vector<MipLayout> mipLayouts;
    const void* pixelData = nullptr;
    std::uint32_t pixelDataSize = 0;
    StagingIntent stagingIntent = StagingIntent::Upload;
    bool backendOwnedStaging = true;
    std::uint64_t frameSerial = 0;
    std::uint64_t resourceGeneration = 0;
};

struct ReadbackTextureDesc {
    std::pmr::string label;
    GpuResourceId sourceResource;
    GpuResourceId destinationReadbackBuffer;
    std::uint64_t sizeBytes = 0;
    StagingIntent stagingIntent = StagingIntent::Readback;
    bool backendOwnedStaging = true;
    /// Optional explicit source range. The legacy zero-extent form reads the
    /// complete source resource at mip 0/layer 0; non-zero extent requires
    /// this range and an explicit row layout below.
    TextureSubresourceRange sourceSubresources;
    Extent3D extent;
    std::uint32_t bytesPerRow = 0;
    std::uint32_t rowsPerImage = 0;
    std::pmr::vector<TextureToBufferCopyDesc::MipLayout> mipLayouts;
    std::uint64_t destinationOffsetBytes = 0;
};

enum class CommandValidationStatus {
    Success,
    DiagnosticFailure,
    Deferred,
};

struct CommandValidationResult {
    bool success = false;
    std::pmr::string diagnostics;
    std::uint32_t recordedCommandCount = 0;
    CommandValidationStatus status = CommandValidationStatus::DiagnosticFailure;
    std::pmr::string diagnosticId;

    bool isDeferred() const noexcept
    {
        return !success && status == CommandValidationStatus::Deferred;
    }
};

class ARP_API CommandBuffer {
public:
    virtual ~CommandBuffer() = default;

    virtual void* queryInterface(std::string_view) noexcept { return nullptr; }

    virtual std::uint32_t commandCount() const = 0;
};

class ARP_API RenderBundle {
public:
    virtual ~RenderBundle() = default;
    virtual std::uint32_t commandCount() const = 0;
};

class ARP_API SecondaryCommandBuffer {
public:
    virtual ~SecondaryCommandBuffer() = default;
    virtual std::uint32_t commandCount() const = 0;
    virtual bool softSecondary() const = 0;
};

class ARP_API RenderPassEncoder {
public:
    virtual ~RenderPassEncoder() = default;

    virtual void* queryInterface(std::string_view) noexcept { return nullptr; }

    virtual CommandValidationResult setPipeline(const RenderPipeline& pipeline) = 0;

    /// Set the viewport for subsequent draws in this render pass. Overrides
    /// the pipeline's static viewport state for this pass; the pipeline MUST
    /// have been created with dynamicViewportScissor=true when the backend
    /// requires an opt-in (Vulkan). Values must be fully specified - there is
    /// no implicit "inherit framebuffer size" default (AGENTS.md RHI Rule 4:
    /// viewport is named as state that must come from a complete L2
    /// descriptor or lowering output). A second call overrides the previous
    /// one and only affects subsequent draws. State does NOT persist across
    /// nextSubpass; each subpass must re-issue setViewport if it needs a
    /// non-default rect. Requires a pipeline to be bound first.
    virtual CommandValidationResult setViewport(const ViewportState& viewport) = 0;

    /// Set the scissor rect for subsequent draws. Same lifecycle rules as
    /// setViewport: pipeline must be bound first, values must be fully
    /// specified (width/height > 0), does not persist across nextSubpass.
    virtual CommandValidationResult setScissor(const ScissorState& scissor) = 0;

    // VertexStreamBinding/1: binding may precede setPipeline. Logical stream
    // bindings persist across pipeline switches within the render pass;
    // descriptor/push-constant updates must not overwrite vertex data.
    virtual CommandValidationResult bindVertexBuffer(
        std::uint32_t slot, const GpuBuffer& buffer, std::uint64_t offsetBytes = 0) = 0;
    virtual CommandValidationResult bindIndexBuffer(
        const GpuBuffer& buffer, IndexFormat format, std::uint64_t offsetBytes = 0) = 0;

    /// Bind the Metal tessellation-factor buffer for a PatchList pipeline.
    /// The buffer is a dedicated tessellation resource, not a vertex buffer.
    /// It may be recorded before or after setPipeline, but a PatchList draw
    /// must have one valid binding. The stride is part of the generated shader
    /// ABI and must match the selected pipeline reflection.
    /// Backends without an executable PatchList lowering fail explicitly via
    /// the default implementation.
    virtual CommandValidationResult bindTessellationFactorBuffer(
        const GpuBuffer&, std::uint64_t, std::uint32_t)
    {
        return {false, "RHI tessellation factor buffer lowering is unavailable", 0};
    }

    virtual CommandValidationResult bindDescriptorSet(std::uint32_t set,
        const DescriptorSet& descriptorSet,
        const std::uint32_t* dynamicOffsets = nullptr,
        std::uint32_t dynamicOffsetCount = 0) = 0;
    /// Dynamic offsets follow the same cross-backend ordering rule: values
    /// are supplied in ascending binding-number order for bindings in that
    /// set whose layout declares dynamicOffset=true. The order is
    /// independent of source declaration order.
    virtual CommandValidationResult bindDescriptorTable(
        std::uint32_t set, const DescriptorTable& descriptorTable) = 0;
    // AtomicFullBlockPush profile requires non-null data.
    // The data=nullptr default is removed; all production callers
    // must provide a valid push-constant buffer.
    virtual CommandValidationResult pushConstants(ShaderStage stages,
        std::uint32_t offsetBytes,
        std::uint32_t sizeBytes,
        const void* data) = 0;

    virtual CommandValidationResult pushConstants(const PushConstantsDesc& desc)
    {
        return pushConstants(desc.stages, desc.offsetBytes, desc.sizeBytes, desc.data);
    }

    virtual CommandValidationResult executeSecondary(
        const SecondaryCommandBuffer& commandBuffer) = 0;
    /// Execute draw records against the currently bound render-pass state.
    /// Backends must consume a native bundle payload or fail explicitly.
    virtual CommandValidationResult executeRenderBundle(const RenderBundle& bundle) = 0;
    virtual CommandValidationResult drawIndexed(std::uint32_t indexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstIndex,
        std::int32_t vertexOffset,
        std::uint32_t firstInstance) = 0;
    virtual CommandValidationResult drawIndexedIndirect(const GpuBuffer& indirectBuffer,
        std::uint64_t indirectOffset,
        std::uint32_t drawCount,
        std::uint32_t stride) = 0;

    /// SingleRecordIndirectSequence/1: ordered GPU argument reads with the
    /// identity of separate single draws. No native multi-draw capability,
    /// CPU readback or per-record state changes. Full range preflight first.
    virtual CommandValidationResult drawIndexedIndirectSequence(
        const GpuBuffer& buffer, std::uint64_t offset, std::uint32_t count, std::uint32_t stride)
    {
        const IndexedIndirectSequenceRange range(buffer.desc(), offset, count, stride);
        if (!range.valid())
            return {false, "RHI-INDIRECT-SEQUENCE-RANGE-INVALID", 0u};
        CommandValidationResult result{};
        for (std::uint32_t ordinal = 0; ordinal < count; ++ordinal) {
            std::uint64_t address = 0;
            range.offset(ordinal, address);
            result = drawIndexedIndirect(buffer, address, 1u, stride);
            if (!result.success)
                return result;
        }
        return result;
    }

    /// Indirect draw with GPU-provided draw count. Reads up to maxDrawCount
    /// DrawIndexedIndirectCommand records from argsBuffer (starting at
    /// argsOffset), but the actual draw count is read from countBuffer
    /// (at countOffset, as a single uint32). The GPU-side count lets a
    /// compute shader fully control how many draws execute without a CPU
    /// readback. Corresponds to vkCmdDrawIndexedIndirectCount. Requires
    /// EngineFeature::MultiDrawIndirectCount; callers must gate on the
    /// feature and fall back to drawIndexedIndirect with a CPU-known
    /// maxDrawCount when unsupported.
    virtual CommandValidationResult drawIndexedIndirectCount(const GpuBuffer& argsBuffer,
        std::uint64_t argsOffset,
        const GpuBuffer& countBuffer,
        std::uint64_t countOffset,
        std::uint32_t maxDrawCount,
        std::uint32_t stride) = 0;
    virtual CommandValidationResult draw(std::uint32_t vertexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstVertex,
        std::uint32_t firstInstance) = 0;
    virtual CommandValidationResult writeTimestamp(
        const QueryPool& queryPool, std::uint32_t queryIndex) = 0;

    /// Record one cumulative pipeline-statistics snapshot. The operation is
    /// separate from writeTimestamp so a timestamp pool can never be
    /// reinterpreted as a statistics pool.
    virtual CommandValidationResult writePipelineStatistics(const QueryPool&, std::uint32_t)
    {
        return {false, "RHI pipeline-statistics query lowering is unavailable", 0};
    }

    /// Advance to the next subpass. Must be called between subpass recordings
    /// when RenderPassDesc.subpasses has more than one entry. On single-subpass
    /// passes this is never called. On Metal, subpass transitions are implicit
    /// (framebuffer fetch reads within the same render pass), so this is a
    /// no-op that just advances the internal subpass counter.
    virtual CommandValidationResult nextSubpass() = 0;

    /// Returns the current subpass index (0-based). Always 0 for single-
    /// subpass render passes.
    virtual std::uint32_t currentSubpassIndex() const = 0;

    virtual CommandValidationResult end() = 0;
};

class ARP_API ComputePassEncoder {
public:
    virtual ~ComputePassEncoder() = default;

    virtual void* queryInterface(std::string_view) noexcept { return nullptr; }

    virtual CommandValidationResult setPipeline(const ComputePipeline& pipeline) = 0;
    virtual CommandValidationResult bindDescriptorSet(std::uint32_t set,
        const DescriptorSet& descriptorSet,
        const std::uint32_t* dynamicOffsets = nullptr,
        std::uint32_t dynamicOffsetCount = 0) = 0;
    virtual CommandValidationResult bindDescriptorTable(
        std::uint32_t set, const DescriptorTable& descriptorTable) = 0;
    // AtomicFullBlockPush profile requires non-null data.
    // The data=nullptr default is removed; all production callers
    // must provide a valid push-constant buffer.
    virtual CommandValidationResult pushConstants(ShaderStage stages,
        std::uint32_t offsetBytes,
        std::uint32_t sizeBytes,
        const void* data) = 0;

    virtual CommandValidationResult pushConstants(const PushConstantsDesc& desc)
    {
        return pushConstants(desc.stages, desc.offsetBytes, desc.sizeBytes, desc.data);
    }

    virtual CommandValidationResult dispatch(
        std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ) = 0;
    virtual CommandValidationResult dispatchIndirect(
        const GpuBuffer& indirectBuffer, std::uint64_t indirectOffset) = 0;
    virtual CommandValidationResult writeTimestamp(
        const QueryPool& queryPool, std::uint32_t queryIndex) = 0;

    virtual CommandValidationResult writePipelineStatistics(const QueryPool&, std::uint32_t)
    {
        return {false, "RHI pipeline-statistics query lowering is unavailable", 0};
    }

    virtual CommandValidationResult end() = 0;
};

class ARP_API TransferPassEncoder {
public:
    virtual ~TransferPassEncoder() = default;
    virtual CommandValidationResult copyBufferToBuffer(std::uint64_t sizeBytes) = 0;
    virtual CommandValidationResult copyBufferToBuffer(const BufferCopyDesc& desc) = 0;
    virtual CommandValidationResult copyBufferToTexture(const BufferToTextureCopyDesc& desc) = 0;
    virtual CommandValidationResult copyTextureToBuffer(const TextureToBufferCopyDesc& desc) = 0;
    virtual CommandValidationResult copyTextureToTexture(const TextureToTextureCopyDesc& desc) = 0;
    virtual CommandValidationResult uploadBuffer(const UploadBufferDesc& desc) = 0;
    virtual CommandValidationResult uploadTexture(const UploadTextureDesc& desc) = 0;
    virtual CommandValidationResult readbackTexture(const ReadbackTextureDesc& desc) = 0;
    virtual CommandValidationResult end() = 0;
};

class ARP_API RenderBundleEncoder {
public:
    virtual ~RenderBundleEncoder() = default;

    /// WebGPU records the pipeline into the native render bundle. Other
    /// backends may defer pipeline consumption to executeRenderBundle, but
    /// must preserve the same topology/attachment compatibility contract.
    virtual CommandValidationResult setPipeline(const RenderPipeline&)
    {
        return {false, "RHI render-bundle pipeline lowering is unavailable", 0};
    }

    /// Native render bundles capture vertex-buffer state. The consuming pass
    /// remains responsible for any state not exposed by this L2 interface.
    virtual CommandValidationResult bindVertexBuffer(
        std::uint32_t, const GpuBuffer&, std::uint64_t = 0)
    {
        return {false, "RHI render-bundle vertex-buffer lowering is unavailable", 0};
    }

    virtual CommandValidationResult bindDescriptorSet(
        std::uint32_t, const DescriptorSet&, const std::uint32_t* = nullptr, std::uint32_t = 0)
    {
        return {false, "RHI render-bundle descriptor-set lowering is unavailable", 0};
    }

    /// Bind the dedicated tessellation-factor buffer for a PatchList bundle.
    /// The consuming render pass still validates the complete Metal
    /// tessellation ABI against its bound pipeline. Native ICB lowerings may
    /// reject a rebind after the first draw because the factor state is
    /// applied to the consuming encoder as one immutable bundle precondition.
    virtual CommandValidationResult bindTessellationFactorBuffer(
        const GpuBuffer&, std::uint64_t, std::uint32_t)
    {
        return {false, "RHI render-bundle tessellation factor buffer lowering is unavailable", 0};
    }

    /// Index-buffer state belongs to the bundle because native indirect
    /// commands capture the index source while the bundle is recorded.
    virtual CommandValidationResult bindIndexBuffer(
        const GpuBuffer&, IndexFormat, std::uint64_t = 0)
    {
        return {false, "RHI render-bundle index-buffer lowering is unavailable", 0};
    }

    virtual CommandValidationResult drawIndexed(std::uint32_t indexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstIndex,
        std::int32_t vertexOffset,
        std::uint32_t firstInstance) = 0;
    virtual CommandValidationResult draw(std::uint32_t vertexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstVertex,
        std::uint32_t firstInstance) = 0;
    virtual arp::core::UniquePtr<RenderBundle> finish() = 0;
};

class ARP_API SecondaryCommandEncoder {
public:
    virtual ~SecondaryCommandEncoder() = default;

    /// Bind the dedicated tessellation-factor buffer for a PatchList
    /// secondary command buffer. The binding is retained by the command
    /// buffer and validated again by the consuming render pass.
    virtual CommandValidationResult bindTessellationFactorBuffer(
        const GpuBuffer&, std::uint64_t, std::uint32_t)
    {
        return {false, "RHI secondary tessellation factor buffer lowering is unavailable", 0};
    }

    // AtomicFullBlockPush profile requires non-null data.
    // The data=nullptr default is removed; all production callers
    // must provide a valid push-constant buffer.
    virtual CommandValidationResult pushConstants(ShaderStage stages,
        std::uint32_t offsetBytes,
        std::uint32_t sizeBytes,
        const void* data) = 0;

    virtual CommandValidationResult pushConstants(const PushConstantsDesc& desc)
    {
        return pushConstants(desc.stages, desc.offsetBytes, desc.sizeBytes, desc.data);
    }

    /// Indexed secondary draws carry their index source in the secondary
    /// payload. A consuming render pass still supplies pipeline and vertex
    /// descriptor state.
    virtual CommandValidationResult bindIndexBuffer(
        const GpuBuffer& buffer, IndexFormat format, std::uint64_t offsetBytes) = 0;
    virtual CommandValidationResult drawIndexed(std::uint32_t indexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstIndex,
        std::int32_t vertexOffset,
        std::uint32_t firstInstance) = 0;
    virtual CommandValidationResult draw(std::uint32_t vertexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstVertex,
        std::uint32_t firstInstance) = 0;
    virtual arp::core::UniquePtr<SecondaryCommandBuffer> finish() = 0;
};

class ARP_API CommandEncoder {
public:
    virtual ~CommandEncoder() = default;

    virtual void* queryInterface(std::string_view) noexcept { return nullptr; }

    // Opaque recording identity for graph/pass ownership checks. Backends that
    // do not expose indexed passes keep zero and fail that adapter closed.
    virtual std::uint64_t recordingIdentity() const noexcept { return 0; }

    // PreparedGraphicsPassFaultScope/1: the next Render pass consumes this
    // command-owned intent before attachment work. Unsupported backends fail closed.
    virtual CommandValidationResult prepareBindlessGraphicsPass(
        std::shared_ptr<const BindlessPreparedGraphicsBinding>, const BindlessAccessDeclarations&)
    {
        return {false,
            "RHI-BINDLESS-GRAPHICS-PASS-SCOPE-UNAVAILABLE",
            lastValidation().recordedCommandCount};
    }

    virtual ResourceBarrierResult transition(
        GpuBuffer& buffer, ResourceState before, ResourceState after) = 0;
    virtual ResourceBarrierResult transition(
        GpuTexture& texture, ResourceState before, ResourceState after) = 0;
    virtual ResourceBarrierResult transition(GpuTexture& texture,
        const TextureSubresourceRange& range,
        ResourceState before,
        ResourceState after) = 0;
    virtual ResourceBarrierResult transition(
        GpuBuffer& buffer, const ResourceBarrierDesc& desc) = 0;
    virtual ResourceBarrierResult transition(
        GpuTexture& texture, const ResourceBarrierDesc& desc) = 0;

    virtual ResourceBarrierResult memoryBarrier(
        GpuBuffer& buffer, const ResourceMemoryBarrierDesc& desc)
    {
        ResourceBarrierResult result;
        result.resource = buffer.id();
        result.before = buffer.state();
        result.after = buffer.state();
        result.sourceStages = desc.sourceStages;
        result.destinationStages = desc.destinationStages;
        result.sourceAccess = desc.sourceAccess;
        result.destinationAccess = desc.destinationAccess;
        result.stageAccessAware = true;
        result.diagnostics = "RHI-RESOURCE-MEMORY-BARRIER-UNSUPPORTED";
        return result;
    }

    virtual ResourceBarrierResult memoryBarrier(
        GpuTexture& texture, const ResourceMemoryBarrierDesc& desc)
    {
        ResourceBarrierResult result;
        result.resource = texture.id();
        result.before = texture.state();
        result.after = texture.state();
        result.sourceStages = desc.sourceStages;
        result.destinationStages = desc.destinationStages;
        result.sourceAccess = desc.sourceAccess;
        result.destinationAccess = desc.destinationAccess;
        result.stageAccessAware = true;
        result.diagnostics = "RHI-RESOURCE-MEMORY-BARRIER-UNSUPPORTED";
        return result;
    }

    virtual ResourceBarrierResult aliasBarrier(const AliasingBarrierDesc& desc)
    {
        ResourceBarrierResult result;
        result.resource = desc.afterResource;
        result.sourceStages = desc.sourceStages;
        result.destinationStages = desc.destinationStages;
        result.sourceAccess = desc.sourceAccess;
        result.destinationAccess = desc.destinationAccess;
        result.stageAccessAware = true;
        result.diagnostics = "RHI-ALIAS-BARRIER-UNSUPPORTED";
        return result;
    }

    virtual arp::core::UniquePtr<RenderPassEncoder> beginRenderPass(const RenderPassDesc& desc) = 0;
    virtual arp::core::UniquePtr<ComputePassEncoder> beginComputePass(
        const ComputePassDesc& desc) = 0;
    virtual arp::core::UniquePtr<TransferPassEncoder> beginTransferPass(
        const TransferPassDesc& desc) = 0;
    virtual arp::core::UniquePtr<CommandBuffer> finish() = 0;
    virtual CommandValidationResult lastValidation() const = 0;
    virtual void pushDebugGroup(std::string_view label) = 0;
    virtual void popDebugGroup() = 0;
    virtual void insertDebugMarker(std::string_view label) = 0;
};

} // namespace arp::rhi
