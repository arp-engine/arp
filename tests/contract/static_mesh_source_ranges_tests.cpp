#include "assets/mesh_section_grouping.h"
#include "renderer/mesh/mesh_gpu_buffer.h"
#include "renderer/mesh/static_mesh_range_selection.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"

#include <array>
#include <cassert>
#include <cstring>
#include <limits>
#include <random>

namespace {
using namespace arp;

assets::MeshAsset fixture()
{
    assets::MeshAsset mesh;
    for (float x : {-10.f, 0.f, .5f, 10.f, 30.f}) {
        const auto begin = static_cast<std::uint32_t>(mesh.indices.size());
        for (unsigned lane = 0; lane < 3u; ++lane) {
            assets::MeshVertex vertex;
            // World-baked nonuniform/mirrored vertices and tangent handedness
            // must survive grouping, metadata extraction and Null upload.
            vertex.position[0] = x - (lane == 1u ? .2f : 0.f);
            vertex.position[1] = lane == 2u ? .3f : 0.f;
            vertex.position[2] = .5f;
            vertex.normal[0] = -2.f;
            vertex.normal[1] = 3.f;
            vertex.tangent[3] = -1.f;
            mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
            mesh.vertices.push_back(vertex);
        }
        mesh.submeshes.push_back({begin, 3u, 7u});
    }
    assert(mesh.recomputeBounds());
    return mesh;
}

template <class Function> void rejects(Function&& function, const char* id)
{
    bool failed = false;
    try {
        function();
    } catch (const std::exception& error) {
        failed = std::strstr(error.what(), id) != nullptr;
    }
    assert(failed);
}

void clipArithmetic()
{
    std::mt19937 random(1001u);
    std::uniform_real_distribution<float> number(-8.f, 8.f);
    for (unsigned matrixIndex = 0; matrixIndex < 80u; ++matrixIndex) {
        float m[16];
        for (auto& value : m)
            value = number(random);
        core::ConservativeClipVolume volume(m);
        for (unsigned sample = 0; sample < 600u; ++sample) {
            const float point[3] = {number(random), number(random), number(random)};
            float clip[4], fused[4];
            for (unsigned row = 0; row < 4u; ++row) {
                // Independent scalar float shader evaluation; contraction and
                // FMA form are both checked against the rejection proof.
                volatile float a = m[row * 4u] * point[0];
                volatile float b = m[row * 4u + 1u] * point[1];
                volatile float c = m[row * 4u + 2u] * point[2];
                clip[row] = ((a + b) + c) + m[row * 4u + 3u];
                fused[row] = std::fma(m[row * 4u],
                    point[0],
                    std::fma(m[row * 4u + 1u],
                        point[1],
                        std::fma(m[row * 4u + 2u], point[2], m[row * 4u + 3u])));
            }
            const bool visible = clip[0] >= -clip[3] && clip[0] <= clip[3] && clip[1] >= -clip[3]
                && clip[1] <= clip[3] && clip[2] >= 0 && clip[2] <= clip[3];
            const bool fusedVisible = fused[0] >= -fused[3] && fused[0] <= fused[3]
                && fused[1] >= -fused[3] && fused[1] <= fused[3] && fused[2] >= 0
                && fused[2] <= fused[3];
            if (visible || fusedVisible)
                assert(volume.intersects(point, point));
        }
    }
    // Rounding at each canonical boundary must not cull a point that the
    // float shader keeps. The absolute error term also covers FTZ inputs.
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    core::ConservativeClipVolume identityVolume(identity);
    for (float x :
        {-1.f, 1.f, std::nextafter(1.f, 2.f), std::numeric_limits<float>::denorm_min()}) {
        const float point[3] = {x, 0.f, std::numeric_limits<float>::denorm_min()};
        if (x <= 1.f)
            assert(identityVolume.intersects(point, point));
    }
    const float beyond[3] = {1.01f, 0.f, .5f};
    assert(!identityVolume.intersects(beyond, beyond));
    rejects(
        [] {
            core::ConservativeClipVolume missing(nullptr);
        },
        "MATRIX-MISSING");
    float invalidMatrix[16]{};
    rejects(
        [&] {
            core::ConservativeClipVolume invalid(invalidMatrix);
        },
        "PLANE-DEGENERATE");
    invalidMatrix[0] = std::numeric_limits<float>::quiet_NaN();
    rejects(
        [&] {
            core::ConservativeClipVolume invalid(invalidMatrix);
        },
        "MATRIX-NONFINITE");
    const float invalidBounds[3] = {std::numeric_limits<float>::infinity(), 0.f, 0.f};
    rejects(
        [&] {
            identityVolume.intersects(invalidBounds, invalidBounds);
        },
        "BOUNDS-INVALID");
    const float reversed[3] = {2.f, 2.f, 2.f};
    rejects(
        [&] {
            identityVolume.intersects(reversed, beyond);
        },
        "BOUNDS-INVALID");
    std::copy_n(identity, 16u, invalidMatrix);
    invalidMatrix[0] = std::numeric_limits<float>::max();
    const core::ConservativeClipVolume overflowVolume(invalidMatrix);
    rejects(
        [&] {
            overflowVolume.intersects(reversed, reversed);
        },
        "EVALUATION-OVERFLOW");
}
} // namespace

