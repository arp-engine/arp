#include "../fixtures/borrowed_texture_readback_retirement_fixture.h"
#include "../fixtures/cpp_source_evidence.h"
#include "../fixtures/depth_stencil_attachment_input_fixture.h"
#include "../fixtures/frame_input_lifecycle_checks.h"
#include "../fixtures/original_heap_registration_fixture.h"
#include "../fixtures/render_pass_arguments_fixture.h"
#include "../fixtures/webgpu_frozen_render_pass_binding_fixture.h"
#include "../fixtures/webgpu_frozen_transfer_arguments_fixture.h"
#include "../fixtures/webgpu_generic_command_projection_fixture.h"
#include "../fixtures/webgpu_generic_command_usage_fixture.h"
#include "../fixtures/webgpu_generic_recording_admission_fixture.h"
#include "../fixtures/webgpu_ordinary_completion_fixture.h"
#include "../fixtures/webgpu_ordinary_pipeline_fixture.h"
#include "../fixtures/webgpu_ordinary_resource_fixture.h"
#include "../fixtures/webgpu_original_buffer_upload_source_fixture.h"
#include "../fixtures/webgpu_owned_readback_context_fixture.h"
#include "../fixtures/webgpu_public_transition_admission_fixture.h"
#include "../fixtures/webgpu_timestamp_command_point_fixture.h"
#include "../fixtures/webgpu_transfer_retention_fixture.h"
#include "core/memory/cpu_allocator.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_bindless_root_cold_admission.h"
#include "rhi/backends/webgpu/webgpu_device.h"
#include "rhi/backends/webgpu/webgpu_native_buffer_map_receipt.h"
#include "rhi/frontend/bindless_compute_binding.h"
#include "rhi/frontend/command_encoder_base.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/webgpu_runtime_capability_bridge.h"
#include "tests/fixtures/webgpu_host_diagnostic_device_fixture.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#if defined(NDEBUG)
#error "WebGPU device contract tests require active assertions"
#endif

namespace {

void assertOriginalRootPushUploadFailsClosed()
{
    using Admission = arp::rhi_webgpu::WebGpuBindlessRootColdAdmission;
    Admission::Budget budget{2u,
        true,
        16u,
        32u,
        128u,
        65536u,
        {1u, true, 1u, 1u, 1u, 4u, 4u, 65536u},
        {1u, true, 1u, 1u, 3u, 12u, 12u}};
    budget.pushUpload = {1u, true, 2u, 448u, 512u, 2u, 1u};
    Admission::Requirements requirements;
    const auto* id = Admission::deriveRequirements({}, {}, {}, budget, requirements);
    assert(id && std::string_view(id) == "RHI-WEBGPU-ROOT-PUSH-UPLOAD-RUNTIME");
    assert(
        !requirements.originalCommands && !requirements.encoders && !requirements.commandBuffers);
}

void assertNativeMapReceiptFailsClosed()
{
    using Receipt = arp::rhi_webgpu::WebGpuNativeBufferMapReceipt;
    Receipt receipt;
    assert(receipt.start() && receipt.readAndUnmap(nullptr, 0u) && receipt.abortAndDrain()
        && receipt.validateRetainedBinding(nullptr, 0u));
    assert(receipt.prepareCold(nullptr, 0u, nullptr, nullptr));
    assert(receipt.phase() == Receipt::Phase::Empty && !receipt.nativeMapSucceeded()
        && !receipt.nativeUnmapProven() && !receipt.invocationUncertain());
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    unsigned inert = 0u;
    const auto callback = +[](void*, Receipt::Outcome) noexcept {
        assert(false);
    };
    assert(std::string_view(receipt.prepareCold(&inert, 4u, &inert, callback))
        == "RHI-WEBGPU-NATIVE-MAP-RECEIPT-UNAVAILABLE");
    assert(receipt.phase() == Receipt::Phase::Empty && !receipt.nativeMapSucceeded());
#endif
}

void assertWebGpuRecordedStateMetadata(arp::rhi::GpuDevice& device)
{
    using namespace arp;
    using namespace arp::rhi;
    using namespace arp::rhi_webgpu;
    // Backend metadata contract only. Host tests cannot submit browser GPU work.
    auto buffer = device.createBuffer({"WebGpuRecordedState.Buffer",
        64u,
        ResourceUsage::CopySrc | ResourceUsage::CopyDst,
        MemoryUsage::CpuToGpu,
        ResourceState::Undefined});
    auto texture = device.createTexture({"WebGpuRecordedState.Texture",
        {4u, 4u, 1u},
        TextureFormat::Rgba8Unorm,
        TextureUsage::CopyDst | TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined});
    assert(buffer && texture);
    const TextureSubresourceRange range{0u, 1u, 0u, 1u, TextureAspect::Color};
    const auto bufferWrite = makeInferredResourceBarrier(
        buffer->id(), {}, ResourceState::Undefined, ResourceState::CopyDst);
    const auto bufferRead = makeInferredResourceBarrier(
        buffer->id(), {}, ResourceState::CopyDst, ResourceState::CopySrc);
    const auto textureWrite = makeInferredResourceBarrier(
        texture->id(), range, ResourceState::Undefined, ResourceState::CopyDst);
    {
        core::MutationJournal recording, conflicting;
        assert(transitionWebGpuRhiBuffer(*buffer, bufferWrite, &recording).success);
        assert(transitionWebGpuRhiTexture(*texture, textureWrite, &recording).success);
        const auto conflict = transitionWebGpuRhiBuffer(*buffer, bufferRead, &conflicting);
        assert(
            !conflict.success && conflict.diagnostics == "RHI-RESOURCE-STATE-RECORDING-CONFLICT");
        assert(buffer->state() == ResourceState::CopyDst);
        assert(transitionWebGpuRhiBuffer(*buffer, bufferRead, &recording).success);
    }
    assert(buffer->state() == ResourceState::Undefined);
    assert(texture->state() == ResourceState::Undefined);
    {
        core::MutationJournal recording;
        assert(transitionWebGpuRhiBuffer(*buffer, bufferWrite, &recording).success);
        assert(transitionWebGpuRhiTexture(*texture, textureWrite, &recording).success);
        recording.commit(); // Metadata acceptance only, not native queue evidence.
    }
    assert(buffer->state() == ResourceState::CopyDst);
    assert(texture->state() == ResourceState::CopyDst);
}

arp::tests::CppSourceEvidence readText(const char* path)
{
    // Source contracts compare logical lines, independent of checkout CRLF/LF.
    std::ifstream file(path);
    std::ostringstream stream;
    stream << file.rdbuf();
    return arp::core::toPmrString(stream.str());
}

void assertWebGpuRhiBackendDoesNotDelegateToNull()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    const auto deviceSource = readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_device.cpp");
    assert(deviceSource.find("createNullDevice") == std::pmr::string::npos);
#endif
}

void assertWebGpuNativeOwnershipIsSplitForReuse()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    const auto deviceSource = readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_device.cpp");
    const auto nativeHeader =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_runtime.h");
    const auto nativeSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_runtime.cpp");

    assert(!nativeHeader.empty());
    assert(!nativeSource.empty());
    assert(
        deviceSource.find("rhi/backends/webgpu/webgpu_native_runtime.h") != std::pmr::string::npos);
    assert(deviceSource.find("emscripten_webgpu_get_device") == std::pmr::string::npos);
    assert(nativeHeader.find("webGpuNativeDeviceAvailable") != std::pmr::string::npos);
    assert(nativeHeader.find("#include <webgpu/webgpu.h>") == std::pmr::string::npos);
    assert(nativeSource.find("wgpuDeviceCreateBuffer") != std::pmr::string::npos);
    assert(nativeSource.find("wgpuBufferRelease") != std::pmr::string::npos);
#endif
}

void assertWebGpuNativeGetterRequiresBootstrap()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    const auto runtimeSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_runtime.cpp");
    const auto resourcesSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_resources.cpp");
    const auto commandsSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_commands.cpp");
    const auto swapchainSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_swapchain.cpp");

    // emscripten_webgpu_get_device asserts when the browser bootstrap has not
    // installed Module.preinitializedWebGPUDevice. Every native translation
    // unit with a getter must therefore keep the readiness and loss gates.
    assert(runtimeSource.find("Module.preinitializedWebGPUDevice") != std::pmr::string::npos);
    assert(runtimeSource.find("Module.webGpuDeviceLostInfo") != std::pmr::string::npos);
    assert(resourcesSource.find("Module.preinitializedWebGPUDevice") != std::pmr::string::npos);
    assert(commandsSource.find("Module.preinitializedWebGPUDevice") != std::pmr::string::npos);
    assert(swapchainSource.find("Module.preinitializedWebGPUDevice") != std::pmr::string::npos);
#endif
}

void assertWebGpuLimitsComeFromCreatedDevice()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    const auto runtimeHeader =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_runtime.h");
    const auto runtimeSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_runtime.cpp");
    const auto platformSource =
        readText(ARP_SOURCE_DIR "/src/platform/web/render_system_webgpu.cpp");

    assert(runtimeHeader.find("struct WebGpuNativeDeviceLimits") != std::pmr::string::npos);
    assert(runtimeHeader.find("webGpuNativeDeviceLimits()") != std::pmr::string::npos);
    assert(runtimeSource.find("Module.webGpuLimits") == std::pmr::string::npos);
    assert(runtimeSource.find("device.limits.maxSamplersPerShaderStage") != std::pmr::string::npos);
    assert(runtimeSource.find("device.limits.maxStorageTexturesPerShaderStage")
        != std::pmr::string::npos);
    assert(platformSource.find("webGpuNativeDeviceLimits()") != std::pmr::string::npos);
    assert(platformSource.find("probe.maxTexture2DSize = 4096") == std::pmr::string::npos);
    assert(platformSource.find("probe.maxColorAttachments = 4") == std::pmr::string::npos);
    assert(platformSource.find("probe.maxDescriptorBindings = 64") == std::pmr::string::npos);
    assert(runtimeSource.find("maxTexture2DSize || 8192") == std::pmr::string::npos);
    assert(runtimeSource.find("maxColorAttachments || 8") == std::pmr::string::npos);
    assert(runtimeSource.find("maxDescriptorBindings || 100") == std::pmr::string::npos);
    assert(runtimeSource.find("uniformBufferOffsetAlignment || 256") == std::pmr::string::npos);
    assert(runtimeSource.find("maxUniformBufferBindingSize || 65536") == std::pmr::string::npos);
    assert(platformSource.find("maxSampledTexturesPerShaderStage") != std::pmr::string::npos);
#endif
}

void assertWebGpuPublicResourcesUsePrivateNativePayloads()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    const auto objectHeader =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_backend_objects.h");
    const auto objectSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_backend_objects.cpp");
    const auto nativeHeader =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_runtime.h");
    const auto nativeResourceSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_resources.cpp");

    assert(!nativeResourceSource.empty());
    assert(nativeHeader.find("struct WebGpuNativeBufferPayload;") != std::pmr::string::npos);
    assert(nativeHeader.find("struct WebGpuNativeTexturePayload;") != std::pmr::string::npos);
    assert(nativeHeader.find("struct WebGpuNativeTextureViewPayload;") != std::pmr::string::npos);
    assert(nativeHeader.find("struct WebGpuNativeSamplerPayload;") != std::pmr::string::npos);
    assert(nativeHeader.find("createWebGpuNativeBufferPayload") != std::pmr::string::npos);
    assert(nativeHeader.find("createWebGpuNativeTexturePayload") != std::pmr::string::npos);
    assert(nativeHeader.find("createWebGpuNativeTextureViewPayload") != std::pmr::string::npos);
    assert(nativeHeader.find("createWebGpuNativeSamplerPayload") != std::pmr::string::npos);

    assert(objectHeader.find("WebGpuRhiNativePayloadPolicy") != std::pmr::string::npos);
    assert(
        objectSource.find("rhi/backends/webgpu/webgpu_native_runtime.h") != std::pmr::string::npos);
    assert(
        objectSource.find("std::shared_ptr<WebGpuNativeBufferPayload>") != std::pmr::string::npos);
    assert(
        objectSource.find("std::shared_ptr<WebGpuNativeTexturePayload>") != std::pmr::string::npos);
    assert(objectSource.find("std::shared_ptr<WebGpuNativeTextureViewPayload>")
        != std::pmr::string::npos);
    assert(
        objectSource.find("std::shared_ptr<WebGpuNativeSamplerPayload>") != std::pmr::string::npos);
    assert(objectSource.find("createWebGpuNativeBufferPayload(desc, initializer)")
        != std::pmr::string::npos);
    assert(objectSource.find("createWebGpuNativeTexturePayload(desc)") != std::pmr::string::npos);
    // Native texture-view construction is intentionally kept in the native
    // resource translation unit; the public backend object only owns the
    // private payload returned by the RHI-facing factory.
    assert(
        objectSource.find("getOrCreateWebGpuNativeTextureViewPayload") != std::pmr::string::npos);
    assert(objectSource.find("createWebGpuNativeSamplerPayload(desc)") != std::pmr::string::npos);
    assert(objectSource.find("#include <webgpu/webgpu.h>") == std::pmr::string::npos);
    assert(objectSource.find("WGPUBuffer") == std::pmr::string::npos);
    assert(objectSource.find("WGPUTexture") == std::pmr::string::npos);

    assert(nativeResourceSource.find("createWebGpuNativeBufferPayload") != std::pmr::string::npos);
    assert(nativeResourceSource.find("createWebGpuNativeTexturePayload") != std::pmr::string::npos);
    assert(nativeResourceSource.find("createWebGpuNativeTextureViewPayload")
        != std::pmr::string::npos);
    assert(nativeResourceSource.find("createWebGpuNativeSamplerPayload") != std::pmr::string::npos);
    // Preserve texture dimension; the pinned scalar-only ABI must reject an
    // array before native layout construction can discard its cardinality.
    assert(nativeResourceSource.find("nativeTextureViewDimension") != std::pmr::string::npos);
    const auto layoutStart =
        nativeResourceSource.find("std::optional<WGPUBindGroupLayoutEntry> nativeLayoutEntry(");
    const auto arrayGate = nativeResourceSource.find(
        "WebGpuDescriptorArrayAbiContract::validateCount(binding.arrayCount)", layoutStart);
    const auto layoutEntry =
        nativeResourceSource.find("auto entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT", layoutStart);
    assert(layoutStart != std::pmr::string::npos && arrayGate != std::pmr::string::npos
        && arrayGate < layoutEntry);
    assert(nativeResourceSource.find("entry.bindingArraySize = 0u", arrayGate)
        != std::pmr::string::npos);
    assert(nativeResourceSource.find("entry.bindingArraySize = binding.arrayCount")
        == std::pmr::string::npos);
#endif
}

