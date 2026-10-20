#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace arp::core::runtime {

// A retirement stamped S may be reaped only below both CPU consumption and
// every pending GPU submission's serial. Slot reuse requires the old fence
// complete, so newer work never prevents reclaiming older generations.
template <std::size_t SlotCount> class GpuRetirementSerialGate final {
public:
    void submitted(std::size_t slot, std::uint64_t serial) noexcept { serials_.at(slot) = serial; }

    template <class Ready> std::uint64_t safeBefore(std::uint64_t cpuCompleted, Ready&& ready) const
    {
        for (std::size_t slot = 0; slot < SlotCount; ++slot)
            if (!ready(slot))
                cpuCompleted = std::min(cpuCompleted, serials_[slot]);
        return cpuCompleted;
    }

private:
    std::array<std::uint64_t, SlotCount> serials_{};
};
} // namespace arp::core::runtime
