#include "core/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/backends/vulkan/vulkan_swapchain_retirement.h"
#include "rhi/semantics/vulkan_swapchain_policy.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <utility>

namespace arp::rhi_vulkan {

namespace {

template <typename Fn> Fn functionFrom(PFN_vkVoidFunction value)
{
    Fn result = nullptr;
    static_assert(sizeof(result) == sizeof(value), "unexpected Vulkan function pointer size");
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

std::pmr::string vkFailure(const char* operation, VkResult result)
{
    std::ostringstream message;
    message << operation << " failed: " << static_cast<int>(result);
    return arp::core::toPmrString(message.str());
}

rhi::semantics::vulkan::VulkanSwapchainNativeFrameStatus nativeFrameStatusFor(const VkResult result)
{
    switch (result) {
    case VK_SUCCESS:
        return rhi::semantics::vulkan::VulkanSwapchainNativeFrameStatus::Success;
    case VK_SUBOPTIMAL_KHR:
        return rhi::semantics::vulkan::VulkanSwapchainNativeFrameStatus::Suboptimal;
    case VK_ERROR_OUT_OF_DATE_KHR:
        return rhi::semantics::vulkan::VulkanSwapchainNativeFrameStatus::OutOfDate;
    case VK_ERROR_SURFACE_LOST_KHR:
        return rhi::semantics::vulkan::VulkanSwapchainNativeFrameStatus::SurfaceLost;
    default:
        break;
    }
    return rhi::semantics::vulkan::VulkanSwapchainNativeFrameStatus::FatalError;
}

VkFormat formatFor(rhi::TextureFormat format)
{
    switch (format) {
    case rhi::TextureFormat::Bgra8Unorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case rhi::TextureFormat::Rgba8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case rhi::TextureFormat::Rgba8UnormSrgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case rhi::TextureFormat::Rgba16Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case rhi::TextureFormat::R32Float:
        return VK_FORMAT_R32_SFLOAT;
    case rhi::TextureFormat::D32Float:
    case rhi::TextureFormat::Depth24Stencil8:
    case rhi::TextureFormat::R8Unorm:
    case rhi::TextureFormat::Rg16Float:
    case rhi::TextureFormat::Bc1Unorm:
    case rhi::TextureFormat::Bc3Unorm:
    case rhi::TextureFormat::Bc7Unorm:
    case rhi::TextureFormat::Bc7UnormSrgb:
    case rhi::TextureFormat::Unknown:
        // Swapchain images are limited to the color presentation formats
        // above; all other RHI formats fail closed as VK_FORMAT_UNDEFINED.
        break;
    }
    return VK_FORMAT_UNDEFINED;
}

rhi::TextureFormat textureFormatFor(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_B8G8R8A8_UNORM:
        return rhi::TextureFormat::Bgra8Unorm;
    case VK_FORMAT_R8G8B8A8_UNORM:
        return rhi::TextureFormat::Rgba8Unorm;
    case VK_FORMAT_R8G8B8A8_SRGB:
        return rhi::TextureFormat::Rgba8UnormSrgb;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return rhi::TextureFormat::Rgba16Float;
    default:
        break;
    }
    return rhi::TextureFormat::Unknown;
}

class VulkanSwapchainLifetime final {
public:
    explicit VulkanSwapchainLifetime(std::shared_ptr<VulkanRhiRuntime> runtime)
        : runtime_(std::move(runtime))
    {}

    static std::shared_ptr<VulkanSwapchainLifetime> create(
        const std::shared_ptr<VulkanRhiRuntime>& runtime)
    {
        std::lock_guard lock(domainMutex());
        for (auto& entry : domain()) {
            if (!entry.expired())
                continue;
            auto owner = core::makeShared<VulkanSwapchainLifetime>(runtime);
            owner->self_ = owner;
            entry = owner;
            return owner;
        }
        return {};
    }

    void requestRetirement(VkSwapchainKHR swapchain,
        const std::array<VkSemaphore, 2>& available,
        const std::array<VkSemaphore, 2>& finished,
        const std::array<VkFence, 2>& acquired,
        const std::array<bool, 2>& issued,
        const std::array<std::uint64_t, 2>& presentTokens,
        bool nativeWorkIssued) noexcept
    {
        std::lock_guard lock(domainMutex());
        swapchain_ = swapchain;
        available_ = available;
        finished_ = finished;
        acquired_ = acquired;
        issued_ = issued;
        presentTokens_ = presentTokens;
        nativeWorkIssued_ = nativeWorkIssued;
        retired_ = true;
    }

    bool retired() const noexcept { return retired_; }

    const char* imageDiagnostic() const noexcept
    {
        if (retired_ || unproven_)
            return "RHI-CONFORMANCE-CLEAR-SURFACE-RETIRED";
        return runtime_->admissionDiagnostic();
    }

    void markUnproven() noexcept { unproven_ = true; }

    bool tryRetire(const char*& diagnostic) noexcept
    {
        std::lock_guard lock(domainMutex());
        return tryRetireLocked(diagnostic);
    }

private:
    bool tryRetireLocked(const char*& diagnostic) noexcept
    {
        // The self-hold and the pump's local reference are the only permitted
        // owners. Image/command payloads keep this exact native lifetime live.
        if (!retired_ || self_.use_count() != 2) {
            diagnostic = "RHI-VULKAN-SWAPCHAIN-OWNERS-RETAINED";
            return false;
        }
        const auto wait =
            functionFrom<PFN_vkWaitForFences>(runtime_->getDeviceProcAddr("vkWaitForFences"));
        const auto idle =
            functionFrom<PFN_vkDeviceWaitIdle>(runtime_->getDeviceProcAddr("vkDeviceWaitIdle"));
        const auto destroyFence =
            functionFrom<PFN_vkDestroyFence>(runtime_->getDeviceProcAddr("vkDestroyFence"));
        const auto destroySemaphore =
            functionFrom<PFN_vkDestroySemaphore>(runtime_->getDeviceProcAddr("vkDestroySemaphore"));
        const auto destroySwapchain = functionFrom<PFN_vkDestroySwapchainKHR>(
            runtime_->getDeviceProcAddr("vkDestroySwapchainKHR"));
        const auto hasHandle = [](auto handle) {
            return handle != VK_NULL_HANDLE;
        };
        const bool hasFences = std::any_of(acquired_.begin(), acquired_.end(), hasHandle);
        const bool hasSemaphores = std::any_of(available_.begin(), available_.end(), hasHandle)
            || std::any_of(finished_.begin(), finished_.end(), hasHandle);
        if ((hasFences && !destroyFence) || (hasSemaphores && !destroySemaphore)
            || (swapchain_ && !destroySwapchain) || (nativeWorkIssued_ && (!wait || !idle))) {
            diagnostic = "RHI-VULKAN-SWAPCHAIN-RETIREMENT-ENTRYPOINT-UNAVAILABLE";
            return false;
        }
        for (std::size_t slot = 0; slot < acquired_.size(); ++slot) {
            if (!issued_[slot])
                continue;
            if (wait(runtime_->logicalDevice(),
                    1u,
                    &acquired_[slot],
                    VK_TRUE,
                    VulkanRhiFrameWaitPolicy::frameFenceTimeoutNanoseconds)
                != VK_SUCCESS) {
                diagnostic = "RHI-VULKAN-SWAPCHAIN-ACQUIRE-COMPLETION-UNPROVEN";
                return false;
            }
        }
        if (nativeWorkIssued_ && idle(runtime_->logicalDevice()) != VK_SUCCESS) {
            diagnostic = "RHI-VULKAN-SWAPCHAIN-RETIREMENT-IDLE-FAILED";
            return false;
        }
        for (const auto token : presentTokens_)
            if (token)
                runtime_->abandonPresentFence(token);
        for (const auto fence : acquired_)
            if (fence)
                destroyFence(runtime_->logicalDevice(), fence, nullptr);
        for (const auto semaphore : available_)
            if (semaphore)
                destroySemaphore(runtime_->logicalDevice(), semaphore, nullptr);
        for (const auto semaphore : finished_)
            if (semaphore)
                destroySemaphore(runtime_->logicalDevice(), semaphore, nullptr);
        if (swapchain_)
            destroySwapchain(runtime_->logicalDevice(), swapchain_, nullptr);
        acquired_ = {};
        available_ = finished_ = {};
        swapchain_ = VK_NULL_HANDLE;
        self_.reset();
        diagnostic = "";
        return true;
    }

public:
    static std::size_t retireDomain(
        const std::shared_ptr<VulkanRhiRuntime>& runtime, const char*& diagnostic) noexcept
    {
        if (!runtime || runtime->syntheticRuntime()) {
            diagnostic = "RHI-VULKAN-SWAPCHAIN-RETIREMENT-RUNTIME-UNAVAILABLE";
            return 1u;
        }
        std::lock_guard lock(domainMutex());
        std::size_t pending = 0u;
        diagnostic = "";
        for (auto& entry : domain()) {
            const auto owner = entry.lock();
            if (!owner || owner->runtime_ != runtime)
                continue;
            if (!owner->retired()) {
                if (owner->unproven_) {
                    ++pending;
                    if (!*diagnostic)
                        diagnostic = "RHI-VULKAN-SWAPCHAIN-ACTIVE-OWNER-RETAINED";
                }
                continue;
            }
            const char* current = "";
            if (!owner->tryRetireLocked(current)) {
                ++pending;
                if (!*diagnostic)
                    diagnostic = current;
            }
        }
        return pending;
    }

