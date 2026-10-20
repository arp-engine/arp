#include "pcg/terrain_scatter.h"

#include <cassert>
#include <cmath>
#include <set>
#include <vector>

namespace {

using arp::core::countScatter;
using arp::core::ScatterDesc;
using arp::core::ScatterPoint;
using arp::core::TerrainHeightField;
using arp::core::terrainScatter;

TerrainHeightField makeFlatField(float h = 0.0f)
{
    TerrainHeightField field;
    field.baseHeight = h;
    return field;
}

void flat_field_scatter_all_within_bounds()
{
    const auto field = makeFlatField(0.0f);
    ScatterDesc desc;
    desc.minX = -20.0f;
    desc.maxX = 20.0f;
    desc.minZ = -20.0f;
    desc.maxZ = 20.0f;
    desc.step = 5.0f;
    desc.density = 1.0f; // accept every cell
    desc.seed = 1u;

    std::vector<ScatterPoint> points;
    terrainScatter(field, desc, [&points](ScatterPoint p) {
        points.push_back(p);
    });

    // 8x8 grid = 64 cells, all accepted at density 1.0
    assert(points.size() == 64u);

    // Every point must be within [minX-halfStep, maxX+halfStep) on each axis
    // (jitter is ±step/2 around the cell origin).
    for (const auto& p : points) {
        assert(p.x >= -22.5f && p.x <= 22.5f);
        assert(p.z >= -22.5f && p.z <= 22.5f);
        // Flat field → y = 0 everywhere.
        assert(std::fabs(p.y) < 1e-5f);
        // jitter r ∈ [0, 1)
        assert(p.jitter >= 0.0f && p.jitter < 1.0f);
    }
}

void density_gate_reduces_count()
{
    const auto field = makeFlatField(0.0f);
    ScatterDesc desc;
    desc.minX = -50.0f;
    desc.maxX = 50.0f;
    desc.minZ = -50.0f;
    desc.maxZ = 50.0f;
    desc.step = 10.0f;
    desc.seed = 1u;

    desc.density = 1.0f;
    const auto full = countScatter(field, desc);

    desc.density = 0.5f;
    const auto half = countScatter(field, desc);

    desc.density = 0.0f;
    const auto none = countScatter(field, desc);

    // Full density keeps every cell; none rejects all.
    assert(full > 0u);
    assert(none == 0u);
    // Half density keeps roughly half (±20% for a deterministic hash).
    const float ratio = static_cast<float>(half) / static_cast<float>(full);
    assert(ratio > 0.3f && ratio < 0.7f);
}

void scatter_is_deterministic()
{
    const auto field = makeFlatField(0.0f);
    ScatterDesc desc;
    desc.minX = -20.0f;
    desc.maxX = 20.0f;
    desc.minZ = -20.0f;
    desc.maxZ = 20.0f;
    desc.step = 5.0f;
    desc.density = 0.5f;
    desc.seed = 42u;

    std::vector<ScatterPoint> a, b;
    terrainScatter(field, desc, [&a](ScatterPoint p) {
        a.push_back(p);
    });
    terrainScatter(field, desc, [&b](ScatterPoint p) {
        b.push_back(p);
    });

    assert(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        assert(a[i].x == b[i].x);
        assert(a[i].z == b[i].z);
        assert(a[i].y == b[i].y);
        assert(a[i].jitter == b[i].jitter);
    }
}

void world_aligned_submeter_scatter_is_unique_and_partition_stable()
{
    const auto field = makeFlatField(0.0f);
    ScatterDesc whole;
    whole.minX = -10.0f;
    whole.maxX = 10.0f;
    whole.minZ = -10.0f;
    whole.maxZ = 10.0f;
    whole.step = 0.5f;
    whole.density = 1.0f;
    whole.seed = 73u;
    whole.blueNoise = true;
    whole.worldAlignedGrid = true;

    std::vector<ScatterPoint> all;
    terrainScatter(field, whole, [&all](ScatterPoint p) {
        all.push_back(p);
    });
    assert(all.size() == 1600u);

    std::set<std::pair<float, float>> uniquePositions;
    for (const auto& point : all) {
        assert(point.x >= -10.25f && point.x <= 10.25f);
        assert(point.z >= -10.25f && point.z <= 10.25f);
        assert(uniquePositions.emplace(point.x, point.z).second);
    }

    ScatterDesc west = whole;
    west.maxX = 0.0f;
    ScatterDesc east = whole;
    east.minX = 0.0f;
    std::vector<ScatterPoint> partitioned;
    terrainScatter(field, west, [&partitioned](ScatterPoint p) {
        partitioned.push_back(p);
    });
    terrainScatter(field, east, [&partitioned](ScatterPoint p) {
        partitioned.push_back(p);
    });
    assert(partitioned.size() == all.size());
    for (std::size_t i = 0; i < all.size(); ++i) {
        assert(partitioned[i].x == all[i].x);
        assert(partitioned[i].z == all[i].z);
        assert(partitioned[i].jitter == all[i].jitter);
    }
}

void different_seeds_produce_different_scatter()
{
    const auto field = makeFlatField(0.0f);
    ScatterDesc desc;
    desc.minX = -50.0f;
    desc.maxX = 50.0f;
    desc.minZ = -50.0f;
    desc.maxZ = 50.0f;
    desc.step = 10.0f;
    desc.density = 0.5f;

    desc.seed = 1u;
    const auto a = countScatter(field, desc);
    desc.seed = 2u;
    const auto b = countScatter(field, desc);

    // Counts may coincidentally match, but the point positions will differ.
    std::vector<ScatterPoint> pa, pb;
    desc.seed = 1u;
    terrainScatter(field, desc, [&pa](ScatterPoint p) {
        pa.push_back(p);
    });
    desc.seed = 2u;
    terrainScatter(field, desc, [&pb](ScatterPoint p) {
        pb.push_back(p);
    });

    bool differ = (pa.size() != pb.size());
    if (!differ) {
        for (std::size_t i = 0; i < pa.size(); ++i) {
            if (pa[i].x != pb[i].x || pa[i].z != pb[i].z) {
                differ = true;
                break;
            }
        }
    }
    assert(differ);
    (void)a;
    (void)b;
}

void woc_terrain_scatter_follows_height()
{
    // The canonical WoCC terrain has rolling hills. Scattered points should
    // have Y values that track the heightfield.
    const auto field = TerrainHeightField::makeDefaultWoCC(-0.5f);
    ScatterDesc desc;
    desc.minX = -40.0f;
    desc.maxX = 40.0f;
    desc.minZ = -40.0f;
    desc.maxZ = 40.0f;
    desc.step = 10.0f;
    desc.density = 1.0f;
    desc.seed = 1u;

    std::vector<ScatterPoint> points;
    terrainScatter(field, desc, [&points](ScatterPoint p) {
        points.push_back(p);
    });

    assert(!points.empty());
    for (const auto& p : points) {
        // Placements follow the rendered mesh interpolation, not analytic detail
        // that the terrain mesh cannot represent between its vertices.
        const float expected = field.sampleMeshHeight(p.x, p.z);
        assert(std::fabs(p.y - expected) < 1e-5f);
        // WoCC terrain Y is in roughly [-1.3, +0.3].
        assert(p.y > -2.0f && p.y < 2.0f);
    }
}

void slope_rejection_skips_steep_cells()
{
    // A heightfield with a sharp ridge should be rejected at the ridge.
    // We build a custom field: flat everywhere except a steep step at x=0.
    TerrainHeightField field;
    field.baseHeight = 0.0f;
    // A high-frequency, high-amplitude layer makes steep slopes.
    arp::core::HeightWaveLayer steep;
    steep.amplitude = 5.0f;
    steep.freqX = 2.0f; // very high frequency → steep
    steep.freqZ = 0.0f;
    steep.phase = 0.0f;
    field.addLayer(steep);

    ScatterDesc desc;
    desc.minX = -10.0f;
    desc.maxX = 10.0f;
    desc.minZ = -10.0f;
    desc.maxZ = 10.0f;
    desc.step = 2.0f;
    desc.density = 1.0f;
    desc.seed = 1u;
    desc.slopeEpsilon = 0.5f;

    // No slope limit → all cells accepted.
    desc.maxSlope = 1e30f;
    const auto unconstrained = countScatter(field, desc);

    // Tight slope limit → fewer cells.
    desc.maxSlope = 0.3f;
    const auto constrained = countScatter(field, desc);

    assert(constrained < unconstrained);
    assert(constrained > 0u); // some flat cells should survive
}

void count_scatter_matches_actual_count()
{
    const auto field = makeFlatField(0.0f);
    ScatterDesc desc;
    desc.minX = -30.0f;
    desc.maxX = 30.0f;
    desc.minZ = -30.0f;
    desc.maxZ = 30.0f;
    desc.step = 5.0f;
    desc.density = 0.4f;
    desc.seed = 7u;

    const auto counted = countScatter(field, desc);
    std::size_t actual = 0u;
    terrainScatter(field, desc, [&actual](ScatterPoint) {
        ++actual;
    });
    assert(counted == actual);
}

} // namespace

int main()
{
    flat_field_scatter_all_within_bounds();
    density_gate_reduces_count();
    scatter_is_deterministic();
    world_aligned_submeter_scatter_is_unique_and_partition_stable();
    different_seeds_produce_different_scatter();
    woc_terrain_scatter_follows_height();
    slope_rejection_skips_steep_cells();
    count_scatter_matches_actual_count();
    return 0;
}
