#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_command_inputs.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace arp::test {
class WebGpuFrozenCommandScalarsFixture final {
    using Frozen = rhi_webgpu::WebGpuFrozenCommandScalars;
    using Store = rhi::CommandVariableArguments;
    using A = rhi::CommandScalarArguments;
    using K = rhi::CommandUsageKind;

public:
    static void verifyContract()
    {
        using namespace rhi_webgpu;
        WebGpuFrozenCommandScalarsBudget budget;
        assert(std::string_view(Frozen::validateBudget(budget))
            == "RHI-WEBGPU-COMMAND-SCALARS-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(Frozen::validateBudget(budget))
            == "RHI-WEBGPU-COMMAND-SCALARS-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(Frozen::validateBudget(budget))
            == "RHI-WEBGPU-COMMAND-SCALARS-BUDGET");
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        auto usageQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        auto argumentsQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        auto quota = core::makeShared<core::CpuMemoryQuota>(16384u);
        const char* diagnostic = nullptr;
        auto graph = WebGpuGenericCommandProjection::shareOriginal(
            WebGpuGenericCommandProjection::prepare({},
                nullptr,
                0u,
                {1u, true, 0u, 0u, 0u, 0u, 0u, 0u, {}, graphQuota->limitBytes(), 0u},
                graphQuota,
                diagnostic));
        std::array<rhi::CommandUsage, 4> commands{
            {{K::PushDebugGroup}, {K::InsertDebugMarker}, {K::PopDebugGroup}, {K::Finish}}};
        std::array<Store::Input, 4> input{{{*A::capture(K::PushDebugGroup, A::None{}), "debug"},
            {*A::capture(K::InsertDebugMarker, A::None{}), "marker"},
            {*A::capture(K::PopDebugGroup, A::None{})},
            {*A::capture(K::Finish, A::None{})}}};
        const auto makeUsage = [&] {
            return WebGpuGenericCommandUsage::prepareOriginal(graph,
                commands.data(),
                commands.size(),
                {1u, true, 4u, 0u, usageQuota->limitBytes()},
                usageQuota,
                diagnostic);
        };
        const auto makeArguments = [&] {
            return Store::prepareOriginal(input.data(),
                input.size(),
                {1u, true, 4u, 0u, 11u, 0u, argumentsQuota->limitBytes()},
                argumentsQuota,
                diagnostic);
        };
        auto usage = makeUsage();
        auto arguments = makeArguments();
        assert(usage && arguments);
        budget = {1u, true, 4u, 1u, quota->limitBytes()};
        const auto factory = [&] {
            return Frozen::prepareOriginal(usage, arguments, budget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        const auto expect = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !quota->liveBytes());
        };
        --budget.maxCommands;
        expect("RHI-WEBGPU-COMMAND-SCALARS-COUNT");
        ++budget.maxCommands;
        --budget.maxDebugDepth;
        expect("RHI-COMMAND-ORDER-DEBUG-CAPACITY");
        ++budget.maxDebugDepth;
        --budget.maxCpuMetadataBytes;
        expect("RHI-WEBGPU-COMMAND-SCALARS-QUOTA");
        ++budget.maxCpuMetadataBytes;
        const auto originalUsage = usage;
        const auto originalArguments = arguments;
        usage = {};
        expect("RHI-WEBGPU-COMMAND-SCALARS-ORIGINAL-USAGE");
        usage = originalUsage;
        arguments = {};
        expect("RHI-WEBGPU-COMMAND-SCALARS-ORIGINAL-ARGUMENTS");
        arguments = originalArguments;
        usage = std::shared_ptr<const WebGpuGenericCommandUsage>(quota, originalUsage.get());
        expect("RHI-WEBGPU-COMMAND-SCALARS-ORIGINAL-USAGE");
        usage = originalUsage;
        arguments = std::shared_ptr<const Store>(quota, originalArguments.get());
        expect("RHI-WEBGPU-COMMAND-SCALARS-ORIGINAL-ARGUMENTS");
        arguments = originalArguments;
        input[1].scalar = *A::capture(K::PushDebugGroup, A::None{});
        arguments = makeArguments();
        expect("RHI-WEBGPU-COMMAND-SCALARS-ORDER");
        arguments = originalArguments;
        commands[1].kind = K::PopDebugGroup;
        usage = makeUsage();
        expect("RHI-COMMAND-ORDER-DEBUG-UNDERFLOW");
        usage = originalUsage;
        {
            core::CpuMemoryQuotaLease full(quota, quota->limitBytes());
            assert(full.charged() && !factory()
                && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-SCALARS-ALLOCATION");
        }
        auto frozen = factory();
        assert(frozen && !frozen->validate());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const Store::Input exact{*A::capture(K::InsertDebugMarker, A::None{}), "marker"};
        assert(!frozen->compareRaw(1u, exact, nullptr, 0u));
        auto altered = exact;
        altered.text = "changed";
        assert(std::string_view(frozen->compareRaw(1u, altered, nullptr, 0u))
            == "RHI-WEBGPU-COMMAND-SCALARS-INPUT");
        assert(std::string_view(frozen->compareRaw(4u, exact, nullptr, 0u))
            == "RHI-WEBGPU-COMMAND-SCALARS-INPUT");
        assert(std::string_view(frozen->compareRaw(1u, exact, nullptr, 1u))
            == "RHI-WEBGPU-COMMAND-SCALARS-REFERENCE");
        assert(std::string_view(frozen->executionDiagnostic())
            == webGpuGenericRecordingUsageDiagnostic);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::printf("BINDLESS_RHI_FROZEN_SCALARS_CONTRACT_OK %llu order owner quotas allocation0\n",
            static_cast<unsigned long long>(peak));
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual(
        const std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryPhysicalGroups>& original,
        const char* name)
    {
        using namespace rhi_webgpu;
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        auto usageQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        auto argumentsQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        auto quota = core::makeShared<core::CpuMemoryQuota>(16384u);
        auto pipeline = original->pipeline();
        auto group = original;
        const char* diagnostic = nullptr;
        const std::array<WebGpuCommandReference, 2> references{
            {{WebGpuCommandReferenceDomain::Pipeline, WebGpuCommandReferenceSource::Pipeline},
                {WebGpuCommandReferenceDomain::Descriptor,
                    WebGpuCommandReferenceSource::PhysicalGroups}}};
        WebGpuGenericCommandProjection::Sources sources;
        sources.pipelines = &pipeline;
        sources.pipelineCount = 1u;
        sources.groups = &group;
        sources.groupCount = 1u;
        WebGpuGenericCommandProjectionBudget graphBudget{1u,
            true,
            0u,
            0u,
            1u,
            1u,
            2u,
            1u,
            {},
            graphQuota->limitBytes(),
            original->retainedAllocationBytes()};
        graphBudget
            .maxDomainReferences[static_cast<std::size_t>(WebGpuCommandReferenceDomain::Pipeline)] =
            1u;
        graphBudget.maxDomainReferences[static_cast<std::size_t>(
            WebGpuCommandReferenceDomain::Descriptor)] = 1u;
        auto graph = WebGpuGenericCommandProjection::shareOriginal(
            WebGpuGenericCommandProjection::prepare(std::move(sources),
                references.data(),
                references.size(),
                graphBudget,
                graphQuota,
                diagnostic));
        assert(graph && !diagnostic);
        std::array<rhi::CommandUsage, 6> commands{{{K::BeginComputePass, 0u, 0u},
            {K::BindComputePipeline, 0u, 1u},
            {K::BindComputeDescriptor, 1u, 1u},
            {K::Dispatch, 2u, 0u},
            {K::EndComputePass, 2u, 0u},
            {K::Finish, 2u, 0u}}};
        auto usage = WebGpuGenericCommandUsage::prepareOriginal(graph,
            commands.data(),
            commands.size(),
            {1u, true, 6u, 2u, usageQuota->limitBytes()},
            usageQuota,
            diagnostic);
        assert(usage);
        const bool hasSet = !original->observedSets().empty();
        const auto set = hasSet ? original->observedSets()[0].logicalSet : 0u;
        const auto offsetCount =
            hasSet ? original->observedSets()[0].observation->offsets().size() : 0u;
        assert(offsetCount <= 1u);
        const auto* offsets =
            offsetCount ? original->observedSets()[0].observation->offsets().data() : nullptr;
        std::array<Store::Input, 6> input{
            {{*A::capture(K::BeginComputePass, A::None{}), "original compute"},
                {*A::capture(K::BindComputePipeline, A::None{})},
                {*A::capture(K::BindComputeDescriptor, A::DescriptorBinding{set}),
                    {},
                    offsets,
                    static_cast<std::uint32_t>(offsetCount)},
                {*A::capture(K::Dispatch, A::Dispatch{3u, 5u, 7u})},
                {*A::capture(K::EndComputePass, A::None{})},
                {*A::capture(K::Finish, A::None{})}}};
        const auto makeArguments = [&] {
            return Store::prepareOriginal(input.data(),
                input.size(),
                {1u,
                    true,
                    6u,
                    static_cast<std::uint32_t>(offsetCount),
                    16u,
                    0u,
                    argumentsQuota->limitBytes()},
                argumentsQuota,
                diagnostic);
        };
        auto arguments = makeArguments();
        assert(arguments);
        const WebGpuFrozenCommandScalarsBudget budget{1u, true, 6u, 0u, quota->limitBytes()};
        const auto factory = [&] {
            return Frozen::prepareOriginal(usage, arguments, budget, quota, diagnostic);
        };
        if (!pipeline->compute() || !hasSet) {
            assert(!factory()
                && std::string_view(diagnostic)
                    == (!pipeline->compute() ? "RHI-WEBGPU-COMMAND-SCALARS-PIPELINE"
                                             : "RHI-WEBGPU-COMMAND-SCALARS-DESCRIPTOR"));
            assert(!quota->liveBytes());
            std::printf("BINDLESS_RHI_FROZEN_SCALARS_REJECT_OK %s family-or-missing-set\n", name);
            return;
        }
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        const auto originalArguments = arguments;
        input[2].scalar = *A::capture(K::BindComputeDescriptor, A::DescriptorBinding{set ^ 1u});
        arguments = makeArguments();
        assert(
            !factory() && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-SCALARS-DESCRIPTOR");
        input[2].scalar = *A::capture(K::BindComputeDescriptor, A::DescriptorBinding{set});
        arguments = originalArguments;
        std::uint32_t alteredOffset = offsetCount ? offsets[0] ^ 1u : 1u;
        input[2].offsets = &alteredOffset;
        input[2].offsetCount = 1u;
        arguments = Store::prepareOriginal(input.data(),
            input.size(),
            {1u, true, 6u, 1u, 16u, 0u, argumentsQuota->limitBytes()},
            argumentsQuota,
            diagnostic);
        assert(arguments && !factory()
            && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-SCALARS-OFFSETS");
        arguments = originalArguments;
        input[2].offsets = offsets;
        input[2].offsetCount = static_cast<std::uint32_t>(offsetCount);
        if (lossCount_) {
            // Same device/profile is insufficient: current groups must belong
            // to the currently bound original pipeline control block.
            auto foreignPipeline = loss_[0].frozen->usage()->projection()->pipelines()[0];
            WebGpuGenericCommandProjection::Sources foreignSources;
            foreignSources.pipelines = &foreignPipeline;
            foreignSources.pipelineCount = 1u;
            foreignSources.groups = &group;
            foreignSources.groupCount = 1u;
            auto foreignGraph = WebGpuGenericCommandProjection::shareOriginal(
                WebGpuGenericCommandProjection::prepare(std::move(foreignSources),
                    references.data(),
                    references.size(),
                    graphBudget,
                    graphQuota,
                    diagnostic));
            assert(foreignGraph && !foreignGraph->validate());
            auto foreignUsage = WebGpuGenericCommandUsage::prepareOriginal(foreignGraph,
                commands.data(),
                commands.size(),
                {1u, true, 6u, 2u, usageQuota->limitBytes()},
                usageQuota,
                diagnostic);
            assert(foreignUsage
                && !Frozen::prepareOriginal(foreignUsage, arguments, budget, quota, diagnostic)
                && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-SCALARS-PIPELINE");
        }
        auto frozen = factory();
        assert(frozen && !frozen->validate());
        const std::array<WebGpuGenericCommandUsage::RawReference, 2> raw{
            {{WebGpuCommandReferenceDomain::Pipeline, pipeline->compute().get()},
                {WebGpuCommandReferenceDomain::Descriptor, group.get()}}};
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (std::size_t i = 0u; i < commands.size(); ++i)
            assert(!frozen->compareRaw(i,
                input[i],
                commands[i].referenceCount ? &raw[commands[i].firstReference] : nullptr,
                commands[i].referenceCount));
        auto bad = raw[0];
        bad.original = group.get();
        assert(frozen->compareRaw(1u, input[1], &bad, 1u));
        bad = raw[0];
        bad.domain = WebGpuCommandReferenceDomain::Descriptor;
        assert(frozen->compareRaw(1u, input[1], &bad, 1u));
        assert(frozen->compareRaw(1u, input[1], nullptr, 1u));
        assert(frozen->compareRaw(2u, input[2], &raw[0], 1u));
        auto changed = input[3];
        changed.scalar = *A::capture(K::Dispatch, A::Dispatch{3u, 5u, 8u});
        assert(frozen->compareRaw(3u, changed, nullptr, 0u));
        const auto hash = pipeline->compute()->shaderPackageHash;
        ++pipeline->compute()->shaderPackageHash;
        assert(frozen->validate() && frozen->compareRaw(3u, input[3], nullptr, 0u));
        pipeline->compute()->shaderPackageHash = hash;
        assert(!frozen->validate());
        pipeline.reset();
        group.reset();
        graph.reset();
        usage.reset();
        arguments.reset();
        assert(!frozen->validate() && !frozen->compareRaw(3u, input[3], nullptr, 0u));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        if (std::string_view(name) != "ordinary-compute-small-range") {
            assert(lossCount_ < loss_.size());
            loss_[lossCount_++] = {
                std::move(frozen), quota, usageQuota, argumentsQuota, graphQuota};
        }
        std::printf(
            "BINDLESS_RHI_FROZEN_SCALARS_SOURCE_OK %s %llu order set offsets raw mutation drop allocation0\n",
            name,
            static_cast<unsigned long long>(peak));
    }

    static void verifyRetainedDescriptorExpiry()
    {
        assert(lossCount_ == 1u && loss_[0].frozen);
        assert(std::string_view(loss_[0].frozen->validate())
            == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-EXPIRED");
        std::puts("BINDLESS_RHI_FROZEN_SCALARS_DESCRIPTOR_EXPIRY_OK stale-reject retained-owner");
    }

    static void verifyActualDeviceLoss()
    {
        assert(lossCount_ == 1u);
        for (auto& probe : loss_) {
            assert(probe.frozen && probe.frozen->validate());
            assert(std::string_view(probe.frozen->validate())
                == "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE-LOST");
            probe.frozen.reset();
            assert(!probe.quota->liveBytes() && !probe.usageQuota->liveBytes()
                && !probe.argumentsQuota->liveBytes() && !probe.graphQuota->liveBytes());
            probe = {};
        }
        lossCount_ = 0u;
        std::puts("BINDLESS_RHI_FROZEN_SCALARS_LOSS_OK 1 source-reject retire");
    }

private:
    struct LossProbe {
        std::shared_ptr<const Frozen> frozen;
        std::shared_ptr<core::CpuMemoryQuota> quota, usageQuota, argumentsQuota, graphQuota;
    };

    inline static std::array<LossProbe, 1> loss_{};
    inline static std::size_t lossCount_ = 0u;
#endif
};
} // namespace arp::test
