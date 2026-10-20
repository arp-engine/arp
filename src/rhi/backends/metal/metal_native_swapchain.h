#pragma once

#include "core/cpu_allocator.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "rhi/gpu_swapchain.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace arp::rhi_metal {

struct MetalRhiNativeSwapchainStats {
    bool backendPayloadPresent = false;
    bool nativeRuntimeBacked = false;
    bool nativeRuntimeDiagnosticOnly = true;
    bool nativeHandleExposedToUpperLayer = false;
    std::uint32_t nativeLayerCreateCount = 0;
    std::uint32_t nativeDrawableAcquireCount = 0;
    std::uint32_t nativeDrawablePresentCount = 0;
    std::uint32_t nativeDrawablePresentCompletionCount = 0;
    std::uint32_t nativeDrawablePresentFailureCount = 0;
    std::uint32_t nativeDrawablePresentPendingCount = 0;
    std::uint32_t nativeResizeCount = 0;
    std::uint32_t layerLossCount = 0;
    std::uint32_t layerRecreateCount = 0;
    std::uint32_t backgroundPauseCount = 0;
    std::uint32_t foregroundResumeCount = 0;
    std::uint32_t memoryWarningCount = 0;
    std::uint32_t boundedFrameSlotCount = 0;
    std::pmr::string lastPresentDiagnostics;
};

class MetalRhiNativePresentCompletionState final {
public:
    void submitted() noexcept { pending_.fetch_add(1u, std::memory_order_relaxed); }

    void completed(bool success, const char* diagnostics)
    {
        pending_.fetch_sub(1u, std::memory_order_relaxed);
        if (success) {
            completions_.fetch_add(1u, std::memory_order_relaxed);
            return;
        }
        failures_.fetch_add(1u, std::memory_order_relaxed);
        totalFailures_.fetch_add(1u, std::memory_order_relaxed);
        std::scoped_lock lock(mutex_);
        lastDiagnostics_ = diagnostics ? diagnostics : "Metal drawable present failed";
    }

    std::uint32_t pendingCount() const noexcept { return pending_.load(std::memory_order_acquire); }

    bool consumeFailure(std::pmr::string& diagnostics)
    {
        if (failures_.load(std::memory_order_acquire) == 0u) {
            return false;
        }
        failures_.fetch_sub(1u, std::memory_order_acq_rel);
        std::scoped_lock lock(mutex_);
        diagnostics = lastDiagnostics_;
        return true;
    }

    std::uint32_t completionCount() const noexcept
    {
        return completions_.load(std::memory_order_acquire);
    }

    std::uint32_t failureCount() const noexcept
    {
        return totalFailures_.load(std::memory_order_acquire);
    }

private:
    std::atomic<std::uint32_t> pending_ = 0;
    std::atomic<std::uint32_t> completions_ = 0;
    std::atomic<std::uint32_t> failures_ = 0;
    std::atomic<std::uint32_t> totalFailures_ = 0;
    std::mutex mutex_;
    std::string lastDiagnostics_;
};

/// Backend-private completion credential for exactly one logical frame slot.
/// A slot may be recycled only after the native present command, which is
/// ordered after that frame's render command on the Metal queue, completes.
class MetalRhiNativeFrameSlotCompletionState final {
public:
    void complete() noexcept { completed_.store(true, std::memory_order_release); }

    bool isComplete() const noexcept { return completed_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> completed_ = false;
};

class MetalRhiNativeSwapchainPayload {
public:
    MetalRhiNativeSwapchainPayload(rhi::SwapchainDesc desc,
        std::shared_ptr<MetalRhiNativeSwapchainStats> stats,
        std::shared_ptr<void> nativeLayer,
        std::shared_ptr<void> nativeQueue);

    bool acquireDrawable(std::uint32_t frameSlot, std::pmr::string& diagnostics);
    bool discardDrawable(std::uint32_t frameSlot, std::pmr::string& diagnostics);
    bool presentDrawable(std::uint32_t frameSlot,
        std::shared_ptr<MetalRhiNativeFrameSlotCompletionState> frameCompletion,
        std::pmr::string& diagnostics);
    bool consumePresentFailure(std::pmr::string& diagnostics);
    bool resizeDrawableLayer(const rhi::Extent2D& extent, std::pmr::string& diagnostics);
    bool reconfigureDrawableLayer(const rhi::SwapchainDesc& desc, std::pmr::string& diagnostics);
    bool setDrawableLayerAvailable(bool available, std::pmr::string& diagnostics);
    bool backgroundPause(std::pmr::string& diagnostics);
    bool foregroundResume(std::pmr::string& diagnostics);
    bool memoryWarning(std::pmr::string& diagnostics);

    std::shared_ptr<MetalRhiNativeTexturePayload> acquiredDrawableTexture(
        std::uint32_t frameSlot, std::pmr::string& diagnostics) const;

    std::shared_ptr<MetalRhiNativeSwapchainStats> stats() const { return stats_; }

private:
    void clearNativeDrawables();
    bool ensureNativeLayer(std::pmr::string& diagnostics);

    rhi::SwapchainDesc desc_;
    std::shared_ptr<MetalRhiNativeSwapchainStats> stats_;
    std::shared_ptr<void> nativeLayer_;
    std::shared_ptr<void> nativeQueue_;
    std::shared_ptr<void> nativeDevice_;
    std::shared_ptr<MetalRhiNativePresentCompletionState> presentCompletion_;
    std::array<std::shared_ptr<void>, 2> nativeDrawables_;
    bool layerAvailable_ = true;
};

std::shared_ptr<MetalRhiNativeSwapchainStats> createMetalRhiNativeSwapchainStats();

std::shared_ptr<MetalRhiNativeSwapchainPayload> createMetalRhiNativeSwapchainPayload(
    const rhi::SwapchainDesc& desc, std::shared_ptr<MetalRhiNativeSwapchainStats> stats);

} // namespace arp::rhi_metal
