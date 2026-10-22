#include "../fixtures/cpp_source_evidence.h"
#include "../fixtures/depth_stencil_attachment_input_fixture.h"
#include "../fixtures/original_buffer_state_fixture.h"
#include "../fixtures/original_command_sampler_custody_fixture.h"
#include "../fixtures/original_heap_registration_fixture.h"
#include "../fixtures/original_texture_state_fixture.h"
#include "core/memory/cpu_allocator.h"
#include "rhi/backends/metal/metal_allocator.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_binding_validation.h"
#include "rhi/backends/metal/metal_bindless_validation.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_graphics_preparation.h"
#include "rhi/backends/metal/metal_native_commands.h"
#include "rhi/backends/metal/metal_native_descriptors.h"
#include "rhi/backends/metal/metal_native_features.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_native_swapchain.h"
#include "rhi/backends/metal/metal_ordinary_binding.h"
#include "rhi/backends/metal/metal_runtime_smoke.h"
#include "rhi/frontend/bindless_graphics_binding.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_extensions.h"
#include "rhi/gpu_pipeline.h"
#include "tests/fixtures/metal_host_diagnostic_device_fixture.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <vector>

#if defined(NDEBUG)
#error "Metal device contract tests require active assertions"
#endif

namespace {

arp::tests::CppSourceEvidence readText(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream stream;
    stream << file.rdbuf();
    return arp::core::toPmrString(stream.str());
}

arp::tests::CppSourceEvidence readText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream stream;
    stream << file.rdbuf();
    return arp::core::toPmrString(stream.str());
}

bool hasAnyToken(const std::pmr::string& text, const std::pmr::vector<const char*>& tokens)
{
    for (const auto* token : tokens) {
        if (text.find(token) != std::pmr::string::npos) {
            return true;
        }
    }
    return false;
}

void assertMetalRhiBackendDoesNotDelegateToNull()
{
#if defined(ARP_SOURCE_DIR)
    const auto deviceSource = readText(ARP_SOURCE_DIR "/src/rhi/backends/metal/metal_device.cpp");
    assert(deviceSource.find("createNullDevice") == std::pmr::string::npos);
    assert(deviceSource.find("rhi/null_backend") == std::pmr::string::npos);
    assert(deviceSource.find("null_backend") == std::pmr::string::npos);
#endif
}

void assertMetalRhiAppleApiBoundary()
{
#if defined(ARP_SOURCE_DIR)
    const auto root = std::filesystem::path(ARP_SOURCE_DIR);
    const auto metalRoot = root / "src" / "rhi" / "backends" / "metal";
    const auto nativeRuntime = metalRoot / "metal_native_runtime.mm";
    assert(std::filesystem::exists(nativeRuntime));

    const auto nativeRuntimeSource = readText(nativeRuntime);
    assert(nativeRuntimeSource.find("#include <Metal/Metal.h>") != std::pmr::string::npos);
    assert(
        nativeRuntimeSource.find("#include <QuartzCore/CAMetalLayer.h>") != std::pmr::string::npos);

    const std::pmr::vector<const char*> nativeTokens{
        "#include <Metal/",
        "#include <QuartzCore/",
        "id<MTL",
        "MTLDevice",
        "MTLBuffer",
        "MTLTexture",
        "MTLCommandQueue",
        "MTLCommandBuffer",
        "CAMetalLayer",
        "CAMetalDrawable",
    };

    for (const auto& entry : std::filesystem::recursive_directory_iterator(metalRoot)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto ext = entry.path().extension().string();
        if (ext != ".h" && ext != ".hpp" && ext != ".cpp") {
            continue;
        }
        // This private bridge header is included only by Objective-C++ native
        // translation units and intentionally owns the framework enum type.
        if (entry.path().filename() == "metal_native_format.h") {
            continue;
        }
        assert(!hasAnyToken(readText(entry.path()), nativeTokens));
    }

    const auto cmake = readText(root / "CMakeLists.txt");
    assert(cmake.find("if(APPLE)") != std::pmr::string::npos);
    assert(cmake.find("src/rhi/backends/metal/metal_native_runtime.mm") != std::pmr::string::npos);
    assert(
        cmake.find(
            "target_link_libraries(arp_rhi_metal PRIVATE \"-framework Metal\" \"-framework QuartzCore\")")
        != std::pmr::string::npos);
#endif
}

void assertMetalRhiSwapchainUsesPrivateNativePayloads()
{
#if defined(ARP_SOURCE_DIR)
    const auto root = std::filesystem::path(ARP_SOURCE_DIR);
    const auto metalRoot = root / "src" / "rhi" / "backends" / "metal";
    const auto objectHeader = readText(metalRoot / "metal_backend_objects.h");
    const auto swapchainSource = readText(metalRoot / "metal_backend_swapchain.cpp");
    const auto deviceSource = readText(metalRoot / "metal_device.cpp");
    const auto nativeHeader = readText(metalRoot / "metal_native_swapchain.h");
    const auto nativeSwapchainSource = readText(metalRoot / "metal_native_swapchain.mm");
    const auto nativeSwapchainStub = readText(metalRoot / "metal_native_swapchain_stub.cpp");
    const auto cmake = readText(root / "CMakeLists.txt");

    assert(nativeHeader.find("struct MetalRhiNativeSwapchainStats") != std::pmr::string::npos);
    assert(nativeHeader.find("class MetalRhiNativeSwapchainPayload") != std::pmr::string::npos);
    assert(nativeHeader.find("createMetalRhiNativeSwapchainPayload") != std::pmr::string::npos);
    assert(nativeHeader.find("acquireDrawable(std::uint32_t frameSlot") != std::pmr::string::npos);
    assert(nativeHeader.find("MetalRhiNativeFrameSlotCompletionState") != std::pmr::string::npos);
    assert(nativeHeader.find("std::shared_ptr<MetalRhiNativeFrameSlotCompletionState>")
        != std::pmr::string::npos);
    assert(nativeHeader.find("nativeDrawables_") != std::pmr::string::npos);
    assert(objectHeader.find("std::shared_ptr<MetalRhiNativeSwapchainStats>")
        != std::pmr::string::npos);
    assert(swapchainSource.find("std::shared_ptr<MetalRhiNativeSwapchainPayload>")
        != std::pmr::string::npos);
    // The L2 frame-slot bookkeeping now lives in the shared FrameSlotStateMachine
    // helper; the Metal swapchain drives it via slots_.acquireSlot() /
    // slots_.slotInFlight() / slots_.clearSlot(). The native payload calls
    // remain the backend-specific seam.
    assert(swapchainSource.find("FrameSlotStateMachine<2>") != std::pmr::string::npos);
    assert(swapchainSource.find("slots_.acquireSlot()") != std::pmr::string::npos);
    assert(swapchainSource.find("slots_.slotInFlight(") != std::pmr::string::npos);
    assert(swapchainSource.find("slots_.clearSlot(") != std::pmr::string::npos);
    assert(swapchainSource.find("acquireDrawable(acquired.slot") != std::pmr::string::npos);
    assert(swapchainSource.find("recycleCompletedFrameSlots()") != std::pmr::string::npos);
    assert(swapchainSource.find("frameSlotCompletions_") != std::pmr::string::npos);
    for (const auto method : {"setSurfaceAvailable(bool available)",
             "resize(const rhi::Extent2D& extent)",
             "reconfigure("}) {
        const auto start = swapchainSource.find(method);
        const auto guard = swapchainSource.find("slots_.anySlotInFlight()", start);
        const auto native = swapchainSource.find("nativePayload_->", start);
        assert(start != std::pmr::string::npos && guard < native);
        assert(swapchainSource.find("RHI-METAL-SURFACE-MUTATION-IN-FLIGHT", guard) < native);
    }
    assert(swapchainSource.find("resizeDrawableLayer") != std::pmr::string::npos);
    assert(swapchainSource.find("setDrawableLayerAvailable") != std::pmr::string::npos);
    assert(swapchainSource.find("memoryWarning") != std::pmr::string::npos);
    // The per-backend frame-slot state members are gone (lifted to the helper).
    assert(swapchainSource.find("frameSlotsInFlight_") == std::pmr::string::npos);
    assert(swapchainSource.find("findFreeFrameSlot") == std::pmr::string::npos);
    assert(swapchainSource.find("frameSlotInFlight") == std::pmr::string::npos);
    assert(swapchainSource.find("frameSerial_ % maxFramesInFlight()") == std::pmr::string::npos);
    assert(deviceSource.find("inspectMetalRhiNativeSwapchainStats") != std::pmr::string::npos);

    assert(nativeSwapchainSource.find("#include <Metal/Metal.h>") != std::pmr::string::npos);
    assert(nativeSwapchainSource.find("#include <QuartzCore/CAMetalLayer.h>")
        != std::pmr::string::npos);
    assert(nativeSwapchainSource.find("nextDrawable") != std::pmr::string::npos);
    assert(nativeSwapchainSource.find("presentDrawable") != std::pmr::string::npos);
    assert(nativeSwapchainSource.find("nativeDrawables_[frameSlot]") != std::pmr::string::npos);
    assert(nativeSwapchainSource.find("maximumDrawableCount") != std::pmr::string::npos);
    assert(nativeSwapchainSource.find("drawableSize") != std::pmr::string::npos);
    assert(nativeSwapchainSource.find("removeAllAnimations") != std::pmr::string::npos);
    const auto metalResizeValidate = nativeSwapchainSource.find("if (!validExtent(extent))");
    const auto metalResizeCommit = nativeSwapchainSource.find("desc_.extent = extent");
    assert(metalResizeValidate != std::pmr::string::npos);
    assert(metalResizeCommit != std::pmr::string::npos);
    assert(metalResizeValidate < metalResizeCommit);
    assert(
        nativeSwapchainStub.find("nativeRuntimeDiagnosticOnly = true") != std::pmr::string::npos);
    assert(
        cmake.find("src/rhi/backends/metal/metal_native_swapchain.mm") != std::pmr::string::npos);
    assert(cmake.find("src/rhi/backends/metal/metal_native_swapchain_stub.cpp")
        != std::pmr::string::npos);
#endif
}

void assertMetalRhiMinGwDiagnosticOnlyGuardrails()
{
#if defined(ARP_SOURCE_DIR) && (defined(__MINGW32__) || defined(__MINGW64__))
    const auto root = std::filesystem::path(ARP_SOURCE_DIR);
    const auto metalRoot = root / "src" / "rhi" / "backends" / "metal";
    const std::pmr::vector<std::filesystem::path> stubs{
        metalRoot / "metal_native_runtime_stub.cpp",
        metalRoot / "metal_native_resources_stub.cpp",
        metalRoot / "metal_native_descriptors_stub.cpp",
        metalRoot / "metal_native_pipelines_stub.cpp",
        metalRoot / "metal_native_commands_stub.cpp",
        metalRoot / "metal_native_queries_stub.cpp",
        metalRoot / "metal_native_features_stub.cpp",
        metalRoot / "metal_native_sync_stub.cpp",
        metalRoot / "metal_native_secondary_stub.cpp",
        metalRoot / "metal_native_swapchain_stub.cpp",
    };
    const std::pmr::vector<const char*> nativeTokens{
        "#include <Metal/",
        "#include <QuartzCore/",
        "id<MTL",
        "MTLDevice",
        "MTLCommandBuffer",
        "CAMetalLayer",
        "CAMetalDrawable",
    };
    for (const auto& stub : stubs) {
        const auto source = readText(stub);
        assert(!hasAnyToken(source, nativeTokens));
        assert(source.find("createNullDevice") == std::pmr::string::npos);
        assert(source.find("null_backend") == std::pmr::string::npos);
    }
    const auto runtimeBoundary = arp::rhi_metal::describeMetalRhiNativeRuntimeBoundary();
    assert(!runtimeBoundary.runtimeAvailableOnThisPlatform);
    assert(!arp::rhi_metal::probeMetalRhiNativeFeatures().argumentBuffersTier2);
#endif
}

void assertMetalRhiAppleRuntimeSmokeTelemetryExists()
{
#if defined(ARP_SOURCE_DIR)
    const auto root = std::filesystem::path(ARP_SOURCE_DIR);
    const auto metalRoot = root / "src" / "rhi" / "backends" / "metal";
    const auto smokeHeader = readText(metalRoot / "metal_runtime_smoke.h");
    const auto smokeSource = readText(metalRoot / "metal_runtime_smoke.cpp");
    const auto cmake = readText(root / "CMakeLists.txt");
    const std::pmr::vector<const char*> nativeTokens{
        "#include <Metal/",
        "#include <QuartzCore/",
        "id<MTL",
        "MTLDevice",
        "MTLCommandBuffer",
        "CAMetalLayer",
        "CAMetalDrawable",
    };

    assert(smokeHeader.find("MetalRhiAppleRuntimeSmokeReport") != std::pmr::string::npos);
    assert(smokeHeader.find("runMetalRhiAppleRuntimeSmoke") != std::pmr::string::npos);
    assert(smokeSource.find("createMetalRhiDevice") != std::pmr::string::npos);
    assert(smokeSource.find("inspectMetalRhiNativePayload") != std::pmr::string::npos);
    assert(smokeSource.find("inspectMetalRhiNativeCommandStats") != std::pmr::string::npos);
    assert(smokeSource.find("inspectMetalRhiNativeQueueStats") != std::pmr::string::npos);
    assert(smokeSource.find("inspectMetalRhiNativeSwapchainStats") != std::pmr::string::npos);
    assert(!hasAnyToken(smokeSource, nativeTokens));
    assert(cmake.find("src/rhi/backends/metal/metal_runtime_smoke.cpp") != std::pmr::string::npos);
#endif
}

void assertMetalDefaultTextureTransitionInfersFormatAspect()
{
#if defined(ARP_SOURCE_DIR)
    // The texture transition overload that infers the full subresource range
    // (via rhi::defaultTextureSubresourceRange) now lives in the shared
    // CommandEncoderBase final overload rather than each backend's encoder.
    // Verify the base header provides it and that the Metal encoder no longer
    // carries a local copy of the delegation.
    const auto baseHeader = readText(ARP_SOURCE_DIR "/src/rhi/frontend/command_encoder_base.h");
    assert(baseHeader.find("defaultTextureSubresourceRange") != std::pmr::string::npos);
    assert(baseHeader.find("makeInferredResourceBarrier(") != std::pmr::string::npos);
    assert(baseHeader.find("texture.id(), defaultTextureSubresourceRange(texture.desc()),")
        != std::pmr::string::npos);

    const auto commandSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/metal/metal_backend_commands.cpp");
    assert(
        commandSource.find(
            "return transition(texture, { texture.id(), rhi::defaultTextureSubresourceRange(texture.desc()), before, after });")
        == std::pmr::string::npos);
    assert(commandSource.find("return transition(texture, { texture.id(), {}, before, after });")
        == std::pmr::string::npos);
#endif
}

void assertMetalRhiQueryAndProbeContracts()
{
#if defined(ARP_SOURCE_DIR)
    const auto root = std::filesystem::path(ARP_SOURCE_DIR);
    const auto metalRoot = root / "src" / "rhi" / "backends" / "metal";
    const auto queryHeader = readText(metalRoot / "metal_native_queries.h");
    const auto querySource = readText(metalRoot / "metal_native_queries.mm");
    const auto queryStub = readText(metalRoot / "metal_native_queries_stub.cpp");
    const auto commandHeader = readText(metalRoot / "metal_native_commands.h");
    const auto commandSource = readText(metalRoot / "metal_native_commands.mm");
    const auto commandStub = readText(metalRoot / "metal_native_commands_stub.cpp");
    const auto commandBackend = readText(metalRoot / "metal_backend_commands.cpp");
    const auto featureSource = readText(metalRoot / "metal_native_features.mm");
    const auto deviceSource = readText(metalRoot / "metal_device.cpp");
    const auto resourceSource = readText(metalRoot / "metal_native_resources.mm");
    const auto stagingSource = readText(metalRoot / "metal_staging_upload_pool.mm");

    assert(queryHeader.find("NotRecorded") != std::pmr::string::npos);
    assert(queryHeader.find("Recorded") != std::pmr::string::npos);
    assert(queryHeader.find("Pending") != std::pmr::string::npos);
    assert(queryHeader.find("Available") != std::pmr::string::npos);
    assert(queryHeader.find("DiagnosticFailure") != std::pmr::string::npos);
    assert(queryHeader.find("readMetalRhiNativePipelineStatistics") != std::pmr::string::npos);
    assert(querySource.find("resolveCounterRange") != std::pmr::string::npos);
    assert(querySource.find("queryIndex") != std::pmr::string::npos);
    assert(querySource.find("RHI-METAL-QUERY-POOL-RESET-WAITING-FOR-COMPLETION")
        != std::pmr::string::npos);
    assert(querySource.find("markMetalRhiNativeTimestampQuerySubmitted") != std::pmr::string::npos);
    assert(queryStub.find("NATIVE-RUNTIME-UNAVAILABLE") != std::pmr::string::npos);

    assert(commandHeader.find("recordedTimestampQueries_") != std::pmr::string::npos);
    assert(commandHeader.find("recordedPipelineStatisticsQueries_") != std::pmr::string::npos);
    assert(
        commandSource.find("markMetalRhiNativeTimestampQuerySubmitted") != std::pmr::string::npos);
    assert(commandSource.find("resolveMetalRhiNativePipelineStatistics") != std::pmr::string::npos);
    assert(commandSource.find("recordedTimestampQueries") != std::pmr::string::npos);
    // Submission blocks may copy shared owner aliases, never nonempty query
    // vectors: otherwise record and submit each retain their own allocations.
    assert(commandSource.find("const auto retainedQueryPools = retainedQueryPools_;")
        == std::pmr::string::npos);
    assert(commandSource.find("const auto recordedTimestampQueries = recordedTimestampQueries_;")
        == std::pmr::string::npos);
    assert(commandSource.find(
               "const auto recordedPipelineStatisticsQueries = recordedPipelineStatisticsQueries_;")
        == std::pmr::string::npos);
    assert(commandSource.find("submittedQueries->releaseAfterResolution();")
        != std::pmr::string::npos);
    assert(commandBackend.find("metalPool->type() != rhi::QueryType::Timestamp")
        != std::pmr::string::npos);
    assert(commandBackend.find("metalPool->type() != rhi::QueryType::PipelineStatistics")
        != std::pmr::string::npos);
    assert(commandBackend.find("RHI-METAL-TIMESTAMP-QUERY-POOL-INVALID") != std::pmr::string::npos);
    assert(commandBackend.find("RHI-METAL-PIPELINE-STATISTICS-QUERY-POOL-INVALID")
        != std::pmr::string::npos);

    assert(deviceSource.find("hasExecutableOptionalNativeFeature") != std::pmr::string::npos);
    assert(deviceSource.find("readPipelineStatistics") != std::pmr::string::npos);
    assert(deviceSource.find("FeatureNotEnabled") != std::pmr::string::npos);

    const auto bcPipeline = featureSource.find("newRenderPipelineStateWithDescriptor");
    assert(bcPipeline != std::pmr::string::npos);
    assert(featureSource.find("setFragmentTexture") > bcPipeline);
    assert(featureSource.find("drawPrimitives") > bcPipeline);
    assert(featureSource.find("waitUntilCompleted") > bcPipeline);
    assert(featureSource.find("MTLPixelFormatBC1_RGBA") != std::pmr::string::npos);
    assert(featureSource.find("MTLPixelFormatBC3_RGBA") != std::pmr::string::npos);
    assert(featureSource.find("MTLPixelFormatBC7_RGBAUnorm") != std::pmr::string::npos);
    assert(featureSource.find("MTLPixelFormatBC7_RGBAUnorm_sRGB") != std::pmr::string::npos);

    assert(commandSource.find("waitUntilCompleted") != std::pmr::string::npos);
    assert(commandHeader.find("blockingWaitUsed") != std::pmr::string::npos);
    assert(commandSource.find("#if TARGET_OS_IPHONE") != std::pmr::string::npos);
    assert(commandSource.find("#if !TARGET_OS_IPHONE") != std::pmr::string::npos);
    assert(resourceSource.find("#include <TargetConditionals.h>") != std::pmr::string::npos);
    assert(resourceSource.find("MTLResourceStorageModeShared") != std::pmr::string::npos);
    assert(stagingSource.find("#if TARGET_OS_IPHONE") != std::pmr::string::npos);
    assert(stagingSource.find("MTLResourceStorageModeShared") != std::pmr::string::npos);
    assert(commandStub.find("RHI-METAL-WAIT-IDLE-NATIVE-RUNTIME-UNAVAILABLE")
        != std::pmr::string::npos);
#endif
}

