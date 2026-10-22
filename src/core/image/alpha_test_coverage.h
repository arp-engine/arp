#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace arp::core {

// Select the closest representable alpha-test coverage after resampling RGBA8.
// Only alpha changes; zero alpha remains zero. Ties prefer the smallest change
// in threshold. Histogram quantization can prevent exact coverage equality.
inline bool preserveAlphaTestCoverage(const std::uint8_t* reference,
    std::size_t referenceBytes,
    std::uint8_t* destination,
    std::size_t destinationBytes,
    float cutoff)
{
    if (!reference || !destination || !referenceBytes || !destinationBytes || referenceBytes % 4u
        || destinationBytes % 4u || !std::isfinite(cutoff) || cutoff < 0.f || cutoff > 1.f
        || referenceBytes / 4u > UINT32_MAX || destinationBytes / 4u > UINT32_MAX)
        return false;
    if (cutoff == 0.f)
        return true;
    const auto threshold = static_cast<unsigned>(std::ceil(cutoff * 255.f));
    const auto referenceCount = referenceBytes / 4u;
    const auto count = destinationBytes / 4u;
    std::uint64_t referenceCovered = 0;
    std::array<std::uint64_t, 256> histogram{};
    for (std::size_t i = 3; i < referenceBytes; i += 4)
        referenceCovered += reference[i] >= threshold;
    for (std::size_t i = 3; i < destinationBytes; i += 4)
        ++histogram[destination[i]];
    auto bestError = std::numeric_limits<std::uint64_t>::max();
    unsigned bestThreshold = threshold;
    unsigned bestDistance = 256;
    std::uint64_t covered = 0;
    for (unsigned candidate = 255; candidate > 0; --candidate) {
        covered += histogram[candidate];
        const auto actual = covered * referenceCount;
        const auto target = referenceCovered * count;
        const auto error = actual > target ? actual - target : target - actual;
        const auto distance = candidate > threshold ? candidate - threshold : threshold - candidate;
        if (error < bestError || (error == bestError && distance < bestDistance)) {
            bestError = error;
            bestThreshold = candidate;
            bestDistance = distance;
        }
    }
    // Explicitly represent a zero-coverage destination when needed.
    const auto emptyError = referenceCovered * count;
    if (emptyError < bestError) {
        for (std::size_t i = 3; i < destinationBytes; i += 4)
            destination[i] = 0;
        return true;
    }
    for (std::size_t i = 3; i < destinationBytes; i += 4) {
        // Flooring ensures values below the selected threshold stay below the
        // material cutoff, including adjacent alpha bins at small cutoffs.
        destination[i] = static_cast<std::uint8_t>(
            std::min(255u, (static_cast<unsigned>(destination[i]) * threshold) / bestThreshold));
    }
    return true;
}

} // namespace arp::core
