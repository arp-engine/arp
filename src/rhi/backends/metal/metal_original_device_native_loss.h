#pragma once
#include "rhi/frontend/resource_device_lifetime.h"
#include "rhi/frontend/resource_device_native_loss.h"

#include <atomic>
#include <mutex>

namespace arp::test {
class MetalNativeResultLossFixture;
}

namespace arp::rhi_metal {
class MetalOriginalDeviceNativeResultLoss;
class MetalRhiNativeCommandPayload;
class MetalDispatchValidationSession;

// One source for the backend's actual shared native device. Source subscription is
// weak; a completed command never owns or queues a loss callback/receiver.
class MetalOriginalDeviceNativeResultLossSource final : public core::SharedObjectIdentity {
    friend class MetalOriginalDeviceNativeResultLoss;
    friend class MetalRhiNativeCommandPayload;
    friend class MetalDispatchValidationSession;
    friend class arp::test::MetalNativeResultLossFixture;
    friend std::shared_ptr<MetalOriginalDeviceNativeResultLossSource>
    sharedMetalRhiOriginalResultLossSource();

    class ConstructionKey {
        friend std::shared_ptr<MetalOriginalDeviceNativeResultLossSource>
        sharedMetalRhiOriginalResultLossSource();
        friend class arp::test::MetalNativeResultLossFixture;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalOriginalDeviceNativeResultLossSource(ConstructionKey, std::shared_ptr<void> device)
        : device_(std::move(device))
    {}

    const auto& device() const noexcept { return device_; }

    bool lost() const noexcept { return lost_.load(std::memory_order_acquire); }

private:
    const char* attach(const std::shared_ptr<MetalOriginalDeviceNativeResultLoss>&) noexcept;
    const char* detach(const MetalOriginalDeviceNativeResultLoss*) noexcept;
    const char* validate(const MetalOriginalDeviceNativeResultLoss*) const noexcept;
    void observeDeviceRemoval(const void* actualDevice) noexcept;
    const std::shared_ptr<void> device_;
    mutable std::mutex mutex_;
    std::weak_ptr<MetalOriginalDeviceNativeResultLoss> subscription_;
    // Hot observation never locks the cold subscription mutex or touches its
    // weak handle. Attach/detach publish the current live binding identity.
    std::atomic<const MetalOriginalDeviceNativeResultLoss*> subscriptionIdentity_{nullptr};
    std::atomic<bool> lost_{false};
};

// Initialized with shared native infrastructure during cold preparation.
std::shared_ptr<MetalOriginalDeviceNativeResultLossSource> sharedMetalRhiOriginalResultLossSource();
bool metalRhiNativeDeviceRemovalResultsAvailable() noexcept;

class MetalOriginalDeviceNativeResultLoss final : public rhi::OriginalDeviceNativeLossBinding {
    friend class MetalOriginalDeviceNativeResultLossSource;
    friend class arp::test::MetalNativeResultLossFixture;

    class ConstructionKey {
        friend class MetalOriginalDeviceNativeResultLoss;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalOriginalDeviceNativeResultLoss(ConstructionKey,
        const rhi::OriginalDeviceLifetime::NativeLossReceiver&,
        rhi::OriginalDeviceNativeResultLossBudget,
        std::shared_ptr<MetalOriginalDeviceNativeResultLossSource>);
    ~MetalOriginalDeviceNativeResultLoss() override;
    static std::shared_ptr<rhi::OriginalDeviceNativeLossBinding> prepare(
        std::shared_ptr<MetalOriginalDeviceNativeResultLossSource>,
        const rhi::OriginalDeviceLifetime::NativeLossReceiver&,
        const rhi::OriginalDeviceNativeResultLossBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&);
    const char* diagnostic() const noexcept override;
    const char* detach() noexcept override;

private:
    void deliverNativeLoss() const noexcept;
    enum class Phase { Unissued, Armed, NativeLost, Detached };
    const rhi::OriginalDeviceLifetime::NativeLossReceiver receiver_;
    const rhi::OriginalDeviceNativeResultLossBudget budget_;
    const std::shared_ptr<MetalOriginalDeviceNativeResultLossSource> source_;
    mutable std::atomic<Phase> phase_{Phase::Unissued};
};
} // namespace arp::rhi_metal
