#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/query_command_points.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <type_traits>

namespace arp::test {
class QueryCommandPointsFixture final {
    using Points = rhi::QueryCommandPoints;
    using A = rhi::CommandScalarArguments;
    using K = rhi::CommandUsageKind;
    using Stream = rhi::CommandVariableArguments;
    static constexpr std::uint64_t cpuBytes = 65536u;

public:
    static void verify()
    {
        static_assert(!std::is_copy_constructible_v<Points>);
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto l = Points::lowering(backend);
            assert(l.cpuMetadataImplemented && l.diagnostic
                && l.validatorOnly == (backend == rhi::Backend::Null));
        }
        assert(!Points::lowering(static_cast<rhi::Backend>(99u)).cpuMetadataImplemented);
        rhi::QueryCommandPointsBudget b;
        assert(Points::validateBudget(b));
        b.contractVersion = 1u;
        assert(Points::validateBudget(b));
        b.prototypeOptIn = true;
        assert(Points::validateBudget(b));
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 5> quotas;
        for (auto& q : quotas)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const std::array<std::uint64_t, 2> scopes{{31u, 17u}};
        rhi::QueryPoolDesc td;
        td.type = rhi::QueryType::Timestamp;
        td.count = 4u;
        td.timingScopeIds = scopes.data();
        td.timingScopeIdCount = 2u;
        td.label = "原始 query ✓";
        rhi::QueryPoolDesc sd;
        sd.type = rhi::QueryType::PipelineStatistics;
        sd.count = 1u;
        sd.pipelineStatisticsMask = rhi::kAllPipelineStatistics;
        const char* diagnostic = nullptr;
        const rhi::QueryPoolInputSnapshotBudget pb{
            1u, true, 2u, td.label.size(), cpuBytes, cpuBytes, cpuBytes};
        auto timestamp = rhi::QueryPoolInputSnapshot::prepareOriginal(
            td, pb, quotas[0], quotas[1], quotas[2], diagnostic);
        auto statistics = rhi::QueryPoolInputSnapshot::prepareOriginal(
            sd, pb, quotas[0], quotas[1], quotas[2], diagnostic);
        assert(timestamp && statistics);
        const std::string_view passLabel = "原始 pass";
        const auto scalar = [](K k) {
            return *A::capture(k, A::None{});
        };
        std::array<Stream::Input, 9> commands{{{scalar(K::BeginComputePass), passLabel},
            {*A::capture(K::Timestamp, A::Query{0u})},
            {scalar(K::BindComputePipeline)},
            {*A::capture(K::Timestamp, A::Query{1u})},
            {scalar(K::EndComputePass)},
            {scalar(K::BeginRenderPass)},
            {*A::capture(K::PipelineStatistics, A::Query{0u})},
            {scalar(K::EndRenderPass)},
            {scalar(K::Finish)}}};
        std::array<Points::Input, 3> input{
            {{1u,
                 *rhi::QueryPoolOriginalInputArguments::captureOriginal(
                     K::Timestamp, 0u, timestamp)},
                {3u,
                    *rhi::QueryPoolOriginalInputArguments::captureOriginal(
                        K::Timestamp, 1u, timestamp)},
                {6u,
                    *rhi::QueryPoolOriginalInputArguments::captureOriginal(
                        K::PipelineStatistics, 0u, statistics)}}};
        const rhi::CommandVariableArgumentsBudget sb{
            1u, true, 9u, 0u, passLabel.size(), 0u, cpuBytes};
        const auto prepareStream = [&] {
            return Stream::prepareOriginal(
                commands.data(), commands.size(), sb, quotas[3], diagnostic);
        };
        auto stream = prepareStream();
        assert(stream);
        b = {1u,
            true,
            9u,
            2u,
            3u,
            0u,
            passLabel.size(),
            0u,
            0u,
            9u,
            4u,
            td.label.size() * 2u,
            cpuBytes};
        const auto factory = [&] {
            return Points::prepareOriginal(
                stream, input.data(), input.size(), b, quotas[4], diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quotas[4]);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !quotas[4]->liveBytes());
        };
        --b.maxCommands;
        reject("RHI-QUERY-POINTS-STREAM-CAPACITY");
        ++b.maxCommands;
        --b.maxPasses;
        reject("RHI-QUERY-POINTS-PASS-CAPACITY");
        ++b.maxPasses;
        --b.maxQueryCommands;
        reject("RHI-QUERY-POINTS-COUNT");
        ++b.maxQueryCommands;
        --b.maxStreamTextBytes;
        reject("RHI-QUERY-POINTS-STREAM-CAPACITY");
        ++b.maxStreamTextBytes;
        for (auto* value :
            {&b.maxReferencedSlots, &b.maxReferencedScopeIds, &b.maxReferencedLabelBytes}) {
            --*value;
            reject("RHI-QUERY-POINTS-INPUT-CAPACITY");
            ++*value;
        }
        --b.maxCpuMetadataBytes;
        reject("RHI-QUERY-POINTS-QUOTA");
        ++b.maxCpuMetadataBytes;
        {
            core::CpuMemoryQuotaLease full(quotas[4], cpuBytes);
            assert(full.charged() && !factory()
                && std::string_view(diagnostic) == "RHI-QUERY-POINTS-ALLOCATION");
        }
        assert(!Points::prepareOriginal(stream, input.data(), input.size(), b, {}, diagnostic));
        assert(!Points::prepareOriginal(stream, nullptr, input.size(), b, quotas[4], diagnostic));
        assert(!Points::prepareOriginal(stream, input.data(), 0u, b, quotas[4], diagnostic));
        assert(!Points::prepareOriginal(stream, input.data(), 2u, b, quotas[4], diagnostic)
            && std::string_view(diagnostic) == "RHI-QUERY-POINTS-COVERAGE");
        input[0].commandOrdinal = 3u;
        reject("RHI-QUERY-POINTS-COVERAGE");
        input[0].commandOrdinal = 1u;
        const std::array<Points::Input, 3> reordered{{input[1], input[0], input[2]}},
            duplicate{{input[0], input[0], input[2]}};
        assert(!Points::prepareOriginal(stream, reordered.data(), 3u, b, quotas[4], diagnostic));
        assert(!Points::prepareOriginal(stream, duplicate.data(), 3u, b, quotas[4], diagnostic));
        auto forged = std::shared_ptr<const Stream>(quotas[3], stream.get());
        assert(!Points::prepareOriginal(forged, input.data(), 3u, b, quotas[4], diagnostic));
        forged.reset();
        auto foreign = prepareStream();
        assert(foreign);
        auto frozen = factory();
        assert(frozen && frozen->points().size() == 3u && frozen->passes().size() == 2u);
        assert(frozen->passes()[0].begin == 0u && frozen->passes()[0].end == 4u
            && frozen->passes()[0].scope == rhi::semantics::CommandRecordingOrder::Scope::Compute);
        assert(frozen->passes()[1].begin == 5u && frozen->passes()[1].end == 7u
            && frozen->passes()[1].scope == rhi::semantics::CommandRecordingOrder::Scope::Render);
        assert(!frozen->points()[0].pipelineBindOrdinal
            && frozen->points()[1].pipelineBindOrdinal == 2u
            && !frozen->points()[2].pipelineBindOrdinal);
        assert(frozen->points()[0].passIndex == 0u && frozen->points()[2].passIndex == 1u);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaLease full(
                quotas[4], static_cast<std::size_t>(cpuBytes - quotas[4]->liveBytes()));
            assert(full.charged());
            assert(!Points::validateOriginalStream(frozen, stream)
                && Points::validateOriginalStream(frozen, foreign));
            for (std::size_t i = 0u; i < input.size(); ++i)
                assert(frozen->points()[i].original.matches(input[i].original));
            assert(!frozen->nativeExecutionProven());
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        frozen.reset();
        assert(!quotas[4]->liveBytes());
        // Reject malformed complete streams using the existing L2 order parser.
        const auto original = commands;
        commands[0].scalar = scalar(K::BeginTransferPass);
        commands[4].scalar = scalar(K::EndTransferPass);
        stream = prepareStream();
        assert(stream);
        reject("RHI-COMMAND-ORDER-SCOPE");
        commands = original;
        commands[8].scalar = scalar(K::InsertDebugMarker);
        stream = prepareStream();
        assert(stream);
        reject("RHI-COMMAND-ORDER-INCOMPLETE");
        commands = original;
        commands[3].scalar = *A::capture(K::Timestamp, A::Query{2u});
        stream = prepareStream();
        assert(stream);
        reject("RHI-QUERY-POINTS-COVERAGE");
        commands = original;
        commands[2].scalar = scalar(K::PushDebugGroup);
        stream = prepareStream();
        assert(stream);
        reject("RHI-COMMAND-ORDER-SCOPE");
        commands = original;
        stream = prepareStream();
        frozen = factory();
        assert(frozen);
        std::weak_ptr<const Stream> weak = stream;
        stream.reset();
        foreign.reset();
        timestamp.reset();
        statistics.reset();
        assert(!weak.expired());
        assert(frozen->originalStream()->text(0u) == passLabel
            && frozen->points()[0].original.originalInput()->label() == td.label);
        assert(!Points::validateOriginalStream(frozen, frozen->originalStream()));
        frozen.reset();
        assert(weak.expired());
        weak.reset();
        // Input references are deliberately still live until this function returns.
        assert(!quotas[3]->liveBytes() && !quotas[4]->liveBytes());
        std::printf(
            "BINDLESS_RHI_QUERY_POINTS_OK %llu points3 passes2 exact-stream ordered-coverage optional-pipeline quotas rollback-free native-proof-closed allocation0\n",
            static_cast<unsigned long long>(peak));
    }
};
} // namespace arp::test