void assertMetalDdgiComputeContract()
{
#if defined(ARP_SOURCE_DIR)
    const auto source =
        readText(std::filesystem::path(ARP_SOURCE_DIR) / "shaders" / "renderer" / "ddgi.slang");
    assert(source.find("giProbeUpdateComputeMain") != std::pmr::string::npos);
    assert(source.find("giIrradianceResolveComputeMain") != std::pmr::string::npos);
    assert(source.find("giProbeRelocateComputeMain") != std::pmr::string::npos);
    // These bindings are the portable GiProbeSet argument-buffer shape. The
    // Metal lowering must preserve their resource kinds and logical numbers.
    assert(source.find("[[vk::binding(0, 0)]]") != std::pmr::string::npos);
    assert(source.find("ProbeIrradianceOut") != std::pmr::string::npos);
    assert(source.find("[[vk::binding(3, 0)]]") != std::pmr::string::npos);
    assert(source.find("ProbeDepthMomentsOut") != std::pmr::string::npos);
    assert(source.find("[[vk::binding(4, 0)]]") != std::pmr::string::npos);
    assert(source.find("GiIrradianceOutput") != std::pmr::string::npos);
    assert(source.find("[[vk::binding(9, 0)]]") != std::pmr::string::npos);
    assert(source.find("ProbeStateBuffer") != std::pmr::string::npos);
    assert(source.find("[[vk::binding(20, 0)]]") != std::pmr::string::npos);
    assert(source.find("DdgiLocalLights") != std::pmr::string::npos);
    assert(source.find("[[vk::binding(21, 0)]]") != std::pmr::string::npos);
    assert(source.find("ProbeLocalLightInfluences") != std::pmr::string::npos);
#endif
}

arp::rhi::semantics::NativeBackendCapabilityContract makeQueriedMetalContract()
{
    auto contract = arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Metal);
    contract.queriedFromNativeDevice = true;
    contract.queriedExtendedFeatures = true;
    return contract;
}

arp::rhi::ShaderPackageDesc makeMetalShaderPackage()
{
    arp::rhi::ShaderPackageDesc shaderPackage;
    shaderPackage.label = "MetalRhiShaderPackage";
    shaderPackage.slangModulePath = "metal_rhi.slang";
    shaderPackage.entrypoints.push_back(
        {"metal_rhi.slang", "vsMain", arp::rhi::ShaderStage::Vertex});
    shaderPackage.entrypoints.push_back(
        {"metal_rhi.slang", "fsMain", arp::rhi::ShaderStage::Fragment});
    shaderPackage.entrypoints.push_back(
        {"metal_rhi.slang", "csMain", arp::rhi::ShaderStage::Compute});
    shaderPackage.artifacts.spirvPath = "metal_rhi.spv";
    shaderPackage.artifacts.mslPath = "metal_rhi.metal";
    shaderPackage.artifacts.reflectionPath = "metal_rhi.reflection.yaml";
    shaderPackage.reflection.pushConstantByteCount = 64;
    shaderPackage.reflection.pushConstantContractVersion = "AtomicFullBlockPush/1";
    shaderPackage.reflection.pushConstantStages = "VS|FS|CS";
    return shaderPackage;
}

} // namespace

void assertMetalNativeAliasingAndCompletionContract();
void assertMetalGpuCountedAndPatchListContracts();

void validatePreparedComputeLayout()
{
    using namespace arp::rhi;
    using namespace arp::rhi_metal;
    MetalRhiNativePipelinePayload pipeline;
    pipeline.metalArgumentBuffer.reserve(2u);
    pipeline.metalArgumentBuffer.push_back(
        {0u, 4u, 2u, 0u, 2u, DescriptorType::StorageBuffer, true});
    // Generated reflection explicitly marks the alternate direct mapping inactive.
    pipeline.metalBindingSpace.push_back(
        {0u, 4u, UINT32_MAX, DescriptorType::UniformBuffer, false});
    pipeline.nativeBindingStages.record(DescriptorType::UniformBuffer, 2u, ShaderStage::Compute);
    MetalRhiNativeDescriptorGroupPayload descriptors;
    for (unsigned i = 0u; i < 2u; ++i) {
        MetalRhiNativeArgumentBindingPayload binding;
        binding.binding = 4u;
        binding.arrayCount = 2u;
        binding.arrayElement = i;
        binding.type = DescriptorType::StorageBuffer;
        binding.stages = ShaderStage::Compute;
        binding.resourceKind = DescriptorResourceKind::Buffer;
        binding.argumentKind = MetalRhiArgumentKind::Buffer;
        descriptors.bindings.push_back(binding);
    }
    const auto valid = [&] {
        return validateMetalRhiPreparedComputeLayout(pipeline, descriptors, 0u);
    };
    assert(valid().success);
    pipeline.nativeBindingStages = {};
    assert(valid().diagnostics == "RHI-METAL-PREPARED-COMPUTE-PHYSICAL-STAGE-MISMATCH");
    pipeline.nativeBindingStages.record(DescriptorType::UniformBuffer, 2u, ShaderStage::Compute);
    auto& map = pipeline.metalArgumentBuffer.front();
    map.capacity = 1u;
    assert(valid().diagnostics == "RHI-METAL-PREPARED-COMPUTE-ARRAY-MAPPING");
    map.capacity = 2u;
    map.argumentIndex = UINT32_MAX - 1u;
    assert(valid().success);
    map.argumentIndex = UINT32_MAX;
    assert(valid().diagnostics == "RHI-METAL-PREPARED-COMPUTE-ARGUMENT-RANGE");
    map.argumentIndex = 0u;
    descriptors.bindings[1].arrayElement = 0u;
    assert(valid().diagnostics == "RHI-METAL-PREPARED-COMPUTE-DUPLICATE-ELEMENT");
    descriptors.bindings[1].arrayElement = 1u;
    descriptors.bindings.pop_back();
    assert(valid().diagnostics == "RHI-METAL-PREPARED-COMPUTE-ELEMENT-MISSING");
    auto second = descriptors.bindings.front();
    second.arrayElement = 1u;
    descriptors.bindings.push_back(second);
    descriptors.bindings[1].argumentKind = MetalRhiArgumentKind::Texture;
    assert(valid().diagnostics == "RHI-METAL-PREPARED-COMPUTE-TYPE-MISMATCH");
    descriptors.bindings[1].argumentKind = MetalRhiArgumentKind::Buffer;
    descriptors.bindings[1].dynamicOffsetIndex = 0u;
    assert(valid().diagnostics == "RHI-METAL-PREPARED-COMPUTE-BINDING-INVALID");
    descriptors.bindings[1].dynamicOffsetIndex = UINT32_MAX;
    pipeline.pushConstantMetalBufferIndex = 2u;
    assert(valid().diagnostics == "RHI-METAL-PREPARED-COMPUTE-SLOT-OVERLAP");
    pipeline.pushConstantMetalBufferIndex = UINT32_MAX;
    pipeline.metalArgumentBuffer.push_back(map);
    assert(valid().diagnostics == "RHI-METAL-PREPARED-COMPUTE-MAPPING-MISSING-OR-AMBIGUOUS");
    pipeline.metalArgumentBuffer.pop_back();
    map.active = false;
    assert(valid().success);
    pipeline.metalArgumentBuffer.clear();
    pipeline.metalBindingSpace.clear();
    assert(valid().diagnostics == "RHI-METAL-PREPARED-COMPUTE-MAPPING-MISSING-OR-AMBIGUOUS");
#if !defined(__APPLE__)
    arp::core::pmr::String diagnostic;
    assert(!prepareMetalRhiNativeComputeBindings({}, {}, 0u, {}, diagnostic));
    assert(diagnostic == "RHI-METAL-PREPARED-COMPUTE-NATIVE-RUNTIME-UNAVAILABLE");
    assert(!createMetalRhiNativeRenderPipelinePayload(RenderPipelineDesc{},
        PipelineValidationReport{},
        semantics::MetalVertexStreamMapping{},
        std::string_view{},
        std::string_view{}));
    assert(!prepareMetalRhiNativeGraphicsBindings({}, {}, 0u, {}, {}, diagnostic));
    assert(diagnostic == "RHI-METAL-PREPARED-GRAPHICS-NATIVE-RUNTIME-UNAVAILABLE");
#endif
}

void validatePreparedGraphicsLayouts()
{
    using namespace arp::rhi;
    using namespace arp::rhi_metal;
    MetalRhiPreparedGraphicsStageLayout vertex, fragment;
    vertex.stage = ShaderStage::Vertex;
    vertex.entry = "vertexMain";
    fragment.stage = ShaderStage::Fragment;
    fragment.entry = "fragmentMain";
    vertex.arrays.push_back({0u, 4u, 2u, 0u, 2u, DescriptorType::ReadOnlyStorageBuffer, true});
    fragment.arrays.push_back({0u, 4u, 6u, 0u, 2u, DescriptorType::ReadOnlyStorageBuffer, true});
    semantics::MetalPhysicalBindingStages nativeStages;
    assert(nativeStages.record(DescriptorType::UniformBuffer, 2u, ShaderStage::Vertex));
    assert(nativeStages.record(DescriptorType::UniformBuffer, 6u, ShaderStage::Fragment));
    semantics::MetalVertexStreamMapping streams;
    streams.success = true;
    streams.slots[0u] = 6u;
    MetalRhiNativeDescriptorGroupPayload descriptors;
    for (unsigned i = 0u; i < 2u; ++i) {
        MetalRhiNativeArgumentBindingPayload binding;
        binding.binding = 4u;
        binding.arrayCount = 2u;
        binding.arrayElement = i;
        binding.type = DescriptorType::ReadOnlyStorageBuffer;
        binding.stages = ShaderStage::Vertex | ShaderStage::Fragment;
        binding.resourceKind = DescriptorResourceKind::Buffer;
        binding.argumentKind = MetalRhiArgumentKind::Buffer;
        descriptors.bindings.push_back(binding);
    }
    const auto check = [&](const auto& stage) {
        return stage.validate(descriptors, 0u, nativeStages, streams);
    };
    assert(check(vertex).success && check(fragment).success);
    MetalRhiPreparedBindingLocation location;
    assert(vertex.resolve(descriptors.bindings.front(), 0u, nativeStages, location).success
        && location.slot == 2u);
    assert(fragment.resolve(descriptors.bindings.front(), 0u, nativeStages, location).success
        && location.slot == 6u);
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0u; i < 32u; ++i)
        assert(check(vertex).success && check(fragment).success);
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
    streams.slots[0u] = 2u;
    assert(check(vertex).diagnostics == "RHI-METAL-PREPARED-GRAPHICS-SLOT-OVERLAP");
    assert(check(fragment).success);
    streams.slots[0u] = 6u;
    streams.slots[1u] = 6u;
    assert(check(vertex).diagnostics == "RHI-METAL-PREPARED-GRAPHICS-VERTEX-STREAM-INVALID");
    streams.slots[1u] = UINT32_MAX;
    streams.success = false;
    assert(check(vertex).diagnostics == "RHI-METAL-PREPARED-GRAPHICS-VERTEX-STREAM-MISSING");
    assert(check(fragment).success);
    streams.success = true;
    vertex.pushSlot = 6u;
    assert(check(vertex).diagnostics == "RHI-METAL-PREPARED-GRAPHICS-SLOT-OVERLAP");
    vertex.pushSlot = 2u;
    assert(check(vertex).diagnostics == "RHI-METAL-PREPARED-GRAPHICS-SLOT-OVERLAP");
    vertex.pushSlot = UINT32_MAX;
    descriptors.bindings.front().stages = ShaderStage::Fragment;
    assert(check(vertex).diagnostics == "RHI-METAL-PREPARED-GRAPHICS-PHYSICAL-STAGE-MISMATCH");
    descriptors.bindings.front().stages = ShaderStage::Vertex | ShaderStage::Fragment;
    vertex.arrays.front().argumentBufferSlot = 6u;
    assert(check(vertex).diagnostics == "RHI-METAL-PREPARED-GRAPHICS-PHYSICAL-STAGE-MISMATCH");
    vertex.arrays.front().argumentBufferSlot = 2u;
    vertex.arrays.front().capacity = 1u;
    assert(check(vertex).diagnostics == "RHI-METAL-PREPARED-GRAPHICS-ARRAY-MAPPING");
    vertex.arrays.front().capacity = 2u;
    descriptors.bindings.back().arrayElement = 0u;
    assert(check(vertex).diagnostics == "RHI-METAL-PREPARED-GRAPHICS-DUPLICATE-ELEMENT");
    descriptors.bindings.pop_back();
    assert(check(vertex).diagnostics == "RHI-METAL-PREPARED-GRAPHICS-ELEMENT-MISSING");
    vertex.stage = ShaderStage::Vertex | ShaderStage::Fragment;
    assert(check(vertex).diagnostics == "RHI-METAL-PREPARED-GRAPHICS-ENTRY-STAGE-MISMATCH");
    vertex.stage = ShaderStage::Vertex;
    vertex.entry.clear();
    assert(check(vertex).diagnostics == "RHI-METAL-PREPARED-GRAPHICS-ENTRY-STAGE-MISMATCH");
}

