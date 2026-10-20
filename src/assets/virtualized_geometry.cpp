#include "assets/virtualized_geometry.h"

#include "core/diagnostic_builder.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace arp::assets {

namespace {

uint64_t hashValue(uint64_t hash, uint64_t value)
{
    constexpr uint64_t prime = 1099511628211ull;
    for (uint32_t byte = 0; byte < 8; ++byte) {
        hash ^= static_cast<unsigned char>((value >> (byte * 8u)) & 0xffu);
        hash *= prime;
    }
    return hash;
}

uint64_t hashFloat(uint64_t hash, float value)
{
    const auto scaled = static_cast<int64_t>(std::round(value * 1000.0f));
    return hashValue(hash, static_cast<uint64_t>(scaled));
}

bool hasVertex(const std::pmr::vector<uint32_t>& vertices, uint32_t vertex)
{
    return std::find(vertices.begin(), vertices.end(), vertex) != vertices.end();
}

uint32_t appendTriangleVertices(
    std::pmr::vector<uint32_t>& vertices, const TriangleAdjacencyBuilder::TriangleRecord& triangle)
{
    uint32_t added = 0;
    for (const auto vertex : triangle.indices) {
        if (!hasVertex(vertices, vertex)) {
            vertices.push_back(vertex);
            ++added;
        }
    }
    return added;
}

bool canAddTriangle(const std::pmr::vector<uint32_t>& vertices,
    const TriangleAdjacencyBuilder::TriangleRecord& triangle,
    uint32_t maxVertices)
{
    auto unique = vertices;
    appendTriangleVertices(unique, triangle);
    return unique.size() <= maxVertices;
}

bool shareEdge(const TriangleAdjacencyBuilder::TriangleRecord& lhs,
    const TriangleAdjacencyBuilder::TriangleRecord& rhs)
{
    uint32_t sharedVertices = 0;
    for (const auto leftIndex : lhs.indices) {
        for (const auto rightIndex : rhs.indices) {
            if (leftIndex == rightIndex) {
                ++sharedVertices;
            }
        }
    }
    return sharedVertices >= 2;
}

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

Vec3 operator+(Vec3 lhs, Vec3 rhs)
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Vec3 operator-(Vec3 lhs, Vec3 rhs)
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3 operator*(Vec3 value, double scale)
{
    return {value.x * scale, value.y * scale, value.z * scale};
}

double dot(Vec3 lhs, Vec3 rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Vec3 cross(Vec3 lhs, Vec3 rhs)
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

double length(Vec3 value)
{
    return std::sqrt(dot(value, value));
}

Vec3 normalize(Vec3 value)
{
    const auto len = length(value);
    if (len <= 0.0000001) {
        return {};
    }
    return value * (1.0 / len);
}

Vec3 meshPosition(const MeshVertex& vertex)
{
    return {
        vertex.position[0],
        vertex.position[1],
        vertex.position[2],
    };
}

void setMeshPosition(MeshVertex& vertex, Vec3 position)
{
    vertex.position[0] = static_cast<float>(position.x);
    vertex.position[1] = static_cast<float>(position.y);
    vertex.position[2] = static_cast<float>(position.z);
}

struct Quadric {
    double m00 = 0.0;
    double m01 = 0.0;
    double m02 = 0.0;
    double m03 = 0.0;
    double m11 = 0.0;
    double m12 = 0.0;
    double m13 = 0.0;
    double m22 = 0.0;
    double m23 = 0.0;
    double m33 = 0.0;

    void add(const Quadric& other)
    {
        m00 += other.m00;
        m01 += other.m01;
        m02 += other.m02;
        m03 += other.m03;
        m11 += other.m11;
        m12 += other.m12;
        m13 += other.m13;
        m22 += other.m22;
        m23 += other.m23;
        m33 += other.m33;
    }

    void addPlane(double a, double b, double c, double d, double weight)
    {
        m00 += weight * a * a;
        m01 += weight * a * b;
        m02 += weight * a * c;
        m03 += weight * a * d;
        m11 += weight * b * b;
        m12 += weight * b * c;
        m13 += weight * b * d;
        m22 += weight * c * c;
        m23 += weight * c * d;
        m33 += weight * d * d;
    }

    double evaluate(Vec3 position) const
    {
        const auto x = position.x;
        const auto y = position.y;
        const auto z = position.z;
        return m00 * x * x + 2.0 * m01 * x * y + 2.0 * m02 * x * z + 2.0 * m03 * x + m11 * y * y
            + 2.0 * m12 * y * z + 2.0 * m13 * y + m22 * z * z + 2.0 * m23 * z + m33;
    }
};

Quadric combined(Quadric lhs, const Quadric& rhs)
{
    lhs.add(rhs);
    return lhs;
}

bool addTrianglePlane(Quadric& quadric, Vec3 p0, Vec3 p1, Vec3 p2, double weight)
{
    const auto normal = normalize(cross(p1 - p0, p2 - p0));
    if (length(normal) <= 0.0000001) {
        return false;
    }
    const auto d = -dot(normal, p0);
    quadric.addPlane(normal.x, normal.y, normal.z, d, weight);
    return true;
}

bool solveOptimalPosition(const Quadric& quadric, Vec3& out)
{
    const auto a00 = quadric.m00;
    const auto a01 = quadric.m01;
    const auto a02 = quadric.m02;
    const auto a10 = quadric.m01;
    const auto a11 = quadric.m11;
    const auto a12 = quadric.m12;
    const auto a20 = quadric.m02;
    const auto a21 = quadric.m12;
    const auto a22 = quadric.m22;
    const auto b0 = -quadric.m03;
    const auto b1 = -quadric.m13;
    const auto b2 = -quadric.m23;
    const auto det = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20)
        + a02 * (a10 * a21 - a11 * a20);
    if (std::fabs(det) <= 0.00000001) {
        return false;
    }

    const auto detX =
        b0 * (a11 * a22 - a12 * a21) - a01 * (b1 * a22 - a12 * b2) + a02 * (b1 * a21 - a11 * b2);
    const auto detY =
        a00 * (b1 * a22 - a12 * b2) - b0 * (a10 * a22 - a12 * a20) + a02 * (a10 * b2 - b1 * a20);
    const auto detZ =
        a00 * (a11 * b2 - b1 * a21) - a01 * (a10 * b2 - b1 * a20) + b0 * (a10 * a21 - a11 * a20);
    out = {detX / det, detY / det, detZ / det};
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

uint32_t targetTriangleCount(
    uint32_t sourceTriangleCount, const VirtualizedGeometryBuildPolicy& policy)
{
    auto target = static_cast<uint64_t>(sourceTriangleCount)
        * static_cast<uint64_t>(policy.simplificationTargetNumerator);
    target = (target + policy.simplificationTargetDenominator - 1u)
        / policy.simplificationTargetDenominator;
    target = std::max<uint64_t>(1u, target);
    target = std::min<uint64_t>(target, sourceTriangleCount);
    return static_cast<uint32_t>(target);
}

ClusterBounds boundsForTriangles(
    const MeshAsset& mesh, const std::pmr::vector<uint32_t>& triangleIndices)
{
    ClusterBounds bounds;
    if (triangleIndices.empty()) {
        return bounds;
    }

    for (auto axis = 0; axis < 3; ++axis) {
        bounds.min[axis] = std::numeric_limits<float>::max();
        bounds.max[axis] = std::numeric_limits<float>::lowest();
    }

    for (const auto triangleIndex : triangleIndices) {
        const auto firstIndex = triangleIndex * 3u;
        for (uint32_t corner = 0; corner < 3; ++corner) {
            const auto vertexIndex = mesh.indices[firstIndex + corner];
            const auto& vertex = mesh.vertices[vertexIndex];
            for (auto axis = 0; axis < 3; ++axis) {
                bounds.min[axis] = std::min(bounds.min[axis], vertex.position[axis]);
                bounds.max[axis] = std::max(bounds.max[axis], vertex.position[axis]);
            }
        }
    }

    for (auto axis = 0; axis < 3; ++axis) {
        bounds.center[axis] = (bounds.min[axis] + bounds.max[axis]) * 0.5f;
    }

    float radiusSq = 0.0f;
    for (const auto triangleIndex : triangleIndices) {
        const auto firstIndex = triangleIndex * 3u;
        for (uint32_t corner = 0; corner < 3; ++corner) {
            const auto vertexIndex = mesh.indices[firstIndex + corner];
            const auto& vertex = mesh.vertices[vertexIndex];
            const auto dx = vertex.position[0] - bounds.center[0];
            const auto dy = vertex.position[1] - bounds.center[1];
            const auto dz = vertex.position[2] - bounds.center[2];
            radiusSq = std::max(radiusSq, dx * dx + dy * dy + dz * dz);
        }
    }

    bounds.radius = std::sqrt(radiusSq);
    bounds.valid = true;
    return bounds;
}

std::pmr::vector<uint32_t> representativeTriangles(
    const std::pmr::vector<uint32_t>& sourceTriangleIndices,
    const VirtualizedGeometryBuildPolicy& policy)
{
    std::pmr::vector<uint32_t> selected;
    if (sourceTriangleIndices.empty()) {
        return selected;
    }

    const auto target =
        targetTriangleCount(static_cast<uint32_t>(sourceTriangleIndices.size()), policy);

    for (uint32_t index = 0; index < target; ++index) {
        const auto sourceIndex =
            static_cast<size_t>((index * sourceTriangleIndices.size()) / target);
        selected.push_back(sourceTriangleIndices[sourceIndex]);
    }
    return selected;
}

std::pmr::vector<uint32_t> limitedTriangleSet(
    const std::pmr::vector<uint32_t>& sourceTriangleIndices, uint32_t maxTriangles)
{
    if (maxTriangles == 0u || sourceTriangleIndices.size() <= maxTriangles) {
        return sourceTriangleIndices;
    }

    std::pmr::vector<uint32_t> selected;
    selected.reserve(maxTriangles);
    for (uint32_t index = 0; index < maxTriangles; ++index) {
        const auto sourceIndex = static_cast<size_t>(
            (static_cast<uint64_t>(index) * sourceTriangleIndices.size()) / maxTriangles);
        selected.push_back(sourceTriangleIndices[sourceIndex]);
    }
    return selected;
}

bool sameTriangleSet(std::pmr::vector<uint32_t> lhs, std::pmr::vector<uint32_t> rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    std::sort(lhs.begin(), lhs.end());
    std::sort(rhs.begin(), rhs.end());
    return lhs == rhs;
}

struct SimplifiedMeshResult {
    std::pmr::vector<MeshVertex> vertices;
    std::pmr::vector<uint32_t> indices;
    float geometricError = 0.0f;
};

struct QemVertex {
    MeshVertex vertex;
    uint32_t sourceVertex = 0;
    Quadric quadric;
    Quadric surfaceQuadric;
    bool textureCoordinateSeam = false;
    bool removed = false;
};

struct QemTriangle {
    uint32_t vertices[3]{};
    uint32_t component = 0;
    bool removed = false;
};

struct EdgeKey {
    uint32_t a = 0;
    uint32_t b = 0;
};

struct EdgeUse {
    EdgeKey key;
    uint32_t count = 0;
    uint32_t firstTriangle = 0;
};

struct CollapseCandidate {
    bool valid = false;
    uint32_t keep = 0;
    uint32_t remove = 0;
    Vec3 position;
    float uv[2]{};
    double fullError = 0.0;
    double surfaceError = 0.0;
    double textureError = 0.0;
    double sortError = 0.0;
};

EdgeKey makeEdgeKey(uint32_t lhs, uint32_t rhs)
{
    return lhs < rhs ? EdgeKey{lhs, rhs} : EdgeKey{rhs, lhs};
}

bool sameEdge(EdgeKey lhs, EdgeKey rhs)
{
    return lhs.a == rhs.a && lhs.b == rhs.b;
}

bool samePosition(const MeshVertex& lhs, const MeshVertex& rhs)
{
    return lhs.position[0] == rhs.position[0] && lhs.position[1] == rhs.position[1]
        && lhs.position[2] == rhs.position[2];
}

bool sameTextureCoordinate(const MeshVertex& lhs, const MeshVertex& rhs)
{
    return lhs.uv[0] == rhs.uv[0] && lhs.uv[1] == rhs.uv[1];
}

void classifyTextureCoordinateSeams(std::pmr::vector<QemVertex>& vertices)
{
    for (uint32_t left = 0; left < vertices.size(); ++left) {
        for (uint32_t right = left + 1u; right < vertices.size(); ++right) {
            if (samePosition(vertices[left].vertex, vertices[right].vertex)
                && !sameTextureCoordinate(vertices[left].vertex, vertices[right].vertex)) {
                vertices[left].textureCoordinateSeam = true;
                vertices[right].textureCoordinateSeam = true;
            }
        }
    }
}

void addEdgeUse(
    std::pmr::vector<EdgeUse>& edges, uint32_t lhs, uint32_t rhs, uint32_t triangleIndex)
{
    const auto key = makeEdgeKey(lhs, rhs);
    for (auto& edge : edges) {
        if (sameEdge(edge.key, key)) {
            ++edge.count;
            return;
        }
    }
    edges.push_back({key, 1u, triangleIndex});
}

uint32_t findOrAddLocalVertex(
    std::pmr::vector<QemVertex>& vertices, const MeshAsset& mesh, uint32_t sourceVertex)
{
    for (uint32_t index = 0; index < vertices.size(); ++index) {
        if (vertices[index].sourceVertex == sourceVertex) {
            return index;
        }
    }

    QemVertex vertex;
    vertex.vertex = mesh.vertices[sourceVertex];
    vertex.sourceVertex = sourceVertex;
    vertices.push_back(vertex);
    return static_cast<uint32_t>(vertices.size() - 1u);
}

Vec3 triangleNormal(const std::pmr::vector<QemVertex>& vertices, const QemTriangle& triangle)
{
    const auto p0 = meshPosition(vertices[triangle.vertices[0]].vertex);
    const auto p1 = meshPosition(vertices[triangle.vertices[1]].vertex);
    const auto p2 = meshPosition(vertices[triangle.vertices[2]].vertex);
    return normalize(cross(p1 - p0, p2 - p0));
}

bool triangleContains(const QemTriangle& triangle, uint32_t vertex)
{
    return triangle.vertices[0] == vertex || triangle.vertices[1] == vertex
        || triangle.vertices[2] == vertex;
}

bool trianglesShareVertex(const QemTriangle& lhs, const QemTriangle& rhs)
{
    for (const auto left : lhs.vertices) {
        if (triangleContains(rhs, left)) {
            return true;
        }
    }
    return false;
}

bool triangleWouldDegenerate(const QemTriangle& triangle, uint32_t keep, uint32_t remove)
{
    uint32_t rewritten[3]{
        triangle.vertices[0] == remove ? keep : triangle.vertices[0],
        triangle.vertices[1] == remove ? keep : triangle.vertices[1],
        triangle.vertices[2] == remove ? keep : triangle.vertices[2],
    };
    return rewritten[0] == rewritten[1] || rewritten[1] == rewritten[2]
        || rewritten[2] == rewritten[0];
}

uint32_t degenerateTriangleCountAfterCollapse(
    const std::pmr::vector<QemTriangle>& triangles, uint32_t keep, uint32_t remove)
{
    uint32_t count = 0;
    for (const auto& triangle : triangles) {
        if (!triangle.removed && triangleContains(triangle, remove)
            && triangleWouldDegenerate(triangle, keep, remove)) {
            ++count;
        }
    }
    return count;
}

bool collapseWouldRemoveComponent(
    const std::pmr::vector<QemTriangle>& triangles, uint32_t keep, uint32_t remove)
{
    uint32_t maxComponent = 0;
    for (const auto& triangle : triangles) {
        if (!triangle.removed) {
            maxComponent = std::max(maxComponent, triangle.component);
        }
    }

    std::pmr::vector<uint32_t> activeCounts(maxComponent + 1u, 0u);
    std::pmr::vector<uint32_t> removedCounts(maxComponent + 1u, 0u);
    for (const auto& triangle : triangles) {
        if (triangle.removed) {
            continue;
        }
        ++activeCounts[triangle.component];
        if (triangleContains(triangle, remove) && triangleWouldDegenerate(triangle, keep, remove)) {
            ++removedCounts[triangle.component];
        }
    }

    for (uint32_t component = 0; component < activeCounts.size(); ++component) {
        if (activeCounts[component] > 0u && activeCounts[component] == removedCounts[component]) {
            return true;
        }
    }
    return false;
}

bool collapseWouldFlip(const std::pmr::vector<QemVertex>& vertices,
    const std::pmr::vector<QemTriangle>& triangles,
    uint32_t keep,
    uint32_t remove,
    Vec3 position)
{
    for (const auto& triangle : triangles) {
        if (triangle.removed
            || (!triangleContains(triangle, keep) && !triangleContains(triangle, remove))) {
            continue;
        }
        if (triangleWouldDegenerate(triangle, keep, remove)) {
            continue;
        }

        Vec3 points[3];
        for (uint32_t corner = 0; corner < 3; ++corner) {
            const auto vertex = triangle.vertices[corner];
            points[corner] = (vertex == keep || vertex == remove)
                ? position
                : meshPosition(vertices[vertex].vertex);
        }

        const auto oldNormal = triangleNormal(vertices, triangle);
        const auto newNormal = normalize(cross(points[1] - points[0], points[2] - points[0]));
        if (length(oldNormal) <= 0.0000001 || length(newNormal) <= 0.0000001) {
            return true;
        }
        if (dot(oldNormal, newNormal) < 0.05) {
            return true;
        }
    }
    return false;
}

CollapseCandidate makeCollapseCandidate(const std::pmr::vector<QemVertex>& vertices,
    const std::pmr::vector<QemTriangle>& triangles,
    EdgeKey edge,
    uint32_t activeTriangles,
    uint32_t targetTriangles,
    const VirtualizedGeometryBuildPolicy& policy)
{
    CollapseCandidate best;
    if (vertices[edge.a].removed || vertices[edge.b].removed) {
        return best;
    }
    if (policy.preserveTextureCoordinateSeams
        && vertices[edge.a].textureCoordinateSeam != vertices[edge.b].textureCoordinateSeam) {
        return best;
    }
    const auto removedTriangles = degenerateTriangleCountAfterCollapse(triangles, edge.a, edge.b);
    if (removedTriangles == 0u || activeTriangles - removedTriangles < targetTriangles) {
        return best;
    }
    if (policy.preserveTextureCoordinateSeams
        && collapseWouldRemoveComponent(triangles, edge.a, edge.b)) {
        return best;
    }

    const auto quadric = combined(vertices[edge.a].quadric, vertices[edge.b].quadric);
    const auto surfaceQuadric =
        combined(vertices[edge.a].surfaceQuadric, vertices[edge.b].surfaceQuadric);
    const auto p0 = meshPosition(vertices[edge.a].vertex);
    const auto p1 = meshPosition(vertices[edge.b].vertex);
    const auto uv0x = vertices[edge.a].vertex.uv[0];
    const auto uv0y = vertices[edge.a].vertex.uv[1];
    const auto uv1x = vertices[edge.b].vertex.uv[0];
    const auto uv1y = vertices[edge.b].vertex.uv[1];
    Vec3 optimal;
    std::pmr::vector<Vec3> placements;
    if (solveOptimalPosition(quadric, optimal)) {
        placements.push_back(optimal);
    }
    placements.push_back((p0 + p1) * 0.5);
    placements.push_back(p0);
    placements.push_back(p1);

    for (const auto placement : placements) {
        float placementUv[2]{
            (uv0x + uv1x) * 0.5f,
            (uv0y + uv1y) * 0.5f,
        };
        if (placement.x == p0.x && placement.y == p0.y && placement.z == p0.z) {
            placementUv[0] = uv0x;
            placementUv[1] = uv0y;
        } else if (placement.x == p1.x && placement.y == p1.y && placement.z == p1.z) {
            placementUv[0] = uv1x;
            placementUv[1] = uv1y;
        }

        if (collapseWouldFlip(vertices, triangles, edge.a, edge.b, placement)) {
            continue;
        }

        const auto fullError = std::max(0.0, quadric.evaluate(placement));
        const auto surfaceError = std::max(0.0, surfaceQuadric.evaluate(placement));
        const auto du0 = static_cast<double>(placementUv[0] - uv0x);
        const auto dv0 = static_cast<double>(placementUv[1] - uv0y);
        const auto du1 = static_cast<double>(placementUv[0] - uv1x);
        const auto dv1 = static_cast<double>(placementUv[1] - uv1y);
        const auto textureError = du0 * du0 + dv0 * dv0 + du1 * du1 + dv1 * dv1;
        const auto sortError =
            fullError + textureError * static_cast<double>(policy.textureCoordinateErrorWeight);
        if (!best.valid || sortError < best.sortError
            || (sortError == best.sortError && surfaceError < best.surfaceError)) {
            best.valid = true;
            best.keep = edge.a;
            best.remove = edge.b;
            best.position = placement;
            best.uv[0] = placementUv[0];
            best.uv[1] = placementUv[1];
            best.fullError = fullError;
            best.surfaceError = surfaceError;
            best.textureError = textureError;
            best.sortError = sortError;
        }
    }
    return best;
}

void rebuildQuadrics(
    std::pmr::vector<QemVertex>& vertices, const std::pmr::vector<QemTriangle>& triangles)
{
    for (auto& vertex : vertices) {
        vertex.quadric = {};
        vertex.surfaceQuadric = {};
    }

    std::pmr::vector<EdgeUse> edgeUses;
    for (uint32_t triangleIndex = 0; triangleIndex < triangles.size(); ++triangleIndex) {
        const auto& triangle = triangles[triangleIndex];
        if (triangle.removed) {
            continue;
        }

        Quadric planeQuadric;
        const auto p0 = meshPosition(vertices[triangle.vertices[0]].vertex);
        const auto p1 = meshPosition(vertices[triangle.vertices[1]].vertex);
        const auto p2 = meshPosition(vertices[triangle.vertices[2]].vertex);
        if (!addTrianglePlane(planeQuadric, p0, p1, p2, 1.0)) {
            continue;
        }
        for (const auto vertex : triangle.vertices) {
            vertices[vertex].quadric.add(planeQuadric);
            vertices[vertex].surfaceQuadric.add(planeQuadric);
        }

        addEdgeUse(edgeUses, triangle.vertices[0], triangle.vertices[1], triangleIndex);
        addEdgeUse(edgeUses, triangle.vertices[1], triangle.vertices[2], triangleIndex);
        addEdgeUse(edgeUses, triangle.vertices[2], triangle.vertices[0], triangleIndex);
    }

    for (const auto& edgeUse : edgeUses) {
        if (edgeUse.count != 1u) {
            continue;
        }

        const auto& triangle = triangles[edgeUse.firstTriangle];
        const auto faceNormal = triangleNormal(vertices, triangle);
        const auto p0 = meshPosition(vertices[edgeUse.key.a].vertex);
        const auto p1 = meshPosition(vertices[edgeUse.key.b].vertex);
        const auto edgeDirection = normalize(p1 - p0);
        const auto boundaryNormal = normalize(cross(edgeDirection, faceNormal));
        if (length(boundaryNormal) <= 0.0000001) {
            continue;
        }
        const auto d = -dot(boundaryNormal, p0);
        Quadric boundaryQuadric;
        boundaryQuadric.addPlane(boundaryNormal.x, boundaryNormal.y, boundaryNormal.z, d, 4.0);
        vertices[edgeUse.key.a].quadric.add(boundaryQuadric);
        vertices[edgeUse.key.b].quadric.add(boundaryQuadric);
    }
}

std::pmr::vector<EdgeKey> activeEdges(const std::pmr::vector<QemTriangle>& triangles)
{
    std::pmr::vector<EdgeKey> edges;
    auto addUnique = [&](uint32_t lhs, uint32_t rhs) {
        const auto key = makeEdgeKey(lhs, rhs);
        for (const auto edge : edges) {
            if (sameEdge(edge, key)) {
                return;
            }
        }
        edges.push_back(key);
    };

    for (const auto& triangle : triangles) {
        if (triangle.removed) {
            continue;
        }
        addUnique(triangle.vertices[0], triangle.vertices[1]);
        addUnique(triangle.vertices[1], triangle.vertices[2]);
        addUnique(triangle.vertices[2], triangle.vertices[0]);
    }
    return edges;
}

uint32_t activeTriangleCount(const std::pmr::vector<QemTriangle>& triangles)
{
    uint32_t count = 0;
    for (const auto& triangle : triangles) {
        if (!triangle.removed) {
            ++count;
        }
    }
    return count;
}

void assignTriangleComponents(std::pmr::vector<QemTriangle>& triangles)
{
    std::pmr::vector<bool> visited(triangles.size(), false);
    uint32_t component = 0;
    for (uint32_t seed = 0; seed < triangles.size(); ++seed) {
        if (visited[seed]) {
            continue;
        }

        std::pmr::vector<uint32_t> stack;
        stack.push_back(seed);
        visited[seed] = true;
        while (!stack.empty()) {
            const auto triangleIndex = stack.back();
            stack.pop_back();
            triangles[triangleIndex].component = component;
            for (uint32_t candidate = 0; candidate < triangles.size(); ++candidate) {
                if (!visited[candidate]
                    && trianglesShareVertex(triangles[triangleIndex], triangles[candidate])) {
                    visited[candidate] = true;
                    stack.push_back(candidate);
                }
            }
        }
        ++component;
    }
}

void collapseEdge(std::pmr::vector<QemVertex>& vertices,
    std::pmr::vector<QemTriangle>& triangles,
    const CollapseCandidate& candidate)
{
    auto& keep = vertices[candidate.keep];
    auto& remove = vertices[candidate.remove];
    setMeshPosition(keep.vertex, candidate.position);
    keep.vertex.uv[0] = candidate.uv[0];
    keep.vertex.uv[1] = candidate.uv[1];
    const Vec3 normal = normalize({
        keep.vertex.normal[0] + remove.vertex.normal[0],
        keep.vertex.normal[1] + remove.vertex.normal[1],
        keep.vertex.normal[2] + remove.vertex.normal[2],
    });
    if (length(normal) > 0.0000001) {
        keep.vertex.normal[0] = static_cast<float>(normal.x);
        keep.vertex.normal[1] = static_cast<float>(normal.y);
        keep.vertex.normal[2] = static_cast<float>(normal.z);
    }
    keep.quadric.add(remove.quadric);
    keep.surfaceQuadric.add(remove.surfaceQuadric);
    keep.textureCoordinateSeam = keep.textureCoordinateSeam || remove.textureCoordinateSeam;
    remove.removed = true;

    for (auto& triangle : triangles) {
        if (triangle.removed) {
            continue;
        }
        for (auto& vertex : triangle.vertices) {
            if (vertex == candidate.remove) {
                vertex = candidate.keep;
            }
        }
        if (triangle.vertices[0] == triangle.vertices[1]
            || triangle.vertices[1] == triangle.vertices[2]
            || triangle.vertices[2] == triangle.vertices[0]) {
            triangle.removed = true;
        }
    }
}

SimplifiedMeshResult qemSimplifyClusterMesh(const MeshAsset& mesh,
    const std::pmr::vector<uint32_t>& sourceTriangleIndices,
    const VirtualizedGeometryBuildPolicy& policy)
{
    SimplifiedMeshResult result;
    if (sourceTriangleIndices.empty()) {
        return result;
    }

    std::pmr::vector<QemVertex> vertices;
    std::pmr::vector<QemTriangle> triangles;
    for (const auto triangleIndex : sourceTriangleIndices) {
        const auto firstIndex = triangleIndex * 3u;
        if (firstIndex + 2u >= mesh.indices.size()) {
            continue;
        }
        QemTriangle triangle;
        triangle.vertices[0] = findOrAddLocalVertex(vertices, mesh, mesh.indices[firstIndex + 0u]);
        triangle.vertices[1] = findOrAddLocalVertex(vertices, mesh, mesh.indices[firstIndex + 1u]);
        triangle.vertices[2] = findOrAddLocalVertex(vertices, mesh, mesh.indices[firstIndex + 2u]);
        if (triangle.vertices[0] != triangle.vertices[1]
            && triangle.vertices[1] != triangle.vertices[2]
            && triangle.vertices[2] != triangle.vertices[0]) {
            triangles.push_back(triangle);
        }
    }

    if (triangles.empty()) {
        return result;
    }
    assignTriangleComponents(triangles);
    if (policy.preserveTextureCoordinateSeams) {
        classifyTextureCoordinateSeams(vertices);
    }

    const auto target = targetTriangleCount(static_cast<uint32_t>(triangles.size()), policy);
    double maxFullError = 0.0;
    auto activeCount = activeTriangleCount(triangles);
    while (activeCount > target) {
        rebuildQuadrics(vertices, triangles);
        CollapseCandidate best;
        for (const auto edge : activeEdges(triangles)) {
            const auto candidate =
                makeCollapseCandidate(vertices, triangles, edge, activeCount, target, policy);
            if (!candidate.valid) {
                continue;
            }
            if (!best.valid || candidate.sortError < best.sortError
                || (candidate.sortError == best.sortError && candidate.keep < best.keep)
                || (candidate.sortError == best.sortError && candidate.keep == best.keep
                    && candidate.remove < best.remove)) {
                best = candidate;
            }
        }

        if (!best.valid) {
            break;
        }

        maxFullError = std::max(maxFullError, best.fullError);
        collapseEdge(vertices, triangles, best);
        activeCount = activeTriangleCount(triangles);
    }

    std::pmr::vector<uint32_t> remap(vertices.size(), UINT32_MAX);
    for (const auto& triangle : triangles) {
        if (triangle.removed) {
            continue;
        }
        for (const auto vertex : triangle.vertices) {
            if (remap[vertex] == UINT32_MAX) {
                remap[vertex] = static_cast<uint32_t>(result.vertices.size());
                result.vertices.push_back(vertices[vertex].vertex);
            }
            result.indices.push_back(remap[vertex]);
        }
    }
    result.geometricError = static_cast<float>(std::sqrt(maxFullError));
    return result;
}

VirtualizedGeometryCluster* findMutableCluster(
    VirtualizedGeometryHierarchy& hierarchy, uint32_t clusterId)
{
    for (auto& cluster : hierarchy.leafClusters) {
        if (cluster.id == clusterId) {
            return &cluster;
        }
    }
    for (auto& cluster : hierarchy.parentClusters) {
        if (cluster.id == clusterId) {
            return &cluster;
        }
    }
    return nullptr;
}

bool validatePolicy(const VirtualizedGeometryBuildPolicy& policy, std::pmr::string& diagnostics)
{
    auto fail = [&](const char* message) {
        arp::core::appendDiagnostic(diagnostics, message);
        return false;
    };

    if (policy.maxTrianglesPerLeafCluster == 0) {
        return fail("maxTrianglesPerLeafCluster must be greater than zero");
    }
    if (policy.maxVerticesPerLeafCluster < 3) {
        return fail("maxVerticesPerLeafCluster must be at least 3");
    }
    if (policy.maxClustersPerGroup < 2) {
        return fail("maxClustersPerGroup must be at least 2");
    }
    if (policy.simplificationTargetNumerator == 0 || policy.simplificationTargetDenominator == 0
        || policy.simplificationTargetNumerator > policy.simplificationTargetDenominator) {
        return fail("simplification target ratio must be in the range (0, 1]");
    }
    if (policy.maxTrianglesPerSimplificationPass > 0u
        && policy.maxTrianglesPerSimplificationPass < 3u) {
        return fail("maxTrianglesPerSimplificationPass must be zero or at least 3");
    }
    if (!std::isfinite(policy.textureCoordinateErrorWeight)
        || policy.textureCoordinateErrorWeight < 0.0f) {
        return fail("textureCoordinateErrorWeight must be finite and non-negative");
    }
    if (policy.simplificationBackend != ClusterSimplificationBackend::DeterministicPrototype) {
        return fail("unsupported cluster simplification backend");
    }
    return true;
}

uint64_t stableHierarchyHash(const VirtualizedGeometryHierarchy& hierarchy)
{
    uint64_t hash = 1469598103934665603ull;
    auto hashCluster = [&](const VirtualizedGeometryCluster& cluster) {
        hash = hashValue(hash, cluster.id);
        hash = hashValue(hash, cluster.level);
        hash = hashValue(hash, cluster.materialSlot);
        hash = hashValue(hash, cluster.sourceTriangleCount);
        hash = hashValue(hash, cluster.simplificationInputTriangleCount);
        hash = hashValue(hash, cluster.simplifiedTriangleCount);
        hash = hashValue(hash, cluster.parentClusterId);
        hash = hashValue(hash, cluster.simplificationInputComplete ? 1u : 0u);
        hash = hashValue(hash, cluster.leaf ? 1u : 0u);
        hash = hashFloat(hash, cluster.simplificationError);
        for (const auto triangle : cluster.sourceTriangleIndices) {
            hash = hashValue(hash, triangle);
        }
        hash = hashValue(hash, 0xffffu);
        for (const auto triangle : cluster.representativeTriangleIndices) {
            hash = hashValue(hash, triangle);
        }
        hash = hashValue(hash, 0xfffeu);
        for (const auto& vertex : cluster.simplifiedVertices) {
            hash = hashFloat(hash, vertex.position[0]);
            hash = hashFloat(hash, vertex.position[1]);
            hash = hashFloat(hash, vertex.position[2]);
            hash = hashFloat(hash, vertex.normal[0]);
            hash = hashFloat(hash, vertex.normal[1]);
            hash = hashFloat(hash, vertex.normal[2]);
            hash = hashFloat(hash, vertex.uv[0]);
            hash = hashFloat(hash, vertex.uv[1]);
        }
        hash = hashValue(hash, 0xfffcu);
        for (const auto index : cluster.simplifiedIndices) {
            hash = hashValue(hash, index);
        }
        hash = hashValue(hash, 0xfffbu);
        for (const auto child : cluster.children) {
            hash = hashValue(hash, child);
        }
        hash = hashValue(hash, 0xfffdu);
    };
    for (const auto& cluster : hierarchy.leafClusters) {
        hashCluster(cluster);
    }
    for (const auto& cluster : hierarchy.parentClusters) {
        hashCluster(cluster);
    }
    for (const auto root : hierarchy.rootClusterIds) {
        hash = hashValue(hash, root);
    }
    return hash;
}

} // namespace

const VirtualizedGeometryCluster* VirtualizedGeometryHierarchy::findCluster(
    uint32_t clusterId) const
{
    for (const auto& cluster : leafClusters) {
        if (cluster.id == clusterId) {
            return &cluster;
        }
    }
    for (const auto& cluster : parentClusters) {
        if (cluster.id == clusterId) {
            return &cluster;
        }
    }
    return nullptr;
}

TriangleAdjacencyBuilder::Result TriangleAdjacencyBuilder::build(const MeshAsset& mesh) const
{
    Result result;
    if (!mesh.validForIndexedDraw()) {
        result.diagnostics = "mesh is not valid for indexed draw";
        return result;
    }

    for (const auto& submesh : mesh.submeshes) {
        if (submesh.indexCount % 3u != 0) {
            result.diagnostics = "submesh index count must be a multiple of three";
            return result;
        }

        for (uint32_t offset = 0; offset < submesh.indexCount; offset += 3u) {
            const auto firstIndex = submesh.firstIndex + offset;
            TriangleRecord triangle;
            triangle.triangleIndex = static_cast<uint32_t>(result.triangles.size());
            triangle.materialSlot = submesh.materialSlot;
            triangle.indices[0] = mesh.indices[firstIndex + 0u];
            triangle.indices[1] = mesh.indices[firstIndex + 1u];
            triangle.indices[2] = mesh.indices[firstIndex + 2u];
            result.triangles.push_back(triangle);
        }
    }

    result.neighbors.resize(result.triangles.size());
    for (uint32_t left = 0; left < result.triangles.size(); ++left) {
        for (uint32_t right = left + 1u; right < result.triangles.size(); ++right) {
            if (result.triangles[left].materialSlot == result.triangles[right].materialSlot
                && shareEdge(result.triangles[left], result.triangles[right])) {
                result.neighbors[left].push_back(right);
                result.neighbors[right].push_back(left);
            }
        }
    }

    result.success = true;
    return result;
}

LeafClusterBuilder::Result LeafClusterBuilder::build(const MeshAsset& mesh,
    const TriangleAdjacencyBuilder::Result& adjacency,
    const VirtualizedGeometryBuildPolicy& policy,
    uint32_t& nextClusterId) const
{
    Result result;
    if (!adjacency.success) {
        result.diagnostics = adjacency.diagnostics;
        return result;
    }

    std::pmr::vector<bool> assigned(adjacency.triangles.size(), false);
    for (uint32_t seed = 0; seed < adjacency.triangles.size(); ++seed) {
        if (assigned[seed]) {
            continue;
        }

        VirtualizedGeometryCluster cluster;
        cluster.id = nextClusterId++;
        cluster.level = 0;
        cluster.materialSlot = adjacency.triangles[seed].materialSlot;
        cluster.leaf = true;
        cluster.parentClusterId = 0;

        std::pmr::vector<uint32_t> uniqueVertices;
        cluster.sourceTriangleIndices.push_back(seed);
        cluster.representativeTriangleIndices.push_back(seed);
        appendTriangleVertices(uniqueVertices, adjacency.triangles[seed]);
        assigned[seed] = true;

        while (cluster.sourceTriangleIndices.size() < policy.maxTrianglesPerLeafCluster) {
            uint32_t bestCandidate = UINT32_MAX;
            for (const auto triangleId : cluster.sourceTriangleIndices) {
                for (const auto neighborId : adjacency.neighbors[triangleId]) {
                    if (assigned[neighborId]) {
                        continue;
                    }
                    const auto& neighbor = adjacency.triangles[neighborId];
                    if (policy.preserveMaterialBoundaries
                        && neighbor.materialSlot != cluster.materialSlot) {
                        continue;
                    }
                    if (!canAddTriangle(
                            uniqueVertices, neighbor, policy.maxVerticesPerLeafCluster)) {
                        continue;
                    }
                    bestCandidate = std::min(bestCandidate, neighborId);
                }
            }

            if (bestCandidate == UINT32_MAX) {
                break;
            }

            cluster.sourceTriangleIndices.push_back(bestCandidate);
            cluster.representativeTriangleIndices.push_back(bestCandidate);
            appendTriangleVertices(uniqueVertices, adjacency.triangles[bestCandidate]);
            assigned[bestCandidate] = true;
        }

        cluster.sourceTriangleCount = static_cast<uint32_t>(cluster.sourceTriangleIndices.size());
        cluster.simplificationInputTriangleCount = cluster.sourceTriangleCount;
        cluster.simplificationInputComplete = true;
        cluster.simplifiedTriangleCount = cluster.sourceTriangleCount;
        cluster.bounds = boundsForTriangles(mesh, cluster.sourceTriangleIndices);
        result.clusters.push_back(cluster);
    }

    result.success = true;
    return result;
}

VirtualizedGeometryCluster ClusterSimplifier::simplify(const MeshAsset& mesh,
    const std::pmr::vector<uint32_t>& sourceTriangleIndices,
    const std::pmr::vector<uint32_t>& simplificationTriangleIndices,
    const std::pmr::vector<uint32_t>& childClusterIds,
    uint32_t clusterId,
    uint32_t level,
    uint32_t materialSlot,
    const VirtualizedGeometryBuildPolicy& policy) const
{
    VirtualizedGeometryCluster cluster;
    cluster.id = clusterId;
    cluster.level = level;
    cluster.materialSlot = materialSlot;
    cluster.leaf = false;
    cluster.sourceTriangleIndices = sourceTriangleIndices;
    cluster.representativeTriangleIndices = representativeTriangles(sourceTriangleIndices, policy);
    cluster.children = childClusterIds;
    cluster.sourceTriangleCount = static_cast<uint32_t>(cluster.sourceTriangleIndices.size());
    const auto boundedSimplificationTriangles =
        limitedTriangleSet(simplificationTriangleIndices, policy.maxTrianglesPerSimplificationPass);
    cluster.simplificationInputTriangleCount =
        static_cast<uint32_t>(boundedSimplificationTriangles.size());
    cluster.simplificationInputComplete =
        sameTriangleSet(sourceTriangleIndices, boundedSimplificationTriangles);
    auto simplifiedMesh = qemSimplifyClusterMesh(mesh, boundedSimplificationTriangles, policy);
    cluster.simplifiedVertices = std::move(simplifiedMesh.vertices);
    cluster.simplifiedIndices = std::move(simplifiedMesh.indices);
    cluster.simplifiedTriangleCount = static_cast<uint32_t>(cluster.simplifiedIndices.size() / 3u);
    cluster.simplificationError = simplifiedMesh.geometricError;
    cluster.bounds = boundsForTriangles(mesh, cluster.sourceTriangleIndices);
    return cluster;
}

VirtualizedGeometryBuildResult ClusterHierarchyBuilder::build(const MeshAsset& mesh,
    std::pmr::vector<VirtualizedGeometryCluster> leafClusters,
    const VirtualizedGeometryBuildPolicy& policy) const
{
    VirtualizedGeometryBuildResult result;
    if (leafClusters.empty()) {
        result.diagnostics = "virtualized geometry requires at least one leaf cluster";
        return result;
    }

    result.hierarchy.leafClusters = std::move(leafClusters);
    uint32_t nextClusterId = 1;
    for (const auto& cluster : result.hierarchy.leafClusters) {
        nextClusterId = std::max(nextClusterId, cluster.id + 1u);
    }

    std::pmr::vector<uint32_t> currentLevel;
    for (const auto& cluster : result.hierarchy.leafClusters) {
        currentLevel.push_back(cluster.id);
    }

    ClusterSimplifier simplifier;
    uint32_t level = 1;
    while (currentLevel.size() > 1) {
        std::pmr::vector<uint32_t> nextLevel;
        bool madeParent = false;

        for (size_t cursor = 0; cursor < currentLevel.size();) {
            const auto* first = result.hierarchy.findCluster(currentLevel[cursor]);
            if (!first) {
                result.diagnostics = "cluster hierarchy references an unknown cluster";
                return result;
            }

            std::pmr::vector<uint32_t> groupIds;
            const auto materialSlot = first->materialSlot;
            while (cursor < currentLevel.size() && groupIds.size() < policy.maxClustersPerGroup) {
                const auto* candidate = result.hierarchy.findCluster(currentLevel[cursor]);
                if (!candidate) {
                    result.diagnostics = "cluster hierarchy references an unknown cluster";
                    return result;
                }
                if (policy.preserveMaterialBoundaries && candidate->materialSlot != materialSlot) {
                    break;
                }
                groupIds.push_back(candidate->id);
                ++cursor;
            }

            if (groupIds.size() == 1) {
                nextLevel.push_back(groupIds.front());
                continue;
            }

            std::pmr::vector<uint32_t> sourceTriangles;
            std::pmr::vector<uint32_t> simplificationTriangles;
            for (const auto clusterId : groupIds) {
                const auto* child = result.hierarchy.findCluster(clusterId);
                sourceTriangles.insert(sourceTriangles.end(),
                    child->sourceTriangleIndices.begin(),
                    child->sourceTriangleIndices.end());
                const auto& childSimplificationTriangles = child->leaf
                    ? child->sourceTriangleIndices
                    : child->representativeTriangleIndices;
                simplificationTriangles.insert(simplificationTriangles.end(),
                    childSimplificationTriangles.begin(),
                    childSimplificationTriangles.end());
            }

            auto parent = simplifier.simplify(mesh,
                sourceTriangles,
                simplificationTriangles,
                groupIds,
                nextClusterId++,
                level,
                materialSlot,
                policy);
            for (const auto childId : groupIds) {
                auto* child = findMutableCluster(result.hierarchy, childId);
                if (child) {
                    child->parentClusterId = parent.id;
                }
            }
            if (parent.simplifiedTriangleCount < parent.sourceTriangleCount) {
                result.hierarchy.hasAutomaticSimplification = true;
            }
            nextLevel.push_back(parent.id);
            result.hierarchy.parentClusters.push_back(std::move(parent));
            madeParent = true;
        }

        currentLevel = std::move(nextLevel);
        ++level;
        if (!madeParent) {
            break;
        }
    }

    result.hierarchy.rootClusterIds = std::move(currentLevel);
    result.hierarchy.stableHash = stableHierarchyHash(result.hierarchy);
    result.success = true;
    return result;
}

VirtualizedGeometryBuildResult buildVirtualizedGeometry(
    const MeshAsset& mesh, const VirtualizedGeometryBuildPolicy& policy)
{
    VirtualizedGeometryBuildResult result;
    if (!validatePolicy(policy, result.diagnostics)) {
        return result;
    }

    TriangleAdjacencyBuilder adjacencyBuilder;
    const auto adjacency = adjacencyBuilder.build(mesh);
    if (!adjacency.success) {
        result.diagnostics = adjacency.diagnostics;
        return result;
    }

    uint32_t nextClusterId = 1;
    LeafClusterBuilder leafBuilder;
    auto leafResult = leafBuilder.build(mesh, adjacency, policy, nextClusterId);
    if (!leafResult.success) {
        result.diagnostics = leafResult.diagnostics;
        return result;
    }

    ClusterHierarchyBuilder hierarchyBuilder;
    return hierarchyBuilder.build(mesh, std::move(leafResult.clusters), policy);
}

} // namespace arp::assets
