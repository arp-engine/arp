#pragma once

#include "assets/asset_pipeline.h"
#include "core/api.h"

#include <cstdint>
#include <string>
#include <vector>

namespace arp::assets {

enum class ClusterSimplificationBackend { DeterministicPrototype };

struct ClusterBounds {
    float min[3]{};
    float max[3]{};
    float center[3]{};
    float radius = 0.0f;
    bool valid = false;
};

struct VirtualizedGeometryBuildPolicy {
    uint32_t maxTrianglesPerLeafCluster = 0;
    uint32_t maxVerticesPerLeafCluster = 0;
    uint32_t maxClustersPerGroup = 0;
    uint32_t simplificationTargetNumerator = 0;
    uint32_t simplificationTargetDenominator = 0;
    uint32_t maxTrianglesPerSimplificationPass = 0;
    bool preserveMaterialBoundaries = true;
    bool preserveTextureCoordinateSeams = true;
    float textureCoordinateErrorWeight = 1.0f;
    ClusterSimplificationBackend simplificationBackend =
        ClusterSimplificationBackend::DeterministicPrototype;
};

struct VirtualizedGeometryCluster {
    uint32_t id = 0;
    uint32_t level = 0;
    uint32_t materialSlot = 0;
    uint32_t sourceTriangleCount = 0;
    uint32_t simplificationInputTriangleCount = 0;
    uint32_t simplifiedTriangleCount = 0;
    uint32_t parentClusterId = 0;
    float simplificationError = 0.0f;
    bool simplificationInputComplete = false;
    bool leaf = true;
    ClusterBounds bounds;
    std::pmr::vector<uint32_t> sourceTriangleIndices;
    std::pmr::vector<uint32_t> representativeTriangleIndices;
    std::pmr::vector<MeshVertex> simplifiedVertices;
    std::pmr::vector<uint32_t> simplifiedIndices;
    std::pmr::vector<uint32_t> children;
};

struct ARP_API VirtualizedGeometryHierarchy {
    std::pmr::vector<VirtualizedGeometryCluster> leafClusters;
    std::pmr::vector<VirtualizedGeometryCluster> parentClusters;
    std::pmr::vector<uint32_t> rootClusterIds;
    uint64_t stableHash = 0;
    bool hasAutomaticSimplification = false;

    const VirtualizedGeometryCluster* findCluster(uint32_t clusterId) const;
};

struct VirtualizedGeometryBuildResult {
    bool success = false;
    std::pmr::string diagnostics;
    VirtualizedGeometryHierarchy hierarchy;
};

class ARP_API TriangleAdjacencyBuilder {
public:
    struct TriangleRecord {
        uint32_t triangleIndex = 0;
        uint32_t materialSlot = 0;
        uint32_t indices[3]{};
    };

    struct Result {
        bool success = false;
        std::pmr::string diagnostics;
        std::pmr::vector<TriangleRecord> triangles;
        std::pmr::vector<std::pmr::vector<uint32_t>> neighbors;
    };

    Result build(const MeshAsset& mesh) const;
};

class ARP_API LeafClusterBuilder {
public:
    struct Result {
        bool success = false;
        std::pmr::string diagnostics;
        std::pmr::vector<VirtualizedGeometryCluster> clusters;
    };

    Result build(const MeshAsset& mesh,
        const TriangleAdjacencyBuilder::Result& adjacency,
        const VirtualizedGeometryBuildPolicy& policy,
        uint32_t& nextClusterId) const;
};

class ARP_API ClusterSimplifier {
public:
    VirtualizedGeometryCluster simplify(const MeshAsset& mesh,
        const std::pmr::vector<uint32_t>& sourceTriangleIndices,
        const std::pmr::vector<uint32_t>& simplificationTriangleIndices,
        const std::pmr::vector<uint32_t>& childClusterIds,
        uint32_t clusterId,
        uint32_t level,
        uint32_t materialSlot,
        const VirtualizedGeometryBuildPolicy& policy) const;
};

class ARP_API ClusterHierarchyBuilder {
public:
    VirtualizedGeometryBuildResult build(const MeshAsset& mesh,
        std::pmr::vector<VirtualizedGeometryCluster> leafClusters,
        const VirtualizedGeometryBuildPolicy& policy) const;
};

ARP_API VirtualizedGeometryBuildResult buildVirtualizedGeometry(
    const MeshAsset& mesh, const VirtualizedGeometryBuildPolicy& policy);

} // namespace arp::assets
