#include "rhi/semantics/native_backend_capabilities.h"

#include "core/cpu_allocator.h"

namespace arp::rhi::semantics {

namespace {

// Native/lowering push-constant size limits per backend.
//
// These are diagnostic facts about what the native API or chosen lowering
// can actually transfer in a single push.  They are NOT the L2 contract
// ceiling (kL2AtomicPushConstantLimitBytes) — the L2 limit is always 256
// and must never be silently expanded to match a larger native limit.
//
// - Vulkan: queried from the physical device at runtime.  The generic
//   backend-only contract below is intentionally not executable until that
//   snapshot has been installed.
// - Metal: setBytes supports up to 4096 bytes per encoder stage.
// - WebGPU: uniform buffer lowering is bounded by minUniformBufferOffsetAlignment
//   and the 256-byte push-constant profile; the lowering uses 256.
// - Null: reference validator, matches the L2 ceiling.
inline constexpr std::uint32_t kMetalPushConstantSetBytesLimit = 4096;
inline constexpr std::uint32_t kWebGpuPushConstantLoweringLimit = 256;
inline constexpr std::uint32_t kNullPushConstantReferenceLimit = 256;

// WebGPU L2 contract reserved resources (Track A P2).
//
// Derivation chain for each constant (from codegen contract → spec minimum):
//
//   kWebGpuPushConstantReservedGroup = 3
//     Source: codegen contract in compile_shaders.py
//       WGSL_PUSH_CONSTANT_SHADERS assigns @group(3) @binding(0).
//       lower_wgsl_out_of_reserved_group moves non-push resources out of
//       group 3; validate_wgsl_reserved_group enforces at-most-one binding.
//
//   kWebGpuPushConstantBindingIndex = 0
//     Source: uniform buffer slot 0 in the push-constant bind group layout.
//
//   kWebGpuPushConstantUploadAlignment = 256
//     Source: WebGPU spec §4.2 (minUniformBufferOffsetAlignment).
//       Required alignment for dynamic uniform buffer offsets used in the
//       B2 explicit-layout lowering path.
//
//   kWebGpuPushConstantBoundedFrameBudget = kL2 * 4096 (= 1 MiB)
//     Source: kL2AtomicPushConstantLimitBytes (256) × estimated maximum
//       draw/dispatch commands per frame (4096).  Conservative ceiling;
//       runtime budget enforcement requires device query + frame analysis
//       (P6 — cross-backend conformance).
//
// The historical group/binding constants above are no longer consumed here.
// The executable plan takes limits from the queried runtime; the physical
// group/binding comes from each generated reflection artifact.
std::pmr::vector<NativeBackendFeature> vulkanNativeFeatures()
{
    // Compute dispatch and storage texture binding are natively implemented on
    // Vulkan (vkCreateComputePipelines / vkCmdDispatch /
    // VK_DESCRIPTOR_TYPE_STORAGE_IMAGE + VK_IMAGE_LAYOUT_GENERAL) and are
    // declared here so chooseFeatureFallback returns Native rather than
    // DiagnosticFailure. The previous omission was a contract/reporting gap,
    // not a missing implementation. See RHI_CAPABILITY_MATRIX.md Phase 1 note.
    return {
        NativeBackendFeature::PushConstants,
        NativeBackendFeature::TimelineSynchronization,
        NativeBackendFeature::QueueFamilies,
        NativeBackendFeature::ExplicitMemoryControl,
        // The public binary GpuSemaphore factory is intentionally fail-closed
        // until it has a queue-bound Vulkan lowering. Fences and timeline
        // semaphores are native, but that is not sufficient to claim the
        // broader NativeSync feature.
        NativeBackendFeature::SubpassInputAttachment,
        NativeBackendFeature::ComputeDispatch,
        NativeBackendFeature::StorageTextureBinding,
        // RG16F storage-image support is a per-format Vulkan capability.  The
        // static entry is vocabulary only; hasExecutableNativeFeature() still
        // requires a queried device contract, and the format probe installs
        // the actual runtime feature when supported.
        NativeBackendFeature::Rg16FloatStorageTexture,
        // SSBO binding is core since Vulkan 1.0 (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        // + VK_DESCRIPTOR_TYPE_STORAGE_BUFFER). No per-device probe needed.
        NativeBackendFeature::StorageBufferBinding,
        // Software BVH ray query (DDGI Phase 4) lowers to SSBO + compute
        // dispatch, both Native on Vulkan. No per-device probe needed.
        NativeBackendFeature::SceneGeometryRayQuery,
        // Indirect draw (vkCmdDrawIndexedIndirect) is core since Vulkan 1.0.
        NativeBackendFeature::IndirectDraw,
    };
}

std::pmr::vector<NativeBackendFeature> metalNativeFeatures()
{
    std::pmr::vector<NativeBackendFeature> features{
        NativeBackendFeature::PushConstants,
        NativeBackendFeature::TileMemoryHints,
        NativeBackendFeature::ComputeDispatch,
        NativeBackendFeature::StorageTextureBinding,
        NativeBackendFeature::StorageBufferBinding,
        NativeBackendFeature::IndirectDraw,
    };
#if defined(__APPLE__)
    features.push_back(NativeBackendFeature::ExplicitMemoryControl);
    // MTLSharedEvent is the executable Metal lowering for both the L2
    // timeline semaphore and native queue synchronization contracts. The
    // runtime device probe still gates the published DeviceCapabilities.
    features.push_back(NativeBackendFeature::TimelineSynchronization);
    features.push_back(NativeBackendFeature::NativeSync);
    features.push_back(NativeBackendFeature::SecondaryCommandBuffers);
    features.push_back(NativeBackendFeature::IndirectCommandBuffers);
    // MTLArgumentEncoder is the executable lowering for homogeneous RHI
    // descriptor tables.  The device capability path still removes this
    // feature when the runtime probe cannot create an encoder.
    features.push_back(NativeBackendFeature::ArgumentBuffers);
    // Framebuffer fetch is a shader/pipeline lowering, not merely a render
    // pass flag.  Device publication is gated by the native probe and by the
    // MetalArgumentBuffer/FramebufferFetch reflection contract.
    features.push_back(NativeBackendFeature::FramebufferFetch);
    features.push_back(NativeBackendFeature::Rg16FloatStorageTexture);
    features.push_back(NativeBackendFeature::Rgba16FloatStorageTexture);
    features.push_back(NativeBackendFeature::SceneGeometryRayQuery);
#endif
    return features;
}

std::pmr::vector<NativeBackendFeature> webGpuNativeFeatures()
{
    return {
        NativeBackendFeature::RuntimeLimits,
        NativeBackendFeature::OptionalRuntimeFeatures,
        // drawIndexedIndirect is core WebGPU and is lowered by the native
        // render-pass command path. GPU-counted indirect draw remains a
        // separate fail-closed capability.
        NativeBackendFeature::IndirectDraw,
    };
}

} // namespace

NativeBackendCapabilityContract nativeBackendCapabilityContract(rhi::Backend backend)
{
    NativeBackendCapabilityContract contract;
    contract.backend = backend;
    contract.preservesNativePotential = true;
    contract.flattenedToPortableSubset = false;

    switch (backend) {
    case rhi::Backend::Null:
        contract.exposesBackendSpecificMemoryModel = false;
        contract.features = {NativeBackendFeature::NativeSync};
        contract.maxPushConstantSizeBytes = kNullPushConstantReferenceLimit;
        // Null is the reference validator: report the same surface as the
        // spec-guaranteed backends so parity tests assert a uniform shape.
        contract.depthBiasClampSupported = true;
        break;
    case rhi::Backend::Vulkan:
        contract.exposesBackendSpecificMemoryModel = true;
        contract.features = vulkanNativeFeatures();
        // No device snapshot is available through this overload.  Keep the
        // declared core vocabulary for diagnostics, but publish no numeric
        // limit and let hasExecutableNativeFeature() fail closed until
        // buildVulkanNativeCapabilityContract() installs queried evidence.
        contract.maxPushConstantSizeBytes = 0;
        contract.depthBiasClampSupported = false;
        break;
    case rhi::Backend::Metal:
        contract.exposesBackendSpecificMemoryModel = true;
        contract.features = metalNativeFeatures();
        // Metal setBytes supports up to 4096 bytes per encoder stage.
        // The L2 contract ceiling (256) is still enforced separately.
        contract.maxPushConstantSizeBytes = kMetalPushConstantSetBytesLimit;
        // Metal guarantees setDepthBias:slopeScale:clamp: on every MTLDevice;
        // the clamp parameter is always honored (no feature gate).
        contract.depthBiasClampSupported = true;
        contract.optionalFeatures = {
            NativeBackendFeature::TextureCompressionBc,
            NativeBackendFeature::TimestampQueries,
            NativeBackendFeature::PipelineStatisticsQueries,
            NativeBackendFeature::FramebufferFetchDepth,
        };
        break;
    case rhi::Backend::WebGL:
        // Probe admission is not an executable native capability contract.
        // No query occurred in this static vocabulary constructor.
        break;
    case rhi::Backend::WebGPU:
        contract.exposesBackendSpecificMemoryModel = false;
        contract.features = webGpuNativeFeatures();
        // WebGPU has no native push constants; the uniform-buffer lowering
        // is bounded by the L2 profile ceiling.
        contract.maxPushConstantSizeBytes = kWebGpuPushConstantLoweringLimit;
        // WebGPU spec guarantees WGPUDepthStencilState.depthBiasClamp on
        // every conformant implementation; no feature gate required.
        contract.depthBiasClampSupported = true;
        break;
    }

    return contract;
}

bool hasNativeFeature(const NativeBackendCapabilityContract& contract, NativeBackendFeature feature)
{
    for (const auto entry : contract.features) {
        if (entry == feature) {
            return true;
        }
    }
    return false;
}

bool hasExecutableNativeFeature(
    const NativeBackendCapabilityContract& contract, NativeBackendFeature feature)
{
    // WebGl1Graphics/1 has no native feature execution payload in P0. Even
    // inconsistent upstream vocabulary/query bits cannot authorize one.
    switch (contract.backend) {
    case rhi::Backend::Null:
    case rhi::Backend::Vulkan:
    case rhi::Backend::Metal:
    case rhi::Backend::WebGPU:
        break;
    case rhi::Backend::WebGL:
    default:
        return false;
    }
    if (!hasNativeFeature(contract, feature)) {
        return false;
    }
    // Vulkan per-device limits and feature bits are not represented by the
    // backend-only contract.  A static list of Vulkan core operations is
    // useful as vocabulary, but it must never authorize a native lowering
    // before the physical-device snapshot has been installed.
    if (contract.backend == rhi::Backend::Vulkan && !contract.queriedFromNativeDevice) {
        return false;
    }
    if (!contract.runtimeFeaturesQueried) {
        return true;
    }
    for (const auto entry : contract.runtimeFeatures) {
        if (entry == feature) {
            return true;
        }
    }
    return false;
}

bool hasExecutableOptionalNativeFeature(
    const NativeBackendCapabilityContract& contract, NativeBackendFeature feature)
{
    switch (contract.backend) {
    case rhi::Backend::Null:
    case rhi::Backend::Vulkan:
    case rhi::Backend::Metal:
    case rhi::Backend::WebGPU:
        break;
    case rhi::Backend::WebGL:
    default:
        return false;
    }
    bool declaredOptional = false;
    for (const auto entry : contract.optionalFeatures) {
        if (entry == feature) {
            declaredOptional = true;
            break;
        }
    }
    if (!declaredOptional || !contract.runtimeFeaturesQueried) {
        return false;
    }
    for (const auto entry : contract.runtimeFeatures) {
        if (entry == feature) {
            return true;
        }
    }
    return false;
}

rhi::FeatureFallbackDecision chooseFeatureFallback(
    const NativeBackendCapabilityContract& contract, rhi::EngineFeature feature)
{
    // Include the full fallback policy inline.
#undef NATIVE
#define NATIVE(f) rhi::FallbackMode::Native
#define EMULATED rhi::FallbackMode::Emulated
#define DEGRADED rhi::FallbackMode::Degraded
#define FAIL rhi::FallbackMode::DiagnosticFailure

    auto d = [&](rhi::FallbackMode m) {
        rhi::FeatureFallbackDecision dec;
        dec.feature = feature;
        dec.backend = contract.backend;
        dec.mode = m;
        dec.upperLayerVisible = false;
        return dec;
    };

    switch (contract.backend) {
    case rhi::Backend::Null:
    case rhi::Backend::Vulkan:
    case rhi::Backend::Metal:
    case rhi::Backend::WebGPU:
        break;
    case rhi::Backend::WebGL:
    default:
        return d(FAIL);
    }
    const bool null = contract.backend == rhi::Backend::Null;
    const bool webgpu = contract.backend == rhi::Backend::WebGPU;
    const bool metal = contract.backend == rhi::Backend::Metal;

    switch (feature) {
    case rhi::EngineFeature::PushConstants:
        return d(null || hasExecutableNativeFeature(contract, NativeBackendFeature::PushConstants)
                ? NATIVE(PushConstants)
                : (webgpu || metal) ? EMULATED
                                    : FAIL);
    case rhi::EngineFeature::SecondaryCommandBuffer:
        return d(null
                    || hasExecutableNativeFeature(
                        contract, NativeBackendFeature::SecondaryCommandBuffers)
                ? NATIVE(SecondaryCommandBuffer)
                : metal  ? FAIL
                : webgpu ? rhi::FallbackMode::CpuReplay
                         : FAIL);
    case rhi::EngineFeature::DescriptorTable:
        return d(
            null || hasExecutableNativeFeature(contract, NativeBackendFeature::DescriptorIndexing)
                    || hasExecutableNativeFeature(contract, NativeBackendFeature::ArgumentBuffers)
                ? NATIVE(DescriptorTable)
                : webgpu ? FAIL
                : (contract.backend == rhi::Backend::Vulkan
                      || hasExecutableNativeFeature(contract, NativeBackendFeature::RuntimeLimits))
                ? DEGRADED
                : FAIL);
    case rhi::EngineFeature::TransientAliasing:
        return d(null
                    || hasExecutableNativeFeature(
                        contract, NativeBackendFeature::ExplicitMemoryControl)
                ? NATIVE(TransientAliasing)
                : hasExecutableNativeFeature(contract, NativeBackendFeature::TileMemoryHints)
                ? DEGRADED
                : webgpu ? DEGRADED
                         : FAIL);
    case rhi::EngineFeature::TimelineSync:
        return d(null
                    || hasExecutableNativeFeature(
                        contract, NativeBackendFeature::TimelineSynchronization)
                ? NATIVE(TimelineSync)
                : FAIL);
    case rhi::EngineFeature::ComputeDispatch:
        return d(null || hasExecutableNativeFeature(contract, NativeBackendFeature::ComputeDispatch)
                ? NATIVE(ComputeDispatch)
                : FAIL);
    case rhi::EngineFeature::StorageTextureBinding:
        return d(null
                    || hasExecutableNativeFeature(
                        contract, NativeBackendFeature::StorageTextureBinding)
                ? NATIVE(StorageTextureBinding)
                : FAIL);
    case rhi::EngineFeature::Rg16FloatStorageTexture:
        // Binary Native/DiagnosticFailure - no degraded fallback. WGSL storage
        // texture format is a compile-time constant, so a runtime format
        // switch is not expressible in a single shader variant.
        return d(null
                    || hasExecutableNativeFeature(
                        contract, NativeBackendFeature::Rg16FloatStorageTexture)
                ? NATIVE(Rg16FloatStorageTexture)
                : FAIL);
    case rhi::EngineFeature::Rgba16FloatStorageTexture:
        return d(null
                    || hasExecutableNativeFeature(
                        contract, NativeBackendFeature::Rgba16FloatStorageTexture)
                ? NATIVE(Rgba16FloatStorageTexture)
                : FAIL);
    case rhi::EngineFeature::StorageBufferBinding:
        // Binary Native/DiagnosticFailure. The Metal direct buffer-binding
        // path is executable only for the explicit set-0 lowering accepted by
        // its command encoder.
        return d(
            null || hasExecutableNativeFeature(contract, NativeBackendFeature::StorageBufferBinding)
                ? NATIVE(StorageBufferBinding)
                : FAIL);
    case rhi::EngineFeature::SceneGeometryRayQuery:
        // Binary Native/DiagnosticFailure. This is an engine-level software
        // BVH lowering, not a native ray-query API. Backend-specific probing
        // and profile composition publish one explicit contract fact; the
        // fallback layer must consume that fact rather than re-deriving it
        // from generic compute/storage capabilities.
        return d(null
                    || hasExecutableNativeFeature(
                        contract, NativeBackendFeature::SceneGeometryRayQuery)
                ? NATIVE(SceneGeometryRayQuery)
                : FAIL);
    case rhi::EngineFeature::NativeSync:
        return d(null || hasExecutableNativeFeature(contract, NativeBackendFeature::NativeSync)
                ? NATIVE(NativeSync)
                : FAIL);
    case rhi::EngineFeature::IndirectDraw:
        // drawIndexedIndirect is core on Vulkan (1.0), WebGPU, and Null. The
        // WebGPU path validates the tracked indirect buffer and records the
        // native command; GPU-counted draw remains separate.
        return d(null || hasExecutableNativeFeature(contract, NativeBackendFeature::IndirectDraw)
                ? NATIVE(IndirectDraw)
                : FAIL);
    case rhi::EngineFeature::MultiDrawIndirectCount:
        // Vulkan uses vkCmdDrawIndexedIndirectCount. Metal authorizes the
        // bounded shader-mask equivalent through its IndirectDraw substrate;
        // the selected pipeline must still carry MetalGpuCountedIndirect/1.
        // WebGPU core cannot consume the GPU count buffer.
        return d(null
                    || hasExecutableNativeFeature(contract, NativeBackendFeature::DrawIndirectCount)
                    || (contract.backend == rhi::Backend::Metal
                        && hasExecutableNativeFeature(contract, NativeBackendFeature::IndirectDraw))
                ? (contract.backend == rhi::Backend::Metal ? EMULATED
                                                           : NATIVE(MultiDrawIndirectCount))
                : FAIL);
    case rhi::EngineFeature::SubpassInputAttachment:
        // Subpass input attachments: the FrameGraph SubpassGraph lowers to
        // either a native multi-subpass render pass (tile-local data sharing)
        // or a degraded path (separate passes with cross-pass sampling).
        //
        // Status:
        //   - Null: NATIVE (multi-subpass execution path). The Null backend
        //     validates subpass structure (attachment index bounds, dependency
        //     subpass bounds) in validateRenderPassDesc and bounds-checks
        //     nextSubpass against the declared subpass count. Contract Complete.
        //   - Vulkan: NATIVE. Phase B landed a real multi-subpass VkRenderPass
        //     creation path (per-subpass SubpassDescription with input
        //     attachment references, VkSubpassDependency with BY_REGION,
        //     bounds-checked nextSubpass). Backend Validated by source/structure
        //     contract test (rhi_vulkan_subpass_contract_tests); RenderDoc
        //     visual validation on a real Vulkan device is a Prototype TODO.
        //   - Metal: native substrate is available only for shader packages
        //     carrying the compiler-owned MetalFramebufferFetch/1 artifact;
        //     packages without that evidence fail deterministically.
        //   - WebGPU: DEGRADED when the platform-validated contract contains
        //     SeparatePassSubpassInputAttachment. The generated artifact and
        //     FrameGraph route then use sampled-texture textureLoad reads in
        //     ordered separate passes; native input attachments remain false.
        if (null
            || hasExecutableNativeFeature(contract, NativeBackendFeature::SubpassInputAttachment)
            || hasExecutableNativeFeature(contract, NativeBackendFeature::FramebufferFetch)) {
            return d(NATIVE(SubpassInputAttachment));
        }
        if (contract.backend == rhi::Backend::WebGPU
            && hasExecutableNativeFeature(
                contract, NativeBackendFeature::SeparatePassSubpassInputAttachment)) {
            return d(DEGRADED);
        }
        return d(FAIL);
    case rhi::EngineFeature::SubpassDepthInputAttachment:
        // Depth input reads are a distinct native contract. Null implements
        // the reference validation path; Vulkan currently fails closed in its
        // native render-pass lowering. WebGPU uses the same explicit ordered
        // separate-pass lowering as color input attachments, but keeps the
        // depth capability distinct so a color texture cannot be rebound as a
        // depth texture by inference.
        if (null
            || hasExecutableNativeFeature(
                contract, NativeBackendFeature::SubpassDepthInputAttachment)
            || hasExecutableNativeFeature(contract, NativeBackendFeature::FramebufferFetchDepth)
            || hasExecutableOptionalNativeFeature(
                contract, NativeBackendFeature::FramebufferFetchDepth)) {
            return d(NATIVE(SubpassDepthInputAttachment));
        }
        if (contract.backend == rhi::Backend::WebGPU
            && hasExecutableNativeFeature(
                contract, NativeBackendFeature::SeparatePassSubpassInputAttachment)) {
            return d(DEGRADED);
        }
        return d(FAIL);
    }
    return d(FAIL);
#undef NATIVE
#undef EMULATED
#undef DEGRADED
#undef FAIL
}

PushConstantLoweringPlan buildPushConstantLoweringPlan(
    rhi::Backend backend, const NativeBackendCapabilityContract& capabilityContract)
{
    PushConstantLoweringPlan plan;
    plan.contractVersion = 1;

    const bool hasNativePushConstants =
        hasExecutableNativeFeature(capabilityContract, NativeBackendFeature::PushConstants);

    switch (backend) {
    case rhi::Backend::WebGL:
        plan.diagnosticId = "RHI-WEBGL1-PUSH-EXECUTION-UNAVAILABLE";
        break;
    case rhi::Backend::Vulkan:
        if (hasNativePushConstants) {
            plan.outcome = PushConstantLoweringOutcome::Native;
            plan.lifetimeOwner = PushConstantLifetimeOwner::NativeCommandBuffer;
            plan.hasExecutablePayload = true;
        } else {
            plan.outcome = PushConstantLoweringOutcome::DiagnosticFailure;
        }
        plan.bindingGroup = kNoPhysicalPushConstantBinding;
        plan.bindingIndex = kNoPhysicalPushConstantBinding;
        plan.uploadAlignmentBytes = 0;
        plan.boundedFrameBudgetBytes = capabilityContract.maxPushConstantSizeBytes;
        plan.diagnosticId = "pc-vulkan-native";
        break;

    case rhi::Backend::Metal:
        plan.policy = PushConstantLoweringPolicy::NotApplicable;
        // Metal's setVertexBytes/setFragmentBytes/setBytes path is the native
        // lowering for the AtomicFullBlockPush contract. The command payload
        // validates the same offset/size/data/stage invariants before issuing
        // the encoder calls.
        plan.outcome = hasNativePushConstants ? PushConstantLoweringOutcome::Native
                                              : PushConstantLoweringOutcome::DiagnosticFailure;
        plan.lifetimeOwner = hasNativePushConstants ? PushConstantLifetimeOwner::NativeRenderEncoder
                                                    : PushConstantLifetimeOwner::Unknown;
        plan.hasExecutablePayload = hasNativePushConstants;
        plan.bindingGroup = kNoPhysicalPushConstantBinding;
        plan.bindingIndex = kNoPhysicalPushConstantBinding;
        plan.uploadAlignmentBytes = 0;
        plan.boundedFrameBudgetBytes = capabilityContract.maxPushConstantSizeBytes;
        plan.diagnosticId =
            hasNativePushConstants ? "pc-metal-native-set-bytes" : "pc-metal-missing-set-bytes";
        break;

    case rhi::Backend::WebGPU:
        // The physical group/binding is owned by the generated reflection
        // artifact and copied into each WebGPU pipeline payload. A device-
        // level plan must not invent a group number. Runtime limits are only
        // executable after the browser bootstrap supplies queried facts.
        plan.policy = PushConstantLoweringPolicy::B2ExplicitLayoutDynamicOffset;
        plan.bindingGroup = kNoPhysicalPushConstantBinding;
        plan.bindingIndex = kNoPhysicalPushConstantBinding;
        plan.uploadAlignmentBytes = capabilityContract.uniformBufferOffsetAlignmentBytes;
        plan.boundedFrameBudgetBytes = capabilityContract.pushConstantUploadRingBudgetBytes;
        plan.hasExecutablePayload = capabilityContract.queriedFromNativeDevice
            && capabilityContract.queriedExtendedFeatures
            && capabilityContract.uniformBufferOffsetAlignmentBytes != 0
            && (capabilityContract.uniformBufferOffsetAlignmentBytes
                   & (capabilityContract.uniformBufferOffsetAlignmentBytes - 1u))
                == 0u
            && capabilityContract.pushConstantUploadRingBudgetBytes
                >= rhi::kL2AtomicPushConstantLimitBytes
            && capabilityContract.maxUniformBufferBindingSizeBytes
                >= rhi::kL2AtomicPushConstantLimitBytes;
        plan.lifetimeOwner = plan.hasExecutablePayload ? PushConstantLifetimeOwner::FrameUploadRing
                                                       : PushConstantLifetimeOwner::Unknown;
        plan.outcome = plan.hasExecutablePayload ? PushConstantLoweringOutcome::EquivalentLowering
                                                 : PushConstantLoweringOutcome::DiagnosticFailure;
        plan.diagnosticId = plan.hasExecutablePayload ? "pc-webgpu-reflection-group-runtime-limits"
                                                      : "pc-webgpu-missing-runtime-limits";
        break;

    case rhi::Backend::Null:
        plan.policy = PushConstantLoweringPolicy::NotApplicable;
        plan.outcome = PushConstantLoweringOutcome::Native;
        plan.lifetimeOwner = PushConstantLifetimeOwner::ReferenceValidator;
        plan.hasExecutablePayload = false; // reference validator, not production
        plan.bindingGroup = kNoPhysicalPushConstantBinding;
        plan.bindingIndex = kNoPhysicalPushConstantBinding;
        plan.uploadAlignmentBytes = 0;
        plan.boundedFrameBudgetBytes = rhi::kL2AtomicPushConstantLimitBytes;
        plan.diagnosticId = "pc-null-reference";
        break;
    }

    const bool selectedExecutableOutcome = plan.outcome == PushConstantLoweringOutcome::Native
        || plan.outcome == PushConstantLoweringOutcome::EquivalentLowering;
    if (selectedExecutableOutcome && plan.lifetimeOwner == PushConstantLifetimeOwner::Unknown) {
        plan.outcome = PushConstantLoweringOutcome::DiagnosticFailure;
        plan.hasExecutablePayload = false;
        plan.diagnosticId = "pc-missing-lifetime-owner";
    }

    // Telemetry label: stable identifier for device reports.
    const char* outcomeStr = "";
    switch (plan.outcome) {
    case PushConstantLoweringOutcome::Native:
        outcomeStr = "Native";
        break;
    case PushConstantLoweringOutcome::EquivalentLowering:
        outcomeStr = "EquivalentLowering";
        break;
    case PushConstantLoweringOutcome::PrototypeLegacy:
        outcomeStr = "PrototypeLegacy";
        break;
    case PushConstantLoweringOutcome::DiagnosticFailure:
        outcomeStr = "DiagnosticFailure";
        break;
    }
    plan.telemetryLabel = plan.diagnosticId;
    plan.telemetryLabel += "[";
    plan.telemetryLabel += outcomeStr;
    plan.telemetryLabel += "]";

    return plan;
}

void applyPushConstantLoweringPlan(
    rhi::DeviceCapabilities& capabilities, const PushConstantLoweringPlan& plan)
{
    capabilities.pushConstantLoweringContractVersion = plan.contractVersion;
    capabilities.pushConstantLoweringOutcome = plan.outcome;
    capabilities.pushConstantLoweringBindingGroup = plan.bindingGroup;
    capabilities.pushConstantLoweringBindingIndex = plan.bindingIndex;
    capabilities.pushConstantLoweringAlignmentBytes = plan.uploadAlignmentBytes;
    capabilities.pushConstantLoweringBoundedFrameBudgetBytes = plan.boundedFrameBudgetBytes;
    capabilities.pushConstantLoweringLifetimeOwner = plan.lifetimeOwner;
    capabilities.pushConstantLoweringPolicy = plan.policy;
    capabilities.pushConstantLoweringDiagnosticId = plan.diagnosticId;
    capabilities.pushConstantLoweringTelemetryLabel = plan.telemetryLabel;
    capabilities.pushConstantLoweringHasExecutablePayload = plan.hasExecutablePayload;
}

} // namespace arp::rhi::semantics
