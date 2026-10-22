#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/gpu_capabilities.h"
#include "rhi/gpu_common.h"

#include <cstdint>
#include <vector>

namespace arp::rhi::semantics {

enum class NativeBackendFeature {
    PushConstants,
    DescriptorIndexing,
    SecondaryCommandBuffers,
    TimelineSynchronization,
    QueueFamilies,
    ExplicitMemoryControl,
    ArgumentBuffers,
    TileMemoryHints,
    IndirectCommandBuffers,
    RuntimeLimits,
    OptionalRuntimeFeatures,
    ComputeDispatch,
    StorageTextureBinding,
    NativeSync,
    /// Vulkan subpass + input attachment (tile-local attachment read within
    /// a render pass). Native on Vulkan. Metal uses FramebufferFetch instead.
    /// Note: native capability is declared here; the runtime fallback decision
    /// is in chooseFeatureFallback. Vulkan is Native (Phase B). Metal is
    /// promoted only when the framebuffer-fetch probe and shader contract
    /// succeed; WebGPU uses the separate-pass lowering feature below.
    SubpassInputAttachment,
    /// Compiler/RHI-owned separate-pass lowering for logical subpass inputs.
    /// This is distinct from native tile-local input attachments: WebGPU can
    /// execute it by materializing the writer attachment as a sampled texture
    /// and loading the current fragment in an ordered reader pass.
    SeparatePassSubpassInputAttachment,
    /// Native depth input-attachment read. Kept separate from color input
    /// attachments because Vulkan's current render-pass lowering rejects the
    /// depth read path.
    SubpassDepthInputAttachment,
    /// Metal framebuffer fetch ([[color(N)]] / [[depth]] in fragment shaders).
    /// Native on Metal. Vulkan uses SubpassInputAttachment instead. The
    /// The command path is promoted only when the runtime probe and shader
    /// reflection contain the framebuffer-fetch binding contract.
    FramebufferFetch,
    /// Native Metal framebuffer depth fetch. Kept distinct from color fetch;
    /// a color framebuffer-fetch probe cannot authorize depth reads.
    FramebufferFetchDepth,
    /// Per-format storage-texture support for Rg16Float. Vulkan probes
    /// VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT on VK_FORMAT_R16G16_SFLOAT;
    /// WebGPU bridges from its device-side storage-texture runtime probe;
    /// Metal lists this only after its per-format storage-texture probe and
    /// shader package contract succeed. Null lists it as a reference
    /// validator.
    Rg16FloatStorageTexture,
    /// Per-format storage-texture support for Rgba16Float.
    Rgba16FloatStorageTexture,
    /// Storage buffer (SSBO) binding in compute/graphics shaders. Native on
    /// Vulkan, Metal, WebGPU, and Null when the backend's direct descriptor
    /// lowering contract is available.
    StorageBufferBinding,
    /// Scene geometry ray query via software BVH (DDGI Phase 4). Not a native
    /// backend API feature per se - it is an engine-level capability whose
    /// lowering reuses StorageBufferBinding + ComputeDispatch as the substrate
    /// (CPU-built BVH uploaded as two SSBOs and traversed in a compute shader).
    /// Listed Native only on backends where the complete software-BVH lowering
    /// contract is executable. WebGPU promotes it from a dedicated DDGI
    /// node/leaf binding and compute-pipeline probe in addition to its generic
    /// compute and storage-buffer probes; Metal requires its native compute,
    /// descriptor, and shader probe contracts.
    SceneGeometryRayQuery,
    /// Indirect draw (drawIndexedIndirect). Core on Vulkan since 1.0 and
    /// WebGPU, and executable through the native indirect draw call on Metal.
    IndirectDraw,
    /// Indirect draw with GPU-provided draw count (vkCmdDrawIndexedIndirectCount).
    /// Core on Vulkan 1.2+ or 1.1 + VK_KHR_draw_indirect_count. Probed at device
    /// creation via the Vulkan 1.2 feature chain. Metal/WebGPU: DiagnosticFailure.
    DrawIndirectCount,
    /// Sampling BC-family textures (BC1/BC3/BC7). WebGPU promotes this only
    /// after the browser bootstrap has enabled texture-compression-bc;
    /// Vulkan promotes it only after querying every RHI BC format. This is a
    /// resource-format capability, not a shader or draw feature.
    TextureCompressionBc,
    /// Optional native pipeline-statistics counter/readback contract.
    PipelineStatisticsQueries,
    /// Optional native timestamp counter/sample-buffer contract.
    TimestampQueries,
    /// Native render-bundle recording and execution. This is intentionally
    /// separate from secondary command buffers: WebGPU bundles capture
    /// pipeline/resource state at recording time and require an attachment
    /// compatibility contract at execution time.
    RenderBundles
};

enum class SceneRuntimeBackendSupportLabel { NotRequested, Supported, DiagnosticFailure };

struct SceneRuntimeBackendWorkload {
    bool requested = false;
    std::uint32_t drawCommandCount = 0;
    std::uint32_t materialResourceCount = 0;
    std::uint32_t descriptorSetCount = 0;
    std::uint32_t pushConstantSizeBytes = 0;
    std::uint32_t pushConstantCapacityBytes = 0;
    bool oneCompatibleWorldUiPass = true;
    bool backendHandleExposedToUpperLayer = false;
};

struct NativeBackendCapabilityContract {
    rhi::Backend backend = rhi::Backend::Null;
    bool preservesNativePotential = true;
    bool flattenedToPortableSubset = false;
    bool exposesBackendSpecificMemoryModel = false;
    bool queriedFromNativeDevice = false;
    bool queriedExtendedFeatures = false;
    std::uint32_t maxPushConstantSizeBytes = 0;
    // Backend-owned runtime facts consumed by non-native push-constant
    // lowerings. Zero means the runtime has not supplied the fact.
    std::uint32_t uniformBufferOffsetAlignmentBytes = 0;
    std::uint32_t maxUniformBufferBindingSizeBytes = 0;
    // Declared backend-owned upload-ring budget for an equivalent lowering.
    // Zero means that no workload budget has been supplied.
    std::uint32_t pushConstantUploadRingBudgetBytes = 0;
    std::uint32_t queueFamilyCount = 0;
    std::pmr::vector<NativeBackendFeature> features;
    /// Optional runtime resource/counter capabilities. An unsupported
    /// optional feature does not invalidate the device contract.
    std::pmr::vector<NativeBackendFeature> optionalFeatures;
    /// Runtime effective feature set. When true, lowering must intersect the
    /// declared contract with this snapshot instead of consuming declaration
    /// facts alone. The vector is intentionally backend-neutral so the native
    /// lowering contract cannot authorize an unprobed Metal substrate.
    bool runtimeFeaturesQueried = false;
    std::pmr::vector<NativeBackendFeature> runtimeFeatures;

