// terrain_streamer_stitch_tests.cpp
//
// Verifies the edge-stitching logic that closes T-junction cracks between
// adjacent terrain chunks of differing LOD. The test synthesises a height
// sampler whose terrain has curvature (so linear interpolation between
// sparse samples differs from the true height), generates two adjacent
// chunk meshes at LOD 0 and LOD 1, and checks that the LOD-0 chunk's edge
// vertices -- after stitching -- land exactly on the LOD-1 chunk's linear
// edge, so no gap remains between them.

#include "pcg/terrain_streamer.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

// A heightfield with genuine curvature along the Z axis at the chunk's right
// edge (X=64). The ridge peaks at z=2 (the first LOD-0 midpoint between LOD-1
// vertices at z=0 and z=4). This maximises the difference between the true
// height (sampled at z=2) and the LOD-1 linear edge (lerp of h(z=0), h(z=4))
// -- exactly the T-junction gap that stitching must close. The ridge is
// modulated by x so the interior vertices also vary (keeps the test honest).
float ridgeHeight(float x, float z)
{
    // Ridge along z, centred at z=2 with amplitude 10, width ~1.3m.
    // At z=0 and z=4 the height is ~0; at z=2 it is ~10.
    const float ridge = 10.0f * std::exp(-((z - 2.0f) * (z - 2.0f)) * 1.5f);
    // Mild x variation so corners aren't all identical.
    return ridge + 0.5f * std::sin(x * 0.1f);
}

float dot3(const float* a, const float* b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

float length3(const float* v)
{
    return std::sqrt(dot3(v, v));
}

bool finite3(const float* v)
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

bool test_normal_sampling_matches_lod_grid_spacing()
{
    arp::pcg::TerrainStreamer streamer;
    arp::pcg::TerrainStreamerDesc desc;
    desc.worldHalfExtent = 256.0f;
    desc.chunkSize = 64.0f;
    desc.samplesPerChunk = 33u;
    desc.activeRadius = 6u;
    desc.skirtDepth = 20.0f;
    // LOD0 vertices are 2m apart. This heightfield is exactly zero at every
    // rendered vertex, so the generated mesh is flat. A fixed 0.1m normal
    // sample nevertheless sees a steep alternating derivative and creates
    // phantom triangle lighting that is absent from the geometry.
    desc.heightSampler = [](float wx, float) {
        constexpr float kPi = 3.14159265358979323846f;
        return std::sin(wx * kPi * 0.5f);
    };
    if (!streamer.initialize(desc)) {
        std::cerr << "[terrain-normal-scale-test] FAIL: streamer initialization failed\n";
        return false;
    }

    const auto mesh = streamer.generateChunkMesh({0, 0}, 0, 0u, 0u);
    constexpr std::size_t kSurfaceVertexCount = 33u * 33u;
    if (mesh.vertices.size() < kSurfaceVertexCount) {
        std::cerr << "[terrain-normal-scale-test] FAIL: mesh is incomplete\n";
        return false;
    }

    for (std::size_t i = 0; i < kSurfaceVertexCount; ++i) {
        const auto& normal = mesh.vertices[i].normal;
        if (std::fabs(normal[0]) > 1e-4f || std::fabs(normal[1] - 1.0f) > 1e-4f
            || std::fabs(normal[2]) > 1e-4f) {
            std::cerr << "[terrain-normal-scale-test] FAIL: flat rendered grid has "
                         "phantom slope at vertex "
                      << i << " normal=(" << normal[0] << ',' << normal[1] << ',' << normal[2]
                      << ")\n";
            return false;
        }
    }

    std::cerr << "[terrain-normal-scale-test] OK: normal sampling follows the "
                 "rendered LOD grid\n";
    return true;
}

bool test_heightfield_tangent_frame_is_finite_and_orthogonal()
{
    arp::pcg::TerrainStreamer streamer;
    arp::pcg::TerrainStreamerDesc desc;
    desc.worldHalfExtent = 256.0f;
    desc.chunkSize = 64.0f;
    desc.samplesPerChunk = 33u;
    desc.activeRadius = 6u;
    desc.skirtDepth = 20.0f;
    // Deliberately steep in X. A fixed world-X tangent is almost parallel to
    // this surface normal and used to collapse in the shader TBN projection.
    desc.heightSampler = [](float wx, float wz) {
        return 40.0f * wx + 0.25f * wz;
    };
    if (!streamer.initialize(desc)) {
        std::cerr << "[terrain-frame-test] FAIL: streamer initialization failed\n";
        return false;
    }

    const auto mesh = streamer.generateChunkMesh({0, 0}, 0, 0u, 0u);
    if (mesh.vertices.empty()) {
        std::cerr << "[terrain-frame-test] FAIL: mesh is empty\n";
        return false;
    }

    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto& vertex = mesh.vertices[i];
        if (!finite3(vertex.normal) || !finite3(vertex.tangent)) {
            std::cerr << "[terrain-frame-test] FAIL: non-finite frame at vertex " << i << '\n';
            return false;
        }
        const float normalLength = length3(vertex.normal);
        const float tangentLength = length3(vertex.tangent);
        const float orthogonality = std::fabs(dot3(vertex.normal, vertex.tangent));
        if (std::fabs(normalLength - 1.0f) > 1e-4f || std::fabs(tangentLength - 1.0f) > 1e-4f
            || orthogonality > 1e-4f) {
            std::cerr << "[terrain-frame-test] FAIL: invalid TBN source at vertex " << i
                      << " |N|=" << normalLength << " |T|=" << tangentLength
                      << " abs(dot(N,T))=" << orthogonality << '\n';
            return false;
        }
    }

    std::cerr << "[terrain-frame-test] OK: steep heightfield frames remain finite and orthogonal\n";
    return true;
}

