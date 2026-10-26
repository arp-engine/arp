#include "../fixtures/cpp_source_evidence.h"
#include "../fixtures/depth_stencil_attachment_input_fixture.h"
#include "../fixtures/original_command_sampler_custody_fixture.h"
#include "core/memory/cpu_allocator.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_device.h"
#include "rhi/backends/vulkan/vulkan_native_resources.h"
#include "rhi/backends/vulkan/vulkan_resource_registry.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/frontend/bindless_compute_binding.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/vulkan_device_capability_query.h"
#include "tests/fixtures/retained_allocation_contract.h"
#include "tests/fixtures/sampled_depth_descriptor_contract.h"
#include "tests/fixtures/vulkan_query_only_runtime_owner.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>

// Contract tests must evaluate their expressions in Release builds as well.
// The standard assert macro removes side-effectful calls under NDEBUG, which
// can leave the encoder state untouched and turn a test failure into UB.
#ifdef NDEBUG
#undef assert
#define assert(expression)                                                                         \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            std::fprintf(stderr,                                                                   \
                "RHI Vulkan contract failed: %s (%s:%d)\\n",                                       \
                #expression,                                                                       \
                __FILE__,                                                                          \
                __LINE__);                                                                         \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)
#endif

namespace {

void verifyRegistryLookupPreservesWeakLiveness()
{
    using namespace arp;
    using namespace arp::rhi_vulkan;
    core::DefaultCpuAllocator allocator;
    core::CpuMemoryResource memory(
        allocator, core::CpuAllocationIntent::General, "VulkanRegistryLookupContract");
    VulkanRhiResourceRegistry registry(&memory);
    auto live = core::makeShared<VulkanRhiNativeResourcePayload>();
    auto expired = core::makeShared<VulkanRhiNativeResourcePayload>();
    const auto liveId = rhi::allocateGpuResourceId();
    const auto expiredId = rhi::allocateGpuResourceId();
    registry.registerResource(liveId, live);
    registry.registerResource(expiredId, expired);
    const auto registered = allocator.stats();
    expired.reset();
    // Unrelated lookups do not perform whole-device garbage collection.
    for (unsigned repeat = 0; repeat < 1000u; ++repeat)
        assert(registry.payload(liveId) == live);
    assert(allocator.stats().allocationCount == registered.allocationCount);
    assert(allocator.stats().deallocationCount == registered.deallocationCount);
    assert(!registry.payload(expiredId));
    assert(allocator.stats().deallocationCount == registered.deallocationCount + 1u);
    registry.unregisterResource(liveId);
    assert(!registry.payload(liveId));
    assert(live); // unregister removes identity without destroying a retained owner
    assert(allocator.stats().liveBytes == 0u);

    struct ViewPayload final : VulkanRhiNativeResourcePayload {
        bool backendVulkanTextureViewDescAvailable() const override { return true; }
    };

    auto parent = core::makeShared<VulkanRhiNativeResourcePayload>();
    auto view = core::makeShared<ViewPayload>();
    const auto parentId = rhi::allocateGpuResourceId();
    const auto viewId = rhi::allocateGpuResourceId();
    registry.registerTexture(parentId, parent);
    registry.registerResource(viewId, view, parentId);
    assert(registry.textureViewParentIsLive(viewId));
    registry.unregisterResource(parentId);
    assert(!registry.textureViewParentIsLive(viewId));
    assert(parent); // native retention must not keep a public parent ID live
    registry.registerResource(parentId, parent);
    assert(!registry.textureViewParentIsLive(viewId)); // wrong registered kind
    registry.registerTexture(parentId, parent);
    assert(registry.textureViewParentIsLive(viewId));
    parent.reset();
    assert(!registry.textureViewParentIsLive(viewId));
    assert(!registry.payload(parentId));
    view.reset();
    assert(!registry.payload(viewId));
    assert(allocator.stats().liveBytes == 0u);
}

arp::tests::CppSourceEvidence readText(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream stream;
    stream << file.rdbuf();
    return arp::core::toPmrString(stream.str());
}

void assertVulkanRhiBackendDoesNotDelegateToNull()
{
#ifdef ARP_SOURCE_DIR
    const auto deviceSource = readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_device.cpp");
    assert(deviceSource.find("createNullDevice") == std::pmr::string::npos);
#endif
}

void assertVulkanIndirectDispatchLoweringIsNativeAndFailClosed()
{
#ifdef ARP_SOURCE_DIR
    const auto commandSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_command_objects.cpp");
    const auto nativeHeader =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_native_commands.h");
    const auto nativeSource =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_native_commands.cpp");

    assert(commandSource.find("recordDispatchIndirect") != std::pmr::string::npos);
    assert(commandSource.find("ResourceUsage::IndirectBuffer") != std::pmr::string::npos);
    assert(commandSource.find("sizeof(std::uint32_t) * 3u") != std::pmr::string::npos);
    assert(commandSource.find("resourceRegistry_->payload(indirectBuffer.id())")
        != std::pmr::string::npos);
    assert(
        commandSource.find("pass_.retainResources(&nativeBuffer, 1u)") != std::pmr::string::npos);
    assert(commandSource.find("nativePayload_->retainResources(resources, count)")
        != std::pmr::string::npos);
    assert(commandSource.find("pushState_.onConsume()") != std::pmr::string::npos);
    assert(commandSource.find("indirect dispatch not implemented") == std::pmr::string::npos);
    assert(nativeHeader.find("recordDispatchIndirect") != std::pmr::string::npos);
    assert(nativeSource.find("PFN_vkCmdDispatchIndirect") != std::pmr::string::npos);
    assert(nativeSource.find("vkCmdDispatchIndirect") != std::pmr::string::npos);
#endif
}

void assertVulkanPublicBinarySemaphoreFactoryFailsClosed()
{
#ifdef ARP_SOURCE_DIR
    const auto deviceSource = readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_device.cpp");
    assert(deviceSource.find("public binary semaphore API has no queue-bound Vulkan lowering")
        != std::pmr::string::npos);
    assert(deviceSource.find("return arp::core::makeUnique<DummySemaphore>")
        == std::pmr::string::npos);
    const auto placement =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_heap_placement.cpp");
    assert(deviceSource.find("heapPlacement_.createPlaced(") != std::pmr::string::npos);
    assert(placement.find("RHI-VULKAN-HEAP-RUNTIME-UNAVAILABLE") != std::pmr::string::npos);
    assert(placement.find("runtime_->syntheticRuntime()") != std::pmr::string::npos);
    assert(deviceSource.find("GpuSemaphore> createSemaphore() override") != std::pmr::string::npos);
#endif
}

void assertVulkanRhiRuntimeBoundaryIsOwned()
{
#ifdef ARP_SOURCE_DIR
    const auto deviceHeader = readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_device.h");
    const auto runtimeHeader = readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_runtime.h");
    assert(runtimeHeader.find("struct VulkanRhiRuntimeDesc") != std::pmr::string::npos);
    assert(runtimeHeader.find("class VulkanRhiRuntime") != std::pmr::string::npos);
    assert(runtimeHeader.find("VulkanRhiRuntimeInspection") != std::pmr::string::npos);
    assert(runtimeHeader.find("inspectVulkanRhiRuntime") != std::pmr::string::npos);
    const auto legacyVulkanDeviceToken = std::pmr::string("arp::")
        + "rhi"
          "::VulkanDevice";
    assert(deviceHeader.find(legacyVulkanDeviceToken) == std::pmr::string::npos);
    assert(deviceHeader.find("nativeDevice") == std::pmr::string::npos);
#endif
}

void assertVulkanRhiRuntimeInspectionIsSanitized()
{
    using namespace arp::rhi::semantics::vulkan;
    using namespace arp::rhi_vulkan;

    VulkanRhiRuntimeDesc runtimeDesc;
    runtimeDesc.label = "SanitizedRuntime";
    runtimeDesc.enableSyntheticRuntime = true;
    runtimeDesc.nativeHandlesPrivate = true;
    runtimeDesc.queueFamilyCount = 3;

    const auto runtime = createVulkanRhiRuntime(runtimeDesc);
    assert(runtime);
    const auto inspection = inspectVulkanRhiRuntime(*runtime);
    assert(inspection.success);
    assert(inspection.runtimeOwned);
    assert(inspection.nativeHandlesPrivate);
    assert(inspection.syntheticRuntime);
    assert(!inspection.nativeDeviceAvailable);
    assert(!inspection.nativeSurfaceAvailable);
    assert(inspection.queueFamilyCount == 3u);
    assert(inspection.diagnostics.empty());

    auto leakingRuntimeDesc = runtimeDesc;
    leakingRuntimeDesc.label = "LeakingRuntime";
    leakingRuntimeDesc.nativeHandlesPrivate = false;
    const auto leakingRuntime = createVulkanRhiRuntime(leakingRuntimeDesc);
    assert(leakingRuntime);
    const auto leakingInspection = inspectVulkanRhiRuntime(*leakingRuntime);
    assert(!leakingInspection.success);
    assert(!leakingInspection.nativeHandlesPrivate);
    assert(leakingInspection.diagnostics.find("private") != std::pmr::string::npos);

    VulkanDeviceCapabilitySnapshot snapshot;
    snapshot.queriedFromNativeDevice = true;
    snapshot.queriedExtendedFeatures = true;
    snapshot.maxPushConstantSizeBytes = 256;
    snapshot.supportsDescriptorIndexing = true;
    snapshot.supportsTimelineSynchronization = true;
    snapshot.supportsSecondaryCommandBuffers = true;
    snapshot.queueFamilyCount = 2;
    snapshot.hasDeviceLocalMemory = true;
    snapshot.hasHostVisibleMemory = true;

    VulkanRhiDeviceDesc desc;
    desc.label = "LeakingRuntimeRejected";
    desc.capabilityContract = buildVulkanNativeCapabilityContract(snapshot);
    desc.memoryBudgetBytes = 64u * 1024u * 1024u;
    desc.runtime = leakingRuntime;
    desc.requireNativeResourceAllocation = true;
    const auto report = describeVulkanRhiDevice(desc);
    assert(!report.success);
    assert(report.diagnostics.find("private") != std::pmr::string::npos);
}

VKAPI_ATTR void VKAPI_CALL fakeRuntimeProc()
{}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetInstanceProcAddr(VkInstance, const char*)
{
    return reinterpret_cast<PFN_vkVoidFunction>(&fakeRuntimeProc);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetDeviceProcAddr(VkDevice, const char*)
{
    return reinterpret_cast<PFN_vkVoidFunction>(&fakeRuntimeProc);
}

void assertVulkanRhiRuntimeAcceptsInjectedNativeHandles()
{
    using namespace arp::rhi_vulkan;

    VulkanRhiRuntimeDesc runtimeDesc;
    runtimeDesc.label = "InjectedNativeRuntime";
    runtimeDesc.enableSyntheticRuntime = false;
    runtimeDesc.nativeHandlesPrivate = true;
    runtimeDesc.instance = reinterpret_cast<VkInstance>(std::uintptr_t{0x101});
    runtimeDesc.physicalDevice = reinterpret_cast<VkPhysicalDevice>(std::uintptr_t{0x102});
    runtimeDesc.logicalDevice = reinterpret_cast<VkDevice>(std::uintptr_t{0x103});
    runtimeDesc.graphicsQueue = reinterpret_cast<VkQueue>(std::uintptr_t{0x104});
    runtimeDesc.presentQueue = reinterpret_cast<VkQueue>(std::uintptr_t{0x105});
    runtimeDesc.surface = reinterpret_cast<VkSurfaceKHR>(std::uintptr_t{0x106});
    runtimeDesc.getInstanceProcAddr = &fakeGetInstanceProcAddr;
    runtimeDesc.getDeviceProcAddr = &fakeGetDeviceProcAddr;
    runtimeDesc.graphicsQueueFamilyIndex = 7;
    runtimeDesc.presentQueueFamilyIndex = 9;
    runtimeDesc.queueFamilyCount = 10;
    runtimeDesc.nativeOwner =
        arp::core::makeShared<arp::tests::VulkanQueryOnlyRuntimeOwner>(runtimeDesc);

    const auto runtime = createVulkanRhiRuntime(runtimeDesc);
    assert(runtime);
    assert(runtime->valid());
    assert(!runtime->syntheticRuntime());
    assert(runtime->instance() == runtimeDesc.instance);
    assert(runtime->physicalDevice() == runtimeDesc.physicalDevice);
    assert(runtime->logicalDevice() == runtimeDesc.logicalDevice);
    assert(runtime->graphicsQueue() == runtimeDesc.graphicsQueue);
    assert(runtime->presentQueue() == runtimeDesc.presentQueue);
    assert(runtime->surface() == runtimeDesc.surface);
    assert(runtime->getInstanceProcAddr() == runtimeDesc.getInstanceProcAddr);
    assert(runtime->getDeviceProcAddr() == runtimeDesc.getDeviceProcAddr);
    assert(runtime->getDeviceProcAddr("vkCreateBuffer") != nullptr);
    assert(runtime->graphicsQueueFamilyIndex() == 7u);
    assert(runtime->presentQueueFamilyIndex() == 9u);

    const auto inspection = inspectVulkanRhiRuntime(*runtime);
    assert(inspection.success);
    assert(!inspection.syntheticRuntime);
    assert(inspection.nativeDeviceAvailable);
    assert(inspection.nativeSurfaceAvailable);
    assert(inspection.queueFamilyCount == 10u);
}

void assertVulkanSwapchainSubmitPresentSyncIsBackendOwned()
{
#ifdef ARP_SOURCE_DIR
    const auto resources =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_native_resources.h");
    const auto commands =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_native_commands.cpp");
    const auto commandObjects =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_command_objects.cpp");
    const auto graphicsPass =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_graphics_pass.cpp");
    const auto swapchain = readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_swapchain.cpp");

    assert(resources.find("backendVulkanAcquireSemaphore") != std::pmr::string::npos);
    assert(resources.find("backendVulkanRenderFinishedSemaphore") != std::pmr::string::npos);
    assert(commands.find("submitInfo.waitSemaphoreCount") != std::pmr::string::npos);
    assert(commands.find("submitInfo.pWaitSemaphores") != std::pmr::string::npos);
    assert(commands.find("submitInfo.signalSemaphoreCount") != std::pmr::string::npos);
    assert(commands.find("submitInfo.pSignalSemaphores") != std::pmr::string::npos);
    assert(commands.find("bindSwapchainFrameSync") != std::pmr::string::npos);
    assert(
        commands.find("RHI-VULKAN-SWAPCHAIN-SYNC-CONTRACT-INCOMPLETE") != std::pmr::string::npos);
    assert(commands.find("retainSubmittedCommandForAsyncRetirement") != std::pmr::string::npos);
    assert(commands.find("retireCompletedSubmittedCommands") != std::pmr::string::npos);
    assert(commands.find("vkGetFenceStatus") != std::pmr::string::npos);
    assert(commands.find("submittedFenceCompleted") != std::pmr::string::npos);
    assert(commands.find("completedVulkanRhiSubmittedFrameSerial") != std::pmr::string::npos);
    assert(commands.find("VkTimelineSemaphoreSubmitInfo") != std::pmr::string::npos);
    assert(commands.find("markSubmitted") != std::pmr::string::npos);
    assert(commands.find("rollbackSubmitted") != std::pmr::string::npos);
    assert(commands.find("markCompleted") != std::pmr::string::npos);
    assert(commands.find("vkResetQueryPool") != std::pmr::string::npos
        || readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_query_pool.cpp")
                .find("vkResetQueryPool")
            != std::pmr::string::npos);
    assert(commandObjects.find("retainRecordedStagingUploads") != std::pmr::string::npos);
    assert(
        commandObjects.find("completedVulkanRhiSubmittedFrameSerial()") == std::pmr::string::npos);
    assert(commandObjects.find("nativePayload_->retainResource") != std::pmr::string::npos);
    assert(graphicsPass.find("nativePayload_->retainResource") != std::pmr::string::npos);
    assert(swapchain.find("renderFinished_") != std::pmr::string::npos);
    assert(swapchain.find("completedVulkanRhiSubmittedFrameSerial()") == std::pmr::string::npos);
    assert(swapchain.find(
               "presentInfo.pWaitSemaphores = waitForRenderedFrame ? &renderFinished : nullptr")
        != std::pmr::string::npos);
#endif
}

void assertVulkanSubmittedCommandsDrainBeforeRuntimeTeardown()
{
#ifdef ARP_SOURCE_DIR
    const auto header =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_native_commands.h");
    const auto commands =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_native_commands.cpp");
    const auto device = readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_device.cpp");

    assert(header.find("drainVulkanRhiSubmittedCommandsForShutdown") != std::pmr::string::npos);
    assert(commands.find("drainVulkanRhiSubmittedCommandsForShutdown") != std::pmr::string::npos);
    // Shutdown cleanup cannot discard a pending native closure or manufacture
    // its completion. The real pending-fence test exercises the behavior.
    assert(commands.find("asyncSubmittedCommands().fill({})") == std::pmr::string::npos);
    assert(device.find("~VulkanRhiDevice() override") != std::pmr::string::npos);
    assert(device.find("drainVulkanRhiSubmittedCommandsForShutdown();") != std::pmr::string::npos);
#endif
}

void assertVulkanFloatTextureUploadFormatsAreExplicit()
{
#ifdef ARP_SOURCE_DIR
    const auto commands =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_native_commands.cpp");
    assert(commands.find("case VK_FORMAT_R16G16_SFLOAT:") != std::pmr::string::npos);
    assert(commands.find("case VK_FORMAT_R32_SFLOAT:") != std::pmr::string::npos);
#endif
}

void assertVulkanResourceRegistryDoesNotStrongPinReleasedResources()
{
#ifdef ARP_SOURCE_DIR
    const auto registry =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_resource_registry.h");
    const auto resources = readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_resources.cpp");
    const auto objects =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_backend_objects.h");
    const auto device = readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_device.cpp");

    assert(
        registry.find("std::weak_ptr<VulkanRhiNativeResourcePayload>") != std::pmr::string::npos);
    assert(registry.find("pruneExpired") != std::pmr::string::npos);
    assert(registry.find("entry.payload.lock()") != std::pmr::string::npos);
    assert(registry.find("std::shared_ptr<VulkanRhiNativeResourcePayload> payload;")
        == std::pmr::string::npos);

    const auto textureViewClassBegin = resources.find("class VulkanRhiTextureView final");
    const auto samplerClassBegin = resources.find("class VulkanRhiSampler final");
    const auto anonymousNamespaceEnd = resources.find("} // namespace", samplerClassBegin);
    assert(textureViewClassBegin != std::pmr::string::npos);
    assert(samplerClassBegin != std::pmr::string::npos);
    assert(anonymousNamespaceEnd != std::pmr::string::npos);
    const auto textureViewClass =
        resources.substr(textureViewClassBegin, samplerClassBegin - textureViewClassBegin);
    const auto samplerClass =
        resources.substr(samplerClassBegin, anonymousNamespaceEnd - samplerClassBegin);

    assert(textureViewClass.find("std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload")
        != std::pmr::string::npos);
    assert(textureViewClass.find("nativePayload_(std::move(nativePayload))")
        != std::pmr::string::npos);
    assert(textureViewClass.find("nativePayload() const") != std::pmr::string::npos);
    assert(textureViewClass.find("std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload_")
        != std::pmr::string::npos);
    assert(samplerClass.find("std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload")
        != std::pmr::string::npos);
    assert(samplerClass.find("nativePayload_(std::move(nativePayload))") != std::pmr::string::npos);
    assert(samplerClass.find("nativePayload() const") != std::pmr::string::npos);
    assert(samplerClass.find("std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload_")
        != std::pmr::string::npos);

    const auto textureViewFactoryBegin = objects.find("createVulkanRhiTextureView(");
    const auto samplerFactoryBegin = objects.find("createVulkanRhiSampler(");
    const auto transitionBegin = objects.find("transitionVulkanRhiBuffer(", samplerFactoryBegin);
    assert(textureViewFactoryBegin != std::pmr::string::npos);
    assert(samplerFactoryBegin != std::pmr::string::npos);
    assert(transitionBegin != std::pmr::string::npos);
    const auto textureViewFactory =
        objects.substr(textureViewFactoryBegin, samplerFactoryBegin - textureViewFactoryBegin);
    const auto samplerFactory =
        objects.substr(samplerFactoryBegin, transitionBegin - samplerFactoryBegin);
    assert(textureViewFactory.find(
               "std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload = {}")
        != std::pmr::string::npos);
    assert(samplerFactory.find("std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload = {}")
        != std::pmr::string::npos);
    // Payload overloads live with the backend resource implementations; the
    // public-object header only carries their declarations.
    assert(resources.find("vulkanRhiNativePayload(") != std::pmr::string::npos);
    assert(resources.find("const rhi::GpuTextureView& textureView") != std::pmr::string::npos);
    assert(resources.find("const rhi::GpuSampler& sampler") != std::pmr::string::npos);

    const auto createTextureViewBegin = device.find("createTextureView(");
    const auto createSamplerBegin = device.find("createSampler(", createTextureViewBegin);
    const auto createPipelineLayoutBegin = device.find("createPipelineLayout(", createSamplerBegin);
    assert(createTextureViewBegin != std::pmr::string::npos);
    assert(createSamplerBegin != std::pmr::string::npos);
    assert(createPipelineLayoutBegin != std::pmr::string::npos);
    const auto createTextureViewMethod =
        device.substr(createTextureViewBegin, createSamplerBegin - createTextureViewBegin);
    const auto createSamplerMethod =
        device.substr(createSamplerBegin, createPipelineLayoutBegin - createSamplerBegin);
    assert(createTextureViewMethod.find("auto viewNativePayload") != std::pmr::string::npos);
    assert(createTextureViewMethod.find(
               "createVulkanRhiTextureView(nextResourceId(), texture.id(), resolved,")
        != std::pmr::string::npos);
    assert(createTextureViewMethod.find("viewNativePayload, resourceRegistry_)")
        != std::pmr::string::npos);
    assert(createTextureViewMethod.find(
               "resourceRegistry_->registerResource(view->id(), viewNativePayload, texture.id())")
        != std::pmr::string::npos);
    assert(createSamplerMethod.find("createVulkanRhiSampler(nextResourceId(), desc,")
        != std::pmr::string::npos);
    assert(createSamplerMethod.find("nativeSampler.payload, resourceRegistry_)")
        != std::pmr::string::npos);
#endif
}

void assertVulkanTextureViewAndSamplerRetainNativePayloads()
{
    using namespace arp::rhi;
    using namespace arp::rhi_vulkan;

    auto textureViewPayload = arp::core::makeShared<VulkanRhiNativeResourcePayload>();
    TextureViewDesc viewDesc;
    viewDesc.label = "PayloadRetainedTextureView";
    auto textureView = createVulkanRhiTextureView({101}, {7}, viewDesc, textureViewPayload);
    assert(textureView);
    assert(vulkanRhiNativePayload(*textureView) == textureViewPayload);
    textureViewPayload.reset();
    assert(vulkanRhiNativePayload(*textureView));

    auto samplerPayload = arp::core::makeShared<VulkanRhiNativeResourcePayload>();
    SamplerDesc samplerDesc;
    samplerDesc.label = "PayloadRetainedSampler";
    auto sampler = createVulkanRhiSampler({102}, samplerDesc, samplerPayload);
    assert(sampler);
    assert(vulkanRhiNativePayload(*sampler) == samplerPayload);
    samplerPayload.reset();
    assert(vulkanRhiNativePayload(*sampler));
}

void assertVulkanNativeCommandPayloadRetainsStylizedGraphRenderPasses()
{
#ifdef ARP_SOURCE_DIR
    const auto commandImpl =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_native_commands_impl.h");
    const auto graphicsCommands =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_native_graphics_commands.cpp");
    assert(
        commandImpl.find("std::array<VulkanRhiNativeRenderPassResources, 64> retainedRenderPasses")
        != std::pmr::string::npos);
    assert(graphicsCommands.find(
               "Vulkan RHI native command buffer exceeded retained render-pass capacity")
        != std::pmr::string::npos);
#endif
}

void assertVulkanDefaultTextureTransitionInfersFormatAspect()
{
#ifdef ARP_SOURCE_DIR
    // The texture transition overload that infers the full subresource range
    // (via rhi::defaultTextureSubresourceRange) now lives in the shared
    // CommandEncoderBase final overload rather than each backend's encoder.
    // Verify the base header provides it and that the Vulkan encoder no longer
    // carries a local copy of the delegation.
    const auto baseHeader = readText(ARP_SOURCE_DIR "/src/rhi/frontend/command_encoder_base.h");
    assert(baseHeader.find("defaultTextureSubresourceRange") != std::pmr::string::npos);
    assert(baseHeader.find("makeInferredResourceBarrier(") != std::pmr::string::npos);
    assert(baseHeader.find("texture.id(), defaultTextureSubresourceRange(texture.desc()),")
        != std::pmr::string::npos);

    const auto commandObjects =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_command_objects.cpp");
    assert(
        commandObjects.find(
            "return transition(texture, { texture.id(), rhi::defaultTextureSubresourceRange(texture.desc()), before, after });")
        == std::pmr::string::npos);
    assert(commandObjects.find("return transition(texture, { texture.id(), {}, before, after });")
        == std::pmr::string::npos);
#endif
}

void assertVulkanTextureBarriersTrackExplicitSubresources()
{
#ifdef ARP_SOURCE_DIR
    const auto resources = readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_resources.cpp");
    const auto transitionBegin = resources.find("transitionVulkanRhiTexture(");
    assert(transitionBegin != std::pmr::string::npos);
    const auto transition = resources.substr(transitionBegin);
    assert(transition.find("textureRangeWithin(texture.desc(), desc.textureSubresources)")
        != std::pmr::string::npos);
    assert(transition.find(
               "recordSubresourceState(desc.textureSubresources,desc.before,desc.after,journal)")
        != std::pmr::string::npos);
    assert(
        transition.find("result.diagnostics=diagnostic;return result;") != std::pmr::string::npos);
    assert(resources.find("rhi::common::RecordedTextureStateControl state_")
        != std::pmr::string::npos);
    const auto uploader = readText(ARP_SOURCE_DIR "/src/renderer/terrain_gpu_rhi_uploader.cpp");
    assert(uploader.find("PageArrayInitialLayout") != std::pmr::string::npos);
    assert(
        uploader.find("defaultTextureSubresourceRange(texture.desc())") != std::pmr::string::npos);
#endif
}

void assertVulkanBcCapabilityChecksEveryRhiFormat()
{
#ifdef ARP_SOURCE_DIR
    const auto device = readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_device.cpp");
    assert(device.find("VK_FORMAT_BC1_RGBA_UNORM_BLOCK") != std::pmr::string::npos);
    assert(device.find("VK_FORMAT_BC3_UNORM_BLOCK") != std::pmr::string::npos);
    assert(device.find("VK_FORMAT_BC7_UNORM_BLOCK") != std::pmr::string::npos);
    assert(device.find("VK_FORMAT_BC7_SRGB_BLOCK") != std::pmr::string::npos);
    assert(device.find("VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT") != std::pmr::string::npos);
    assert(device.find("VK_FORMAT_FEATURE_TRANSFER_DST_BIT") != std::pmr::string::npos);
#endif
}

void assertVulkanTransientAliasingOwnsBoundedNativeLifetime()
{
#ifdef ARP_SOURCE_DIR
    const auto device = readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_device.cpp");
    const auto descriptors =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_descriptors.cpp");
    const auto commands =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_native_commands.cpp");

    // Native alias memory is backend-owned, tracked by the same allocator
    // ledger as other Vulkan allocations, and placed images are the only
    // native objects exposed to the common RHI texture facade.
    const auto placement =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_heap_placement.cpp");
    assert(device.find("heapPlacement_.createHeap(") != std::pmr::string::npos);
    assert(device.find("heapPlacement_.createPlaced(") != std::pmr::string::npos);
    assert(device.find("heapPlacement_.query(") != std::pmr::string::npos);
    const auto poolStart = placement.find("class VulkanRhiHeapPlacement::State final");
    const auto poolEnd =
        placement.find("VulkanRhiHeapPlacement::VulkanRhiHeapPlacement", poolStart);
    assert(poolStart != std::pmr::string::npos && poolEnd != std::pmr::string::npos);
    const auto pool = placement.substr(poolStart, poolEnd - poolStart);
    assert(pool.find("VulkanRhiAllocator allocator_;") != std::pmr::string::npos);
    assert(pool.find("allocator_(allocator)") != std::pmr::string::npos);
    assert(pool.find("allocator_.allocate({") != std::pmr::string::npos);
    assert(pool.find("std::shared_ptr<VulkanRhiRuntime> runtime_") != std::pmr::string::npos);
    assert(pool.find("VulkanRhiAllocator* allocator_") == std::pmr::string::npos);
    assert(pool.find("allocator_->") == std::pmr::string::npos);
    assert(pool.find("retireCompletedVulkanRhiSubmittedCommands()") != std::pmr::string::npos);
    assert(pool.find("allocation.use_count() == 1") != std::pmr::string::npos);
    assert(pool.find("allocations_.reserve(") != std::pmr::string::npos);
    assert(pool.find("maxMemoryAllocationCount") != std::pmr::string::npos);
    assert(placement.find("VK_IMAGE_CREATE_ALIAS_BIT") != std::pmr::string::npos);
    assert(placement.find("vkGetImageMemoryRequirements2") != std::pmr::string::npos);
    assert(placement.find("requiresDedicatedAllocation") != std::pmr::string::npos);
    assert(placement.find("estimateTexturePlacementRequirements") == std::pmr::string::npos);

    // These source guards accompany the executable ownership/discard fixture
    // in rhi_vulkan_native_resource_tests. Recorded storage must not regress
    // to a public-wrapper owner or an ever-growing list of old native views.
    assert(descriptors.find("class VulkanDescriptorStorage final") != std::pmr::string::npos);
    assert(descriptors.find("resources(bindingCount,&core::defaultCpuMemoryResource())")
        != std::pmr::string::npos);
    assert(descriptors.find(
               "imageViews(bindingCount, VK_NULL_HANDLE,&core::defaultCpuMemoryResource())")
        != std::pmr::string::npos);
    assert(descriptors.find("RHI-DESCRIPTOR-RECORDED-STORAGE-IN-USE") != std::pmr::string::npos);
    assert(descriptors.find("nativePayload_->resources[index] = resourceRegistry_")
        != std::pmr::string::npos);
    assert(descriptors.find("destroyImageView(device->logicalDevice(), previousImageView, nullptr)")
        != std::pmr::string::npos);
    assert(descriptors.find("retiredImageViews_") == std::pmr::string::npos);

    // Commands release their transient resource keepalive exactly at fence
    // completion, independently of wrapper/destructor timing.
    assert(commands.find("impl_->retainedResources.clear();") != std::pmr::string::npos);
    assert(commands.find("if (status == VK_SUCCESS)") != std::pmr::string::npos);
#endif
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--descriptor-parent") {
#ifdef ARP_SOURCE_DIR
        verifyRegistryLookupPreservesWeakLiveness();
        assertVulkanResourceRegistryDoesNotStrongPinReleasedResources();
        assertVulkanTextureViewAndSamplerRetainNativePayloads();
        std::puts(
            "DescriptorParentLiveness/1 Vulkan registry ownership and view parent guards: PASS");
        return 0;
#else
        return 2;
#endif
    }
    if (argc == 2 && std::string_view(argv[1]) == "--descriptor-storage") {
#ifdef ARP_SOURCE_DIR
        assertVulkanTransientAliasingOwnsBoundedNativeLifetime();
        std::puts("DescriptorRecordedStorage/1 Vulkan source ownership/retirement guards: PASS");
        return 0;
#else
        std::fputs("descriptor storage source guards require ARP_SOURCE_DIR\n", stderr);
        return 2;
#endif
    }
    arp::tests::verifyRetainedAllocationPayload<arp::rhi_vulkan::VulkanRhiNativeResourcePayload>(
        [](auto charge, auto payload) {
            arp::rhi_vulkan::VulkanRhiAllocation allocation;
            allocation.success = true;
            allocation.sizeBytes = 256u;
            allocation.releaseToken = std::move(charge);
            return arp::rhi_vulkan::createVulkanRhiBuffer({1u},
                {"VulkanOwnershipFixture",
                    256u,
                    arp::rhi::ResourceUsage::CopySrc,
                    arp::rhi::MemoryUsage::CpuToGpu,
                    arp::rhi::ResourceState::CopySrc},
                allocation,
                std::move(payload));
        });
    using namespace arp::rhi;
    using namespace arp::rhi::semantics::vulkan;
    using namespace arp::rhi_vulkan;

    assertVulkanRhiBackendDoesNotDelegateToNull();
    assertVulkanIndirectDispatchLoweringIsNativeAndFailClosed();
    assertVulkanPublicBinarySemaphoreFactoryFailsClosed();
    assertVulkanRhiRuntimeBoundaryIsOwned();
    assertVulkanRhiRuntimeInspectionIsSanitized();
    assertVulkanRhiRuntimeAcceptsInjectedNativeHandles();
    assertVulkanSwapchainSubmitPresentSyncIsBackendOwned();
    assertVulkanSubmittedCommandsDrainBeforeRuntimeTeardown();
    assertVulkanFloatTextureUploadFormatsAreExplicit();
    assertVulkanResourceRegistryDoesNotStrongPinReleasedResources();
    verifyRegistryLookupPreservesWeakLiveness();
    assertVulkanTextureViewAndSamplerRetainNativePayloads();
    assertVulkanNativeCommandPayloadRetainsStylizedGraphRenderPasses();
    assertVulkanDefaultTextureTransitionInfersFormatAspect();
    assertVulkanTextureBarriersTrackExplicitSubresources();
    assertVulkanBcCapabilityChecksEveryRhiFormat();
    assertVulkanTransientAliasingOwnsBoundedNativeLifetime();

    VulkanDeviceCapabilitySnapshot snapshot;
    snapshot.queriedFromNativeDevice = true;
    snapshot.queriedExtendedFeatures = true;
    snapshot.maxPushConstantSizeBytes = 256;
    snapshot.supportsDescriptorIndexing = true;
    snapshot.supportsTimelineSynchronization = true;
    snapshot.supportsSecondaryCommandBuffers = true;
    snapshot.queueFamilyCount = 2;
    snapshot.hasDeviceLocalMemory = true;
    snapshot.hasHostVisibleMemory = true;

    VulkanRhiDeviceDesc desc;
    desc.label = "RHIVulkanVerticalSlice";
    // Descriptor liveness requires registered typed payloads. This contract
    // fixture explicitly uses the synthetic runtime, never native evidence.
    VulkanRhiRuntimeDesc fixtureRuntime;
    fixtureRuntime.label = "RHIVulkanVerticalSlice.Validator";
    fixtureRuntime.enableSyntheticRuntime = true;
    desc.runtime = createVulkanRhiRuntime(fixtureRuntime);
    assert(desc.runtime);
    desc.capabilityContract = buildVulkanNativeCapabilityContract(snapshot);
    desc.memoryBudgetBytes = 64u * 1024u * 1024u;
    desc.enableVmaAllocator = true;
    desc.allowNativeHandleExposureToUpperLayers = false;
    desc.allowBlockingFrameWaits = false;
    desc.allowPerDrawPipelineCreation = false;
    desc.allowPerFrameShaderCompilation = false;
    assert(desc.uploadStagingPageSizeBytes == 32u * 1024u * 1024u);
    assert(desc.uploadStagingMaxPageCount == 8u);

    const auto report = describeVulkanRhiDevice(desc);
    assert(report.success);
    assert(report.backend == Backend::Vulkan);
    assert(report.vmaAllocatorIntegrated);
    assert(report.vmaHiddenFromPublicApi);
    assert(report.memoryBudgetTelemetrySanitized);
    assert(report.boundedFrameSynchronization);
    assert(!report.nullDelegateUsed);
    assert(report.backendOwnedResourceObjects);
    assert(report.backendOwnedDescriptorObjects);
    assert(report.backendOwnedPipelineObjects);
    assert(report.backendOwnedCommandObjects);
    assert(report.backendOwnedQueueSubmit);
    assert(report.backendOwnedSwapchain);
    assert(!report.nativeHandleExposedToUpperLayer);
    assert(!report.perDrawPipelineCreationAllowed);
    assert(!report.perFrameShaderCompilationAllowed);

    auto device = createVulkanRhiDevice(desc);
    arp::test::OriginalCommandSamplerCustodyFixture::verify([&]() {
        return createVulkanRhiDevice(desc);
    });
    arp::test::DepthStencilAttachmentInputFixture::verifyContract();
    assert(device);
    arp::test::DepthStencilAttachmentInputFixture::verifyPublicGate(*device);
    assert(device);
    assert(device->capabilities().commandRetention.contractVersion == 0u);
    {
        arp::rhi::CommandEncoderDesc bounded;
        bounded.retentionBudget = arp::rhi::CommandRetentionBudget{1u, true, 2u, 1u, 2u, 0u, 4096u};
        assert(!device->createCommandEncoder(bounded));
    }
    assert(!device->capabilities().features.timelineSync);
    assert(!device->createTimelineSemaphore(0u));
    arp::tests::verifySampledDepthDescriptors(*device);
    assert(device->backend() == Backend::Vulkan);
    auto* bindless = static_cast<arp::rhi::BindlessCrossExtension*>(
        device->queryInterface(arp::rhi::kExtensionBindlessCross));
    assert(bindless);
    for (const auto& combined : bindless->capabilities().combinedSampledStorageTextureLimits)
        assert(!combined);
    assert(bindless->capabilities().deviceIdentity != 0u);
    assert(bindless->capabilities().execution == arp::rhi::BindlessExecution::DiagnosticFailure);
    assert(!bindless->capabilities().limitsQueried);
    assert(!bindless->capabilities().executablePayload);
    assert(!bindless->capabilities().indirectFirstInstance);
    assert(bindless->capabilities().indirectInstanceConsumerContractVersion == 0u);
    assert(bindless->capabilities().zeroBaseIndirectProducerContractVersion == 0u);
    assert(bindless->capabilities().indirectPacketStorageContractVersion == 0u);
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
    arp::rhi::BindlessRequirements bindlessRequest;
    bindlessRequest.deviceIdentity = bindless->capabilities().deviceIdentity;
    arp::rhi::BindlessShaderArtifact bindlessArtifact;
    assert(std::string_view(bindless->plan(bindlessRequest, bindlessArtifact).diagnosticId)
        == "RHI-BINDLESS-OPT-IN-REQUIRED");
    bindlessRequest.optIn = true;
    assert(std::string_view(bindless->plan(bindlessRequest, bindlessArtifact).diagnosticId)
        == "RHI-BINDLESS-EXECUTION-UNAVAILABLE");
    arp::rhi::semantics::ShutdownDrainRequest shutdownDrain;
    shutdownDrain.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    const auto shutdownDrainReport = device->drainForShutdown(shutdownDrain);
    assert(shutdownDrainReport.backend == Backend::Vulkan);
    assert(!shutdownDrainReport.completed());
    assert(shutdownDrainReport.outcome
        == arp::rhi::semantics::ShutdownDrainOutcome::DiagnosticFailure);
    assert(shutdownDrainReport.diagnosticId
        == "ARP-RHI-VULKAN-SHUTDOWN-DRAIN-NATIVE-RUNTIME-UNAVAILABLE");
    const auto caps = device->capabilities();
    assert(caps.backend == Backend::Vulkan);
    // Explicit synthetic objects validate metadata; they do not authenticate
    // native feature queries or advertise executable hardware capabilities.
    assert(!caps.core.graphics);
    assert(!caps.core.compute);
    assert(!caps.explicitFeatures.pushConstants);
    assert(!caps.explicitFeatures.secondaryCommandBuffers);
    assert(!caps.explicitFeatures.explicitBarriers);
    assert(!caps.memory.deviceLocal);
    assert(!caps.memory.hostVisible);
    assert(caps.memory.backendFallbacksHiddenFromUpperLayer);

    auto upload = device->createBuffer({
        "Upload",
        4096,
        ResourceUsage::CopySrc,
        MemoryUsage::CpuToGpu,
        ResourceState::CopySrc,
    });
    auto readback = device->createBuffer({
        "Readback",
        4096,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst,
    });
    auto color = device->createTexture({
        "ColorTarget",
        {64, 64, 1},
        TextureFormat::Bgra8Unorm,
        TextureUsage::RenderAttachment | TextureUsage::CopyDst | TextureUsage::CopySrc,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    assert(upload && readback && color);
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
    assert(!upload->allocation().nativeHandleExposed);
    assert(!color->allocation().nativeHandleExposed);
    assert(device->memoryStats().allocatedBytes > 0);
    assert(device->memoryStats().backendFallbacksHiddenFromUpperLayer);

    auto arena = device->createDescriptorArena({
        "VulkanDescriptorArena",
        2,
        8,
    });
    assert(arena);
    DescriptorLayoutDesc descriptorLayout;
    descriptorLayout.name = "MaterialSet";
    descriptorLayout.bindings.push_back({
        0,
        DescriptorType::UniformBuffer,
        ShaderStage::Vertex | ShaderStage::Fragment,
        1,
        false,
        false,
    });
    auto descriptorSet = arena->allocateDescriptorSet({
        "MaterialSet0",
        descriptorLayout,
    });
    assert(descriptorSet);
    auto descriptorWrite = descriptorSet->write({
        0,
        DescriptorType::UniformBuffer,
        upload->id(),
        DescriptorResourceKind::Buffer,
    });
    assert(descriptorWrite.success);
    assert(descriptorSet->stats().backendFallbacksHiddenFromUpperLayer);
    assert(!descriptorSet->stats().nativeHandleExposedToUpperLayer);
    assert(arena->stats().allocatedSets == 1u);

    auto sampled2DTexture = device->createTexture({
        "VulkanSampled2D",
        {1, 1, 1},
        TextureFormat::Rgba16Float,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead,
    });
    auto sampled2DView = device->createTextureView(*sampled2DTexture,
        {
            "VulkanSampled2DView",
            TextureFormat::Rgba16Float,
            TextureUsage::Sampled,
            TextureViewDimension::Texture2D,
        });
    auto cubeTexture = device->createTexture({
        "VulkanSampledCube",
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
            "VulkanSampledCubeView",
            TextureFormat::Rgba16Float,
            TextureUsage::Sampled,
            TextureViewDimension::TextureCube,
            0,
            1,
            0,
            6,
            TextureAspect::Color,
        });
    assert(sampled2DTexture && sampled2DView && cubeTexture && cubeView);
    DescriptorLayoutDesc cubeDescriptorLayout;
    cubeDescriptorLayout.name = "VulkanCubeMaterialSet";
    cubeDescriptorLayout.bindings.push_back(
        {0, DescriptorType::SampledTexture, ShaderStage::Fragment});
    cubeDescriptorLayout.bindings.back().textureViewDimensionConstrained = true;
    cubeDescriptorLayout.bindings.back().textureViewDimension = TextureViewDimension::TextureCube;
    auto cubeDescriptorSet = arena->allocateDescriptorSet({
        "VulkanCubeMaterialSet0",
        cubeDescriptorLayout,
    });
    assert(cubeDescriptorSet);
    const auto vulkan2DAsCube = cubeDescriptorSet->write({
        0,
        DescriptorType::SampledTexture,
        sampled2DView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(!vulkan2DAsCube.success);
    assert(vulkan2DAsCube.diagnostics.find("texture view dimension") != std::pmr::string::npos);
    assert(cubeDescriptorSet
            ->write({
                0,
                DescriptorType::SampledTexture,
                cubeView->id(),
                DescriptorResourceKind::TextureView,
            })
            .success);
    // Reusing an already validated scalar write may take the Vulkan cache
    // shortcut, but a changed resource must still run view-dimension checks.
    assert(cubeDescriptorSet
            ->write({
                0,
                DescriptorType::SampledTexture,
                cubeView->id(),
                DescriptorResourceKind::TextureView,
            })
            .success);
    const auto changedVulkan2D = cubeDescriptorSet->write({
        0,
        DescriptorType::SampledTexture,
        sampled2DView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(!changedVulkan2D.success);
    assert(changedVulkan2D.diagnostics.find("texture view dimension") != std::pmr::string::npos);

    PipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.pushConstantRanges.push_back({
        ShaderStage::Vertex | ShaderStage::Fragment,
        0,
        16,
    });
    auto pipelineLayout = device->createPipelineLayout(pipelineLayoutDesc);
    assert(pipelineLayout);
    ShaderPackageDesc shaderPackage;
    shaderPackage.label = "VulkanClearFixture";
#ifdef ARP_SHADER_DIR
    shaderPackage.slangModulePath = ARP_SHADER_DIR "/clear.slang";
    shaderPackage.artifacts.spirvPath = ARP_SHADER_DIR "/clear.spv";
    shaderPackage.artifacts.mslPath = ARP_SHADER_DIR "/clear.metal";
    shaderPackage.artifacts.wgslPath = ARP_SHADER_DIR "/clear.wgsl";
    shaderPackage.artifacts.reflectionPath = ARP_SHADER_DIR "/clear.reflection.yaml";
#else
    shaderPackage.slangModulePath = "shaders/clear.slang";
    shaderPackage.artifacts.spirvPath = "shaders/clear.spv";
    shaderPackage.artifacts.mslPath = "shaders/clear.metal";
    shaderPackage.artifacts.wgslPath = "shaders/clear.wgsl";
    shaderPackage.artifacts.reflectionPath = "shaders/clear.reflection.yaml";
#endif
    shaderPackage.entrypoints.push_back({
        shaderPackage.slangModulePath,
        "vertexMain",
        ShaderStage::Vertex,
        ShaderSourceLanguage::Slang,
    });
    shaderPackage.entrypoints.push_back({
        shaderPackage.slangModulePath,
        "fragmentMain",
        ShaderStage::Fragment,
        ShaderSourceLanguage::Slang,
    });
    shaderPackage.reflection.pushConstantByteCount = 16;
    shaderPackage.reflection.pushConstantContractVersion = "AtomicFullBlockPush/1";
    shaderPackage.reflection.pushConstantStages = "FS|VS";
    shaderPackage.reflection.pushConstantMembers.push_back({
        "params",
        "float",
        32,
        4,
        0,
        0,
        0,
        false,
        {},
        0,
        0,
        16,
        0,
    });
    RenderPipelineDesc pipelineDesc;
    pipelineDesc.label = "VulkanTrianglePipeline";
    pipelineDesc.layout = pipelineLayout.get();
    pipelineDesc.shaderPackage = &shaderPackage;
    pipelineDesc.vertexShader = shaderPackage.entrypoints[0];
    pipelineDesc.fragmentShader = shaderPackage.entrypoints[1];
    pipelineDesc.colorFormat = TextureFormat::Bgra8Unorm;
    const auto pipelineReport = device->validateRenderPipeline(pipelineDesc);
    if (!pipelineReport.success) {
        std::cerr << "VulkanTrianglePipeline validation: " << pipelineReport.diagnostics << '\n';
    }
    assert(pipelineReport.success);
    assert(pipelineReport.backend == Backend::Vulkan);
    assert(pipelineReport.pipelineCacheKeyStable);
    auto renderPipeline = device->createRenderPipeline(pipelineDesc);
    assert(renderPipeline);
    assert(!renderPipeline->nativeHandleExposedToUpperLayer());
    {
        const auto before = inspectVulkanRhiNativeResourceStats(*device);
        for (const auto field : {0u, 1u, 2u, 3u}) {
            auto missing = shaderPackage;
            if (field == 0u)
                missing.artifacts.spirvPath.clear();
            if (field == 1u)
                missing.artifacts.reflectionPath.clear();
            if (field == 2u)
                missing.artifacts.spirvPath = "missing-native-factory-artifact.spv";
            if (field == 3u)
                missing.reflection.pushConstantContractVersion = "Unsupported/999";
            auto rejected = pipelineDesc;
            rejected.shaderPackage = &missing;
            assert(!device->createRenderPipeline(rejected));
        }
        auto missing = pipelineDesc;
        missing.shaderPackage = nullptr;
        assert(!device->createRenderPipeline(missing));
        const auto after = inspectVulkanRhiNativeResourceStats(*device);
        assert(before.nativeRenderPipelineCreateCount == after.nativeRenderPipelineCreateCount);
        assert(before.nativeShaderModuleCreateCount == after.nativeShaderModuleCreateCount);
    }

    // P0-B1 consume state machine coverage gap: this Vulkan device contract
    // test exercises runtime boundary inspection, pipeline creation, transfer
    // passes, and swapchain presentation, but does NOT issue any
    // pushConstants / drawIndexed / dispatch sequence. The Vulkan consume
    // state machine (pushState_.hasUnconsumedUpdate / onPush / onConsume in
    // vulkan_command_objects.cpp:227/240/262 and vulkan_graphics_pass.cpp:
    // 223/245/279) is therefore unreachable through this test. The shared
    // semantics are validated by rhi_command_contract_tests.cpp (Null
    // backend) which asserts push->consume->push->push [PC-E013] for both
    // render and compute passes. When this test is registered in the native
    // CMakeLists (currently unregistered), add the same consume-state matrix
    // to cover the Vulkan-specific lowering path.

    // Missing execution runtime must reject. The explicit synthetic runtime
    // above is solely a descriptor/command validator and is tested separately
    // from this no-payload production boundary.
    auto noRuntimeDesc = desc;
    noRuntimeDesc.runtime.reset();
    auto noRuntimeDevice = createVulkanRhiDevice(noRuntimeDesc);
    assert(noRuntimeDevice);
    const auto noRuntimeMemory = noRuntimeDevice->memoryStats();
    assert(!noRuntimeDevice->createBuffer({"NoRuntimeBuffer",
        256u,
        ResourceUsage::UniformBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::Undefined}));
    TextureDesc noRuntimeTexture;
    noRuntimeTexture.label = "NoRuntimeTexture";
    noRuntimeTexture.extent = {4u, 4u, 1u};
    noRuntimeTexture.format = TextureFormat::Rgba8Unorm;
    noRuntimeTexture.usage = TextureUsage::Sampled;
    assert(!noRuntimeDevice->createTexture(noRuntimeTexture));
    assert(!noRuntimeDevice->createSampler({}));
    assert(!noRuntimeDevice->createSwapchain({"NoRuntimeSwapchain", {64u, 64u}}));
    assert(!noRuntimeDevice->createFence());
    assert(!noRuntimeDevice->createSemaphore());
    assert(!noRuntimeDevice->createTimelineSemaphore(0u));
    assert(!noRuntimeDevice->createRenderPipeline(pipelineDesc));
    assert(!noRuntimeDevice->createBufferView(*upload, {"ForeignBufferView"}));
    assert(!noRuntimeDevice->createTextureView(*color, {"ForeignTextureView"}));
    auto noRuntimeArena = noRuntimeDevice->createDescriptorArena({"NoRuntimeArena", 1u, 2u});
    assert(noRuntimeArena);
    assert(!noRuntimeArena->allocateDescriptorSet({"NoRuntimeSet", descriptorLayout}));
    assert(noRuntimeDevice->memoryStats().allocatedBytes == noRuntimeMemory.allocatedBytes);
    assert(noRuntimeDevice->memoryStats().liveResourceCount == noRuntimeMemory.liveResourceCount);
    // Even explicit validator runtimes must not turn an invalid native request
    // into a successful size-only allocation.
    assert(!device->createBuffer({"InvalidUsage",
        256u,
        ResourceUsage::None,
        MemoryUsage::CpuToGpu,
        ResourceState::Undefined}));
    auto encoder = noRuntimeDevice->createCommandEncoder({"VulkanRhiFrame"});
    assert(!encoder);
    auto validator = arp::rhi::createNullDevice();
    auto foreignEncoder = validator->createCommandEncoder({"ForeignCommandPayload"});
    assert(foreignEncoder);
    const auto foreignCommand = foreignEncoder->finish();
    assert(foreignCommand);
    const auto submissionsBefore = noRuntimeDevice->queue().submitCount();
    const auto refusedForeign = noRuntimeDevice->queue().submit(*foreignCommand);
    assert(!refusedForeign.success && !refusedForeign.workAccepted);
    assert(refusedForeign.diagnostics == "RHI-VULKAN-NATIVE-COMMAND-PAYLOAD-MISSING");
    assert(noRuntimeDevice->queue().submitCount() == submissionsBefore);

    auto swapchain = device->createSwapchain({
        "HiddenWindowSurface",
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
    auto nextFrame = swapchain->acquireNextFrame();
    assert(nextFrame.success);
    assert(nextFrame.frameIndex != frame.frameIndex);
    const auto nextPresented = swapchain->present(nextFrame);
    assert(nextPresented.success);

    // FrameInFlightSyncDesc / validateFrameInFlightSync removed during RHI
    // collapse — sync now backend-owned, validated at device creation.

    auto leakingDesc = desc;
    leakingDesc.allowNativeHandleExposureToUpperLayers = true;
    const auto leakingReport = describeVulkanRhiDevice(leakingDesc);
    assert(!leakingReport.success);
    assert(leakingReport.diagnostics.find("native") != std::pmr::string::npos);

    return 0;
}
