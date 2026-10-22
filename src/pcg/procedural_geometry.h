#pragma once

// ProceduralGeometry — composable primitive mesh generators.
//
// These produce assets::MeshVertex buffers (position+normal+uv+tangent) for
// common shapes used by procedural scenery: billboard quads, cones, spheres,
// cylinders. The generators are pure functions that append into a caller-owned
// MeshAsset, so multiple primitives can be merged into a single mesh (the
// foundation of merged-mesh vegetation rendering).
//
// Design:
//   - Pure: no global state, no heap allocation beyond the caller's container.
//   - Composable: each generator appends vertices/indices with a vertex-offset
//     base, so calling them in sequence builds a merged mesh.
//   - Coordinate convention: +Y up, generated at the local origin with the
//     base at y=0 (unless noted). The caller bakes world transforms.

#include "assets/asset_pipeline.h"
#include "core/math/math.h"
#include "pcg/procedural_textures.h" // for TextureRng used by Phase 5a generators

#include <cmath>
#include <functional>
#include <vector>

namespace arp::core {

/// Append a single axis-aligned billboard quad in the XY plane (facing +Z).
/// The quad's base is at y=0, top at y=height, centered on X.
/// Width spans [-width/2, +width/2].
/// Appends 4 vertices + 6 indices to `mesh`.
inline void appendBillboardQuad(assets::MeshAsset& mesh, float width, float height)
{
    const float hw = width * 0.5f;
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

    assets::MeshVertex v;
    v.normal[0] = 0.0f;
    v.normal[1] = 0.0f;
    v.normal[2] = 1.0f;
    v.tangent[0] = 1.0f;
    v.tangent[1] = 0.0f;
    v.tangent[2] = 0.0f;
    v.tangent[3] = 1.0f;

    // Bottom-left
    v.position[0] = -hw;
    v.position[1] = 0.0f;
    v.position[2] = 0.0f;
    v.uv[0] = 0.0f;
    v.uv[1] = 1.0f;
    mesh.vertices.push_back(v);
    // Bottom-right
    v.position[0] = hw;
    v.position[1] = 0.0f;
    v.position[2] = 0.0f;
    v.uv[0] = 1.0f;
    v.uv[1] = 1.0f;
    mesh.vertices.push_back(v);
    // Top-left
    v.position[0] = -hw;
    v.position[1] = height;
    v.position[2] = 0.0f;
    v.uv[0] = 0.0f;
    v.uv[1] = 0.0f;
    mesh.vertices.push_back(v);
    // Top-right
    v.position[0] = hw;
    v.position[1] = height;
    v.position[2] = 0.0f;
    v.uv[0] = 1.0f;
    v.uv[1] = 0.0f;
    mesh.vertices.push_back(v);

    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);
}

/// Append a crossed pair of billboard quads (X-shape) for grass tufts.
/// Two quads rotated 90° apart around Y, sharing the same base at y=0.
///
/// Single-sided (one winding per quad): the PBR pipeline renders these with
/// CullMode::None, so both faces of each quad are drawn regardless of winding,
/// and the fragment shader (pbr_scene.slang normalFromTexture) flips the
/// normal on back faces via SV_IsFrontFacing. This replaces the former
/// "emit each quad twice with opposite winding + flip normals in geometry"
/// approach, which doubled the vertex/index count and was the pattern that
/// led to the appendCone normal-flip bug (interior rendered black).
///
/// Appends 8 vertices + 12 indices.
inline void appendCrossedQuads(assets::MeshAsset& mesh, float width, float height)
{
    // First quad faces +Z (single-sided; CullMode::None draws the back face
    // too, with the fragment shader flipping the normal via SV_IsFrontFacing).
    appendBillboardQuad(mesh, width, height);
    // Second quad rotated 90° around Y -> faces +X.
    const auto base1 = static_cast<std::uint32_t>(mesh.vertices.size());
    appendBillboardQuad(mesh, width, height);
    // Rotate the second quad's vertices 90° around Y: (x,y,z) -> (z,y,-x)
    for (std::uint32_t i = base1; i < mesh.vertices.size(); ++i) {
        auto& v = mesh.vertices[i];
        const float px = v.position[0];
        const float pz = v.position[2];
        v.position[0] = pz;
        v.position[2] = -px;
        // Normal too
        const float nx = v.normal[0];
        const float nz = v.normal[2];
        v.normal[0] = nz;
        v.normal[2] = -nx;
    }
}

