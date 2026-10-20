#pragma once
#include <cstdint>

namespace arp::rhi {
class BufferReadbackRangeContract final {
public:
    // Portable range: WebGPU mapAsync requires an eight-byte offset and a
    // four-byte size. Native backends preserve this same authored range.
    static constexpr std::uint64_t offsetAlignment = 8u, sizeAlignment = 4u;

    struct Range {
        std::uint64_t allocationBytes = 0u, offsetBytes = 0u, sizeBytes = 0u;

        constexpr const char* diagnostic() const noexcept
        {
            if (!allocationBytes || !sizeBytes || offsetBytes % offsetAlignment
                || sizeBytes % sizeAlignment)
                return "RHI-BUFFER-READBACK-MAP-RANGE-ALIGNMENT";
            return offsetBytes <= allocationBytes && sizeBytes <= allocationBytes - offsetBytes
                ? nullptr
                : "RHI-BUFFER-READBACK-MAP-RANGE-BOUNDS";
        }

        constexpr bool matches(const Range& other) const noexcept
        {
            return allocationBytes == other.allocationBytes && offsetBytes == other.offsetBytes
                && sizeBytes == other.sizeBytes;
        }
    };
};
} // namespace arp::rhi
