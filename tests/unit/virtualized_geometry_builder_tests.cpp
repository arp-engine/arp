#include "assets/asset_pipeline.h"
#include "assets/virtualized_geometry.h"

#include <cassert>
#include <cmath>

namespace {

arp::assets::MeshAsset makeStripMesh(uint32_t quadCount, uint32_t materialSlot)
{
    arp::assets::MeshAsset mesh;
    for (uint32_t vertex = 0; vertex <= quadCount; ++vertex) {
        const float x = static_cast<float>(vertex);
        mesh.vertices.push_back({{x, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {x, 0.0f}});
        mesh.vertices.push_back({{x, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {x, 1.0f}});
    }

    for (uint32_t quad = 0; quad < quadCount; ++quad) {
        const auto base = quad * 2u;
        mesh.indices.insert(mesh.indices.end(),
            {
                base,
                base + 1u,
                base + 2u,
                base + 1u,
                base + 3u,
                base + 2u,
            });
    }
    mesh.submeshes.push_back({0, static_cast<uint32_t>(mesh.indices.size()), materialSlot});
    return mesh;
}

arp::assets::MeshAsset makeTwoMaterialMesh()
{
    auto mesh = makeStripMesh(2, 0);
    const uint32_t secondFirstIndex = static_cast<uint32_t>(mesh.indices.size());
    const uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({{0.0f, 2.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}});
    mesh.vertices.push_back({{1.0f, 2.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}});
    mesh.vertices.push_back({{0.0f, 3.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}});
    mesh.indices.insert(mesh.indices.end(), {baseVertex, baseVertex + 1u, baseVertex + 2u});
    mesh.submeshes.push_back({secondFirstIndex, 3, 1});
    return mesh;
}

arp::assets::MeshAsset makeUvSeamMesh()
{
    arp::assets::MeshAsset mesh;
    mesh.vertices = {
        {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.5f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}},
        {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.5f}},
    };
    mesh.indices = {
        0,
        1,
        2,
        3,
        4,
        5,
    };
    mesh.submeshes.push_back({0, static_cast<uint32_t>(mesh.indices.size()), 0});
    return mesh;
}

arp::assets::MeshAsset makeQuadMesh()
{
    arp::assets::MeshAsset mesh;
    mesh.vertices = {
        {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    };
    mesh.indices = {0, 1, 2, 1, 3, 2};
    mesh.submeshes.push_back({0, static_cast<uint32_t>(mesh.indices.size()), 0});
    return mesh;
}

arp::assets::MeshAsset makeCoincidentUvIslandPairMesh()
{
    arp::assets::MeshAsset mesh;
    mesh.vertices = {
        {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.25f, 0.25f}},
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.75f, 0.25f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.25f, 0.75f}},
        {{1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.75f, 0.75f}},
    };
    mesh.indices = {
        0,
        1,
        2,
        1,
        3,
        2,
        4,
        5,
        6,
        5,
        7,
        6,
    };
    mesh.submeshes.push_back({0, static_cast<uint32_t>(mesh.indices.size()), 0});
    return mesh;
}

arp::assets::VirtualizedGeometryBuildPolicy testPolicy()
{
    arp::assets::VirtualizedGeometryBuildPolicy policy;
    policy.maxTrianglesPerLeafCluster = 2;
    policy.maxVerticesPerLeafCluster = 6;
    policy.maxClustersPerGroup = 2;
    policy.simplificationTargetNumerator = 1;
    policy.simplificationTargetDenominator = 2;
    policy.preserveMaterialBoundaries = true;
    policy.simplificationBackend =
        arp::assets::ClusterSimplificationBackend::DeterministicPrototype;
    return policy;
}

bool near(float lhs, float rhs)
{
    return std::fabs(lhs - rhs) < 0.001f;
}

} // namespace