/// Append a cone (circular base at y=0, apex at y=height) with `radialSegments`
/// sides. Matches the reference pine proxy: ConeGeometry(radius, height, segs, 1).
///
/// Side triangles use a single outward-facing winding. The PBR pipeline renders
/// them with CullMode::None (both faces drawn); back-face lighting is handled
/// by the fragment shader (SV_IsFrontFacing normal flip in normalFromTexture).
/// Formerly each side was emitted twice with opposite winding to defeat
/// CullMode::Back, but the duplicated interior face kept the outward normal
/// un-flipped and rendered black (nDotV≈0) — the "other side is black" symptom.
///
/// Generates `radialSegments + 1` vertices + `radialSegments` * 3 side indices
/// (single-sided); a base fan is appended only if `includeBase` is true.
inline void appendCone(assets::MeshAsset& mesh,
    float radius,
    float height,
    std::uint32_t radialSegments,
    bool includeBase = false)
{
    if (radialSegments < 3u)
        return;
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

    // Apex vertex
    assets::MeshVertex apex;
    apex.position[0] = 0.0f;
    apex.position[1] = height;
    apex.position[2] = 0.0f;
    apex.normal[0] = 0.0f;
    apex.normal[1] = 1.0f;
    apex.normal[2] = 0.0f;
    apex.uv[0] = 0.5f;
    apex.uv[1] = 0.0f;
    mesh.vertices.push_back(apex);

    // Base ring vertices
    for (std::uint32_t i = 0u; i < radialSegments; ++i) {
        const float angle = (static_cast<float>(i) / static_cast<float>(radialSegments)) * 2.0f
            * 3.14159265358979323846f;
        assets::MeshVertex v;
        v.position[0] = std::cos(angle) * radius;
        v.position[1] = 0.0f;
        v.position[2] = std::sin(angle) * radius;
        // Side-facing normal (approximate — pointing outward and up)
        const float ny = radius / std::sqrt(radius * radius + height * height);
        const float nr = height / std::sqrt(radius * radius + height * height);
        v.normal[0] = std::cos(angle) * nr;
        v.normal[1] = ny;
        v.normal[2] = std::sin(angle) * nr;
        v.uv[0] = static_cast<float>(i) / static_cast<float>(radialSegments);
        v.uv[1] = 1.0f;
        mesh.vertices.push_back(v);
    }

    // Side triangles - single outward-facing winding. The PBR pipeline renders
    // these with CullMode::None, so the interior is drawn too; the fragment
    // shader (pbr_scene.slang normalFromTexture) flips the outward normal to
    // face the viewer on back faces via SV_IsFrontFacing. This replaces the
    // former "emit each side triangle twice with opposite winding" approach,
    // which kept the interior visible but left the outward normals un-flipped,
    // so the interior rendered black.
    for (std::uint32_t i = 0u; i < radialSegments; ++i) {
        const std::uint32_t b0 = base + 1u + i;
        const std::uint32_t b1 = base + 1u + ((i + 1u) % radialSegments);
        mesh.indices.push_back(base); // apex
        mesh.indices.push_back(b0);
        mesh.indices.push_back(b1);
    }

    if (includeBase) {
        // Base center vertex
        const auto centerIdx = static_cast<std::uint32_t>(mesh.vertices.size());
        assets::MeshVertex cv;
        cv.position[0] = 0.0f;
        cv.position[1] = 0.0f;
        cv.position[2] = 0.0f;
        cv.normal[0] = 0.0f;
        cv.normal[1] = -1.0f;
        cv.normal[2] = 0.0f;
        cv.uv[0] = 0.5f;
        cv.uv[1] = 0.5f;
        mesh.vertices.push_back(cv);
        for (std::uint32_t i = 0u; i < radialSegments; ++i) {
            const std::uint32_t b0 = base + 1u + i;
            const std::uint32_t b1 = base + 1u + ((i + 1u) % radialSegments);
            mesh.indices.push_back(centerIdx);
            mesh.indices.push_back(b1);
            mesh.indices.push_back(b0);
        }
    }
}

/// Append a UV sphere of the given radius, centered at (0, radius, 0) so its
/// bottom touches y=0. Matches the reference oak proxy:
/// SphereGeometry(radius, widthSegs, heightSegs).
inline void appendSphere(assets::MeshAsset& mesh,
    float radius,
    std::uint32_t widthSegments,
    std::uint32_t heightSegments)
{
    if (widthSegments < 3u || heightSegments < 2u)
        return;
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    constexpr float kPi = 3.14159265358979323846f;

    for (std::uint32_t y = 0u; y <= heightSegments; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(heightSegments);
        const float phi = v * kPi; // 0..π (top to bottom)
        const float sy = std::cos(phi);
        const float r = std::sin(phi);
        for (std::uint32_t x = 0u; x <= widthSegments; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(widthSegments);
            const float theta = u * 2.0f * kPi;
            assets::MeshVertex mv;
            mv.position[0] = std::cos(theta) * r * radius;
            mv.position[1] = sy * radius + radius; // offset so base at y=0
            mv.position[2] = std::sin(theta) * r * radius;
            mv.normal[0] = std::cos(theta) * r;
            mv.normal[1] = sy;
            mv.normal[2] = std::sin(theta) * r;
            mv.uv[0] = u;
            mv.uv[1] = v;
            mv.tangent[0] = -std::sin(theta);
            mv.tangent[1] = 0.0f;
            mv.tangent[2] = std::cos(theta);
            mv.tangent[3] = 1.0f;
            mesh.vertices.push_back(mv);
        }
    }

    const std::uint32_t ringSize = widthSegments + 1u;
    for (std::uint32_t y = 0u; y < heightSegments; ++y) {
        for (std::uint32_t x = 0u; x < widthSegments; ++x) {
            const std::uint32_t a = base + y * ringSize + x;
            const std::uint32_t b = a + 1u;
            const std::uint32_t c = a + ringSize;
            const std::uint32_t d = c + 1u;
            mesh.indices.push_back(a);
            mesh.indices.push_back(c);
            mesh.indices.push_back(b);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
            mesh.indices.push_back(d);
        }
    }
}