int main()
{
    auto mesh = fixture();
    const auto original = mesh;
    assets::groupMeshSectionsByMaterial(mesh, true);
    assert(mesh.sourceRangesVersion == assets::kMeshSourceRangesVersion);
    assert(mesh.sourceRanges.size() == 5u && mesh.submeshes.size() == 1u);
    assert(mesh.indices == original.indices);
    assert(std::memcmp(mesh.vertices.data(),
               original.vertices.data(),
               mesh.vertices.size() * sizeof(assets::MeshVertex))
        == 0);
    assets::validateMeshSourceRanges(mesh);
    for (unsigned i = 0; i < 5u; ++i) {
        const auto& range = mesh.sourceRanges[i];
        assert(range.sourceFirstIndex == i * 3u && range.indices.firstIndex == i * 3u);
        assert(range.parentSubmesh == 0 && range.indices.materialSlot == 7u);
        assert(range.maxAbsNormal[0] == 2.f && range.maxAbsNormal[1] == 3.f);
    }
    auto reordered = original;
    reordered.submeshes[0].materialSlot = 9u;
    reordered.submeshes[2].materialSlot = 2u;
    assets::groupMeshSectionsByMaterial(reordered, true);
    assets::validateMeshSourceRanges(reordered);
    assert(reordered.sourceRanges.front().sourceFirstIndex == 6u);
    assert(reordered.sourceRanges.back().sourceFirstIndex == 0u);
    assert(std::memcmp(reordered.vertices.data(),
               original.vertices.data(),
               original.vertices.size() * sizeof(assets::MeshVertex))
        == 0);
    auto invalid = mesh;
    invalid.vertices[0].position[0] = 100.f;
    rejects(
        [&] {
            assets::validateMeshSourceRanges(invalid);
        },
        "BOUNDS-NOT-CONSERVATIVE");
    invalid = mesh;
    invalid.sourceRanges[0].indices.firstIndex = 1u;
    rejects(
        [&] {
            assets::validateMeshSourceRanges(invalid);
        },
        "PARTITION-INVALID");
    invalid = mesh;
    invalid.sourceRangesVersion = 2u;
    rejects(
        [&] {
            assets::validateMeshSourceRanges(invalid);
        },
        "VERSION-INVALID");
    rejects(
        [&] {
            assets::partitionMeshSectionsSpatially(mesh, {2u, 32u});
        },
        "INCOMPATIBLE");
    invalid = original;
    invalid.vertices[0].normal[0] = std::numeric_limits<float>::quiet_NaN();
    rejects(
        [&] {
            assets::groupMeshSectionsByMaterial(invalid, true);
        },
        "GEOMETRY-NONFINITE");
    assert(invalid.indices == original.indices && invalid.sourceRanges.empty());

    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::array<float, 16u> shifted;
    std::copy_n(identity, 16u, shifted.begin());
    shifted[3] = -10.f;
    const core::ConservativeClipVolume camera(identity);
    const std::array<core::ConservativeClipVolume, 3u> cascades = {
        core::ConservativeClipVolume(shifted.data()), camera, camera};
    core::pmr::Vector<core::IndexRun> output;
    output.reserve(20u);
    const auto plan = renderer::StaticMeshRangeSelection::append(
        mesh.sourceRanges, 0u, camera, cascades.data(), cascades.size(), 0.f, output, 20u);
    assert(plan.selectedSources[0] == 2u && plan.views[0].count == 1u);
    assert(output[plan.views[0].first].first == 3u && output[plan.views[0].first].count == 6u);
    assert(plan.selectedSources[1] == 1u && output[plan.views[1].first].first == 9u);
    assert(plan.triangles[0] == 2u && plan.triangles[1] == 1u);
    // Geometry displaced by the actual unnormalized static normal bias must
    // remain eligible even when its original bounds are outside the volume.
    output.clear();
    const auto biased = renderer::StaticMeshRangeSelection::append(
        mesh.sourceRanges, 0u, camera, cascades.data(), cascades.size(), 5.f, output, 20u);
    assert(biased.selectedSources[2] >= 3u);
    auto far = shifted;
    far[3] = -1000.f;
    output.clear();
    const auto empty = renderer::StaticMeshRangeSelection::append(mesh.sourceRanges,
        0u,
        core::ConservativeClipVolume(far.data()),
        nullptr,
        0u,
        0.f,
        output,
        20u);
    assert(empty.views[0].count == 0 && output.empty());
    rejects(
        [&] {
            renderer::StaticMeshRangeSelection::append(
                mesh.sourceRanges, 0u, camera, cascades.data(), cascades.size(), 0.f, output, 0u);
        },
        "BUDGET-INVALID");

    auto device = rhi::createNullDevice();
    auto uploaded = renderer::uploadMeshToRhi(*device, mesh);
    assert(uploaded.success && uploaded.mesh.sourceRangesVersion() == 1u);
    assert(uploaded.mesh.sourceRanges().size() == 5u);
    invalid = mesh;
    invalid.vertices[0].position[0] = 100.f;
    assert(!renderer::uploadMeshToRhi(*device, invalid).success);
    rhi::NullHiddenFrameSmokeRunner runner(*device);
    const auto smoke = runner.run({});
    assert(smoke.success && smoke.submittedFrameCount == 2u);
    assert(smoke.descriptorCompletenessValidated && smoke.resourceStats.pendingDestroyCount == 0u);
    for (const auto& frame : smoke.frames)
        assert(frame.presentSucceeded && frame.diagnostics.empty());
    clipArithmetic();
}
