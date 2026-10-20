#pragma once

#include "platform/vulkan_runtime_bootstrap.h"
#include "renderer/render_graph/render_graph_resource_management_smoke.h"
#include "rhi/backends/vulkan/vulkan_swapchain_retirement.h"
#include "tests/fixtures/vulkan_heap_owner_fixture.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace arp::tests {

// Original platform bootstrap and backend factories, actual surface/device and
// native acquire/submit/present. Interception only withholds completion or returns
// errors; it never manufactures a successful native payload or completion.
class VulkanWsiFailureRetentionFixture final {
    inline static PFN_vkGetDeviceProcAddr resolver_ = nullptr;
    inline static VkDevice nativeDevice_ = VK_NULL_HANDLE;
    inline static bool failWait_ = false, failIdle_ = false, failPresent_ = false;
    inline static unsigned acquireCalls_ = 0, presentCalls_ = 0, idleCalls_ = 0;
    inline static unsigned destroyedSwapchains_ = 0, destroyedSemaphores_ = 0, destroyedFences_ = 0;
    inline static bool watchCreations_ = false;
    inline static std::array<VkSemaphore, 4> watchedSemaphores_{};
    inline static std::array<VkFence, 2> watchedFences_{};
    inline static VkSwapchainKHR watchedSwapchain_ = VK_NULL_HANDLE;
    inline static unsigned watchedSemaphoreCount_ = 0, watchedFenceCount_ = 0;
    inline static unsigned watchedSemaphoreDestruction_ = 0, watchedFenceDestruction_ = 0,
                           watchedSwapchainDestruction_ = 0;

    template <typename T> static T original(const char* name)
    {
        return reinterpret_cast<T>(resolver_(nativeDevice_, name));
    }

