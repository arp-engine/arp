#pragma once
#include "rhi/frontend/command_arguments.h"
#include "rhi/frontend/command_draw_validation.h"
#include "rhi/frontend/visibility/gpu_visibility_layout.h"

namespace arp::rhi::visibility {
// Bounds of every draw the closed VisibilityKernel/1 producer can emit. The
// analyzers must inspect the exact immutable native input owners; these borrowed
// facts neither replace their custody nor authorize a GPU submission.
class GpuVisibilityIndirectFetchContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool optIn = false;
        std::uint64_t maxDraws = 0u, maxStreams = 0u, maxAnalyzedIndices = 0u;
    };

    struct Point {
        std::uint32_t group = 0u, cascade = 0u, templateId = 0u;
        std::uint64_t drawOffsetBytes = 0u, mappingOffsetBytes = 0u;
        std::uint64_t visibleSliceOffsetBytes = 0u;
        rhi::CommandScalarArguments::DrawIndexed maximumDraw{};
        rhi::IndexInputConsumptionContract::DrawRange fetch{};
        // A zero firstInstance draw uses this logical base through Mapping16.
        // The shader read is relative to its cascade's visible-instance slice.
        std::uint32_t logicalFirstInstance = 0u;

        constexpr bool nativeExecutionProven() const noexcept { return false; }
    };

    struct Report {
        const char* diagnostic = nullptr;
        std::uint64_t draws = 0u, streams = 0u, analyzedIndices = 0u;

        constexpr bool nativeExecutionProven() const noexcept { return false; }
    };

    static constexpr const char* validateBudget(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "PCG-GPU-VISIBILITY-FETCH-VERSION";
        if (!b.optIn)
            return "PCG-GPU-VISIBILITY-FETCH-OPT-IN";
        return b.maxDraws && b.maxStreams && b.maxAnalyzedIndices
            ? nullptr
            : "PCG-GPU-VISIBILITY-FETCH-BUDGET";
    }

    // layout is already independently inspected using its explicit visit
    // budgets. Revalidate it here so neither a fabricated Report nor unchecked
    // scene values can lend bounds to the GPU producer.
    //
    // analyze(point, remainingIndices, fetch) authenticates the selected mesh's
    // original index binding and analyzes those initialized bytes. streams(point,
    // remainingStreams, count) authenticates its pipeline and original vertex
    // sources, then uses the existing VertexInputConsumptionContract for fetch.
    // Both adapters must be pure inspections over retained immutable owners.
    template <class Analyzer, class Streams, class Visitor>
    static Report visit(const GpuVisibilityLayoutContract::Input& input,
        const GpuVisibilityLayoutContract::Budget& layoutBudget,
        bool nativeFirstInstance,
        const Budget& budget,
        Analyzer&& analyze,
        Streams&& streams,
        Visitor&& visitor) noexcept
    {
        if (const auto* id = validateBudget(budget))
            return {id};
        GpuVisibilityLayoutContract::Report layout;
        if (const auto* id = GpuVisibilityLayoutContract::inspect(input, layoutBudget, layout))
            return {id};
        auto inspected = walk(input,
            nativeFirstInstance,
            budget,
            analyze,
            streams,
            [](const Point&) noexcept -> const char* {
                return nullptr;
            });
        return inspected.diagnostic
            ? inspected
            : walk(input, nativeFirstInstance, budget, analyze, streams, visitor);
    }

    // Addressed by the real indirect consumer. The complete layout/frozen
    // source proof remains a prerequisite; this only derives producer maxima.
    static const char* projectMaximum(const GpuVisibilityLayoutContract::Input& input,
        bool nativeFirstInstance,
        std::uint32_t cascade,
        std::uint32_t groupIndex,
        Point& output) noexcept
    {
        output = {};
        if (!input.groups || groupIndex >= input.groupCount
            || input.groupCount > input.groupCapacity || cascade >= input.cascades
            || (input.cascades != 1u && input.cascades != 3u) || !input.instanceCapacity)
            return "PCG-GPU-VISIBILITY-FETCH-POINT";
        const auto& group = input.groups[groupIndex];
        const bool alternate = cascade == 1u && group.shadowCascade1IndexCount;
        Point point;
        point.group = groupIndex;
        point.cascade = cascade;
        point.templateId = alternate ? group.shadowCascade1TemplateId : group.templateId;
        const auto slot = std::uint64_t(cascade) * input.groupCapacity + groupIndex;
        point.drawOffsetBytes = slot * sizeof(rhi::DrawIndexedIndirectCommand);
        point.mappingOffsetBytes = slot * sizeof(rhi::IndirectInstanceBase);
        point.visibleSliceOffsetBytes =
            std::uint64_t(cascade) * input.instanceCapacity * sizeof(Instance64);
        point.logicalFirstInstance = group.globalFirstInstance;
        point.maximumDraw = {alternate ? group.shadowCascade1IndexCount : group.indexCount,
            group.globalInstanceCount,
            alternate ? group.shadowCascade1FirstIndex : group.firstIndex,
            0,
            nativeFirstInstance ? group.globalFirstInstance : 0u};
        output = point;
        return nullptr;
    }

    static const char* validateVertexStream(const Point& point,
        const rhi::VertexInputConsumptionContract::Stream& stream,
        std::uint64_t bytes,
        std::uint64_t offset) noexcept
    {
        // Visible Instance64 is a shader storage read with an independent ABI,
        // not an authored per-instance vertex stream in this producer family.
        if (stream.rate != rhi::VertexInputRate::Vertex)
            return "PCG-GPU-VISIBILITY-FETCH-INSTANCE-STREAM";
        return rhi::VertexInputConsumptionContract::validateStreamRange(stream,
            bytes,
            offset,
            point.fetch.vertexCount,
            point.fetch.firstVertex,
            point.maximumDraw.instances,
            point.maximumDraw.firstInstance);
    }

