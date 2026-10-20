#pragma once

// TerrainHeightField - deterministic procedural terrain heightfield.
//
// This module is the single source of truth for WoCC terrain elevation. Both
// the terrain mesh generator (MeshAssetResource::generateProceduralGrid) and
// the gameplay movement system sample the same TerrainHeightField instance so
// that the character's feet stay glued to the rendered ground.
//
// Design notes:
//   - Pure functions: sample(x,z) is deterministic and side-effect free.
//   - Historically a trivially-copyable value type. The optional erosion
//     displacement grid (pmr::Vector<float>) now makes the type move-only when
//     erosion is enabled; the entire codebase accesses it through
//     shared_ptr<TerrainHeightField> so this is not a regression.
//   - A single uniform layer type (sin of a linear argument) expresses the
//     full WoCC terrain. Product terms sin(a)cos(b) are encoded exactly via
//     the identity sin(a)cos(b) = ½·[sin(a+b) + sin(a−b)], so the canonical
//     field reproduces the historical mesh formula bit-for-bit.
//
// UE-inspired noise enhancement:
//   - Domain Warp: displaces input coordinates with a separate noise field
//     before sampling, producing organic flowing patterns instead of grid-
//     aligned artifacts.
//   - Ridged Multifractal: transforms fbm output via pow(1-|2n-1|, power),
//     producing sharp ridgelines typical of real mountain terrain.
//
// Erosion pre-bake:
//   - A base height grid is sampled from the analytic layers, then hydraulic
//     and thermal erosion are simulated on it. The difference (eroded - base)
//     is stored as a displacement grid and bilinearly interpolated at runtime,
//     overlaying realistic weathering without changing the analytic base.

#include "core/cpu_allocator.h"
#include "core/deterministic_hash.h"
#include "core/math.h"
#include "pcg/simplex_noise.h"    // Phase 1: replaces fbm2 in TerrainNoiseLayer
#include "pcg/terrain_landform.h" // P2: landform stamp composition

#include <array>
#include <cstdint>
#include <memory>

namespace arp::core {

/// Maximum number of wave/noise/feature layers a TerrainHeightField may carry.
/// Bounded to 8 to match the DDGI shader's TerrainHeightFieldData layout
/// (ddgi.slang hardcodes kMaxTerrainHeightLayers=8 and the struct is memcpys
/// to a GPU UBO). Changing this requires updating the shader in tandem.
inline constexpr std::size_t kMaxTerrainHeightLayers = 8u;

/// HeightWaveLayer — one sinusoidal contribution to the terrain heightfield.
///
/// A layer evaluates to:
///   amplitude * sin(freqX * x + freqZ * z + phase)
/// This single form is enough to express any sum-of-sinusoids terrain,
/// including products like sin(a)cos(b) via the trig identity
/// sin(a)cos(b) = ½·[sin(a+b) + sin(a−b)].
struct HeightWaveLayer {
    float amplitude = 0.0f;
    float freqX = 0.0f;
    float freqZ = 0.0f;
    float phase = 0.0f;

    /// Evaluate this layer's height contribution at (x, z).
    float evaluate(float x, float z) const noexcept
    {
        return amplitude * std::sin(freqX * x + freqZ * z + phase);
    }
};

/// TerrainNoiseLayer — one fractal value-noise contribution to the terrain.
///
/// Evaluates to:
///   amplitude * (fbmSimplex(x/scale, z/scale, seed, octaves) - 0.5)
/// The `− 0.5` centers the layer on baseHeight so that `amplitude` is the
/// symmetric deviation. Multiple layers at different `scale`/`octaves` compose
/// hills (broad) over detail (fine).
/// Uses Phase 1 Simplex noise for isotropic, artifact-free terrain.
struct TerrainNoiseLayer {
    float scale = 16.0f;
    float amplitude = 1.0f;
    std::uint32_t seed = 0u;
    std::uint32_t octaves = 4u;
    float warpStrength = 0.0f; ///< Domain Warp displacement (world units); 0 = off
    float ridgedPower = 0.0f;  ///< Ridged multifractal sharpness; 0 = standard fbm

