#include "core/memory/cpu_allocator.h"
#include "rhi/semantics/vulkan_swapchain_policy.h"

#include <cassert>

int main()
{
    using namespace arp::rhi::semantics::vulkan;

    const auto normalSurface = chooseVulkanSwapchainImageCount({
        2,
        0,
        2,
    });
    assert(normalSurface.nativeImageCount == 2u);
    assert(normalSurface.logicalFrameSlots == 2u);
    assert(!normalSurface.nativeSurfaceRequiredMoreImages);
    assert(normalSurface.diagnostics.empty());

    const auto tripleBufferedSurface = chooseVulkanSwapchainImageCount({
        3,
        4,
        2,
    });
    assert(tripleBufferedSurface.nativeImageCount == 3u);
    assert(tripleBufferedSurface.logicalFrameSlots == 2u);
    assert(tripleBufferedSurface.nativeSurfaceRequiredMoreImages);
    assert(tripleBufferedSurface.diagnostics.find("native surface") != std::pmr::string::npos);

    const auto constrainedSurface = chooseVulkanSwapchainImageCount({
        1,
        1,
        2,
    });
    assert(constrainedSurface.nativeImageCount == 1u);
    assert(constrainedSurface.logicalFrameSlots == 1u);
    assert(constrainedSurface.diagnostics.find("less than two") != std::pmr::string::npos);

    const auto dynamicExtent = chooseVulkanSwapchainExtent({
        {1280u, 720u},
        {0u, 0u},
        {64u, 64u},
        {1024u, 512u},
        false,
    });
    assert(dynamicExtent.success);
    assert(dynamicExtent.extent.width == 1024u);
    assert(dynamicExtent.extent.height == 512u);
    assert(!dynamicExtent.drawableExtentUnavailable);

    const auto minimizedExtent = chooseVulkanSwapchainExtent({
        {1280u, 720u},
        {0u, 0u},
        {1u, 1u},
        {8192u, 8192u},
        true,
    });
    assert(!minimizedExtent.success);
    assert(minimizedExtent.drawableExtentUnavailable);
    assert(
        minimizedExtent.diagnostics.find("zero native surface extent") != std::pmr::string::npos);

    const auto invalidRequestedExtent = chooseVulkanSwapchainExtent({
        {0u, 720u},
        {0u, 0u},
        {1u, 1u},
        {8192u, 8192u},
        false,
    });
    assert(!invalidRequestedExtent.success);
    assert(invalidRequestedExtent.diagnostics.find("non-zero requested extent")
        != std::pmr::string::npos);

    const auto acquired =
        lowerVulkanSwapchainAcquireStatus(VulkanSwapchainNativeFrameStatus::Success);
    assert(acquired.success);
    assert(acquired.status == arp::rhi::AcquireStatus::Acquired);
    assert(!acquired.recoverable);
    assert(!acquired.swapchainRebuildRequired);

    const auto suboptimalAcquire =
        lowerVulkanSwapchainAcquireStatus(VulkanSwapchainNativeFrameStatus::Suboptimal);
    assert(suboptimalAcquire.success);
    assert(suboptimalAcquire.status == arp::rhi::AcquireStatus::Suboptimal);
    assert(suboptimalAcquire.recoverable);
    assert(suboptimalAcquire.swapchainRebuildRequired);

    const auto outOfDateAcquire =
        lowerVulkanSwapchainAcquireStatus(VulkanSwapchainNativeFrameStatus::OutOfDate);
    assert(!outOfDateAcquire.success);
    assert(outOfDateAcquire.status == arp::rhi::AcquireStatus::OutOfDate);
    assert(outOfDateAcquire.recoverable);
    assert(outOfDateAcquire.swapchainRebuildRequired);

    const auto presented =
        lowerVulkanSwapchainPresentStatus(VulkanSwapchainNativeFrameStatus::Success);
    assert(presented.success);
    assert(presented.status == arp::rhi::PresentStatus::Presented);
    assert(!presented.recoverable);
    assert(!presented.swapchainRebuildRequired);

    const auto suboptimalPresent =
        lowerVulkanSwapchainPresentStatus(VulkanSwapchainNativeFrameStatus::Suboptimal);
    assert(suboptimalPresent.success);
    assert(suboptimalPresent.status == arp::rhi::PresentStatus::Suboptimal);
    assert(suboptimalPresent.recoverable);
    assert(suboptimalPresent.swapchainRebuildRequired);

    const auto outOfDatePresent =
        lowerVulkanSwapchainPresentStatus(VulkanSwapchainNativeFrameStatus::OutOfDate);
    assert(!outOfDatePresent.success);
    assert(outOfDatePresent.status == arp::rhi::PresentStatus::OutOfDate);
    assert(outOfDatePresent.recoverable);
    assert(outOfDatePresent.swapchainRebuildRequired);

    const auto surfaceLostPresent =
        lowerVulkanSwapchainPresentStatus(VulkanSwapchainNativeFrameStatus::SurfaceLost);
    assert(!surfaceLostPresent.success);
    assert(surfaceLostPresent.status == arp::rhi::PresentStatus::SurfaceLost);
    assert(surfaceLostPresent.recoverable);
    assert(surfaceLostPresent.surfaceLost);
    return 0;
}
