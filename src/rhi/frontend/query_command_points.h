#pragma once
#include "rhi/frontend/command_arguments.h"
#include "rhi/frontend/command_recording_order.h"
#include "rhi/frontend/query_pool_arguments.h"
#include "rhi/semantics/query_lowering.h"

namespace arp::rhi {
struct QueryCommandPointsBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxCommands = 0u, maxPasses = 0u, maxQueryCommands = 0u, maxDebugDepth = 0u;
    std::uint64_t maxStreamTextBytes = 0u, maxStreamOffsets = 0u, maxStreamPushBytes = 0u;
    std::uint64_t maxReferencedSlots = 0u, maxReferencedScopeIds = 0u, maxReferencedLabelBytes = 0u,
                  maxCpuMetadataBytes = 0u;
};

// CPU command-point metadata. Original native query/pass/pipeline ownership and
// execution authority require a separate backend-private binding contract.
class QueryCommandPoints final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class QueryCommandPoints;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Order = semantics::CommandRecordingOrder;

public:
    struct Input {
        std::size_t commandOrdinal;
        QueryPoolOriginalInputArguments original;
    };

    struct Pass {
        std::size_t begin, end;
        Order::Scope scope;
    };

    struct Point {
        std::size_t commandOrdinal, passIndex;
        std::optional<std::size_t> pipelineBindOrdinal;
        QueryPoolOriginalInputArguments original;
    };

    struct Lowering {
        bool cpuMetadataImplemented = false, validatorOnly = false;
        const char* diagnostic = "RHI-QUERY-POINTS-BACKEND";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::QueryLowering::QueryCommandPoints_lowering<Lowering>(backend);
    }

    QueryCommandPoints(ConstructionKey,
        std::shared_ptr<const CommandVariableArguments> stream,
        std::size_t passes,
        std::size_t points)
        : stream_(std::move(stream))
    {
        passes_.reserve(passes);
        points_.reserve(points);
    }

    static const char* validateBudget(const QueryCommandPointsBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-QUERY-POINTS-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-QUERY-POINTS-OPT-IN";
        if (!b.maxCommands || !b.maxPasses || !b.maxQueryCommands || !b.maxReferencedSlots
            || !b.maxCpuMetadataBytes)
            return "RHI-QUERY-POINTS-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<const QueryCommandPoints> prepareOriginal(
        std::shared_ptr<const CommandVariableArguments> stream,
        const Input* input,
        std::size_t count,
        const QueryCommandPointsBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != b.maxCpuMetadataBytes) {
            diagnostic = "RHI-QUERY-POINTS-QUOTA";
            return {};
        }
        if (!stream || !stream->hasQuotaBoundSharedOwner(stream)) {
            diagnostic = "RHI-QUERY-POINTS-STREAM-OWNER";
            return {};
        }
        if (!input || !count || count > b.maxQueryCommands) {
            diagnostic = "RHI-QUERY-POINTS-COUNT";
            return {};
        }
        std::size_t passCount = 0u;
        if ((diagnostic = validateStream(*stream, input, count, b, passCount)))
            return {};
        try {
            core::CpuMemoryQuotaScope metadata(quota);
            auto result = core::makeUnique<QueryCommandPoints>(
                ConstructionKey{}, std::move(stream), passCount, count);
            std::size_t query = 0u;
            std::optional<std::size_t> pipeline;
            const auto& commands = result->stream_->records();
            for (std::size_t i = 0u; i < commands.size(); ++i) {
                const auto kind = commands[i].scalar.kind();
                if (isBegin(kind)) {
                    result->passes_.push_back({i, i, scope(kind)});
                    pipeline.reset();
                } else if (isEnd(kind)) {
                    result->passes_.back().end = i;
                    pipeline.reset();
                } else if (kind == CommandUsageKind::BindGraphicsPipeline
                    || kind == CommandUsageKind::BindComputePipeline)
                    pipeline = i;
                else if (isQuery(kind))
                    result->points_.push_back(
                        {i, result->passes_.size() - 1u, pipeline, input[query++].original});
            }
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-QUERY-POINTS-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-QUERY-POINTS-ALLOCATION";
            return {};
        }
    }

    const auto& originalStream() const noexcept { return stream_; }

    const auto& points() const noexcept { return points_; }

    const auto& passes() const noexcept { return passes_; }

    static const char* validateOriginalStream(const std::shared_ptr<const QueryCommandPoints>& self,
        const std::shared_ptr<const CommandVariableArguments>& stream) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-QUERY-POINTS-OWNER";
        if (!stream || !stream->hasQuotaBoundSharedOwner(stream)
            || !core::SharedOwnerProjection::sameOwner(self->stream_, stream))
            return "RHI-QUERY-POINTS-STREAM-OWNER";
        return nullptr;
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    static bool isQuery(CommandUsageKind k) noexcept
    {
        return k == CommandUsageKind::Timestamp || k == CommandUsageKind::PipelineStatistics;
    }

    static bool isBegin(CommandUsageKind k) noexcept
    {
        return k == CommandUsageKind::BeginRenderPass || k == CommandUsageKind::BeginComputePass
            || k == CommandUsageKind::BeginTransferPass;
    }

    static bool isEnd(CommandUsageKind k) noexcept
    {
        return k == CommandUsageKind::EndRenderPass || k == CommandUsageKind::EndComputePass
            || k == CommandUsageKind::EndTransferPass;
    }

    static Order::Scope scope(CommandUsageKind k) noexcept
    {
        return k == CommandUsageKind::BeginRenderPass ? Order::Scope::Render
            : k == CommandUsageKind::BeginComputePass ? Order::Scope::Compute
                                                      : Order::Scope::Transfer;
    }

    static const char* validateStream(const CommandVariableArguments& stream,
        const Input* input,
        std::size_t count,
        const QueryCommandPointsBudget& b,
        std::size_t& passCount) noexcept
    {
        const auto& commands = stream.records();
        if (commands.empty() || commands.size() > b.maxCommands)
            return "RHI-QUERY-POINTS-STREAM-CAPACITY";
        Order order(b.maxDebugDepth);
        std::size_t query = 0u;
        std::uint64_t text = 0u, offsets = 0u, bytes = 0u, slots = 0u, scopes = 0u, labels = 0u;
        for (std::size_t i = 0u; i < commands.size(); ++i) {
            const auto& command = commands[i];
            const auto kind = command.scalar.kind();
            if (const auto* id = order.append(kind))
                return id;
            if (command.textCount > b.maxStreamTextBytes - text
                || command.offsetCount > b.maxStreamOffsets - offsets
                || command.byteCount > b.maxStreamPushBytes - bytes)
                return "RHI-QUERY-POINTS-STREAM-CAPACITY";
            text += command.textCount;
            offsets += command.offsetCount;
            bytes += command.byteCount;
            if (isBegin(kind) && ++passCount > b.maxPasses)
                return "RHI-QUERY-POINTS-PASS-CAPACITY";
            if (!isQuery(kind))
                continue;
            if (query == count || input[query].commandOrdinal != i
                || !input[query].original.scalar().matches(command.scalar))
                return "RHI-QUERY-POINTS-COVERAGE";
            const auto& original = input[query++].original;
            if (const auto* id = original.validateDeclaration())
                return id;
            const auto& pool = *original.originalInput();
            if (pool.scalarFields().count() > b.maxReferencedSlots - slots
                || pool.scopeIds().size() > b.maxReferencedScopeIds - scopes
                || pool.label().size() > b.maxReferencedLabelBytes - labels)
                return "RHI-QUERY-POINTS-INPUT-CAPACITY";
            slots += pool.scalarFields().count();
            scopes += pool.scopeIds().size();
            labels += pool.label().size();
        }
        if (!order.complete())
            return "RHI-COMMAND-ORDER-INCOMPLETE";
        return query == count ? nullptr : "RHI-QUERY-POINTS-COVERAGE";
    }

    const std::shared_ptr<const CommandVariableArguments> stream_;
    core::pmr::Vector<Pass> passes_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Point> points_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::rhi