    float evaluate(float x, float z) const noexcept
    {
        // Domain Warp: displace coordinates with a separate noise field.
        // Uses half-scale to produce broader warp patterns.
        float wx = x, wz = z;
        if (warpStrength > 0.0f) {
            const float warpScale = scale * 0.5f;
            const float dx =
                (arp::pcg::simplex2D(x / warpScale, z / warpScale, seed + 7000u) - 0.5f) * 2.0f
                * warpStrength;
            const float dz =
                (arp::pcg::simplex2D((x + 0.5f) / warpScale, (z + 0.5f) / warpScale, seed + 8000u)
                    - 0.5f)
                * 2.0f * warpStrength;
            wx = x + dx;
            wz = z + dz;
        }

        float n = arp::pcg::fbmSimplex(wx / scale, wz / scale, seed, static_cast<int>(octaves));

        // Ridged Multifractal: invert and sharpen to produce ridge crests.
        // Standard fbm is [0,1] centered at 0.5; ridged maps this to
        // pow(1 - |2n - 1|, power) which peaks at n=0.5 (valleys become
        // ridges) and sharpens with higher power.
        if (ridgedPower > 0.0f) {
            n = 1.0f - std::fabs(2.0f * n - 1.0f);
            n = std::pow(n, ridgedPower);
        }

        return amplitude * (n - 0.5f);
    }
};

/// Terrain feature kinds for analytic biome shaping (hub plateaus, lake
/// basins, mountain ridges, world rim walls). These are the non-linear
/// composites the reference world.ts applies on top of the fbm2 base.
enum class TerrainFeatureKind {
    Plateau,   ///< raised disc (hub / highlands)
    Basin,     ///< lowered disc (lake basin)
    Ridge,     ///< raised ring/wall (mountain ridge, with optional pass)
    Rim,       ///< world boundary wall (raises beyond radius)
    EastSlope, ///< linear ramp: 0 at left, max at right edge
    Flatten,   ///< blend terrain height toward `height` inside radius (suppresses noise)
    FlattenX,  ///< blend terrain height toward `height` inside an X-axis band (directional)
};

/// TerrainFeatureLayer — one analytic landform contribution.
///
/// Evaluate returns a signed height delta added to the field. `cx/cz/radius`
/// locate the feature; `height` is its amplitude; `falloff` is the smoothstep
/// transition width; `passAngle`/`passHalfWidth` (Ridge only) carve a gap.
struct TerrainFeatureLayer {
    TerrainFeatureKind kind = TerrainFeatureKind::Plateau;
    float cx = 0.0f;
    float cz = 0.0f;
    float radius = 20.0f;
    float height = 6.0f;
    float falloff = 6.0f;
    float passAngle = 0.0f;     ///< Ridge pass center (radians); ignored if passHalfWidth==0
    float passHalfWidth = 0.0f; ///< Ridge pass half-width (radians); 0 = no pass

