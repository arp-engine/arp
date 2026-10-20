// Contract tests for the shared swapchain frame-slot state machine in
// rhi/frontend/frame_slot_state_machine.h.
//
// This helper consolidates the double-buffer slot bookkeeping that was
// previously duplicated verbatim across metal_backend_swapchain.cpp,
// webgpu_backend_swapchain.cpp and null_swapchain.cpp. The tests pin the
// state-machine semantics so backend swapchains can rely on the helper's
// invariants without each re-asserting them.
//
// Status: Contract Complete.

#include "rhi/frontend/frame_slot_state_machine.h"

#include <cassert>
#include <cstdint>

namespace {

using StateMachine = arp::rhi::FrameSlotStateMachine<2>;

void assertFreshMachineHasNoInFlightSlotsAndZeroSerial()
{
    StateMachine sm;
    assert(!sm.anySlotInFlight());
    assert(sm.frameSerial() == 0);
    assert(sm.slotCount() == 2);
    // No slot is in flight on a fresh machine.
    assert(!sm.slotInFlight(0, 0));
    assert(!sm.slotInFlight(0, 1));
    assert(!sm.slotInFlight(1, 1));
}

void assertAcquireSlotAdvancesSerialAndMarksSlotInFlight()
{
    StateMachine sm;
    const auto first = sm.acquireSlot();
    assert(first.success);
    assert(first.slot == 0);
    assert(first.serial == 1);
    assert(sm.frameSerial() == 1);
    assert(sm.anySlotInFlight());
    assert(sm.slotInFlight(0, 1));
    // A second acquire rotates to the next free slot and stamps serial 2.
    const auto second = sm.acquireSlot();
    assert(second.success);
    assert(second.slot == 1);
    assert(second.serial == 2);
    assert(sm.frameSerial() == 2);
    assert(sm.slotInFlight(1, 2));
}

void assertAcquireFailsWhenAllSlotsInFlight()
{
    StateMachine sm;
    assert(sm.acquireSlot().success);
    assert(sm.acquireSlot().success);
    // Both double-buffer slots are now in flight; the third acquire must fail
    // without advancing the serial.
    const auto third = sm.acquireSlot();
    assert(!third.success);
    assert(third.slot == sm.slotCount());
    assert(third.serial == sm.frameSerial());
    assert(sm.frameSerial() == 2);
}

void assertClearSlotReleasesItForReuse()
{
    StateMachine sm;
    sm.acquireSlot();
    const auto second = sm.acquireSlot();
    assert(second.slot == 1);

    sm.clearSlot(0);
    assert(!sm.slotInFlight(0, 1));
    assert(sm.anySlotInFlight());
    assert(sm.slotInFlight(1, 2));

    // The cleared slot is reused on the next acquire (round-robin cursor points
    // past slot 1, wraps to slot 0).
    const auto reused = sm.acquireSlot();
    assert(reused.success);
    assert(reused.slot == 0);
    assert(reused.serial == 3);
    assert(sm.slotInFlight(0, 3));
}

void assertClearSlotIsBoundsCheckedAndIdempotent()
{
    StateMachine sm;
    sm.acquireSlot();
    // Out-of-range indices are ignored, not UB.
    sm.clearSlot(99);
    assert(sm.anySlotInFlight());
    // Clearing an already-free slot is a no-op.
    sm.clearSlot(1);
    sm.clearSlot(1);
    assert(sm.anySlotInFlight());
}

void assertClearAllSlotsResetsEverySlotAndCursor()
{
    StateMachine sm;
    sm.acquireSlot();
    sm.acquireSlot();
    assert(sm.anySlotInFlight());

    sm.clearAllSlots();
    assert(!sm.anySlotInFlight());
    assert(!sm.slotInFlight(0, 1));
    assert(!sm.slotInFlight(1, 2));

    // After clearAllSlots the round-robin cursor resets to 0, so the next
    // acquire lands on slot 0 (not wherever the cursor was before the clear).
    const auto next = sm.acquireSlot();
    assert(next.success);
    assert(next.slot == 0);
    // The serial counter is NOT reset by clearAllSlots (only by reset()).
    assert(next.serial == 3);
}

void assertSlotInFlightRejectsStaleSerial()
{
    StateMachine sm;
    const auto acquired = sm.acquireSlot();
    assert(sm.slotInFlight(acquired.slot, acquired.serial));
    // Wrong serial for an in-flight slot is rejected.
    assert(!sm.slotInFlight(acquired.slot, acquired.serial + 1));
    assert(!sm.slotInFlight(acquired.slot, 0));
    // Wrong slot index is rejected.
    assert(!sm.slotInFlight(acquired.slot + 1, acquired.serial));
    // Out-of-range slot is rejected without UB.
    assert(!sm.slotInFlight(99, acquired.serial));
}

void assertPresentedFrameRetainsItsPhysicalSlotButNotItsPublicHandle()
{
    StateMachine sm;
    const auto acquired = sm.acquireSlot();
    assert(sm.slotAcquired(acquired.slot, acquired.serial));
    assert(sm.markPresented(acquired.slot, acquired.serial));
    assert(sm.slotInFlight(acquired.slot, acquired.serial));
    assert(!sm.slotAcquired(acquired.slot, acquired.serial));
    assert(!sm.markPresented(acquired.slot, acquired.serial));

    const auto second = sm.acquireSlot();
    assert(second.success);
    assert(!sm.acquireSlot().success);

    sm.clearSlot(acquired.slot);
    assert(!sm.slotInFlight(acquired.slot, acquired.serial));
    const auto recycled = sm.acquireSlot();
    assert(recycled.success && recycled.slot == acquired.slot);
}

void assertMarkInFlightStampsExplicitSerialWithoutAdvancingCounter()
{
    StateMachine sm;
    // Backends that compute their own serial (e.g. Vulkan) can stamp it
    // directly. The internal counter is not advanced, so a subsequent
    // acquireSlot() will still produce the next sequential serial.
    sm.markInFlight(0, 77);
    assert(sm.slotInFlight(0, 77));
    assert(sm.frameSerial() == 0);

    const auto next = sm.acquireSlot();
    assert(next.success);
    assert(next.slot == 1);
    assert(next.serial == 1);
    assert(sm.frameSerial() == 1);
}

void assertResetClearsSlotsAndSerial()
{
    StateMachine sm;
    sm.acquireSlot();
    sm.acquireSlot();
    assert(sm.frameSerial() == 2);

    sm.reset();
    assert(!sm.anySlotInFlight());
    assert(sm.frameSerial() == 0);
    // After reset the first acquire yields serial 1 again.
    const auto first = sm.acquireSlot();
    assert(first.success);
    assert(first.serial == 1);
}

void assertRoundRobinCursorWrapsAround()
{
    StateMachine sm;
    // Acquire slot 0, clear it; cursor now points at 1. Acquire slot 1, clear
    // it; cursor wraps to 0. The next acquire should land on slot 0 again,
    // proving the round-robin cursor wraps rather than always scanning from 0.
    const auto a = sm.acquireSlot();
    assert(a.slot == 0);
    sm.clearSlot(0);
    const auto b = sm.acquireSlot();
    assert(b.slot == 1);
    sm.clearSlot(1);
    const auto c = sm.acquireSlot();
    assert(c.slot == 0);
}

void assertFourSlotVariantSupportsMoreThanDoubleBuffer()
{
    // The helper is templated on slot count; verify a 4-slot instantiation
    // behaves correctly (future triple/quadruple-buffer swapchains).
    arp::rhi::FrameSlotStateMachine<4> sm;
    assert(sm.slotCount() == 4);
    for (std::uint32_t i = 0; i < 4; ++i) {
        const auto acquired = sm.acquireSlot();
        assert(acquired.success);
        assert(acquired.slot == i);
        assert(acquired.serial == i + 1);
    }
    const auto fifth = sm.acquireSlot();
    assert(!fifth.success);
    assert(sm.anySlotInFlight());
    sm.clearSlot(2);
    assert(sm.slotInFlight(2, 3) == false);
    const auto reused = sm.acquireSlot();
    assert(reused.success);
    assert(reused.slot == 2);
}

} // namespace

int main()
{
    assertFreshMachineHasNoInFlightSlotsAndZeroSerial();
    assertAcquireSlotAdvancesSerialAndMarksSlotInFlight();
    assertAcquireFailsWhenAllSlotsInFlight();
    assertClearSlotReleasesItForReuse();
    assertClearSlotIsBoundsCheckedAndIdempotent();
    assertClearAllSlotsResetsEverySlotAndCursor();
    assertSlotInFlightRejectsStaleSerial();
    assertPresentedFrameRetainsItsPhysicalSlotButNotItsPublicHandle();
    assertMarkInFlightStampsExplicitSerialWithoutAdvancingCounter();
    assertResetClearsSlotsAndSerial();
    assertRoundRobinCursorWrapsAround();
    assertFourSlotVariantSupportsMoreThanDoubleBuffer();
    return 0;
}