/// Append an inverted sky dome (full UV sphere centered at origin) for skybox
/// rendering. The sphere is generated with outward-facing normals/winding so
/// that when rendered with CullMode::Front (cull front faces → render inside),
/// the camera sees the interior surface — matching the reference
/// THREE.BackSide dome (sky.ts:341,389, DOME_RADIUS=560).
///
/// The sphere is centered at the local origin (NOT offset to y=0 like
/// appendSphere) because the dome follows the camera: the vertex shader adds
/// a per-frame camera XZ offset. Object-space position doubles as the view
/// direction for equirect/gradient sampling (sky.ts:233 `vDir = position`).
///
/// `widthSegments`/`heightSegments` match the reference high-tier dome
/// (32, 20). Appends (w+1)*(h+1) vertices + w*h*6 indices.
inline void appendSkyDome(assets::MeshAsset& mesh,
    float radius,
    std::uint32_t widthSegments,
    std::uint32_t heightSegments)
{
    if (widthSegments < 3u || heightSegments < 2u)
        return;
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    constexpr float kPi = 3.14159265358979323846f;

    for (std::uint32_t y = 0u; y <= heightSegments; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(heightSegments);
        const float phi = v * kPi; // 0..π (top to bottom)
        const float sy = std::cos(phi);
        const float r = std::sin(phi);
        for (std::uint32_t x = 0u; x <= widthSegments; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(widthSegments);
            const float theta = u * 2.0f * kPi;
            assets::MeshVertex mv;
            mv.position[0] = std::cos(theta) * r * radius;
            mv.position[1] = sy * radius; // centered at origin
            mv.position[2] = std::sin(theta) * r * radius;
            // Normal points outward; the sky shader doesn't use it but the
            // vertex layout requires it.
            mv.normal[0] = std::cos(theta) * r;
            mv.normal[1] = sy;
            mv.normal[2] = std::sin(theta) * r;
            mv.uv[0] = u;
            mv.uv[1] = v;
            mv.tangent[0] = -std::sin(theta);
            mv.tangent[1] = 0.0f;
            mv.tangent[2] = std::cos(theta);
            mv.tangent[3] = 1.0f;
            mesh.vertices.push_back(mv);
        }
    }

    const std::uint32_t ringSize = widthSegments + 1u;
    for (std::uint32_t y = 0u; y < heightSegments; ++y) {
        for (std::uint32_t x = 0u; x < widthSegments; ++x) {
            const std::uint32_t a = base + y * ringSize + x;
            const std::uint32_t b = a + 1u;
            const std::uint32_t c = a + ringSize;
            const std::uint32_t d = c + 1u;
            // Outward-dome winding: triangle face normal points AWAY from the
            // sphere center (outward), the standard sphere winding. The sky
            // pipeline (wocc_pbr_pipeline.cpp) renders with CullMode::Front +
            // FrontFace::Clockwise, equivalent to the reference's
            // THREE.BackSide (sky.ts:344/385) on a standard outward sphere: a
            // camera inside the dome sees each outward triangle's back face,
            // which CullMode::Front keeps (it culls the front face). Verified
            // numerically with the corrected VP matrix (sky.slang viewProjection
            // uses the default RowMajor layout, matching pbr_scene): outward
            // winding keeps 588/594 front-facing triangles visible while inward
            // keeps 0 (all culled) — so outward is the only correct choice.
            // (An earlier inward attempt was a misdiagnosis made while the VP
            // matrix was still transposed by the `row_major` declaration bug in
            // sky.slang, which made the dome project through a transposed clip
            // space and masked the true winding behavior.) Verified by
            // wocc_shadow_sky_contract_tests (sky_dome_triangles_face_outward).
            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
            mesh.indices.push_back(b);
            mesh.indices.push_back(d);
            mesh.indices.push_back(c);
        }
    }
}

/// Append a low-poly rock: a scaled, noise-displaced sphere approximating a
/// boulder. `radius` is the nominal size; `lobes` controls displacement frequency.
inline void appendRock(
    assets::MeshAsset& mesh, float radius, std::uint32_t segments = 6u, float lobiness = 0.25f)
{
    if (segments < 3u)
        return;
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    constexpr float kPi = 3.14159265358979323846f;

    // Generate a low-poly sphere with per-vertex radial displacement to give
    // it an irregular boulder silhouette.
    for (std::uint32_t y = 0u; y <= segments; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(segments);
        const float phi = v * kPi;
        const float sy = std::cos(phi);
        const float r = std::sin(phi);
        for (std::uint32_t x = 0u; x <= segments; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(segments);
            const float theta = u * 2.0f * kPi;
            // Deterministic displacement from angles (no RNG needed).
            const float disp = 1.0f + lobiness * std::sin(theta * 3.0f + phi * 2.0f)
                + lobiness * 0.5f * std::cos(theta * 5.0f - phi * 4.0f);
            assets::MeshVertex mv;
            mv.position[0] = std::cos(theta) * r * radius * disp;
            mv.position[1] = sy * radius * disp * 0.7f + radius * 0.5f; // flatten + raise
            mv.position[2] = std::sin(theta) * r * radius * disp;
            mv.normal[0] = std::cos(theta) * r;
            mv.normal[1] = sy;
            mv.normal[2] = std::sin(theta) * r;
            mv.uv[0] = u;
            mv.uv[1] = v;
            mv.tangent[0] = 1.0f;
            mv.tangent[1] = 0.0f;
            mv.tangent[2] = 0.0f;
            mv.tangent[3] = 1.0f;
            mesh.vertices.push_back(mv);
        }
    }

    const std::uint32_t ringSize = segments + 1u;
    for (std::uint32_t y = 0u; y < segments; ++y) {
        for (std::uint32_t x = 0u; x < segments; ++x) {
            const std::uint32_t a = base + y * ringSize + x;
            const std::uint32_t b = a + 1u;
            const std::uint32_t c = a + ringSize;
            const std::uint32_t d = c + 1u;
            mesh.indices.push_back(a);
            mesh.indices.push_back(c);
            mesh.indices.push_back(b);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
            mesh.indices.push_back(d);
        }
    }
}

// ─── Phase 5a: Extended model library ───────────────────────────────────────