bool test_skirt_frames_and_winding_face_outward()
{
    struct EdgeCase {
        std::uint8_t mask;
        float outward[3];
        const char* label;
    };

    constexpr EdgeCase kEdges[] = {
        {0x1u, {0.0f, 0.0f, -1.0f}, "bottom"},
        {0x2u, {0.0f, 0.0f, 1.0f}, "top"},
        {0x4u, {-1.0f, 0.0f, 0.0f}, "left"},
        {0x8u, {1.0f, 0.0f, 0.0f}, "right"},
    };

    arp::pcg::TerrainStreamer streamer;
    arp::pcg::TerrainStreamerDesc desc;
    desc.worldHalfExtent = 256.0f;
    desc.chunkSize = 64.0f;
    desc.samplesPerChunk = 33u;
    desc.activeRadius = 6u;
    desc.skirtDepth = 20.0f;
    desc.heightSampler = [](float wx, float wz) {
        return 0.2f * wx + 0.1f * wz;
    };
    if (!streamer.initialize(desc)) {
        std::cerr << "[skirt-frame-test] FAIL: streamer initialization failed\n";
        return false;
    }

    constexpr std::uint32_t kSamples = 32u;
    constexpr std::size_t kSurfaceVertexCount = 33u * 33u;
    constexpr std::size_t kSurfaceIndexCount = 32u * 32u * 6u;

    for (const auto& edge : kEdges) {
        const auto mesh = streamer.generateChunkMesh({0, 0}, 0, edge.mask, 0u);
        if (mesh.vertices.size() != kSurfaceVertexCount + kSamples * 4u
            || mesh.indices.size() != kSurfaceIndexCount + kSamples * 6u) {
            std::cerr << "[skirt-frame-test] FAIL: " << edge.label
                      << " skirt has unexpected geometry counts\n";
            return false;
        }

        for (std::uint32_t segment = 0u; segment < kSamples; ++segment) {
            const std::size_t vertexBase = kSurfaceVertexCount + segment * 4u;
            for (std::size_t lane = 0u; lane < 4u; ++lane) {
                const auto& vertex = mesh.vertices[vertexBase + lane];
                const float normalError = std::fabs(vertex.normal[0] - edge.outward[0])
                    + std::fabs(vertex.normal[1] - edge.outward[1])
                    + std::fabs(vertex.normal[2] - edge.outward[2]);
                const float tangentLength = length3(vertex.tangent);
                const float orthogonality = std::fabs(dot3(vertex.normal, vertex.tangent));
                const float bitangentY =
                    (vertex.normal[2] * vertex.tangent[0] - vertex.normal[0] * vertex.tangent[2])
                    * vertex.tangent[3];
                if (!finite3(vertex.normal) || !finite3(vertex.tangent) || normalError > 1e-5f
                    || std::fabs(tangentLength - 1.0f) > 1e-4f || orthogonality > 1e-4f
                    || bitangentY >= -1e-5f) {
                    std::cerr << "[skirt-frame-test] FAIL: invalid " << edge.label
                              << " frame at segment=" << segment << " lane=" << lane
                              << " normalError=" << normalError << " |T|=" << tangentLength
                              << " abs(dot(N,T))=" << orthogonality << " bitangentY=" << bitangentY
                              << '\n';
                    return false;
                }
            }

            const std::size_t indexBase = kSurfaceIndexCount + segment * 6u;
            for (std::size_t triangle = 0u; triangle < 2u; ++triangle) {
                const std::uint32_t ia = mesh.indices[indexBase + triangle * 3u];
                const std::uint32_t ib = mesh.indices[indexBase + triangle * 3u + 1u];
                const std::uint32_t ic = mesh.indices[indexBase + triangle * 3u + 2u];
                const auto& a = mesh.vertices[ia];
                const auto& b = mesh.vertices[ib];
                const auto& c = mesh.vertices[ic];
                const float ab[3] = {
                    b.position[0] - a.position[0],
                    b.position[1] - a.position[1],
                    b.position[2] - a.position[2],
                };
                const float ac[3] = {
                    c.position[0] - a.position[0],
                    c.position[1] - a.position[1],
                    c.position[2] - a.position[2],
                };
                const float faceNormal[3] = {
                    ab[1] * ac[2] - ab[2] * ac[1],
                    ab[2] * ac[0] - ab[0] * ac[2],
                    ab[0] * ac[1] - ab[1] * ac[0],
                };
                if (dot3(faceNormal, edge.outward) <= 0.0f) {
                    std::cerr << "[skirt-frame-test] FAIL: " << edge.label
                              << " triangle winding faces inward at segment=" << segment
                              << " triangle=" << triangle << '\n';
                    return false;
                }
            }
        }
    }

    std::cerr << "[skirt-frame-test] OK: all skirt frames and windings face outward\n";
    return true;
}

