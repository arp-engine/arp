#include "assets/tangent_generation.h"

#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"

#include <array>
#include <cmath>

namespace arp::assets {

namespace {

using arp::core::Vec3;

Vec3 positionOf(const MeshVertex& vertex)
{
    return Vec3{vertex.position[0], vertex.position[1], vertex.position[2]};
}

Vec3 normalOf(const MeshVertex& vertex)
{
    return core::normalizeOr(
        Vec3{vertex.normal[0], vertex.normal[1], vertex.normal[2]}, Vec3{0.0f, 0.0f, 1.0f});
}

bool finite(const Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool nonDegenerate(const Vec3 value)
{
    return finite(value) && core::dot(value, value) > 1.0e-12f;
}

Vec3 fallbackTangentForNormal(const Vec3 normal)
{
    const Vec3 axis = std::fabs(normal.z) < 0.9f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
    return core::normalizeOr(core::cross(axis, normal), Vec3{1.0f, 0.0f, 0.0f});
}

void accumulateTriangleTangents(const MeshAsset& mesh,
    const uint32_t i0,
    const uint32_t i1,
    const uint32_t i2,
    const uint32_t firstVertex,
    std::pmr::vector<Vec3>& tangentSums,
    std::pmr::vector<Vec3>& bitangentSums)
{
    const auto& v0 = mesh.vertices[i0];
    const auto& v1 = mesh.vertices[i1];
    const auto& v2 = mesh.vertices[i2];

    const auto p0 = positionOf(v0);
    const auto p1 = positionOf(v1);
    const auto p2 = positionOf(v2);
    const auto edge1 = p1 - p0;
    const auto edge2 = p2 - p0;

    const auto du1 = v1.uv[0] - v0.uv[0];
    const auto dv1 = v1.uv[1] - v0.uv[1];
    const auto du2 = v2.uv[0] - v0.uv[0];
    const auto dv2 = v2.uv[1] - v0.uv[1];
    const auto determinant = du1 * dv2 - dv1 * du2;
    if (std::fabs(determinant) <= 0.000001f) {
        return;
    }

    const auto invDet = 1.0f / determinant;
    const auto tangent = core::scale(core::scale(edge1, dv2) - core::scale(edge2, dv1), invDet);
    const auto bitangent = core::scale(core::scale(edge2, du1) - core::scale(edge1, du2), invDet);

    const auto local0 = i0 - firstVertex;
    const auto local1 = i1 - firstVertex;
    const auto local2 = i2 - firstVertex;
    tangentSums[local0] = tangentSums[local0] + tangent;
    tangentSums[local1] = tangentSums[local1] + tangent;
    tangentSums[local2] = tangentSums[local2] + tangent;
    bitangentSums[local0] = bitangentSums[local0] + bitangent;
    bitangentSums[local1] = bitangentSums[local1] + bitangent;
    bitangentSums[local2] = bitangentSums[local2] + bitangent;
}

bool generateTangentsForRanges(MeshAsset& mesh,
    const uint32_t firstVertex,
    const uint32_t vertexCount,
    const Submesh* ranges,
    const std::size_t rangeCount,
    const bool compensateNormalMapForUvYFlip)
{
    if (!mesh.validForIndexedDraw() || vertexCount == 0u || ranges == nullptr || rangeCount == 0u
        || firstVertex > mesh.vertices.size() || vertexCount > mesh.vertices.size() - firstVertex) {
        return false;
    }

    const auto vertexEnd = firstVertex + vertexCount;
    for (std::size_t rangeIndex = 0u; rangeIndex < rangeCount; ++rangeIndex) {
        const auto& range = ranges[rangeIndex];
        if (range.indexCount == 0u || range.indexCount % 3u != 0u
            || range.firstIndex > mesh.indices.size()
            || range.indexCount > mesh.indices.size() - range.firstIndex) {
            return false;
        }
        for (uint32_t offset = 0u; offset < range.indexCount; ++offset) {
            const auto vertexIndex = mesh.indices[range.firstIndex + offset];
            if (vertexIndex < firstVertex || vertexIndex >= vertexEnd) {
                return false;
            }
        }
    }

    std::pmr::vector<Vec3> tangentSums(vertexCount);
    std::pmr::vector<Vec3> bitangentSums(vertexCount);
    for (std::size_t rangeIndex = 0u; rangeIndex < rangeCount; ++rangeIndex) {
        const auto& range = ranges[rangeIndex];
        const auto end = range.firstIndex + range.indexCount;
        for (uint32_t index = range.firstIndex; index < end; index += 3u) {
            accumulateTriangleTangents(mesh,
                mesh.indices[index + 0u],
                mesh.indices[index + 1u],
                mesh.indices[index + 2u],
                firstVertex,
                tangentSums,
                bitangentSums);
        }
    }

    for (uint32_t localIndex = 0u; localIndex < vertexCount; ++localIndex) {
        auto& vertex = mesh.vertices[firstVertex + localIndex];
        const auto normal = normalOf(vertex);
        const auto fallback = fallbackTangentForNormal(normal);
        const auto orthogonalTangent = tangentSums[localIndex]
            - core::scale(normal, core::dot(normal, tangentSums[localIndex]));
        const auto tangent = core::normalizeOr(orthogonalTangent, fallback);
        auto handedness = core::dot(core::cross(normal, tangent), bitangentSums[localIndex]) < 0.0f
            ? -1.0f
            : 1.0f;
        if (compensateNormalMapForUvYFlip) {
            // Flipping V reverses the UV-derived bitangent. The normal-map
            // texels are not transformed, so reverse the generated sign once
            // more to keep their green channel in the authored tangent frame.
            handedness = -handedness;
        }

        vertex.tangent[0] = tangent.x;
        vertex.tangent[1] = tangent.y;
        vertex.tangent[2] = tangent.z;
        vertex.tangent[3] = handedness;
    }
    return true;
}

} // namespace

bool applyImportedTangentFrame(MeshVertex& vertex,
    const std::array<float, 3>& tangentValues,
    const std::array<float, 3>& bitangentValues)
{
    const auto sourceTangent = Vec3{tangentValues[0], tangentValues[1], tangentValues[2]};
    const auto sourceBitangent = Vec3{bitangentValues[0], bitangentValues[1], bitangentValues[2]};
    if (!nonDegenerate(sourceTangent) || !nonDegenerate(sourceBitangent)) {
        return false;
    }

    const auto normal = normalOf(vertex);
    const auto orthogonalTangent =
        sourceTangent - core::scale(normal, core::dot(normal, sourceTangent));
    if (!nonDegenerate(orthogonalTangent)) {
        return false;
    }
    const auto tangent = core::normalizeOr(orthogonalTangent, fallbackTangentForNormal(normal));
    const auto handedness =
        core::dot(core::cross(normal, tangent), sourceBitangent) < 0.0f ? -1.0f : 1.0f;

    vertex.tangent[0] = tangent.x;
    vertex.tangent[1] = tangent.y;
    vertex.tangent[2] = tangent.z;
    vertex.tangent[3] = handedness;
    return true;
}

bool generateTangentsForIndexedMesh(MeshAsset& mesh, const bool compensateNormalMapForUvYFlip)
{
    return generateTangentsForRanges(mesh,
        0u,
        static_cast<uint32_t>(mesh.vertices.size()),
        mesh.submeshes.data(),
        mesh.submeshes.size(),
        compensateNormalMapForUvYFlip);
}

bool generateTangentsForIndexedMeshRange(
    MeshAsset& mesh, const IndexedMeshTangentRange& range, const bool compensateNormalMapForUvYFlip)
{
    const std::array submeshes{
        Submesh{range.firstIndex, range.indexCount, 0u},
    };
    return generateTangentsForRanges(mesh,
        range.firstVertex,
        range.vertexCount,
        submeshes.data(),
        submeshes.size(),
        compensateNormalMapForUvYFlip);
}

} // namespace arp::assets
