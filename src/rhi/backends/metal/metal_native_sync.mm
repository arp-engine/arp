#include "rhi/backends/metal/metal_native_sync.h"

#include "core/cpu_allocator.h"
#include "rhi/backends/metal/metal_native_runtime.h"

#include <Metal/Metal.h>
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
    if (this->value() >= value) {
        return true;
    }
    if (timeoutNs == 0u) {
        return false;
    }
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

namespace {

id<MTLDevice> defaultMetalDevice()
{
    return reinterpret_cast<id<MTLDevice>>(metalRhiNativeDeviceHandle());
}

std::shared_ptr<void> retainNativeObject(id object)
{
    if (object == nil) {
        return {};
    }
    return std::shared_ptr<void>(reinterpret_cast<void*>(object), [](void* nativeObject) {
        if (nativeObject) {
            [reinterpret_cast<id>(nativeObject) release];
        }
    });
}

} // namespace

std::shared_ptr<MetalRhiNativeSharedEventPayload> createMetalRhiNativeSharedEventPayload(
    const char* label)
{
    auto payload = arp::core::makeShared<MetalRhiNativeSharedEventPayload>();
    payload->nativeDevice = metalRhiNativeDeviceIdentity();
    payload->state = arp::core::makeShared<MetalRhiNativeSharedEventState>();
    if (!@available(macOS 10.14, iOS 12.0, *)) {
        return payload;
    }
    id<MTLDevice> device = defaultMetalDevice();
    if (device == nil) {
        return payload;
    }
    id<MTLSharedEvent> event = [device newSharedEvent];
    if (event == nil) {
        return payload;
    }
    event.signaledValue = 0;
    if (label != nullptr) {
        event.label = [NSString stringWithUTF8String:label];
    }
    MTLSharedEventListener* listener = [[MTLSharedEventListener alloc] init];
    if (listener == nil) {
        [event release];
        return payload;
    }
    payload->nativeEvent = retainNativeObject(event);
    payload->nativeListener = retainNativeObject(listener);
    payload->nativeRuntimeBacked = true;
    payload->nativeRuntimeDiagnosticOnly = false;
    return payload;
}

void signalMetalRhiNativeSharedEvent(
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>& payload, std::uint64_t value)
{
    if (!payload || !payload->state) {
        return;
    }
    payload->state->signal(value);
    if (!payload->nativeRuntimeBacked || !payload->nativeEvent) {
        return;
    }
    id<MTLSharedEvent> event = reinterpret_cast<id<MTLSharedEvent>>(payload->nativeEvent.get());
    if (event.signaledValue < value) {
        event.signaledValue = value;
    }
}

void waitMetalRhiNativeSharedEvent(
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>& payload, std::uint64_t value)
{
    if (payload && payload->state) {
        payload->state->wait(value);
    }
}

bool waitForMetalRhiNativeSharedEvent(
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>& payload,
    std::uint64_t value,
    std::uint64_t timeoutNs)
{
    return payload && payload->state && payload->state->waitFor(value, timeoutNs);
}

bool encodeMetalRhiNativeSharedEventWait(const std::shared_ptr<void>& nativeCommandBuffer,
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>& payload,
    std::uint64_t value,
    std::pmr::string& diagnostics)
{
    if (!@available(macOS 10.14, iOS 12.0, *)) {
        diagnostics = "RHI-METAL-SHARED-EVENT-API-UNAVAILABLE";
        return false;
    }
    if (!nativeCommandBuffer || !payload || !payload->nativeRuntimeBacked || !payload->nativeEvent
        || !payload->nativeDevice) {
        diagnostics = "RHI-METAL-SHARED-EVENT-WAIT-PAYLOAD-MISSING";
        return false;
    }
    id<MTLCommandBuffer> commandBuffer =
        reinterpret_cast<id<MTLCommandBuffer>>(nativeCommandBuffer.get());
    id<MTLSharedEvent> event = reinterpret_cast<id<MTLSharedEvent>>(payload->nativeEvent.get());
    id<MTLDevice> payloadDevice = reinterpret_cast<id<MTLDevice>>(payload->nativeDevice.get());
    if (commandBuffer == nil || event == nil || commandBuffer.commandQueue == nil
        || payloadDevice == nil || commandBuffer.commandQueue.device != payloadDevice) {
        diagnostics = "RHI-METAL-SHARED-EVENT-WAIT-NATIVE-OBJECT-MISSING";
        return false;
    }
    [commandBuffer encodeWaitForEvent:event value:value];
    return true;
}

bool encodeMetalRhiNativeSharedEventSignal(const std::shared_ptr<void>& nativeCommandBuffer,
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>& payload,
    std::uint64_t value,
    std::pmr::string& diagnostics)
{
    if (!@available(macOS 10.14, iOS 12.0, *)) {
        diagnostics = "RHI-METAL-SHARED-EVENT-API-UNAVAILABLE";
        return false;
    }
    if (!nativeCommandBuffer || !payload || !payload->nativeRuntimeBacked || !payload->nativeEvent
        || !payload->nativeListener || !payload->state || !payload->nativeDevice) {
        diagnostics = "RHI-METAL-SHARED-EVENT-SIGNAL-PAYLOAD-MISSING";
        return false;
    }
    id<MTLCommandBuffer> commandBuffer =
        reinterpret_cast<id<MTLCommandBuffer>>(nativeCommandBuffer.get());
    id<MTLSharedEvent> event = reinterpret_cast<id<MTLSharedEvent>>(payload->nativeEvent.get());
    MTLSharedEventListener* listener =
        reinterpret_cast<MTLSharedEventListener*>(payload->nativeListener.get());
    id<MTLDevice> payloadDevice = reinterpret_cast<id<MTLDevice>>(payload->nativeDevice.get());
    if (commandBuffer == nil || event == nil || listener == nil || commandBuffer.commandQueue == nil
        || payloadDevice == nil || commandBuffer.commandQueue.device != payloadDevice) {
        diagnostics = "RHI-METAL-SHARED-EVENT-SIGNAL-NATIVE-OBJECT-MISSING";
        return false;
    }
    auto state = payload->state;
    [event notifyListener:listener
                  atValue:value
                    block:^(id<MTLSharedEvent>, uint64_t signaledValue) {
                        state->signal(signaledValue);
                    }];
    [commandBuffer encodeSignalEvent:event value:value];
    return true;
}

bool metalRhiNativeTimelineSynchronizationAvailable()
{
    if (!@available(macOS 10.14, iOS 12.0, *))
        return false;
    id<MTLDevice> device = defaultMetalDevice();
    if (device == nil)
        return false;
    id<MTLSharedEvent> event = [device newSharedEvent];
    const bool available = event != nil;
    [event release];
    return available;
}

} // namespace arp::rhi_metal
