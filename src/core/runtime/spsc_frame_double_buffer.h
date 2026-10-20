#pragma once

// SPSC (single-producer single-consumer) frame double buffer with a
// frame-completion fence, for the render-thread / logic-thread split.
//
// The logic (producer) thread assembles a per-frame submission in one slot
// while the render (consumer) thread executes the previously published
// submission from the other slot. Two slots give one frame of overlap with
// bounded memory (no unbounded queue growth). A separate frame-completion
// serial lets the logic thread wait for the render thread to finish a specific
// frame before reading back render-thread-written state (performance snapshot,
// close-requested flag, GPU timing) -- without that, those read-backs would
// race the render thread's write.
//
// Design invariants (guarded by
// tests/contract/spsc_frame_double_buffer_contract_tests.cpp):
//   - Exactly one producer thread and one consumer thread.
//   - Slot state machine: Writing -> Ready -> Reading -> Consumed -> Writing.
//     Only the producer transitions Writing->Ready (publishWrite) and
//     Consumed->Writing (acquireForWrite). Only the consumer transitions
//     Ready->Reading (acquireForRead) and Reading->Consumed (releaseRead).
//   - acquireForWrite blocks (back-pressure) until a slot is Consumed; this
//     bounds in-flight submissions to 2 and prevents unbounded growth.
//   - acquireForRead blocks until a slot is Ready, or returns nullptr once
//     requestStop() has been called (clean shutdown).
//   - signalFrameCompleted(serial) is called by the consumer after it finishes
//     a frame; waitFrameCompleted(target) blocks the producer until
//     completedFrameSerial_ >= target.
//
// Style follows src/rhi/frontend/frame_slot_state_machine.h: header-only,
// noexcept where possible, no owned native resources. Unlike that helper
// (which is a pure slot tracker with no producer/consumer semantics), this one
// owns the submission storage and provides publish/consume + stop/fence
// synchronization. Uses std::mutex/cv (low-frequency wakeups; the hot path is
// the in-slot work, not the slot handoff).

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace arp::core::runtime {

enum class FrameSlotState : std::uint32_t {
    Writing,  // producer is assembling the submission in this slot
    Ready,    // producer finished; waiting for consumer to pick it up
    Reading,  // consumer is executing the submission
    Consumed, // consumer finished; slot available for the next write
};

// Fixed-capacity (2-slot) SPSC frame double buffer for type T. T must be
// default-constructible and copyable/movable as needed by the producer's
// assembly pattern (the producer writes into the slot in place, so T only
// needs to be default-constructible + assignable per field).
template <typename T> class SpscFrameDoubleBuffer {
public:
    SpscFrameDoubleBuffer() = default;
    ~SpscFrameDoubleBuffer() = default;

    SpscFrameDoubleBuffer(const SpscFrameDoubleBuffer&) = delete;
    SpscFrameDoubleBuffer& operator=(const SpscFrameDoubleBuffer&) = delete;
    SpscFrameDoubleBuffer(SpscFrameDoubleBuffer&&) = delete;
    SpscFrameDoubleBuffer& operator=(SpscFrameDoubleBuffer&&) = delete;

    // ---- Producer (logic thread) ----

    // Acquire a writable slot, blocking until one is Consumed (back-pressure).
    // The returned reference is valid until the next publishWrite() +
    // acquireForWrite() cycle on this slot.
    T& acquireForWrite()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] {
            return stopRequested_.load(std::memory_order_acquire) || hasConsumedSlotLocked();
        });
        // If stop is requested during shutdown, prefer a Consumed slot if one
        // exists so the producer can still drain; otherwise return slot 0.
        const std::uint32_t slot = pickConsumedSlotLocked();
        state_[slot].store(FrameSlotState::Writing, std::memory_order_release);
        writeIndex_.store(slot, std::memory_order_release);
        return slots_[slot];
    }

    // Publish the current write slot (Writing -> Ready) and wake the consumer.
    // The slot is stamped with a monotonically increasing publish serial so
    // the consumer can drain Ready slots in FIFO order even when both slots
    // are Ready (which happens transiently when the producer publishes the
    // second frame before the consumer reads the first).
    void publishWrite()
    {
        const std::uint32_t slot = writeIndex_.load(std::memory_order_acquire);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_[slot].store(FrameSlotState::Ready, std::memory_order_release);
            publishSerial_[slot] = ++nextPublishSerial_;
        }
        cv_.notify_one();
    }

    // Wait until the consumer has completed the frame with the given serial.
    // Used by the producer before reading back render-thread-written state.
    void waitFrameCompleted(std::uint64_t targetSerial)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this, targetSerial] {
            return completedFrameSerial_.load(std::memory_order_acquire) >= targetSerial
                || stopRequested_.load(std::memory_order_acquire);
        });
    }

    // ---- Consumer (render thread) ----

    // Acquire a readable submission, blocking until one is Ready. Returns
    // nullptr once requestStop() has been called and no Ready slot remains
    // (clean shutdown: the consumer drains pending work then exits). When both
    // slots are Ready, the oldest published slot is returned first (FIFO).
    const T* acquireForRead()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] {
            return stopRequested_.load(std::memory_order_acquire) || hasReadySlotLocked();
        });
        if (!hasReadySlotLocked()) {
            // Stop requested and nothing left to drain.
            return nullptr;
        }
        const std::uint32_t slot = pickOldestReadySlotLocked();
        state_[slot].store(FrameSlotState::Reading, std::memory_order_release);
        return &slots_[slot];
    }

    // Release the current read slot (Reading -> Consumed). Called after the
    // consumer has finished executing the submission (including any
    // signalFrameCompleted).
    void releaseRead()
    {
        // The consumer always releases the slot it most recently acquired for
        // reading. We identify it as the slot currently in Reading state.
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::uint32_t slot = 0; slot < kSlotCount; ++slot) {
            if (state_[slot].load(std::memory_order_acquire) == FrameSlotState::Reading) {
                state_[slot].store(FrameSlotState::Consumed, std::memory_order_release);
                cv_.notify_one();
                return;
            }
        }
    }

    // Signal that the consumer has finished the frame with the given serial.
    // Typically called after releaseRead() for that frame. Wakes any producer
    // blocked in waitFrameCompleted().
    void signalFrameCompleted(std::uint64_t serial)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            completedFrameSerial_.store(serial, std::memory_order_release);
        }
        cv_.notify_all();
    }

    // ---- Shutdown ----

    // Request the consumer to stop. Wakes acquireForRead() (returns nullptr
    // once no Ready slot remains) and waitFrameCompleted(). The producer must
    // still publish its in-flight write before the consumer can drain it.
    void requestStop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopRequested_.store(true, std::memory_order_release);
        }
        cv_.notify_all();
    }

    bool stopRequested() const noexcept { return stopRequested_.load(std::memory_order_acquire); }

    // Scheduler-only park between deferred consumption attempts. Keep the
    // current Reading slot; unrelated publishes do not imply input readiness.
    // A stop request interrupts the park. No GPU completion is inferred here.
    bool waitForRetry(std::chrono::milliseconds cadence)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return !cv_.wait_for(lock, cadence, [this] {
            return stopRequested_.load(std::memory_order_acquire);
        });
    }

    std::uint64_t completedFrameSerial() const noexcept
    {
        return completedFrameSerial_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::uint32_t kSlotCount = 2;
    static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;

    bool hasConsumedSlotLocked() const noexcept
    {
        for (std::uint32_t slot = 0; slot < kSlotCount; ++slot) {
            if (state_[slot].load(std::memory_order_acquire) == FrameSlotState::Consumed) {
                return true;
            }
        }
        return false;
    }

    bool hasReadySlotLocked() const noexcept
    {
        for (std::uint32_t slot = 0; slot < kSlotCount; ++slot) {
            if (state_[slot].load(std::memory_order_acquire) == FrameSlotState::Ready) {
                return true;
            }
        }
        return false;
    }

    // Pick the Consumed slot to write into (round-robin-ish: prefer the one
    // that is not the just-released read slot to maximize overlap). Falls back
    // to slot 0 if neither is Consumed (only happens under stop during
    // shutdown).
    std::uint32_t pickConsumedSlotLocked() const noexcept
    {
        for (std::uint32_t slot = 0; slot < kSlotCount; ++slot) {
            if (state_[slot].load(std::memory_order_acquire) == FrameSlotState::Consumed) {
                return slot;
            }
        }
        return 0;
    }

    // Pick the Ready slot with the oldest publish serial (FIFO drain order).
    // Must only be called when hasReadySlotLocked() is true. With 2 slots, at
    // most 2 are Ready; the one published earlier (smaller serial) is drained
    // first so the consumer observes frames in the order the producer published
    // them. A slot that has never been published has serial 0; a freshly
    // published slot has serial >= 1, so any Ready slot compares meaningfully.
    std::uint32_t pickOldestReadySlotLocked() const noexcept
    {
        std::uint32_t best = kInvalid;
        std::uint64_t bestSerial = ~std::uint64_t{0};
        for (std::uint32_t slot = 0; slot < kSlotCount; ++slot) {
            if (state_[slot].load(std::memory_order_acquire) == FrameSlotState::Ready
                && publishSerial_[slot] < bestSerial) {
                best = slot;
                bestSerial = publishSerial_[slot];
            }
        }
        // hasReadySlotLocked() guaranteed at least one Ready slot, so best is
        // always set. Fall back to 0 defensively.
        return best == kInvalid ? 0u : best;
    }

    std::array<T, kSlotCount> slots_{};
    std::array<std::atomic<FrameSlotState>, kSlotCount> state_ = {
        {{FrameSlotState::Consumed}, {FrameSlotState::Consumed}}};
    // Per-slot publish serial, stamped by publishWrite() under mutex_. Used by
    // pickOldestReadySlotLocked() for FIFO drain order. Slot 0 + slot 1 both
    // start at 0 (never published); the first publishWrite() stamps serial 1.
    std::array<std::uint64_t, kSlotCount> publishSerial_{{0u, 0u}};
    std::uint64_t nextPublishSerial_ = 0u;
    std::atomic<std::uint32_t> writeIndex_{0};
    std::atomic<std::uint64_t> completedFrameSerial_{0};
    std::atomic<bool> stopRequested_{false};
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace arp::core::runtime
