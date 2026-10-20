#include "../fixtures/conformance_evidence_admission_fixture.h"
#include "../fixtures/ordered_reuse_admission_fixture.h"
#include "../fixtures/webgl_graphics_request_fixture.h"
#include "../fixtures/webgl_profile_format_fixture.h"
#include "../fixtures/webgl_profile_identity_fixture.h"
#include "../fixtures/webgl_profile_limits_fixture.h"
#include "../fixtures/webgl_profile_memory_fixture.h"
#include "../fixtures/webgl_profile_precision_fixture.h"
#include "../fixtures/webgl_profile_probe_fixture.h"
#include "../rhi_shader_package_test_helpers.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/command_draw_validation.h"
#include "rhi/frontend/query_command_points.h"
#include "rhi/frontend/query_slot_declaration_control.h"
#include "rhi/semantics/bindless_attachment_clear_lowering.h"
#include "rhi/semantics/bindless_entry_access.h"
#include "rhi/semantics/bindless_original_execution_lowering.h"
#include "rhi/semantics/bindless_root_program_lowering.h"
#include "rhi/semantics/bindless_root_sequence_lowering.h"
#include "rhi/semantics/bindless_scene_raster.h"
#include "rhi/semantics/geometry_content_lowering.h"
#include "rhi/semantics/gpu_timing_lowering.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/native_command_lowering.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"
#include "rhi/semantics/native_platform_lifecycle_lowering.h"
#include "rhi/semantics/ordered_resource_reuse.h"
#include "rhi/semantics/webgl_runtime_profile.h"

#include <array>
#include <cassert>
#include <cstring>

using namespace arp::rhi;
using namespace arp::rhi::semantics;

