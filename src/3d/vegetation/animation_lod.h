#pragma once
#include <cstdint>

namespace arp::vegetation {
/// Map a resolved LOD tier index to the animation/wind LOD class (0-3).
///
/// Tier 0 -> class 0 (full wind: primary bend + gust + turbulence + flutter)
/// Tier 1 -> class 1 (gust: primary bend + gust)
/// Tier 2 -> class 2 (sway: primary bend only)
/// Beyond last tier (pre-cull) -> class 3 (frozen: no deformation)
///
/// This replaces the old computeLodClass lambda which used chunk-center
/// distance with unrelated 20/40/60m thresholds. Now wind LOD is derived
/// from the same tier resolution as geometry LOD, so they are always coherent.
inline uint8_t tierToWindClass(int tier, int tierCount) noexcept
{
    if (tier >= tierCount)
        return 3u; // beyond last tier: frozen
    if (tier >= 3)
        return 3u;
    return static_cast<uint8_t>(tier);
}

} // namespace arp::vegetation
