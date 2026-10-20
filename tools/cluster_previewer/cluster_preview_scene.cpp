#include "tools/cluster_previewer/cluster_preview_scene.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace arp::tools::cluster_previewer {

namespace {

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

Color colorForCluster(uint32_t clusterId)
{
    const auto r = static_cast<float>(((clusterId * 73u) % 211u) + 32u) / 255.0f;
    const auto g = static_cast<float>(((clusterId * 151u) % 199u) + 40u) / 255.0f;
    const auto b = static_cast<float>(((clusterId * 43u) % 181u) + 60u) / 255.0f;
    return {r, g, b};
}

Color shadedColor(const assets::MeshVertex& vertex)
{
    constexpr Color base{0.70f, 0.76f, 0.80f};
    const float lx = -0.35f;
    const float ly = 0.55f;
    const float lz = 0.76f;
    const auto nLength = std::sqrt(vertex.normal[0] * vertex.normal[0]
        + vertex.normal[1] * vertex.normal[1] + vertex.normal[2] * vertex.normal[2]);
    const auto invLength = 1.0f / std::max(nLength, 0.0001f);
    const auto diffuse = std::max(0.0f,
        vertex.normal[0] * invLength * lx + vertex.normal[1] * invLength * ly
            + vertex.normal[2] * invLength * lz);
    const auto intensity = 0.42f + diffuse * 0.58f;
    return {base.r * intensity, base.g * intensity, base.b * intensity};
}

Color colorForVertex(
    const assets::MeshVertex& vertex, uint32_t clusterId, ClusterPreviewColorMode colorMode)
{
    if (colorMode == ClusterPreviewColorMode::ClusterColor) {
        return colorForCluster(clusterId);
    }
    if (colorMode == ClusterPreviewColorMode::Textured) {
        return {1.0f, 1.0f, 1.0f};
    }
    return shadedColor(vertex);
}

void expandBounds(PreviewBounds& bounds, float x, float y, float z)
{
    if (!bounds.valid) {
        bounds.min[0] = bounds.max[0] = x;
        bounds.min[1] = bounds.max[1] = y;
        bounds.min[2] = bounds.max[2] = z;
        bounds.valid = true;
        return;
    }

    bounds.min[0] = std::min(bounds.min[0], x);
    bounds.min[1] = std::min(bounds.min[1], y);
    bounds.min[2] = std::min(bounds.min[2], z);
    bounds.max[0] = std::max(bounds.max[0], x);
    bounds.max[1] = std::max(bounds.max[1], y);
    bounds.max[2] = std::max(bounds.max[2], z);
}

void finalizeBounds(PreviewBounds& bounds)
{
    if (!bounds.valid) {
        return;
    }

    for (int axis = 0; axis < 3; ++axis) {
        bounds.center[axis] = (bounds.min[axis] + bounds.max[axis]) * 0.5f;
    }
    float radiusSq = 0.0f;
    for (int xIndex = 0; xIndex < 2; ++xIndex) {
        for (int yIndex = 0; yIndex < 2; ++yIndex) {
            for (int zIndex = 0; zIndex < 2; ++zIndex) {
                const float x = xIndex == 0 ? bounds.min[0] : bounds.max[0];
                const float y = yIndex == 0 ? bounds.min[1] : bounds.max[1];
                const float z = zIndex == 0 ? bounds.min[2] : bounds.max[2];
                const auto dx = x - bounds.center[0];
                const auto dy = y - bounds.center[1];
                const auto dz = z - bounds.center[2];
                radiusSq = std::max(radiusSq, dx * dx + dy * dy + dz * dz);
            }
        }
    }
    bounds.radius = std::sqrt(radiusSq);
}

PreviewVertex makeVertex(const assets::MeshVertex& vertex, const Color& color)
{
    return {
        vertex.position[0],
        vertex.position[1],
        vertex.position[2],
        color.r,
        color.g,
        color.b,
        vertex.uv[0],
        vertex.uv[1],
    };
}

void appendSourceTriangles(ClusterPreviewScene& scene,
    const assets::MeshAsset& mesh,
    const assets::VirtualizedGeometryCluster& cluster,
    const std::pmr::vector<uint32_t>& triangleIds,
    ClusterPreviewColorMode colorMode)
{
    for (const auto triangleId : triangleIds) {
        const auto firstIndex = triangleId * 3u;
        if (firstIndex + 2u >= mesh.indices.size()) {
            scene.success = false;
            scene.diagnostics = "cluster preview references triangle outside mesh indices";
            return;
        }

        const auto baseVertex = static_cast<uint32_t>(scene.triangleVertices.size());
        for (uint32_t corner = 0; corner < 3; ++corner) {
            const auto vertexIndex = mesh.indices[firstIndex + corner];
            if (vertexIndex >= mesh.vertices.size()) {
                scene.success = false;
                scene.diagnostics = "cluster preview references vertex outside mesh";
                return;
            }
            const auto previewVertex = makeVertex(mesh.vertices[vertexIndex],
                colorForVertex(mesh.vertices[vertexIndex], cluster.id, colorMode));
            scene.triangleVertices.push_back(previewVertex);
            scene.triangleIndices.push_back(baseVertex + corner);
            expandBounds(scene.bounds, previewVertex.x, previewVertex.y, previewVertex.z);
        }
        ++scene.triangleCount;
    }
}

void appendClusterTriangles(ClusterPreviewScene& scene,
    const assets::MeshAsset& mesh,
    const assets::VirtualizedGeometryCluster& cluster,
    ClusterPreviewColorMode colorMode)
{
    if (cluster.leaf) {
        appendSourceTriangles(scene, mesh, cluster, cluster.sourceTriangleIndices, colorMode);
        return;
    }

    if (!cluster.simplifiedIndices.empty()) {
        if (cluster.simplifiedIndices.size() % 3u != 0) {
            scene.success = false;
            scene.diagnostics =
                "cluster preview simplified mesh index count must be a multiple of three";
            return;
        }

        for (const auto index : cluster.simplifiedIndices) {
            if (index >= cluster.simplifiedVertices.size()) {
                scene.success = false;
                scene.diagnostics =
                    "cluster preview simplified mesh references vertex outside cluster";
                return;
            }
            const auto previewIndex = static_cast<uint32_t>(scene.triangleVertices.size());
            const auto previewVertex = makeVertex(cluster.simplifiedVertices[index],
                colorForVertex(cluster.simplifiedVertices[index], cluster.id, colorMode));
            scene.triangleVertices.push_back(previewVertex);
            scene.triangleIndices.push_back(previewIndex);
            expandBounds(scene.bounds, previewVertex.x, previewVertex.y, previewVertex.z);
        }
        scene.triangleCount += static_cast<uint32_t>(cluster.simplifiedIndices.size() / 3u);
        return;
    }

    appendSourceTriangles(scene, mesh, cluster, cluster.representativeTriangleIndices, colorMode);
}

void appendBoundsLines(ClusterPreviewScene& scene,
    const assets::VirtualizedGeometryCluster& cluster,
    ClusterPreviewColorMode colorMode)
{
    if (!cluster.bounds.valid) {
        return;
    }

    const auto color = colorMode == ClusterPreviewColorMode::ClusterColor
        ? colorForCluster(cluster.id)
        : Color{0.58f, 0.62f, 0.66f};
    const auto baseVertex = static_cast<uint32_t>(scene.lineVertices.size());
    const float x0 = cluster.bounds.min[0];
    const float y0 = cluster.bounds.min[1];
    const float z0 = cluster.bounds.min[2];
    const float x1 = cluster.bounds.max[0];
    const float y1 = cluster.bounds.max[1];
    const float z1 = cluster.bounds.max[2];

    scene.lineVertices.push_back({x0, y0, z0, color.r, color.g, color.b});
    scene.lineVertices.push_back({x1, y0, z0, color.r, color.g, color.b});
    scene.lineVertices.push_back({x1, y1, z0, color.r, color.g, color.b});
    scene.lineVertices.push_back({x0, y1, z0, color.r, color.g, color.b});
    scene.lineVertices.push_back({x0, y0, z1, color.r, color.g, color.b});
    scene.lineVertices.push_back({x1, y0, z1, color.r, color.g, color.b});
    scene.lineVertices.push_back({x1, y1, z1, color.r, color.g, color.b});
    scene.lineVertices.push_back({x0, y1, z1, color.r, color.g, color.b});

    const uint32_t edges[] = {
        0,
        1,
        1,
        2,
        2,
        3,
        3,
        0,
        4,
        5,
        5,
        6,
        6,
        7,
        7,
        4,
        0,
        4,
        1,
        5,
        2,
        6,
        3,
        7,
    };
    for (const auto edgeIndex : edges) {
        scene.lineIndices.push_back(baseVertex + edgeIndex);
    }
}

void appendSceneBoundsLines(ClusterPreviewScene& scene)
{
    if (!scene.bounds.valid) {
        return;
    }

    const auto baseVertex = static_cast<uint32_t>(scene.lineVertices.size());
    const float x0 = scene.bounds.min[0];
    const float y0 = scene.bounds.min[1];
    const float z0 = scene.bounds.min[2];
    const float x1 = scene.bounds.max[0];
    const float y1 = scene.bounds.max[1];
    const float z1 = scene.bounds.max[2];
    constexpr Color color{0.86f, 0.88f, 0.90f};

    scene.lineVertices.push_back({x0, y0, z0, color.r, color.g, color.b});
    scene.lineVertices.push_back({x1, y0, z0, color.r, color.g, color.b});
    scene.lineVertices.push_back({x1, y1, z0, color.r, color.g, color.b});
    scene.lineVertices.push_back({x0, y1, z0, color.r, color.g, color.b});
    scene.lineVertices.push_back({x0, y0, z1, color.r, color.g, color.b});
    scene.lineVertices.push_back({x1, y0, z1, color.r, color.g, color.b});
    scene.lineVertices.push_back({x1, y1, z1, color.r, color.g, color.b});
    scene.lineVertices.push_back({x0, y1, z1, color.r, color.g, color.b});

    const uint32_t edges[] = {
        0,
        1,
        1,
        2,
        2,
        3,
        3,
        0,
        4,
        5,
        5,
        6,
        6,
        7,
        7,
        4,
        0,
        4,
        1,
        5,
        2,
        6,
        3,
        7,
    };
    for (const auto edgeIndex : edges) {
        scene.lineIndices.push_back(baseVertex + edgeIndex);
    }
}

bool appendRawMesh(
    ClusterPreviewScene& scene, const assets::MeshAsset& mesh, ClusterPreviewColorMode colorMode)
{
    for (uint32_t triangle = 0; triangle < mesh.indices.size() / 3u; ++triangle) {
        const auto baseVertex = static_cast<uint32_t>(scene.triangleVertices.size());
        for (uint32_t corner = 0; corner < 3; ++corner) {
            const auto vertexIndex = mesh.indices[triangle * 3u + corner];
            if (vertexIndex >= mesh.vertices.size()) {
                scene.success = false;
                scene.diagnostics = "raw mesh preview references vertex outside mesh";
                return false;
            }
            const auto previewVertex = makeVertex(mesh.vertices[vertexIndex],
                colorForVertex(mesh.vertices[vertexIndex], 0u, colorMode));
            scene.triangleVertices.push_back(previewVertex);
            scene.triangleIndices.push_back(baseVertex + corner);
            expandBounds(scene.bounds, previewVertex.x, previewVertex.y, previewVertex.z);
        }
        ++scene.triangleCount;
    }
    scene.clusterCount = 1;
    finalizeBounds(scene.bounds);
    appendSceneBoundsLines(scene);
    return true;
}

std::pmr::vector<const assets::VirtualizedGeometryCluster*> selectClusters(
    const assets::VirtualizedGeometryHierarchy& hierarchy, ClusterPreviewMode mode)
{
    std::pmr::vector<const assets::VirtualizedGeometryCluster*> clusters;
    switch (mode) {
    case ClusterPreviewMode::RawMesh:
        break;
    case ClusterPreviewMode::LeafClusters:
        for (const auto& cluster : hierarchy.leafClusters) {
            clusters.push_back(&cluster);
        }
        break;
    case ClusterPreviewMode::ParentClusters:
        if (!hierarchy.parentClusters.empty()) {
            uint32_t parentLevel = UINT32_MAX;
            for (const auto& cluster : hierarchy.parentClusters) {
                parentLevel = std::min(parentLevel, cluster.level);
            }
            for (const auto& cluster : hierarchy.parentClusters) {
                if (cluster.level == parentLevel) {
                    clusters.push_back(&cluster);
                }
            }
        }
        break;
    case ClusterPreviewMode::RootClusters:
        for (const auto rootId : hierarchy.rootClusterIds) {
            const auto* cluster = hierarchy.findCluster(rootId);
            if (cluster) {
                clusters.push_back(cluster);
            }
        }
        break;
    case ClusterPreviewMode::ErrorCut:
        break;
    }
    return clusters;
}

void appendErrorCutClusters(std::pmr::vector<const assets::VirtualizedGeometryCluster*>& selected,
    const assets::VirtualizedGeometryHierarchy& hierarchy,
    const assets::VirtualizedGeometryCluster& cluster,
    const ClusterPreviewCutSettings& cutSettings)
{
    constexpr float kRelativeErrorCutThreshold = 0.018f;
    if (cluster.leaf || cluster.children.empty()) {
        selected.push_back(&cluster);
        return;
    }

    const auto allowedError = std::max(cluster.bounds.radius, 0.0001f) * kRelativeErrorCutThreshold
        * cutSettings.errorScale;
    const auto hierarchyFanout =
        std::max<uint32_t>(1u, static_cast<uint32_t>(cluster.children.size()));
    const auto retainsImmediateDetail = static_cast<uint64_t>(cluster.simplifiedTriangleCount)
            * static_cast<uint64_t>(hierarchyFanout)
        >= static_cast<uint64_t>(cluster.sourceTriangleCount);
    const auto canUseSimplifiedProxy =
        cluster.simplificationInputComplete && !cluster.simplifiedIndices.empty();
    if (canUseSimplifiedProxy && cluster.simplificationError <= allowedError
        && retainsImmediateDetail) {
        selected.push_back(&cluster);
        return;
    }

    for (const auto childId : cluster.children) {
        const auto* child = hierarchy.findCluster(childId);
        if (child != nullptr) {
            appendErrorCutClusters(selected, hierarchy, *child, cutSettings);
        }
    }
}

std::pmr::vector<const assets::VirtualizedGeometryCluster*> selectErrorCutClusters(
    const assets::VirtualizedGeometryHierarchy& hierarchy,
    const ClusterPreviewCutSettings& cutSettings)
{
    std::pmr::vector<const assets::VirtualizedGeometryCluster*> clusters;
    for (const auto rootId : hierarchy.rootClusterIds) {
        const auto* root = hierarchy.findCluster(rootId);
        if (root != nullptr) {
            appendErrorCutClusters(clusters, hierarchy, *root, cutSettings);
        }
    }
    return clusters;
}

} // namespace

