#pragma once

#include "core/cpu_allocator.h"
#include "rhi/frontend/command_retention_budget.h"
#include "rhi/gpu_execution_status.h"
#include "rhi/gpu_texture_format.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace arp::rhi {

/// L2 AtomicFullBlockPush profile ceiling (bytes).
///
/// This is the engine-level L2 contract limit for push-constant size.  It is
/// deliberately separate from the native/lowering limit reported per-backend
/// in NativeBackendCapabilityContract::maxPushConstantSizeBytes.  The native
/// limit may be larger (e.g. Metal setBytes supports 4096) but the L2
/// contract must never be silently expanded to match it — doing so would
/// break the cross-backend portability guarantee.
inline constexpr std::uint32_t kL2AtomicPushConstantLimitBytes = 256;

enum class RhiSeverity {
    Info,
    Warning,
    Error,
    Fatal // handler must not return; default calls std::abort()
};

using RhiErrorCallback = void (*)(RhiSeverity severity, const char* message, void* userData);

class RhiErrorHandler {
public:
    static RhiErrorHandler& get();

    void setCallback(RhiErrorCallback callback, void* userData = nullptr);
    void report(RhiSeverity severity, const char* message);
    [[noreturn]] void fatal(const char* message);

private:
    RhiErrorCallback callback_ = nullptr;
    void* userData_ = nullptr;
    static void defaultReport(RhiSeverity severity, const char* message);
};

enum class Backend { Null = 0, Vulkan = 1, Metal = 2, WebGPU = 3, WebGL = 4 };

enum class ResourceState {
    Undefined,
    CopySrc,
    CopyDst,
    ShaderRead,
    ShaderWrite,
    ColorAttachment,
    DepthAttachment,
    Present
};

struct CoreCapabilities {
    bool graphics = false;
    bool compute = false;
};

enum class CapabilityMode : std::uint8_t {
    Native,
    Equivalent,
    Degraded,
    DiagnosticFailure,
};

struct CommandRetentionCapabilities {
    // Version zero is the approved unavailable lowering, not an opt-in.
    std::uint32_t contractVersion = 0u;
    CapabilityMode mode = CapabilityMode::DiagnosticFailure;
    bool boundedRecordingPayload = false;
    bool completionRetiresOwners = false;
    bool validatorOnly = false;
};

/// Numeric compute/resource facts required by algorithms such as Forward+.
/// Zero is never interpreted as a default: it means the backend did not
/// publish a queried or specification-guaranteed limit and consumers fail
/// closed.
struct ComputeResourceCapabilities {
    CapabilityMode mode = CapabilityMode::DiagnosticFailure;
    bool sampledDepthTexture = false;
    std::uint32_t maxComputeWorkgroupInvocations = 0u;
    std::uint32_t maxComputeWorkgroupSizeX = 0u;
    std::uint32_t maxComputeWorkgroupSizeY = 0u;
    std::uint32_t maxComputeWorkgroupMemoryBytes = 0u;
    std::uint32_t maxStorageBufferBindingsPerStage = 0u;
    std::uint64_t maxStorageBufferBindingSizeBytes = 0u;
};

struct ExplicitCapabilities {
    bool pushConstants = false;
    bool secondaryCommandBuffers = false;
    bool explicitBarriers = false;
};

struct SwapchainCapabilities {
    std::uint32_t requestedImageCount = 2;
    std::uint32_t maxFramesInFlight = 2;
};

struct MemoryCapabilities {
    bool deviceLocal = false;
    bool hostVisible = false;
    bool transientAliasing = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
};

