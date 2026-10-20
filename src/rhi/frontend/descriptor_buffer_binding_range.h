#pragma once
#include "core/unsigned_range.h"

#include <cstdint>

namespace arp::rhi {
// Version1 explicitly means the whole buffer; its size0 is symbolic metadata.
// Version2 is an authored finite slice, independent of declared access coverage.
struct DescriptorBufferBindingRange {
    std::uint32_t contractVersion = 1u;
    std::uint64_t offsetBytes = 0u, sizeBytes = 0u;

    constexpr bool matches(const DescriptorBufferBindingRange& other) const noexcept
    {
        return contractVersion == other.contractVersion && offsetBytes == other.offsetBytes
            && sizeBytes == other.sizeBytes;
    }
};

class DescriptorBufferBindingRangeContract final {
public:
    struct Resolved {
        std::uint64_t offsetBytes = 0u, sizeBytes = 0u;
    };

    static constexpr const char* validateSchema(
        const DescriptorBufferBindingRange& range, bool bufferDomain) noexcept
    {
        if (range.contractVersion == 1u)
            return range.offsetBytes || range.sizeBytes ? "RHI-DESCRIPTOR-BUFFER-WHOLE-NONCANONICAL"
                                                        : nullptr;
        if (range.contractVersion != 2u)
            return "RHI-DESCRIPTOR-BUFFER-RANGE-VERSION";
        if (!bufferDomain)
            return "RHI-DESCRIPTOR-BUFFER-RANGE-DOMAIN";
        return range.sizeBytes ? nullptr : "RHI-DESCRIPTOR-BUFFER-RANGE-EMPTY";
    }

    // Generic writes remain closed. Original finite write /1 uses its own
    // explicit final transaction; it does not bypass this generic gate.
    static constexpr const char* writeDiagnostic(
        const DescriptorBufferBindingRange& range, bool bufferDomain) noexcept
    {
        if (const auto* id = validateSchema(range, bufferDomain))
            return id;
        return range.contractVersion == 2u ? "RHI-DESCRIPTOR-BUFFER-SUBRANGE-LOWERING-MISSING"
                                           : nullptr;
    }

    static constexpr const char* resolve(const DescriptorBufferBindingRange& range,
        std::uint64_t bufferBytes,
        std::uint32_t dynamicOffset,
        Resolved& result) noexcept
    {
        result = {};
        if (const auto* id = validateSchema(range, true))
            return id;
        if (range.contractVersion == 1u) {
            if (dynamicOffset)
                return "RHI-DESCRIPTOR-WHOLE-BUFFER-DYNAMIC-RANGE-UNPROVEN";
            if (!bufferBytes)
                return "RHI-DESCRIPTOR-BUFFER-RANGE-BOUNDS";
            result = {0u, bufferBytes};
            return nullptr;
        }
        if (!core::containsUnsignedRange(
                std::uint64_t{0u}, bufferBytes, range.offsetBytes, range.sizeBytes))
            return "RHI-DESCRIPTOR-BUFFER-RANGE-BOUNDS";
        // Static containment proves subtraction safe. Tail containment proves
        // the final addition safe and keeps the authored size unchanged.
        if (!core::containsUnsignedRange(std::uint64_t{0u},
                bufferBytes - range.offsetBytes,
                std::uint64_t{dynamicOffset},
                range.sizeBytes))
            return "RHI-DESCRIPTOR-BUFFER-DYNAMIC-RANGE-BOUNDS";
        result = {range.offsetBytes + dynamicOffset, range.sizeBytes};
        return nullptr;
    }
};
} // namespace arp::rhi