ClusterPreviewScene buildClusterPreviewScene(const assets::MeshAsset& mesh,
    const assets::VirtualizedGeometryHierarchy& hierarchy,
    ClusterPreviewMode mode,
    ClusterPreviewColorMode colorMode,
    ClusterPreviewCutSettings cutSettings)
{
    ClusterPreviewScene scene;
    scene.mode = mode;
    if (!mesh.validForIndexedDraw()) {
        scene.diagnostics = "cluster preview requires a valid indexed mesh";
        return scene;
    }

    if (mode == ClusterPreviewMode::RawMesh) {
        scene.success = true;
        if (!appendRawMesh(scene, mesh, colorMode)) {
            return scene;
        }
        return scene;
    }

    if (mode == ClusterPreviewMode::ErrorCut
        && (!std::isfinite(cutSettings.errorScale) || cutSettings.errorScale <= 0.0f)) {
        scene.diagnostics = "cluster preview cut error scale must be finite and positive";
        return scene;
    }

    const auto clusters = mode == ClusterPreviewMode::ErrorCut
        ? selectErrorCutClusters(hierarchy, cutSettings)
        : selectClusters(hierarchy, mode);
    if (clusters.empty()) {
        scene.diagnostics = "cluster preview requires at least one cluster";
        return scene;
    }

    scene.success = true;
    for (const auto* cluster : clusters) {
        appendClusterTriangles(scene, mesh, *cluster, colorMode);
        if (!scene.success) {
            return scene;
        }
        appendBoundsLines(scene, *cluster, colorMode);
    }
    scene.clusterCount = static_cast<uint32_t>(clusters.size());
    finalizeBounds(scene.bounds);
    return scene;
}

} // namespace arp::tools::cluster_previewer
