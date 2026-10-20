#pragma once

#include "core/cpu_allocator.h"
#include "rhi/gpu_swapchain.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace arp::rhi::semantics::vulkan {

struct VulkanSwapchainImageCountRequest {
    std::uint32_t nativeMinImageCount = 2;
    std::uint32_t nativeMaxImageCount = 0;
    std::uint32_t requestedLogicalFrameSlots = 2;
};

struct VulkanSwapchainImageCountDecision {
    std::uint32_t nativeImageCount = 2;
    std::uint32_t logicalFrameSlots = 2;
    bool nativeSurfaceRequiredMoreImages = false;
    std::pmr::string diagnostics;
};

struct VulkanSwapchainExtentRequest {
    rhi::Extent2D requestedExtent;
    rhi::Extent2D nativeCurrentExtent;
    rhi::Extent2D nativeMinExtent;
    rhi::Extent2D nativeMaxExtent;
    bool nativeCurrentExtentFixed = false;
};

struct VulkanSwapchainExtentDecision {
    bool success = false;
    rhi::Extent2D extent;
    bool drawableExtentUnavailable = false;
    std::pmr::string diagnostics;
};

enum class VulkanSwapchainNativeFrameStatus {
    Success,
    Suboptimal,
    OutOfDate,
    SurfaceLost,
    FatalError
};

struct VulkanSwapchainAcquireStatusDecision {
    bool success = false;
    rhi::AcquireStatus status = rhi::AcquireStatus::Failed;
    bool recoverable = false;
    bool swapchainRebuildRequired = false;
    bool surfaceLost = false;
};

struct VulkanSwapchainPresentStatusDecision {
    bool success = false;
    rhi::PresentStatus status = rhi::PresentStatus::Failed;
    bool recoverable = false;
    bool swapchainRebuildRequired = false;
    bool surfaceLost = false;
};

inline VulkanSwapchainImageCountDecision chooseVulkanSwapchainImageCount(
    const VulkanSwapchainImageCountRequest& request)
{
    VulkanSwapchainImageCountDecision decision;
    const auto requested =
        request.requestedLogicalFrameSlots == 0 ? 2u : request.requestedLogicalFrameSlots;
    decision.logicalFrameSlots = std::min<std::uint32_t>(requested, 2u);

    auto nativeImageCount = std::max(request.nativeMinImageCount, requested);
    if (request.nativeMaxImageCount != 0 && nativeImageCount > request.nativeMaxImageCount) {
        nativeImageCount = request.nativeMaxImageCount;
    }
    decision.nativeImageCount = nativeImageCount;

    if (nativeImageCount > requested) {
        decision.nativeSurfaceRequiredMoreImages = true;
        decision.diagnostics =
            "native surface requires more images than the L2 double-buffer request";
    } else if (nativeImageCount < 2) {
        decision.logicalFrameSlots = nativeImageCount;
        decision.diagnostics = "native surface supports less than two swapchain images";
    }

    return decision;
}

inline bool isNonZeroExtent(const rhi::Extent2D& extent)
{
    return extent.width > 0 && extent.height > 0;
}

inline VulkanSwapchainExtentDecision chooseVulkanSwapchainExtent(
    const VulkanSwapchainExtentRequest& request)
{
    VulkanSwapchainExtentDecision decision;
    if (!isNonZeroExtent(request.requestedExtent)) {
        decision.diagnostics = "Vulkan swapchain lowering requires a non-zero requested extent";
        return decision;
    }

    if (request.nativeCurrentExtentFixed) {
        if (!isNonZeroExtent(request.nativeCurrentExtent)) {
            decision.drawableExtentUnavailable = true;
            decision.diagnostics = "Vulkan swapchain lowering rejected zero native surface extent";
            return decision;
        }
        decision.success = true;
        decision.extent = request.nativeCurrentExtent;
        return decision;
    }

    if (!isNonZeroExtent(request.nativeMaxExtent)
        || request.nativeMinExtent.width > request.nativeMaxExtent.width
        || request.nativeMinExtent.height > request.nativeMaxExtent.height) {
        decision.diagnostics =
            "Vulkan swapchain lowering requires valid native surface extent bounds";
        return decision;
    }

    decision.extent = {
        std::max(request.nativeMinExtent.width,
            std::min(request.nativeMaxExtent.width, request.requestedExtent.width)),
        std::max(request.nativeMinExtent.height,
            std::min(request.nativeMaxExtent.height, request.requestedExtent.height)),
    };
    if (!isNonZeroExtent(decision.extent)) {
        decision.drawableExtentUnavailable = true;
        decision.diagnostics = "Vulkan swapchain lowering resolved zero native surface extent";
        return decision;
    }

    decision.success = true;
    return decision;
}

inline VulkanSwapchainAcquireStatusDecision lowerVulkanSwapchainAcquireStatus(
    const VulkanSwapchainNativeFrameStatus status)
{
    VulkanSwapchainAcquireStatusDecision decision;
    switch (status) {
    case VulkanSwapchainNativeFrameStatus::Success:
        decision.success = true;
        decision.status = rhi::AcquireStatus::Acquired;
        break;
    case VulkanSwapchainNativeFrameStatus::Suboptimal:
        decision.success = true;
        decision.status = rhi::AcquireStatus::Suboptimal;
        break;
    case VulkanSwapchainNativeFrameStatus::OutOfDate:
        decision.status = rhi::AcquireStatus::OutOfDate;
        break;
    case VulkanSwapchainNativeFrameStatus::SurfaceLost:
        decision.status = rhi::AcquireStatus::SurfaceLost;
        break;
    case VulkanSwapchainNativeFrameStatus::FatalError:
        decision.status = rhi::AcquireStatus::Failed;
        break;
    }
    decision.recoverable = rhi::acquireStatusIsRecoverable(decision.status);
    decision.swapchainRebuildRequired = rhi::acquireStatusRequiresSwapchainRebuild(decision.status);
    decision.surfaceLost = decision.status == rhi::AcquireStatus::SurfaceLost;
    return decision;
}

inline VulkanSwapchainPresentStatusDecision lowerVulkanSwapchainPresentStatus(
    const VulkanSwapchainNativeFrameStatus status)
{
    VulkanSwapchainPresentStatusDecision decision;
    switch (status) {
    case VulkanSwapchainNativeFrameStatus::Success:
        decision.success = true;
        decision.status = rhi::PresentStatus::Presented;
        break;
    case VulkanSwapchainNativeFrameStatus::Suboptimal:
        decision.success = true;
        decision.status = rhi::PresentStatus::Suboptimal;
        break;
    case VulkanSwapchainNativeFrameStatus::OutOfDate:
        decision.status = rhi::PresentStatus::OutOfDate;
        break;
    case VulkanSwapchainNativeFrameStatus::SurfaceLost:
        decision.status = rhi::PresentStatus::SurfaceLost;
        break;
    case VulkanSwapchainNativeFrameStatus::FatalError:
        decision.status = rhi::PresentStatus::Failed;
        break;
    }
    decision.recoverable = rhi::presentStatusIsRecoverable(decision.status);
    decision.swapchainRebuildRequired = rhi::presentStatusRequiresSwapchainRebuild(decision.status);
    decision.surfaceLost = decision.status == rhi::PresentStatus::SurfaceLost;
    return decision;
}

} // namespace arp::rhi::semantics::vulkan
