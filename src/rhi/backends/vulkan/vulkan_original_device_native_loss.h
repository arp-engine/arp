#pragma once
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/frontend/resource_device_lifetime.h"

namespace arp::rhi_vulkan {
// One weak runtime subscription. Binding retains runtime; runtime never owns
// binding, so facade/observation custody cannot create a lifetime cycle. Native
// results, preparation and detachment require the original external serialization.
class VulkanOriginalDeviceNativeResultLoss final : public rhi::OriginalDeviceNativeLossBinding {
    friend class VulkanRhiRuntime;

    class ConstructionKey {
        friend class VulkanOriginalDeviceNativeResultLoss;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    VulkanOriginalDeviceNativeResultLoss(ConstructionKey,
        const rhi::OriginalDeviceLifetime::NativeLossReceiver&,
        rhi::OriginalDeviceNativeResultLossBudget,
        std::shared_ptr<VulkanRhiRuntime>);
    ~VulkanOriginalDeviceNativeResultLoss() override;
    static std::shared_ptr<rhi::OriginalDeviceNativeLossBinding> prepare(
        std::shared_ptr<VulkanRhiRuntime>,
        const rhi::OriginalDeviceLifetime::NativeLossReceiver&,
        const rhi::OriginalDeviceNativeResultLossBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&);
    const char* diagnostic() const noexcept override;
    const char* detach() noexcept override;

private:
    void deliverNativeLoss() noexcept;
    enum class Phase { Unissued, Armed, NativeLost, Detached };
    const rhi::OriginalDeviceLifetime::NativeLossReceiver receiver_;
    const rhi::OriginalDeviceNativeResultLossBudget budget_;
    const std::shared_ptr<VulkanRhiRuntime> runtime_;
    Phase phase_ = Phase::Unissued;
};
} // namespace arp::rhi_vulkan
