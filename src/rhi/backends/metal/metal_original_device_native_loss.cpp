#include "rhi/backends/metal/metal_original_device_native_loss.h"

#include "rhi/backends/metal/metal_native_runtime.h"

namespace arp::rhi_metal {
std::shared_ptr<MetalOriginalDeviceNativeResultLossSource> sharedMetalRhiOriginalResultLossSource()
{
    static const auto source = [] {
        core::CpuMemoryQuotaScope scope(nullptr);
        const auto device = metalRhiNativeDeviceIdentity();
        return device && metalRhiNativeDeviceRemovalResultsAvailable()
            ? core::makeShared<MetalOriginalDeviceNativeResultLossSource>(
                  MetalOriginalDeviceNativeResultLossSource::ConstructionKey{}, device)
            : std::shared_ptr<MetalOriginalDeviceNativeResultLossSource>{};
    }();
    return source;
}

const char* MetalOriginalDeviceNativeResultLossSource::attach(
    const std::shared_ptr<MetalOriginalDeviceNativeResultLoss>& binding) noexcept
{
    std::lock_guard lock(mutex_);
    if (lost())
        return "RHI-METAL-RESULT-LOSS-DEVICE-LOST";
    if (!subscription_.expired())
        return "RHI-METAL-RESULT-LOSS-SOURCE-CAPACITY";
    subscription_ = binding;
    subscriptionIdentity_.store(binding.get(), std::memory_order_release);
    return nullptr;
}

const char* MetalOriginalDeviceNativeResultLossSource::detach(
    const MetalOriginalDeviceNativeResultLoss* binding) noexcept
{
    std::lock_guard lock(mutex_);
    const auto retained = subscription_.lock();
    if (retained && retained.get() != binding)
        return "RHI-METAL-RESULT-LOSS-FOREIGN-SOURCE";
    subscription_.reset();
    subscriptionIdentity_.store(nullptr, std::memory_order_release);
    return nullptr;
}

const char* MetalOriginalDeviceNativeResultLossSource::validate(
    const MetalOriginalDeviceNativeResultLoss* binding) const noexcept
{
    if (lost())
        return "RHI-METAL-RESULT-LOSS-DEVICE-LOST";
    return subscriptionIdentity_.load(std::memory_order_acquire) == binding
        ? nullptr
        : "RHI-METAL-RESULT-LOSS-FOREIGN-SOURCE";
}

void MetalOriginalDeviceNativeResultLossSource::observeDeviceRemoval(
    const void* actualDevice) noexcept
{
    if (!device_ || actualDevice != device_.get())
        return;
    std::shared_ptr<MetalOriginalDeviceNativeResultLoss> retained;
    {
        std::lock_guard lock(mutex_);
        lost_.store(true, std::memory_order_release);
        retained = subscription_.lock();
    }
    if (retained)
        retained->deliverNativeLoss();
}

MetalOriginalDeviceNativeResultLoss::MetalOriginalDeviceNativeResultLoss(ConstructionKey,
    const rhi::OriginalDeviceLifetime::NativeLossReceiver& receiver,
    rhi::OriginalDeviceNativeResultLossBudget budget,
    std::shared_ptr<MetalOriginalDeviceNativeResultLossSource> source)
    : receiver_(receiver), budget_(budget), source_(std::move(source))
{}

MetalOriginalDeviceNativeResultLoss::~MetalOriginalDeviceNativeResultLoss()
{
    detach();
}

std::shared_ptr<rhi::OriginalDeviceNativeLossBinding> MetalOriginalDeviceNativeResultLoss::prepare(
    std::shared_ptr<MetalOriginalDeviceNativeResultLossSource> source,
    const rhi::OriginalDeviceLifetime::NativeLossReceiver& receiver,
    const rhi::OriginalDeviceNativeResultLossBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    if ((diagnostic = rhi::OriginalDeviceNativeResultLossContract::validateIntent(
             budget, quota ? quota->limitBytes() : 0u))
        || (diagnostic = receiver.diagnostic()))
        return {};
    if (!source || !source->sharedOwnerMatches(source) || !source->device()
        || !core::SharedOwnerProjection::sameOwner(
            source->device(), metalRhiNativeDeviceIdentity())) {
        diagnostic = "RHI-METAL-RESULT-LOSS-NATIVE-SOURCE-UNAVAILABLE";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto binding = shareOriginal(core::makeUnique<MetalOriginalDeviceNativeResultLoss>(
            ConstructionKey{}, receiver, budget, std::move(source)));
        // Arm before publishing the weak subscription: a native completion may
        // report removal as soon as attach releases its source lock.
        binding->phase_.store(Phase::Armed, std::memory_order_release);
        if ((diagnostic = binding->source_->attach(binding))) {
            binding->phase_.store(Phase::Unissued, std::memory_order_release);
            return {};
        }
        diagnostic = binding->diagnostic();
        return diagnostic ? std::shared_ptr<rhi::OriginalDeviceNativeLossBinding>{} : binding;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-ORIGINAL-RESULT-LOSS-ALLOCATION";
        return {};
    }
}

const char* MetalOriginalDeviceNativeResultLoss::diagnostic() const noexcept
{
    if (const auto* id = receiver_.diagnostic())
        return id;
    if (const auto* id = rhi::OriginalDeviceNativeResultLossContract::validateIntent(
            budget_, originalMetadataQuota() ? originalMetadataQuota()->limitBytes() : 0u))
        return id;
    if (phase_.load(std::memory_order_acquire) != Phase::Armed)
        return "RHI-ORIGINAL-RESULT-LOSS-RETIRED";
    if (source_->lost()) {
        deliverNativeLoss();
        return receiver_.diagnostic();
    }
    return source_->validate(this);
}

const char* MetalOriginalDeviceNativeResultLoss::detach() noexcept
{
    const auto previous = phase_.exchange(Phase::Detached, std::memory_order_acq_rel);
    return previous == Phase::Unissued || previous == Phase::Detached ? nullptr
                                                                      : source_->detach(this);
}

void MetalOriginalDeviceNativeResultLoss::deliverNativeLoss() const noexcept
{
    auto expected = Phase::Armed;
    if (phase_.compare_exchange_strong(
            expected, Phase::NativeLost, std::memory_order_acq_rel, std::memory_order_acquire))
        receiver_.deliverNativeLoss();
}
} // namespace arp::rhi_metal
