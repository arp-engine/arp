#include "../fixtures/canonical_texture_content_creation_fixture.h"
#include "../fixtures/completion_admission_transaction_fixture.h"
#include "../fixtures/cpu_quota_factory_fixture.h"
#include "../fixtures/descriptor_slot_shape_fixture.h"
#include "../fixtures/descriptor_whole_buffer_binding_fixture.h"
#include "../fixtures/original_buffer_state_fixture.h"
#include "../fixtures/original_command_admission_contract_fixture.h"
#include "../fixtures/original_command_buffer_upload_inputs_fixture.h"
#include "../fixtures/original_command_descriptor_slots_fixture.h"
#include "../fixtures/original_command_inputs_fixture.h"
#include "../fixtures/original_command_lifecycle_reservation_fixture.h"
#include "../fixtures/original_command_native_lifecycle_fixture.h"
#include "../fixtures/original_command_recording_seal_fixture.h"
#include "../fixtures/original_command_render_pass_inputs_fixture.h"
#include "../fixtures/original_command_resource_lease_fixture.h"
#include "../fixtures/original_command_sampler_custody_fixture.h"
#include "../fixtures/original_command_state_admission_contract_fixture.h"
#include "../fixtures/original_command_stream_copy_fixture.h"
#include "../fixtures/original_command_texture_readback_inputs_fixture.h"
#include "../fixtures/original_command_texture_upload_inputs_fixture.h"
#include "../fixtures/original_command_texture_view_source_fixture.h"
#include "../fixtures/original_command_typed_source_contract_fixture.h"
#include "../fixtures/original_descriptor_buffer_binding_fixture.h"
#include "../fixtures/original_descriptor_depth_artifacts_fixture.h"
#include "../fixtures/original_device_lifetime_fixture.h"
#include "../fixtures/original_device_native_loss_observation_fixture.h"
#include "../fixtures/original_device_native_loss_observer_contract_fixture.h"
#include "../fixtures/original_general_shader_resources_projection_fixture.h"
#include "../fixtures/original_geometry_readback_composition_fixture.h"
#include "../fixtures/original_observed_native_loss_binding_fixture.h"
#include "../fixtures/original_resource_registration_fixture.h"
#include "../fixtures/original_runtime_length_native_interval_fixture.h"
#include "../fixtures/original_runtime_length_native_pipeline_fixture.h"
#include "../fixtures/original_target_memory_fixture.h"
#include "../fixtures/original_texture_state_fixture.h"
#include "../fixtures/original_typed_attachment_dependency_probe_fixture.h"
#include "../fixtures/original_typed_callback_custody_fixture.h"
#include "../fixtures/original_typed_cold_command_fixture.h"
#include "../fixtures/original_typed_native_cold_contract_fixture.h"
#include "../fixtures/original_typed_native_execution_fixture.h"
#include "../fixtures/original_typed_native_loss_pipeline_fixture.h"
#include "../fixtures/original_typed_native_loss_registration_fixture.h"
#include "../fixtures/original_typed_shader_dependency_probe_fixture.h"
#include "../fixtures/original_typed_usage_projection_fixture.h"
#include "../fixtures/render_pass_arguments_fixture.h"
#include "../fixtures/texture_readback_input_quota_fixture.h"
#include "../fixtures/texture_readback_ownership_quota_fixture.h"
#include "../fixtures/texture_upload_input_quota_fixture.h"
#include "../fixtures/typed_shader_usage_scope_contract_fixture.h"
#include "../fixtures/webgpu_canonical_buffer_upload_source_fixture.h"
#include "../fixtures/webgpu_canonical_callback_custody_fixture.h"
#include "../fixtures/webgpu_canonical_cold_admission_fixture.h"
#include "../fixtures/webgpu_canonical_cold_command_fixture.h"
#include "../fixtures/webgpu_canonical_copy_pass_lowering_fixture.h"
#include "../fixtures/webgpu_canonical_issuer_preparation_fixture.h"
#include "../fixtures/webgpu_canonical_loss_bound_closure_fixture.h"
#include "../fixtures/webgpu_canonical_native_closure_fixture.h"
#include "../fixtures/webgpu_canonical_ordered_copy_attachment_fixture.h"
#include "../fixtures/webgpu_canonical_readback_joins_fixture.h"
#include "../fixtures/webgpu_canonical_scoped_recording_fixture.h"
#include "../fixtures/webgpu_canonical_submission_fixture.h"
#include "../fixtures/webgpu_canonical_texture_upload_source_fixture.h"
#include "../fixtures/webgpu_depth_stencil_attachment_input_fixture.h"
#include "../fixtures/webgpu_descriptor_dependency_retirement_fixture.h"
#include "../fixtures/webgpu_frozen_render_pass_binding_fixture.h"
#include "../fixtures/webgpu_generic_command_usage_fixture.h"
#include "../fixtures/webgpu_generic_queue_admission_fixture.h"
#include "../fixtures/webgpu_map_handoff_fixture.h"
#include "../fixtures/webgpu_ordinary_completion_fixture.h"
#include "../fixtures/webgpu_ordinary_pipeline_fixture.h"
#include "../fixtures/webgpu_ordinary_resource_fixture.h"
#include "../fixtures/webgpu_ordinary_transfer_execution_fixture.h"
#include "../fixtures/webgpu_original_buffer_upload_source_fixture.h"
#include "../fixtures/webgpu_original_l2_device_loss_fixture.h"
#include "../fixtures/webgpu_original_texture_upload_source_fixture.h"
#include "../fixtures/webgpu_owned_readback_advancement_fixture.h"
#include "../fixtures/webgpu_owned_readback_context_fixture.h"
#include "../fixtures/webgpu_queue_readback_wait_fixture.h"
#include "../fixtures/webgpu_timestamp_command_point_fixture.h"
#include "../fixtures/webgpu_transfer_retention_fixture.h"
#include "../fixtures/wgsl_buffer_minimum_native_fixture.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_binding_space_lowering.h"
#include "rhi/backends/webgpu/webgpu_device.h"
#include "rhi/backends/webgpu/webgpu_native_command_payload_impl.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "rhi/backends/webgpu/webgpu_native_string_view.h"
#include "rhi/frontend/bindless_indirect_packet.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/bindless_snapshot_metadata.h"
#include "rhi/frontend/command_indirect_commands.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_extensions.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/indirect_instance_lowering.h"
#include "rhi/semantics/webgpu_runtime_capability_bridge.h"
#include "rhi/semantics/webgpu_runtime_profile.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <webgpu/webgpu.h>
#endif

#if defined(__EMSCRIPTEN__)
class WebGpuFaultColdPreparationFixture {
public:
    static bool verify()
    {
        using namespace arp::rhi_webgpu;
        const auto limits = webGpuNativeDeviceLimits();
        arp::core::pmr::String diagnostic;
        diagnostic.reserve(160u);
        const arp::core::pmr::String label("cold fault quota fixture");
        auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1024u * 1024u);
        arp::core::CpuMemoryQuotaScope scope(quota);
        arp::test::CpuQuotaFactoryFixture admission(quota);
        const auto factory = [&] {
            return createWebGpuNativeCommandPayload(label,
                limits.uniformBufferOffsetAlignment,
                limits.maxUniformBufferBindingSize,
                limits.uniformBufferOffsetAlignment,
                1u,
                {},
                false,
                diagnostic,
                2u);
        };
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        if (diagnostic != "RHI-WEBGPU-COMMAND-PREPARATION-ALLOCATION" || quota->liveBytes())
            return false;
        {
            arp::core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            if (!pressure.charged() || factory()
                || diagnostic != "RHI-WEBGPU-COMMAND-PREPARATION-ALLOCATION")
                return false;
            constexpr char literal[] = "borrowed native label longer than small string storage";
            const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
            const auto view = webGpuNativeStringView(literal);
            if (view.data != literal || view.length != sizeof(literal) - 1u
                || arp::core::defaultCpuAllocator().stats().allocationCount != before)
                return false;
        }
        if (quota->liveBytes())
            return false;
        if (!verifyBoundaries())
            return false;
        if (quota->liveBytes())
            return false;
        std::printf(
            "BINDLESS_RHI_FAULT_COLD_PREPARATION_OK %llu\n", static_cast<unsigned long long>(peak));
        return true;
    }

    static std::shared_ptr<arp::rhi_webgpu::WebGpuRhiBindlessFaultReadback> scheduleAndSubmit(
        arp::rhi_webgpu::WebGpuNativeCommandPayload& commands,
        const std::shared_ptr<arp::rhi_webgpu::WebGpuNativeBufferPayload>& latch,
        arp::core::pmr::String& diagnostic)
    {
        auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u);
        arp::core::CpuMemoryQuotaScope scope(quota);
        arp::core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
        if (!pressure.charged())
            return {};
        const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
        auto result = commands.scheduleBindlessFaultReadback(latch, diagnostic);
        if (!result || !commands.finish(diagnostic) || !commands.submit(diagnostic)
            || arp::core::defaultCpuAllocator().stats().allocationCount != before)
            return {};
        std::puts("BINDLESS_RHI_FAULT_NO_ALLOCATION_OK");
        return result;
    }

private:
    static bool verifyBoundaries()
    {
        using namespace arp::rhi_webgpu;
        using namespace arp::rhi;
        const auto limits = webGpuNativeDeviceLimits();
        arp::core::pmr::String diagnostic;
        diagnostic.reserve(160u);
        const auto factory = [&](std::uint32_t count) {
            return createWebGpuNativeCommandPayload("fault slot boundary fixture",
                limits.uniformBufferOffsetAlignment,
                limits.maxUniformBufferBindingSize,
                limits.uniformBufferOffsetAlignment,
                1u,
                {},
                false,
                diagnostic,
                count);
        };
        BufferDesc desc;
        desc.label = "fault slot boundary latch";
        desc.sizeBytes = sizeof(std::uint32_t);
        desc.usage = ResourceUsage::StorageBuffer | ResourceUsage::CopySrc | ResourceUsage::CopyDst;
        desc.memory = MemoryUsage::GpuOnly;
        auto first = createWebGpuNativeBufferPayload(desc);
        auto second = createWebGpuNativeBufferPayload(desc);
        auto zero = factory(0u);
        if (!first || !second || !zero || zero->recordBindlessFaultLatchClear(first, diagnostic)
            || diagnostic != webGpuGenericRecordingUsageDiagnostic
            || zero->scheduleBindlessFaultReadback(first, diagnostic)
            || diagnostic != webGpuGenericRecordingUsageDiagnostic)
            return false;
        auto two = factory(2u);
        if (!two || two->recordBindlessFaultLatchClear(first, diagnostic)
            || diagnostic != webGpuGenericRecordingUsageDiagnostic
            || two->recordBindlessFaultLatchClear(second, diagnostic)
            || diagnostic != webGpuGenericRecordingUsageDiagnostic
            || two->scheduleBindlessFaultReadback(first, diagnostic)
            || diagnostic != webGpuGenericRecordingUsageDiagnostic
            || two->scheduleBindlessFaultReadback(second, diagnostic)
            || diagnostic != webGpuGenericRecordingUsageDiagnostic)
            return false;
        return !two->finished();
    }
};