/// Append a truncated cone (cylinder with different top/bottom radii).
/// Base at y=0, top at y=height. Used for tree trunks.
inline void appendTruncatedCone(assets::MeshAsset& mesh,
    float baseRadius,
    float topRadius,
    float height,
    std::uint32_t radialSegments = 8u)
{
    if (radialSegments < 3u)
        return;
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

    // Bottom ring
    for (std::uint32_t i = 0u; i < radialSegments; ++i) {
        const float angle =
            (static_cast<float>(i) / static_cast<float>(radialSegments)) * 6.283185307f;
        assets::MeshVertex v;
        v.position[0] = std::cos(angle) * baseRadius;
        v.position[1] = 0.0f;
        v.position[2] = std::sin(angle) * baseRadius;
        v.normal[0] = std::cos(angle);
        v.normal[1] = 0.1f;
        v.normal[2] = std::sin(angle);
        const float nr = 1.0f / std::sqrt(baseRadius * baseRadius + height * height);
        v.normal[0] *= nr;
        v.normal[2] *= nr;
        v.normal[1] = height * nr;
        v.uv[0] = static_cast<float>(i) / static_cast<float>(radialSegments);
        v.uv[1] = 0.0f;
        v.tangent[0] = 1.0f;
        v.tangent[1] = 0.0f;
        v.tangent[2] = 0.0f;
        v.tangent[3] = 1.0f;
        mesh.vertices.push_back(v);
    }
    // Top ring
    for (std::uint32_t i = 0u; i < radialSegments; ++i) {
        const float angle =
            (static_cast<float>(i) / static_cast<float>(radialSegments)) * 6.283185307f;
        assets::MeshVertex v;
        v.position[0] = std::cos(angle) * topRadius;
        v.position[1] = height;
        v.position[2] = std::sin(angle) * topRadius;
        v.normal[0] = std::cos(angle);
        v.normal[1] = 0.1f;
        v.normal[2] = std::sin(angle);
        const float nr = 1.0f / std::sqrt(topRadius * topRadius + height * height);
        v.normal[0] *= nr;
        v.normal[2] *= nr;
        v.normal[1] = height * nr;
        v.uv[0] = static_cast<float>(i) / static_cast<float>(radialSegments);
        v.uv[1] = 1.0f;
        v.tangent[0] = 1.0f;
        v.tangent[1] = 0.0f;
        v.tangent[2] = 0.0f;
        v.tangent[3] = 1.0f;
        mesh.vertices.push_back(v);
    }
    // Side triangles
    for (std::uint32_t i = 0u; i < radialSegments; ++i) {
        const std::uint32_t b0 = base + i;
        const std::uint32_t b1 = base + ((i + 1u) % radialSegments);
        const std::uint32_t t0 = base + radialSegments + i;
        const std::uint32_t t1 = base + radialSegments + ((i + 1u) % radialSegments);
        mesh.indices.push_back(b0);
        mesh.indices.push_back(t0);
        mesh.indices.push_back(b1);
        mesh.indices.push_back(b1);
        mesh.indices.push_back(t0);
        mesh.indices.push_back(t1);
    }
}

/// Full tree: truncated cone trunk + 2-3 overlapping ellipsoid canopy blobs.
inline void appendFullTree(assets::MeshAsset& mesh,
    float trunkHeight,
    float trunkBaseR,
    float trunkTopRatio,
    float canopyRadius,
    int blobCount = 3,
    std::uint32_t segs = 8u)
{
    // Trunk
    appendTruncatedCone(mesh, trunkBaseR, trunkBaseR * trunkTopRatio, trunkHeight, segs);

    // Canopy: overlapping ellipsoids at top of trunk
    arp::core::pgtex::TextureRng rng(static_cast<uint32_t>(mesh.vertices.size()));
    for (int b = 0; b < blobCount; ++b) {
        const float offsetX = (rng.next() - 0.5f) * canopyRadius * 0.8f;
        const float offsetY = trunkHeight + (rng.next() - 0.3f) * canopyRadius * 0.5f;
        const float offsetZ = (rng.next() - 0.5f) * canopyRadius * 0.8f;
        const float sr = canopyRadius * (0.7f + rng.next() * 0.5f);
        const float sy = canopyRadius * (0.6f + rng.next() * 0.6f);

        const auto baseV = static_cast<std::uint32_t>(mesh.vertices.size());
        appendSphere(mesh, 1.0f, segs, segs / 2u);
        // Transform to ellipsoid position
        for (std::uint32_t i = baseV; i < mesh.vertices.size(); ++i) {
            auto& v = mesh.vertices[i];
            v.position[0] = v.position[0] * sr + offsetX;
            v.position[1] = v.position[1] * sy + offsetY;
            v.position[2] = v.position[2] * sr + offsetZ;
        }
    }
}

/// Bush: 2-4 flattened ellipsoids clustered together.
inline void appendBush(
    assets::MeshAsset& mesh, float radius, float height, int blobCount = 3, std::uint32_t segs = 6u)
{
    arp::core::pgtex::TextureRng rng(static_cast<uint32_t>(mesh.vertices.size()));
    for (int b = 0; b < blobCount; ++b) {
        const float ox = (rng.next() - 0.5f) * radius * 0.6f;
        const float oy = height * 0.3f + rng.next() * height * 0.4f;
        const float oz = (rng.next() - 0.5f) * radius * 0.6f;
        const float sr = radius * (0.6f + rng.next() * 0.6f);

        const auto baseV = static_cast<std::uint32_t>(mesh.vertices.size());
        appendSphere(mesh, 1.0f, segs, segs / 2u);
        for (std::uint32_t i = baseV; i < mesh.vertices.size(); ++i) {
            auto& v = mesh.vertices[i];
            v.position[0] = v.position[0] * sr + ox;
            v.position[1] = v.position[1] * height * 0.4f + oy;
            v.position[2] = v.position[2] * sr + oz;
        }
    }
}