struct RuntimeFeatureCapabilities {
    bool pushConstants = false;
    bool secondaryCommandBuffers = false;
    bool descriptorTable = false;
    bool transientAliasing = false;
    bool timelineSync = false;
    bool computeDispatch = false;
    bool storageTextureBinding = false;
    bool nativeSync = false;
    /// True only when the device can execute and asynchronously resolve the
    /// RHI timing-query lowering plan. It is not inferred from CPU timers.
    bool timestampQueries = false;
    /// True only when the device can execute and asynchronously resolve the
    /// RHI pipeline-statistics query contract. This is intentionally separate
    /// from timestampQueries: a timestamp counter set cannot authorize
    /// pipeline-statistics results.
    bool pipelineStatisticsQueries = false;
    /// Multi-subpass render passes with input attachment (Vulkan) /
    /// framebuffer fetch (Metal). A backend may expose an explicit degraded
    /// lowering through subpassInputAttachmentDegraded.
    bool subpassInputAttachment = false;
    /// True when SubpassGraph may use the backend's explicit degraded
    /// lowering. False means a false subpassInputAttachment capability is a
    /// diagnostic failure, not permission to reinterpret the shader resource.
    bool subpassInputAttachmentDegraded = false;
    /// Native depth input-attachment reads. This is separate from color input
    /// attachments because the WebGPU separate-pass route still requires a
    /// depth-specific WGSL and bind-group contract.
    bool subpassDepthInputAttachment = false;
    /// True when depth input reads use ordered separate-pass lowering.
    bool subpassDepthInputAttachmentDegraded = false;
    /// Per-format storage-texture support for Rg16Float. Binary
    /// Native/DiagnosticFailure - no degraded fallback.
    bool rg16FloatStorageTexture = false;
    /// Per-format storage-texture support for Rgba16Float. WebGPU validates
    /// storage formats independently, so this is not implied by the generic
    /// storage-texture binding capability.
    bool rgba16FloatStorageTexture = false;
    /// Storage buffer (SSBO) binding in compute/graphics shaders. Required by
    /// DDGI Phase 3 probe-state buffer. Binary Native/DiagnosticFailure (see
    /// EngineFeature::StorageBufferBinding docs).
    bool storageBufferBinding = false;
    /// Scene geometry ray query via software BVH. Required by DDGI Phase 4
    /// probe tracing (terrain + static meshes participate in ray intersection
    /// instead of the analytic terrain-only march). Binary
    /// Native/DiagnosticFailure (see EngineFeature::SceneGeometryRayQuery docs).
    bool sceneGeometryRayQuery = false;
    /// Indirect draw via drawIndexedIndirect. Required by P5 GPU-driven
    /// vegetation cull. Native on Vulkan, Metal, WebGPU, and Null. Binary
    /// Native/DiagnosticFailure.
    bool indirectDraw = false;
    /// Indirect draw with GPU-provided draw count. Vulkan uses the native
    /// count command; Metal uses a bounded equivalent only when the selected
    /// vertex shader carries MetalGpuCountedIndirect/1. The capability names
    /// the available substrate, while pipeline validation remains ABI-gated.
    bool multiDrawIndirectCount = false;
    /// Backend-neutral support for sampling/uploading BC-family textures
    /// (BC1/BC3/BC7). Every format in the RHI family must be supported before
    /// this is true. This is a resource capability, not a shader/backend
    /// policy; upper layers must select a decoded fallback when it is false.
    bool textureCompressionBc = false;
    /// R32SampledUpload/1: GpuOnly R32Float 2D texture, backend-owned padded
    /// upload, CopyDst -> ShaderRead, and unfiltered shader texel reads.
    /// Does not promise linear filtering, storage writes or render attachment.
    /// Null is validator-only; Vulkan queries optimal sampled/transfer-dst
    /// format support; Metal probes private shader-readable R32 texture
    /// creation (blit upload is core) and uses the tested staging path.
    /// WebGPU remains false until its upload/read contract is validated.
    /// False must reject consumers of this combination before allocation.
    bool r32FloatSampledUpload = false;
};

enum class QueryType : std::uint32_t { Timestamp = 0, PipelineStatistics = 1 };

/// L2 pipeline-statistics fields. Values are cumulative native counter
/// snapshots at the recorded query point; the RHI does not infer interval
/// deltas or synthesize unsupported fields.
enum class PipelineStatistic : std::uint8_t {
    VertexInvocations = 0,
    PostTessellationVertexInvocations = 1,
    ClipperInvocations = 2,
    ClipperPrimitivesOut = 3,
    FragmentInvocations = 4,
    FragmentsPassed = 5,
    ComputeKernelInvocations = 6,
};

using PipelineStatisticsMask = std::uint32_t;

inline constexpr PipelineStatisticsMask pipelineStatisticBit(PipelineStatistic statistic)
{
    return PipelineStatisticsMask{1u} << static_cast<std::uint32_t>(statistic);
}

inline constexpr PipelineStatisticsMask kAllPipelineStatistics =
    pipelineStatisticBit(PipelineStatistic::VertexInvocations)
    | pipelineStatisticBit(PipelineStatistic::PostTessellationVertexInvocations)
    | pipelineStatisticBit(PipelineStatistic::ClipperInvocations)
    | pipelineStatisticBit(PipelineStatistic::ClipperPrimitivesOut)
    | pipelineStatisticBit(PipelineStatistic::FragmentInvocations)
    | pipelineStatisticBit(PipelineStatistic::FragmentsPassed)
    | pipelineStatisticBit(PipelineStatistic::ComputeKernelInvocations);

