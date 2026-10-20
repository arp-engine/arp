#pragma once

// Deterministic integer hash — ported from world-of-claudecraft/src/sim/rng.ts.
//
// This is the canonical 2D → [0,1) hash used by the reference project for all
// scatter placement (decorations, dressing, instance jitter). It MUST be
// bit-for-bit identical to the TypeScript implementation so that scatter
// results match the reference when given the same seed.
//
// The algorithm uses 32-bit unsigned integer multiply-with-xor; C++ uint32_t
// arithmetic wraps modulo 2^32 exactly like JavaScript's Math.imul.
//
// rng.ts:45-53 hash2(x, y, seed):
//   let h = seed >>> 0;
//   h = Math.imul(h ^ (x * 374761393), 668265263);
//   h = Math.imul(h ^ (y * 1274126177), 461845907);
//   h ^= h >>> 13;
//   h = Math.imul(h, 1274126177);
//   h ^= h >>> 16;
//   return (h >>> 0) / 4294967296;

#include <cmath>
#include <cstdint>

namespace arp::core {

/// 32-bit multiply that wraps modulo 2^32, matching JavaScript Math.imul.
/// (Standard C++ unsigned multiplication already wraps, so this is a plain
/// multiply — but we name it to make the contract explicit.)
inline std::uint32_t imul32(std::uint32_t a, std::uint32_t b) noexcept
{
    return a * b;
}

/// hash2 — deterministic 2D integer hash to [0, 1).
///
/// Ported from world-of-claudecraft/src/sim/rng.ts:45. The same (x, y, seed)
/// always yields the same value on every platform (32-bit unsigned arithmetic
/// is well-defined and wraps modulo 2^32).
inline float hash2(std::uint32_t x, std::uint32_t y, std::uint32_t seed) noexcept
{
    std::uint32_t h = seed;
    h = imul32(h ^ imul32(x, 374761393u), 668265263u);
    h = imul32(h ^ imul32(y, 1274126177u), 461845907u);
    h ^= h >> 13;
    h = imul32(h, 1274126177u);
    h ^= h >> 16;
    return static_cast<float>(h) / 4294967296.0f;
}

/// Convenience overload for signed coordinates (common in scatter grids).
/// The conversion to unsigned is well-defined in C++ (mod 2^32).
inline float hash2(int x, int y, std::uint32_t seed) noexcept
{
    return hash2(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), seed);
}

// ---------------------------------------------------------------------------
// Stateless value-noise / fractal-noise primitives.
//
// Ported from world-of-claudecraft/src/sim/rng.ts (noise2 / fbm2). These are
// layered on top of hash2() so they inherit its cross-platform bit-for-bit
// determinism. They are the terrain-shaping building blocks consumed by
// TerrainHeightField (see core/terrain_heightfield.h) and any procedural
// generator that must match the reference world.
// ---------------------------------------------------------------------------

/// Smoothstep — C2-continuous Hermite interpolation in [0,1].
inline float smooth(float t) noexcept
{
    return t * t * (3.0f - 2.0f * t);
}

/// Value noise in [0,1]. Bilinearly interpolates hash2() at the four integer
/// corners of the cell containing (x, y), using smoothstep on the fractional
/// part. Ported verbatim from rng.ts:60 noise2().
inline float noise2(float x, float y, std::uint32_t seed) noexcept
{
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const float xf = x - static_cast<float>(xi);
    const float yf = y - static_cast<float>(yi);
    const float a = hash2(xi, yi, seed);
    const float b = hash2(xi + 1, yi, seed);
    const float c = hash2(xi, yi + 1, seed);
    const float d = hash2(xi + 1, yi + 1, seed);
    const float u = smooth(xf);
    const float v = smooth(yf);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

/// Fractal Brownian motion in [0,1]: sum of `octaves` value-noise layers at
/// doubling frequency and halving amplitude, normalized by total amplitude.
/// Ported verbatim from rng.ts:75 fbm2().
inline float fbm2(float x, float y, std::uint32_t seed, std::uint32_t octaves = 4u) noexcept
{
    float sum = 0.0f;
    float amp = 0.5f;
    float freq = 1.0f;
    float total = 0.0f;
    for (std::uint32_t i = 0u; i < octaves; ++i) {
        sum += noise2(x * freq, y * freq, seed + i * 1013u) * amp;
        total += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum / total;
}

} // namespace arp::core