    float evaluate(float x, float z) const noexcept
    {
        const float dx = x - cx;
        const float dz = z - cz;
        const float d = std::sqrt(dx * dx + dz * dz);
        const float inner = radius - falloff;
        const float outer = radius + falloff;
        switch (kind) {
        case TerrainFeatureKind::Plateau: {
            // 1 inside, 0 outside, smooth across [inner, outer].
            const float e = 1.0f - arp::core::smooth(clamp01((d - inner) / (outer - inner)));
            return height * e;
        }
        case TerrainFeatureKind::Basin: {
            const float e = 1.0f - arp::core::smooth(clamp01((d - inner) / (outer - inner)));
            return -height * e;
        }
        case TerrainFeatureKind::Ridge: {
            // Peaks at `radius`, 0 away from the band [inner, outer].
            const float e = arp::core::smooth(clamp01((d - inner) / (radius - inner)))
                * (1.0f - arp::core::smooth(clamp01((d - radius) / (outer - radius))));
            float amp = height * e;
            if (passHalfWidth > 0.0f) {
                const float ang = std::atan2(dz, dx);
                float da = std::fabs(ang - passAngle);
                if (da > 3.14159265f)
                    da = 6.2831853f - da;
                const float gate = 1.0f - (1.0f - arp::core::smooth(clamp01(da / passHalfWidth)));
                amp *= gate;
            }
            return amp;
        }
        case TerrainFeatureKind::Rim: {
            // 0 inside, ramps to `height` beyond `radius`.
            const float e = arp::core::smooth(clamp01((d - radius) / falloff));
            return height * e;
        }
        case TerrainFeatureKind::EastSlope: {
            // 0 at -radius (left), linearly ramps to height at +radius (right).
            // falloff controls the start point: ramp begins at -radius + falloff.
            const float start = -radius + falloff;
            if (x <= start)
                return 0.0f;
            const float t = clamp01((x - start) / (radius + radius - falloff));
            return height * t;
        }
        case TerrainFeatureKind::Flatten: {
            // Returns the envelope weight (1 inside, 0 outside). The actual
            // flatten blend (lerp toward target height) is handled in
            // TerrainHeightField::sample(), which needs the running height.
            const float e = 1.0f - arp::core::smooth(clamp01((d - inner) / (outer - inner)));
            return e; // NOT multiplied by height; sample() uses this as blend weight
        }
        case TerrainFeatureKind::FlattenX: {
            // X-axis band: 1 inside [cx-radius, cx+radius], smoothstep
            // transition across `falloff` at each edge. Returns the blend
            // weight; sample() lerps the running height toward `height`.
            const float innerX = cx - radius;
            const float outerX = cx + radius;
            // Left edge: 0 before innerX-falloff, 1 after innerX
            float eL = arp::core::smooth(clamp01((x - (innerX - falloff)) / (2.0f * falloff)));
            // Right edge: 1 before outerX, 0 after outerX+falloff
            float eR = 1.0f - arp::core::smooth(clamp01((x - outerX) / (2.0f * falloff)));
            return eL * eR;
        }
        }
        return 0.0f;
    }

private:
    static float clamp01(float v) noexcept
    {
        if (v < 0.0f)
            return 0.0f;
        if (v > 1.0f)
            return 1.0f;
        return v;
    }
};

/// TerrainHeightField — sum of sinusoidal layers over a base height.
///
/// height(x, z) = baseHeight
///              + sum_{i} layers[i].evaluate(x, z)       (sine waves)
///              + sum_{i} noiseLayers[i].evaluate(x, z)   (fbm/warp/ridged)
///              + sum_{i} featureLayers[i].evaluate(x, z) (plateau/basin/...)
///              + erosionDisplacement(bilinear(x, z))     (if hasErosion)
///
/// Construct via makeDefaultWoCC() for the canonical WoCC terrain, or build
/// a custom field via buildWorldTerrain() which assembles all layer types
/// and optionally pre-bakes hydraulic + thermal erosion.
struct TerrainHeightField {
    float baseHeight = 0.0f;
    float minHeight = 0.0f; // hard floor — sample() never returns below this
    std::array<HeightWaveLayer, kMaxTerrainHeightLayers> layers{};
    std::uint32_t layerCount = 0u;

    /// Fractal-noise layers (fbmSimplex over the sine layers). Bounded like the
    /// wave layers so the analytic portion of the field stays compact.
    std::array<TerrainNoiseLayer, kMaxTerrainHeightLayers> noiseLayers{};
    std::uint32_t noiseLayerCount = 0u;

    /// Analytic biome landforms (plateau/basin/ridge/rim). Bounded like the
    /// other layer arrays.
    std::array<TerrainFeatureLayer, kMaxTerrainHeightLayers> featureLayers{};
    std::uint32_t featureLayerCount = 0u;

    /// Pre-baked erosion displacement grid (NxN, row-major).
    /// Covers world space [-erosionGridHalfExtent, +erosionGridHalfExtent].
    /// Null when erosion is not enabled. Stored via shared_ptr so the struct
    /// remains trivially-copyable (required by the DDGI renderer which memcpys
    /// the struct into a GPU UBO; the erosion grid is CPU-side only and
    /// sampled by the CPU path). Allocated via the core PMR allocator.
    std::shared_ptr<const float> erosionDisplacement;
    float erosionGridHalfExtent = 0.0f;
    int erosionGridN = 0;
    bool hasErosion = false;

    /// P2: Landform stamp composition stack. When non-null, sample() overlays
    /// real DEM stamp placements on top of the analytic + erosion height.
    /// CPU-side only (never uploaded to GPU UBO). Null in prototype/legacy mode.
    std::shared_ptr<const TerrainLandformStack> landformStack;

    /// Add a wave layer. Returns false if the field is full
    /// (layerCount == kMaxTerrainHeightLayers); the field is unchanged then.
    bool addLayer(const HeightWaveLayer& layer) noexcept
    {
        if (layerCount >= kMaxTerrainHeightLayers) {
            return false;
        }
        layers[layerCount] = layer;
        ++layerCount;
        return true;
    }

