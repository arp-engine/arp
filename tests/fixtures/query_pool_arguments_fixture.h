#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "rhi/backends/webgpu/webgpu_original_command_inputs.h"
#include "rhi/frontend/query_pool_arguments.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <type_traits>

namespace arp::test {
class QueryPoolArgumentsFixture final {
    using Fields = rhi::QueryPoolArguments;
    using Snapshot = rhi::QueryPoolInputSnapshot;
    using Arguments = rhi::QueryPoolOriginalInputArguments;
    static constexpr std::size_t cpuBytes = 65536u;
    using Quotas = std::array<std::shared_ptr<core::CpuMemoryQuota>, 3>;

    static rhi::QueryPoolDesc input(const std::uint64_t* scopes)
    {
        rhi::QueryPoolDesc d;
        d.label = "原始查询 scope IDs ✓ 🔒";
        d.type = static_cast<rhi::QueryType>(UINT32_MAX);
        d.count = UINT32_MAX;
        d.timingScopeIds = scopes;
        d.timingScopeIdCount = 3u;
        d.pipelineStatisticsMask = UINT32_MAX;
        return d;
    }

    static constexpr std::array<void (*)(rhi::QueryPoolDesc&), 5> mutations{
        {[](auto& d) {
             d.type = rhi::QueryType::Timestamp;
         },
            [](auto& d) {
                --d.count;
            },
            [](auto& d) {
                --d.timingScopeIdCount;
            },
            [](auto& d) {
                d.timingScopeIds = nullptr;
            },
            [](auto& d) {
                --d.pipelineStatisticsMask;
            }}};

public:
    static void verify()
    {
        using namespace rhi;
        static_assert(
            std::is_trivially_copyable_v<Fields> && !std::is_default_constructible_v<Fields>);
        std::array<std::uint64_t, 3> scopes{{UINT64_MAX, 0u, UINT64_MAX}};
        auto d = input(scopes.data());
        auto canonical = d;
        const auto fields = Fields::captureScalarFields(d);
        assert(fields.matchesScalarFields(d) && fields.scopePointerPresent());
        assert(!fields.validateDescriptor().success && !validateQueryPoolDesc(d).success);
        for (const auto mutate : mutations) {
            auto changed = d;
            mutate(changed);
            assert(!fields.matchesScalarFields(changed));
            assert(Fields::captureScalarFields(changed).matchesScalarFields(changed));
        }
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto decision = Snapshot::lowering(backend);
            assert(decision.cpuMetadataImplemented
                && decision.validatorOnly == (backend == Backend::Null)
                && std::string_view(decision.diagnostic) == "RHI-QUERY-ORIGINAL-OWNER-UNPROVEN");
        }
        assert(!Snapshot::lowering(static_cast<Backend>(UINT32_MAX)).cpuMetadataImplemented);
        QueryPoolInputSnapshotBudget b;
        assert(std::string_view(Snapshot::validateBudget(b)) == "RHI-QUERY-INPUT-VERSION");
        b.contractVersion = 1u;
        assert(std::string_view(Snapshot::validateBudget(b)) == "RHI-QUERY-INPUT-OPT-IN");
        b.prototypeOptIn = true;
        assert(std::string_view(Snapshot::validateBudget(b)) == "RHI-QUERY-INPUT-BUDGET");
        b = {1u, true, 3u, d.label.size(), cpuBytes, cpuBytes, cpuBytes};
        Quotas q;
        for (auto& quota : q)
            quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return Snapshot::prepareOriginal(d, b, q[0], q[1], q[2], diagnostic);
        };
        const auto retired = [&] {
            for (const auto& quota : q)
                assert(!quota->liveBytes());
        };
        std::array<std::uint64_t, 3> peaks{};
        for (std::size_t i = 0u; i < q.size(); ++i) {
            CpuQuotaFactoryFixture pressure(q[i]);
            peaks[i] = pressure.measure(factory);
            assert(peaks[i]);
            pressure.verify(peaks[i], factory);
            retired();
            {
                core::CpuMemoryQuotaLease full(q[i], cpuBytes);
                assert(full.charged() && !factory());
            }
            retired();
            auto missing = q;
            missing[i].reset();
            assert(!Snapshot::prepareOriginal(d, b, missing[0], missing[1], missing[2], diagnostic)
                && std::string_view(diagnostic) == "RHI-QUERY-INPUT-QUOTA");
            retired();
        }
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id);
            retired();
        };
        --b.maxScopeIds;
        reject("RHI-QUERY-INPUT-CAPACITY");
        ++b.maxScopeIds;
        --b.maxLabelBytes;
        reject("RHI-QUERY-INPUT-CAPACITY");
        ++b.maxLabelBytes;
        --b.maxCpuMetadataBytes;
        reject("RHI-QUERY-INPUT-QUOTA");
        ++b.maxCpuMetadataBytes;
        --b.maxCpuLabelBytes;
        reject("RHI-QUERY-INPUT-QUOTA");
        ++b.maxCpuLabelBytes;
        --b.maxCpuScopeBytes;
        reject("RHI-QUERY-INPUT-QUOTA");
        ++b.maxCpuScopeBytes;
        b.maxScopeIds = UINT32_MAX;
        reject("RHI-QUERY-INPUT-ALLOCATION");
        b.maxScopeIds = 3u;
        d.timingScopeIds = nullptr;
        reject("RHI-QUERY-INPUT-SCOPE-POINTER");
        d.timingScopeIds = scopes.data();
        auto snapshot = factory();
        assert(snapshot && snapshot->matches(d) && snapshot->hasQuotaBoundSharedOwner(snapshot));
        auto forged = std::shared_ptr<const Snapshot>(q[0], snapshot.get());
        assert(!Arguments::captureOriginal(CommandUsageKind::Timestamp, 0u, forged));
        forged.reset();
        d.label.clear();
        d.label.shrink_to_fit();
        scopes[0] = 1u;
        assert(!snapshot->matches(canonical));
        scopes[0] = UINT64_MAX;
        assert(snapshot->matches(canonical));
        std::array<QueryPoolDesc, 5> changedInputs;
        for (std::size_t i = 0u; i < changedInputs.size(); ++i) {
            changedInputs[i] = canonical;
            mutations[i](changedInputs[i]);
        }
        {
            std::array<core::CpuMemoryQuotaLease, 3> full{
                {{q[0], static_cast<std::size_t>(cpuBytes - q[0]->liveBytes())},
                    {q[1], static_cast<std::size_t>(cpuBytes - q[1]->liveBytes())},
                    {q[2], static_cast<std::size_t>(cpuBytes - q[2]->liveBytes())}}};
            for (const auto& lease : full)
                assert(lease.charged());
            const auto old = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            for (const auto& changed : changedInputs)
                assert(!snapshot->matches(changed));
            for (std::size_t i = 0u; i < scopes.size(); ++i) {
                const auto original = scopes[i];
                scopes[i] = original == 0u ? 1u : original - 1u;
                assert(!snapshot->matches(canonical));
                scopes[i] = original;
            }
            std::swap(scopes[0], scopes[1]);
            assert(!snapshot->matches(canonical));
            std::swap(scopes[0], scopes[1]);
            canonical.label[0] = 'x';
            assert(!snapshot->matches(canonical));
            canonical.label[0] = snapshot->label()[0];
            const auto timestamp = Arguments::captureOriginal(
                           CommandUsageKind::Timestamp, UINT32_MAX, snapshot),
                       statistics = Arguments::captureOriginal(
                           CommandUsageKind::PipelineStatistics, UINT32_MAX, snapshot);
            assert(timestamp && statistics && !timestamp->matches(*statistics)
                && timestamp->matches(*timestamp) && timestamp->validateDeclaration());
            assert(!Arguments::captureOriginal(CommandUsageKind::Dispatch, 0u, snapshot));
            assert(rhi_webgpu::WebGpuOriginalCommandInputs::family(CommandUsageKind::Timestamp)
                == rhi_webgpu::WebGpuOriginalCommandInputs::Family::Scalar);
            rhi_webgpu::WebGpuCommandReference query{CommandReferenceDomain::Query,
                rhi_webgpu::WebGpuCommandReferenceSource::Transfer,
                {17u}};
            rhi_webgpu::WebGpuGenericCommandProjectionBudget gb;
            gb.contractVersion = 1u;
            gb.prototypeOptIn = true;
            gb.maxReferences = 1u;
            gb.maxCpuMetadataBytes = cpuBytes;
            gb.maxDomainReferences[static_cast<std::size_t>(CommandReferenceDomain::Query)] = 1u;
            assert(!rhi_webgpu::WebGpuGenericCommandProjection::prepare(
                       {}, &query, 1u, gb, q[0], diagnostic)
                && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-DOMAIN-UNPROVEN");
            assert(std::string_view(snapshot->executionDiagnostic())
                    == "RHI-QUERY-ORIGINAL-OWNER-UNPROVEN"
                && core::defaultCpuAllocator().stats().allocationCount == allocations);
            std::pmr::set_default_resource(old);
        }
        snapshot.reset();
        retired();
        QueryPoolDesc valid;
        valid.type = QueryType::Timestamp;
        valid.count = 6u;
        valid.timingScopeIdCount = 3u;
        valid.timingScopeIds = scopes.data();
        b.maxLabelBytes = 0u;
        auto timestampInput = Snapshot::prepareOriginal(valid, b, q[0], q[1], q[2], diagnostic);
        assert(timestampInput && timestampInput->validateDescriptor().success);
        auto timestamp =
            Arguments::captureOriginal(CommandUsageKind::Timestamp, 5u, timestampInput);
        assert(timestamp && !timestamp->validateDeclaration());
        auto index = Arguments::captureOriginal(CommandUsageKind::Timestamp, 6u, timestampInput);
        assert(
            index && std::string_view(index->validateDeclaration()) == "RHI-QUERY-COMMAND-INDEX");
        auto kind =
            Arguments::captureOriginal(CommandUsageKind::PipelineStatistics, 0u, timestampInput);
        assert(kind && std::string_view(kind->validateDeclaration()) == "RHI-QUERY-COMMAND-TYPE");
        timestamp.reset();
        index.reset();
        kind.reset();
        timestampInput.reset();
        retired();
        valid.type = QueryType::PipelineStatistics;
        valid.count = 1u;
        valid.timingScopeIdCount = 0u;
        valid.timingScopeIds = nullptr;
        valid.pipelineStatisticsMask = kAllPipelineStatistics;
        b.maxScopeIds = 0u;
        auto statisticsInput = Snapshot::prepareOriginal(valid, b, q[0], q[1], q[2], diagnostic);
        assert(statisticsInput && statisticsInput->validateDescriptor().success);
        auto statistics =
            Arguments::captureOriginal(CommandUsageKind::PipelineStatistics, 0u, statisticsInput);
        assert(statistics && !statistics->validateDeclaration());
        statistics.reset();
        statisticsInput.reset();
        retired();
        valid.type = QueryType::Timestamp;
        valid.count = 0u;
        valid.pipelineStatisticsMask = 0u;
        valid.timingScopeIds = scopes.data();
        valid.label.assign("a\0\xff", 3u);
        b.maxLabelBytes = 3u;
        auto emptyPointer = Snapshot::prepareOriginal(valid, b, q[0], q[1], q[2], diagnostic);
        assert(emptyPointer && emptyPointer->matches(valid) && emptyPointer->scopeIds().empty()
            && emptyPointer->scalarFields().scopePointerPresent());
        assert(!emptyPointer->validateDescriptor().success
            && emptyPointer->label() == std::string_view("a\0\xff", 3u));
        valid.count = 1u;
        assert(!emptyPointer->matches(valid));
        emptyPointer.reset();
        retired();
        std::printf(
            "BINDLESS_RHI_QUERY_INPUT_OK %llu %llu %llu fields5 ordered-scopes3 pointer-presence labels kinds2 declaration-only budgets3 gates-closed allocation0\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]),
            static_cast<unsigned long long>(peaks[2]));
    }

    static void prepareActualLoss()
    {
        const std::array<std::uint64_t, 3> scopes{{UINT64_MAX, 0u, UINT64_MAX}};
        auto d = input(scopes.data());
        for (auto& q : lossQuotas_)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* diagnostic = nullptr;
        lossSnapshot_ = Snapshot::prepareOriginal(d,
            {1u, true, 3u, d.label.size(), cpuBytes, cpuBytes, cpuBytes},
            lossQuotas_[0],
            lossQuotas_[1],
            lossQuotas_[2],
            diagnostic);
        assert(lossSnapshot_);
    }

    static void verifyActualDeviceLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost());
        const std::array<std::uint64_t, 3> scopes{{UINT64_MAX, 0u, UINT64_MAX}};
        assert(lossSnapshot_->matches(input(scopes.data()))
            && std::string_view(lossSnapshot_->executionDiagnostic())
                == "RHI-QUERY-ORIGINAL-OWNER-UNPROVEN");
        lossSnapshot_.reset();
        for (auto& q : lossQuotas_) {
            assert(!q->liveBytes());
            q.reset();
        }
        std::puts(
            "BINDLESS_RHI_QUERY_INPUT_LOSS_OK cpu-original-data preserved native-source-closed exact-retire");
    }

private:
    static inline std::shared_ptr<const Snapshot> lossSnapshot_;
    static inline Quotas lossQuotas_;
};
} // namespace arp::test
