#include "pcg/world_terrain.h"

#include "pcg/domain_warp.h"
#include "pcg/hydraulic_erosion.h"
#include "pcg/simplex_noise.h"
#include "pcg/thermal_erosion.h"
#include "pcg/voronoi_noise.h"

#include <cmath>
#include <cstring>

namespace arp::core {

TerrainHeightField buildWorldTerrain(
    std::uint32_t seed, const ZoneShapingConfig& c, float baseHeight)
{
    TerrainHeightField field;
    field.baseHeight = baseHeight;
    field.minHeight = c.minHeight; // hard floor guarantee

    // ─── 1. Domain-warped Simplex noise — all params from config ──────
    {
        TerrainNoiseLayer broad;
        broad.scale = c.halfExtent * c.broadScaleRatio;
        broad.amplitude = c.broadAmplitude;
        broad.seed = seed;
        broad.octaves = static_cast<std::uint32_t>(c.broadOctaves);
        broad.warpStrength = c.broadWarpStrength;
        broad.ridgedPower = c.broadRidgedPower;
        field.addNoiseLayer(broad);
    }
    {
        TerrainNoiseLayer medium;
        medium.scale = c.halfExtent * c.mediumScaleRatio;
        medium.amplitude = c.mediumAmplitude;
        medium.seed = seed + 501u;
        medium.octaves = static_cast<std::uint32_t>(c.mediumOctaves);
        medium.warpStrength = c.mediumWarpStrength;
        medium.ridgedPower = c.mediumRidgedPower;
        field.addNoiseLayer(medium);
    }
    {
        TerrainNoiseLayer fine;
        fine.scale = c.halfExtent * c.fineScaleRatio;
        fine.amplitude = c.fineAmplitude;
        fine.seed = seed + 1013u;
        fine.octaves = static_cast<std::uint32_t>(c.fineOctaves);
        fine.warpStrength = c.fineWarpStrength;
        fine.ridgedPower = c.fineRidgedPower;
        field.addNoiseLayer(fine);
    }

    // ─── 2/2b/3. Terrain zones ──────────────────────────────────────────
    // P2: The directional plains/hills, ocean basin, east slope, and rim
    // wall are legacy analytic shapes. In landform (stamp) mode they are
    // suppressed so the DEM stamps define the dominant silhouette instead.
    if (c.directionalLayout && c.legacyLandforms) {
        // Directional west-to-east layout (ocean -> beach -> plains -> hills
        // -> mountains) using X-axis FlattenX zones.  The ocean basin and
        // east slope are added by steps 7/8 below as usual.

        // 2a. Plains: flatten a wide X-band to a low height so the terrain
        //     is actually flat (noise suppressed) in the plains zone.
        {
            TerrainFeatureLayer plains;
            plains.kind = TerrainFeatureKind::FlattenX;
            plains.cx = c.plainsCenterX;
            plains.radius = c.plainsHalfWidth;
            plains.height = c.plainsHeight;
            plains.falloff = c.plainsHalfWidth * 0.3f;
            field.addFeatureLayer(plains);
        }

        // 2b. Hills: a series of FlattenX bands at increasing X with increasing
        //     target heights, creating a stepped transition from plains to
        //     mountain base.  Each band is centred at hillStartX + i*stepWidth.
        for (int i = 0; i < c.hillSteps; ++i) {
            TerrainFeatureLayer hill;
            hill.kind = TerrainFeatureKind::FlattenX;
            hill.cx = c.hillStartX + static_cast<float>(i) * c.hillStepWidth;
            hill.radius = c.hillStepWidth * 0.5f;
            hill.height = c.hillStartHeight + static_cast<float>(i) * c.hillHeightStep;
            hill.falloff = c.hillStepWidth * c.hillFalloffRatio;
            field.addFeatureLayer(hill);
        }

        // 3. Mountain rings are skipped in directional mode; the EastSlope
        //    (step 8) provides the eastward mountain rise instead.
    } else if (c.legacyLandforms) {
        // ─── 2. Central plains bowl ──────────────────────────────────────
        {
            TerrainFeatureLayer plain;
            plain.kind = TerrainFeatureKind::Basin;
            plain.cx = 0.0f;
            plain.cz = 0.0f;
            plain.radius = c.plainsRadius;
            plain.height = c.plainsBasinDepth;
            plain.falloff = c.plainsRadius * c.plainsFalloffRatio;
            field.addFeatureLayer(plain);
        }

        // ─── 2b. Midfield foothill plateaus ──────────────────────────────
        // Gentle concentric plateaus between the central plains and the
        // mountain rings, producing a stepped transition instead of a raw
        // noise gap.  Uses Flatten (not Plateau) so the terrain is actually
        // smoothed to the target height inside each ring.
        for (int i = 0; i < c.midfieldPlateaus; ++i) {
            TerrainFeatureLayer plateau;
            plateau.kind = TerrainFeatureKind::Flatten;
            plateau.cx = 0.0f;
            plateau.cz = 0.0f;
            plateau.radius = c.midfieldStartRadius + static_cast<float>(i) * c.midfieldSpacing;
            plateau.height = c.midfieldStartHeight + static_cast<float>(i) * c.midfieldHeightStep;
            plateau.falloff = plateau.radius * c.midfieldFalloffRatio;
            field.addFeatureLayer(plateau);
        }

        // ─── 3. Nested mountain rings with passes ─────────────────────────
        struct RingDesc {
            float radius;
            float height;
        };

        RingDesc rings[] = {
            {c.innerRingRadius, c.innerRingHeight}, {c.outerRingRadius, c.outerRingHeight}};
        int rc = c.mountainRings > 2 ? 2 : c.mountainRings;
        for (int r = 0; r < rc; ++r) {
            for (int p = 0; p < c.mountainPasses; ++p) {
                TerrainFeatureLayer ridge;
                ridge.kind = TerrainFeatureKind::Ridge;
                ridge.cx = 0.0f;
                ridge.cz = 0.0f;
                ridge.radius = rings[r].radius
                    + (static_cast<float>(p) - static_cast<float>(c.mountainPasses) * 0.5f)
                        * rings[r].radius * c.ringPassRadiusJitter;
                ridge.height = rings[r].height * c.ringPassHeightBoost;
                ridge.falloff = rings[r].radius * c.ringFalloffRatio;
                ridge.passAngle =
                    (static_cast<float>(p) / static_cast<float>(c.mountainPasses)) * 6.283185307f
                    + static_cast<float>(r) * c.passAngleStagger;
                ridge.passHalfWidth = c.passBaseHalfWidth
                    + arp::pcg::simplex2D(static_cast<float>(r * 10 + p), rings[r].radius, seed)
                        * c.passWidthNoise;
                field.addFeatureLayer(ridge);
            }
        }
    }

    // ─── 4. Voronoi ridge accent on mountains ─────────────────────────
    if (c.voronoiRidgeStrength > 0.0f) {
        TerrainFeatureLayer vbasin;
        vbasin.kind = TerrainFeatureKind::Basin;
        vbasin.cx = 0.0f;
        vbasin.cz = 0.0f;
        vbasin.radius = c.voronoiBasinRadius;
        vbasin.height =
            -c.voronoiRidgeStrength; // negative Basin = cancels centre, leaves ridges at edge
        vbasin.falloff = c.voronoiBasinRadius * c.voronoiBasinFalloff;
        field.addFeatureLayer(vbasin); // ← previously dead code, now wired
    }

    // ─── 5. Lakes ────────────────────────────────────────────────────
    for (std::uint32_t i = 0u; i < c.lakeCount; ++i) {
        const float u = arp::core::hash2(i * 2u + 1u, 7u, seed + 31u);
        const float v = arp::core::hash2(i * 2u + 2u, 13u, seed + 31u);
        const float ang = u * 6.2831853f;
        const float rad = (c.lakePosMin + (c.lakePosMax - c.lakePosMin) * v) * c.halfExtent;
        TerrainFeatureLayer lake;
        lake.kind = TerrainFeatureKind::Basin;
        lake.cx = std::cos(ang) * rad;
        lake.cz = std::sin(ang) * rad;
        lake.radius = c.lakeRadius
            * (c.lakeSizeMin
                + (c.lakeSizeMax - c.lakeSizeMin) * arp::core::hash2(i + 5u, 3u, seed));
        lake.height = c.lakeDepth;
        lake.falloff = lake.radius * c.lakeFalloffRatio;
        field.addFeatureLayer(lake);
    }

    // ─── 6. Rivers — negative corridors from mountains to centre ──────
    for (int r = 0; r < c.riverCount; ++r) {
        float startAngle = (static_cast<float>(r) / static_cast<float>(c.riverCount)) * 6.283185307f
            + arp::pcg::simplex2D(static_cast<float>(r), 0.0f, seed + 777u) * c.riverAngleNoise;
        float startRad = c.innerRingRadius
            * (c.riverStartRadiusMin
                + (c.riverStartRadiusMax - c.riverStartRadiusMin)
                    * arp::pcg::simplex2D(static_cast<float>(r + 10), 0.0f, seed));
        float endRad = c.plainsRadius
            * (c.riverEndRadiusMin
                + (c.riverEndRadiusMax - c.riverEndRadiusMin)
                    * arp::pcg::simplex2D(static_cast<float>(r + 20), 0.0f, seed));

        float sx = std::cos(startAngle) * startRad;
        float sz = std::sin(startAngle) * startRad;
        float ex = std::cos(startAngle + c.riverCurvature) * endRad;
        float ez = std::sin(startAngle + c.riverCurvature) * endRad;

        TerrainFeatureLayer river;
        river.kind = TerrainFeatureKind::Basin;
        river.cx = (sx + ex) * 0.5f;
        river.cz = (sz + ez) * 0.5f;
        river.radius =
            std::sqrt((sx - ex) * (sx - ex) + (sz - ez) * (sz - ez)) * 0.5f + c.riverBaseWidth;
        river.height = c.riverDepth;
        river.falloff = c.riverFalloff;
        field.addFeatureLayer(river);
    }

    // ─── 7. Left ocean + beach ────────────────────────────────────────
    if (c.oceanLeft && c.legacyLandforms) {
        // Ocean basin: a large disc that carves the terrain below water
        // level on the left side.  In directional layout the ocean centre
        // sits at the left world edge with a radius that just reaches the
        // plains' western boundary, so the falloff (= beach width) creates
        // a sandy transition into the plains.
        TerrainFeatureLayer ocean;
        ocean.kind = TerrainFeatureKind::Basin;
        if (c.directionalLayout) {
            ocean.cx = -c.halfExtent; // left edge
            ocean.cz = 0.0f;
            // Right edge of ocean disc = cx + radius = -halfExtent + radius.
            // We want this near the plains' western edge:
            //   plainsCenterX - plainsHalfWidth = -50 - 100 = -150
            // So radius = (-150) - (-300) = 150.
            ocean.radius = (c.plainsCenterX - c.plainsHalfWidth) + c.halfExtent;
        } else {
            ocean.cx = -c.halfExtent * 0.5f;
            ocean.cz = 0.0f;
            ocean.radius = c.halfExtent * 0.45f;
        }
        ocean.height = c.oceanDepth;
        ocean.falloff = c.oceanBeachWidth; // gentle beach slope
        field.addFeatureLayer(ocean);
    }

    // ─── 8. Eastward slope ────────────────────────────────────────────
    if (c.eastSlope && c.legacyLandforms) {
        // Add a linear ramp: 0 at startX, maxHeight at +halfExtent.
        TerrainFeatureLayer ramp;
        ramp.kind = TerrainFeatureKind::EastSlope;
        ramp.cx = 0.0f;
        ramp.cz = 0.0f;
        ramp.radius = c.halfExtent;
        ramp.height = c.eastSlopeHeight;
        ramp.falloff = c.halfExtent * c.eastSlopeStartRatio;
        field.addFeatureLayer(ramp);
    }

    // ─── 9. World rim wall ────────────────────────────────────────────
    if (c.rim && c.legacyLandforms) {
        TerrainFeatureLayer rim;
        rim.kind = TerrainFeatureKind::Rim;
        rim.cx = 0.0f;
        rim.cz = 0.0f;
        rim.radius = c.halfExtent;
        rim.height = c.rimHeight;
        rim.falloff = c.halfExtent * c.rimFalloffRatio;
        field.addFeatureLayer(rim);
    }

    // ─── 10. Pre-baked erosion (hydraulic + thermal) ───────────────────
    // UE-inspired: sample the analytic heightfield onto a grid, simulate
    // water droplet erosion (carves valleys, deposites sediment on plains)
    // and thermal erosion (talus slopes, scree fields). Store the *displacement*
    // (eroded - base) so the analytic base shape is preserved and the erosion
    // overlay is bilinearly interpolated at runtime by sample().
    if (c.erosionEnabled && c.erosionGridN >= 4) {
        const int N = c.erosionGridN;
        const float he = c.halfExtent;
        const std::size_t total = static_cast<std::size_t>(N) * static_cast<std::size_t>(N);

        // Allocate base and eroded grids via the core PMR allocator.
        auto& res = arp::core::defaultCpuMemoryResource();
        std::pmr::polymorphic_allocator<float> alloc{&res};
        float* baseGrid = alloc.allocate(total);
        float* erodedGrid = alloc.allocate(total);

        // Sample the analytic heightfield (before erosion is attached).
        // Temporarily disable hasErosion on the field so sample() returns
        // the pure analytic height.
        const float span = he * 2.0f;
        const float step = span / static_cast<float>(N - 1);
        for (int iz = 0; iz < N; ++iz) {
            for (int ix = 0; ix < N; ++ix) {
                const float wx = -he + static_cast<float>(ix) * step;
                const float wz = -he + static_cast<float>(iz) * step;
                const float h = field.sample(wx, wz);
                const std::size_t idx = static_cast<std::size_t>(iz) * N + ix;
                baseGrid[idx] = h;
                erodedGrid[idx] = h;
            }
        }

        // Hydraulic erosion: droplets carve valleys and deposit sediment.
        arp::pcg::HydraulicErosionParams hp;
        hp.numDroplets = c.erosionDroplets;
        hp.maxSteps = c.erosionDropletMaxSteps;
        hp.inertia = c.erosionInertia;
        hp.sedimentCapacity = c.erosionSedimentCapacity;
        hp.erosionRate = c.erosionRate;
        hp.depositRate = c.erosionDepositRate;
        hp.evaporationRate = c.erosionEvaporation;
        arp::pcg::hydraulicErode(N, N, erodedGrid, seed, hp);

        // Thermal erosion: talus slopes and scree fields.
        arp::pcg::ThermalErosionParams tp;
        tp.iterations = c.thermalIterations;
        tp.cellSize = step;
        tp.talusAngle = c.thermalTalusAngle;
        arp::pcg::thermalErode(N, N, erodedGrid, tp);

        // Compute displacement = eroded - base, store in a new allocation
        // that the TerrainHeightField will own via shared_ptr.
        float* displacement = alloc.allocate(total);
        for (std::size_t i = 0; i < total; ++i) {
            displacement[i] = erodedGrid[i] - baseGrid[i];
        }

        // Attach to the field (takes ownership of displacement).
        field.setErosionGrid(displacement, N, he, &res);

        // Free the temporary grids (displacement is now owned by the field).
        alloc.deallocate(baseGrid, total);
        alloc.deallocate(erodedGrid, total);
    }

    return field;
}

} // namespace arp::core
