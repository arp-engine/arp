#include "assets/mesh_section_grouping.h"
#include "renderer/mesh/mesh_gpu_buffer.h"
#include "rhi/backends/null/null_backend.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
using namespace arp;

assets::MeshAsset fixture()
{
    assets::MeshAsset mesh;
    // Interleave distant triangles, preserve duplicate oriented faces and
    // different material assignments. These are already world-space values.
    for (unsigned triangle = 0; triangle < 12u; ++triangle) {
        const float offset = triangle % 2u ? 100.f : -100.f;
        for (unsigned lane = 0; lane < 3u; ++lane) {
            assets::MeshVertex vertex;
            vertex.position[0] = offset + (lane == 1u ? 2.f : 0.f);
            vertex.position[1] = float(triangle) + (lane == 2u ? .25f : 0.f);
            vertex.position[2] = lane == 2u ? 3.f : 0.f;
            vertex.uv[0] = float(triangle) / 12.f;
            vertex.tangent[3] = triangle % 2u ? -1.f : 1.f;
            mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
            mesh.vertices.push_back(vertex);
        }
    }
    mesh.indices.insert(mesh.indices.end(), {0, 1, 2, 2, 1, 0});
    mesh.submeshes = {{0, 18, 7}, {18, 18, 3}, {36, 6, 7}};
    assert(mesh.recomputeBounds());
    return mesh;
}

auto orientedFaces(const assets::MeshAsset& mesh)
{
    core::pmr::Vector<std::array<std::uint32_t, 4>> faces;
    std::uint32_t covered = 0;
    for (const auto& section : mesh.submeshes) {
        assert(section.firstIndex == covered && section.indexCount % 3u == 0u);
        for (unsigned i = 0; i < section.indexCount; i += 3u) {
            const auto first = section.firstIndex + i;
            faces.push_back({section.materialSlot,
                mesh.indices[first],
                mesh.indices[first + 1u],
                mesh.indices[first + 2u]});
        }
        covered += section.indexCount;
    }
    assert(covered == mesh.indices.size());
    std::sort(faces.begin(), faces.end());
    return faces;
}

void expectAtomicFailure(
    assets::MeshAsset mesh, assets::MeshSectionSpatialPartition policy, const char* diagnostic)
{
    const auto original = mesh;
    bool rejected = false;
    try {
        assets::partitionMeshSectionsSpatially(mesh, policy);
    } catch (const std::exception& error) {
        rejected = std::strstr(error.what(), diagnostic) != nullptr;
    }
    assert(rejected);
    assert(mesh.indices == original.indices);
    assert(mesh.submeshes.size() == original.submeshes.size());
    assert(std::memcmp(mesh.vertices.data(),
               original.vertices.data(),
               mesh.vertices.size() * sizeof(assets::MeshVertex))
        == 0);
    for (std::size_t i = 0; i < mesh.submeshes.size(); ++i) {
        assert(mesh.submeshes[i].firstIndex == original.submeshes[i].firstIndex);
        assert(mesh.submeshes[i].indexCount == original.submeshes[i].indexCount);
        assert(mesh.submeshes[i].materialSlot == original.submeshes[i].materialSlot);
    }
}
} // namespace

int main()
{
    const auto original = fixture();
    auto first = original;
    assets::groupMeshSectionsByMaterial(first);
    assets::partitionMeshSectionsSpatially(first, {2u, 16u});
    assert(first.submeshes.size() <= 16u);
    assert(orientedFaces(first) == orientedFaces(original));
    assert(std::memcmp(first.vertices.data(),
               original.vertices.data(),
               first.vertices.size() * sizeof(assets::MeshVertex))
        == 0);
    for (const auto& section : first.submeshes)
        assert(section.indexCount <= 6u);
    auto repeat = original;
    assets::groupMeshSectionsByMaterial(repeat);
    assets::partitionMeshSectionsSpatially(repeat, {2u, 16u});
    assert(repeat.indices == first.indices);
    assert(orientedFaces(repeat) == orientedFaces(first));

    // Metadata errors, capacity exhaustion and malformed sections must not
    // leave partially reordered data that a later upload could consume.
    expectAtomicFailure(original, {0u, 16u}, "MESH-SECTION-SPATIAL-POLICY-INVALID");
    expectAtomicFailure(original, {2u, 0u}, "MESH-SECTION-SPATIAL-POLICY-INVALID");
    expectAtomicFailure(original, {2u, 1u}, "TRIANGLE-CLUSTER-CAPACITY-EXCEEDED");
    auto gap = original;
    gap.submeshes[1].firstIndex++;
    expectAtomicFailure(gap, {2u, 16u}, "MESH-SECTION-GROUPING-NONPARTITIONED-INDICES");
    auto nonfinite = original;
    nonfinite.vertices[0].position[0] = std::numeric_limits<float>::infinity();
    expectAtomicFailure(nonfinite, {2u, 16u}, "TRIANGLE-CLUSTER-POSITION-INVALID");

    // Null validates the exact ordinary vertex/index ABI consumed by all
    // backend indexed-draw lowerings. This is not native GPU evidence.
    auto device = rhi::createNullDevice();
    assert(device);
    auto uploaded = renderer::uploadMeshToRhi(*device, first);
    assert(uploaded.success);
    assert(uploaded.mesh.submeshBounds().size() == first.submeshes.size());
    for (std::size_t s = 0; s < first.submeshes.size(); ++s) {
        const auto& bounds = uploaded.mesh.submeshBounds()[s];
        assert(bounds.valid && std::isfinite(bounds.radius));
        const auto& section = first.submeshes[s];
        for (unsigned i = 0; i < section.indexCount; ++i) {
            const auto& v = first.vertices[first.indices[section.firstIndex + i]];
            const float dx = v.position[0] - bounds.center.x;
            const float dy = v.position[1] - bounds.center.y;
            const float dz = v.position[2] - bounds.center.z;
            assert(dx * dx + dy * dy + dz * dz <= bounds.radius * bounds.radius + .001f);
        }
    }
}