struct PipelineStatisticValue {
    PipelineStatistic statistic = PipelineStatistic::VertexInvocations;
    std::uint64_t value = 0;
};

/// Backend-neutral asynchronous pipeline-statistics readback. A result is a
/// cumulative counter snapshot associated with one explicit query index. The
/// backend must retain the query pool until completion and must report
/// Pending/Unsupported instead of returning CPU-side estimates.
enum class PipelineStatisticsAvailability : std::uint8_t {
    Available,
    Unsupported,
    FeatureNotEnabled,
    Pending,
    NotRecorded,
    DiagnosticFailure,
};

struct GpuPipelineStatisticsSample {
    static constexpr std::uint32_t kMaxStatistics = 7;
    PipelineStatisticsAvailability availability = PipelineStatisticsAvailability::Unsupported;
    std::uint64_t completedFrameSerial = 0;
    std::uint32_t queryIndex = 0;
    PipelineStatisticsMask statisticMask = 0;
    std::array<PipelineStatisticValue, kMaxStatistics> values{};
    std::uint32_t valueCount = 0;
    const char* diagnostic = "backend does not expose pipeline-statistics queries";
};

/// Backend-neutral GPU timing result. Values are produced asynchronously and
/// therefore describe the most recently completed frame, never the frame
/// currently being recorded. A backend must state why timing is unavailable
/// instead of substituting CPU submission time.
enum class GpuTimingAvailability : std::uint8_t {
    Available,
    Unsupported,
    FeatureNotEnabled,
    Pending,
    DiagnosticFailure,
};

/// One completed caller-defined scope. scopeId is opaque to RHI: the caller
/// owns its registry and may use any stable ID scheme.
struct GpuTimingScopeSample {
    std::uint64_t scopeId = 0;
    float elapsedMs = 0.0f;
};

/// Backend-neutral asynchronous readback. RHI intentionally has no knowledge
/// of terrain, vegetation, shadows, or any other application vocabulary.
struct GpuTimingSample {
    static constexpr std::uint32_t kMaxScopes = 32;
    GpuTimingAvailability availability = GpuTimingAvailability::Unsupported;
    std::uint64_t completedFrameSerial = 0;
    std::uint32_t scopeCount = 0;
    std::array<GpuTimingScopeSample, kMaxScopes> scopes{};
    const char* diagnostic = "backend does not expose GPU timestamp queries";
};

struct QueryPoolDesc {
    std::pmr::string label;
    QueryType type = QueryType::Timestamp;
    std::uint32_t count = 0;
    /// Optional caller-owned scope mapping for timestamp pairs. The backend
    /// copies it while creating the pool; no pointer survives the call.
    const std::uint64_t* timingScopeIds = nullptr;
    std::uint32_t timingScopeIdCount = 0;
    /// Required for PipelineStatistics pools. The mask is explicit and must
    /// contain only kAllPipelineStatistics bits. It is ignored for Timestamp
    /// pools and must remain zero there.
    PipelineStatisticsMask pipelineStatisticsMask = 0;
};

struct QueryPoolDescriptorValidation {
    bool success = false;
    const char* diagnostics = "RHI query-pool descriptor is invalid";

    // Exact scalar boundary shared by owned metadata and full descriptors.
    static QueryPoolDescriptorValidation fromFields(QueryType type,
        std::uint32_t count,
        bool timingScopeIdsPresent,
        std::uint32_t timingScopeIdCount,
        PipelineStatisticsMask pipelineStatisticsMask) noexcept
    {
        if (count == 0u) {
            return {false, "RHI-QUERY-POOL-COUNT-MUST-BE-NONZERO"};
        }
        if (type == QueryType::Timestamp) {
            if (pipelineStatisticsMask != 0u || count > GpuTimingSample::kMaxScopes * 2u
                || timingScopeIdCount > GpuTimingSample::kMaxScopes
                || (timingScopeIdCount == 0u && timingScopeIdsPresent)
                || (timingScopeIdCount > 0u
                    && (!timingScopeIdsPresent
                        || timingScopeIdCount > std::numeric_limits<std::uint32_t>::max() / 2u
                        || timingScopeIdCount * 2u != count))) {
                return {false, "RHI-TIMESTAMP-QUERY-DESCRIPTOR-INVALID"};
            }
            return {true, ""};
        }
        if (type == QueryType::PipelineStatistics) {
            if (pipelineStatisticsMask == 0u
                || (pipelineStatisticsMask & ~kAllPipelineStatistics) != 0u
                || timingScopeIdCount != 0u || timingScopeIdsPresent) {
                return {false, "RHI-PIPELINE-STATISTICS-DESCRIPTOR-INVALID"};
            }
            return {true, ""};
        }
        return {false, "RHI-QUERY-POOL-TYPE-UNSUPPORTED"};
    }
};