void assertWebGpuDescriptorsUsePrivateNativePayloads()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    const auto objectHeader =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_backend_objects.h");
    const auto descriptorSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_backend_descriptors.cpp");
    const auto deviceSource = readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_device.cpp");
    const auto nativeHeader =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_runtime.h");
    const auto nativeResourceSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_resources.cpp");

    assert(
        nativeHeader.find("struct WebGpuNativeDescriptorGroupPayload;") != std::pmr::string::npos);
    assert(nativeHeader.find("WebGpuNativeDescriptorBindingPayload") != std::pmr::string::npos);
    assert(nativeHeader.find("createWebGpuNativeDescriptorGroupPayload") != std::pmr::string::npos);
    assert(objectHeader.find("class WebGpuRhiResourceRegistry") != std::pmr::string::npos);
    assert(descriptorSource.find("WebGpuRhiResourceRegistry") != std::pmr::string::npos);
    assert(descriptorSource.find("std::shared_ptr<WebGpuNativeDescriptorGroupPayload>")
        != std::pmr::string::npos);
    assert(descriptorSource.find("createWebGpuNativeDescriptorGroupPayload")
        != std::pmr::string::npos);
    assert(deviceSource.find("registry_->registerBuffer") != std::pmr::string::npos);
    assert(deviceSource.find("registry_->registerTextureView") != std::pmr::string::npos);
    assert(deviceSource.find("registry_->registerSampler") != std::pmr::string::npos);
    assert(deviceSource.find(
               "createWebGpuRhiDescriptorArena(arenaDesc, registry_, nativePayloadPolicy())")
        != std::pmr::string::npos);
    assert(nativeResourceSource.find("wgpuDeviceCreateBindGroupLayout") != std::pmr::string::npos);
    assert(nativeResourceSource.find("wgpuDeviceCreateBindGroup") != std::pmr::string::npos);
    const auto commandSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_backend_commands.cpp");
    assert(commandSource.find("WebGPU RHI descriptor table bind requires a native command payload")
        != std::pmr::string::npos);
    assert(descriptorSource.find("#include <webgpu/webgpu.h>") == std::pmr::string::npos);
    assert(descriptorSource.find("WGPUBindGroup") == std::pmr::string::npos);
#endif
}

void assertWebGpuTransferCopyRequiresExplicitResources()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    const auto commandSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_backend_commands.cpp");
    assert(commandSource.find(
               "WebGPU RHI buffer copy requires explicit source and destination resources")
        != std::pmr::string::npos);
#endif
}

void assertWebGpuPipelinesUsePrivateNativePayloads()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    const auto objectHeader =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_backend_objects.h");
    const auto objectSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_backend_objects.cpp");
    const auto nativeHeader =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_runtime.h");
    const auto nativeResourceSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_resources.cpp");

    assert(nativeHeader.find("struct WebGpuNativeShaderModulePayload;") != std::pmr::string::npos);
    assert(
        nativeHeader.find("struct WebGpuNativeRenderPipelinePayload;") != std::pmr::string::npos);
    assert(
        nativeHeader.find("struct WebGpuNativeComputePipelinePayload;") != std::pmr::string::npos);
    assert(nativeHeader.find("createWebGpuNativeShaderModulePayload") != std::pmr::string::npos);
    assert(nativeHeader.find("createWebGpuNativeRenderPipelinePayload") != std::pmr::string::npos);
    assert(nativeHeader.find("createWebGpuNativeComputePipelinePayload") != std::pmr::string::npos);
    assert(objectSource.find("std::shared_ptr<WebGpuNativeRenderPipelinePayload>")
        != std::pmr::string::npos);
    assert(objectSource.find("std::shared_ptr<WebGpuNativeComputePipelinePayload>")
        != std::pmr::string::npos);
    assert(objectSource.find("createWebGpuNativeRenderPipelinePayload(desc)")
        != std::pmr::string::npos);
    assert(objectSource.find("createWebGpuNativeComputePipelinePayload(desc)")
        != std::pmr::string::npos);
    assert(nativeResourceSource.find("wgpuDeviceCreateShaderModule") != std::pmr::string::npos);
    assert(nativeResourceSource.find("wgpuDeviceCreateRenderPipeline") != std::pmr::string::npos);
    assert(nativeResourceSource.find("wgpuDeviceCreateComputePipeline") != std::pmr::string::npos);

    // The WebGPU lowering artifact is the executable capability contract. It
    // must reject an unavailable target before the native shader-module path
    // is entered; otherwise an unavailable WGSL artifact is reported as a
    // missing file/native object instead of a lowering DiagnosticFailure.
    const auto firstLowering = nativeResourceSource.find(
        "const auto lowering = loadWebGpuBindingSpaceLowering(*desc.shaderPackage);");
    const auto firstShaderModule = nativeResourceSource.find(
        "auto shader = createWebGpuNativeShaderModulePayload(*desc.shaderPackage);");
    assert(firstLowering != std::pmr::string::npos);
    assert(firstShaderModule != std::pmr::string::npos);
    assert(firstLowering < firstShaderModule);
    const auto secondLowering = nativeResourceSource.find(
        "const auto lowering = loadWebGpuBindingSpaceLowering(*desc.shaderPackage);",
        firstLowering + 1);
    const auto secondShaderModule = nativeResourceSource.find(
        "auto shader = createWebGpuNativeShaderModulePayload(*desc.shaderPackage);",
        firstShaderModule + 1);
    assert(secondLowering != std::pmr::string::npos);
    assert(secondShaderModule != std::pmr::string::npos);
    assert(secondLowering < secondShaderModule);
    assert(objectHeader.find("#include <webgpu/webgpu.h>") == std::pmr::string::npos);
    assert(objectSource.find("#include <webgpu/webgpu.h>") == std::pmr::string::npos);
#endif
}

void assertOpaqueProcessorRecordingAdmission()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    // These two reference-only entries require issued opaque closed objects.
    // Do not fabricate a processor/snapshot/authorization to claim native proof.
    const auto source =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_commands.cpp");
    assert(
        source.find(
            "scheduleIndirectProcessorFaultReadback(\n"
            "    const WebGpuNativeIndirectProcessor& processor, core::pmr::String& diagnostic)\n"
            "{\n    if (const auto* id=restrictedRecordingDiagnostic()) { diagnostic=id; return {}; }")
        != arp::core::pmr::String::npos);
    assert(
        source.find(
            "recordIndirectProcessorDraw(const WebGpuNativeIndirectProcessor& processor,\n"
            "    const rhi::BindlessSnapshot& snapshot, const rhi::BindlessAuthorization& authorization,\n"
            "    const rhi::BindlessAccessDeclarations& indirectAccess, core::pmr::String& diagnostic)\n"
            "{\n    if (const auto* id=restrictedRecordingDiagnostic()) { diagnostic=id; return {}; }")
        != arp::core::pmr::String::npos);
#endif
}

void assertWebGpuCommandsUsePrivateNativePayloads()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    const auto objectHeader =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_backend_objects.h");
    const auto commandSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_backend_commands.cpp");
    const auto deviceSource = readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_device.cpp");
    const auto nativeHeader =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_runtime.h");
    const auto nativeRuntimeSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_runtime.cpp");
    const auto nativeCommandSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_commands.cpp");
    const auto nativeTransferCommandSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_transfer_commands.cpp");
    const auto nativeResourceSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_resources.cpp");
    const auto descriptorContractSource = readText(ARP_SOURCE_DIR "/src/rhi/gpu_descriptors.h");
    const auto vulkanCommandSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_native_commands.cpp");
    const auto vulkanResourceSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_native_resources.cpp");
    const auto vulkanPipelineSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_native_pipelines.cpp");
    const auto vulkanPassSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_graphics_pass.cpp");
    const auto nativeTransferLoweringSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_transfer_copy_lowering.h");
    const auto nativeCommandSources =
        nativeCommandSource + nativeTransferCommandSource + nativeTransferLoweringSource;

    assert(nativeHeader.find("struct WebGpuNativeCommandPayload;") != std::pmr::string::npos);
    assert(nativeHeader.find("WebGpuNativeCommandStats") != std::pmr::string::npos);
    assert(nativeHeader.find("createWebGpuNativeCommandPayload") != std::pmr::string::npos);
    assert(
        objectHeader.find("std::shared_ptr<WebGpuNativeCommandStats>") != std::pmr::string::npos);
    assert(commandSource.find("std::shared_ptr<WebGpuNativeCommandPayload>")
        != std::pmr::string::npos);
    assert(commandSource.find("recordBufferBarrier") != std::pmr::string::npos);
    assert(commandSource.find("recordTextureBarrier") != std::pmr::string::npos);
    assert(commandSource.find("recordBufferCopy") != std::pmr::string::npos);
    assert(commandSource.find("recordBufferToTextureCopy") != std::pmr::string::npos);
    assert(commandSource.find("recordTextureToBufferCopy") != std::pmr::string::npos);
    assert(commandSource.find("readbackTexture") != std::pmr::string::npos);
    assert(commandSource.find("defaultTextureSubresourceRange(texture)") != std::pmr::string::npos);
    assert(commandSource.find("destination buffer") != std::pmr::string::npos);
    assert(commandSource.find("recordRenderPassBegin") != std::pmr::string::npos);
    assert(commandSource.find("recordGraphicsPipelineBind") != std::pmr::string::npos);
    assert(commandSource.find("recordDrawIndexed") != std::pmr::string::npos);
    assert(commandSource.find("recordComputePipelineBind") != std::pmr::string::npos);
    assert(commandSource.find("recordDescriptorSetBind") != std::pmr::string::npos);
    assert(commandSource.find("recordPushConstants") != std::pmr::string::npos);
    assert(commandSource.find("recordDispatch") != std::pmr::string::npos);
    assert(commandSource.find("render pipeline bind requires a native command payload")
        != std::pmr::string::npos);
    assert(commandSource.find("render descriptor bind requires a native command payload")
        != std::pmr::string::npos);
    assert(commandSource.find("indexed draw requires a native command payload")
        != std::pmr::string::npos);
    assert(commandSource.find("compute pipeline bind requires a native command payload")
        != std::pmr::string::npos);
    assert(commandSource.find("compute descriptor bind requires a native command payload")
        != std::pmr::string::npos);
    assert(
        commandSource.find("dispatch requires a native command payload") != std::pmr::string::npos);
    assert(commandSource.find("buffer-to-texture copy requires a native command payload")
        != std::pmr::string::npos);
    assert(commandSource.find("texture-to-buffer copy requires a native command payload")
        != std::pmr::string::npos);
    assert(commandSource.find("texture-to-texture copy requires a native command payload")
        != std::pmr::string::npos);
    assert(commandSource.find("RHI-WEBGPU-PASS-NATIVE-PAYLOAD-MISSING") != std::pmr::string::npos);
    assert(commandSource.find("nonTransferRecordingDiagnostic()") != std::pmr::string::npos);
    assert(commandSource.find("preparedRecordingDiagnostic()") != std::pmr::string::npos);
    assert(commandSource.find("command encoder requires a native command payload")
        != std::pmr::string::npos);
    assert(deviceSource.find("webGpuGenericCompletionResourceGraphDiagnostic")
        != std::pmr::string::npos);
    std::size_t optionalPayloadBranchCount = 0;
    for (auto position = commandSource.find("if (nativePayload_)");
        position != std::pmr::string::npos;
        position = commandSource.find("if (nativePayload_)", position + 1)) {
        ++optionalPayloadBranchCount;
    }
    // The only optional payload branches are debug markers. Executable RHI
    // commands must use the fail-closed `if (!nativePayload_)` gate.
    assert(optionalPayloadBranchCount == 3u);
    assert(deviceSource.find("webGpuNativePayloadFor(commandBuffer)") == std::pmr::string::npos);
    assert(commandSource.find("#include <webgpu/webgpu.h>") == std::pmr::string::npos);
    assert(commandSource.find("WGPUCommandEncoder") == std::pmr::string::npos);

    assert(nativeCommandSources.find("wgpuDeviceCreateCommandEncoder") != std::pmr::string::npos);
    assert(nativeCommandSources.find("wgpuCommandEncoderCopyBufferToBuffer")
        != std::pmr::string::npos);
    assert(nativeCommandSources.find("wgpuCommandEncoderCopyBufferToTexture")
        != std::pmr::string::npos);
    assert(nativeCommandSources.find("wgpuCommandEncoderCopyTextureToBuffer")
        != std::pmr::string::npos);
    assert(nativeCommandSources.find("recordDrawIndexedIndirect") != std::pmr::string::npos);
    assert(nativeCommandSources.find("recordDrawIndexedIndirectCount") != std::pmr::string::npos);
    assert(nativeCommandSources.find("wgpuRenderPassEncoderDrawIndexedIndirect")
        != std::pmr::string::npos);
    assert(commandSource.find("indirect draw count has invalid args/count range")
        != std::pmr::string::npos);
    assert(commandSource.find("indirect draw count requires tracked args and count buffers")
        != std::pmr::string::npos);
    assert(commandSource.find("ResourceUsage::IndirectBuffer") != std::pmr::string::npos);
    assert(commandSource.find("vertex bind requires VertexBuffer usage") != std::pmr::string::npos);
    assert(commandSource.find("index bind requires IndexBuffer usage") != std::pmr::string::npos);
    assert(nativeResourceSource.find("hasDynamicOffset") != std::pmr::string::npos);
    assert(nativeResourceSource.find("dynamicOffsetBindings.begin()") != std::pmr::string::npos);
    assert(nativeResourceSource.find("dynamicOffsetBindings.end()") != std::pmr::string::npos);
    assert(nativeCommandSource.find("dynamic offset count") != std::pmr::string::npos);
    assert(nativeCommandSource.find("wgpuRenderPassEncoderSetBindGroup") != std::pmr::string::npos);
    assert(
        nativeCommandSource.find("wgpuComputePassEncoderSetBindGroup") != std::pmr::string::npos);
    assert(nativeCommandSource.find("does not provide GPU-counted indexed-indirect draw lowering")
        != std::pmr::string::npos);
    const auto commandLoweringSource =
        readText(ARP_SOURCE_DIR "/src/rhi/semantics/native_command_lowering.cpp");
    assert(commandLoweringSource.find("CommandRecordingIntent::RenderBundle")
        != std::pmr::string::npos);
    assert(nativeCommandSource.find("pushConstantBindGroup") != std::pmr::string::npos);
    assert(nativeCommandSource.find("nativeQueue() const noexcept") != std::pmr::string::npos);
    assert(nativeRuntimeSource.find("webGpuNativeTrackQueueCompletion") == std::pmr::string::npos);
    assert(nativeRuntimeSource.find("QueueCompletionState") == std::pmr::string::npos);
    assert(nativeRuntimeSource.find("unreclaimedQueueCompletions") == std::pmr::string::npos);
    assert(deviceSource.find("prepareForSubmission") == std::pmr::string::npos);
    const auto queueAdmission = deviceSource.find("webGpuGenericCompletionResourceGraphDiagnostic");
    assert(queueAdmission != std::pmr::string::npos);
    const auto queueClass = deviceSource.find("class WebGpuRhiQueue");
    const auto queueEnd = deviceSource.find("class WebGpuTextureReadbackExtensionImpl", queueClass);
    const auto queueSource = deviceSource.substr(queueClass, queueEnd - queueClass);
    assert(queueSource.find("nativePayload->submit") == std::pmr::string::npos);
    assert(queueSource.find("acceptRecordedCommandOwnership") == std::pmr::string::npos);
    assert(queueSource.find("markWebGpuRhiCommandBufferSubmitted") == std::pmr::string::npos);
    const auto nativeSubmitBegin =
        nativeCommandSource.find("bool WebGpuNativeCommandPayload::submit(");
    const auto nativeSubmitEnd =
        nativeCommandSource.find("bool WebGpuNativeCommandPayload::finished()", nativeSubmitBegin);
    const auto nativeSubmit =
        nativeCommandSource.substr(nativeSubmitBegin, nativeSubmitEnd - nativeSubmitBegin);
    assert(nativeSubmit.find("webGpuGenericCompletionResourceGraphDiagnostic")
        != std::pmr::string::npos);
    assert(nativeSubmit.find("pendingTimestampReadbacks") == std::pmr::string::npos);
    assert(nativeSubmit.find("pendingTextureReadbacks") == std::pmr::string::npos);
    assert(nativeSubmit.find("core::makeUnique") == std::pmr::string::npos);
    assert(deviceSource.find("class WebGpuCompletionFence") == std::pmr::string::npos);
    assert(deviceSource.find("WebGpuCompletionTimeline") == std::pmr::string::npos);
    assert(deviceSource.find("RHI-WEBGPU-PUBLIC-FENCE-RESOURCE-GRAPH-UNAVAILABLE")
        != std::pmr::string::npos);
    assert(deviceSource.find("completed_->wait()") == std::pmr::string::npos);
    assert(deviceSource.find("WebGPU RHI timeline synchronization is unsupported by WebGPU core")
        != std::pmr::string::npos);
    assert(nativeRuntimeSource.find("timelineState") == std::pmr::string::npos);
    assert(nativeRuntimeSource.find("timelineValue") == std::pmr::string::npos);
    assert(deviceSource.find("void wait() override {}") == std::pmr::string::npos);
    assert(deviceSource.find("createFence() override") != std::pmr::string::npos);
    assert(deviceSource.find("createTimelineSemaphore") != std::pmr::string::npos);
    assert(nativeCommandSource.find("const auto dynamicOffset") != std::pmr::string::npos);
    assert(nativeCommandSource.find("&dynamicOffset") != std::pmr::string::npos);
    assert(nativeResourceSource.find("entry.buffer.hasDynamicOffset = binding.dynamicOffset")
        != std::pmr::string::npos);
    assert(
        descriptorContractSource.find("validateDynamicOffsetArguments") != std::pmr::string::npos);
    assert(vulkanCommandSource.find("dynamicOffsetCount,") != std::pmr::string::npos);
    assert(vulkanCommandSource.find("dynamicOffsets);") != std::pmr::string::npos);
    assert(vulkanResourceSource.find("VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC")
        != std::pmr::string::npos);
    assert(vulkanResourceSource.find("VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC")
        != std::pmr::string::npos);
    assert(vulkanPipelineSource.find("binding.dynamicOffset") != std::pmr::string::npos);
    assert(vulkanPassSource.find("validateDynamicOffsetArguments") != std::pmr::string::npos);
    assert(nativeCommandSources.find("recordDispatchIndirect") != std::pmr::string::npos);
    assert(nativeCommandSources.find("wgpuComputePassEncoderDispatchWorkgroupsIndirect")
        != std::pmr::string::npos);
    assert(
        nativeCommandSources.find("wgpuCommandEncoderBeginRenderPass") != std::pmr::string::npos);
    assert(nativeCommandSources.find("wgpuRenderPassEncoderDrawIndexed") != std::pmr::string::npos);
    assert(nativeCommandSources.find("wgpuComputePassEncoderDispatchWorkgroups")
        != std::pmr::string::npos);
    assert(nativeCommandSources.find("wgpuCommandEncoderFinish") != std::pmr::string::npos);
    assert(nativeCommandSources.find("wgpuQueueSubmit") != std::pmr::string::npos);
    assert(
        nativeCommandSources.find("pushConstantUploadRingByteCapacity") != std::pmr::string::npos);
    // The physical push-constant group comes from the active pipeline's
    // generated lowering artifact; the historical group-3 convention must
    // not survive in command recording.
    assert(nativeCommandSource.find("impl_->pushConstantGroup = pipeline.pushConstantGroup")
        != std::pmr::string::npos);
    assert(nativeCommandSource.find("impl_->pushConstantGroup,") != std::pmr::string::npos);

    // Uploads must not report success when the native command payload is
    // absent. Immediate texture writes have no persistent native staging copy;
    // array writes still consume the RHI's explicit slice count.
    assert(commandSource.find("sourceUploadBuffer.isValid()") != std::pmr::string::npos);
    assert(commandSource.find("destinationResource.isValid()") != std::pmr::string::npos);
    assert(commandSource.find("upload source or destination buffer usage/range is invalid")
        != std::pmr::string::npos);
    assert(
        commandSource.find("upload requires a native command payload") != std::pmr::string::npos);
    assert(commandSource.find("nativeStagingBytes") == std::pmr::string::npos);
    assert(
        commandSource.find("RHI-WEBGPU-STAGING-COPY-PATH-UNAVAILABLE") != std::pmr::string::npos);
    assert(nativeTransferCommandSource.find("nativeCopySliceCount") != std::pmr::string::npos);
    assert(
        nativeTransferCommandSource.find("rowsPerImage * sliceCount64") != std::pmr::string::npos);
