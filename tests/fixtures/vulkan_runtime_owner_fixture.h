#pragma once

#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "tests/fixtures/vulkan_headless_runtime.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

namespace arp::tests {

// Actual VkDevice teardown lives in the headless bootstrap's retained owner.
// This fixture proves empty native command/cache ownership, not resource or WSI lifetime.
class VulkanRuntimeOwnerFixture final {
public:
    static int run()
    {
        using namespace rhi_vulkan;
        auto first = core::makeUnique<VulkanHeadlessRuntime>();
        VulkanHeadlessRuntime second;
        if (!first->runtime() || !second.runtime())
            return 77;
        {
            VulkanRhiNativeResourceAllocator resources(first->runtime());
            core::pmr::String diagnostic;
            std::array<std::shared_ptr<VulkanRhiNativeCommandPayload>,
                VulkanRhiCommandDomain::reuseCapacity + 3u>
                prepared;
            for (auto& command : prepared) {
                command = createVulkanRhiNativeCommandPayload(resources, diagnostic);
                assert(command && command->end(diagnostic) && command->submit(diagnostic));
            }
            assert(vkQueueWaitIdle(first->runtime()->graphicsQueue()) == VK_SUCCESS);
            retireCompletedVulkanRhiSubmittedCommands(*first->runtime());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            prepared.fill(nullptr);
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            const auto fenceCount = [&] {
                const auto& slots = first->runtime()->commandDomain().reusableFences;
                return std::count_if(slots.begin(), slots.end(), [](VkFence fence) {
                    return fence != VK_NULL_HANDLE;
                });
            };
            assert(fenceCount() == VulkanRhiCommandDomain::reuseCapacity);
            const auto created = resources.stats().nativeCommandPoolCreateCount;
            for (std::size_t i = 0; i < VulkanRhiCommandDomain::reuseCapacity; ++i) {
                prepared[i] = createVulkanRhiNativeCommandPayload(resources, diagnostic);
                assert(
                    prepared[i] && prepared[i]->end(diagnostic) && prepared[i]->submit(diagnostic));
            }
            assert(resources.stats().nativeCommandPoolCreateCount == created);
            assert(fenceCount() == 0u);
            std::thread foreignOwner([&] {
                core::pmr::String refused;
                assert(!createVulkanRhiNativeCommandPayload(resources, refused));
                assert(refused == "RHI-VULKAN-COMMAND-OWNER-THREAD");
            });
            foreignOwner.join();
            assert(resources.stats().nativeCommandPoolCreateCount == created);
            assert(vkQueueWaitIdle(first->runtime()->graphicsQueue()) == VK_SUCCESS);
            retireCompletedVulkanRhiSubmittedCommands(*first->runtime());
        }
        drainVulkanRhiSubmittedCommandsForShutdown(*first->runtime());
        const auto owner = first->nativeOwner();
        const std::weak_ptr<VulkanRhiRuntime> runtime = first->runtime();
        const VkDevice device = first->device();
        core::pmr::String diagnostic;
        std::shared_ptr<VulkanRhiNativeCommandPayload> original;
        std::shared_ptr<VulkanRhiNativeCommandPayload> other;
        {
            VulkanRhiNativeResourceAllocator resources(first->runtime());
            VulkanRhiNativeResourceAllocator otherResources(second.runtime());
            original =
                createVulkanRhiNativeCommandPayload(resources, diagnostic, {"owner first", 1u, 1u});
            other = createVulkanRhiNativeCommandPayload(
                otherResources, diagnostic, {"owner other", 1u, 1u});
        }
        assert(original && other);
        assert(original->end(diagnostic) && other->end(diagnostic));
        auto timelineOwner =
            VulkanRhiTimelineSemaphorePayload::create(first->runtime(), 0u, diagnostic);
        assert(timelineOwner);
        const auto timeline = timelineOwner->nativeHandle();
        VulkanRhiTimelineSubmitInfo wait;
        wait.waitSemaphore = timelineOwner;
        wait.waitValue = 1u;
        bool accepted = false;
        const bool submitted = original->submit(wait, diagnostic, &accepted);
        wait = {};
        timelineOwner.reset();
        const bool pendingBefore = !original->submittedFenceCompleted();
        const std::weak_ptr<VulkanRhiNativeCommandPayload> observedCommand = original;
        first.reset();
        original.reset();
        const bool retainedPending =
            !owner.expired() && !runtime.expired() && !observedCommand.expired();
        bool otherAccepted = false;
        const bool otherSubmitted = other->submit({}, diagnostic, &otherAccepted);
        const bool otherCompleted = other->waitSubmittedFenceUntil(
            std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic);
        retireCompletedVulkanRhiSubmittedCommands(*second.runtime());
        drainVulkanRhiSubmittedCommandsForShutdown(*second.runtime());
        original = observedCommand.lock();
        const bool stillPending = original && !original->submittedFenceCompleted();
        const bool retainedAfterOther = !owner.expired();
        // Always release the real GPU dependency before assertion or teardown.
        VkSemaphoreSignalInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
        signal.semaphore = timeline;
        signal.value = 1u;
        const auto signalResult = vkSignalSemaphore(device, &signal);
        const bool originalCompleted = original
            && original->waitSubmittedFenceUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic);
        assert(signalResult == VK_SUCCESS && originalCompleted);
        wait = {};
        timelineOwner.reset();
        retireCompletedVulkanRhiSubmittedCommands(*original->runtime());
        original.reset();
        other.reset();
        const bool releasedWithRuntime = owner.expired() && runtime.expired();
        drainVulkanRhiSubmittedCommandsForShutdown(*second.runtime());
        const bool releasedAfterDrain =
            owner.expired() && runtime.expired() && observedCommand.expired();
        std::printf(
            "runtime-owner: submitted=%d accepted=%d pending=%d retained=%d other=%d/%d/%d still-pending=%d original-completed=%d runtime-released=%d released=%d\n",
            submitted,
            accepted,
            pendingBefore,
            retainedPending,
            otherSubmitted,
            otherAccepted,
            otherCompleted,
            stillPending,
            originalCompleted,
            releasedWithRuntime,
            releasedAfterDrain);
        std::fflush(stdout);
        assert(submitted && accepted && pendingBefore && retainedPending);
        assert(otherSubmitted && otherAccepted && otherCompleted && stillPending
            && retainedAfterOther);
        assert(releasedWithRuntime && releasedAfterDrain);
        return 0;
    }
};

} // namespace arp::tests