    static std::size_t pendingDomain(const std::shared_ptr<VulkanRhiRuntime>& runtime) noexcept
    {
        std::lock_guard lock(domainMutex());
        std::size_t pending = 0u;
        for (auto& entry : domain())
            if (const auto owner = entry.lock();
                owner && owner->runtime_ == runtime && (owner->retired() || owner->unproven_))
                ++pending;
        return pending;
    }

private:
    static std::array<std::weak_ptr<VulkanSwapchainLifetime>, kVulkanMaxAsyncSubmittedCommands>&
    domain()
    {
        static std::array<std::weak_ptr<VulkanSwapchainLifetime>, kVulkanMaxAsyncSubmittedCommands>
            entries;
        return entries;
    }

    static std::mutex& domainMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    std::shared_ptr<VulkanRhiRuntime> runtime_;
    std::shared_ptr<VulkanSwapchainLifetime> self_;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::array<VkSemaphore, 2> available_{}, finished_{};
    std::array<VkFence, 2> acquired_{};
    std::array<bool, 2> issued_{};
    std::array<std::uint64_t, 2> presentTokens_{};
    std::atomic<bool> retired_{false};
    std::atomic<bool> unproven_{false};
    bool nativeWorkIssued_ = false;
};

class SwapchainImagePayload final : public VulkanRhiNativeResourcePayload {
public:
    SwapchainImagePayload(VkImage image,
        VkFormat format,
        rhi::Extent3D extent,
        VkSemaphore acquireSemaphore,
        VkSemaphore renderFinishedSemaphore,
        std::uint64_t frameSerial,
        rhi::ResourceState acquiredState,
        std::shared_ptr<VulkanRhiSwapchainFrameSyncState> syncState,
        std::shared_ptr<VulkanSwapchainLifetime> lifetime)
        : image_(image), format_(format), extent_(extent), acquireSemaphore_(acquireSemaphore),
          renderFinishedSemaphore_(renderFinishedSemaphore), frameSerial_(frameSerial),
          acquiredState_(acquiredState), syncState_(std::move(syncState)),
          lifetime_(std::move(lifetime))
    {}

    const char* backendVulkanSwapchainImageDiagnostic() const noexcept override
    {
        return lifetime_->imageDiagnostic();
    }

    VkImage backendVulkanImageHandle() const override { return image_; }

    VkFormat backendVulkanImageFormat() const override { return format_; }

    rhi::Extent3D backendVulkanImageExtent() const override { return extent_; }

    bool backendVulkanSwapchainFrameSyncAvailable() const override { return true; }

    rhi::ResourceState backendVulkanSwapchainAcquiredState() const override
    {
        return acquiredState_;
    }

    VkSemaphore backendVulkanAcquireSemaphore() const override { return acquireSemaphore_; }

    VkSemaphore backendVulkanRenderFinishedSemaphore() const override
    {
        return renderFinishedSemaphore_;
    }

    std::uint64_t backendVulkanSwapchainFrameSerial() const override { return frameSerial_; }