bool test_active_lod_seam_uses_stitch_without_skirt()
{
    arp::pcg::TerrainStreamer streamer;
    arp::pcg::TerrainStreamerDesc desc;
    desc.worldHalfExtent = 2048.0f;
    desc.chunkSize = 64.0f;
    desc.samplesPerChunk = 33u;
    desc.activeRadius = 6u;
    desc.skirtDepth = 20.0f;
    desc.heightSampler = [](float wx, float wz) {
        return 0.1f * wx + 0.05f * wz;
    };
    if (!streamer.initialize(desc)) {
        std::cerr << "[seam-mask-test] FAIL: streamer initialization failed\n";
        return false;
    }

    streamer.update(0.0f, 0.0f, 1.0f, 0.0f, 0.0f);

    const arp::pcg::ChunkGenRequest* lodSeam = nullptr;
    const arp::pcg::ChunkGenRequest* streamingBoundary = nullptr;
    for (const auto& request : streamer.genRequests()) {
        if (request.coord == arp::pcg::ChunkCoord{1, 0})
            lodSeam = &request;
        if (request.coord == arp::pcg::ChunkCoord{6, 0})
            streamingBoundary = &request;
    }

    if (lodSeam == nullptr || streamingBoundary == nullptr) {
        std::cerr << "[seam-mask-test] FAIL: expected chunks were not scheduled\n";
        return false;
    }

    // Chunk (1,0) is LOD0 and its active +X neighbour (2,0) is LOD1.
    // The shared edge must be stitched but must not also emit a visible wall.
    if ((lodSeam->stitchEdgeMask & 0x8u) == 0u || (lodSeam->skirtEdgeMask & 0x8u) != 0u) {
        std::cerr << "[seam-mask-test] FAIL: active LOD seam masks skirt="
                  << static_cast<unsigned>(lodSeam->skirtEdgeMask)
                  << " stitch=" << static_cast<unsigned>(lodSeam->stitchEdgeMask) << '\n';
        return false;
    }

    // Chunk (6,0) is on the circular active-set boundary; its +X neighbour is
    // not resident. That true streaming boundary still requires a skirt.
    if ((streamingBoundary->skirtEdgeMask & 0x8u) == 0u
        || (streamingBoundary->stitchEdgeMask & 0x8u) != 0u) {
        std::cerr << "[seam-mask-test] FAIL: streaming boundary masks skirt="
                  << static_cast<unsigned>(streamingBoundary->skirtEdgeMask)
                  << " stitch=" << static_cast<unsigned>(streamingBoundary->stitchEdgeMask) << '\n';
        return false;
    }

    std::cerr << "[seam-mask-test] OK: active LOD seam stitches without a skirt; "
                 "streaming boundary retains its skirt\n";
    return true;
}

