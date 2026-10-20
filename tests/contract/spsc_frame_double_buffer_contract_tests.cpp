// Contract test for SpscFrameDoubleBuffer<T> (Phase 2 of
// docs/render-thread-double-buffer-plan.md).
//
// Guards the SPSC frame double buffer's state-machine invariants and the
// frame-completion fence so the render-thread / logic-thread split can rely on
// them. Uses `int` as the submission type T so the test stays in the core layer
// (does not depend on WoCC types). The WoCC instantiation
// (WoccRenderFrameSubmission) is exercised end-to-end by the desktop smoke run
// (arp_world_of_claudecraft --hidden --frames N).
//
// Invariants under test:
//   1. Slot state machine: Writing -> Ready -> Reading -> Consumed -> Writing.
//   2. Back-pressure: acquireForWrite blocks until a slot is Consumed (tested
//      by filling both slots and observing that a third acquireForWrite does
//      not return until the consumer releases one).
//   3. acquireForRead returns nullptr once requestStop is called and no Ready
//      slot remains (clean shutdown drains pending work first).
//   4. Frame-completion fence: signalFrameCompleted(serial) unblocks a
//      producer blocked in waitFrameCompleted(targetSerial) once serial >=
//      target.
//   5. Single-consumer data integrity: the value the producer writes into a
//      slot is the value the consumer reads from it (no cross-slot aliasing).
//   6. stopRequested() unblocks waitFrameCompleted (no deadlock on shutdown).

#include "core/runtime/spsc_frame_double_buffer.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace {

using arp::core::runtime::SpscFrameDoubleBuffer;

void test_basic_publish_consume_roundtrip()
{
    // Single producer + single consumer thread. Producer writes 3 frames,
    // consumer reads them in order. Verifies value integrity (no aliasing).
    SpscFrameDoubleBuffer<int> buffer;
    std::vector<int> seen;
    std::thread consumer([&] {
        for (int i = 0; i < 3; ++i) {
            const int* p = buffer.acquireForRead();
            assert(p != nullptr);
            seen.push_back(*p);
            buffer.releaseRead();
            buffer.signalFrameCompleted(static_cast<std::uint64_t>(i + 1));
        }
    });

    for (int i = 0; i < 3; ++i) {
        int& slot = buffer.acquireForWrite();
        slot = 100 + i;
        buffer.publishWrite();
        // Wait for the consumer to finish this frame before overwriting any
        // state the consumer might still be reading (steady-state fence use).
        buffer.waitFrameCompleted(static_cast<std::uint64_t>(i + 1));
    }
    consumer.join();

    assert(seen.size() == 3u);
    assert(seen[0] == 100);
    assert(seen[1] == 101);
    assert(seen[2] == 102);
}

void test_backpressure_blocks_producer()
{
    // With 2 slots, the producer can publish at most 2 frames ahead of the
    // consumer before acquireForWrite must block. We verify back-pressure by
    // publishing 2 frames (filling both slots), then attempting a 3rd
    // acquireForWrite on a separate thread that should NOT complete until the
    // consumer releases a slot.
    SpscFrameDoubleBuffer<int> buffer;
    // Publish frame 1 + 2 (both slots become Ready).
    buffer.acquireForWrite() = 1;
    buffer.publishWrite();
    buffer.acquireForWrite() = 2;
    buffer.publishWrite();

    std::atomic<bool> thirdAcquireCompleted{false};
    std::thread producer([&] {
        // This must block until the consumer releases a slot.
        buffer.acquireForWrite() = 3;
        thirdAcquireCompleted.store(true, std::memory_order_release);
    });

    // Give the producer thread time to (try to) acquire. It should still be
    // blocked because no slot is Consumed yet.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!thirdAcquireCompleted.load(std::memory_order_acquire));

    // Consumer drains one frame -> one slot becomes Consumed -> producer
    // unblocks.
    const int* p = buffer.acquireForRead();
    assert(p != nullptr && *p == 1);
    buffer.releaseRead();
    buffer.signalFrameCompleted(1);

    // Producer should now complete.
    producer.join();
    assert(thirdAcquireCompleted.load(std::memory_order_acquire));
}

