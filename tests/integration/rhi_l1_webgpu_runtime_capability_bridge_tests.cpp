#include "core/memory/cpu_allocator.h"
#include "rhi/semantics/native_engine_fallback_bridge.h"
#include "rhi/semantics/webgpu_runtime_capability_bridge.h"

#include <cassert>
#include <string>

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi::semantics;

    WebGpuRuntimeProbe browserProbe;
    browserProbe.browserRuntime = true;
    browserProbe.canvasSurface = true;
    browserProbe.maxTexture2DSize = 4096;
    browserProbe.maxColorAttachments = 1;
    browserProbe.maxDescriptorBindings = 32;
    browserProbe.maxSampledTexturesPerShaderStage = 16;
    browserProbe.maxSamplersPerShaderStage = 8;
    browserProbe.maxUniformBuffersPerShaderStage = 6;
    browserProbe.maxStorageTexturesPerShaderStage = 4;
    browserProbe.uniformBufferOffsetAlignment = 256;
    browserProbe.storageBufferOffsetAlignment = 256;
    browserProbe.maxUniformBufferBindingSize = 65536;
    browserProbe.maxStorageBufferBindingSize = 134217728;
    browserProbe.uniformBufferOffsetAlignment = 256;
    browserProbe.storageBufferOffsetAlignment = 256;
    browserProbe.maxUniformBufferBindingSize = 65536;
    browserProbe.maxStorageBufferBindingSize = 134217728;

    const auto browserProfile = buildWebGpuRuntimeProfile(browserProbe);
    assert(browserProfile.limits.maxSamplersPerShaderStage == 8);
    assert(browserProfile.limits.maxUniformBuffersPerShaderStage == 6);
    assert(browserProfile.limits.maxStorageTexturesPerShaderStage == 4);
    const auto browserBridge = buildWebGpuRuntimeCapabilityContract(browserProfile);
    assert(browserBridge.success);
    assert(browserBridge.contract.backend == Backend::WebGPU);
    assert(browserBridge.contract.preservesNativePotential);
    assert(!browserBridge.contract.flattenedToPortableSubset);
    assert(!browserBridge.contract.exposesBackendSpecificMemoryModel);
    assert(browserBridge.contract.queriedFromNativeDevice == false);
    assert(browserBridge.contract.queriedExtendedFeatures == true);
    assert(hasNativeFeature(browserBridge.contract, NativeBackendFeature::RuntimeLimits));
    assert(hasNativeFeature(browserBridge.contract, NativeBackendFeature::OptionalRuntimeFeatures));
    assert(!hasNativeFeature(browserBridge.contract, NativeBackendFeature::ComputeDispatch));
    assert(!hasNativeFeature(browserBridge.contract, NativeBackendFeature::StorageTextureBinding));
    assert(!hasNativeFeature(browserBridge.contract, NativeBackendFeature::IndirectDraw));

    const auto unsupportedComputeRequirements =
        webGpuComputeFrameFeatureRequirements(browserProfile);
    const auto unsupportedPlan =
        buildNativeEngineFallbackPlan(browserBridge.contract, unsupportedComputeRequirements);
    assert(!unsupportedPlan.success);
    assert(unsupportedPlan.diagnosticFailureCount == 4u);
    assert(unsupportedPlan.diagnostics.find("compute dispatch") != std::pmr::string::npos);
    assert(unsupportedPlan.diagnostics.find("storage texture") != std::pmr::string::npos);
    assert(unsupportedPlan.diagnostics.find("storage buffer") != std::pmr::string::npos);

    WebGpuRuntimeProbe unqueriedStorageProbe = browserProbe;
    unqueriedStorageProbe.storageTextureBinding = true;
    unqueriedStorageProbe.bgra8UnormStorage = true;
    const auto unqueriedStorageProfile = buildWebGpuRuntimeProfile(unqueriedStorageProbe);
    const auto unqueriedStorageBridge =
        buildWebGpuRuntimeCapabilityContract(unqueriedStorageProfile);
    assert(unqueriedStorageBridge.success);
    assert(unqueriedStorageBridge.contract.queriedFromNativeDevice == false);
    assert(!hasNativeFeature(
        unqueriedStorageBridge.contract, NativeBackendFeature::StorageTextureBinding));

    WebGpuRuntimeProbe promotedProbe = browserProbe;
    promotedProbe.nativeDevice = true;
    promotedProbe.computeDispatch = true;
    promotedProbe.storageTextureBinding = true;
    promotedProbe.storageBuffer = true;
    promotedProbe.sceneGeometryRayQuery = true;
    promotedProbe.bgra8UnormStorage = true;
    promotedProbe.rg16FloatStorage = true;
    promotedProbe.rgba16FloatStorage = true;
    promotedProbe.textureCompressionBc = true;
    promotedProbe.maxTexture2DSize = 8192;
    promotedProbe.maxColorAttachments = 4;
    promotedProbe.maxDescriptorBindings = 64;
    promotedProbe.maxSampledTexturesPerShaderStage = 16;
    promotedProbe.uniformBufferOffsetAlignment = 256;
    promotedProbe.storageBufferOffsetAlignment = 256;
    promotedProbe.maxUniformBufferBindingSize = 65536;
    promotedProbe.maxStorageBufferBindingSize = 134217728;

    const auto promotedProfile = buildWebGpuRuntimeProfile(promotedProbe);
    const auto promotedBridge = buildWebGpuRuntimeCapabilityContract(promotedProfile);
    assert(promotedBridge.success);
    assert(promotedBridge.contract.backend == Backend::WebGPU);
    assert(promotedBridge.contract.queriedFromNativeDevice);
    assert(promotedBridge.contract.queriedExtendedFeatures);
    assert(!hasNativeFeature(promotedBridge.contract, NativeBackendFeature::DescriptorIndexing));
    assert(chooseFeatureFallback(promotedBridge.contract, EngineFeature::DescriptorTable).mode
        == FallbackMode::DiagnosticFailure);
    assert(hasNativeFeature(promotedBridge.contract, NativeBackendFeature::RuntimeLimits));
    assert(
        hasNativeFeature(promotedBridge.contract, NativeBackendFeature::OptionalRuntimeFeatures));
    assert(hasNativeFeature(promotedBridge.contract, NativeBackendFeature::ComputeDispatch));
    assert(hasNativeFeature(promotedBridge.contract, NativeBackendFeature::StorageTextureBinding));
    assert(hasNativeFeature(promotedBridge.contract, NativeBackendFeature::StorageBufferBinding));
    assert(hasNativeFeature(promotedBridge.contract, NativeBackendFeature::SceneGeometryRayQuery));
    assert(hasNativeFeature(promotedBridge.contract, NativeBackendFeature::IndirectDraw));
    assert(
        hasNativeFeature(promotedBridge.contract, NativeBackendFeature::Rgba16FloatStorageTexture));
    assert(
        hasNativeFeature(promotedBridge.contract, NativeBackendFeature::Rg16FloatStorageTexture));
    assert(hasNativeFeature(promotedBridge.contract, NativeBackendFeature::TextureCompressionBc));

    const auto computeRequirements = webGpuComputeFrameFeatureRequirements(promotedProfile);
    const auto promotedPlan =
        buildNativeEngineFallbackPlan(promotedBridge.contract, computeRequirements);
    // The request explicitly includes a descriptor table. Native compute and
    // storage support do not manufacture a binding-array payload.
    assert(!promotedPlan.success);
    assert(promotedPlan.backend == Backend::WebGPU);
    assert(promotedPlan.nativeFeatureCount == 3u);
    assert(promotedPlan.emulatedFeatureCount == 1u);
    assert(promotedPlan.cpuReplayFeatureCount == 0u);
    assert(promotedPlan.degradedFeatureCount == 0u);
    assert(promotedPlan.diagnosticFailureCount == 1u);
    assert(promotedPlan.telemetryRequired);
    assert(promotedPlan.diagnostics.find("descriptor table") != std::pmr::string::npos);
    assert(!promotedPlan.upperLayerVisibleFallbacks);

    EngineFeatureRequirementSet rg16Requirements;
    rg16Requirements.logicalFrameSlots = 2;
    rg16Requirements.features.push_back({EngineFeature::Rg16FloatStorageTexture,
        FeatureCorrectnessImpact::PreservesCorrectness,
        "WebGPU RG16F storage requires the queried storage-texture format probe"});
    const auto rg16Plan = buildNativeEngineFallbackPlan(promotedBridge.contract, rg16Requirements);
    assert(rg16Plan.success);
    assert(rg16Plan.nativeFeatureCount == 1u);
    assert(rg16Plan.diagnosticFailureCount == 0u);

    WebGpuRuntimeProbe invalidProbe = browserProbe;
    invalidProbe.canvasSurface = false;
    const auto invalidProfile = buildWebGpuRuntimeProfile(invalidProbe);
    const auto invalidBridge = buildWebGpuRuntimeCapabilityContract(invalidProfile);
    assert(!invalidBridge.success);
    assert(invalidBridge.diagnostics.find("canvas") != std::pmr::string::npos);
    assert(!hasNativeFeature(invalidBridge.contract, NativeBackendFeature::ComputeDispatch));

    return 0;
}
