#pragma once

// Dedicated long-lived render thread that consumes submissions from an
// SpscFrameDoubleBuffer. This is the engine-layer sibling of
// src/core/runtime/work_service.h (a generic N-worker task pool): where
// WorkService is for short fire-and-forget tasks, RenderThread owns a single
// dedicated thread whose entire lifetime is the render session, and whose loop
// is a strict acquire-consume-release cycle over the double buffer.
//
// Per AGENTS.md (RHI contract #1) all RHI calls must be serial on one thread;
// the RenderThread is intended to be that thread. The consumer implementation
// (game layer) is what actually calls into RHI -- this header is RHI-agnostic
// and only knows about the double buffer and the consume callback.
//
// Design (follows src/rhi/frontend/frame_slot_state_machine.h header-only style):
//   - RenderThreadConsumer<T>: virtual interface the game layer implements.
//     consumeFrame() returns Completed, Deferred or Stop. Deferred retains the
//     packet for a scheduled retry; Stop terminates after consumption.
//   - RenderThread<T>: owns the std::thread. start() spawns, stop() requests
//     the double buffer to stop + joins. The run loop:
//       acquireForRead -> (nullptr => exit) -> consumeFrame -> releaseRead ->
//       signalFrameCompleted(submissionSerial).
//   - Header-only template so core does not depend on the game-layer
//     submission type (instantiation happens at the use site).

#include "core/concurrency/spsc_frame_double_buffer.h"
#include "core/diagnostics/diagnostic_error.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <utility>

namespace arp::core::runtime {

// CPU packet consumption, distinct from GPU completion/presentation.
// See RenderFrameDeferral/1. Deferred retains the current immutable packet.
enum class RenderFrameConsumeStatus { Completed, Deferred, Stop };
inline constexpr auto kRenderFrameRetryCadence = std::chrono::milliseconds{1};

enum class RenderThreadStopState { Completed, DeadlineExpired, InvalidDeadline };

/// Result of an explicit bounded render-thread shutdown attempt.  A deadline
/// result deliberately leaves the thread joinable: callers must retain all
/// objects referenced by its consumer until a later successful join.
struct RenderThreadStopReport {
    RenderThreadStopState state = RenderThreadStopState::InvalidDeadline;
    std::uint64_t elapsedNanoseconds = 0u;
    std::uint64_t remainingNanoseconds = 0u;

