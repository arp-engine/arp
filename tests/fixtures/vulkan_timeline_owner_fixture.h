#pragma once

#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_device.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/semantics/vulkan_device_capability_query.h"
#include "tests/fixtures/vulkan_headless_runtime.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace arp::tests {

class VulkanTimelineOwnerFixture final {
    struct Observation {
        std::weak_ptr<rhi_vulkan::VulkanRhiRuntime> runtime;
        std::weak_ptr<rhi_vulkan::VulkanRhiNativeRuntimeOwner> owner;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        std::array<VkSemaphore, 2> semaphores{};
        std::array<VkSemaphore, 2> deferred{};
        unsigned created = 0u, destroyAttempts = 0u, destroyed = 0u, early = 0u;
        unsigned deferredCount = 0u, submits = 0u, fences = 0u;
        bool guard = false, completed = false, rejectQueue = false, rejectIdle = false;
        bool allOwned = true, signalObserved = false, failCreate = false;
        const char* missingEntrypoint = nullptr;
    };

    inline static Observation* observation_ = nullptr;

    static VKAPI_ATTR VkResult VKAPI_CALL createSemaphore(VkDevice device,
        const VkSemaphoreCreateInfo* info,
        const VkAllocationCallbacks* callbacks,
        VkSemaphore* semaphore)
    {
        if (observation_ && device == observation_->device && observation_->failCreate) {
            *semaphore = rhi_vulkan::syntheticHandle<VkSemaphore>(0x123u);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        const auto result = vkCreateSemaphore(device, info, callbacks, semaphore);
        if (result == VK_SUCCESS && observation_ && device == observation_->device) {
            assert(observation_->created < observation_->semaphores.size());
            observation_->semaphores[observation_->created++] = *semaphore;
        }
        return result;
    }

    static VKAPI_ATTR void VKAPI_CALL destroySemaphore(
        VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* callbacks)
    {
        auto* o = observation_;
        if (o && device == o->device) {
            ++o->destroyAttempts;
            o->allOwned &= !o->runtime.expired() && !o->owner.expired();
            if (o->guard && !o->completed) {
                ++o->early;
                assert(o->deferredCount < o->deferred.size());
                o->deferred[o->deferredCount++] = semaphore;
                return; // Explicit test rescue only; any use of it fails the test.
            }
            if (o->completed && semaphore == o->semaphores[1]) {
                std::uint64_t value = 0u;
                o->signalObserved =
                    vkGetSemaphoreCounterValue(device, semaphore, &value) == VK_SUCCESS
                    && value == 2u;
            }
            ++o->destroyed;
        }
        vkDestroySemaphore(device, semaphore, callbacks);
    }

    static VKAPI_ATTR VkResult VKAPI_CALL createFence(VkDevice device,
        const VkFenceCreateInfo* info,
        const VkAllocationCallbacks* callbacks,
        VkFence* fence)
    {
        if (observation_ && device == observation_->device)
            ++observation_->fences;
        return vkCreateFence(device, info, callbacks, fence);
    }

    static VKAPI_ATTR VkResult VKAPI_CALL submit(
        VkQueue queue, std::uint32_t count, const VkSubmitInfo* info, VkFence fence)
    {
        if (observation_ && queue == observation_->queue) {
            ++observation_->submits;
            observation_->fence = fence;
            if (observation_->rejectQueue)
                return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        return vkQueueSubmit(queue, count, info, fence);
    }

    static VKAPI_ATTR VkResult VKAPI_CALL fenceStatus(VkDevice device, VkFence fence)
    {
        const auto result = vkGetFenceStatus(device, fence);
        if (observation_ && device == observation_->device && fence == observation_->fence
            && result == VK_SUCCESS)
            observation_->completed = true;
        return result;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL waitFences(VkDevice device,
        std::uint32_t count,
        const VkFence* fences,
        VkBool32 waitAll,
        std::uint64_t timeout)
    {
        const auto result = vkWaitForFences(device, count, fences, waitAll, timeout);
        if (observation_ && device == observation_->device && count == 1u
            && fences[0] == observation_->fence && result == VK_SUCCESS)
            observation_->completed = true;
        return result;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL idle(VkDevice device)
    {
        if (observation_ && device == observation_->device && observation_->rejectIdle)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        const auto result = vkDeviceWaitIdle(device);
        if (observation_ && device == observation_->device && result == VK_SUCCESS)
            observation_->completed = true;
        return result;
    }

    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name)
    {
        if (observation_ && device == observation_->device && observation_->missingEntrypoint
            && std::strcmp(name, observation_->missingEntrypoint) == 0)
            return nullptr;
        if (std::strcmp(name, "vkCreateSemaphore") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(createSemaphore);
        if (std::strcmp(name, "vkDestroySemaphore") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(destroySemaphore);
        if (std::strcmp(name, "vkCreateFence") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(createFence);
        if (std::strcmp(name, "vkQueueSubmit") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(submit);
        if (std::strcmp(name, "vkGetFenceStatus") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(fenceStatus);
        if (std::strcmp(name, "vkWaitForFences") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(waitFences);
        if (std::strcmp(name, "vkDeviceWaitIdle") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(idle);
        return vkGetDeviceProcAddr(device, name);
    }

    static core::UniquePtr<rhi::GpuDevice> makeDevice(const VulkanHeadlessRuntime& native)
    {
        rhi_vulkan::VulkanRhiDeviceDesc desc;
        desc.label = "timeline ownership";
        desc.runtime = native.runtime();
        desc.memoryBudgetBytes = 64u * 1024u * 1024u;
        rhi::semantics::vulkan::VulkanDeviceCapabilitySnapshot snapshot;
        snapshot.queriedFromNativeDevice = native.runtime()->valid();
        snapshot.queriedExtendedFeatures = true;
        snapshot.supportsTimelineSynchronization = native.runtime()->timelineSemaphoreEnabled();
        desc.capabilityContract =
            rhi::semantics::vulkan::buildVulkanNativeCapabilityContract(snapshot);
        return rhi_vulkan::createVulkanRhiDevice(desc);
    }

public:
    enum class Mode { Independent, Pending, WrongDevice, Unproven, Admission };

    static int run(Mode mode)
    {
        using namespace rhi_vulkan;
        std::fprintf(stderr, "timeline-owner: mode=%u stage=primary-create\n", unsigned(mode));
        auto native = core::makeUnique<VulkanHeadlessRuntime>(false, resolve);
        std::fprintf(stderr, "timeline-owner: mode=%u stage=foreign-create\n", unsigned(mode));
        VulkanHeadlessRuntime other;
        if (!native->runtime() || !other.runtime()) {
            std::fprintf(stderr,
                "timeline-owner: mode=%u unavailable primary=%d foreign=%d\n",
                unsigned(mode),
                bool(native->runtime()),
                bool(other.runtime()));
            return 77;
        }
        std::fprintf(stderr, "timeline-owner: mode=%u stage=payload-create\n", unsigned(mode));
        auto rescue = native->runtime();
        Observation o;
        o.runtime = rescue;
        o.owner = native->nativeOwner();
        o.device = native->device();
        o.queue = rescue->graphicsQueue();
        observation_ = &o;
        auto device = makeDevice(*native);
        assert(device && device->capabilities().features.timelineSync);
        auto wait = device->createTimelineSemaphore(0u);
        assert(wait && o.created == 1u && wait->getValue() == 0u);
        if (mode == Mode::Admission) {
            core::pmr::String diagnostic;
            for (const auto* name : {"vkCreateSemaphore",
                     "vkDestroySemaphore",
                     "vkSignalSemaphore",
                     "vkWaitSemaphores",
                     "vkGetSemaphoreCounterValue"}) {
                o.missingEntrypoint = name;
                assert(!VulkanRhiTimelineSemaphorePayload::create(rescue, 0u, diagnostic));
                assert(diagnostic == "RHI-VULKAN-TIMELINE-ENTRYPOINT-UNAVAILABLE");
            }
            o.missingEntrypoint = nullptr;
            o.failCreate = true;
            assert(!VulkanRhiTimelineSemaphorePayload::create(rescue, 0u, diagnostic));
            assert(diagnostic == "RHI-VULKAN-TIMELINE-CREATE-FAILED");
            assert(o.created == 1u && o.destroyAttempts == 0u);
            o.failCreate = false;
            auto initialized = VulkanRhiTimelineSemaphorePayload::create(rescue, 7u, diagnostic);
            std::uint64_t value = 0u;
            assert(initialized && diagnostic.empty()
                && initialized->queryCounter(value) == VK_SUCCESS && value == 7u
                && initialized->hostSignal(9u) == VK_SUCCESS
                && initialized->hostWait(9u, 1'000'000'000u) == VK_SUCCESS
                && initialized->queryCounter(value) == VK_SUCCESS && value == 9u);
            initialized.reset();
            wait.reset();
            device.reset();
            native.reset();
            rescue.reset();
            const bool released = o.runtime.expired() && o.owner.expired();
            observation_ = nullptr;
            std::printf(
                "timeline-admission: missing=5 failed-create=1 real-value=%llu destroyed=%u owned=%d released=%d\n",
                static_cast<unsigned long long>(value),
                o.destroyed,
                o.allOwned,
                released);
            assert(o.created == 2u && o.destroyed == 2u && o.allOwned && released);
            return 0;
        }
        if (mode == Mode::Independent) {
            device.reset();
            native.reset();
            const bool retained = rescue.use_count() > 1;
            if (retained)
                rescue.reset();
            wait->signal(3u);
            wait->hostWait(3u, 1'000'000'000u);
            const bool nativeUse = wait->getValue() == 3u;
            wait.reset();
            rescue.reset();
            const bool released = o.runtime.expired() && o.owner.expired();
            observation_ = nullptr;
            std::printf(
                "timeline-independent: retained=%d native-use=%d owned=%d destroyed=%u released=%d\n",
                retained,
                nativeUse,
                o.allOwned,
                o.destroyed,
                released);
            std::fflush(stdout);
            assert(retained && nativeUse && o.allOwned && o.destroyed == 1u && released);
            return 0;
        }
        auto signal = device->createTimelineSemaphore(0u);
        assert(signal && o.created == 2u);
        auto encoder = device->createCommandEncoder({"timeline command"});
        assert(encoder);
        auto command = encoder->finish();
        encoder.reset();
        assert(command);
        auto payload = vulkanRhiCommandPayload(*command);
        assert(payload);
        core::pmr::String diagnostic;
        if (mode == Mode::WrongDevice) {
            std::fprintf(stderr, "timeline-owner: stage=wrong-device-submit\n");
            auto otherDevice = makeDevice(other);
            auto foreign = otherDevice->createTimelineSemaphore(0u);
            assert(foreign);
            o.rejectQueue = true; // Never issue an invalid cross-device native call on red.
            const auto result = device->queue().submit(*command, foreign.get(), 1u, nullptr, 0u);
            o.rejectQueue = false;
            const bool rejected = !result.success && !result.hasAcceptedWork()
                && result.diagnostics == "RHI-VULKAN-TIMELINE-SYNC-OWNER-MISMATCH"
                && o.submits == 0u && o.fences == 0u;
            std::fprintf(stderr, "timeline-owner: stage=foreign-payload-release\n");
            foreign.reset();
            otherDevice.reset();
            command.reset();
            payload.reset();
            wait.reset();
            signal.reset();
            device.reset();
            std::fprintf(stderr, "timeline-owner: stage=primary-runtime-release\n");
            native.reset();
            drainVulkanRhiSubmittedCommandsForShutdown(*rescue);
            rescue.reset();
            std::fprintf(stderr, "timeline-owner: stage=foreign-runtime-release\n");
            observation_ = nullptr;
            std::printf("timeline-wrong-device: rejected=%d native-submits=%u native-fences=%u\n",
                rejected,
                o.submits,
                o.fences);
            std::fflush(stdout);
            assert(rejected);
            return 0;
        }
        o.guard = true;
        o.rejectQueue = mode == Mode::Unproven;
        const auto allocationsBefore = core::defaultCpuAllocator().stats().allocationCount;
        const auto result = device->queue().submit(*command, wait.get(), 1u, signal.get(), 2u);
        const auto submitAllocations =
            core::defaultCpuAllocator().stats().allocationCount - allocationsBefore;
        o.rejectQueue = false;
        const bool accepted = result.success && result.hasAcceptedWork();
        const bool unproven = !result.success && !result.hasAcceptedWork()
            && result.diagnostics == "RHI-VULKAN-TIMELINE-SUBMIT-UNPROVEN";
        const auto signalHandle = o.semaphores[1];
        wait.reset();
        signal.reset();
        command.reset();
        device.reset();
        native.reset();
        const bool retained = o.destroyAttempts == 0u;
        const std::weak_ptr<VulkanRhiNativeCommandPayload> observed = payload;
        payload.reset();
        const bool pendingOwned = !observed.expired();
        assert(waitIdleAndRetireVulkanRhiSubmittedCommands(other.runtime()) == nullptr);
        const bool unrelatedPreserved = o.destroyAttempts == 0u && !observed.expired();
        bool failurePreserved = true;
        if (mode == Mode::Unproven) {
            o.rejectIdle = true;
            const auto* failedIdle = waitIdleAndRetireVulkanRhiSubmittedCommands(rescue);
            o.rejectIdle = false;
            failurePreserved = failedIdle
                && std::strcmp(failedIdle, "RHI-VULKAN-IDLE-COMPLETION-FAILED") == 0
                && o.destroyAttempts == 0u && !observed.expired();
            assert(waitIdleAndRetireVulkanRhiSubmittedCommands(rescue) == nullptr);
        } else {
            // Always unblock the real queue before assertion or native teardown.
            VkSemaphoreSignalInfo release{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
            release.semaphore = o.semaphores[0];
            release.value = 1u;
            assert(vkSignalSemaphore(o.device, &release) == VK_SUCCESS);
            auto completion = observed.lock();
            assert(completion
                && completion->waitSubmittedFenceUntil(
                    std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
            std::uint64_t value = 0u;
            // Signal may already be destroyed during completion; observe the
            // actual signal before retirement through its retained test guard.
            if (o.deferredCount) {
                assert(vkGetSemaphoreCounterValue(o.device, signalHandle, &value) == VK_SUCCESS);
                assert(value == 2u);
            }
        }
        retireCompletedVulkanRhiSubmittedCommands(*rescue);
        drainVulkanRhiSubmittedCommandsForShutdown(*rescue);
        // Red cleanup only; green must not need any deferred native destroy.
        for (unsigned i = 0u; i < o.deferredCount; ++i)
            vkDestroySemaphore(o.device, o.deferred[i], nullptr);
        const bool completed = o.completed;
        rescue.reset();
        const bool released = o.runtime.expired() && o.owner.expired() && observed.expired();
        observation_ = nullptr;
        std::printf(
            "timeline-submission: unproven-mode=%d accepted=%d unproven=%d retained=%d pending-owned=%d unrelated=%d failed-idle=%d completed=%d early=%u destroyed=%u released=%d\n",
            mode == Mode::Unproven,
            accepted,
            unproven,
            retained,
            pendingOwned,
            unrelatedPreserved,
            failurePreserved,
            completed,
            o.early,
            o.destroyed,
            released);
        std::fflush(stdout);
        assert((mode == Mode::Unproven ? unproven : accepted) && retained && pendingOwned);
        assert(mode == Mode::Unproven || submitAllocations == 0u);
        assert(unrelatedPreserved && failurePreserved && completed && o.early == 0u);
        assert(mode == Mode::Unproven || o.signalObserved);
        assert(o.destroyed == 2u && o.allOwned && released);
        return 0;
    }
};

} // namespace arp::tests