/// Dead tree: trunk + 2-4 bare branches (thin cones extending upward/outward).
inline void appendDeadTree(assets::MeshAsset& mesh,
    float height,
    float baseRadius,
    int branchCount = 3,
    std::uint32_t segs = 6u)
{
    appendTruncatedCone(mesh, baseRadius, baseRadius * 0.4f, height, segs);

    arp::core::pgtex::TextureRng rng(static_cast<uint32_t>(mesh.vertices.size()));
    for (int b = 0; b < branchCount; ++b) {
        const float startY = height * (0.4f + rng.next() * 0.5f);
        const float angle = rng.next() * 6.283185307f;
        const float tilt = 0.3f + rng.next() * 0.5f;
        const float len = height * (0.2f + rng.next() * 0.4f);
        const float br = baseRadius * (0.1f + rng.next() * 0.2f);

        const auto baseV = static_cast<std::uint32_t>(mesh.vertices.size());
        for (std::uint32_t i = 0; i < segs; ++i) {
            const float a = (static_cast<float>(i) / static_cast<float>(segs)) * 6.283185307f;
            assets::MeshVertex v;
            v.position[0] = std::cos(a) * br;
            v.position[1] = startY;
            v.position[2] = std::sin(a) * br;
            v.uv[0] = static_cast<float>(i) / static_cast<float>(segs);
            v.uv[1] = 0.0f;
            mesh.vertices.push_back(v);
        }
        for (std::uint32_t i = 0; i < segs; ++i) {
            const float a = (static_cast<float>(i) / static_cast<float>(segs)) * 6.283185307f;
            assets::MeshVertex v;
            v.position[0] = std::cos(angle) * tilt * len + std::cos(a) * br * 0.2f;
            v.position[1] = startY + len * (1.0f - tilt * 0.3f);
            v.position[2] = std::sin(angle) * tilt * len + std::sin(a) * br * 0.2f;
            v.uv[0] = static_cast<float>(i) / static_cast<float>(segs);
            v.uv[1] = 1.0f;
            mesh.vertices.push_back(v);
        }
        for (std::uint32_t i = 0; i < segs; ++i) {
            const auto b0 = baseV + i, b1 = baseV + (i + 1u) % segs;
            const auto t0 = baseV + segs + i, t1 = baseV + segs + (i + 1u) % segs;
            mesh.indices.push_back(b0);
            mesh.indices.push_back(t0);
            mesh.indices.push_back(b1);
            mesh.indices.push_back(b1);
            mesh.indices.push_back(t0);
            mesh.indices.push_back(t1);
        }
    }
}

/// Stump: short truncated cone + irregular top.
inline void appendStump(
    assets::MeshAsset& mesh, float radius, float height, std::uint32_t segs = 8u)
{
    appendTruncatedCone(mesh, radius, radius * 0.7f, height, segs);
}

/// Fallen log: horizontal truncated cone with slight bend.
inline void appendFallenLog(
    assets::MeshAsset& mesh, float length, float radius, float bend = 0.1f, std::uint32_t segs = 8u)
{
    const int slices = 6;
    for (int s = 0; s < slices; ++s) {
        const float t0 = static_cast<float>(s) / static_cast<float>(slices);
        const float t1 = static_cast<float>(s + 1) / static_cast<float>(slices);
        const float x0 = t0 * length;
        const float x1 = t1 * length;
        const float y0 = std::sin(t0 * 3.14159265f) * bend * length;
        const float y1 = std::sin(t1 * 3.14159265f) * bend * length;
        const float r0 = radius * (1.0f - t0 * 0.3f);
        const float r1 = radius * (1.0f - t1 * 0.3f);

        const auto baseV = static_cast<std::uint32_t>(mesh.vertices.size());
        for (std::uint32_t i = 0; i < segs; ++i) {
            const float a = (static_cast<float>(i) / static_cast<float>(segs)) * 6.283185307f;
            assets::MeshVertex v;
            v.position[0] = x0;
            v.position[1] = y0 + std::cos(a) * r0;
            v.position[2] = std::sin(a) * r0;
            v.uv[0] = t0;
            v.uv[1] = static_cast<float>(i) / static_cast<float>(segs);
            mesh.vertices.push_back(v);
        }
        for (std::uint32_t i = 0; i < segs; ++i) {
            const float a = (static_cast<float>(i) / static_cast<float>(segs)) * 6.283185307f;
            assets::MeshVertex v;
            v.position[0] = x1;
            v.position[1] = y1 + std::cos(a) * r1;
            v.position[2] = std::sin(a) * r1;
            v.uv[0] = t1;
            v.uv[1] = static_cast<float>(i) / static_cast<float>(segs);
            mesh.vertices.push_back(v);
        }
        for (std::uint32_t i = 0; i < segs; ++i) {
            const auto b0 = baseV + i, b1 = baseV + (i + 1u) % segs;
            const auto t0 = baseV + segs + i, t1 = baseV + segs + (i + 1u) % segs;
            mesh.indices.push_back(b0);
            mesh.indices.push_back(t0);
            mesh.indices.push_back(b1);
            mesh.indices.push_back(b1);
            mesh.indices.push_back(t0);
            mesh.indices.push_back(t1);
        }
    }
}

