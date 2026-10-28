#pragma once

#include "rhi/gpu_common.h"

#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vulkan/vulkan.h>

namespace arp::rhi_vulkan {
class VulkanRhiNativeCommandPayload;

// Existing physical ceilings, now charged once to the original native runtime.
inline constexpr std::size_t kVulkanMaxAsyncSubmittedCommands = 64u;

// Backend-private CommandStorage/1. The first cold command preparation binds the
// serialized native command owner. Timing snapshots alone are cross-thread.
// Cached handles borrow the runtime's native owner, never retain the runtime.
class VulkanRhiCommandDomain final {
public:
    static constexpr std::size_t reuseCapacity = 8u;

    struct ReusableCommandResources {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        std::uint32_t queueFamilyIndex = 0u;
    };

    const char* bindOwner() noexcept
    {
        unsigned expected = 0u;
        if (ownerState_.compare_exchange_strong(expected, 1u, std::memory_order_acq_rel)) {
            owner_ = std::this_thread::get_id();
            ownerState_.store(2u, std::memory_order_release);
            return nullptr;
        }
        return ownerDiagnostic();
    }

    const char* ownerDiagnostic() const noexcept
    {
        const auto state = ownerState_.load(std::memory_order_acquire);
        if (!state)
            return nullptr;
        if (state != 2u)
            return "RHI-VULKAN-COMMAND-OWNER-BINDING";
        return owner_ == std::this_thread::get_id() ? nullptr : "RHI-VULKAN-COMMAND-OWNER-THREAD";
    }

    std::array<ReusableCommandResources, reuseCapacity> reusableCommands{};
    std::array<VkFence, reuseCapacity> reusableFences{};
    std::array<std::shared_ptr<VulkanRhiNativeCommandPayload>, kVulkanMaxAsyncSubmittedCommands>
        submitted{};
    std::uint64_t completedFrameSerial = 0u;

    struct CompletedTiming {
        std::atomic<std::uint64_t> sequence{0u}, frameSerial{0u};
        std::atomic<rhi::GpuTimingAvailability> availability{rhi::GpuTimingAvailability::Pending};
        std::atomic<const char*> diagnostic{"RHI-VULKAN-TIMING-PUBLICATION-PENDING"};
        std::atomic<std::uint32_t> scopeCount{0u};
        std::array<std::atomic<std::uint64_t>, rhi::GpuTimingSample::kMaxScopes> scopeIds{};
        std::array<std::atomic<float>, rhi::GpuTimingSample::kMaxScopes> scopeMilliseconds{};
    } timing;

private:
    std::atomic<unsigned> ownerState_{0u};
    std::thread::id owner_;
};
} // namespace arp::rhi_vulkan
