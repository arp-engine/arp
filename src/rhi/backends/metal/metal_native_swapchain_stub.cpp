#include "core/memory/cpu_allocator.h"
#include "rhi/backends/metal/metal_native_swapchain.h"

#include <utility>

namespace arp::rhi_metal {

bool MetalRhiNativeSwapchainPayload::ensureNativeLayer(std::pmr::string&)
{
    return true;
}

MetalRhiNativeSwapchainPayload::MetalRhiNativeSwapchainPayload(rhi::SwapchainDesc desc,
    std::shared_ptr<MetalRhiNativeSwapchainStats> stats,
    std::shared_ptr<void> nativeLayer,
    std::shared_ptr<void> nativeQueue)
    : desc_(std::move(desc)), stats_(std::move(stats)), nativeLayer_(std::move(nativeLayer)),
      nativeQueue_(std::move(nativeQueue))
{}

void MetalRhiNativeSwapchainPayload::clearNativeDrawables()
{
    for (auto& drawable : nativeDrawables_) {
        drawable.reset();
    }
}

bool MetalRhiNativeSwapchainPayload::acquireDrawable(
    std::uint32_t frameSlot, std::pmr::string& diagnostics)
{
    if (frameSlot >= nativeDrawables_.size()) {
        diagnostics = "Metal RHI diagnostic drawable acquire frame slot is invalid";
        return false;
    }
    diagnostics = "RHI-METAL-DRAWABLE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeSwapchainPayload::presentDrawable(std::uint32_t frameSlot,
    std::shared_ptr<MetalRhiNativeFrameSlotCompletionState> frameCompletion,
    std::pmr::string& diagnostics)
{
    if (frameSlot >= nativeDrawables_.size() || !frameCompletion) {
        diagnostics = "Metal RHI diagnostic drawable present frame slot is invalid";
        return false;
    }
    diagnostics = "RHI-METAL-DRAWABLE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeSwapchainPayload::discardDrawable(
    std::uint32_t frameSlot, std::pmr::string& diagnostics)
{
    if (frameSlot >= nativeDrawables_.size()) {
        diagnostics = "Metal RHI diagnostic drawable discard frame slot is invalid";
        return false;
    }
    nativeDrawables_[frameSlot].reset();
    return true;
}

bool MetalRhiNativeSwapchainPayload::resizeDrawableLayer(
    const rhi::Extent2D& extent, std::pmr::string& diagnostics)
{
    if (extent.width == 0 || extent.height == 0) {
        diagnostics = "Metal RHI diagnostic drawable resize extent is invalid";
        return false;
    }
    desc_.extent = extent;
    clearNativeDrawables();
    return true;
}

bool MetalRhiNativeSwapchainPayload::setDrawableLayerAvailable(bool available, std::pmr::string&)
{
    layerAvailable_ = available;
    clearNativeDrawables();
    if (stats_) {
        if (available) {
            ++stats_->layerRecreateCount;
        } else {
            ++stats_->layerLossCount;
        }
    }
    return true;
}

bool MetalRhiNativeSwapchainPayload::backgroundPause(std::pmr::string& diagnostics)
{
    (void)diagnostics;
    layerAvailable_ = false;
    clearNativeDrawables();
    if (stats_) {
        ++stats_->backgroundPauseCount;
    }
    return true;
}

bool MetalRhiNativeSwapchainPayload::foregroundResume(std::pmr::string& diagnostics)
{
    (void)diagnostics;
    layerAvailable_ = true;
    if (stats_) {
        ++stats_->foregroundResumeCount;
    }
    return true;
}

bool MetalRhiNativeSwapchainPayload::memoryWarning(std::pmr::string&)
{
    clearNativeDrawables();
    if (stats_) {
        ++stats_->memoryWarningCount;
    }
    return true;
}

bool MetalRhiNativeSwapchainPayload::consumePresentFailure(std::pmr::string&)
{
    return false;
}

bool MetalRhiNativeSwapchainPayload::reconfigureDrawableLayer(
    const rhi::SwapchainDesc& desc, std::pmr::string& diagnostics)
{
    if (desc.extent.width == 0 || desc.extent.height == 0) {
        diagnostics = "Metal RHI diagnostic drawable reconfigure extent is invalid";
        return false;
    }
    desc_ = desc;
    clearNativeDrawables();
    return true;
}

std::shared_ptr<MetalRhiNativeTexturePayload>
MetalRhiNativeSwapchainPayload::acquiredDrawableTexture(
    std::uint32_t, std::pmr::string& diagnostics) const
{
    diagnostics = "RHI-METAL-DRAWABLE-TEXTURE-NATIVE-RUNTIME-UNAVAILABLE";
    return nullptr;
}

std::shared_ptr<MetalRhiNativeSwapchainStats> createMetalRhiNativeSwapchainStats()
{
    auto stats = arp::core::makeShared<MetalRhiNativeSwapchainStats>();
    stats->backendPayloadPresent = true;
    stats->nativeRuntimeDiagnosticOnly = true;
    stats->boundedFrameSlotCount = 2;
    return stats;
}

std::shared_ptr<MetalRhiNativeSwapchainPayload> createMetalRhiNativeSwapchainPayload(
    const rhi::SwapchainDesc& desc, std::shared_ptr<MetalRhiNativeSwapchainStats> stats)
{
    if (!stats) {
        stats = createMetalRhiNativeSwapchainStats();
    }
    stats->backendPayloadPresent = true;
    stats->nativeRuntimeDiagnosticOnly = true;
    stats->boundedFrameSlotCount = 2;
    return arp::core::makeShared<MetalRhiNativeSwapchainPayload>(
        desc, std::move(stats), nullptr, nullptr);
}

} // namespace arp::rhi_metal
