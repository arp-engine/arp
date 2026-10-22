#pragma once

#include "3d/lod/lod_transition.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace arp::lod {

// Distance profile uses world meters. ScreenDiameterLod retains its distinct
// normalized projected-diameter metric; callers select the metric explicitly.
using core::LodTransition;
using core::LodTransitionBudget;
using core::ScreenDiameterLod;

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
    const char* validate() const noexcept
    {
        if (tierCount < 1 || tierCount > static_cast<int>(tiers.size()))
            return "LOD-PROFILE-TIER-COUNT";
        if (!std::isfinite(cullDistance) || cullDistance <= 0.0f
            || !std::isfinite(shadowCullDistance) || shadowCullDistance < 0.0f)
            return "LOD-PROFILE-DISTANCE";
        float previous = 0.0f;
        for (int i = 0; i < tierCount; ++i) {
            const auto& tier = tiers[static_cast<std::size_t>(i)];
            if (!std::isfinite(tier.farBound) || tier.farBound <= previous
                || !std::isfinite(tier.hysteresis) || tier.hysteresis < 0.0f)
                return "LOD-PROFILE-TIER-ORDER";
            previous = tier.farBound;
        }
        return nullptr;
    }

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
        if (validate() != nullptr || !std::isfinite(dist) || dist < 0.0f)
            return -1;
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
        if (validate() != nullptr || !std::isfinite(dist) || dist < 0.0f)
            return true;
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

} // namespace arp::lod