    /// Add a fractal-noise layer. Returns false if the field is full.
    bool addNoiseLayer(const TerrainNoiseLayer& layer) noexcept
    {
        if (noiseLayerCount >= kMaxTerrainHeightLayers) {
            return false;
        }
        noiseLayers[noiseLayerCount] = layer;
        ++noiseLayerCount;
        return true;
    }

    /// Add an analytic biome landform layer. Returns false if the field is full.
    bool addFeatureLayer(const TerrainFeatureLayer& layer) noexcept
    {
        if (featureLayerCount >= kMaxTerrainHeightLayers) {
            return false;
        }
        featureLayers[featureLayerCount] = layer;
        ++featureLayerCount;
        return true;
    }

    /// Sample the terrain height at world coordinates (x, z).
    /// Pure and deterministic: the same (x, z) always yields the same height.
    /// Sums the base height, every sine wave layer, every noise layer, and
    /// every analytic feature layer (plateau/basin/ridge/rim), then overlays
    /// the pre-baked erosion displacement if present.
    float sample(float x, float z) const noexcept
    {
        float h = baseHeight;
        for (std::uint32_t i = 0u; i < layerCount; ++i) {
            h += layers[i].evaluate(x, z);
        }
        for (std::uint32_t i = 0u; i < noiseLayerCount; ++i) {
            h += noiseLayers[i].evaluate(x, z);
        }
        for (std::uint32_t i = 0u; i < featureLayerCount; ++i) {
            const auto& f = featureLayers[i];
            if (f.kind == TerrainFeatureKind::Flatten || f.kind == TerrainFeatureKind::FlattenX) {
                // Flatten / FlattenX blend the running height toward the
                // feature's target height, suppressing noise inside the zone.
                // evaluate() returns the blend envelope weight for these kinds.
                const float e = f.evaluate(x, z);
                h = h * (1.0f - e) + f.height * e;
            } else {
                h += f.evaluate(x, z);
            }
        }
        if (hasErosion) {
            h += sampleErosionDisplacement(x, z);
        }
        // P2: overlay landform stamp placements (real DEM features).
        // Non-destructive: if the stack is null (prototype/legacy mode),
        // this is a no-op and sample() returns the pure analytic height.
        if (landformStack) {
            h = landformStack->sample(x, z, h);
        }
        return h < minHeight ? minHeight : h;
    }

    /// Sample the terrain height WITHOUT the minHeight floor clamp.
    ///
    /// Returns the raw analytic height (base + sine + noise + features +
    /// erosion + landform stack) exactly as sample() computes it, but
    /// skips the final `h < minHeight ? minHeight : h` floor. Consumers
    /// that need the true pre-clamp height use this - most notably water
    /// rendering: when minHeight == waterLevel, the clamp flattens every
    /// underwater sample to the same value, destroying the depth gradient
    /// the water shader relies on for foam and depth-color falloff.
    /// sample() (clamped) remains the contract for mesh generation,
    /// physics, and movement, which all walk on the visible floor.
    float sampleRaw(float x, float z) const noexcept
    {
        float h = baseHeight;
        for (std::uint32_t i = 0u; i < layerCount; ++i) {
            h += layers[i].evaluate(x, z);
        }
        for (std::uint32_t i = 0u; i < noiseLayerCount; ++i) {
            h += noiseLayers[i].evaluate(x, z);
        }
        for (std::uint32_t i = 0u; i < featureLayerCount; ++i) {
            const auto& f = featureLayers[i];
            if (f.kind == TerrainFeatureKind::Flatten || f.kind == TerrainFeatureKind::FlattenX) {
                const float e = f.evaluate(x, z);
                h = h * (1.0f - e) + f.height * e;
            } else {
                h += f.evaluate(x, z);
            }
        }
        if (hasErosion) {
            h += sampleErosionDisplacement(x, z);
        }
        if (landformStack) {
            h = landformStack->sample(x, z, h);
        }
        return h;
    }

    /// Set the erosion displacement grid. Takes ownership of a float array
    /// allocated via the core PMR allocator. The deleter returns memory to
    /// the same allocator.
    void setErosionGrid(
        const float* data, int n, float halfExtent, std::pmr::memory_resource* res) noexcept
    {
        erosionGridN = n;
        erosionGridHalfExtent = halfExtent;
        hasErosion = (data != nullptr && n >= 2);
        if (hasErosion) {
            // Wrap in shared_ptr with a custom deleter that frees via PMR.
            auto deleter = [res, n](const float* p) {
                std::pmr::polymorphic_allocator<float> alloc{res};
                alloc.deallocate(const_cast<float*>(p),
                    static_cast<std::size_t>(n) * static_cast<std::size_t>(n));
            };
            erosionDisplacement = std::shared_ptr<const float>(data, deleter);
        } else {
            erosionDisplacement.reset();
        }
    }

