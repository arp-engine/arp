#include "rhi/gpu_commands.h"
#include "rhi/semantics/fallback_plan.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/native_command_lowering.h"
#include "rhi/semantics/native_engine_fallback_bridge.h"
#include "rhi/semantics/subpass_input_lowering.h"

#include <cassert>

namespace {

arp::rhi::semantics::NativeBackendCapabilityContract makeQueriedVulkanContract()
{
    auto contract = arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Vulkan);
    contract.queriedFromNativeDevice = true;
    contract.queriedExtendedFeatures = true;
    return contract;
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi::semantics;

    // Extension opt-in must never become a base capability merely because a
    // planner recorded it successfully.
    EngineFallbackPlanner planner({Backend::WebGPU, 2, 1});
    const auto recorded = planner.recordFeatureDecision({
        EngineFeature::StorageTextureBinding,
        FallbackMode::ExtensionOptIn,
        FeatureCorrectnessImpact::PreservesCorrectness,
        "requires explicit extension contract",
    });
    assert(recorded.success);
    assert(recorded.explicitOptInRequired);
    const auto plan = planner.buildPlan();
    assert(plan.success);
    assert(plan.upperLayerVisibleFallbacks);
    assert(plan.telemetryRequired);
    DeviceCapabilities caps;
    caps.features.storageTextureBinding = true;
    plan.applyTo(caps);
    assert(!caps.features.storageTextureBinding);

    // NativeSync has no equivalent CPU fallback. Backends without a native
    // implementation must fail capability planning rather than expose a
    // DummyFence as a completion primitive. On Apple, Metal's static
    // vocabulary names the MTLSharedEvent lowering; the runtime device still
    // has to publish the queried feature set before it is executable.
    const auto webGpuSync = chooseFeatureFallback(
        nativeBackendCapabilityContract(Backend::WebGPU), EngineFeature::NativeSync);
    const auto metalSync = chooseFeatureFallback(
        nativeBackendCapabilityContract(Backend::Metal), EngineFeature::NativeSync);
    const auto vulkanSync = chooseFeatureFallback(
        nativeBackendCapabilityContract(Backend::Vulkan), EngineFeature::NativeSync);
    const auto nullSync = chooseFeatureFallback(
        nativeBackendCapabilityContract(Backend::Null), EngineFeature::NativeSync);
    assert(webGpuSync.mode == FallbackMode::DiagnosticFailure);
#if defined(__APPLE__)
    assert(metalSync.mode == FallbackMode::Native);
#else
    assert(metalSync.mode == FallbackMode::DiagnosticFailure);
#endif
    assert(vulkanSync.mode == FallbackMode::DiagnosticFailure);
    assert(nullSync.mode == FallbackMode::Native);

    auto metalUnprobed = nativeBackendCapabilityContract(Backend::Metal);
    metalUnprobed.runtimeFeaturesQueried = true;
    metalUnprobed.runtimeFeatures.clear();
    assert(chooseFeatureFallback(metalUnprobed, EngineFeature::NativeSync).mode
        == FallbackMode::DiagnosticFailure);
    auto metalProbed = nativeBackendCapabilityContract(Backend::Metal);
    metalProbed.runtimeFeaturesQueried = true;
    metalProbed.runtimeFeatures.push_back(NativeBackendFeature::NativeSync);
    assert(chooseFeatureFallback(metalProbed, EngineFeature::NativeSync).mode
        == (hasNativeFeature(metalProbed, NativeBackendFeature::NativeSync)
                ? FallbackMode::Native
                : FallbackMode::DiagnosticFailure));

    // WebGPU completion callbacks are not an L2 timeline lowering, and its
    // lack of placed-resource primitives is a degraded ordinary-allocation
    // policy rather than an executable aliasing implementation.
    const auto webGpuTimeline = chooseFeatureFallback(
        nativeBackendCapabilityContract(Backend::WebGPU), EngineFeature::TimelineSync);
    const auto webGpuAliasing = chooseFeatureFallback(
        nativeBackendCapabilityContract(Backend::WebGPU), EngineFeature::TransientAliasing);
    assert(webGpuTimeline.mode == FallbackMode::DiagnosticFailure);
    assert(webGpuAliasing.mode == FallbackMode::Degraded);