/// Shared L2 query descriptor contract.  Backend factories may add native
/// capability checks, but must not reinterpret or weaken these invariants.
inline QueryPoolDescriptorValidation validateQueryPoolDesc(const QueryPoolDesc& desc) noexcept
{
    return QueryPoolDescriptorValidation::fromFields(desc.type,
        desc.count,
        desc.timingScopeIds != nullptr,
        desc.timingScopeIdCount,
        desc.pipelineStatisticsMask);
}

struct DeviceCapabilities {
    Backend backend = Backend::Null;
    core::pmr::String deviceName{&core::defaultCpuMemoryResource()};
    CoreCapabilities core;
    ComputeResourceCapabilities computeResources;
    ExplicitCapabilities explicitFeatures;
    SwapchainCapabilities swapchain;
    MemoryCapabilities memory;
    RuntimeFeatureCapabilities features;
    CommandRetentionCapabilities commandRetention;

    /// Full planner result, kept separate from boolean feature compatibility
    /// fields so Native/Emulated/CpuReplay remain distinguishable.
    core::pmr::Vector<EngineFeatureExecutionStatus> executionStatuses{
        &core::defaultCpuMemoryResource()};
    /// Query lifecycle status is separate from EngineFeature because query
    /// pools are optional objects with per-pool pending/readback state.
    core::pmr::Vector<QueryExecutionStatus> queryExecutionStatuses{
        &core::defaultCpuMemoryResource()};
    ExecutionStatusTelemetrySummary statusTelemetry;

    // Push-constant lowering plan (Track A P2).
    // Populated once at device creation.  Renderer/game layers read these
    // fields for telemetry and diagnostics without backend branches.
    core::pmr::String pushConstantLoweringDiagnosticId{&core::defaultCpuMemoryResource()};
    core::pmr::String pushConstantLoweringTelemetryLabel{&core::defaultCpuMemoryResource()};
    bool pushConstantLoweringHasExecutablePayload = false;
    std::uint32_t pushConstantLoweringContractVersion = 0;
    PushConstantLoweringOutcome pushConstantLoweringOutcome =
        PushConstantLoweringOutcome::DiagnosticFailure;
    std::uint32_t pushConstantLoweringBindingGroup = kNoPhysicalPushConstantBinding;
    std::uint32_t pushConstantLoweringBindingIndex = kNoPhysicalPushConstantBinding;
    std::uint32_t pushConstantLoweringAlignmentBytes = 0;
    std::uint32_t pushConstantLoweringBoundedFrameBudgetBytes = 0;
    PushConstantLifetimeOwner pushConstantLoweringLifetimeOwner =
        PushConstantLifetimeOwner::Unknown;
    PushConstantLoweringPolicy pushConstantLoweringPolicy =
        PushConstantLoweringPolicy::NotApplicable;
};