    std::shared_ptr<VulkanRhiSwapchainFrameSyncState>
    backendVulkanSwapchainFrameSyncState() const override
    {
        return syncState_;
    }

private:
    VkImage image_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    rhi::Extent3D extent_;
    VkSemaphore acquireSemaphore_ = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore_ = VK_NULL_HANDLE;
    std::uint64_t frameSerial_ = 0;
    rhi::ResourceState acquiredState_ = rhi::ResourceState::Undefined;
    std::shared_ptr<VulkanRhiSwapchainFrameSyncState> syncState_;
    std::shared_ptr<VulkanSwapchainLifetime> lifetime_;
};

VkSurfaceFormatKHR chooseSurfaceFormat(
    const std::pmr::vector<VkSurfaceFormatKHR>& formats, rhi::TextureFormat requestedFormat)
{
    const auto requested = formatFor(requestedFormat);
    if (requested != VK_FORMAT_UNDEFINED) {
        for (const auto& format : formats) {
            if (format.format == requested) {
                return format;
            }
        }
    }
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM
            && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

VkPresentModeKHR choosePresentMode(
    const std::pmr::vector<VkPresentModeKHR>& presentModes, const rhi::PresentationMode requested)
{
    if (requested == rhi::PresentationMode::VSync)
        return VK_PRESENT_MODE_FIFO_KHR;
    if (requested != rhi::PresentationMode::Immediate) {
        for (const auto mode : presentModes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return mode;
            }
        }
    }
    // FIFO is always available but can quantize acquire to the desktop's
    // 30Hz composition cadence. Prefer immediate when mailbox is absent so
    // frame pacing is governed by the renderer rather than VSync.
    for (const auto mode : presentModes) {
        if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

const char* presentModeName(const VkPresentModeKHR mode)
{
    switch (mode) {
    case VK_PRESENT_MODE_FIFO_KHR:
        return "fifo";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        return "fifo-relaxed";
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return "mailbox";
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return "immediate";
    default:
        return "unknown";
    }
}

const char* presentationRequestName(const rhi::PresentationMode mode)
{
    switch (mode) {
    case rhi::PresentationMode::VSync:
        return "vsync";
    case rhi::PresentationMode::LowLatency:
        return "low-latency";
    case rhi::PresentationMode::Immediate:
        return "immediate";
    default:
        return "auto";
    }
}

rhi::PresentationMode presentationPolicyFor(const VkPresentModeKHR mode)
{
    switch (mode) {
    case VK_PRESENT_MODE_FIFO_KHR:
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        return rhi::PresentationMode::VSync;
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return rhi::PresentationMode::LowLatency;
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return rhi::PresentationMode::Immediate;
    default:
        return rhi::PresentationMode::Auto;
    }
}

std::uint32_t chooseImageCount(const VkSurfaceCapabilitiesKHR& capabilities)
{
    const auto decision = rhi::semantics::vulkan::chooseVulkanSwapchainImageCount({
        capabilities.minImageCount,
        capabilities.maxImageCount,
        2,
    });
    return decision.nativeImageCount;
}

} // namespace

class VulkanRhiSwapchain final : public rhi::Swapchain {
public:
    VulkanRhiSwapchain(rhi::SwapchainDesc desc,
        VulkanRhiNativeResourceAllocator nativeResources,
        std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry)
        : desc_(std::move(desc)), device_(nativeResources.runtime()),
          nativeSurface_(device_ ? device_->surface() : VK_NULL_HANDLE),
          stats_(std::move(nativeResources).sharedStats()),
          resourceRegistry_(std::move(resourceRegistry))
    {
        if (desc_.imageCount < 2) {
            desc_.imageCount = 2;
        }
        nativeRequested_ = device_ && device_->valid();
        if (nativeRequested_ && !device_->syntheticRuntime()) {
            lifetime_ = VulkanSwapchainLifetime::create(device_);
            if (!lifetime_) {
                diagnostics_ = "RHI-VULKAN-SWAPCHAIN-LIFETIME-CAPACITY";
                return;
            }
        }
        if (nativeRequested_) {
            nativeReady_ = createNativeSwapchain(VK_NULL_HANDLE, desc_.extent, diagnostics_);
        }
    }

    ~VulkanRhiSwapchain() override { destroyNative(); }

    bool ready() const { return !nativeRequested_ || nativeReady_; }

    const core::pmr::String& diagnostics() const noexcept { return diagnostics_; }

    std::uint32_t imageCount() const override
    {
        return nativeReady_ ? nativeImageCount_ : desc_.imageCount;
    }

    std::uint32_t maxFramesInFlight() const override { return 2; }

    rhi::SwapchainPresentationStatus presentationStatus() const override
    {
        return {
            desc_.presentationMode,
            presentationPolicyFor(nativePresentMode_),
            desc_.presentationMode != rhi::PresentationMode::Auto
                && presentationPolicyFor(nativePresentMode_) != desc_.presentationMode,
            presentModeName(nativePresentMode_),
        };
    }

    rhi::AcquiredFrame acquireNextFrame() override
    {
        if (nativeFailure_)
            return rhi::makeAcquireFailureResult(
                "RHI-VULKAN-SWAPCHAIN-REPLACEMENT-REQUIRED", frameSerial_, 0);
        if (!surfaceAvailable_) {
            return rhi::makeAcquireFailureResult("Vulkan RHI swapchain surface is unavailable",
                frameSerial_,
                0,
                rhi::AcquireStatus::SurfaceLost);
        }
        if (nativeReady_) {
            return acquireNativeFrame();
        }
        if (nativeRequested_) {
            return rhi::makeAcquireFailureResult(
                "RHI-VULKAN-SWAPCHAIN-NATIVE-UNAVAILABLE", frameSerial_, 0);
        }
        const auto slot = findFreeFrameSlot();
        if (slot >= maxFramesInFlight()) {
            return rhi::makeAcquireFailureResult(
                "Vulkan RHI swapchain has no free double-buffer frame slot", frameSerial_, 0);
        }
        if (stats_) {
            ++stats_->backendSwapchainAcquireCount;
        }
        ++frameSerial_;
        markFrameSlotInFlight(slot, frameSerial_);
        return rhi::makeAcquireSuccessResult(frameSerial_, slot);
    }

    bool isFrameAcquired(const rhi::AcquiredFrame& frame) const noexcept override
    {
        return frame.success && frameSlotDrawable(frame);
    }

    rhi::AcquiredSwapchainImage acquiredImage(const rhi::AcquiredFrame& frame) override
    {
        if (!frame.success || !frameSlotDrawable(frame)) {
            return {
                false,
                "Vulkan RHI swapchain image requires a live acquired frame",
                {},
                nullptr,
            };
        }
        if (!resourceRegistry_) {
            return {
                false,
                "Vulkan RHI swapchain image requires backend resource registry ownership",
                {},
                nullptr,
            };
        }

        const auto slot = frame.frameIndex;
        // Each public wrapper has its own identity, even when a frame slot or
        // native presentation image is reused. A stale ID cannot name a later
        // acquired frame by overwriting the registry entry for that slot.
        const auto imageId = rhi::allocateGpuResourceId();
        const auto extent = rhi::Extent2D{
            nativeReady_ ? nativeExtent_.width : desc_.extent.width,
            nativeReady_ ? nativeExtent_.height : desc_.extent.height,
        };
        const auto format = desc_.format != rhi::TextureFormat::Unknown
            ? desc_.format
            : textureFormatFor(nativeFormat_);
        const auto nativeFormat =
            nativeFormat_ != VK_FORMAT_UNDEFINED ? nativeFormat_ : formatFor(format);

        // Acquire does not promise preservation of presentation-engine
        // contents. Enter L2 as Undefined on every frame so the first graph
        // use explicitly discards external contents and never guesses a
        // native old layout from a previous present.
        constexpr auto acquiredState = rhi::ResourceState::Undefined;
        std::shared_ptr<VulkanRhiNativeResourcePayload> payload;
        if (device_ && device_->syntheticRuntime()) {
            payload = arp::core::makeShared<VulkanRhiNativeResourcePayload>();
        } else {
            const auto nativeImageIndex = nativeImageIndexBySlot_[slot];
            if (nativeImageIndex >= nativeImages_.size()
                || nativeImages_[nativeImageIndex] == VK_NULL_HANDLE) {
                return {
                    false,
                    "Vulkan RHI swapchain image native index is unavailable for the acquired frame",
                    {},
                    nullptr,
                };
            }
            if (!frameSyncStates_[slot]) {
                frameSyncStates_[slot] =
                    arp::core::makeShared<VulkanRhiSwapchainFrameSyncState>(device_);
            }
            payload = arp::core::makeShared<SwapchainImagePayload>(nativeImages_[nativeImageIndex],
                nativeFormat,
                rhi::Extent3D{extent.width, extent.height, 1u},
                imageAvailable_[slot],
                renderFinished_[slot],
                frame.frameSerial,
                acquiredState,
                frameSyncStates_[slot],
                lifetime_);
        }
        resourceRegistry_->registerTexture(imageId, payload);

        rhi::TextureDesc textureDesc{
            "VulkanSwapchainImage",
            {extent.width, extent.height, 1u},
            format,
            rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::CopyDst
                | rhi::TextureUsage::CopySrc,
            rhi::MemoryUsage::GpuOnly,
            // Acquired swapchain contents are intentionally discarded. The
            // SwapchainImageRef below separately carries the required final
            // Present state.
            acquiredState,
            1u,
            1u,
        };
        VulkanRhiAllocation allocation;
        allocation.success = true;
        allocation.sizeBytes = 0;
        auto texture = createVulkanRhiTexture(
            imageId, textureDesc, allocation, std::move(payload), resourceRegistry_);
        if (!texture) {
            return {
                false,
                "Vulkan RHI swapchain image texture wrapping failed",
                {},
                nullptr,
            };
        }

        return {
            true,
            "",
            rhi::SwapchainImageRef{
                texture->id(),
                slot,
                frame.frameSerial,
                extent,
                format,
                rhi::ResourceState::Present,
            },
            std::move(texture),
        };
    }

    rhi::FrameDiscardResult discardFrame(const rhi::AcquiredFrame& frame) override
    {
        if (!frame.success || !frameSlotDrawable(frame)) {
            return rhi::makeFrameDiscardFailureResult(
                "Vulkan RHI swapchain discard frame is not drawable",
                frame.frameSerial,
                frame.frameIndex);
        }
        if (nativeReady_) {
            // Unsubmitted acquire has no render-finished signal or Present
            // layout. The common native gate retains it instead of issuing
            // an unsynchronised presentation as a cancellation substitute.
            const auto released = presentNativeFrame(frame);
            if (!released.success) {
                return rhi::makeFrameDiscardFailureResult(
                    "Vulkan RHI swapchain discard failed: " + released.diagnostics,
                    frame.frameSerial,
                    frame.frameIndex,
                    released.status == rhi::PresentStatus::SurfaceLost
                        ? rhi::FrameDiscardStatus::SurfaceLost
                        : rhi::FrameDiscardStatus::Failed);
            }
            return rhi::makeFrameDiscardSuccessResult(frame.frameSerial, frame.frameIndex);
        }
        clearFrameSlot(frame.frameIndex);
        return rhi::makeFrameDiscardSuccessResult(frame.frameSerial, frame.frameIndex);
    }

    rhi::PresentResult present(const rhi::AcquiredFrame& frame,
        const rhi::FramePresentationCompletion& completion = {}) override
    {
        // Native Vulkan swapchain images retain their submitted command and
        // fence through SwapchainImagePayload. That established native path
        // remains the authoritative completion source for image retirement.
        (void)completion;
        if (!surfaceAvailable_) {
            return rhi::makePresentFailureResult(
                "Vulkan RHI swapchain present blocked by unavailable surface",
                frame.frameSerial,
                frame.frameIndex,
                rhi::PresentStatus::SurfaceLost);
        }
        if (!frame.success || frame.frameIndex >= maxFramesInFlight()) {
            return rhi::makePresentFailureResult("Vulkan RHI swapchain present frame is invalid",
                frame.frameSerial,
                frame.frameIndex);
        }
        if (nativeReady_) {
            return presentNativeFrame(frame);
        }
        if (nativeRequested_) {
            return rhi::makePresentFailureResult(
                "RHI-VULKAN-SWAPCHAIN-NATIVE-UNAVAILABLE", frame.frameSerial, frame.frameIndex);
        }
        if (!frameSlotDrawable(frame)) {
            return rhi::makePresentFailureResult(
                "Vulkan RHI swapchain present frame is not in flight",
                frame.frameSerial,
                frame.frameIndex);
        }
        if (stats_) {
            ++stats_->backendSwapchainPresentCount;
        }
        clearFrameSlot(frame.frameIndex);
        return rhi::makePresentSuccessResult(frame.frameSerial, frame.frameIndex);
    }

    rhi::SwapchainLifecycleResult setSurfaceAvailable(bool available) override
    {
        if (nativeFailure_)
            return {false,
                "RHI-VULKAN-SWAPCHAIN-REPLACEMENT-REQUIRED",
                false,
                false,
                true,
                false,
                desc_.extent};
        if (device_ && !device_->syntheticRuntime()) {
            if (!retireCompletedNativeFrameSlots() || frameSlotsInFlight_[0]
                || frameSlotsInFlight_[1]) {
                return {false,
                    "RHI-VULKAN-SURFACE-FRAMES-RETAINED",
                    surfaceAvailable_,
                    surfaceAvailable_,
                    false,
                    false,
                    desc_.extent};
            }
        }
        surfaceAvailable_ = available;
        clearFrameSlots();
        return {
            true,
            "",
            available,
            available,
            available,
            !available,
            desc_.extent,
        };
    }

    rhi::SwapchainLifecycleResult resize(const rhi::Extent2D& extent) override
    {
        if (nativeFailure_)
            return {false,
                "RHI-VULKAN-SWAPCHAIN-REPLACEMENT-REQUIRED",
                false,
                false,
                true,
                false,
                desc_.extent};
        const bool valid = extent.width > 0 && extent.height > 0;
        if (!valid) {
            return {
                false,
                "Vulkan RHI swapchain resize extent is invalid",
                surfaceAvailable_,
                surfaceAvailable_,
                true,
                true,
                desc_.extent,
            };
        }
        if (nativeRequested_) {
            const auto oldSwapchain = nativeSwapchain_;
            const bool oldReady = nativeReady_;
            const auto oldPresentMode = nativePresentMode_;
            if (device_ && !device_->syntheticRuntime()) {
                for (std::uint32_t slot = 0; slot < maxFramesInFlight(); ++slot) {
                    if (frameSlotsInFlight_[slot] && !frameSlotsPresented_[slot]) {
                        return {false,
                            "RHI-VULKAN-RESIZE-FRAME-ACQUIRED",
                            surfaceAvailable_,
                            surfaceAvailable_,
                            true,
                            true,
                            desc_.extent};
                    }
                }
                if (device_->presentFenceEnabled()) {
                    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::nanoseconds(
                            VulkanRhiFrameWaitPolicy::frameFenceTimeoutNanoseconds);
                    std::pmr::string drainDiagnostics;
                    if (!drainVulkanRhiSubmittedCommandsUntil(deadline, drainDiagnostics)
                        || !device_->drainPresentFencesUntil(deadline, drainDiagnostics)) {
                        return {false,
                            drainDiagnostics,
                            surfaceAvailable_,
                            surfaceAvailable_,
                            true,
                            true,
                            desc_.extent};
                    }
                } else {
                    auto* deviceWaitIdle = functionFrom<PFN_vkDeviceWaitIdle>(
                        device_->getDeviceProcAddr("vkDeviceWaitIdle"));
                    if (!deviceWaitIdle || deviceWaitIdle(device_->logicalDevice()) != VK_SUCCESS) {
                        return {false,
                            "RHI-VULKAN-RESIZE-LEGACY-IDLE-FAILED",
                            surfaceAvailable_,
                            surfaceAvailable_,
                            true,
                            true,
                            desc_.extent};
                    }
                }
                retireCompletedVulkanRhiSubmittedCommands();
                if (lifetime_ && lifetime_.use_count() != 2) {
                    return {false,
                        "RHI-VULKAN-RESIZE-IMAGE-OWNERS-RETAINED",
                        surfaceAvailable_,
                        surfaceAvailable_,
                        true,
                        false,
                        desc_.extent};
                }
            }
            bool oldSwapchainRetired = false;
            const bool created =
                createNativeSwapchain(oldSwapchain, extent, diagnostics_, &oldSwapchainRetired);
            if (!created) {
                nativePresentMode_ = oldPresentMode;
                if (oldSwapchainRetired) {
                    destroyNativeSwapchain(oldSwapchain);
                    nativeSwapchain_ = VK_NULL_HANDLE;
                    nativeReady_ = false;
                    clearFrameSlots();
                } else {
                    nativeSwapchain_ = oldSwapchain;
                    nativeReady_ = oldReady;
                }
                return {false,
                    diagnostics_,
                    surfaceAvailable_,
                    surfaceAvailable_ && nativeReady_,
                    true,
                    true,
                    desc_.extent};
            }
            nativeReady_ = true;
            destroyNativeSwapchain(oldSwapchain);
            if (stats_) {
                ++stats_->nativeSwapchainResizeCount;
            }
        } else {
            desc_.extent = extent;
        }
        clearFrameSlots();
        return {true, "", surfaceAvailable_, surfaceAvailable_, true, true, desc_.extent};
    }

    rhi::SwapchainLifecycleResult reconfigure(
        const rhi::SwapchainReconfigureRequest& request) override
    {
        if (request.desc.extent.width == 0 || request.desc.extent.height == 0) {
            return {false,
                "Vulkan RHI swapchain reconfigure extent is invalid",
                surfaceAvailable_,
                surfaceAvailable_,
                false,
                false,
                desc_.extent};
        }
        const auto previousDesc = desc_;
        desc_ = request.desc;
        const auto result = resize(request.desc.extent);
        if (!result.success)
            desc_ = previousDesc;
        return result;
    }

private:
    std::uint32_t findFreeFrameSlot() const
    {
        for (std::uint32_t offset = 0; offset < maxFramesInFlight(); ++offset) {
            const auto slot = (nextFrameSlot_ + offset) % maxFramesInFlight();
            if (!frameSlotsInFlight_[slot]) {
                return slot;
            }
        }
        return maxFramesInFlight();
    }

    void markFrameSlotInFlight(std::uint32_t slot, std::uint64_t serial)
    {
        frameSlotsInFlight_[slot] = true;
        frameSlotsPresented_[slot] = false;
        frameSlotSerials_[slot] = serial;
        nextFrameSlot_ = (slot + 1u) % maxFramesInFlight();
    }

    void markFrameSlotPresented(std::uint32_t slot)
    {
        if (slot < maxFramesInFlight()) {
            frameSlotsPresented_[slot] = true;
        }
    }

    void clearFrameSlot(std::uint32_t slot)
    {
        if (slot >= maxFramesInFlight()) {
            return;
        }
        if (presentFenceTokens_[slot] && device_) {
            device_->abandonPresentFence(presentFenceTokens_[slot]);
            presentFenceTokens_[slot] = 0;
        }
        frameSlotsInFlight_[slot] = false;
        frameSlotsPresented_[slot] = false;
        frameSlotSerials_[slot] = 0;
        nativeImageIndexBySlot_[slot] = 0;
        frameSyncStates_[slot].reset();
    }

    void clearFrameSlots()
    {
        for (std::uint32_t slot = 0; slot < maxFramesInFlight(); ++slot) {
            clearFrameSlot(slot);
        }
        nativeFrameAcquired_ = false;
        nextFrameSlot_ = 0;
    }

    bool frameSlotInFlight(const rhi::AcquiredFrame& frame) const
    {
        return frame.frameIndex < maxFramesInFlight() && frameSlotsInFlight_[frame.frameIndex]
            && frameSlotSerials_[frame.frameIndex] == frame.frameSerial;
    }

    bool frameSlotDrawable(const rhi::AcquiredFrame& frame) const
    {
        return !nativeFailure_ && frameSlotInFlight(frame)
            && !frameSlotsPresented_[frame.frameIndex];
    }

    bool retireCompletedNativeFrameSlots()
    {
        for (std::uint32_t slot = 0; slot < maxFramesInFlight(); ++slot) {
            if (device_ && device_->presentFenceEnabled()) {
                if (frameSlotsInFlight_[slot] && frameSlotsPresented_[slot]
                    && presentFenceTokens_[slot]
                    && device_->presentFenceCompleted(presentFenceTokens_[slot])) {
                    presentFenceTokens_[slot] = 0;
                    clearFrameSlot(slot);
                }
                continue;
            }
            if (frameSlotsInFlight_[slot] && frameSlotsPresented_[slot]) {
                const auto retirement =
                    pollVulkanLegacyPresentRetirement(device_, frameSyncStates_[slot]);
                if (retirement.status == VulkanPresentRetirementStatus::Failed) {
                    diagnostics_ = retirement.diagnosticId;
                    return false;
                }
                if (retirement.status == VulkanPresentRetirementStatus::Ready)
                    clearFrameSlot(slot);
            }
        }
        nativeFrameAcquired_ = frameSlotsInFlight_[0] || frameSlotsInFlight_[1];
        return true;
    }

    bool createNativeSwapchain(VkSwapchainKHR oldSwapchain,
        const rhi::Extent2D& requestedExtent,
        std::pmr::string& diagnostics,
        bool* oldSwapchainRetired = nullptr)
    {
        if (oldSwapchainRetired)
            *oldSwapchainRetired = false;
        if (!device_ || !device_->valid() || nativeSurface_ == VK_NULL_HANDLE) {
            if (device_ && device_->syntheticRuntime()) {
                nativeSwapchain_ = syntheticHandle<VkSwapchainKHR>(1u);
                nativeImageCount_ = std::max(2u, desc_.imageCount);
                nativeExtent_ = {requestedExtent.width, requestedExtent.height};
                nativeFormat_ = formatFor(desc_.format);
                desc_.extent = requestedExtent;
                nativeImages_.clear();
                nativeFrameAcquired_ = false;
                if (stats_) {
                    ++stats_->nativeSwapchainCreateCount;
                    stats_->nativeSwapchainImageCount += nativeImageCount_;
                }
                return true;
            }
            diagnostics = "Vulkan RHI native swapchain requires a runtime and surface";
            return false;
        }
        if (!device_->refreshSurfaceDetails()) {
            diagnostics = "Vulkan RHI native swapchain failed to refresh surface details";
            return false;
        }
        if (device_->surfaceFormats().empty() || device_->presentModes().empty()) {
            diagnostics = "Vulkan RHI native swapchain surface details are incomplete";
            return false;
        }
        auto* createSwapchain = functionFrom<PFN_vkCreateSwapchainKHR>(
            device_->getDeviceProcAddr("vkCreateSwapchainKHR"));
        auto* getSwapchainImages = functionFrom<PFN_vkGetSwapchainImagesKHR>(
            device_->getDeviceProcAddr("vkGetSwapchainImagesKHR"));
        auto* createSemaphore =
            functionFrom<PFN_vkCreateSemaphore>(device_->getDeviceProcAddr("vkCreateSemaphore"));
        auto* createFence =
            functionFrom<PFN_vkCreateFence>(device_->getDeviceProcAddr("vkCreateFence"));
        if (!createSwapchain || !getSwapchainImages || !createSemaphore || !createFence) {
            diagnostics = "Vulkan RHI native swapchain missing Vulkan entrypoints";
            return false;
        }

        const auto surfaceFormat = chooseSurfaceFormat(device_->surfaceFormats(), desc_.format);
        const auto rhiFormat = textureFormatFor(surfaceFormat.format);
        if (rhiFormat == rhi::TextureFormat::Unknown) {
            diagnostics = "Vulkan RHI native swapchain selected an unsupported color format";
            return false;
        }
        const auto presentMode = choosePresentMode(device_->presentModes(), desc_.presentationMode);
        nativePresentMode_ = presentMode;
        std::cerr << "[vulkan-swapchain] request="
                  << presentationRequestName(desc_.presentationMode)
                  << " selected=" << presentModeName(presentMode) << " supported=";
        for (const auto mode : device_->presentModes())
            std::cerr << presentModeName(mode) << ',';
        std::cerr << '\n';
        const auto extentDecision = rhi::semantics::vulkan::chooseVulkanSwapchainExtent({
            requestedExtent,
            {
                device_->surfaceCapabilities().currentExtent.width,
                device_->surfaceCapabilities().currentExtent.height,
            },
            {
                device_->surfaceCapabilities().minImageExtent.width,
                device_->surfaceCapabilities().minImageExtent.height,
            },
            {
                device_->surfaceCapabilities().maxImageExtent.width,
                device_->surfaceCapabilities().maxImageExtent.height,
            },
            device_->surfaceCapabilities().currentExtent.width != UINT32_MAX,
        });
        if (!extentDecision.success) {
            diagnostics = extentDecision.diagnostics;
            return false;
        }
        const auto extent = VkExtent2D{
            extentDecision.extent.width,
            extentDecision.extent.height,
        };
        const auto requiredImageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
            | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if ((device_->surfaceCapabilities().supportedUsageFlags & requiredImageUsage)
            != requiredImageUsage) {
            diagnostics =
                "Vulkan RHI native swapchain surface does not support required image usage "
                "flags: COLOR_ATTACHMENT | TRANSFER_DST | TRANSFER_SRC";
            return false;
        }
        const auto nativeImageCount = chooseImageCount(device_->surfaceCapabilities());
        const std::uint32_t queueFamilies[] = {
            device_->graphicsQueueFamilyIndex(),
            device_->presentQueueFamilyIndex(),
        };

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = nativeSurface_;
        createInfo.minImageCount = nativeImageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = requiredImageUsage;
        if (queueFamilies[0] != queueFamilies[1]) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilies;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        createInfo.preTransform = device_->surfaceCapabilities().currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = oldSwapchain;

        VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
        const auto created =
            createSwapchain(device_->logicalDevice(), &createInfo, nullptr, &newSwapchain);
        if (created != VK_SUCCESS) {
            diagnostics = vkFailure("vkCreateSwapchainKHR", created);
            return false;
        }
        if (oldSwapchainRetired && oldSwapchain)
            *oldSwapchainRetired = true;

        std::uint32_t actualImageCount = 0;
        const auto imageCountQueried =
            getSwapchainImages(device_->logicalDevice(), newSwapchain, &actualImageCount, nullptr);
        if (imageCountQueried != VK_SUCCESS || actualImageCount == 0) {
            destroyNativeSwapchain(newSwapchain);
            diagnostics = "Vulkan RHI native swapchain failed to query images";
            return false;
        }
        std::pmr::vector<VkImage> images(actualImageCount);
        const auto imagesQueried = getSwapchainImages(
            device_->logicalDevice(), newSwapchain, &actualImageCount, images.data());
        if (imagesQueried != VK_SUCCESS || actualImageCount == 0) {
            destroyNativeSwapchain(newSwapchain);
            diagnostics = "Vulkan RHI native swapchain failed to fetch images";
            return false;
        }
        images.resize(actualImageCount);

        if (!imageAvailable_[0]) {
            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            for (auto& semaphore : imageAvailable_) {
                const auto semaphoreCreated =
                    createSemaphore(device_->logicalDevice(), &semaphoreInfo, nullptr, &semaphore);
                if (semaphoreCreated != VK_SUCCESS) {
                    destroyNativeSwapchain(newSwapchain);
                    diagnostics = vkFailure("vkCreateSemaphore", semaphoreCreated);
                    return false;
                }
            }
        }
        if (!renderFinished_[0]) {
            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            for (auto& semaphore : renderFinished_) {
                const auto semaphoreCreated =
                    createSemaphore(device_->logicalDevice(), &semaphoreInfo, nullptr, &semaphore);
                if (semaphoreCreated != VK_SUCCESS) {
                    destroyNativeSwapchain(newSwapchain);
                    diagnostics = vkFailure("vkCreateSemaphore", semaphoreCreated);
                    return false;
                }
            }
        }
        if (!imageAcquired_[0]) {
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            for (auto& fence : imageAcquired_) {
                const auto fenceCreated =
                    createFence(device_->logicalDevice(), &fenceInfo, nullptr, &fence);
                if (fenceCreated != VK_SUCCESS) {
                    destroyNativeSwapchain(newSwapchain);
                    diagnostics = vkFailure("vkCreateFence", fenceCreated);
                    return false;
                }
            }
        }

        nativeSwapchain_ = newSwapchain;
        nativeImageCount_ = actualImageCount;
        nativeExtent_ = extent;
        nativeFormat_ = surfaceFormat.format;
        desc_.format = rhiFormat;
        desc_.extent = {extent.width, extent.height};
        nativeImages_ = std::move(images);
        nativeFrameAcquired_ = false;
        if (stats_) {
            ++stats_->nativeSwapchainCreateCount;
            stats_->nativeSwapchainImageCount += actualImageCount;
        }
        return true;
    }

    rhi::AcquiredFrame acquireNativeFrame()
    {
        if (!retireCompletedNativeFrameSlots())
            return rhi::makeAcquireFailureResult(diagnostics_, frameSerial_, 0);
        auto slot = findFreeFrameSlot();
        if (slot >= maxFramesInFlight() && device_ && device_->presentFenceEnabled()) {
            for (std::uint32_t candidate = 0; candidate < maxFramesInFlight(); ++candidate) {
                if (!frameSlotsInFlight_[candidate] || !frameSlotsPresented_[candidate]
                    || !presentFenceTokens_[candidate])
                    continue;
                const auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::nanoseconds(
                        VulkanRhiFrameWaitPolicy::frameFenceTimeoutNanoseconds);
                if (device_->waitPresentFenceUntil(
                        presentFenceTokens_[candidate], deadline, diagnostics_)) {
                    presentFenceTokens_[candidate] = 0;
                    clearFrameSlot(candidate);
                    slot = findFreeFrameSlot();
                }
                break;
            }
        }
        if (slot >= maxFramesInFlight()) {
            return rhi::makeAcquireFailureResult(
                "Vulkan RHI swapchain has no free double-buffer frame slot", frameSerial_, 0);
        }
        if (device_ && device_->syntheticRuntime()) {
            ++frameSerial_;
            nativeFrameAcquired_ = true;
            nativeFrameSerial_ = frameSerial_;
            nativeFrameSlot_ = slot;
            nativeImageIndex_ = slot;
            nativeImageIndexBySlot_[slot] = slot;
            markFrameSlotInFlight(slot, frameSerial_);
            if (stats_) {
                ++stats_->nativeSwapchainAcquireCount;
                ++stats_->backendSwapchainAcquireCount;
            }
            return rhi::makeAcquireSuccessResult(frameSerial_, slot);
        }
        auto* acquireNextImage = functionFrom<PFN_vkAcquireNextImageKHR>(
            device_->getDeviceProcAddr("vkAcquireNextImageKHR"));
        auto* resetFences =
            functionFrom<PFN_vkResetFences>(device_->getDeviceProcAddr("vkResetFences"));
        auto* waitForFences =
            functionFrom<PFN_vkWaitForFences>(device_->getDeviceProcAddr("vkWaitForFences"));
        if (!acquireNextImage || !resetFences || !waitForFences) {
            return rhi::makeAcquireFailureResult(
                "Vulkan RHI native swapchain missing acquire fence entrypoints", frameSerial_, 0);
        }
        std::uint32_t imageIndex = 0;
        if (frameSyncStates_[slot]) {
            const auto submittedCommand = frameSyncStates_[slot]->submittedCommand();
            if (submittedCommand) {
                std::pmr::string retirementDiagnostics;
                if (!submittedCommand->waitSubmittedFence(retirementDiagnostics)) {
                    return rhi::makeAcquireFailureResult(retirementDiagnostics, frameSerial_, slot);
                }
            }
        }
        const auto fence = imageAcquired_[slot];
        const auto reset = resetFences(device_->logicalDevice(), 1, &fence);
        if (reset != VK_SUCCESS) {
            return rhi::makeAcquireFailureResult(
                vkFailure("vkResetFences", reset), frameSerial_, slot);
        }
        acquireFenceIssued_[slot] = false;
        const auto acquired = acquireNextImage(device_->logicalDevice(),
            nativeSwapchain_,
            VulkanRhiFrameWaitPolicy::acquireNextImageTimeoutNanoseconds,
            imageAvailable_[slot],
            fence,
            &imageIndex);
        if (acquired == VK_TIMEOUT) {
            return rhi::makeAcquireFailureResult(
                "vkAcquireNextImageKHR timed out", frameSerial_, slot);
        }
        const auto acquireDecision = rhi::semantics::vulkan::lowerVulkanSwapchainAcquireStatus(
            nativeFrameStatusFor(acquired));
        if (!acquireDecision.success) {
            return rhi::makeAcquireFailureResult(vkFailure("vkAcquireNextImageKHR", acquired),
                frameSerial_,
                slot,
                acquireDecision.status);
        }
        // Native acquire has already transferred an image and asynchronous
        // semaphore/fence operations. Own it before the fallible host wait.
        acquireFenceIssued_[slot] = true;
        nativeWorkIssued_ = true;
        ++frameSerial_;
        nativeFrameAcquired_ = true;
        nativeFrameSerial_ = frameSerial_;
        nativeFrameSlot_ = slot;
        nativeImageIndex_ = imageIndex;
        nativeImageIndexBySlot_[slot] = imageIndex;
        markFrameSlotInFlight(slot, frameSerial_);
        const auto waited = waitForFences(device_->logicalDevice(),
            1,
            &fence,
            VK_TRUE,
            VulkanRhiFrameWaitPolicy::frameFenceTimeoutNanoseconds);
        if (waited != VK_SUCCESS) {
            nativeFailure_ = true;
            lifetime_->markUnproven();
            return rhi::makeAcquireFailureResult(
                "RHI-VULKAN-SWAPCHAIN-ACQUIRE-COMPLETION-UNPROVEN", frameSerial_, slot);
        }
        if (stats_) {
            ++stats_->nativeSwapchainAcquireCount;
            ++stats_->backendSwapchainAcquireCount;
        }
        return rhi::makeAcquireSuccessResult(frameSerial_, slot, acquireDecision.status);
    }

    rhi::PresentResult presentNativeFrame(const rhi::AcquiredFrame& frame)
    {
        if (nativeFailure_)
            return rhi::makePresentFailureResult(
                "RHI-VULKAN-SWAPCHAIN-REPLACEMENT-REQUIRED", frame.frameSerial, frame.frameIndex);
        if (!nativeFrameAcquired_ || !frameSlotDrawable(frame)) {
            return rhi::makePresentFailureResult(
                "Vulkan RHI native present frame does not match acquired frame",
                frame.frameSerial,
                frame.frameIndex);
        }
        if (device_ && device_->syntheticRuntime()) {
            clearFrameSlot(frame.frameIndex);
            nativeFrameAcquired_ = frameSlotsInFlight_[0] || frameSlotsInFlight_[1];
            if (stats_) {
                ++stats_->nativeSwapchainPresentCount;
                ++stats_->backendSwapchainPresentCount;
            }
            return rhi::makePresentSuccessResult(frame.frameSerial, frame.frameIndex);
        }
        auto* queuePresent =
            functionFrom<PFN_vkQueuePresentKHR>(device_->getDeviceProcAddr("vkQueuePresentKHR"));
        if (!queuePresent) {
            return rhi::makePresentFailureResult(
                "Vulkan RHI native swapchain missing vkQueuePresentKHR",
                frame.frameSerial,
                frame.frameIndex);
        }
        const auto renderFinished = renderFinished_[frame.frameIndex];
        const auto imageIndex = nativeImageIndexBySlot_[frame.frameIndex];
        const auto frameSyncState = frameSyncStates_[frame.frameIndex];
        const bool waitForRenderedFrame =
            frameSyncState && frameSyncState->renderSubmitted() && renderFinished;
        if (!waitForRenderedFrame)
            return rhi::makePresentFailureResult("RHI-VULKAN-SWAPCHAIN-UNSUBMITTED-FRAME-RETAINED",
                frame.frameSerial,
                frame.frameIndex);
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = waitForRenderedFrame ? 1u : 0u;
        presentInfo.pWaitSemaphores = waitForRenderedFrame ? &renderFinished : nullptr;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &nativeSwapchain_;
        presentInfo.pImageIndices = &imageIndex;
        VkFence presentFence = VK_NULL_HANDLE;
        std::uint64_t presentToken = 0;
        VkSwapchainPresentFenceInfoKHR fenceInfo{};
        if (device_->presentFenceEnabled()) {
            if (!device_->reservePresentFence(presentFence, presentToken, diagnostics_)) {
                return rhi::makePresentFailureResult(
                    diagnostics_, frame.frameSerial, frame.frameIndex);
            }
            fenceInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR;
            fenceInfo.swapchainCount = 1u;
            fenceInfo.pFences = &presentFence;
            presentInfo.pNext = &fenceInfo;
        }
        const auto presented = queuePresent(device_->presentQueue(), &presentInfo);
        const auto presentDecision = rhi::semantics::vulkan::lowerVulkanSwapchainPresentStatus(
            nativeFrameStatusFor(presented));
        if (!presentDecision.success) {
            // A returned error cannot prove whether the native queue consumed
            // the binary semaphore. Close admission and retain all owners.
            presentFenceTokens_[frame.frameIndex] = presentToken;
            markFrameSlotPresented(frame.frameIndex);
            nativeFailure_ = true;
            lifetime_->markUnproven();
            nativeFrameAcquired_ = frameSlotsInFlight_[0] || frameSlotsInFlight_[1];
            return rhi::makePresentFailureResult(vkFailure("vkQueuePresentKHR", presented),
                frame.frameSerial,
                frame.frameIndex,
                presentDecision.status);
        }
        if (frameSyncStates_[frame.frameIndex])
            frameSyncStates_[frame.frameIndex]->nativePresentAccepted_ = true;
        presentFenceTokens_[frame.frameIndex] = presentToken;
        markFrameSlotPresented(frame.frameIndex);
        nativeFrameAcquired_ = frameSlotsInFlight_[0] || frameSlotsInFlight_[1];
        if (stats_) {
            ++stats_->nativeSwapchainPresentCount;
            ++stats_->backendSwapchainPresentCount;
        }
        return rhi::makePresentSuccessResult(
            frame.frameSerial, frame.frameIndex, presentDecision.status);
    }

    void destroyNativeSwapchain(VkSwapchainKHR swapchain)
    {
        if (!device_ || !swapchain) {
            return;
        }
        if (device_->syntheticRuntime()) {
            return;
        }
        auto* destroySwapchain = functionFrom<PFN_vkDestroySwapchainKHR>(
            device_->getDeviceProcAddr("vkDestroySwapchainKHR"));
        if (destroySwapchain) {
            destroySwapchain(device_->logicalDevice(), swapchain, nullptr);
        }
    }

    /// Destroy per-slot semaphores and fences (but not the swapchain itself).
    /// Called on resize to clear pending semaphore operations before the
    /// semaphores are recreated.  The caller must ensure the device is idle
    /// (vkDeviceWaitIdle) before calling this.
    void destroyNativeSyncObjects()
    {
        if (!device_ || device_->syntheticRuntime()) {
            return;
        }
        auto* destroyFence =
            functionFrom<PFN_vkDestroyFence>(device_->getDeviceProcAddr("vkDestroyFence"));
        auto* destroySemaphore =
            functionFrom<PFN_vkDestroySemaphore>(device_->getDeviceProcAddr("vkDestroySemaphore"));
        if (destroyFence) {
            for (auto& fence : imageAcquired_) {
                if (fence) {
                    destroyFence(device_->logicalDevice(), fence, nullptr);
                    fence = VK_NULL_HANDLE;
                }
            }
        }
        if (destroySemaphore) {
            for (auto& semaphore : imageAvailable_) {
                if (semaphore) {
                    destroySemaphore(device_->logicalDevice(), semaphore, nullptr);
                    semaphore = VK_NULL_HANDLE;
                }
            }
            for (auto& semaphore : renderFinished_) {
                if (semaphore) {
                    destroySemaphore(device_->logicalDevice(), semaphore, nullptr);
                    semaphore = VK_NULL_HANDLE;
                }
            }
        }
    }

    void destroyNative()
    {
        if (!device_) {
            return;
        }
        if (lifetime_) {
            lifetime_->requestRetirement(nativeSwapchain_,
                imageAvailable_,
                renderFinished_,
                imageAcquired_,
                acquireFenceIssued_,
                presentFenceTokens_,
                nativeWorkIssued_);
            const std::weak_ptr<VulkanSwapchainLifetime> pending = lifetime_;
            lifetime_.reset();
            if (const auto owner = pending.lock()) {
                const char* diagnostic = "";
                if (!owner->tryRetire(diagnostic))
                    std::fprintf(stderr, "[%s] native swapchain lifetime retained\n", diagnostic);
            }
            imageAvailable_ = renderFinished_ = {};
            imageAcquired_ = {};
        } else {
            destroyNativeSyncObjects();
            destroyNativeSwapchain(nativeSwapchain_);
        }
        nativeSwapchain_ = VK_NULL_HANDLE;
    }

    rhi::SwapchainDesc desc_;
    std::shared_ptr<VulkanRhiRuntime> device_;
    std::shared_ptr<VulkanSwapchainLifetime> lifetime_;
    VkSurfaceKHR nativeSurface_ = VK_NULL_HANDLE;
    std::shared_ptr<VulkanRhiNativeResourceStats> stats_;
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry_;
    VkSwapchainKHR nativeSwapchain_ = VK_NULL_HANDLE;
    VkPresentModeKHR nativePresentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    VkFormat nativeFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D nativeExtent_{};
    std::uint32_t nativeImageCount_ = 0;
    std::pmr::vector<VkImage> nativeImages_;
    std::array<VkSemaphore, 2> imageAvailable_{};
    std::array<VkSemaphore, 2> renderFinished_{};
    std::array<VkFence, 2> imageAcquired_{};
    std::array<bool, 2> acquireFenceIssued_{};
    std::array<bool, 2> frameSlotsInFlight_{};
    std::array<bool, 2> frameSlotsPresented_{};
    std::array<std::uint64_t, 2> frameSlotSerials_{};
    std::array<std::uint64_t, 2> presentFenceTokens_{};
    std::array<std::uint32_t, 2> nativeImageIndexBySlot_{};
    std::array<std::shared_ptr<VulkanRhiSwapchainFrameSyncState>, 2> frameSyncStates_{};
    std::pmr::string diagnostics_;
    bool nativeRequested_ = false;
    bool nativeWorkIssued_ = false;
    bool nativeFailure_ = false;
    bool nativeReady_ = false;
    bool surfaceAvailable_ = true;
    bool nativeFrameAcquired_ = false;
    std::uint32_t nextFrameSlot_ = 0;
    std::uint64_t frameSerial_ = 0;
    std::uint64_t nativeFrameSerial_ = 0;
    std::uint32_t nativeFrameSlot_ = 0;
    std::uint32_t nativeImageIndex_ = 0;
};

std::size_t retireVulkanRhiSwapchainLifetimes(
    const std::shared_ptr<VulkanRhiRuntime>& runtime, const char*& diagnostic) noexcept
{
    return VulkanSwapchainLifetime::retireDomain(runtime, diagnostic);
}

std::size_t pendingVulkanRhiSwapchainLifetimes(
    const std::shared_ptr<VulkanRhiRuntime>& runtime) noexcept
{
    return VulkanSwapchainLifetime::pendingDomain(runtime);
}

VulkanPresentRetirementResult pollVulkanLegacyPresentRetirement(
    const std::shared_ptr<VulkanRhiRuntime>& runtime,
    const std::shared_ptr<VulkanRhiSwapchainFrameSyncState>& frame)
{
    using Status = VulkanPresentRetirementStatus;
    if (!runtime || runtime->syntheticRuntime() || !runtime->valid())
        return {Status::Failed, "RHI-VULKAN-PRESENT-RETIREMENT-NATIVE-UNAVAILABLE"};
    if (!frame)
        return {};
    if (!frame->belongsTo(runtime))
        return {Status::Failed, "RHI-VULKAN-PRESENT-RETIREMENT-OWNER-MISMATCH"};
    if (!frame->renderSubmitted())
        return {};
    if (!frame->graphicsCompleted()) {
        if (const auto command = frame->submittedCommand())
            (void)command->submittedFenceCompleted();
        if (!frame->graphicsCompleted())
            return {};
    }
    const auto waitIdle =
        functionFrom<PFN_vkQueueWaitIdle>(runtime->getDeviceProcAddr("vkQueueWaitIdle"));
    if (!waitIdle || !runtime->presentQueue())
        return {Status::Failed, "RHI-VULKAN-PRESENT-RETIREMENT-IDLE-UNAVAILABLE"};
    const auto result = waitIdle(runtime->presentQueue());
    if (result != VK_SUCCESS)
        return {Status::Failed,
            result == VK_ERROR_DEVICE_LOST ? "RHI-VULKAN-PRESENT-RETIREMENT-DEVICE-LOST"
                                           : "RHI-VULKAN-PRESENT-RETIREMENT-IDLE-FAILED",
            result};
    return {Status::Ready, "", result};
}

arp::core::UniquePtr<rhi::Swapchain> createVulkanRhiSwapchain(const rhi::SwapchainDesc& desc,
    VulkanRhiNativeResourceAllocator nativeResources,
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry)
{
    if (desc.extent.width == 0 || desc.extent.height == 0) {
        return nullptr;
    }
    auto swapchain = arp::core::makeUnique<VulkanRhiSwapchain>(
        desc, std::move(nativeResources), std::move(resourceRegistry));
    if (!swapchain->ready()) {
        rhi::RhiErrorHandler::get().report(
            rhi::RhiSeverity::Error, swapchain->diagnostics().c_str());
        return {};
    }
    return swapchain;
}

} // namespace arp::rhi_vulkan
