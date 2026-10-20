// Contract test for RenderThread<T> + RenderThreadConsumer<T> (Phase 2 of
// docs/render-thread-double-buffer-plan.md).
//
// Guards the dedicated render thread's lifecycle and the consumer contract:
//   1. start() spawns a thread that consumes frames until stop() is called.
//   2. stop() joins the thread cleanly (no dangling thread, no std::terminate).
//   3. consumeFrame returning false requests the thread to stop after the
//      current frame (drains + exits).
//   4. The render thread signals frame-completion for each consumed frame so
//      the producer's waitFrameCompleted unblocks.
//   5. stop() with no pending work returns promptly (clean shutdown of an
//      idle render thread).
//
// Uses a test consumer that counts consumed frames and can be configured to
// return false after N frames (simulating a close-requested / fatal error).
// Stays in the core layer (depends only on arp_core) -- does not pull in WoCC.

#include "core/runtime/render_thread.h"
#include "core/runtime/spsc_frame_double_buffer.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {

using arp::core::runtime::RenderFrameConsumeStatus;
using arp::core::runtime::RenderThread;
using arp::core::runtime::RenderThreadConsumer;
using arp::core::runtime::SpscFrameDoubleBuffer;

// Test consumer: counts consumed frames and optionally stops after a fixed
// count (simulating a close-requested / fatal-error path). Reports the
// submission value (an int) as the completed serial so the producer's
// waitFrameCompleted can target a specific frame.
struct CountingConsumer : public RenderThreadConsumer<int> {
    std::atomic<std::uint32_t> consumedCount{0};
    std::atomic<std::uint64_t> lastSerial{0};
    // If non-zero, the consumer returns false (request stop) after this many
    // frames have been consumed.
    std::uint32_t stopAfterCount = 0;

    RenderFrameConsumeStatus consumeFrame(
        const int& submission, std::uint64_t* outCompletedSerial) override
    {
        const std::uint32_t n = consumedCount.fetch_add(1, std::memory_order_acq_rel) + 1u;
        const std::uint64_t serial = static_cast<std::uint64_t>(submission);
        lastSerial.store(serial, std::memory_order_release);
        if (outCompletedSerial != nullptr) {
            *outCompletedSerial = serial;
        }
        if (stopAfterCount != 0u && n >= stopAfterCount) {
            return RenderFrameConsumeStatus::Stop;
        }
        return RenderFrameConsumeStatus::Completed;
    }
};

void test_start_stop_clean_join_idle()
{
    // start -> stop with no pending work: the render thread is blocked in
    // acquireForRead; requestStop unblocks it (returns nullptr) and the thread
    // exits. stop() joins. No dangling thread, no std::terminate.
    SpscFrameDoubleBuffer<int> buffer;
    CountingConsumer consumer;
    RenderThread<int> rt(buffer, consumer);

    rt.start();
    // No work published; stop must still join cleanly.
    rt.stop();

    assert(consumer.consumedCount.load() == 0u);
}

void test_consume_frames_then_stop()
{
    // Publish 5 frames, then stop. The render thread drains all 5 (requestStop
    // lets pending Ready slots drain before acquireForRead returns nullptr).
    SpscFrameDoubleBuffer<int> buffer;
    CountingConsumer consumer;
    RenderThread<int> rt(buffer, consumer);
    rt.start();

    for (int i = 1; i <= 5; ++i) {
        buffer.acquireForWrite() = i;
        buffer.publishWrite();
    }
    // Wait for the 5th frame to complete (proves the render thread consumed
    // all of them).
    buffer.waitFrameCompleted(5);

    rt.stop();
    assert(consumer.consumedCount.load() == 5u);
    assert(consumer.lastSerial.load() == 5u);
}