    // Static vocabulary tests policy selection, not native Metal execution.
    // A queried device without the required features must still fail closed.
    const auto vulkanCompute =
        chooseFeatureFallback(makeQueriedVulkanContract(), EngineFeature::ComputeDispatch);
    const auto vulkanStorage =
        chooseFeatureFallback(makeQueriedVulkanContract(), EngineFeature::StorageTextureBinding);
    const auto metalCompute = chooseFeatureFallback(
        nativeBackendCapabilityContract(Backend::Metal), EngineFeature::ComputeDispatch);
    const auto metalStorage = chooseFeatureFallback(
        nativeBackendCapabilityContract(Backend::Metal), EngineFeature::StorageTextureBinding);
    const auto nullCompute = chooseFeatureFallback(
        nativeBackendCapabilityContract(Backend::Null), EngineFeature::ComputeDispatch);
    const auto nullStorage = chooseFeatureFallback(
        nativeBackendCapabilityContract(Backend::Null), EngineFeature::StorageTextureBinding);
    assert(vulkanCompute.mode == FallbackMode::Native);
    assert(vulkanStorage.mode == FallbackMode::Native);
    assert(metalCompute.mode == FallbackMode::Native);
    assert(metalStorage.mode == FallbackMode::Native);
    assert(chooseFeatureFallback(metalUnprobed, EngineFeature::ComputeDispatch).mode
        == FallbackMode::DiagnosticFailure);
    assert(chooseFeatureFallback(metalUnprobed, EngineFeature::StorageTextureBinding).mode
        == FallbackMode::DiagnosticFailure);
    assert(nullCompute.mode == FallbackMode::Native);
    assert(nullStorage.mode == FallbackMode::Native);

    // Phase 4: SceneGeometryRayQuery is an explicit software-BVH lowering
    // contract. Vulkan and Null publish it directly; Metal and an unprobed
    // WebGPU contract fail closed. Generic compute/storage facts alone must
    // not promote the feature, because the DDGI-specific binding/pipeline
    // probe is owned by the WebGPU runtime profile.
    const auto vulkanSceneRay =
        chooseFeatureFallback(makeQueriedVulkanContract(), EngineFeature::SceneGeometryRayQuery);
    const auto metalSceneRay = chooseFeatureFallback(
        nativeBackendCapabilityContract(Backend::Metal), EngineFeature::SceneGeometryRayQuery);
    const auto webGpuSceneRay = chooseFeatureFallback(
        nativeBackendCapabilityContract(Backend::WebGPU), EngineFeature::SceneGeometryRayQuery);
    const auto nullSceneRay = chooseFeatureFallback(
        nativeBackendCapabilityContract(Backend::Null), EngineFeature::SceneGeometryRayQuery);
    assert(vulkanSceneRay.mode == FallbackMode::Native);
#if defined(__APPLE__)
    assert(metalSceneRay.mode == FallbackMode::Native);
#else
    assert(metalSceneRay.mode == FallbackMode::DiagnosticFailure);
#endif
    assert(webGpuSceneRay.mode == FallbackMode::DiagnosticFailure);
    assert(nullSceneRay.mode == FallbackMode::Native);

    auto webGpuUnderlyingOnly = nativeBackendCapabilityContract(Backend::WebGPU);
    webGpuUnderlyingOnly.features.push_back(NativeBackendFeature::ComputeDispatch);
    webGpuUnderlyingOnly.features.push_back(NativeBackendFeature::StorageBufferBinding);
    assert(chooseFeatureFallback(webGpuUnderlyingOnly, EngineFeature::SceneGeometryRayQuery).mode
        == FallbackMode::DiagnosticFailure);
    webGpuUnderlyingOnly.features.push_back(NativeBackendFeature::SceneGeometryRayQuery);
    assert(chooseFeatureFallback(webGpuUnderlyingOnly, EngineFeature::SceneGeometryRayQuery).mode
        == FallbackMode::Native);

    // Subpass support is a code-owned lowering decision. A FrameGraph
    // separate-pass branch is not sufficient to advertise WebGPU support
    // while compiler-owned sampled-texture binding/reflection is incomplete.
    const auto webGpuSubpass = chooseFeatureFallback(
        nativeBackendCapabilityContract(Backend::WebGPU), EngineFeature::SubpassInputAttachment);
    const auto metalSubpass = chooseFeatureFallback(
        nativeBackendCapabilityContract(Backend::Metal), EngineFeature::SubpassInputAttachment);
    const auto vulkanSubpass =
        chooseFeatureFallback(makeQueriedVulkanContract(), EngineFeature::SubpassInputAttachment);
    const auto vulkanDepthSubpass = chooseFeatureFallback(
        makeQueriedVulkanContract(), EngineFeature::SubpassDepthInputAttachment);
    assert(webGpuSubpass.mode == FallbackMode::DiagnosticFailure);
#if defined(__APPLE__)
    assert(metalSubpass.mode == FallbackMode::Native);
#else
    assert(metalSubpass.mode == FallbackMode::DiagnosticFailure);
#endif
    assert(vulkanSubpass.mode == FallbackMode::Native);
    assert(vulkanDepthSubpass.mode == FallbackMode::DiagnosticFailure);

