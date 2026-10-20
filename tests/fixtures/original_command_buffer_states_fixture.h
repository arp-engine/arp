#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_state.h"
#include "rhi/semantics/command_lowering.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandBufferStatesFixture final {
    static constexpr std::uint64_t bytes = 65536u;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    template <class Factory, class Transition>
    static void verify(Factory&& create, Transition&& transition)
    {
        using Graph = rhi::OriginalCommandBufferStates;
        using Inputs = rhi::OriginalCommandInputs;
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        using Store = rhi::CommandVariableArguments;
        using Scalars = rhi::OriginalCommandScalars;
        using Scalar = rhi::CommandScalarArguments;
        using K = rhi::CommandUsageKind;
        using S = rhi::ResourceState;
        auto domain = quota(), registry = quota(), sourceQuota = quota(), inputQuota = quota(),
             stateQuota = quota();
        {
            auto device = create();
            assert(device);
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            assert(!device->prepareOriginalResourceRegistration(
                {1u, true, 2u, 0u, 0u, 7u, bytes}, registry));
            rhi::BufferDesc desc;
            desc.sizeBytes = 64u;
            desc.usage = rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst;
            desc.initialState = S::CopySrc;
            auto a = device->createBuffer(desc);
            desc.initialState = S::CopyDst;
            auto b = device->createBuffer(desc);
            assert(a && b);
            auto* original = a.get();
            const rhi::BufferCopyDesc copy{a->id(), b->id(), 0u, 0u, 16u};
            const char* diagnostic = nullptr;
            std::array<Sources::Source, 2> leases{
                rhi::OriginalCommandBufferLease::prepareOriginal(
                    a, {1u, true, 2u, bytes}, sourceQuota, diagnostic),
                rhi::OriginalCommandBufferLease::prepareOriginal(
                    b, {1u, true, 2u, bytes}, sourceQuota, diagnostic)};
            rhi::OriginalCommandAdmissionBudget budget;
            budget.contractVersion = 1u;
            budget.prototypeOptIn = true;
            budget.maxCommands = 4u;
            budget.maxReferences = 2u;
            budget.maxGraphOwners = 9u;
            budget.maxInputDescriptors = 1u;
            budget.maxStateObservations = 2u;
            budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] =
                2u;
            budget.maxCpuBytes[0] = budget.maxCpuBytes[1] = bytes;
            auto sources = Sources::prepareOriginal(
                leases.data(), leases.size(), budget, sourceQuota, diagnostic);
            assert(sources);
            const std::array<rhi::CommandUsage, 4> commands{{{K::BeginTransferPass, 0u, 0u},
                {K::BufferCopy, 0u, 2u},
                {K::EndTransferPass, 2u, 0u},
                {K::Finish, 2u, 0u}}};
            const std::array<Usage::Reference, 2> references{{{0u}, {1u}}};
            auto usage = Usage::prepareOriginal(sources,
                commands.data(),
                commands.size(),
                references.data(),
                references.size(),
                inputQuota,
                diagnostic);
            assert(usage);
            const std::array<Store::Input, 4> fields{
                {{*Scalar::capture(K::BeginTransferPass, Scalar::None{})},
                    {*Scalar::capture(K::BufferCopy, Scalar::None{})},
                    {*Scalar::capture(K::EndTransferPass, Scalar::None{})},
                    {*Scalar::capture(K::Finish, Scalar::None{})}}};
            auto store = Store::prepareOriginal(fields.data(),
                fields.size(),
                {1u, true, 4u, 0u, 0u, 0u, bytes},
                inputQuota,
                diagnostic);
            assert(store);
            auto scalars = Scalars::prepareOriginal(usage, store, inputQuota, diagnostic);
            assert(scalars);
            const Inputs::OriginalDescriptor descriptor{
                1u, *rhi::TransferCopyArguments::capture(copy)};
            const auto inputFactory = [&] {
                return Inputs::prepareOriginal(scalars, &descriptor, 1u, inputQuota, diagnostic);
            };
            auto inputs = inputFactory(), equalInputs = inputFactory();
            assert(inputs && equalInputs && inputs->retainedOwnerCount() == 9u);
            rhi::OriginalCommandStateAdmissionBudget stateBudget{
                1u, true, 1u, 2u, 2u, 0u, 15u, bytes};
            const auto factory = [&] {
                return Graph::prepareOriginal(inputs, *device, stateBudget, stateQuota, diagnostic);
            };
            CpuQuotaFactoryFixture pressure(stateQuota);
            const auto peak = pressure.measure(factory);
            pressure.verify(peak, factory);
            {
                core::CpuMemoryQuotaLease full(stateQuota, bytes - stateQuota->liveBytes());
                assert(full.charged() && !factory());
                is(diagnostic, "RHI-ORIGINAL-BUFFER-STATES-ALLOCATION");
            }
            const auto saved = stateBudget;
            --stateBudget.maxResourceRegistrations;
            assert(!factory());
            is(diagnostic, "RHI-ORIGINAL-STATE-REGISTRATION-CAPACITY");
            stateBudget = saved;
            --stateBudget.maxStateObservations;
            assert(!factory());
            is(diagnostic, "RHI-ORIGINAL-BUFFER-STATES-OBSERVATION-CAPACITY");
            stateBudget = saved;
            --stateBudget.maxRetainedOwners;
            assert(!factory());
            is(diagnostic, "RHI-ORIGINAL-STATE-OWNER-CAPACITY");
            stateBudget = saved;
            assert(!Graph::prepareOriginal(inputs, *device, stateBudget, inputQuota, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-BUFFER-STATES-QUOTA-DOMAIN");
            assert(!Graph::prepareOriginal(inputs, *device, stateBudget, sourceQuota, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-BUFFER-STATES-QUOTA-DOMAIN");
            auto forgedInput = std::shared_ptr<const Inputs>(sourceQuota, inputs.get());
            assert(
                !Graph::prepareOriginal(forgedInput, *device, stateBudget, stateQuota, diagnostic));
            forgedInput.reset();
            auto graph = factory();
            assert(graph && graph->retainedOwnerCount() == 15u && graph->entries().size() == 2u
                && !Graph::validateOriginal(graph));
            is(graph->compareOriginalInputs(equalInputs, *device),
                "RHI-ORIGINAL-BUFFER-STATES-INPUTS");
            auto forged = std::shared_ptr<const Graph>(sourceQuota, graph.get());
            is(Graph::validateOriginal(forged), "RHI-ORIGINAL-BUFFER-STATES-OWNER");
            forged.reset();
            auto foreign = create();
            auto foreignDomain = quota(), foreignRegistry = quota();
            assert(foreign);
            assert(
                !foreign->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, foreignDomain));
            assert(!foreign->prepareOriginalResourceRegistration(
                {1u, true, 2u, 0u, 0u, 7u, bytes}, foreignRegistry));
            is(graph->compareOriginalInputs(inputs, *foreign),
                "RHI-ORIGINAL-DEVICE-LIFETIME-FOREIGN-DEVICE");
            assert(!Graph::prepareOriginal(inputs, *foreign, stateBudget, stateQuota, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
            core::MutationJournal journal;
            const auto observed = graph->entries()[0].observation;
            assert(transition(*original, S::CopySrc, S::CopyDst, &journal).success);
            is(graph->validate(), "RHI-ORIGINAL-BUFFER-STATE-PENDING");
            assert(!factory());
            journal.rollback();
            is(graph->validate(), "RHI-ORIGINAL-BUFFER-STATE-STALE");
            assert(original->state() == observed.state());
            graph = factory();
            assert(graph);
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{domain, registry, sourceQuota, inputQuota, stateQuota}, [&]() noexcept {
                    if (const auto* id = Graph::validateOriginal(graph))
                        return id;
                    if (const auto* id = graph->compareOriginalInputs(inputs, *device))
                        return id;
                    if (!graph->compareOriginalInputs(equalInputs, *device))
                        return "TEST-FOREIGN-INPUTS-ACCEPTED";
                    for (const auto backend : {rhi::Backend::Null,
                             rhi::Backend::Vulkan,
                             rhi::Backend::Metal,
                             rhi::Backend::WebGPU})
                        if (!graph->executionDiagnostic(backend) || graph->nativeExecutionProven()
                            || arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(
                                backend)
                                .originalIssuerAvailable)
                            return "TEST-EXECUTION-OPEN";
                    if (!transition(*original, S::CopySrc, S::CopyDst, &journal).success)
                        return "TEST-ORIGINAL-WRITE";
                    if (!graph->validate())
                        return "TEST-PENDING-ACCEPTED";
                    journal.rollback();
                    return graph->validate() ? nullptr : "TEST-ROLLBACK-STALE-ACCEPTED";
                }));
            graph = factory();
            assert(graph);
            assert(transition(*original, S::CopySrc, S::CopyDst, &journal).success);
            journal.commit();
            is(graph->validate(), "RHI-ORIGINAL-BUFFER-STATE-STALE");
            graph = factory();
            assert(graph && graph->entries()[0].observation.state() == S::CopyDst);
            const auto limit = stateQuota->limitBytes();
            stateQuota->setLimitBytes(limit - 1u);
            is(graph->validate(), "RHI-ORIGINAL-BUFFER-STATES-QUOTA");
            stateQuota->setLimitBytes(limit);
            std::weak_ptr<const Inputs> retained = inputs;
            equalInputs.reset();
            inputs.reset();
            scalars.reset();
            usage.reset();
            store.reset();
            sources.reset();
            leases = {};
            assert(!retained.expired() && !graph->validate()
                && !graph->compareOriginalInputs(retained.lock(), *device));
            device.reset();
            is(graph->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
            graph.reset();
            retained.reset();
            foreign.reset();
        }
        assert(!domain->liveBytes() && !registry->liveBytes() && !sourceQuota->liveBytes()
            && !inputQuota->liveBytes() && !stateQuota->liveBytes());
        std::puts(
            "BINDLESS_RHI_ORIGINAL_BUFFER_STATES_OK exact-input-source-device committed-observations2 owner-union15 independent-limits cold-exact-less-exhausted pending-stale-ABA retained-drop-shutdown hot-allocation0 gates4-closed");
    }
};
} // namespace arp::test