static void verifyQueryAndDrawDecisions()
{
    const auto points = QueryCommandPoints::lowering(Backend::WebGL);
    const auto snapshot = QueryPoolInputSnapshot::lowering(Backend::WebGL);
    const auto slots = common::QuerySlotDeclarationControl::lowering(Backend::WebGL);
    assert(!points.cpuMetadataImplemented && !points.validatorOnly);
    assert(!snapshot.cpuMetadataImplemented && !snapshot.validatorOnly);
    assert(!slots.declarationImplemented && !slots.validatorOnly);
    for (auto diagnostic : {points.diagnostic, snapshot.diagnostic, slots.diagnostic})
        assert(std::strcmp(diagnostic, "RHI-WEBGL1-QUERY-UNSUPPORTED") == 0);

    // Even a complete set of facts for a different backend cannot authorize
    // query creation on WebGL. This profile has no production query payload.
    OriginalQueryCreationFacts facts{true, true, true, true, true, true, true};
    for (auto type : {QueryType::Timestamp, QueryType::PipelineStatistics}) {
        const auto creation =
            QueryLowering::originalCreation(Backend::WebGL, type, 1u, "probe", facts, 1u);
        assert(creation.mode == CapabilityMode::DiagnosticFailure && !creation.validatorOnly);
        assert(std::strcmp(creation.diagnostic, "RHI-WEBGL1-QUERY-UNSUPPORTED") == 0);
    }
    for (auto version : {1u, 2u, 3u}) {
        const auto indexed = OriginalIndexedDrawFetchLowering::current(Backend::WebGL, version);
        assert(
            !indexed.validatorOnly && !indexed.projectionAvailable && !indexed.executionAvailable);
        assert(
            std::strcmp(indexed.diagnostic, "RHI-WEBGL1-ORIGINAL-INDEXED-FETCH-UNAVAILABLE") == 0);
    }
    const auto indirect = IndirectArgumentLowering::current(Backend::WebGL);
    const auto producer = GpuIndirectProducerLowering::current(Backend::WebGL);
    const auto storage = GraphicsStorageWriteLowering::current(Backend::WebGL);
    const auto instance = InstanceAttributeFetchLowering::current(Backend::WebGL);
    assert(!indirect.validatorOnly && !indirect.executable
        && indirect.mode == CapabilityMode::DiagnosticFailure);
    assert(!producer.validatorOnly && !producer.executable
        && producer.mode == CapabilityMode::DiagnosticFailure);
    assert(!storage.validatorOnly && !storage.executable
        && storage.mode == CapabilityMode::DiagnosticFailure);
    assert(!instance.validatorOnly && !instance.executable
        && instance.mode == CapabilityMode::DiagnosticFailure);
    assert(indirect.contractVersion == kIndirectArgumentConsumptionContractVersion);
    assert(producer.contractVersion == kGpuIndirectProducerContractVersion);
    assert(instance.contractVersion == 1u);
    assert(std::strcmp(indirect.diagnosticId, "RHI-WEBGL1-INDIRECT-DRAW-UNSUPPORTED") == 0);
    assert(std::strcmp(producer.diagnosticId, "RHI-WEBGL1-GPU-INDIRECT-PRODUCER-UNSUPPORTED") == 0);
    assert(std::strcmp(storage.diagnosticId, "RHI-WEBGL1-GRAPHICS-STORAGE-WRITE-UNSUPPORTED") == 0);
    assert(
        std::strcmp(instance.diagnosticId, "RHI-WEBGL1-INSTANCE-ATTRIBUTE-FETCH-UNAVAILABLE") == 0);

    for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const bool validator = backend == Backend::Null;
        const auto oldPoints = QueryCommandPoints::lowering(backend);
        const auto oldSnapshot = QueryPoolInputSnapshot::lowering(backend);
        const auto oldSlots = common::QuerySlotDeclarationControl::lowering(backend);
        assert(oldPoints.cpuMetadataImplemented && oldPoints.validatorOnly == validator);
        assert(oldSnapshot.cpuMetadataImplemented && oldSnapshot.validatorOnly == validator);
        assert(oldSlots.declarationImplemented && oldSlots.validatorOnly == validator);
        const auto creation =
            QueryLowering::originalCreation(backend, QueryType::Timestamp, 1u, "probe", facts, 1u);
        assert(creation.mode
            == (backend == Backend::WebGPU ? CapabilityMode::Native
                                           : CapabilityMode::DiagnosticFailure));
        assert(creation.validatorOnly == validator);
        const auto a = IndirectArgumentLowering::current(backend);
        const auto b = GpuIndirectProducerLowering::current(backend);
        const auto c = GraphicsStorageWriteLowering::current(backend);
        const auto d = InstanceAttributeFetchLowering::current(backend);
        assert(!a.executable && !b.executable && !c.executable && !d.executable);
        assert(a.validatorOnly == validator && b.validatorOnly == validator
            && c.validatorOnly == validator && d.validatorOnly == validator);
        const auto mode =
            validator ? CapabilityMode::Equivalent : CapabilityMode::DiagnosticFailure;
        assert(a.mode == mode && b.mode == mode && c.mode == mode && d.mode == mode);
        for (auto version : {1u, 2u, 3u}) {
            const auto indexed = OriginalIndexedDrawFetchLowering::current(backend, version);
            assert(!indexed.executionAvailable && indexed.validatorOnly == validator);
            const bool projection = validator || version >= 2u || backend == Backend::WebGPU;
            assert(indexed.projectionAvailable == projection);
        }
    }
}

