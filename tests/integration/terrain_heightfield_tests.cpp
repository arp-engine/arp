#include "pcg/terrain_heightfield.h"
#include "pcg/world_terrain.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

// The original procedural-grid height formula, kept here as the golden
// reference. The heightfield MUST reproduce this bit-for-bit (within float
// epsilon) so the rendered mesh and the movement sampler never disagree.
float goldenWoCCHeight(float baseHeight, float x, float z)
{
    return baseHeight + 0.5f * std::sin(x * 0.08f + 0.3f) * std::cos(z * 0.08f + 0.5f)
        + 0.3f * std::sin(x * 0.15f + z * 0.12f + 1.0f);
}

void default_field_matches_original_formula()
{
    // Use a positive baseHeight so minHeight (default 0) doesn't clamp
    // the negative excursions of the sine layers.
    const float baseHeight = 5.0f;
    const auto field = arp::core::TerrainHeightField::makeDefaultWoCC(baseHeight);
    assert(field.layerCount == 3u);

    // Sample a dense grid including negative, zero, positive, and large coords
    // (the WoCC world spans X/Z up to ~900).
    for (int zi = -20; zi <= 20; ++zi) {
        for (int xi = -20; xi <= 20; ++xi) {
            const float x = static_cast<float>(xi) * 7.3f;
            const float z = static_cast<float>(zi) * 11.1f;
            const float expected = goldenWoCCHeight(baseHeight, x, z);
            const float actual = field.sample(x, z);
            const float diff = std::fabs(actual - expected);
            // Sum-of-sinusoids expansion should match to ~1e-6, but MinGW
            // -ffast-math can introduce larger deviations. Use 1e-3 as a
            // safe bound that still catches algebraic errors.
            if (diff >= 1e-3f) {
                std::cerr << "FAIL at (" << x << "," << z << "): expected=" << expected
                          << " actual=" << actual << " diff=" << diff << "\n";
            }
            assert(diff < 1e-3f);
        }
    }
}

void sample_is_deterministic()
{
    const auto field = arp::core::TerrainHeightField::makeDefaultWoCC(0.0f);
    const float a = field.sample(13.7f, -42.1f);
    const float b = field.sample(13.7f, -42.1f);
    assert(a == b);
}

void origin_height_is_base_plus_layers()
{
    const float baseHeight = 2.0f;
    const auto field = arp::core::TerrainHeightField::makeDefaultWoCC(baseHeight);
    // At (0,0): sin(phase) contributions only.
    const float expected =
        baseHeight + 0.25f * std::sin(0.8f) + 0.25f * std::sin(-0.2f) + 0.3f * std::sin(1.0f);
    const float actual = field.sample(0.0f, 0.0f);
    assert(std::fabs(actual - expected) < 1e-6f);
}

void sample_normal_is_unit_length_and_points_up()
{
    const auto field = arp::core::TerrainHeightField::makeDefaultWoCC(5.0f);
    for (int zi = -5; zi <= 5; ++zi) {
        for (int xi = -5; xi <= 5; ++xi) {
            const float x = static_cast<float>(xi) * 13.0f;
            const float z = static_cast<float>(zi) * 13.0f;
            const auto n = field.sampleNormal(x, z);
            const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            assert(std::fabs(len - 1.0f) < 1e-4f);
            // Normal points away from the ground, so y > 0 on rolling terrain.
            assert(n.y > 0.0f);
        }
    }
}

void sample_normal_finite_difference_matches_heightfield()
{
    const auto field = arp::core::TerrainHeightField::makeDefaultWoCC(5.0f);
    const float x = 17.3f;
    const float z = -29.7f;
    const float eps = 0.1f;
    const auto n = field.sampleNormal(x, z, eps);
    const float h = field.sample(x, z);
    const float hx = field.sample(x + eps, z);
    const float hz = field.sample(x, z + eps);
    const float expectedNx = -(hx - h) / eps;
    const float expectedNz = -(hz - h) / eps;
    const float expectedNy = 1.0f;
    const float len =
        std::sqrt(expectedNx * expectedNx + expectedNy * expectedNy + expectedNz * expectedNz);
    const float expNx = expectedNx / len;
    const float expNy = expectedNy / len;
    const float expNz = expectedNz / len;
    assert(std::fabs(n.x - expNx) < 1e-5f);
    assert(std::fabs(n.y - expNy) < 1e-5f);
    assert(std::fabs(n.z - expNz) < 1e-5f);
    (void)expNy;
}

