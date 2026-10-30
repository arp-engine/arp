#include "../fixtures/generated_shader_package_fixture.h"
#include "../fixtures/original_command_sampler_custody_fixture.h"
#include "../fixtures/original_command_typed_source_contract_fixture.h"
#include "../fixtures/original_descriptor_native_identity_fixture.h"
#include "../fixtures/original_device_native_loss_observer_contract_fixture.h"
#include "../fixtures/original_geometry_readback_composition_fixture.h"
#include "../fixtures/original_observed_native_loss_binding_fixture.h"
#include "../fixtures/original_typed_attachment_dependency_probe_fixture.h"
#include "../fixtures/original_typed_cold_command_fixture.h"
#include "../fixtures/original_typed_issuer_preparation_fixture.h"
#include "../fixtures/original_typed_native_cold_contract_fixture.h"
#include "../fixtures/original_typed_native_execution_fixture.h"
#include "../fixtures/original_typed_native_loss_registration_fixture.h"
#include "../fixtures/original_typed_usage_projection_fixture.h"
#include "../fixtures/original_whole_descriptor_write_fixture.h"
#include "../fixtures/typed_shader_usage_scope_contract_fixture.h"
#include "../fixtures/webgpu_canonical_cold_admission_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"
#include "rhi/gpu_swapchain.h"

#include <cassert>
#include <cstdio>
#include <string>

#if defined(NDEBUG)
#error "Null hidden-frame smoke tests require active assertions"
#endif

