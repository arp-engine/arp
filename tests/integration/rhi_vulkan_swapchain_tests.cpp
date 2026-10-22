#include "../fixtures/frame_input_lifecycle_checks.h"
#include "core/memory/cpu_allocator.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/vulkan/vulkan_device.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/gpu_swapchain.h"
#include "rhi/semantics/vulkan_device_capability_query.h"

#include <array>
#include <cassert>
#include <memory>

namespace {

arp::rhi::SwapchainDesc makeSwapchainDesc(const char* label)
{
    return {
        label,
        {640u, 360u},
        arp::rhi::TextureFormat::Bgra8Unorm,
        2u,
    };
}

void assertTwoFrameSlotLifecycle(arp::rhi::GpuDevice& device, const char* label)
{
    auto swapchain = device.createSwapchain(makeSwapchainDesc(label));
    assert(swapchain);
    assert(swapchain->imageCount() >= 2u);
    assert(swapchain->maxFramesInFlight() == 2u);

    arp::rhi::RhiFrameContractValidator validator({2u});

    const auto frame0 = swapchain->acquireNextFrame();
    assert(validator.validateAcquireResult(frame0).success);
    assert(frame0.frameSerial == 1u);
    assert(frame0.frameIndex == 0u);

    const auto frame1 = swapchain->acquireNextFrame();
    assert(validator.validateAcquireResult(frame1).success);
    assert(frame1.frameSerial == 2u);
    assert(frame1.frameIndex == 1u);

    const auto exhausted = swapchain->acquireNextFrame();
    assert(!exhausted.success);
    assert(exhausted.diagnostics.find("frame slot") != std::pmr::string::npos);

    const auto present0 = swapchain->present(frame0);
    assert(validator.validatePresentResult(frame0, present0).success);

    const auto reused0 = swapchain->acquireNextFrame();
    assert(validator.validateAcquireResult(reused0).success);
    assert(reused0.frameSerial == 3u);
    assert(reused0.frameIndex == 0u);

    const auto stalePresent0 = swapchain->present(frame0);
    assert(!stalePresent0.success);
    assert(stalePresent0.diagnostics.find("present") != std::pmr::string::npos);

    const auto present1 = swapchain->present(frame1);
    assert(validator.validatePresentResult(frame1, present1).success);
    const auto presentReused0 = swapchain->present(reused0);
    assert(validator.validatePresentResult(reused0, presentReused0).success);
}

void assertAcquiredSwapchainImageContract(arp::rhi::GpuDevice& device, const char* label)
{
    auto swapchain = device.createSwapchain(makeSwapchainDesc(label));
    assert(swapchain);

    arp::rhi::RhiFrameContractValidator validator({2u});

    const auto frame = swapchain->acquireNextFrame();
    assert(validator.validateAcquireResult(frame).success);

    auto image = swapchain->acquiredImage(frame);
    assert(image.success);
    assert(image.diagnostics.empty());
    assert(image.texture);
    assert(image.texture->desc().label.find("SwapchainImage") != std::pmr::string::npos);
    assert(image.texture->desc().initialState == arp::rhi::ResourceState::Undefined);
    assert(!image.texture->allocation().nativeHandleExposed);
    assert(validator.validateSwapchainImage(image.ref).success);
    assert(image.ref.image == image.texture->id());
    assert(image.ref.imageIndex == frame.frameIndex);
    assert(image.ref.frameSerial == frame.frameSerial);
    assert(image.ref.extent.width == 640u);
    assert(image.ref.extent.height == 360u);
    assert(image.ref.format == arp::rhi::TextureFormat::Bgra8Unorm);
    assert(image.ref.state == arp::rhi::ResourceState::Present);

    const auto presented = swapchain->present(frame);
    assert(validator.validatePresentResult(frame, presented).success);

    auto staleImage = swapchain->acquiredImage(frame);
    assert(!staleImage.success);
    assert(staleImage.diagnostics.find("acquired frame") != std::pmr::string::npos);

    // Keep the old wrapper alive while its frame slot is reused. The old ID
    // must not resolve to the newly acquired presentation resource.
    std::array<arp::rhi::GpuResourceId, 4u> subsequentIds{};
    for (std::size_t i = 0u; i < subsequentIds.size(); ++i) {
        const auto nextFrame = swapchain->acquireNextFrame();
        assert(nextFrame.success);
        auto nextImage = swapchain->acquiredImage(nextFrame);
        assert(nextImage.success && nextImage.texture);
        subsequentIds[i] = nextImage.texture->id();
        assert(subsequentIds[i] != image.texture->id());
        for (std::size_t j = 0u; j < i; ++j)
            assert(subsequentIds[j] != subsequentIds[i]);
        assert(swapchain->present(nextFrame).success);
    }
}

arp::rhi::semantics::vulkan::VulkanDeviceCapabilitySnapshot makeVulkanCapabilitySnapshot()
{
    arp::rhi::semantics::vulkan::VulkanDeviceCapabilitySnapshot snapshot;
    snapshot.queriedFromNativeDevice = true;
    snapshot.queriedExtendedFeatures = true;
    snapshot.maxPushConstantSizeBytes = 256;
    snapshot.supportsDescriptorIndexing = true;
    snapshot.supportsTimelineSynchronization = true;
    snapshot.supportsSecondaryCommandBuffers = true;
    snapshot.queueFamilyCount = 2;
    snapshot.hasDeviceLocalMemory = true;
    snapshot.hasHostVisibleMemory = true;
    return snapshot;
}

arp::core::UniquePtr<arp::rhi::GpuDevice> makeSyntheticVulkanDevice()
{
    const auto snapshot = makeVulkanCapabilitySnapshot();

    arp::rhi_vulkan::VulkanRhiRuntimeDesc runtimeDesc;
    runtimeDesc.label = "RHIVulkanSwapchainSyntheticRuntime";
    runtimeDesc.enableSyntheticRuntime = true;
    runtimeDesc.nativeHandlesPrivate = true;
    runtimeDesc.queueFamilyCount = snapshot.queueFamilyCount;
    runtimeDesc.hasDeviceLocalMemory = snapshot.hasDeviceLocalMemory;
    runtimeDesc.hasHostVisibleMemory = snapshot.hasHostVisibleMemory;
    const auto runtime = arp::rhi_vulkan::createVulkanRhiRuntime(runtimeDesc);
    assert(runtime);

    arp::rhi_vulkan::VulkanRhiDeviceDesc desc;
    desc.label = "RHIVulkanSwapchainSyntheticDevice";
    desc.capabilityContract =
        arp::rhi::semantics::vulkan::buildVulkanNativeCapabilityContract(snapshot);
    desc.memoryBudgetBytes = 64u * 1024u * 1024u;
    desc.runtime = runtime;
    desc.requireNativeResourceAllocation = true;
    desc.allowNativeHandleExposureToUpperLayers = false;
    desc.allowBlockingFrameWaits = false;
    desc.allowPerDrawPipelineCreation = false;
    desc.allowPerFrameShaderCompilation = false;

    const auto report = arp::rhi_vulkan::describeVulkanRhiDevice(desc);
    assert(report.success);
    assert(report.backendOwnedSwapchain);
    assert(!report.nativeHandleExposedToUpperLayer);

    return arp::rhi_vulkan::createVulkanRhiDevice(desc);
}

arp::core::UniquePtr<arp::rhi::GpuDevice> makeSemanticVulkanDevice()
{
    const auto snapshot = makeVulkanCapabilitySnapshot();

    arp::rhi_vulkan::VulkanRhiDeviceDesc desc;
    desc.label = "RHIVulkanSwapchainSemanticDevice";
    desc.capabilityContract =
        arp::rhi::semantics::vulkan::buildVulkanNativeCapabilityContract(snapshot);
    desc.memoryBudgetBytes = 64u * 1024u * 1024u;
    desc.requireNativeResourceAllocation = false;
    desc.allowNativeHandleExposureToUpperLayers = false;
    desc.allowBlockingFrameWaits = false;
    desc.allowPerDrawPipelineCreation = false;
    desc.allowPerFrameShaderCompilation = false;

    const auto report = arp::rhi_vulkan::describeVulkanRhiDevice(desc);
    assert(report.success);
    assert(report.backendOwnedSwapchain);
    assert(!report.nativeHandleExposedToUpperLayer);

    return arp::rhi_vulkan::createVulkanRhiDevice(desc);
}

void assertResizeAndSurfaceLifecycle(arp::rhi::GpuDevice& device, const char* label)
{
    auto swapchain = device.createSwapchain(makeSwapchainDesc(label));
    assert(swapchain);

    const auto frame = swapchain->acquireNextFrame();
    assert(frame.success);

    const auto surfaceLost = swapchain->setSurfaceAvailable(false);
    assert(surfaceLost.success);
    assert(!surfaceLost.acquireAllowed);
    assert(!surfaceLost.presentAllowed);
    assert(surfaceLost.releaseSurfaceResources);

    const auto acquireWhileLost = swapchain->acquireNextFrame();
    assert(!acquireWhileLost.success);
    assert(acquireWhileLost.diagnostics.find("surface") != std::pmr::string::npos);

    const auto presentWhileLost = swapchain->present(frame);
    assert(!presentWhileLost.success);
    assert(presentWhileLost.diagnostics.find("surface") != std::pmr::string::npos);

    const auto restored = swapchain->setSurfaceAvailable(true);
    assert(restored.success);
    assert(restored.acquireAllowed);
    assert(restored.presentAllowed);
    assert(restored.swapchainRebuildRequired);

    const auto invalidResize = swapchain->resize({0u, 360u});
    assert(!invalidResize.success);
    assert(invalidResize.diagnostics.find("extent") != std::pmr::string::npos);

    const auto resized = swapchain->resize({800u, 450u});
    assert(resized.success);
    assert(resized.swapchainRebuildRequired);
    assert(resized.releaseSurfaceResources);
    assert(resized.extent.width == 800u);
    assert(resized.extent.height == 450u);
    arp::tests::verifyFrameInputLifecycle(*swapchain, makeSwapchainDesc(label));
}

void assertInvalidResizePreservesLifecycleExtent()
{
    auto noRuntime = makeSemanticVulkanDevice();
    assert(noRuntime);
    assert(!noRuntime->createSwapchain(makeSwapchainDesc("NoRuntimeMustReject")));
    // Resize semantics require the explicitly selected validator runtime;
    // clearing requireNativeResourceAllocation cannot create a fake swapchain.
    auto device = makeSyntheticVulkanDevice();
    assert(device);

    auto swapchain = device->createSwapchain(makeSwapchainDesc("RHIVulkanSemanticInvalidResize"));
    assert(swapchain);

    const auto invalidResize = swapchain->resize({0u, 360u});
    assert(!invalidResize.success);
    assert(invalidResize.extent.width == 640u);
    assert(invalidResize.extent.height == 360u);

    const auto resized = swapchain->resize({800u, 450u});
    assert(resized.success);
    assert(resized.extent.width == 800u);
    assert(resized.extent.height == 450u);
}

void assertVulkanSwapchainStatsUseBackendOwnedPath()
{
    auto device = makeSyntheticVulkanDevice();
    assert(device);

    auto swapchain = device->createSwapchain(makeSwapchainDesc("RHIVulkanSwapchainStats"));
    assert(swapchain);

    const auto frame0 = swapchain->acquireNextFrame();
    assert(frame0.success);
    const auto present0 = swapchain->present(frame0);
    assert(present0.success);

    const auto stats = arp::rhi_vulkan::inspectVulkanRhiNativeResourceStats(*device);
    assert(stats.nativeSwapchainCreateCount >= 1u);
    assert(stats.nativeSwapchainAcquireCount >= 1u);
    assert(stats.nativeSwapchainPresentCount >= 1u);
    assert(stats.backendSwapchainAcquireCount >= 1u);
    assert(stats.backendSwapchainPresentCount >= 1u);
    assert(stats.nativeHandlesHiddenFromPublicApi);
}

} // namespace

int main()
{
    auto nullDevice = arp::rhi::createNullDevice();
    assert(nullDevice);
    assertTwoFrameSlotLifecycle(*nullDevice, "RHINullSwapchainLifecycle");
    assertAcquiredSwapchainImageContract(*nullDevice, "RHINullSwapchainImage");
    assertResizeAndSurfaceLifecycle(*nullDevice, "RHINullSwapchainResize");

    auto vulkanDevice = makeSyntheticVulkanDevice();
    assert(vulkanDevice);
    assertTwoFrameSlotLifecycle(*vulkanDevice, "RHIVulkanSwapchainLifecycle");
    assertAcquiredSwapchainImageContract(*vulkanDevice, "RHIVulkanSwapchainImage");
    assertResizeAndSurfaceLifecycle(*vulkanDevice, "RHIVulkanSwapchainResize");
    assertInvalidResizePreservesLifecycleExtent();
    assertVulkanSwapchainStatsUseBackendOwnedPath();

    return 0;
}
