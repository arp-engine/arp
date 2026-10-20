#include "assets/tangent_generation.h"

#include <cassert>
#include <cmath>

namespace {

bool near(const float a, const float b)
{
    return std::fabs(a - b) < 0.0001f;
}

} // namespace

int main()
{
    {
        arp::assets::MeshVertex vertex;
        vertex.normal[0] = 0.0f;
        vertex.normal[1] = 0.0f;
        vertex.normal[2] = 1.0f;
        assert(
            arp::assets::applyImportedTangentFrame(vertex, {2.0f, 0.0f, 1.0f}, {0.0f, 3.0f, 0.0f}));
        assert(near(vertex.tangent[0], 1.0f));
        assert(near(vertex.tangent[1], 0.0f));
        assert(near(vertex.tangent[2], 0.0f));
        assert(near(vertex.tangent[3], 1.0f));

        assert(arp::assets::applyImportedTangentFrame(
            vertex, {1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}));
        assert(near(vertex.tangent[3], -1.0f));
        assert(!arp::assets::applyImportedTangentFrame(
            vertex, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}));
    }

    arp::assets::MeshAsset mesh;
    mesh.vertices = {
        arp::assets::MeshVertex{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        arp::assets::MeshVertex{{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        arp::assets::MeshVertex{{1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        arp::assets::MeshVertex{{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};
    mesh.submeshes = {arp::assets::Submesh{0, 6, 0}};

    assert(arp::assets::generateTangentsForIndexedMesh(mesh));
    for (const auto& vertex : mesh.vertices) {
        assert(near(vertex.tangent[0], 1.0f));
        assert(near(vertex.tangent[1], 0.0f));
        assert(near(vertex.tangent[2], 0.0f));
        assert(near(vertex.tangent[3], 1.0f));
    }

    auto uncompensatedFlippedUvMesh = mesh;
    for (auto& vertex : uncompensatedFlippedUvMesh.vertices) {
        vertex.uv[1] = 1.0f - vertex.uv[1];
    }
    assert(arp::assets::generateTangentsForIndexedMesh(uncompensatedFlippedUvMesh));
    for (const auto& vertex : uncompensatedFlippedUvMesh.vertices) {
        assert(near(vertex.tangent[3], -1.0f));
    }

    auto flippedUvMesh = uncompensatedFlippedUvMesh;
    assert(arp::assets::generateTangentsForIndexedMesh(flippedUvMesh, true));
    for (const auto& vertex : flippedUvMesh.vertices) {
        assert(near(vertex.tangent[0], 1.0f));
        assert(near(vertex.tangent[1], 0.0f));
        assert(near(vertex.tangent[2], 0.0f));
        assert(near(vertex.tangent[3], 1.0f));
    }

    arp::assets::MeshAsset mixedMesh;
    mixedMesh.vertices = {
        arp::assets::MeshVertex{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        arp::assets::MeshVertex{{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        arp::assets::MeshVertex{{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        arp::assets::MeshVertex{{2.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        arp::assets::MeshVertex{{3.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        arp::assets::MeshVertex{{2.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    };
    mixedMesh.indices = {0, 1, 2, 3, 4, 5};
    mixedMesh.submeshes = {
        arp::assets::Submesh{0, 3, 0},
        arp::assets::Submesh{3, 3, 1},
    };
    for (std::size_t index = 0u; index < 3u; ++index) {
        assert(arp::assets::applyImportedTangentFrame(
            mixedMesh.vertices[index], {0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}));
    }
    assert(arp::assets::generateTangentsForIndexedMeshRange(
        mixedMesh, arp::assets::IndexedMeshTangentRange{3, 3, 3, 3}));
    for (std::size_t index = 0u; index < 3u; ++index) {
        assert(near(mixedMesh.vertices[index].tangent[0], 0.0f));
        assert(near(mixedMesh.vertices[index].tangent[1], 1.0f));
        assert(near(mixedMesh.vertices[index].tangent[3], 1.0f));
    }
    for (std::size_t index = 3u; index < mixedMesh.vertices.size(); ++index) {
        assert(near(mixedMesh.vertices[index].tangent[0], 1.0f));
        assert(near(mixedMesh.vertices[index].tangent[1], 0.0f));
        assert(near(mixedMesh.vertices[index].tangent[3], 1.0f));
    }
    assert(!arp::assets::generateTangentsForIndexedMeshRange(
        mixedMesh, arp::assets::IndexedMeshTangentRange{0, 3, 3, 3}));

    return 0;
}
