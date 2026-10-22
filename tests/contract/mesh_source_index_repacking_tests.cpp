#include "assets/mesh/source_index_repacking.h"

#include <cassert>
#include <cstring>

using namespace arp;

template <class Function> void rejects(Function&& function, const char* diagnostic)
{
    bool caught = false;
    try {
        function();
    } catch (const core::DiagnosticError& error) {
        caught = std::strstr(error.what(), diagnostic);
    }
    assert(caught);
}

int main()
{
    assets::MeshAsset original;
    for (unsigned i = 0; i < 12; ++i) {
        assets::MeshVertex vertex;
        vertex.position[0] = float(i);
        vertex.position[1] = float(i % 3);
        original.vertices.push_back(vertex);
        original.indices.push_back(i);
    }
    original.submeshes = {{0, 9, 7}, {9, 3, 11}};
    original.sourceRangesVersion = assets::kMeshSourceRangesVersion;
    original.sourceRanges.push_back(assets::buildMeshSourceRange(original, {0, 3, 7}, 0, 9));
    original.sourceRanges.push_back(assets::buildMeshSourceRange(original, {3, 6, 7}, 0, 0));
    original.sourceRanges.push_back(assets::buildMeshSourceRange(original, {9, 3, 11}, 1, 6));
    core::pmr::Vector<std::uint32_t> order({2, 1, 0}, &core::defaultCpuMemoryResource());
    const auto packed = assets::repackMeshSourceIndices(original, order, 4096);
    const std::uint32_t expected[] = {3, 4, 5, 6, 7, 8, 0, 1, 2, 9, 10, 11};
    assert(std::equal(packed.indices.begin(), packed.indices.end(), std::begin(expected)));
    assert(packed.ranges.size() == 3 && packed.oldToNew[0] == 1 && packed.oldToNew[1] == 0
        && packed.oldToNew[2] == 2);
    assert(packed.ranges[0].sourceFirstIndex == 0 && packed.ranges[1].sourceFirstIndex == 9);
    for (unsigned i = 0; i < 12; ++i)
        assert(original.indices[i] == i);
    assets::MeshAsset changed = original;
    changed.indices = packed.indices;
    changed.sourceRanges = packed.ranges;
    assets::validateMeshSourceRanges(changed);
    // Independently verify complete ordered vertex attributes for each source,
    // including triangle winding and corner order, rather than only counters.
    for (std::size_t i = 0; i < original.sourceRanges.size(); ++i) {
        const auto& a = original.sourceRanges[i];
        const auto& b = packed.ranges[packed.oldToNew[i]];
        for (unsigned j = 0; j < a.indices.indexCount; ++j) {
            const auto& av = original.vertices[original.indices[a.indices.firstIndex + j]];
            const auto& bv = changed.vertices[changed.indices[b.indices.firstIndex + j]];
            assert(std::memcmp(&av, &bv, sizeof(av)) == 0);
        }
    }
    rejects(
        [&] {
            assets::repackMeshSourceIndices(original, order, 1);
        },
        "REPACK-BUDGET");
    order[0] = 0;
    rejects(
        [&] {
            assets::repackMeshSourceIndices(original, order, 4096);
        },
        "ORDER-BIJECTION");
    order[0] = 3;
    rejects(
        [&] {
            assets::repackMeshSourceIndices(original, order, 4096);
        },
        "ORDER-BIJECTION");
    order.pop_back();
    rejects(
        [&] {
            assets::repackMeshSourceIndices(original, order, 4096);
        },
        "ORDER-COUNT");
    changed.sourceRanges[0].indices.firstIndex = 1;
    rejects(
        [&] {
            assets::repackMeshSourceIndices(changed, order, 4096);
        },
        "PARTITION-INVALID");
}