/// Slab rock: flat, angular polyhedron for cliff/rock faces.
inline void appendSlabRock(assets::MeshAsset& mesh, float size, std::uint32_t verts = 8u)
{
    arp::core::pgtex::TextureRng rng(static_cast<uint32_t>(mesh.vertices.size()));
    const float h = size * (0.15f + rng.next() * 0.3f);
    const auto baseV = static_cast<std::uint32_t>(mesh.vertices.size());

    // Bottom face center
    assets::MeshVertex cv;
    cv.position[0] = 0.0f;
    cv.position[1] = 0.0f;
    cv.position[2] = 0.0f;
    cv.normal[0] = 0.0f;
    cv.normal[1] = -1.0f;
    cv.normal[2] = 0.0f;
    cv.uv[0] = 0.5f;
    cv.uv[1] = 0.5f;
    mesh.vertices.push_back(cv);

    // Bottom ring
    for (std::uint32_t i = 0; i < verts; ++i) {
        const float a = (static_cast<float>(i) / static_cast<float>(verts)) * 6.283185307f;
        const float r = size * (0.7f + rng.next() * 0.5f);
        assets::MeshVertex v;
        v.position[0] = std::cos(a) * r + (rng.next() - 0.5f) * size * 0.15f;
        v.position[1] = 0.0f;
        v.position[2] = std::sin(a) * r + (rng.next() - 0.5f) * size * 0.15f;
        v.uv[0] = static_cast<float>(i) / static_cast<float>(verts);
        v.uv[1] = 0.0f;
        mesh.vertices.push_back(v);
    }
    // Bottom triangles
    for (std::uint32_t i = 0; i < verts; ++i) {
        auto a = baseV, b = baseV + 1u + i, c = baseV + 1u + (i + 1u) % verts;
        mesh.indices.push_back(a);
        mesh.indices.push_back(c);
        mesh.indices.push_back(b);
    }

    // Top face center
    const auto topC = static_cast<std::uint32_t>(mesh.vertices.size());
    assets::MeshVertex tv;
    tv.position[0] = 0.0f;
    tv.position[1] = h;
    tv.position[2] = 0.0f;
    tv.normal[0] = 0.0f;
    tv.normal[1] = 1.0f;
    tv.normal[2] = 0.0f;
    tv.uv[0] = 0.5f;
    tv.uv[1] = 0.5f;
    mesh.vertices.push_back(tv);

    // Top ring (same XY offsets as bottom, but at Y=h)
    for (std::uint32_t i = 0; i < verts; ++i) {
        const auto& bv = mesh.vertices[baseV + 1u + i];
        assets::MeshVertex v = bv;
        v.position[1] = h;
        v.normal[0] = 0.0f;
        v.normal[1] = 1.0f;
        v.normal[2] = 0.0f;
        mesh.vertices.push_back(v);
    }
    // Top triangles
    for (std::uint32_t i = 0; i < verts; ++i) {
        mesh.indices.push_back(topC);
        mesh.indices.push_back(topC + 1u + i);
        mesh.indices.push_back(topC + 1u + (i + 1u) % verts);
    }
    // Side quads (connect bottom ring to top ring)
    for (std::uint32_t i = 0; i < verts; ++i) {
        auto b0 = baseV + 1u + i, b1 = baseV + 1u + (i + 1u) % verts;
        auto t0 = topC + 1u + i, t1 = topC + 1u + (i + 1u) % verts;
        mesh.indices.push_back(b0);
        mesh.indices.push_back(t0);
        mesh.indices.push_back(b1);
        mesh.indices.push_back(b1);
        mesh.indices.push_back(t0);
        mesh.indices.push_back(t1);
    }
}

/// Flower: thin stem + circular head.
inline void appendFlower(
    assets::MeshAsset& mesh, float stemHeight, float headRadius, std::uint32_t segs = 6u)
{
    appendTruncatedCone(mesh, 0.05f, 0.03f, stemHeight, segs);
    const auto baseV = static_cast<std::uint32_t>(mesh.vertices.size());
    appendSphere(mesh, headRadius, segs, segs / 2u);
    for (std::uint32_t i = baseV; i < mesh.vertices.size(); ++i) {
        mesh.vertices[i].position[1] += stemHeight;
        mesh.vertices[i].position[0] *= 1.0f;
        mesh.vertices[i].position[2] *= 1.0f;
        mesh.vertices[i].position[1] *= 0.4f; // flatten
    }
}

/// Mushroom: thin stem + hemisphere cap with bottom ring.
inline void appendMushroom(
    assets::MeshAsset& mesh, float stemHeight, float capRadius, std::uint32_t segs = 8u)
{
    appendTruncatedCone(mesh, 0.06f, 0.04f, stemHeight, segs);
    const auto baseV = static_cast<std::uint32_t>(mesh.vertices.size());
    appendSphere(mesh, capRadius, segs * 2u, segs);
    for (std::uint32_t i = baseV; i < mesh.vertices.size(); ++i) {
        auto& v = mesh.vertices[i];
        v.position[1] += stemHeight - capRadius * 0.2f;
        // Squash top half
        if (v.position[1] > stemHeight)
            v.position[1] = stemHeight + (v.position[1] - stemHeight) * 0.7f;
    }
}

// ─── Architecture primitives (used by Phase 8) ──────────────────────────────

/// Simple box (AABB).
inline void appendBox(assets::MeshAsset& mesh, float w, float d, float h)
{
    const float hw = w * 0.5f, hd = d * 0.5f;
    const auto baseV = static_cast<std::uint32_t>(mesh.vertices.size());
    // 8 corners
    auto addV = [&](float x, float y, float z, float u, float v) {
        assets::MeshVertex mv;
        mv.position[0] = x;
        mv.position[1] = y;
        mv.position[2] = z;
        mv.normal[0] = (x > 0 ? 1.0f : (x < 0 ? -1.0f : 0.0f));
        mv.normal[1] = 0.0f;
        mv.normal[2] = (z > 0 ? 1.0f : (z < 0 ? -1.0f : 0.0f));
        mv.uv[0] = u;
        mv.uv[1] = v;
        mv.tangent[0] = 1;
        mv.tangent[1] = 0;
        mv.tangent[2] = 0;
        mv.tangent[3] = 1;
        mesh.vertices.push_back(mv);
    };
    // Bottom face
    addV(-hw, 0, -hd, 0, 1);
    addV(hw, 0, -hd, 1, 1);
    addV(hw, 0, hd, 1, 0);
    addV(-hw, 0, hd, 0, 0);
    // Top face
    addV(-hw, h, -hd, 0, 1);
    addV(hw, h, -hd, 1, 1);
    addV(hw, h, hd, 1, 0);
    addV(-hw, h, hd, 0, 0);

    auto quad = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        mesh.indices.push_back(baseV + a);
        mesh.indices.push_back(baseV + c);
        mesh.indices.push_back(baseV + b);
        mesh.indices.push_back(baseV + b);
        mesh.indices.push_back(baseV + c);
        mesh.indices.push_back(baseV + d);
    };
    quad(0, 1, 4, 5);
    quad(1, 2, 5, 6);
    quad(2, 3, 6, 7);
    quad(3, 0, 7, 4); // sides
    quad(0, 3, 1, 2); // bottom
    quad(4, 5, 7, 6); // top
}