#endif
}

void assertWebGpuPushBindingIsReflectionOwned()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    const auto commandSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_commands.cpp");
    const auto payloadSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_payloads.h");
    const auto codegenSource = readText(ARP_SOURCE_DIR "/scripts/compile_shaders.py");

    // The bind group is built from the pipeline's reflection-lowered physical
    // binding, then cached in the command payload for atomic uploads.
    assert(commandSource.find("pipeline.pushConstantBinding") != std::pmr::string::npos);
    assert(
        commandSource.find("impl_->pushConstantBinding == UINT32_MAX") != std::pmr::string::npos);
    assert(commandSource.find("entry.binding = 0u") == std::pmr::string::npos);
    assert(payloadSource.find("pushConstantBinding") != std::pmr::string::npos);
    assert(codegenSource.find("PUSH_CONSTANT_LOWERING_CONTRACTS") == std::pmr::string::npos);
    assert(codegenSource.find("derive_wgsl_push_constant_contract") != std::pmr::string::npos);
#endif
}

void assertWebGpuUnsupportedFactoriesFailClosed()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    const auto deviceSource = readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_device.cpp");

    assert(deviceSource.find("return arp::core::makeUnique<DummyMemoryHeap>")
        == std::pmr::string::npos);
    assert(deviceSource.find("return arp::core::makeUnique<DummySemaphore>")
        == std::pmr::string::npos);
    assert(deviceSource.find("direct placed-resource entry point must fail closed")
        != std::pmr::string::npos);
    assert(deviceSource.find("// Never expose the callback counter as a timeline semaphore.")
        != std::pmr::string::npos);
#endif
}

void assertWebGpuTimestampQueryUsesARealBackendPayload()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    const auto queryHeader =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_query_pool.h");
    const auto querySource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_query_pool.cpp");
    const auto nativeHeader =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_runtime.h");
    const auto nativeRuntimeSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_runtime.cpp");
    const auto nativeSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_commands.cpp");
    const auto deviceSource = readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_device.cpp");
    const auto commandSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_backend_commands.cpp");

    assert(queryHeader.find("class WebGpuRhiQueryPool") != std::pmr::string::npos);
    assert(queryHeader.find("QueryExecutionState") != std::pmr::string::npos);
    assert(querySource.find("markRecorded") != std::pmr::string::npos);
    assert(querySource.find("RESET-WAITING-FOR-COMPLETION") != std::pmr::string::npos);
    assert(querySource.find("wgpuDeviceCreateQuerySet") != std::pmr::string::npos);
    assert(querySource.find("webGpuNativeTimestampQuerySupported") != std::pmr::string::npos);
    assert(nativeHeader.find("webGpuNativeLatestGpuTimingSample") != std::pmr::string::npos);
    assert(nativeSource.find("wgpuRenderPassEncoderWriteTimestamp") != std::pmr::string::npos);
    assert(nativeSource.find("wgpuComputePassEncoderWriteTimestamp") != std::pmr::string::npos);
    assert(nativeSource.find("wgpuCommandEncoderResolveQuerySet") != std::pmr::string::npos);
    assert(nativeSource.find("completeWebGpuTimestampReadback") != std::pmr::string::npos);
    assert(nativeSource.find("wgpuBufferMapAsync") != std::pmr::string::npos);
    assert(nativeSource.find("markAvailable") != std::pmr::string::npos);
    assert(nativeSource.find("mapWebGpuNativeTimingReadbackBuffers") == std::pmr::string::npos);
    // A future id is not callback completion or permission to reclaim state.
    // The explicit spontaneous map callback can already have destroyed it.
    for (const auto* forbidden :
        {"const auto future = wgpuBufferMapAsync(", "const auto mapFuture = wgpuBufferMapAsync("}) {
        assert(nativeSource.find(forbidden) == std::pmr::string::npos);
        assert(nativeRuntimeSource.find(forbidden) == std::pmr::string::npos);
    }
    assert(nativeSource.find("std::exchange(pending.resolveBuffer, nullptr)")
        == std::pmr::string::npos);
    assert(nativeSource.find("std::exchange(pending.readbackBuffer, nullptr)")
        == std::pmr::string::npos);
    assert(nativeRuntimeSource.find("std::exchange(readbackBuffer_, nullptr)")
        != std::pmr::string::npos);
    assert(nativeRuntimeSource.find("webGpuNativeTimestampCommandPointFacts")
        != std::pmr::string::npos);
    assert(nativeRuntimeSource.find("WebGpuTimestampCommandPointGate::decide")
        != std::pmr::string::npos);
    assert(nativeRuntimeSource.find("d.features.has('timestamp-query')") != std::pmr::string::npos);
    assert(nativeRuntimeSource.find("GPURenderPassEncoder.prototype") != std::pmr::string::npos);
    assert(nativeRuntimeSource.find("GPUComputePassEncoder.prototype") != std::pmr::string::npos);
    assert(commandSource.find("recordTimestamp") != std::pmr::string::npos);
    assert(deviceSource.find("createWebGpuRhiQueryPool") != std::pmr::string::npos);
    assert(deviceSource.find("WebGPU timestamp-query payload is unavailable")
        != std::pmr::string::npos);
#endif
}

void assertWebGpuStorageBufferCapabilityIsIndependent()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    const auto nativeHeader =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_runtime.h");
    const auto nativeSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_runtime.cpp");
    const auto platformSource =
        readText(ARP_SOURCE_DIR "/src/platform/web/render_system_webgpu.cpp");
    const auto profileSource =
        readText(ARP_SOURCE_DIR "/src/rhi/semantics/webgpu_runtime_profile.cpp");
    const auto deviceSource = readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_device.cpp");
    const auto bridgeSource =
        readText(ARP_SOURCE_DIR "/src/rhi/semantics/webgpu_runtime_capability_bridge.cpp");

    assert(nativeHeader.find("webGpuNativeDeviceSupportsStorageBuffer") != std::pmr::string::npos);
    assert(nativeHeader.find("webGpuNativeDeviceSupportsSceneGeometryRayQuery")
        != std::pmr::string::npos);
    assert(
        nativeHeader.find("webGpuNativeDeviceSupportsComputeDispatch") != std::pmr::string::npos);
    assert(nativeHeader.find("webGpuNativeDeviceSupportsStorageTextureBinding")
        != std::pmr::string::npos);
    assert(nativeSource.find("WGPUBufferUsage_Storage") != std::pmr::string::npos);
    assert(nativeSource.find("WGPUBufferBindingType_Storage") != std::pmr::string::npos);
    assert(nativeSource.find("RHI WebGPU compute capability probe") != std::pmr::string::npos);
    assert(
        nativeSource.find("RHI WebGPU storage-texture capability probe") != std::pmr::string::npos);
    assert(platformSource.find("probe.storageBuffer") != std::pmr::string::npos);
    assert(
        platformSource.find("webGpuNativeDeviceSupportsStorageBuffer") != std::pmr::string::npos);
    assert(platformSource.find("webGpuNativeDeviceSupportsSceneGeometryRayQuery")
        != std::pmr::string::npos);
    assert(
        platformSource.find("webGpuNativeDeviceSupportsComputeDispatch") != std::pmr::string::npos);
    assert(platformSource.find("webGpuNativeDeviceSupportsStorageTextureBinding")
        != std::pmr::string::npos);
    assert(platformSource.find("probe.computeDispatch = true") == std::pmr::string::npos);
    assert(platformSource.find("probe.storageTextureBinding = true") == std::pmr::string::npos);
    assert(
        profileSource.find("probe.nativeDevice && probe.storageBuffer") != std::pmr::string::npos);
    assert(profileSource.find("profile.optionalFeatures.storageTextureBinding;")
        != std::pmr::string::npos);
    assert(profileSource.find("profile.optionalFeatures.sceneGeometryRayQuery")
        != std::pmr::string::npos);
    assert(profileSource.find("probe.sceneGeometryRayQuery") != std::pmr::string::npos);
    assert(
        profileSource.find("profile.optionalFeatures.computeDispatch") != std::pmr::string::npos);
    assert(profileSource.find("&& probe.sceneGeometryRayQuery") != std::pmr::string::npos);
    assert(deviceSource.find("report.storageBufferPromotionValidated") != std::pmr::string::npos);
    assert(deviceSource.find("report.sceneGeometryRayQueryPromotionValidated")
        != std::pmr::string::npos);
    assert(bridgeSource.find("profile.optionalFeatures.sceneGeometryRayQuery")
        != std::pmr::string::npos);
    assert(
        bridgeSource.find(
            "profile.optionalFeatures.computeDispatch\n        && profile.optionalFeatures.storageBuffer")
        == std::pmr::string::npos);
#endif
}

void assertWebGpuFixedIndirectCapabilityIsPublishedOnlyAfterProbe()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    const auto bridgeSource =
        readText(ARP_SOURCE_DIR "/src/rhi/semantics/webgpu_runtime_capability_bridge.cpp");
    const auto profileSource =
        readText(ARP_SOURCE_DIR "/src/rhi/semantics/webgpu_runtime_profile.cpp");
    const auto deviceSource = readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_device.cpp");

    assert(bridgeSource.find("NativeBackendFeature::IndirectDraw") != std::pmr::string::npos);
    assert(profileSource.find("case EngineFeature::IndirectDraw:") != std::pmr::string::npos);
    assert(profileSource.find("WebGpuRuntimeProfileStatus::PlatformValidated")
        != std::pmr::string::npos);
    assert(deviceSource.find("IndirectDraw") != std::pmr::string::npos);
#endif
}

void assertWebGpuDescriptorTableCapabilityFailsClosed()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    const auto profileSource =
        readText(ARP_SOURCE_DIR "/src/rhi/semantics/webgpu_runtime_profile.cpp");
    const auto capabilitySource =
        readText(ARP_SOURCE_DIR "/src/rhi/semantics/native_backend_capabilities.cpp");
    const auto commandSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_backend_commands.cpp");

    assert(profileSource.find("case EngineFeature::DescriptorTable:") != std::pmr::string::npos);
    assert(profileSource.find("return FallbackMode::DiagnosticFailure;") != std::pmr::string::npos);
    assert(capabilitySource.find("? NATIVE(DescriptorTable) : webgpu ? FAIL")
        != std::pmr::string::npos);
    assert(commandSource.find("WebGPU RHI descriptor table bind requires a native command payload")
        != std::pmr::string::npos);
#endif
}

