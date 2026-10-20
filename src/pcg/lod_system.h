#pragma once

/// @file lod_system.h
/// @brief P4: Unified LOD profile framework.
///
/// Replaces the previously scattered LOD systems (8 independent implementations
/// with different metrics, thresholds, and no shared hysteresis) with a single
/// LodProfile abstraction inspired by commercial engines (Unreal/Unity/Godot).
///
/// A LodProfile describes N distance tiers (each with a hysteresis band) plus a
/// cull distance. All LOD decisions -- vegetation geometry LOD, vegetation
/// animation/wind LOD, distance cull, and terrain chunk LOD -- query the same
/// resolveTier() / shouldCull() interface, so thresholds are coherent and
/// hysteresis is applied uniformly.
///
/// Per-category vegetation profiles (treeLodProfile / rockLodProfile / etc.)
/// live in foliage_instance_batch.h alongside the TemplateCategory enum they
/// dispatch on. The terrain adapter (terrainLodProfile) lives here because
/// terrain has no category dependency.
///
/// Systems unified by this framework:
///   - S1: Terrain chunk mesh LOD (4 tiers, was chunk-distance + inline hysteresis)
///   - S2: Vegetation geometry LOD (3 tiers, was 3 duplicated lambdas, no hysteresis)
///   - S3: Vegetation animation/wind LOD (4 tiers, was chunk-center proxy, no hysteresis)
///   - S4: Distance cull (was per-category constants, no hysteresis)
/// Systems NOT unified (out of P4 scope):
///   - S5: shadowLod (P6: now wired via shadowCullDistance on LodProfile)
///   - S6: Water detail fade (continuous, independent system)
///   - S8: CSM cascades (view-depth driven, different dimension)

#include <algorithm>
#include <array>
#include <cmath>

namespace arp::pcg {

// ─── LOD tier descriptor ────────────────────────────────────────────────────

/// A single LOD tier: the distance band at which this detail level applies,
/// plus a hysteresis margin to prevent flicker at tier boundaries.
///
/// An object is in tier N when its distance falls in
///   [prevFarBound, farBound)
/// where prevFarBound is the farBound of tier N-1 (0 for tier 0).
///
/// Hysteresis: once in tier N, the object stays in tier N until the distance
/// moves past farBound + hysteresis (upgrading to a coarser tier) or past
/// prevFarBound - hysteresis (downgrading to a finer tier). This prevents
/// rapid toggling when the distance hovers near a boundary.
struct LodTier {
    /// Distance (meters) at which this tier ends and the next (coarser) tier
    /// begins. The last tier's farBound typically equals cullDistance.
    float farBound = 0.0f;
    /// Hysteresis margin (meters). The object must move this far past a
    /// boundary before its tier changes. Mirrors the terrain streamer's
    /// kDefaultHysteresisMargin pattern (0.3 chunk units there).
    float hysteresis = 3.0f;
};

// ─── Unified LOD profile ────────────────────────────────────────────────────

/// Unified LOD profile: distance tiers + cull distance.
///
/// One profile per category family drives geometry LOD selection, animation
/// LOD class derivation, and distance cull -- all from the same tier
/// resolution, so thresholds are coherent (no more "geometry LOD2 but wind
/// LOD0" contradictions).
///
/// Usage:
///   const auto& profile = categoryLodProfile(category);
///   int tier = profile.resolveTier(dist, currentTier);
///   // tier -> geometry LOD index (when lodStride > 1)
///   // tier -> wind class via tierToWindClass(tier, profile.tierCount)
///   // profile.shouldCull(dist, currentlyVisible) -> cull decision
struct LodProfile {
    /// Distance tiers, ordered near (highest detail) to far (coarsest).
    /// Tier 0 = highest detail. Unused tiers beyond tierCount are ignored.
    std::array<LodTier, 4> tiers{};
    /// Number of active tiers (1-4). Grass has 1 (no geometry LOD);
    /// trees/rocks have 3; terrain has 4.
    int tierCount = 1;
    /// Cull distance (meters). Beyond this, the object is not rendered.
    /// The last tier's farBound should typically equal cullDistance.
    float cullDistance = 100.0f;

