#pragma once
#include "rhi/frontend/visibility/gpu_visibility_indirect_fetch.h"
#include "rhi/frontend/visibility/gpu_visibility_root_consumer.h"
#include "rhi/frontend/visibility/gpu_visibility_root_inputs.h"
#include "rhi/frontend/visibility/gpu_visibility_root_readback.h"

namespace arp::rhi::visibility {
// Cold join of the real four producer closures and the actual consumer's
// pipeline epoch/index/vertex bindings. Template metadata names canonical
// sources only; the backend's closed actors authenticate the backing bytes.
// Instance shader ABI, native recording and completion are separate gates.
class GpuVisibilityRootGeometryContract final {
public:
    struct Vertex {
        std::uint32_t slot = 0u;
        std::size_t source = SIZE_MAX;
        std::uint64_t offset = 0u;
    };

    struct Mesh {
        std::size_t indexSource = SIZE_MAX;
        rhi::IndexFormat indexFormat = rhi::IndexFormat::Uint16;
        std::uint64_t indexOffset = 0u;
        const Vertex* vertices = nullptr;
        std::size_t vertexCount = 0u;
    };

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool optIn = false;
        GpuVisibilityRootInputContract::Budget inputs;
        rhi::OriginalIndexedDrawFetchContract::Budget fetch;
        std::uint64_t maxVertexBindingsPerTemplate = 0u;
        GpuVisibilityRootConsumerContract::Budget consumer;
        GpuVisibilityRootReadbackContract::Budget readbacks;
    };

    struct Report {
        const char* diagnostic = nullptr;
        GpuVisibilityRootInputContract::Report inputs;
        rhi::OriginalIndexedDrawFetchContract::Result fetch;
        GpuVisibilityRootConsumerContract::Report consumer;
        GpuVisibilityRootReadbackContract::Report readbacks;

        constexpr bool nativeExecutionProven() const noexcept { return false; }
    };