void assertMetalGraphicsBufferAdmission()
{
    using namespace arp::rhi;
    using namespace arp::rhi_metal;
    using Op = BindlessOperation;
    const auto graphics = ShaderStage::Vertex | ShaderStage::Fragment;
    BindlessShaderArtifact artifact;
    artifact.execution = BindlessExecution::Native;
    artifact.activePrefixContractVersion = 2u;
    artifact.snapshotMetadata = {1u, 6u, 0u, graphics};
    artifact.faultLatch = {1u, 7u, graphics};
    artifact.faultReportingContractVersion = 1u;
    artifact.physicalLayout.bindings.push_back({0u, DescriptorType::UniformBuffer, graphics, 2u});
    artifact.physicalLayout.bindings.push_back(
        {2u, DescriptorType::ReadOnlyStorageBuffer, graphics, 2u});
    artifact.physicalLayout.bindings.push_back(
        {4u, DescriptorType::StorageBuffer, ShaderStage::Fragment, 2u});
    artifact.physicalLayout.bindings.push_back(
        {6u, DescriptorType::ReadOnlyStorageBuffer, graphics});
    artifact.physicalLayout.bindings.push_back({7u, DescriptorType::StorageBuffer, graphics});
    for (unsigned index = 0u; index < 3u; ++index) {
        BindlessTableMapping table;
        table.tableId = index + 1u;
        table.firstBinding = index * 2u;
        table.capacity = 2u;
        table.nonUniform = true;
        table.operations = index == 0u ? Op::Load
            : index == 1u              ? Op::Load | Op::Query
                                       : Op::Query | Op::Atomic;
        artifact.tables.push_back(table);
    }
    assert(validateMetalBindlessGraphicsArtifact(artifact).success);
    assert(!validateMetalBindlessComputeArtifact(artifact).success);
    {
        auto sampled = artifact;
        auto& image = sampled.physicalLayout.bindings[0u];
        image.type = DescriptorType::SampledTexture;
        image.stages = ShaderStage::Fragment;
        image.textureViewDimensionConstrained = true;
        auto& sampler = sampled.physicalLayout.bindings[1u];
        sampler.type = DescriptorType::Sampler;
        sampler.stages = ShaderStage::Fragment;
        sampled.tables[0u].operations = Op::Load | Op::Query | Op::SampleLevel | Op::SampleGrad;
        sampled.tables[0u].samplerTableId = sampled.tables[1u].tableId;
        sampled.tables[1u].operations = Op::None;
        assert(!validateMetalBindlessGraphicsArtifact(sampled).success);
        assert(validateMetalBindlessGraphicsArtifact(sampled, true).success);
        for (const auto dimension :
            {TextureViewDimension::Texture2D, TextureViewDimension::Texture2DArray}) {
            sampled.physicalLayout.bindings[0u].textureViewDimension = dimension;
            assert(validateMetalBindlessGraphicsArtifact(sampled, true).success);
        }
        for (unsigned field = 0u; field < 8u; ++field) {
            auto denied = sampled;
            auto& binding = denied.physicalLayout.bindings[0u];
            if (field == 0u)
                binding.textureViewDimension = TextureViewDimension::TextureCube;
            if (field == 1u)
                binding.depthTexture = true;
            if (field == 2u)
                binding.stages = ShaderStage::Vertex;
            if (field == 3u)
                denied.tables[0u].operations = static_cast<Op>(1u << 7u);
            if (field == 4u)
                denied.tables[0u].operations = Op::SampleCompare;
            if (field == 5u)
                binding.type = DescriptorType::StorageTexture;
            if (field == 6u)
                denied.tables[0u].samplerTableId = 0u;
            if (field == 7u)
                denied.physicalLayout.bindings[1u].comparisonSampler = true;
            assert(!validateMetalBindlessGraphicsArtifact(denied, true).success);
        }
    }
    {
        auto composed = artifact;
        composed.set = 1u;
        composed.pipelineInterfaceContractVersion = 1u;
        composed.snapshotMetadata.stages = composed.faultLatch.stages = ShaderStage::Compute;
        for (auto& binding : composed.physicalLayout.bindings)
            binding.stages = ShaderStage::Compute;
        BindlessPipelineDescriptorDomain frame;
        frame.set = 0u;
        frame.layout.name = "Frame";
        frame.layout.bindings.push_back({3u, DescriptorType::UniformBuffer, ShaderStage::Compute});
        composed.additionalDomains.push_back(frame);
        assert(validateMetalBindlessComputeArtifact(composed).success);
        assert(validateMetalBindlessBufferSlotBudget(composed, 9u).success);
        assert(validateMetalBindlessBufferSlotBudget(composed, 8u).diagnostics
            == "RHI-METAL-BINDLESS-BUFFER-SLOT-BUDGET");
        assert(!validateMetalBindlessGraphicsArtifact(composed).success);
        auto graphicsDomains = artifact;
        graphicsDomains.set = 1u;
        graphicsDomains.pipelineInterfaceContractVersion = 1u;
        frame.layout.bindings.front().stages = graphics;
        graphicsDomains.additionalDomains.push_back(frame);
        assert(validateMetalBindlessGraphicsArtifact(graphicsDomains).success);
        assert(validateMetalBindlessBufferSlotBudget(graphicsDomains, 9u).success);
        assert(!validateMetalBindlessBufferSlotBudget(graphicsDomains, 8u).success);
        {
            auto ordinaryDepth = graphicsDomains;
            BindlessPipelineDescriptorDomain images;
            images.set = 2u;
            images.layout.name = "OrdinaryDepthDeclarations";
            DescriptorBindingDesc image;
            image.type = DescriptorType::SampledTexture;
            image.stages = ShaderStage::Fragment;
            image.depthTexture = true;
            image.textureViewDimensionConstrained = true;
            images.layout.bindings.push_back(image);
            DescriptorBindingDesc sampler;
            sampler.binding = 1u;
            sampler.type = DescriptorType::Sampler;
            sampler.stages = ShaderStage::Fragment;
            sampler.comparisonSampler = true;
            images.layout.bindings.push_back(sampler);
            ordinaryDepth.additionalDomains.push_back(images);
            assert(!validateMetalBindlessGraphicsArtifact(ordinaryDepth).success);
            assert(validateMetalBindlessGraphicsArtifact(ordinaryDepth, true).success);
            MetalRhiNativeFeatureProbe probe;
            assert(validateMetalBindlessOrdinaryDepthPipelinePrerequisites(ordinaryDepth, probe)
                       .diagnostics
                == "RHI-METAL-BINDLESS-ORDINARY-DEPTH-PIPELINE-CAPABILITY");
            assert(validateMetalBindlessOrdinaryDepthPipelinePrerequisites(graphicsDomains, probe)
                    .success);
            probe.sampledDepthTexture = true;
            assert(validateMetalBindlessOrdinaryDepthPipelinePrerequisites(ordinaryDepth, probe)
                    .success);
            for (unsigned mode = 0u; mode < 10u; ++mode) {
                auto invalid = ordinaryDepth;
                auto& binding = invalid.additionalDomains.back().layout.bindings.front();
                if (mode == 0u)
                    binding.stages = ShaderStage::Vertex;
                if (mode == 1u)
                    binding.stages = ShaderStage::Compute;
                if (mode == 2u)
                    binding.stages = graphics;
                if (mode == 3u)
                    binding.textureViewDimension = TextureViewDimension::Texture2DArray;
                if (mode == 4u)
                    binding.arrayCount = 2u;
                if (mode == 5u)
                    binding.dynamicOffset = true;
                if (mode == 6u)
                    binding.tableBacked = true;
                if (mode == 7u)
                    binding.textureViewDimensionConstrained = false;
                if (mode == 8u)
                    binding.type = DescriptorType::StorageTexture;
                if (mode == 9u)
                    invalid.additionalDomains.back().layout.bindings.back().stages =
                        ShaderStage::Vertex;
                assert(!validateMetalBindlessGraphicsArtifact(invalid, true).success);
            }
        }
        {
            auto ordinaryColor = graphicsDomains;
            BindlessPipelineDescriptorDomain images;
            images.set = 2u;
            images.layout.name = "OrdinaryColor";
            DescriptorBindingDesc image;
            image.binding = 0u;
            image.type = DescriptorType::SampledTexture;
            image.stages = graphics;
            image.textureViewDimensionConstrained = true;
            image.textureViewDimension = TextureViewDimension::TextureCube;
            images.layout.bindings.push_back(image);
            DescriptorBindingDesc sampler;
            sampler.binding = 1u;
            sampler.type = DescriptorType::Sampler;
            sampler.stages = graphics;
            images.layout.bindings.push_back(sampler);
            ordinaryColor.additionalDomains.push_back(images);
            assert(!validateMetalBindlessGraphicsArtifact(ordinaryColor).success);
            assert(validateMetalBindlessGraphicsArtifact(ordinaryColor, true).success);
            for (const auto dimension : {TextureViewDimension::Texture2D,
                     TextureViewDimension::Texture2DArray,
                     TextureViewDimension::TextureCube}) {
                auto candidate = ordinaryColor;
                candidate.additionalDomains.back().layout.bindings.front().textureViewDimension =
                    dimension;
                assert(validateMetalBindlessGraphicsArtifact(candidate, true).success);
            }
            for (unsigned mode = 0u; mode < 9u; ++mode) {
                auto invalid = ordinaryColor;
                auto& binding = invalid.additionalDomains.back().layout.bindings.front();
                if (mode == 0u)
                    binding.stages = ShaderStage::Compute;
                if (mode == 1u)
                    binding.stages = ShaderStage::None;
                if (mode == 2u)
                    binding.arrayCount = 2u;
                if (mode == 3u)
                    binding.dynamicOffset = true;
                if (mode == 4u)
                    binding.tableBacked = true;
                if (mode == 5u)
                    binding.depthTexture = true;
                if (mode == 6u)
                    binding.textureViewDimensionConstrained = false;
                if (mode == 7u)
                    binding.textureViewDimension = TextureViewDimension::Texture3D;
                if (mode == 8u)
                    invalid.additionalDomains.back().layout.bindings.back().comparisonSampler =
                        true;
                assert(!validateMetalBindlessGraphicsArtifact(invalid, true).success);
            }
        }
        for (unsigned mode = 0u; mode < 8u; ++mode) {
            auto invalid = graphicsDomains;
            auto& binding = invalid.additionalDomains.front().layout.bindings.front();
            if (mode == 0u)
                binding.stages = ShaderStage::Compute;
            if (mode == 1u)
                binding.stages = ShaderStage::None;
            if (mode == 2u)
                binding.arrayCount = 2u;
            if (mode == 3u)
                binding.dynamicOffset = true;
            if (mode == 4u)
                binding.type = DescriptorType::SampledTexture;
            if (mode == 5u)
                binding.tableBacked = true;
            if (mode == 6u)
                invalid.pipelineInterfaceContractVersion = 0u;
            if (mode == 7u)
                binding.type = DescriptorType::StorageBuffer;
            assert(!validateMetalBindlessGraphicsArtifact(invalid).success);
        }
        for (unsigned mode = 0u; mode < 8u; ++mode) {
            auto invalid = composed;
            auto& binding = invalid.additionalDomains.front().layout.bindings.front();
            if (mode == 0u)
                binding.stages = ShaderStage::Fragment;
            if (mode == 1u)
                binding.arrayCount = 2u;
            if (mode == 2u)
                binding.dynamicOffset = true;
            if (mode == 3u)
                binding.type = DescriptorType::SampledTexture;
            if (mode == 4u)
                binding.type = DescriptorType::Sampler;
            if (mode == 5u)
                binding.type = DescriptorType::StorageTexture;
            if (mode == 6u)
                binding.tableBacked = true;
            if (mode == 7u)
                invalid.pipelineInterfaceContractVersion = 0u;
            assert(!validateMetalBindlessComputeArtifact(invalid).success);
        }
    }
    for (unsigned mode = 0u; mode < 10u; ++mode) {
        auto invalid = artifact;
        if (mode == 0u)
            ++invalid.contractVersion;
        if (mode == 1u)
            invalid.execution = BindlessExecution::Emulated;
        if (mode == 2u)
            invalid.set = 1u;
        if (mode == 3u)
            invalid.otherBindGroups = 1u;
        if (mode == 4u)
            invalid.activePrefixContractVersion = 1u;
        if (mode == 5u)
            invalid.snapshotMetadata.contractVersion = 0u;
        if (mode == 6u)
            invalid.faultReportingContractVersion = 0u;
        if (mode == 7u)
            invalid.faultLatch.stages = ShaderStage::Compute;
        if (mode == 8u)
            invalid.indirectInstanceBaseContractVersion = 1u;
        if (mode == 9u)
            invalid.zeroBaseIndirectProducerContractVersion = 1u;
        assert(validateMetalBindlessGraphicsArtifact(invalid).diagnostics
            == "RHI-METAL-BINDLESS-GRAPHICS-ABI-UNSUPPORTED");
    }
    for (unsigned mode = 0u; mode < 6u; ++mode) {
        auto invalid = artifact;
        if (mode == 0u)
            invalid.physicalLayout.bindings[0].dynamicOffset = true;
        if (mode == 1u)
            invalid.physicalLayout.bindings[0].stages = ShaderStage::Compute;
        if (mode == 2u)
            invalid.physicalLayout.bindings[0].stages = ShaderStage::None;
        if (mode == 3u)
            invalid.physicalLayout.bindings[0].type = DescriptorType::SampledTexture;
        if (mode == 4u)
            invalid.physicalLayout.bindings[0].type = DescriptorType::Sampler;
        if (mode == 5u)
            invalid.physicalLayout.bindings[0].type = DescriptorType::StorageTexture;
        assert(validateMetalBindlessGraphicsArtifact(invalid).diagnostics
            == "RHI-METAL-BINDLESS-GRAPHICS-RESOURCE-UNSUPPORTED");
    }
    auto readableStorage = artifact;
    readableStorage.tables[2].operations = Op::Load;
    assert(validateMetalBindlessGraphicsArtifact(readableStorage).success);
    auto writableStorage = artifact;
    writableStorage.tables[2].operations = Op::Store;
    assert(validateMetalBindlessGraphicsArtifact(writableStorage).success);
    for (unsigned mode = 0u; mode < 4u; ++mode) {
        auto invalid = artifact;
        if (mode == 0u)
            invalid.tables[0].operations = Op::Query;
        if (mode == 1u)
            invalid.tables[1].operations = Op::Store;
        if (mode == 2u)
            invalid.tables[2].operations = Op::SampleLevel;
        if (mode == 3u)
            invalid.physicalLayout.bindings[2].stages = graphics;
        assert(validateMetalBindlessGraphicsArtifact(invalid).diagnostics
            == "RHI-METAL-BINDLESS-GRAPHICS-OPERATION-UNSUPPORTED");
    }
    auto invalid = artifact;
    invalid.reservedPerStage[0].uniformBuffers = 1u;
    assert(validateMetalBindlessGraphicsArtifact(invalid).diagnostics
        == "RHI-METAL-BINDLESS-GRAPHICS-DOMAIN-UNSUPPORTED");
    invalid = artifact;
    invalid.physicalLayout.bindings.push_back({9u, DescriptorType::UniformBuffer, graphics});
    assert(validateMetalBindlessGraphicsArtifact(invalid).diagnostics
        == "RHI-METAL-BINDLESS-GRAPHICS-DOMAIN-UNSUPPORTED");
    invalid = artifact;
    invalid.tables[0].samplerTableId = 2u;
    assert(validateMetalBindlessGraphicsArtifact(invalid).diagnostics
        == "RHI-METAL-BINDLESS-GRAPHICS-DOMAIN-UNSUPPORTED");
    // Vertex-only authored domains have no writable tables; the reserved fault
    // sink is independently valid in Vertex and is not a table-write opt-in.
    artifact.physicalLayout.bindings.erase(artifact.physicalLayout.bindings.begin() + 2u);
    artifact.tables.pop_back();
    artifact.snapshotMetadata.stages = ShaderStage::Vertex;
    artifact.faultLatch.stages = ShaderStage::Vertex;
    for (auto& binding : artifact.physicalLayout.bindings)
        binding.stages = ShaderStage::Vertex;
    assert(validateMetalBindlessGraphicsArtifact(artifact).success);
}