void add_layer_respects_capacity()
{
    arp::core::TerrainHeightField field;
    field.baseHeight = 0.0f;
    for (std::uint32_t i = 0; i < arp::core::kMaxTerrainHeightLayers; ++i) {
        const bool ok = field.addLayer({1.0f, 0.1f, 0.1f, 0.0f});
        assert(ok);
        assert(field.layerCount == i + 1u);
    }
    // One more should be rejected.
    const bool overflow = field.addLayer({1.0f, 0.1f, 0.1f, 0.0f});
    assert(!overflow);
    assert(field.layerCount == arp::core::kMaxTerrainHeightLayers);
}

void custom_field_sums_layers()
{
    arp::core::TerrainHeightField field;
    field.baseHeight = 1.0f;
    field.addLayer({2.0f, 0.0f, 0.0f, 0.0f});       // 2·sin(0) = 0
    field.addLayer({3.0f, 0.0f, 0.0f, 1.5707963f}); // 3·sin(π/2) = 3
    const float h = field.sample(0.0f, 0.0f);
    assert(std::fabs(h - 4.0f) < 1e-5f);
}

// ── UE-inspired noise enhancement tests ─────────────────────────────

void ridged_noise_differs_from_smooth()
{
    // A noise layer with ridgedPower > 0 should produce different output
    // than the same layer without ridging.
    arp::core::TerrainNoiseLayer smooth;
    smooth.scale = 16.0f;
    smooth.amplitude = 1.0f;
    smooth.seed = 42u;
    smooth.octaves = 4u;

    arp::core::TerrainNoiseLayer ridged = smooth;
    ridged.ridgedPower = 2.0f;

    bool anyDifferent = false;
    for (int zi = -10; zi <= 10; ++zi) {
        for (int xi = -10; xi <= 10; ++xi) {
            const float x = static_cast<float>(xi) * 3.7f;
            const float z = static_cast<float>(zi) * 5.1f;
            if (std::fabs(smooth.evaluate(x, z) - ridged.evaluate(x, z)) > 1e-5f) {
                anyDifferent = true;
                break;
            }
        }
        if (anyDifferent)
            break;
    }
    assert(anyDifferent);
}

void domain_warp_differs_from_no_warp()
{
    // A noise layer with warpStrength > 0 should produce different output
    // than the same layer without warping.
    arp::core::TerrainNoiseLayer noWarp;
    noWarp.scale = 16.0f;
    noWarp.amplitude = 1.0f;
    noWarp.seed = 42u;
    noWarp.octaves = 4u;

    arp::core::TerrainNoiseLayer warped = noWarp;
    warped.warpStrength = 5.0f;

    bool anyDifferent = false;
    for (int zi = -10; zi <= 10; ++zi) {
        for (int xi = -10; xi <= 10; ++xi) {
            const float x = static_cast<float>(xi) * 3.7f;
            const float z = static_cast<float>(zi) * 5.1f;
            if (std::fabs(noWarp.evaluate(x, z) - warped.evaluate(x, z)) > 1e-5f) {
                anyDifferent = true;
                break;
            }
        }
        if (anyDifferent)
            break;
    }
    assert(anyDifferent);
}

// ── Erosion displacement tests ──────────────────────────────────────

void erosion_displacement_bilinear_interpolation()
{
    // Build a field with a known 2x2 displacement grid.
    // Grid covers [-10, +10] with N=2, so cells are at (-10,-10) and (+10,+10).
    // displacement[0]=0 (corner 0,0), displacement[1]=0 (corner 0,1),
    // displacement[2]=0 (corner 1,0), displacement[3]=4 (corner 1,1).
    // At centre (0,0) -> fx=0.5, fz=0.5 -> bilinear = 0.5*(0+0) + 0.5*(0+4)*0.5 = 1.0
    arp::core::TerrainHeightField field;
    field.baseHeight = 5.0f;

    const int N = 2;
    const float he = 10.0f;
    auto& res = arp::core::defaultCpuMemoryResource();
    std::pmr::polymorphic_allocator<float> alloc{&res};
    float* disp = alloc.allocate(static_cast<std::size_t>(N) * N);
    disp[0] = 0.0f;
    disp[1] = 0.0f; // row 0
    disp[2] = 0.0f;
    disp[3] = 4.0f; // row 1
    field.setErosionGrid(disp, N, he, &res);

    assert(field.hasErosion);
    assert(field.erosionGridN == 2);

    // At corner (-10, -10): displacement should be 0.
    const float dCorner = field.sampleErosionDisplacement(-10.0f, -10.0f);
    assert(std::fabs(dCorner - 0.0f) < 1e-5f);

    // At corner (+10, +10): displacement should be 4.
    const float dFarCorner = field.sampleErosionDisplacement(10.0f, 10.0f);
    assert(std::fabs(dFarCorner - 4.0f) < 1e-5f);

    // At center (0, 0): bilinear of (0,0,0,4) at (0.5,0.5) = 1.0.
    const float dCenter = field.sampleErosionDisplacement(0.0f, 0.0f);
    assert(std::fabs(dCenter - 1.0f) < 1e-5f);

    // sample() should include the displacement: baseHeight(5) + displacement(1) = 6.
    const float h = field.sample(0.0f, 0.0f);
    assert(std::fabs(h - 6.0f) < 1e-4f);
}