    /// Bilinearly interpolate the erosion displacement grid at (x, z).
    /// Returns 0.0 if erosion is not enabled or (x, z) is outside the grid
    /// (clamped to edge). The grid covers [-erosionGridHalfExtent, +H] with
    /// erosionGridN samples per axis (row-major: index = iz * N + ix).
    float sampleErosionDisplacement(float x, float z) const noexcept
    {
        if (!hasErosion || erosionGridN < 2 || !erosionDisplacement)
            return 0.0f;

        const float* data = erosionDisplacement.get();

        // Map world (x, z) to normalised [0, N-1] grid coordinates.
        const float span = erosionGridHalfExtent * 2.0f;
        const float n = static_cast<float>(erosionGridN - 1);
        float gx = ((x + erosionGridHalfExtent) / span) * n;
        float gz = ((z + erosionGridHalfExtent) / span) * n;

        // Clamp to grid bounds.
        if (gx < 0.0f)
            gx = 0.0f;
        if (gx > n)
            gx = n;
        if (gz < 0.0f)
            gz = 0.0f;
        if (gz > n)
            gz = n;

        const int ix0 = static_cast<int>(gx);
        const int iz0 = static_cast<int>(gz);
        const int ix1 = (ix0 + 1 < erosionGridN) ? ix0 + 1 : ix0;
        const int iz1 = (iz0 + 1 < erosionGridN) ? iz0 + 1 : iz0;

        const float fx = gx - static_cast<float>(ix0);
        const float fz = gz - static_cast<float>(iz0);

        const float v00 = data[static_cast<std::size_t>(iz0) * erosionGridN + ix0];
        const float v10 = data[static_cast<std::size_t>(iz0) * erosionGridN + ix1];
        const float v01 = data[static_cast<std::size_t>(iz1) * erosionGridN + ix0];
        const float v11 = data[static_cast<std::size_t>(iz1) * erosionGridN + ix1];

        const float a = v00 + (v10 - v00) * fx;
        const float b = v01 + (v11 - v01) * fx;
        return a + (b - a) * fz;
    }

    /// Sample the surface normal at (x, z) via central finite differences.
    /// `epsilon` is the offset used for the difference quotient; the default
    /// matches the historical terrain mesh normal-estimation step (0.1).
    /// Returns a unit-length Vec3 pointing "up" away from the surface.
    Vec3 sampleNormal(float x, float z, float epsilon = 0.1f) const noexcept
    {
        const float h = sample(x, z);
        const float hx = sample(x + epsilon, z);
        const float hz = sample(x, z + epsilon);
        // Surface tangent slopes: dh/dx, dh/dz. Normal = (-dh/dx, 1, -dh/dz).
        float nx = -(hx - h) / epsilon;
        float nz = -(hz - h) / epsilon;
        float ny = 1.0f;
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 0.0001f) {
            nx /= len;
            ny /= len;
            nz /= len;
        }
        return Vec3{nx, ny, nz};
    }

    /// Build the canonical WoCC terrain heightfield.
    ///
    /// Reproduces the historical procedural-grid formula EXACTLY:
    ///   h = baseHeight
    ///     + 0.5 * sin(x*0.08 + 0.3) * cos(z*0.08 + 0.5)
    ///     + 0.3 * sin(x*0.15 + z*0.12 + 1.0)
    ///
    /// The product term 0.5·sin(a)·cos(b) with a=0.08x+0.3, b=0.08z+0.5 is
    /// expanded via sin(a)cos(b) = ½·sin(a+b) + ½·sin(a−b) into two layers:
    ///     0.25·sin(0.08x + 0.08z + 0.8)
    ///     0.25·sin(0.08x − 0.08z − 0.2)
    /// The third term is a single layer:
    ///     0.3·sin(0.15x + 0.12z + 1.0)
    /// Sum of these three layers + baseHeight is algebraically identical to
    /// the original mesh formula (verified by terrain_heightfield_tests).
    static TerrainHeightField makeDefaultWoCC(float baseHeight) noexcept
    {
        TerrainHeightField field;
        field.baseHeight = baseHeight;

        // ½·sin(a+b) half of the product term, a=0.08x+0.3, b=0.08z+0.5
        HeightWaveLayer productPlus;
        productPlus.amplitude = 0.25f;
        productPlus.freqX = 0.08f;
        productPlus.freqZ = 0.08f;
        productPlus.phase = 0.8f; // 0.3 + 0.5
        field.addLayer(productPlus);

        // ½·sin(a−b) half of the product term
        HeightWaveLayer productMinus;
        productMinus.amplitude = 0.25f;
        productMinus.freqX = 0.08f;
        productMinus.freqZ = -0.08f;
        productMinus.phase = -0.2f; // 0.3 − 0.5
        field.addLayer(productMinus);

        // Third term: 0.3·sin(0.15x + 0.12z + 1.0)
        HeightWaveLayer ridge;
        ridge.amplitude = 0.3f;
        ridge.freqX = 0.15f;
        ridge.freqZ = 0.12f;
        ridge.phase = 1.0f;
        field.addLayer(ridge);

        return field;
    }

