#pragma once
#include "rhi/backends/webgpu/native_validation.h"

#include <cstdint>
#include <utility>

namespace arp::rhi_webgpu {
// NativeCompletionReceipt/1: bounded backend-owned promise adapter. Cold slots
// own one foreign state and two listeners each, like the existing loss bridge.
// Subscription uses the same native queue/scope promise; no future, success or
// replacement callback is synthesized. Listener userdata clears before delivery.
class WebGpuNativeCompletionReceipt final {
public:
    enum class Phase : std::uint32_t { Prepared = 0u, Bound = 1u, Delivered = 2u, Suppressed = 3u };
    enum class Kind : std::uint32_t { Queue, Scope };
    enum class Outcome : std::uint32_t { Success, NativeError, Rejected };
    using Callback = void (*)(void*, Outcome) noexcept;

    class Prepared final {
    public:
        Prepared() = default;

        explicit Prepared(std::uintptr_t state) noexcept : state_(state) {}

        Prepared(const Prepared&) = delete;
        Prepared& operator=(const Prepared&) = delete;

        Prepared(Prepared&& other) noexcept : state_(std::exchange(other.state_, 0u)) {}

        Prepared& operator=(Prepared&& other) noexcept
        {
            if (this != &other) {
                WebGpuNativeCompletionReceipt::release(state_);
                state_ = std::exchange(other.state_, 0u);
            }
            return *this;
        }

        ~Prepared() { WebGpuNativeCompletionReceipt::release(state_); }

        explicit operator bool() const noexcept { return state_ != 0u; }

        std::uintptr_t release() noexcept { return std::exchange(state_, 0u); }

        bool matches(
            void* device, void* queue, Kind kind, void* userdata, Callback callback) const noexcept
        {
            return WebGpuNativeCompletionReceipt::matchesPrepared(
                state_, device, queue, kind, userdata, callback);
        }

    private:
        std::uintptr_t state_ = 0u;
    };

    static Prepared prepare(void* device, void* queue, Kind, void* userdata, Callback) noexcept;
    static const char* inspect(void* device, void* queue) noexcept;
    // Admission only, never native completion. Prevent two cold shared
    // recorders from interleaving the device's LIFO error-scope transactions.
    static bool claimScopeTransaction(void* device, const void* owner) noexcept;
    static bool releaseScopeTransaction(void* device, const void* owner) noexcept;
    // False includes unknown/thrown invocation, never a proven no-op. Caller
    // must publish MaySubmit and submitted retention before entering this API.
    static_assert(WebGpuErrorScopeContract::count == 3u
            && WebGpuErrorScopeContract::filters[0] == WebGpuErrorScopeContract::Filter::Internal
            && WebGpuErrorScopeContract::filters[1] == WebGpuErrorScopeContract::Filter::OutOfMemory
            && WebGpuErrorScopeContract::filters[2] == WebGpuErrorScopeContract::Filter::Validation,
        "NativeCompletionReceipt/1 guarded native filters must match ErrorScopeContract/1");
    static bool pushScopes(void* device) noexcept;
    static bool invoke(void* queue, void* commandBuffer) noexcept;
    // InitializedCanonicalBufferStaging/1: full source bytes are already frozen
    // and retained. False has unknown invocation effects, just like submit.
    static bool writeBuffer(
        void* queue, void* buffer, const void* bytes, std::uint64_t size) noexcept;
    // False proves listener userdata suppression, not native queue completion.
    // Queue registration failure retains submitted work until physical loss.
    static bool bind(std::uintptr_t) noexcept;
    static bool prepared(std::uintptr_t) noexcept;
    // Exact cold native binding, not callback/completion evidence.
    static bool matchesPrepared(
        std::uintptr_t, void* device, void* queue, Kind, void* userdata, Callback) noexcept;
    static bool matchesIssued(
        std::uintptr_t, void* device, void* queue, Kind, void* userdata, Callback) noexcept;
    static bool matchesDelivery(std::uintptr_t,
        void* device,
        void* queue,
        Kind,
        void* userdata,
        Callback,
        Outcome) noexcept;
    static void release(std::uintptr_t) noexcept;
};
} // namespace arp::rhi_webgpu
