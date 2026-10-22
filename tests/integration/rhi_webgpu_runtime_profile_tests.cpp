#include "core/memory/cpu_allocator.h"
#include "rhi/semantics/webgpu_runtime_profile.h"

#include <cassert>
#include <string>

int main()
{
    using namespace arp::rhi;

    WebGpuRuntimeProbe browserProbe;
    browserProbe.browserRuntime = true;
    browserProbe.canvasSurface = true;
    browserProbe.maxTexture2DSize = 4096;
    browserProbe.maxColorAttachments = 1;
    browserProbe.maxDescriptorBindings = 32;
    browserProbe.maxSampledTexturesPerShaderStage = 16;
    browserProbe.uniformBufferOffsetAlignment = 256;
    browserProbe.storageBufferOffsetAlignment = 256;
    browserProbe.maxUniformBufferBindingSize = 65536;
    browserProbe.maxStorageBufferBindingSize = 134217728;

    const auto browserProfile = buildWebGpuRuntimeProfile(browserProbe);
    assert(browserProfile.backend == Backend::WebGPU);
    assert(browserProfile.status == WebGpuRuntimeProfileStatus::ContractComplete);
    assert(browserProfile.shaderTarget == std::pmr::string("WGSL"));
    assert(browserProfile.browserRuntime);
    assert(browserProfile.canvasSurface);
    assert(browserProfile.descriptorTerminologyPreserved);
    assert(browserProfile.descriptorsLowerThroughBackendOwnedGroups);
    assert(browserProfile.pushConstantsEmulated);
    assert(browserProfile.softSecondaryCommandFallback == FallbackMode::CpuReplay);
    assert(browserProfile.logicalFrameSlots == 2u);
    assert(browserProfile.requestedSwapchainImageCount == 2u);
    assert(!browserProfile.nativeHandleExposedToUpperLayer);
    assert(browserProfile.limits.maxTexture2DSize == 4096u);
    assert(browserProfile.limits.maxColorAttachments == 1u);
    assert(browserProfile.limits.maxDescriptorBindings == 32u);
    assert(!browserProfile.optionalFeatures.computeDispatch);
    assert(!browserProfile.optionalFeatures.storageTextureBinding);
    assert(!browserProfile.optionalFeatures.sceneGeometryRayQuery);
    assert(browserProfile.requiredFeatures.sampledTextures);
    assert(browserProfile.requiredFeatures.cpuToGpuUploads);
    assert(browserProfile.requiredFeatures.depthTestedIndexedDraw);

    const auto defaultRequirements = webGpuDefaultFrameFeatureRequirements(browserProfile);
    assert(defaultRequirements.logicalFrameSlots == 2u);
    assert(defaultRequirements.features.size() == 1u);
    assert(defaultRequirements.features[0].feature == EngineFeature::PushConstants);

    const auto fallbackPlan = buildWebGpuRuntimeFallbackPlan(browserProfile, defaultRequirements);
    assert(fallbackPlan.success);
    assert(fallbackPlan.backend == Backend::WebGPU);
    assert(fallbackPlan.emulatedFeatureCount == 1u);
    assert(fallbackPlan.cpuReplayFeatureCount == 0u);
    assert(!fallbackPlan.upperLayerVisibleFallbacks);
    assert(fallbackPlan.backendFallbacksHiddenFromUpperLayer);

    EngineFeatureRequirementSet indirectRequirements;
    indirectRequirements.maxFeatureDecisions = 1u;
    indirectRequirements.features.push_back({EngineFeature::IndirectDraw,
        FeatureCorrectnessImpact::PreservesCorrectness,
        "fixed-count indirect draw"});
    const auto unpromotedIndirectPlan =
        buildWebGpuRuntimeFallbackPlan(browserProfile, indirectRequirements);
    assert(!unpromotedIndirectPlan.success);
    assert(unpromotedIndirectPlan.diagnosticFailureCount == 1u);

    WebGpuRuntimeProbe promotedProbe = browserProbe;
    promotedProbe.nativeDevice = true;
    promotedProbe.computeDispatch = true;
    promotedProbe.storageTextureBinding = true;
    promotedProbe.storageBuffer = true;
    promotedProbe.sceneGeometryRayQuery = true;
    promotedProbe.bgra8UnormStorage = true;
    promotedProbe.rgba16FloatStorage = true;
    promotedProbe.maxTexture2DSize = 8192;
    promotedProbe.maxColorAttachments = 4;
    promotedProbe.maxDescriptorBindings = 64;
    promotedProbe.maxSampledTexturesPerShaderStage = 16;
    promotedProbe.uniformBufferOffsetAlignment = 256;
    promotedProbe.storageBufferOffsetAlignment = 256;
    promotedProbe.maxUniformBufferBindingSize = 65536;
    promotedProbe.maxStorageBufferBindingSize = 134217728;

    const auto promotedProfile = buildWebGpuRuntimeProfile(promotedProbe);
    assert(promotedProfile.status == WebGpuRuntimeProfileStatus::PlatformValidated);
    assert(promotedProfile.optionalFeatures.computeDispatch);
    assert(promotedProfile.optionalFeatures.storageTextureBinding);
    assert(promotedProfile.optionalFeatures.storageBuffer);
    assert(promotedProfile.optionalFeatures.sceneGeometryRayQuery);
    assert(promotedProfile.optionalFeatures.bgra8UnormStorage);
    assert(promotedProfile.optionalFeatures.rgba16FloatStorage);
    assert(promotedProfile.limits.maxTexture2DSize == 8192u);
    assert(promotedProfile.limits.maxColorAttachments == 4u);
    assert(promotedProfile.limits.maxDescriptorBindings == 64u);
    const auto promotedIndirectPlan =
        buildWebGpuRuntimeFallbackPlan(promotedProfile, indirectRequirements);
    assert(promotedIndirectPlan.success);
    assert(promotedIndirectPlan.nativeFeatureCount == 1u);

    EngineFeatureRequirementSet sceneRayRequirements;
    sceneRayRequirements.maxFeatureDecisions = 1u;
    sceneRayRequirements.features.push_back({EngineFeature::SceneGeometryRayQuery,
        FeatureCorrectnessImpact::PreservesCorrectness,
        "DDGI software BVH scene tracing"});
    const auto promotedSceneRayPlan =
        buildWebGpuRuntimeFallbackPlan(promotedProfile, sceneRayRequirements);
    assert(promotedSceneRayPlan.success);
    assert(promotedSceneRayPlan.nativeFeatureCount == 1u);

    const auto computeRequirements = webGpuComputeFrameFeatureRequirements(promotedProfile);
    assert(computeRequirements.features.size() == 5u);
    assert(computeRequirements.features[1].feature == EngineFeature::ComputeDispatch);
    assert(computeRequirements.features[2].feature == EngineFeature::StorageTextureBinding);
    assert(computeRequirements.features[3].feature == EngineFeature::StorageBufferBinding);
    assert(computeRequirements.features[4].feature == EngineFeature::DescriptorTable);

    const auto promotedPromotionReport =
        validateWebGpuComputeStoragePromotion(promotedProfile, computeRequirements);
    assert(promotedPromotionReport.success);
    assert(promotedPromotionReport.requiresComputeDispatch);
    assert(promotedPromotionReport.requiresStorageTextureBinding);
    assert(promotedPromotionReport.requiresStorageBuffer);
    assert(promotedPromotionReport.computePromotionValidated);
    assert(promotedPromotionReport.storageTexturePromotionValidated);
    assert(promotedPromotionReport.correctnessPreserved);
    assert(!promotedPromotionReport.deterministicDiagnosticFailure);
    assert(promotedPromotionReport.diagnosticFailureCount == 0u);

    const auto computePlan = buildWebGpuRuntimeFallbackPlan(promotedProfile, computeRequirements);
    assert(!computePlan.success);
    assert(computePlan.nativeFeatureCount == 3u);
    assert(computePlan.degradedFeatureCount == 0u);
    assert(computePlan.diagnosticFailureCount == 1u);
    assert(computePlan.telemetryRequired);
    assert(computePlan.diagnostics.find("descriptor table") != std::pmr::string::npos);

    // Storage-buffer binding is an independent RHI capability. It may be
    // available even when the device does not promote storage textures.
    WebGpuRuntimeProbe independentStorageProbe = browserProbe;
    independentStorageProbe.nativeDevice = true;
    independentStorageProbe.storageBuffer = true;
    const auto independentStorageProfile = buildWebGpuRuntimeProfile(independentStorageProbe);
    assert(independentStorageProfile.optionalFeatures.storageBuffer);
    assert(!independentStorageProfile.optionalFeatures.storageTextureBinding);
    assert(!independentStorageProfile.optionalFeatures.sceneGeometryRayQuery);

    EngineFeatureRequirementSet storageBufferRequirements = defaultRequirements;
    storageBufferRequirements.maxFeatureDecisions = 2u;
    storageBufferRequirements.features.clear();
    storageBufferRequirements.features.push_back({EngineFeature::StorageBufferBinding,
        FeatureCorrectnessImpact::PreservesCorrectness,
        "storage-buffer binding contract"});
    const auto storageBufferPromotion =
        validateWebGpuComputeStoragePromotion(independentStorageProfile, storageBufferRequirements);
    assert(storageBufferPromotion.success);
    assert(storageBufferPromotion.requiresStorageBuffer);
    assert(storageBufferPromotion.storageBufferPromotionValidated);
    assert(storageBufferPromotion.diagnosticFailureCount == 0u);

    WebGpuRuntimeProbe missingStorageProbe = browserProbe;
    missingStorageProbe.nativeDevice = true;
    const auto missingStorageProfile = buildWebGpuRuntimeProfile(missingStorageProbe);
    const auto missingStorageBufferPromotion =
        validateWebGpuComputeStoragePromotion(missingStorageProfile, storageBufferRequirements);
    assert(!missingStorageBufferPromotion.success);
    assert(missingStorageBufferPromotion.requiresStorageBuffer);
    assert(!missingStorageBufferPromotion.storageBufferPromotionValidated);
    assert(missingStorageBufferPromotion.diagnosticFailureCount == 1u);
    assert(
        missingStorageBufferPromotion.diagnostics.find("storage buffer") != std::pmr::string::npos);

    WebGpuRuntimeProbe unqueriedStorageProbe = browserProbe;
    unqueriedStorageProbe.storageTextureBinding = true;
    unqueriedStorageProbe.bgra8UnormStorage = true;
    const auto unqueriedStorageProfile = buildWebGpuRuntimeProfile(unqueriedStorageProbe);
    assert(unqueriedStorageProfile.status == WebGpuRuntimeProfileStatus::ContractComplete);
    assert(!unqueriedStorageProfile.optionalFeatures.storageTextureBinding);
    assert(!unqueriedStorageProfile.optionalFeatures.bgra8UnormStorage);
    assert(!unqueriedStorageProfile.optionalFeatures.rgba16FloatStorage);

    WebGpuRuntimeProbe partialPromotionProbe = browserProbe;
    partialPromotionProbe.nativeDevice = true;
    partialPromotionProbe.computeDispatch = true;
    const auto partialPromotionProfile = buildWebGpuRuntimeProfile(partialPromotionProbe);
    assert(!partialPromotionProfile.optionalFeatures.sceneGeometryRayQuery);
    const auto partialPromotionRequirements =
        webGpuComputeFrameFeatureRequirements(partialPromotionProfile);
    const auto partialPromotionReport = validateWebGpuComputeStoragePromotion(
        partialPromotionProfile, partialPromotionRequirements);
    assert(!partialPromotionReport.success);
    assert(partialPromotionReport.requiresComputeDispatch);
    assert(partialPromotionReport.requiresStorageTextureBinding);
    assert(partialPromotionReport.requiresStorageBuffer);
    assert(partialPromotionReport.computePromotionValidated);
    assert(!partialPromotionReport.storageTexturePromotionValidated);
    assert(!partialPromotionReport.storageBufferPromotionValidated);
    assert(!partialPromotionReport.correctnessPreserved);
    assert(partialPromotionReport.deterministicDiagnosticFailure);
    assert(partialPromotionReport.diagnosticFailureCount == 2u);
    assert(partialPromotionReport.diagnostics.find("storage texture binding")
        != std::pmr::string::npos);
    assert(partialPromotionReport.diagnostics.find("correctness") != std::pmr::string::npos);

    const auto partialPromotionPlan =
        buildWebGpuRuntimeFallbackPlan(partialPromotionProfile, partialPromotionRequirements);
    assert(!partialPromotionPlan.success);
    assert(partialPromotionPlan.nativeFeatureCount == 1u);
    assert(partialPromotionPlan.diagnosticFailureCount == 3u);
    assert(
        partialPromotionPlan.diagnostics.find("storage texture binding") != std::pmr::string::npos);
    assert(partialPromotionPlan.diagnostics.find("correctness") != std::pmr::string::npos);

    auto correctnessSensitiveRequirements = computeRequirements;
    correctnessSensitiveRequirements.features[4].correctnessImpact =
        FeatureCorrectnessImpact::ChangesCorrectness;
    const auto correctnessPlan =
        buildWebGpuRuntimeFallbackPlan(promotedProfile, correctnessSensitiveRequirements);
    assert(!correctnessPlan.success);
    assert(correctnessPlan.diagnosticFailureCount == 1u);
    assert(correctnessPlan.diagnostics.find("correctness") != std::pmr::string::npos);

    const auto unsupportedComputeRequirements =
        webGpuComputeFrameFeatureRequirements(browserProfile);
    const auto unsupportedComputePlan =
        buildWebGpuRuntimeFallbackPlan(browserProfile, unsupportedComputeRequirements);
    assert(!unsupportedComputePlan.success);
    assert(unsupportedComputePlan.diagnosticFailureCount == 4u);
    assert(unsupportedComputePlan.diagnostics.find("compute dispatch") != std::pmr::string::npos);
    assert(unsupportedComputePlan.diagnostics.find("storage texture") != std::pmr::string::npos);
    assert(unsupportedComputePlan.diagnostics.find("correctness") != std::pmr::string::npos);

    const auto unsupportedPromotionReport =
        validateWebGpuComputeStoragePromotion(browserProfile, unsupportedComputeRequirements);
    assert(!unsupportedPromotionReport.success);
    assert(!unsupportedPromotionReport.computePromotionValidated);
    assert(!unsupportedPromotionReport.storageTexturePromotionValidated);
    assert(!unsupportedPromotionReport.storageBufferPromotionValidated);
    assert(!unsupportedPromotionReport.correctnessPreserved);
    assert(unsupportedPromotionReport.deterministicDiagnosticFailure);
    assert(unsupportedPromotionReport.diagnosticFailureCount == 3u);
    assert(
        unsupportedPromotionReport.diagnostics.find("compute dispatch") != std::pmr::string::npos);
    assert(unsupportedPromotionReport.diagnostics.find("storage texture binding")
        != std::pmr::string::npos);
    assert(unsupportedPromotionReport.diagnostics.find("storage buffer binding")
        != std::pmr::string::npos);

    WebGpuRuntimeProbe invalidProbe = browserProbe;
    invalidProbe.canvasSurface = false;
    const auto invalidProfile = buildWebGpuRuntimeProfile(invalidProbe);
    assert(invalidProfile.status == WebGpuRuntimeProfileStatus::DiagnosticFailure);
    assert(invalidProfile.diagnostics.find("canvas") != std::pmr::string::npos);

    auto invalidRequirements = defaultRequirements;
    invalidRequirements.logicalFrameSlots = 3;
    const auto invalidPlan = buildWebGpuRuntimeFallbackPlan(browserProfile, invalidRequirements);
    assert(!invalidPlan.success);
    assert(invalidPlan.diagnostics.find("frame slot") != std::pmr::string::npos);

    return 0;
}