int main()
{
    arp::test::WebGlGraphicsRequestFixture::verify();
    ConformanceEvidenceAdmissionFixture::verify();
    const auto reuseAllocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    arp::test::OrderedReuseAdmissionFixture::verify();
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == reuseAllocations);
    arp::test::WebGlProfileLimitsFixture::verify();
    arp::test::WebGlProfilePrecisionFixture::verify();
    arp::test::WebGlProfileFormatFixture::verify();
    arp::test::WebGlProfileIdentityFixture::verify();
    arp::test::WebGlProfileMemoryFixture::verify();
    verifyQueryAndDrawDecisions();
    static_assert(static_cast<int>(Backend::Null) == 0);
    static_assert(static_cast<int>(Backend::WebGPU) == 3);
    static_assert(static_cast<int>(Backend::WebGL) == 4);
    WebGlRuntimeProbe probe;
    assert(!WebGlRuntimeProfile::fromProbe(probe).valid());
    probe = arp::test::WebGlProfileProbeFixture::baseline();
    const auto profile = WebGlRuntimeProfile::fromProbe(probe);
    assert(profile.valid());
    WebGlGraphicsRequirements requirement;
    requirement.vertexAttributes = 8;
    requirement.vertexUniformVectors = 128;
    requirement.fragmentUniformVectors = 16;
    requirement.fragmentTextureUnits = requirement.combinedTextureUnits = 8;
    auto result = profile.admit(requirement);
    assert(result.compatible && !result.executable);
    ++requirement.vertexUniformVectors;
    assert(!profile.admit(requirement).compatible);
    --requirement.vertexUniformVectors;
    requirement.vertexTextureUnits = 1;
    assert(!profile.admit(requirement).compatible);
    requirement.vertexTextureUnits = 0;
    requirement.fragmentHighFloatPrecision = 1;
    assert(!profile.admit(requirement).compatible);
    requirement.fragmentHighFloatPrecision = 0;
    requirement.uint32Indices = true;
    assert(!profile.admit(requirement).compatible);
    probe.extensions.uint32Indices = true;
    assert(!WebGlRuntimeProfile::fromProbe(probe).admit(requirement).compatible);
    WebGlExtensionOptIn optIn;
    optIn.contractVersion = 1;
    optIn.extensions.uint32Indices = true;
    assert(!WebGlRuntimeProfile::fromProbe(probe).admit(requirement, optIn).compatible);
    probe.enabledExtensions.uint32Indices = true;
    assert(WebGlRuntimeProfile::fromProbe(probe).admit(requirement, optIn).compatible);
    assert(!WebGlRuntimeProfile::fromProbe(probe).admit(requirement, optIn).executable);
    probe.contextMajorVersion = 2;
    assert(!WebGlRuntimeProfile::fromProbe(probe).valid());

    // Each extension requires queried availability, caller opt-in and actual
    // enablement in the current context. None of these facts grants execution.
    auto baseline = arp::test::WebGlProfileProbeFixture::baseline();
    constexpr std::array extensionFields{&WebGlRuntimeExtensions::uint32Indices,
        &WebGlRuntimeExtensions::instancing,
        &WebGlRuntimeExtensions::vertexArrayObjects,
        &WebGlRuntimeExtensions::depthTextures,
        &WebGlRuntimeExtensions::derivatives};
    constexpr std::array requirementFields{&WebGlGraphicsRequirements::uint32Indices,
        &WebGlGraphicsRequirements::instancing,
        &WebGlGraphicsRequirements::vertexArrayObjects,
        &WebGlGraphicsRequirements::depthTextures,
        &WebGlGraphicsRequirements::derivatives};
    for (std::size_t i = 0; i < extensionFields.size(); ++i) {
        auto p = baseline;
        auto r = WebGlGraphicsRequirements{};
        r.*requirementFields[i] = true;
        auto o = WebGlExtensionOptIn{};
        assert(!WebGlRuntimeProfile::fromProbe(p).admit(r, o).compatible);
        p.extensions.*extensionFields[i] = true;
        assert(!WebGlRuntimeProfile::fromProbe(p).admit(r, o).compatible);
        o.contractVersion = 1;
        o.extensions.*extensionFields[i] = true;
        const auto disabled = WebGlRuntimeProfile::fromProbe(p).admit(r, o);
        assert(!disabled.compatible);
        assert(std::strcmp(disabled.diagnostic, "RHI-WEBGL1-EXTENSION-NOT-ENABLED") == 0);
        p.enabledExtensions.*extensionFields[i] = true;
        auto admitted = WebGlRuntimeProfile::fromProbe(p).admit(r, o);
        assert(admitted.compatible && !admitted.executable);
        o.contractVersion = 2;
        assert(!WebGlRuntimeProfile::fromProbe(p).admit(r, o).compatible);
        o.contractVersion = 1;
        o.extensions.*extensionFields[i] = false;
        assert(!WebGlRuntimeProfile::fromProbe(p).admit(r, o).compatible);
        p.extensions.*extensionFields[i] = false;
        assert(!WebGlRuntimeProfile::fromProbe(p).valid());
    }
    auto incomplete = baseline;
    incomplete.extensionsQueried = false;
    assert(!WebGlRuntimeProfile::fromProbe(incomplete).valid());

    constexpr std::array limitFields{&WebGlRuntimeLimits::maxVertexAttributes,
        &WebGlRuntimeLimits::maxVertexUniformVectors,
        &WebGlRuntimeLimits::maxFragmentUniformVectors,
        &WebGlRuntimeLimits::maxVaryingVectors,
        &WebGlRuntimeLimits::maxVertexTextureUnits,
        &WebGlRuntimeLimits::maxFragmentTextureUnits,
        &WebGlRuntimeLimits::maxCombinedTextureUnits,
        &WebGlRuntimeLimits::maxTextureSize,
        &WebGlRuntimeLimits::maxTextureSize,
        &WebGlRuntimeLimits::maxViewportWidth,
        &WebGlRuntimeLimits::maxViewportHeight};
    constexpr std::array workloadFields{&WebGlGraphicsRequirements::vertexAttributes,
        &WebGlGraphicsRequirements::vertexUniformVectors,
        &WebGlGraphicsRequirements::fragmentUniformVectors,
        &WebGlGraphicsRequirements::varyingVectors,
        &WebGlGraphicsRequirements::vertexTextureUnits,
        &WebGlGraphicsRequirements::fragmentTextureUnits,
        &WebGlGraphicsRequirements::combinedTextureUnits,
        &WebGlGraphicsRequirements::textureWidth,
        &WebGlGraphicsRequirements::textureHeight,
        &WebGlGraphicsRequirements::viewportWidth,
        &WebGlGraphicsRequirements::viewportHeight};
    for (std::size_t i = 0; i < limitFields.size(); ++i) {
        auto r = WebGlGraphicsRequirements{};
        r.*workloadFields[i] = baseline.limits.*limitFields[i];
        if (i == 4 || i == 5)
            r.combinedTextureUnits = r.*workloadFields[i];
        assert(WebGlRuntimeProfile::fromProbe(baseline).admit(r).compatible);
        ++(r.*workloadFields[i]);
        assert(!WebGlRuntimeProfile::fromProbe(baseline).admit(r).compatible);
    }
    probe.contextMajorVersion = 1;
    probe.contextLost = true;
    assert(!WebGlRuntimeProfile::fromProbe(probe).valid());
    probe.contextLost = false;
    probe.limitsQueried = false;
    assert(!WebGlRuntimeProfile::fromProbe(probe).valid());
    probe.limitsQueried = true;
    probe.limits.maxFragmentUniformVectors = 0;
    assert(!WebGlRuntimeProfile::fromProbe(probe).valid());

    OrderedResourceReuseIntent intent;
    intent.contractVersion = 1;
    intent.sameContext = intent.sameQueue = intent.allRecordedReferencesIssued = true;
    intent.contextGeneration = intent.resourceContextGeneration = 1;
    for (auto backend : std::array{
             Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        const auto decision = OrderedResourceReuseContract::assess(backend, intent);
        assert(!decision.executable && !decision.provesGpuCompletion);
        assert(
            decision.orderingCompatible == (backend == Backend::Null || backend == Backend::WebGL));
        assert(decision.validatorOnly == (backend == Backend::Null));
    }
    auto rejects = [&](const OrderedResourceReuseIntent& invalid) {
        const auto decision = OrderedResourceReuseContract::assess(Backend::WebGL, invalid);
        assert(
            !decision.orderingCompatible && !decision.executable && !decision.provesGpuCompletion);
    };
    for (auto field : {&OrderedResourceReuseIntent::cpuReadback,
             &OrderedResourceReuseIntent::externalSharing,
             &OrderedResourceReuseIntent::aliasHeap,
             &OrderedResourceReuseIntent::publishesGpuCompletion,
             &OrderedResourceReuseIntent::contextLost}) {
        auto invalid = intent;
        invalid.*field = true;
        rejects(invalid);
    }
    for (auto field : {&OrderedResourceReuseIntent::sameQueue,
             &OrderedResourceReuseIntent::sameContext,
             &OrderedResourceReuseIntent::allRecordedReferencesIssued}) {
        auto invalid = intent;
        invalid.*field = false;
        rejects(invalid);
    }
    intent.resourceContextGeneration = 2;
    rejects(intent);
    intent.resourceContextGeneration = 1;
    intent.contractVersion = 0;
    rejects(intent);

    const auto contract = nativeBackendCapabilityContract(Backend::WebGL);
    assert(contract.features.empty());
    assert(!contract.queriedFromNativeDevice && !contract.runtimeFeaturesQueried);
    for (auto unsupported : {Backend::WebGL, static_cast<Backend>(255)}) {
        auto inconsistent = nativeBackendCapabilityContract(unsupported);
        inconsistent.features = {NativeBackendFeature::ComputeDispatch};
        inconsistent.optionalFeatures = {NativeBackendFeature::ComputeDispatch};
        inconsistent.runtimeFeatures = {NativeBackendFeature::ComputeDispatch};
        inconsistent.queriedFromNativeDevice = inconsistent.runtimeFeaturesQueried = true;
        assert(!hasExecutableNativeFeature(inconsistent, NativeBackendFeature::ComputeDispatch));
        assert(!hasExecutableOptionalNativeFeature(
            inconsistent, NativeBackendFeature::ComputeDispatch));
        assert(chooseFeatureFallback(inconsistent, EngineFeature::ComputeDispatch).mode
            == FallbackMode::DiagnosticFailure);
    }
    for (auto feature : {EngineFeature::PushConstants,
             EngineFeature::SecondaryCommandBuffer,
             EngineFeature::DescriptorTable,
             EngineFeature::TransientAliasing,
             EngineFeature::TimelineSync,
             EngineFeature::ComputeDispatch,
             EngineFeature::StorageTextureBinding,
             EngineFeature::NativeSync,
             EngineFeature::SubpassInputAttachment,
             EngineFeature::SubpassDepthInputAttachment,
             EngineFeature::Rg16FloatStorageTexture,
             EngineFeature::Rgba16FloatStorageTexture,
             EngineFeature::StorageBufferBinding,
             EngineFeature::SceneGeometryRayQuery,
             EngineFeature::IndirectDraw,
             EngineFeature::MultiDrawIndirectCount})
        assert(chooseFeatureFallback(contract, feature).mode == FallbackMode::DiagnosticFailure);
    assert(!buildPushConstantLoweringPlan(Backend::WebGL, contract).hasExecutablePayload);
    // Neither complete vocabulary from another backend nor forged query facts
    // can make the future graphics lowering executable. Exercise the actual
    // command/pipeline gates, including explicit CPU-replay requests.
    for (const auto source : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        auto forged = nativeBackendCapabilityContract(source);
        forged.backend = Backend::WebGL;
        forged.optionalFeatures = forged.features;
        forged.runtimeFeatures = forged.features;
        forged.queriedFromNativeDevice = forged.queriedExtendedFeatures =
            forged.runtimeFeaturesQueried = true;
        forged.maxPushConstantSizeBytes = kL2AtomicPushConstantLimitBytes;
        forged.uniformBufferOffsetAlignmentBytes = 256u;
        forged.maxUniformBufferBindingSizeBytes = 65536u;
        forged.pushConstantUploadRingBudgetBytes = 65536u;
        for (const auto feature : forged.features) {
            assert(!hasExecutableNativeFeature(forged, feature));
            assert(!hasExecutableOptionalNativeFeature(forged, feature));
        }
        assert(!buildPushConstantLoweringPlan(Backend::WebGL, forged).hasExecutablePayload);
        constexpr bool CommandWorkloadDesc::* requests[] = {&CommandWorkloadDesc::usesRenderPass,
            &CommandWorkloadDesc::usesPushConstants,
            &CommandWorkloadDesc::usesBarriers,
            &CommandWorkloadDesc::usesDepthStencilAttachment,
            &CommandWorkloadDesc::usesResolveTargets,
            &CommandWorkloadDesc::usesTransferCopies,
            &CommandWorkloadDesc::usesUploadStaging,
            &CommandWorkloadDesc::usesReadbackStaging,
            &CommandWorkloadDesc::usesSubresourceBarriers,
            &CommandWorkloadDesc::usesStageAccessBarriers,
            &CommandWorkloadDesc::usesDynamicViewportScissor,
            &CommandWorkloadDesc::usesComputeDispatch,
            &CommandWorkloadDesc::usesIndirectDraw,
            &CommandWorkloadDesc::usesGpuCountedIndirectDraw,
            &CommandWorkloadDesc::usesPatchListIndirectDraw,
            &CommandWorkloadDesc::usesTimestampQuery,
            &CommandWorkloadDesc::usesPipelineStatisticsQuery};
        for (const auto intent : {CommandRecordingIntent::PrimaryCommandBuffer,
                 CommandRecordingIntent::SoftSecondaryCommandBuffer,
                 CommandRecordingIntent::RenderBundle}) {
            for (const auto request : requests) {
                CommandWorkloadDesc candidate;
                candidate.commandCount = 1u;
                candidate.allowCpuReplay = true;
                candidate.usesRenderBundle = intent == CommandRecordingIntent::RenderBundle;
                candidate.*request = true;
                const auto denied = lowerCommandWorkload(forged, candidate, intent);
                assert(!denied.success && denied.fallbackMode == FallbackMode::DiagnosticFailure);
                assert(denied.semantics.empty());
            }
        }
        for (const bool hasPackage : {false, true}) {
            const auto package = arp::rhi::test::makePushContractShaderPackage();
            RenderPipelineDesc candidate;
            candidate.colorFormat = TextureFormat::Rgba8Unorm;
            candidate.shaderPackage = hasPackage ? &package : nullptr;
            const auto denied = lowerRenderPipelineState(forged, candidate);
            assert(!denied.success && denied.fallbackMode == FallbackMode::DiagnosticFailure);
            assert(denied.diagnostics == "RHI-WEBGL1-PIPELINE-EXECUTION-UNAVAILABLE");
            assert(!denied.rasterStateLowered && !denied.blendWriteMaskLowered
                && !denied.stencilStateLowered && !denied.depthBiasStateLowered
                && !denied.topologyLowered && !denied.viewportScissorLowered
                && !denied.resolveStateLowered && !denied.depthOnlyStateLowered
                && !denied.vertexInputStateLowered && !denied.tessellationStateLowered);
        }
    }
    CommandWorkloadDesc workload;
    workload.commandCount = 1;
    workload.usesRenderPass = true;
    assert(!lowerCommandWorkload(contract, workload, CommandRecordingIntent::PrimaryCommandBuffer)
            .success);
    RenderPipelineDesc render;
    render.colorFormat = TextureFormat::Rgba8Unorm;
    assert(validateRenderPipelineState(render).success);
    const auto renderDecision = lowerRenderPipelineState(contract, render);
    assert(
        !renderDecision.success && renderDecision.fallbackMode == FallbackMode::DiagnosticFailure);
    assert(renderDecision.diagnostics == "RHI-WEBGL1-PIPELINE-EXECUTION-UNAVAILABLE");
    assert(!renderDecision.rasterStateLowered && !renderDecision.tessellationStateLowered);
    // Reuse the Null layout validator and established package fixture only to
    // construct a valid L2 descriptor. They provide no native execution proof.
    auto nullDevice = createNullDevice();
    PipelineLayoutDesc layoutDesc;
    layoutDesc.pushConstantRanges.push_back({ShaderStage::Compute, 0, 128});
    const auto layout = nullDevice->createPipelineLayout(layoutDesc);
    assert(layout);
    const auto shader = test::makePushContractShaderPackage();
    ComputePipelineDesc compute;
    compute.layout = layout.get();
    compute.shaderPackage = &shader;
    compute.computeShader = shader.entrypoints[2];
    assert(validateComputePipelineState(compute).success);
    const auto computeDecision = lowerComputePipelineState(contract, compute);
    assert(!computeDecision.success
        && computeDecision.fallbackMode == FallbackMode::DiagnosticFailure);
    assert(computeDecision.diagnostics == "RHI-WEBGL1-COMPUTE-UNSUPPORTED");
    assert(!computeDecision.shaderStateLowered && !computeDecision.layoutStateLowered
        && !computeDecision.pushConstantStateLowered
        && !computeDecision.descriptorBindingStateLowered);
    arp::platform::PlatformLifecycleState lifecycle;
    const auto lifecycleDecision = lowerPlatformLifecycleEvent(
        contract, lifecycle, arp::platform::PlatformLifecycleEvent::Resize);
    assert(!lifecycleDecision.success
        && lifecycleDecision.fallbackMode == FallbackMode::DiagnosticFailure);

    // A caller-provided timestamp bit cannot grant a backend an execution
    // payload. Keep the four existing mappings and refuse WebGL/unknown even
    // if an inconsistent upstream capability says timestamps are enabled.
    arp::core::pmr::Vector<GpuTimingScopeRequest> timingScopes;
    timingScopes.push_back({1u, {}, true});
    for (auto enabled : {false, true}) {
        const auto timing = lowerGpuTimingScopes(Backend::WebGL, enabled, timingScopes);
        assert(timing.outcome == GpuTimingLoweringOutcome::Unsupported);
        assert(timing.queryCount == 0 && timing.slots.empty());
        assert(timing.diagnosticId == "RHI-WEBGL1-GPU-TIMING-UNSUPPORTED");
    }
    for (auto backend : {Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto timing = lowerGpuTimingScopes(backend, true, timingScopes);
        assert(timing.outcome == GpuTimingLoweringOutcome::Native);
        assert(timing.queryCount == 2 && gpuTimingPlanValid(timing));
    }
    const auto unknownTiming = lowerGpuTimingScopes(static_cast<Backend>(255), true, timingScopes);
    assert(unknownTiming.outcome == GpuTimingLoweringOutcome::Unsupported);
    auto forgedTiming = lowerGpuTimingScopes(Backend::Vulkan, true, timingScopes);
    forgedTiming.backend = Backend::WebGL;
    assert(!gpuTimingPlanValid(forgedTiming));
    forgedTiming.backend = Backend::Null;
    assert(!gpuTimingPlanValid(forgedTiming));
    forgedTiming.backend = static_cast<Backend>(255);
    assert(!gpuTimingPlanValid(forgedTiming));

    const auto geometry = OriginalBindlessGeometryExecutionLowering::current(Backend::WebGL);
    assert(!geometry.validatorOnly && !geometry.executionAvailable);
    assert(std::strcmp(geometry.diagnostic, "RHI-WEBGL1-BINDLESS-GEOMETRY-UNSUPPORTED") == 0);
    const auto hostGeometry = OriginalHostGeometryContentLowering::current(Backend::WebGL, 1u);
    assert(hostGeometry.mode == CapabilityMode::DiagnosticFailure);
    assert(!hostGeometry.validatorOnly && !hostGeometry.initializationAvailable
        && !hostGeometry.executionAvailable);
    assert(
        std::strcmp(hostGeometry.diagnostic, "RHI-WEBGL1-HOST-GEOMETRY-CONTENT-UNSUPPORTED") == 0);
    const auto attachment = OriginalBindlessAttachmentClearLowering::lowering(Backend::WebGL);
    assert(!attachment.preparationAvailable && !attachment.validatorOnly
        && !attachment.executionAvailable);
    assert(std::strcmp(attachment.diagnostic, "RHI-WEBGL1-BINDLESS-ATTACHMENT-CLEAR-UNSUPPORTED")
        == 0);
    for (auto version : {1u, 2u, 3u, 4u, 5u, 6u}) {
        const auto program = OriginalBindlessRootProgramLowering::lowering(Backend::WebGL, version);
        assert(!program.preparationAvailable && !program.executionAvailable
            && !program.completionAvailable);
        assert(
            std::strcmp(program.diagnostic, "RHI-WEBGL1-BINDLESS-ROOT-PROGRAM-UNSUPPORTED") == 0);
    }
    const auto produced = OriginalBindlessRootProgramLowering::lowering(Backend::WebGL, 7u);
    assert(!produced.preparationAvailable && !produced.executionAvailable
        && !produced.completionAvailable);
    assert(
        std::strcmp(produced.diagnostic, "RHI-WEBGL1-BINDLESS-PRODUCED-GEOMETRY-UNSUPPORTED") == 0);
    for (auto version : {1u, 2u, 3u, 4u}) {
        const auto entry = OriginalBindlessEntryAccessContract::lowering(Backend::WebGL, version);
        assert(!entry.projectionAvailable && !entry.validatorOnly && !entry.executionAvailable);
        assert(std::strcmp(entry.diagnostic, "RHI-WEBGL1-BINDLESS-ENTRY-ACCESS-UNSUPPORTED") == 0);
    }
    for (auto version : {1u, 2u}) {
        const auto roots = OriginalBindlessRootSequenceLowering::current(Backend::WebGL, version);
        assert(!roots.projectionAvailable && !roots.nativeExecutionAvailable);
        assert(std::strcmp(roots.diagnostic, "RHI-WEBGL1-BINDLESS-ROOT-SEQUENCE-UNSUPPORTED") == 0);
    }
    const auto scene = BindlessSceneRasterContract::lowering(Backend::WebGL);
    assert(!scene.preparationAvailable && !scene.validatorOnly && !scene.executionAvailable);
    assert(std::strcmp(scene.diagnostic, "RHI-WEBGL1-BINDLESS-SCENE-RASTER-UNSUPPORTED") == 0);
    for (auto version : {15u, 16u}) {
        const auto execution = OriginalBindlessExecutionLowering::lowering(Backend::WebGL, version);
        assert(!execution.executionAvailable && !execution.completionAvailable);
        assert(
            std::strcmp(execution.diagnostic, "RHI-WEBGL1-BINDLESS-PRODUCED-EXECUTION-UNSUPPORTED")
            == 0);
    }
    for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto existingGeometry = OriginalBindlessGeometryExecutionLowering::current(backend);
        assert(existingGeometry.validatorOnly == (backend == Backend::Null));
        assert(existingGeometry.executionAvailable
            == (backend == Backend::Vulkan || backend == Backend::Metal));
        const auto existingHostGeometry = OriginalHostGeometryContentLowering::current(backend, 1u);
        assert(existingHostGeometry.initializationAvailable
            == (backend == Backend::Vulkan || backend == Backend::Metal
                || backend == Backend::WebGPU));
        const auto existingAttachment = OriginalBindlessAttachmentClearLowering::lowering(backend);
        assert(existingAttachment.preparationAvailable
            == (backend == Backend::Null || backend == Backend::Vulkan));
        const auto existingProgram = OriginalBindlessRootProgramLowering::lowering(backend);
        assert(existingProgram.preparationAvailable == (backend == Backend::WebGPU));
    }
}