bool test_stitch_snaps_edge_to_coarse_linear()
{
    arp::pcg::TerrainStreamer streamer;
    arp::pcg::TerrainStreamerDesc desc;
    desc.worldHalfExtent = 256.0f;
    desc.chunkSize = 64.0f;
    desc.samplesPerChunk = 33u;
    desc.activeRadius = 6u;
    desc.baseHeight = 0.0f;
    desc.skirtDepth = 20.0f;
    desc.heightSampler = [](float wx, float wz) {
        return ridgeHeight(wx, wz);
    };
    if (!streamer.initialize(desc)) {
        std::cerr << "[stitch-test] FAIL: streamer initialization failed\n";
        return false;
    }

    // Two adjacent chunks along X: chunk A at (0,0), chunk B at (1,0).
    // A is LOD 0 (fine, 2m/seg), B is LOD 1 (coarse, 4m/seg).
    // Stitch A's +X (right) edge onto B's -X (left) edge.
    const arp::pcg::ChunkCoord coordA{0, 0};
    const arp::pcg::ChunkCoord coordB{1, 0};

    const auto meshFine = streamer.generateChunkMesh(coordA, 0, 0u, 0x8u); // stitch right edge
    const auto meshCoarse = streamer.generateChunkMesh(coordB, 1, 0u, 0u); // no stitch

    assert(!meshFine.vertices.empty());
    assert(!meshCoarse.vertices.empty());

    // LOD 0: 33 verts/edge -> samples=32. Right edge is xi=32, zi=0..32.
    // Vertex index = zi * 33 + 32. World X = 64 (chunk A's right edge).
    const std::uint32_t vertsXFine = 33u;
    const std::uint32_t rightEdgeX = 32u;
    const std::uint32_t vertsZFine = 33u;

    // Before stitching, the fine edge vertex at zi=1 (world z=2) carries the
    // TRUE height h(2) ~ 10.0, not the linear interp of h(0),h(4) ~ small.
    // After stitching (mask 0x8 = right edge), it should be snapped to
    // lerp(h(z=0), h(z=4), 0.5).
    const float h0 = ridgeHeight(64.0f, 0.0f); // fine edge z=0 (corner, unchanged)
    const float h4 = ridgeHeight(64.0f, 4.0f); // fine edge z=4 (even, unchanged)
    const float expectedSnapped = (h0 + h4) * 0.5f;
    const float trueHeight = ridgeHeight(64.0f, 2.0f); // h(2) before stitch

    // Confirm the test is meaningful: the true height differs from the linear
    // edge by a significant amount (the ridge makes h(2) >> lerp(h0,h4)).
    if (std::fabs(trueHeight - expectedSnapped) < 1.0f) {
        std::cerr << "[stitch-test] WARN: test heightfield has no curvature at "
                  << "the midpoint; stitch effect is not exercised.\n";
        return false;
    }

    // Find the fine edge vertex at zi=1 (world z=2).
    const std::uint32_t idxZi1 = 1u * vertsXFine + rightEdgeX;
    const auto& vStitched = meshFine.vertices[idxZi1];
    const float actualSnapped = vStitched.position[1];

    const float err = std::fabs(actualSnapped - expectedSnapped);
    if (err > 1e-4f) {
        std::cerr << "[stitch-test] FAIL: edge vertex z=2 height=" << actualSnapped
                  << " expected=" << expectedSnapped << " (true h(2)=" << trueHeight << ")\n";
        return false;
    }

    // Corner vertices (zi=0 and zi=32) must be unchanged (anchored to true
    // heightfield).
    const auto& vCorner0 = meshFine.vertices[0u * vertsXFine + rightEdgeX];
    const auto& vCornerLast = meshFine.vertices[(vertsZFine - 1u) * vertsXFine + rightEdgeX];
    if (std::fabs(vCorner0.position[1] - h0) > 1e-4f) {
        std::cerr << "[stitch-test] FAIL: corner z=0 was modified by stitch\n";
        return false;
    }
    if (std::fabs(vCornerLast.position[1] - ridgeHeight(64.0f, 64.0f)) > 1e-4f) {
        std::cerr << "[stitch-test] FAIL: corner z=max was modified by stitch\n";
        return false;
    }

    std::cerr << "[stitch-test] OK: edge vertex snapped from true h(2)=" << trueHeight
              << " to linear=" << expectedSnapped
              << " (gap closed=" << (trueHeight - expectedSnapped) << ")\n";
    return true;
}

