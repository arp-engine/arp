#pragma once
#include "rhi/backends/webgpu/webgpu_native_buffer_map_state.h"
#include "rhi/buffer_readback_range.h"

#include <cstddef>
#include <cstdint>

namespace arp::rhi_webgpu {
// Backend-private NativeBufferMapReceipt/1. The original cold slot owns this
// object and must outlive genuine C future delivery, including synchronous
// abort delivery during unmap. No synthetic completion or bridge-state writes.
class WebGpuNativeBufferMapReceipt final {
public:
    enum class Phase { Empty, Prepared, Pending, Delivered };
    enum class Outcome { Success, NativeError };
    using Callback = void (*)(void*, Outcome) noexcept;
    using Range = rhi::BufferReadbackRangeContract::Range;
    // WebGPU GPUBuffer.mapAsync size must be a multiple of four bytes.
    static constexpr std::size_t mapSizeAlignment = 4u;
    WebGpuNativeBufferMapReceipt() = default;
    WebGpuNativeBufferMapReceipt(const WebGpuNativeBufferMapReceipt&) = delete;
    WebGpuNativeBufferMapReceipt& operator=(const WebGpuNativeBufferMapReceipt&) = delete;
    const char* prepareCold(void* buffer, std::size_t bytes, void* userdata, Callback) noexcept;
    const char* prepareColdRange(void* buffer, const Range&, void* userdata, Callback) noexcept;
    const char* validateCold() const noexcept;
    // Exact cold native binding and genuine C-receipt phase, without authority
    // to issue commands, commit original ownership or publish a snapshot.
    const char* validateRetainedBinding(void* buffer, std::size_t bytes) const noexcept;
    const char* validateRetainedRange(void* buffer, const Range&) const noexcept;
    const char* start() noexcept;
    // Copies only a genuinely mapped range, then unmaps even on read failure.
    // CPU destination is an exact caller-owned cold range, never allocated here.
    const char* readAndUnmap(void* destination, std::size_t bytes) noexcept;
    // Does not fabricate delivery: pending futures must really invoke C callback.
    const char* abortAndDrain() noexcept;
    // Caller must already have genuine physical device-loss evidence. Destroy
    // the exact quarantined buffer through C; never repair bridge bookkeeping.
    const char* destroyAndDrainAfterNativeLoss() noexcept;

    Phase phase() const noexcept { return phase_; }

    bool nativeMapSucceeded() const noexcept { return phase_ == Phase::Delivered && success_; }

    bool nativeUnmapProven() const noexcept { return unmapProven_; }

    bool invocationUncertain() const noexcept { return invocationUncertain_; }

private:
    static void invokeMap(void*) noexcept;
    static void invokeRead(void*) noexcept;
    static void invokeUnmap(void*) noexcept;
    static void invokeDestroy(void*) noexcept;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void completeMap(WGPUMapAsyncStatus, WGPUStringView, void*, void*) noexcept;
#endif
    void* buffer_ = nullptr;
    Range range_;
    void* userdata_ = nullptr;
    Callback callback_ = nullptr;
    Phase phase_ = Phase::Empty;
    bool success_ = false, unmapProven_ = false, invocationUncertain_ = false,
         readSucceeded_ = false, futureIssued_ = false;
    void* destination_ = nullptr;
};
} // namespace arp::rhi_webgpu