void erosion_grid_out_of_bounds_clamps()
{
    arp::core::TerrainHeightField field;
    field.baseHeight = 0.0f;

    const int N = 4;
    const float he = 10.0f;
    auto& res = arp::core::defaultCpuMemoryResource();
    std::pmr::polymorphic_allocator<float> alloc{&res};
    float* disp = alloc.allocate(static_cast<std::size_t>(N) * N);
    for (int i = 0; i < N * N; ++i)
        disp[i] = static_cast<float>(i);
    field.setErosionGrid(disp, N, he, &res);

    // Far outside the grid should clamp to the corner value.
    const float dFar = field.sampleErosionDisplacement(1000.0f, 1000.0f);
    // Corner (N-1, N-1) = index (N-1)*N + (N-1) = 3*4+3 = 15.
    assert(std::fabs(dFar - 15.0f) < 1e-5f);

    // Far outside negative should clamp to corner (0,0) = 0.
    const float dNeg = field.sampleErosionDisplacement(-1000.0f, -1000.0f);
    assert(std::fabs(dNeg - 0.0f) < 1e-5f);
}

void erosion_changes_height()
{
    // buildWorldTerrain with erosion enabled should produce different terrain
    // than without erosion (the displacement overlay is non-zero somewhere).
    arp::core::ZoneShapingConfig zone;
    zone.halfExtent = 50.0f;
    zone.broadAmplitude = 6.0f;
    zone.broadOctaves = 4;
    zone.erosionEnabled = false;

    arp::core::ZoneShapingConfig zoneEroded = zone;
    zoneEroded.erosionEnabled = true;
    zoneEroded.erosionGridN = 64;
    zoneEroded.erosionDroplets = 500;

    const auto fieldPlain = arp::core::buildWorldTerrain(1337u, zone, 9.0f);
    const auto fieldEroded = arp::core::buildWorldTerrain(1337u, zoneEroded, 9.0f);

    assert(!fieldPlain.hasErosion);
    assert(fieldEroded.hasErosion);

    // There should be at least one sample point where erosion changed the height.
    bool anyDifferent = false;
    for (int zi = -5; zi <= 5; ++zi) {
        for (int xi = -5; xi <= 5; ++xi) {
            const float x = static_cast<float>(xi) * 8.0f;
            const float z = static_cast<float>(zi) * 8.0f;
            if (std::fabs(fieldPlain.sample(x, z) - fieldEroded.sample(x, z)) > 1e-4f) {
                anyDifferent = true;
                break;
            }
        }
        if (anyDifferent)
            break;
    }
    assert(anyDifferent);
}

void erosion_is_deterministic()
{
    // Same seed + config must produce identical terrain.
    arp::core::ZoneShapingConfig zone;
    zone.halfExtent = 50.0f;
    zone.broadAmplitude = 6.0f;
    zone.broadOctaves = 4;
    zone.erosionEnabled = true;
    zone.erosionGridN = 64;
    zone.erosionDroplets = 500;

    const auto fieldA = arp::core::buildWorldTerrain(1337u, zone, 9.0f);
    const auto fieldB = arp::core::buildWorldTerrain(1337u, zone, 9.0f);

    for (int zi = -5; zi <= 5; ++zi) {
        for (int xi = -5; xi <= 5; ++xi) {
            const float x = static_cast<float>(xi) * 8.0f;
            const float z = static_cast<float>(zi) * 8.0f;
            assert(std::fabs(fieldA.sample(x, z) - fieldB.sample(x, z)) < 1e-5f);
        }
    }
}

} // namespace

int main()
{
    default_field_matches_original_formula();
    sample_is_deterministic();
    origin_height_is_base_plus_layers();
    sample_normal_is_unit_length_and_points_up();
    sample_normal_finite_difference_matches_heightfield();
    add_layer_respects_capacity();
    custom_field_sums_layers();

    // UE-inspired noise enhancement
    ridged_noise_differs_from_smooth();
    domain_warp_differs_from_no_warp();

    // Erosion displacement
    erosion_displacement_bilinear_interpolation();
    erosion_grid_out_of_bounds_clamps();
    erosion_changes_height();
    erosion_is_deterministic();

    return 0;
}
