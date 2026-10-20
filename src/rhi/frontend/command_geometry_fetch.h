#pragma once
#include "rhi/frontend/command_indirect_accesses.h"

namespace arp::rhi {
// OriginalIndexedDrawFetch/1 projects ordinary Inputs5; /2 joins prepared
// Graphics Root Inputs7; /3 bounds produced indexed draws over Inputs8.
// All borrow the same immutable pipeline, epoch and initialized-content walkers.
// The index
// adapter must consume the exact initialized owner and lease; a range alone is
// not native authority. No extra graph, retained layout or execution receipt.
class OriginalIndexedDrawFetchContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxCommands = 0u, maxDraws = 0u, maxStreams = 0u, maxBindingScans = 0u,
                      maxAnalyzedIndices = 0u;

        constexpr const char* diagnostic() const noexcept
        {
            if (contractVersion < 1u || contractVersion > 3u)
                return "RHI-ORIGINAL-INDEXED-FETCH-VERSION";
            if (!prototypeOptIn)
                return "RHI-ORIGINAL-INDEXED-FETCH-OPT-IN";
            return maxCommands && maxDraws && maxBindingScans && maxAnalyzedIndices
                ? nullptr
                : "RHI-ORIGINAL-INDEXED-FETCH-BUDGET";
        }
    };

    struct Point {
        std::size_t commandIndex = SIZE_MAX, scopeBegin = SIZE_MAX, pipelineSource = SIZE_MAX,
                    pipelineBind = SIZE_MAX, indexBind = SIZE_MAX, indexSource = SIZE_MAX;
        CommandScalarArguments::DrawIndexed draw{};
        CommandScalarArguments::IndexBinding index{};
        IndexInputConsumptionContract::DrawRange fetch{};
        std::uint64_t vertexStreams = 0u, argumentOffset = 0u;
        std::size_t argumentSource = SIZE_MAX;
        std::uint32_t argumentRecord = 0u;

        bool nativeExecutionProven() const noexcept { return false; }
    };

    struct Result {
        const char* diagnostic = nullptr;
        std::size_t commandIndex = 0u;
        std::uint64_t draws = 0u, streams = 0u, bindingScans = 0u, analyzedIndices = 0u;

        bool nativeExecutionProven() const noexcept { return false; }
    };

    static constexpr auto lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        return semantics::OriginalIndexedDrawFetchLowering::current(backend, version);
    }

    // Complete dry validation precedes any caller output. Adapter invocations
    // only inspect retained immutable bytes; they must not perform GPU work.
    template <class Analyzer, class Visitor>
    static Result visit(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const Budget& budget,
        Analyzer&& analyze,
        Visitor&& visitor) noexcept
    {
        if (budget.contractVersion >= 2u)
            return {"RHI-ORIGINAL-ROOT-INDEXED-FETCH-INITIALIZED-CUSTODY-REQUIRED"};
        const auto vertex = [](const auto&,
                                const auto&,
                                const auto&,
                                const auto&,
                                const auto&) noexcept -> const char* {
            return nullptr;
        };
        return visitInitialized(inputs, budget, analyze, vertex, visitor);
    }

    // The same immutable pipeline/stream/binding and range inspector, with an
    // additional initialized source custody check. No layout or graph is copied.
    template <class Analyzer, class VertexAnalyzer, class Visitor>
    static Result visitInitialized(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const Budget& budget,
        Analyzer&& analyze,
        VertexAnalyzer&& vertex,
        Visitor&& visitor) noexcept
    {
        if (budget.contractVersion == 3u)
            return {"RHI-ORIGINAL-PRODUCED-INDEXED-FETCH-BOUNDS-REQUIRED"};
        const auto direct = [](const Point&,
                                CommandScalarArguments::DrawIndexed&) noexcept -> const char* {
            return "RHI-ORIGINAL-PRODUCED-INDEXED-FETCH-BOUNDS-REQUIRED";
        };
        return visitImpl<false>(inputs, budget, direct, analyze, vertex, visitor);
    }

    // The bounds adapter derives the maximum draw from an independently
    // authenticated closed GPU producer. It receives the actual bound pipeline
    // epoch and original argument address; it cannot substitute those bindings.
    // This CPU projection does not grant producer, content or native authority.
    template <class Bounds, class Analyzer, class VertexAnalyzer, class Visitor>
    static Result visitProducedInitialized(
        const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const Budget& budget,
        Bounds&& bounds,
        Analyzer&& analyze,
        VertexAnalyzer&& vertex,
        Visitor&& visitor) noexcept
    {
        if (budget.contractVersion != 3u)
            return {"RHI-ORIGINAL-PRODUCED-INDEXED-FETCH-PROFILE"};
        return visitImpl<true>(inputs, budget, bounds, analyze, vertex, visitor);
    }

