#include "rhi/backends/vulkan/vulkan_timeline_semaphore.h"

#include <utility>

namespace arp::rhi_vulkan {

VulkanRhiTimelineSemaphorePayload::VulkanRhiTimelineSemaphorePayload(
    std::shared_ptr<VulkanRhiRuntime> runtime)
    : runtime_(std::move(runtime))
{}

VulkanRhiTimelineSemaphorePayload::~VulkanRhiTimelineSemaphorePayload()
{
    if (semaphore_ && destroy_)
        destroy_(runtime_->logicalDevice(), semaphore_, nullptr);
}

std::shared_ptr<VulkanRhiTimelineSemaphorePayload> VulkanRhiTimelineSemaphorePayload::create(
    std::shared_ptr<VulkanRhiRuntime> runtime,
    std::uint64_t initialValue,
    core::pmr::String& diagnostic)
{
    if (!runtime || !runtime->valid() || runtime->syntheticRuntime()
        || !runtime->timelineSemaphoreEnabled()) {
        diagnostic = "RHI-VULKAN-TIMELINE-NATIVE-RUNTIME-UNAVAILABLE";
        return {};
    }
    auto create =
        reinterpret_cast<PFN_vkCreateSemaphore>(runtime->getDeviceProcAddr("vkCreateSemaphore"));
    auto destroy =
        reinterpret_cast<PFN_vkDestroySemaphore>(runtime->getDeviceProcAddr("vkDestroySemaphore"));
    auto signal =
        reinterpret_cast<PFN_vkSignalSemaphore>(runtime->getDeviceProcAddr("vkSignalSemaphore"));
    auto wait =
        reinterpret_cast<PFN_vkWaitSemaphores>(runtime->getDeviceProcAddr("vkWaitSemaphores"));
    auto query = reinterpret_cast<PFN_vkGetSemaphoreCounterValue>(
        runtime->getDeviceProcAddr("vkGetSemaphoreCounterValue"));
    if (!create || !destroy || !signal || !wait || !query) {
        diagnostic = "RHI-VULKAN-TIMELINE-ENTRYPOINT-UNAVAILABLE";
        return {};
    }
    std::shared_ptr<VulkanRhiTimelineSemaphorePayload> payload;
    try {
        payload = core::makeShared<VulkanRhiTimelineSemaphorePayload>(std::move(runtime));
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-VULKAN-TIMELINE-HOST-ALLOCATION-FAILED";
        return {};
    }
    payload->destroy_ = destroy;
    payload->signal_ = signal;
    payload->wait_ = wait;
    payload->query_ = query;
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type.initialValue = initialValue;
    VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    info.pNext = &type;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    const auto result = create(payload->runtime_->logicalDevice(), &info, nullptr, &semaphore);
    payload->runtime_->captureOriginalDeviceLoss(result);
    if (result != VK_SUCCESS) {
        diagnostic = result == VK_ERROR_DEVICE_LOST ? "RHI-VULKAN-TIMELINE-CREATE-DEVICE-LOST"
                                                    : "RHI-VULKAN-TIMELINE-CREATE-FAILED";
        return {};
    }
    if (!semaphore) {
        diagnostic = "RHI-VULKAN-TIMELINE-CREATE-PAYLOAD-MISSING";
        return {};
    }
    payload->semaphore_ = semaphore;
    diagnostic.clear();
    return payload;
}

VkSemaphore VulkanRhiTimelineSemaphorePayload::nativeHandle() const noexcept
{
    return semaphore_;
}

bool VulkanRhiTimelineSemaphorePayload::belongsTo(const VulkanRhiRuntime& runtime) const noexcept
{
    return semaphore_ && runtime_ && runtime.valid() && !runtime.syntheticRuntime()
        && runtime_->logicalDevice() == runtime.logicalDevice()
        && runtime_->instance() == runtime.instance()
        && runtime_->physicalDevice() == runtime.physicalDevice();
}

VkResult VulkanRhiTimelineSemaphorePayload::hostSignal(std::uint64_t value) const
{
    if (!semaphore_ || !signal_)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkSemaphoreSignalInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
    info.semaphore = semaphore_;
    info.value = value;
    const auto result = signal_(runtime_->logicalDevice(), &info);
    runtime_->captureOriginalDeviceLoss(result);
    return result;
}

VkResult VulkanRhiTimelineSemaphorePayload::hostWait(
    std::uint64_t value, std::uint64_t timeoutNs) const
{
    if (!semaphore_ || !wait_)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkSemaphoreWaitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    info.semaphoreCount = 1u;
    info.pSemaphores = &semaphore_;
    info.pValues = &value;
    const auto result = wait_(runtime_->logicalDevice(), &info, timeoutNs);
    runtime_->captureOriginalDeviceLoss(result);
    return result;
}

VkResult VulkanRhiTimelineSemaphorePayload::queryCounter(std::uint64_t& value) const
{
    if (!semaphore_ || !query_)
        return VK_ERROR_INITIALIZATION_FAILED;
    std::uint64_t queried = 0u;
    const auto result = query_(runtime_->logicalDevice(), semaphore_, &queried);
    runtime_->captureOriginalDeviceLoss(result);
    if (result == VK_SUCCESS)
        value = queried;
    return result;
}

} // namespace arp::rhi_vulkan
