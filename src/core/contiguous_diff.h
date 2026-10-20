#pragma once

#include "core/cpu_allocator.h"

#include <cstddef>
#include <cstring>
#include <type_traits>

namespace arp::core {

struct ContiguousDiffRange {
    std::size_t firstElement = 0u;
    std::size_t elementCount = 0u;
};

struct ContiguousDiffSummary {
    std::size_t changedElementCount = 0u;
    bool fullRangeFallback = false;
    bool coalescedRanges = false;
};

// Produces exact contiguous changed ranges, then deterministically merges the
// pair separated by the smallest unchanged gap until maxRangeCount is met.
template <typename T>
ContiguousDiffSummary buildContiguousDiffRanges(const T* current,
    std::size_t currentCount,
    const T* previous,
    std::size_t previousCount,
    std::size_t maxRangeCount,
    pmr::Vector<ContiguousDiffRange>& ranges)
{
    static_assert(
        std::is_trivially_copyable_v<T>, "Contiguous diff requires byte-comparable elements");
    ranges.clear();
    if (current == nullptr || currentCount == 0u) {
        return {};
    }
    if (previous == nullptr || previousCount != currentCount) {
        ranges.push_back({0u, currentCount});
        return {currentCount, true};
    }

    ContiguousDiffSummary summary;
    std::size_t runStart = currentCount;
    for (std::size_t i = 0u; i < currentCount; ++i) {
        const bool changed = std::memcmp(current + i, previous + i, sizeof(T)) != 0;
        if (changed) {
            ++summary.changedElementCount;
            if (runStart == currentCount) {
                runStart = i;
            }
        }
        if ((!changed || i + 1u == currentCount) && runStart != currentCount) {
            const auto runEnd = changed && i + 1u == currentCount ? i + 1u : i;
            ranges.push_back({runStart, runEnd - runStart});
            runStart = currentCount;
        }
    }

    if (!ranges.empty() && maxRangeCount == 0u) {
        ranges.clear();
        ranges.push_back({0u, currentCount});
        summary.fullRangeFallback = true;
        return summary;
    }
    while (ranges.size() > maxRangeCount) {
        std::size_t mergeIndex = 0u;
        std::size_t smallestGap = currentCount;
        for (std::size_t i = 0u; i + 1u < ranges.size(); ++i) {
            const auto rangeEnd = ranges[i].firstElement + ranges[i].elementCount;
            const auto gap = ranges[i + 1u].firstElement - rangeEnd;
            if (gap < smallestGap) {
                smallestGap = gap;
                mergeIndex = i;
            }
        }
        const auto mergedEnd =
            ranges[mergeIndex + 1u].firstElement + ranges[mergeIndex + 1u].elementCount;
        ranges[mergeIndex].elementCount = mergedEnd - ranges[mergeIndex].firstElement;
        ranges.erase(ranges.begin() + static_cast<std::ptrdiff_t>(mergeIndex + 1u));
        summary.coalescedRanges = true;
    }
    return summary;
}

} // namespace arp::core