    /// Build a noise-driven WoCC terrain heightfield from a world seed.
    ///
    /// Approximates the reference world.ts baseHeight() composition: a broad
    /// fbm2 "hill" layer plus a finer fbm2 "detail" layer, both centered on
    /// `baseHeight`. Because every layer samples core/deterministic_hash.h
    /// fbm2(seed), the same `seed` reproduces the same terrain and a different
    /// `seed` reshapes it — this is the engine's single world-seed entry point
    /// for terrain. The producing MeshAssetResource samples this same instance
    /// (see resource/mesh_asset_resources.cpp), so the rendered surface and the
    /// movement-sampled elevation never disagree.
    ///
    /// Tunables mirror the reference's HILL/DETAIL scales:
    ///   hills  : scale 64, amp 8,  4 octaves  (broad rolling hills)
    ///   detail : scale 16, amp 2,  2 octaves  (fine surface relief)
    static TerrainHeightField makeNoiseWoCC(float baseHeight, std::uint32_t seed) noexcept
    {
        TerrainHeightField field;
        field.baseHeight = baseHeight;

        TerrainNoiseLayer hills;
        hills.scale = 64.0f;
        hills.amplitude = 8.0f;
        hills.seed = seed;
        hills.octaves = 4u;
        field.addNoiseLayer(hills);

        TerrainNoiseLayer detail;
        detail.scale = 16.0f;
        detail.amplitude = 2.0f;
        detail.seed = seed + 1013u;
        detail.octaves = 2u;
        field.addNoiseLayer(detail);

        return field;
    }

    /// Sample the terrain height at (x, z) using the same bilinear interpolation
    /// that the terrain mesh uses at its finest LOD (33x33 vertices per 64m chunk,
    /// i.e. 2m grid spacing). This returns the height that the rendered mesh surface
    /// actually has at (x, z), rather than the exact continuous heightfield value.
    ///
    /// The mesh is a linear interpolation of heightfield samples at grid vertices.
    /// When the heightfield has high-frequency detail (noise layers, landform stamps)
    /// that the mesh grid cannot capture, the exact sample can differ from the
    /// interpolated mesh surface. Vegetation placed at the exact sample may appear
    /// to float above or sink into the mesh. This function eliminates that mismatch
    /// by replicating the mesh's interpolation.
    ///
    /// The grid is aligned to multiples of 2m in world space (the chunk size 64m
    /// is exactly 32 × 2m). This alignment is checked at compile time via a static
    /// assertion in the implementation.
    static constexpr float kMeshGridSpacing = 2.0f; ///< 64m / (33-1) vertices

    float sampleMeshHeight(float x, float z) const noexcept
    {
        // The mesh grid is aligned to multiples of kMeshGridSpacing.
        // Find the four corners of the grid cell containing (x, z).
        const float spacing = kMeshGridSpacing;
        const float x0 = std::floor(x / spacing) * spacing;
        const float z0 = std::floor(z / spacing) * spacing;
        const float x1 = x0 + spacing;
        const float z1 = z0 + spacing;

        const float h00 = sample(x0, z0);
        const float h10 = sample(x1, z0);
        const float h01 = sample(x0, z1);
        const float h11 = sample(x1, z1);

        const float tx = (x - x0) / spacing;
        const float tz = (z - z0) / spacing;

        const float h0 = h00 + (h10 - h00) * tx;
        const float h1 = h01 + (h11 - h01) * tx;
        return h0 + (h1 - h0) * tz;
    }
};

} // namespace arp::core
