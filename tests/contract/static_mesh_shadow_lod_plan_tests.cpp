#include "renderer/shadows/static_mesh_shadow_lod_plan.h"

#include <cassert>
#include <cstring>
#include <iostream>

using Plan = arp::renderer::StaticMeshShadowLodPlan;
using namespace arp;
constexpr std::array<float, 16> identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

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

void checkIndexCohorts()
{
    core::pmr::Vector<assets::MeshShadowLodSource> sources{&core::defaultCpuMemoryResource()};
    for (unsigned n = 0; n < 3; ++n) {
        assets::MeshShadowLodSource source;
        source.node = n;
        source.approximable = true;
        source.worldAffine = identity;
        source.localAbsoluteSupport = {1, 1, 1};
        source.surfaceError = {0, .0001, n == 1 ? .1 : .0002};
        for (unsigned level = 0; level < 3; ++level) {
            auto& range = source.ranges[level];
            range.parentSubmesh = (n == 2 ? 10 : 0) + level;
            range.indices = {0, level == 0 ? 12u : level == 1 ? 6u : 3u, n == 2 ? 1u : 0u};
            // Source 0 permits L2 but not L1 after its normal-bias envelope.
            // Source 1 permits L1 but not L2. Their common eligible level is L0.
            if (n == 0 && level == 1)
                std::fill(std::begin(range.maxAbsNormal), std::end(range.maxAbsNormal), 100.f);
        }
        sources.push_back(source);
    }
    for (unsigned level = 0; level < 3; ++level) {
        const unsigned order[3][3] = {{0, 1, 2}, {1, 0, 2}, {0, 1, 2}};
        unsigned offset = level * 99;
        for (const auto n : order[level]) {
            auto& range = sources[n].ranges[level];
            range.indices.firstIndex = offset;
            offset += range.indices.indexCount;
        }
    }
    std::array<Plan::Cascade, 3> cascades;
    for (auto& cascade : cascades) {
        cascade.viewProjection = identity;
        cascade.viewProjection[10] = .001f;
        cascade.width = cascade.height = 1000;
    }
    const auto verify = [&](const Plan& plan, const std::array<unsigned, 3>& expected) {
        unsigned visits[3][3]{};
        for (const auto& carrier : plan.carriers())
            for (unsigned c = 0; c < 3; ++c) {
                const auto& runs = plan.runs();
                core::CheckedIndexRunSpan checked(runs.data(),
                    runs.size(),
                    carrier.views[c],
                    {carrier.parent.firstIndex, carrier.parent.indexCount},
                    UINT32_MAX);
                for (const auto& run : checked)
                    for (unsigned n = 0; n < sources.size(); ++n) {
                        const auto& range = sources[n].ranges[carrier.level];
                        if (range.parentSubmesh == carrier.parentSubmesh
                            && range.indices.firstIndex >= run.first
                            && std::uint64_t(range.indices.firstIndex) + range.indices.indexCount
                                <= run.first + run.count) {
                            assert(expected[n] == carrier.level);
                            ++visits[c][n];
                        }
                    }
            }
        for (const auto& cascade : visits)
            for (const auto count : cascade)
                assert(count == 1);
    };
    Plan plan;
    plan.prepare(sources, {3, 2, 9}, 1, .5);
    assert(plan.update(cascades, .01f));
    verify(plan, {2, 1, 2});
    // The third source belongs to a separate original parent/material; even an
    // unbounded-looking explicit limit must not pull it into the first cohort.
    for (const auto size : {2u, 3u, UINT32_MAX}) {
        plan.prepare(sources, {3, 2, 9}, 1, .5, {1, size});
        assert(plan.update(cascades, .01f));
        verify(plan, {0, 0, 2});
        assert(plan.statistics().triangles[0] == 9);
        assert(plan.statistics().runs[0] == 2);
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 100; ++i) {
            assert(!plan.update(cascades, .01f));
            cascades[0].viewProjection[3] += .0001f;
            assert(plan.update(cascades, .01f));
            verify(plan, {0, 0, 2});
        }
        assert(before.allocationCount == core::defaultCpuAllocator().stats().allocationCount);
        // Changed bias changes eligible intersections and must invalidate cache.
        assert(plan.update(cascades, 0));
        verify(plan, {1, 1, 2});
    }
    // Explicit size 1 is identical to the existing independent policy.
    plan.prepare(sources, {3, 2, 9}, 1, .5, {1, 1});
    assert(plan.update(cascades, .01f));
    verify(plan, {2, 1, 2});
    for (const auto policy : {Plan::IndexCohortPolicy{0, 2}, Plan::IndexCohortPolicy{1, 0}}) {
        rejects(
            [&] {
                plan.prepare(sources, {3, 2, 9}, 1, .5, policy);
            },
            "COHORT-POLICY-INVALID");
        assert(!plan.valid());
        rejects(
            [&] {
                plan.update(cascades, 0);
            },
            "NOT-PREPARED");
    }
}

