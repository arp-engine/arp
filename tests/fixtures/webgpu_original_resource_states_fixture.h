#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_resource_states.h"
#include "rhi/backends/webgpu/webgpu_original_transfer_state_validation.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class WebGpuOriginalResourceStatesFixture final {
    using Plan = rhi_webgpu::WebGpuOriginalResourceStates;

public:
    static void verifyControlDomain()
    {
        using namespace rhi;
        for (std::uint32_t i = 0u; i < 8u; ++i) {
            common::RecordedResourceStateControl control(static_cast<ResourceState>(i));
            auto observation = control.observe();
            assert(observation && control.matches(*observation));
            assert(semantics::isBufferResourceState(static_cast<ResourceState>(i)) == (i < 5u));
        }
        common::RecordedResourceStateControl invalid(static_cast<ResourceState>(UINT32_MAX));
        assert(
            !invalid.observe() && !invalid.matches({static_cast<ResourceState>(UINT32_MAX), 1u}));
    }

    static std::shared_ptr<const Plan> verify(
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalCommandInputs> inputs,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char* label)
    {
        using namespace rhi_webgpu;
        const char* diagnostic = nullptr;
        WebGpuOriginalResourceStatesBudget budget;
        assert(
            std::string_view(Plan::validateBudget(budget)) == "RHI-WEBGPU-ORIGINAL-STATES-VERSION");
        budget.contractVersion = 1u;
        assert(
            std::string_view(Plan::validateBudget(budget)) == "RHI-WEBGPU-ORIGINAL-STATES-OPT-IN");
        budget.prototypeOptIn = true;
        assert(
            std::string_view(Plan::validateBudget(budget)) == "RHI-WEBGPU-ORIGINAL-STATES-BUDGET");
        const auto& graph = *inputs->scalars()->usage()->projection();
        std::size_t references = 0u;
        if (graph.transfer())
            references += graph.transfer()->resources().size();
        if (graph.resources())
            references += graph.resources()->resources().size();
        for (const auto& group : graph.physicalGroups())
            for (const auto& set : group->observedSets())
                references += set.observation->resources().resources().size();
        budget = {1u,
            true,
            static_cast<std::uint32_t>(references),
            static_cast<std::uint32_t>(references),
            quota->limitBytes()};
        auto factory = [&] {
            return Plan::prepareOriginal(inputs, budget, quota, diagnostic);
        };
        auto scout = factory();
        assert(scout && !scout->validate());
        const auto controls = scout->states().size();
        scout.reset();
        budget.maxStateControls = static_cast<std::uint32_t>(controls);
        CpuQuotaFactoryFixture pressure(quota);
        const auto exactPeak = pressure.measure(factory);
        pressure.verify(exactPeak, factory);
        const auto expect = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !quota->liveBytes());
        };
        auto original = inputs;
        inputs.reset();
        expect("RHI-WEBGPU-ORIGINAL-STATES-INPUTS");
        inputs = original;
        inputs = std::shared_ptr<const WebGpuOriginalCommandInputs>(quota, original.get());
        expect("RHI-WEBGPU-ORIGINAL-STATES-INPUTS");
        inputs = original;
        --budget.maxCpuMetadataBytes;
        expect("RHI-WEBGPU-ORIGINAL-STATES-QUOTA");
        ++budget.maxCpuMetadataBytes;
        budget.maxStateControls = UINT32_MAX;
        expect("RHI-WEBGPU-ORIGINAL-STATES-ALLOCATION");
        budget.maxStateControls = static_cast<std::uint32_t>(controls);
        {
            core::CpuMemoryQuotaLease exhausted(quota, quota->limitBytes());
            assert(exhausted.charged());
            assert(!factory()
                && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATES-ALLOCATION");
        }
        if (references) {
            --budget.maxSourceReferences;
            expect("RHI-WEBGPU-ORIGINAL-STATES-REFERENCE-CAPACITY");
            ++budget.maxSourceReferences;
        }
        auto plan = factory();
        assert(plan && !plan->validate() && plan->sourceReferences() == references);
        plan.reset();
        if (controls) {
            --budget.maxStateControls;
            expect("RHI-WEBGPU-ORIGINAL-STATES-CONTROL-CAPACITY");
            ++budget.maxStateControls;
        }
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        plan = factory();
        std::pmr::set_default_resource(previous);
        assert(plan && !plan->validate());
        if (controls) {
            // The original retained control is private metadata, with no native
            // operation. Exercise its journal domain under external serialization.
            const auto control = plan->states()[0].source.control;
            const auto write = [&](rhi::ResourceState after, core::MutationJournal* journal) {
                if (control.scalarControl())
                    return const_cast<rhi::common::RecordedResourceStateControl*>(
                        control.scalarControl())
                        ->write(after, journal);
                return const_cast<rhi::common::RecordedTextureStateControl*>(
                    control.textureControl())
                    ->writeWhole(after, journal);
            };
            const auto before = control.state();
            if (plan->states()[0].source.domain == Plan::Domain::Buffer) {
                assert(!write(rhi::ResourceState::ColorAttachment, nullptr));
                assert(std::string_view(plan->validate())
                    == "RHI-WEBGPU-ORIGINAL-STATES-STATE-DOMAIN");
                assert(!factory()
                    && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATES-STATE-DOMAIN");
                assert(!write(before, nullptr));
                plan.reset();
                plan = factory();
                assert(plan);
            }
            const auto revision = control.revision();
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(std::string_view(write(static_cast<rhi::ResourceState>(UINT32_MAX), nullptr))
                == "RHI-RESOURCE-STATE-INVALID");
            assert(control.revision() == revision && !plan->validate());
            assert(!write(before, nullptr));
            assert(std::string_view(plan->validate()) == "RHI-WEBGPU-ORIGINAL-STATES-STALE");
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            plan.reset();
            plan = factory();
            assert(plan);
            core::MutationJournal accepted, conflicting;
            assert(!write(before, &accepted));
            assert(std::string_view(plan->validate()) == "RHI-WEBGPU-ORIGINAL-STATES-PENDING");
            assert(
                !factory() && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATES-PENDING");
            const auto pendingRevision = control.revision();
            assert(std::string_view(write(before, &conflicting))
                == "RHI-RESOURCE-STATE-RECORDING-CONFLICT");
            assert(conflicting.empty() && control.revision() == pendingRevision);
            accepted.rollback();
            assert(control.state() == before && control.revision() > pendingRevision);
            assert(std::string_view(plan->validate()) == "RHI-WEBGPU-ORIGINAL-STATES-STALE");
            plan.reset();
            plan = factory();
            assert(plan);
            const auto after = before == rhi::ResourceState::CopySrc ? rhi::ResourceState::CopyDst
                                                                     : rhi::ResourceState::CopySrc;
            assert(!write(after, &accepted));
            accepted.commit();
            assert(control.state() == after && plan->validate());
            plan.reset();
            plan = factory();
            assert(plan && plan->states()[0].observation.value == after);
            assert(!write(before, nullptr));
            assert(plan->validate());
            plan.reset();
            plan = factory();
            assert(plan && !plan->validate());
        }
        const auto validation = WebGpuOriginalTransferStateValidation::validate(plan, 0u);
        if (std::string_view(label) == "empty" || std::string_view(label) == "inventory")
            assert(!validation.diagnostic);
        else
            assert(validation
                    .diagnostic); // Full pass/Graphics and mixed alias dependencies remain unproven.
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (const auto& state : plan->states())
            assert(plan->find(state.source.resource) == &state);
        assert(!plan->find({UINT64_MAX}));
        assert(
            std::string_view(plan->executionDiagnostic()) == webGpuGenericRecordingUsageDiagnostic);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::printf(
            "BINDLESS_RHI_ORIGINAL_STATES_OK %s %llu %zu %zu current revision rollback quotas allocation0\n",
            label,
            static_cast<unsigned long long>(exactPeak),
            references,
            controls);
        return plan;
    }
};
} // namespace arp::test