    /// Whether the backend can lower a non-zero `depthBiasClamp` in
    /// `DepthStencilState`. This is a per-device capability fact (not an
    /// opt-in extension): Vulkan requires the `depthBiasClamp` physical
    /// device feature (VUID-00754); Metal and WebGPU guarantee it by spec;
    /// Null reports true so parity tests assert a uniform surface. Pipeline
    /// state lowering consumes this to fail closed when a descriptor uses a
    /// non-zero clamp on a device that cannot honor it, instead of letting
    /// the native path silently drop the value.
    bool depthBiasClampSupported = false;
};

// ── Push Constant Lowering Plan (Track A P2) ────────────────────────────

using PushConstantLoweringOutcome = rhi::PushConstantLoweringOutcome;
using PushConstantLifetimeOwner = rhi::PushConstantLifetimeOwner;
using PushConstantLoweringPolicy = rhi::PushConstantLoweringPolicy;

/// Per-device push-constant lowering plan (generated at device creation).
inline constexpr std::uint32_t kNoPhysicalPushConstantBinding = rhi::kNoPhysicalPushConstantBinding;

struct PushConstantLoweringPlan {
    std::uint32_t contractVersion = 1;
    PushConstantLoweringOutcome outcome = PushConstantLoweringOutcome::DiagnosticFailure;
    /// Physical group/index are meaningful only for a reflection-owned
    /// resource lowering. Native Vulkan/Metal push constants and Null have no
    /// RHI descriptor binding; they must not publish a fabricated 0/0 slot.
    std::uint32_t bindingGroup = kNoPhysicalPushConstantBinding;
    std::uint32_t bindingIndex = kNoPhysicalPushConstantBinding;
    std::uint32_t uploadAlignmentBytes = 0;
    std::uint32_t boundedFrameBudgetBytes = 0;
    PushConstantLifetimeOwner lifetimeOwner = PushConstantLifetimeOwner::Unknown;
    PushConstantLoweringPolicy policy = PushConstantLoweringPolicy::NotApplicable;
    core::pmr::String diagnosticId{&core::defaultCpuMemoryResource()};
    core::pmr::String telemetryLabel{&core::defaultCpuMemoryResource()};
    bool hasExecutablePayload = false;
};

/// Generate the push-constant lowering plan once at device creation.
PushConstantLoweringPlan buildPushConstantLoweringPlan(
    rhi::Backend backend, const NativeBackendCapabilityContract& capabilityContract);

/// Copy the complete selected plan into the backend-neutral device report.
/// Backends must use this helper instead of publishing a partial subset of
/// push-constant metadata.
void applyPushConstantLoweringPlan(
    rhi::DeviceCapabilities& capabilities, const PushConstantLoweringPlan& plan);

NativeBackendCapabilityContract nativeBackendCapabilityContract(rhi::Backend backend);
bool hasNativeFeature(
    const NativeBackendCapabilityContract& contract, NativeBackendFeature feature);

bool hasExecutableNativeFeature(
    const NativeBackendCapabilityContract& contract, NativeBackendFeature feature);

bool hasExecutableOptionalNativeFeature(
    const NativeBackendCapabilityContract& contract, NativeBackendFeature feature);

rhi::FeatureFallbackDecision chooseFeatureFallback(
    const NativeBackendCapabilityContract& contract, rhi::EngineFeature feature);

} // namespace arp::rhi::semantics
