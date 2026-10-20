#include "rhi/backends/vulkan/vulkan_original_device_native_loss.h"

namespace arp::rhi_vulkan {
VulkanOriginalDeviceNativeResultLoss::VulkanOriginalDeviceNativeResultLoss(ConstructionKey,
    const rhi::OriginalDeviceLifetime::NativeLossReceiver& receiver,
    rhi::OriginalDeviceNativeResultLossBudget budget,
    std::shared_ptr<VulkanRhiRuntime> runtime)
    : receiver_(receiver), budget_(budget), runtime_(std::move(runtime))
{}

VulkanOriginalDeviceNativeResultLoss::~VulkanOriginalDeviceNativeResultLoss()
{
    detach();
}

std::shared_ptr<rhi::OriginalDeviceNativeLossBinding> VulkanOriginalDeviceNativeResultLoss::prepare(
    std::shared_ptr<VulkanRhiRuntime> runtime,
    const rhi::OriginalDeviceLifetime::NativeLossReceiver& receiver,
    const rhi::OriginalDeviceNativeResultLossBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    if ((diagnostic = rhi::OriginalDeviceNativeResultLossContract::validateIntent(
             budget, quota ? quota->limitBytes() : 0u)))
        return {};
    if ((diagnostic = receiver.diagnostic()))
        return {};
    if (!runtime || runtime->syntheticRuntime() || !runtime->valid() || !runtime->logicalDevice()) {
        diagnostic = "RHI-ORIGINAL-RESULT-LOSS-NATIVE-SOURCE-UNAVAILABLE";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto binding = shareOriginal(core::makeUnique<VulkanOriginalDeviceNativeResultLoss>(
            ConstructionKey{}, receiver, budget, std::move(runtime)));
        if ((diagnostic = binding->runtime_->attachOriginalResultLoss(binding)))
            return {};
        binding->phase_ = Phase::Armed;
        diagnostic = nullptr;
        return binding;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-ORIGINAL-RESULT-LOSS-ALLOCATION";
        return {};
    }
}

const char* VulkanOriginalDeviceNativeResultLoss::diagnostic() const noexcept
{
    if (const auto* id = receiver_.diagnostic())
        return id;
    if (const auto* id = rhi::OriginalDeviceNativeResultLossContract::validateIntent(
            budget_, originalMetadataQuota() ? originalMetadataQuota()->limitBytes() : 0u))
        return id;
    if (phase_ != Phase::Armed)
        return "RHI-ORIGINAL-RESULT-LOSS-RETIRED";
    return runtime_->validateOriginalResultLoss(this);
}

const char* VulkanOriginalDeviceNativeResultLoss::detach() noexcept
{
    if (phase_ == Phase::Detached || phase_ == Phase::Unissued)
        return nullptr;
    if (const auto* id = runtime_->detachOriginalResultLoss(this))
        return id;
    phase_ = Phase::Detached;
    return nullptr;
}

void VulkanOriginalDeviceNativeResultLoss::deliverNativeLoss() noexcept
{
    if (phase_ != Phase::Armed)
        return;
    phase_ = Phase::NativeLost;
    receiver_.deliverNativeLoss();
}
} // namespace arp::rhi_vulkan