void assertWebGpuBackendUsesAllocatorOwnedContainers()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    const auto backendRoot = std::filesystem::path(ARP_SOURCE_DIR "/src/rhi/backends/webgpu");
    for (const auto& entry : std::filesystem::directory_iterator(backendRoot)) {
        if (!entry.is_regular_file())
            continue;
        const auto extension = entry.path().extension().string();
        if (extension != ".cpp" && extension != ".h")
            continue;
        const auto source = readText(entry.path().string().c_str());
        assert(source.find("#include <vector>") == std::pmr::string::npos);
        assert(source.find("std::vector") == std::pmr::string::npos);
        assert(source.find("std::pmr::vector") == std::pmr::string::npos);
    }
#endif
}

void assertWebGpuDepthSubpassDoesNotUseColorLowering()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    const auto renderGraphSource =
        readText(ARP_SOURCE_DIR "/src/renderer/render_graph/render_graph.cpp");

    // Color and depth input attachments both use the executable separate-pass
    // route, but depth must retain texture_depth_2d and descriptor metadata.
    assert(renderGraphSource.find("does not provide an executable SubpassDepthInputAttachment")
        != std::pmr::string::npos);
    assert(renderGraphSource.find("!caps.features.subpassDepthInputAttachment")
        != std::pmr::string::npos);
    assert(renderGraphSource.find("subpassDepthInputAttachmentDegraded") != std::pmr::string::npos);
#endif
}

void assertWebGpuDefaultTextureTransitionInfersFormatAspect()
{
    using namespace arp::rhi;
    TextureDesc texture;
    texture.format = TextureFormat::D32Float;
    texture.mipLevelCount = 3;
    texture.arrayLayerCount = 2;
    const auto range = defaultTextureSubresourceRange(texture);
    assert(range.aspect == TextureAspect::Depth);
    assert(range.baseMipLevel == 0 && range.mipLevelCount == 3);
    assert(range.baseArrayLayer == 0 && range.arrayLayerCount == 2);
    const GpuResourceId resource{71};
    const auto barrier = makeInferredResourceBarrier(
        resource, range, ResourceState::DepthAttachment, ResourceState::ShaderRead);
    assert(barrier.resource == resource);
    assert(barrier.textureSubresources.aspect == TextureAspect::Depth);
    assert(barrier.textureSubresources.mipLevelCount == 3);
    assert(barrier.textureSubresources.arrayLayerCount == 2);
    assert(barrier.before == ResourceState::DepthAttachment);
    assert(barrier.after == ResourceState::ShaderRead);
    assert(barrier.sourceStages == PipelineStage::DepthStencil);
    assert(barrier.destinationStages
        == (PipelineStage::VertexShader | PipelineStage::FragmentShader
            | PipelineStage::ComputeShader));
    assert(barrier.sourceAccess == ResourceAccess::DepthStencilWrite);
    assert(barrier.destinationAccess == ResourceAccess::ShaderRead);
    texture.format = TextureFormat::Depth24Stencil8;
    assert(defaultTextureSubresourceRange(texture).aspect == TextureAspect::Depth);
    texture.format = TextureFormat::Rgba8Unorm;
    assert(defaultTextureSubresourceRange(texture).aspect == TextureAspect::Color);
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    // The texture transition overload that infers the full subresource range
    // (via rhi::defaultTextureSubresourceRange) now lives in the shared
    // CommandEncoderBase final overload rather than each backend's encoder.
    // Verify the base header provides it and that the WebGPU encoder no longer
    // carries a local copy of the delegation.
    const auto baseHeader = readText(ARP_SOURCE_DIR "/src/rhi/frontend/command_encoder_base.h");
    assert(baseHeader.find("defaultTextureSubresourceRange") != std::pmr::string::npos);
    assert(baseHeader.find("makeInferredResourceBarrier(") != std::pmr::string::npos);
    assert(baseHeader.find("texture.id(), defaultTextureSubresourceRange(texture.desc()),")
        != std::pmr::string::npos);

    const auto commandSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_backend_commands.cpp");
    assert(
        commandSource.find(
            "return transition(texture, { texture.id(), rhi::defaultTextureSubresourceRange(texture.desc()), before, after });")
        == std::pmr::string::npos);
    assert(commandSource.find("return transition(texture, { texture.id(), {}, before, after });")
        == std::pmr::string::npos);
#endif
}

void assertWebGpuSwapchainUsesPrivateNativePayloads()
{
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
    const auto objectHeader =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_backend_objects.h");
    const auto swapchainSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_backend_swapchain.cpp");
    const auto deviceSource = readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_device.cpp");
    const auto nativeHeader =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_runtime.h");
    const auto nativeSwapchainSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/webgpu/webgpu_native_swapchain.cpp");

    assert(nativeHeader.find("struct WebGpuNativeSwapchainPayload;") != std::pmr::string::npos);
    assert(nativeHeader.find("WebGpuNativeSwapchainStats") != std::pmr::string::npos);
    assert(nativeHeader.find("createWebGpuNativeSwapchainPayload") != std::pmr::string::npos);
    assert(nativeHeader.find("acquireCanvas(std::uint32_t frameSlot") != std::pmr::string::npos);
    assert(nativeHeader.find("browserManagedPresent(std::uint32_t frameSlot")
        != std::pmr::string::npos);
    assert(
        objectHeader.find("std::shared_ptr<WebGpuNativeSwapchainStats>") != std::pmr::string::npos);
    assert(swapchainSource.find("std::shared_ptr<WebGpuNativeSwapchainPayload>")
        != std::pmr::string::npos);
    // The L2 frame-slot bookkeeping now lives in the shared FrameSlotStateMachine
    // helper; the WebGPU swapchain drives it via slots_.acquireSlot() /
    // slots_.slotInFlight() / slots_.clearSlot(). The native payload calls
    // remain the backend-specific seam.
    assert(swapchainSource.find("FrameSlotStateMachine<2>") != std::pmr::string::npos);
    assert(swapchainSource.find("slots_.acquireSlot()") != std::pmr::string::npos);
    assert(swapchainSource.find("slots_.slotInFlight(") != std::pmr::string::npos);
    assert(swapchainSource.find("slots_.clearSlot(") != std::pmr::string::npos);
    assert(swapchainSource.find("acquireCanvas(acquired.slot") != std::pmr::string::npos);
    assert(
        swapchainSource.find("browserManagedPresent(frame.frameIndex") != std::pmr::string::npos);
    assert(swapchainSource.find("resizeCanvas") != std::pmr::string::npos);
    assert(swapchainSource.find("surfaceLoss") != std::pmr::string::npos);
    assert(swapchainSource.find("hiddenTabFrameSkip") != std::pmr::string::npos);
    // The per-backend frame-slot state members are gone (lifted to the helper).
    assert(swapchainSource.find("frameSlotsInFlight_") == std::pmr::string::npos);
    assert(swapchainSource.find("findFreeFrameSlot") == std::pmr::string::npos);
    assert(swapchainSource.find("frameSlotInFlight") == std::pmr::string::npos);
    assert(swapchainSource.find("frameSerial_ % maxFramesInFlight()") == std::pmr::string::npos);
    assert(swapchainSource.find("#include <webgpu/webgpu.h>") == std::pmr::string::npos);
    assert(swapchainSource.find("WGPUSurface") == std::pmr::string::npos);
    assert(deviceSource.find("inspectWebGpuRhiNativeSwapchainStats") != std::pmr::string::npos);

    assert(nativeSwapchainSource.find("wgpuInstanceCreateSurface") != std::pmr::string::npos);
    assert(nativeSwapchainSource.find("wgpuSurfaceConfigure") != std::pmr::string::npos);
    assert(nativeSwapchainSource.find("wgpuSurfaceGetCurrentTexture") != std::pmr::string::npos);
    assert(nativeSwapchainSource.find("wgpuSurfaceUnconfigure") != std::pmr::string::npos);
    assert(nativeSwapchainSource.find("browserManagedPresent") != std::pmr::string::npos);
    assert(nativeSwapchainSource.find("currentTexturesBySlot") != std::pmr::string::npos);
    assert(nativeSwapchainSource.find("releaseTexture(impl_->currentTexture)")
        == std::pmr::string::npos);
    assert(nativeSwapchainSource.find("const auto previousDesc = impl_->desc")
        != std::pmr::string::npos);
    assert(nativeSwapchainSource.find("impl_->desc = previousDesc") != std::pmr::string::npos);
    assert(
        nativeSwapchainSource.find("const bool previousSurfaceAvailable = impl_->surfaceAvailable")
        != std::pmr::string::npos);
    assert(nativeSwapchainSource.find("impl_->surfaceAvailable = previousSurfaceAvailable")
        != std::pmr::string::npos);
#endif
}

arp::rhi::WebGpuRuntimeProfile makeValidatedWebGpuProfile()
{
    arp::rhi::WebGpuRuntimeProbe probe;
    probe.browserRuntime = true;
    probe.canvasSurface = true;
    probe.nativeDevice = true;
    probe.computeDispatch = true;
    probe.storageTextureBinding = true;
    probe.storageBuffer = true;
    probe.sceneGeometryRayQuery = true;
    probe.timestampQuery = true;
    probe.bgra8UnormStorage = true;
    probe.rgba16FloatStorage = true;
    probe.rgba16FloatBlendable = true;
    probe.textureCompressionBc = true;
    probe.maxTexture2DSize = 8192;
    probe.maxColorAttachments = 4;
    probe.maxDescriptorBindings = 64;
    probe.maxSampledTexturesPerShaderStage = 16;
    probe.uniformBufferOffsetAlignment = 256;
    probe.storageBufferOffsetAlignment = 256;
    probe.maxUniformBufferBindingSize = 65536;
    probe.maxStorageBufferBindingSize = 134217728;
    return arp::rhi::buildWebGpuRuntimeProfile(probe);
}

arp::rhi::ShaderPackageDesc makeWebGpuShaderPackage()
{
    arp::rhi::ShaderPackageDesc shaderPackage;
    shaderPackage.label = "WebGpuRhiShaderPackage";
    shaderPackage.slangModulePath = "webgpu_rhi.slang";
    shaderPackage.entrypoints.push_back(
        {"webgpu_rhi.slang", "vsMain", arp::rhi::ShaderStage::Vertex});
    shaderPackage.entrypoints.push_back(
        {"webgpu_rhi.slang", "fsMain", arp::rhi::ShaderStage::Fragment});
    shaderPackage.entrypoints.push_back(
        {"webgpu_rhi.slang", "csMain", arp::rhi::ShaderStage::Compute});
    shaderPackage.artifacts.spirvPath = "webgpu_rhi.spv";
    shaderPackage.artifacts.mslPath = "webgpu_rhi.metal";
    shaderPackage.artifacts.wgslPath = "webgpu_rhi.wgsl";
#if defined(ARP_SOURCE_DIR)
    shaderPackage.artifacts.reflectionPath =
        ARP_SOURCE_DIR "/tests/fixtures/webgpu_rhi.reflection.yaml";
#else
    shaderPackage.artifacts.reflectionPath = "webgpu_rhi.reflection.yaml";
#endif
    shaderPackage.reflection.pushConstantByteCount = 64;
    shaderPackage.reflection.pushConstantContractVersion = "AtomicFullBlockPush/1";
    shaderPackage.reflection.pushConstantStages = "VS|FS|CS";
    shaderPackage.reflection.pushConstantWebGpuBindingGroup = 3;
    shaderPackage.reflection.pushConstantWebGpuBindingIndex = 0;
    shaderPackage.reflection.pushConstantMembers.push_back({
        "data",
        "uint32",
        32,
        0,
        0,
        0,
        0,
        false,
        {16},
        4,
        0,
        64,
        0,
    });
    return shaderPackage;
}

arp::rhi_webgpu::WebGpuRhiDeviceDesc makeWebGpuDeviceDesc(
    const arp::rhi::WebGpuRuntimeProfile& profile)
{
    auto bridge = arp::rhi::semantics::buildWebGpuRuntimeCapabilityContract(profile);

    arp::rhi_webgpu::WebGpuRhiDeviceDesc desc;
    desc.label = "RHIWebGPUVerticalSlice";
    desc.runtimeProfile = profile;
    desc.capabilityContract = bridge.contract;
    desc.memoryBudgetBytes = 64u * 1024u * 1024u;
    desc.enableBackendDescriptorGroups = true;
    desc.enablePushConstantUploadRing = true;
    desc.pushConstantUploadRingByteCapacity = 4096;
    desc.uploadStagingByteCapacity = 4u * 1024u * 1024u;
    desc.enableCanvasLifecycleDiagnostics = true;
    desc.enableResizeDiagnostics = true;
    desc.enableSurfaceLostDiagnostics = true;
    desc.enableHiddenTabFrameSkipping = true;
    desc.requireComputeStorageRuntimePromotion = true;
    desc.allowNativeHandleExposureToUpperLayers = false;
    desc.allowBlockingSurfaceAcquire = false;
    desc.allowPerDrawPipelineCreation = false;
    desc.allowPerFrameShaderCompilation = false;
    return desc;
}

} // namespace

void verifyHostSwapchainMutationAdmission()
{
    using namespace arp;
    rhi::SwapchainDesc desc{
        "HostOnlySurfaceMutation", {32u, 32u}, rhi::TextureFormat::Rgba8Unorm, 2u};
    auto swapchain = rhi_webgpu::createWebGpuRhiSwapchain(desc, {false, false}, {}, {});
    assert(swapchain);
    const auto acquired = swapchain->acquireNextFrame();
    assert(acquired.success);
    const auto lost = swapchain->setSurfaceAvailable(false);
    const auto resized = swapchain->resize({64u, 64u});
    rhi::SwapchainReconfigureRequest request;
    request.desc = desc;
    request.desc.extent = {64u, 64u};
    const auto reconfigured = swapchain->reconfigure(request);
    for (const auto& rejected : {lost, resized, reconfigured}) {
        assert(!rejected.success && !rejected.acquireAllowed && !rejected.presentAllowed
            && !rejected.releaseSurfaceResources && rejected.swapchainRebuildRequired);
        assert(rejected.diagnostics == "RHI-WEBGPU-SURFACE-MUTATION-IN-FLIGHT");
    }
    assert(swapchain->isFrameAcquired(acquired));
    assert(swapchain->discardFrame(acquired).success);
    assert(swapchain->resize({64u, 64u}).success);
    assert(swapchain->setSurfaceAvailable(false).success);
}