    /// P6: Shadow cull distance (meters). Beyond this distance, the instance's
    /// shadowLod is set to 2 (off) -- the shadow vertex shader emits a
    /// degenerate (offscreen) position so the rasterizer clips it. This is
    /// independent of cullDistance because shadow relevance extends farther
    /// than visual rendering (CSM cascade 2 reaches 300m), but most
    /// vegetation is batch-culled well before that. Set to 0 for categories
    /// that never cast shadows (grass); the shadowLod resolver returns 2
    /// unconditionally when shadowCullDistance == 0.
    float shadowCullDistance = 0.0f;

    /// Resolve the LOD tier index for a given distance, with hysteresis.
    ///
    /// @param dist          World-space distance from camera to object (meters).
    /// @param currentTier   The object's current tier (-1 = first evaluation,
    ///                      no hysteresis; 0..tierCount-1 = apply hysteresis).
    /// @return Tier index in [0, tierCount-1]. Returns tierCount if beyond
    ///         cullDistance (caller should treat as "cull" via shouldCull).
    int resolveTier(float dist, int currentTier) const noexcept
    {
        // First evaluation or invalid current tier: no hysteresis, hard switch.
        if (currentTier < 0 || currentTier >= tierCount) {
            return resolveTierNoHysteresis(dist);
        }

        // Hysteresis: only change tier if the distance has moved past the
        // boundary by at least the hysteresis margin of the relevant tier.
        const int ideal = resolveTierNoHysteresis(dist);
        if (ideal == currentTier)
            return currentTier;

        if (ideal > currentTier) {
            // Upgrading to coarser tier: require dist > boundary + hysteresis.
            // The boundary is currentTier's farBound.
            const float boundary = tiers[static_cast<std::size_t>(currentTier)].farBound;
            const float hyst = tiers[static_cast<std::size_t>(currentTier)].hysteresis;
            if (dist < boundary + hyst) {
                return currentTier; // stay at finer tier
            }
        } else {
            // ideal < currentTier: downgrading to finer tier.
            // Require dist < boundary - hysteresis, where boundary is the
            // farBound of the tier we're downgrading INTO (ideal).
            const float boundary = tiers[static_cast<std::size_t>(ideal)].farBound;
            const float hyst = tiers[static_cast<std::size_t>(ideal)].hysteresis;
            if (dist > boundary - hyst) {
                return currentTier; // stay at coarser tier
            }
        }
        return ideal;
    }

    /// Whether the object should be culled at this distance, with hysteresis
    /// on the cull/uncull transition to prevent flicker at the cull boundary.
    ///
    /// @param dist               World-space distance from camera (meters).
    /// @param currentlyVisible   Whether the object is currently visible
    ///                           (true = was rendered last frame).
    /// @return true if the object should be culled (not rendered).
    bool shouldCull(float dist, bool currentlyVisible) const noexcept
    {
        // Use the last tier's hysteresis for the cull boundary.
        const float hyst = tiers[static_cast<std::size_t>(tierCount - 1)].hysteresis;
        if (currentlyVisible) {
            // Already visible: require dist > cull + hysteresis to cull.
            return dist > cullDistance + hyst;
        }
        // Not visible: require dist < cull - hysteresis to un-cull.
        return dist > cullDistance - hyst;
    }

private:
    /// Resolve tier without hysteresis (hard boundary switch).
    int resolveTierNoHysteresis(float dist) const noexcept
    {
        for (int i = 0; i < tierCount; ++i) {
            if (dist < tiers[static_cast<std::size_t>(i)].farBound) {
                return i;
            }
        }
        return tierCount - 1; // beyond all tiers: stay at coarsest (pre-cull)
    }
};

// ─── Wind/animation class mapping ───────────────────────────────────────────

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

// ─── Terrain LOD adapter ────────────────────────────────────────────────────

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

} // namespace arp::pcg