/// Gable roof (triangular prism).
inline void appendGableRoof(assets::MeshAsset& mesh, float w, float d, float h, float overhang)
{
    const float hw = w * 0.5f + overhang, hd = d * 0.5f + overhang;
    const auto baseV = static_cast<std::uint32_t>(mesh.vertices.size());

    auto addV = [&](float x, float y, float z, float u, float v) {
        assets::MeshVertex mv;
        mv.position[0] = x;
        mv.position[1] = y;
        mv.position[2] = z;
        mv.normal[0] = (y > 0.01f) ? 0.0f : 0.0f;
        mv.normal[1] = (y > 0.01f) ? 0.707f : -1.0f;
        mv.normal[2] = (y > 0.01f && z > 0) ? 0.707f : 0.0f;
        mv.uv[0] = u;
        mv.uv[1] = v;
        mv.tangent[0] = 1;
        mv.tangent[1] = 0;
        mv.tangent[2] = 0;
        mv.tangent[3] = 1;
        mesh.vertices.push_back(mv);
    };

    const float peakH = h; // roof peak relative to eave
    // Eave corners (y=0)
    addV(-hw, 0, -hd, 0, 1);
    addV(hw, 0, -hd, 1, 1);
    addV(hw, 0, hd, 1, 0);
    addV(-hw, 0, hd, 0, 0);
    // Ridge (y=h, center)
    addV(0, peakH, -hd, 0.5f, 0.5f);
    addV(0, peakH, hd, 0.5f, 0.5f);

    auto tri = [&](uint32_t a, uint32_t b, uint32_t c) {
        mesh.indices.push_back(baseV + a);
        mesh.indices.push_back(baseV + c);
        mesh.indices.push_back(baseV + b);
    };
    // Front gable
    tri(0, 1, 4);
    tri(0, 4, 3);
    tri(3, 4, 5);
    tri(3, 5, 2);
    tri(2, 5, 1);
    tri(1, 5, 4);
    // Back gable
    // Simplification: just do two triangular faces
    mesh.indices.push_back(baseV + 0);
    mesh.indices.push_back(baseV + 4);
    mesh.indices.push_back(baseV + 1);
    mesh.indices.push_back(baseV + 1);
    mesh.indices.push_back(baseV + 4);
    mesh.indices.push_back(baseV + 5);
    mesh.indices.push_back(baseV + 1);
    mesh.indices.push_back(baseV + 5);
    mesh.indices.push_back(baseV + 2);
    mesh.indices.push_back(baseV + 2);
    mesh.indices.push_back(baseV + 5);
    mesh.indices.push_back(baseV + 3);
    mesh.indices.push_back(baseV + 3);
    mesh.indices.push_back(baseV + 5);
    mesh.indices.push_back(baseV + 4);
    mesh.indices.push_back(baseV + 3);
    mesh.indices.push_back(baseV + 4);
    mesh.indices.push_back(baseV + 0);
}

// ─── Road / path primitives ──────────────────────────────────────────────────