int main(int argc, char** argv)
{
    verifyHostSwapchainMutationAdmission();
    if (argc == 2 && std::string_view(argv[1]) == "--heap-placement") {
        const auto profile = makeValidatedWebGpuProfile();
        auto device = arp::tests::createWebGpuContractDevice(makeWebGpuDeviceDesc(profile));
        assert(device && device->lastMemoryHeapCreationDiagnostic().empty());
        arp::rhi::TextureDesc placement;
        placement.extent = {4u, 4u, 1u};
        placement.format = arp::rhi::TextureFormat::Rgba8Unorm;
        placement.usage = arp::rhi::TextureUsage::Sampled;
        const auto unavailable = device->queryTexturePlacementRequirements(placement);
        assert(!unavailable.sizeBytes && !unavailable.alignmentBytes);
        assert(!device->createMemoryHeap(1024u, 256u, arp::rhi::MemoryUsage::Transient));
        assert(
            device->lastMemoryHeapCreationDiagnostic() == "RHI-WEBGPU-HEAP-PLACEMENT-UNAVAILABLE");
        assert(!device->capabilities().features.transientAliasing);
        std::puts(
            "HeapPlacementAdmission/1 WebGPU direct heap/query refusal: PASS (no native claim)");
        return 0;
    }
    assertOriginalRootPushUploadFailsClosed();
    assertNativeMapReceiptFailsClosed();
    arp::test::WebGpuFrozenRenderPassBindingFixture::verifyContract();
    arp::test::RenderPassArgumentsFixture::verifyContract();
    arp::test::WebGpuGenericCommandUsageFixture::verifyContract();
    arp::test::WebGpuFrozenTransferArgumentsFixture::verifyContract();
    arp::test::WebGpuOriginalBufferUploadSourceFixture::verifyContract();
    assertOpaqueProcessorRecordingAdmission();
    arp::test::WebGpuGenericRecordingAdmissionFixture::verifyContract();
    arp::test::WebGpuTransferRetentionFixture::verifyBudgetGates();
    arp::test::WebGpuTimestampCommandPointFixture::verifyContract();
    arp::test::WebGpuGenericCommandProjectionFixture::verifyContract();
    arp::test::WebGpuOrdinaryCompletionFixture::verifyContract();
    arp::test::WebGpuOrdinaryResourceFixture::verifyContract();
    arp::test::WebGpuOrdinaryDescriptorFixture::verifyContract();
    arp::test::WebGpuOrdinaryPipelineFixture::verifyContract();
    arp::test::WebGpuOrdinaryPhysicalGroupsFixture::verifyContract();
    using namespace arp::rhi;
    using namespace arp::rhi_webgpu;

    // L2 contract requires non-null push-constant data.
    static const std::uint8_t kPushData[256] = {};
    assertWebGpuRhiBackendDoesNotDelegateToNull();
    assertWebGpuNativeOwnershipIsSplitForReuse();
    assertWebGpuNativeGetterRequiresBootstrap();
    assertWebGpuLimitsComeFromCreatedDevice();
    assertWebGpuPushBindingIsReflectionOwned();
    assertWebGpuPublicResourcesUsePrivateNativePayloads();
    assertWebGpuDescriptorsUsePrivateNativePayloads();
    assertWebGpuTransferCopyRequiresExplicitResources();
    assertWebGpuPipelinesUsePrivateNativePayloads();
    assertWebGpuCommandsUsePrivateNativePayloads();
    assertWebGpuTimestampQueryUsesARealBackendPayload();
    assertWebGpuStorageBufferCapabilityIsIndependent();
    assertWebGpuFixedIndirectCapabilityIsPublishedOnlyAfterProbe();
    assertWebGpuDescriptorTableCapabilityFailsClosed();
    assertWebGpuBackendUsesAllocatorOwnedContainers();
    assertWebGpuDepthSubpassDoesNotUseColorLowering();
    assertWebGpuDefaultTextureTransitionInfersFormatAspect();
    assertWebGpuSwapchainUsesPrivateNativePayloads();
    assertWebGpuUnsupportedFactoriesFailClosed();

    const auto profile = makeValidatedWebGpuProfile();
    assert(profile.status == WebGpuRuntimeProfileStatus::PlatformValidated);
    assert(profile.optionalFeatures.computeDispatch);
    assert(profile.optionalFeatures.storageTextureBinding);
    assert(profile.optionalFeatures.timestampQuery);
    auto profileBridge = arp::rhi::semantics::buildWebGpuRuntimeCapabilityContract(profile);
    assert(profileBridge.success);
    assert(arp::rhi::semantics::hasNativeFeature(
        profileBridge.contract, arp::rhi::semantics::NativeBackendFeature::IndirectDraw));
    assert(arp::rhi::semantics::hasNativeFeature(profileBridge.contract,
        arp::rhi::semantics::NativeBackendFeature::SeparatePassSubpassInputAttachment));
    assert(arp::rhi::semantics::hasNativeFeature(
        profileBridge.contract, arp::rhi::semantics::NativeBackendFeature::SceneGeometryRayQuery));
    WebGpuSceneRuntimeWorkloadDesc sceneRuntimeWorkload;
    sceneRuntimeWorkload.requested = true;
    sceneRuntimeWorkload.drawCommandCount = 3;
    sceneRuntimeWorkload.materialResourceCount = 3;
    sceneRuntimeWorkload.descriptorSetCount = 2;
    sceneRuntimeWorkload.pushConstantSizeBytes = 128;
    sceneRuntimeWorkload.oneCompatibleWorldUiPass = true;
    const auto sceneRuntimeSupport =
        validateWebGpuSceneRuntimeWorkload(profile, sceneRuntimeWorkload);
    assert(sceneRuntimeSupport.success);
    assert(sceneRuntimeSupport.supportLabel == WebGpuSceneRuntimeSupportLabel::Supported);
    assert(sceneRuntimeSupport.descriptorCapacityValidated);
    assert(sceneRuntimeSupport.pushConstantEmulationValidated);
    assert(sceneRuntimeSupport.onePassValidated);
    assert(!sceneRuntimeSupport.nativeHandleExposedToUpperLayer);

    auto insufficientDescriptorProbe = WebGpuRuntimeProbe{};
    insufficientDescriptorProbe.browserRuntime = true;
    insufficientDescriptorProbe.canvasSurface = true;
    insufficientDescriptorProbe.nativeDevice = true;
    insufficientDescriptorProbe.maxTexture2DSize = 4096;
    insufficientDescriptorProbe.maxColorAttachments = 4;
    insufficientDescriptorProbe.maxDescriptorBindings = 1;
    insufficientDescriptorProbe.maxSampledTexturesPerShaderStage = 16;
    insufficientDescriptorProbe.uniformBufferOffsetAlignment = 256;
    insufficientDescriptorProbe.storageBufferOffsetAlignment = 256;
    insufficientDescriptorProbe.maxUniformBufferBindingSize = 65536;
    insufficientDescriptorProbe.maxStorageBufferBindingSize = 134217728;
    const auto insufficientDescriptorProfile =
        buildWebGpuRuntimeProfile(insufficientDescriptorProbe);
    const auto sceneRuntimeDiagnostic =
        validateWebGpuSceneRuntimeWorkload(insufficientDescriptorProfile, sceneRuntimeWorkload);
    assert(!sceneRuntimeDiagnostic.success);
    assert(
        sceneRuntimeDiagnostic.supportLabel == WebGpuSceneRuntimeSupportLabel::DiagnosticFailure);
    assert(sceneRuntimeDiagnostic.diagnostics.find("scene-runtime") != std::pmr::string::npos);

    auto desc = makeWebGpuDeviceDesc(profile);
    const auto report = describeWebGpuRhiDevice(desc);
    assert(report.success);
    // A usable scalar-descriptor device must not require/promote a native
    // binding-array ABI that the command and resource paths reject.
    assert(!arp::rhi::semantics::hasNativeFeature(
        desc.capabilityContract, arp::rhi::semantics::NativeBackendFeature::DescriptorIndexing));
    auto fabricatedArrayDesc = desc;
    fabricatedArrayDesc.capabilityContract.features.push_back(
        arp::rhi::semantics::NativeBackendFeature::DescriptorIndexing);
    assert(!describeWebGpuRhiDevice(fabricatedArrayDesc).success);
    assert(report.backend == Backend::WebGPU);
    assert(report.runtimeCapabilityPromotedFromProbe);
    assert(report.computePromotionValidated);
    assert(report.storageTexturePromotionValidated);
    assert(report.storageBufferPromotionValidated);
    assert(report.sceneGeometryRayQueryPromotionValidated);
    assert(report.backendDescriptorGroupsIntegrated);
    assert(report.backendDescriptorVocabularyHiddenFromPublicApi);
    assert(report.pushConstantUploadRingEmulated);
    assert(report.pushConstantUploadRingByteCapacity >= 64u);
    assert(report.canvasLifecycleDiagnosticsSupported);
    assert(report.resizeDiagnosticsSupported);
    assert(report.surfaceLostDiagnosticsSupported);
    assert(report.hiddenTabFrameSkippingSupported);
    assert(report.boundedSurfaceSynchronization);
    assert(!report.queueSubmitSupported);
    assert(report.swapchainCanvasLifecycleSupported);
    assert(!report.nullDelegateUsed);
    assert(report.backendOwnedResourceObjects);
    assert(report.backendOwnedDescriptorObjects);
    assert(report.backendOwnedPipelineObjects);
    assert(report.backendOwnedCommandObjects);
    assert(report.backendOwnedQueueSubmit);
    assert(report.backendOwnedSwapchain);
    assert(report.backendOwnedUploadStagingSupported);
    assert(report.uploadStagingNativeHandlesHidden);
    assert(report.uploadStagingBounded);
    assert(report.publicDeviceRuntimeBackingRequired);
#if defined(__EMSCRIPTEN__)
    if (report.publicDeviceNativeDeviceAvailable) {
        assert(!report.publicDeviceDiagnosticOnlyFallback);
    } else {
        assert(report.publicDeviceDiagnosticOnlyFallback);
    }
    assert(!report.publicDeviceWgpuBackedObjects);
    assert(report.publicDeviceSemanticOnlyFallback);
#else
    assert(!report.publicDeviceNativeDeviceAvailable);
    assert(!report.publicDeviceWgpuBackedObjects);
    assert(report.publicDeviceSemanticOnlyFallback);
    assert(report.publicDeviceDiagnosticOnlyFallback);
    assert(report.nonEmscriptenDiagnosticOnly);
    assert(report.nonEmscriptenNativeClaimRejected);
#endif
    assert(report.nativeDeviceAvailable == report.publicDeviceNativeDeviceAvailable);
    assert(!report.nativeBufferCreated);
    assert(!report.nativeTextureCreated);
    assert(!report.nativeTextureViewCreated);
    assert(!report.nativeSamplerCreated);
    assert(!report.nativeDescriptorGroupCreated);
    assert(!report.nativeShaderModuleCreated);
    assert(!report.nativePipelineCreated);
    assert(!report.nativeCommandEncoderCreated);
    assert(!report.nativeCommandBufferCreated);
    assert(!report.queueSubmitted);
    assert(!report.canvasAcquired);
    assert(!report.canvasPresentedByBrowser);
    assert(!report.resizeHandled);
    assert(!report.surfaceLossHandled);
    assert(!report.hiddenTabHandled);
    assert(!report.perFramePipelineRebuild);
    assert(!report.nativeHandleExposedToUpperLayer);
    assert(!report.perDrawPipelineCreationAllowed);
    assert(!report.perFrameShaderCompilationAllowed);

    auto forgedCapabilityContract = desc;
    forgedCapabilityContract.capabilityContract.features.push_back(
        semantics::NativeBackendFeature::Rg16FloatStorageTexture);
    const auto forgedCapabilityReport = describeWebGpuRhiDevice(forgedCapabilityContract);
    assert(!forgedCapabilityReport.success);
    assert(forgedCapabilityReport.diagnostics.find("contract mismatch") != std::pmr::string::npos);

    auto missingStorageBufferProbe = WebGpuRuntimeProbe{};
    missingStorageBufferProbe.browserRuntime = true;
    missingStorageBufferProbe.canvasSurface = true;
    missingStorageBufferProbe.nativeDevice = true;
    missingStorageBufferProbe.computeDispatch = true;
    missingStorageBufferProbe.storageTextureBinding = true;
    const auto missingStorageBufferProfile = buildWebGpuRuntimeProfile(missingStorageBufferProbe);
    const auto missingStorageBufferReport =
        describeWebGpuRhiDevice(makeWebGpuDeviceDesc(missingStorageBufferProfile));
    assert(!missingStorageBufferReport.success);
    assert(!missingStorageBufferReport.diagnostics.empty());

    if (!arp::rhi_webgpu::webGpuNativeDeviceAvailable()) {
        assert(!arp::rhi_webgpu::createWebGpuRhiDevice(desc));
        for (const auto version : {0u, 2u, 0xffffffffu})
            assert(!arp::rhi_webgpu::createWebGpuRhiHostDiagnosticDevice(desc, version));
        auto fixture = arp::rhi_webgpu::createWebGpuRhiHostDiagnosticDevice(desc, 1u);
        assert(fixture && !fixture->capabilities().core.graphics);
        auto invalid = desc;
        invalid.uploadStagingByteCapacity = 0u;
        assert(!arp::rhi_webgpu::createWebGpuRhiHostDiagnosticDevice(invalid, 1u));
    } else {
        assert(!arp::rhi_webgpu::createWebGpuRhiHostDiagnosticDevice(desc, 1u));
    }
    auto device = arp::tests::createWebGpuContractDevice(desc);
    assert(device);
    assert(!device->capabilities().features.nativeSync);
    assert(!device->createFence());
    {
        auto unsupported = arp::tests::createWebGpuContractDevice(desc);
        assert(unsupported);
        arp::tests::OriginalHeapRegistrationFixture::verifyUnavailable(
            *unsupported, "RHI-ORIGINAL-HEAP-WEBGPU-ISSUER-UNAVAILABLE");
    }
    {
        TextureDesc placement;
        placement.extent = {4u, 4u, 1u};
        placement.format = TextureFormat::Rgba8Unorm;
        placement.usage = TextureUsage::Sampled;
        const auto unavailable = device->queryTexturePlacementRequirements(placement);
        assert(!unavailable.sizeBytes && !unavailable.alignmentBytes);
        assert(!device->createMemoryHeap(1024u, 256u, MemoryUsage::Transient));
    }

    arp::test::DepthStencilAttachmentInputFixture::verifyContract();
    assert(device);
    arp::test::WebGpuOwnedReadbackContextFixture::verifyUnavailable(*device);
    arp::test::BorrowedTextureReadbackRetirementFixture(*device).verify();
    arp::test::DepthStencilAttachmentInputFixture::verifyPublicGate(*device);
    arp::test::WebGpuPublicTransitionAdmissionFixture(*device).verify();
    assert(device && device->capabilities().commandRetention.contractVersion == 0u);
    {
        arp::rhi::CommandEncoderDesc bounded;
        bounded.retentionBudget = arp::rhi::CommandRetentionBudget{1u, true, 2u, 1u, 2u, 0u, 4096u};
        assert(!device->createCommandEncoder(bounded));
    }
    assert(device);
    assert(device->backend() == Backend::WebGPU);
    auto* bindless =
        static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
    assert(bindless);
    assert(bindless->capabilities().deviceIdentity != 0u);
    assert(bindless->capabilities().execution == BindlessExecution::DiagnosticFailure);
    assert(!bindless->capabilities().executablePayload);
    assert(!bindless->capabilities().originalOrdinaryDepthGraphicsPreparationContractVersion);
    assert(bindless->capabilities().indirectInstanceConsumerContractVersion == 0u);
    assert(bindless->capabilities().faultReadbackContractVersion == 0u);
    assert(bindless->capabilities().indirectPacketStorageContractVersion == 0u);
    assert(bindless->capabilities().zeroBaseIndirectProducerContractVersion == 0u);
    assert(bindless->capabilities().preparedComputeBindingContractVersion == 0u);
    assert(bindless->capabilities().preparedGraphicsBindingContractVersion == 0u);
    assert(bindless->capabilities().preparedGraphicsPassFaultScopeContractVersion == 0u);
    {
        arp::core::pmr::String diagnostic;
        arp::rhi::BindlessComputeBindingPool pool(*bindless, {});
        arp::rhi::BindlessComputeBindingDesc request;
        request.optIn = true;
        assert(!pool.prepare(request, diagnostic)
            && diagnostic == "RHI-BINDLESS-PREPARED-COMPUTE-UNAVAILABLE");
    }
    arp::core::pmr::String producerCompletionDiagnostic;
    assert(!prepareWebGpuProducerCompletionOwner(0u, producerCompletionDiagnostic));
    assert(producerCompletionDiagnostic == "RHI-WEBGPU-PRODUCER-COMPLETION-BUDGET");
    if (!webGpuNativeDeviceAvailable()) {
        assert(!prepareWebGpuProducerCompletionOwner(1u, producerCompletionDiagnostic));
        assert(producerCompletionDiagnostic == "RHI-WEBGPU-PRODUCER-COMPLETION-NATIVE-UNAVAILABLE");
    }
    const auto nativeBindlessLimits = webGpuNativeDeviceLimits();
    assert(bindless->capabilities().indirectFirstInstance
        == webGpuNativeDeviceSupportsIndirectFirstInstance());
    for (const auto& combined : bindless->capabilities().combinedSampledStorageTextureLimits)
        assert(!combined);
    if (webGpuNativeDeviceAvailable()) {
        assert(bindless->capabilities().limitsQueried);
        assert(bindless->capabilities().maxBindGroups == nativeBindlessLimits.maxBindGroups);
        assert(bindless->capabilities().maxBindingsPerGroup
            == nativeBindlessLimits.maxDescriptorBindings);
        assert(bindless->capabilities().maxUniformBufferBindingSize
            == nativeBindlessLimits.maxUniformBufferBindingSize);
        assert(bindless->capabilities().maxStorageBufferBindingSize
            == nativeBindlessLimits.maxStorageBufferBindingSize);
        for (const auto& stage : bindless->capabilities().stageLimits) {
            assert(stage.sampledTextures == nativeBindlessLimits.maxSampledTexturesPerShaderStage);
            assert(stage.samplers == nativeBindlessLimits.maxSamplersPerShaderStage);
            assert(stage.storageTextures == nativeBindlessLimits.maxStorageTexturesPerShaderStage);
            assert(stage.uniformBuffers == nativeBindlessLimits.maxUniformBuffersPerShaderStage);
            assert(stage.storageBuffers == nativeBindlessLimits.maxStorageBuffersPerShaderStage);
        }
        for (std::size_t stage = 0; stage < kBindlessShaderStages; ++stage) {
            for (std::size_t resource = 0; resource < kBindlessResourceClasses; ++resource) {
                const auto& support = bindless->capabilities().support[stage][resource];
                const bool publicReadOnlyStorageBuffer = stage == 2u && resource == 4u;
                const bool publicStorageBuffer = stage == 2u && resource == 5u;
                const bool publicSampledTexture = stage == 2u && resource == 0u;
                const bool publicGraphicsSampledTexture = stage == 1u && resource == 0u;
                const bool publicGraphicsSampler = stage == 1u && resource == 1u;
                const bool publicGraphicsReadOnlyStorageBuffer = stage == 1u && resource == 4u;
                const bool publicGraphicsUniformBuffer = stage == 1u && resource == 3u;
                const bool publicGraphicsStorageBuffer = stage == 1u && resource == 5u;
                const bool publicSampler = stage == 2u && resource == 1u;
                const bool publicStorageTexture = stage == 2u && resource == 2u;
                const bool publicUniformBuffer = stage == 2u && resource == 3u;
                const auto readOnlyOperations = BindlessOperation::Load | BindlessOperation::Query;
                const auto sampledOperations = BindlessOperation::Load | BindlessOperation::Query
                    | BindlessOperation::SampleLevel;
                const auto graphicsSampledOperations =
                    sampledOperations | BindlessOperation::SampleGrad;
                const auto uniformOperations = BindlessOperation::Load;
                const auto storageTextureOperations =
                    BindlessOperation::Load | BindlessOperation::Query | BindlessOperation::Store;
                const auto publicOperations = BindlessOperation::Load | BindlessOperation::Query
                    | BindlessOperation::Store | BindlessOperation::Atomic;
                const auto expectedOperations = publicGraphicsSampledTexture
                    ? graphicsSampledOperations
                    : publicGraphicsReadOnlyStorageBuffer ? readOnlyOperations
                    : publicGraphicsUniformBuffer         ? uniformOperations
                    : publicGraphicsStorageBuffer         ? publicOperations
                    : publicStorageTexture                ? storageTextureOperations
                    : publicSampledTexture                ? sampledOperations
                    : publicUniformBuffer                 ? uniformOperations
                    : publicReadOnlyStorageBuffer         ? readOnlyOperations
                    : publicStorageBuffer                 ? publicOperations
                                                          : BindlessOperation::None;
                assert(support.operations == expectedOperations);
                assert(support.nonUniform
                    == (publicStorageTexture || publicSampledTexture || publicGraphicsSampledTexture
                        || publicGraphicsReadOnlyStorageBuffer || publicGraphicsUniformBuffer
                        || publicGraphicsStorageBuffer || publicSampler || publicGraphicsSampler
                        || publicUniformBuffer || publicReadOnlyStorageBuffer
                        || publicStorageBuffer));
            }
        }
    } else {
        assert(!bindless->capabilities().limitsQueried);
        assert(bindless->capabilities().maxBindGroups == 0u);
        assert(bindless->capabilities().maxBindingsPerGroup == 0u);
        for (const auto& stage : bindless->capabilities().support) {
            for (const auto& resource : stage) {
                assert(resource.operations == BindlessOperation::None);
                assert(!resource.nonUniform);
            }
        }
    }
    BindlessRequirements bindlessRequest;
    bindlessRequest.deviceIdentity = bindless->capabilities().deviceIdentity;
    BindlessShaderArtifact bindlessArtifact;
    assert(std::string_view(bindless->plan(bindlessRequest, bindlessArtifact).diagnosticId)
        == "RHI-BINDLESS-OPT-IN-REQUIRED");
    bindlessRequest.optIn = true;
    assert(!bindless->plan(bindlessRequest, bindlessArtifact).success);
    arp::rhi::semantics::ShutdownDrainRequest shutdownDrain;
    shutdownDrain.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    const auto shutdownDrainReport = device->drainForShutdown(shutdownDrain);
    assert(shutdownDrainReport.backend == Backend::WebGPU);
    assert(!shutdownDrainReport.completed());
    assert(shutdownDrainReport.outcome
        == arp::rhi::semantics::ShutdownDrainOutcome::DiagnosticFailure);
    assert(
        shutdownDrainReport.diagnosticId == "ARP-RHI-WEBGPU-SHUTDOWN-DRAIN-DEADLINE-UNSUPPORTED");
    assertWebGpuRecordedStateMetadata(*device);

    // The concrete Rgba16Float storage capability is independently enforced
    // at resource creation, even when generic storage-texture binding is
    // available. This is the same fail-closed rule consumed by DDGI.
    auto missingRgbaProfile = makeValidatedWebGpuProfile();
    missingRgbaProfile.optionalFeatures.rgba16FloatStorage = false;
    auto missingRgbaDevice =
        arp::tests::createWebGpuContractDevice(makeWebGpuDeviceDesc(missingRgbaProfile));
    assert(missingRgbaDevice);
    auto unsupportedRgbaStorageTexture = missingRgbaDevice->createTexture({
        "WebGpuUnsupportedRgba16StorageTexture",
        {64, 64, 1},
        TextureFormat::Rgba16Float,
        TextureUsage::Storage | TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    assert(!unsupportedRgbaStorageTexture);

    auto unsupportedBcProfile = makeValidatedWebGpuProfile();
    unsupportedBcProfile.optionalFeatures.textureCompressionBc = false;
    auto unsupportedBcDevice =
        arp::tests::createWebGpuContractDevice(makeWebGpuDeviceDesc(unsupportedBcProfile));
    assert(unsupportedBcDevice);
    auto rejectedBcTexture = unsupportedBcDevice->createTexture({
        "WebGpuRejectedBc7Texture",
        {64, 64, 1},
        TextureFormat::Bc7Unorm,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead,
    });
    assert(!rejectedBcTexture);

    auto rejectedBcStorageTexture = device->createTexture({
        "WebGpuRejectedBcStorageTexture",
        {64, 64, 1},
        TextureFormat::Bc7Unorm,
        TextureUsage::Storage | TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    assert(!rejectedBcStorageTexture);

    // Capability absence must be enforced at the WebGPU RHI resource entry,
    // not deferred to an asynchronous native validation error. Build a
    // platform-validated profile whose adapter probe found no storage support.
    auto noStorageProbe = WebGpuRuntimeProbe{};
    noStorageProbe.browserRuntime = true;
    noStorageProbe.canvasSurface = true;
    noStorageProbe.nativeDevice = true;
    noStorageProbe.maxTexture2DSize = 8192;
    noStorageProbe.maxColorAttachments = 4;
    noStorageProbe.maxDescriptorBindings = 64;
    noStorageProbe.maxSampledTexturesPerShaderStage = 16;
    noStorageProbe.uniformBufferOffsetAlignment = 256;
    noStorageProbe.storageBufferOffsetAlignment = 256;
    noStorageProbe.maxUniformBufferBindingSize = 65536;
    noStorageProbe.maxStorageBufferBindingSize = 134217728;
    auto noStorageProfile = buildWebGpuRuntimeProfile(noStorageProbe);
    auto noStorageCapabilityDesc = makeWebGpuDeviceDesc(noStorageProfile);
    noStorageCapabilityDesc.requireComputeStorageRuntimePromotion = false;
    auto noStorageCapabilityDevice =
        arp::tests::createWebGpuContractDevice(noStorageCapabilityDesc);
    assert(noStorageCapabilityDevice);
    auto rejectedStorageBuffer = noStorageCapabilityDevice->createBuffer({
        "WebGpuRejectedStorageBuffer",
        4096,
        ResourceUsage::StorageBuffer | ResourceUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::CopyDst,
    });
    auto rejectedStorageTexture = noStorageCapabilityDevice->createTexture({
        "WebGpuRejectedStorageTexture",
        {64, 64, 1},
        TextureFormat::Rgba16Float,
        TextureUsage::Storage | TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    assert(!rejectedStorageBuffer);
    assert(!rejectedStorageTexture);

    const auto caps = device->capabilities();
    assert(caps.backend == Backend::WebGPU);
    const bool nativeAvailable = webGpuNativeDeviceAvailable() && !webGpuNativeDeviceLost();
    // This fixture's writable profile supplies CPU contract inputs. Only the
    // actual runtime can supply executable device capabilities; keep running
    // the remaining diagnostic-only resource/command checks on a host stub.
    assert(caps.core.graphics == nativeAvailable);
    assert(caps.core.compute == nativeAvailable);
    assert(!caps.explicitFeatures.pushConstants);
    assert(!caps.explicitFeatures.secondaryCommandBuffers);
    assert(caps.features.secondaryCommandBuffers == nativeAvailable);
    assert(!caps.features.descriptorTable);
    assert(caps.features.indirectDraw == nativeAvailable);
    assert(caps.features.textureCompressionBc == nativeAvailable);
    assert(!device->createRenderBundleEncoder({"WebGpuUnsupportedRenderBundle"}));
    assert(caps.explicitFeatures.explicitBarriers == nativeAvailable);
    assert(caps.memory.deviceLocal == nativeAvailable);
    assert(caps.memory.hostVisible == nativeAvailable);
    assert(caps.memory.backendFallbacksHiddenFromUpperLayer);
    if (!nativeAvailable) {
        assert(!caps.features.pushConstants && !caps.features.computeDispatch
            && !caps.features.storageBufferBinding && !caps.features.storageTextureBinding
            && !caps.features.nativeSync && !caps.features.timelineSync);
        assert(!caps.pushConstantLoweringHasExecutablePayload
            && caps.pushConstantLoweringOutcome == PushConstantLoweringOutcome::DiagnosticFailure);
        assert(caps.pushConstantLoweringDiagnosticId == "RHI-DEVICE-NATIVE-RUNTIME-UNAVAILABLE");
        assert(caps.swapchain.requestedImageCount == 0u && caps.swapchain.maxFramesInFlight == 0u);
        for (const auto& status : caps.executionStatuses)
            assert(!status.executable);
        std::puts("WEBGPU_HOST_CAPABILITY_REVOCATION_OK native-unavailable profile-cannot-grant");
    }

    std::uint64_t timingScopeId = 1u;
    QueryPoolDesc validTimingPoolDesc;
    validTimingPoolDesc.label = "WebGpuTimestampContractPool";
    validTimingPoolDesc.type = QueryType::Timestamp;
    validTimingPoolDesc.count = 2u;
    validTimingPoolDesc.timingScopeIds = &timingScopeId;
    validTimingPoolDesc.timingScopeIdCount = 1u;
    auto timingPool = device->createQueryPool(validTimingPoolDesc);
    const auto timingSupported = webGpuNativeTimestampQuerySupported();
    assert(static_cast<bool>(timingPool) == timingSupported);

    QueryPoolDesc invalidTimingPoolDesc = validTimingPoolDesc;
    invalidTimingPoolDesc.count = 4u;
    assert(!device->createQueryPool(invalidTimingPoolDesc));

    auto invalidResizeSwapchain = device->createSwapchain({
        "#WebGpuCanvasInvalidResizeContract",
        {320u, 180u},
        TextureFormat::Bgra8Unorm,
        2u,
    });
    assert(invalidResizeSwapchain);
    const auto invalidSwapchainResize = invalidResizeSwapchain->resize({0u, 180u});
    assert(!invalidSwapchainResize.success);
    assert(invalidSwapchainResize.diagnostics.find("extent") != std::pmr::string::npos);
    assert(invalidSwapchainResize.extent.width == 320u);
    assert(invalidSwapchainResize.extent.height == 180u);
    const auto validSwapchainResize = invalidResizeSwapchain->resize({640u, 360u});
    assert(validSwapchainResize.success);
    assert(validSwapchainResize.extent.width == 640u);
    assert(validSwapchainResize.extent.height == 360u);

    auto upload = device->createBuffer({
        "WebGpuUploadBuffer",
        16384,
        ResourceUsage::CopySrc,
        MemoryUsage::CpuToGpu,
        ResourceState::CopySrc,
    });
    auto uniform = device->createBuffer({
        "WebGpuUniformBuffer",
        256,
        ResourceUsage::UniformBuffer | ResourceUsage::CopyDst,
        MemoryUsage::CpuToGpu,
        ResourceState::CopyDst,
    });
    auto storage = device->createBuffer({
        "WebGpuStorageBuffer",
        4096,
        ResourceUsage::StorageBuffer | ResourceUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::CopyDst,
    });
    auto readback = device->createBuffer({
        "WebGpuReadbackBuffer",
        16384,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst,
    });
    auto color = device->createTexture({
        "WebGpuCanvasColor",
        {64, 64, 1},
        TextureFormat::Bgra8Unorm,
        TextureUsage::RenderAttachment | TextureUsage::CopyDst | TextureUsage::CopySrc,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    auto msaaColor = device->createTexture({
        "WebGpuMsaaColor",
        {64, 64, 1},
        TextureFormat::Bgra8Unorm,
        TextureUsage::RenderAttachment,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
        1,
        1,
        4,
        false,
    });
    auto storageTexture = device->createTexture({
        "WebGpuStorageTexture",
        {64, 64, 1},
        TextureFormat::Rgba16Float,
        TextureUsage::Storage | TextureUsage::Sampled | TextureUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    // Rg16Float storage is a separate per-format capability. The validated
    // profile above intentionally does not promote it, so resource creation
    // must fail at the RHI boundary before any native WebGPU validation error.
    auto unsupportedRg16StorageTexture = device->createTexture({
        "WebGpuUnsupportedRg16StorageTexture",
        {64, 64, 1},
        TextureFormat::Rg16Float,
        TextureUsage::Storage | TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    assert(upload && uniform && storage && readback && color && msaaColor && storageTexture);
    {
        auto sealed = device->createBuffer({"HostSealContract",
            4u,
            ResourceUsage::CopyDst,
            MemoryUsage::CpuToGpu,
            ResourceState::CopyDst});
        assert(sealed);
        sealed->sealHostWrites();
        sealed->sealHostWrites();
        assert(sealed->hostWritesSealed() && !sealed->gpuWritesSealed()
            && !sealed->gpuTransferWritesSealed());
        const std::uint32_t word = 1u;
        assert(device->writeBuffer(*sealed, &word, sizeof(word)).diagnostics
            == "RHI-BUFFER-HOST-WRITES-SEALED");
        sealed->sealWrites();
        sealed->sealHostWrites();
        assert(sealed->gpuWritesSealed() && sealed->gpuTransferWritesSealed());
    }
    assert(!unsupportedRg16StorageTexture);
    assert(!upload->allocation().nativeHandleExposed);
    assert(!storageTexture->allocation().nativeHandleExposed);
    const auto memoryBeforeBudgetReject = device->memoryStats();
    assert(memoryBeforeBudgetReject.allocatedBytes > 0);
    assert(memoryBeforeBudgetReject.budgetCapacityBytes == desc.memoryBudgetBytes);
    assert(memoryBeforeBudgetReject.peakAllocatedBytes >= memoryBeforeBudgetReject.allocatedBytes);
    assert(memoryBeforeBudgetReject.backendFallbacksHiddenFromUpperLayer);

    // Existing resources already retain part of the fixed ledger, so this is
    // a deterministic allocator rejection rather than a browser-runtime or
    // native allocation failure.
    const auto overBudget = device->createBuffer({
        "WebGpuOverBudgetAllocation",
        memoryBeforeBudgetReject.budgetCapacityBytes,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    assert(!overBudget);

    const auto memoryAfterBudgetReject = device->memoryStats();
    assert(memoryAfterBudgetReject.budgetCapacityBytes
        == memoryBeforeBudgetReject.budgetCapacityBytes);
    assert(memoryAfterBudgetReject.allocatedBytes == memoryBeforeBudgetReject.allocatedBytes);
    assert(
        memoryAfterBudgetReject.peakAllocatedBytes == memoryBeforeBudgetReject.peakAllocatedBytes);
    assert(memoryAfterBudgetReject.budgetRejectionCount
        == memoryBeforeBudgetReject.budgetRejectionCount + 1u);

    const auto physicalBudget = device->physicalBudgetLedger();
    assert(physicalBudget.valid());
    const auto& allocationBudget =
        physicalBudget.category(semantics::PhysicalBudgetCategory::ResourceAllocation);
    assert(allocationBudget.observed());
    assert(allocationBudget.unit == semantics::PhysicalBudgetUnit::Bytes);
    assert(allocationBudget.capacity == desc.memoryBudgetBytes);
    assert(allocationBudget.current == memoryAfterBudgetReject.allocatedBytes);
    assert(allocationBudget.peak == memoryAfterBudgetReject.peakAllocatedBytes);
    assert(allocationBudget.rejectionCount == memoryAfterBudgetReject.budgetRejectionCount);

    const auto& stagingBudget =
        physicalBudget.category(semantics::PhysicalBudgetCategory::UploadStaging);
    assert(stagingBudget.observed());
    assert(stagingBudget.capacity == desc.uploadStagingByteCapacity);

    const auto writeWithoutCopyDst = device->writeBuffer(*upload, kPushData, 4u, 0u);
    assert(!writeWithoutCopyDst.success);
    assert(writeWithoutCopyDst.diagnostics.find("CopyDst") != std::pmr::string::npos);
    const auto unalignedWrite = device->writeBuffer(*uniform, kPushData, 3u, 0u);
    assert(!unalignedWrite.success);
    assert(unalignedWrite.diagnostics.find("4-byte aligned") != std::pmr::string::npos);

    auto storageView = device->createBufferView(*storage,
        {
            "WebGpuStorageBufferView",
            0,
            4096,
            16,
            ResourceUsage::StorageBuffer,
        });
    auto storageTextureView = device->createTextureView(*storageTexture,
        {
            "WebGpuStorageTextureView",
            TextureFormat::Rgba16Float,
            TextureUsage::Storage,
            TextureViewDimension::Texture2D,
            0,
            1,
            0,
            1,
            TextureAspect::Color,
        });
    auto sampledTextureView = device->createTextureView(*storageTexture,
        {
            "WebGpuSampledTextureView",
            TextureFormat::Rgba16Float,
            TextureUsage::Sampled,
            TextureViewDimension::Texture2D,
            0,
            1,
            0,
            1,
            TextureAspect::Color,
        });
    auto outOfRangeTextureView = device->createTextureView(*storageTexture,
        {
            "WebGpuOutOfRangeTextureView",
            TextureFormat::Rgba16Float,
            TextureUsage::Sampled,
            TextureViewDimension::Texture2D,
            1,
            1,
            0,
            1,
            TextureAspect::Color,
        });
    auto expandedUsageTextureView = device->createTextureView(*storageTexture,
        {
            "WebGpuExpandedUsageTextureView",
            TextureFormat::Rgba16Float,
            TextureUsage::RenderAttachment,
            TextureViewDimension::Texture2D,
            0,
            1,
            0,
            1,
            TextureAspect::Color,
        });
    auto cubeTexture = device->createTexture({
        "WebGpuSampledCube",
        {1, 1, 1},
        TextureFormat::Rgba16Float,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead,
        1,
        6,
        1,
        true,
    });
    auto cubeTextureView = device->createTextureView(*cubeTexture,
        {
            "WebGpuSampledCubeView",
            TextureFormat::Rgba16Float,
            TextureUsage::Sampled,
            TextureViewDimension::TextureCube,
            0,
            1,
            0,
            6,
            TextureAspect::Color,
        });
    auto sampler = device->createSampler({
        "WebGpuLinearSampler",
        SamplerFilter::Linear,
        SamplerFilter::Linear,
        SamplerAddressMode::ClampToEdge,
        SamplerAddressMode::ClampToEdge,
        SamplerAddressMode::ClampToEdge,
    });
    SamplerDesc comparisonSamplerDesc;
    comparisonSamplerDesc.label = "WebGpuComparisonSampler";
    comparisonSamplerDesc.compareOp = CompareOp::LessEqual;
    auto comparisonSampler = device->createSampler(comparisonSamplerDesc);
    assert(storageView);
    assert(storageTextureView);
    assert(sampledTextureView);
    assert(!outOfRangeTextureView);
    assert(!expandedUsageTextureView);
    assert(cubeTexture);
    assert(cubeTextureView);
    assert(sampler);
    assert(comparisonSampler);

    DescriptorLayoutDesc descriptorLayout;
    descriptorLayout.name = "WebGpuNativeDescriptorGroup";
    descriptorLayout.bindings.push_back(
        {0, DescriptorType::UniformBuffer, ShaderStage::Vertex | ShaderStage::Fragment});
    descriptorLayout.bindings.push_back({1, DescriptorType::StorageBuffer, ShaderStage::Compute});
    descriptorLayout.bindings.push_back({2, DescriptorType::SampledTexture, ShaderStage::Fragment});
    descriptorLayout.bindings.push_back({3, DescriptorType::StorageTexture, ShaderStage::Compute});
    descriptorLayout.bindings.push_back({4, DescriptorType::Sampler, ShaderStage::Fragment});
    descriptorLayout.bindings.push_back(
        {5, DescriptorType::ReadOnlyStorageBuffer, ShaderStage::Compute});
    auto descriptorArena = device->createDescriptorArena({
        "WebGpuNativeDescriptorArena",
        2,
        8,
    });
    assert(descriptorArena);
    auto descriptorSet = descriptorArena->allocateDescriptorSet({
        "WebGpuNativeDescriptorSet",
        descriptorLayout,
    });
    assert(descriptorSet);
    assert(descriptorSet
            ->write({
                0,
                DescriptorType::UniformBuffer,
                uniform->id(),
                DescriptorResourceKind::Buffer,
            })
            .success);
    assert(descriptorSet
            ->write({
                1,
                DescriptorType::StorageBuffer,
                storage->id(),
                DescriptorResourceKind::Buffer,
            })
            .success);
    const auto readOnlyStorageWrite = descriptorSet->write({
        5,
        DescriptorType::ReadOnlyStorageBuffer,
        storage->id(),
        DescriptorResourceKind::Buffer,
    });
    if (!readOnlyStorageWrite.success) {
        std::printf("[arp-rhi-contract] readonly-storage-write=failed reason=%s\n",
            readOnlyStorageWrite.diagnostics.c_str());
        return 1;
    }
    std::printf("[arp-rhi-contract] readonly-storage-write=passed\n");
    assert(descriptorSet
            ->write({
                2,
                DescriptorType::SampledTexture,
                sampledTextureView->id(),
                DescriptorResourceKind::TextureView,
            })
            .success);
    assert(descriptorSet
            ->write({
                3,
                DescriptorType::StorageTexture,
                storageTextureView->id(),
                DescriptorResourceKind::TextureView,
            })
            .success);
    assert(descriptorSet
            ->write({
                4,
                DescriptorType::Sampler,
                sampler->id(),
                DescriptorResourceKind::Sampler,
            })
            .success);
    const auto comparisonToRegularBinding = descriptorSet->write({
        4,
        DescriptorType::Sampler,
        comparisonSampler->id(),
        DescriptorResourceKind::Sampler,
    });
    assert(!comparisonToRegularBinding.success);
    assert(
        comparisonToRegularBinding.diagnostics.find("comparison state") != std::pmr::string::npos);
    assert(descriptorSet->stats().writtenBindings == 6u);

    DescriptorLayoutDesc comparisonDescriptorLayout;
    comparisonDescriptorLayout.name = "WebGpuComparisonDescriptorGroup";
    comparisonDescriptorLayout.bindings.push_back(
        {0, DescriptorType::Sampler, ShaderStage::Fragment});
    comparisonDescriptorLayout.bindings.back().comparisonSampler = true;
    auto comparisonDescriptorArena = device->createDescriptorArena({
        "WebGpuComparisonDescriptorArena",
        1,
        1,
    });
    assert(comparisonDescriptorArena);
    auto comparisonDescriptorSet = comparisonDescriptorArena->allocateDescriptorSet({
        "WebGpuComparisonDescriptorSet",
        comparisonDescriptorLayout,
    });
    assert(comparisonDescriptorSet);
    assert(comparisonDescriptorSet
            ->write({
                0,
                DescriptorType::Sampler,
                comparisonSampler->id(),
                DescriptorResourceKind::Sampler,
            })
            .success);
    const auto regularToComparisonBinding = comparisonDescriptorSet->write({
        0,
        DescriptorType::Sampler,
        sampler->id(),
        DescriptorResourceKind::Sampler,
    });
    assert(!regularToComparisonBinding.success);
    assert(
        regularToComparisonBinding.diagnostics.find("comparison state") != std::pmr::string::npos);
    DescriptorLayoutDesc cubeDescriptorLayout;
    cubeDescriptorLayout.name = "WebGpuCubeDescriptorGroup";
    cubeDescriptorLayout.bindings.push_back(
        {0, DescriptorType::SampledTexture, ShaderStage::Fragment});
    cubeDescriptorLayout.bindings.back().textureViewDimensionConstrained = true;
    cubeDescriptorLayout.bindings.back().textureViewDimension = TextureViewDimension::TextureCube;
    auto cubeDescriptorSet = descriptorArena->allocateDescriptorSet({
        "WebGpuCubeDescriptorSet",
        cubeDescriptorLayout,
    });
    assert(cubeDescriptorSet);
    const auto webGpu2DAsCube = cubeDescriptorSet->write({
        0,
        DescriptorType::SampledTexture,
        sampledTextureView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(!webGpu2DAsCube.success);
    assert(webGpu2DAsCube.diagnostics.find("texture view dimension") != std::pmr::string::npos);
    assert(cubeDescriptorSet
            ->write({
                0,
                DescriptorType::SampledTexture,
                cubeTextureView->id(),
                DescriptorResourceKind::TextureView,
            })
            .success);

    PipelineLayoutDesc layoutDesc;
    layoutDesc.pushConstantRanges.push_back(
        {ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute, 0, 64});
    auto layout = device->createPipelineLayout(layoutDesc);
    assert(layout);
    auto shaderPackage = makeWebGpuShaderPackage();
    assert(validateShaderPackage(shaderPackage).success);

    RenderPipelineDesc renderPipelineDesc;
    renderPipelineDesc.label = "WebGpuRhiRenderPipeline";
    renderPipelineDesc.layout = layout.get();
    renderPipelineDesc.shaderPackage = &shaderPackage;
    renderPipelineDesc.vertexShader = {"webgpu_rhi.slang", "vsMain", ShaderStage::Vertex};
    renderPipelineDesc.fragmentShader = {"webgpu_rhi.slang", "fsMain", ShaderStage::Fragment};
    renderPipelineDesc.colorFormat = TextureFormat::Bgra8Unorm;
    renderPipelineDesc.multisample.sampleCount = 4;
    renderPipelineDesc.colorTargets.push_back({
        TextureFormat::Bgra8Unorm,
        true,
        {BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, BlendOp::Add},
        {BlendFactor::One, BlendFactor::OneMinusSrcAlpha, BlendOp::Add},
        ColorWriteMask::All,
        true,
    });
    const auto renderPipelineReport = device->validateRenderPipeline(renderPipelineDesc);
    if (!renderPipelineReport.success)
        std::fprintf(stderr,
            "WebGPU render pipeline contract failed: %s\n",
            renderPipelineReport.diagnostics.c_str());
    assert(renderPipelineReport.success);
    assert(renderPipelineReport.backend == Backend::WebGPU);
    assert(renderPipelineReport.pipelineCacheKeyStable);
    auto unsupportedPatchPipeline = renderPipelineDesc;
    unsupportedPatchPipeline.topology = PrimitiveTopology::PatchList;
    const auto unsupportedPatchReport = device->validateRenderPipeline(unsupportedPatchPipeline);
    assert(!unsupportedPatchReport.success);
    assert(unsupportedPatchReport.diagnostics.find("PatchList") != std::pmr::string::npos);
    auto renderPipeline = device->createRenderPipeline(renderPipelineDesc);
    assert(renderPipeline);
    assert(!renderPipeline->nativeHandleExposedToUpperLayer());

    ComputePipelineDesc computePipelineDesc;
    computePipelineDesc.label = "WebGpuRhiComputePipeline";
    computePipelineDesc.layout = layout.get();
    computePipelineDesc.shaderPackage = &shaderPackage;
    computePipelineDesc.computeShader = {"webgpu_rhi.slang", "csMain", ShaderStage::Compute};
    const auto computePipelineReport = device->validateComputePipeline(computePipelineDesc);
    assert(computePipelineReport.success);
    assert(computePipelineReport.backend == Backend::WebGPU);
    auto computePipeline = device->createComputePipeline(computePipelineDesc);
    assert(computePipeline);

    auto encoder = device->createCommandEncoder({"WebGpuRhiFrame"});
    assert(encoder
        && encoder->prepareBindlessGraphicsPass({}, BindlessAccessArray(nullptr, 0u)).diagnostics
            == "RHI-BINDLESS-GRAPHICS-PASS-SCOPE-UNAVAILABLE");
    ResourceBarrierDesc barrier;
    barrier.resource = msaaColor->id();
    barrier.textureSubresources = {0, 1, 0, 1, TextureAspect::Color};
    barrier.before = ResourceState::Undefined;
    barrier.after = ResourceState::ColorAttachment;
    barrier.sourceStages = PipelineStage::TopOfPipe;
    barrier.destinationStages = PipelineStage::ColorAttachmentOutput;
    barrier.destinationAccess = ResourceAccess::ColorAttachmentWrite;
    const auto barrierResult = encoder->transition(*msaaColor, barrier);
    if (report.publicDeviceNativeDeviceAvailable) {
        assert(barrierResult.success);
        assert(barrierResult.stageAccessAware);
    } else {
        assert(!barrierResult.success);
        assert(barrierResult.diagnostics == "RHI-WEBGPU-BARRIER-NATIVE-PAYLOAD-MISSING");
    }

    RenderPassDesc passDesc;
    passDesc.label = "WebGpuCanvasPass";
    passDesc.colorTargetName = msaaColor->desc().label;
    passDesc.colorAttachments.push_back({
        "WebGpuCanvasColor",
        msaaColor->id(),
        msaaColor->desc().format,
        LoadOp::Clear,
        StoreOp::Store,
        true,
        color->id(),
    });
    if (report.publicDeviceNativeDeviceAvailable) {
        auto pass = encoder->beginRenderPass(passDesc);
        assert(pass);
        const auto framePipelineBind = pass->setPipeline(*renderPipeline);
        assert(framePipelineBind.success);
        const auto renderPush = pass->pushConstants(ShaderStage::Vertex | ShaderStage::Fragment,
            0,
            64,
            static_cast<const void*>(kPushData));
        assert(renderPush.success);
        const auto frameDraw = pass->drawIndexed(3, 1, 0, 0, 0);
        assert(frameDraw.success);
        const auto framePassEnd = pass->end();
        assert(framePassEnd.success);

        // P0-B1 consume state machine: push -> consume -> push succeeds, but
        // push -> push (without intervening draw/dispatch) fails with the
        // stable [PC-E013] diagnostic.  Verified on WebGPU the same way as the
        // Null backend (rhi_command_contract_tests.cpp).  This block runs only
        // under Emscripten where the WebGPU backend has a live native payload.
        auto consumeEncoder = device->createCommandEncoder({"WebGpuConsumeCheck"});
        RenderPassDesc consumePassDesc;
        consumePassDesc.label = "WebGpuConsumeCheckPass";
        consumePassDesc.colorTargetName = color->desc().label;
        consumePassDesc.colorAttachments.push_back({
            "WebGpuConsumeColor",
            color->id(),
            color->desc().format,
            LoadOp::DontCare,
            StoreOp::Store,
            false,
        });
        auto consumePass = consumeEncoder->beginRenderPass(consumePassDesc);
        assert(consumePass);
        const auto consumePipelineBind = consumePass->setPipeline(*renderPipeline);
        assert(consumePipelineBind.success);
        // push -> consume -> push must succeed.
        const auto consumePush =
            consumePass->pushConstants(ShaderStage::Vertex | ShaderStage::Fragment,
                0,
                64,
                static_cast<const void*>(kPushData));
        assert(consumePush.success);
        const auto consumeDraw = consumePass->drawIndexed(3, 1, 0, 0, 0);
        assert(consumeDraw.success);
        const auto consumePushAfterDraw =
            consumePass->pushConstants(ShaderStage::Vertex | ShaderStage::Fragment,
                0,
                64,
                static_cast<const void*>(kPushData));
        assert(consumePushAfterDraw.success);
        // push -> push (no intervening consume) must fail with [PC-E013].
        const auto webgpuDuplicatePush =
            consumePass->pushConstants(ShaderStage::Vertex | ShaderStage::Fragment,
                0,
                64,
                static_cast<const void*>(kPushData));
        assert(!webgpuDuplicatePush.success);
        assert(webgpuDuplicatePush.diagnostics.find("[PC-E013]") != std::pmr::string::npos);
        const auto consumePassEnd = consumePass->end();
        assert(consumePassEnd.success);
        const auto consumeCommandBuffer = consumeEncoder->finish();
        assert(consumeCommandBuffer);

        auto compute = encoder->beginComputePass({"WebGpuComputeStoragePass"});
        assert(compute);
        const auto computePipelineBind = compute->setPipeline(*computePipeline);
        assert(computePipelineBind.success);
        const auto computePush = compute->pushConstants(ShaderStage::Compute, 0, 64, kPushData);
        assert(computePush.success);
        const auto computeDispatch = compute->dispatch(2, 2, 1);
        assert(computeDispatch.success);
        const auto computePassEnd = compute->end();
        assert(computePassEnd.success);

        auto transfer = encoder->beginTransferPass({"WebGpuUploadReadback"});
        assert(transfer);
        const auto legacyBufferCopy = transfer->copyBufferToBuffer(4u);
        assert(!legacyBufferCopy.success);
        assert(legacyBufferCopy.diagnostics.find("explicit source and destination")
            != std::pmr::string::npos);
        const auto uploadBufferResult = transfer->uploadBuffer({
            "WebGpuUploadBufferCommand",
            upload->id(),
            storage->id(),
            4u,
            StagingIntent::Upload,
            true,
            1u,
        });
        assert(uploadBufferResult.success);
        const auto unalignedCopy = transfer->copyBufferToTexture({
            upload->id(),
            color->id(),
            0,
            0,
            0,
            {0, 1, 0, 1, TextureAspect::Color},
            {64, 64, 1},
        });
        assert(!unalignedCopy.success);
        const auto bufferToTextureCopy = transfer->copyBufferToTexture({
            upload->id(),
            color->id(),
            0,
            256,
            64,
            {0, 1, 0, 1, TextureAspect::Color},
            {64, 64, 1},
        });
        assert(bufferToTextureCopy.success);
        const auto textureToBufferCopy = transfer->copyTextureToBuffer({
            color->id(),
            readback->id(),
            {0, 1, 0, 1, TextureAspect::Color},
            0,
            256,
            64,
            {64, 64, 1},
        });
        assert(textureToBufferCopy.success);
        const auto transferPassEnd = transfer->end();
        assert(transferPassEnd.success);

        auto commands = encoder->finish();
        assert(commands);
        const auto submitted = device->queue().submit(*commands);
        assert(!submitted.success && !submitted.hasAcceptedWork());
        assert(submitted.diagnostics == webGpuGenericCompletionResourceGraphDiagnostic);
        assert(submitted.submittedCommandCount == commands->commandCount());

        const auto commandStats = inspectWebGpuRhiNativeCommandStats(*device);
        assert(!commandStats.nativeHandleExposedToUpperLayer);
        assert(commandStats.nativeCommandEncoderCreateCount >= 1u);
        assert(commandStats.nativeCommandBufferCreateCount >= 1u);
        assert(commandStats.nativeQueueSubmitCount == 0u);
        assert(commandStats.nativeTextureBarrierRecordCount >= 1u);
        assert(commandStats.nativeBufferToTextureCopyRecordCount == 1u);
        assert(commandStats.nativeTextureToBufferCopyRecordCount == 1u);
        assert(commandStats.nativeRenderPassBeginRecordCount == 1u);
        assert(commandStats.nativeIndexedDrawRecordCount == 1u);
        assert(commandStats.nativeComputePassBeginRecordCount == 1u);
        assert(commandStats.nativeDescriptorBindRecordCount >= 1u);
        assert(commandStats.nativePushConstantUploadRingWriteCount >= 2u);
        assert(commandStats.nativeDispatchRecordCount == 1u);
    } else {
        auto noNativeRenderPass = encoder->beginRenderPass(passDesc);
        assert(!noNativeRenderPass);
        auto noNativeComputePass = encoder->beginComputePass({"WebGpuNoNativeComputePass"});
        assert(!noNativeComputePass);
        auto noNativeTransferPass = encoder->beginTransferPass({"WebGpuNoNativeTransferPass"});
        assert(!noNativeTransferPass);
        assert(!encoder->finish());

        const auto commandStats = inspectWebGpuRhiNativeCommandStats(*device);
        assert(!commandStats.nativeHandleExposedToUpperLayer);
        assert(commandStats.nativeCommandEncoderCreateCount == 0u);
        assert(commandStats.nativeCommandBufferCreateCount == 0u);
        assert(commandStats.nativeQueueSubmitCount == 0u);
    }

    auto swapchain = device->createSwapchain({
        "#WebGpuCanvasInvalidResizeContract",
        {320, 180},
        TextureFormat::Bgra8Unorm,
        2,
    });
    assert(swapchain);
    if (!swapchain) {
        return 1;
    }
    assert(swapchain->maxFramesInFlight() == 2u);
    auto frame = swapchain->acquireNextFrame();
    assert(frame.success);
    if (!frame.success) {
        return 1;
    }
    const auto presented = swapchain->present(frame);
    assert(presented.success);
    const auto duplicatePresent = swapchain->present(frame);
    assert(!duplicatePresent.success);
    auto firstInFlight = swapchain->acquireNextFrame();
    assert(firstInFlight.success);
    auto secondInFlight = swapchain->acquireNextFrame();
    assert(secondInFlight.success);
    assert(firstInFlight.frameIndex != secondInFlight.frameIndex);
    const auto saturatedFrame = swapchain->acquireNextFrame();
    assert(!saturatedFrame.success);
    assert(saturatedFrame.diagnostics.find("no free") != std::pmr::string::npos);
    const auto firstPresented = swapchain->present(firstInFlight);
    assert(firstPresented.success);
    auto recycledFrame = swapchain->acquireNextFrame();
    assert(recycledFrame.success);
    assert(recycledFrame.frameIndex == firstInFlight.frameIndex);
    const auto secondPresented = swapchain->present(secondInFlight);
    assert(secondPresented.success);
    const auto recycledPresented = swapchain->present(recycledFrame);
    assert(recycledPresented.success);
    const auto resized = swapchain->resize({640, 360});
    assert(resized.success);
    assert(resized.swapchainRebuildRequired);
    const auto surfaceLost = swapchain->setSurfaceAvailable(false);
    assert(surfaceLost.success);
    assert(surfaceLost.releaseSurfaceResources);
    assert(!surfaceLost.acquireAllowed);
    const auto hiddenFrame = swapchain->acquireNextFrame();
    assert(!hiddenFrame.success);

    const auto swapchainStats = inspectWebGpuRhiNativeSwapchainStats(*device);
    assert(!swapchainStats.nativeHandleExposedToUpperLayer);
    if (report.publicDeviceNativeDeviceAvailable) {
        assert(swapchainStats.nativeSurfaceCreateCount >= 1u);
        assert(swapchainStats.nativeCanvasAcquireCount >= 1u);
        assert(swapchainStats.browserManagedPresentCount >= 1u);
        assert(swapchainStats.nativeResizeCount >= 1u);
        assert(swapchainStats.surfaceLossCount >= 1u);
        assert(swapchainStats.hiddenTabFrameSkipCount >= 1u);
        assert(swapchainStats.boundedFrameSlotCount == 2u);
    } else {
        assert(swapchainStats.nativeSurfaceCreateCount == 0u);
        assert(swapchainStats.nativeCanvasAcquireCount == 0u);
        assert(swapchainStats.browserManagedPresentCount == 0u);
        assert(swapchainStats.nativeResizeCount == 0u);
    }
    const auto resizeLifecycle =
        describeWebGpuRhiCanvasLifecycle(desc, WebGpuRhiCanvasEvent::Resize);
    assert(resizeLifecycle.success);
    assert(resizeLifecycle.resizeDiagnosed);
    assert(resizeLifecycle.swapchainRebuildRequired);
    const auto lostLifecycle =
        describeWebGpuRhiCanvasLifecycle(desc, WebGpuRhiCanvasEvent::SurfaceLost);
    assert(lostLifecycle.success);
    assert(lostLifecycle.surfaceLostDiagnosed);
    assert(lostLifecycle.releaseSurfaceResources);
    assert(!lostLifecycle.acquireAllowed);
    const auto hiddenLifecycle =
        describeWebGpuRhiCanvasLifecycle(desc, WebGpuRhiCanvasEvent::HiddenTab);
    assert(hiddenLifecycle.success);
    assert(hiddenLifecycle.hiddenTabFrameSkipped);
    assert(!hiddenLifecycle.acquireAllowed);
    assert(!hiddenLifecycle.nativeHandleExposedToUpperLayer);

    // FrameInFlightSyncDesc / validateFrameInFlightSync removed during RHI
    // collapse — sync now backend-owned, validated at device creation.

    const auto commandSlice = describeWebGpuRhiCommandSlice();
    assert(commandSlice.backendDescriptorVocabularyHiddenFromUpperLayer);
    assert(!commandSlice.nativeCommandHandleExposedToUpperLayer);
    assert(commandSlice.pushConstantUploadRingSupported);
    assert(commandSlice.backendOwnedUploadStagingSupported);
    assert(commandSlice.softSecondaryCommandReplaySupported);
    assert(commandSlice.canvasLifecycleDiagnosticsRecorded);
    assert(commandSlice.logicalFrameSlots == 2u);

    auto unqueriedProbe = WebGpuRuntimeProbe{};
    unqueriedProbe.browserRuntime = true;
    unqueriedProbe.canvasSurface = true;
    unqueriedProbe.nativeDevice = false;
    unqueriedProbe.computeDispatch = true;
    unqueriedProbe.storageTextureBinding = true;
    const auto unqueriedProfile = buildWebGpuRuntimeProfile(unqueriedProbe);
    assert(!unqueriedProfile.optionalFeatures.computeDispatch);
    assert(!unqueriedProfile.optionalFeatures.storageTextureBinding);
    auto unqueriedDesc = makeWebGpuDeviceDesc(unqueriedProfile);
    const auto unqueriedReport = describeWebGpuRhiDevice(unqueriedDesc);
    assert(!unqueriedReport.success);
    assert(unqueriedReport.diagnostics.find("runtime") != std::pmr::string::npos);

    auto leakingDesc = desc;
    leakingDesc.allowNativeHandleExposureToUpperLayers = true;
    const auto leakingReport = describeWebGpuRhiDevice(leakingDesc);
    assert(!leakingReport.success);
    assert(leakingReport.diagnostics.find("native") != std::pmr::string::npos);

    auto wrongBackend = desc;
    wrongBackend.capabilityContract.backend = Backend::Vulkan;
    const auto wrongBackendReport = describeWebGpuRhiDevice(wrongBackend);
    assert(!wrongBackendReport.success);
    assert(wrongBackendReport.diagnostics.find("WebGPU") != std::pmr::string::npos);

    assert(swapchain->setSurfaceAvailable(true).success);
    arp::tests::verifyFrameInputLifecycle(*swapchain,
        {"#WebGpuCanvasInvalidResizeContract", {320u, 180u}, TextureFormat::Bgra8Unorm, 2u},
        report.publicDeviceNativeDeviceAvailable);
    return 0;
}
