#pragma once

#include "command_recording_order_fixture.h"
#include "command_scalar_arguments_fixture.h"
#include "command_usage_contract_fixture.h"
#include "cpu_quota_factory_fixture.h"
#include "recorded_resource_state_sequence_fixture.h"
#include "resource_dependency_arguments_fixture.h"
#include "rhi/backends/webgpu/webgpu_command_inputs.h"
#include "shared_identity_projection_fixture.h"
#include "versioned_mutation_value_fixture.h"
#include "webgpu_frozen_command_scalars_fixture.h"
#include "webgpu_original_command_inputs_fixture.h"
#include "webgpu_original_scalar_access_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace arp::test {
class WebGpuGenericCommandUsageFixture final {
public:
    static void verifyContract()
    {
        using namespace rhi_webgpu;
        CommandUsageContractFixture::verify();
        CommandRecordingOrderFixture::verify();
        ResourceDependencyArgumentsFixture::verify();
        RecordedResourceStateSequenceFixture::verify();
        VersionedMutationValueFixture::verify();
        SharedIdentityProjectionFixture::verify();
        CommandVariableArgumentsFixture::verify();
        WebGpuFrozenCommandScalarsFixture::verifyContract();
        WebGpuOriginalCommandInputsFixture::verifyContract();
        WebGpuOriginalScalarAccessFixture::verifyContract();
        WebGpuGenericCommandUsageBudget budget;
        assert(std::string_view(WebGpuGenericCommandUsage::validateBudget(budget))
            == "RHI-WEBGPU-COMMAND-USAGE-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(WebGpuGenericCommandUsage::validateBudget(budget))
            == "RHI-WEBGPU-COMMAND-USAGE-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(WebGpuGenericCommandUsage::validateBudget(budget))
            == "RHI-WEBGPU-COMMAND-USAGE-BUDGET");
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        const char* diagnostic = nullptr;
        const auto makeGraph = [&] {
            return WebGpuGenericCommandProjection::prepare({},
                nullptr,
                0u,
                {1u, true, 0u, 0u, 0u, 0u, 0u, 0u, {}, graphQuota->limitBytes(), 0u},
                graphQuota,
                diagnostic);
        };
        std::shared_ptr<const WebGpuGenericCommandProjection> original =
            WebGpuGenericCommandProjection::shareOriginal(makeGraph());
        auto quota = core::makeShared<core::CpuMemoryQuota>(16384u);
        budget.maxCommands = 2u;
        budget.maxCpuMetadataBytes = quota->limitBytes();
        std::array<rhi::CommandUsage, 2> commands{
            {{rhi::CommandUsageKind::InsertDebugMarker}, {rhi::CommandUsageKind::Finish}}};
        const auto factory = [&] {
            return WebGpuGenericCommandUsage::prepareOriginal(
                original, commands.data(), commands.size(), budget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture admission(quota);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        assert(!quota->liveBytes());
        auto plan = factory();
        assert(plan && !diagnostic && !plan->validate());
        {
            WebGpuGenericCommandUsage::Consumption consumption(plan);
            assert(!consumption.complete());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            auto ticket =
                consumption.preflight(rhi::CommandUsageKind::Finish, nullptr, 0u, diagnostic);
            assert(!ticket && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-USAGE-ORDER");
            for (const auto kind :
                {rhi::CommandUsageKind::InsertDebugMarker, rhi::CommandUsageKind::Finish}) {
                ticket = consumption.preflight(kind, nullptr, 0u, diagnostic);
                assert(ticket && !diagnostic && consumption.commit(ticket, diagnostic));
            }
            assert(consumption.complete() && consumption.consumedCommands() == 2u
                && !consumption.consumedReferences());
            assert(std::string_view(consumption.executionDiagnostic())
                == webGpuGenericRecordingUsageDiagnostic);
            assert(!consumption.preflight(rhi::CommandUsageKind::Finish, nullptr, 0u, diagnostic));
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        }
        plan.reset();
        assert(!quota->liveBytes());
        const auto expect = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !quota->liveBytes());
        };
        budget.maxCommands = 1u;
        expect("RHI-WEBGPU-COMMAND-USAGE-CAPACITY");
        budget.maxCommands = 2u;
        commands[0].firstReference = 1u;
        expect("RHI-WEBGPU-COMMAND-USAGE-SPAN");
        commands[0].firstReference = 0u;
        commands[0].kind = rhi::CommandUsageKind::Finish;
        expect("RHI-WEBGPU-COMMAND-USAGE-FINISH");
        commands[0].kind = rhi::CommandUsageKind::InsertDebugMarker;
        commands[1].kind = rhi::CommandUsageKind::Dispatch;
        expect("RHI-WEBGPU-COMMAND-USAGE-FINISH");
        commands[1].kind = rhi::CommandUsageKind::Finish;
        commands[0].kind = rhi::CommandUsageKind::BindComputePipeline;
        expect("RHI-WEBGPU-COMMAND-USAGE-SIGNATURE");
        commands[0].kind = rhi::CommandUsageKind::InsertDebugMarker;
        commands[0].kind = static_cast<rhi::CommandUsageKind>(UINT32_MAX);
        expect("RHI-WEBGPU-COMMAND-USAGE-SIGNATURE");
        commands[0].kind = rhi::CommandUsageKind::InsertDebugMarker;
        --budget.maxCpuMetadataBytes;
        expect("RHI-WEBGPU-COMMAND-USAGE-QUOTA");
        ++budget.maxCpuMetadataBytes;
        budget.maxCommands = UINT32_MAX;
        expect("RHI-WEBGPU-COMMAND-USAGE-ALLOCATION");
        budget.maxCommands = 2u;
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            assert(pressure.charged() && !factory()
                && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-USAGE-ALLOCATION");
        }
        auto canonical = original;
        original = core::shareUnique(makeGraph());
        expect("RHI-WEBGPU-COMMAND-USAGE-ORIGINAL-PROJECTION");
        original = std::shared_ptr<const WebGpuGenericCommandProjection>(quota, canonical.get());
        expect("RHI-WEBGPU-COMMAND-USAGE-ORIGINAL-PROJECTION");
        original = canonical;
        assert(!WebGpuGenericCommandUsage::prepare(original, nullptr, 0u, budget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-USAGE-CAPACITY");
        auto unissued = core::shareUnique(WebGpuGenericCommandUsage::prepare(
            original, commands.data(), commands.size(), budget, quota, diagnostic));
        {
            WebGpuGenericCommandUsage::Consumption consumption(unissued);
            assert(
                std::string_view(consumption.validate()) == "RHI-WEBGPU-COMMAND-USAGE-ORIGINAL-PLAN"
                && !consumption.complete());
        }
        unissued.reset();
        WebGpuGenericCommandUsage::Consumption missing({});
        assert(std::string_view(missing.validate()) == "RHI-WEBGPU-COMMAND-USAGE-ORIGINAL-PLAN"
            && !missing.complete());
        original.reset();
        canonical.reset();
        assert(!quota->liveBytes() && !graphQuota->liveBytes());
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static std::shared_ptr<const rhi_webgpu::WebGpuGenericCommandUsage> verifyActual(
        const std::shared_ptr<const rhi_webgpu::WebGpuGenericCommandProjection>& original,
        std::array<rhi::CommandUsage, 3> commands,
        const rhi_webgpu::WebGpuGenericCommandUsage::RawReference* references,
        std::size_t referenceCount,
        const char* name)
    {
        using namespace rhi_webgpu;
        using Plan = WebGpuGenericCommandUsage;
        auto quota = core::makeShared<core::CpuMemoryQuota>(16384u);
        WebGpuGenericCommandUsageBudget budget{
            1u, true, 3u, static_cast<std::uint32_t>(referenceCount), quota->limitBytes()};
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return Plan::prepareOriginal(
                original, commands.data(), commands.size(), budget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture admission(quota);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        assert(!quota->liveBytes());
        const auto expect = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !quota->liveBytes());
        };
        --budget.maxReferenceUsages;
        expect("RHI-WEBGPU-COMMAND-USAGE-CAPACITY");
        ++budget.maxReferenceUsages;
        --budget.maxCommands;
        expect("RHI-WEBGPU-COMMAND-USAGE-CAPACITY");
        ++budget.maxCommands;
        const auto copiedCommands = commands;
        commands[0].kind = rhi::CommandUsageKind::Draw;
        expect("RHI-WEBGPU-COMMAND-USAGE-SIGNATURE");
        commands = copiedCommands;
        commands[1].firstReference = 0u;
        expect("RHI-WEBGPU-COMMAND-USAGE-SPAN");
        commands = copiedCommands;
        commands[0].referenceCount = UINT32_MAX;
        expect("RHI-WEBGPU-COMMAND-USAGE-SPAN");
        commands = copiedCommands;
        const rhi::CommandUsage onlyFinish{rhi::CommandUsageKind::Finish};
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Plan::prepare(original, &onlyFinish, 1u, budget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-USAGE-REFERENCE-TAIL");
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        auto plan = factory();
        assert(plan && !diagnostic);
        commands[0].kind = rhi::CommandUsageKind::Finish;
        assert(plan->commands()[0].kind == copiedCommands[0].kind && !plan->validate());
        std::array<Plan::RawReference, 4> bad{};
        assert(referenceCount <= bad.size() && copiedCommands[0].referenceCount);
        for (std::size_t i = 0u; i < referenceCount; ++i)
            bad[i] = references[i];
        std::weak_ptr<const Plan> weak = plan;
        {
            Plan::Consumption consumption(plan), foreign(plan);
            std::shared_ptr<const Plan> alias(quota, plan.get());
            Plan::Consumption forged(alias);
            assert(std::string_view(forged.validate()) == "RHI-WEBGPU-COMMAND-USAGE-ORIGINAL-PLAN");
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            assert(pressure.charged());
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            const auto first = copiedCommands[0];
            assert(!consumption.preflight(
                       copiedCommands[1].kind, references, first.referenceCount, diagnostic)
                && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-USAGE-ORDER");
            assert(!consumption.preflight(
                       first.kind, references, first.referenceCount - 1u, diagnostic)
                && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-USAGE-REFERENCE-COUNT");
            assert(!consumption.preflight(first.kind, nullptr, first.referenceCount, diagnostic)
                && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-USAGE-REFERENCE-COUNT");
            bad[first.referenceCount - 1u].original = nullptr;
            assert(!consumption.preflight(first.kind, bad.data(), first.referenceCount, diagnostic)
                && std::string_view(diagnostic)
                    == "RHI-WEBGPU-COMMAND-PROJECTION-REFERENCE-UNCOVERED");
            assert(!consumption.consumedCommands() && !consumption.consumedReferences());
            bad[first.referenceCount - 1u].original = &copiedCommands;
            assert(!consumption.preflight(first.kind, bad.data(), first.referenceCount, diagnostic)
                && std::string_view(diagnostic)
                    == "RHI-WEBGPU-COMMAND-PROJECTION-REFERENCE-UNCOVERED");
            assert(!consumption.consumedCommands() && !consumption.consumedReferences());
            bad[first.referenceCount - 1u] = references[first.referenceCount - 1u];
            bad[0].domain = rhi::CommandReferenceDomain::Count;
            assert(
                !consumption.preflight(first.kind, bad.data(), first.referenceCount, diagnostic));
            const auto foreignTicket =
                foreign.preflight(first.kind, references, first.referenceCount, diagnostic);
            assert(foreignTicket && !consumption.commit(foreignTicket, diagnostic)
                && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-USAGE-TICKET");
            for (const auto& command : copiedCommands) {
                assert(!consumption.complete());
                const auto ticket = consumption.preflight(command.kind,
                    command.referenceCount ? references + command.firstReference : nullptr,
                    command.referenceCount,
                    diagnostic);
                assert(ticket && !diagnostic && consumption.commit(ticket, diagnostic));
                assert(!consumption.commit(ticket, diagnostic)
                    && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-USAGE-TICKET");
            }
            assert(consumption.complete() && consumption.consumedCommands() == 3u
                && consumption.consumedReferences() == referenceCount);
            assert(std::string_view(consumption.executionDiagnostic())
                == webGpuGenericRecordingUsageDiagnostic);
            assert(!consumption.preflight(rhi::CommandUsageKind::Finish, nullptr, 0u, diagnostic));
            foreign.abort();
            assert(!foreign.commit(foreignTicket, diagnostic) && !foreign.complete());
            plan.reset();
            assert(!weak.expired() && !consumption.validate());
            assert(core::defaultCpuAllocator().stats().allocationCount == before);
        }
        assert(weak.expired());
        weak.reset();
        assert(!quota->liveBytes());
        commands = copiedCommands;
        plan = factory();
        assert(plan);
        std::printf("BINDLESS_RHI_GENERIC_USAGE_OK %s %llu 3 %zu\n",
            name,
            static_cast<unsigned long long>(peak),
            referenceCount);
        return plan;
    }
#endif
};
} // namespace arp::test
