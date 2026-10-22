#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/semantics/fallback_plan.h"

#include <cstdint>
#include <string>

namespace arp::rhi {

enum class WebGpuRuntimeProfileStatus { ContractComplete, PlatformValidated, DiagnosticFailure };

struct WebGpuRuntimeProbe {
    bool browserRuntime = false;
    bool canvasSurface = false;
    bool nativeDevice = false;
    bool computeDispatch = false;
    bool storageTextureBinding = false;
    bool timestampQuery = false;
    bool bgra8UnormStorage = false;
    bool rgba16FloatStorage = false;
    bool rgba16FloatBlendable = false;
    bool rg16FloatStorage = false;
    bool textureCompressionBc = false;
    // Storage buffers (WGPUBufferUsage_STORAGE) are core in WebGPU, but the
    // profile still records an independent native binding probe so capability
    // promotion is based on the resource contract being consumed. This must
    // not be inferred from storageTextureBinding: the two capabilities are
    // independent and DDGI Phase 3 consumes an SSBO, not a storage texture.
    bool storageBuffer = false;
    // Complete DDGI software-BVH binding/pipeline probe. This is intentionally
    // separate from the generic storage-buffer probe.
    bool sceneGeometryRayQuery = false;
    // Zero means the browser/device probe did not publish this limit. The
    // profile builder must fail closed; these fields must not carry desktop
    // guesses that can be mistaken for queried WebGPU facts.
    std::uint32_t maxTexture2DSize = 0;
    std::uint32_t maxColorAttachments = 0;
    std::uint32_t maxDescriptorBindings = 0;
    std::uint32_t maxSampledTexturesPerShaderStage = 0;
    std::uint32_t maxSamplersPerShaderStage = 0;
    std::uint32_t maxUniformBuffersPerShaderStage = 0;
    std::uint32_t maxStorageTexturesPerShaderStage = 0;
    std::uint32_t maxBindGroups = 0;
    std::uint32_t uniformBufferOffsetAlignment = 0;
    std::uint32_t storageBufferOffsetAlignment = 0;
    std::uint32_t maxUniformBufferBindingSize = 0;
    std::uint32_t maxStorageBufferBindingSize = 0;
    std::uint32_t maxComputeInvocationsPerWorkgroup = 0;
    std::uint32_t maxComputeWorkgroupSizeX = 0;
    std::uint32_t maxComputeWorkgroupSizeY = 0;
    std::uint32_t maxComputeWorkgroupStorageSize = 0;
    std::uint32_t maxStorageBuffersPerShaderStage = 0;
};

struct WebGpuRequiredRuntimeFeatures {
    bool sampledTextures = false;
    bool cpuToGpuUploads = false;
    bool depthTestedIndexedDraw = false;
    bool canvasSwapchain = false;
};

struct WebGpuOptionalRuntimeFeatures {
    bool computeDispatch = false;
    bool storageTextureBinding = false;
    bool timestampQuery = false;
    bool bgra8UnormStorage = false;
    bool rgba16FloatStorage = false;
    bool rgba16FloatBlendable = false;
    bool rg16FloatStorage = false;
    bool textureCompressionBc = false;
    // Core in WebGPU, promoted only from the independent storage-buffer probe.
    bool storageBuffer = false;
    // Engine-level software BVH ray-query lowering. This is not a browser
    // native ray-query feature: it is published only after the backend-owned
    // compute and storage-buffer probes have both succeeded, then consumed as
    // one immutable profile fact by the semantic bridge and device.
    bool sceneGeometryRayQuery = false;
};

struct WebGpuRuntimeLimits {
    std::uint32_t maxTexture2DSize = 0;
    std::uint32_t maxColorAttachments = 0;
    std::uint32_t maxDescriptorBindings = 0;
    std::uint32_t maxSampledTexturesPerShaderStage = 0;
    std::uint32_t maxSamplersPerShaderStage = 0;
    std::uint32_t maxUniformBuffersPerShaderStage = 0;
    std::uint32_t maxStorageTexturesPerShaderStage = 0;
    std::uint32_t maxBindGroups = 0;
    std::uint32_t uniformBufferOffsetAlignment = 0;
    std::uint32_t storageBufferOffsetAlignment = 0;
    std::uint32_t maxUniformBufferBindingSize = 0;
    std::uint32_t maxStorageBufferBindingSize = 0;
    std::uint32_t maxComputeInvocationsPerWorkgroup = 0;
    std::uint32_t maxComputeWorkgroupSizeX = 0;
    std::uint32_t maxComputeWorkgroupSizeY = 0;
    std::uint32_t maxComputeWorkgroupStorageSize = 0;
    std::uint32_t maxStorageBuffersPerShaderStage = 0;
};

struct WebGpuRuntimeProfile {
    Backend backend = Backend::WebGPU;
    WebGpuRuntimeProfileStatus status = WebGpuRuntimeProfileStatus::ContractComplete;
    std::pmr::string shaderTarget = "WGSL";
    bool browserRuntime = false;
    bool canvasSurface = false;
    bool descriptorTerminologyPreserved = true;
    bool descriptorsLowerThroughBackendOwnedGroups = true;
    bool pushConstantsEmulated = true;
    FallbackMode softSecondaryCommandFallback = FallbackMode::DiagnosticFailure;
    std::uint32_t logicalFrameSlots = 2;
    std::uint32_t requestedSwapchainImageCount = 2;
    bool nativeHandleExposedToUpperLayer = false;
    WebGpuRequiredRuntimeFeatures requiredFeatures;
    WebGpuOptionalRuntimeFeatures optionalFeatures;
    WebGpuRuntimeLimits limits;
    std::pmr::string diagnostics;
};

struct WebGpuComputeStoragePromotionReport {
    bool success = false;
    bool requiresComputeDispatch = false;
    bool requiresStorageTextureBinding = false;
    bool requiresStorageBuffer = false;
    bool computePromotionValidated = false;
    bool storageTexturePromotionValidated = false;
    bool storageBufferPromotionValidated = false;
    bool correctnessPreserved = true;
    bool deterministicDiagnosticFailure = false;
    std::uint32_t diagnosticFailureCount = 0;
    std::pmr::string diagnostics;
};

enum class WebGpuSceneRuntimeSupportLabel { NotRequested, Supported, DiagnosticFailure };

struct WebGpuSceneRuntimeWorkloadDesc {
    bool requested = false;
    std::uint32_t drawCommandCount = 0;
    std::uint32_t materialResourceCount = 0;
    std::uint32_t descriptorSetCount = 0;
    std::uint32_t pushConstantSizeBytes = 0;
    bool oneCompatibleWorldUiPass = true;
    bool nativeHandleExposedToUpperLayer = false;
};

struct WebGpuSceneRuntimeSupportReport {
    bool success = false;
    WebGpuSceneRuntimeSupportLabel supportLabel = WebGpuSceneRuntimeSupportLabel::NotRequested;
    bool descriptorCapacityValidated = false;
    bool pushConstantEmulationValidated = false;
    bool onePassValidated = false;
    bool nativeHandleExposedToUpperLayer = false;
    std::pmr::string diagnostics;
};

class WebGpuComputeStoragePromotionValidator {
public:
    WebGpuComputeStoragePromotionReport evaluate(
        const WebGpuRuntimeProfile& profile, const EngineFeatureRequirementSet& requirements) const;
};

WebGpuRuntimeProfile buildWebGpuRuntimeProfile(const WebGpuRuntimeProbe& probe);

EngineFeatureRequirementSet webGpuDefaultFrameFeatureRequirements(
    const WebGpuRuntimeProfile& profile);

EngineFeatureRequirementSet webGpuComputeFrameFeatureRequirements(
    const WebGpuRuntimeProfile& profile);

EngineFallbackPlan buildWebGpuRuntimeFallbackPlan(
    const WebGpuRuntimeProfile& profile, const EngineFeatureRequirementSet& requirements);

WebGpuComputeStoragePromotionReport validateWebGpuComputeStoragePromotion(
    const WebGpuRuntimeProfile& profile, const EngineFeatureRequirementSet& requirements);

WebGpuSceneRuntimeSupportReport validateWebGpuSceneRuntimeWorkload(
    const WebGpuRuntimeProfile& profile, const WebGpuSceneRuntimeWorkloadDesc& workload);

} // namespace arp::rhi