void test_consumer_returning_false_stops_thread()
{
    // Consumer returns false after 3 frames (simulating close-requested). The
    // render thread must: (a) finish consuming the 3rd frame, (b) release the
    // slot, (c) signal frame-completion for it, (d) request stop + exit. The
    // producer must observe stopRequested so its next waitFrameCompleted /
    // acquireForWrite can detect the dead consumer.
    SpscFrameDoubleBuffer<int> buffer;
    CountingConsumer consumer;
    consumer.stopAfterCount = 3;
    RenderThread<int> rt(buffer, consumer);
    rt.start();

    for (int i = 1; i <= 3; ++i) {
        buffer.acquireForWrite() = i;
        buffer.publishWrite();
    }
    // The 3rd frame's completion must be signalled even though the consumer
    // returned false (RenderThread::run releases + signals before exit).
    buffer.waitFrameCompleted(3);

    // The render thread has now exited (or is about to). stopRequested must be
    // observable. stop() must still be safe to call (idempotent join).
    assert(buffer.stopRequested());
    rt.stop();
    assert(consumer.consumedCount.load() == 3u);
}

void test_destructor_calls_stop_as_safety_net()
{
    // If the caller forgets stop(), the RenderThread destructor must call stop
    // to avoid std::terminate on a joinable thread. We verify by constructing +
    // destroying without an explicit stop().
    SpscFrameDoubleBuffer<int> buffer;
    CountingConsumer consumer;
    {
        RenderThread<int> rt(buffer, consumer);
        rt.start();
        // Publish one frame so the thread has something to consume.
        buffer.acquireForWrite() = 7;
        buffer.publishWrite();
        // Destructor runs here; must join without std::terminate.
    }
    // If we reach here, the destructor's safety-net stop() worked.
    assert(consumer.consumedCount.load() >= 1u);
}

struct BlockingConsumer final : RenderThreadConsumer<int> {
    void awaitEntry()
    {
        std::unique_lock lock(mutex);
        condition.wait(lock, [this] {
            return entered;
        });
    }

    void release()
    {
        {
            std::lock_guard lock(mutex);
            released = true;
        }
        condition.notify_all();
    }

    RenderFrameConsumeStatus consumeFrame(const int&, std::uint64_t* serial) override
    {
        {
            std::lock_guard lock(mutex);
            entered = true;
        }
        condition.notify_all();
        std::unique_lock lock(mutex);
        condition.wait(lock, [this] {
            return released;
        });
        *serial = 1u;
        return RenderFrameConsumeStatus::Completed;
    }

    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool released = false;
};

void test_stop_until_reports_unresponsive_consumer_without_forcing_join()
{
    SpscFrameDoubleBuffer<int> buffer;
    BlockingConsumer consumer;
    RenderThread<int> thread(buffer, consumer);
    buffer.acquireForWrite() = 1;
    buffer.publishWrite();
    thread.start();
    consumer.awaitEntry();

    const auto expired =
        thread.stopUntil(std::chrono::steady_clock::now() + std::chrono::milliseconds{10});
    assert(expired.state == arp::core::runtime::RenderThreadStopState::DeadlineExpired);
    assert(!expired.completed());
    assert(expired.remainingNanoseconds == 0u);

    consumer.release();
    const auto completed =
        thread.stopUntil(std::chrono::steady_clock::now() + std::chrono::seconds{1});
    assert(completed.completed());
    assert(completed.remainingNanoseconds > 0u);
}

void test_double_start_is_noop()
{
    // start() must be idempotent: calling it twice must not spawn two threads
    // (the second call is a no-op). We verify by starting twice and observing
    // exactly one consumer thread's worth of work.
    SpscFrameDoubleBuffer<int> buffer;
    CountingConsumer consumer;
    RenderThread<int> rt(buffer, consumer);
    rt.start();
    rt.start(); // no-op
    rt.stop();
    assert(consumer.consumedCount.load() == 0u);
}

struct DeferredConsumer final : RenderThreadConsumer<int> {
    std::mutex mutex;
    std::condition_variable cv;
    bool ready = false;
    unsigned attempts = 0u;
    unsigned completed = 0u;
    const int* firstAddress = nullptr;