bool test_no_stitch_preserves_true_height()
{
    // With stitchEdgeMask=0, edge vertices keep their true heightfield height.
    arp::pcg::TerrainStreamer streamer;
    arp::pcg::TerrainStreamerDesc desc;
    desc.worldHalfExtent = 256.0f;
    desc.chunkSize = 64.0f;
    desc.samplesPerChunk = 33u;
    desc.activeRadius = 6u;
    desc.baseHeight = 0.0f;
    desc.skirtDepth = 20.0f;
    desc.heightSampler = [](float wx, float wz) {
        return ridgeHeight(wx, wz);
    };
    if (!streamer.initialize(desc)) {
        std::cerr << "[nostitch-test] FAIL: streamer initialization failed\n";
        return false;
    }

    const arp::pcg::ChunkCoord coordA{0, 0};
    const auto mesh = streamer.generateChunkMesh(coordA, 0, 0u, 0u); // no stitch

    const std::uint32_t vertsX = 33u;
    const std::uint32_t rightEdgeX = 32u;
    const auto& v = mesh.vertices[1u * vertsX + rightEdgeX]; // zi=1, right edge
    const float expected = ridgeHeight(64.0f, 2.0f);
    const float err = std::fabs(v.position[1] - expected);
    if (err > 1e-4f) {
        std::cerr << "[nostitch-test] FAIL: edge height=" << v.position[1]
                  << " expected=" << expected << "\n";
        return false;
    }
    std::cerr << "[nostitch-test] OK: no-stitch preserves true h(2)=" << expected << "\n";
    return true;
}

