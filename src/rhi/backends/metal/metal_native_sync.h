#pragma once

#include "core/memory/cpu_allocator.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

namespace arp::rhi_metal {

class MetalRhiNativeSharedEventState final {
public:
    void signal(std::uint64_t value) noexcept;
    std::uint64_t value() const noexcept;
    void wait(std::uint64_t value) const;
    bool waitFor(std::uint64_t value, std::uint64_t timeoutNs) const;

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable condition_;
    std::atomic<std::uint64_t> value_ = 0;
};

struct MetalRhiNativeSharedEventPayload {
    std::shared_ptr<void> nativeDevice;
    std::shared_ptr<void> nativeEvent;
    std::shared_ptr<void> nativeListener;
    std::shared_ptr<MetalRhiNativeSharedEventState> state;
    bool nativeRuntimeBacked = false;
    bool nativeRuntimeDiagnosticOnly = true;
};

std::shared_ptr<MetalRhiNativeSharedEventPayload> createMetalRhiNativeSharedEventPayload(
    const char* label);

void signalMetalRhiNativeSharedEvent(
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>& payload, std::uint64_t value);

void waitMetalRhiNativeSharedEvent(
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>& payload, std::uint64_t value);

bool waitForMetalRhiNativeSharedEvent(
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>& payload,
    std::uint64_t value,
    std::uint64_t timeoutNs);

bool encodeMetalRhiNativeSharedEventWait(const std::shared_ptr<void>& nativeCommandBuffer,
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>& payload,
    std::uint64_t value,
    std::pmr::string& diagnostics);

bool encodeMetalRhiNativeSharedEventSignal(const std::shared_ptr<void>& nativeCommandBuffer,
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>& payload,
    std::uint64_t value,
    std::pmr::string& diagnostics);

bool metalRhiNativeTimelineSynchronizationAvailable();

} // namespace arp::rhi_metal