    RenderFrameConsumeStatus consumeFrame(const int& submission, std::uint64_t* serial) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++attempts;
        if (completed == 0u) {
            if (!firstAddress)
                firstAddress = &submission;
            assert(firstAddress == &submission && submission == 11);
            if (!ready) {
                // Even a stale serial written by a consumer must be ignored
                // while the packet remains Deferred.
                *serial = 999u;
                cv.notify_all();
                return RenderFrameConsumeStatus::Deferred;
            }
        } else
            assert(completed == 1u && submission == 12);
        ++completed;
        *serial = static_cast<std::uint64_t>(submission);
        cv.notify_all();
        return RenderFrameConsumeStatus::Completed;
    }

    void awaitAttempt()
    {
        std::unique_lock<std::mutex> lock(mutex);
        const bool observed = cv.wait_for(lock, std::chrono::seconds{5}, [this] {
            return attempts != 0u;
        });
        assert(observed);
    }
};

void test_deferred_retains_packet_and_fifo()
{
    SpscFrameDoubleBuffer<int> buffer;
    DeferredConsumer consumer;
    RenderThread<int> rt(buffer, consumer);
    buffer.acquireForWrite() = 11;
    buffer.publishWrite();
    rt.start();
    consumer.awaitAttempt();
    assert(buffer.completedFrameSerial() == 0u);
    auto& second = buffer.acquireForWrite();
    {
        std::lock_guard<std::mutex> lock(consumer.mutex);
        assert(&second != consumer.firstAddress && consumer.completed == 0u);
    }
    second = 12;
    buffer.publishWrite();
    {
        std::lock_guard<std::mutex> lock(consumer.mutex);
        assert(consumer.completed == 0u && buffer.completedFrameSerial() == 0u);
        consumer.ready = true;
    }
    buffer.waitFrameCompleted(12u);
    rt.stop();
    assert(consumer.completed == 2u && consumer.attempts >= 3u);
    assert(buffer.completedFrameSerial() == 12u);
}

void test_stop_cancels_deferred_without_completion()
{
    SpscFrameDoubleBuffer<int> buffer;
    DeferredConsumer consumer;
    // Long cadence proves stop wakes the scheduler rather than waiting for
    // the next timed retry. It is test policy, not a GPU completion timeout.
    RenderThread<int> rt(buffer, consumer, std::chrono::seconds{30});
    buffer.acquireForWrite() = 11;
    buffer.publishWrite();
    rt.start();
    consumer.awaitAttempt();
    const auto start = std::chrono::steady_clock::now();
    rt.stop();
    assert(std::chrono::steady_clock::now() - start < std::chrono::seconds{5});
    assert(consumer.completed == 0u && buffer.completedFrameSerial() == 0u);
    assert(buffer.stopRequested());
}

void test_invalid_retry_cadence_rejected()
{
    SpscFrameDoubleBuffer<int> buffer;
    CountingConsumer consumer;
    for (const auto cadence : {std::chrono::milliseconds{0}, std::chrono::milliseconds{-1}}) {
        bool rejected = false;
        try {
            RenderThread<int> rt(buffer, consumer, cadence);
        } catch (const arp::core::DiagnosticError& error) {
            rejected = std::string_view(error.what()) == "CORE-RENDER-RETRY-CADENCE-INVALID";
        }
        assert(rejected);
    }
}

} // namespace

int main()
{
    test_start_stop_clean_join_idle();
    test_consume_frames_then_stop();
    test_consumer_returning_false_stops_thread();
    test_destructor_calls_stop_as_safety_net();
    test_stop_until_reports_unresponsive_consumer_without_forcing_join();
    test_double_start_is_noop();
    test_deferred_retains_packet_and_fifo();
    test_stop_cancels_deferred_without_completion();
    test_invalid_retry_cadence_rejected();
    return 0;
}