private:
    template <class Analyzer, class Streams, class Visitor>
    static Report walk(const GpuVisibilityLayoutContract::Input& input,
        bool nativeFirstInstance,
        const Budget& budget,
        Analyzer& analyze,
        Streams& streams,
        Visitor&& visitor) noexcept
    {
        Report report;
        const auto reject = [](const char* id) noexcept {
            return Report{id};
        };
        for (std::uint32_t cascade = 0u; cascade < input.cascades; ++cascade)
            for (std::uint32_t groupIndex = 0u; groupIndex < input.groupCount; ++groupIndex) {
                if (report.draws == budget.maxDraws)
                    return reject("PCG-GPU-VISIBILITY-FETCH-DRAW-CAPACITY");
                Point point;
                if (const auto* id =
                        projectMaximum(input, nativeFirstInstance, cascade, groupIndex, point))
                    return reject(id);
                if (point.maximumDraw.indices > budget.maxAnalyzedIndices - report.analyzedIndices)
                    return reject("PCG-GPU-VISIBILITY-FETCH-INDEX-CAPACITY");
                if (const auto* id = analyze(
                        point, budget.maxAnalyzedIndices - report.analyzedIndices, point.fetch))
                    return reject(id);
                // Empty/forged analyzer output must not authorize geometry.
                // Indexed20 is TriangleList without restart and vertexOffset0.
                if (!point.fetch.vertexCount
                    || point.fetch.firstIndex != point.maximumDraw.firstIndex
                    || point.fetch.indexCount != point.maximumDraw.indices
                    || point.fetch.baseVertex != point.maximumDraw.vertexOffset)
                    return reject("PCG-GPU-VISIBILITY-FETCH-INDEX-CORRESPONDENCE");
                report.analyzedIndices += point.maximumDraw.indices;
                std::uint64_t count = 0u;
                if (const auto* id = streams(point, budget.maxStreams - report.streams, count))
                    return reject(id);
                if (!count || count > budget.maxStreams - report.streams)
                    return reject("PCG-GPU-VISIBILITY-FETCH-STREAM-CAPACITY");
                report.streams += count;
                ++report.draws;
                if (const auto* id = visitor(point))
                    return reject(id);
            }
        return report;
    }
};
} // namespace arp::rhi::visibility