void test_stop_returns_nullptr_after_drain()
{
    // requestStop must let the consumer drain pending Ready slots, then return
    // nullptr from acquireForRead. A pending frame must NOT be lost.
    SpscFrameDoubleBuffer<int> buffer;
    buffer.acquireForWrite() = 42;
    buffer.publishWrite();

    buffer.requestStop();

    // Consumer: the pending Ready slot is still drained.
    const int* p = buffer.acquireForRead();
    assert(p != nullptr);
    assert(*p == 42);
    buffer.releaseRead();

    // Next acquireForRead returns nullptr (stop + no more Ready).
    const int* p2 = buffer.acquireForRead();
    assert(p2 == nullptr);
}

void test_stop_with_no_pending_work_returns_nullptr_immediately()
{
    // requestStop with nothing Ready: acquireForRead returns nullptr right
    // away (clean shutdown with an idle consumer).
    SpscFrameDoubleBuffer<int> buffer;
    buffer.requestStop();
    const int* p = buffer.acquireForRead();
    assert(p == nullptr);
}

void test_wait_frame_completed_unblocks_on_stop()
{
    // Producer blocked in waitFrameCompleted(target) where target will never be
    // signalled. requestStop must unblock it so the producer can observe
    // stopRequested and exit (no deadlock on shutdown).
    SpscFrameDoubleBuffer<int> buffer;
    std::atomic<bool> waitReturned{false};
    std::thread producer([&] {
        buffer.waitFrameCompleted(9999); // never signalled
        waitReturned.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!waitReturned.load(std::memory_order_acquire));

    buffer.requestStop();
    producer.join();
    assert(waitReturned.load(std::memory_order_acquire));
    assert(buffer.stopRequested());
}

void test_default_completed_serial_is_zero()
{
    // Fresh buffer reports completedFrameSerial == 0 (no frames completed yet).
    SpscFrameDoubleBuffer<int> buffer;
    assert(buffer.completedFrameSerial() == 0u);
    assert(!buffer.stopRequested());
}

void test_steady_state_overlap_no_data_loss()
{
    // Steady-state stress: producer and consumer run concurrently for many
    // frames with one frame of overlap (producer waits for frame N-1 to
    // complete before reading back, but publishWrite(N) happens before
    // waitFrameCompleted(N) so consumer executes N while producer assembles
    // N+1). Verifies no data loss / no aliasing over a longer run.
    constexpr std::uint32_t kFrames = 200;
    SpscFrameDoubleBuffer<int> buffer;

    std::vector<int> seen;
    seen.reserve(kFrames);
    std::thread consumer([&] {
        for (std::uint32_t i = 0; i < kFrames; ++i) {
            const int* p = buffer.acquireForRead();
            if (p == nullptr)
                break;
            seen.push_back(*p);
            buffer.releaseRead();
            buffer.signalFrameCompleted(static_cast<std::uint64_t>(i + 1));
        }
    });

    for (std::uint32_t i = 0; i < kFrames; ++i) {
        int& slot = buffer.acquireForWrite();
        slot = static_cast<int>(i);
        buffer.publishWrite();
        // Steady-state fence: wait for the previous frame to complete before
        // reading back any state. This is the exact pattern stepFrame uses.
        if (i > 0) {
            buffer.waitFrameCompleted(static_cast<std::uint64_t>(i));
        }
    }
    // Wait for the final frame.
    buffer.waitFrameCompleted(static_cast<std::uint64_t>(kFrames));
    consumer.join();

    assert(seen.size() == kFrames);
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        assert(seen[i] == static_cast<int>(i));
    }
}

} // namespace

int main()
{
    test_default_completed_serial_is_zero();
    test_basic_publish_consume_roundtrip();
    test_backpressure_blocks_producer();
    test_stop_returns_nullptr_after_drain();
    test_stop_with_no_pending_work_returns_nullptr_immediately();
    test_wait_frame_completed_unblocks_on_stop();
    test_steady_state_overlap_no_data_loss();
    return 0;
}