    // A platform-validated WebGPU contract may expose the executable
    // compiler/RHI-owned separate-pass route, but it must remain degraded
    // rather than being mistaken for native input attachments.
    auto webGpuSeparatePassContract = nativeBackendCapabilityContract(Backend::WebGPU);
    webGpuSeparatePassContract.queriedFromNativeDevice = true;
    webGpuSeparatePassContract.queriedExtendedFeatures = true;
    webGpuSeparatePassContract.features.push_back(
        NativeBackendFeature::SeparatePassSubpassInputAttachment);
    const auto webGpuSeparatePassSubpass =
        chooseFeatureFallback(webGpuSeparatePassContract, EngineFeature::SubpassInputAttachment);
    const auto webGpuSeparatePassDepthSubpass = chooseFeatureFallback(
        webGpuSeparatePassContract, EngineFeature::SubpassDepthInputAttachment);
    assert(webGpuSeparatePassSubpass.mode == FallbackMode::Degraded);
    assert(webGpuSeparatePassDepthSubpass.mode == FallbackMode::Degraded);

    // The primary capability boolean must not erase the distinction between
    // an executable degraded lowering and a diagnostic failure.
    {
        EngineFeatureRequirementSet requirements;
        requirements.features.push_back({EngineFeature::SubpassInputAttachment,
            FeatureCorrectnessImpact::PreservesCorrectness,
            "test"});
        auto metalPlan = buildNativeEngineFallbackPlan(
            nativeBackendCapabilityContract(Backend::Metal), requirements);
        DeviceCapabilities metalCaps;
        metalPlan.applyTo(metalCaps);
#if defined(__APPLE__)
        assert(metalCaps.features.subpassInputAttachment);
        assert(!metalCaps.features.subpassInputAttachmentDegraded);
        assert(subpassInputLoweringMode(metalCaps) == SubpassInputLoweringMode::Native);
#else
        assert(!metalCaps.features.subpassInputAttachment);
        assert(!metalCaps.features.subpassInputAttachmentDegraded);
#endif

        auto webGpuPlan = buildNativeEngineFallbackPlan(
            nativeBackendCapabilityContract(Backend::WebGPU), requirements);
        DeviceCapabilities webGpuCaps;
        webGpuPlan.applyTo(webGpuCaps);
        assert(!webGpuCaps.features.subpassInputAttachment);
        assert(!webGpuCaps.features.subpassInputAttachmentDegraded);
#if !defined(__APPLE__)
        assert(subpassInputLoweringMode(metalCaps) == SubpassInputLoweringMode::DiagnosticFailure);
#endif
        assert(subpassInputLoweringMode(webGpuCaps) == SubpassInputLoweringMode::DiagnosticFailure);

        auto separatePassPlan =
            buildNativeEngineFallbackPlan(webGpuSeparatePassContract, requirements);
        DeviceCapabilities separatePassCaps;
        separatePassPlan.applyTo(separatePassCaps);
        assert(!separatePassCaps.features.subpassInputAttachment);
        assert(separatePassCaps.features.subpassInputAttachmentDegraded);
        assert(!separatePassCaps.features.subpassDepthInputAttachment);
        assert(!separatePassCaps.features.subpassDepthInputAttachmentDegraded);
        assert(
            subpassInputLoweringMode(separatePassCaps) == SubpassInputLoweringMode::SeparatePass);
    }

    DeviceCapabilities nativeSubpassCaps;
    nativeSubpassCaps.features.subpassInputAttachment = true;
    assert(subpassInputLoweringMode(nativeSubpassCaps) == SubpassInputLoweringMode::Native);

    DeviceCapabilities separatePassCaps;
    separatePassCaps.features.subpassInputAttachmentDegraded = true;
    assert(subpassInputLoweringMode(separatePassCaps) == SubpassInputLoweringMode::SeparatePass);

    const auto nativeSubpassContract = subpassInputLoweringContract(nativeSubpassCaps);
    assert(nativeSubpassContract.physicalBinding
        == SubpassInputPhysicalBinding::NativeInputAttachment);
    assert(!nativeSubpassContract.requiresSampler);
    assert(nativeSubpassContract.executable);

    const auto separatePassContract = subpassInputLoweringContract(separatePassCaps);
    assert(separatePassContract.physicalBinding == SubpassInputPhysicalBinding::SampledTexture);
    assert(separatePassContract.loadSemantics == SubpassInputLoadSemantics::CurrentFragment);
    assert(!separatePassContract.requiresSampler);
    assert(separatePassContract.executable);

