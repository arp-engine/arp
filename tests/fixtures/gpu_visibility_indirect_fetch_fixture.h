#pragma once
#include "pcg/gpu_visibility_indirect_fetch.h"

#include <array>
#include <cassert>

namespace arp::test {
class GpuVisibilityIndirectFetchFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using Fetch = pcg::GpuVisibilityIndirectFetchContract;
        std::array<pcg::GpuVisibilityDrawGroup, 2u> groups{};
        for (auto& group : groups) {
            group.globalInstanceCount = 2u;
            group.localBoundsCenterRadius[3u] = 1.f;
            group.cullDistance = 100.f;
            group.castsShadow = 1u;
            group.indexCount = 3u;
        }
        groups[1u].globalFirstInstance = 2u;
        groups[1u].shadowCascade1TemplateId = 1u;
        groups[1u].shadowCascade1FirstIndex = 3u;
        groups[1u].shadowCascade1IndexCount = 6u;
        const std::array<std::uint32_t, 4u> owners{0u, 0u, 1u, 1u};
        const std::array<std::uint32_t, 2u> indexCounts{3u, 9u};
        const pcg::GpuVisibilityLayoutContract::Input input{
            groups.data(), owners.data(), indexCounts.data(), 2u, 4u, 2u, 1u, 5u, 3u, 3u};
        const pcg::GpuVisibilityLayoutContract::Budget layoutBudget{1u, true, 11u, 10u, 1u};
        Fetch::Budget budget{1u, true, 6u, 6u, 21u};
        // Prefix bytes exercise a real nonzero initialized index-binding offset.
        std::array<std::uint8_t, 20u> indexBytes{};
        for (std::size_t i = 0u; i < 9u; ++i)
            indexBytes[2u + i * 2u] = static_cast<std::uint8_t>(i % 3u);
        bool forge = false;
        const auto analyze = [&](const Fetch::Point& point,
                                 std::uint64_t remaining,
                                 IndexInputConsumptionContract::DrawRange& output) noexcept {
            const auto* id =
                IndexInputConsumptionContract::analyzeLittleEndianBytes(indexBytes.data(),
                    2u + indexCounts[point.templateId] * 2u,
                    IndexFormat::Uint16,
                    2u,
                    point.maximumDraw.firstIndex,
                    point.maximumDraw.indices,
                    point.maximumDraw.vertexOffset,
                    remaining,
                    output);
            if (!id && forge)
                ++output.indexCount;
            return id;
        };
        VertexInputConsumptionContract::Stream vertex{
            0u, 16u, VertexInputRate::Vertex, 0u, 1u, 12u};
        std::uint64_t vertexBytes = 48u;
        const auto streams =
            [&](const Fetch::Point& point, std::uint64_t, std::uint64_t& count) noexcept {
                count = 1u;
                return Fetch::validateVertexStream(point, vertex, vertexBytes, 4u);
            };
        std::uint64_t visits = 0u;
        const auto visitor = [&](const Fetch::Point& point) noexcept -> const char* {
            ++visits;
            assert(point.drawOffsetBytes == (point.cascade * 3u + point.group) * 20u);
            assert(point.mappingOffsetBytes == (point.cascade * 3u + point.group) * 16u);
            assert(point.visibleSliceOffsetBytes == point.cascade * 5u * 64u);
            assert(point.logicalFirstInstance == point.group * 2u);
            assert(point.maximumDraw.instances == 2u && point.fetch.vertexCount == 3u);
            assert(point.templateId == (point.cascade == 1u && point.group == 1u ? 1u : 0u));
            assert(!point.nativeExecutionProven());
            return nullptr;
        };
        const auto baseline = core::defaultCpuAllocator().stats().allocationCount;
        for (const bool nativeBase : {false, true}) {
            visits = 0u;
            const auto checkedVisitor = [&](const Fetch::Point& point) noexcept -> const char* {
                assert(point.maximumDraw.firstInstance
                    == (nativeBase ? point.logicalFirstInstance : 0u));
                return visitor(point);
            };
            const auto report = Fetch::visit(
                input, layoutBudget, nativeBase, budget, analyze, streams, checkedVisitor);
            assert(!report.diagnostic && report.draws == 6u && report.streams == 6u
                && report.analyzedIndices == 21u && visits == 6u
                && !report.nativeExecutionProven());
        }
        const auto reject = [&](const Fetch::Budget& b, const char* expected) {
            visits = 0u;
            const auto report =
                Fetch::visit(input, layoutBudget, false, b, analyze, streams, visitor);
            assert(report.diagnostic && core::StringView(report.diagnostic) == expected
                && !report.draws && !report.streams && !report.analyzedIndices && !visits);
        };
        for (const auto member : {&Fetch::Budget::maxDraws,
                 &Fetch::Budget::maxStreams,
                 &Fetch::Budget::maxAnalyzedIndices}) {
            auto shortBudget = budget;
            --(shortBudget.*member);
            reject(shortBudget,
                member == &Fetch::Budget::maxDraws ? "PCG-GPU-VISIBILITY-FETCH-DRAW-CAPACITY"
                    : member == &Fetch::Budget::maxStreams
                    ? "PCG-GPU-VISIBILITY-FETCH-STREAM-CAPACITY"
                    : "PCG-GPU-VISIBILITY-FETCH-INDEX-CAPACITY");
        }
        forge = true;
        reject(budget, "PCG-GPU-VISIBILITY-FETCH-INDEX-CORRESPONDENCE");
        forge = false;
        vertex.rate = VertexInputRate::Instance;
        reject(budget, "PCG-GPU-VISIBILITY-FETCH-INSTANCE-STREAM");
        vertex.rate = VertexInputRate::Vertex;
        // Only the alternate mesh references this high index. The complete dry
        // pass must find it before publishing even an earlier valid draw.
        indexBytes[2u + 8u * 2u] = 3u;
        visits = 0u;
        const auto outOfBounds =
            Fetch::visit(input, layoutBudget, false, budget, analyze, streams, visitor);
        assert(outOfBounds.diagnostic && !outOfBounds.draws && !visits);
        indexBytes[2u + 8u * 2u] = 2u;
        assert(core::defaultCpuAllocator().stats().allocationCount == baseline);
    }
};
} // namespace arp::test
