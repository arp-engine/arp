#pragma once

#include "pcg/terrain_heightfield.h"

#include <cstdint>

namespace arp::core {

struct ZoneShapingConfig {
    float halfExtent = 1000.0f;
    float minHeight = 0.0f; // hard floor — terrain never dips below this

    // Noise (driven by YAML terrain.noise)
    float broadAmplitude = 3.0f, mediumAmplitude = 1.5f, fineAmplitude = 0.5f;
    int broadOctaves = 4, mediumOctaves = 3, fineOctaves = 2;
    float broadScaleRatio = 0.25f, mediumScaleRatio = 0.06f, fineScaleRatio = 0.015f;

    // UE-inspired noise enhancement (per layer). 0 = disabled.
    // Domain Warp: displaces input coords with separate noise, organic flow.
    // Ridged Multifractal: sharp ridgelines via pow(1-|2n-1|, power).
    float broadWarpStrength = 0.0f, broadRidgedPower = 0.0f;
    float mediumWarpStrength = 0.0f, mediumRidgedPower = 0.0f;
    float fineWarpStrength = 0.0f, fineRidgedPower = 0.0f;

    // Pre-baked erosion (hydraulic + thermal). Disabled by default.
    // When enabled, buildWorldTerrain samples the analytic heightfield onto
    // an NxN grid, runs erosion, and stores the displacement (eroded - base)
    // for runtime bilinear interpolation. This produces realistic river
    // valleys and talus slopes without changing the analytic base shape.
    bool erosionEnabled = false;
    int erosionGridN = 256;               // grid resolution (NxN)
    int erosionDroplets = 20000;          // hydraulic erosion droplet count
    int erosionDropletMaxSteps = 96;      // max steps per droplet
    float erosionInertia = 0.05f;         // droplet velocity persistence
    float erosionSedimentCapacity = 4.0f; // max sediment per droplet
    float erosionRate = 0.3f;             // how fast sediment erodes
    float erosionDepositRate = 0.3f;      // how fast sediment deposits
    float erosionEvaporation = 0.01f;     // water lost per step
    int thermalIterations = 16;           // thermal erosion passes
    float thermalTalusAngle = 35.0f;      // angle of repose (degrees)

    // Central plains
    float plainsRadius = 250.0f;
    float plainsBasinDepth = 3.0f;
    float plainsFalloffRatio = 0.6f;

    // Midfield foothills - gentle concentric plateaus between plains and
    // mountains.  Produces a stepped transition (plains -> plateau -> ... ->
    // mountain base) instead of a raw noise gap.
    int midfieldPlateaus = 0;           // number of concentric plateaus (0 = disabled)
    float midfieldStartRadius = 60.0f;  // radius of the first plateau
    float midfieldStartHeight = 5.0f;   // height of the first plateau
    float midfieldSpacing = 40.0f;      // radial spacing between plateaus
    float midfieldHeightStep = 4.0f;    // height increment per plateau
    float midfieldFalloffRatio = 0.35f; // transition width / radius

    // Directional zone layout (west -> east along X axis).  Produces the
    // target layout: ocean -> beach -> plains -> hills -> mountains.
    // When `directionalLayout` is true, these fields override the radial
    // plains/midfield/mountain-ring logic with X-axis FlattenX zones.
    bool directionalLayout = false;
    float plainsCenterX = -50.0f;   // X center of the plains zone
    float plainsHalfWidth = 100.0f; // plains extends ±this from center
    float plainsHeight = 2.0f;      // target height for flat plains
    int hillSteps = 3;              // number of hill transition steps
    float hillStartX = 50.0f;       // where hills begin (east of plains)
    float hillStepWidth = 40.0f;    // X width of each hill step
    float hillStartHeight = 6.0f;   // height of first hill step
    float hillHeightStep = 8.0f;    // height increment per step
    float hillFalloffRatio = 0.4f;  // transition smoothness / step width

    // Mountain rings
    int mountainRings = 2;
    float innerRingRadius = 400.0f, innerRingHeight = 18.0f;
    float outerRingRadius = 700.0f, outerRingHeight = 25.0f;
    int mountainPasses = 4;
    float ringPassRadiusJitter = 0.08f, ringPassHeightBoost = 1.2f;
    float ringFalloffRatio = 0.08f;
    float passBaseHalfWidth = 0.25f, passWidthNoise = 0.3f;
    float passAngleStagger = 1.2f;

    // Voronoi ridge noise
    float voronoiRidgeStrength = 5.0f;
    float voronoiBasinRadius = 300.0f;
    float voronoiBasinFalloff = 0.3f;

    // Lakes
    std::uint32_t lakeCount = 6u;
    float lakeRadius = 80.0f, lakeDepth = 6.0f;
    float lakePosMin = 0.15f, lakePosMax = 0.80f;
    float lakeSizeMin = 0.7f, lakeSizeMax = 1.3f;
    float lakeFalloffRatio = 0.5f;

    // Rivers
    int riverCount = 4;
    float riverDepth = 3.0f;
    float riverAngleNoise = 0.5f;
    float riverStartRadiusMin = 0.8f, riverStartRadiusMax = 1.1f;
    float riverEndRadiusMin = 0.3f, riverEndRadiusMax = 0.8f;
    float riverCurvature = 0.3f;
    float riverBaseWidth = 20.0f, riverFalloff = 30.0f;

    // World boundary
    bool rim = true;
    float rimHeight = 15.0f;
    float rimFalloffRatio = 0.12f;

    // ── Directional terrain (Phase 7) ────────────────────────────────
    // Ocean on left side (negative X).  If true, the left half of the
    // world is carved into ocean with a beach transition.
    bool oceanLeft = false;
    float oceanBeachWidth = 120.0f; // beach transition zone (m)
    float oceanDepth = 20.0f;       // depth below water level

    // Eastward slope: land rises from centre toward the right edge.
    bool eastSlope = false;
    float eastSlopeHeight = 30.0f;    // extra height at far east edge
    float eastSlopeStartRatio = 0.0f; // slope starts at this fraction of halfExtent

    // P2: When false (landform/stamp mode), the old analytic shape layers
    // (directional plains/hills, oceanLeft basin, eastSlope ramp, rim wall)
    // are suppressed so the DEM stamps provide the dominant silhouette.
    // When true (prototype/legacy mode), all old shape layers are active
    // for A/B compatibility.
    bool legacyLandforms = true;
};

TerrainHeightField buildWorldTerrain(
    std::uint32_t seed, const ZoneShapingConfig& config, float baseHeight = 9.0f);

/// Legacy alias: default small-world zone (halfExtent=50).
/// Prefer constructing ZoneShapingConfig from YAML.
inline ZoneShapingConfig defaultWoCCZone() noexcept
{
    ZoneShapingConfig c;
    c.halfExtent = 50.0f;
    c.minHeight = 0.0f;
    c.broadAmplitude = 6.0f;
    c.mediumAmplitude = 3.0f;
    c.fineAmplitude = 1.0f;
    return c;
}

} // namespace arp::core
