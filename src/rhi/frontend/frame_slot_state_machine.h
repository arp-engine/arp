#pragma once

// Shared L2 frame-slot state machine for RHI swapchains.
//
// The Metal, WebGPU and Null swapchain backends previously duplicated the same
// double-buffer frame-slot bookkeeping verbatim:
//   - per-slot in-flight flag + stamped frame serial
//   - round-robin free-slot search
//   - mark-in-flight on acquire, mark-presented before native submission,
//     clear-on-retirement, clear-all-on-resize/surface-loss
//   - logical-frame validity check (frameIndex in range + acquired + serial match)
//
// This header consolidates that state machine into a single header-only helper
// that each backend swapchain holds as a member. The helper owns ONLY the L2
// bookkeeping (booleans, serials, cursor); it does not touch native handles,
// drawables, textures or surfaces. Backends retain full ownership of native
// resource lifecycle and call into the helper for slot accounting only.
//
// Vulkan already has the same Free -> Acquired -> PresentedPendingRetirement
// state model because swapchain images cannot be re-acquired until the GPU has
// finished presenting them. Its native semaphore retirement remains private;
// this helper gives Metal, WebGPU and Null the same public-frame semantics.
//
// Layering: header-only, namespace arp::rhi, no native API dependency, no
// dynamic allocation (fixed std::array sized by template parameter).
//
// Status: Contract Complete.

#include <array>
#include <cstdint>

namespace arp::rhi {

// Result of an acquireSlot() attempt. When `success` is false, `slot` is set to
// `frameSlotCount` (one past the last valid index) and `serial` is unchanged
// (callers should stamp their failure result with the pre-acquire serial).
struct FrameSlotAcquireResult {
    bool success = false;
    std::uint32_t slot = 0;
    std::uint64_t serial = 0;
};

// Fixed-capacity double-buffer (or N-buffer) frame-slot state machine.
// `FrameSlotCount` is the number of logical in-flight slots; swapchains
// currently use 2 (double-buffer). The helper is trivially copyable and holds
// no owned resources, so it can be a plain member of a swapchain class.
template <std::uint32_t FrameSlotCount = 2> class FrameSlotStateMachine {
public:
    static constexpr std::uint32_t slotCount() noexcept { return FrameSlotCount; }

    // Returns the current acquire counter. The first successful acquire yields
    // serial 1 (the counter is pre-incremented). Backends use this to stamp
    // failure results that carry the "last issued" serial.
    std::uint64_t frameSerial() const noexcept { return frameSerial_; }

    // True if any slot is currently in flight (acquired but not yet cleared by
    // present / retire). Used by backends to compute `nativeFrameAcquired`-style
    // derived state.
    bool anySlotInFlight() const noexcept
    {
        for (std::uint32_t slot = 0; slot < FrameSlotCount; ++slot) {
            if (frameSlotsInFlight_[slot]) {
                return true;
            }
        }
        return false;
    }

    // Attempts to find a free slot, mark it in-flight, stamp and advance the
    // serial. Returns {false, slotCount, currentSerial} when no slot is free.
    // The round-robin cursor starts at the slot after the last acquired one,
    // matching the previous per-backend findFreeFrameSlot + markFrameSlotInFlight
    // pair exactly.
    FrameSlotAcquireResult acquireSlot() noexcept
    {
        const auto slot = findFreeFrameSlot();
        if (slot >= FrameSlotCount) {
            return {false, slot, frameSerial_};
        }
        const auto serial = ++frameSerial_;
        markInFlight(slot, serial);
        return {true, slot, serial};
    }

    // Marks a slot in-flight and logically acquired with an explicit serial, without advancing the
    // internal counter. Used by backends that compute the serial themselves
    // (e.g. Vulkan stamps the serial before calling native acquire). For
    // Metal/WebGPU/Null the acquireSlot() helper above is preferred.
    void markInFlight(std::uint32_t slot, std::uint64_t serial) noexcept
    {
        if (slot >= FrameSlotCount) {
            return;
        }
        frameSlotsInFlight_[slot] = true;
        frameSlotsPresented_[slot] = false;
        frameSlotSerials_[slot] = serial;
        nextFrameSlot_ = (slot + 1u) % FrameSlotCount;
    }

    // Clears a single slot back to free. Idempotent and bounds-checked.
    void clearSlot(std::uint32_t slot) noexcept
    {
        if (slot >= FrameSlotCount) {
            return;
        }
        frameSlotsInFlight_[slot] = false;
        frameSlotsPresented_[slot] = false;
        frameSlotSerials_[slot] = 0;
    }

    // Clears every slot and resets the round-robin cursor. Called on resize and
    // surface-loss so a stalled slot cannot leak into the next surface session.
    void clearAllSlots() noexcept
    {
        for (std::uint32_t slot = 0; slot < FrameSlotCount; ++slot) {
            frameSlotsInFlight_[slot] = false;
            frameSlotsPresented_[slot] = false;
            frameSlotSerials_[slot] = 0;
        }
        nextFrameSlot_ = 0;
    }

    // Validates that a frame reference refers to a live in-flight slot with a
    // matching serial. Backends use this to reject stale/duplicate presents.
    bool slotInFlight(std::uint32_t slot, std::uint64_t serial) const noexcept
    {
        return slot < FrameSlotCount && frameSlotsInFlight_[slot]
            && frameSlotSerials_[slot] == serial;
    }

    // Transitions an acquired slot to presented. The physical slot remains in
    // flight until its backend-owned completion retires it, but the public
    // frame handle becomes unusable immediately: a frame may not be encoded,
    // discarded, or presented twice after presentation has been accepted.
    bool markPresented(std::uint32_t slot, std::uint64_t serial) noexcept
    {
        if (!slotInFlight(slot, serial) || frameSlotsPresented_[slot]) {
            return false;
        }
        frameSlotsPresented_[slot] = true;
        return true;
    }

    // True only while the public frame handle is still owned by the caller.
    // This intentionally differs from slotInFlight(): presented frames retain
    // their physical slot until native presentation/render completion.
    bool slotAcquired(std::uint32_t slot, std::uint64_t serial) const noexcept
    {
        return slotInFlight(slot, serial) && !frameSlotsPresented_[slot];
    }

    // Resets the acquire counter to zero. Intended for tests and full teardown;
    // production swapchains should not need to call this (the serial is
    // monotonic across the swapchain's lifetime).
    void reset() noexcept
    {
        clearAllSlots();
        frameSerial_ = 0;
    }

private:
    std::uint32_t findFreeFrameSlot() const noexcept
    {
        for (std::uint32_t offset = 0; offset < FrameSlotCount; ++offset) {
            const auto slot = (nextFrameSlot_ + offset) % FrameSlotCount;
            if (!frameSlotsInFlight_[slot]) {
                return slot;
            }
        }
        return FrameSlotCount;
    }

    std::array<bool, FrameSlotCount> frameSlotsInFlight_{};
    std::array<bool, FrameSlotCount> frameSlotsPresented_{};
    std::array<std::uint64_t, FrameSlotCount> frameSlotSerials_{};
    std::uint32_t nextFrameSlot_ = 0;
    std::uint64_t frameSerial_ = 0;
};

} // namespace arp::rhi