int main()
{
    const auto source = makeStripMesh(4, 7);
    const auto built = arp::assets::buildVirtualizedGeometry(source, testPolicy());
    assert(built.success);
    assert(built.hierarchy.leafClusters.size() == 4);
    assert(built.hierarchy.parentClusters.size() == 3);
    assert(built.hierarchy.rootClusterIds.size() == 1);
    assert(built.hierarchy.hasAutomaticSimplification);
    assert(built.hierarchy.parentClusters.front().sourceTriangleCount == 4);
    assert(built.hierarchy.parentClusters.front().simplificationInputTriangleCount == 4);
    assert(built.hierarchy.parentClusters.front().simplificationInputComplete);
    assert(built.hierarchy.parentClusters.front().simplifiedTriangleCount == 2);
    assert(built.hierarchy.parentClusters.front().simplifiedVertices.size() >= 3);
    assert(built.hierarchy.parentClusters.front().simplifiedIndices.size()
        == built.hierarchy.parentClusters.front().simplifiedTriangleCount * 3u);
    for (const auto index : built.hierarchy.parentClusters.front().simplifiedIndices) {
        assert(index < built.hierarchy.parentClusters.front().simplifiedVertices.size());
    }
    assert(near(built.hierarchy.parentClusters.front().simplificationError, 0.0f));
    assert(built.hierarchy.parentClusters.front().children.size() == 2);
    assert(built.hierarchy.rootClusterIds.front() == built.hierarchy.parentClusters.back().id);
    assert(built.hierarchy.stableHash != 0);

    const auto rebuilt = arp::assets::buildVirtualizedGeometry(source, testPolicy());
    assert(rebuilt.success);
    assert(rebuilt.hierarchy.stableHash == built.hierarchy.stableHash);

    const auto materialMesh = makeTwoMaterialMesh();
    const auto materialHierarchy =
        arp::assets::buildVirtualizedGeometry(materialMesh, testPolicy());
    assert(materialHierarchy.success);
    for (const auto& parent : materialHierarchy.hierarchy.parentClusters) {
        for (const auto childId : parent.children) {
            const auto* child = materialHierarchy.hierarchy.findCluster(childId);
            assert(child);
            assert(child->materialSlot == parent.materialSlot);
        }
    }

    auto seamPolicy = testPolicy();
    seamPolicy.maxTrianglesPerLeafCluster = 1;
    seamPolicy.maxVerticesPerLeafCluster = 3;
    seamPolicy.maxClustersPerGroup = 2;
    const auto seamHierarchy = arp::assets::buildVirtualizedGeometry(makeUvSeamMesh(), seamPolicy);
    assert(seamHierarchy.success);
    assert(seamHierarchy.hierarchy.parentClusters.size() == 1);
    assert(seamHierarchy.hierarchy.parentClusters.front().sourceTriangleCount == 2);
    assert(seamHierarchy.hierarchy.parentClusters.front().simplifiedTriangleCount == 2);

    auto uvIslandPolicy = testPolicy();
    uvIslandPolicy.maxTrianglesPerLeafCluster = 2;
    uvIslandPolicy.maxVerticesPerLeafCluster = 4;
    uvIslandPolicy.maxClustersPerGroup = 2;
    const auto uvIslandHierarchy =
        arp::assets::buildVirtualizedGeometry(makeCoincidentUvIslandPairMesh(), uvIslandPolicy);
    assert(uvIslandHierarchy.success);
    assert(uvIslandHierarchy.hierarchy.parentClusters.size() == 1);
    assert(uvIslandHierarchy.hierarchy.parentClusters.front().sourceTriangleCount == 4);
    assert(uvIslandHierarchy.hierarchy.parentClusters.front().simplifiedTriangleCount == 2);

    auto quadPolicy = testPolicy();
    quadPolicy.maxTrianglesPerLeafCluster = 1;
    quadPolicy.maxVerticesPerLeafCluster = 3;
    quadPolicy.maxClustersPerGroup = 2;
    const auto quadHierarchy = arp::assets::buildVirtualizedGeometry(makeQuadMesh(), quadPolicy);
    assert(quadHierarchy.success);
    assert(quadHierarchy.hierarchy.parentClusters.size() == 1);
    assert(quadHierarchy.hierarchy.parentClusters.front().simplifiedTriangleCount == 1);
    assert(quadHierarchy.hierarchy.parentClusters.front().simplificationError > 0.0f);

    auto invalidPolicy = testPolicy();
    invalidPolicy.maxTrianglesPerLeafCluster = 0;
    const auto invalidPolicyBuild = arp::assets::buildVirtualizedGeometry(source, invalidPolicy);
    assert(!invalidPolicyBuild.success);
    assert(invalidPolicyBuild.diagnostics.find("maxTrianglesPerLeafCluster")
        != std::pmr::string::npos);

    auto invalidSimplificationBudget = testPolicy();
    invalidSimplificationBudget.maxTrianglesPerSimplificationPass = 2;
    const auto invalidBudgetBuild =
        arp::assets::buildVirtualizedGeometry(source, invalidSimplificationBudget);
    assert(!invalidBudgetBuild.success);
    assert(invalidBudgetBuild.diagnostics.find("maxTrianglesPerSimplificationPass")
        != std::pmr::string::npos);

    auto cappedSimplificationPolicy = testPolicy();
    cappedSimplificationPolicy.maxTrianglesPerSimplificationPass = 3;
    const auto cappedSimplificationBuild =
        arp::assets::buildVirtualizedGeometry(source, cappedSimplificationPolicy);
    assert(cappedSimplificationBuild.success);
    assert(
        cappedSimplificationBuild.hierarchy.parentClusters.front().simplificationInputTriangleCount
        == 3);
    assert(!cappedSimplificationBuild.hierarchy.parentClusters.front().simplificationInputComplete);

    arp::assets::MeshAsset invalidMesh;
    invalidMesh.vertices = source.vertices;
    invalidMesh.indices = {0, 1, 999};
    invalidMesh.submeshes.push_back({0, 3, 0});
    const auto invalidMeshBuild = arp::assets::buildVirtualizedGeometry(invalidMesh, testPolicy());
    assert(!invalidMeshBuild.success);
    assert(invalidMeshBuild.diagnostics.find("indexed draw") != std::pmr::string::npos);

    return 0;
}
