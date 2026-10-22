#pragma once

// Camera frustum extraction and intersection tests.
//
// Provides `Frustum` - 6 clip planes extracted from a view-projection
// matrix via the Gribb-Hartmann method. Supports AABB and sphere
// intersection tests used by the BVH query system.
//
// Matrix convention: WoccPbrPushConstants::viewProjection is stored in
// column-major layout (the transpose of core::multiply(proj, view)), which
// the shader consumes via mul(M, v) to compute clip = M · v. Use
// fromColumnMajorMatrix() for that buffer. fromRowMajorMatrix() is provided
// for matrices stored row-major (m[row*4+col] = M[row][col]).
//
// Depth range: [0, 1] (matching core::perspective()).
//   Near plane = row2 (clip.z >= 0)
//   Far plane  = row3 - row2 (clip.w - clip.z >= 0)

#include "core/math/math.h"

#include <cstdint>

namespace arp::core {

struct Frustum {
    // 6 clip planes in the order: left, right, bottom, top, near, far.
    // Each plane (a, b, c, d): a*px + b*py + c*pz + d >= 0 means the
    // point is inside (or on the boundary of) the frustum.
    // Planes are NOT normalized (no sqrt); this is fine for boolean
    // inside/outside tests but means the .w component is not a true
    // distance. Normalize if you need exact distances.
    Vec4 planes[6];

    /// Extract 6 frustum planes from a column-major VP matrix (clip = M · v).
    /// Column-major means m[col*4+row] = M[row][col]; this is the layout used
    /// by WoccPbrPushConstants::viewProjection.
    /// Uses the Gribb-Hartmann row-addition method on the rows of M.
    /// @param m  16 floats, column-major: m[col*4+row] = M[row][col].
    static Frustum fromColumnMajorMatrix(const float m[16])
    {
        // Rows of M, recovered from column-major storage:
        // row_i = (m[i], m[i+4], m[i+8], m[i+12])  i.e. stride-4 access.
        const Vec4 row0{m[0], m[4], m[8], m[12]};
        const Vec4 row1{m[1], m[5], m[9], m[13]};
        const Vec4 row2{m[2], m[6], m[10], m[14]};
        const Vec4 row3{m[3], m[7], m[11], m[15]};
        return fromRows(row0, row1, row2, row3);
    }

    /// Extract 6 frustum planes from a row-major VP matrix (clip = M · v).
    /// Row-major means m[row*4+col] = M[row][col].
    /// @param m  16 floats, row-major: m[row*4+col] = M[row][col].
    static Frustum fromRowMajorMatrix(const float m[16])
    {
        // Rows of M, recovered from row-major storage:
        // row_i = (m[i*4], m[i*4+1], m[i*4+2], m[i*4+3])  contiguous access.
        const Vec4 row0{m[0], m[1], m[2], m[3]};
        const Vec4 row1{m[4], m[5], m[6], m[7]};
        const Vec4 row2{m[8], m[9], m[10], m[11]};
        const Vec4 row3{m[12], m[13], m[14], m[15]};
        return fromRows(row0, row1, row2, row3);
    }

private:
    /// Build the frustum from the 4 rows of a VP matrix where clip = M · v.
    /// Inside the frustum (after perspective divide):
    ///   -clip.w <= clip.x <= clip.w  ->  clip.w+clip.x >= 0 (left),
    ///                                    clip.w-clip.x >= 0 (right)
    ///   -clip.w <= clip.y <= clip.w  ->  clip.w+clip.y >= 0 (bottom),
    ///                                    clip.w-clip.y >= 0 (top)
    ///   0 <= clip.z <= clip.w        ->  clip.z >= 0 (near, [0,1] depth),
    ///                                    clip.w-clip.z >= 0 (far)
    static Frustum fromRows(Vec4 row0, Vec4 row1, Vec4 row2, Vec4 row3)
    {
        Frustum f;
        f.planes[0] = row3 + row0; // Left:   clip.w + clip.x >= 0
        f.planes[1] = row3 - row0; // Right:  clip.w - clip.x >= 0
        f.planes[2] = row3 + row1; // Bottom: clip.w + clip.y >= 0
        f.planes[3] = row3 - row1; // Top:    clip.w - clip.y >= 0
        f.planes[4] = row2;        // Near:   clip.z >= 0  ([0,1] depth)
        f.planes[5] = row3 - row2; // Far:    clip.w - clip.z >= 0
        return f;
    }

public:
    /// Test whether a point is inside all 6 frustum planes.
    bool intersectsPoint(Vec3 p) const
    {
        for (int i = 0; i < 6; ++i) {
            const Vec4& pl = planes[i];
            if (pl.x * p.x + pl.y * p.y + pl.z * p.z + pl.w < 0.0f)
                return false;
        }
        return true;
    }

    /// Test whether a sphere (center + radius) intersects the frustum.
    /// Conservative: reports visible if the sphere is not entirely
    /// outside any single plane.
    bool intersectsSphere(Vec3 center, float radius) const
    {
        for (int i = 0; i < 6; ++i) {
            const Vec4& pl = planes[i];
            const float dist = pl.x * center.x + pl.y * center.y + pl.z * center.z + pl.w;
            if (dist < -radius)
                return false; // sphere is entirely behind this plane
        }
        return true;
    }

    /// Test whether an AABB (min/max corners) intersects the frustum.
    /// Uses the p-vertex/n-vertex optimization: for each plane, only the
    /// corner of the AABB that is most aligned with the plane normal (the
    /// "positive vertex") needs to be tested. If that corner is outside,
    /// the entire box is outside that plane.
    ///
    /// @param minBounds  AABB min corner (minX, minY, minZ).
    /// @param maxBounds  AABB max corner (maxX, maxY, maxZ).
    bool intersectsAabbMinMax(Vec3 minBounds, Vec3 maxBounds) const
    {
        for (int i = 0; i < 6; ++i) {
            const Vec4& pl = planes[i];
            // p-vertex: the AABB corner farthest in the plane-normal direction.
            // For each axis, pick max if normal > 0, else min.
            const float px = (pl.x >= 0.0f) ? maxBounds.x : minBounds.x;
            const float py = (pl.y >= 0.0f) ? maxBounds.y : minBounds.y;
            const float pz = (pl.z >= 0.0f) ? maxBounds.z : minBounds.z;
            const float dist = pl.x * px + pl.y * py + pl.z * pz + pl.w;
            if (dist < 0.0f)
                return false; // p-vertex is outside this plane -> AABB fully outside
        }
        return true;
    }
};

} // namespace arp::core
