#include "rhi/semantics/webgpu_runtime_capability_bridge.h"

namespace arp::rhi::semantics {

namespace {

void addFeature(NativeBackendCapabilityContract& contract, NativeBackendFeature feature)
{
    if (!hasNativeFeature(contract, feature)) {
        contract.features.push_back(feature);
    }
}

NativeBackendCapabilityContract makeBaseContract(const rhi::WebGpuRuntimeProfile& profile)
{
    NativeBackendCapabilityContract contract;
    contract.backend = rhi::Backend::WebGPU;
    contract.preservesNativePotential = true;
    contract.flattenedToPortableSubset = false;
    contract.exposesBackendSpecificMemoryModel = false;
    // WGPUDepthStencilState always carries depthBiasClamp in the WebGPU
    // core API.  Leaving the default false makes the generic pipeline
    // lowering mistakenly apply Vulkan's optional-feature rule and reject
    // otherwise valid browser pipelines before they reach WebGPU.
    contract.depthBiasClampSupported = true;
    contract.queriedFromNativeDevice =
        profile.status == rhi::WebGpuRuntimeProfileStatus::PlatformValidated;
    contract.queriedExtendedFeatures = true;
    contract.uniformBufferOffsetAlignmentBytes = profile.limits.uniformBufferOffsetAlignment;
    contract.maxUniformBufferBindingSizeBytes = profile.limits.maxUniformBufferBindingSize;
    addFeature(contract, NativeBackendFeature::RuntimeLimits);
    addFeature(contract, NativeBackendFeature::OptionalRuntimeFeatures);
    return contract;
}

} // namespace

WebGpuRuntimeCapabilityBridgeResult buildWebGpuRuntimeCapabilityContract(
    const rhi::WebGpuRuntimeProfile& profile)
{
    WebGpuRuntimeCapabilityBridgeResult result;
    result.contract = makeBaseContract(profile);
    result.success = profile.status != rhi::WebGpuRuntimeProfileStatus::DiagnosticFailure;
    result.diagnostics = profile.diagnostics;

    if (!result.success) {
        return result;
    }

    if (profile.optionalFeatures.computeDispatch) {
        addFeature(result.contract, NativeBackendFeature::ComputeDispatch);
    }
    if (profile.optionalFeatures.storageTextureBinding) {
        addFeature(result.contract, NativeBackendFeature::StorageTextureBinding);
    }
    if (profile.optionalFeatures.rg16FloatStorage) {
        addFeature(result.contract, NativeBackendFeature::Rg16FloatStorageTexture);
    }
    if (profile.optionalFeatures.rgba16FloatStorage) {
        addFeature(result.contract, NativeBackendFeature::Rgba16FloatStorageTexture);
    }
    if (profile.optionalFeatures.storageBuffer) {
        addFeature(result.contract, NativeBackendFeature::StorageBufferBinding);
    }
    if (profile.optionalFeatures.textureCompressionBc) {
        addFeature(result.contract, NativeBackendFeature::TextureCompressionBc);
    }
    if (profile.optionalFeatures.timestampQuery) {
        addFeature(result.contract, NativeBackendFeature::TimestampQueries);
        result.contract.optionalFeatures.push_back(NativeBackendFeature::TimestampQueries);
    }
    // DDGI scene tracing is a backend-neutral software BVH lowering. The
    // runtime profile owns the result of the backend probe composition; this
    // bridge must forward that single fact instead of re-deriving it.
    if (profile.optionalFeatures.sceneGeometryRayQuery) {
        addFeature(result.contract, NativeBackendFeature::SceneGeometryRayQuery);
    }
    // Platform validation does not prove binding-array execution. The current
    // browser ABI rejects arrays with RHI-WEBGPU-DESCRIPTOR-ARRAY-ABI-UNAVAILABLE.
    // BindlessCross/1 scalar expansion is a separately gated Emulated contract;
    // it must never promote NativeBackendFeature::DescriptorIndexing.
    // drawIndexedIndirect is part of the executable WebGPU command contract.
    // It is separate from GPU-counted indirect draws, which remain
    // DiagnosticFailure because WebGPU core cannot consume a count buffer.
    if (profile.status == rhi::WebGpuRuntimeProfileStatus::PlatformValidated) {
        addFeature(result.contract, NativeBackendFeature::IndirectDraw);
        // WebGPU has no native input attachments, but the compiler/RHI-owned
        // separate-pass lowering is executable after the native device has
        // been validated. Keep this distinct from native subpass support.
        addFeature(result.contract, NativeBackendFeature::SeparatePassSubpassInputAttachment);
        // Native WebGPU render bundles are executable only after the same
        // platform validation that authorizes the imported device. The
        // backend command path retains the native bundle and checks the
        // recording/pass attachment contract; do not advertise this from the
        // backend vocabulary-only contract.
        addFeature(result.contract, NativeBackendFeature::RenderBundles);
        result.contract.runtimeFeaturesQueried = true;
        for (const auto feature : result.contract.features) {
            result.contract.runtimeFeatures.push_back(feature);
        }
    }

    return result;
}

} // namespace arp::rhi::semantics