struct Extent2D {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

/// Return the backend-neutral scalar used for normalized RGBA8 readback
/// statistics.  The byte order is part of the RHI texture format, not a
/// backend detail: BGRA8 stores B,G,R,A while RGBA8 stores R,G,B,A.  Alpha is
/// intentionally excluded so an opaque presentation target does not dominate
/// the color statistic.
inline double canonicalColorLuminance(TextureFormat format, const std::uint8_t* pixel) noexcept
{
    if (pixel == nullptr)
        return 0.0;
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    if (format == TextureFormat::Bgra8Unorm) {
        blue = static_cast<double>(pixel[0]) / 255.0;
        green = static_cast<double>(pixel[1]) / 255.0;
        red = static_cast<double>(pixel[2]) / 255.0;
    } else if (format == TextureFormat::Rgba8Unorm || format == TextureFormat::Rgba8UnormSrgb) {
        red = static_cast<double>(pixel[0]) / 255.0;
        green = static_cast<double>(pixel[1]) / 255.0;
        blue = static_cast<double>(pixel[2]) / 255.0;
    } else {
        return static_cast<double>(pixel[0]) / 255.0;
    }
    return 0.2126 * red + 0.7152 * green + 0.0722 * blue;
}

/// Bytes per pixel for a given rhi::TextureFormat. Pure function shared by all
/// backends (Vulkan/Metal/WebGPU/Null) and the Vulkan allocator's texture size
/// estimation. Previously duplicated verbatim across 5 sites; consolidated here
/// to satisfy the "no duplicated utility functions in business/backend paths"
/// constraint. Returns 0 for Unknown/unrecognized formats AND for
/// block-compressed formats (BC1/BC3/BC7) -- use bytesPerBlock() for those.
inline std::uint64_t bytesPerPixel(TextureFormat format) noexcept
{
    switch (format) {
    case TextureFormat::Bgra8Unorm:
    case TextureFormat::Rgba8Unorm:
    case TextureFormat::Rgba8UnormSrgb:
    case TextureFormat::R8Unorm:
    case TextureFormat::R32Float:
    case TextureFormat::D32Float:
    case TextureFormat::Depth24Stencil8:
        return format == TextureFormat::R8Unorm ? 1 : 4;
    case TextureFormat::Rgba16Float:
        return 8;
    case TextureFormat::Rg16Float:
        return 4;
    case TextureFormat::Unknown:
    case TextureFormat::Bc1Unorm:
    case TextureFormat::Bc3Unorm:
    case TextureFormat::Bc7Unorm:
    case TextureFormat::Bc7UnormSrgb:
        return 0;
    }
    return 0;
}

/// True for block-compressed formats (BC1/BC3/BC7). These pack a fixed-size
/// block of texels (see blockExtent()) into a fixed byte count (see
/// bytesPerBlock()) rather than storing bytes per texel. All sizing and
/// upload-pitch math must route through the block-aware helpers for these.
inline bool isBlockCompressed(TextureFormat format) noexcept
{
    switch (format) {
    case TextureFormat::Bc1Unorm:
    case TextureFormat::Bc3Unorm:
    case TextureFormat::Bc7Unorm:
    case TextureFormat::Bc7UnormSrgb:
        return true;
    default:
        return false;
    }
}

/// Bytes per 4x4 texel block for a block-compressed format. BC1=8 bytes/block
/// (4:1 over RGBA8), BC3=16 bytes/block (2:1, two BC1 planes + alpha), BC7=16
/// bytes/block (2:1). Returns 0 for non-block-compressed formats -- callers
/// must guard with isBlockCompressed() first.
inline std::uint32_t bytesPerBlock(TextureFormat format) noexcept
{
    switch (format) {
    case TextureFormat::Bc1Unorm:
        return 8u;
    case TextureFormat::Bc3Unorm:
    case TextureFormat::Bc7Unorm:
    case TextureFormat::Bc7UnormSrgb:
        return 16u;
    default:
        return 0u;
    }
}

/// Texel extent of a single block along each axis (BC formats are 4x4).
/// Returns 1 for non-block-compressed formats (one texel per "block").
inline std::uint32_t blockExtent(TextureFormat format) noexcept
{
    return isBlockCompressed(format) ? 4u : 1u;
}

/// Number of blocks along an axis for a given texel count, rounding up to
/// cover partial blocks (a 1x1 BC7 surface still consumes one 4x4 block).
inline std::uint32_t blocksForExtent(std::uint32_t texels, std::uint32_t blockExtent) noexcept
{
    if (blockExtent == 0u)
        return 0u;
    return texels / blockExtent + (texels % blockExtent != 0u ? 1u : 0u);
}

/// Total bytes for a single mip level of a 2D/3D texture. For uncompressed
/// formats this is width*height*depth*bytesPerPixel. For block-compressed
/// formats it is ceil(w/4)*ceil(h/4)*depth*bytesPerBlock. Returns 0 for
/// Unknown/unrecognized formats.
inline std::uint64_t bytesForTextureMip(TextureFormat format,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t depth = 1u) noexcept
{
    if (isBlockCompressed(format)) {
        const auto be = blockExtent(format);
        const auto bp = bytesPerBlock(format);
        if (be == 0u || bp == 0u)
            return 0u;
        const auto bw = blocksForExtent(std::max(width, 1u), be);
        const auto bh = blocksForExtent(std::max(height, 1u), be);
        const auto bd = std::max(depth, 1u);
        return static_cast<std::uint64_t>(bw) * bh * bd * bp;
    }
    const auto bpp = bytesPerPixel(format);
    if (bpp == 0u)
        return 0u;
    return static_cast<std::uint64_t>(std::max(width, 1u)) * std::max(height, 1u)
        * std::max(depth, 1u) * bpp;
}

/// Comparison function used by depth/stencil tests and sampler compare
/// operations. Defined here (gpu_common.h) rather than gpu_pipeline.h so
/// that SamplerDesc in gpu_resources.h can reference it without creating
/// a circular include (gpu_pipeline.h → gpu_descriptors.h → gpu_resources.h).
enum class CompareOp { Never, Less, LessEqual, Equal, Greater, GreaterEqual, NotEqual, Always };

} // namespace arp::rhi
