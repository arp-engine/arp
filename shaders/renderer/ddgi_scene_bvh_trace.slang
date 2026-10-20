// Shared production BVH traversal; bindings, gDdgi and materialAlbedo are
// supplied by the including shader. Native contract fixtures use this exact code.

bool hasSceneBvhGeometry()
{
    return gDdgi.sceneBvhParams.x > 0.0 || gDdgi.sceneBvhParams.z > 0.0;
}

bool rayAabbIntersect(
    float3 origin, float3 invDir, float3 aabbMin, float3 aabbMax,
    float maxDist, out float tNear)
{
    // Initialize defensively (caller may read tNear on a false return).
    tNear = 0.0;
    float3 t1 = (aabbMin - origin) * invDir;
    float3 t2 = (aabbMax - origin) * invDir;
    float3 tMin = min(t1, t2);
    float3 tMax = max(t1, t2);
    float tEnter = max(max(tMin.x, tMin.y), tMin.z);
    float tExit  = min(min(tMax.x, tMax.y), tMax.z);
    // Ray hits the slab if the entry is before the exit AND the exit is
    // within the ray's forward range. Clamp the entry to >= 0 (rays that
    // originate inside the AABB still hit at t=0).
    tNear = max(tEnter, 0.0);
    return tExit >= tNear && tNear <= maxDist;
}

// M枚ller-Trumbore ray-triangle intersection. Returns true and sets t to the
// hit distance (>= 0) and normal to the triangle face normal when the ray
// intersects triangle (v0, v1, v2) within [0, maxDist]. Backface culling is
// disabled (DDGI probes sample both sides of thin geometry like walls).
bool rayTriangleIntersect(
    float3 origin, float3 dir, float3 v0, float3 v1, float3 v2,
    float maxDist, out float t, out float3 normal, out float2 barycentric)
{
    // Initialize out params defensively (early-return paths below).
    t = -1.0;
    normal = float3(0.0, 1.0, 0.0);
    barycentric = float2(0.0, 0.0);
    float3 e1 = v1 - v0;
    float3 e2 = v2 - v0;
    float3 pvec = cross(dir, e2);
    float det = dot(e1, pvec);
    // Parallel ray -> no hit. Use an epsilon to avoid denormal blowup.
    if (abs(det) < 1e-6) return false;
    float invDet = 1.0 / det;
    float3 tvec = origin - v0;
    float u = dot(tvec, pvec) * invDet;
    if (u < 0.0 || u > 1.0) return false;
    float3 qvec = cross(tvec, e1);
    float v = dot(dir, qvec) * invDet;
    if (v < 0.0 || (u + v) > 1.0) return false;
    t = dot(e2, qvec) * invDet;
    if (t < 0.0 || t > maxDist) return false;
    barycentric = float2(u, v);
    // Face normal (not interpolated - DDGI does not need per-pixel normals).
    normal = normalize(cross(e1, e2));
    // Flip the normal toward the ray origin so backface hits report the
    // side the ray came from (avoids negative cosine lighting).
    if (dot(normal, dir) > 0.0) normal = -normal;
    return true;
}

struct SceneRayHit {
    float  hitDist;       // > 0 = hit, <= 0 = miss (sky)
    float3 hitNormal;
    float3 albedo;
    float3 emissive;
    uint   materialIdx;
    bool   backFacing;
};

GpuBvhNode loadSceneBvhNode(uint treeKind, uint nodeIndex)
{
    if (treeKind == 0u) return SceneBvhNodes[nodeIndex];
    return DynamicTlasNodes[nodeIndex];
}

GpuBvhLeaf loadSceneBvhLeaf(uint treeKind, uint leafIndex)
{
    if (treeKind == 0u) return SceneBvhLeaves[leafIndex];
    return DynamicTlasLeaves[leafIndex];
}

float4 loadSceneBvhEmissive(uint treeKind, uint leafIndex)
{
    if (treeKind == 0u) return SceneBvhEmissive[leafIndex];
    return DynamicTlasEmissive[leafIndex];
}

uint ddgiStaticInstanceCount()
{
    // The first instance record's stableId lane is reserved by the CPU
    // uploader as a bounded instance-count metadata word. A zero value is
    // the legacy identity-table representation.
    const uint encodedCount = asuint(SceneBvhInstances[8u].x);
    if (encodedCount == 0u) return 1u;
    return min(encodedCount, 64u);
}

float4x4 loadDdgiInstanceMatrix(uint instanceIndex, bool localFromWorld)
{
    const uint base = instanceIndex * 9u;
    const uint matrixBase = localFromWorld ? base + 4u : base;
    return float4x4(
        SceneBvhInstances[matrixBase + 0u],
        SceneBvhInstances[matrixBase + 1u],
        SceneBvhInstances[matrixBase + 2u],
        SceneBvhInstances[matrixBase + 3u]);
}

