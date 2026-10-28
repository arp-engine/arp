#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/backends/vulkan/vulkan_swapchain_retirement.h"
#include "tests/fixtures/vulkan_headless_runtime.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>

namespace arp::tests {

// Real command/semaphore/fence evidence; this headless fixture does not prove WSI.
class VulkanScopedPresentRetirementFixture final {
    using State = rhi_vulkan::VulkanRhiSwapchainFrameSyncState;
    static_assert(!std::is_copy_constructible_v<State> && !std::is_copy_assignable_v<State>);
    enum class IdleMode { Real, Missing, Failed, Lost };
    inline static IdleMode idleMode_ = IdleMode::Real;
    inline static unsigned idleCalls_ = 0;
    inline static unsigned rejectedSubmitCalls_ = 0;
    inline static bool rejectSubmit_ = false;

    static VKAPI_ATTR VkResult VKAPI_CALL idle(VkQueue queue)
    {
        ++idleCalls_;
        if (idleMode_ == IdleMode::Failed)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        if (idleMode_ == IdleMode::Lost)
            return VK_ERROR_DEVICE_LOST;
        return vkQueueWaitIdle(queue);
    }

    static VKAPI_ATTR VkResult VKAPI_CALL submit(
        VkQueue queue, std::uint32_t count, const VkSubmitInfo* submits, VkFence fence)
    {
        if (rejectSubmit_) {
            ++rejectedSubmitCalls_;
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        return vkQueueSubmit(queue, count, submits, fence);
    }

    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name)
    {
        if (std::strcmp(name, "vkQueueWaitIdle") == 0)
            return idleMode_ == IdleMode::Missing ? nullptr
                                                  : reinterpret_cast<PFN_vkVoidFunction>(idle);
        if (std::strcmp(name, "vkQueueSubmit") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(submit);
        return vkGetDeviceProcAddr(device, name);
    }

    class ImageSync final : public rhi_vulkan::VulkanRhiNativeResourcePayload {
    public:
        ImageSync(VkSemaphore acquire, VkSemaphore finished, std::shared_ptr<State> state)
            : acquire_(acquire), finished_(finished), state_(std::move(state))
        {}

        bool backendVulkanSwapchainFrameSyncAvailable() const override { return true; }

        VkSemaphore backendVulkanAcquireSemaphore() const override { return acquire_; }

        VkSemaphore backendVulkanRenderFinishedSemaphore() const override { return finished_; }

        std::uint64_t backendVulkanSwapchainFrameSerial() const override { return 1u; }

        std::shared_ptr<State> backendVulkanSwapchainFrameSyncState() const override
        {
            return state_;
        }

    private:
        VkSemaphore acquire_;
        VkSemaphore finished_;
        std::shared_ptr<State> state_;
    };

public:
    static int run()
    {
        using namespace rhi_vulkan;
        using Status = VulkanPresentRetirementStatus;
        VulkanHeadlessRuntime native(false, resolve, true);
        VulkanHeadlessRuntime unrelated;
        if (!native.runtime() || !unrelated.runtime())
            return 77;
        idleCalls_ = rejectedSubmitCalls_ = 0;
        idleMode_ = IdleMode::Real;
        rejectSubmit_ = false;
        VulkanRhiAllocator allocator({"scoped frame", 64u * 1024u * 1024u}, native.runtime());
        VulkanRhiAllocator otherAllocator(
            {"unrelated frame", 64u * 1024u * 1024u}, unrelated.runtime());
        VulkanRhiNativeResourceAllocator resources(native.runtime(), &allocator);
        VulkanRhiNativeResourceAllocator otherResources(unrelated.runtime(), &otherAllocator);
        VkSemaphoreCreateInfo binaryInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore acquire = VK_NULL_HANDLE, finished = VK_NULL_HANDLE;
        assert(vkCreateSemaphore(native.device(), &binaryInfo, nullptr, &acquire) == VK_SUCCESS);
        assert(vkCreateSemaphore(native.device(), &binaryInfo, nullptr, &finished) == VK_SUCCESS);
        core::pmr::String timelineDiagnostic;
        auto timelineOwner =
            VulkanRhiTimelineSemaphorePayload::create(native.runtime(), 0u, timelineDiagnostic);
        assert(timelineOwner);
        const auto timeline = timelineOwner->nativeHandle();
        auto state = core::makeShared<State>(native.runtime());
        auto unsubmitted = core::makeShared<State>(native.runtime());
        ImageSync image(acquire, finished, state);
        core::pmr::String diagnostic;
        auto command =
            createVulkanRhiNativeCommandPayload(resources, diagnostic, {"original frame"});
        auto duplicate =
            createVulkanRhiNativeCommandPayload(resources, diagnostic, {"duplicate frame"});
        auto other =
            createVulkanRhiNativeCommandPayload(otherResources, diagnostic, {"other runtime"});
        assert(command && duplicate && other);
        assert(command->bindSwapchainFrameSync(
            image, rhi::ResourceState::ColorAttachment, diagnostic));
        assert(duplicate->bindSwapchainFrameSync(
            image, rhi::ResourceState::ColorAttachment, diagnostic));
        const bool wrongOwnerRejected =
            !other->bindSwapchainFrameSync(image, rhi::ResourceState::ColorAttachment, diagnostic)
            && diagnostic == "RHI-VULKAN-SWAPCHAIN-SYNC-OWNER-MISMATCH";
        VkSubmitInfo signalAcquire{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        signalAcquire.signalSemaphoreCount = 1;
        signalAcquire.pSignalSemaphores = &acquire;
        assert(vkQueueSubmit(native.runtime()->graphicsQueue(), 1, &signalAcquire, VK_NULL_HANDLE)
            == VK_SUCCESS);
        VulkanRhiTimelineSubmitInfo wait;
        wait.waitSemaphore = timelineOwner;
        wait.waitValue = 1u;
        bool accepted = false;
        assert(
            command->end(diagnostic) && command->submit(wait, diagnostic, &accepted) && accepted);
        assert(duplicate->end(diagnostic));
        rejectSubmit_ = true;
        const bool duplicateRejected = !duplicate->submit({}, diagnostic, &accepted) && !accepted
            && diagnostic == "RHI-VULKAN-SWAPCHAIN-SYNC-ALREADY-SUBMITTED"
            && rejectedSubmitCalls_ == 0;
        rejectSubmit_ = false;
        other->observeFrameSerial(99u);
        assert(other->end(diagnostic) && other->submit(diagnostic));
        assert(other->waitSubmittedFenceUntil(
            std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
        retireCompletedVulkanRhiSubmittedCommands(*native.runtime());
        const bool pendingAfterUnrelated = !state->graphicsCompleted()
            && pollVulkanLegacyPresentRetirement(native.runtime(), state).status == Status::Pending
            && pollVulkanLegacyPresentRetirement(native.runtime(), unsubmitted).status
                == Status::Pending
            && idleCalls_ == 0;
        const auto wrongOwner = pollVulkanLegacyPresentRetirement(unrelated.runtime(), state);
        const bool wrongRetirementOwnerRejected = wrongOwner.status == Status::Failed
            && std::strcmp(wrongOwner.diagnosticId, "RHI-VULKAN-PRESENT-RETIREMENT-OWNER-MISMATCH")
                == 0
            && idleCalls_ == 0;
        // Release the actual GPU wait BEFORE asserting any test outcome.
        VkSemaphoreSignalInfo release{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
        release.semaphore = timeline;
        release.value = 1u;
        assert(vkSignalSemaphore(native.device(), &release) == VK_SUCCESS);
        assert(command->waitSubmittedFenceUntil(
            std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
        std::weak_ptr<VulkanRhiNativeCommandPayload> observedCommand = command;
        retireCompletedVulkanRhiSubmittedCommands(*native.runtime());
        command.reset();
        const bool exactCompletionRetained =
            state->graphicsCompleted() && observedCommand.expired();
        idleMode_ = IdleMode::Missing;
        const auto missing = pollVulkanLegacyPresentRetirement(native.runtime(), state);
        const bool missingRejected = missing.status == Status::Failed && idleCalls_ == 0
            && std::strcmp(missing.diagnosticId, "RHI-VULKAN-PRESENT-RETIREMENT-IDLE-UNAVAILABLE")
                == 0;
        idleMode_ = IdleMode::Failed;
        const auto failed = pollVulkanLegacyPresentRetirement(native.runtime(), state);
        const bool failureRejected = failed.status == Status::Failed && idleCalls_ == 1
            && std::strcmp(failed.diagnosticId, "RHI-VULKAN-PRESENT-RETIREMENT-IDLE-FAILED") == 0;
        idleMode_ = IdleMode::Real;
        const auto success = pollVulkanLegacyPresentRetirement(native.runtime(), state);
        const bool realIdleSucceeded = success.status == Status::Ready && idleCalls_ == 2;
        idleMode_ = IdleMode::Lost;
        const auto lost = pollVulkanLegacyPresentRetirement(native.runtime(), state);
        const bool lossRejected = lost.status == Status::Failed && idleCalls_ == 3
            && std::strcmp(lost.diagnosticId, "RHI-VULKAN-PRESENT-RETIREMENT-DEVICE-LOST") == 0;
        idleMode_ = IdleMode::Real;
        duplicate.reset();
        other.reset();
        retireCompletedVulkanRhiSubmittedCommands(*native.runtime());
        VulkanRhiRuntimeDesc validatorDesc;
        validatorDesc.enableSyntheticRuntime = true;
        auto synthetic = createVulkanRhiRuntime(validatorDesc);
        VulkanRhiNativeResourceAllocator syntheticResources(synthetic);
        auto syntheticState = core::makeShared<State>(synthetic);
        ImageSync syntheticImage(VK_NULL_HANDLE, VK_NULL_HANDLE, syntheticState);
        auto validator =
            createVulkanRhiNativeCommandPayload(syntheticResources, diagnostic, {"validator only"});
        assert(validator
            && validator->bindSwapchainFrameSync(
                syntheticImage, rhi::ResourceState::ColorAttachment, diagnostic));
        assert(validator->end(diagnostic) && validator->submit(diagnostic)
            && validator->submittedFenceCompleted());
        const bool validatorNotNative = !syntheticState->graphicsCompleted()
            && pollVulkanLegacyPresentRetirement(synthetic, syntheticState).status
                == Status::Failed;
        validator.reset();
        retireCompletedVulkanRhiSubmittedCommands(*native.runtime());
        // Commands are all proven complete. Release the backend reusable
        // caches while both test-owned VkDevice objects still exist.
        drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
        vkDestroySemaphore(native.device(), acquire, nullptr);
        vkDestroySemaphore(native.device(), finished, nullptr);
        wait = {};
        timelineOwner.reset();
        std::printf(
            "scoped-present: wrong-command-owner=%d duplicate=%d unrelated-pending=%d wrong-retire-owner=%d exact-after-expiry=%d missing=%d failed=%d native-idle=%d lost=%d\n",
            wrongOwnerRejected,
            duplicateRejected,
            pendingAfterUnrelated,
            wrongRetirementOwnerRejected,
            exactCompletionRetained,
            missingRejected,
            failureRejected,
            realIdleSucceeded,
            lossRejected);
        std::fflush(stdout);
        assert(validatorNotNative && wrongOwnerRejected && duplicateRejected
            && pendingAfterUnrelated && wrongRetirementOwnerRejected && exactCompletionRetained
            && missingRejected && failureRejected && realIdleSucceeded && lossRejected);
        return 0;
    }
};
} // namespace arp::tests
