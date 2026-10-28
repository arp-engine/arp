#include "rhi/semantics/webgpu_runtime_profile.h"

#include "core/diagnostics/diagnostic_builder.h"
#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/fallback_planner.h"

namespace arp::rhi {

namespace {

EngineFeatureRequirement makeRequirement(EngineFeature feature,
    FeatureCorrectnessImpact correctnessImpact,
    const std::pmr::string& reason)
{
    EngineFeatureRequirement requirement;
    requirement.feature = feature;
    requirement.correctnessImpact = correctnessImpact;
    requirement.reason = reason;
    return requirement;
}

void recordPromotionFailure(
    WebGpuComputeStoragePromotionReport& report, const std::pmr::string& diagnostics)
{
    ++report.diagnosticFailureCount;
    arp::core::appendDiagnostic(report.diagnostics, diagnostics);
}

FallbackMode chooseWebGpuFeatureMode(const WebGpuRuntimeProfile& profile, EngineFeature feature)
{
    switch (feature) {
    case EngineFeature::PushConstants:
        return profile.pushConstantsEmulated ? FallbackMode::Emulated
                                             : FallbackMode::DiagnosticFailure;
    case EngineFeature::SecondaryCommandBuffer:
        return profile.softSecondaryCommandFallback;
    case EngineFeature::DescriptorTable:
        // No executable binding-array ABI is available in the current browser
        // runtime. BindlessCross scalar expansion is gated independently.
        return FallbackMode::DiagnosticFailure;
    case EngineFeature::TransientAliasing:
        return FallbackMode::Degraded;
    case EngineFeature::TimelineSync:
        return FallbackMode::DiagnosticFailure;
    case EngineFeature::ComputeDispatch:
        return profile.optionalFeatures.computeDispatch ? FallbackMode::Native
                                                        : FallbackMode::DiagnosticFailure;
    case EngineFeature::StorageTextureBinding:
        return profile.optionalFeatures.storageTextureBinding ? FallbackMode::Native
                                                              : FallbackMode::DiagnosticFailure;
    case EngineFeature::NativeSync:
        return FallbackMode::DiagnosticFailure;
    case EngineFeature::SubpassInputAttachment:
        // WebGPU has no native input attachment API. The compiler/RHI-owned
        // separate-pass lowering is executable once the browser device has
        // been platform-validated, and is reported as degraded so upper
        // layers select the ordered sampled-texture route.
        return profile.status == WebGpuRuntimeProfileStatus::PlatformValidated
            ? FallbackMode::Degraded
            : FallbackMode::DiagnosticFailure;
    case EngineFeature::SubpassDepthInputAttachment:
        return profile.status == WebGpuRuntimeProfileStatus::PlatformValidated
            ? FallbackMode::Degraded
            : FallbackMode::DiagnosticFailure;
    case EngineFeature::Rg16FloatStorageTexture:
        return profile.optionalFeatures.rg16FloatStorage ? FallbackMode::Native
                                                         : FallbackMode::DiagnosticFailure;
    case EngineFeature::Rgba16FloatStorageTexture:
        return profile.optionalFeatures.rgba16FloatStorage ? FallbackMode::Native
                                                           : FallbackMode::DiagnosticFailure;
    case EngineFeature::StorageBufferBinding:
        return profile.optionalFeatures.storageBuffer ? FallbackMode::Native
                                                      : FallbackMode::DiagnosticFailure;
    case EngineFeature::SceneGeometryRayQuery:
        return profile.optionalFeatures.sceneGeometryRayQuery ? FallbackMode::Native
                                                              : FallbackMode::DiagnosticFailure;
    case EngineFeature::IndirectDraw:
        // The command encoder has a native lowering, but a desktop
        // ContractComplete profile does not own a browser device. Do not
        // publish a native capability until the platform probe has validated
        // the executable WebGPU runtime.
        return profile.status == WebGpuRuntimeProfileStatus::PlatformValidated
            ? FallbackMode::Native
            : FallbackMode::DiagnosticFailure;
    case EngineFeature::MultiDrawIndirectCount:
        return FallbackMode::DiagnosticFailure;
    }
    return FallbackMode::DiagnosticFailure;
}

} // namespace

WebGpuComputeStoragePromotionReport WebGpuComputeStoragePromotionValidator::evaluate(
    const WebGpuRuntimeProfile& profile, const EngineFeatureRequirementSet& requirements) const
{
    WebGpuComputeStoragePromotionReport report;
    report.computePromotionValidated = profile.optionalFeatures.computeDispatch;
    report.storageTexturePromotionValidated = profile.optionalFeatures.storageTextureBinding;
    report.storageBufferPromotionValidated = profile.optionalFeatures.storageBuffer;

    for (const auto& requirement : requirements.features) {
        switch (requirement.feature) {
        case EngineFeature::ComputeDispatch:
            report.requiresComputeDispatch = true;
            if (!profile.optionalFeatures.computeDispatch) {
                recordPromotionFailure(report,
                    "compute dispatch requires native WebGPU runtime promotion to preserve "
                    "correctness");
            }
            break;
        case EngineFeature::StorageTextureBinding:
            report.requiresStorageTextureBinding = true;
            if (!profile.optionalFeatures.storageTextureBinding) {
                recordPromotionFailure(report,
                    "storage texture binding requires native WebGPU runtime promotion to preserve "
                    "correctness");
            }
            break;
        case EngineFeature::StorageBufferBinding:
            report.requiresStorageBuffer = true;
            if (!profile.optionalFeatures.storageBuffer) {
                recordPromotionFailure(report,
                    "storage buffer binding requires an independent native WebGPU buffer probe to "
                    "preserve correctness");
            }
            break;
        case EngineFeature::PushConstants:
        case EngineFeature::SecondaryCommandBuffer:
        case EngineFeature::DescriptorTable:
        case EngineFeature::TransientAliasing:
        case EngineFeature::TimelineSync:
        case EngineFeature::NativeSync:
        case EngineFeature::SubpassInputAttachment:
        case EngineFeature::SubpassDepthInputAttachment:
        case EngineFeature::Rg16FloatStorageTexture:
        case EngineFeature::Rgba16FloatStorageTexture:
        case EngineFeature::SceneGeometryRayQuery:
        case EngineFeature::IndirectDraw:
        case EngineFeature::MultiDrawIndirectCount:
            break;
        }
    }

    if ((report.requiresComputeDispatch || report.requiresStorageTextureBinding
            || report.requiresStorageBuffer)
        && profile.status == WebGpuRuntimeProfileStatus::DiagnosticFailure) {
        recordPromotionFailure(
            report, "compute/storage promotion requires a valid WebGPU runtime profile");
    }

    report.correctnessPreserved = report.diagnosticFailureCount == 0;
    report.deterministicDiagnosticFailure =
        report.diagnosticFailureCount > 0 && !report.diagnostics.empty();
    report.success = report.correctnessPreserved;
    return report;
}

WebGpuRuntimeProfile buildWebGpuRuntimeProfile(const WebGpuRuntimeProbe& probe)
{
    WebGpuRuntimeProfile profile;
    profile.backend = Backend::WebGPU;
    profile.status = probe.nativeDevice ? WebGpuRuntimeProfileStatus::PlatformValidated
                                        : WebGpuRuntimeProfileStatus::ContractComplete;
    profile.browserRuntime = probe.browserRuntime;
    profile.canvasSurface = probe.canvasSurface;
    profile.descriptorTerminologyPreserved = true;
    profile.descriptorsLowerThroughBackendOwnedGroups = true;
    profile.pushConstantsEmulated = true;
    // WebGPU has no native L2 secondary-command API. The fallback is an
    // explicit allocator-owned CPU replay path; callers must opt into it and
    // it must never be published as native secondary support.
    profile.softSecondaryCommandFallback = FallbackMode::CpuReplay;
    profile.logicalFrameSlots = 2;
    profile.requestedSwapchainImageCount = 2;
    profile.nativeHandleExposedToUpperLayer = false;
    profile.requiredFeatures.sampledTextures = true;
    profile.requiredFeatures.cpuToGpuUploads = true;
    profile.requiredFeatures.depthTestedIndexedDraw = true;
    profile.requiredFeatures.canvasSwapchain = probe.canvasSurface;
    profile.optionalFeatures.computeDispatch = probe.nativeDevice && probe.computeDispatch;
    profile.optionalFeatures.storageTextureBinding =
        probe.nativeDevice && probe.storageTextureBinding;
    profile.optionalFeatures.timestampQuery = probe.nativeDevice && probe.timestampQuery;
    profile.optionalFeatures.bgra8UnormStorage =
        profile.optionalFeatures.storageTextureBinding && probe.bgra8UnormStorage;
    profile.optionalFeatures.rgba16FloatStorage =
        profile.optionalFeatures.storageTextureBinding && probe.rgba16FloatStorage;
    profile.optionalFeatures.rg16FloatStorage =
        profile.optionalFeatures.storageTextureBinding && probe.rg16FloatStorage;
    profile.optionalFeatures.textureCompressionBc =
        probe.nativeDevice && probe.textureCompressionBc;
    // Storage buffers and storage textures are separate RHI capabilities.
    // Even though storage buffers are core in WebGPU, keep promotion tied to
    // the backend-owned buffer/bind-group probe so the profile reflects the
    // resource contract actually available to the device.
    profile.optionalFeatures.storageBuffer = probe.nativeDevice && probe.storageBuffer;
    profile.optionalFeatures.sceneGeometryRayQuery = profile.optionalFeatures.computeDispatch
        && profile.optionalFeatures.storageBuffer && probe.sceneGeometryRayQuery;
    profile.optionalFeatures.rgba16FloatBlendable =
        probe.nativeDevice && probe.rgba16FloatBlendable;
    profile.limits.maxTexture2DSize = probe.maxTexture2DSize;
    profile.limits.maxColorAttachments = probe.maxColorAttachments;
    profile.limits.maxDescriptorBindings = probe.maxDescriptorBindings;
    profile.limits.maxSampledTexturesPerShaderStage = probe.maxSampledTexturesPerShaderStage;
    profile.limits.maxSamplersPerShaderStage = probe.maxSamplersPerShaderStage;
    profile.limits.maxUniformBuffersPerShaderStage = probe.maxUniformBuffersPerShaderStage;
    profile.limits.maxStorageTexturesPerShaderStage = probe.maxStorageTexturesPerShaderStage;
    profile.limits.maxBindGroups = probe.maxBindGroups;
    profile.limits.uniformBufferOffsetAlignment = probe.uniformBufferOffsetAlignment;
    profile.limits.storageBufferOffsetAlignment = probe.storageBufferOffsetAlignment;
    profile.limits.maxUniformBufferBindingSize = probe.maxUniformBufferBindingSize;
    profile.limits.maxStorageBufferBindingSize = probe.maxStorageBufferBindingSize;
    profile.limits.maxComputeInvocationsPerWorkgroup = probe.maxComputeInvocationsPerWorkgroup;
    profile.limits.maxComputeWorkgroupSizeX = probe.maxComputeWorkgroupSizeX;
    profile.limits.maxComputeWorkgroupSizeY = probe.maxComputeWorkgroupSizeY;
    profile.limits.maxComputeWorkgroupStorageSize = probe.maxComputeWorkgroupStorageSize;
    profile.limits.maxStorageBuffersPerShaderStage = probe.maxStorageBuffersPerShaderStage;
    profile.limits.maxComputeWorkgroupSizeZ = probe.maxComputeWorkgroupSizeZ;
    profile.limits.maxComputeWorkgroupsPerDimension = probe.maxComputeWorkgroupsPerDimension;

    if (!probe.canvasSurface) {
        profile.status = WebGpuRuntimeProfileStatus::DiagnosticFailure;
        arp::core::appendDiagnostic(
            profile.diagnostics, "WebGPU runtime profile requires a browser canvas surface");
    }
    if (probe.maxTexture2DSize == 0) {
        profile.status = WebGpuRuntimeProfileStatus::DiagnosticFailure;
        arp::core::appendDiagnostic(
            profile.diagnostics, "WebGPU runtime profile requires maxTexture2DSize");
    }
    if (probe.maxColorAttachments == 0) {
        profile.status = WebGpuRuntimeProfileStatus::DiagnosticFailure;
        arp::core::appendDiagnostic(
            profile.diagnostics, "WebGPU runtime profile requires color attachment support");
    }
    if (probe.maxDescriptorBindings == 0) {
        profile.status = WebGpuRuntimeProfileStatus::DiagnosticFailure;
        arp::core::appendDiagnostic(
            profile.diagnostics, "WebGPU runtime profile requires descriptor binding capacity");
    }
    if (probe.maxSampledTexturesPerShaderStage == 0) {
        profile.status = WebGpuRuntimeProfileStatus::DiagnosticFailure;
        arp::core::appendDiagnostic(
            profile.diagnostics, "WebGPU runtime profile requires sampled-texture stage capacity");
    }
    if (probe.uniformBufferOffsetAlignment == 0) {
        profile.status = WebGpuRuntimeProfileStatus::DiagnosticFailure;
        arp::core::appendDiagnostic(
            profile.diagnostics, "WebGPU runtime profile requires uniform buffer offset alignment");
    }
    if (probe.maxBindGroups != 0u && probe.maxBindGroups < 4u) {
        profile.status = WebGpuRuntimeProfileStatus::DiagnosticFailure;
        arp::core::appendDiagnostic(profile.diagnostics,
            "WebGPU runtime profile reports maxBindGroups below the specification minimum");
    }
    if (probe.storageBuffer && probe.storageBufferOffsetAlignment == 0) {
        profile.status = WebGpuRuntimeProfileStatus::DiagnosticFailure;
        arp::core::appendDiagnostic(
            profile.diagnostics, "WebGPU runtime profile requires storage buffer offset alignment");
    }
    if (probe.maxUniformBufferBindingSize == 0) {
        profile.status = WebGpuRuntimeProfileStatus::DiagnosticFailure;
        arp::core::appendDiagnostic(
            profile.diagnostics, "WebGPU runtime profile requires maxUniformBufferBindingSize");
    }
    if (probe.uniformBufferOffsetAlignment != 0
        && (probe.uniformBufferOffsetAlignment & (probe.uniformBufferOffsetAlignment - 1u)) != 0u) {
        profile.status = WebGpuRuntimeProfileStatus::DiagnosticFailure;
        arp::core::appendDiagnostic(profile.diagnostics,
            "WebGPU runtime profile requires power-of-two uniform buffer offset alignment");
    }
    if (probe.storageBuffer && probe.storageBufferOffsetAlignment != 0
        && (probe.storageBufferOffsetAlignment & (probe.storageBufferOffsetAlignment - 1u)) != 0u) {
        profile.status = WebGpuRuntimeProfileStatus::DiagnosticFailure;
        arp::core::appendDiagnostic(profile.diagnostics,
            "WebGPU runtime profile requires power-of-two storage buffer offset alignment");
    }
    if (probe.storageBuffer && probe.maxStorageBufferBindingSize == 0) {
        profile.status = WebGpuRuntimeProfileStatus::DiagnosticFailure;
        arp::core::appendDiagnostic(
            profile.diagnostics, "WebGPU runtime profile requires maxStorageBufferBindingSize");
    }
    if (probe.maxUniformBufferBindingSize != 0
        && probe.maxUniformBufferBindingSize < kL2AtomicPushConstantLimitBytes) {
        profile.status = WebGpuRuntimeProfileStatus::DiagnosticFailure;
        arp::core::appendDiagnostic(profile.diagnostics,
            "WebGPU runtime profile uniform binding limit is below the L2 push-constant contract");
    }

    return profile;
}

EngineFeatureRequirementSet webGpuDefaultFrameFeatureRequirements(
    const WebGpuRuntimeProfile& profile)
{
    EngineFeatureRequirementSet requirements;
    requirements.logicalFrameSlots = profile.logicalFrameSlots;
    requirements.maxFeatureDecisions = 4;
    requirements.features.reserve(1);
    requirements.features.push_back(makeRequirement(EngineFeature::PushConstants,
        FeatureCorrectnessImpact::PreservesCorrectness,
        "WebGPU uses backend-owned upload ring for push constants"));
    return requirements;
}

EngineFeatureRequirementSet webGpuComputeFrameFeatureRequirements(
    const WebGpuRuntimeProfile& profile)
{
    auto requirements = webGpuDefaultFrameFeatureRequirements(profile);
    requirements.maxFeatureDecisions = 7;
    requirements.features.push_back(makeRequirement(EngineFeature::ComputeDispatch,
        FeatureCorrectnessImpact::PreservesCorrectness,
        "WebGPU compute dispatch requires validated native runtime support to preserve "
        "correctness"));
    requirements.features.push_back(makeRequirement(EngineFeature::StorageTextureBinding,
        FeatureCorrectnessImpact::PreservesCorrectness,
        "WebGPU storage texture binding requires validated runtime feature support to preserve "
        "correctness"));
    requirements.features.push_back(makeRequirement(EngineFeature::StorageBufferBinding,
        FeatureCorrectnessImpact::PreservesCorrectness,
        "WebGPU storage buffer binding requires an independent validated runtime probe to preserve "
        "correctness"));
    requirements.features.push_back(makeRequirement(EngineFeature::DescriptorTable,
        FeatureCorrectnessImpact::PerformanceOnly,
        "WebGPU descriptor table requires an executable binding-array ABI"));
    return requirements;
}

EngineFallbackPlan buildWebGpuRuntimeFallbackPlan(
    const WebGpuRuntimeProfile& profile, const EngineFeatureRequirementSet& requirements)
{
    EngineFallbackPlanner planner({
        Backend::WebGPU,
        requirements.logicalFrameSlots,
        requirements.maxFeatureDecisions == 0
            ? static_cast<std::uint32_t>(requirements.features.size())
            : requirements.maxFeatureDecisions,
    });

    for (const auto& requirement : requirements.features) {
        planner.recordFeatureDecision({
            requirement.feature,
            chooseWebGpuFeatureMode(profile, requirement.feature),
            requirement.correctnessImpact,
            requirement.reason,
        });
    }

    return planner.buildPlan();
}

WebGpuComputeStoragePromotionReport validateWebGpuComputeStoragePromotion(
    const WebGpuRuntimeProfile& profile, const EngineFeatureRequirementSet& requirements)
{
    WebGpuComputeStoragePromotionValidator validator;
    return validator.evaluate(profile, requirements);
}

WebGpuSceneRuntimeSupportReport validateWebGpuSceneRuntimeWorkload(
    const WebGpuRuntimeProfile& profile, const WebGpuSceneRuntimeWorkloadDesc& workload)
{
    WebGpuSceneRuntimeSupportReport report;
    if (!workload.requested) {
        report.supportLabel = WebGpuSceneRuntimeSupportLabel::NotRequested;
        report.success = true;
        return report;
    }

    report.nativeHandleExposedToUpperLayer =
        profile.nativeHandleExposedToUpperLayer || workload.nativeHandleExposedToUpperLayer;
    report.descriptorCapacityValidated = workload.descriptorSetCount > 0
        && workload.descriptorSetCount <= profile.limits.maxDescriptorBindings;
    report.pushConstantEmulationValidated =
        workload.pushConstantSizeBytes > 0 && profile.pushConstantsEmulated;
    report.onePassValidated = workload.oneCompatibleWorldUiPass;

    if (profile.status == WebGpuRuntimeProfileStatus::DiagnosticFailure) {
        arp::core::appendDiagnostic(
            report.diagnostics, "WebGPU scene-runtime workload requires a valid runtime profile");
    }
    if (workload.drawCommandCount == 0 || workload.materialResourceCount == 0) {
        arp::core::appendDiagnostic(report.diagnostics,
            "WebGPU scene-runtime workload requires draw commands and material resources");
    }
    if (!report.descriptorCapacityValidated) {
        arp::core::appendDiagnostic(
            report.diagnostics, "WebGPU scene-runtime workload exceeds descriptor capacity");
    }
    if (!report.pushConstantEmulationValidated) {
        arp::core::appendDiagnostic(report.diagnostics,
            "WebGPU scene-runtime workload requires push constant emulation metadata");
    }
    if (!report.onePassValidated) {
        arp::core::appendDiagnostic(report.diagnostics,
            "WebGPU scene-runtime workload requires compatible one-pass intent");
    }
    if (report.nativeHandleExposedToUpperLayer) {
        arp::core::appendDiagnostic(report.diagnostics,
            "WebGPU scene-runtime workload rejects upper-layer handle exposure");
    }

    report.success = report.diagnostics.empty();
    report.supportLabel = report.success ? WebGpuSceneRuntimeSupportLabel::Supported
                                         : WebGpuSceneRuntimeSupportLabel::DiagnosticFailure;
    return report;
}

} // namespace arp::rhi