int main()
{
    arp::test::OriginalCommandSamplerCustodyFixture::verify([]() {
        return arp::rhi::createNullDevice();
    });
    arp::test::OriginalTypedDependencyDecisionFixture::verifyContract();
    arp::test::OriginalTypedNativePayloadFixture::verifyContract();
    arp::test::OriginalTypedColdAdmissionFixture::verifyContract();
    arp::test::OriginalTypedColdCommandFixture::verifyContract();
    arp::test::OriginalDeviceNativeLossObserverContractFixture::verify();
    arp::test::OriginalObservedNativeLossBindingFixture::verifyContract();
    arp::test::OriginalTypedIssuerPreparationFixture::verifyContract();
    arp::test::OriginalTypedNativeExecutionFixture::verifyContract();
    arp::test::OriginalTypedAttachmentDependencyProbeFixture::verifyContract();
    arp::test::OriginalTypedNativeColdContractFixture::verifyContract();
    arp::test::OriginalGeometryReadbackCompositionFixture::verifyContract();
    arp::test::TypedShaderUsageScopeContractFixture::verifyContract();
    arp::test::OriginalTypedUsageProjectionFixture::verifyContract();
    arp::test::OriginalTypedNativeLossRegistrationFixture::verifyContract();
    arp::test::OriginalTypedNativeLossRegistrationFixture::verifyGate([]() {
        return arp::rhi::createNullDevice();
    });
    arp::test::OriginalDescriptorNativeIdentityFixture::verifyContract();
    arp::test::OriginalWholeDescriptorWriteFixture::verifyContract();
    arp::test::WebGpuCanonicalColdAdmissionFixture::verifyContract();
    using namespace arp::rhi;
    {
        auto validator = createNullDevice();
        assert(validator);
        OriginalTargetMemoryIntervalContract::Observation out;
        out.minimumBindingBytes = UINT64_MAX;
        out.stageMask = UINT32_MAX;
        const auto* id = validator->diagnoseOriginalTargetMemoryBufferInterval({1u, true},
            std::shared_ptr<ComputePipeline>{},
            std::shared_ptr<const OriginalCommandBufferLease>{},
            0u,
            0u,
            {2u, 0u, 4u},
            0u,
            out);
        assert(id && arp::core::StringView(id) == "RHI-ORIGINAL-TARGET-INTERVAL-NULL-VALIDATOR-ONLY"
            && !out.minimumBindingBytes && !out.stageMask && !out.range.sizeBytes);
    }

    arp::test::OriginalCommandTypedSourceContractFixture::verifyContract();
#if !defined(__EMSCRIPTEN__)
    arp::test::OriginalPipelineFullDefinitionFixture::verifyHost();
#endif
    arp::test::OriginalCommandTypedSourceContractFixture::verifyOriginalResources([]() {
        return createNullDevice();
    });

    auto device = createNullDevice();
    assert(device);
    arp::test::OriginalWholeDescriptorWriteFixture::verifyValidator(*device);

    ShaderPackageDesc smokePackage;
    smokePackage.slangModulePath = "null_hidden_frame_smoke.slang";
    smokePackage.artifacts = {"null_hidden_frame_smoke.spv",
        "null_hidden_frame_smoke.metal",
        "null_hidden_frame_smoke.wgsl",
        "null_hidden_frame_smoke.reflection.yaml"};
    smokePackage.entrypoints = {{smokePackage.slangModulePath, "vsMain", ShaderStage::Vertex},
        {smokePackage.slangModulePath, "fsMain", ShaderStage::Fragment}};
    smokePackage.reflection.descriptorBindings = {
        {0, 0, DescriptorType::UniformBuffer, ShaderStage::Vertex | ShaderStage::Fragment}};
    test::GeneratedShaderPackageFixture::prepare(
        smokePackage, "null_hidden_frame_smoke.artifact-set.yaml");
    NullHiddenFrameSmokeRunner runner(*device);
    const auto report = runner.run({
        "NullHiddenFrameSmoke",
        {320, 180},
        TextureFormat::Bgra8Unorm,
        2u,
        &smokePackage,
    });

    if (!report.success) {
        std::fprintf(stderr, "Null hidden-frame smoke failed: %s\n", report.diagnostics.c_str());
        for (const auto& frame : report.frames) {
            std::fprintf(stderr,
                "  frame %llu: %s\n",
                static_cast<unsigned long long>(frame.frameSerial),
                frame.diagnostics.c_str());
        }
        return 1;
    }
    assert(report.backend == Backend::Null);
    assert(report.swapchainImageCount == 2u);
    assert(report.maxFramesInFlight == 2u);
    assert(report.frames.size() == 2u);
    assert(report.submittedFrameCount == 2u);
    assert(report.totalSubmittedCommandCount > 0u);
    assert(report.descriptorCompletenessValidated);
    assert(report.backendFallbacksHiddenFromUpperLayer);
    assert(!report.nativeHandleExposedToUpperLayer);
    assert(report.resourceStats.logicalFrameSlots == 2u);
    assert(report.resourceStats.transientResourceCount == 0u);
    assert(report.resourceStats.pendingDestroyCount == 0u);
    assert(report.resourceStats.backendFallbacksHiddenFromUpperLayer);
    assert(!report.resourceStats.nativeHandleExposedToUpperLayer);

    const RhiFrameContractValidator frameValidator({2});
    RenderOutputDesc output;
    output.color.label = "NullHiddenFrameSmokeBackbuffer";
    output.color.target = {9001};
    output.color.extent = {320, 180};
    output.color.format = TextureFormat::Bgra8Unorm;
    output.color.finalState = ResourceState::Present;
    output.presentFormat = TextureFormat::Bgra8Unorm;
    assert(frameValidator.validateRenderOutput(output).success);

    for (const auto& frame : report.frames) {
        assert(frame.frameSlot < 2u);
        assert(frame.submittedCommandCount > 0u);
        assert(frame.presentSucceeded);
        assert(frame.releaseStats.releasedCount == 1u);
        assert(frame.releaseStats.releasedBytes > 0u);
        assert(frame.diagnostics.empty());

        AcquiredFrame acquired;
        acquired.success = true;
        acquired.frameSerial = frame.frameSerial;
        acquired.frameIndex = frame.frameSlot;
        acquired.status = AcquireStatus::Acquired;
        assert(frameValidator.validateAcquireResult(acquired).success);

        PresentResult present;
        present.success = frame.presentSucceeded;
        present.frameSerial = frame.frameSerial;
        present.frameIndex = frame.frameSlot;
        present.status = PresentStatus::Presented;
        assert(frameValidator.validatePresentResult(acquired, present).success);

        SwapchainImageRef swapchainImage;
        swapchainImage.image = {9001 + frame.frameSlot};
        swapchainImage.imageIndex = frame.frameSlot;
        swapchainImage.frameSerial = frame.frameSerial;
        swapchainImage.extent = {320, 180};
        swapchainImage.format = TextureFormat::Bgra8Unorm;
        swapchainImage.state = ResourceState::Present;
        assert(frameValidator.validateSwapchainImage(swapchainImage).success);
    }

    return 0;
}