    template <class Closure, class Lookup, class Instance>
    static Report inspect(const std::array<std::shared_ptr<const Closure>, 4u>& closures,
        const std::shared_ptr<const rhi::OriginalCommandResourceStates>& graph,
        const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& stream,
        const GpuVisibilityLayoutContract::Input& layout,
        const Instance* instances,
        const GpuVisibilityParameters& parameters,
        const Mesh* meshes,
        std::size_t meshCount,
        const std::size_t* consumerRoots,
        std::size_t consumerCount,
        Lookup&& lookup,
        std::uint32_t queriedWorkgroupLimit,
        bool queriedNativeBase,
        const Budget& budget,
        const std::shared_ptr<const rhi::OriginalCommandBufferReadbackBinding>* readbacks,
        std::size_t readbackCount) noexcept
    {
        using namespace rhi;
        const auto reject = [](const char* id) noexcept {
            return Report{id};
        };
        if (budget.contractVersion != 1u)
            return reject("PCG-GPU-VISIBILITY-GEOMETRY-VERSION");
        if (!budget.optIn)
            return reject("PCG-GPU-VISIBILITY-GEOMETRY-OPT-IN");
        if (budget.fetch.contractVersion != 3u || !budget.maxVertexBindingsPerTemplate)
            return reject("PCG-GPU-VISIBILITY-GEOMETRY-BUDGET");
        Report result;
        result.inputs = GpuVisibilityRootInputContract::inspectClosures(closures,
            graph,
            stream,
            layout,
            instances,
            parameters,
            queriedWorkgroupLimit,
            queriedNativeBase,
            budget.inputs);
        if (result.inputs.diagnostic)
            return reject(result.inputs.diagnostic);
        GpuVisibilityRootProducerContract::Roots producers;
        for (std::size_t i = 0u; i < producers.size(); ++i)
            producers[i] = closures[i]->originalRoot();
        result.consumer = GpuVisibilityRootConsumerContract::inspect(producers,
            closures[0u]->originalInventory(),
            graph,
            stream,
            layout,
            budget.inputs.layout,
            queriedWorkgroupLimit,
            budget.inputs.producer,
            budget.consumer);
        if (result.consumer.diagnostic)
            return reject(result.consumer.diagnostic);
        const core::RowMajorIndexGrid consumers(layout.cascades, layout.groupCount, SIZE_MAX);
        if (!meshes || meshCount != layout.templateCount || !consumerRoots || !consumers.valid()
            || consumerCount != consumers.size())
            return reject("PCG-GPU-VISIBILITY-GEOMETRY-TEMPLATES");
        const auto& inventory = closures[0u]->originalInventory();
        using Content = typename std::decay_t<decltype(lookup(std::size_t{}))>::element_type;
        using Fetch = OriginalIndexedDrawFetchContract;
        using Maximum = GpuVisibilityIndirectFetchContract;
        const auto project = [&](const Fetch::Point& point,
                                 Maximum::Point& out) noexcept -> const char* {
            if (point.argumentSource != result.inputs.producer.sources[5u]
                || point.argumentOffset % sizeof(DrawIndexedIndirectCommand))
                return "PCG-GPU-VISIBILITY-GEOMETRY-ARGUMENT";
            const auto slot = point.argumentOffset / sizeof(DrawIndexedIndirectCommand);
            const auto cascade = slot / layout.groupCapacity;
            const auto group = slot % layout.groupCapacity;
            if (cascade >= layout.cascades || group >= layout.groupCount)
                return "PCG-GPU-VISIBILITY-GEOMETRY-ARGUMENT";
            return Maximum::projectMaximum(layout,
                queriedNativeBase,
                static_cast<std::uint32_t>(cascade),
                static_cast<std::uint32_t>(group),
                out);
        };
        const auto bounds = [&](const Fetch::Point& point, auto& draw) noexcept -> const char* {
            Maximum::Point maximum;
            if (const auto* id = project(point, maximum))
                return id;
            const auto consumer = std::size_t(maximum.cascade) * layout.groupCount + maximum.group;
            if (consumerRoots[consumer] != point.pipelineSource
                || point.pipelineSource >= inventory->sources().size())
                return "PCG-GPU-VISIBILITY-GEOMETRY-PIPELINE";
            const auto* root =
                std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                    &inventory->sources()[point.pipelineSource]);
            if (!root
                || !core::SharedOwnerProjection::sameOwner(
                    (*root)->originalSnapshot(), closures[0u]->originalRoot()->originalSnapshot()))
                return "PCG-GPU-VISIBILITY-GEOMETRY-SNAPSHOT";
            draw = maximum.maximumDraw;
            return nullptr;
        };
        const auto analyze =
            [&](const Fetch::Point& point, const auto& lease, auto& out) noexcept -> const char* {
            Maximum::Point maximum;
            if (const auto* id = project(point, maximum))
                return id;
            const auto& mesh = meshes[maximum.templateId];
            if (point.indexSource != mesh.indexSource || point.index.format != mesh.indexFormat
                || point.index.offset != mesh.indexOffset)
                return "PCG-GPU-VISIBILITY-GEOMETRY-INDEX-TEMPLATE";
            return Content::analyzeInitializedIndices(lookup(point.indexSource),
                lease,
                point.index.format,
                point.index.offset,
                point.draw.firstIndex,
                point.draw.indices,
                point.draw.vertexOffset,
                out);
        };
        const auto vertex = [&](const Fetch::Point& point,
                                const auto& binding,
                                const auto& fields,
                                const auto& values,
                                const auto& lease) noexcept -> const char* {
            Maximum::Point maximum;
            if (const auto* id = project(point, maximum))
                return id;
            const auto& mesh = meshes[maximum.templateId];
            if (!mesh.vertices || !mesh.vertexCount
                || mesh.vertexCount > budget.maxVertexBindingsPerTemplate
                || fields.rate != VertexInputRate::Vertex)
                return "PCG-GPU-VISIBILITY-GEOMETRY-VERTEX-TEMPLATE";
            const Vertex* selected = nullptr;
            for (std::size_t i = 0u; i < mesh.vertexCount; ++i) {
                if (mesh.vertices[i].slot != fields.slot)
                    continue;
                if (selected)
                    return "PCG-GPU-VISIBILITY-GEOMETRY-VERTEX-AMBIGUOUS";
                selected = &mesh.vertices[i];
            }
            if (!selected || selected->source != binding.sourceIndex
                || selected->offset != values.offset)
                return "PCG-GPU-VISIBILITY-GEOMETRY-VERTEX-TEMPLATE";
            return Content::validateInitializedVertices(lookup(binding.sourceIndex),
                lease,
                fields,
                values.offset,
                point.fetch.vertexCount,
                point.fetch.firstVertex,
                point.draw.instances,
                point.draw.firstInstance);
        };
        const auto complete = [&](const Fetch::Point& point) noexcept -> const char* {
            Maximum::Point maximum;
            if (const auto* id = project(point, maximum))
                return id;
            return meshes[maximum.templateId].vertexCount == point.vertexStreams
                ? nullptr
                : "PCG-GPU-VISIBILITY-GEOMETRY-VERTEX-COVERAGE";
        };
        result.fetch = stream->visitInitializedProducedDrawFetch(
            budget.fetch, bounds, analyze, vertex, complete);
        if (result.fetch.diagnostic)
            return reject(result.fetch.diagnostic);
        if (result.fetch.draws != result.inputs.producer.indirectRecords)
            return reject("PCG-GPU-VISIBILITY-GEOMETRY-DRAW-COVERAGE");
        result.readbacks = GpuVisibilityRootReadbackContract::inspect(
            result.inputs.producer, stream, layout, readbacks, readbackCount, budget.readbacks);
        return result.readbacks.diagnostic ? reject(result.readbacks.diagnostic) : result;
    }
};
} // namespace arp::rhi::visibility