    static VKAPI_ATTR VkResult VKAPI_CALL acquire(VkDevice device,
        VkSwapchainKHR swapchain,
        std::uint64_t timeout,
        VkSemaphore semaphore,
        VkFence fence,
        std::uint32_t* index)
    {
        ++acquireCalls_;
        return original<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR")(
            device, swapchain, timeout, semaphore, fence, index);
    }

    static VKAPI_ATTR VkResult VKAPI_CALL wait(VkDevice device,
        std::uint32_t count,
        const VkFence* fences,
        VkBool32 all,
        std::uint64_t timeout)
    {
        if (failWait_)
            return VK_TIMEOUT;
        return original<PFN_vkWaitForFences>("vkWaitForFences")(
            device, count, fences, all, timeout);
    }

    static VKAPI_ATTR VkResult VKAPI_CALL idle(VkDevice device)
    {
        ++idleCalls_;
        if (failIdle_)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        return original<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(device);
    }

    static VKAPI_ATTR VkResult VKAPI_CALL present(VkQueue queue, const VkPresentInfoKHR* info)
    {
        ++presentCalls_;
        const auto actual = original<PFN_vkQueuePresentKHR>("vkQueuePresentKHR")(queue, info);
        // Native work really occurred; hide its successful return to test the
        // pessimistic unknown-result path without issuing an invalid present.
        return failPresent_ && (actual == VK_SUCCESS || actual == VK_SUBOPTIMAL_KHR)
            ? VK_ERROR_OUT_OF_DATE_KHR
            : actual;
    }

    static VKAPI_ATTR void VKAPI_CALL destroySwapchain(
        VkDevice device, VkSwapchainKHR handle, const VkAllocationCallbacks* callbacks)
    {
        ++destroyedSwapchains_;
        if (handle == watchedSwapchain_)
            ++watchedSwapchainDestruction_;
        original<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR")(device, handle, callbacks);
    }

    static VKAPI_ATTR void VKAPI_CALL destroySemaphore(
        VkDevice device, VkSemaphore handle, const VkAllocationCallbacks* callbacks)
    {
        ++destroyedSemaphores_;
        if (std::find(watchedSemaphores_.begin(), watchedSemaphores_.end(), handle)
            != watchedSemaphores_.end())
            ++watchedSemaphoreDestruction_;
        original<PFN_vkDestroySemaphore>("vkDestroySemaphore")(device, handle, callbacks);
    }

    static VKAPI_ATTR void VKAPI_CALL destroyFence(
        VkDevice device, VkFence handle, const VkAllocationCallbacks* callbacks)
    {
        ++destroyedFences_;
        if (std::find(watchedFences_.begin(), watchedFences_.end(), handle) != watchedFences_.end())
            ++watchedFenceDestruction_;
        original<PFN_vkDestroyFence>("vkDestroyFence")(device, handle, callbacks);
    }

    static VKAPI_ATTR VkResult VKAPI_CALL createSwapchain(VkDevice device,
        const VkSwapchainCreateInfoKHR* info,
        const VkAllocationCallbacks* callbacks,
        VkSwapchainKHR* output)
    {
        const auto result = original<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR")(
            device, info, callbacks, output);
        if (watchCreations_ && result == VK_SUCCESS)
            watchedSwapchain_ = *output;
        return result;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL createSemaphore(VkDevice device,
        const VkSemaphoreCreateInfo* info,
        const VkAllocationCallbacks* callbacks,
        VkSemaphore* output)
    {
        const auto result =
            original<PFN_vkCreateSemaphore>("vkCreateSemaphore")(device, info, callbacks, output);
        if (watchCreations_ && result == VK_SUCCESS) {
            assert(watchedSemaphoreCount_ < watchedSemaphores_.size());
            watchedSemaphores_[watchedSemaphoreCount_++] = *output;
        }
        return result;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL createFence(VkDevice device,
        const VkFenceCreateInfo* info,
        const VkAllocationCallbacks* callbacks,
        VkFence* output)
    {
        const auto result =
            original<PFN_vkCreateFence>("vkCreateFence")(device, info, callbacks, output);
        if (watchCreations_ && result == VK_SUCCESS) {
            assert(watchedFenceCount_ < watchedFences_.size());
            watchedFences_[watchedFenceCount_++] = *output;
        }
        return result;
    }

    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name)
    {
        if (std::strcmp(name, "vkCreateSwapchainKHR") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(createSwapchain);
        if (std::strcmp(name, "vkCreateSemaphore") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(createSemaphore);
        if (std::strcmp(name, "vkCreateFence") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(createFence);
        if (std::strcmp(name, "vkAcquireNextImageKHR") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(acquire);
        if (std::strcmp(name, "vkWaitForFences") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(wait);
        if (std::strcmp(name, "vkDeviceWaitIdle") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(idle);
        if (std::strcmp(name, "vkQueuePresentKHR") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(present);
        if (std::strcmp(name, "vkDestroySwapchainKHR") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(destroySwapchain);
        if (std::strcmp(name, "vkDestroySemaphore") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(destroySemaphore);
        if (std::strcmp(name, "vkDestroyFence") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(destroyFence);
        return resolver_(device, name);
    }

public:
    static int run(bool maintenance = false)
    {
        using namespace rhi_vulkan;
        platform::WindowSystem windowSystem;
        auto initialized = windowSystem.initialize();
        if (!initialized.success)
            return 77;
        auto window =
            windowSystem.createWindow({"OriginalWsiFailureRetention", 320, 180, true, true});
        if (!window.valid())
            return 77;
        core::pmr::String diagnostic;
        auto desc = platform::VulkanSurfaceRuntimeBootstrap::create(
            windowSystem, window, "OriginalWsiFailureRetention", diagnostic);
        if (!desc.nativeOwner) {
            std::fprintf(stderr, "native WSI unavailable: %s\n", diagnostic.c_str());
            return 77;
        }
        if (maintenance && !desc.swapchainMaintenance1Enabled) {
            std::puts("Native swapchain maintenance1 unavailable");
            return 77;
        }
        const std::weak_ptr<VulkanRhiNativeRuntimeOwner> originalOwner = desc.nativeOwner;
        resolver_ = desc.getDeviceProcAddr;
        nativeDevice_ = desc.logicalDevice;
        desc.getDeviceProcAddr = resolve;
        // Exercise both legacy and actually enabled maintenance contracts.
        // No enabled feature is inferred from the backend or platform name.
        desc.swapchainMaintenance1Enabled = maintenance;
        auto runtime = createVulkanRhiRuntime(desc);
        assert(runtime && runtime->valid() && runtime->surface());
        assert(runtime->presentFenceEnabled() == maintenance);
        const std::weak_ptr<VulkanRhiRuntime> observedRuntime = runtime;
        desc.nativeOwner.reset();
        auto device = VulkanHeapOwnerFixture::makeDevice(runtime);
        assert(device);
        const rhi::SwapchainDesc swapchainDesc{
            "OriginalWsiFailureRetention", {320, 180}, rhi::TextureFormat::Bgra8Unorm, 2u};

        // Unsubmitted discard/present and surface notifications cannot erase a
        // real acquisition or silently consume its binary semaphore.
        {
            auto swapchain = device->createSwapchain(swapchainDesc);
            assert(swapchain);
            const auto frame = swapchain->acquireNextFrame();
            assert(frame.success);
            const auto before = presentCalls_;
            const auto discard = swapchain->discardFrame(frame);
            const auto unsubmittedPresent = swapchain->present(frame);
            const auto surface = swapchain->setSurfaceAvailable(false);
            assert(!discard.success && !unsubmittedPresent.success && !surface.success);
            assert(unsubmittedPresent.diagnostics
                == "RHI-VULKAN-SWAPCHAIN-UNSUBMITTED-FRAME-RETAINED");
            assert(surface.diagnostics == "RHI-VULKAN-SURFACE-FRAMES-RETAINED");
            assert(swapchain->isFrameAcquired(frame) && presentCalls_ == before);
            assert(!swapchain->resize({400, 225}).success);
        }

        // A native acquire succeeds BEFORE the injected host wait timeout.
        // The failed public acquire cannot free the backend slot or semaphores.
        bool forgedFrameRejected = false;
        {
            auto swapchain = device->createSwapchain(swapchainDesc);
            assert(swapchain);
            failWait_ = true;
            const auto frame = swapchain->acquireNextFrame();
            failWait_ = false;
            assert(!frame.success
                && frame.diagnostics == "RHI-VULKAN-SWAPCHAIN-ACQUIRE-COMPLETION-UNPROVEN");
            const auto before = acquireCalls_;
            const auto retry = swapchain->acquireNextFrame();
            assert(
                !retry.success && retry.diagnostics == "RHI-VULKAN-SWAPCHAIN-REPLACEMENT-REQUIRED");
            assert(acquireCalls_ == before && !swapchain->resize({400, 225}).success);
            const auto surfaceAfterFailure = swapchain->setSurfaceAvailable(false);
            const auto resizeAfterFailure = swapchain->resize({400, 225});
            assert(!surfaceAfterFailure.success && !surfaceAfterFailure.acquireAllowed
                && !surfaceAfterFailure.presentAllowed
                && surfaceAfterFailure.swapchainRebuildRequired);
            assert(!resizeAfterFailure.success && !resizeAfterFailure.acquireAllowed
                && !resizeAfterFailure.presentAllowed
                && resizeAfterFailure.swapchainRebuildRequired);
            assert(pendingVulkanRhiSwapchainLifetimes(runtime) == 1u);
            const char* retainedDiagnostic = "";
            assert(retireVulkanRhiSwapchainLifetimes(runtime, retainedDiagnostic) == 1u
                && std::strcmp(retainedDiagnostic, "RHI-VULKAN-SWAPCHAIN-ACTIVE-OWNER-RETAINED")
                    == 0);
            rhi::semantics::ShutdownDrainRequest request;
            request.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            const auto drain = device->drainForShutdown(request);
            assert(!drain.completed());
            if (maintenance)
                assert(drain.diagnosticId == "RHI-VULKAN-SHUTDOWN-SWAPCHAIN-RETIREMENT-PENDING");
            auto forged = frame;
            forged.success = true;
            auto forgedImage = swapchain->acquiredImage(forged);
            forgedFrameRejected =
                !swapchain->isFrameAcquired(forged) && !forgedImage.success && !forgedImage.texture;
        }
        // Assert after safe original fence/idle cleanup, even on the red path.
        assert(forgedFrameRejected);

        // Real submission and present, followed by a pessimistic error return.
        {
            auto swapchain = device->createSwapchain(swapchainDesc);
            assert(swapchain);
            failPresent_ = true;
            {
                render_graph::RenderGraphResourceManagementSmokeRunner runner(
                    *device, swapchain.get());
                const auto report = runner.run({2, {64, 64, 1}, rhi::TextureFormat::Rgba8Unorm});
                assert(!report.success && presentCalls_ > 0u);
                assert(report.diagnostics.find("vkQueuePresentKHR") != core::pmr::String::npos);
            }
            failPresent_ = false;
            const auto before = acquireCalls_;
            const auto retry = swapchain->acquireNextFrame();
            assert(
                !retry.success && retry.diagnostics == "RHI-VULKAN-SWAPCHAIN-REPLACEMENT-REQUIRED");
            assert(acquireCalls_ == before);
            assert(!swapchain->setSurfaceAvailable(false).success);
            device->waitIdle();
        }

        // Retained external image owners must outlive their destroyed facade.
        {
            auto swapchain = device->createSwapchain(swapchainDesc);
            assert(swapchain);
            const auto frame = swapchain->acquireNextFrame();
            assert(frame.success);
            auto image = swapchain->acquiredImage(frame);
            assert(image.success && image.texture);
            const auto before = destroyedSwapchains_;
            swapchain.reset();
            assert(destroyedSwapchains_ == before);
            image.texture.reset();
            const char* retirementDiagnostic = "";
            assert(retireVulkanRhiSwapchainLifetimes(runtime, retirementDiagnostic) == 0u);
            assert(destroyedSwapchains_ == before + 1u);
        }

        // Failed final idle retains every original native handle and owner.
        watchCreations_ = true;
        auto swapchain = device->createSwapchain(swapchainDesc);
        watchCreations_ = false;
        assert(swapchain && swapchain->acquireNextFrame().success);
        assert(watchedSemaphoreCount_ == 4u && watchedFenceCount_ == 2u && watchedSwapchain_);
        failIdle_ = true;
        swapchain.reset();
        device.reset();
        runtime.reset();
        const bool retained = !originalOwner.expired() && watchedSemaphoreDestruction_ == 0u
            && watchedFenceDestruction_ == 0u && watchedSwapchainDestruction_ == 0u;
        const char* pendingDiagnostic = "";
        const auto pending =
            retireVulkanRhiSwapchainLifetimes(observedRuntime.lock(), pendingDiagnostic);
        const bool pendingCorrect = pending == 1u
            && std::strcmp(pendingDiagnostic, "RHI-VULKAN-SWAPCHAIN-RETIREMENT-IDLE-FAILED") == 0;
        VulkanHeadlessRuntime unrelated;
        assert(unrelated.runtime());
        const auto idleBeforeUnrelated = idleCalls_;
        const bool unrelatedCannotRetire =
            retireVulkanRhiSwapchainLifetimes(unrelated.runtime(), pendingDiagnostic) == 0u
            && idleCalls_ == idleBeforeUnrelated && !originalOwner.expired();
        failIdle_ = false;
        const auto remaining =
            retireVulkanRhiSwapchainLifetimes(observedRuntime.lock(), pendingDiagnostic);
        assert(retained && pendingCorrect && unrelatedCannotRetire && remaining == 0u
            && !*pendingDiagnostic);
        assert(watchedSemaphoreDestruction_ == 4u && watchedFenceDestruction_ == 2u
            && watchedSwapchainDestruction_ == 1u && originalOwner.expired()
            && observedRuntime.expired());
        std::printf(
            "original-wsi: acquire=%u present=%u idle=%u unsubmitted/surface/acquire-wait/present-error/image-owner/idle-quarantine/retry=PASS\n",
            acquireCalls_,
            presentCalls_,
            idleCalls_);
        return 0;
    }
};

} // namespace arp::tests
