#pragma once
#include "3d/lod/lod_profile.h"

namespace arp::terrain {
using lod::LodProfile;

/// Terrain LOD profile: wraps the terrain's existing 4-tier chunk-distance
/// table as a LodProfile with world-distance tiers (chunk-distance * chunkSize).
///
/// Terrain streaming is chunk-grid based, so internally it works in chunk
/// units. This adapter converts the existing thresholds (2/5/10/50 chunks)
/// to world distances (128/320/640/3200m at chunkSize=64) so terrain shares
/// the same resolveTier() hysteresis logic as vegetation. The terrain's
/// samplesPerEdge table (33/17/9/5) is indexed by the resolved tier index
/// and remains in terrain_streamer.h (it is terrain-specific mesh detail,
/// not a general LOD concept).
///
/// @param chunkSize  World size of one terrain chunk (meters). Typically 64.
inline LodProfile terrainLodProfile(float chunkSize) noexcept
{
    LodProfile p;
    // Convert the existing 2/5/10/50 chunk table to world distances.
    // Hysteresis 0.3 chunk units = 0.3 * chunkSize meters (matches the
    // original kDefaultHysteresisMargin).
    const float hyst = 0.3f * chunkSize;
    p.tiers[0] = {2.0f * chunkSize, hyst};  // LOD0: 0-128m, 33 samples
    p.tiers[1] = {5.0f * chunkSize, hyst};  // LOD1: 128-320m, 17 samples
    p.tiers[2] = {10.0f * chunkSize, hyst}; // LOD2: 320-640m, 9 samples
    p.tiers[3] = {50.0f * chunkSize, hyst}; // LOD3: 640-3200m, 5 samples (fog)
    p.tierCount = 4;
    p.cullDistance = 50.0f * chunkSize; // fog edge
    return p;
}

} // namespace arp::terrain
