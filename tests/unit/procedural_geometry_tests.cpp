#include "assets/asset_pipeline.h"
#include "pcg/procedural_geometry.h"
#include "pcg/terrain_heightfield.h"
#include "pcg/terrain_scatter.h"

#include <cassert>
#include <cmath>

namespace {

using arp::core::ScatterDesc;
using arp::core::ScatterPoint;
using arp::core::TerrainHeightField;
using arp::core::terrainScatter;

// --- Procedural geometry ---

void crossed_quads_produce_8_vertices_12_indices_single_sided()
{
    arp::assets::MeshAsset mesh;
    arp::core::appendCrossedQuads(mesh, 1.0f, 1.0f);
    assert(mesh.vertices.size() == 8u); // 2 single-sided quads × 4 verts
    assert(mesh.indices.size() == 12u); // 2 quads × 6 indices (single-sided)
    // All indices must be in range
    for (const auto idx : mesh.indices) {
        assert(idx < mesh.vertices.size());
    }
}

void cone_provides_apex_and_radial_ring()
{
    arp::assets::MeshAsset mesh;
    const std::uint32_t segments = 7u;
    arp::core::appendCone(mesh, 2.0f, 5.0f, segments, false);
    // apex + 7 base ring verts
    assert(mesh.vertices.size() == segments + 1u);
    // 7 side triangles × 3 indices (single-sided; CullMode::None draws both
    // faces, the fragment shader flips normals on back faces via SV_IsFrontFacing)
    assert(mesh.indices.size() == segments * 3u);
    // Apex is at y=height
    assert(std::fabs(mesh.vertices[0].position[1] - 5.0f) < 1e-5f);
    // Base ring is at y=0
    for (std::uint32_t i = 1u; i <= segments; ++i) {
        assert(std::fabs(mesh.vertices[i].position[1]) < 1e-5f);
    }
}

void sphere_is_closed_and_roughly_spherical()
{
    arp::assets::MeshAsset mesh;
    arp::core::appendSphere(mesh, 1.5f, 8u, 5u);
    assert(!mesh.vertices.empty());
    assert(!mesh.indices.empty());
    // All vertices should be within ~1.5 of (0, 1.5, 0) (sphere center).
    for (const auto& v : mesh.vertices) {
        const float dx = v.position[0];
        const float dy = v.position[1] - 1.5f;
        const float dz = v.position[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        assert(dist <= 1.6f && dist >= 1.4f);
    }
}

void rock_is_non_empty_and_valid()
{
    arp::assets::MeshAsset mesh;
    arp::core::appendRock(mesh, 1.0f, 5u, 0.3f);
    assert(!mesh.vertices.empty());
    assert(!mesh.indices.empty());
    for (const auto idx : mesh.indices) {
        assert(idx < mesh.vertices.size());
    }
}

void merged_mesh_by_sequential_append_is_valid()
{
    // Appending multiple primitives should produce a valid combined mesh
    // with contiguous indices.
    arp::assets::MeshAsset mesh;
    arp::core::appendCrossedQuads(mesh, 1.0f, 1.0f);
    arp::core::appendCone(mesh, 2.0f, 5.0f, 6u, false);
    arp::core::appendSphere(mesh, 1.0f, 6u, 4u);
    for (const auto idx : mesh.indices) {
        assert(idx < mesh.vertices.size());
    }
    // Should have more vertices than any single primitive.
    assert(mesh.vertices.size() > 16u);
}

// --- Vegetation scatter integration ---

void vegetation_scatter_produces_points_on_terrain()
{
    const auto field = TerrainHeightField::makeDefaultWoCC(-0.5f);
    ScatterDesc desc;
    desc.minX = -40.0f;
    desc.maxX = 40.0f;
    desc.minZ = -40.0f;
    desc.maxZ = 40.0f;
    desc.step = 5.0f;
    desc.density = 0.5f;
    desc.seed = 1u;

    int count = 0;
    terrainScatter(field, desc, [&count, &field](ScatterPoint p) {
        ++count;
        // Y must match the heightfield at (x, z).
        const float expected = field.sample(p.x, p.z);
        assert(std::fabs(p.y - expected) < 1e-4f);
    });
    assert(count > 0);
}

void vegetation_scatter_count_is_bounded_by_area_and_density()
{
    const auto field = TerrainHeightField::makeDefaultWoCC(-0.5f);
    // 50×50 area, step=5 → 10×10=100 cells. At density=1.0, all 100 accepted.
    ScatterDesc desc;
    desc.minX = 0.0f;
    desc.maxX = 50.0f;
    desc.minZ = 0.0f;
    desc.maxZ = 50.0f;
    desc.step = 5.0f;
    desc.density = 1.0f;
    desc.seed = 1u;
    const auto count = arp::core::countScatter(field, desc);
    assert(count == 100u);
}

// --- Sky dome geometry ---

void sky_dome_produces_expected_vertex_and_index_counts()
{
    // 32×20 dome -> (32+1)*(20+1) = 693 vertices, 32*20*6 = 3840 indices.
    arp::assets::MeshAsset mesh;
    arp::core::appendSkyDome(mesh, 560.0f, 32u, 20u);
    assert(mesh.vertices.size() == 693u);
    assert(mesh.indices.size() == 3840u);
    for (const auto idx : mesh.indices) {
        assert(idx < mesh.vertices.size());
    }
}

void sky_dome_is_centered_at_origin_and_spherical()
{
    // The sky dome must be centered at the origin (NOT offset to y=0 like
    // appendSphere) because the vertex shader adds the camera XZ offset.
    arp::assets::MeshAsset mesh;
    arp::core::appendSkyDome(mesh, 560.0f, 16u, 10u);
    for (const auto& v : mesh.vertices) {
        const float dx = v.position[0];
        const float dy = v.position[1];
        const float dz = v.position[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        // All vertices must lie on a sphere of radius 560 centered at origin.
        assert(std::fabs(dist - 560.0f) < 1e-2f);
    }
}

void sky_dome_top_vertex_is_at_plus_radius()
{
    // The first vertex (y=0 ring, phi=0) must be at the zenith (0, +radius, 0).
    arp::assets::MeshAsset mesh;
    arp::core::appendSkyDome(mesh, 100.0f, 8u, 4u);
    assert(std::fabs(mesh.vertices[0].position[0]) < 1e-5f);
    assert(std::fabs(mesh.vertices[0].position[1] - 100.0f) < 1e-5f);
    assert(std::fabs(mesh.vertices[0].position[2]) < 1e-5f);
}

void sky_dome_bottom_vertex_is_at_minus_radius()
{
    // The last ring (phi=π) must be at the nadir (0, -radius, 0).
    arp::assets::MeshAsset mesh;
    const std::uint32_t w = 8u, h = 4u;
    arp::core::appendSkyDome(mesh, 100.0f, w, h);
    // Last ring is at index h*(w+1); all share y = cos(π)*radius = -radius.
    const auto lastRingStart = h * (w + 1u);
    for (std::uint32_t i = 0u; i <= w; ++i) {
        const auto& v = mesh.vertices[lastRingStart + i];
        assert(std::fabs(v.position[1] + 100.0f) < 1e-4f);
    }
}

void sky_dome_rejects_invalid_segment_counts()
{
    arp::assets::MeshAsset mesh;
    arp::core::appendSkyDome(mesh, 100.0f, 2u, 10u); // widthSegments < 3
    assert(mesh.vertices.empty());
    assert(mesh.indices.empty());
    arp::core::appendSkyDome(mesh, 100.0f, 10u, 1u); // heightSegments < 2
    assert(mesh.vertices.empty());
    assert(mesh.indices.empty());
}

void sky_dome_merges_with_other_primitives()
{
    // Appending sky dome after other primitives should produce a valid
    // combined mesh with contiguous indices.
    arp::assets::MeshAsset mesh;
    arp::core::appendCrossedQuads(mesh, 1.0f, 1.0f);
    const auto offset = mesh.vertices.size();
    arp::core::appendSkyDome(mesh, 560.0f, 8u, 4u);
    for (const auto idx : mesh.indices) {
        assert(idx < mesh.vertices.size());
    }
    // Sky dome vertices start at `offset`; verify the first sky vertex is at
    // the zenith (0, +radius, 0).
    assert(std::fabs(mesh.vertices[offset].position[1] - 560.0f) < 1e-2f);
}

// --- Winding / culling-behavior contract tests ---
//
// The WoCC PBR pipeline renders opaque geometry with FrontFace::Clockwise +
// CullMode::Back, and the perspective matrix Y-flips (math.h perspective()
// m[5] = -yScale). Under that setup a triangle is front-facing (kept) iff its
// screen-space winding (y-down, Vulkan window origin top-left) is clockwise,
// i.e. signed area (y-down) > 0. These tests reproduce the pipeline's VP
// matrix and assert each primitive is actually VISIBLE (has front-facing
// triangles) from the viewing direction it is meant to be seen from. This is
// the real correctness standard - normal *direction* alone is misleading
// because the Y-flip inverts screen-space handedness.

namespace culling_test {
// Build the row-major push-constant VP the shader consumes (app.cpp:683-688).
inline void buildViewProjection(
    const arp::core::Mat4& proj, const arp::core::Mat4& view, float pc[16])
{
    const auto vp = arp::core::multiply(proj, view);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            pc[r * 4 + c] = vp.m[c * 4 + r];
}

inline arp::core::Vec3 projectToNdc(const float pc[16], const float p[3])
{
    const float v[4] = {p[0], p[1], p[2], 1.0f};
    float clip[4] = {
        pc[0] * v[0] + pc[1] * v[1] + pc[2] * v[2] + pc[3],
        pc[4] * v[0] + pc[5] * v[1] + pc[6] * v[2] + pc[7],
        pc[8] * v[0] + pc[9] * v[1] + pc[10] * v[2] + pc[11],
        pc[12] * v[0] + pc[13] * v[1] + pc[14] * v[2] + pc[15],
    };
    const float iw = std::fabs(clip[3]) > 1e-9f ? 1.0f / clip[3] : 1.0f;
    return {clip[0] * iw, clip[1] * iw, clip[2] * iw};
}

// Count front-facing (kept) triangles of `mesh` viewed from `cam` looking at
// `target`, using the pipeline's FrontFace::Clockwise + CullMode::Back rule.
inline int countFrontFacing(
    const arp::assets::MeshAsset& mesh, arp::core::Vec3 cam, arp::core::Vec3 target)
{
    const auto proj = arp::core::perspective(60.0f, 1.0f, 0.1f, 1000.0f);
    const auto view = arp::core::lookAt(cam, target);
    float pc[16];
    buildViewProjection(proj, view, pc);
    int front = 0;
    for (std::size_t i = 0u; i + 2u < mesh.indices.size(); i += 3u) {
        const auto A = projectToNdc(pc, mesh.vertices[mesh.indices[i]].position);
        const auto B = projectToNdc(pc, mesh.vertices[mesh.indices[i + 1u]].position);
        const auto C = projectToNdc(pc, mesh.vertices[mesh.indices[i + 2u]].position);
        const float ay = -A.y, by = -B.y, cy = -C.y;
        const float areaYDown = (B.x - A.x) * (cy - ay) - (by - ay) * (C.x - A.x);
        if (areaYDown > 0.0f)
            ++front;
    }
    return front;
}
} // namespace culling_test

void billboard_quad_winding_faces_intended_side()
{
    // appendBillboardQuad stores +Z normals with a winding that is front-facing
    // (SV_IsFrontFacing == true) when viewed from +Z. Under CullMode::None both
    // faces render, but front-facing-ness selects which normal the fragment
    // shader uses: a front face keeps the +Z normal, a back face flips it to
    // -Z. So the winding must be front-facing from +Z (the intended front) for
    // the un-flipped normal to point the right way.
    arp::assets::MeshAsset mesh;
    arp::core::appendBillboardQuad(mesh, 1.0f, 1.0f);
    const int fromFront =
        culling_test::countFrontFacing(mesh, {0.0f, 0.5f, 5.0f}, {0.0f, 0.5f, 0.0f});
    const int fromBack =
        culling_test::countFrontFacing(mesh, {0.0f, 0.5f, -5.0f}, {0.0f, 0.5f, 0.0f});
    assert(fromFront > 0); // front-facing from +Z (keeps +Z normal)
    assert(fromBack == 0); // back-facing from -Z (shader flips normal to -Z)
}

void crossed_quads_face_orthogonal_directions()
{
    // Two single-sided quads face +Z and +X. Under CullMode::None both faces
    // of each quad render (back faces get flipped normals via SV_IsFrontFacing),
    // so total visibility is guaranteed without per-quad winding duplication.
    // This test verifies the winding direction so each quad's front face
    // (un-flipped normal) points the intended way: the +Z quad is front-facing
    // from +Z, the +X quad from +X. (The former 360° "no blind sector" scan is
    // moot under CullMode::None — nothing is culled — so it is replaced by a
    // directional winding check.)
    arp::assets::MeshAsset mesh;
    arp::core::appendCrossedQuads(mesh, 1.0f, 1.0f);
    assert(mesh.indices.size() == 12u); // single-sided: 2 quads × 6 indices

    // From +Z: the first quad is front-facing (un-flipped +Z normal).
    const int fromZ = culling_test::countFrontFacing(mesh, {0.0f, 0.5f, 5.0f}, {0.0f, 0.5f, 0.0f});
    assert(fromZ > 0);
    // From +X: the second (rotated 90°) quad is front-facing.
    const int fromX = culling_test::countFrontFacing(mesh, {5.0f, 0.5f, 0.0f}, {0.0f, 0.5f, 0.0f});
    assert(fromX > 0);
}

void cone_visible_from_outside()
{
    // The cone (tree proxy) is a closed surface; from outside it must have
    // front-facing (kept) triangles. From inside (camera at the apex looking
    // out) it would be back-facing.
    arp::assets::MeshAsset mesh;
    arp::core::appendCone(mesh, 1.5f, 3.0f, 8u, false);
    // Camera outside, looking at the cone from the side.
    const int front = culling_test::countFrontFacing(mesh, {5.0f, 2.0f, 5.0f}, {0.0f, 1.5f, 0.0f});
    assert(front > 0); // visible from outside
}

void sphere_visible_from_outside()
{
    // The sphere (critter/rock body) is a closed surface; from outside it must
    // have front-facing triangles on the near hemisphere.
    arp::assets::MeshAsset mesh;
    arp::core::appendSphere(mesh, 1.5f, 8u, 5u);
    const int front = culling_test::countFrontFacing(mesh, {5.0f, 1.5f, 0.0f}, {0.0f, 1.5f, 0.0f});
    assert(front > 0); // visible from outside
}

void rock_visible_from_outside()
{
    // A rock is a displaced closed sphere; from outside it must have
    // front-facing triangles.
    arp::assets::MeshAsset mesh;
    arp::core::appendRock(mesh, 1.0f, 6u, 0.25f);
    const int front = culling_test::countFrontFacing(mesh, {5.0f, 1.0f, 0.0f}, {0.0f, 0.5f, 0.0f});
    assert(front > 0); // visible from outside
}

} // namespace

int main()
{
    crossed_quads_produce_8_vertices_12_indices_single_sided();
    cone_provides_apex_and_radial_ring();
    sphere_is_closed_and_roughly_spherical();
    rock_is_non_empty_and_valid();
    merged_mesh_by_sequential_append_is_valid();
    vegetation_scatter_produces_points_on_terrain();
    vegetation_scatter_count_is_bounded_by_area_and_density();
    sky_dome_produces_expected_vertex_and_index_counts();
    sky_dome_is_centered_at_origin_and_spherical();
    sky_dome_top_vertex_is_at_plus_radius();
    sky_dome_bottom_vertex_is_at_minus_radius();
    sky_dome_rejects_invalid_segment_counts();
    sky_dome_merges_with_other_primitives();
    billboard_quad_winding_faces_intended_side();
    crossed_quads_face_orthogonal_directions();
    cone_visible_from_outside();
    sphere_visible_from_outside();
    rock_visible_from_outside();
    return 0;
}