    DeviceCapabilities diagnosticSubpassCaps;
    const auto diagnosticSubpassContract = subpassInputLoweringContract(diagnosticSubpassCaps);
    assert(diagnosticSubpassContract.physicalBinding == SubpassInputPhysicalBinding::Unsupported);
    assert(!diagnosticSubpassContract.executable);

    // CPU replay is an explicit request at the API boundary.  Lowering still
    // describes the required backend behavior, but cannot be selected by a
    // default descriptor.
    SecondaryCommandEncoderDesc secondaryDesc;
    assert(!secondaryDesc.allowCpuReplay);
    CommandWorkloadDesc workload;
    workload.commandCount = 1;
    const auto webGpuReplay = lowerCommandWorkload(nativeBackendCapabilityContract(Backend::WebGPU),
        workload,
        CommandRecordingIntent::SoftSecondaryCommandBuffer);
    assert(!webGpuReplay.success);
    assert(webGpuReplay.fallbackMode == FallbackMode::DiagnosticFailure);
    assert(webGpuReplay.diagnostics.find("explicit CPU replay authorization")
        != std::pmr::string::npos);
    const auto metalReplay = lowerCommandWorkload(nativeBackendCapabilityContract(Backend::Metal),
        workload,
        CommandRecordingIntent::SoftSecondaryCommandBuffer);
    const bool metalNativeSecondary =
        hasExecutableNativeFeature(nativeBackendCapabilityContract(Backend::Metal),
            NativeBackendFeature::IndirectCommandBuffers);
    assert(metalReplay.success == metalNativeSecondary);
    assert(metalReplay.fallbackMode
        == (metalNativeSecondary ? FallbackMode::Native : FallbackMode::DiagnosticFailure));
    auto replayOptIn = workload;
    replayOptIn.allowCpuReplay = true;
    const auto metalOptInReplay =
        lowerCommandWorkload(nativeBackendCapabilityContract(Backend::Metal),
            replayOptIn,
            CommandRecordingIntent::SoftSecondaryCommandBuffer);
    assert(metalOptInReplay.success);
    assert(metalOptInReplay.fallbackMode
        == (metalNativeSecondary ? FallbackMode::Native : FallbackMode::CpuReplay));

    // Metal's reflected setBytes payload is an executable lowering. The
    // lowering decision must expose that semantic instead of retaining the
    // former diagnostic-only result.
    auto metalPushWorkload = workload;
    metalPushWorkload.usesPushConstants = true;
    const auto metalPush = lowerCommandWorkload(nativeBackendCapabilityContract(Backend::Metal),
        metalPushWorkload,
        CommandRecordingIntent::PrimaryCommandBuffer);
    assert(metalPush.success);
    assert(metalPush.fallbackMode == FallbackMode::Native);
    assert(hasNativeCommandSemantic(metalPush, NativeCommandSemantic::MetalPushConstantUpload));

    DeviceCapabilities telemetryCaps;
    EngineFeatureExecutionStatus nativeStatus;
    nativeStatus.mode = FallbackMode::Native;
    EngineFeatureExecutionStatus equivalentStatus;
    equivalentStatus.mode = FallbackMode::Emulated;
    EngineFeatureExecutionStatus replayStatus;
    replayStatus.mode = FallbackMode::CpuReplay;
    EngineFeatureExecutionStatus degradedStatus;
    degradedStatus.mode = FallbackMode::Degraded;
    EngineFeatureExecutionStatus failureStatus;
    failureStatus.mode = FallbackMode::DiagnosticFailure;
    telemetryCaps.executionStatuses = {
        nativeStatus,
        equivalentStatus,
        replayStatus,
        degradedStatus,
        failureStatus,
    };
    QueryExecutionStatus queryStatus;
    queryStatus.state = QueryExecutionState::Pending;
    telemetryCaps.queryExecutionStatuses.push_back(queryStatus);
    queryStatus.state = QueryExecutionState::DiagnosticFailure;
    telemetryCaps.queryExecutionStatuses.push_back(queryStatus);
    const auto telemetry = summarizeExecutionStatuses(
        telemetryCaps.executionStatuses, telemetryCaps.queryExecutionStatuses);
    assert(telemetry.nativeFeatureCount == 1u);
    assert(telemetry.equivalentFeatureCount == 1u);
    assert(telemetry.cpuReplayFeatureCount == 1u);
    assert(telemetry.degradedFeatureCount == 1u);
    assert(telemetry.diagnosticFailureFeatureCount == 1u);
    assert(telemetry.pendingQueryCount == 1u);
    assert(telemetry.diagnosticFailureQueryCount == 1u);

    return 0;
}