// Iterative BVH traversal with a fixed-size stack (32 entries). The BVH is
// built with median-split down to one triangle per leaf, so the traversal
// pushes internal-node children and tests leaf triangles directly. Tracks
// the nearest hit and prunes subtrees whose AABB entry is beyond the
// current best hit (standard BVH ray-traversal optimization).
SceneRayHit traceSceneBvhTree(
    float3 origin, float3 dir, float maxDist, uint treeKind)
{
    SceneRayHit hit;
    hit.hitDist = -1.0;
    hit.hitNormal = float3(0.0, 1.0, 0.0);
    hit.albedo = float3(0.0, 0.0, 0.0);
    hit.emissive = float3(0.0, 0.0, 0.0);
    hit.materialIdx = 0u;
    hit.backFacing = false;

    uint triangleCount = treeKind == 0u
        ? uint(gDdgi.sceneBvhParams.x)
        : uint(gDdgi.sceneBvhParams.z);
    uint nodeCount = treeKind == 0u
        ? uint(gDdgi.sceneBvhParams.y)
        : uint(gDdgi.sceneBvhParams.w);
    if (triangleCount == 0u || nodeCount == 0u) {
        return hit; // BVH empty -> caller falls back to marchTerrain
    }

    // Precompute inverse direction (avoid per-node division). Clamp each
    // component to a tiny epsilon to avoid Inf for axis-aligned rays. This
    // produces a very large (but finite) t value for the degenerate axis,
    // which the slab test correctly treats as "no constraint on that axis".
    float3 safeDir = float3(
        abs(dir.x) > 1e-20 ? dir.x : (dir.x >= 0.0 ? 1e-20 : -1e-20),
        abs(dir.y) > 1e-20 ? dir.y : (dir.y >= 0.0 ? 1e-20 : -1e-20),
        abs(dir.z) > 1e-20 ? dir.z : (dir.z >= 0.0 ? 1e-20 : -1e-20));
    float3 invDir = 1.0 / safeDir;

    // Fixed-size traversal stack. 32 entries covers BVH depths up to 32,
    // which is well beyond the ~17-deep tree a 100k-triangle scene produces
    // (log2(100k) ~= 17). Overflow is guarded by saturating the stack top.
    const uint kStackDepth = 32u;
    uint stack[kStackDepth];
    int stackTop = 0;
    stack[0u] = 0u; // root

    float bestT = maxDist;

    while (stackTop >= 0) {
        uint nodeIdx = stack[uint(stackTop)];
        stackTop -= 1;

        GpuBvhNode node = loadSceneBvhNode(treeKind, nodeIdx);
        float tNear;
        // Prune: AABB entry beyond current best hit -> skip subtree.
        if (!rayAabbIntersect(origin, invDir, node.aabbMin, node.aabbMax, bestT, tNear)) {
            continue;
        }

        if (node.leftChild == kBvhInvalidLeafMarker) {
            // Leaf: test exactly one primitive (BVH builder guarantees
            // leafCount == 1; rightChild encodes the leaf index).
            GpuBvhLeaf tri = loadSceneBvhLeaf(treeKind, node.rightChild);
            float t;
            float3 triNormal;
            float2 triBarycentric = float2(0.0, 0.0);
            bool didHit = false;
            if (tri.materialIdx >= 0x80000000u) {
                // P6-B: Proxy AABB leaf. v0 = min, v1 = max. Use the
                // slab-method ray-AABB test instead of ray-triangle.
                if (rayAabbIntersect(origin, invDir, tri.v0, tri.v1,
                                     bestT, t)) {
                    didHit = true;
                    // Approximate normal: dominant axis of (hitPoint - center).
                    // DDGI is diffuse indirect light, so normal precision is
                    // not critical - a face-aligned normal is sufficient.
                    float3 center = (tri.v0 + tri.v1) * 0.5;
                    float3 d = (origin + dir * t) - center;
                    float3 ad = abs(d);
                    if (ad.x > ad.y && ad.x > ad.z) {
                        triNormal = float3(d.x > 0.0 ? 1.0 : -1.0, 0.0, 0.0);
                    } else if (ad.y > ad.z) {
                        triNormal = float3(0.0, d.y > 0.0 ? 1.0 : -1.0, 0.0);
                    } else {
                        triNormal = float3(0.0, 0.0, d.z > 0.0 ? 1.0 : -1.0);
                    }
                }
            } else {
                // Real triangle: Moller-Trumbore ray-triangle test.
                if (rayTriangleIntersect(origin, dir, tri.v0, tri.v1, tri.v2,
                                         bestT, t, triNormal, triBarycentric)) {
                    didHit = true;
                }
            }
            if (didHit) {
                const float hitW = 1.0 - triBarycentric.x - triBarycentric.y;
                const float2 hitUv = tri.uv0 * hitW
                    + tri.uv1 * triBarycentric.x + tri.uv2 * triBarycentric.y;
                if (ddgiUsesSceneMaterial(tri.materialIdx)
                    && !ddgiSceneMaterialVisible(tri.materialIdx,
                        ddgiSceneMaterialColor(tri.materialIdx, hitUv).a)) continue;
                bestT = t;
                hit.hitDist = t;
                hit.hitNormal = triNormal;
                hit.backFacing = tri.materialIdx < 0x80000000u
                    && dot(cross(tri.v1-tri.v0, tri.v2-tri.v0), dir) > 0.0;
                hit.materialIdx = tri.materialIdx;
                const float3 hitPosition = origin + dir * t;
                hit.albedo = materialAlbedo(tri.materialIdx, hitPosition, hitUv);
                hit.emissive = max(
                    loadSceneBvhEmissive(treeKind, node.rightChild).rgb,
                    0.0.xxx);
            }
        } else {
            // Internal: push both children (push right first so left is
            // popped first - left-to-right traversal matches the CPU build
            // order and improves cache locality on the GPU).
            if (stackTop + 1 < int(kStackDepth)) {
                stackTop += 1;
                stack[uint(stackTop)] = node.rightChild;
            }
            if (stackTop + 1 < int(kStackDepth)) {
                stackTop += 1;
                stack[uint(stackTop)] = node.leftChild;
            }
        }
    }

    return hit;
}