int main()
{
    checkIndexCohorts();
    core::pmr::Vector<assets::MeshShadowLodSource> sources{&core::defaultCpuMemoryResource()};
    for (unsigned n = 0; n < 5; ++n) {
        assets::MeshShadowLodSource source;
        source.node = n;
        source.worldAffine = identity;
        source.approximable = n < 3;
        source.localAbsoluteSupport = {1, 1, 1};
        constexpr double errors[][3] = {{0, .001, .004}, {0, .015, .06}, {0, .04, .2}};
        if (source.approximable)
            std::copy(std::begin(errors[n]), std::end(errors[n]), source.surfaceError.begin());
        for (unsigned level = 0; level < 3; ++level) {
            auto& range = source.ranges[level];
            range.parentSubmesh = n == 4 ? 100 + level : 10 + level;
            range.indices.materialSlot = n == 4 ? 1 : 0;
            range.indices.indexCount = source.approximable ? (12 - level * 3) : 12;
            range.bounds.valid = true;
            for (unsigned a = 0; a < 3; ++a)
                range.maxAbsNormal[a] = 1;
        }
        sources.push_back(source);
    }
    // Repack/reorder levels independently. The planner must use each bank's
    // intervals rather than pairing ranges by position or assuming offsets.
    const unsigned order[3][5] = {{0, 1, 2, 3, 4}, {2, 0, 3, 1, 4}, {1, 3, 2, 0, 4}};
    for (unsigned level = 0; level < 3; ++level) {
        unsigned offset = level * 99;
        for (const auto node : order[level]) {
            auto& range = sources[node].ranges[level];
            range.indices.firstIndex = offset;
            offset += range.indices.indexCount;
        }
    }
    Plan plan;
    plan.prepare(sources, {5, 2, 15}, 1, .5);
    std::array<Plan::Cascade, 3> cascades;
    for (unsigned c = 0; c < 3; ++c) {
        cascades[c].viewProjection = identity;
        const float scale[] = {1, .1f, .01f};
        cascades[c].viewProjection[0] = cascades[c].viewProjection[5] = scale[c];
        cascades[c].viewProjection[10] = .001f;
        cascades[c].width = cascades[c].height = 1000;
    }
    assert(plan.update(cascades, 0));
    const unsigned expected[3][5] = {{1, 0, 0, 0, 0}, {2, 1, 0, 0, 0}, {2, 2, 1, 0, 0}};
    const auto validate = [&] {
        unsigned visited[3][5]{};
        for (const auto& carrier : plan.carriers()) {
            for (unsigned c = 0; c < 3; ++c) {
                const auto& runs = plan.runs();
                core::CheckedIndexRunSpan checked(runs.data(),
                    runs.size(),
                    carrier.views[c],
                    {carrier.parent.firstIndex, carrier.parent.indexCount},
                    UINT32_MAX);
                for (const auto& run : checked)
                    for (unsigned n = 0; n < sources.size(); ++n) {
                        const auto& range = sources[n].ranges[carrier.level];
                        if (range.parentSubmesh != carrier.parentSubmesh)
                            continue;
                        if (range.indices.firstIndex >= run.first
                            && std::uint64_t(range.indices.firstIndex) + range.indices.indexCount
                                <= run.first + run.count) {
                            assert(expected[c][n] == carrier.level);
                            ++visited[c][n];
                        }
                    }
            }
        }
        for (const auto& cascade : visited)
            for (const auto count : cascade)
                assert(count == 1);
    };
    validate();
    assert(plan.carriers().size() == 4); // two L0 materials + two rock candidates
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 200; ++i) {
        assert(!plan.update(cascades, 0));
        cascades[0].viewProjection[3] = i * .001f;
        plan.update(cascades, 0);
        validate();
    }
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.allocationCount == after.allocationCount);
    // Changed viewport and bias cannot reuse a stale choice.
    cascades[0].width = 2000;
    assert(plan.update(cascades, .1f));
    assert(plan.statistics().sources[0][0] == 5);
    cascades[0].viewProjection[15] = 0;
    rejects(
        [&] {
            plan.update(cascades, 0);
        },
        "AFFINE-W-ONE-REQUIRED");
    assert(!plan.valid());
    rejects(
        [&] {
            plan.carriers();
        },
        "NOT-VALID");
    cascades[0].viewProjection[15] = 1;
    assert(plan.update(cascades, 0));
    rejects(
        [&] {
            plan.prepare(sources, {4, 2, 15}, 1, .5);
        },
        "BUDGET-INVALID");
    assert(!plan.valid());
    rejects(
        [&] {
            plan.update(cascades, 0);
        },
        "NOT-PREPARED");
    rejects(
        [&] {
            plan.prepare(sources, {5, 1, 15}, 1, .5);
        },
        "SECTION-BUDGET");
    auto invalid = sources;
    invalid[1].node = invalid[0].node;
    rejects(
        [&] {
            plan.prepare(invalid, {5, 2, 15}, 1, .5);
        },
        "IDENTITY-DUPLICATE");
    invalid = sources;
    invalid[0].ranges[1].indices.firstIndex += 1;
    rejects(
        [&] {
            plan.prepare(invalid, {5, 2, 15}, 1, .5);
        },
        "PARTITION-INVALID");
    plan.prepare(sources, {5, 2, 1}, 1, .5);
    rejects(
        [&] {
            plan.update(cascades, 0);
        },
        "RUN-BUDGET");
    assert(!plan.valid());
    std::cout
        << "shadow plan independent offsets, exact coverage, cache, zero update allocations and negative gates passed\n";
}
