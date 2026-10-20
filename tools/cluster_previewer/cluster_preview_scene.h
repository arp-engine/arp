#pragma once

#include "assets/asset_pipeline.h"
#include "assets/virtualized_geometry.h"

#include <cstdint>
#include <string>
#include <vector>

namespace arp::tools::cluster_previewer {

enum class ClusterPreviewMode { RawMesh, LeafClusters, ParentClusters, RootClusters, ErrorCut };

enum class ClusterPreviewColorMode { Shaded, ClusterColor, Textured };

struct ClusterPreviewCutSettings {
    float errorScale = 3.0f;
};

struct PreviewBounds {
    float min[3]{};
    float max[3]{};
    float center[3]{};
    float radius = 0.0f;
    bool valid = false;
};

struct PreviewVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float u = 0.0f;
    float v = 0.0f;
};

struct ClusterPreviewScene {
    bool success = false;
    std::pmr::string diagnostics;
    ClusterPreviewMode mode = ClusterPreviewMode::LeafClusters;
    uint32_t clusterCount = 0;
    uint32_t triangleCount = 0;
    std::pmr::vector<PreviewVertex> triangleVertices;
    std::pmr::vector<uint32_t> triangleIndices;
    std::pmr::vector<PreviewVertex> lineVertices;
    std::pmr::vector<uint32_t> lineIndices;
    PreviewBounds bounds;
};

ClusterPreviewScene buildClusterPreviewScene(const assets::MeshAsset& mesh,
    const assets::VirtualizedGeometryHierarchy& hierarchy,
    ClusterPreviewMode mode,
    ClusterPreviewColorMode colorMode = ClusterPreviewColorMode::ClusterColor,
    ClusterPreviewCutSettings cutSettings = {});

} // namespace arp::tools::cluster_previewer