// Static geometry is a persistent local-space BLAS. The instance table is a
// small bounded TLAS contract: each instance transforms the world ray into
// BLAS space, then transforms the nearest hit back into world space. Dynamic
// proxy geometry keeps its dedicated world-space TLAS below.
SceneRayHit traceSceneBvhStaticInstances(
    float3 origin, float3 dir, float maxDist)
{
    SceneRayHit nearest;
    nearest.hitDist = -1.0;
    nearest.hitNormal = float3(0.0, 1.0, 0.0);
    nearest.albedo = float3(0.0, 0.0, 0.0);
    nearest.emissive = float3(0.0, 0.0, 0.0);
    nearest.materialIdx = 0u;
    nearest.backFacing = false;
    float bestWorldT = maxDist;
    const uint instanceCount = ddgiStaticInstanceCount();
    for (uint instanceIndex = 0u; instanceIndex < instanceCount; ++instanceIndex) {
        const float4x4 localFromWorld = loadDdgiInstanceMatrix(instanceIndex, true);
        const float4x4 worldFromLocal = loadDdgiInstanceMatrix(instanceIndex, false);
        const float3 localOrigin = mul(
            localFromWorld, float4(origin, 1.0)).xyz;
        const float3 localDirectionRaw = mul(
            localFromWorld, float4(dir, 0.0)).xyz;
        const float localDirectionLength = length(localDirectionRaw);
        if (localDirectionLength <= 1.0e-6) continue;
        const float3 localDirection = localDirectionRaw / localDirectionLength;
        // Normalizing the transformed direction changes the ray parameter
        // by its length. Preserve the exact world interval under any scale.
        const SceneRayHit candidate = traceSceneBvhTree(
            localOrigin, localDirection, bestWorldT * localDirectionLength, 0u);
        if (candidate.hitDist <= 0.0) continue;
        const float3 localPosition = localOrigin
            + localDirection * candidate.hitDist;
        const float3 worldPosition = mul(
            worldFromLocal, float4(localPosition, 1.0)).xyz;
        const float worldT = dot(worldPosition - origin, dir);
        if (worldT <= 0.0 || worldT > bestWorldT) continue;
        bestWorldT = worldT;
        nearest = candidate;
        nearest.hitDist = worldT;
        nearest.hitNormal = normalize(mul(
            transpose(localFromWorld), float4(candidate.hitNormal, 0.0)).xyz);
    }
    return nearest;
}

SceneRayHit traceSceneBvh(float3 origin, float3 dir, float maxDist)
{
    SceneRayHit staticHit = traceSceneBvhStaticInstances(origin, dir, maxDist);
    if (gDdgi.sceneBvhParams.z <= 0.0) return staticHit;
    SceneRayHit dynamicHit = traceSceneBvhTree(origin, dir, maxDist, 1u);
    if (dynamicHit.hitDist > 0.0
        && (staticHit.hitDist <= 0.0 || dynamicHit.hitDist < staticHit.hitDist)) {
        return dynamicHit;
    }
    return staticHit;
}