private:
    using K = CommandUsageKind;

    template <bool Produced, class Bounds, class Analyzer, class VertexAnalyzer, class Visitor>
    static Result visitImpl(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const Budget& budget,
        Bounds& bounds,
        Analyzer& analyze,
        VertexAnalyzer& vertex,
        Visitor& visitor) noexcept
    {
        const auto discard = [](const Point&) noexcept -> const char* {
            return nullptr;
        };
        const auto checked = walk<Produced>(inputs, budget, bounds, analyze, vertex, discard);
        return checked.diagnostic
            ? checked
            : walk<Produced>(inputs, budget, bounds, analyze, vertex, visitor);
    }

    template <bool Produced, class Bounds, class Analyzer, class VertexAnalyzer, class Visitor>
    static Result walk(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const Budget& budget,
        Bounds& bounds,
        Analyzer& analyze,
        VertexAnalyzer& vertexAnalyze,
        Visitor&& visitor) noexcept
    {
        Result result;
        const auto fail = [&](const char* id) noexcept {
            auto out = result;
            out.diagnostic = id;
            return out;
        };
        if (const auto* id = budget.diagnostic())
            return fail(id);
        if (const auto* id = OriginalCommandInputs::validateOriginal(inputs))
            return fail(id);
        const bool preparedRoot = budget.contractVersion >= 2u;
        if (inputs->originalBudget().contractVersion != (Produced ? 8u : preparedRoot ? 7u : 5u))
            return fail("RHI-ORIGINAL-INDEXED-FETCH-INPUTS-PROFILE");
        const auto& scalars = *inputs->originalScalars();
        const auto& usage = *scalars.originalUsage();
        const auto& sources = usage.originalSources()->sources();
        const auto& arguments = scalars.originalArguments()->records();
        if (usage.commands().size() > budget.maxCommands)
            return fail("RHI-ORIGINAL-INDEXED-FETCH-COMMAND-CAPACITY");
        Point point;
        for (std::size_t command = 0u; command < usage.commands().size(); ++command) {
            result.commandIndex = command;
            const auto kind = usage.commands()[command].kind;
            if (kind == K::BeginRenderPass) {
                point = {};
                point.scopeBegin = command;
            }
            if (kind == K::EndRenderPass)
                point = {};
            if (kind == (preparedRoot ? K::BindPreparedGraphicsBinding : K::BindGraphicsPipeline)) {
                if (preparedRoot) {
                    const auto& operand = usage.commands()[command];
                    if (operand.referenceCount != 1u)
                        return fail("RHI-ORIGINAL-INDEXED-FETCH-PIPELINE");
                    point.pipelineSource = usage.references()[operand.firstReference].sourceIndex;
                } else {
                    const auto* binding =
                        std::get_if<OriginalCommandPipelineBinding>(inputs->descriptorAt(command));
                    if (!binding)
                        return fail("RHI-ORIGINAL-INDEXED-FETCH-PIPELINE");
                    point.pipelineSource = binding->sourceIndex;
                }
                point.pipelineBind = command;
                point.indexBind = point.indexSource = SIZE_MAX;
            }
            if (kind != (Produced ? K::DrawIndexedIndirect : K::DrawIndexed)) {
                if (Produced
                    && (kind == K::Draw || kind == K::DrawIndexed
                        || kind == K::DrawIndexedIndirectCount || kind == K::IndirectProcessorDraw))
                    return fail("RHI-ORIGINAL-PRODUCED-INDEXED-FETCH-FOREIGN-DRAW");
                continue;
            }
            std::uint32_t records = 1u;
            const CommandScalarArguments::DrawIndirect* indirect = nullptr;
            if constexpr (Produced) {
                const auto& operand = usage.commands()[command];
                if (operand.referenceCount != 1u)
                    return fail("RHI-ORIGINAL-PRODUCED-INDEXED-FETCH-OPERAND");
                point.argumentSource = usage.references()[operand.firstReference].sourceIndex;
                if (point.argumentSource >= sources.size())
                    return fail("RHI-ORIGINAL-PRODUCED-INDEXED-FETCH-OPERAND");
                const auto* buffer = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                    &sources[point.argumentSource]);
                indirect =
                    arguments[command].scalar.template get<CommandScalarArguments::DrawIndirect>();
                std::uint64_t extent = 0u;
                if (!buffer || !indirect)
                    return fail("RHI-ORIGINAL-PRODUCED-INDEXED-FETCH-OPERAND");
                if (const auto* id = OriginalCommandIndirectAccesses::inspectRange(
                        (*buffer)->originalObject()->desc(), *indirect, extent))
                    return fail(id);
                records = indirect->count;
            }
            if (records > budget.maxDraws - result.draws)
                return fail("RHI-ORIGINAL-INDEXED-FETCH-DRAW-CAPACITY");
            for (std::uint32_t record = 0u; record < records; ++record) {
                if (result.draws == budget.maxDraws)
                    return fail("RHI-ORIGINAL-INDEXED-FETCH-DRAW-CAPACITY");
                if (point.scopeBegin == SIZE_MAX || point.pipelineSource >= sources.size())
                    return fail("RHI-ORIGINAL-INDEXED-FETCH-PIPELINE");
                const auto* pipeline = OriginalCommandGeometryBindingContract::graphicsPipeline(
                    *usage.originalSources(), point.pipelineSource, preparedRoot);
                if (!pipeline)
                    return fail("RHI-ORIGINAL-INDEXED-FETCH-PIPELINE");
                const auto* definition =
                    OriginalCommandGeometryBindingContract::vertexBuffers(*pipeline, preparedRoot);
                if (!definition)
                    return fail("RHI-ORIGINAL-INDEXED-FETCH-PIPELINE-DEFINITION");
                const auto topology = preparedRoot
                    ? pipeline->primitiveTopology()
                    : pipeline->originalRenderPipelineDefinition()->topology;
                if (topology != PrimitiveTopology::TriangleList)
                    return fail("RHI-ORIGINAL-INDEXED-FETCH-RESTART-PROFILE");
                point.commandIndex = command;
                if constexpr (Produced) {
                    point.argumentRecord = record;
                    point.argumentOffset =
                        indirect->offset + std::uint64_t(record) * indirect->stride;
                    point.draw = {};
                    if (const auto* id = bounds(point, point.draw))
                        return fail(id);
                } else {
                    const auto* direct =
                        arguments[command]
                            .scalar.template get<CommandScalarArguments::DrawIndexed>();
                    if (!direct)
                        return fail("RHI-ORIGINAL-INDEXED-FETCH-DRAW-EMPTY");
                    point.draw = *direct;
                }
                const auto* draw = &point.draw;
                if (!draw->indices || !draw->instances)
                    return fail("RHI-ORIGINAL-INDEXED-FETCH-DRAW-EMPTY");
                point.vertexStreams = 0u;
                const char* scanDiagnostic = nullptr;
                const auto latest = [&](K selected, std::uint32_t slot) noexcept
                    -> const OriginalCommandInputs::OriginalDescriptor* {
                    const auto found = inputs->latestGeometryBinding(selected,
                        slot,
                        point.pipelineBind,
                        command,
                        budget.maxBindingScans,
                        result.bindingScans);
                    if (found.scanCapacityExceeded)
                        scanDiagnostic = "RHI-ORIGINAL-INDEXED-FETCH-SCAN-CAPACITY";
                    return found.record;
                };
                const auto* index = latest(K::BindIndexBuffer, 0u);
                if (!index)
                    return fail(scanDiagnostic ? scanDiagnostic
                                               : "RHI-ORIGINAL-INDEXED-FETCH-INDEX-UNBOUND");
                const auto& bound = std::get<OriginalCommandGeometryBinding>(index->arguments);
                point.indexBind = index->commandIndex;
                point.indexSource = bound.sourceIndex;
                point.index = *arguments[point.indexBind]
                                   .scalar.template get<CommandScalarArguments::IndexBinding>();
                const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                    sources[point.indexSource]);
                if (!core::containsUnsignedRange(std::uint64_t{0u},
                        budget.maxAnalyzedIndices,
                        result.analyzedIndices,
                        std::uint64_t{draw->indices}))
                    return fail("RHI-ORIGINAL-INDEXED-FETCH-ANALYSIS-CAPACITY");
                point.fetch = {};
                if (const auto* id = analyze(point, lease, point.fetch))
                    return fail(id);
                if (point.fetch.firstIndex != draw->firstIndex
                    || point.fetch.indexCount != draw->indices
                    || point.fetch.baseVertex != draw->vertexOffset || !point.fetch.vertexCount)
                    return fail("RHI-ORIGINAL-INDEXED-FETCH-ANALYZER-CORRESPONDENCE");
                result.analyzedIndices += draw->indices;
                for (const auto& layout : *definition) {
                    if (result.streams == budget.maxStreams)
                        return fail("RHI-ORIGINAL-INDEXED-FETCH-STREAM-CAPACITY");
                    VertexInputConsumptionContract::Stream stream{};
                    if (const auto* id =
                            VertexInputConsumptionContract::inspectStream(layout, stream))
                        return fail(id);
                    const auto* vertex = latest(K::BindVertexBuffer, stream.slot);
                    if (!vertex)
                        return fail(scanDiagnostic ? scanDiagnostic
                                                   : "RHI-ORIGINAL-INDEXED-FETCH-VERTEX-UNBOUND");
                    const auto& binding =
                        std::get<OriginalCommandGeometryBinding>(vertex->arguments);
                    const auto& buffer =
                        std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                            sources[binding.sourceIndex]);
                    const auto* fields =
                        arguments[vertex->commandIndex]
                            .scalar.template get<CommandScalarArguments::VertexBinding>();
                    if (const auto* id = VertexInputConsumptionContract::validateStreamRange(stream,
                            buffer->layout().sizeBytes,
                            fields->offset,
                            point.fetch.vertexCount,
                            point.fetch.firstVertex,
                            draw->instances,
                            draw->firstInstance))
                        return fail(id);
                    if (const auto* id = vertexAnalyze(point, binding, stream, *fields, buffer))
                        return fail(id);
                    ++result.streams;
                    ++point.vertexStreams;
                }
                ++result.draws;
                if (const auto* id = visitor(point))
                    return fail(id);
            }
        }
        result.commandIndex = usage.commands().size();
        return result;
    }
};
} // namespace arp::rhi
