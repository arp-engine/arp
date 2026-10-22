#include "core/hash/deterministic_hash.h"

#include <cassert>
#include <cmath>

namespace {

// Golden values computed from the TypeScript reference (rng.ts:45 hash2).
// These lock the C++ port to the exact same bit stream so scatter results
// match the reference world-of-claudecraft project for a given seed.
void hash2_matches_reference_values()
{
    // hash2(0, 0, 1)
    assert(std::fabs(arp::core::hash2(0u, 0u, 1u) - 0.557727f) < 1e-5f);
    // hash2(10, 20, 1)
    assert(std::fabs(arp::core::hash2(10u, 20u, 1u) - 0.098176f) < 1e-5f);
    // hash2(-5, 7, 42) — signed overload
    assert(std::fabs(arp::core::hash2(-5, 7, 42u) - 0.697351f) < 1e-5f);
}

void hash2_is_deterministic()
{
    const float a = arp::core::hash2(123u, 456u, 789u);
    const float b = arp::core::hash2(123u, 456u, 789u);
    assert(a == b);
}

void hash2_in_unit_range()
{
    for (int y = -50; y < 50; ++y) {
        for (int x = -50; x < 50; ++x) {
            const float h = arp::core::hash2(x, y, 1u);
            assert(h >= 0.0f && h < 1.0f);
        }
    }
}

void hash2_different_seeds_differ()
{
    // Different seeds should (almost always) produce different values.
    const float a = arp::core::hash2(7u, 13u, 1u);
    const float b = arp::core::hash2(7u, 13u, 2u);
    assert(a != b);
}

void hash2_different_coords_differ()
{
    // Same seed, different coordinates should (almost always) differ.
    const float a = arp::core::hash2(7u, 13u, 1u);
    const float b = arp::core::hash2(8u, 13u, 1u);
    const float c = arp::core::hash2(7u, 14u, 1u);
    assert(a != b);
    assert(a != c);
}

} // namespace

int main()
{
    hash2_matches_reference_values();
    hash2_is_deterministic();
    hash2_in_unit_range();
    hash2_different_seeds_differ();
    hash2_different_coords_differ();
    return 0;
}