namespace {

struct AsyncResult {
    bool done = false;
    bool success = false;
};

bool waitForQueue(WGPUQueue queue)
{
    return arp::test::WebGpuQueueReadbackWaitFixture::queue(queue);
}

bool mapRead(WGPUBuffer buffer, std::size_t bytes)
{
    return arp::test::WebGpuQueueReadbackWaitFixture::map(buffer, bytes);
}

void verifyOriginalStreamGeometry(const arp::rhi_webgpu::WebGpuRhiDeviceDesc& desc)
{
    using Geometry = arp::test::OriginalVertexGeometryExecutionFixture;
    const int lifecycle = EM_ASM_INT({
        // clang-format off
            const mode = new URLSearchParams(location.search).get("geometry-lifecycle");
            if (mode === null)
                return 0;
            if (mode === "recording-cancel")
                return 1;
            if (mode === "submission-loss")
                return 2;
            if (mode === "map-loss")
                return 3;
            return -1;
        // clang-format on
    });
    assert(lifecycle >= 0 && lifecycle <= 3);
    const arp::shader::GraphicsShaderModuleLoadBudget loadBudget{
        1024u * 1024u, 16u * 1024u * 1024u, 4u * 1024u * 1024u, 4u, 4u, 256u, 2u};
    auto artifactQuota =
        arp::core::makeShared<arp::core::CpuMemoryQuota>(loadBudget.maxCpuMetadataBytes);
    auto inputQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(65536u);
    arp::core::pmr::String diagnostic;
    auto artifact = arp::test::OriginalTargetMemoryFixture::loadModuleWithTargetMemory(
        "/ordinary-pipeline-fixture/original_vertex_geometry_execution/original_vertex_geometry_execution.graphics.module.yaml",
        loadBudget,
        artifactQuota,
        diagnostic);
    assert(artifact);
    auto input = arp::rhi::OriginalGraphicsPipelineInput::prepare(
        artifact, {1u, 65536u}, inputQuota, diagnostic);
    assert(input);
    arp::test::SingleStorageContractFixture facts(
        arp::rhi::ShaderStage::Fragment, arp::rhi::DescriptorType::ReadOnlyStorageBuffer);
    auto state = arp::test::WebGpuOrdinaryPipelineFixture::graphicsState();
    state.shaderPackage = &input->package();
    state.vertexShader = input->package().entrypoints[0];
    state.fragmentShader = input->package().entrypoints[1];
    state.vertexBuffers = {
        {0u, 8u, arp::rhi::VertexInputRate::Vertex, {{0u, 0u, arp::rhi::VertexFormat::Float32x2}}},
        {1u,
            24u,
            arp::rhi::VertexInputRate::Instance,
            {{1u, 0u, arp::rhi::VertexFormat::Float32x2},
                {2u, 8u, arp::rhi::VertexFormat::Float32x4}}}};
    artifact.reset();
    arp::test::OriginalAcceptedGraphicsPipelineFactoryFixture::verifyStreamGeometryActual(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        input,
        facts.layout,
        state,
        static_cast<Geometry::LifecycleCase>(lifecycle));
    input.reset();
    state.shaderPackage = nullptr;
    assert(!artifactQuota->liveBytes() && !inputQuota->liveBytes());
}

bool validateDeviceBindlessLimits()
{
    const auto limits = arp::rhi_webgpu::webGpuNativeDeviceLimits();
    if (!arp::rhi_webgpu::webGpuNativeDeviceAvailable() || !limits.maxDescriptorBindings
        || !limits.maxBindGroups || !limits.maxSampledTexturesPerShaderStage
        || !limits.maxSamplersPerShaderStage || !limits.maxStorageTexturesPerShaderStage
        || !limits.maxUniformBuffersPerShaderStage || !limits.maxStorageBuffersPerShaderStage
        || !limits.maxUniformBufferBindingSize || !limits.maxStorageBufferBindingSize)
        return false;

    arp::rhi::WebGpuRuntimeProbe probe;
    probe.browserRuntime = true;
    probe.canvasSurface = true;
    probe.nativeDevice = true;
    probe.computeDispatch = arp::rhi_webgpu::webGpuNativeDeviceSupportsComputeDispatch();
    probe.storageTextureBinding =
        arp::rhi_webgpu::webGpuNativeDeviceSupportsStorageTextureBinding();
    probe.rgba16FloatStorage = arp::rhi_webgpu::webGpuNativeDeviceSupportsStorageTexture(
        arp::rhi::TextureFormat::Rgba16Float);
    probe.storageBuffer = arp::rhi_webgpu::webGpuNativeDeviceSupportsStorageBuffer();
    probe.maxTexture2DSize = limits.maxTexture2DSize;
    probe.maxColorAttachments = limits.maxColorAttachments;
    probe.maxDescriptorBindings = limits.maxDescriptorBindings;
    probe.maxSampledTexturesPerShaderStage = limits.maxSampledTexturesPerShaderStage;
    probe.maxSamplersPerShaderStage = limits.maxSamplersPerShaderStage;
    probe.maxUniformBuffersPerShaderStage = limits.maxUniformBuffersPerShaderStage;
    probe.maxStorageTexturesPerShaderStage = limits.maxStorageTexturesPerShaderStage;
    probe.maxStorageBuffersPerShaderStage = limits.maxStorageBuffersPerShaderStage;
    probe.maxBindGroups = limits.maxBindGroups;
    probe.uniformBufferOffsetAlignment = limits.uniformBufferOffsetAlignment;
    probe.storageBufferOffsetAlignment = limits.storageBufferOffsetAlignment;
    probe.maxUniformBufferBindingSize = limits.maxUniformBufferBindingSize;
    probe.maxStorageBufferBindingSize = limits.maxStorageBufferBindingSize;
    probe.maxComputeInvocationsPerWorkgroup = limits.maxComputeInvocationsPerWorkgroup;
    probe.maxComputeWorkgroupSizeX = limits.maxComputeWorkgroupSizeX;
    probe.maxComputeWorkgroupSizeY = limits.maxComputeWorkgroupSizeY;
    probe.maxComputeWorkgroupStorageSize = limits.maxComputeWorkgroupStorageSize;
    const auto profile = arp::rhi::buildWebGpuRuntimeProfile(probe);
    const auto contract = arp::rhi::semantics::buildWebGpuRuntimeCapabilityContract(profile);
    if (!contract.success)
        return false;

    arp::rhi_webgpu::WebGpuRhiDeviceDesc desc;
    desc.label = "WebGPU bindless planning limits fixture";
    desc.runtimeProfile = profile;
    desc.capabilityContract = contract.contract;
    desc.memoryBudgetBytes = 64u * 1024u * 1024u;
    desc.pushConstantDeclaredWorkloadByteBudget = 4096u;
    desc.pushConstantUploadRingByteCapacity = 4096u;
    desc.uploadStagingByteCapacity = 4u * 1024u * 1024u;
    desc.requireComputeStorageRuntimePromotion =
        probe.computeDispatch && probe.storageTextureBinding && probe.storageBuffer;
    auto device = arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    if (!device)
        return false;
    if (EM_ASM_INT({
            // clang-format off
            return new URLSearchParams(location.search).has("shader-texture-input");
            // clang-format on
        })) {
        arp::test::WebGpuCanonicalTextureUploadSourceFixture::verifyCompletedShaderTextureInput(
            [&]() {
                return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
            },
            waitForQueue,
            mapRead);
        return true;
    }
    if (EM_ASM_INT({
            // clang-format off
                    return new URLSearchParams(location.search).has("general-resource-projection");
            // clang-format on
        })) {
        arp::test::OriginalCommandSamplerCustodyFixture::verify([&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        });
        arp::test::OriginalCommandSamplerCustodyFixture::verifyNativeLossJoin([&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        });
        arp::test::OriginalGeneralShaderResourcesProjectionFixture::verifyActual([&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        });
        return true;
    }
    arp::test::OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([] {
        arp::test::OriginalGeometryReadbackCompositionFixture::verifyContract();
    });
    if (EM_ASM_INT({
            // clang-format off
            return new URLSearchParams(location.search).has("stream-geometry");
            // clang-format on
        })) {
        verifyOriginalStreamGeometry(desc);
        return true;
    }
    if (EM_ASM_INT({
            // clang-format off
            return new URLSearchParams(location.search).has("typed-execution");
            // clang-format on
        })) {
        arp::test::OriginalTypedNativeExecutionFixture::verify(
            [&]() {
                return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
            },
            waitForQueue,
            mapRead);
        return true;
    }
    arp::test::OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([] {
        arp::test::OriginalTypedNativeColdContractFixture::verifyContract();
    });
    arp::test::OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([] {
        arp::test::TypedShaderUsageScopeContractFixture::verifyContract();
    });
    arp::test::OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([] {
        arp::test::OriginalTypedUsageProjectionFixture::verifyContract();
    });
    arp::test::OriginalTypedNativeLossRegistrationFixture::verifyNative([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::OriginalCommandStreamCopyFixture::verify([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::OriginalDeviceNativeLossObservationFixture::verifyMissing([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::OriginalDeviceNativeLossObservationFixture::verifyNative([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::OriginalCommandLifecycleReservationFixture::verify([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::OriginalCommandRecordingSealFixture::verify([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::OriginalCommandNativeLifecycleFixture::verify([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::WebGpuCanonicalCopyPassLoweringFixture::verify([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::WebGpuCanonicalIssuerPreparationFixture::verify([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::WebGpuCanonicalScopedRecordingFixture::verify([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::WebGpuCanonicalSubmissionFixture::verify(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        waitForQueue,
        mapRead);
    arp::test::WebGpuCanonicalOrderedCopyAttachmentFixture::verifyNative(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        waitForQueue,
        mapRead);
    arp::test::WebGpuCanonicalBufferUploadSourceFixture::verify(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        waitForQueue,
        mapRead);
    arp::test::WebGpuCanonicalTextureUploadSourceFixture::verify(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        waitForQueue,
        mapRead);
    arp::test::CanonicalTextureContentCreationFixture::verifyNative([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::OriginalCommandOrderedUploadContractFixture::verifyContract();
    arp::test::WebGpuCanonicalCallbackCustodyFixture::verify([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::WebGpuCanonicalColdCommandFixture::verify([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::WebGpuCanonicalColdAdmissionFixture::verify([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::WebGpuCanonicalLossBoundClosureFixture::verify([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::WebGpuCanonicalNativeClosureFixture::verify([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::OriginalDeviceLifetimeFixture::verify([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::OriginalRuntimeLengthNativePipelineFixture::verify([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::OriginalRuntimeLengthNativeIntervalFixture::verify(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        waitForQueue,
        mapRead);
    arp::test::OriginalWholeDescriptorWriteFixture::verifyContract();
    arp::test::OriginalCommandDescriptorSlotsFixture::verify(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        waitForQueue,
        mapRead);
    arp::test::OriginalTypedDependencyDecisionFixture::verifyContract();
    arp::test::OriginalTypedNativePayloadFixture::verifyContract();
    arp::test::OriginalTypedColdAdmissionFixture::verifyContract();
    arp::test::OriginalTypedColdCommandFixture::verifyContract();
    arp::test::OriginalDeviceNativeLossObserverContractFixture::verify();
    arp::test::OriginalObservedNativeLossBindingFixture::verifyContract();
    arp::test::OriginalTypedCallbackCustodyFixture::verifyContract();
    arp::test::OriginalTypedIssuerPreparationFixture::verifyContract();
    arp::test::OriginalObservedNativeLossBindingFixture::prepare([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::OriginalTypedAttachmentDependencyProbeFixture::verifyContract();
    arp::test::OriginalTypedAttachmentDependencyProbeFixture::verify(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        waitForQueue,
        mapRead);
    arp::test::OriginalTypedCallbackCustodyFixture::verify([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::OriginalTypedShaderDependencyProbeFixture::verify(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        waitForQueue,
        mapRead);
    arp::test::WebGpuOriginalL2DeviceLossFixture::prepare([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::OriginalResourceRegistrationFixture::verify([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::OriginalPipelineFactoryRegistrationFixture::verifyWebGpuFacadeAndLossGate([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::OriginalPipelineFactoryRegistrationFixture::verifyWebGpuFacadeAndLossGate(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        4u);
    {
        arp::test::SingleStorageContractFixture facts(arp::rhi::ShaderStage::Compute);
        const arp::shader::SingleEntryShaderLoadBudget loadBudget{
            1024u * 1024u, 4u * 1024u * 1024u, 1024u * 1024u, 4u};
        auto artifactQuota =
            arp::core::makeShared<arp::core::CpuMemoryQuota>(loadBudget.maxCpuMetadataBytes);
        arp::core::pmr::String diagnostic;
        auto artifact = arp::shader::SingleEntryShaderArtifact::load(
            "/ordinary-pipeline-fixture/ordinary_indirect_producer/ordinary_indirect_producer.computeMain.entry.yaml",
            loadBudget,
            artifactQuota,
            diagnostic);
        assert(artifact);
        arp::test::OriginalShaderArtifactOwnerFixture::entry(artifact);
        auto memoryInput = arp::test::OriginalSingleEntryPipelineInputFixture::verify(artifact);
        arp::rhi::ShaderPackageDesc interface;
        assert(arp::rhi::populateSingleEntryShaderInterface(*artifact, interface).success);
        auto package = arp::test::WebGpuOrdinaryPipelineFixture::ordinaryPackage(
            "ordinary_indirect_producer", interface.reflection);
        auto entry = interface.entrypoints[0];
        entry.modulePath = package.slangModulePath;
        package.entrypoints.push_back(entry);
        arp::test::OriginalSingleEntryPipelineInputFixture::verifyFileLowering(
            memoryInput, package);
        arp::test::OriginalPipelineFullDefinitionFixture::verifyCompute(
            [&]() {
                return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
            },
            std::move(package),
            facts.layout,
            entry);
        const char* originalFolder = "/ordinary-pipeline-fixture/ordinary_indirect_producer";
        EM_ASM(
            {
                // clang-format off
                    const path = UTF8ToString($0);
                    FS.rename(path, path + ".memory-input-hidden");
                // clang-format on
            },
            originalFolder);
        artifact.reset();
        arp::test::OriginalSingleEntryPipelineInputFixture::verifyMemoryLowering(memoryInput);
        auto memoryLayout =
            arp::core::shareUnique(arp::rhi_webgpu::createWebGpuRhiPipelineLayout(facts.layout));
        assert(memoryLayout);
        arp::test::OriginalArtifactComputeConsumptionFixture::verifyActual(
            memoryInput, *memoryLayout);
        arp::test::OriginalArtifactComputeCreationFixture::verifyActual(memoryInput, memoryLayout);
        arp::test::OriginalAcceptedComputePipelineFactoryFixture::verifyActual(
            [&]() {
                return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
            },
            memoryInput,
            facts.layout);
        arp::test::OriginalTypedNativeLossPipelineFixture::verifyActual<arp::rhi::ComputePipeline>(
            [&](bool overlap = false, std::uint32_t owners = 11u) {
                return arp::core::makeUnique<
                    arp::test::OriginalAcceptedComputePipelineFactoryFixture::PreparedContext>(
                    [&]() {
                        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
                    },
                    memoryInput,
                    facts.layout,
                    owners,
                    7u,
                    1u,
                    0u,
                    0u,
                    overlap);
            });
        assert(!arp::rhi::OriginalSingleEntryPipelineInput::validateOriginal(memoryInput));
        EM_ASM(
            {
                // clang-format off
                    const path = UTF8ToString($0);
                    FS.rename(path + ".memory-input-hidden", path);
                // clang-format on
            },
            originalFolder);
        std::puts(
            "BINDLESS_RHI_ORIGINAL_ARTIFACT_MEMORY_FILES_ABSENT_OK caller-dropped complete-authored-folder-absent memory-lowering-passed");
    }
    {
        const arp::shader::GraphicsShaderModuleLoadBudget loadBudget{
            1024u * 1024u, 16u * 1024u * 1024u, 4u * 1024u * 1024u, 4u, 4u, 256u, 2u};
        auto artifactQuota =
            arp::core::makeShared<arp::core::CpuMemoryQuota>(loadBudget.maxCpuMetadataBytes);
        arp::core::pmr::String diagnostic;
        auto artifact = arp::shader::GraphicsShaderModuleArtifact::load(
            "/ordinary-pipeline-fixture/ordinary_graphics_storage_read/ordinary_graphics_storage_read.graphics.module.yaml",
            loadBudget,
            artifactQuota,
            diagnostic);
        assert(artifact);
        auto input = arp::test::OriginalGraphicsPipelineInputFixture::verify(artifact);
        const auto inputQuota = input->quota();
        std::weak_ptr<const arp::shader::GraphicsShaderModuleArtifact> observer = artifact;
        auto filePackage = arp::test::WebGpuOrdinaryPipelineFixture::ordinaryPackage(
            "ordinary_graphics_storage_read", input->package().reflection);
        arp::test::OriginalGraphicsPipelineInputFixture::verifyFileLowering(input, filePackage);
        const char* folder = "/ordinary-pipeline-fixture/ordinary_graphics_storage_read";
        EM_ASM(
            {
                // clang-format off
                    const path = UTF8ToString($0);
                    FS.rename(path, path + ".memory-input-hidden");
                // clang-format on
            },
            folder);
        artifact.reset();
        assert(!observer.expired());
        arp::test::OriginalGraphicsPipelineInputFixture::verifyMemoryLowering(input);
        assert(!arp::rhi::OriginalGraphicsPipelineInput::validateOriginal(input));
        input.reset();
        observer.reset();
        assert(!artifactQuota->liveBytes() && !inputQuota->liveBytes());
        EM_ASM(
            {
                // clang-format off
                    const path = UTF8ToString($0);
                    FS.rename(path + ".memory-input-hidden", path);
                // clang-format on
            },
            folder);
        std::puts(
            "BINDLESS_RHI_ORIGINAL_GRAPHICS_MEMORY_FILES_ABSENT_OK caller-dropped complete-authored-folder-absent quotas2-zero");
    }
    {
        const arp::shader::GraphicsShaderModuleLoadBudget loadBudget{
            1024u * 1024u, 16u * 1024u * 1024u, 4u * 1024u * 1024u, 4u, 4u, 256u, 2u};
        auto artifactQuota =
            arp::core::makeShared<arp::core::CpuMemoryQuota>(loadBudget.maxCpuMetadataBytes);
        auto inputQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(65536u);
        arp::core::pmr::String diagnostic;
        auto artifact = arp::test::OriginalTargetMemoryFixture::loadModuleWithTargetMemory(
            "/ordinary-pipeline-fixture/ordinary_graphics_storage_read/ordinary_graphics_storage_read.graphics.module.yaml",
            loadBudget,
            artifactQuota,
            diagnostic);
        assert(artifact);
        auto input = arp::rhi::OriginalGraphicsPipelineInput::prepare(
            artifact, {1u, 65536u}, inputQuota, diagnostic);
        assert(input);
        arp::test::SingleStorageContractFixture facts(
            arp::rhi::ShaderStage::Fragment, arp::rhi::DescriptorType::ReadOnlyStorageBuffer);
        auto layout =
            arp::core::shareUnique(arp::rhi_webgpu::createWebGpuRhiPipelineLayout(facts.layout));
        assert(layout);
        auto state = arp::test::WebGpuOrdinaryPipelineFixture::graphicsState();
        state.layout = layout.get();
        state.shaderPackage = &input->package();
        state.vertexShader = input->package().entrypoints[0];
        state.fragmentShader = input->package().entrypoints[1];
        const char* folder = "/ordinary-pipeline-fixture/ordinary_graphics_storage_read";
        EM_ASM(
            {
                // clang-format off
                    const path = UTF8ToString($0);
                    FS.rename(path, path + ".native-consumption-hidden");
                // clang-format on
            },
            folder);
        artifact.reset();
        arp::test::OriginalArtifactGraphicsConsumptionFixture::verifyActual(input, state);
        arp::test::OriginalArtifactGraphicsCreationFixture::verifyActual(input, layout, state);
        arp::test::OriginalAcceptedGraphicsPipelineFactoryFixture::verifyActual(
            [&]() {
                return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
            },
            input,
            facts.layout,
            state);
        arp::test::OriginalAcceptedGraphicsPipelineFactoryFixture::verifyGeometryActual(
            [&]() {
                return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
            },
            input,
            facts.layout,
            state);
        arp::test::OriginalTypedNativeLossPipelineFixture::verifyActual<arp::rhi::RenderPipeline>(
            [&](bool overlap = false, std::uint32_t owners = 11u) {
                return arp::core::makeUnique<
                    arp::test::OriginalAcceptedGraphicsPipelineFactoryFixture::PreparedContext>(
                    [&]() {
                        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
                    },
                    input,
                    facts.layout,
                    state,
                    owners,
                    7u,
                    1u,
                    false,
                    0u,
                    0u,
                    overlap);
            });
        input.reset();
        assert(artifactQuota->liveBytes() && inputQuota->liveBytes());
        EM_ASM(
            {
                // clang-format off
                    const path = UTF8ToString($0);
                    FS.rename(path + ".native-consumption-hidden", path);
                // clang-format on
            },
            folder);
        std::puts(
            "BINDLESS_RHI_ORIGINAL_GRAPHICS_NATIVE_FILES_ABSENT_OK caller-dropped complete-authored-folder-absent original-native-custody");
    }
    arp::test::OriginalDescriptorFactoryContentFixture::verify([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::OriginalDescriptorFactoryContentFixture::verifyNativeLossGate([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    verifyOriginalStreamGeometry(desc);
    arp::test::OriginalCommandTypedSourceContractFixture::verifyOriginalResources(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        true,
        true);
    arp::test::OriginalCommandTypedSourceContractFixture::verifyOriginalResources(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        true,
        true,
        true);
    arp::test::OriginalBufferStateFixture::verify(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        [](arp::rhi::GpuBuffer& buffer,
            arp::rhi::ResourceState before,
            arp::rhi::ResourceState after,
            arp::core::MutationJournal* journal) {
            return arp::rhi_webgpu::transitionWebGpuRhiBuffer(
                buffer, {buffer.id(), {}, before, after}, journal);
        });
    arp::test::OriginalTextureStateFixture::verify(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        [](arp::rhi::GpuTexture& texture,
            const arp::rhi::TextureSubresourceRange& range,
            arp::rhi::ResourceState before,
            arp::rhi::ResourceState after,
            arp::core::MutationJournal* journal) {
            return arp::rhi_webgpu::transitionWebGpuRhiTexture(
                texture, {texture.id(), range, before, after}, journal);
        },
        true);
    arp::test::OriginalCommandResourceLeaseFixture::verify(*device);
    arp::test::OriginalCommandInputsFixture::verify(*device);
    arp::test::OriginalCommandTextureViewSourceFixture::verify(*device);
    arp::test::OriginalCommandRenderPassInputsFixture::verify(*device);
    arp::test::OriginalCommandTextureUploadInputsFixture::verify(*device);
    arp::test::OriginalCommandBufferUploadInputsFixture::verify(*device);
    arp::test::TextureReadbackOwnershipQuotaFixture::verify(*device);
    arp::test::OriginalCommandTextureReadbackInputsFixture::verify(*device);
    arp::test::WebGpuCanonicalReadbackJoinsFixture::verify(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        mapRead);
    arp::test::WebGpuCanonicalReadbackJoinsFixture::verifyMapCold([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::WebGpuCanonicalReadbackJoinsFixture::verifyMapReceipts(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        waitForQueue);
    arp::test::WebGpuCanonicalReadbackJoinsFixture::verifySnapshotPublication(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        waitForQueue);
    arp::test::WebGpuCanonicalReadbackJoinsFixture::verifyMapExecution(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        waitForQueue);
    arp::test::WebGpuCanonicalReadbackJoinsFixture::verifyRetainedMapping(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        waitForQueue);
    arp::test::WebGpuCanonicalReadbackJoinsFixture::verifyResultStorage([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::WebGpuCanonicalReadbackJoinsFixture::verifyOrdered(
        [&]() {
            return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        waitForQueue,
        mapRead);
    arp::test::WebGpuCanonicalReadbackJoinsFixture::prepareOrderedLoss([&]() {
        return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
    });
    arp::test::TextureUploadInputQuotaFixture::verify();
    arp::test::TextureReadbackInputQuotaFixture::verify();
    arp::test::WebGpuOwnedReadbackContextFixture::verifyActual(*device, desc);
    arp::test::WebGpuOwnedReadbackAdvancementFixture::verifyActual(desc);
    arp::test::WebGpuGenericQueueAdmissionFixture::verifyActual(*device);
    auto* extension = static_cast<arp::rhi::BindlessCrossExtension*>(
        device->queryInterface(arp::rhi::kExtensionBindlessCross));
    if (!extension)
        return false;
    const auto& caps = extension->capabilities();
    if (!caps.limitsQueried || caps.execution != arp::rhi::BindlessExecution::DiagnosticFailure
        || caps.executablePayload || caps.faultReadbackContractVersion != 1u
        || caps.originalExecutionContractVersion != 1u || caps.maxBindGroups != limits.maxBindGroups
        || caps.maxBindingsPerGroup != limits.maxDescriptorBindings
        || caps.maxUniformBufferBindingSize != limits.maxUniformBufferBindingSize
        || caps.maxStorageBufferBindingSize != limits.maxStorageBufferBindingSize)
        return false;
    const bool browserIndirectFirstInstance = EM_ASM_INT({
        // clang-format off
            var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
            return !!(device && device.features && typeof device.features.has === 'function' &&
                      device.features.has('indirect-first-instance'));
        // clang-format on
    }) != 0;
    if (caps.indirectFirstInstance != browserIndirectFirstInstance)
        return false;
    std::printf(
        "BINDLESS_RHI_DEVICE_INDIRECT_FIRST_INSTANCE %u\n", caps.indirectFirstInstance ? 1u : 0u);
    for (const auto& stage : caps.stageLimits) {
        if (stage.sampledTextures != limits.maxSampledTexturesPerShaderStage
            || stage.samplers != limits.maxSamplersPerShaderStage
            || stage.storageTextures != limits.maxStorageTexturesPerShaderStage
            || stage.uniformBuffers != limits.maxUniformBuffersPerShaderStage
            || stage.storageBuffers != limits.maxStorageBuffersPerShaderStage)
            return false;
    }

    using arp::rhi::BindlessOperation;
    const auto hasSupport =
        [&](std::size_t stage, std::size_t resourceClass, BindlessOperation operations) {
            const auto& support = caps.support[stage][resourceClass];
            return support.operations == operations && support.nonUniform;
        };
    if (!hasSupport(2u,
            0u,
            BindlessOperation::Load | BindlessOperation::Query | BindlessOperation::SampleLevel)
        || !hasSupport(2u, 1u, BindlessOperation::None)
        || !hasSupport(
            2u, 2u, BindlessOperation::Load | BindlessOperation::Query | BindlessOperation::Store)
        || !hasSupport(2u, 3u, BindlessOperation::Load)
        || !hasSupport(2u, 4u, BindlessOperation::Load | BindlessOperation::Query)
        || !hasSupport(2u,
            5u,
            BindlessOperation::Load | BindlessOperation::Query | BindlessOperation::Store
                | BindlessOperation::Atomic)
        || !hasSupport(1u,
            0u,
            BindlessOperation::Load | BindlessOperation::Query | BindlessOperation::SampleLevel
                | BindlessOperation::SampleGrad)
        || !hasSupport(1u, 1u, BindlessOperation::None)
        || !hasSupport(1u, 3u, BindlessOperation::Load)
        || !hasSupport(1u, 4u, BindlessOperation::Load | BindlessOperation::Query)
        || !hasSupport(1u,
            5u,
            BindlessOperation::Load | BindlessOperation::Query | BindlessOperation::Store
                | BindlessOperation::Atomic))
        return false;
    for (std::size_t resource = 0; resource < arp::rhi::kBindlessResourceClasses; ++resource) {
        if (resource == 0u || resource == 1u || resource == 2u || resource == 3u || resource == 4u
            || resource == 5u)
            continue;
        const auto& support = caps.support[2][resource];
        if (support.operations != BindlessOperation::None || support.nonUniform)
            return false;
    }
    for (std::size_t resource = 1u; resource < arp::rhi::kBindlessResourceClasses; ++resource) {
        if (resource == 1u || resource == 3u || resource == 4u || resource == 5u)
            continue;
        const auto& support = caps.support[1][resource];
        if (support.operations != BindlessOperation::None || support.nonUniform)
            return false;
    }
    for (const auto& support : caps.support[0]) {
        if (support.operations != BindlessOperation::None || support.nonUniform)
            return false;
    }
    return true;
}

bool executePublicBindlessCompute(
    const std::shared_ptr<const arp::rhi::BindlessShaderPackage>& package)
{
    using namespace arp::rhi;
    using namespace arp::rhi_webgpu;

    const auto limits = webGpuNativeDeviceLimits();
    WebGpuRuntimeProbe probe;
    probe.browserRuntime = true;
    probe.canvasSurface = true;
    probe.nativeDevice = true;
    probe.computeDispatch = webGpuNativeDeviceSupportsComputeDispatch();
    probe.storageTextureBinding = webGpuNativeDeviceSupportsStorageTextureBinding();
    probe.storageBuffer = webGpuNativeDeviceSupportsStorageBuffer();
    probe.maxTexture2DSize = limits.maxTexture2DSize;
    probe.maxColorAttachments = limits.maxColorAttachments;
    probe.maxDescriptorBindings = limits.maxDescriptorBindings;
    probe.maxSampledTexturesPerShaderStage = limits.maxSampledTexturesPerShaderStage;
    probe.maxSamplersPerShaderStage = limits.maxSamplersPerShaderStage;
    probe.maxUniformBuffersPerShaderStage = limits.maxUniformBuffersPerShaderStage;
    probe.maxStorageTexturesPerShaderStage = limits.maxStorageTexturesPerShaderStage;
    probe.maxStorageBuffersPerShaderStage = limits.maxStorageBuffersPerShaderStage;
    probe.maxBindGroups = limits.maxBindGroups;
    probe.uniformBufferOffsetAlignment = limits.uniformBufferOffsetAlignment;
    probe.storageBufferOffsetAlignment = limits.storageBufferOffsetAlignment;
    probe.maxUniformBufferBindingSize = limits.maxUniformBufferBindingSize;
    probe.maxStorageBufferBindingSize = limits.maxStorageBufferBindingSize;
    probe.maxComputeInvocationsPerWorkgroup = limits.maxComputeInvocationsPerWorkgroup;
    probe.maxComputeWorkgroupSizeX = limits.maxComputeWorkgroupSizeX;
    probe.maxComputeWorkgroupSizeY = limits.maxComputeWorkgroupSizeY;
    probe.maxComputeWorkgroupStorageSize = limits.maxComputeWorkgroupStorageSize;
    const auto profile = buildWebGpuRuntimeProfile(probe);
    const auto bridge = semantics::buildWebGpuRuntimeCapabilityContract(profile);
    if (!bridge.success)
        return false;

    WebGpuRhiDeviceDesc desc;
    desc.label = "Public WebGPU BindlessCross compute fixture";
    desc.runtimeProfile = profile;
    desc.capabilityContract = bridge.contract;
    desc.memoryBudgetBytes = 16u * 1024u * 1024u;
    desc.pushConstantDeclaredWorkloadByteBudget = 4096u;
    desc.pushConstantUploadRingByteCapacity = 4096u;
    desc.uploadStagingByteCapacity = 1024u * 1024u;
    desc.requireComputeStorageRuntimePromotion =
        probe.computeDispatch && probe.storageTextureBinding && probe.storageBuffer;
    auto device = createWebGpuRhiDevice(desc);
    if (!device)
        return false;
    auto* extension =
        static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
    if (!extension)
        return false;

    BindlessRequirements request;
    request.optIn = true;
    request.allowEmulated = true;
    request.deviceIdentity = extension->capabilities().deviceIdentity;
    request.shaderPackageIdentity = package->artifact().shaderPackageIdentity;
    request.budget = {1u, 24u, package->artifact().snapshotBytes, 32u};
    for (const auto& mapping : package->artifact().tables) {
        const auto binding = std::find_if(package->artifact().physicalLayout.bindings.begin(),
            package->artifact().physicalLayout.bindings.end(),
            [&](const auto& value) {
                return value.binding == mapping.firstBinding;
            });
        if (binding == package->artifact().physicalLayout.bindings.end())
            return false;
        BindlessTableRequirement table;
        table.tableId = mapping.tableId;
        table.binding = *binding;
        table.resourceCount = mapping.capacity;
        table.operations = mapping.operations;
        table.nonUniform = mapping.nonUniform;
        table.bufferLayout = mapping.bufferLayout;
        request.tables.push_back(table);
    }
    const auto plan = extension->plan(request, package->artifact());
    if (!plan.success || !plan.executable || plan.execution != BindlessExecution::Emulated
        || plan.bindingModel != BindlessBindingModel::ScalarExpansion) {
        std::printf("BINDLESS_RHI_PUBLIC_PLAN_FAILED %s\n", plan.diagnosticId);
        return false;
    }
    auto noOptIn = request;
    noOptIn.optIn = false;
    if (std::string_view(extension->plan(noOptIn, package->artifact()).diagnosticId)
        != "RHI-BINDLESS-OPT-IN-REQUIRED")
        return false;
    auto noEmulation = request;
    noEmulation.allowEmulated = false;
    if (extension->plan(noEmulation, package->artifact()).success)
        return false;
    BindlessSnapshotPool snapshots(*device, request.budget);
    arp::core::pmr::String diagnostic;
    auto snapshot = snapshots.prepareShared(request, package->artifact(), diagnostic);
    if (!snapshot) {
        std::printf("BINDLESS_RHI_PUBLIC_SNAPSHOT_PREPARE_FAILED %s\n", diagnostic.c_str());
        return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_SNAPSHOT_PREPARED");

    std::array<std::uint32_t, 2> counterInitial{10u, 20u};
    std::array<std::array<float, 4>, 2> uniformInitial{
        {{{3.25f, 0.0f, 0.0f, 0.0f}}, {{7.5f, 0.0f, 0.0f, 0.0f}}}};
    std::array<std::uint32_t, 44> zero{};
    std::array<std::array<std::uint8_t, 256>, 2> imagePixels{};
    std::array<std::array<std::uint8_t, 256>, 2> outputPixels{};
    const std::array<float, 2> outputInitial{0.25f, 0.75f};
    for (std::size_t i = 0; i < outputInitial.size(); ++i)
        std::memcpy(outputPixels[i].data(), &outputInitial[i], sizeof(float));
    imagePixels[0][0] = 255u;
    imagePixels[0][3] = 255u;
    imagePixels[0][6] = 255u;
    imagePixels[0][7] = 255u;
    imagePixels[1][1] = 255u;
    imagePixels[1][3] = 255u;
    imagePixels[1][4] = 255u;
    imagePixels[1][5] = 255u;
    imagePixels[1][6] = 255u;
    imagePixels[1][7] = 255u;
    auto counterA = device->createBuffer({"counter A",
        sizeof(std::uint32_t),
        ResourceUsage::StorageBuffer | ResourceUsage::CopySrc | ResourceUsage::CopyDst,
        MemoryUsage::CpuToGpu,
        ResourceState::Undefined});
    auto counterB = device->createBuffer({"counter B",
        sizeof(std::uint32_t),
        ResourceUsage::StorageBuffer | ResourceUsage::CopySrc | ResourceUsage::CopyDst,
        MemoryUsage::CpuToGpu,
        ResourceState::Undefined});
    auto inputA = device->createBuffer({"read-only input A",
        sizeof(std::uint32_t),
        ResourceUsage::StorageBuffer | ResourceUsage::CopySrc | ResourceUsage::CopyDst,
        MemoryUsage::CpuToGpu,
        ResourceState::Undefined});
    auto inputB = device->createBuffer({"read-only input B",
        sizeof(std::uint32_t),
        ResourceUsage::StorageBuffer | ResourceUsage::CopySrc | ResourceUsage::CopyDst,
        MemoryUsage::CpuToGpu,
        ResourceState::Undefined});
    auto resultBuffer = device->createBuffer({"bindless buffer operation results",
        44u * sizeof(std::uint32_t),
        ResourceUsage::StorageBuffer | ResourceUsage::CopySrc | ResourceUsage::CopyDst,
        MemoryUsage::CpuToGpu,
        ResourceState::Undefined});
    auto readback = device->createBuffer({"bindless buffer operation readback",
        46u * sizeof(std::uint32_t),
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::Undefined});
    auto textureReadback = device->createBuffer({"bindless storage texture readback",
        512u,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::Undefined});
    std::array<arp::core::UniquePtr<GpuBuffer>, 2> uniforms;
    std::array<arp::core::UniquePtr<GpuTexture>, 2> images;
    std::array<arp::core::UniquePtr<GpuTextureView>, 2> imageViews;
    std::array<arp::core::UniquePtr<GpuTexture>, 2> outputs;
    std::array<arp::core::UniquePtr<GpuTextureView>, 2> outputViews;
    std::array<arp::core::UniquePtr<GpuSampler>, 2> samplers;
    for (std::size_t i = 0; i < images.size(); ++i) {
        uniforms[i] = device->createBuffer({i == 0u ? "bindless uniform A" : "bindless uniform B",
            sizeof(uniformInitial[i]),
            ResourceUsage::UniformBuffer | ResourceUsage::CopyDst,
            MemoryUsage::CpuToGpu,
            ResourceState::Undefined});
        TextureDesc imageDesc;
        imageDesc.label = i == 0u ? "public bindless red image" : "public bindless green image";
        imageDesc.extent = {2u, 1u, 1u};
        imageDesc.format = TextureFormat::Rgba8Unorm;
        imageDesc.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
        imageDesc.initialState = ResourceState::Undefined;
        images[i] = device->createTexture(imageDesc);
        TextureViewDesc viewDesc;
        viewDesc.label = imageDesc.label;
        viewDesc.format = imageDesc.format;
        viewDesc.usage = TextureUsage::Sampled;
        if (images[i])
            imageViews[i] = device->createTextureView(*images[i], viewDesc);
        TextureDesc outputDesc;
        outputDesc.label = i == 0u ? "public bindless output zero" : "public bindless output one";
        outputDesc.extent = {1u, 1u, 1u};
        outputDesc.format = TextureFormat::R32Float;
        outputDesc.usage = TextureUsage::Storage | TextureUsage::CopySrc | TextureUsage::CopyDst;
        outputDesc.initialState = ResourceState::Undefined;
        outputs[i] = device->createTexture(outputDesc);
        TextureViewDesc outputViewDesc;
        outputViewDesc.label = outputDesc.label;
        outputViewDesc.format = outputDesc.format;
        outputViewDesc.usage = TextureUsage::Storage;
        if (outputs[i])
            outputViews[i] = device->createTextureView(*outputs[i], outputViewDesc);
        SamplerDesc samplerDesc;
        samplerDesc.minFilter = samplerDesc.magFilter =
            i == 0u ? SamplerFilter::Nearest : SamplerFilter::Linear;
        samplerDesc.mipFilter = SamplerMipFilter::Nearest;
        samplers[i] = device->createSampler(samplerDesc);
    }
    if (!counterA || !counterB || !inputA || !inputB || !resultBuffer || !readback
        || !textureReadback || !images[0] || !images[1] || !imageViews[0] || !imageViews[1]
        || !outputs[0] || !outputs[1] || !outputViews[0] || !outputViews[1] || !samplers[0]
        || !samplers[1] || !uniforms[0] || !uniforms[1]) {
        std::puts("BINDLESS_RHI_PUBLIC_RESOURCE_CREATE_FAILED");
        return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_RESOURCES_CREATED");
    const auto uploadA = device->writeBuffer(*counterA, &counterInitial[0], sizeof(std::uint32_t));
    const auto uploadB = device->writeBuffer(*counterB, &counterInitial[1], sizeof(std::uint32_t));
    const auto uploadInputA =
        device->writeBuffer(*inputA, &counterInitial[0], sizeof(std::uint32_t));
    const auto uploadInputB =
        device->writeBuffer(*inputB, &counterInitial[1], sizeof(std::uint32_t));
    const auto uploadResult = device->writeBuffer(*resultBuffer, zero.data(), sizeof(zero));
    std::array<CommandValidationResult, 2> uploadUniforms{};
    for (std::size_t i = 0; i < uniforms.size(); ++i) {
        uploadUniforms[i] =
            device->writeBuffer(*uniforms[i], uniformInitial[i].data(), sizeof(uniformInitial[i]));
    }
    if (!uploadA.success || !uploadB.success || !uploadInputA.success || !uploadInputB.success
        || !uploadResult.success || !uploadUniforms[0].success || !uploadUniforms[1].success) {
        std::printf("BINDLESS_RHI_PUBLIC_UPLOAD_FAILED %s %s %s %s %s %s %s\n",
            uploadA.diagnostics.c_str(),
            uploadB.diagnostics.c_str(),
            uploadInputA.diagnostics.c_str(),
            uploadInputB.diagnostics.c_str(),
            uploadResult.diagnostics.c_str(),
            uploadUniforms[0].diagnostics.c_str(),
            uploadUniforms[1].diagnostics.c_str());
        return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_BUFFER_UPLOADS_RECORDED");

    auto uploadEncoder = device->createCommandEncoder({"public bindless texture upload", 1u, 1u});
    if (!uploadEncoder) {
        std::puts("BINDLESS_RHI_PUBLIC_TEXTURE_ENCODER_FAILED");
        return false;
    }
    for (const auto& image : images) {
        if (!uploadEncoder->transition(*image, ResourceState::Undefined, ResourceState::CopyDst)
                .success) {
            std::puts("BINDLESS_RHI_PUBLIC_TEXTURE_UPLOAD_TRANSITION_FAILED");
            return false;
        }
    }
    for (const auto& output : outputs) {
        if (!uploadEncoder->transition(*output, ResourceState::Undefined, ResourceState::CopyDst)
                .success) {
            std::puts("BINDLESS_RHI_PUBLIC_STORAGE_TEXTURE_UPLOAD_TRANSITION_FAILED");
            return false;
        }
    }
    auto textureUpload = uploadEncoder->beginTransferPass({"public bindless texture upload"});
    if (!textureUpload) {
        std::puts("BINDLESS_RHI_PUBLIC_TEXTURE_TRANSFER_BEGIN_FAILED");
        return false;
    }
    for (std::size_t i = 0; i < images.size(); ++i) {
        UploadTextureDesc upload;
        upload.label = i == 0u ? "upload red bindless image" : "upload green bindless image";
        upload.destinationTexture = images[i]->id();
        upload.extent = {2u, 1u, 1u};
        upload.format = TextureFormat::Rgba8Unorm;
        upload.bytesPerRow = 256u;
        upload.rowsPerImage = 1u;
        upload.subresources = defaultTextureSubresourceRange(images[i]->desc());
        upload.pixelData = imagePixels[i].data();
        upload.pixelDataSize = static_cast<std::uint32_t>(imagePixels[i].size());
        const auto uploaded = textureUpload->uploadTexture(upload);
        if (!uploaded.success) {
            std::printf(
                "BINDLESS_RHI_PUBLIC_TEXTURE_UPLOAD_FAILED %s\n", uploaded.diagnostics.c_str());
            return false;
        }
    }
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        UploadTextureDesc upload;
        upload.label = i == 0u ? "upload bindless output zero" : "upload bindless output one";
        upload.destinationTexture = outputs[i]->id();
        upload.extent = {1u, 1u, 1u};
        upload.format = TextureFormat::R32Float;
        upload.bytesPerRow = 256u;
        upload.rowsPerImage = 1u;
        upload.subresources = defaultTextureSubresourceRange(outputs[i]->desc());
        upload.pixelData = outputPixels[i].data();
        upload.pixelDataSize = static_cast<std::uint32_t>(outputPixels[i].size());
        const auto uploaded = textureUpload->uploadTexture(upload);
        if (!uploaded.success) {
            std::printf("BINDLESS_RHI_PUBLIC_STORAGE_TEXTURE_UPLOAD_FAILED %s\n",
                uploaded.diagnostics.c_str());
            return false;
        }
    }
    if (!textureUpload->end().success) {
        std::puts("BINDLESS_RHI_PUBLIC_TEXTURE_TRANSFER_END_FAILED");
        return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_TEXTURE_UPLOADS_RECORDED");
    for (const auto& image : images) {
        if (!uploadEncoder->transition(*image, ResourceState::CopyDst, ResourceState::ShaderRead)
                .success) {
            std::puts("BINDLESS_RHI_PUBLIC_TEXTURE_READ_TRANSITION_FAILED");
            return false;
        }
    }
    for (const auto& output : outputs) {
        if (!uploadEncoder->transition(*output, ResourceState::CopyDst, ResourceState::ShaderWrite)
                .success) {
            std::puts("BINDLESS_RHI_PUBLIC_STORAGE_TEXTURE_WRITE_TRANSITION_FAILED");
            return false;
        }
    }
    auto uploadCommands = uploadEncoder->finish();
    if (!uploadCommands) {
        std::puts("BINDLESS_RHI_PUBLIC_TEXTURE_COMMAND_FINISH_FAILED");
        return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_TEXTURE_COMMANDS_FINISHED");
    std::puts("BINDLESS_RHI_PUBLIC_TEXTURE_SUBMIT_BEGIN");
    const auto textureSubmit = device->queue().submit(*uploadCommands);
    std::printf("BINDLESS_RHI_PUBLIC_TEXTURE_SUBMIT_RESULT %u %u %s\n",
        textureSubmit.success,
        textureSubmit.workAccepted,
        textureSubmit.diagnostics.c_str());
    if (!textureSubmit.hasAcceptedWork()) {
        std::puts("BINDLESS_RHI_PUBLIC_TEXTURE_SUBMIT_FAILED");
        return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_TEXTURES_READY");

    for (std::size_t tableIndex = 0; tableIndex < package->artifact().tables.size(); ++tableIndex) {
        const auto& mapping = package->artifact().tables[tableIndex];
        for (std::uint32_t i = 0; i < 2u; ++i) {
            const auto handle = snapshot->handle(mapping.tableId, i);
            ValidationResult written;
            if (mapping.tableId == 1u) {
                written = snapshot->writeBuffer(handle, i == 0u ? *counterA : *counterB);
            } else if (mapping.tableId == 2u) {
                written = snapshot->writeBuffer(handle, i == 0u ? *inputA : *inputB);
            } else if (mapping.tableId == 3u) {
                written = snapshot->writeTexture(handle, *imageViews[i], ResourceState::ShaderRead);
            } else if (mapping.tableId == 4u) {
                written =
                    snapshot->writeTexture(handle, *outputViews[i], ResourceState::ShaderWrite);
            } else if (mapping.tableId == 5u) {
                DescriptorBindingWrite descriptor;
                descriptor.type = DescriptorType::Sampler;
                descriptor.resource = samplers[i]->id();
                descriptor.resourceKind = DescriptorResourceKind::Sampler;
                written = snapshot->write(handle, descriptor);
            } else if (mapping.tableId == 6u) {
                written = snapshot->writeBuffer(handle, *uniforms[i]);
            } else {
                return false;
            }
            if (!written.success) {
                std::printf("BINDLESS_RHI_PUBLIC_TABLE_WRITE_FAILED %u %u %s\n",
                    mapping.tableId,
                    i,
                    written.diagnostics.c_str());
                return false;
            }
        }
    }
    const auto ordinary = snapshot->writeOrdinaryBuffer(2u, *resultBuffer);
    if (!ordinary.success) {
        std::printf("BINDLESS_RHI_PUBLIC_ORDINARY_WRITE_FAILED %s\n", ordinary.diagnostics.c_str());
        return false;
    }
    const auto sealed = snapshot->seal(*device);
    if (!sealed.success) {
        std::printf("BINDLESS_RHI_PUBLIC_SNAPSHOT_SEAL_FAILED %s\n", sealed.diagnostics.c_str());
        return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_SNAPSHOT_SEALED");
    const auto latchPayload = webGpuNativePayloadFor(*snapshot->faultLatchBuffer());
    const auto readbackPayload = webGpuNativePayloadFor(*snapshot->faultReadbackBuffer());
    if (!latchPayload || !latchPayload->buffer || !readbackPayload || !readbackPayload->buffer)
        return false;
    auto authorization = extension->authorize(package, *snapshot, diagnostic);
    if (!authorization) {
        std::printf("BINDLESS_RHI_PUBLIC_AUTHORIZATION_FAILED %s\n", diagnostic.c_str());
        return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_AUTHORIZED");
    auto pipeline = extension->createComputePipeline(authorization, "computeMain", diagnostic);
    if (!pipeline) {
        std::printf("BINDLESS_RHI_PUBLIC_PIPELINE_FAILED %s\n", diagnostic.c_str());
        return false;
    }

    std::array<BindlessResourceAccess, 13> accesses{};
    const auto setBufferAccess = [&](BindlessResourceAccess& access,
                                     const GpuBuffer& buffer,
                                     ResourceAccess shaderAccess,
                                     ResourceState shaderState) {
        access.resource = buffer.id();
        access.kind = BindlessAccessKind::Buffer;
        access.stages = ShaderStage::Compute;
        access.pipelineStages = PipelineStage::ComputeShader;
        access.access = shaderAccess;
        access.state = shaderState;
        access.bufferSize = buffer.desc().sizeBytes;
    };
    setBufferAccess(accesses[0],
        *counterA,
        ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite,
        ResourceState::ShaderWrite);
    setBufferAccess(accesses[1],
        *counterB,
        ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite,
        ResourceState::ShaderWrite);
    setBufferAccess(accesses[2], *inputA, ResourceAccess::ShaderRead, ResourceState::ShaderRead);
    setBufferAccess(accesses[3], *inputB, ResourceAccess::ShaderRead, ResourceState::ShaderRead);
    setBufferAccess(accesses[4],
        *resultBuffer,
        ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite,
        ResourceState::ShaderWrite);
    setBufferAccess(
        accesses[5], *uniforms[0], ResourceAccess::ShaderRead, ResourceState::ShaderRead);
    setBufferAccess(
        accesses[6], *uniforms[1], ResourceAccess::ShaderRead, ResourceState::ShaderRead);
    for (std::size_t i = 0; i < images.size(); ++i) {
        auto& access = accesses[7u + i];
        access.resource = images[i]->id();
        access.kind = BindlessAccessKind::Texture;
        access.stages = ShaderStage::Compute;
        access.pipelineStages = PipelineStage::ComputeShader;
        access.access = ResourceAccess::ShaderRead;
        access.state = ResourceState::ShaderRead;
        access.textureRange = defaultTextureSubresourceRange(images[i]->desc());
    }
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        auto& access = accesses[9u + i];
        access.resource = outputs[i]->id();
        access.kind = BindlessAccessKind::Texture;
        access.stages = ShaderStage::Compute;
        access.pipelineStages = PipelineStage::ComputeShader;
        access.access = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
        access.state = ResourceState::ShaderWrite;
        access.textureRange = defaultTextureSubresourceRange(outputs[i]->desc());
    }
    accesses[11] = snapshot->metadataAccess();
    accesses[12] = snapshot->faultLatchAccess();
    std::array<BindlessResourceAccess, 14> completeAccesses{};
    std::copy(accesses.begin(), accesses.end(), completeAccesses.begin());
    completeAccesses[13] = snapshot->faultReadbackAccess();

    auto encoder = device->createCommandEncoder({"public bindless compute", 1u, 1u});
    if (!encoder)
        return false;
    if (!encoder->transition(*counterA, ResourceState::Undefined, ResourceState::ShaderWrite)
            .success
        || !encoder->transition(*counterB, ResourceState::Undefined, ResourceState::ShaderWrite)
            .success
        || !encoder->transition(*inputA, ResourceState::Undefined, ResourceState::ShaderRead)
            .success
        || !encoder->transition(*inputB, ResourceState::Undefined, ResourceState::ShaderRead)
            .success
        || !encoder->transition(*uniforms[0], ResourceState::Undefined, ResourceState::ShaderRead)
            .success
        || !encoder->transition(*uniforms[1], ResourceState::Undefined, ResourceState::ShaderRead)
            .success
        || !encoder->transition(*resultBuffer, ResourceState::Undefined, ResourceState::ShaderWrite)
            .success) {
        std::puts("BINDLESS_RHI_PUBLIC_TRANSITION_FAILED");
        return false;
    }
    auto pass = encoder->beginComputePass({"public bindless compute"});
    if (!pass) {
        std::puts("BINDLESS_RHI_PUBLIC_PASS_BEGIN_FAILED");
        return false;
    }
    const auto pipelineBound = pass->setPipeline(*pipeline);
    if (!pipelineBound.success) {
        std::printf(
            "BINDLESS_RHI_PUBLIC_SET_PIPELINE_FAILED %s\n", pipelineBound.diagnostics.c_str());
        return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_PIPELINE_BOUND");
    auto* indexed =
        static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
    if (!indexed) {
        std::puts("BINDLESS_RHI_PUBLIC_PASS_EXTENSION_MISSING");
        return false;
    }
    const auto bypass =
        pass->bindDescriptorSet(snapshot->setIndex(), *snapshot->descriptorSet(), nullptr, 0u);
    if (bypass.success || bypass.diagnostics != "RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED") {
        std::printf("BINDLESS_RHI_PUBLIC_BYPASS_CHECK_FAILED %d %s\n",
            bypass.success,
            bypass.diagnostics.c_str());
        return false;
    }
    auto forgedPlan = plan;
    ++forgedPlan.identity;
    const auto forged = indexed->bindSnapshot(snapshot,
        forgedPlan,
        snapshot->generation(),
        BindlessAccessArray(completeAccesses.data(), completeAccesses.size()));
    if (forged.success || forged.diagnostics != "RHI-BINDLESS-RECORD-PLAN-MISMATCH") {
        std::printf("BINDLESS_RHI_PUBLIC_FORGED_PLAN_CHECK_FAILED %d %s\n",
            forged.success,
            forged.diagnostics.c_str());
        return false;
    }
    const auto missingAccess = indexed->bindSnapshot(snapshot,
        plan,
        snapshot->generation(),
        BindlessAccessArray(completeAccesses.data(), completeAccesses.size() - 1u));
    if (missingAccess.success
        || missingAccess.diagnostics != "RHI-BINDLESS-FAULT-READBACK-ACCESS-INCOMPLETE") {
        std::printf("BINDLESS_RHI_PUBLIC_MISSING_ACCESS_CHECK_FAILED %d %s\n",
            missingAccess.success,
            missingAccess.diagnostics.c_str());
        return false;
    }
    const auto bound = indexed->bindSnapshot(snapshot,
        plan,
        snapshot->generation(),
        BindlessAccessArray(completeAccesses.data(), completeAccesses.size()));
    if (!bound.success) {
        std::printf("BINDLESS_RHI_PUBLIC_SNAPSHOT_BIND_FAILED %s\n", bound.diagnostics.c_str());
        return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_SNAPSHOT_BOUND");
    const auto dispatched = pass->dispatch(1u, 1u, 1u);
    if (!dispatched.success) {
        std::printf("BINDLESS_RHI_PUBLIC_DISPATCH_FAILED %s\n", dispatched.diagnostics.c_str());
        return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_DISPATCH_RECORDED");
    const auto passEnded = pass->end();
    if (!passEnded.success) {
        std::printf("BINDLESS_RHI_PUBLIC_PASS_END_FAILED %s\n", passEnded.diagnostics.c_str());
        return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_PASS_ENDED");
    if (!encoder->transition(*counterA, ResourceState::ShaderWrite, ResourceState::CopySrc).success
        || !encoder->transition(*counterB, ResourceState::ShaderWrite, ResourceState::CopySrc)
            .success
        || !encoder->transition(*resultBuffer, ResourceState::ShaderWrite, ResourceState::CopySrc)
            .success
        || !encoder->transition(*outputs[0], ResourceState::ShaderWrite, ResourceState::CopySrc)
            .success
        || !encoder->transition(*outputs[1], ResourceState::ShaderWrite, ResourceState::CopySrc)
            .success
        || !encoder->transition(*readback, ResourceState::Undefined, ResourceState::CopyDst).success
        || !encoder->transition(*textureReadback, ResourceState::Undefined, ResourceState::CopyDst)
            .success) {
        std::puts("BINDLESS_RHI_PUBLIC_READBACK_TRANSITION_FAILED");
        return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_READBACK_TRANSITIONS_RECORDED");
    auto transfer = encoder->beginTransferPass({"public bindless readback"});
    if (!transfer
        || !transfer
            ->copyBufferToBuffer({counterA->id(), readback->id(), 0u, 0u, sizeof(std::uint32_t)})
            .success
        || !transfer
            ->copyBufferToBuffer(
                {counterB->id(), readback->id(), 0u, sizeof(std::uint32_t), sizeof(std::uint32_t)})
            .success
        || !transfer
            ->copyBufferToBuffer({resultBuffer->id(),
                readback->id(),
                0u,
                2u * sizeof(std::uint32_t),
                44u * sizeof(std::uint32_t)})
            .success
        || !transfer
            ->copyTextureToBuffer({outputs[0]->id(),
                textureReadback->id(),
                defaultTextureSubresourceRange(outputs[0]->desc()),
                0u,
                256u,
                1u,
                {1u, 1u, 1u}})
            .success
        || !transfer
            ->copyTextureToBuffer({outputs[1]->id(),
                textureReadback->id(),
                defaultTextureSubresourceRange(outputs[1]->desc()),
                256u,
                256u,
                1u,
                {1u, 1u, 1u}})
            .success
        || !transfer->end().success) {
        std::puts("BINDLESS_RHI_PUBLIC_READBACK_COPY_FAILED");
        return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_READBACK_COPY_RECORDED");
    auto commandBuffer = encoder->finish();
    if (!commandBuffer) {
        std::puts("BINDLESS_RHI_PUBLIC_COMMAND_FINISH_FAILED");
        return false;
    }
    auto* reports = static_cast<BindlessCommandBufferExtension*>(
        commandBuffer->queryInterface(kExtensionBindlessCross));
    if (!reports)
        return false;
    auto fence = device->createFence();
    if (!fence)
        return false;
    const auto submit = device->queue().submit(*commandBuffer, fence.get());
    if (!submit.hasAcceptedWork()) {
        std::printf("BINDLESS_RHI_PUBLIC_SUBMIT_FAILED %s\n", submit.diagnostics.c_str());
        return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_SUBMITTED");
    if (BindlessFaultSubmissionLease::acquire(snapshot, diagnostic).valid()
        || diagnostic != "RHI-BINDLESS-FAULT-SUBMISSION-IN-FLIGHT")
        return false;
    for (unsigned attempt = 0; attempt < 500u && !fence->isSignaled(); ++attempt)
        emscripten_sleep(10);
    if (!fence->isSignaled()) {
        std::puts("BINDLESS_RHI_PUBLIC_COMPLETION_TIMEOUT");
        return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_COMPLETED");
    auto report = reports->faultReport(snapshot->generation());
    for (unsigned attempt = 0;
        attempt < 500u && report.status == BindlessFaultValidationStatus::Pending;
        ++attempt) {
        emscripten_sleep(1);
        report = reports->faultReport(snapshot->generation());
    }
    if (report.status != BindlessFaultValidationStatus::Clean || !report.validate().success) {
        std::printf("BINDLESS_RHI_PUBLIC_FAULT_REPORT_FAILED %u %u %s\n",
            static_cast<unsigned>(report.status),
            report.faultBits,
            report.validate().diagnostics.c_str());
        return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_FAULT_CLEAN");
    {
        auto retired = BindlessFaultSubmissionLease::acquire(snapshot, diagnostic);
        if (!retired.valid() || snapshot->faultLatchPriorState() != ResourceState::CopySrc)
            return false;
    }

    auto payload = webGpuNativePayloadFor(*readback);
    if (!payload || !payload->buffer || !mapRead(payload->buffer, 46u * sizeof(std::uint32_t))) {
        std::puts("BINDLESS_RHI_PUBLIC_MAP_READ_FAILED");
        return false;
    }
    const auto* mapped = static_cast<const std::uint32_t*>(
        wgpuBufferGetConstMappedRange(payload->buffer, 0, 46u * sizeof(std::uint32_t)));
    const bool matches = mapped && mapped[0] == 11u && mapped[1] == 21u && mapped[2] == 10u
        && mapped[3] == 10u && mapped[4] == 1u && mapped[5] == 10u && mapped[6] == 1u
        && mapped[7] == 255u && mapped[8] == 2u && mapped[9] == 1u && mapped[10] == 1048576000u
        && mapped[11] == 0x00800080u && mapped[12] == 1078984704u && mapped[13] == 20u
        && mapped[14] == 20u && mapped[15] == 1u && mapped[16] == 20u && mapped[17] == 1u
        && mapped[18] == 0u && mapped[19] == 2u && mapped[20] == 1u && mapped[21] == 1061158912u
        && mapped[22] == 0x0000ff00u && mapped[23] == 1089470464u;
    bool indirectEncodingMatches =
        mapped && package->artifact().indirectInstanceBaseContractVersion == 1u;
    for (std::uint32_t i = 0u; indirectEncodingMatches && i < 2u; ++i) {
        const auto encoded = arp::rhi::semantics::IndirectInstanceBaseLowering::encode(
            {3u, 2u, 7u, -4, 1u + i * 4u}, 10u);
        const std::array<std::uint32_t, 11> expected{{encoded.physical.indexCount,
            encoded.physical.instanceCount,
            encoded.physical.firstIndex,
            static_cast<std::uint32_t>(encoded.physical.vertexOffset),
            encoded.physical.firstInstance,
            encoded.mapping.firstInstance,
            encoded.mapping.instanceCount,
            encoded.mapping.instanceCapacity,
            encoded.mapping.contractVersion,
            arp::rhi::semantics::IndirectInstanceBaseLowering::resolve(encoded.mapping, 0u)
                .logicalInstance,
            arp::rhi::semantics::IndirectInstanceBaseLowering::resolve(encoded.mapping, 1u)
                .logicalInstance}};
        for (std::uint32_t word = 0u; word < expected.size(); ++word)
            indirectEncodingMatches =
                indirectEncodingMatches && mapped[24u + i * 11u + word] == expected[word];
    }
    if ((!matches || !indirectEncodingMatches) && mapped) {
        std::printf("BINDLESS_RHI_PUBLIC_COMPUTE_VALUES");
        for (std::uint32_t i = 0; i < 46u; ++i)
            std::printf(" %u", mapped[i]);
        std::puts("");
    }
    wgpuBufferUnmap(payload->buffer);
    auto texturePayload = webGpuNativePayloadFor(*textureReadback);
    if (!texturePayload || !texturePayload->buffer || !mapRead(texturePayload->buffer, 512u))
        return false;
    const auto* outputValues =
        static_cast<const float*>(wgpuBufferGetConstMappedRange(texturePayload->buffer, 0, 512u));
    const bool outputsMatch = outputValues && outputValues[0] == 0.5f && outputValues[64] == 1.5f;
    wgpuBufferUnmap(texturePayload->buffer);
    if (matches && outputsMatch)
        std::puts(
            "BINDLESS_RHI_PUBLIC_COMPUTE_READBACK 11 21 10 10 1 10 1 255 2 1 0.25 0x00800080 3.25 20 20 1 20 1 0 2 1 0.75 0x0000ff00 7.5 0.5 1.5");
    if (indirectEncodingMatches)
        std::puts("BINDLESS_RHI_PUBLIC_INDIRECT_BASE_ENCODING_READBACK_OK");
    if (!matches || !outputsMatch || !indirectEncodingMatches)
        return false;

    // Invalid GPU-generated ranges are a separate negative frame. They must
    // suppress physical instances and yield Faulted rather than Clean.
    auto faultPipeline =
        extension->createComputePipeline(authorization, "indirectFaultMain", diagnostic);
    auto faultEncoder =
        device->createCommandEncoder({"indirect instance base fault probe", 1u, 1u});
    if (!faultPipeline || !faultEncoder) {
        std::printf("BINDLESS_RHI_PUBLIC_BASE_FAULT_PREPARE_FAILED %s\n", diagnostic.c_str());
        return false;
    }
    for (auto* writable : {counterA.get(), counterB.get(), resultBuffer.get()}) {
        const auto valid =
            faultEncoder->transition(*writable, ResourceState::CopySrc, ResourceState::ShaderWrite);
        if (!valid.success) {
            std::printf(
                "BINDLESS_RHI_PUBLIC_BASE_FAULT_TRANSITION_FAILED %s\n", valid.diagnostics.c_str());
            return false;
        }
    }
    for (const auto& output : outputs)
        if (!faultEncoder->transition(*output, ResourceState::CopySrc, ResourceState::ShaderWrite)
                .success)
            return false;
    auto faultPass = faultEncoder->beginComputePass({"indirect instance base fault probe"});
    if (!faultPass)
        return false;
    const auto faultPipelineBound = faultPass->setPipeline(*faultPipeline);
    if (!faultPipelineBound.success) {
        std::printf("BINDLESS_RHI_PUBLIC_BASE_FAULT_PIPELINE_FAILED %s\n",
            faultPipelineBound.diagnostics.c_str());
        return false;
    }
    auto* faultIndexed =
        static_cast<BindlessPassExtension*>(faultPass->queryInterface(kExtensionBindlessCross));
    if (!faultIndexed)
        return false;
    const auto faultSnapshotBound = faultIndexed->bindSnapshot(
        snapshot, plan, snapshot->generation(), completeAccesses.data(), completeAccesses.size());
    if (!faultSnapshotBound.success) {
        std::printf("BINDLESS_RHI_PUBLIC_BASE_FAULT_SNAPSHOT_FAILED %s\n",
            faultSnapshotBound.diagnostics.c_str());
        return false;
    }
    if (!faultPass->dispatch(1u, 1u, 1u).success || !faultPass->end().success
        || !faultEncoder
            ->transition(*resultBuffer, ResourceState::ShaderWrite, ResourceState::CopySrc)
            .success)
        return false;
    auto faultTransfer = faultEncoder->beginTransferPass({"indirect fault encoding readback"});
    if (!faultTransfer
        || !faultTransfer
            ->copyBufferToBuffer(
                {resultBuffer->id(), readback->id(), 0u, 0u, 22u * sizeof(std::uint32_t)})
            .success
        || !faultTransfer->end().success)
        return false;
    auto faultCommands = faultEncoder->finish();
    auto faultFence = device->createFence();
    if (!faultCommands || !faultFence)
        return false;
    auto* faultReports = static_cast<BindlessCommandBufferExtension*>(
        faultCommands->queryInterface(kExtensionBindlessCross));
    if (!faultReports
        || !device->queue().submit(*faultCommands, faultFence.get()).hasAcceptedWork())
        return false;
    for (unsigned i = 0u; i < 500u && !faultFence->isSignaled(); ++i)
        emscripten_sleep(10);
    if (!faultFence->isSignaled())
        return false;
    auto faultReport = faultReports->faultReport(snapshot->generation());
    for (unsigned i = 0u; i < 500u && faultReport.status == BindlessFaultValidationStatus::Pending;
        ++i) {
        emscripten_sleep(1);
        faultReport = faultReports->faultReport(snapshot->generation());
    }
    if (faultReport.status != BindlessFaultValidationStatus::Faulted || faultReport.faultBits != 1u
        || faultReport.validate().success || !mapRead(payload->buffer, 22u * sizeof(std::uint32_t)))
        return false;
    const auto* faultValues = static_cast<const std::uint32_t*>(
        wgpuBufferGetConstMappedRange(payload->buffer, 0u, 22u * sizeof(std::uint32_t)));
    bool suppressed = faultValues != nullptr;
    const std::array<std::uint32_t, 11> rejected{
        {3u, 0u, 7u, static_cast<std::uint32_t>(-4), 0u, 0u, 0u, 0u, 0u, 0u, 0u}};
    for (std::uint32_t i = 0u; suppressed && i < 22u; ++i)
        suppressed = faultValues[i] == rejected[i % rejected.size()];
    wgpuBufferUnmap(payload->buffer);
    if (suppressed)
        std::puts("BINDLESS_RHI_PUBLIC_INDIRECT_BASE_FAULT_PROBE_OK 1");
    return suppressed;
}

bool validatePublicProtectedPacketStorage(arp::rhi::GpuDevice& device,
    arp::rhi::GpuDevice& peer,
    const arp::rhi::BindlessRequirements& request,
    const arp::rhi::BindlessShaderArtifact& consumer)
{
    using namespace arp::rhi;
    using namespace arp::rhi_webgpu;
    arp::core::pmr::String diagnostic;
    constexpr std::uint64_t bytes = sizeof(DrawIndexedIndirectCommand)
        + sizeof(IndirectInstanceBase) + sizeof(ZeroBaseIndirectProducerParams);
    const ZeroBaseIndirectProducerParams parameters{
        5u, consumer.indirectInstanceConsumer.instanceCapacity, 0u, 1u};
    BindlessIndirectPacketPool packets(device, {1u, bytes, bytes});
    auto packet = packets.prepareShared(request, consumer, parameters, diagnostic);
    if (!packet || !packet->validate().success || packet->reportedBufferBytes() != bytes
        || packets.livePackets() != 1u || packets.chargedBytes() != bytes)
        return false;
    if (packets.prepareShared(request, consumer, parameters, diagnostic)
        || diagnostic != "RHI-BINDLESS-INDIRECT-PACKET-POOL-FULL")
        return false;
    BindlessIndirectPacketPool foreign(peer, {1u, bytes, bytes});
    if (foreign.prepareShared(request, consumer, parameters, diagnostic)
        || diagnostic != "RHI-BINDLESS-DEVICE-MISMATCH")
        return false;
    auto writable = device.createBuffer({"packet foreign host write",
        4u,
        ResourceUsage::CopySrc | ResourceUsage::CopyDst,
        MemoryUsage::CpuToGpu,
        ResourceState::CopySrc});
    const std::uint32_t zero = 0u;
    if (!writable
        || peer.writeBuffer(*writable, &zero, sizeof(zero)).diagnostics
            != "RHI-WEBGPU-BUFFER-WRITE-DEVICE-MISMATCH")
        return false;
    DescriptorLayoutDesc writableLayout;
    writableLayout.name = "protected packet writable descriptor rejection";
    writableLayout.bindings.push_back(
        {0u, DescriptorType::StorageBuffer, ShaderStage::Compute, 1u});
    auto arena = device.createDescriptorArena({"protected packet permissions", 2u, 2u});
    auto writableSet =
        arena ? arena->allocateDescriptorSet({"protected write", writableLayout}) : nullptr;
    DescriptorLayoutDesc readonlyLayout;
    readonlyLayout.name = "protected packet readonly consumer";
    readonlyLayout.bindings.push_back(
        {0u, DescriptorType::ReadOnlyStorageBuffer, ShaderStage::Vertex, 1u});
    auto readonlySet =
        arena ? arena->allocateDescriptorSet({"protected read", readonlyLayout}) : nullptr;
    if (!writableSet || !readonlySet)
        return false;
    DescriptorBindingWrite write;
    write.binding = 0u;
    write.type = DescriptorType::StorageBuffer;
    write.resourceKind = DescriptorResourceKind::Buffer;
    for (auto* output :
        {packet->physicalArgs(), packet->instanceMappings(), packet->producerParameters()}) {
        if (!output->hostWritesSealed() || !output->gpuWritesSealed()
            || !output->gpuTransferWritesSealed()
            || device.writeBuffer(*output, &zero, sizeof(zero)).diagnostics
                != "RHI-BUFFER-HOST-WRITES-SEALED")
            return false;
        auto commands =
            device.createCommandEncoder({"protected packet transfer rejection", 0u, 0u});
        auto pass = commands ? commands->beginTransferPass({"protected packet transfer rejection"})
                             : nullptr;
        BufferCopyDesc copy;
        copy.sourceBuffer = writable->id();
        copy.destinationBuffer = output->id();
        copy.sizeBytes = sizeof(zero);
        if (!pass || pass->copyBufferToBuffer(copy).diagnostics != "RHI-BUFFER-GPU-WRITES-SEALED"
            || !pass->end().success)
            return false;
        if (output != packet->producerParameters()) {
            write.resource = output->id();
            if (writableSet->write(write).diagnostics != "RHI-BUFFER-GPU-WRITES-SEALED")
                return false;
        }
    }
    write.type = DescriptorType::ReadOnlyStorageBuffer;
    write.resource = packet->instanceMappings()->id();
    if (!readonlySet->write(write).success)
        return false;
    auto readback = device.createBuffer({"protected packet initialized readback",
        bytes,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst});
    auto commands = device.createCommandEncoder({"protected packet initialized readback", 0u, 0u});
    if (!readback || !commands)
        return false;
    for (auto* buffer :
        {packet->physicalArgs(), packet->instanceMappings(), packet->producerParameters()})
        if (!commands->transition(*buffer, buffer->state(), ResourceState::CopySrc).success)
            return false;
    auto pass = commands->beginTransferPass({"protected packet initialized readback"});
    if (!pass)
        return false;
    std::uint64_t offset = 0u;
    for (auto* buffer :
        {packet->physicalArgs(), packet->instanceMappings(), packet->producerParameters()}) {
        BufferCopyDesc copy;
        copy.sourceBuffer = buffer->id();
        copy.destinationBuffer = readback->id();
        copy.destinationOffsetBytes = offset;
        copy.sizeBytes = buffer->desc().sizeBytes;
        if (!pass->copyBufferToBuffer(copy).success)
            return false;
        offset += copy.sizeBytes;
    }
    if (!pass->end().success)
        return false;
    auto recorded = commands->finish();
    if (!recorded || !device.queue().submit(*recorded).hasAcceptedWork())
        return false;
    auto nativeDevice = emscripten_webgpu_get_device();
    auto queue = nativeDevice ? wgpuDeviceGetQueue(nativeDevice) : nullptr;
    const auto nativeReadback = webGpuNativePayloadFor(*readback);
    if (!queue || !waitForQueue(queue) || !nativeReadback
        || !mapRead(nativeReadback->buffer, bytes))
        return false;
    const auto* result = static_cast<const std::uint8_t*>(
        wgpuBufferGetConstMappedRange(nativeReadback->buffer, 0u, bytes));
    const std::array<std::uint32_t, 9> suppressed{};
    const bool matched = result && std::memcmp(result, suppressed.data(), sizeof(suppressed)) == 0
        && std::memcmp(result + sizeof(suppressed), &parameters, sizeof(parameters)) == 0;
    wgpuBufferUnmap(nativeReadback->buffer);
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(nativeDevice);
    if (!matched)
        return false;
    readonlySet.reset();
    writableSet.reset();
    arena.reset();
    packet.reset();
    if (packets.livePackets() || packets.chargedBytes())
        return false;
    std::shared_ptr<BindlessIndirectPacket> survivor;
    {
        BindlessIndirectPacketPool temporary(device, {1u, bytes, bytes});
        survivor = temporary.prepareShared(request, consumer, parameters, diagnostic);
    }
    if (!survivor || !survivor->validate().success)
        return false;
    auto unopted = request;
    unopted.optIn = false;
    if (packets.prepareShared(unopted, consumer, parameters, diagnostic)
        || diagnostic != "RHI-BINDLESS-OPT-IN-REQUIRED")
        return false;
    BindlessIndirectPacketPool insufficient(device, {1u, bytes - 1u, bytes});
    if (insufficient.prepareShared(request, consumer, parameters, diagnostic)
        || diagnostic != "RHI-BINDLESS-INDIRECT-PACKET-FOOTPRINT-EXCEEDED"
        || insufficient.livePackets() || insufficient.chargedBytes())
        return false;
    std::puts("BINDLESS_RHI_PUBLIC_PROTECTED_PACKET_STORAGE_OK 52");
    return true;
}

bool validatePublicProcessorDraw(arp::rhi::GpuDevice& device,
    arp::rhi::GpuDevice& peer,
    const std::shared_ptr<const arp::rhi::BindlessIndirectProcessorIntent>& intent,
    const std::shared_ptr<const arp::rhi::BindlessShaderPackage>& consumerPackage,
    arp::rhi::GpuBuffer& source,
    const arp::rhi::BindlessAccessDeclarations& producerAccess,
    const arp::rhi::BindlessAccessDeclarations& consumerAccess)
{
    using namespace arp::rhi;
    const auto* extension =
        static_cast<const BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
    const auto& desc = intent->desc();
    arp::core::pmr::String diagnostic;
    const auto fail = [&](const char* step) {
        std::printf("BINDLESS_RHI_PUBLIC_PROCESSOR_DRAW_FAILED %s %s\n", step, diagnostic.c_str());
        return false;
    };
    const auto ownedBytes =
        std::uint64_t{desc.producer->artifact().snapshotMetadata.wordCount} * sizeof(std::uint32_t)
        + sizeof(std::uint32_t) + sizeof(DrawIndexedIndirectCommand) + sizeof(IndirectInstanceBase);
    const auto ceiling = intent->retainedBytes() + ownedBytes;
    const BindlessIndirectProcessorBudget budget{1u, ceiling, ceiling};
    if (extension->createIndirectProcessorPool(peer, budget, diagnostic)
        || diagnostic != "RHI-WEBGPU-INDIRECT-PROCESSOR-DEVICE-MISMATCH")
        return fail("foreign-pool");
    auto pool = extension->createIndirectProcessorPool(device, budget, diagnostic);
    auto processor = pool ? pool->prepare(intent, diagnostic) : nullptr;
    if (!processor || !processor->identity() || pool->liveProcessors() != 1u
        || pool->chargedBytes() != ceiling)
        return fail("prepare");
    RenderPipelineDesc state;
    state.label = "public protected packet consumer";
    state.colorFormat = TextureFormat::Rgba8Unorm;
    state.dynamicViewportScissor = true;
    auto pipeline = extension->createRenderPipeline(
        desc.consumerAuthorization, state, "vertexMain", "fragmentMain", diagnostic);
    const std::shared_ptr<const BindlessShaderPackage> aliasedPackage(
        desc.consumerAuthorization, &desc.consumerAuthorization->package());
    if (extension->authorize(aliasedPackage, *desc.consumerSnapshot, diagnostic)
        || diagnostic != "RHI-BINDLESS-AUTHORIZATION-PACKAGE-OWNER-MISMATCH")
        return fail("aliased-package-owner");
    auto wrongAuthorization =
        extension->authorize(consumerPackage, *desc.consumerSnapshot, diagnostic);
    auto wrongPipeline = extension->createRenderPipeline(
        wrongAuthorization, state, "vertexMain", "fragmentMain", diagnostic);
    TextureDesc targetDesc;
    targetDesc.label = "public protected packet target";
    targetDesc.extent = {1u, 1u, 1u};
    targetDesc.format = TextureFormat::Rgba8Unorm;
    targetDesc.usage = TextureUsage::RenderAttachment | TextureUsage::CopySrc;
    auto target = device.createTexture(targetDesc);
    auto readback = device.createBuffer({"public packet pixel and record readback",
        292u,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst});
    const std::uint32_t indices[]{0u, 1u, 2u};
    auto indexBuffer = device.createBuffer({"public packet indices",
        sizeof(indices),
        ResourceUsage::IndexBuffer | ResourceUsage::CopyDst,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    if (!pipeline || !wrongPipeline || !target || !readback || !indexBuffer
        || !device.writeBuffer(*indexBuffer, indices, sizeof(indices)).success)
        return fail("resources");
    const auto makeEncoder = [&](std::uint32_t count = 1u) {
        return device.createCommandEncoder({"public protected packet", 1u, 1u, 1u, count});
    };
    const auto record = [&](CommandEncoder& encoder,
                            std::shared_ptr<const BindlessIndirectProcessor> value,
                            const BindlessAccessDeclarations& produced) {
        auto* commands = static_cast<BindlessCommandEncoderExtension*>(
            encoder.queryInterface(kExtensionBindlessCross));
        return commands
            ? commands->recordIndirectProcessor(std::move(value), produced, consumerAccess)
            : CommandValidationResult{};
    };

    class ForgedProcessor final : public BindlessIndirectProcessor {
    public:
        explicit ForgedProcessor(std::shared_ptr<const BindlessIndirectProcessor> owner)
            : owner_(std::move(owner))
        {}

        std::uint64_t identity() const noexcept override { return owner_->identity(); }

        const BindlessIndirectProcessorIntent& intent() const noexcept override
        {
            return owner_->intent();
        }

        ValidationResult validate() const override { return owner_->validate(); }

        std::uint64_t retainedBytes() const noexcept override { return owner_->retainedBytes(); }

    private:
        std::shared_ptr<const BindlessIndirectProcessor> owner_;
    };

    {
        auto denied = peer.createCommandEncoder({"foreign RHI processor command", 1u, 1u, 1u, 1u});
        if (!denied
            || record(*denied, processor, producerAccess).diagnostics
                != "RHI-WEBGPU-INDIRECT-PROCESSOR-DEVICE-MISMATCH")
            return fail("foreign-device-record");
    }
    {
        auto denied = makeEncoder(0u);
        if (!denied
            || record(*denied, processor, producerAccess).diagnostics
                != "RHI-WEBGPU-INDIRECT-PROCESSOR-COMMAND-BUDGET")
            return fail("command-budget");
    }
    {
        auto denied = makeEncoder();
        if (!denied
            || record(*denied, arp::core::makeShared<ForgedProcessor>(processor), producerAccess)
                    .diagnostics
                != "RHI-WEBGPU-INDIRECT-PROCESSOR-PAYLOAD-MISSING"
            || record(*denied, processor, BindlessAccessArray(nullptr, 0u)).diagnostics
                != "RHI-BINDLESS-INDIRECT-PRODUCER-ACCESS-INCOMPLETE"
            || !record(*denied, processor, producerAccess).success || denied->finish()
            || denied->lastValidation().diagnostics
                != "RHI-WEBGPU-INDIRECT-PROCESSOR-CONSUMER-DRAW-MISSING")
            return fail("record-rejections");
    }
    for (unsigned rejection = 0u; rejection < 4u; ++rejection) {
        std::array<BindlessResourceAccess, 7> accesses{};
        if (consumerAccess.size() != accesses.size())
            return fail("fixture-access-count");
        for (std::size_t i = 0u; i < accesses.size(); ++i)
            if (!consumerAccess.read(i, accesses[i]))
                return fail("fixture-access-read");
        if (rejection == 1u)
            accesses[6] = {};
        if (rejection == 2u)
            accesses[0] = {};
        auto denied = makeEncoder();
        if (rejection == 3u)
            denied = device.createCommandEncoder(
                {"public packet missing snapshot budget", 1u, 0u, 1u, 1u});
        if (!denied || !record(*denied, processor, producerAccess).success
            || !denied->transition(
                          *target, ResourceState::Undefined, ResourceState::ColorAttachment)
                .success)
            return fail("wrong-consumer-record");
        RenderPassDesc passDesc;
        passDesc.colorAttachments.push_back({"target",
            target->id(),
            TextureFormat::Rgba8Unorm,
            LoadOp::Clear,
            StoreOp::Store,
            false,
            {},
            {0, 0, 1, 1}});
        auto pass = denied->beginRenderPass(passDesc);
        if (!pass || !pass->setPipeline(rejection ? *pipeline : *wrongPipeline).success)
            return fail("wrong-consumer-pass");
        auto* indexed =
            static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
        if (!indexed)
            return fail("negative-pass-extension");
        const auto bound = indexed->bindSnapshot(desc.consumerSnapshot,
            desc.consumerPlan,
            desc.consumerGeneration,
            BindlessAccessArray(accesses.data(), accesses.size()));
        if (rejection >= 2u) {
            if (bound.success
                || bound.diagnostics
                    != (rejection == 2u ? "RHI-BINDLESS-ACCESS-SET-INCOMPLETE"
                                        : "RHI-BINDLESS-COMMAND-SNAPSHOT-BUDGET")) {
                diagnostic = bound.diagnostics;
                return fail("negative-snapshot");
            }
        } else if (!bound.success
            || indexed->drawIndirectPacket(*processor).diagnostics
                != (rejection ? "RHI-BINDLESS-INDIRECT-ACCESS-INCOMPLETE"
                              : "RHI-BINDLESS-INDIRECT-PROCESSOR-CONSUMER-MISMATCH"))
            return fail("negative-draw");
        const auto ended = pass->end();
        if (rejection >= 2u) {
            if (ended.success || ended.diagnostics != "RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED")
                return fail("negative-pass-end");
        } else if (!ended.success) {
            diagnostic = ended.diagnostics;
            return fail("negative-pass-end");
        }
    }
    // A valid producer in one encoder cannot authorize a consumer in another.
    {
        auto produced = makeEncoder();
        auto foreign = makeEncoder(0u);
        if (!produced || !foreign || !record(*produced, processor, producerAccess).success
            || !foreign
                ->transition(*target, ResourceState::Undefined, ResourceState::ColorAttachment)
                .success)
            return fail("foreign-record");
        RenderPassDesc passDesc;
        passDesc.colorAttachments.push_back({"target",
            target->id(),
            TextureFormat::Rgba8Unorm,
            LoadOp::Clear,
            StoreOp::Store,
            false,
            {},
            {0, 0, 1, 1}});
        auto pass = foreign->beginRenderPass(passDesc);
        if (!pass || !pass->setPipeline(*pipeline).success)
            return fail("foreign-pass");
        auto* indexed =
            static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
        if (!indexed
            || !indexed
                ->bindSnapshot(desc.consumerSnapshot,
                    desc.consumerPlan,
                    desc.consumerGeneration,
                    consumerAccess)
                .success
            || indexed->drawIndirectPacket(*processor).diagnostics
                != "RHI-WEBGPU-INDIRECT-PROCESSOR-NOT-PRODUCED"
            || !pass->end().success)
            return fail("foreign-consumer");
    }
    std::puts("BINDLESS_RHI_PUBLIC_PROCESSOR_REJECTIONS_OK");
    for (unsigned probe = 0u; probe < 2u; ++probe) {
        const std::array<std::uint32_t, 10> logical{
            {0u, 0u, 0u, 0u, 0u, 3u, 1u, 0u, 0u, probe ? 3u : 1u}};
        if (!device.writeBuffer(source, logical.data(), sizeof(logical)).success)
            return fail("source-upload");
        auto encoder = makeEncoder();
        auto* args = desc.packet->physicalArgs();
        auto* mapping = desc.packet->instanceMappings();
        const auto before = probe ? ResourceState::ShaderRead : ResourceState::ShaderWrite;
        if (!encoder || !encoder->transition(*args, before, ResourceState::CopyDst).success
            || !encoder->transition(*mapping, before, ResourceState::CopyDst).success)
            return fail("producer-transition");
        const auto recorded = record(*encoder, processor, producerAccess);
        if (!recorded.success) {
            diagnostic = recorded.diagnostics;
            return fail("produce");
        }
        if (!encoder->transition(*args, ResourceState::CopyDst, ResourceState::ShaderRead).success
            || !encoder->transition(*mapping, ResourceState::CopyDst, ResourceState::ShaderRead)
                .success
            || !encoder
                ->transition(*target,
                    probe ? ResourceState::CopySrc : ResourceState::Undefined,
                    ResourceState::ColorAttachment)
                .success)
            return fail("consumer-transition");
        RenderPassDesc passDesc;
        passDesc.colorAttachments.push_back({"target",
            target->id(),
            TextureFormat::Rgba8Unorm,
            LoadOp::Clear,
            StoreOp::Store,
            false,
            {},
            {0, 0, 1, 1}});
        auto pass = encoder->beginRenderPass(passDesc);
        if (!pass || !pass->setPipeline(*pipeline).success)
            return fail("consumer-pass");
        auto* indexed =
            static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
        if (!indexed
            || !indexed
                ->bindSnapshot(desc.consumerSnapshot,
                    desc.consumerPlan,
                    desc.consumerGeneration,
                    consumerAccess)
                .success
            || !pass->setViewport({0, 0, 1, 1, 0, 1}).success
            || !pass->setScissor({0, 0, 1, 1}).success
            || indexed->drawIndirectPacket(*processor).diagnostics
                != "RHI-WEBGPU-INDIRECT-INDEX-BUFFER-UNBOUND"
            || !pass->bindIndexBuffer(*indexBuffer, IndexFormat::Uint32, 0u).success)
            return fail("consumer-bind");
        const auto drawn = indexed->drawIndirectPacket(*processor);
        if (!drawn.success) {
            diagnostic = drawn.diagnostics;
            return fail("draw");
        }
        if (indexed->drawIndirectPacket(*processor).diagnostics
                != "RHI-WEBGPU-INDIRECT-PROCESSOR-ALREADY-CONSUMED"
            || !pass->end().success
            || !encoder->transition(*target, ResourceState::ColorAttachment, ResourceState::CopySrc)
                .success
            || !encoder->transition(*args, ResourceState::ShaderRead, ResourceState::CopySrc)
                .success
            || !encoder->transition(*mapping, ResourceState::ShaderRead, ResourceState::CopySrc)
                .success)
            return fail("consumer-end");
        auto transfer = encoder->beginTransferPass({"public packet readback"});
        if (!transfer
            || !transfer
                ->copyTextureToBuffer({target->id(),
                    readback->id(),
                    defaultTextureSubresourceRange(target->desc()),
                    0u,
                    256u,
                    1u,
                    {1u, 1u, 1u}})
                .success
            || !transfer
                ->copyBufferToBuffer(
                    {args->id(), readback->id(), 0u, 256u, sizeof(DrawIndexedIndirectCommand)})
                .success
            || !transfer
                ->copyBufferToBuffer(
                    {mapping->id(), readback->id(), 0u, 276u, sizeof(IndirectInstanceBase)})
                .success
            || !transfer->end().success
            || !encoder->transition(*args, ResourceState::CopySrc, ResourceState::ShaderRead)
                .success
            || !encoder->transition(*mapping, ResourceState::CopySrc, ResourceState::ShaderRead)
                .success)
            return fail("readback");
        auto commands = encoder->finish();
        auto* reports = commands ? static_cast<BindlessCommandBufferExtension*>(
                                       commands->queryInterface(kExtensionBindlessCross))
                                 : nullptr;
        auto fence = device.createFence();
        const auto identity = processor->identity();
        if (!reports || !fence
            || reports->processorFaultReport(identity).status
                != BindlessFaultValidationStatus::Pending
            || !device.queue().submit(*commands, fence.get()).hasAcceptedWork())
            return fail("submit");
        std::weak_ptr<const BindlessIndirectProcessor> observed = processor;
        if (probe) {
            processor.reset();
            pool.reset();
            encoder.reset();
        }
        if (observed.expired())
            return fail("retention");
        for (unsigned i = 0u; i < 500u && !fence->isSignaled(); ++i)
            emscripten_sleep(10);
        auto produced = reports->processorFaultReport(identity);
        auto aggregate = reports->faultReport(desc.consumerGeneration);
        for (unsigned i = 0u; i < 500u
            && (produced.status == BindlessFaultValidationStatus::Pending
                || aggregate.status == BindlessFaultValidationStatus::Pending);
            ++i) {
            emscripten_sleep(10);
            produced = reports->processorFaultReport(identity);
            aggregate = reports->faultReport(desc.consumerGeneration);
        }
        {
            auto retired = BindlessFaultSubmissionLease::acquire(desc.consumerSnapshot, diagnostic);
            if (!retired.valid()
                || desc.consumerSnapshot->faultLatchPriorState() != ResourceState::CopySrc)
                return fail("terminal-lease-retirement");
        }
        const auto output = arp::rhi_webgpu::webGpuNativePayloadFor(*readback);
        if (!fence->isSignaled() || !output || !mapRead(output->buffer, 292u))
            return fail("completion");
        const auto* bytes = static_cast<const std::uint8_t*>(
            wgpuBufferGetConstMappedRange(output->buffer, 0u, 292u));
        const std::array<std::uint8_t, 4> expected = probe
            ? std::array<std::uint8_t, 4>{0u, 0u, 255u, 255u}
            : std::array<std::uint8_t, 4>{0u, 255u, 0u, 255u};
        DrawIndexedIndirectCommand logicalCommand;
        std::memcpy(&logicalCommand, logical.data() + 5u, sizeof(logicalCommand));
        const auto reference = semantics::IndirectInstanceBaseLowering::encode(logicalCommand, 2u);
        const auto status =
            probe ? BindlessFaultValidationStatus::Faulted : BindlessFaultValidationStatus::Clean;
        const bool matched = bytes && std::memcmp(bytes, expected.data(), expected.size()) == 0
            && std::memcmp(bytes + 256u, &reference.physical, sizeof(reference.physical)) == 0
            && std::memcmp(bytes + 276u, &reference.mapping, sizeof(reference.mapping)) == 0
            && produced.status == status && aggregate.status == status
            && produced.faultBits == probe && aggregate.faultBits == probe
            && aggregate.validate().success == !probe;
        std::array<std::uint32_t, 9> words{};
        if (bytes) {
            std::memcpy(words.data(), bytes + 256u, sizeof(words));
            std::printf(
                "BINDLESS_RHI_PUBLIC_PROCESSOR_READBACK %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n",
                probe,
                bytes[0],
                bytes[1],
                bytes[2],
                bytes[3],
                produced.faultBits,
                aggregate.faultBits,
                aggregate.validate().success,
                words[0],
                words[1],
                words[2],
                words[3],
                words[4],
                words[5],
                words[6],
                words[7],
                words[8]);
        }
        wgpuBufferUnmap(output->buffer);
        if (!matched || (probe && !observed.expired()))
            return fail("output");
    }
    std::puts("BINDLESS_RHI_PUBLIC_PROCESSOR_DRAW_OK 2");
    return true;
}

bool validatePublicProcessorIntent(arp::rhi::GpuDevice& device,
    arp::rhi::GpuDevice& peer,
    std::shared_ptr<const arp::rhi::BindlessShaderPackage> consumer,
    std::shared_ptr<const arp::rhi::BindlessShaderPackage> producer)
{
    using namespace arp::rhi;
    const auto* extension =
        static_cast<const BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
    if (!extension || !consumer || !producer
        || extension->capabilities().zeroBaseIndirectProducerContractVersion != 1u)
        return false;
    const auto& artifact = consumer->artifact();
    BindlessRequirements request;
    request.optIn = true;
    request.allowEmulated = true;
    request.deviceIdentity = extension->capabilities().deviceIdentity;
    request.shaderPackageIdentity = artifact.shaderPackageIdentity;
    request.budget = {1u, 5u, artifact.snapshotBytes, 2u};
    for (const auto& table : artifact.tables) {
        const auto binding = std::find_if(artifact.physicalLayout.bindings.begin(),
            artifact.physicalLayout.bindings.end(),
            [&](const auto& value) {
                return value.binding == table.firstBinding;
            });
        if (binding == artifact.physicalLayout.bindings.end())
            return false;
        request.tables.push_back({table.tableId,
            *binding,
            table.capacity,
            table.operations,
            table.nonUniform,
            table.bufferLayout});
    }
    arp::core::pmr::String diagnostic;
    const auto fail = [&](const char* step) {
        std::printf("BINDLESS_RHI_PROCESSOR_INTENT_FAILED %s %s\n", step, diagnostic.c_str());
        return false;
    };
    constexpr auto packetBytes = sizeof(DrawIndexedIndirectCommand) + sizeof(IndirectInstanceBase)
        + sizeof(ZeroBaseIndirectProducerParams);
    BindlessIndirectPacketPool packets(device, {1u, packetBytes, packetBytes});
    auto packet = packets.prepareShared(request, artifact, {5u, 2u, 0u, 1u}, diagnostic);
    if (!packet)
        return fail("packet");
    BindlessSnapshotPool snapshots(device, request.budget);
    auto snapshot = snapshots.prepareShared(request, artifact, diagnostic);
    if (!snapshot || !snapshot->writeOrdinaryBuffer(0u, *packet->instanceMappings()).success)
        return fail("snapshot");
    std::array<arp::core::UniquePtr<GpuTexture>, 2> sampled;
    std::array<arp::core::UniquePtr<GpuTextureView>, 2> views;
    TextureViewDesc viewDesc;
    viewDesc.format = TextureFormat::Rgba8Unorm;
    viewDesc.usage = TextureUsage::Sampled;
    auto uploadEncoder = device.createCommandEncoder({"processor consumer texture upload", 0u, 0u});
    if (!uploadEncoder)
        return fail("texture-upload-encoder");
    for (std::uint32_t i = 0u; i < sampled.size(); ++i) {
        sampled[i] = device.createTexture({i ? "processor green" : "processor red",
            {1u, 1u, 1u},
            TextureFormat::Rgba8Unorm,
            TextureUsage::Sampled | TextureUsage::CopyDst});
        if (!sampled[i])
            return fail("texture");
        views[i] = device.createTextureView(*sampled[i], viewDesc);
        if (!views[i]
            || !uploadEncoder
                ->transition(*sampled[i], ResourceState::Undefined, ResourceState::CopyDst)
                .success)
            return fail("texture-transition");
    }
    auto uploadPass = uploadEncoder->beginTransferPass({"processor red green upload"});
    if (!uploadPass)
        return fail("texture-upload-pass");
    for (std::uint32_t i = 0u; i < sampled.size(); ++i) {
        std::array<std::uint8_t, 256> pixels{};
        pixels[i] = 255u;
        pixels[3] = 255u;
        UploadTextureDesc upload;
        upload.destinationTexture = sampled[i]->id();
        upload.extent = {1u, 1u, 1u};
        upload.format = TextureFormat::Rgba8Unorm;
        upload.bytesPerRow = 256u;
        upload.rowsPerImage = 1u;
        upload.subresources = defaultTextureSubresourceRange(sampled[i]->desc());
        upload.pixelData = pixels.data();
        upload.pixelDataSize = pixels.size();
        if (!uploadPass->uploadTexture(upload).success)
            return fail("texture-upload");
    }
    if (!uploadPass->end().success)
        return fail("texture-upload-end");
    for (std::uint32_t i = 0u; i < sampled.size(); ++i)
        if (!uploadEncoder
                ->transition(*sampled[i], ResourceState::CopyDst, ResourceState::ShaderRead)
                .success
            || !snapshot->writeTexture(
                            snapshot->handle(1u, i), *views[i], ResourceState::ShaderRead)
                .success)
            return fail("texture-publish");
    auto uploadCommands = uploadEncoder->finish();
    if (!uploadCommands || !device.queue().submit(*uploadCommands).hasAcceptedWork())
        return fail("texture-submit");
    const auto sealed = snapshot->seal(device);
    if (!sealed.success) {
        diagnostic = sealed.diagnostics;
        return fail("seal");
    }
    auto authorization = extension->authorize(consumer, *snapshot, diagnostic);
    auto source = arp::core::shareUnique(device.createBuffer({"processor intent logical source",
        40u,
        ResourceUsage::StorageBuffer | ResourceUsage::CopyDst,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead}));
    const std::array<std::uint32_t, 10> logical{{0u, 0u, 0u, 0u, 0u, 3u, 1u, 0u, 0u, 1u}};
    if (!authorization || !source
        || !device.writeBuffer(*source, logical.data(), sizeof(logical)).success)
        return fail("authorization-source");
    BindlessIndirectProcessorIntentDesc desc{true,
        producer,
        source,
        packet,
        snapshot,
        authorization,
        snapshot->plan(),
        snapshot->generation()};
    const auto retainedBytes = source->allocation().estimatedSizeBytes
        + packet->reportedBufferBytes() + snapshot->plan().snapshotBytes + producer->sourceBytes()
        + consumer->sourceBytes();
    BindlessIndirectProcessorIntentPool intents(device, {1u, retainedBytes, retainedBytes});
    auto foreignSource = arp::core::shareUnique(peer.createBuffer(source->desc()));
    auto foreign = desc;
    foreign.logicalSource = foreignSource;
    if (!foreignSource || intents.prepare(foreign, diagnostic)
        || diagnostic != "RHI-BINDLESS-INDIRECT-PROCESSOR-SOURCE-DEVICE-MISMATCH")
        return fail("foreign-source");
    auto intent = intents.prepare(desc, diagnostic);
    if (!intent || !intent->validate().success || intent->retainedBytes() != retainedBytes
        || intents.liveIntents() != 1u || intents.chargedBytes() != retainedBytes)
        return fail("intent");
    const auto producerAccess = packet->producerAccesses(*source);
    std::array<BindlessResourceAccess, 7> consumerAccess{};
    for (std::size_t i = 0u; i < sampled.size(); ++i) {
        consumerAccess[i] = {sampled[i]->id(),
            BindlessAccessKind::Texture,
            ShaderStage::Fragment,
            PipelineStage::FragmentShader,
            ResourceAccess::ShaderRead,
            ResourceState::ShaderRead};
        consumerAccess[i].textureRange = defaultTextureSubresourceRange(sampled[i]->desc());
    }
    consumerAccess[2] = {packet->instanceMappings()->id(),
        BindlessAccessKind::Buffer,
        ShaderStage::Vertex,
        PipelineStage::VertexShader,
        ResourceAccess::ShaderRead,
        ResourceState::ShaderRead,
        0u,
        sizeof(IndirectInstanceBase)};
    consumerAccess[3] = snapshot->metadataAccess();
    consumerAccess[4] = snapshot->faultLatchAccess();
    consumerAccess[5] = snapshot->faultReadbackAccess();
    consumerAccess[6] = {packet->physicalArgs()->id(),
        BindlessAccessKind::Buffer,
        ShaderStage::None,
        PipelineStage::DrawIndirect,
        ResourceAccess::IndirectCommandRead,
        ResourceState::ShaderRead,
        0u,
        sizeof(DrawIndexedIndirectCommand)};
    const BindlessAccessArray producerDeclarations(producerAccess.data(), producerAccess.size());
    const BindlessAccessArray consumerDeclarations(consumerAccess.data(), consumerAccess.size());
    const auto record = intent->validateForRecord(producerDeclarations, consumerDeclarations);
    if (!record.success) {
        diagnostic = record.diagnostics;
        return fail("record");
    }
    {
        using namespace arp::rhi_webgpu;
        const auto ownedBytes =
            std::uint64_t{producer->artifact().snapshotMetadata.wordCount} * sizeof(std::uint32_t)
            + sizeof(std::uint32_t) + sizeof(DrawIndexedIndirectCommand)
            + sizeof(IndirectInstanceBase);
        const auto ceiling = retainedBytes + ownedBytes;
        WebGpuNativeIndirectProcessorPool invalid(device, {0u, ceiling, ceiling});
        if (invalid.prepare(intent, diagnostic)
            || diagnostic != "RHI-WEBGPU-INDIRECT-PROCESSOR-BUDGET")
            return fail("native-budget");
        WebGpuNativeIndirectProcessorPool tooSmall(device, {1u, ceiling - 1u, ceiling});
        if (tooSmall.prepare(intent, diagnostic)
            || diagnostic != "RHI-WEBGPU-INDIRECT-PROCESSOR-FOOTPRINT-EXCEEDED"
            || tooSmall.liveProcessors() || tooSmall.chargedBytes())
            return fail("native-footprint");
        WebGpuNativeIndirectProcessorPool foreignPool(peer, {1u, ceiling, ceiling});
        if (foreignPool.prepare(intent, diagnostic)
            || diagnostic != "RHI-WEBGPU-INDIRECT-PROCESSOR-DEVICE-MISMATCH")
            return fail("native-device");
        auto pool = arp::core::makeUnique<WebGpuNativeIndirectProcessorPool>(
            device, WebGpuIndirectProcessorBudget{1u, ceiling, ceiling});
        const auto nativeSource = webGpuNativePayloadFor(*source);
        nativeSource->sizeBytes--;
        const bool rejectedSource = !pool->prepare(intent, diagnostic)
            && diagnostic == "RHI-WEBGPU-INDIRECT-PROCESSOR-SOURCE-INVALID";
        nativeSource->sizeBytes++;
        if (!rejectedSource || pool->liveProcessors() || pool->chargedBytes())
            return fail("native-source-extent");
        auto processor = pool->prepare(intent, diagnostic);
        if (!processor || !processor->validate().success
            || processor->ownedBufferBytes() != ownedBytes || processor->retainedBytes() != ceiling
            || pool->liveProcessors() != 1u || pool->chargedBytes() != ceiling)
            return fail("native-prepare");
        if (pool->prepare(intent, diagnostic)
            || diagnostic != "RHI-WEBGPU-INDIRECT-PROCESSOR-POOL-FULL")
            return fail("native-pool-full");
        {
            WebGpuNativeIndirectProcessorPool residentLimited(device, {2u, ceiling, ceiling});
            auto retained = residentLimited.prepare(intent, diagnostic);
            if (!retained || residentLimited.prepare(intent, diagnostic)
                || diagnostic != "RHI-WEBGPU-INDIRECT-PROCESSOR-RESIDENT-BUDGET"
                || residentLimited.liveProcessors() != 1u
                || residentLimited.chargedBytes() != ceiling)
                return fail("native-resident-budget");
        }
        const auto limits = webGpuNativeDeviceLimits();
        auto completion = prepareWebGpuProducerCompletionOwner(2u, diagnostic);
        const auto makeCommands = [&](std::uint32_t capacity) {
            return createWebGpuNativeCommandPayload("Protected packet processor",
                limits.uniformBufferOffsetAlignment,
                limits.maxUniformBufferBindingSize,
                limits.uniformBufferOffsetAlignment,
                1u,
                {},
                false,
                diagnostic,
                1u,
                0u,
                capacity,
                completion);
        };
        if (!completion)
            return fail("native-completion-owner");
        const BindlessAccessArray empty(nullptr, 0u);
        {
            auto denied = makeCommands(0u);
            if (!denied
                || denied->recordPreparedIndirectProcessor(
                    processor, producerDeclarations, consumerDeclarations, diagnostic)
                || diagnostic != "RHI-WEBGPU-INDIRECT-PROCESSOR-COMMAND-BUDGET")
                return fail("native-command-budget");
        }
        {
            auto denied = makeCommands(1u);
            if (!denied
                || denied->recordPreparedIndirectProcessor(
                    processor, empty, consumerDeclarations, diagnostic)
                || diagnostic != "RHI-BINDLESS-INDIRECT-PRODUCER-ACCESS-INCOMPLETE"
                || denied->recordPreparedIndirectProcessor(
                    processor, producerDeclarations, empty, diagnostic))
                return fail("native-declarations");
        }
        {
            auto denied = makeCommands(1u);
            const auto destination = webGpuNativePayloadFor(*packet->physicalArgs());
            destination->sizeBytes--;
            const bool rejected = denied
                && !denied->recordPreparedIndirectProcessor(
                    processor, producerDeclarations, consumerDeclarations, diagnostic)
                && diagnostic == "RHI-WEBGPU-INDIRECT-PROCESSOR-PAYLOAD-INVALID";
            destination->sizeBytes++;
            if (!rejected)
                return fail("native-destination-extent");
        }
        {
            auto cancelled = makeCommands(1u);
            if (!cancelled
                || !cancelled->recordPreparedIndirectProcessor(
                    processor, producerDeclarations, consumerDeclarations, diagnostic))
                return fail("native-cancel-record");
        }
        if (completion->retained.pending())
            return fail("native-cancel-release");
        {
            auto unreported = makeCommands(1u);
            if (!unreported
                || !unreported->recordPreparedIndirectProcessor(
                    processor, producerDeclarations, consumerDeclarations, diagnostic)
                || unreported->finish(diagnostic)
                || diagnostic != "RHI-WEBGPU-BINDLESS-FAULT-READBACK-REQUIRED")
                return fail("native-missing-report");
        }
        auto readback = device.createBuffer({"protected packet processor readback",
            36u,
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu,
            ResourceState::Undefined});
        const auto output = readback ? webGpuNativePayloadFor(*readback) : nullptr;
        if (!output || !output->buffer)
            return fail("native-readback-buffer");
        // Both valid and GPU-invalid logical sources consume the same immutable
        // packet/intent. Invalid ranges suppress instances and must be Faulted.
        for (unsigned probe = 0u; probe < 2u; ++probe) {
            auto logicalInput = logical;
            if (probe)
                logicalInput[9] = 3u;
            if (!device.writeBuffer(*source, logicalInput.data(), sizeof(logicalInput)).success)
                return fail("native-source-upload");
            auto commands = makeCommands(1u);
            const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
            if (!commands
                || !commands->recordPreparedIndirectProcessor(
                    processor, producerDeclarations, consumerDeclarations, diagnostic)
                || arp::core::defaultCpuAllocator().stats().allocationCount != before)
                return fail("native-record-allocation");
            {
                auto competing = makeCommands(1u);
                if (!competing
                    || competing->recordPreparedIndirectProcessor(
                        processor, producerDeclarations, consumerDeclarations, diagnostic)
                    || diagnostic != "RHI-BINDLESS-INDIRECT-PACKET-IN-FLIGHT")
                    return fail("native-in-flight");
            }
            BufferCopyDesc argsCopy;
            argsCopy.sizeBytes = sizeof(DrawIndexedIndirectCommand);
            BufferCopyDesc mappingCopy;
            mappingCopy.destinationOffsetBytes = sizeof(DrawIndexedIndirectCommand);
            mappingCopy.sizeBytes = sizeof(IndirectInstanceBase);
            auto fault = commands->scheduleIndirectProcessorFaultReadback(*processor, diagnostic);
            if (!fault
                || !commands->recordBufferCopy(
                    *webGpuNativePayloadFor(*packet->physicalArgs()), *output, argsCopy, diagnostic)
                || !commands->recordBufferCopy(*webGpuNativePayloadFor(*packet->instanceMappings()),
                    *output,
                    mappingCopy,
                    diagnostic)
                || !commands->finish(diagnostic) || !commands->submit(diagnostic))
                return fail("native-submit");
            std::weak_ptr<const WebGpuNativeIndirectProcessor> observedProcessor = processor;
            if (probe) {
                processor.reset();
                pool.reset();
            }
            commands.reset();
            if (observedProcessor.expired() || completion->retained.pending() != 1u)
                return fail("native-retention");
            for (unsigned i = 0u; i < 500u && completion->retained.pending(); ++i)
                emscripten_sleep(10);
            for (unsigned i = 0u;
                i < 500u && fault->status() == BindlessFaultValidationStatus::Pending;
                ++i)
                emscripten_sleep(10);
            if (completion->retained.pending() || !mapRead(output->buffer, 36u))
                return fail("native-completion");
            DrawIndexedIndirectCommand logicalCommand;
            std::memcpy(&logicalCommand, logicalInput.data() + 5u, sizeof(logicalCommand));
            const auto reference =
                semantics::IndirectInstanceBaseLowering::encode(logicalCommand, 2u);
            const auto* bytes = static_cast<const std::uint8_t*>(
                wgpuBufferGetConstMappedRange(output->buffer, 0u, 36u));
            const bool matched = bytes
                && std::memcmp(bytes, &reference.physical, sizeof(reference.physical)) == 0
                && std::memcmp(bytes + sizeof(reference.physical),
                       &reference.mapping,
                       sizeof(reference.mapping))
                    == 0
                && fault->faultBits() == probe
                && fault->status()
                    == (probe ? BindlessFaultValidationStatus::Faulted
                              : BindlessFaultValidationStatus::Clean)
                && fault->validate().success == !probe;
            std::array<std::uint32_t, 9> observedWords{};
            if (bytes)
                std::memcpy(observedWords.data(), bytes, sizeof(observedWords));
            wgpuBufferUnmap(output->buffer);
            if (!matched || (probe && !observedProcessor.expired()))
                return fail("native-output");
            std::printf(
                "BINDLESS_RHI_PROTECTED_PROCESSOR_READBACK %u %u %u %u %u %u %u %u %u %u %u\n",
                probe,
                observedWords[0],
                observedWords[1],
                observedWords[2],
                observedWords[3],
                observedWords[4],
                observedWords[5],
                observedWords[6],
                observedWords[7],
                observedWords[8],
                fault->faultBits());
        }
        std::puts("BINDLESS_RHI_PROTECTED_PROCESSOR_OK 2");
    }
    if (!validatePublicProcessorDraw(
            device, peer, intent, consumer, *source, producerDeclarations, consumerDeclarations))
        return false;
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0u; i < 32u; ++i) {
        auto lease = BindlessIndirectProcessorRecordingLease::acquire(
            intent, 17u, producerDeclarations, consumerDeclarations, diagnostic);
        if (!lease.valid()
            || !lease.validateForRecord(*intent, 17u, producerDeclarations, consumerDeclarations)
                .success)
            return fail("lease-loop");
    }
    if (allocations != arp::core::defaultCpuAllocator().stats().allocationCount)
        return fail("allocation");
    auto lease = BindlessIndirectProcessorRecordingLease::acquire(
        intent, 17u, producerDeclarations, consumerDeclarations, diagnostic);
    if (!lease.valid()
        || BindlessIndirectProcessorRecordingLease::acquire(
            intent, 18u, producerDeclarations, consumerDeclarations, diagnostic)
            .valid()
        || diagnostic != "RHI-BINDLESS-INDIRECT-PACKET-IN-FLIGHT")
        return fail("overlap");
    std::weak_ptr<const BindlessIndirectProcessorIntent> observed = intent;
    intent.reset();
    source.reset();
    packet.reset();
    snapshot.reset();
    authorization.reset();
    desc = {};
    foreign = {};
    if (observed.expired() || !observed.lock()->validate().success)
        return fail("retention");
    lease = {};
    if (!observed.expired() || intents.liveIntents() || intents.chargedBytes())
        return fail("release");
    std::puts("BINDLESS_RHI_PUBLIC_PROCESSOR_INTENT_PREPARATION_OK");
    return true;
}

class PublicBindlessGraphicsFixture {
public:
    static bool execute(const std::shared_ptr<const arp::rhi::BindlessShaderPackage>& package,
        std::shared_ptr<const arp::rhi::BindlessShaderPackage> indirectConsumer,
        std::shared_ptr<const arp::rhi::BindlessShaderPackage> producer);

private:
    static bool executeFrame(arp::core::UniquePtr<arp::rhi::GpuDevice> device,
        arp::core::UniquePtr<arp::rhi::GpuDevice> peer,
        const std::shared_ptr<const arp::rhi::BindlessShaderPackage>& package,
        const arp::rhi::BindlessRequirements& request,
        const arp::rhi::BindlessLoweringPlan& plan);
};

bool PublicBindlessGraphicsFixture::execute(
    const std::shared_ptr<const arp::rhi::BindlessShaderPackage>& package,
    std::shared_ptr<const arp::rhi::BindlessShaderPackage> indirectConsumer,
    std::shared_ptr<const arp::rhi::BindlessShaderPackage> producer)
{
    using namespace arp::rhi;
    using namespace arp::rhi_webgpu;

    const auto limits = webGpuNativeDeviceLimits();
    WebGpuRuntimeProbe probe;
    probe.browserRuntime = true;
    probe.canvasSurface = true;
    probe.nativeDevice = true;
    probe.computeDispatch = webGpuNativeDeviceSupportsComputeDispatch();
    probe.storageTextureBinding = webGpuNativeDeviceSupportsStorageTextureBinding();
    probe.storageBuffer = webGpuNativeDeviceSupportsStorageBuffer();
    probe.maxTexture2DSize = limits.maxTexture2DSize;
    probe.maxColorAttachments = limits.maxColorAttachments;
    probe.maxDescriptorBindings = limits.maxDescriptorBindings;
    probe.maxSampledTexturesPerShaderStage = limits.maxSampledTexturesPerShaderStage;
    probe.maxSamplersPerShaderStage = limits.maxSamplersPerShaderStage;
    probe.maxUniformBuffersPerShaderStage = limits.maxUniformBuffersPerShaderStage;
    probe.maxStorageTexturesPerShaderStage = limits.maxStorageTexturesPerShaderStage;
    probe.maxStorageBuffersPerShaderStage = limits.maxStorageBuffersPerShaderStage;
    probe.maxBindGroups = limits.maxBindGroups;
    probe.uniformBufferOffsetAlignment = limits.uniformBufferOffsetAlignment;
    probe.storageBufferOffsetAlignment = limits.storageBufferOffsetAlignment;
    probe.maxUniformBufferBindingSize = limits.maxUniformBufferBindingSize;
    probe.maxStorageBufferBindingSize = limits.maxStorageBufferBindingSize;
    probe.maxComputeInvocationsPerWorkgroup = limits.maxComputeInvocationsPerWorkgroup;
    probe.maxComputeWorkgroupSizeX = limits.maxComputeWorkgroupSizeX;
    probe.maxComputeWorkgroupSizeY = limits.maxComputeWorkgroupSizeY;
    probe.maxComputeWorkgroupStorageSize = limits.maxComputeWorkgroupStorageSize;
    const auto profile = buildWebGpuRuntimeProfile(probe);
    const auto bridge = semantics::buildWebGpuRuntimeCapabilityContract(profile);
    if (!bridge.success)
        return false;
    WebGpuRhiDeviceDesc desc;
    desc.label = "Public WebGPU BindlessCross graphics fixture";
    desc.runtimeProfile = profile;
    desc.capabilityContract = bridge.contract;
    desc.memoryBudgetBytes = 8u * 1024u * 1024u;
    desc.pushConstantDeclaredWorkloadByteBudget = 4096u;
    desc.pushConstantUploadRingByteCapacity = 4096u;
    desc.uploadStagingByteCapacity = 1024u * 1024u;
    auto device = createWebGpuRhiDevice(desc);
    if (!device)
        return false;
    auto* extension =
        static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
    if (!extension)
        return false;

    BindlessRequirements request;
    request.optIn = true;
    request.allowEmulated = true;
    request.deviceIdentity = extension->capabilities().deviceIdentity;
    request.shaderPackageIdentity = package->artifact().shaderPackageIdentity;
    request.budget = {1u, 14u, package->artifact().snapshotBytes, 29u};
    for (const auto& mapping : package->artifact().tables) {
        const auto binding = std::find_if(package->artifact().physicalLayout.bindings.begin(),
            package->artifact().physicalLayout.bindings.end(),
            [&](const auto& value) {
                return value.binding == mapping.firstBinding;
            });
        if (binding == package->artifact().physicalLayout.bindings.end())
            return false;
        BindlessTableRequirement table;
        table.tableId = mapping.tableId;
        table.binding = *binding;
        table.resourceCount = mapping.capacity;
        table.operations = mapping.operations;
        table.nonUniform = mapping.nonUniform;
        table.bufferLayout = mapping.bufferLayout;
        request.tables.push_back(table);
    }
    const auto plan = extension->plan(request, package->artifact());
    if (!plan.success || !plan.executable || plan.execution != BindlessExecution::Emulated
        || plan.bindingModel != BindlessBindingModel::ScalarExpansion) {
        std::printf("BINDLESS_RHI_PUBLIC_GRAPHICS_PLAN_FAILED %s\n", plan.diagnosticId);
        return false;
    }
    arp::core::pmr::String diagnostic;
    auto peer = createWebGpuRhiDevice(desc);
    if (!peer || extension->capabilities().indirectPacketStorageContractVersion != 1u
        || extension->capabilities().zeroBaseIndirectProducerContractVersion != 1u
        || !validatePublicProtectedPacketStorage(*device, *peer, request, package->artifact()))
        return false;
    if (!validatePublicProcessorIntent(
            *device, *peer, std::move(indirectConsumer), std::move(producer)))
        return false;
    // Keep ordinary graphics locals out of the nested processor continuation.
    // Each path retains the original devices and runs under the same stack cap.
    return executeFrame(std::move(device), std::move(peer), package, request, plan);
}

bool PublicBindlessGraphicsFixture::executeFrame(arp::core::UniquePtr<arp::rhi::GpuDevice> device,
    arp::core::UniquePtr<arp::rhi::GpuDevice> peer,
    const std::shared_ptr<const arp::rhi::BindlessShaderPackage>& package,
    const arp::rhi::BindlessRequirements& request,
    const arp::rhi::BindlessLoweringPlan& plan)
{
    using namespace arp::rhi;
    using namespace arp::rhi_webgpu;
    auto* extension =
        static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
    if (!extension)
        return false;
    arp::core::pmr::String diagnostic;
    std::shared_ptr<BindlessIndirectPacket> expiredPacket;
    {
        auto peerRequest = request;
        auto* peerExtension =
            static_cast<BindlessCrossExtension*>(peer->queryInterface(kExtensionBindlessCross));
        if (!peerExtension)
            return false;
        peerRequest.deviceIdentity = peerExtension->capabilities().deviceIdentity;
        constexpr std::uint64_t packetBytes = sizeof(DrawIndexedIndirectCommand)
            + sizeof(IndirectInstanceBase) + sizeof(ZeroBaseIndirectProducerParams);
        BindlessIndirectPacketPool peerPackets(*peer, {1u, packetBytes, packetBytes});
        const ZeroBaseIndirectProducerParams peerParameters{
            5u, package->artifact().indirectInstanceConsumer.instanceCapacity, 0u, 1u};
        expiredPacket =
            peerPackets.prepareShared(peerRequest, package->artifact(), peerParameters, diagnostic);
        if (!expiredPacket || !expiredPacket->validate().success)
            return false;
    }
    peer.reset();
    if (expiredPacket->validate().diagnostics != "RHI-BINDLESS-INDIRECT-PACKET-STALE")
        return false;
    expiredPacket.reset();
    std::puts("BINDLESS_RHI_PUBLIC_PROTECTED_PACKET_DEVICE_EPOCH_OK");
    BindlessSnapshotPool snapshots(*device, request.budget);
    auto snapshot = snapshots.prepareShared(request, package->artifact(), diagnostic);
    if (!snapshot) {
        std::printf("BINDLESS_RHI_PUBLIC_GRAPHICS_SNAPSHOT_FAILED %s\n", diagnostic.c_str());
        return false;
    }

    std::array<arp::core::UniquePtr<GpuTexture>, 2> images;
    std::array<arp::core::UniquePtr<GpuTextureView>, 2> views;
    const std::array<std::array<std::uint8_t, 4>, 4> texels{{{{255u, 0u, 0u, 255u}},
        {{0u, 0u, 255u, 255u}},
        {{0u, 255u, 0u, 255u}},
        {{255u, 255u, 0u, 255u}}}};
    std::array<arp::core::UniquePtr<GpuSampler>, 2> samplers;
    for (std::size_t i = 0; i < samplers.size(); ++i) {
        SamplerDesc samplerDesc;
        samplerDesc.minFilter = samplerDesc.magFilter =
            i == 0u ? SamplerFilter::Nearest : SamplerFilter::Linear;
        samplerDesc.mipFilter = SamplerMipFilter::Nearest;
        samplers[i] = device->createSampler(samplerDesc);
        if (!samplers[i])
            return false;
    }
    for (std::size_t i = 0; i < images.size(); ++i) {
        TextureDesc imageDesc;
        imageDesc.label = i == 0u ? "public graphics red image" : "public graphics green image";
        imageDesc.extent = {2u, 1u, 1u};
        imageDesc.format = TextureFormat::Rgba8Unorm;
        imageDesc.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
        images[i] = device->createTexture(imageDesc);
        TextureViewDesc viewDesc;
        viewDesc.format = imageDesc.format;
        viewDesc.usage = TextureUsage::Sampled;
        if (images[i])
            views[i] = device->createTextureView(*images[i], viewDesc);
        if (!images[i] || !views[i])
            return false;
    }
    TextureDesc targetDesc;
    targetDesc.label = "public bindless graphics target";
    targetDesc.extent = {2u, 1u, 1u};
    targetDesc.format = TextureFormat::Rgba8Unorm;
    targetDesc.usage = TextureUsage::RenderAttachment | TextureUsage::CopySrc;
    auto target = device->createTexture(targetDesc);
    auto readback = device->createBuffer({"public bindless graphics readback",
        256u,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::Undefined});
    auto counterReadback = device->createBuffer({"public fragment counter readback",
        8u,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::Undefined});
    const auto baseEncoding =
        semantics::IndirectInstanceBaseLowering::encode({3u, 2u, 0u, 0, 0u}, 2u);
    auto instanceMappings = device->createBuffer({"public Vertex instance mappings",
        sizeof(IndirectInstanceBase),
        ResourceUsage::StorageBuffer | ResourceUsage::CopyDst | ResourceUsage::CopySrc,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    if (!instanceMappings
        || !device
            ->writeBuffer(*instanceMappings, &baseEncoding.mapping, sizeof(baseEncoding.mapping))
            .success)
        return false;
    std::array<arp::core::UniquePtr<GpuBuffer>, 2> materials;
    const std::array<std::uint32_t, 2> materialIndices{0u, 1u};
    std::array<arp::core::UniquePtr<GpuBuffer>, 2> factors;
    std::array<arp::core::UniquePtr<GpuBuffer>, 2> counters;
    const std::array<std::uint32_t, 2> counterInitial{10u, 20u};
    const std::array<std::array<float, 4>, 2> factorValues{
        {{{0.5f, 1.0f, 1.0f, 1.0f}}, {{1.0f, 0.5f, 1.0f, 1.0f}}}};
    for (std::size_t i = 0; i < materials.size(); ++i) {
        materials[i] = device->createBuffer(
            {i == 0u ? "public material index buffer zero" : "public material index buffer one",
                sizeof(std::uint32_t),
                ResourceUsage::StorageBuffer | ResourceUsage::CopyDst,
                MemoryUsage::CpuToGpu,
                ResourceState::Undefined});
        factors[i] = device->createBuffer(
            {i == 0u ? "public factor buffer zero" : "public factor buffer one",
                sizeof(factorValues[i]),
                ResourceUsage::UniformBuffer | ResourceUsage::CopyDst,
                MemoryUsage::CpuToGpu,
                ResourceState::Undefined});
        counters[i] = device->createBuffer(
            {i == 0u ? "public fragment counter zero" : "public fragment counter one",
                sizeof(std::uint32_t),
                ResourceUsage::StorageBuffer | ResourceUsage::CopySrc | ResourceUsage::CopyDst,
                MemoryUsage::CpuToGpu,
                ResourceState::Undefined});
    }
    if (!target || !readback || !counterReadback || !materials[0] || !materials[1] || !factors[0]
        || !factors[1] || !counters[0] || !counters[1])
        return false;
    for (std::size_t i = 0; i < materials.size(); ++i) {
        if (!device->writeBuffer(*materials[i], &materialIndices[i], sizeof(std::uint32_t)).success)
            return false;
        if (!device->writeBuffer(*factors[i], factorValues[i].data(), sizeof(factorValues[i]))
                .success)
            return false;
        if (!device->writeBuffer(*counters[i], &counterInitial[i], sizeof(counterInitial[i]))
                .success)
            return false;
    }

    auto uploadEncoder =
        device->createCommandEncoder({"public bindless graphics texture upload", 0u, 0u});
    if (!uploadEncoder)
        return false;
    for (const auto& image : images)
        if (!uploadEncoder->transition(*image, ResourceState::Undefined, ResourceState::CopyDst)
                .success)
            return false;
    auto uploadPass = uploadEncoder->beginTransferPass({"public graphics texture upload"});
    if (!uploadPass)
        return false;
    for (std::size_t i = 0; i < images.size(); ++i) {
        std::array<std::uint8_t, 256> pixels{};
        for (std::size_t p = 0; p < 2u; ++p)
            std::copy(
                texels[i * 2u + p].begin(), texels[i * 2u + p].end(), pixels.begin() + p * 4u);
        UploadTextureDesc upload;
        upload.destinationTexture = images[i]->id();
        upload.extent = {2u, 1u, 1u};
        upload.format = TextureFormat::Rgba8Unorm;
        upload.bytesPerRow = 256u;
        upload.rowsPerImage = 1u;
        upload.subresources = defaultTextureSubresourceRange(images[i]->desc());
        upload.pixelData = pixels.data();
        upload.pixelDataSize = static_cast<std::uint32_t>(pixels.size());
        if (!uploadPass->uploadTexture(upload).success)
            return false;
    }
    if (!uploadPass->end().success)
        return false;
    for (const auto& image : images)
        if (!uploadEncoder->transition(*image, ResourceState::CopyDst, ResourceState::ShaderRead)
                .success)
            return false;
    auto uploadCommands = uploadEncoder->finish();
    if (!uploadCommands || !device->queue().submit(*uploadCommands).hasAcceptedWork())
        return false;
    for (std::size_t i = 0; i < views.size(); ++i) {
        if (!snapshot
                ->writeTexture(snapshot->handle(1u, static_cast<std::uint32_t>(i)),
                    *views[i],
                    ResourceState::ShaderRead)
                .success)
            return false;
    }
    for (std::size_t i = 0; i < samplers.size(); ++i) {
        if (!snapshot
                ->writeSampler(snapshot->handle(2u, static_cast<std::uint32_t>(i)), *samplers[i])
                .success)
            return false;
    }
    for (std::size_t i = 0; i < materials.size(); ++i) {
        if (!snapshot
                ->writeBuffer(snapshot->handle(3u, static_cast<std::uint32_t>(i)), *materials[i])
                .success)
            return false;
        if (!snapshot->writeBuffer(snapshot->handle(4u, static_cast<std::uint32_t>(i)), *factors[i])
                .success)
            return false;
        if (!snapshot->writeBuffer(
                         snapshot->handle(5u, static_cast<std::uint32_t>(i)), *counters[i])
                .success)
            return false;
    }
    auto undersizedMappings = device->createBuffer({"rejected short instance mappings",
        12u,
        ResourceUsage::StorageBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    if (!undersizedMappings
        || snapshot->writeOrdinaryBuffer(0u, *undersizedMappings).diagnostics
            != "RHI-BINDLESS-INDIRECT-INSTANCE-CONSUMER-RANGE"
        || !snapshot->writeOrdinaryBuffer(0u, *instanceMappings).success
        || !snapshot->writeBuffer(snapshot->handle(6u, 0u), *instanceMappings).success
        || !snapshot->seal(*device).success)
        return false;
    auto authorization = extension->authorize(package, *snapshot, diagnostic);
    if (!authorization) {
        std::printf("BINDLESS_RHI_PUBLIC_GRAPHICS_AUTHORIZATION_FAILED %s\n", diagnostic.c_str());
        return false;
    }
    RenderPipelineDesc state;
    state.label = "Public bindless graphics fixture";
    state.colorFormat = TextureFormat::Rgba8Unorm;
    state.dynamicViewportScissor = true;
    const auto authoredState = validateBindlessAuthoredRenderState(state, true);
    if (!authoredState.success) {
        std::printf(
            "BINDLESS_RHI_PUBLIC_GRAPHICS_AUTHORED_STATE_FAILED %s %d %d %d %d %d %d %d %d\n",
            authoredState.diagnostics.c_str(),
            state.layout != nullptr,
            state.shaderPackage != nullptr,
            state.vertexShader.modulePath.empty(),
            state.vertexShader.entryPoint.empty(),
            state.fragmentShader.modulePath.empty(),
            state.fragmentShader.entryPoint.empty(),
            state.tessellationControlShader.modulePath.empty(),
            state.tessellationEvaluationShader.modulePath.empty());
        return false;
    }
    auto pipeline = extension->createRenderPipeline(
        authorization, state, "vertexMain", "fragmentMain", diagnostic);
    if (!pipeline) {
        std::printf("BINDLESS_RHI_PUBLIC_GRAPHICS_PIPELINE_FAILED %s\n", diagnostic.c_str());
        return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_GRAPHICS_PIPELINE_CREATED");
    std::array<BindlessResourceAccess, 12> accesses{};
    for (std::size_t i = 0; i < images.size(); ++i) {
        auto& access = accesses[i];
        access.resource = images[i]->id();
        access.kind = BindlessAccessKind::Texture;
        access.stages = ShaderStage::Fragment;
        access.pipelineStages = PipelineStage::FragmentShader;
        access.access = ResourceAccess::ShaderRead;
        access.state = ResourceState::ShaderRead;
        access.textureRange = defaultTextureSubresourceRange(images[i]->desc());
    }
    for (std::size_t i = 0; i < materials.size(); ++i) {
        auto& access = accesses[2u + i];
        access.resource = materials[i]->id();
        access.kind = BindlessAccessKind::Buffer;
        access.stages = ShaderStage::Fragment;
        access.pipelineStages = PipelineStage::FragmentShader;
        access.access = ResourceAccess::ShaderRead;
        access.state = ResourceState::ShaderRead;
        access.bufferSize = materials[i]->desc().sizeBytes;
    }
    for (std::size_t i = 0; i < factors.size(); ++i) {
        auto& access = accesses[4u + i];
        access.resource = factors[i]->id();
        access.kind = BindlessAccessKind::Buffer;
        access.stages = ShaderStage::Fragment;
        access.pipelineStages = PipelineStage::FragmentShader;
        access.access = ResourceAccess::ShaderRead;
        access.state = ResourceState::ShaderRead;
        access.bufferSize = factors[i]->desc().sizeBytes;
    }
    for (std::size_t i = 0; i < counters.size(); ++i) {
        auto& access = accesses[6u + i];
        access.resource = counters[i]->id();
        access.kind = BindlessAccessKind::Buffer;
        access.stages = ShaderStage::Fragment;
        access.pipelineStages = PipelineStage::FragmentShader;
        access.access = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
        access.state = ResourceState::ShaderWrite;
        access.bufferSize = counters[i]->desc().sizeBytes;
    }
    accesses[8] = snapshot->metadataAccess();
    accesses[9] = snapshot->faultLatchAccess();
    accesses[10] = snapshot->faultReadbackAccess();
    accesses[11].resource = instanceMappings->id();
    accesses[11].kind = BindlessAccessKind::Buffer;
    accesses[11].stages = ShaderStage::Vertex;
    accesses[11].pipelineStages = PipelineStage::VertexShader;
    accesses[11].access = ResourceAccess::ShaderRead;
    accesses[11].state = ResourceState::ShaderRead;
    accesses[11].bufferSize = sizeof(IndirectInstanceBase);

    auto mappingProducer =
        extension->createComputePipeline(authorization, "instanceBaseMain", diagnostic);
    auto mappingReadback = device->createBuffer({"GPU-produced instance mapping readback",
        sizeof(IndirectInstanceBase),
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst});
    if (!mappingProducer || !mappingReadback)
        return false;
    const auto produceMapping = [&](IndirectInstanceBase input) {
        const auto reference = semantics::IndirectInstanceBaseLowering::encode(
            {3u, input.instanceCount, 0u, 0, input.firstInstance}, input.instanceCapacity);
        if (!reference.valid()
            || !device->writeBuffer(*instanceMappings, &input, sizeof(input)).success)
            return false;
        auto producerEncoder = device->createCommandEncoder({"GPU mapping producer", 1u, 1u});
        if (!producerEncoder
            || !producerEncoder
                ->transition(
                    *instanceMappings, ResourceState::ShaderRead, ResourceState::ShaderWrite)
                .success)
            return false;
        std::array<BindlessResourceAccess, 5> producerAccesses{};
        producerAccesses[0] = accesses[11];
        producerAccesses[0].stages = ShaderStage::Compute;
        producerAccesses[0].pipelineStages = PipelineStage::ComputeShader;
        producerAccesses[0].access = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
        producerAccesses[0].state = ResourceState::ShaderWrite;
        producerAccesses[1] = snapshot->metadataAccess();
        producerAccesses[2] = snapshot->faultLatchAccess();
        producerAccesses[3] = snapshot->faultReadbackAccess();
        producerAccesses[4] = accesses[11];
        producerAccesses[4].stages = ShaderStage::None;
        producerAccesses[4].pipelineStages = PipelineStage::Transfer;
        producerAccesses[4].access = ResourceAccess::TransferRead;
        producerAccesses[4].state = ResourceState::CopySrc;
        auto producerPass = producerEncoder->beginComputePass({"GPU mapping producer"});
        if (!producerPass || !producerPass->setPipeline(*mappingProducer).success)
            return false;
        auto* producerExtension = static_cast<BindlessPassExtension*>(
            producerPass->queryInterface(kExtensionBindlessCross));
        if (!producerExtension
            || !producerExtension
                ->bindSnapshot(snapshot,
                    plan,
                    snapshot->generation(),
                    BindlessAccessArray(producerAccesses.data(), producerAccesses.size()))
                .success
            || !producerPass->dispatch(1u, 1u, 1u).success || !producerPass->end().success
            || !producerEncoder
                ->transition(*instanceMappings, ResourceState::ShaderWrite, ResourceState::CopySrc)
                .success)
            return false;
        auto producerTransfer =
            producerEncoder->beginTransferPass({"GPU mapping producer readback"});
        if (!producerTransfer
            || !producerTransfer
                ->copyBufferToBuffer({instanceMappings->id(),
                    mappingReadback->id(),
                    0u,
                    0u,
                    sizeof(IndirectInstanceBase)})
                .success
            || !producerTransfer->end().success
            || !producerEncoder
                ->transition(*instanceMappings, ResourceState::CopySrc, ResourceState::ShaderRead)
                .success)
            return false;
        auto producerCommands = producerEncoder->finish();
        if (!producerCommands)
            return false;
        auto* producerReports = static_cast<BindlessCommandBufferExtension*>(
            producerCommands->queryInterface(kExtensionBindlessCross));
        auto producerFence = device->createFence();
        if (!producerReports || !producerFence
            || !device->queue().submit(*producerCommands, producerFence.get()).hasAcceptedWork())
            return false;
        for (unsigned i = 0; i < 500u && !producerFence->isSignaled(); ++i)
            emscripten_sleep(10);
        if (!producerFence->isSignaled())
            return false;
        auto producerReport = producerReports->faultReport(snapshot->generation());
        for (unsigned i = 0;
            i < 500u && producerReport.status == BindlessFaultValidationStatus::Pending;
            ++i) {
            emscripten_sleep(1);
            producerReport = producerReports->faultReport(snapshot->generation());
        }
        if (!producerReport.validate().success)
            return false;
        const auto payload = webGpuNativePayloadFor(*mappingReadback);
        if (!payload || !mapRead(payload->buffer, sizeof(IndirectInstanceBase)))
            return false;
        const auto* data =
            wgpuBufferGetConstMappedRange(payload->buffer, 0u, sizeof(IndirectInstanceBase));
        const bool equal =
            data && std::memcmp(data, &reference.mapping, sizeof(reference.mapping)) == 0;
        wgpuBufferUnmap(payload->buffer);
        return equal;
    };
    if (!produceMapping(baseEncoding.mapping))
        return false;

    auto encoder = device->createCommandEncoder({"public bindless graphics", 1u, 1u});
    if (!encoder)
        return false;
    auto transition =
        encoder->transition(*target, ResourceState::Undefined, ResourceState::ColorAttachment);
    if (!transition.success) {
        std::printf("BINDLESS_RHI_PUBLIC_GRAPHICS_TARGET_TRANSITION_FAILED %s\n",
            transition.diagnostics.c_str());
        return false;
    }
    transition = encoder->transition(*readback, ResourceState::Undefined, ResourceState::CopyDst);
    if (!transition.success) {
        std::printf("BINDLESS_RHI_PUBLIC_GRAPHICS_READBACK_TRANSITION_FAILED %s\n",
            transition.diagnostics.c_str());
        return false;
    }
    if (!encoder->transition(*counterReadback, ResourceState::Undefined, ResourceState::CopyDst)
            .success)
        return false;
    for (const auto& counter : counters)
        if (!encoder->transition(*counter, ResourceState::Undefined, ResourceState::ShaderWrite)
                .success)
            return false;
    RenderPassDesc passDesc;
    passDesc.label = "public bindless graphics pass";
    passDesc.colorAttachments.push_back({"target",
        target->id(),
        TextureFormat::Rgba8Unorm,
        LoadOp::Clear,
        StoreOp::Store,
        false,
        {},
        {0, 0, 0, 1}});
    auto pass = encoder->beginRenderPass(passDesc);
    if (!pass) {
        std::printf("BINDLESS_RHI_PUBLIC_GRAPHICS_PASS_BEGIN_FAILED %s\n",
            encoder->lastValidation().diagnostics.c_str());
        return false;
    }
    auto bound = pass->setPipeline(*pipeline);
    if (!bound.success) {
        std::printf(
            "BINDLESS_RHI_PUBLIC_GRAPHICS_SET_PIPELINE_FAILED %s\n", bound.diagnostics.c_str());
        return false;
    }
    auto* indexed =
        static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
    if (!indexed)
        return false;
    auto wrongMappingAccess = accesses;
    wrongMappingAccess[11].stages = ShaderStage::Fragment;
    wrongMappingAccess[11].pipelineStages = PipelineStage::FragmentShader;
    if (indexed
            ->bindSnapshot(snapshot,
                plan,
                snapshot->generation(),
                BindlessAccessArray(wrongMappingAccess.data(), wrongMappingAccess.size()))
            .diagnostics
        != "RHI-BINDLESS-ACCESS-SET-INCOMPLETE")
        return false;
    auto readOnlyAccesses = accesses;
    readOnlyAccesses[6].access = ResourceAccess::ShaderRead;
    readOnlyAccesses[6].state = ResourceState::ShaderRead;
    const auto missingWrite = indexed->bindSnapshot(snapshot,
        plan,
        snapshot->generation(),
        BindlessAccessArray(readOnlyAccesses.data(), readOnlyAccesses.size()));
    if (missingWrite.success || missingWrite.diagnostics != "RHI-BINDLESS-ACCESS-SET-INCOMPLETE") {
        std::printf("BINDLESS_RHI_PUBLIC_GRAPHICS_MISSING_WRITE_CHECK_FAILED %d %s\n",
            missingWrite.success,
            missingWrite.diagnostics.c_str());
        return false;
    }
    const auto snapshotBound = indexed->bindSnapshot(snapshot,
        plan,
        snapshot->generation(),
        BindlessAccessArray(accesses.data(), accesses.size()));
    if (!snapshotBound.success) {
        std::printf("BINDLESS_RHI_PUBLIC_GRAPHICS_SNAPSHOT_BIND_FAILED %s\n",
            snapshotBound.diagnostics.c_str());
        return false;
    }
    const auto viewport = pass->setViewport({0, 0, 2, 1, 0, 1});
    const auto scissor = pass->setScissor({0, 0, 2, 1});
    if (!viewport.success || !scissor.success) {
        std::printf("BINDLESS_RHI_PUBLIC_GRAPHICS_DYNAMIC_STATE_FAILED %s %s\n",
            viewport.diagnostics.c_str(),
            scissor.diagnostics.c_str());
        return false;
    }
    const auto drawn = pass->draw(3u, 1u, 0u, 0u);
    if (!drawn.success) {
        std::printf("BINDLESS_RHI_PUBLIC_GRAPHICS_DRAW_FAILED %s\n", drawn.diagnostics.c_str());
        return false;
    }
    const auto ended = pass->end();
    if (!ended.success) {
        std::printf("BINDLESS_RHI_PUBLIC_GRAPHICS_PASS_END_FAILED %s\n", ended.diagnostics.c_str());
        return false;
    }
    for (const auto& counter : counters)
        if (!encoder->transition(*counter, ResourceState::ShaderWrite, ResourceState::CopySrc)
                .success)
            return false;
    transition =
        encoder->transition(*target, ResourceState::ColorAttachment, ResourceState::CopySrc);
    if (!transition.success) {
        std::printf("BINDLESS_RHI_PUBLIC_GRAPHICS_TARGET_READ_TRANSITION_FAILED %s\n",
            transition.diagnostics.c_str());
        return false;
    }
    auto transfer = encoder->beginTransferPass({"public bindless graphics readback"});
    if (!transfer)
        return false;
    const auto copied = transfer->copyTextureToBuffer({target->id(),
        readback->id(),
        defaultTextureSubresourceRange(target->desc()),
        0u,
        256u,
        1u,
        {2u, 1u, 1u}});
    if (!copied.success) {
        std::printf("BINDLESS_RHI_PUBLIC_GRAPHICS_COPY_FAILED %s\n", copied.diagnostics.c_str());
        return false;
    }
    for (std::size_t i = 0; i < counters.size(); ++i)
        if (!transfer
                ->copyBufferToBuffer({counters[i]->id(),
                    counterReadback->id(),
                    0u,
                    static_cast<std::uint64_t>(i * sizeof(std::uint32_t)),
                    sizeof(std::uint32_t)})
                .success)
            return false;
    if (!transfer->end().success)
        return false;
    auto commands = encoder->finish();
    if (!commands)
        return false;
    auto* reports = static_cast<BindlessCommandBufferExtension*>(
        commands->queryInterface(kExtensionBindlessCross));
    auto fence = device->createFence();
    if (!reports || !fence || !device->queue().submit(*commands, fence.get()).hasAcceptedWork())
        return false;
    for (unsigned i = 0; i < 500u && !fence->isSignaled(); ++i)
        emscripten_sleep(10);
    if (!fence->isSignaled())
        return false;
    auto report = reports->faultReport(snapshot->generation());
    for (unsigned i = 0; i < 500u && report.status == BindlessFaultValidationStatus::Pending; ++i) {
        emscripten_sleep(1);
        report = reports->faultReport(snapshot->generation());
    }
    if (report.status != BindlessFaultValidationStatus::Clean || !report.validate().success)
        return false;
    const auto output = webGpuNativePayloadFor(*readback);
    if (!output || !output->buffer || !mapRead(output->buffer, 256u))
        return false;
    const auto* pixels =
        static_cast<const std::uint8_t*>(wgpuBufferGetConstMappedRange(output->buffer, 0u, 256u));
    const bool matched = pixels && pixels[0] >= 63u && pixels[0] <= 65u && pixels[1] == 0u
        && pixels[2] >= 127u && pixels[2] <= 128u && pixels[4] == 255u && pixels[5] >= 127u
        && pixels[5] <= 128u && pixels[6] == 0u;
    wgpuBufferUnmap(output->buffer);
    const auto counterOutput = webGpuNativePayloadFor(*counterReadback);
    if (!counterOutput || !counterOutput->buffer || !mapRead(counterOutput->buffer, 8u))
        return false;
    const auto* counterValues = static_cast<const std::uint32_t*>(
        wgpuBufferGetConstMappedRange(counterOutput->buffer, 0u, 8u));
    const bool countersMatch = counterValues && counterValues[0] == 12u && counterValues[1] == 22u;
    wgpuBufferUnmap(counterOutput->buffer);
    if (matched && countersMatch) {
        std::puts("BINDLESS_RHI_PUBLIC_GRAPHICS_READBACK 64 0 128 255 255 128 0 255 0");
        std::puts("BINDLESS_RHI_PUBLIC_GRAPHICS_STORAGE_READBACK 12 22");
    }
    if (!matched || !countersMatch)
        return false;

    struct ConsumerCase {
        IndirectInstanceBase mapping;
        std::uint32_t physicalInstance;
        bool valid;
    };

    const std::array<ConsumerCase, 7> consumerCases{{{{1u, 1u, 2u, 1u}, 0u, true},
        {{0u, 2u, 2u, 0u}, 0u, false},
        {{0u, 1u, 1u, 1u}, 0u, false},
        {{0u, 2u, 3u, 1u}, 0u, false},
        {{UINT32_MAX, 2u, 2u, 1u}, 0u, false},
        {{0u, 0u, 2u, 1u}, 0u, false},
        {{0u, 1u, 2u, 1u}, 1u, false}}};
    for (const auto& test : consumerCases) {
        std::printf("BINDLESS_RHI_CONSUMER_CASE_BEGIN %u %u %u %u %u\n",
            test.mapping.firstInstance,
            test.mapping.instanceCount,
            test.mapping.instanceCapacity,
            test.mapping.contractVersion,
            test.physicalInstance);
        const auto reference = semantics::IndirectInstanceBaseLowering::resolve(
            test.mapping, test.physicalInstance, 2u);
        if (reference.valid() != test.valid
            || !device->writeBuffer(*instanceMappings, &test.mapping, sizeof(test.mapping)).success)
            return false;
        if (test.valid && !produceMapping(test.mapping))
            return false;
        for (std::size_t i = 0; i < counters.size(); ++i)
            if (!device->writeBuffer(*counters[i], &counterInitial[i], sizeof(counterInitial[i]))
                    .success)
                return false;
        auto consumerTarget = device->createTexture(targetDesc);
        auto consumerPixels = device->createBuffer({"instance consumer pixels",
            256u,
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu,
            ResourceState::Undefined});
        auto consumerCounters = device->createBuffer({"instance consumer side effects",
            8u,
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu,
            ResourceState::Undefined});
        auto consumerEncoder =
            device->createCommandEncoder({"instance consumer validation", 1u, 1u});
        if (!consumerTarget || !consumerPixels || !consumerCounters || !consumerEncoder
            || !consumerEncoder
                ->transition(
                    *consumerTarget, ResourceState::Undefined, ResourceState::ColorAttachment)
                .success
            || !consumerEncoder
                ->transition(*consumerPixels, ResourceState::Undefined, ResourceState::CopyDst)
                .success
            || !consumerEncoder
                ->transition(*consumerCounters, ResourceState::Undefined, ResourceState::CopyDst)
                .success)
            return false;
        for (const auto& counter : counters)
            if (!consumerEncoder
                    ->transition(*counter, ResourceState::CopySrc, ResourceState::ShaderWrite)
                    .success)
                return false;
        auto consumerPassDesc = passDesc;
        consumerPassDesc.colorAttachments[0].target = consumerTarget->id();
        auto consumerPass = consumerEncoder->beginRenderPass(consumerPassDesc);
        if (!consumerPass || !consumerPass->setPipeline(*pipeline).success)
            return false;
        auto* consumerExtension = static_cast<BindlessPassExtension*>(
            consumerPass->queryInterface(kExtensionBindlessCross));
        if (!consumerExtension
            || !consumerExtension
                ->bindSnapshot(snapshot,
                    plan,
                    snapshot->generation(),
                    BindlessAccessArray(accesses.data(), accesses.size()))
                .success
            || !consumerPass->setViewport({0, 0, 2, 1, 0, 1}).success
            || !consumerPass->setScissor({0, 0, 2, 1}).success
            || !consumerPass->draw(3u, 1u, 0u, test.physicalInstance).success
            || !consumerPass->end().success
            || !consumerEncoder
                ->transition(
                    *consumerTarget, ResourceState::ColorAttachment, ResourceState::CopySrc)
                .success)
            return false;
        for (const auto& counter : counters)
            if (!consumerEncoder
                    ->transition(*counter, ResourceState::ShaderWrite, ResourceState::CopySrc)
                    .success)
                return false;
        auto consumerTransfer = consumerEncoder->beginTransferPass({"instance consumer readback"});
        if (!consumerTransfer
            || !consumerTransfer
                ->copyTextureToBuffer({consumerTarget->id(),
                    consumerPixels->id(),
                    defaultTextureSubresourceRange(consumerTarget->desc()),
                    0u,
                    256u,
                    1u,
                    {2u, 1u, 1u}})
                .success)
            return false;
        for (std::size_t i = 0; i < counters.size(); ++i)
            if (!consumerTransfer
                    ->copyBufferToBuffer({counters[i]->id(),
                        consumerCounters->id(),
                        0u,
                        std::uint64_t(i * sizeof(std::uint32_t)),
                        sizeof(std::uint32_t)})
                    .success)
                return false;
        if (!consumerTransfer->end().success)
            return false;
        auto consumerCommands = consumerEncoder->finish();
        if (!consumerCommands)
            return false;
        auto* consumerReports = static_cast<BindlessCommandBufferExtension*>(
            consumerCommands->queryInterface(kExtensionBindlessCross));
        auto consumerFence = device->createFence();
        if (!consumerReports || !consumerFence
            || !device->queue().submit(*consumerCommands, consumerFence.get()).hasAcceptedWork())
            return false;
        for (unsigned i = 0; i < 500u && !consumerFence->isSignaled(); ++i)
            emscripten_sleep(10);
        if (!consumerFence->isSignaled())
            return false;
        auto consumerReport = consumerReports->faultReport(snapshot->generation());
        for (unsigned i = 0;
            i < 500u && consumerReport.status == BindlessFaultValidationStatus::Pending;
            ++i) {
            emscripten_sleep(1);
            consumerReport = consumerReports->faultReport(snapshot->generation());
        }
        if (consumerReport.status
                != (test.valid ? BindlessFaultValidationStatus::Clean
                               : BindlessFaultValidationStatus::Faulted)
            || consumerReport.faultBits != (test.valid ? 0u : 1u)
            || consumerReport.validate().success != test.valid) {
            std::printf("BINDLESS_RHI_CONSUMER_FAULT_MISMATCH %u %u\n",
                static_cast<unsigned>(consumerReport.status),
                consumerReport.faultBits);
            return false;
        }
        const auto pixelsPayload = webGpuNativePayloadFor(*consumerPixels);
        const auto counterPayload = webGpuNativePayloadFor(*consumerCounters);
        if (!pixelsPayload || !counterPayload || !mapRead(pixelsPayload->buffer, 256u)
            || !mapRead(counterPayload->buffer, 8u))
            return false;
        const auto* consumerPixelData = static_cast<const std::uint8_t*>(
            wgpuBufferGetConstMappedRange(pixelsPayload->buffer, 0u, 256u));
        const auto* consumerCounterData = static_cast<const std::uint32_t*>(
            wgpuBufferGetConstMappedRange(counterPayload->buffer, 0u, 8u));
        const std::array<std::uint8_t, 8> expected = test.valid
            ? std::array<std::uint8_t, 8>{32u, 0u, 64u, 128u, 128u, 64u, 0u, 128u}
            : std::array<std::uint8_t, 8>{128u, 255u, 0u, 255u, 128u, 255u, 0u, 255u};
        const bool consumerMatched = consumerPixelData && consumerCounterData
            && std::equal(expected.begin(),
                expected.end(),
                consumerPixelData,
                [](std::uint8_t expectedValue, std::uint8_t actualValue) {
                    const auto delta = int(expectedValue) - int(actualValue);
                    return delta >= -1 && delta <= 1;
                })
            && consumerCounterData[0] == (test.valid ? 12u : 10u)
            && consumerCounterData[1] == (test.valid ? 22u : 20u);
        if (!consumerMatched && consumerPixelData && consumerCounterData)
            std::printf(
                "BINDLESS_RHI_CONSUMER_PIXEL_MISMATCH %u %u %u %u %u %u %u %u counters=%u,%u\n",
                consumerPixelData[0],
                consumerPixelData[1],
                consumerPixelData[2],
                consumerPixelData[3],
                consumerPixelData[4],
                consumerPixelData[5],
                consumerPixelData[6],
                consumerPixelData[7],
                consumerCounterData[0],
                consumerCounterData[1]);
        if (consumerMatched)
            std::printf(
                "BINDLESS_RHI_CONSUMER_CASE_READBACK %u %u %u %u %u %u %u %u counters=%u,%u fault=%u\n",
                consumerPixelData[0],
                consumerPixelData[1],
                consumerPixelData[2],
                consumerPixelData[3],
                consumerPixelData[4],
                consumerPixelData[5],
                consumerPixelData[6],
                consumerPixelData[7],
                consumerCounterData[0],
                consumerCounterData[1],
                consumerReport.faultBits);
        wgpuBufferUnmap(pixelsPayload->buffer);
        wgpuBufferUnmap(counterPayload->buffer);
        if (!consumerMatched)
            return false;
    }
    std::puts("BINDLESS_RHI_PUBLIC_INDIRECT_BASE_CONSUMER_READBACK_OK 1 6");
    std::puts("BINDLESS_RHI_PUBLIC_GPU_MAPPING_CONSUMER_READBACK_OK");
    if (!device->writeBuffer(*instanceMappings, &baseEncoding.mapping, sizeof(baseEncoding.mapping))
            .success)
        return false;
    // This is a second graphics validation frame over the same snapshot. Reset
    // the writable inputs after the first submission completes so both passes
    // exercise identical shader preconditions, including the atomic old value.
    for (std::size_t i = 0; i < counters.size(); ++i)
        if (!device->writeBuffer(*counters[i], &counterInitial[i], sizeof(counterInitial[i]))
                .success)
            return false;
    const std::uint32_t indices[] = {0u, 1u, 2u};
    const DrawIndexedIndirectCommand indirectRecord{3u, 1u, 0u, 0, 1u};
    auto indexBuffer = device->createBuffer({"public indexed indirect indices",
        sizeof(indices),
        ResourceUsage::IndexBuffer | ResourceUsage::CopyDst,
        MemoryUsage::CpuToGpu,
        ResourceState::Undefined});
    auto argsBuffer = device->createBuffer({"public indexed indirect args",
        sizeof(indirectRecord),
        ResourceUsage::StorageBuffer | ResourceUsage::IndirectBuffer | ResourceUsage::CopyDst,
        MemoryUsage::CpuToGpu,
        ResourceState::Undefined});
    auto undeclaredArgs = device->createBuffer({"public undeclared indirect args",
        sizeof(indirectRecord),
        ResourceUsage::StorageBuffer | ResourceUsage::IndirectBuffer | ResourceUsage::CopyDst,
        MemoryUsage::CpuToGpu,
        ResourceState::Undefined});
    auto indirectTarget = device->createTexture(targetDesc);
    auto indirectReadback = device->createBuffer({"public bindless indirect readback",
        256u,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::Undefined});
    if (!indexBuffer || !argsBuffer || !undeclaredArgs || !indirectTarget || !indirectReadback
        || !device->writeBuffer(*indexBuffer, indices, sizeof(indices)).success
        || !device->writeBuffer(*argsBuffer, &indirectRecord, sizeof(indirectRecord)).success
        || !device->writeBuffer(*undeclaredArgs, &indirectRecord, sizeof(indirectRecord)).success)
        return false;
    std::array<BindlessResourceAccess, 13> indirectAccesses{};
    std::copy(accesses.begin(), accesses.end(), indirectAccesses.begin());
    indirectAccesses[12].resource = argsBuffer->id();
    indirectAccesses[12].kind = BindlessAccessKind::Buffer;
    indirectAccesses[12].pipelineStages = PipelineStage::DrawIndirect;
    indirectAccesses[12].access = ResourceAccess::IndirectCommandRead;
    indirectAccesses[12].state = ResourceState::ShaderRead;
    indirectAccesses[12].bufferSize = sizeof(indirectRecord);
    auto indirectEncoder =
        device->createCommandEncoder({"public bindless indirect graphics", 1u, 1u, 1u});
    if (!indirectEncoder
        || !indirectEncoder
            ->transition(*indirectTarget, ResourceState::Undefined, ResourceState::ColorAttachment)
            .success
        || !indirectEncoder
            ->transition(*argsBuffer, ResourceState::Undefined, ResourceState::ShaderRead)
            .success
        || !indirectEncoder
            ->transition(*indexBuffer, ResourceState::Undefined, ResourceState::ShaderRead)
            .success
        || !indirectEncoder
            ->transition(*indirectReadback, ResourceState::Undefined, ResourceState::CopyDst)
            .success)
        return false;
    for (const auto& counter : counters)
        if (!indirectEncoder
                ->transition(*counter, ResourceState::CopySrc, ResourceState::ShaderWrite)
                .success)
            return false;
    RenderPassDesc indirectPassDesc = passDesc;
    indirectPassDesc.colorAttachments[0].target = indirectTarget->id();
    auto indirectPass = indirectEncoder->beginRenderPass(indirectPassDesc);
    if (!indirectPass || !indirectPass->setPipeline(*pipeline).success)
        return false;
    auto* indirectExtension =
        static_cast<BindlessPassExtension*>(indirectPass->queryInterface(kExtensionBindlessCross));
    if (!indirectExtension)
        return false;
    const auto indirectBound = indirectExtension->bindSnapshot(snapshot,
        plan,
        snapshot->generation(),
        BindlessAccessArray(indirectAccesses.data(), indirectAccesses.size()));
    if (!indirectBound.success) {
        std::printf(
            "BINDLESS_RHI_PUBLIC_INDIRECT_BIND_FAILED %s\n", indirectBound.diagnostics.c_str());
        return false;
    }
    if (!extension->capabilities().indirectFirstInstance) {
        const auto rejected =
            indirectPass->drawIndexedIndirect(*argsBuffer, 0u, 1u, sizeof(indirectRecord));
        if (rejected.success
            || rejected.diagnostics != "RHI-BINDLESS-INDIRECT-FIRST-INSTANCE-UNAVAILABLE"
            || !indirectPass->end().success)
            return false;
        std::puts("BINDLESS_RHI_PUBLIC_INDIRECT_FEATURE_UNAVAILABLE");
        return true;
    }
    indirectAccesses[12].resource = readback->id();
    if (indirectPass->drawIndexedIndirect(*undeclaredArgs, 0u, 1u, sizeof(indirectRecord))
                .diagnostics
            != "RHI-BINDLESS-INDIRECT-ACCESS-INCOMPLETE"
        || indirectPass->drawIndexedIndirect(*argsBuffer, 0u, 2u, sizeof(indirectRecord))
                .diagnostics
            != "RHI-BINDLESS-INDIRECT-MULTIDRAW-UNAVAILABLE"
        || !indirectPass->setViewport({0, 0, 2, 1, 0, 1}).success
        || !indirectPass->setScissor({0, 0, 2, 1}).success)
        return false;
    if (indirectPass->drawIndexedIndirect(*argsBuffer, 0u, 1u, sizeof(indirectRecord)).diagnostics
            != "RHI-WEBGPU-INDIRECT-INDEX-BUFFER-UNBOUND"
        || !indirectPass->bindIndexBuffer(*indexBuffer, IndexFormat::Uint32, 0u).success)
        return false;
    const auto indirectDraw =
        indirectPass->drawIndexedIndirect(*argsBuffer, 0u, 1u, sizeof(indirectRecord));
    if (!indirectDraw.success) {
        std::printf(
            "BINDLESS_RHI_PUBLIC_INDIRECT_DRAW_FAILED %s\n", indirectDraw.diagnostics.c_str());
        return false;
    }
    if (!indirectPass->end().success
        || !indirectEncoder
            ->transition(*indirectTarget, ResourceState::ColorAttachment, ResourceState::CopySrc)
            .success)
        return false;
    auto indirectTransfer = indirectEncoder->beginTransferPass({"bindless indirect readback"});
    if (!indirectTransfer
        || !indirectTransfer
            ->copyTextureToBuffer({indirectTarget->id(),
                indirectReadback->id(),
                defaultTextureSubresourceRange(indirectTarget->desc()),
                0u,
                256u,
                1u,
                {2u, 1u, 1u}})
            .success
        || !indirectTransfer->end().success)
        return false;
    auto indirectCommands = indirectEncoder->finish();
    if (!indirectCommands)
        return false;
    auto* indirectReports = static_cast<BindlessCommandBufferExtension*>(
        indirectCommands->queryInterface(kExtensionBindlessCross));
    auto indirectFence = device->createFence();
    if (!indirectReports || !indirectFence
        || indirectReports->faultReport(snapshot->generation()).status
            != BindlessFaultValidationStatus::Pending
        || !device->queue().submit(*indirectCommands, indirectFence.get()).hasAcceptedWork())
        return false;
    for (unsigned i = 0; i < 500u && !indirectFence->isSignaled(); ++i)
        emscripten_sleep(10);
    if (!indirectFence->isSignaled())
        return false;
    auto indirectReport = indirectReports->faultReport(snapshot->generation());
    for (unsigned i = 0;
        i < 500u && indirectReport.status == BindlessFaultValidationStatus::Pending;
        ++i) {
        emscripten_sleep(1);
        indirectReport = indirectReports->faultReport(snapshot->generation());
    }
    if (indirectReport.status != BindlessFaultValidationStatus::Clean
        || !indirectReport.validate().success)
        return false;
    const auto indirectOutput = webGpuNativePayloadFor(*indirectReadback);
    if (!indirectOutput || !indirectOutput->buffer || !mapRead(indirectOutput->buffer, 256u))
        return false;
    const auto* indirectPixels = static_cast<const std::uint8_t*>(
        wgpuBufferGetConstMappedRange(indirectOutput->buffer, 0u, 256u));
    const bool indirectMatched = indirectPixels && indirectPixels[0] >= 31u
        && indirectPixels[0] <= 33u && indirectPixels[1] == 0u && indirectPixels[2] >= 63u
        && indirectPixels[2] <= 65u && indirectPixels[4] >= 127u && indirectPixels[4] <= 128u
        && indirectPixels[5] >= 63u && indirectPixels[5] <= 65u && indirectPixels[6] == 0u;
    if (!indirectMatched && indirectPixels)
        std::printf("BINDLESS_RHI_PUBLIC_INDIRECT_PIXEL_MISMATCH %u %u %u %u %u %u %u %u\n",
            indirectPixels[0],
            indirectPixels[1],
            indirectPixels[2],
            indirectPixels[3],
            indirectPixels[4],
            indirectPixels[5],
            indirectPixels[6],
            indirectPixels[7]);
    wgpuBufferUnmap(indirectOutput->buffer);
    if (indirectMatched)
        std::puts("BINDLESS_RHI_PUBLIC_INDIRECT_READBACK 32 0 64 255 128 64 0 255 0");
    return indirectMatched;
}

WGPUBuffer uploadBuffer(
    WGPUDevice device, WGPUQueue queue, const void* data, std::size_t size, WGPUBufferUsage usage)
{
    auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = static_cast<WGPUBufferUsage>(usage | WGPUBufferUsage_CopyDst);
    auto buffer = wgpuDeviceCreateBuffer(device, &desc);
    if (buffer)
        wgpuQueueWriteBuffer(queue, buffer, 0, data, size);
    return buffer;
}

arp::core::pmr::Vector<std::uint32_t> snapshotMetadata(
    const arp::rhi::BindlessShaderPackage& package)
{
    using namespace arp::rhi;

    class Source final : public BindlessSnapshotMetadataSource {
    public:
        explicit Source(const BindlessShaderArtifact& artifact) : artifact_(artifact) {}

        std::size_t tableCount() const noexcept override { return artifact_.tables.size(); }

        bool readTable(std::size_t i, BindlessMetadataTableSource& out) const override
        {
            out = {&artifact_.tables[i], artifact_.tables[i].capacity};
            return true;
        }

        bool readElementCount(std::size_t i, std::uint32_t, std::uint32_t& out) const override
        {
            out = artifact_.tables[i].bufferLayout.elementStrideBytes ? 1u : 0u;
            return true;
        }

    private:
        const BindlessShaderArtifact& artifact_;
    } source(package.artifact());

    arp::core::pmr::Vector<std::uint32_t> metadata;
    metadata.resize(package.artifact().snapshotMetadata.wordCount);
    if (!buildBindlessSnapshotMetadata(
            package.artifact().snapshotMetadata, 1u, source, metadata.data(), metadata.size())
            .success)
        return {};
    return metadata;
}

bool executePreparedZeroBaseProducer(const arp::rhi::BindlessShaderPackage& package,
    const arp::rhi::BindlessShaderPackage& ordinaryPackage)
{
    using namespace arp::rhi;
    using namespace arp::rhi_webgpu;
    auto device = emscripten_webgpu_get_device();
    if (!device)
        return false;
    auto queue = wgpuDeviceGetQueue(device);
    if (!queue)
        return false;
    const auto limits = webGpuNativeDeviceLimits();
    arp::core::pmr::String diagnostic;
    if (prepareWebGpuProducerCompletionOwner(0u, diagnostic)
        || diagnostic != "RHI-WEBGPU-PRODUCER-COMPLETION-BUDGET")
        return false;
    // This fixture needs at most the active command and one competing command.
    auto completionOwner = prepareWebGpuProducerCompletionOwner(2u, diagnostic);
    if (!completionOwner)
        return false;
    const std::array<DrawIndexedIndirectCommand, 5> logical{{{3u, 1u, 2u, -3, 0x01000000u},
        {3u, 0u, 2u, -3, 2u},
        {3u, 1u, 2u, -3, 2u},
        {3u, 1u, 2u, -3, 0u},
        {3u, 1u, 2u, -3, 0u}}};
    for (std::size_t probe = 0u; probe < logical.size(); ++probe) {
        const ZeroBaseIndirectProducerParams parameters{probe == 3u ? UINT32_MAX : 5u,
            probe == 0u       ? 0x01000001u
                : probe == 4u ? 0u
                              : 2u,
            0u,
            1u};
        std::array<std::uint32_t, 10> source{};
        std::memcpy(source.data() + 5u, &logical[probe], sizeof(DrawIndexedIndirectCommand));
        const DrawIndexedIndirectCommand initial{};
        const IndirectInstanceBase initialMapping{};
        const std::uint32_t initialFault = 0u;
        auto metadata = snapshotMetadata(package);
        if (metadata.empty())
            return false;
        for (const auto& table : package.artifact().tables)
            metadata[table.metadataWordOffset + 4u] = table.tableId == 1u ? source.size()
                : table.tableId == 2u                                     ? 5u
                : table.tableId == 3u                                     ? 4u
                                                                          : 1u;
        std::array<std::shared_ptr<WebGpuNativeBufferPayload>, 6> buffers{};
        const auto add =
            [&](std::size_t index, const void* data, std::size_t size, WGPUBufferUsage usage) {
                auto buffer = uploadBuffer(device, queue, data, size, usage);
                if (!buffer)
                    return false;
                buffers[index] = arp::core::makeShared<WebGpuNativeBufferPayload>();
                buffers[index]->buffer = buffer;
                buffers[index]->sizeBytes = size;
                buffers[index]->ownerDevice = device;
                return true;
            };
        if (!add(0u,
                metadata.data(),
                metadata.size() * sizeof(std::uint32_t),
                WGPUBufferUsage_Storage)
            || !add(1u,
                &initialFault,
                sizeof(initialFault),
                WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc)
            || !add(2u, source.data(), sizeof(source), WGPUBufferUsage_Storage)
            || !add(
                3u, &initial, sizeof(initial), WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc)
            || !add(4u,
                &initialMapping,
                sizeof(initialMapping),
                WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc)
            || !add(5u, &parameters, sizeof(parameters), WGPUBufferUsage_Uniform))
            return false;
        auto descriptors = arp::core::makeShared<WebGpuNativeDescriptorGroupPayload>();
        for (const auto& binding : package.artifact().physicalLayout.bindings) {
            WebGpuNativeDescriptorBindingPayload entry;
            entry.binding = binding;
            entry.write.binding = binding.binding;
            entry.write.type = binding.type;
            entry.write.resourceKind = DescriptorResourceKind::Buffer;
            if (binding.binding == package.artifact().snapshotMetadata.binding)
                entry.buffer = buffers[0];
            else if (binding.binding == package.artifact().faultLatch.binding)
                entry.buffer = buffers[1];
            else {
                const auto table = std::find_if(package.artifact().tables.begin(),
                    package.artifact().tables.end(),
                    [&](const auto& value) {
                        return value.firstBinding == binding.binding;
                    });
                if (table == package.artifact().tables.end() || table->tableId > 4u
                    || !table->tableId)
                    return false;
                entry.buffer = buffers[table->tableId + 1u];
            }
            descriptors->bindings.push_back(std::move(entry));
        }
        if (prepareWebGpuNativeZeroBaseIndirectProducer(ordinaryPackage, descriptors, diagnostic)
            || diagnostic != "RHI-WEBGPU-ZERO-BASE-PRODUCER-PACKAGE-INVALID")
            return false;
        auto malformed = arp::core::makeShared<WebGpuNativeDescriptorGroupPayload>();
        malformed->bindings = descriptors->bindings;
        malformed->bindings.pop_back();
        if (prepareWebGpuNativeZeroBaseIndirectProducer(package, malformed, diagnostic)
            || diagnostic != "RHI-WEBGPU-ZERO-BASE-PRODUCER-DESCRIPTORS-INVALID")
            return false;
        malformed->bindings = descriptors->bindings;
        for (auto& entry : malformed->bindings)
            if (entry.buffer == buffers[2u])
                entry.buffer = buffers[3u];
        if (prepareWebGpuNativeZeroBaseIndirectProducer(package, malformed, diagnostic)
            || diagnostic != "RHI-WEBGPU-ZERO-BASE-PRODUCER-ALIAS-UNSUPPORTED")
            return false;
        buffers[5]->sizeBytes--;
        if (prepareWebGpuNativeZeroBaseIndirectProducer(package, descriptors, diagnostic)
            || diagnostic != "RHI-WEBGPU-ZERO-BASE-PRODUCER-BUFFER-INVALID")
            return false;
        buffers[5]->sizeBytes++;
        buffers[2]->ownerDevice = nullptr;
        if (prepareWebGpuNativeZeroBaseIndirectProducer(package, descriptors, diagnostic)
            || diagnostic != "RHI-WEBGPU-ZERO-BASE-PRODUCER-DEVICE-MISMATCH")
            return false;
        buffers[2]->ownerDevice = device;
        auto producer =
            prepareWebGpuNativeZeroBaseIndirectProducer(package, descriptors, diagnostic);
        if (!producer) {
            std::fprintf(stderr, "RHI-WEBGPU-PREPARED-PRODUCER: %s\n", diagnostic.c_str());
            return false;
        }
        const auto makeCommands = [&](std::uint32_t capacity) {
            return createWebGpuNativeCommandPayload("Prepared zero-base producer",
                limits.uniformBufferOffsetAlignment,
                limits.maxUniformBufferBindingSize,
                limits.uniformBufferOffsetAlignment,
                1u,
                {},
                false,
                diagnostic,
                1u,
                0u,
                capacity,
                completionOwner);
        };
        if (makeCommands(2u) || diagnostic != "RHI-WEBGPU-ZERO-BASE-PRODUCER-FAULT-BUDGET")
            return false;
        if (createWebGpuNativeCommandPayload("Missing completion owner",
                limits.uniformBufferOffsetAlignment,
                limits.maxUniformBufferBindingSize,
                limits.uniformBufferOffsetAlignment,
                1u,
                {},
                false,
                diagnostic,
                1u,
                0u,
                1u)
            || diagnostic != "RHI-WEBGPU-PRODUCER-COMPLETION-OWNER-MISSING-OR-CLOSED")
            return false;
        {
            auto denied = makeCommands(0u);
            if (!denied || !denied->recordComputePassBegin({"no producer capacity"}, diagnostic)
                || denied->recordPreparedZeroBaseIndirectProducer(producer, diagnostic)
                || diagnostic != "RHI-WEBGPU-ZERO-BASE-PRODUCER-COMMAND-BUDGET")
                return false;
        }
        {
            auto uncleared = makeCommands(1u);
            if (!uncleared
                || !uncleared->recordComputePassBegin({"uncleared producer fault"}, diagnostic)
                || uncleared->recordPreparedZeroBaseIndirectProducer(producer, diagnostic)
                || diagnostic != "RHI-WEBGPU-ZERO-BASE-PRODUCER-FAULT-NOT-CLEARED")
                return false;
        }
        {
            auto cancelled = makeCommands(1u);
            if (!cancelled || !cancelled->recordBindlessFaultLatchClear(buffers[1u], diagnostic)
                || !cancelled->recordComputePassBegin({"cancelled producer"}, diagnostic)
                || !cancelled->recordPreparedZeroBaseIndirectProducer(producer, diagnostic))
                return false;
        }
        if (completionOwner->retained.pending() != 0u)
            return false;
        {
            auto missingReport = makeCommands(1u);
            if (!missingReport
                || !missingReport->recordBindlessFaultLatchClear(buffers[1u], diagnostic)
                || !missingReport->recordComputePassBegin(
                    {"missing producer fault readback"}, diagnostic)
                || !missingReport->recordPreparedZeroBaseIndirectProducer(producer, diagnostic)
                || !missingReport->recordComputePassEnd(diagnostic)
                || missingReport->finish(diagnostic)
                || diagnostic != "RHI-WEBGPU-BINDLESS-FAULT-READBACK-REQUIRED")
                return false;
        }
        auto commands = makeCommands(1u);
        if (!commands || !commands->recordBindlessFaultLatchClear(buffers[1u], diagnostic)
            || !commands->recordComputePassBegin({"prepared producer"}, diagnostic))
            return false;
        const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
        if (!commands->recordPreparedZeroBaseIndirectProducer(producer, diagnostic)
            || arp::core::defaultCpuAllocator().stats().allocationCount != allocations)
            return false;
        {
            auto alias =
                prepareWebGpuNativeZeroBaseIndirectProducer(package, descriptors, diagnostic);
            auto aliasCommands = createWebGpuNativeCommandPayload("Aliased prepared producer",
                limits.uniformBufferOffsetAlignment,
                limits.maxUniformBufferBindingSize,
                limits.uniformBufferOffsetAlignment,
                1u,
                {},
                false,
                diagnostic,
                2u,
                0u,
                2u,
                completionOwner);
            // The distinct object shares the fault latch. Its first use is
            // independent of the producer's own single-recording lease.
            if (!alias || !aliasCommands
                || !aliasCommands->recordBindlessFaultLatchClear(buffers[1u], diagnostic)
                || !aliasCommands->recordComputePassBegin({"fault alias rejection"}, diagnostic)
                || !aliasCommands->recordPreparedZeroBaseIndirectProducer(alias, diagnostic))
                return false;
            auto secondAlias =
                prepareWebGpuNativeZeroBaseIndirectProducer(package, descriptors, diagnostic);
            if (!secondAlias
                || aliasCommands->recordPreparedZeroBaseIndirectProducer(secondAlias, diagnostic)
                || diagnostic != "RHI-WEBGPU-ZERO-BASE-PRODUCER-FAULT-ALIAS")
                return false;
            if (makeCommands(1u) || diagnostic != "RHI-WEBGPU-PRODUCER-COMPLETION-CAPACITY"
                || completionOwner->retained.pending() != 2u)
                return false;
        }
        {
            auto competing = makeCommands(1u);
            if (!competing || !competing->recordBindlessFaultLatchClear(buffers[1u], diagnostic)
                || !competing->recordComputePassBegin({"competing producer"}, diagnostic)
                || competing->recordPreparedZeroBaseIndirectProducer(producer, diagnostic)
                || diagnostic != "RHI-WEBGPU-ZERO-BASE-PRODUCER-IN-FLIGHT")
                return false;
        }
        if (!commands->recordComputePassEnd(diagnostic))
            return false;
        auto readbackDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        readbackDesc.size = sizeof(DrawIndexedIndirectCommand) + sizeof(IndirectInstanceBase)
            + sizeof(initialFault);
        readbackDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        auto readback = arp::core::makeShared<WebGpuNativeBufferPayload>();
        readback->buffer = wgpuDeviceCreateBuffer(device, &readbackDesc);
        readback->sizeBytes = readbackDesc.size;
        readback->ownerDevice = device;
        if (!readback->buffer)
            return false;
        const std::array<std::uint32_t, 3> outputs{3u, 4u, 1u};
        std::uint64_t destination = 0u;
        for (const auto index : outputs) {
            BufferCopyDesc copy;
            copy.destinationOffsetBytes = destination;
            copy.sizeBytes = buffers[index]->sizeBytes;
            if (!commands->recordBufferCopy(*buffers[index], *readback, copy, diagnostic))
                return false;
            destination += copy.sizeBytes;
        }
        auto fault = WebGpuFaultColdPreparationFixture::scheduleAndSubmit(
            *commands, buffers[1u], diagnostic);
        if (!fault)
            return false;
        std::weak_ptr<WebGpuNativeZeroBaseIndirectProducer> retained = producer;
        producer.reset();
        commands.reset();
        if (retained.expired() || completionOwner->retained.pending() != 1u || !waitForQueue(queue)
            || !mapRead(readback->buffer, readbackDesc.size))
            return false;
        for (unsigned attempt = 0u; attempt < 500u && !retained.expired(); ++attempt)
            emscripten_sleep(10);
        if (!retained.expired() || completionOwner->retained.pending() != 0u)
            return false;
        for (unsigned attempt = 0u;
            attempt < 500u && fault->status() == BindlessFaultValidationStatus::Pending;
            ++attempt)
            emscripten_sleep(10);
        const auto reference = semantics::IndirectInstanceBaseLowering::encode(
            logical[probe], parameters.instanceCapacity);
        const DrawIndexedIndirectCommand expectedArgs =
            probe == 3u ? DrawIndexedIndirectCommand{} : reference.physical;
        const IndirectInstanceBase expectedMapping =
            probe == 3u ? IndirectInstanceBase{} : reference.mapping;
        const std::uint32_t expectedFault = probe < 2u ? 0u : 1u;
        const auto* bytes = static_cast<const std::uint8_t*>(
            wgpuBufferGetConstMappedRange(readback->buffer, 0u, readbackDesc.size));
        const bool matched = bytes && std::memcmp(bytes, &expectedArgs, sizeof(expectedArgs)) == 0
            && std::memcmp(bytes + sizeof(expectedArgs), &expectedMapping, sizeof(expectedMapping))
                == 0
            && std::memcmp(bytes + sizeof(expectedArgs) + sizeof(expectedMapping),
                   &expectedFault,
                   sizeof(expectedFault))
                == 0
            && fault->faultBits() == expectedFault
            && (expectedFault ? !fault->validate().success : fault->validate().success)
            && fault->status()
                == (expectedFault ? BindlessFaultValidationStatus::Faulted
                                  : BindlessFaultValidationStatus::Clean);
        std::array<std::uint32_t, 10> observed{};
        if (bytes)
            std::memcpy(observed.data(), bytes, sizeof(observed));
        wgpuBufferUnmap(readback->buffer);
        if (!matched) {
            std::fprintf(stderr,
                "RHI-WEBGPU-PREPARED-PRODUCER-READBACK probe=%zu bits=%u\n",
                probe,
                fault->faultBits());
            return false;
        }
        std::printf(
            "BINDLESS_RHI_PREPARED_ZERO_BASE_PRODUCER_PROBE %zu %u\n", probe, expectedFault);
        std::printf("BINDLESS_RHI_PREPARED_ZERO_BASE_PRODUCER_READBACK %zu %u %u %u %u"
                    " %u %u %u %u %u %u %u %u %u %u\n",
            probe,
            parameters.sourceWordOffset,
            parameters.instanceCapacity,
            parameters.reserved,
            parameters.contractVersion,
            observed[0],
            observed[1],
            observed[2],
            observed[3],
            observed[4],
            observed[5],
            observed[6],
            observed[7],
            observed[8],
            observed[9]);
    }
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    std::puts("BINDLESS_RHI_PREPARED_ZERO_BASE_PRODUCER_OK 5");
    return true;
}

bool validatePreparedProducerDeviceLoss()
{
    using namespace arp::rhi_webgpu;
    arp::core::pmr::String diagnostic;
    auto owner = prepareWebGpuProducerCompletionOwner(2u, diagnostic);
    if (!owner)
        return false;
    const auto limits = webGpuNativeDeviceLimits();
    const auto makeCommands = [&]() {
        return createWebGpuNativeCommandPayload("Quarantined producer owner",
            limits.uniformBufferOffsetAlignment,
            limits.maxUniformBufferBindingSize,
            limits.uniformBufferOffsetAlignment,
            1u,
            {},
            false,
            diagnostic,
            1u,
            0u,
            1u,
            owner);
    };
    auto prepared = makeCommands();
    auto retention = arp::core::makeUnique<WebGpuZeroBaseProducerRetention>();
    const auto failedTicket = owner->retained.reserve(retention);
    // Inject only the ownership result, never native completion or device loss.
    // This proves failure gates; the actual lost promise below proves recovery.
    if (!prepared || !failedTicket || !owner->retained.submit(failedTicket)
        || !owner->retained.resolve(failedTicket, false) || owner->retained.pending() != 2u)
        return false;
    if (makeCommands() || diagnostic != "RHI-WEBGPU-PRODUCER-COMPLETION-OWNER-MISSING-OR-CLOSED")
        return false;
    if (prepared->finish(diagnostic) || diagnostic != webGpuGenericRecordingUsageDiagnostic
        || prepared->finished() || prepared->submit(diagnostic)
        || diagnostic != webGpuGenericCompletionResourceGraphDiagnostic)
        return false;
    prepared.reset();
    if (owner->retained.pending() != 1u || owner->retained.accepting())
        return false;
    std::weak_ptr<WebGpuProducerCompletionOwner> observed = owner;
    owner.reset();
    if (observed.expired())
        return false;
    auto device = emscripten_webgpu_get_device();
    if (!device)
        return false;
    arp::test::WebGpuOriginalTextureUploadSourceFixture::activatePendingNativeMapLoss();
    arp::test::WebGpuCanonicalScopedRecordingFixture::beginActualLossProbe();
    arp::test::WebGpuCanonicalBufferUploadSourceFixture::beginActualLossProbe();
    arp::test::WebGpuCanonicalTextureUploadSourceFixture::beginActualLossProbe();
    arp::test::WebGpuCanonicalOrderedCopyAttachmentFixture::beginActualLossProbe();
    arp::test::WebGpuCanonicalReadbackJoinsFixture::beginMapExecutionLoss();
    arp::test::WebGpuCanonicalReadbackJoinsFixture::beginOrderedLoss();
    arp::test::WebGpuCanonicalSubmissionFixture::beginActualLossProbe(
        arp::test::WebGpuCanonicalUploadProjectionFixture::scopeProbeMode()
            ? &arp::test::WebGpuCanonicalUploadProjectionFixture::beginUploadScopeUncertainty
            : &arp::test::WebGpuCanonicalReadbackJoinsFixture::beginOrderedScopeUncertainty);
    // Emdawn imported devices have no native DeviceLostEvent future. Destroy
    // the exact browser-owned object and observe its actual lost promise.
    const bool destroyed = EM_ASM_INT(
                               {
                                   // clang-format off
                                       var device = WebGPU.getJsObject($0);
                                       if (!device || device !== Module.preinitializedWebGPUDevice ||
                                           typeof device.destroy !== 'function')
                                           return 0;
                                       device.destroy();
                                       return 1;
                                   // clang-format on
                               },
                               device)
        != 0;
    if (!destroyed) {
        wgpuDeviceRelease(device);
        return false;
    }
    for (unsigned attempt = 0u; attempt < 500u && !observed.expired(); ++attempt)
        emscripten_sleep(10);
    const bool recovered = observed.expired() && webGpuNativeDeviceLost();
    wgpuDeviceRelease(device);
    if (recovered)
        std::puts("BINDLESS_RHI_PREPARED_PRODUCER_LOSS_RECOVERY_OK");
    return recovered;
}

bool dispatchAndReadback(const arp::rhi::BindlessShaderPackage& package,
    const arp::rhi_webgpu::WebGpuNativeComputePipelinePayload& pipeline)
{
    auto device = emscripten_webgpu_get_device();
    if (!device)
        return false;
    auto queue = wgpuDeviceGetQueue(device);
    if (!queue)
        return false;
    const auto prepared =
        arp::rhi_webgpu::prepareBindlessWebGpuComputeInterface(package, "computeMain");
    if (!prepared.success || prepared.layout.descriptorLayouts.size() != 1u)
        return false;

    const std::array<float, 4> uniformA{1, 2, 3, 4};
    const std::array<float, 4> uniformB{5, 6, 7, 8};
    const std::uint32_t inputA = 10, inputB = 20, counterA = 2, counterB = 3;
    const std::array<std::uint32_t, 5> faults{};
    const std::uint32_t latch = 0;
    auto metadata = snapshotMetadata(package);
    if (metadata.empty())
        return false;

    struct BufferBinding {
        WGPUBuffer buffer = nullptr;
        std::uint64_t size = 0;
    };

    std::array<BufferBinding, 9> buffers{};
    std::array<std::shared_ptr<arp::rhi_webgpu::WebGpuNativeBufferPayload>, 9> bufferPayloads{};
    const auto add =
        [&](std::size_t index, const void* data, std::size_t bytes, WGPUBufferUsage usage) {
            buffers[index] = {uploadBuffer(device, queue, data, bytes, usage), bytes};
            if (buffers[index].buffer) {
                bufferPayloads[index] =
                    arp::core::makeShared<arp::rhi_webgpu::WebGpuNativeBufferPayload>();
                bufferPayloads[index]->buffer = buffers[index].buffer;
                bufferPayloads[index]->sizeBytes = bytes;
                bufferPayloads[index]->ownerDevice = device;
            }
            return buffers[index].buffer != nullptr;
        };
    const auto uniformUsage = WGPUBufferUsage_Uniform;
    const auto storageUsage = WGPUBufferUsage_Storage;
    const auto writableUsage =
        static_cast<WGPUBufferUsage>(WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    if (!add(0, uniformA.data(), sizeof(uniformA), uniformUsage)
        || !add(1, uniformB.data(), sizeof(uniformB), uniformUsage)
        || !add(2, &inputA, sizeof(inputA), storageUsage)
        || !add(3, &inputB, sizeof(inputB), storageUsage)
        || !add(4, &counterA, sizeof(counterA), writableUsage)
        || !add(5, &counterB, sizeof(counterB), writableUsage)
        || !add(6, faults.data(), sizeof(faults), writableUsage)
        || !add(7, metadata.data(), metadata.size() * sizeof(std::uint32_t), storageUsage)
        || !add(8, &latch, sizeof(latch), writableUsage))
        return false;

    std::array<WGPUTexture, 2> textures{};
    std::array<WGPUTextureView, 2> views{};
    std::array<std::shared_ptr<arp::rhi_webgpu::WebGpuNativeTexturePayload>, 2> texturePayloads{};
    std::array<std::shared_ptr<arp::rhi_webgpu::WebGpuNativeTextureViewPayload>, 2> viewPayloads{};
    for (unsigned i = 0; i < textures.size(); ++i) {
        auto desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.dimension = WGPUTextureDimension_2D;
        desc.size = {1, 1, 1};
        desc.mipLevelCount = 1;
        desc.sampleCount = 1;
        desc.format = WGPUTextureFormat_R32Float;
        desc.usage = WGPUTextureUsage_StorageBinding;
        textures[i] = wgpuDeviceCreateTexture(device, &desc);
        if (!textures[i])
            return false;
        texturePayloads[i] = arp::core::makeShared<arp::rhi_webgpu::WebGpuNativeTexturePayload>();
        texturePayloads[i]->texture = textures[i];
        views[i] = wgpuTextureCreateView(textures[i], nullptr);
        if (!views[i])
            return false;
        viewPayloads[i] = arp::core::makeShared<arp::rhi_webgpu::WebGpuNativeTextureViewPayload>();
        viewPayloads[i]->view = views[i];
        viewPayloads[i]->parentTexture = texturePayloads[i];
    }
    auto group = arp::core::makeShared<arp::rhi_webgpu::WebGpuNativeDescriptorGroupPayload>();
    for (const auto& binding : prepared.layout.descriptorLayouts.front().bindings) {
        arp::rhi_webgpu::WebGpuNativeDescriptorBindingPayload entry;
        entry.binding = binding;
        entry.write.binding = binding.binding;
        entry.write.type = binding.type;
        if (binding.binding >= 4u && binding.binding < 6u) {
            entry.textureView = viewPayloads[binding.binding - 4u];
            entry.write.resourceKind = arp::rhi::DescriptorResourceKind::TextureView;
        } else if (binding.binding >= 6u && binding.binding < 15u) {
            entry.buffer = bufferPayloads[binding.binding - 6u];
            entry.write.resourceKind = arp::rhi::DescriptorResourceKind::Buffer;
        } else {
            return false;
        }
        group->bindings.push_back(std::move(entry));
    }
    for (const auto& binding : package.artifact().physicalLayout.bindings) {
        if (binding.binding >= 4u)
            continue;
        arp::rhi_webgpu::WebGpuNativeDescriptorBindingPayload inactive;
        inactive.binding = binding;
        inactive.write.binding = binding.binding;
        inactive.write.type = binding.type;
        group->bindings.push_back(std::move(inactive));
    }

    auto readbackDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    readbackDesc.size = 32;
    readbackDesc.usage =
        static_cast<WGPUBufferUsage>(WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    auto readback = wgpuDeviceCreateBuffer(device, &readbackDesc);
    if (!readback)
        return false;
    auto readbackPayload = arp::core::makeShared<arp::rhi_webgpu::WebGpuNativeBufferPayload>();
    readbackPayload->buffer = readback;
    readbackPayload->sizeBytes = 32;
    const auto limits = arp::rhi_webgpu::webGpuNativeDeviceLimits();
    if (!limits.uniformBufferOffsetAlignment || !limits.maxUniformBufferBindingSize)
        return false;
    arp::core::pmr::String diagnostic;
    auto commands =
        arp::rhi_webgpu::createWebGpuNativeCommandPayload("Bindless retained-package dispatch",
            limits.uniformBufferOffsetAlignment,
            limits.maxUniformBufferBindingSize,
            limits.uniformBufferOffsetAlignment,
            1u,
            {},
            false,
            diagnostic,
            1u);
    if (!commands)
        return false;
    if (commands->recordBindlessFaultLatchClear(bufferPayloads[7], diagnostic)
        || diagnostic != "RHI-WEBGPU-BINDLESS-FAULT-CLEAR-BUFFER-INVALID")
        return false;
    diagnostic.clear();
    bufferPayloads[8]->ownerDevice = nullptr;
    if (commands->recordBindlessFaultLatchClear(bufferPayloads[8], diagnostic)
        || diagnostic != "RHI-WEBGPU-BINDLESS-FAULT-CLEAR-DEVICE-MISMATCH")
        return false;
    bufferPayloads[8]->ownerDevice = device;
    diagnostic.clear();
    if (commands->scheduleBindlessFaultReadback(bufferPayloads[8], diagnostic)
        || diagnostic != "RHI-WEBGPU-BINDLESS-FAULT-READBACK-NOT-CLEARED")
        return false;
    diagnostic.clear();
    if (!commands->recordBindlessFaultLatchClear(bufferPayloads[8], diagnostic))
        return false;
    if (commands->finish(diagnostic) || diagnostic != "RHI-WEBGPU-BINDLESS-FAULT-READBACK-REQUIRED")
        return false;
    diagnostic.clear();
    bufferPayloads[8]->ownerDevice = nullptr;
    if (commands->scheduleBindlessFaultReadback(bufferPayloads[8], diagnostic)
        || diagnostic != "RHI-WEBGPU-BINDLESS-FAULT-READBACK-DEVICE-MISMATCH")
        return false;
    bufferPayloads[8]->ownerDevice = device;
    diagnostic.clear();
    if (!commands->recordComputePassBegin({"bindless fixture"}, diagnostic)
        || !commands->recordComputePipelineBind(pipeline, diagnostic)) {
        std::fprintf(stderr, "RHI-BINDLESS-WEBGPU-COMMAND: %s\n", diagnostic.c_str());
        return false;
    }
    auto incomplete = arp::core::makeShared<arp::rhi_webgpu::WebGpuNativeDescriptorGroupPayload>();
    incomplete->bindings = group->bindings;
    incomplete->bindings.erase(std::remove_if(incomplete->bindings.begin(),
                                   incomplete->bindings.end(),
                                   [](const auto& binding) {
                                       return binding.binding.binding == 14u;
                                   }),
        incomplete->bindings.end());
    if (commands->recordDescriptorSetBind(
            package.artifact().set, incomplete, nullptr, 0u, diagnostic)
        || diagnostic != "RHI-WEBGPU-COMPACT-REMAP-ACTIVE-BINDING-MISSING") {
        std::fprintf(stderr, "RHI-BINDLESS-WEBGPU-INCOMPLETE-DESCRIPTOR: %s\n", diagnostic.c_str());
        return false;
    }
    diagnostic.clear();
    if (!commands->recordDescriptorSetBind(package.artifact().set, group, nullptr, 0u, diagnostic)
        || !commands->recordDispatch(1, 1, 1, diagnostic)
        || !commands->recordComputePassEnd(diagnostic)) {
        std::fprintf(stderr, "RHI-BINDLESS-WEBGPU-COMMAND: %s\n", diagnostic.c_str());
        return false;
    }
    for (const auto [source, destination, bytes] : {std::array<std::uint64_t, 3>{4, 0, 4},
             std::array<std::uint64_t, 3>{5, 4, 4},
             std::array<std::uint64_t, 3>{6, 8, 20},
             std::array<std::uint64_t, 3>{8, 28, 4}}) {
        arp::rhi::BufferCopyDesc copy;
        copy.destinationOffsetBytes = destination;
        copy.sizeBytes = bytes;
        if (!commands->recordBufferCopy(
                *bufferPayloads[source], *readbackPayload, copy, diagnostic)) {
            std::fprintf(stderr, "RHI-BINDLESS-WEBGPU-COPY: %s\n", diagnostic.c_str());
            return false;
        }
    }
    auto faultResult = WebGpuFaultColdPreparationFixture::scheduleAndSubmit(
        *commands, bufferPayloads[8], diagnostic);
    if (!faultResult || faultResult->status() != arp::rhi::BindlessFaultValidationStatus::Pending)
        return false;
    if (!waitForQueue(queue) || !mapRead(readback, 32))
        return false;
    const auto* mapped =
        static_cast<const std::uint32_t*>(wgpuBufferGetConstMappedRange(readback, 0, 32));
    const std::array<std::uint32_t, 8> expected{16, 27, 15, 26, 1, 7, 31, 1};
    const bool matched = mapped && std::memcmp(mapped, expected.data(), 32) == 0
        && faultResult->status() == arp::rhi::BindlessFaultValidationStatus::Faulted
        && faultResult->faultBits() == 1u && !faultResult->validate().success;
    if (mapped) {
        std::printf("BINDLESS_RHI_READBACK %u %u %u %u %u %u %u %u\n",
            mapped[0],
            mapped[1],
            mapped[2],
            mapped[3],
            mapped[4],
            mapped[5],
            mapped[6],
            mapped[7]);
    }
    wgpuBufferUnmap(readback);
    commands.reset();
    arp::rhi::BufferDesc abandonedDesc;
    abandonedDesc.label = "Bindless abandoned fault latch";
    abandonedDesc.sizeBytes = sizeof(std::uint32_t);
    abandonedDesc.usage = arp::rhi::ResourceUsage::StorageBuffer | arp::rhi::ResourceUsage::CopySrc
        | arp::rhi::ResourceUsage::CopyDst;
    abandonedDesc.memory = arp::rhi::MemoryUsage::GpuOnly;
    auto abandonedLatch = arp::rhi_webgpu::createWebGpuNativeBufferPayload(abandonedDesc);
    auto abandonedCommands =
        arp::rhi_webgpu::createWebGpuNativeCommandPayload("Bindless abandoned fault probe",
            limits.uniformBufferOffsetAlignment,
            limits.maxUniformBufferBindingSize,
            limits.uniformBufferOffsetAlignment,
            2u,
            {},
            false,
            diagnostic,
            1u);
    if (!abandonedLatch || !abandonedCommands
        || !abandonedCommands->recordBindlessFaultLatchClear(abandonedLatch, diagnostic))
        return false;
    auto abandoned = abandonedCommands->scheduleBindlessFaultReadback(abandonedLatch, diagnostic);
    if (!abandoned || abandoned->status() != arp::rhi::BindlessFaultValidationStatus::Pending)
        return false;
    abandonedCommands.reset();
    if (abandoned->status() != arp::rhi::BindlessFaultValidationStatus::Abandoned
        || abandoned->validate().success)
        return false;
    incomplete.reset();
    group.reset();
    readbackPayload.reset();
    for (auto& payload : viewPayloads)
        payload.reset();
    for (auto& payload : texturePayloads)
        payload.reset();
    for (auto& payload : bufferPayloads)
        payload.reset();
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    return matched;
}

bool drawAndReadback(const arp::rhi::BindlessShaderPackage& package,
    const arp::rhi_webgpu::WebGpuNativeRenderPipelinePayload& pipeline,
    std::string_view fragmentEntry,
    bool sampled)
{
    auto device = emscripten_webgpu_get_device();
    if (!device)
        return false;
    auto queue = wgpuDeviceGetQueue(device);
    if (!queue)
        return false;
    const auto prepared =
        arp::rhi_webgpu::prepareBindlessWebGpuRenderInterface(package, "vertexMain", fragmentEntry);
    auto metadata = snapshotMetadata(package);
    if (!prepared.success || prepared.layout.descriptorLayouts.size() != 1u || metadata.empty())
        return false;

    const std::uint32_t inputA = 10u, inputB = 20u, zero = 0u;
    const auto makeBuffer = [&](const void* data, std::size_t bytes, WGPUBufferUsage usage) {
        auto payload = arp::core::makeShared<arp::rhi_webgpu::WebGpuNativeBufferPayload>();
        payload->buffer = uploadBuffer(device, queue, data, bytes, usage);
        payload->sizeBytes = bytes;
        payload->ownerDevice = device;
        return payload;
    };
    std::shared_ptr<arp::rhi_webgpu::WebGpuNativeBufferPayload> first;
    std::shared_ptr<arp::rhi_webgpu::WebGpuNativeBufferPayload> second;
    if (!sampled) {
        first = makeBuffer(&inputA, sizeof(inputA), WGPUBufferUsage_Storage);
        second = makeBuffer(&inputB, sizeof(inputB), WGPUBufferUsage_Storage);
    }
    auto metadataBuffer =
        makeBuffer(metadata.data(), metadata.size() * 4u, WGPUBufferUsage_Storage);
    auto fault = makeBuffer(&zero,
        sizeof(zero),
        static_cast<WGPUBufferUsage>(WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc));
    if ((!sampled && (!first->buffer || !second->buffer)) || !metadataBuffer->buffer
        || !fault->buffer)
        return false;

    std::array<std::shared_ptr<arp::rhi_webgpu::WebGpuNativeTexturePayload>, 2> images{};
    std::array<std::shared_ptr<arp::rhi_webgpu::WebGpuNativeTextureViewPayload>, 2> imageViews{};
    std::array<std::shared_ptr<arp::rhi_webgpu::WebGpuNativeSamplerPayload>, 2> samplers{};
    if (sampled) {
        arp::rhi::TextureDesc imageDesc;
        imageDesc.label = "Bindless sampled fixture";
        imageDesc.extent = {2u, 2u, 1u};
        imageDesc.format = arp::rhi::TextureFormat::Rgba8Unorm;
        imageDesc.usage = arp::rhi::TextureUsage::Sampled | arp::rhi::TextureUsage::CopyDst;
        imageDesc.mipLevelCount = 2u;
        arp::rhi::TextureViewDesc viewDesc;
        viewDesc.format = imageDesc.format;
        viewDesc.usage = arp::rhi::TextureUsage::Sampled;
        viewDesc.mipLevelCount = 2u;
        arp::rhi::SamplerDesc samplerDesc;
        for (unsigned i = 0; i < images.size(); ++i) {
            images[i] = arp::rhi_webgpu::createWebGpuNativeTexturePayload(imageDesc);
            if (!images[i] || !images[i]->texture)
                return false;
            imageViews[i] =
                arp::rhi_webgpu::createWebGpuNativeTextureViewPayload(*images[i], viewDesc);
            if (!imageViews[i] || !imageViews[i]->view)
                return false;
            imageViews[i]->parentTexture = images[i];
            samplers[i] = arp::rhi_webgpu::createWebGpuNativeSamplerPayload(samplerDesc);
            if (!samplers[i] || !samplers[i]->sampler)
                return false;
        }
    }

    auto group = arp::core::makeShared<arp::rhi_webgpu::WebGpuNativeDescriptorGroupPayload>();
    for (const auto& binding : prepared.layout.descriptorLayouts.front().bindings) {
        arp::rhi_webgpu::WebGpuNativeDescriptorBindingPayload entry;
        entry.binding = binding;
        entry.write.binding = binding.binding;
        entry.write.type = binding.type;
        entry.write.resourceKind = arp::rhi::DescriptorResourceKind::Buffer;
        switch (binding.binding) {
        case 0:
        case 1:
            if (!sampled)
                return false;
            entry.textureView = imageViews[binding.binding];
            entry.write.resourceKind = arp::rhi::DescriptorResourceKind::TextureView;
            break;
        case 2:
        case 3:
            if (!sampled)
                return false;
            entry.sampler = samplers[binding.binding - 2u];
            entry.write.resourceKind = arp::rhi::DescriptorResourceKind::Sampler;
            break;
        case 8:
            entry.buffer = first;
            break;
        case 9:
            entry.buffer = second;
            break;
        case 13:
            entry.buffer = metadataBuffer;
            break;
        case 14:
            entry.buffer = fault;
            break;
        default:
            return false;
        }
        group->bindings.push_back(std::move(entry));
    }

    arp::rhi::TextureDesc targetDesc;
    targetDesc.label = "Bindless fragment output";
    targetDesc.extent = {2u, 2u, 1u};
    targetDesc.format = arp::rhi::TextureFormat::Rgba8Unorm;
    targetDesc.usage = arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::CopySrc;
    auto target = arp::rhi_webgpu::createWebGpuNativeTexturePayload(targetDesc);
    if (!target || !target->texture)
        return false;
    auto readbackDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    readbackDesc.size = 516u;
    readbackDesc.usage =
        static_cast<WGPUBufferUsage>(WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    auto readback = arp::core::makeShared<arp::rhi_webgpu::WebGpuNativeBufferPayload>();
    readback->buffer = wgpuDeviceCreateBuffer(device, &readbackDesc);
    readback->sizeBytes = readbackDesc.size;
    if (!readback->buffer)
        return false;

    arp::rhi::RenderPassDesc pass;
    pass.label = "Bindless fragment draw";
    pass.colorAttachments.emplace_back();
    pass.colorAttachments.back().format = targetDesc.format;
    pass.colorAttachments.back().loadOp = arp::rhi::LoadOp::Clear;
    pass.colorAttachments.back().storeOp = arp::rhi::StoreOp::Store;
    pass.colorAttachments.back().clearColor = {0, 0, 0, 1};
    arp::core::pmr::Vector<const arp::rhi_webgpu::WebGpuNativeTexturePayload*> colors;
    colors.push_back(target.get());
    arp::core::pmr::Vector<const arp::rhi_webgpu::WebGpuNativeTexturePayload*> resolves;
    resolves.push_back(nullptr);
    const auto limits = arp::rhi_webgpu::webGpuNativeDeviceLimits();
    if (!limits.uniformBufferOffsetAlignment || !limits.maxUniformBufferBindingSize)
        return false;
    arp::core::pmr::String diagnostic;
    auto commands =
        arp::rhi_webgpu::createWebGpuNativeCommandPayload("Bindless retained-package render",
            limits.uniformBufferOffsetAlignment,
            limits.maxUniformBufferBindingSize,
            limits.uniformBufferOffsetAlignment,
            1u,
            {},
            false,
            diagnostic,
            1u);
    if (!commands)
        return false;
    if (sampled) {
        for (unsigned i = 0; i < images.size(); ++i) {
            std::array<std::uint8_t, 16> pixels{};
            for (unsigned p = 0; p < 4; ++p) {
                pixels[p * 4u + (i == 0u ? 0u : 1u)] = 255u;
                pixels[p * 4u + 3u] = 255u;
            }
            arp::rhi::UploadTextureDesc upload;
            upload.extent = {2u, 2u, 1u};
            upload.format = arp::rhi::TextureFormat::Rgba8Unorm;
            upload.bytesPerRow = 8u;
            upload.rowsPerImage = 2u;
            upload.pixelData = pixels.data();
            upload.pixelDataSize = static_cast<std::uint32_t>(pixels.size());
            if (!commands->recordTextureUpload(*images[i], upload, diagnostic)) {
                std::fprintf(
                    stderr, "RHI-BINDLESS-WEBGPU-TEXTURE-UPLOAD: %s\n", diagnostic.c_str());
                return false;
            }
            std::array<std::uint8_t, 4> mip{};
            mip[i == 0u ? 0u : 1u] = 255u;
            mip[3] = 255u;
            upload.extent = {1u, 1u, 1u};
            upload.bytesPerRow = 4u;
            upload.rowsPerImage = 1u;
            upload.mipLevel = 1u;
            upload.subresources.baseMipLevel = 1u;
            upload.pixelData = mip.data();
            upload.pixelDataSize = static_cast<std::uint32_t>(mip.size());
            if (!commands->recordTextureUpload(*images[i], upload, diagnostic)) {
                std::fprintf(
                    stderr, "RHI-BINDLESS-WEBGPU-TEXTURE-UPLOAD: %s\n", diagnostic.c_str());
                return false;
            }
        }
    }
    if (!commands || !commands->recordRenderPassBegin(pass, colors, resolves, nullptr, diagnostic)
        || !commands->recordBindlessFaultLatchClear(fault, diagnostic)
        || !commands->recordGraphicsPipelineBind(pipeline, diagnostic)
        || !commands->recordRenderDescriptorSetBind(
            package.artifact().set, group, nullptr, 0u, diagnostic)
        || !commands->recordDraw(3u, 1u, 0u, 0u, diagnostic)
        || !commands->recordRenderPassEnd(diagnostic)) {
        std::fprintf(stderr, "RHI-BINDLESS-WEBGPU-RENDER-COMMAND: %s\n", diagnostic.c_str());
        return false;
    }
    arp::rhi::TextureToBufferCopyDesc copy;
    copy.bytesPerRow = 256u;
    copy.rowsPerImage = 2u;
    copy.extent = {2u, 2u, 1u};
    arp::rhi::BufferCopyDesc faultCopy;
    faultCopy.destinationOffsetBytes = 512u;
    faultCopy.sizeBytes = sizeof(std::uint32_t);
    if (!commands->recordTextureToBufferCopy(*target, *readback, copy, diagnostic)
        || !commands->recordBufferCopy(*fault, *readback, faultCopy, diagnostic)) {
        std::fprintf(stderr, "RHI-BINDLESS-WEBGPU-RENDER-COPY: %s\n", diagnostic.c_str());
        return false;
    }
    auto faultResult = commands->scheduleBindlessFaultReadback(fault, diagnostic);
    if (!faultResult || faultResult->status() != arp::rhi::BindlessFaultValidationStatus::Pending
        || !commands->finish(diagnostic) || !commands->submit(diagnostic)) {
        std::fprintf(stderr, "RHI-BINDLESS-WEBGPU-RENDER-SUBMIT: %s\n", diagnostic.c_str());
        return false;
    }
    if (!waitForQueue(queue) || !mapRead(readback->buffer, 516u))
        return false;
    const auto* pixels =
        static_cast<const std::uint8_t*>(wgpuBufferGetConstMappedRange(readback->buffer, 0, 516u));
    std::uint32_t faultBits = UINT32_MAX;
    if (pixels)
        std::memcpy(&faultBits, pixels + 512u, sizeof(faultBits));
    const bool matched = pixels && faultBits == 0u
        && faultResult->status() == arp::rhi::BindlessFaultValidationStatus::Clean
        && faultResult->validate().success
        && (sampled
                ? pixels[0] == 255u && pixels[1] == 0u && pixels[4] == 0u && pixels[5] == 255u
                    && pixels[256] == 255u && pixels[257] == 0u && pixels[260] == 0u
                    && pixels[261] == 255u
                : pixels[0] == 10u && pixels[4] == 20u && pixels[256] == 10u && pixels[260] == 20u);
    if (pixels) {
        if (sampled)
            std::printf("BINDLESS_RHI_SAMPLED_READBACK %u %u %u %u %u\n",
                pixels[0],
                pixels[5],
                pixels[256],
                pixels[261],
                faultBits);
        else
            std::printf("BINDLESS_RHI_RENDER_READBACK %u %u %u %u %u\n",
                pixels[0],
                pixels[4],
                pixels[256],
                pixels[260],
                faultBits);
    }
    wgpuBufferUnmap(readback->buffer);
    commands.reset();
    group.reset();
    target.reset();
    readback.reset();
    fault.reset();
    metadataBuffer.reset();
    second.reset();
    first.reset();
    for (auto& sampler : samplers)
        sampler.reset();
    for (auto& view : imageViews)
        view.reset();
    for (auto& image : images)
        image.reset();
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    return matched;
}

} // namespace
#endif

int main()
{
    std::puts("BINDLESS_RHI_BROWSER_MAIN_ENTERED no-execution-authority");
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (EM_ASM_INT({
            // clang-format off
            return new URLSearchParams(location.search).has("shader-texture-input");
            // clang-format on
        })) {
        arp::test::CanonicalTextureContentCreationFixture::verifyCompletedShaderInputContract();
        assert(validateDeviceBindlessLimits());
        std::puts(
            "BINDLESS_RHI_COMPLETED_SHADER_TEXTURE_INPUT_SCENARIO_OK no-public-or-shader-authority");
        return 0;
    }
    if (EM_ASM_INT({
            // clang-format off
                    return new URLSearchParams(location.search).has("general-resource-projection");
            // clang-format on
        })) {
        arp::test::OriginalCommandDescriptorConsumptionFixture::verifyGeneralResources();
        arp::test::OriginalTypedUsageProjectionFixture::verifyContract();
        assert(validateDeviceBindlessLimits());
        std::puts(
            "BINDLESS_RHI_GENERAL_ORIGINAL_PROJECTION_SCENARIO_OK private-submission-numerical-unproven-public-closed");
        return 0;
    }
    if (EM_ASM_INT({
            // clang-format off
            return new URLSearchParams(location.search).has("shader-usage-values");
            // clang-format on
        })) {
        arp::test::OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([] {
            arp::test::TypedShaderUsageScopeContractFixture::verifyContract();
        });
        std::puts(
            "BINDLESS_RHI_SHADER_USAGE_VALUES_SCENARIO_OK no-native-execution-or-public-authority");
        return 0;
    }
    if (EM_ASM_INT({
            // clang-format off
            return new URLSearchParams(location.search).has("stream-geometry");
            // clang-format on
        })) {
        assert(validateDeviceBindlessLimits());
        std::puts("BINDLESS_RHI_STREAM_GEOMETRY_SCENARIO_OK");
        return 0;
    }
    if (EM_ASM_INT({
            // clang-format off
            return new URLSearchParams(location.search).has("typed-execution");
            // clang-format on
        })) {
        assert(validateDeviceBindlessLimits());
        std::puts("BINDLESS_RHI_TYPED_NATIVE_EXECUTION_SCENARIO_OK");
        return 0;
    }
#endif
    arp::test::WgslBufferMinimumNativeFixture::verify(
        "/buffer-layout-probe/buffer_layout_probe.wgsl");
    arp::test::DescriptorSlotShapeFixture::verify();
    arp::test::DescriptorBufferBindingRangeFixture::verifyContract();
    arp::test::OriginalDescriptorDepthArtifactsFixture::verify(
        "/original-descriptor-depth-fixture/descriptor_depth_compute/descriptor_depth_compute.computeMain.entry.yaml",
        "/original-descriptor-depth-fixture/descriptor_depth/descriptor_depth.graphics.module.yaml");
    arp::test::OriginalDescriptorBufferBindingFixture::verifyContract();
    arp::test::DescriptorWholeBufferBindingFixture::verifyContract();
    arp::test::OriginalCommandTypedSourceContractFixture::verifyContract();
    arp::test::OriginalCommandAdmissionContractFixture::verify();
    arp::test::OriginalCommandStateAdmissionContractFixture::verify();
    arp::test::WebGpuFrozenRenderPassBindingFixture::verifyContract();
    arp::test::CompletionAdmissionTransactionFixture::verify();
    arp::test::RenderPassArgumentsFixture::verifyContract();
    arp::rhi::BindlessPackageLoadBudget budget{
        2u * 1024u * 1024u, 16u * 1024u * 1024u, 64u, 16u, 256u, 64u};
    arp::core::pmr::String diagnostic;
    const auto package = arp::rhi::BindlessShaderPackage::load(
        "/bindless-rhi-fixture/BindlessFixture.emulated.bindless.yaml", budget, diagnostic);
    if (!package) {
        std::fprintf(stderr, "RHI-BINDLESS-WEBGPU-PACKAGE: %s\n", diagnostic.c_str());
        return 1;
    }
    const auto publicPackage = arp::rhi::BindlessShaderPackage::load(
        "/bindless-rhi-fixture/public-compute/WebGpuPublicCompute.emulated.bindless.yaml",
        budget,
        diagnostic);
    if (!publicPackage) {
        std::fprintf(stderr, "RHI-BINDLESS-WEBGPU-PUBLIC-PACKAGE: %s\n", diagnostic.c_str());
        return 1;
    }
    const auto publicGraphicsPackage = arp::rhi::BindlessShaderPackage::load(
        "/bindless-rhi-fixture/public-graphics/WebGpuPublicGraphics.emulated.bindless.yaml",
        budget,
        diagnostic);
    if (!publicGraphicsPackage) {
        std::fprintf(
            stderr, "RHI-BINDLESS-WEBGPU-PUBLIC-GRAPHICS-PACKAGE: %s\n", diagnostic.c_str());
        return 1;
    }
    const auto pipeline = arp::rhi_webgpu::createWebGpuNativeBindlessComputePipelinePayload(
        *package, "computeMain", diagnostic);
    if (!pipeline) {
        std::fprintf(stderr, "RHI-BINDLESS-WEBGPU-PIPELINE: %s\n", diagnostic.c_str());
        return 1;
    }
    arp::rhi::RenderPipelineDesc renderState;
    renderState.label = "Bindless fragment buffer fixture";
    renderState.colorFormat = arp::rhi::TextureFormat::Rgba8Unorm;
    renderState.hasViewport = true;
    renderState.viewport = {0, 0, 2, 2, 0, 1};
    renderState.hasScissor = true;
    renderState.scissor = {0, 0, 2, 2};
    const auto renderPipeline = arp::rhi_webgpu::createWebGpuNativeBindlessRenderPipelinePayload(
        *package, "vertexMain", "fragmentBufferMain", renderState, diagnostic);
    if (!renderPipeline) {
        std::fprintf(stderr, "RHI-BINDLESS-WEBGPU-RENDER-PIPELINE: %s\n", diagnostic.c_str());
        return 1;
    }
    const auto sampledPipeline = arp::rhi_webgpu::createWebGpuNativeBindlessRenderPipelinePayload(
        *package, "vertexMain", "fragmentMain", renderState, diagnostic);
    if (!sampledPipeline) {
        std::fprintf(stderr, "RHI-BINDLESS-WEBGPU-SAMPLED-PIPELINE: %s\n", diagnostic.c_str());
        return 1;
    }
#if defined(__EMSCRIPTEN__)
    if (!arp::test::WebGpuDepthStencilAttachmentInputFixture::verify(waitForQueue, mapRead))
        return 1;
    auto frozenPassBindingLoss = arp::test::WebGpuFrozenRenderPassBindingFixture::verifyActual();
    frozenPassBindingLoss.verifyDroppedSources();
    if (!WebGpuFaultColdPreparationFixture::verify())
        return 1;
    arp::test::WebGpuDescriptorDependencyRetirementFixture::prepare();
    arp::test::WebGpuTransferRetentionFixture::verifyBudgetGates();
    arp::test::WebGpuTransferRetentionFixture::verifyNativeManifest();
    arp::test::WebGpuTransferRetentionFixture::verifyImmediateTextureUpload();
    arp::test::WebGpuTimestampCommandPointFixture::verifyContract();
    arp::test::WebGpuTimestampCommandPointFixture::verifyActual();
    arp::test::WebGpuMapHandoffFixture::verifyActual();
    arp::test::WebGpuGenericCommandProjectionFixture::verifyContract();
    arp::test::WebGpuGenericCommandUsageFixture::verifyContract();
    arp::test::WebGpuFrozenTransferArgumentsFixture::verifyContract();
    arp::test::WebGpuOriginalBufferUploadSourceFixture::verifyContract();
    arp::test::WebGpuOriginalBufferUploadSourceFixture::verifyActual();
    arp::test::WebGpuOriginalTextureUploadDestinationFixture::verifyContract();
    arp::test::WebGpuOriginalTextureUploadDestinationFixture::verifyActual();
    arp::test::WebGpuOriginalTextureUploadSourceFixture::verifyContract();
    arp::test::WebGpuOriginalTextureUploadSourceFixture::verifyActual();
    arp::test::WebGpuOriginalTextureReadbackSourceFixture::verifyActual();
    arp::test::WebGpuOriginalTextureReadbackDestinationFixture::verifyActual();
    arp::test::WebGpuOrdinaryCompletionFixture::verifyContract();
    arp::test::WebGpuOrdinaryResourceFixture::verifyContract();
    arp::test::WebGpuOrdinaryDescriptorFixture::verifyContract();
    arp::test::WebGpuOrdinaryPipelineFixture::verifyContract();
    arp::test::WebGpuOrdinaryPhysicalGroupsFixture::verifyContract();
    auto ordinaryPipelineLoss = arp::test::WebGpuOrdinaryPipelineFixture::verifyActual(*package);
    arp::test::WebGpuOrdinaryResourceFixture::verifyActual();
    auto ordinaryCompletionLoss =
        arp::test::WebGpuOrdinaryCompletionFixture::verifyActualPreparation();
    auto ordinaryCompletion =
        arp::test::WebGpuOrdinaryTransferExecutionFixture::prepareCompletionOwner();
    if (!arp::test::WebGpuOrdinaryTransferExecutionFixture::verify(
            ordinaryCompletion, waitForQueue, mapRead))
        return 1;
    ordinaryCompletion.reset();
    if (!validateDeviceBindlessLimits()) {
        std::fprintf(stderr, "RHI-BINDLESS-WEBGPU-DEVICE-LIMITS-MISMATCH\n");
        return 1;
    }
    std::puts("BINDLESS_RHI_DEVICE_LIMITS_OK");
    std::puts("BINDLESS_RHI_PIPELINE_CREATED");
    std::puts("BINDLESS_RHI_RENDER_PIPELINE_CREATED");
    std::puts("BINDLESS_RHI_SAMPLED_PIPELINE_CREATED");
    std::puts("BINDLESS_RHI_GENERIC_EXECUTION_UNAVAILABLE");
    emscripten_sleep(100);
    const auto errors = EM_ASM_INT({
        // clang-format off
                return Module.webGpuUncapturedErrors ? Module.webGpuUncapturedErrors.length : 0;
        // clang-format on
    });
    if (errors != 0) {
        std::fprintf(stderr, "RHI-BINDLESS-WEBGPU-NATIVE-VALIDATION-ERROR\n");
        return 1;
    }
    auto ordinaryLoss = arp::test::WebGpuOrdinaryTransferExecutionFixture::prepareLossProbe();
    auto frozenTransferLoss =
        arp::test::WebGpuOrdinaryTransferExecutionFixture::prepareLossProbe(true);
    arp::test::WebGpuOrdinaryTransferExecutionFixture::prepareOriginalDependencyIsolatedProbes();
    arp::test::WebGpuOriginalBufferUploadSourceFixture::prepareActualLoss();
    arp::test::TextureUploadArgumentsFixture::prepareActualLoss();
    arp::test::WebGpuOriginalQuerySourceGateFixture::prepareActualLoss();
    arp::test::WebGpuOriginalQueryOwnerFixture::prepareActualLoss();
    arp::test::WebGpuOriginalQueryBindingFixture::prepareActualLoss();
    arp::test::WebGpuOriginalQuerySlotsFixture::prepareActualLoss();
    arp::test::WebGpuOriginalQueryRecordReservationFixture::prepareActualLoss();
    arp::test::QueryPoolArgumentsFixture::prepareActualLoss();
    arp::test::TextureReadbackArgumentsFixture::prepareActualLoss();
    arp::test::TextureReadbackLayoutFixture::prepareActualLoss();
    arp::test::TextureReadbackContentCoverageFixture::prepareActualLoss();
    arp::test::WebGpuOriginalTextureReadbackSourceFixture::prepareActualLoss();
    arp::test::WebGpuOriginalTextureReadbackDestinationFixture::prepareActualLoss();
    arp::test::TextureUploadPackingFixture::prepareActualLoss();
    arp::test::TextureUploadContentTransactionFixture::prepareActualLoss();
    arp::test::WebGpuOriginalTextureUploadDestinationFixture::prepareActualLoss();
    arp::test::WebGpuOriginalTextureUploadSourceFixture::prepareActualLoss();
    if (!validatePreparedProducerDeviceLoss()) {
        std::fprintf(stderr, "RHI-WEBGPU-PREPARED-PRODUCER-LOSS-RECOVERY-FAILED\n");
        return 1;
    }
    arp::test::WebGpuOriginalL2DeviceLossFixture::verifyActualLoss();
    arp::test::OriginalObservedNativeLossBindingFixture::verifyActualLoss();
    arp::test::OriginalTypedCallbackCustodyFixture::verifyActualLoss();
    arp::test::WebGpuCanonicalCopyPassLoweringFixture::verifyActualLoss();
    arp::test::WebGpuCanonicalReadbackJoinsFixture::verifyMapExecutionLoss();
    arp::test::WebGpuCanonicalReadbackJoinsFixture::verifyActualLoss();
    arp::test::WebGpuCanonicalReadbackJoinsFixture::verifyOrderedLoss();
    arp::test::WebGpuCanonicalIssuerPreparationFixture::verifyActualLoss();
    arp::test::WebGpuCanonicalScopedRecordingFixture::verifyActualLoss();
    arp::test::WebGpuCanonicalSubmissionFixture::verifyActualLoss();
    arp::test::WebGpuCanonicalBufferUploadSourceFixture::verifyActualLoss();
    arp::test::WebGpuCanonicalTextureUploadSourceFixture::verifyActualLoss();
    arp::test::CanonicalTextureContentCreationFixture::verifyActualLoss();
    arp::test::WebGpuCanonicalOrderedCopyAttachmentFixture::verifyActualLoss();
    arp::test::WebGpuCanonicalCallbackCustodyFixture::verifyActualLoss();
    arp::test::WebGpuCanonicalColdCommandFixture::verifyActualLoss();
    arp::test::WebGpuOriginalStateTransactionFixture::verifyActualDeviceLoss();
    arp::test::WebGpuOriginalBufferUploadSourceFixture::verifyActualDeviceLoss();
    arp::test::TextureUploadArgumentsFixture::verifyActualDeviceLoss();
    arp::test::WebGpuOriginalQuerySourceGateFixture::verifyActualDeviceLoss();
    arp::test::WebGpuOriginalQueryOwnerFixture::verifyActualDeviceLoss();
    arp::test::WebGpuOriginalQueryBindingFixture::verifyActualDeviceLoss();
    arp::test::WebGpuOriginalQuerySlotsFixture::verifyActualDeviceLoss();
    arp::test::WebGpuOriginalQueryRecordReservationFixture::verifyActualDeviceLoss();
    arp::test::QueryPoolArgumentsFixture::verifyActualDeviceLoss();
    arp::test::TextureReadbackArgumentsFixture::verifyActualDeviceLoss();
    arp::test::TextureReadbackLayoutFixture::verifyActualDeviceLoss();
    arp::test::TextureReadbackContentCoverageFixture::verifyActualDeviceLoss();
    arp::test::WebGpuOriginalTextureReadbackSourceFixture::verifyActualDeviceLoss();
    arp::test::WebGpuOriginalTextureReadbackDestinationFixture::verifyActualDeviceLoss();
    arp::test::TextureUploadPackingFixture::verifyActualDeviceLoss();
    arp::test::TextureUploadContentTransactionFixture::verifyActualDeviceLoss();
    arp::test::WebGpuOriginalTextureUploadDestinationFixture::verifyActualDeviceLoss();
    arp::test::WebGpuOriginalTextureUploadSourceFixture::verifyActualDeviceLoss();
    arp::test::WebGpuOwnedReadbackContextFixture::verifyLoss();
    arp::test::WebGpuOwnedReadbackAdvancementFixture::verifyLoss();
    arp::test::WebGpuOriginalStateTransactionFixture::verifyLossQuotasRetired();
    arp::test::WebGpuOriginalTransferDependencyBatchFixture::verifyActualDeviceLoss();
    arp::test::WebGpuOrdinaryTransferExecutionFixture::verifyOriginalDependencyIsolatedRetired();
    arp::test::WebGpuOriginalTransferBatchFixture::verifyActualDeviceLoss();
    arp::test::WebGpuOriginalTransferStateValidationFixture::verifyActualDeviceLoss();
    if (!ordinaryLoss.retiredAfterActualLoss()) {
        std::fprintf(stderr, "RHI-WEBGPU-TRANSFER-LOSS-RECOVERY-FAILED\n");
        return 1;
    }
    if (!frozenTransferLoss.retiredAfterActualLoss()) {
        std::fprintf(stderr, "RHI-WEBGPU-FROZEN-TRANSFER-LOSS-RETIREMENT\n");
        return 1;
    }
    if (!frozenPassBindingLoss.retiredAfterActualLoss())
        return 1;
    std::puts("BINDLESS_RHI_FROZEN_TRANSFER_LOSS_RECOVERY_OK");
    std::puts("BINDLESS_RHI_TRANSFER_LOSS_RECOVERY_OK");
    if (!ordinaryCompletionLoss.retiredAfterActualLoss()) {
        std::fprintf(stderr, "RHI-WEBGPU-ORDINARY-COMPLETION-LOSS-RECOVERY-FAILED\n");
        return 1;
    }
    std::puts("BINDLESS_RHI_ORDINARY_COMPLETION_LOSS_RECOVERY_OK");
    arp::test::WebGpuOriginalComputeBatchFixture::verifyActualDeviceLoss();
    arp::test::WebGpuOriginalScalarAccessFixture::verifyActualDeviceLoss();
    arp::test::WebGpuOriginalCommandInputsFixture::verifyGraphicsDeviceLoss();
    arp::test::WebGpuFrozenCommandScalarsFixture::verifyActualDeviceLoss();
    arp::test::WebGpuOrdinaryShaderValidationFixture::verifyActualDeviceLoss();
    arp::test::OriginalArtifactComputeCreationFixture::verifyActualLoss();
    arp::test::OriginalRuntimeLengthNativeIntervalFixture::verifyActualLoss();
    arp::test::OriginalCommandDescriptorSlotsFixture::verifyActualLoss();
    arp::test::OriginalAcceptedComputePipelineFactoryFixture::verifyActualLoss();
    arp::test::OriginalTypedNativeLossPipelineFixture::verifyActualLoss<
        arp::rhi::ComputePipeline>();
    arp::test::OriginalArtifactComputeConsumptionFixture::verifyActualLoss();
    arp::test::OriginalArtifactGraphicsCreationFixture::verifyActualLoss();
    arp::test::OriginalAcceptedGraphicsPipelineFactoryFixture::verifyActualLoss();
    arp::test::OriginalTypedNativeLossPipelineFixture::verifyActualLoss<arp::rhi::RenderPipeline>();
    arp::test::OriginalArtifactGraphicsConsumptionFixture::verifyActualLoss();
    if (std::string_view(ordinaryPipelineLoss->validate())
        != "RHI-WEBGPU-ORDINARY-PIPELINE-DEVICE-LOST")
        return 1;
    ordinaryPipelineLoss.reset();
    std::puts("BINDLESS_RHI_ORDINARY_PIPELINE_DEVICE_LOSS_OK");
    arp::test::WebGpuDescriptorDependencyRetirementFixture::verifyLoss();
#endif
    std::puts("BINDLESS_RHI_ADMISSION_SMOKE_OK");
    return 0;
}
