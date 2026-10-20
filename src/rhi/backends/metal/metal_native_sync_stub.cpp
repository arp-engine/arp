#include "rhi/backends/metal/metal_native_sync.h"

#include <chrono>

namespace arp::rhi_metal {

void MetalRhiNativeSharedEventState::signal(std::uint64_t value) noexcept
{
    auto current = value_.load(std::memory_order_acquire);
    while (current < value
        && !value_.compare_exchange_weak(
            current, value, std::memory_order_release, std::memory_order_acquire)) {
    }
    condition_.notify_all();
}

std::uint64_t MetalRhiNativeSharedEventState::value() const noexcept
{
    return value_.load(std::memory_order_acquire);
}

void MetalRhiNativeSharedEventState::wait(std::uint64_t value) const
{
    std::unique_lock lock(mutex_);
    condition_.wait(lock, [this, value] {
        return this->value() >= value;
    });
}

bool MetalRhiNativeSharedEventState::waitFor(std::uint64_t value, std::uint64_t timeoutNs) const
{
    std::unique_lock lock(mutex_);
    if (this->value() >= value)
        return true;
    if (timeoutNs == 0u)
        return false;
    if (timeoutNs == UINT64_MAX) {
        condition_.wait(lock, [this, value] {
            return this->value() >= value;
        });
        return true;
    }
    return condition_.wait_for(lock, std::chrono::nanoseconds(timeoutNs), [this, value] {
        return this->value() >= value;
    });
}

std::shared_ptr<MetalRhiNativeSharedEventPayload> createMetalRhiNativeSharedEventPayload(
    const char*)
{
    return {};
}

void signalMetalRhiNativeSharedEvent(
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>&, std::uint64_t)
{}

void waitMetalRhiNativeSharedEvent(
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>&, std::uint64_t)
{}

bool waitForMetalRhiNativeSharedEvent(
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>&, std::uint64_t, std::uint64_t)
{
    return false;
}

bool encodeMetalRhiNativeSharedEventWait(const std::shared_ptr<void>&,
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>&,
    std::uint64_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-SHARED-EVENT-WAIT-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool encodeMetalRhiNativeSharedEventSignal(const std::shared_ptr<void>&,
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>&,
    std::uint64_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-SHARED-EVENT-SIGNAL-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool metalRhiNativeTimelineSynchronizationAvailable()
{
    return false;
}

} // namespace arp::rhi_metal