// Canonical owner selection is metadata validation, independent of native
// resource creation. Conflicting or missing fields must never pick a fallback.
void assertCanonicalRegisteredPayloadOwners()
{
    using namespace arp;
    using namespace rhi_metal;
    auto buffer = core::makeShared<MetalRhiNativeBufferPayload>();
    auto texture = core::makeShared<MetalRhiNativeTexturePayload>();
    auto view = core::makeShared<MetalRhiNativeTextureViewPayload>();
    auto sampler = core::makeShared<MetalRhiNativeSamplerPayload>();
    texture->resource.allocationReleaseToken = core::makeShared<unsigned>(1u);
    texture->resource.nativeDevice = core::makeShared<unsigned>(2u);
    texture->aliasHeap = core::makeShared<unsigned>(3u);
    texture->resource.nativeObject = core::makeShared<unsigned>(4u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    MetalRhiRegisteredResource resource;
    assert(!resource.payloadOwner());
    resource.buffer = buffer;
    assert(resource.payloadOwner().get() == buffer.get());
    resource.kind = rhi::DescriptorResourceKind::TextureView;
    assert(!resource.payloadOwner());
    resource.buffer.reset();
    resource.texture = texture;
    assert(resource.payloadOwner().get() == texture.get());
    resource.textureView = view;
    assert(!resource.payloadOwner());
    resource.texture.reset();
    assert(resource.payloadOwner().get() == view.get());
    resource.sampler = sampler;
    assert(!resource.payloadOwner());
    resource.kind = rhi::DescriptorResourceKind::Sampler;
    assert(!resource.payloadOwner());
    resource.textureView.reset();
    assert(resource.payloadOwner().get() == sampler.get());
    resource.buffer = buffer;
    assert(!resource.payloadOwner());
    resource.kind = static_cast<rhi::DescriptorResourceKind>(UINT32_MAX);
    assert(!resource.payloadOwner());
    const auto projected = MetalRhiResourceOwnerProjection::owners(
        rhi::DescriptorResourceKind::TextureView, {}, texture, {}, {});
    assert(projected[0] == texture->resource.allocationReleaseToken
        && projected[1] == texture->resource.nativeDevice && projected[2] == texture->aliasHeap
        && projected[3] == texture->resource.nativeObject && projected[4] == texture);
    const auto invalid = MetalRhiResourceOwnerProjection::owners(
        rhi::DescriptorResourceKind::TextureView, {}, texture, view, {});
    for (const auto& owner : invalid)
        assert(!owner);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
}

void assertOrdinaryPreparationBudgetAndShape()
{
    using namespace arp;
    using namespace rhi;
    using namespace rhi_metal;
    assert(MetalRhiOrdinaryGraphicsResourceDomain::allowsDescriptor(
        DescriptorType::ReadOnlyStorageBuffer, 1u, false));
    assert(MetalRhiOrdinaryGraphicsResourceDomain::allowsDescriptor(
        DescriptorType::ReadOnlyStorageBuffer, 2u, false));
    assert(MetalRhiOrdinaryGraphicsResourceDomain::allowsDescriptor(
        DescriptorType::UniformBuffer, 2u, false));
    assert(!MetalRhiOrdinaryGraphicsResourceDomain::allowsDescriptor(
        DescriptorType::StorageBuffer, 1u, false));
    assert(!MetalRhiOrdinaryGraphicsResourceDomain::allowsDescriptor(
        DescriptorType::StorageTexture, 1u, false));
    assert(!MetalRhiOrdinaryGraphicsResourceDomain::allowsDescriptor(
        DescriptorType::SampledTexture, 1u, true));
    static_assert(!std::is_copy_constructible_v<MetalRhiOrdinaryPreparedComputePush>);
    static_assert(!std::is_copy_assignable_v<MetalRhiOrdinaryPreparedComputePush>);
    static_assert(!std::is_default_constructible_v<MetalRhiOrdinaryPreparedComputePush>);
    static_assert(!std::is_default_constructible_v<MetalRhiOrdinaryPreparedGraphicsPush>);
    static_assert(!std::is_copy_constructible_v<MetalRhiOrdinaryPreparedGraphicsPush>);
    {
        // Pure CPU identity probe; these objects never prove native execution.
        MetalRhiNativeDescriptorGroupPayload group;
        auto buffer = core::makeShared<MetalRhiNativeBufferPayload>();
        buffer->resource.nativeDevice = core::makeShared<unsigned>(1u);
        buffer->resource.nativeObject = core::makeShared<unsigned>(2u);
        buffer->resource.allocationReleaseToken = core::makeShared<unsigned>(3u);
        MetalRhiNativeArgumentBindingPayload binding;
        binding.resourceKind = DescriptorResourceKind::Buffer;
        binding.buffer = buffer;
        group.bindings.push_back(binding);
        assert(MetalRhiPreparedDescriptorIdentityValidator::validate(group));
        assert(!MetalRhiPreparedDescriptorIdentityValidator::capture(group));
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!MetalRhiPreparedDescriptorIdentityValidator::validate(group));
        auto object = buffer->resource.nativeObject;
        buffer->resource.nativeObject =
            std::shared_ptr<void>(buffer->resource.allocationReleaseToken, object.get());
        assert(MetalRhiPreparedDescriptorIdentityValidator::validate(group));
        buffer->resource.nativeObject = object;
        assert(!MetalRhiPreparedDescriptorIdentityValidator::validate(group));
        buffer->resource.nativeObject.reset();
        assert(MetalRhiPreparedDescriptorIdentityValidator::capture(group));
        buffer->resource.nativeObject = object;
        group.bindings[0].resourceKind = DescriptorResourceKind::Sampler;
        assert(MetalRhiPreparedDescriptorIdentityValidator::capture(group));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
    {
        // CPU contract only; no synthetic native facts authorize a GPU payload.
        PipelineLayoutDesc source;
        source.pushConstantRanges = {{ShaderStage::Vertex | ShaderStage::Fragment, 0u, 32u}};
        ShaderReflectionDesc reflection;
        reflection.pushConstantContractVersion = "AtomicFullBlockPush/1";
        reflection.pushConstantStages = "VS|FS";
        reflection.pushConstantByteCount = 32u;
        DescriptorConsumptionContract owned(source, reflection);
        source.pushConstantRanges.clear();
        const auto check = [&](ShaderStage stages, std::uint32_t offset, std::uint32_t bytes) {
            return semantics::validatePushConstantBinding(owned.pushRanges(),
                ShaderStage::Vertex | ShaderStage::Fragment,
                stages,
                offset,
                bytes,
                &reflection);
        };
        assert(check(ShaderStage::Vertex | ShaderStage::Fragment, 0u, 32u).success);
        assert(!check(ShaderStage::Compute, 0u, 32u).success);
        assert(!check(ShaderStage::Vertex, 16u, 16u).success);
        assert(!check(ShaderStage::Vertex, 0u, 16u).success);
        reflection.pushConstantStages = "Vertex|Fragment";
        assert(!check(ShaderStage::Vertex, 0u, 32u).success);
    }
    rhi::semantics::PushConstantConsumptionState pushState;
    assert(!pushState.hasUnconsumedUpdate());
    pushState.onPush();
    assert(pushState.hasUnconsumedUpdate());
    pushState.onConsume();
    assert(!pushState.hasUnconsumedUpdate());
    pushState.onPush();
    pushState.onPassEnd();
    assert(!pushState.hasUnconsumedUpdate());
    auto quota = core::makeShared<core::CpuMemoryQuota>(4096u);
    MetalRhiOrdinaryBindingBudget budget{1u, true, 4096u, 0u, 0u};
    assert(!budget.validate(quota));
    {
        std::array<std::uint32_t, 8> source{1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
        const auto before = quota->liveBytes();
        {
            core::CpuMemoryQuotaScope scope(quota);
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            MetalRhiOrdinaryPushBlock block(
                {ShaderStage::Vertex | ShaderStage::Fragment, 0u, sizeof(source), source.data()},
                budget,
                quota);
            std::pmr::set_default_resource(previous);
            assert(block.validate().success && quota->liveBytes() > before);
            source.fill(0u);
            assert(block.bytes()[0] == 1u && block.bytes().size() == sizeof(source));
        }
        assert(quota->liveBytes() == before);
    }
    assert(budget.validate({}));
    for (const auto bytes : {0u, 4095u}) {
        auto other = core::makeShared<core::CpuMemoryQuota>(bytes);
        assert(budget.validate(other));
    }
    quota->setLimitBytes(4095u);
    assert(budget.validate(quota));
    quota->setLimitBytes(4096u);
    for (const auto invalid : {MetalRhiOrdinaryBindingBudget{0u, true, 4096u, 0u, 0u},
             MetalRhiOrdinaryBindingBudget{1u, false, 4096u, 0u, 0u},
             MetalRhiOrdinaryBindingBudget{1u, true, 0u, 0u, 0u},
             MetalRhiOrdinaryBindingBudget{1u, true, 4096u, 1u, 0u},
             MetalRhiOrdinaryBindingBudget{1u, true, 4096u, 0u, 1u}})
        assert(invalid.validate(quota));
    budget.maxArgumentBuffers = 1u;
    budget.maxArgumentBufferBytes = 1u;
    assert(!budget.validate(quota));
    DescriptorBindingDesc expected{7u, DescriptorType::SampledTexture, ShaderStage::Compute, 3u};
    auto actual = expected;
    assert(descriptorBindingLayoutMatches(expected, actual));
    actual.stages = ShaderStage::Compute | ShaderStage::Fragment;
    assert(descriptorBindingLayoutMatches(expected, actual));
    actual.stages = ShaderStage::Fragment;
    assert(!descriptorBindingLayoutMatches(expected, actual));
    for (const auto flag : {&DescriptorBindingDesc::tableBacked,
             &DescriptorBindingDesc::dynamicOffset,
             &DescriptorBindingDesc::textureViewDimensionConstrained,
             &DescriptorBindingDesc::depthTexture,
             &DescriptorBindingDesc::sampledTextureFilterable,
             &DescriptorBindingDesc::comparisonSampler}) {
        actual = expected;
        actual.*flag = !(expected.*flag);
        assert(!descriptorBindingLayoutMatches(expected, actual));
    }
    actual = expected;
    actual.binding++;
    assert(!descriptorBindingLayoutMatches(expected, actual));
    actual = expected;
    actual.arrayCount--;
    assert(!descriptorBindingLayoutMatches(expected, actual));
    actual = expected;
    actual.type = DescriptorType::StorageTexture;
    assert(!descriptorBindingLayoutMatches(expected, actual));
    actual = expected;
    actual.textureViewDimension = TextureViewDimension::TextureCube;
    assert(!descriptorBindingLayoutMatches(expected, actual));
    actual = expected;
    actual.storageTextureFormat = TextureFormat::Rgba16Float;
    assert(!descriptorBindingLayoutMatches(expected, actual));
    actual = expected;
    actual.storageTextureAccess = StorageTextureAccess::ReadOnly;
    assert(!descriptorBindingLayoutMatches(expected, actual));
    // CPU range contracts supply explicit metadata fixtures; these assertions
    // grant no native capability. Actual reflected facts are tested separately.
    core::pmr::Vector<MetalRhiBufferBindingFact> facts{{0u, 16u, 96u}, {1u, 16u, 272u}};
    const MetalRhiBufferBindingFact* fact = nullptr;
    assert(!validateMetalRhiBufferOffset(facts, 1u, 0u, 192u, 288u, fact) && fact == &facts[0]);
    assert(validateMetalRhiBufferOffset(facts, 0u, 0u, 0u, 288u, fact) && !fact);
    assert(validateMetalRhiBufferOffset(facts, 1u, 2u, 0u, 288u, fact) && !fact);
    assert(validateMetalRhiBufferOffset(facts, 1u, 0u, 193u, 288u, fact));
    assert(validateMetalRhiBufferOffset(facts, 1u, 0u, 208u, 288u, fact));
    assert(validateMetalRhiBufferOffset(facts, 1u, 0u, 288u, 288u, fact));
    assert(validateMetalRhiBufferOffset(facts, 1u, 0u, 0u, 0u, fact));
    assert(validateMetalRhiBufferOffset(facts, 1u, 0u, UINT64_MAX - 15u, UINT64_MAX, fact));
    facts[0].requiredBytes = 0u;
    assert(validateMetalRhiBufferOffset(facts, 1u, 0u, 0u, 288u, fact));
    facts[0].requiredBytes = 96u;
    facts[0].alignment = 0u;
    assert(validateMetalRhiBufferOffset(facts, 1u, 0u, 0u, 288u, fact));
    facts[0].alignment = 16u;
    facts.push_back(facts[0]);
    assert(validateMetalRhiBufferOffset(facts, 1u, 0u, 0u, 288u, fact) && !fact);
    facts.pop_back();
    MetalRhiStageBufferBindingFacts vertexFacts;
    vertexFacts.stage = ShaderStage::Vertex;
    vertexFacts.version = 1u;
    vertexFacts.entries = facts;
    MetalRhiStageBufferBindingFacts fragmentFacts;
    fragmentFacts.stage = ShaderStage::Fragment;
    fragmentFacts.version = 1u;
    fragmentFacts.entries = {{3u, 32u, 128u}};
    MetalRhiStageBufferRange vertexRange, fragmentRange;
    assert(!MetalRhiStageBufferRangeValidator::capture(
        vertexFacts, ShaderStage::Vertex, 0u, 192u, 320u, vertexRange));
    assert(!MetalRhiStageBufferRangeValidator::capture(
        fragmentFacts, ShaderStage::Fragment, 3u, 192u, 320u, fragmentRange));
    assert(vertexRange.fact.requiredBytes == 96u && fragmentRange.fact.requiredBytes == 128u);
    assert(!MetalRhiStageBufferRangeValidator::validate(
        fragmentFacts, ShaderStage::Fragment, 3u, 192u, 320u, fragmentRange));
    assert(MetalRhiStageBufferRangeValidator::validate(
        fragmentFacts, ShaderStage::Vertex, 3u, 192u, 320u, fragmentRange));
    assert(MetalRhiStageBufferRangeValidator::validate(
        fragmentFacts, ShaderStage::Fragment, 3u, 160u, 320u, fragmentRange));
    assert(MetalRhiStageBufferRangeValidator::validate(
        fragmentFacts, ShaderStage::Fragment, 3u, 192u, 320u, vertexRange));
    fragmentFacts.version = 0u;
    assert(MetalRhiStageBufferRangeValidator::validate(
        fragmentFacts, ShaderStage::Fragment, 3u, 192u, 320u, fragmentRange));
    fragmentFacts.version = 1u;
    fragmentFacts.entries[0].requiredBytes = 0u;
    assert(MetalRhiStageBufferRangeValidator::validate(
        fragmentFacts, ShaderStage::Fragment, 3u, 192u, 320u, fragmentRange));
    fragmentFacts.entries[0].requiredBytes = 128u;
    fragmentFacts.entries.push_back(fragmentFacts.entries[0]);
    assert(MetalRhiStageBufferRangeValidator::validate(
        fragmentFacts, ShaderStage::Fragment, 3u, 192u, 320u, fragmentRange));
    fragmentFacts.entries.pop_back();
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned repeat = 0u; repeat < 32u; ++repeat)
        assert(!validateMetalRhiBufferOffset(facts, 1u, 1u, 544u, 816u, fact));
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    MetalRhiNativeArgumentBindingPayload payload;
    payload.resourceKind = DescriptorResourceKind::Buffer;
    payload.buffer = core::makeShared<MetalRhiNativeBufferPayload>();
    assert(payload.payloadOwner() == payload.buffer);
    payload.sampler = core::makeShared<MetalRhiNativeSamplerPayload>();
    assert(!payload.payloadOwner());
    // CPU ownership fixtures only: no object below is an executable Metal payload.
    MetalRhiNativeResourcePayload identity;
    identity.nativeDevice = core::makeShared<unsigned>(1u);
    identity.nativeObject = core::makeShared<unsigned>(2u);
    identity.allocationReleaseToken = core::makeShared<unsigned>(3u);
    MetalRhiNativeResourceObservation observation(identity);
    assert(observation.matches(identity));
    const auto originalObject = identity.nativeObject;
    auto foreignOwner = core::makeShared<unsigned>(4u);
    identity.nativeObject = std::shared_ptr<void>(foreignOwner, originalObject.get());
    assert(!observation.matches(identity));
    identity.nativeObject = originalObject;
    assert(observation.matches(identity));
    const auto originalAllocation = identity.allocationReleaseToken;
    identity.allocationReleaseToken = foreignOwner;
    assert(!observation.matches(identity));
    identity.allocationReleaseToken = originalAllocation;

    struct AccountingReleaseProbe {
        AccountingReleaseProbe(std::weak_ptr<void> token, bool& destroyed)
            : token(std::move(token)), destroyed(destroyed)
        {}

        ~AccountingReleaseProbe()
        {
            assert(!token.expired());
            destroyed = true;
        }

        std::weak_ptr<void> token;
        bool& destroyed;
    };

    bool destroyed = false;
    {
        auto token = core::makeShared<unsigned>(5u);
        MetalRhiNativeResourcePayload ordered;
        ordered.nativeDevice = identity.nativeDevice;
        ordered.allocationReleaseToken = token;
        ordered.nativeObject =
            core::makeShared<AccountingReleaseProbe>(std::weak_ptr<void>(token), destroyed);
        MetalRhiNativeResourceObservation retained(ordered);
        ordered.nativeObject.reset();
        ordered.nativeDevice.reset();
        ordered.allocationReleaseToken.reset();
        token.reset();
        assert(!destroyed);
    }
    assert(destroyed);
}

int main()
{
    if (!arp::rhi_metal::describeMetalRhiNativeRuntimeBoundary().runtimeAvailableOnThisPlatform) {
        arp::rhi_metal::MetalRhiDeviceDesc host;
        host.capabilityContract =
            arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Metal);
        host.capabilityContract.queriedFromNativeDevice = true;
        host.capabilityContract.queriedExtendedFeatures = true;
        host.memoryBudgetBytes = 1024u * 1024u;
        host.uploadStagingPageSizeBytes = 4096u;
        host.uploadStagingMaxPageCount = 1u;
        assert(arp::rhi_metal::describeMetalRhiDevice(host).success);
        assert(!arp::rhi_metal::createMetalRhiDevice(host));
        for (const auto version : {0u, 2u, 0xffffffffu})
            assert(!arp::rhi_metal::createMetalRhiHostDiagnosticDevice(host, version));
        auto fixture = arp::rhi_metal::createMetalRhiHostDiagnosticDevice(host, 1u);
        assert(fixture && !fixture->capabilities().core.graphics);
        auto invalid = host;
        invalid.uploadStagingMaxPageCount = 0u;
        assert(!arp::rhi_metal::createMetalRhiHostDiagnosticDevice(invalid, 1u));
    }
    {
        using namespace arp;
        using namespace arp::rhi_metal;
        MetalRhiAllocator allocator({"CPU failure atomicity", 1024u, true});
        auto retained = allocator.allocate({"retained", 128u, rhi::MemoryUsage::GpuOnly});
        assert(retained.success);
        const auto before = allocator.memoryStats();
        auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        const MetalRhiAllocationDesc candidate{"candidate", 896u, rhi::MemoryUsage::GpuOnly};
        bool rejected = false;
        {
            core::CpuMemoryQuotaScope scope(quota);
            try {
                (void)allocator.allocate(candidate);
            } catch (const std::bad_alloc&) {
                rejected = true;
            }
        }
        const auto after = allocator.memoryStats();
        assert(rejected && !quota->liveBytes());
        assert(after.allocatedBytes == before.allocatedBytes
            && after.liveResourceCount == before.liveResourceCount
            && after.peakAllocatedBytes == before.peakAllocatedBytes
            && after.budgetRejectionCount == before.budgetRejectionCount);
        auto retry = allocator.allocate(candidate);
        assert(retry.success && allocator.memoryStats().allocatedBytes == 1024u);
        retry.releaseToken.reset();
        assert(allocator.memoryStats().allocatedBytes == 128u);
        retained.releaseToken.reset();
        assert(
            !allocator.memoryStats().allocatedBytes && !allocator.memoryStats().liveResourceCount);
    }
    {
        using namespace arp::rhi_metal;
        MetalRhiAllocator allocator({"observed reservation contract", 1024u, true});
        MetalRhiAllocator foreign({"foreign reservation contract", 1024u, true});
        auto allocation =
            allocator.allocate({"native candidate", 1024u, arp::rhi::MemoryUsage::GpuOnly});
        assert(allocation.success);
        auto stale = allocation;
        assert(!foreign.shrinkReservation(allocation, 300u));
        assert(!allocator.shrinkReservation(allocation, 1025u));
        assert(!allocator.shrinkReservation(allocation, 0u));
        assert(allocator.memoryStats().allocatedBytes == 1024u);
        const auto count = arp::core::defaultCpuAllocator().stats().allocationCount;
        assert(allocator.shrinkReservation(allocation, 300u));
        assert(arp::core::defaultCpuAllocator().stats().allocationCount == count);
        assert(!allocator.shrinkReservation(stale, 200u));
        assert(allocation.sizeBytes == 300u && allocator.memoryStats().allocatedBytes == 300u
            && allocator.memoryStats().liveResourceCount == 1u);
        allocation.releaseToken.reset();
        assert(allocator.memoryStats().allocatedBytes == 300u);
        stale.releaseToken.reset();
        assert(
            !allocator.memoryStats().allocatedBytes && !allocator.memoryStats().liveResourceCount);
    }
    assertCanonicalRegisteredPayloadOwners();
    assertOrdinaryPreparationBudgetAndShape();
    using namespace arp::rhi;
    using namespace arp::rhi_metal;
    static const std::uint8_t kPushData[256] = {};

    // Source and stub contracts are mandatory on every platform. Runtime
    // assertions below are skipped only after these deterministic checks.
    assertMetalRhiBackendDoesNotDelegateToNull();
    assertMetalRhiAppleApiBoundary();
    assertMetalRhiSwapchainUsesPrivateNativePayloads();
    assertMetalRhiMinGwDiagnosticOnlyGuardrails();
    assertMetalRhiAppleRuntimeSmokeTelemetryExists();
    assertMetalDefaultTextureTransitionInfersFormatAspect();
    assertMetalRhiQueryAndProbeContracts();
    assertMetalDdgiComputeContract();
    assertMetalGraphicsBufferAdmission();
    assertMetalNativeAliasingAndCompletionContract();
    assertMetalGpuCountedAndPatchListContracts();
    validatePreparedComputeLayout();
    validatePreparedGraphicsLayouts();

    // Remaining checks require a real Metal backend with Apple API support.
    {
        MetalRhiDeviceDesc probe;
        probe.label = "MetalAvailabilityProbe";
        const auto report = describeMetalRhiDevice(probe);
        if (!report.success) {
            return 0;
        }
    }

    MetalRhiDeviceDesc desc;
    desc.label = "RHIMetalVerticalSlice";
    desc.capabilityContract = makeQueriedMetalContract();
    desc.memoryBudgetBytes = 64u * 1024u * 1024u;
    desc.uploadStagingPageSizeBytes = 4u * 1024u * 1024u;
    desc.uploadStagingMaxPageCount = 4u;
    desc.enableArgumentBuffers = true;
    desc.enablePushConstantUploadBuffer = true;
    desc.enableMemorylessTransientTextures = true;
    desc.enableDrawableLifecycleGuards = true;
    desc.allowNativeHandleExposureToUpperLayers = false;
    desc.allowBlockingDrawableAcquire = false;
    desc.allowPerDrawPipelineCreation = false;
    desc.allowPerFrameShaderCompilation = false;

    const auto report = describeMetalRhiDevice(desc);
    if (!report.success) {
        // Metal is not available on this platform (stub/MinGW environment).
        // The test requires a real Metal backend with Apple API support.
        return 0;
    }
    assert(report.success);
    assert(report.backend == Backend::Metal);
    // Argument-buffer availability is a queried native fact; this contract
    // test must not assume every Metal device exposes the probe substrate.
    assert(!report.nativeHandleExposedToUpperLayer);
    assert(report.argumentBindingsHiddenFromPublicApi);
    assert(!report.pushConstantUploadBufferEmulated);
    assert(report.memoryIntentMapped);
    assert(report.memoryBudgetTelemetrySanitized);
    assert(report.drawableLifecycleGuarded);
    assert(report.boundedDrawableSynchronization);
    assert(report.queueSubmitSupported);
    assert(report.swapchainDrawableLifecycleSupported);
    assert(!report.nullDelegateUsed);
    assert(report.backendOwnedDevice);
    assert(report.backendOwnedResourceObjects);
    assert(report.backendOwnedDescriptorObjects);
    assert(report.backendOwnedPipelineObjects);
    assert(report.backendOwnedCommandObjects);
    assert(report.backendOwnedQueueSubmit);
    assert(report.backendOwnedSwapchain);
    assert(report.backendOwnedUploadStagingSupported);
    assert(report.uploadStagingNativeHandlesHidden);
    assert(report.uploadStagingBounded);
    assert(report.nativeResourcePayloadsPrivate);
    assert(report.nativeBufferCreationPathPresent);
    assert(report.nativeTextureCreationPathPresent);
    assert(report.nativeTextureViewCreationPathPresent);
    assert(report.nativeSamplerCreationPathPresent);
    assert(report.privateStorageIntentMapped);
    assert(report.sharedStorageIntentMapped);
    assert(report.managedStorageIntentMapped);
    assert(report.memorylessStorageIntentMapped);
    const auto runtimeBoundaryForReport = describeMetalRhiNativeRuntimeBoundary();
    assert(report.nonAppleNativeResourceRuntimeDiagnosticOnly
        == !runtimeBoundaryForReport.runtimeAvailableOnThisPlatform);
    assert(report.nativeArgumentBindingPayloadsPrivate);
    assert(report.nativeArgumentBufferCreationPathPresent == report.argumentBuffersIntegrated);
    assert(report.uniformBufferArgumentBindingSupported);
    assert(report.storageBufferArgumentBindingSupported);
    assert(report.sampledTextureArgumentBindingSupported);
    assert(report.storageTextureArgumentBindingSupported);
    assert(report.samplerArgumentBindingSupported);
    assert(report.nonAppleNativeDescriptorRuntimeDiagnosticOnly
        == !runtimeBoundaryForReport.runtimeAvailableOnThisPlatform);
    assert(report.nativeShaderModuleCreationPathPresent);
    assert(report.nativeRenderPipelineCreationPathPresent);
    assert(report.nativeComputePipelineCreationPathPresent);
    assert(report.nonAppleNativePipelineRuntimeDiagnosticOnly
        == !runtimeBoundaryForReport.runtimeAvailableOnThisPlatform);
    assert(report.nativeCommandBufferCreationPathPresent);
    assert(report.nativeRenderEncoderPathPresent);
    assert(report.nativeComputeEncoderPathPresent);
    assert(report.nativeBlitEncoderPathPresent);
    assert(report.nativeDescriptorArgumentBindingRecordPathPresent);
    assert(report.nativePushConstantUploadBufferPathPresent);
    assert(report.nativeQueueSubmitPathPresent);
    assert(report.boundedCompletionTelemetryPresent);
    assert(report.nonAppleNativeCommandRuntimeDiagnosticOnly
        == !runtimeBoundaryForReport.runtimeAvailableOnThisPlatform);
    assert(report.nativeDrawableLayerCreationPathPresent);
    assert(report.nativeDrawableAcquirePathPresent);
    assert(report.nativeDrawablePresentPathPresent);
    assert(report.nativeDrawableResizePathPresent);
    assert(report.nativeDrawableLayerLossRecreatePathPresent);
    assert(report.nativeDrawableBackgroundForegroundPathPresent);
    assert(report.nativeDrawableMemoryWarningPathPresent);
    assert(report.nativeDrawableTwoFrameSlotsPresent);
    assert(report.nonAppleNativeSwapchainRuntimeDiagnosticOnly
        == !runtimeBoundaryForReport.runtimeAvailableOnThisPlatform);
#if defined(__MINGW32__) || defined(__MINGW64__)
    assert(report.windowsMingwNativeRuntimeDiagnosticOnly);
    assert(report.windowsMingwNativeExecutionSuppressed);
    assert(report.windowsMingwSourceGuardrailsEnforced);
    assert(report.windowsMingwNoNullDelegateGuardrailEnforced);
#endif
    assert(!report.nativeHandleExposedToUpperLayer);
    assert(!report.perDrawPipelineCreationAllowed);
    assert(!report.perFrameShaderCompilationAllowed);

    MetalRhiAppleRuntimeSmokeReport emptySmokeReport;
    assert(!emptySmokeReport.publicDevicePathExercised);
    assert(!emptySmokeReport.nativeResourceStatsObserved);
    assert(!emptySmokeReport.nativeCommandStatsObserved);
    assert(!emptySmokeReport.nativeQueueSubmitObserved);
    assert(!emptySmokeReport.nativeDrawablePresentObserved);

    MetalRhiAppleRuntimeSmokeDesc smokeDesc;
    smokeDesc.deviceDesc = desc;
    smokeDesc.extent = {320, 180};
    smokeDesc.colorFormat = TextureFormat::Bgra8Unorm;
    const auto smokeReport = runMetalRhiAppleRuntimeSmoke(smokeDesc);
    assert(smokeReport.runtimeAvailable == runtimeBoundaryForReport.runtimeAvailableOnThisPlatform);
#if defined(__MINGW32__) || defined(__MINGW64__)
    assert(!smokeReport.success);
    assert(smokeReport.diagnosticOnly);
    assert(!smokeReport.publicDevicePathExercised);
    assert(!smokeReport.nativeResourceStatsObserved);
    assert(!smokeReport.nativeCommandStatsObserved);
    assert(!smokeReport.nativeQueueSubmitObserved);
    assert(!smokeReport.nativeDrawablePresentObserved);
#endif

    if (!runtimeBoundaryForReport.runtimeAvailableOnThisPlatform) {
        assert(!arp::rhi_metal::createMetalRhiDevice(desc));
    }
    auto device = arp::tests::createMetalContractDevice(desc);
    assert(device);
    auto originalFence = device->createFence();
    assert(originalFence && !originalFence->isSignaled());
    bool hostSignalRejected = false;
    try {
        originalFence->signal();
    } catch (const std::logic_error& e) {
        hostSignalRejected =
            std::string_view(e.what()) == "RHI-METAL-FENCE-HOST-SIGNAL-UNAUTHENTICATED";
    }
    assert(hostSignalRejected && !originalFence->isSignaled());
    originalFence->reset();

    assert(device);
    {
        auto unsupported = arp::tests::createMetalContractDevice(desc);
        assert(unsupported);
        arp::tests::OriginalHeapRegistrationFixture::verifyUnavailable(
            *unsupported, "RHI-ORIGINAL-HEAP-METAL-ISSUER-UNAVAILABLE");
    }
    {
        TextureDesc placement;
        placement.extent = {4u, 4u, 1u};
        placement.format = TextureFormat::Rgba8Unorm;
        placement.usage = TextureUsage::Sampled;
        placement.memory = MemoryUsage::GpuToCpu;
        const auto invalid = device->queryTexturePlacementRequirements(placement);
        assert(!invalid.sizeBytes && !invalid.alignmentBytes);
        assert(!device->createMemoryHeap(1024u, 256u, MemoryUsage::CpuToGpu));
        assert(device->lastMemoryHeapCreationDiagnostic() == "RHI-HEAP-MEMORY-INTENT");
#if defined(__MINGW32__) || defined(__MINGW64__)
        placement.memory = MemoryUsage::GpuOnly;
        const auto unavailable = device->queryTexturePlacementRequirements(placement);
        assert(!unavailable.sizeBytes && !unavailable.alignmentBytes);
#endif
    }

    arp::test::OriginalCommandSamplerCustodyFixture::verify([&]() {
        return arp::tests::createMetalContractDevice(desc);
    });
    arp::test::OriginalBufferStateFixture::verify(
        [&]() {
            return arp::tests::createMetalContractDevice(desc);
        },
        [](GpuBuffer& buffer,
            ResourceState before,
            ResourceState after,
            arp::core::MutationJournal* journal) {
            return setMetalRhiBufferStateForBarrier(
                buffer, {buffer.id(), {}, before, after}, journal);
        });
    arp::test::OriginalTextureStateFixture::verify(
        [&]() {
            return arp::tests::createMetalContractDevice(desc);
        },
        [](GpuTexture& texture,
            const TextureSubresourceRange& range,
            ResourceState before,
            ResourceState after,
            arp::core::MutationJournal* journal) {
            return setMetalRhiTextureStateForBarrier(
                texture, {texture.id(), range, before, after}, journal);
        },
        true);
    arp::test::DepthStencilAttachmentInputFixture::verifyContract();
    assert(device);
    arp::test::DepthStencilAttachmentInputFixture::verifyPublicGate(*device);
    assert(device);
    assert(device->backend() == Backend::Metal);
    auto* bindless =
        static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
    assert(bindless);
    {
        MetalRhiNativeFeatureProbe probe;
        probe.argumentBuffersTier2 = true;
        probe.nativeSync = true;
        probe.maxStorageBufferBindingsPerStage = 31u;
        probe.maxStorageBufferBindingSizeBytes = 65536u;
        probe.maxTexture2DDimension = 16384u;
        probe.maxTexture3DDimension = 2048u;
        probe.maxTextureArrayLayers = 2048u;
        const auto enabled = metalBindlessComputeCapabilities(probe, true, true, 123u);
        assert(enabled.execution == BindlessExecution::Native && enabled.limitsQueried);
        assert(enabled.preparedComputeBindingContractVersion == 1u);
        assert(!enabled.originalNativeColorComputeExecutionContractVersion);
        assert(enabled.preparedGraphicsBindingContractVersion == 0u
            && enabled.preparedGraphicsPassFaultScopeContractVersion == 0u);
        assert(!metalBindlessGraphicsExecutionAvailable(enabled));
        assert(enabled.maxStorageBufferBindingSize == probe.maxStorageBufferBindingSizeBytes);
        assert(enabled.workloadResourceLimits.contractVersion == 1u
            && enabled.workloadResourceLimits.maxBufferBytes
                == probe.maxStorageBufferBindingSizeBytes
            && enabled.workloadResourceLimits.texture2DDimension == probe.maxTexture2DDimension
            && enabled.workloadResourceLimits.texture3DDimension == probe.maxTexture3DDimension
            && enabled.workloadResourceLimits.textureArrayLayers == probe.maxTextureArrayLayers
            && !enabled.workloadResourceLimits.uniformOffsetAlignment
            && !enabled.workloadResourceLimits.storageOffsetAlignment);
        auto unknownShape = probe;
        unknownShape.maxTexture2DDimension = unknownShape.maxTexture3DDimension =
            unknownShape.maxTextureArrayLayers = 0u;
        const auto unknownFacts = metalBindlessComputeCapabilities(unknownShape, true, true, 123u);
        assert(!unknownFacts.workloadResourceLimits.texture2DDimension
            && !unknownFacts.workloadResourceLimits.texture3DDimension
            && !unknownFacts.workloadResourceLimits.textureArrayLayers);
        assert(enabled.support[2u][5u].nonUniform);
        for (unsigned stage = 0u; stage < 2u; ++stage)
            for (const auto& resource : enabled.support[stage])
                assert(resource.operations == BindlessOperation::None && !resource.nonUniform);
        for (unsigned resource = 0u; resource < 3u; ++resource)
            assert(enabled.support[2u][resource].operations == BindlessOperation::None);
        assert(!enabled.combinedSampledStorageTextureLimits[2u]);
        const auto graphics =
            metalBindlessComputeCapabilities(probe, false, true, 123u, false, true);
        assert(metalBindlessGraphicsExecutionAvailable(graphics));
        assert(graphics.preparedGraphicsBindingContractVersion == 1u
            && graphics.preparedGraphicsPassFaultScopeContractVersion == 1u);
        assert(graphics.preparedComputeBindingContractVersion == 0u);
        assert(graphics.originalGraphicsExecutionContractVersion == 0u);
        assert(graphics.support[0u][3u].operations == BindlessOperation::Load);
        assert(graphics.support[1u][4u].operations
            == (BindlessOperation::Load | BindlessOperation::Query));
        assert(graphics.support[1u][5u].operations
            == (BindlessOperation::Load | BindlessOperation::Query | BindlessOperation::Atomic));
        assert(graphics.maxBindGroups == probe.maxStorageBufferBindingsPerStage);
        for (std::size_t kind = 0u; kind < kBindlessResourceClasses; ++kind)
            assert(graphics.ordinaryDescriptorDomains[kind] == (kind >= 3u));
        assert(graphics.stageLimits[0u].storageBuffers == probe.maxStorageBufferBindingsPerStage
            && graphics.stageLimits[1u].uniformBuffers == probe.maxStorageBufferBindingsPerStage);
        assert(graphics.support[0u][5u].operations == BindlessOperation::None
            && !graphics.support[0u][5u].nonUniform);
        for (unsigned stage = 0u; stage < 3u; ++stage)
            for (unsigned resource = 0u; resource < 6u; ++resource)
                if (stage == 2u || resource < 3u)
                    assert(graphics.support[stage][resource].operations == BindlessOperation::None
                        && !graphics.support[stage][resource].nonUniform);
        for (unsigned field = 0u; field < 6u; ++field) {
            auto missing = probe;
            if (field == 0u)
                missing.argumentBuffersTier2 = false;
            if (field == 1u)
                missing.nativeSync = false;
            if (field == 2u)
                missing.maxStorageBufferBindingsPerStage = 0u;
            if (field == 3u)
                missing.maxStorageBufferBindingSizeBytes = 0u;
            const auto closed = metalBindlessComputeCapabilities(
                missing, false, field != 4u, field == 5u ? 0u : 123u, false, true);
            assert(!closed.executablePayload && !metalBindlessGraphicsExecutionAvailable(closed));
        }
        probe.maxTextureBindingsPerStage = 128u;
        probe.maxSamplerBindingsPerStage = 16u;
        probe.r32FloatReadWriteTexture = true;
        const auto sampledGraphics =
            metalBindlessComputeCapabilities(probe, false, true, 123u, false, true, true, true);
        assert(sampledGraphics.support[1u][0u].operations
            == (BindlessOperation::Load | BindlessOperation::Query | BindlessOperation::SampleLevel
                | BindlessOperation::SampleGrad));
        assert(sampledGraphics.support[1u][1u].nonUniform
            && sampledGraphics.support[1u][0u].nonUniform);
        assert(!sampledGraphics.preparedComputeBindingContractVersion);
        assert(sampledGraphics.stageLimits[1u].sampledTextures == probe.maxTextureBindingsPerStage
            && sampledGraphics.stageLimits[1u].samplers == probe.maxSamplerBindingsPerStage);
        assert(sampledGraphics.ordinaryDescriptorDomains[0u]
            && sampledGraphics.ordinaryDescriptorDomains[1u]
            && !sampledGraphics.ordinaryDescriptorDomains[2u]
            && sampledGraphics.stageLimits[0u].sampledTextures == probe.maxTextureBindingsPerStage
            && sampledGraphics.stageLimits[0u].samplers == probe.maxSamplerBindingsPerStage
            && sampledGraphics.support[0u][0u].operations == BindlessOperation::None
            && sampledGraphics.support[0u][1u].operations == BindlessOperation::None);
        assert(!sampledGraphics.support[0u][0u].nonUniform
            && !sampledGraphics.support[1u][2u].nonUniform);
        for (unsigned field = 0u; field < 4u; ++field) {
            auto missing = probe;
            if (!field)
                missing.maxTextureBindingsPerStage = 0u;
            if (field == 1u)
                missing.maxSamplerBindingsPerStage = 0u;
            const auto denied = metalBindlessComputeCapabilities(
                missing, false, true, 123u, false, field != 2u, true, field != 3u);
            assert(!denied.support[1u][0u].nonUniform && !denied.support[1u][1u].nonUniform);
            assert(!denied.ordinaryDescriptorDomains[0u] && !denied.ordinaryDescriptorDomains[1u]);
        }
        assert(metalBindlessComputeCapabilities(probe, true, true, 123u).support[2u][0u].operations
            == BindlessOperation::None);
        const auto images = metalBindlessComputeCapabilities(probe, true, true, 123u, true);
        assert(images.originalNativeColorComputeExecutionContractVersion == 8u);
        assert(!images.originalNativeDepthComputeExecutionContractVersion
            && !images.originalNativeDepthRootPreparationContractVersion);
        auto depthProbe = probe;
        depthProbe.sampledDepthTexture = true;
        auto graphicsRuntime = sampledGraphics;
        assert(!metalBindlessGraphicsDepthExecutionAvailable(graphicsRuntime, depthProbe));
        graphicsRuntime.originalGraphicsExecutionContractVersion = 1u;
        assert(metalBindlessGraphicsDepthExecutionAvailable(graphicsRuntime, depthProbe));
        for (unsigned missing = 0u; missing < 10u; ++missing) {
            auto facts = graphicsRuntime;
            auto native = depthProbe;
            if (missing == 0u)
                native.sampledDepthTexture = false;
            if (missing == 1u)
                native.argumentBuffersTier2 = false;
            if (missing == 2u)
                native.nativeSync = false;
            if (missing == 3u)
                native.maxTextureBindingsPerStage = 0u;
            if (missing == 4u)
                native.maxSamplerBindingsPerStage = 0u;
            if (missing == 5u)
                facts.ordinaryDescriptorDomains[0u] = false;
            if (missing == 6u)
                facts.ordinaryDescriptorDomains[1u] = false;
            if (missing == 7u)
                facts.executablePayload = false;
            if (missing == 8u)
                facts.stageLimits[1u].sampledTextures = 0u;
            if (missing == 9u)
                facts.stageLimits[1u].samplers = 0u;
            assert(!metalBindlessGraphicsDepthExecutionAvailable(facts, native));
        }
        const auto computeOnly =
            metalBindlessComputeCapabilities(depthProbe, true, true, 123u, true);
        assert(!metalBindlessGraphicsDepthExecutionAvailable(computeOnly, depthProbe));
        depthProbe.r32FloatReadWriteTexture = false;
        const auto depth = metalBindlessComputeCapabilities(depthProbe, true, true, 123u, true);
        assert(depth.originalNativeDepthComputeExecutionContractVersion == 6u
            && depth.originalNativeDepthRootPreparationContractVersion == 6u
            && !depth.originalNativeColorComputeExecutionContractVersion
            && !depth.stageLimits[2u].storageTextures);
        for (unsigned mode = 0u; mode < 6u; ++mode) {
            auto denied = depthProbe;
            if (mode == 0u)
                denied.sampledDepthTexture = false;
            if (mode == 1u)
                denied.maxTextureBindingsPerStage = 0u;
            if (mode == 2u)
                denied.maxSamplerBindingsPerStage = 0u;
            const auto caps =
                metalBindlessComputeCapabilities(denied, mode != 3u, mode != 4u, 123u, mode != 5u);
            assert(!caps.originalNativeDepthComputeExecutionContractVersion
                && !caps.originalNativeDepthRootPreparationContractVersion);
        }
        assert(!images.originalStoragePreparationContractVersion
            && !images.originalStorageRootPreparationContractVersion);
        auto storageProbe = probe;
        storageProbe.rgba16FloatStorageTexture = true;
        const auto storage = metalBindlessComputeCapabilities(storageProbe, true, true, 123u, true);
        assert(storage.originalStoragePreparationContractVersion == 7u
            && storage.originalStorageRootPreparationContractVersion == 8u);
        const auto noStorage =
            metalBindlessComputeCapabilities(storageProbe, true, true, 123u, false);
        assert(!noStorage.originalStoragePreparationContractVersion
            && !noStorage.originalStorageRootPreparationContractVersion);
        for (unsigned field = 0u; field != 5u; ++field) {
            auto missing = storageProbe;
            if (field == 0u)
                missing.rgba16FloatStorageTexture = false;
            if (field == 1u)
                missing.argumentBuffersTier2 = false;
            if (field == 2u)
                missing.nativeSync = false;
            if (field == 3u)
                missing.maxTextureBindingsPerStage = 0u;
            if (field == 4u)
                missing.maxStorageBufferBindingSizeBytes = 0u;
            const auto denied = metalBindlessComputeCapabilities(missing, true, true, 123u, true);
            assert(!denied.originalStoragePreparationContractVersion
                && !denied.originalStorageRootPreparationContractVersion);
        }
        assert(images.support[2u][0u].operations
            == (BindlessOperation::Load | BindlessOperation::Query
                | BindlessOperation::SampleLevel));
        assert(images.support[2u][1u].nonUniform && images.support[2u][2u].nonUniform);
        assert(images.combinedSampledStorageTextureLimits[2u] == probe.maxTextureBindingsPerStage);
        assert(images.preparedGraphicsBindingContractVersion == 0u
            && !metalBindlessGraphicsExecutionAvailable(images));
        for (unsigned mode = 0u; mode < 3u; ++mode) {
            auto unavailable = probe;
            if (mode == 0u)
                unavailable.r32FloatReadWriteTexture = false;
            if (mode == 1u)
                unavailable.maxTextureBindingsPerStage = 0u;
            if (mode == 2u)
                unavailable.maxSamplerBindingsPerStage = 0u;
            const auto closed =
                metalBindlessComputeCapabilities(unavailable, true, true, 123u, true);
            assert(!closed.originalNativeColorComputeExecutionContractVersion);
            assert(closed.support[2u][0u].operations == BindlessOperation::None
                && !closed.support[2u][1u].nonUniform
                && !closed.combinedSampledStorageTextureLimits[2u]);
        }
        assert(!metalBindlessComputeCapabilities(probe, false, true, 123u, true).executablePayload);
        assert(!metalBindlessComputeCapabilities(probe, true, false, 123u, true).executablePayload);
        assert(metalBindlessComputeCapabilities(probe, false, true, 123u).execution
            == BindlessExecution::DiagnosticFailure);
        assert(metalBindlessComputeCapabilities(probe, true, false, 123u).execution
            == BindlessExecution::DiagnosticFailure);
        for (unsigned field = 0u; field < 4u; ++field) {
            auto missing = probe;
            if (field == 0u)
                missing.argumentBuffersTier2 = false;
            if (field == 1u)
                missing.nativeSync = false;
            if (field == 2u)
                missing.maxStorageBufferBindingsPerStage = 0u;
            if (field == 3u)
                missing.maxStorageBufferBindingSizeBytes = 0u;
            assert(metalBindlessComputeCapabilities(missing, true, true, 123u).execution
                == BindlessExecution::DiagnosticFailure);
        }
        auto disabled = desc;
        disabled.enableBindlessCompute = true;
        disabled.enableBindlessGraphics = true;
        disabled.enableArgumentBuffers = false;
        auto disabledDevice = arp::tests::createMetalContractDevice(disabled);
        assert(disabledDevice);
        auto* disabledExtension = static_cast<BindlessCrossExtension*>(
            disabledDevice->queryInterface(kExtensionBindlessCross));
        assert(disabledExtension && !disabledExtension->capabilities().executablePayload);
        assert(disabledExtension->capabilities().preparedComputeBindingContractVersion == 0u);
        assert(!metalBindlessGraphicsExecutionAvailable(disabledExtension->capabilities()));
        if (!runtimeBoundaryForReport.runtimeAvailableOnThisPlatform) {
            disabled.enableArgumentBuffers = true;
            auto stubDevice = arp::tests::createMetalContractDevice(disabled);
            assert(stubDevice);
            auto* stubExtension = static_cast<BindlessCrossExtension*>(
                stubDevice->queryInterface(kExtensionBindlessCross));
            assert(stubExtension && !stubExtension->capabilities().executablePayload);
            assert(stubExtension->capabilities().execution == BindlessExecution::DiagnosticFailure);
        }
    }
    using arp::rhi_metal::MetalRhiArgumentResourceUse;
    using arp::rhi_metal::metalRhiArgumentResourceUse;
    assert(metalRhiArgumentResourceUse(DescriptorType::UniformBuffer)
        == MetalRhiArgumentResourceUse::Read);
    assert(metalRhiArgumentResourceUse(DescriptorType::ReadOnlyStorageBuffer)
        == MetalRhiArgumentResourceUse::Read);
    assert(metalRhiArgumentResourceUse(DescriptorType::SampledTexture)
        == MetalRhiArgumentResourceUse::ReadSample);
    assert(metalRhiArgumentResourceUse(DescriptorType::StorageBuffer)
        == MetalRhiArgumentResourceUse::ReadWrite);
    assert(metalRhiArgumentResourceUse(DescriptorType::StorageTexture)
        == MetalRhiArgumentResourceUse::ReadWrite);
    assert(
        metalRhiArgumentResourceUse(DescriptorType::Sampler) == MetalRhiArgumentResourceUse::None);
    assert(bindless->capabilities().deviceIdentity != 0u);
    assert(bindless->capabilities().execution == BindlessExecution::DiagnosticFailure);
    assert(!bindless->capabilities().limitsQueried);
    assert(!bindless->capabilities().executablePayload);
    assert(bindless->capabilities().indirectInstanceConsumerContractVersion == 0u);
    assert(bindless->capabilities().zeroBaseIndirectProducerContractVersion == 0u);
    assert(bindless->capabilities().indirectPacketStorageContractVersion == 0u);
    assert(bindless->capabilities().faultReadbackContractVersion == 0u);
    assert(bindless->capabilities().preparedComputeBindingContractVersion == 0u);
    assert(bindless->capabilities().preparedGraphicsBindingContractVersion == 0u);
    assert(bindless->capabilities().preparedGraphicsPassFaultScopeContractVersion == 0u);
    {
        auto invalidOptIn = desc;
        invalidOptIn.enableBindlessComputeImages = true;
        assert(!describeMetalRhiDevice(invalidOptIn).success
            && describeMetalRhiDevice(invalidOptIn).diagnostics
                == "RHI-METAL-BINDLESS-COMPUTE-IMAGES-BASE-OPT-IN-REQUIRED");
        invalidOptIn.enableBindlessCompute = true;
        invalidOptIn.enableArgumentBuffers = false;
        assert(!describeMetalRhiDevice(invalidOptIn).success);
        using Images = MetalRhiBindlessComputeImageContract;
        assert(Images::validateTexture(DescriptorType::SampledTexture,
            TextureFormat::Rgba8Unorm,
            TextureViewDimension::Texture2D,
            TextureAspect::Color,
            1u,
            1u)
                .success);
        assert(Images::validateTexture(DescriptorType::StorageTexture,
            TextureFormat::R32Float,
            TextureViewDimension::Texture2D,
            TextureAspect::Color,
            1u,
            1u)
                .success);
        assert(!Images::validateTexture(DescriptorType::SampledTexture,
            TextureFormat::R32Float,
            TextureViewDimension::Texture2D,
            TextureAspect::Color,
            1u,
            1u)
                .success);
        assert(!Images::validateTexture(DescriptorType::StorageTexture,
            TextureFormat::R32Float,
            TextureViewDimension::Texture2DArray,
            TextureAspect::Color,
            1u,
            1u)
                .success);
        assert(!Images::validateTexture(DescriptorType::SampledTexture,
            TextureFormat::Rgba8Unorm,
            TextureViewDimension::Texture2D,
            TextureAspect::Depth,
            1u,
            1u)
                .success);
        assert(!Images::validateTexture(DescriptorType::StorageTexture,
            TextureFormat::R32Float,
            TextureViewDimension::Texture2D,
            TextureAspect::Color,
            2u,
            1u)
                .success);
        assert(!Images::validateTexture(DescriptorType::StorageTexture,
            TextureFormat::R32Float,
            TextureViewDimension::Texture2D,
            TextureAspect::Color,
            1u,
            2u)
                .success);
        assert(Images::validateSampler(false, true).success);
        assert(!Images::validateSampler(true, true).success
            && !Images::validateSampler(false, false).success);
    }
    {
        arp::core::pmr::String diagnostic;
        arp::rhi::BindlessComputeBindingPool pool(*bindless, {});
        arp::rhi::BindlessComputeBindingDesc request;
        request.optIn = true;
        assert(!pool.prepare(request, diagnostic)
            && diagnostic == "RHI-BINDLESS-PREPARED-COMPUTE-UNAVAILABLE");
        BindlessGraphicsBindingPool graphicsPool(*bindless, {});
        BindlessGraphicsBindingDesc graphicsRequest;
        graphicsRequest.optIn = true;
        assert(!graphicsPool.prepare(graphicsRequest, diagnostic)
            && diagnostic == "RHI-BINDLESS-PREPARED-GRAPHICS-UNAVAILABLE");
        assert(!prepareMetalRhiGraphicsBinding({}, {}, diagnostic)
            && diagnostic == "RHI-BINDLESS-PREPARED-GRAPHICS-OWNERS-MISSING");
    }
    for (const auto& stage : bindless->capabilities().support) {
        for (const auto& resource : stage) {
            assert(resource.operations == BindlessOperation::None);
            assert(!resource.nonUniform);
        }
    }
    BindlessRequirements bindlessRequest;
    bindlessRequest.deviceIdentity = bindless->capabilities().deviceIdentity;
    BindlessShaderArtifact bindlessArtifact;
    assert(std::string_view(bindless->plan(bindlessRequest, bindlessArtifact).diagnosticId)
        == "RHI-BINDLESS-OPT-IN-REQUIRED");
    bindlessRequest.optIn = true;
    assert(std::string_view(bindless->plan(bindlessRequest, bindlessArtifact).diagnosticId)
        == "RHI-BINDLESS-EXECUTION-UNAVAILABLE");
    std::pmr::string bindlessPipelineDiagnostic;
    assert(!bindless->createComputePipeline({}, "computeMain", bindlessPipelineDiagnostic));
    assert(bindlessPipelineDiagnostic == "RHI-METAL-BINDLESS-EXECUTION-UNAVAILABLE");
    assert(!bindless->createRenderPipeline(
        {}, {}, "vertexMain", "cleanFragment", bindlessPipelineDiagnostic));
    assert(bindlessPipelineDiagnostic == "RHI-METAL-BINDLESS-GRAPHICS-EXECUTION-UNAVAILABLE");
    assert(!createMetalRhiAuthorizedBindlessRenderPipeline(*bindless,
        {},
        {},
        "vertexMain",
        "cleanFragment",
        desc.capabilityContract,
        {},
        {},
        bindlessPipelineDiagnostic));
    assert(bindlessPipelineDiagnostic == "RHI-METAL-BINDLESS-GRAPHICS-EXECUTION-UNAVAILABLE");
    std::pmr::string bindlessPayloadLabel = "MetalBindlessRetentionContract";
    auto bindlessCommandPayload =
        createMetalRhiNativeCommandPayload(bindlessPayloadLabel, nullptr, nullptr, 1u, 1u);
    assert(bindlessCommandPayload);
    auto opaqueBindlessOwner = std::make_shared<std::uint32_t>(1u);
    std::shared_ptr<const void> opaqueBindlessTicket = opaqueBindlessOwner;
    std::pmr::string bindlessRetentionDiagnostic;
    assert(!bindlessCommandPayload->retainIndexedPipeline(
        opaqueBindlessTicket, bindlessRetentionDiagnostic));
    assert(bindlessRetentionDiagnostic == "RHI-METAL-BINDLESS-NATIVE-RUNTIME-UNAVAILABLE");
    arp::rhi::semantics::ShutdownDrainRequest shutdownDrain;
    shutdownDrain.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const auto shutdownDrainReport = device->drainForShutdown(shutdownDrain);
    assert(shutdownDrainReport.backend == Backend::Metal);
    assert(shutdownDrainReport.outcome == arp::rhi::semantics::ShutdownDrainOutcome::Native);
    assert(shutdownDrainReport.state == arp::rhi::semantics::ShutdownDrainState::Completed);
    assert(shutdownDrainReport.completed());
    assert(shutdownDrainReport.remainingNanoseconds > 0u);
    shutdownDrain.deadline = std::chrono::steady_clock::now() - std::chrono::nanoseconds(1);
    const auto expiredShutdownDrain = device->drainForShutdown(shutdownDrain);
    assert(!expiredShutdownDrain.completed());
    assert(expiredShutdownDrain.state == arp::rhi::semantics::ShutdownDrainState::DeadlineExpired);
    assert(expiredShutdownDrain.diagnosticId
        == arp::rhi::semantics::kShutdownDrainDeadlineExpiredDiagnostic);
    const auto caps = device->capabilities();
    assert(caps.backend == Backend::Metal);
    assert(caps.core.graphics);
    assert(caps.core.compute);
    assert(!caps.explicitFeatures.pushConstants);
    assert(caps.explicitFeatures.secondaryCommandBuffers);
    assert(caps.explicitFeatures.explicitBarriers);
    assert(caps.memory.deviceLocal);
    assert(caps.memory.hostVisible);
    assert(caps.memory.transientAliasing);
    assert(caps.memory.backendFallbacksHiddenFromUpperLayer);
    assert(!caps.features.pushConstants);
    assert(!caps.pushConstantLoweringHasExecutablePayload);

    auto invalidResizeSwapchain = device->createSwapchain({
        "MetalDrawableInvalidResizeContract",
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

    auto privateBuffer = device->createBuffer({
        "MetalPrivateVertexBuffer",
        4096,
        ResourceUsage::VertexBuffer | ResourceUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::CopyDst,
    });
    auto sharedUpload = device->createBuffer({
        "MetalSharedUploadBuffer",
        4096,
        ResourceUsage::CopySrc,
        MemoryUsage::CpuToGpu,
        ResourceState::CopySrc,
    });
    auto managedReadback = device->createBuffer({
        "MetalManagedReadbackBuffer",
        4096,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst,
    });
    auto memorylessColor = device->createTexture({
        "MetalMemorylessColor",
        {64, 64, 1},
        TextureFormat::Bgra8Unorm,
        TextureUsage::RenderAttachment | TextureUsage::CopySrc | TextureUsage::CopyDst,
        MemoryUsage::Transient,
        ResourceState::Undefined,
    });
    assert(privateBuffer && sharedUpload && managedReadback && memorylessColor);
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
    assert(!privateBuffer->allocation().nativeHandleExposed);
    assert(!memorylessColor->allocation().nativeHandleExposed);

    const auto runtimeBoundary = runtimeBoundaryForReport;
    const auto privateBufferPayload = inspectMetalRhiNativePayload(*privateBuffer);
    assert(privateBufferPayload.backendPayloadPresent);
    assert(privateBufferPayload.storageMode == MetalRhiNativeStorageMode::Private);
    assert(!privateBufferPayload.nativeHandleExposedToUpperLayer);
    assert(
        privateBufferPayload.nativeRuntimeBacked == runtimeBoundary.runtimeAvailableOnThisPlatform);
    assert(!runtimeBoundary.runtimeAvailableOnThisPlatform
        == privateBufferPayload.nativeRuntimeDiagnosticOnly);

    const auto sharedUploadPayload = inspectMetalRhiNativePayload(*sharedUpload);
    assert(sharedUploadPayload.backendPayloadPresent);
    assert(sharedUploadPayload.storageMode == MetalRhiNativeStorageMode::Shared);
    assert(!sharedUploadPayload.nativeHandleExposedToUpperLayer);

    const auto managedReadbackPayload = inspectMetalRhiNativePayload(*managedReadback);
    assert(managedReadbackPayload.backendPayloadPresent);
    assert(managedReadbackPayload.storageMode == MetalRhiNativeStorageMode::Managed);
    assert(!managedReadbackPayload.nativeHandleExposedToUpperLayer);

    const auto memorylessTexturePayload = inspectMetalRhiNativePayload(*memorylessColor);
    assert(memorylessTexturePayload.backendPayloadPresent);
    assert(memorylessTexturePayload.storageMode == MetalRhiNativeStorageMode::Memoryless);
    assert(!memorylessTexturePayload.nativeHandleExposedToUpperLayer);

    auto colorView = device->createTextureView(*memorylessColor,
        {
            "MetalMemorylessColorView",
            TextureFormat::Bgra8Unorm,
            TextureUsage::Sampled,
            TextureViewDimension::Texture2D,
            0,
            1,
            0,
            1,
            TextureAspect::Color,
        });
    auto sampler = device->createSampler({
        "MetalLinearSampler",
        SamplerFilter::Linear,
        SamplerFilter::Linear,
        SamplerAddressMode::ClampToEdge,
        SamplerAddressMode::Repeat,
        SamplerAddressMode::ClampToEdge,
    });
    assert(colorView && sampler);

    const auto viewPayload = inspectMetalRhiNativePayload(*colorView);
    assert(viewPayload.backendPayloadPresent);
    assert(viewPayload.storageMode == MetalRhiNativeStorageMode::Memoryless);
    assert(!viewPayload.nativeHandleExposedToUpperLayer);

    const auto samplerPayload = inspectMetalRhiNativePayload(*sampler);
    assert(samplerPayload.backendPayloadPresent);
    assert(samplerPayload.storageMode == MetalRhiNativeStorageMode::None);
    assert(!samplerPayload.nativeHandleExposedToUpperLayer);

    auto uniformBuffer = device->createBuffer({
        "MetalUniformBuffer",
        256,
        ResourceUsage::UniformBuffer | ResourceUsage::CopyDst,
        MemoryUsage::CpuToGpu,
        ResourceState::CopyDst,
    });
    auto storageBuffer = device->createBuffer({
        "MetalStorageBuffer",
        512,
        ResourceUsage::StorageBuffer | ResourceUsage::CopySrc | ResourceUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderWrite,
    });
    auto sampledTexture = device->createTexture({
        "MetalSampledTexture",
        {32, 32, 1},
        TextureFormat::Bgra8Unorm,
        TextureUsage::Sampled | TextureUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead,
    });
    auto storageTexture = device->createTexture({
        "MetalStorageTexture",
        {32, 32, 1},
        TextureFormat::Rgba8Unorm,
        TextureUsage::Storage | TextureUsage::CopySrc | TextureUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderWrite,
    });
    assert(uniformBuffer && storageBuffer && sampledTexture && storageTexture);

    auto sampledView = device->createTextureView(*sampledTexture,
        {
            "MetalSampledTextureView",
            TextureFormat::Bgra8Unorm,
            TextureUsage::Sampled,
            TextureViewDimension::Texture2D,
            0,
            1,
            0,
            1,
            TextureAspect::Color,
        });
    auto storageView = device->createTextureView(*storageTexture,
        {
            "MetalStorageTextureView",
            TextureFormat::Rgba8Unorm,
            TextureUsage::Storage,
            TextureViewDimension::Texture2D,
            0,
            1,
            0,
            1,
            TextureAspect::Color,
        });
    auto cubeTexture = device->createTexture({
        "MetalSampledCube",
        {1, 1, 1},
        TextureFormat::Rgba16Float,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead,
        1,
        6,
        true,
    });
    auto cubeView = device->createTextureView(*cubeTexture,
        {
            "MetalSampledCubeView",
            TextureFormat::Rgba16Float,
            TextureUsage::Sampled,
            TextureViewDimension::TextureCube,
            0,
            1,
            0,
            6,
            TextureAspect::Color,
        });
    assert(sampledView && storageView && cubeTexture && cubeView);

    DescriptorLayoutDesc argumentLayout;
    argumentLayout.name = "MetalMaterialArguments";
    argumentLayout.bindings.push_back(
        {0, DescriptorType::UniformBuffer, ShaderStage::Vertex | ShaderStage::Fragment});
    argumentLayout.bindings.push_back({1, DescriptorType::StorageBuffer, ShaderStage::Compute});
    argumentLayout.bindings.push_back({2, DescriptorType::SampledTexture, ShaderStage::Fragment});
    argumentLayout.bindings.push_back({3, DescriptorType::StorageTexture, ShaderStage::Compute});
    argumentLayout.bindings.push_back({4, DescriptorType::Sampler, ShaderStage::Fragment});

    auto descriptorArena = device->createDescriptorArena({
        "MetalArgumentDescriptorArena",
        2,
        16,
    });
    auto descriptorSet = descriptorArena->allocateDescriptorSet({
        "MetalMaterialArgumentSet",
        argumentLayout,
    });
    assert(descriptorSet);
    const auto wrongKindWrite = descriptorSet->write({
        0,
        DescriptorType::UniformBuffer,
        sampledView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(!wrongKindWrite.success);
    assert(descriptorSet
            ->write({
                0,
                DescriptorType::UniformBuffer,
                uniformBuffer->id(),
                DescriptorResourceKind::Buffer,
            })
            .success);
    assert(descriptorSet
            ->write({
                1,
                DescriptorType::StorageBuffer,
                storageBuffer->id(),
                DescriptorResourceKind::Buffer,
            })
            .success);
    assert(descriptorSet
            ->write({
                2,
                DescriptorType::SampledTexture,
                sampledView->id(),
                DescriptorResourceKind::TextureView,
            })
            .success);
    assert(descriptorSet
            ->write({
                3,
                DescriptorType::StorageTexture,
                storageView->id(),
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
    DescriptorLayoutDesc cubeArgumentLayout;
    cubeArgumentLayout.name = "MetalCubeArguments";
    cubeArgumentLayout.bindings.push_back(
        {0, DescriptorType::SampledTexture, ShaderStage::Fragment});
    cubeArgumentLayout.bindings.back().textureViewDimensionConstrained = true;
    cubeArgumentLayout.bindings.back().textureViewDimension = TextureViewDimension::TextureCube;
    auto cubeDescriptorSet = descriptorArena->allocateDescriptorSet({
        "MetalCubeArgumentSet",
        cubeArgumentLayout,
    });
    assert(cubeDescriptorSet);
    const auto metal2DAsCube = cubeDescriptorSet->write({
        0,
        DescriptorType::SampledTexture,
        sampledView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(!metal2DAsCube.success);
    assert(metal2DAsCube.diagnostics.find("texture view dimension") != std::pmr::string::npos);
    assert(cubeDescriptorSet
            ->write({
                0,
                DescriptorType::SampledTexture,
                cubeView->id(),
                DescriptorResourceKind::TextureView,
            })
            .success);

    const auto descriptorPayload = inspectMetalRhiDescriptorPayload(*descriptorSet);
    assert(descriptorPayload.backendPayloadPresent);
    assert(descriptorPayload.argumentBindingsBacked);
    assert(descriptorPayload.bindingCount == 5u);
    assert(descriptorPayload.bufferArgumentCount == 2u);
    assert(descriptorPayload.textureArgumentCount == 2u);
    assert(descriptorPayload.samplerArgumentCount == 1u);
    assert(descriptorPayload.uniformBufferBound);
    assert(descriptorPayload.storageBufferBound);
    assert(descriptorPayload.sampledTextureBound);
    assert(descriptorPayload.storageTextureBound);
    assert(descriptorPayload.samplerBound);
    assert(!descriptorPayload.nativeHandleExposedToUpperLayer);
    assert(descriptorPayload.nativeRuntimeBacked == runtimeBoundary.runtimeAvailableOnThisPlatform);
    assert(descriptorPayload.nativeRuntimeDiagnosticOnly
        == !runtimeBoundary.runtimeAvailableOnThisPlatform);

    const auto memoryBeforeBudgetReject = device->memoryStats();
    assert(memoryBeforeBudgetReject.allocatedBytes > 0);
    assert(memoryBeforeBudgetReject.budgetCapacityBytes == desc.memoryBudgetBytes);
    assert(memoryBeforeBudgetReject.peakAllocatedBytes >= memoryBeforeBudgetReject.allocatedBytes);
    assert(memoryBeforeBudgetReject.backendFallbacksHiddenFromUpperLayer);

    // Existing resources already retain part of the fixed ledger, so a buffer
    // equal to the complete device budget must be rejected before native Metal
    // allocation. This observes the real allocator's L2 budget path.
    const auto overBudget = device->createBuffer({
        "MetalOverBudgetAllocation",
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
    assert(
        stagingBudget.capacity == desc.uploadStagingPageSizeBytes * desc.uploadStagingMaxPageCount);

    const auto& commandBudget = physicalBudget.category(semantics::PhysicalBudgetCategory::Command);
    assert(commandBudget.observed());
    assert(commandBudget.unit == semantics::PhysicalBudgetUnit::CommandBuffers);
    assert(commandBudget.capacity == kMetalNativeCommandBufferCapacity);
    assert(commandBudget.current <= commandBudget.peak);
    assert(commandBudget.peak <= commandBudget.capacity);
    assert(commandBudget.liveCount == commandBudget.current);

    PipelineLayoutDesc layoutDesc;
    layoutDesc.pushConstantRanges.push_back(
        {ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute, 0, 64});

    auto& budgetedDescriptors = device->descriptorCache();
    assert(budgetedDescriptors.configureBudget(
        {semantics::kCacheBudgetContractVersion, "MetalDeviceContract/Descriptor", 4u}));
    assert(budgetedDescriptors.acquireDescriptorArena({"CacheBudgetExact", 1u, 4u}));
    assert(!budgetedDescriptors.acquireDescriptorArena({"CacheBudgetRejected", 1u, 1u}));

    auto& budgetedPipelines = device->pipelineCache();
    assert(budgetedPipelines.configureBudget(
        {semantics::kCacheBudgetContractVersion, "MetalDeviceContract/Pipeline", 1u}));
    assert(budgetedPipelines.acquirePipelineLayout(layoutDesc));

    const auto cachePhysicalBudget = device->physicalBudgetLedger();
    const auto& descriptorCacheBudget =
        cachePhysicalBudget.category(semantics::PhysicalBudgetCategory::Descriptor);
    assert(descriptorCacheBudget.observed()
        && descriptorCacheBudget.unit == semantics::PhysicalBudgetUnit::Descriptors
        && descriptorCacheBudget.capacity == 4u && descriptorCacheBudget.current == 4u
        && descriptorCacheBudget.rejectionCount == 1u);
    const auto& pipelineCacheBudget =
        cachePhysicalBudget.category(semantics::PhysicalBudgetCategory::PipelineCache);
    assert(pipelineCacheBudget.observed()
        && pipelineCacheBudget.unit == semantics::PhysicalBudgetUnit::PipelineEntries
        && pipelineCacheBudget.capacity == 1u && pipelineCacheBudget.current == 1u);

    auto layout = device->createPipelineLayout(layoutDesc);
    assert(layout);
    auto shaderPackage = makeMetalShaderPackage();
    assert(validateShaderPackage(shaderPackage).success);

    // A contract that lacks an executable push-constant payload must fail
    // closed at pipeline creation rather than defer the error to recording.
    if (!caps.pushConstantLoweringHasExecutablePayload) {
        RenderPipelineDesc rejectedRenderDesc;
        rejectedRenderDesc.label = "MetalRejectedPushRenderPipeline";
        rejectedRenderDesc.layout = layout.get();
        rejectedRenderDesc.shaderPackage = &shaderPackage;
        rejectedRenderDesc.vertexShader = {"metal_rhi.slang", "vsMain", ShaderStage::Vertex};
        rejectedRenderDesc.fragmentShader = {"metal_rhi.slang", "fsMain", ShaderStage::Fragment};
        rejectedRenderDesc.colorFormat = TextureFormat::Bgra8Unorm;
        const auto rejectedRender = device->validateRenderPipeline(rejectedRenderDesc);
        assert(!rejectedRender.success);
        assert(rejectedRender.diagnostics.find("[PC-E011]") != std::pmr::string::npos);
        assert(!device->createRenderPipeline(rejectedRenderDesc));

        ComputePipelineDesc rejectedComputeDesc;
        rejectedComputeDesc.label = "MetalRejectedPushComputePipeline";
        rejectedComputeDesc.layout = layout.get();
        rejectedComputeDesc.shaderPackage = &shaderPackage;
        rejectedComputeDesc.computeShader = {"metal_rhi.slang", "csMain", ShaderStage::Compute};
        const auto rejectedCompute = device->validateComputePipeline(rejectedComputeDesc);
        assert(!rejectedCompute.success);
        assert(rejectedCompute.diagnostics.find("[PC-E011]") != std::pmr::string::npos);
        assert(!device->createComputePipeline(rejectedComputeDesc));
        return 0;
    }

    RenderPipelineDesc renderPipelineDesc;
    renderPipelineDesc.label = "MetalRhiRenderPipeline";
    renderPipelineDesc.layout = layout.get();
    renderPipelineDesc.shaderPackage = &shaderPackage;
    renderPipelineDesc.vertexShader = {"metal_rhi.slang", "vsMain", ShaderStage::Vertex};
    renderPipelineDesc.fragmentShader = {"metal_rhi.slang", "fsMain", ShaderStage::Fragment};
    renderPipelineDesc.colorFormat = TextureFormat::Bgra8Unorm;
    renderPipelineDesc.depthFormat = TextureFormat::Depth24Stencil8;
    renderPipelineDesc.depthStencil.format = TextureFormat::Depth24Stencil8;
    renderPipelineDesc.depthStencil.depthTestEnabled = true;
    renderPipelineDesc.depthStencil.stencilEnabled = true;
    renderPipelineDesc.multisample.sampleCount = 4;
    const auto renderPipelineReport = device->validateRenderPipeline(renderPipelineDesc);
    assert(renderPipelineReport.success);
    assert(renderPipelineReport.backend == Backend::Metal);
    assert(renderPipelineReport.pipelineCacheKeyStable);
    auto renderPipeline = device->createRenderPipeline(renderPipelineDesc);
    assert(renderPipeline);
    assert(!renderPipeline->nativeHandleExposedToUpperLayer());
    const auto renderPayload = inspectMetalRhiPipelinePayload(*renderPipeline);
    assert(renderPayload.backendPayloadPresent);
    assert(renderPayload.shaderModulePayloadPresent);
    assert(renderPayload.renderPipelinePayloadPresent);
    assert(!renderPayload.computePipelinePayloadPresent);
    assert(renderPayload.pipelineCacheKeyStable);
    assert(renderPayload.pipelineCacheKey == renderPipelineReport.pipelineCacheKey);
    assert(!renderPayload.nativeHandleExposedToUpperLayer);
    assert(renderPayload.nativeRuntimeBacked == runtimeBoundary.runtimeAvailableOnThisPlatform);
    assert(renderPayload.nativeRuntimeDiagnosticOnly
        == !runtimeBoundary.runtimeAvailableOnThisPlatform);

    ComputePipelineDesc computePipelineDesc;
    computePipelineDesc.label = "MetalRhiComputePipeline";
    computePipelineDesc.layout = layout.get();
    computePipelineDesc.shaderPackage = &shaderPackage;
    computePipelineDesc.computeShader = {"metal_rhi.slang", "csMain", ShaderStage::Compute};
    const auto computePipelineReport = device->validateComputePipeline(computePipelineDesc);
    assert(computePipelineReport.success);
    assert(computePipelineReport.backend == Backend::Metal);
    auto computePipeline = device->createComputePipeline(computePipelineDesc);
    assert(computePipeline);
    assert(!computePipeline->bindlessAuthorization());
    const auto computePayload = inspectMetalRhiPipelinePayload(*computePipeline);
    assert(computePayload.backendPayloadPresent);
    assert(computePayload.shaderModulePayloadPresent);
    assert(!computePayload.renderPipelinePayloadPresent);
    assert(computePayload.computePipelinePayloadPresent);
    assert(computePayload.pipelineCacheKeyStable);
    assert(computePayload.pipelineCacheKey == computePipelineReport.pipelineCacheKey);
    assert(!computePayload.nativeHandleExposedToUpperLayer);
    assert(computePayload.nativeRuntimeBacked == runtimeBoundary.runtimeAvailableOnThisPlatform);
    assert(computePayload.nativeRuntimeDiagnosticOnly
        == !runtimeBoundary.runtimeAvailableOnThisPlatform);

    const auto verifiedMslPayload = createMetalRhiNativeComputePipelinePayload(computePipelineDesc,
        computePipelineReport,
        std::string_view(readText(shaderPackage.artifacts.mslPath)));
    assert(verifiedMslPayload);
    assert(
        verifiedMslPayload->nativeRuntimeBacked == runtimeBoundary.runtimeAvailableOnThisPlatform);
    assert(verifiedMslPayload->nativeRuntimeDiagnosticOnly
        == !runtimeBoundary.runtimeAvailableOnThisPlatform);
    assert(!verifiedMslPayload->nativeHandleExposedToUpperLayer);

    auto encoder = device->createCommandEncoder({"MetalRhiFrame"});
    {
        assert(device->capabilities().commandRetention.contractVersion == 0u);
        CommandEncoderDesc bounded;
        bounded.retentionBudget = CommandRetentionBudget{1u, true, 2u, 1u, 2u, 0u, 4096u};
        assert(!device->createCommandEncoder(bounded));
    }
    assert(encoder && encoder->recordingIdentity() != 0u);
    {
        const auto native = metalRhiNativePayloadFor(*encoder);
        assert(native);
        const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
        const auto first = native->preparedCompletionState(),
                   second = native->preparedCompletionState();
        assert(first == second);
        assert(static_cast<bool>(first) == runtimeBoundary.runtimeAvailableOnThisPlatform);
        if (first)
            assert(!first->isComplete());
        assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
    const auto beforeGraphicsIntent = encoder->lastValidation().recordedCommandCount;
    const auto closedGraphicsIntent =
        encoder->prepareBindlessGraphicsPass({}, BindlessAccessArray(nullptr, 0u));
    assert(!closedGraphicsIntent.success
        && closedGraphicsIntent.diagnostics == "RHI-BINDLESS-GRAPHICS-PASS-SCOPE-UNAVAILABLE"
        && closedGraphicsIntent.recordedCommandCount == beforeGraphicsIntent);
    ResourceBarrierDesc barrier;
    barrier.resource = memorylessColor->id();
    barrier.textureSubresources = {0, 1, 0, 1, TextureAspect::Color};
    barrier.before = ResourceState::Undefined;
    barrier.after = ResourceState::ColorAttachment;
    barrier.sourceStages = PipelineStage::TopOfPipe;
    barrier.destinationStages = PipelineStage::ColorAttachmentOutput;
    barrier.destinationAccess = ResourceAccess::ColorAttachmentWrite;
    const auto barrierResult = encoder->transition(*memorylessColor, barrier);
    assert(barrierResult.success);
    assert(barrierResult.stageAccessAware);

    RenderPassDesc passDesc;
    passDesc.label = "MetalDrawablePass";
    passDesc.colorTargetName = memorylessColor->desc().label;
    passDesc.colorAttachments.push_back({
        "MetalDrawableColor",
        memorylessColor->id(),
        memorylessColor->desc().format,
        LoadOp::Clear,
        StoreOp::Store,
    });
    auto pass = encoder->beginRenderPass(passDesc);
    assert(pass);
    assert(pass->queryInterface(kExtensionBindlessCross) == nullptr);
    assert(pass->setPipeline(*renderPipeline).success);
    assert(pass->bindDescriptorSet(0, *descriptorSet).success);
    const auto renderPush =
        pass->pushConstants(ShaderStage::Vertex | ShaderStage::Fragment, 0, 64, kPushData);
    assert(renderPush.success);
    assert(renderPush.recordedCommandCount == 5u);
    assert(pass->drawIndexed(3, 1, 0, 0, 0).success);
    assert(pass->end().success);

    auto computePass = encoder->beginComputePass({"MetalComputePass"});
    assert(computePass);
    assert(computePass->queryInterface(kExtensionBindlessCross) == nullptr);
    assert(computePass->setPipeline(*computePipeline).success);
    assert(computePass->bindDescriptorSet(0, *descriptorSet).success);
    const auto computePush = computePass->pushConstants(ShaderStage::Compute, 0, 64, kPushData);
    assert(computePush.success);
    assert(computePush.recordedCommandCount == 10u);
    assert(computePass->dispatch(1, 1, 1).success);
    assert(computePass->end().success);

    auto transferPass = encoder->beginTransferPass({"MetalBlitPass"});
    assert(transferPass);
    assert(transferPass
            ->copyBufferToBuffer({
                storageBuffer->id(),
                uniformBuffer->id(),
                0,
                0,
                128,
            })
            .success);
    assert(transferPass->end().success);

    auto commands = encoder->finish();
    assert(commands);
    auto* bindlessReports = static_cast<BindlessCommandBufferExtension*>(
        commands->queryInterface(kExtensionBindlessCross));
    assert(bindlessReports);
    const auto unscheduledFault = bindlessReports->faultReport(0x4d455441u);
    assert(unscheduledFault.snapshotGeneration == 0x4d455441u);
    assert(unscheduledFault.status == BindlessFaultValidationStatus::Unavailable);
    assert(unscheduledFault.faultBits == 0u);
    const auto commandStats = inspectMetalRhiNativeCommandStats(*commands);
    assert(commandStats.backendPayloadPresent);
    assert(commandStats.nativeCommandBufferCreated);
    assert(commandStats.nativeRenderEncoderCreated);
    assert(commandStats.nativeComputeEncoderCreated);
    assert(commandStats.nativeBlitEncoderCreated);
    assert(commandStats.nativeGraphicsPipelineBound);
    assert(commandStats.nativeComputePipelineBound);
    assert(commandStats.nativeDescriptorArgumentBound);
    assert(!commandStats.nativePushConstantUploadBufferWritten);
    assert(commandStats.nativePushConstantSetBytesWritten);
    assert(commandStats.nativePushConstantUploadBytes == 64u);
    assert(commandStats.nativeIndexedDrawEncoded);
    assert(commandStats.nativeDispatchEncoded);
    assert(commandStats.nativeBlitCopyEncoded);
    assert(commandStats.nativeCommandBufferFinished);
    assert(!commandStats.nativeHandleExposedToUpperLayer);
    assert(commandStats.nativeRuntimeBacked == runtimeBoundary.runtimeAvailableOnThisPlatform);
    assert(commandStats.nativeRuntimeDiagnosticOnly
        == !runtimeBoundary.runtimeAvailableOnThisPlatform);

    const auto submitted = device->queue().submit(*commands);
    assert(submitted.success);
    assert(submitted.submittedCommandCount == commands->commandCount());
    const auto queueStats = inspectMetalRhiNativeQueueStats(device->queue());
    assert(queueStats.backendPayloadPresent);
    assert(queueStats.nativeQueueSubmitCount >= 1u);
    assert(queueStats.boundedCompletionTelemetry);
    assert(!queueStats.blockingWaitUsed);
    assert(!queueStats.nativeHandleExposedToUpperLayer);
    assert(queueStats.nativeRuntimeBacked == runtimeBoundary.runtimeAvailableOnThisPlatform);
    assert(
        queueStats.nativeRuntimeDiagnosticOnly == !runtimeBoundary.runtimeAvailableOnThisPlatform);

    auto swapchain = device->createSwapchain({
        "MetalDrawableSurface",
        {320, 180},
        TextureFormat::Bgra8Unorm,
        2,
    });
    assert(swapchain);
    assert(swapchain->maxFramesInFlight() == 2u);
    auto frame = swapchain->acquireNextFrame();
    assert(frame.success);
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
    assert(swapchain->present(firstInFlight).success);
    auto recycledFrame = swapchain->acquireNextFrame();
    assert(recycledFrame.success);
    assert(recycledFrame.frameIndex == firstInFlight.frameIndex);
    assert(swapchain->present(secondInFlight).success);
    assert(swapchain->present(recycledFrame).success);
    device->waitIdle();
    const auto resized = swapchain->resize({640, 360});
    assert(resized.success);
    assert(resized.swapchainRebuildRequired);
    const auto layerLost = swapchain->setSurfaceAvailable(false);
    assert(layerLost.success);
    assert(layerLost.releaseSurfaceResources);
    assert(!layerLost.acquireAllowed);
    const auto pausedFrame = swapchain->acquireNextFrame();
    assert(!pausedFrame.success);
    const auto layerRestored = swapchain->setSurfaceAvailable(true);
    assert(layerRestored.success);
    assert(layerRestored.acquireAllowed);
    auto restoredFrame = swapchain->acquireNextFrame();
    assert(restoredFrame.success);
    assert(swapchain->present(restoredFrame).success);

    const auto swapchainStats = inspectMetalRhiNativeSwapchainStats(*device);
    assert(swapchainStats.backendPayloadPresent);
    assert(!swapchainStats.nativeHandleExposedToUpperLayer);
    assert(swapchainStats.boundedFrameSlotCount == 2u);
    assert(swapchainStats.memoryWarningCount >= 1u);
    assert(swapchainStats.backgroundPauseCount >= 1u);
    assert(swapchainStats.foregroundResumeCount >= 1u);
    assert(swapchainStats.layerLossCount >= 1u);
    assert(swapchainStats.layerRecreateCount >= 1u);
    assert(swapchainStats.nativeRuntimeBacked == runtimeBoundary.runtimeAvailableOnThisPlatform);
    assert(swapchainStats.nativeRuntimeDiagnosticOnly
        == !runtimeBoundary.runtimeAvailableOnThisPlatform);
    if (runtimeBoundary.runtimeAvailableOnThisPlatform) {
        assert(swapchainStats.nativeLayerCreateCount >= 1u);
        assert(swapchainStats.nativeDrawableAcquireCount >= 1u);
        assert(swapchainStats.nativeDrawablePresentCount >= 1u);
        assert(swapchainStats.nativeResizeCount >= 1u);
    } else {
        assert(swapchainStats.nativeLayerCreateCount == 0u);
        assert(swapchainStats.nativeDrawableAcquireCount == 0u);
        assert(swapchainStats.nativeDrawablePresentCount == 0u);
        assert(swapchainStats.nativeResizeCount == 0u);
    }

    const auto resizeLifecycle =
        describeMetalRhiDrawableLifecycle(desc, MetalRhiDrawableLifecycleEvent::Resize);
    assert(resizeLifecycle.success);
    assert(resizeLifecycle.resizeHandled);
    assert(resizeLifecycle.swapchainRebuildRequired);
    const auto layerLossLifecycle =
        describeMetalRhiDrawableLifecycle(desc, MetalRhiDrawableLifecycleEvent::LayerLost);
    assert(layerLossLifecycle.success);
    assert(layerLossLifecycle.layerLossHandled);
    assert(layerLossLifecycle.releaseDrawableResources);
    assert(!layerLossLifecycle.acquireAllowed);
    const auto backgroundLifecycle =
        describeMetalRhiDrawableLifecycle(desc, MetalRhiDrawableLifecycleEvent::BackgroundPause);
    assert(backgroundLifecycle.success);
    assert(backgroundLifecycle.backgroundPauseHandled);
    assert(!backgroundLifecycle.acquireAllowed);
    assert(!backgroundLifecycle.presentAllowed);
    const auto foregroundLifecycle =
        describeMetalRhiDrawableLifecycle(desc, MetalRhiDrawableLifecycleEvent::ForegroundResume);
    assert(foregroundLifecycle.success);
    assert(foregroundLifecycle.foregroundResumeHandled);
    assert(foregroundLifecycle.acquireAllowed);
    const auto memoryWarningLifecycle =
        describeMetalRhiDrawableLifecycle(desc, MetalRhiDrawableLifecycleEvent::MemoryWarning);
    assert(memoryWarningLifecycle.success);
    assert(memoryWarningLifecycle.memoryWarningHandled);
    assert(memoryWarningLifecycle.releaseDrawableResources);

    // FrameInFlightSyncDesc / validateFrameInFlightSync removed during RHI
    // collapse — sync now backend-owned, validated at device creation.

    const auto commandSlice = describeMetalRhiCommandSlice();
    assert(commandSlice.backendVocabularyHiddenFromUpperLayer);
    assert(!commandSlice.nativeCommandHandleExposedToUpperLayer);
    assert(commandSlice.pushConstantUploadBufferSupported);
    assert(commandSlice.drawableLifecycleGuarded);
    assert(commandSlice.logicalFrameSlots == 2u);

    auto leakingDesc = desc;
    leakingDesc.allowNativeHandleExposureToUpperLayers = true;
    const auto leakingReport = describeMetalRhiDevice(leakingDesc);
    assert(!leakingReport.success);
    assert(leakingReport.diagnostics.find("native") != std::pmr::string::npos);

    auto wrongBackend = desc;
    wrongBackend.capabilityContract.backend = Backend::Vulkan;
    const auto wrongBackendReport = describeMetalRhiDevice(wrongBackend);
    assert(!wrongBackendReport.success);
    assert(wrongBackendReport.diagnostics.find("Metal") != std::pmr::string::npos);

    auto malformedDirectBudget = desc;
    malformedDirectBudget.directObjectBudget = {
        arp::rhi::semantics::kDirectObjectBudgetContractVersion,
        "MetalDirectObjectBudgetMalformed",
        1u,
        0u,
    };
    assert(!describeMetalRhiDevice(malformedDirectBudget).success);

    auto boundedDirectObjects = desc;
    boundedDirectObjects.directObjectBudget = {
        arp::rhi::semantics::kDirectObjectBudgetContractVersion,
        "MetalDirectObjectBudgetContract",
        1u,
        4u,
    };
    auto boundedDevice = arp::tests::createMetalContractDevice(boundedDirectObjects);
    assert(boundedDevice);
    auto directLayout = boundedDevice->createPipelineLayout({});
    assert(directLayout);
    assert(!boundedDevice->createPipelineLayout({}));
    auto directArena =
        boundedDevice->createDescriptorArena({"MetalDirectObjectBudgetArena", 1u, 4u});
    assert(directArena);
    assert(!boundedDevice->createDescriptorArena({"MetalDirectObjectBudgetOneShort", 1u, 1u}));
    const auto directBudgetLedger = boundedDevice->physicalBudgetLedger();
    assert(directBudgetLedger.valid());
    const auto& directPipelines = directBudgetLedger.category(
        arp::rhi::semantics::PhysicalBudgetCategory::DirectPipelineObjects);
    const auto& directDescriptors = directBudgetLedger.category(
        arp::rhi::semantics::PhysicalBudgetCategory::DirectDescriptorArenas);
    assert(directPipelines.observed()
        && directPipelines.unit == arp::rhi::semantics::PhysicalBudgetUnit::PipelineEntries
        && directPipelines.capacity == 1u && directPipelines.current == 1u
        && directPipelines.rejectionCount == 1u && directPipelines.liveCount == 1u);
    assert(directDescriptors.observed()
        && directDescriptors.unit == arp::rhi::semantics::PhysicalBudgetUnit::Descriptors
        && directDescriptors.capacity == 4u && directDescriptors.current == 4u
        && directDescriptors.rejectionCount == 1u && directDescriptors.liveCount == 1u);
    directLayout.reset();
    directArena.reset();
    assert(boundedDevice->createPipelineLayout({}));
    assert(boundedDevice->createDescriptorArena({"MetalDirectObjectBudgetRecovered", 1u, 4u}));

    // Native Metal framebuffer-fetch keeps InputAttachment as a logical
    // descriptor type. Allocation is therefore valid; the descriptor write
    // still requires a concrete render-attachment resource and reflection
    // mapping before it can be consumed.
    DescriptorLayoutDesc inputLayout;
    inputLayout.name = "MetalInputAttachment";
    inputLayout.bindings.push_back({0, DescriptorType::InputAttachment, ShaderStage::Fragment});
    PipelineLayoutDesc inputPipelineLayout;
    inputPipelineLayout.descriptorLayouts.push_back(inputLayout);
    inputPipelineLayout.descriptorSets.push_back({0, inputLayout.name});
    assert(createMetalRhiPipelineLayout(inputPipelineLayout));
    auto inputArena = createMetalRhiDescriptorArena({"MetalInputAttachmentArena", 1, 1}, nullptr);
    assert(inputArena);
    assert(inputArena->allocateSet(inputLayout).success);

    return 0;
}

void assertMetalNativeAliasingAndCompletionContract()
{
#if defined(ARP_SOURCE_DIR)
    const auto root = std::filesystem::path(ARP_SOURCE_DIR);
    const auto metalRoot = root / "src" / "rhi" / "backends" / "metal";
    const auto device = readText(metalRoot / "metal_device.cpp");
    const auto commands = readText(metalRoot / "metal_native_commands.mm");
    const auto commandStub = readText(metalRoot / "metal_native_commands_stub.cpp");
    const auto resources = readText(metalRoot / "metal_native_resources.mm");
    const auto resourceStub = readText(metalRoot / "metal_native_resources_stub.cpp");

    assert(resources.find("MTLHeapTypePlacement") != std::pmr::string::npos);
    assert(resources.find("newTextureWithDescriptor:textureDesc") != std::pmr::string::npos);
    assert(resources.find("offset:offsetBytes") != std::pmr::string::npos);
    assert(commands.find("addCompletedHandler") != std::pmr::string::npos);
    assert(commands.find("completion->complete(false)") != std::pmr::string::npos);
    assert(
        commands.find(
            "completionState->complete(commandBufferSucceeded && indirectLifetimeValid && canonicalLifetimeValid && closedLifetimeValid && originalLifetimeValid)")
        != std::pmr::string::npos);
    assert(
        commands.find(
            "GpuIndirectOutputLifetimeContract::validateTransition((*submittedIndirect)->status(),after,true)")
        != std::pmr::string::npos);
    assert(
        commands.find(
            "GpuIndirectOutputLifetimeContract::validateTransition((*submittedCanonical)->status(),after,true)")
        != std::pmr::string::npos);
    assert(
        commands.find(
            "commandBufferSucceeded && canonicalLifetimeValid) submittedCanonicalLease->markFaultReadbackCompleted()")
        != std::pmr::string::npos);
    assert(commands.find("if (canonicalProducerStates_) canonicalProducerStates_->commit()")
        != std::pmr::string::npos);
    assert(
        commands.find(
            "GpuIndirectOutputLifetimeContract::validateTransition(reservation->status(),after,true)")
        != std::pmr::string::npos);
    const auto closedTransfer =
        commands.find("submissionRetention_->closedConsumer=closedConsumerNative_");
    const auto closedCallback = commands.find("if (*submittedClosed)", closedTransfer);
    const auto closedRetired = commands.find("submittedClosed->reset()", closedCallback);
    const auto completionPublished =
        commands.find("completionState->complete(commandBufferSucceeded", closedRetired);
    assert(closedTransfer != std::pmr::string::npos && closedCallback > closedTransfer
        && closedRetired > closedCallback && completionPublished > closedRetired
        && completionPublished != std::pmr::string::npos);
    assert(
        commands.find(
            "reservation->producerLease_.release(); reservation->consumerLease_.release(); reservation->packetLease_.release()",
            closedCallback)
        < closedRetired);
    assert(commands.find("if (committingClosed) committingClosed->states()->commit()")
        != std::pmr::string::npos);

    assert(commandStub.find("RHI-METAL-CANONICAL-PRODUCER-NATIVE-RUNTIME-UNAVAILABLE")
        != std::pmr::string::npos);
    assert(commands.find("updateFence:fence") != std::pmr::string::npos);
    assert(commands.find("waitForFence:fence") != std::pmr::string::npos);
    assert(commands.find("makeAliasable") != std::pmr::string::npos);
    assert(device.find("return createTexture(desc)") == std::pmr::string::npos);
    assert(device.find("metalRhiNativePlacementAliasingAvailable()") != std::pmr::string::npos);
    assert(device.find("metalRhiNativeCompletionAndAliasingCommandsAvailable()")
        != std::pmr::string::npos);
    assert(commandStub.find("RHI-METAL-COMPLETION-NATIVE-RUNTIME-UNAVAILABLE")
        != std::pmr::string::npos);
    assert(commandStub.find("RHI-METAL-ALIAS-BARRIER-NATIVE-RUNTIME-UNAVAILABLE")
        != std::pmr::string::npos);
    assert(resourceStub.find("metalRhiNativePlacementAliasingAvailable") != std::pmr::string::npos);
#if defined(__MINGW32__) || defined(__MINGW64__)
    assert(!arp::rhi_metal::metalRhiNativePlacementAliasingAvailable());
    assert(!arp::rhi_metal::metalRhiNativeCompletionAndAliasingCommandsAvailable());
#endif
#endif
}

void assertMetalGpuCountedAndPatchListContracts()
{
#if defined(ARP_SOURCE_DIR)
    const auto root = std::filesystem::path(ARP_SOURCE_DIR);
    const auto pipelineHeader = readText(root / "src/rhi/gpu_pipeline.h");
    const auto commandHeader = readText(root / "src/rhi/gpu_commands.h");
    const auto backendPipeline =
        readText(root / "src/rhi/backends/metal/metal_backend_pipelines.cpp");
    const auto nativePipeline = readText(root / "src/rhi/backends/metal/metal_native_pipelines.mm");
    const auto nativeCommands = readText(root / "src/rhi/backends/metal/metal_native_commands.mm");
    const auto backendCommands =
        readText(root / "src/rhi/backends/metal/metal_backend_commands.cpp");
    const auto commandLowering = readText(root / "src/rhi/semantics/native_command_lowering.cpp");
    const auto codegen = readText(root / "scripts/compile_shaders.py");
    const auto device = readText(root / "src/rhi/backends/metal/metal_device.cpp");

    assert(pipelineHeader.find("hasMetalGpuCountedIndirectMaskAbi") != std::pmr::string::npos);
    assert(pipelineHeader.find("hasMetalTessellationAbi") != std::pmr::string::npos);
    assert(backendPipeline.find("RHI-METAL-GPU-COUNTED-INDIRECT-REFLECTION-ABI-INVALID")
        != std::pmr::string::npos);
    assert(backendPipeline.find("RHI-METAL-PATCH-LIST-TESSELLATION-ABI-NOT-LOWERED")
        != std::pmr::string::npos);
    assert(nativePipeline.find("metalTessellationVertexEntryPoint") != std::pmr::string::npos);
    assert(nativePipeline.find("tessellationFactorFormat") != std::pmr::string::npos);
    assert(commandHeader.find("bindTessellationFactorBuffer") != std::pmr::string::npos);
    assert(backendCommands.find("RHI-METAL-PATCH-DRAW-REQUIRES-TESSELLATION-FACTOR-BUFFER")
        != std::pmr::string::npos);
    assert(commandLowering.find("RHI-METAL-PATCH-INDIRECT-DRAW-LOWERING-UNAVAILABLE")
        != std::pmr::string::npos);
    assert(nativeCommands.find("setTessellationFactorBuffer") != std::pmr::string::npos);
    assert(nativeCommands.find("drawPatches:") != std::pmr::string::npos);
    assert(nativeCommands.find("drawIndexedPatches:") != std::pmr::string::npos);
    assert(codegen.find("ARP_METAL_GPU_COUNTED_INDIRECT_MASK") != std::pmr::string::npos);
    assert(codegen.find("ARP_METAL_TESSELLATION_ABI") != std::pmr::string::npos);
    assert(device.find("caps.features.multiDrawIndirectCount = caps.features.indirectDraw")
        != std::pmr::string::npos);
#endif
}