/// Road strip: a flat quad ribbon that follows a series of waypoints on the
/// XZ plane, draping onto the terrain via `heightSampler`.  Each waypoint
/// produces a left/right pair of vertices; consecutive pairs form two triangles.
///
/// \param heightSampler  world-space (x,z) -> terrain height (the road will
///                        sit `heightOffset` metres above it).
/// \param points          array of waypoints (world XZ).
/// \param pointCount      number of waypoints (>= 2 to produce any geometry).
/// \param width           road width in world units.
/// \param heightOffset    how far above the terrain the road surface floats
///                        (prevents z-fighting; 0.05-0.15 is typical).
inline void appendRoadStrip(assets::MeshAsset& mesh,
    const std::function<float(float, float)>& heightSampler,
    const float* points,
    std::size_t pointCount,
    float width,
    float heightOffset = 0.08f)
{
    if (pointCount < 2 || width <= 0.0f)
        return;
    const float hw = width * 0.5f;

    // Resample the polyline into ~0.75m segments so the road closely conforms
    // to terrain curvature even on bumpy ground.
    std::vector<float> dense; // x0,z0,x1,z1,...
    dense.reserve(pointCount * 2);
    for (std::size_t i = 0; i < pointCount; ++i) {
        const float x = points[i * 2];
        const float z = points[i * 2 + 1];
        if (i > 0) {
            const float px = points[(i - 1) * 2];
            const float pz = points[(i - 1) * 2 + 1];
            const float dx = x - px, dz = z - pz;
            const float segLen = std::sqrt(dx * dx + dz * dz);
            const int steps = std::max(1, static_cast<int>(segLen / 0.75f));
            for (int s = 1; s <= steps; ++s) {
                const float t = static_cast<float>(s) / static_cast<float>(steps);
                dense.push_back(px + dx * t);
                dense.push_back(pz + dz * t);
            }
        } else {
            dense.push_back(x);
            dense.push_back(z);
        }
    }
    const std::size_t numDense = dense.size() / 2;
    if (numDense < 2)
        return;

    // Build left/right vertex pairs along the dense polyline.
    float uvDist = 0.0f; // accumulated world distance for V coordinate
    for (std::size_t i = 0; i < numDense; ++i) {
        const float x = dense[i * 2];
        const float z = dense[i * 2 + 1];

        // Join the two neighbouring segments with a bounded miter instead of
        // simply offsetting the outgoing segment.  The latter makes the two
        // sides cross at a sharp turn: the resulting bow-tie quads overlap in
        // depth and show up as the characteristic black triangular fragments.
        float prevTx = 0.0f, prevTz = 0.0f;
        float nextTx = 0.0f, nextTz = 0.0f;
        if (i > 0) {
            prevTx = x - dense[(i - 1) * 2];
            prevTz = z - dense[(i - 1) * 2 + 1];
        }
        if (i + 1 < numDense) {
            nextTx = dense[(i + 1) * 2] - x;
            nextTz = dense[(i + 1) * 2 + 1] - z;
        }
        const float prevLen = std::sqrt(prevTx * prevTx + prevTz * prevTz);
        const float nextLen = std::sqrt(nextTx * nextTx + nextTz * nextTz);
        if (prevLen > 1e-6f) {
            prevTx /= prevLen;
            prevTz /= prevLen;
        }
        if (nextLen > 1e-6f) {
            nextTx /= nextLen;
            nextTz /= nextLen;
        }
        if (prevLen <= 1e-6f) {
            prevTx = nextTx;
            prevTz = nextTz;
        }
        if (nextLen <= 1e-6f) {
            nextTx = prevTx;
            nextTz = prevTz;
        }
        if (prevLen <= 1e-6f && nextLen <= 1e-6f) {
            prevTx = nextTx = 1.0f;
            prevTz = nextTz = 0.0f;
        }

        float tx = prevTx + nextTx;
        float tz = prevTz + nextTz;
        const float tLen = std::sqrt(tx * tx + tz * tz);
        if (tLen < 1e-6f) {
            tx = nextTx;
            tz = nextTz;
        } else {
            tx /= tLen;
            tz /= tLen;
        }

        const float prevPx = -prevTz, prevPz = prevTx;
        const float nextPx = -nextTz, nextPz = nextTx;
        float px = prevPx + nextPx;
        float pz = prevPz + nextPz;
        const float pLen = std::sqrt(px * px + pz * pz);
        if (pLen < 1e-6f) {
            px = nextPx;
            pz = nextPz;
        } else {
            px /= pLen;
            pz /= pLen;
        }

        // Cap the miter at twice the half-width.  This preserves a clean
        // join for normal corners while keeping near U-turns from producing
        // very long spikes or folding the ribbon over itself.
        const float denom = std::fabs(px * nextPx + pz * nextPz);
        const float miterScale = std::min(hw / std::max(denom, 0.5f), hw * 2.0f);

        // Sample terrain height independently at each vertex position so the
        // road conforms to cross-slope terrain instead of floating above or
        // sinking into it.
        const float lx = x + px * miterScale, lz = z + pz * miterScale;
        const float rx = x - px * miterScale, rz = z - pz * miterScale;
        const float yL = heightSampler ? heightSampler(lx, lz) + heightOffset : heightOffset;
        const float yR = heightSampler ? heightSampler(rx, rz) + heightOffset : heightOffset;

        // Compute terrain normal at each vertex via finite differences so
        // lighting matches the slope the road is draped over.
        float nlx = 0.0f, nly = 1.0f, nlz = 0.0f;
        float nrx = 0.0f, nry = 1.0f, nrz = 0.0f;
        if (heightSampler) {
            const float eps = 0.5f;
            // Left vertex normal
            {
                const float hc = heightSampler(lx, lz);
                const float hx = heightSampler(lx + eps, lz);
                const float hz = heightSampler(lx, lz + eps);
                float nx = -(hx - hc) / eps;
                float nz = -(hz - hc) / eps;
                float ny = 1.0f;
                const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 0.0001f) {
                    nx /= len;
                    ny /= len;
                    nz /= len;
                }
                nlx = nx;
                nly = ny;
                nlz = nz;
            }
            // Right vertex normal
            {
                const float hc = heightSampler(rx, rz);
                const float hx = heightSampler(rx + eps, rz);
                const float hz = heightSampler(rx, rz + eps);
                float nx = -(hx - hc) / eps;
                float nz = -(hz - hc) / eps;
                float ny = 1.0f;
                const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 0.0001f) {
                    nx /= len;
                    ny /= len;
                    nz /= len;
                }
                nrx = nx;
                nry = ny;
                nrz = nz;
            }
        }

        // Left vertex
        assets::MeshVertex vl;
        vl.position[0] = lx;
        vl.position[1] = yL;
        vl.position[2] = lz;
        vl.normal[0] = nlx;
        vl.normal[1] = nly;
        vl.normal[2] = nlz;
        vl.uv[0] = 0.0f;
        vl.uv[1] = uvDist / 4.0f;
        vl.tangent[0] = tx;
        vl.tangent[1] = 0.0f;
        vl.tangent[2] = tz;
        vl.tangent[3] = 1.0f;
        mesh.vertices.push_back(vl);

        // Right vertex
        assets::MeshVertex vr;
        vr.position[0] = rx;
        vr.position[1] = yR;
        vr.position[2] = rz;
        vr.normal[0] = nrx;
        vr.normal[1] = nry;
        vr.normal[2] = nrz;
        vr.uv[0] = 1.0f;
        vr.uv[1] = uvDist / 4.0f;
        vr.tangent[0] = tx;
        vr.tangent[1] = 0.0f;
        vr.tangent[2] = tz;
        vr.tangent[3] = 1.0f;
        mesh.vertices.push_back(vr);

        // Advance V by segment length.
        if (i + 1 < numDense) {
            const float ndx = dense[(i + 1) * 2] - x;
            const float ndz = dense[(i + 1) * 2 + 1] - z;
            uvDist += std::sqrt(ndx * ndx + ndz * ndz);
        }
    }

    // Two triangles per quad.
    const auto baseV = static_cast<std::uint32_t>(mesh.vertices.size() - numDense * 2);
    for (std::size_t i = 0; i + 1 < numDense; ++i) {
        const auto l0 = baseV + static_cast<std::uint32_t>(i * 2);
        const auto r0 = l0 + 1u;
        const auto l1 = l0 + 2u;
        const auto r1 = l0 + 3u;
        // Mesh coordinates are +Y up.  The projection used by the PBR path
        // inverts raster Y, so a geometrically counter-clockwise top face is
        // the pipeline's clockwise front face.  The opposite winding reaches
        // normalFromTexture as a back face and is deliberately lit with its
        // normal inverted, which turns an otherwise valid road nearly black.
        mesh.indices.push_back(l0);
        mesh.indices.push_back(l1);
        mesh.indices.push_back(r0);
        mesh.indices.push_back(r0);
        mesh.indices.push_back(l1);
        mesh.indices.push_back(r1);
    }
}

} // namespace arp::core