bool test_asset_declared_samples_drive_lod_mesh_resolution()
{
    arp::pcg::TerrainStreamer streamer;
    arp::pcg::TerrainStreamerDesc desc;
    desc.worldBounds = {-150.0f, 150.0f, -150.0f, 150.0f};
    desc.chunkSize = 150.0f;
    desc.samplesPerChunk = 129u;
    desc.activeRadius = 2u;
    desc.heightSampler = [](float wx, float wz) {
        return 0.01f * wx + 0.02f * wz;
    };
    if (!streamer.initialize(desc)) {
        std::cerr << "[asset-resolution-test] FAIL: streamer initialization failed\n";
        return false;
    }

    const auto lod0 = streamer.generateChunkMesh({0, 0}, 0, 0u, 0u);
    const auto lod1 = streamer.generateChunkMesh({0, 0}, 1, 0u, 0u);
    if (lod0.vertices.size() != 129u * 129u || lod1.vertices.size() != 65u * 65u) {
        std::cerr << "[asset-resolution-test] FAIL: expected 129/65 edge samples, got lod0="
                  << lod0.vertices.size() << " lod1=" << lod1.vertices.size() << '\n';
        return false;
    }
    std::cerr << "[asset-resolution-test] OK: asset samplesPerChunk produces "
                 "129/65 terrain LOD meshes\n";
    return true;
}

bool test_chunk_world_bounds_match_mesh_and_adjacent_chunks()
{
    const arp::terrain::TerrainBounds worlds[] = {
        {-300.0f, 300.0f, -300.0f, 300.0f},
        {-75.0f, 225.0f, 125.0f, 575.0f},
        {}, // legacy signed grid, without an asset origin
    };
    for (const auto& world : worlds) {
        arp::pcg::TerrainStreamer streamer;
        arp::pcg::TerrainStreamerDesc desc;
        desc.worldBounds = world;
        desc.chunkSize = 150.0f;
        desc.worldHalfExtent = 300.0f;
        desc.heightSampler = [](float x, float z) {
            return x * 0.01f + z * 0.02f;
        };
        if (!streamer.initialize(desc))
            return false;
        const arp::pcg::ChunkCoord c =
            world.valid() ? arp::pcg::ChunkCoord{0, 1} : arp::pcg::ChunkCoord{-1, -2};
        const auto bounds = streamer.chunkWorldBounds(c);
        const float expectedX = world.valid() ? world.minX : -150.0f;
        const float expectedZ = world.valid() ? world.minZ + 150.0f : -300.0f;
        const auto mesh = streamer.generateChunkMesh(c, 0);
        const auto nextX = streamer.chunkWorldBounds({c.x + 1, c.z});
        const auto nextZ = streamer.chunkWorldBounds({c.x, c.z + 1});
        if (bounds.minX != expectedX || bounds.minZ != expectedZ
            || bounds.maxX != expectedX + 150.0f || bounds.maxZ != expectedZ + 150.0f
            || bounds.maxX != nextX.minX || bounds.maxZ != nextZ.minZ || mesh.vertices.empty()
            || mesh.vertices.front().position[0] != bounds.minX
            || mesh.vertices.front().position[2] != bounds.minZ
            || mesh.vertices.back().position[0] != bounds.maxX
            || mesh.vertices.back().position[2] != bounds.maxZ) {
            std::cerr << "[chunk-bounds-test] FAIL: mesh/scatter footprint mismatch\n";
            return false;
        }
    }
    std::cerr << "[chunk-bounds-test] OK: asset origins and legacy signed grid\n";
    return true;
}

} // namespace

int main()
{
    bool ok = true;
    ok &= test_normal_sampling_matches_lod_grid_spacing();
    ok &= test_heightfield_tangent_frame_is_finite_and_orthogonal();
    ok &= test_skirt_frames_and_winding_face_outward();
    ok &= test_active_lod_seam_uses_stitch_without_skirt();
    ok &= test_no_stitch_preserves_true_height();
    ok &= test_stitch_snaps_edge_to_coarse_linear();
    ok &= test_asset_declared_samples_drive_lod_mesh_resolution();
    ok &= test_chunk_world_bounds_match_mesh_and_adjacent_chunks();
    if (!ok) {
        std::cerr << "terrain_streamer_stitch_tests: FAILED\n";
        return 1;
    }
    std::cerr << "terrain_streamer_stitch_tests: ALL PASSED\n";
    return 0;
}