    bool completed() const noexcept { return state == RenderThreadStopState::Completed; }
};

// Consumer interface for the render thread. Implemented by the game layer.
// All methods are invoked on the render thread.
//
// consumeFrame attempts one packet and reports back (a) its consumption
// status and (b) the serial to signal as
// completed, so the producer's waitFrameCompleted(targetSerial) can target a
// specific frame. The serial is reported by the consumer (not read off T by
// the core) so this header never has to know T's layout -- the WoCC
// submission carries submissionSerial, but that is a game-layer contract, not
// a core-layer one.
template <typename T> class RenderThreadConsumer {
public:
    virtual ~RenderThreadConsumer() = default;

    // Execute one frame's submission. On success, set *outCompletedSerial to
    // the serial the render thread should report as completed (the producer
    // waits on this). Stop releases/signals before exit. Deferred keeps the
    // immutable packet and ignores outCompletedSerial until a terminal result.
    // No irreversible replay side effects may precede Deferred.
    virtual RenderFrameConsumeStatus consumeFrame(
        const T& submission, std::uint64_t* outCompletedSerial) = 0;
};

// Dedicated render thread. Owns one std::thread that loops over the double
// buffer. Not copyable/movable (owns a thread).
template <typename T> class RenderThread {
public:
    RenderThread(SpscFrameDoubleBuffer<T>& buffer,
        RenderThreadConsumer<T>& consumer,
        std::chrono::milliseconds retryCadence = kRenderFrameRetryCadence)
        : buffer_(buffer), consumer_(consumer), retryCadence_(retryCadence)
    {
        if (retryCadence_.count() <= 0)
            throw core::DiagnosticError("CORE-RENDER-RETRY-CADENCE-INVALID");
    }

    ~RenderThread()
    {
        // Safety net: if stop() was not called, request stop and join to
        // avoid std::terminate on destruction of a joinable thread.
        if (thread_.joinable()) {
            stop();
        }
    }

    RenderThread(const RenderThread&) = delete;
    RenderThread& operator=(const RenderThread&) = delete;
    RenderThread(RenderThread&&) = delete;
    RenderThread& operator=(RenderThread&&) = delete;

    // Spawn the render thread. Must be called exactly once before stop().
    // Calling start() twice is a no-op on the second call.
    void start()
    {
        if (started_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        {
            std::lock_guard lock(exitMutex_);
            exited_ = false;
        }
        thread_ = std::thread([this] {
            run();
        });
    }

    // Request the render thread to stop and join it. After this returns the
    // thread is no longer running and the double buffer is in the stopped
    // state. Safe to call from the logic thread. The render thread finishes
    // any in-flight frame (acquireForRead returns nullptr only after pending
    // Ready slots are drained) before exiting.
    void stop()
    {
        buffer_.requestStop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    /// Request cancellation and wait only until the caller-owned absolute
    /// deadline.  This is a lifecycle boundary, not a frame-loop primitive.
    RenderThreadStopReport stopUntil(const std::chrono::steady_clock::time_point deadline)
    {
        const auto started = std::chrono::steady_clock::now();
        buffer_.requestStop();
        if (!thread_.joinable()) {
            return makeStopReport(RenderThreadStopState::Completed, started, deadline);
        }
        if (deadline <= started) {
            return makeStopReport(RenderThreadStopState::InvalidDeadline, started, deadline);
        }
        {
            std::unique_lock lock(exitMutex_);
            if (!exitCondition_.wait_until(lock, deadline, [this] {
                    return exited_;
                })) {
                return makeStopReport(RenderThreadStopState::DeadlineExpired, started, deadline);
            }
        }
        thread_.join();
        return makeStopReport(
            RenderThreadStopState::Completed, started, deadline, std::chrono::steady_clock::now());
    }

private:
    static RenderThreadStopReport makeStopReport(const RenderThreadStopState state,
        const std::chrono::steady_clock::time_point started,
        const std::chrono::steady_clock::time_point deadline,
        const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now())
    {
        const auto elapsed =
            finished > started ? finished - started : std::chrono::steady_clock::duration::zero();
        const auto remaining =
            deadline > finished ? deadline - finished : std::chrono::steady_clock::duration::zero();
        return {
            state,
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()),
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count()),
        };
    }

    void signalExited()
    {
        {
            std::lock_guard lock(exitMutex_);
            exited_ = true;
        }
        exitCondition_.notify_all();
    }

    void run()
    {
        struct ExitSignal final {
            RenderThread& owner;

            ~ExitSignal() { owner.signalExited(); }
        } exitSignal{*this};

        while (true) {
            const T* submission = buffer_.acquireForRead();
            if (submission == nullptr) {
                // Stop requested and nothing left to drain.
                break;
            }
            std::uint64_t completedSerial = 0;
            auto outcome = consumer_.consumeFrame(*submission, &completedSerial);
            while (outcome == RenderFrameConsumeStatus::Deferred) {
                if (!buffer_.waitForRetry(retryCadence_)) {
                    // Cancellation wakes producer waits through stopRequested,
                    // never through a fabricated consumption/GPU serial.
                    buffer_.releaseRead();
                    return;
                }
                completedSerial = 0;
                outcome = consumer_.consumeFrame(*submission, &completedSerial);
            }
            buffer_.releaseRead();
            if (outcome == RenderFrameConsumeStatus::Stop)
                buffer_.requestStop();
            buffer_.signalFrameCompleted(completedSerial);
            if (outcome == RenderFrameConsumeStatus::Stop) {
                // Consumer requested stop after this frame. Ensure the double
                // buffer is in the stopped state so the producer's
                // waitFrameCompleted/acquireForWrite can also unblock.
                break;
            }
        }
    }

    SpscFrameDoubleBuffer<T>& buffer_;
    RenderThreadConsumer<T>& consumer_;
    std::chrono::milliseconds retryCadence_;
    std::thread thread_;
    std::atomic<bool> started_{false};
    std::mutex exitMutex_;
    std::condition_variable exitCondition_;
    bool exited_ = true;
};

} // namespace arp::core::runtime
