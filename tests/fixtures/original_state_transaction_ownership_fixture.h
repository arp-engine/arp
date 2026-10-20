#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_state.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalStateTransactionOwnershipFixture final {
    using Root = rhi::OriginalCommandStateTransaction;
    using Graph = rhi::OriginalCommandResourceStates;
    static constexpr std::uint64_t bytes = 65536u;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    // Shared canonical single-Buffer graph recipe for exact terminal ownership
    // tests. It issues no native command, native completion or state mutation.
    static std::shared_ptr<Root> prepareBufferOnlyOriginal(rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& inputQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& stateQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& recordingQuota)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        using Inputs = rhi::OriginalCommandInputs;
        using Scalars = rhi::OriginalCommandScalars;
        using Store = rhi::CommandVariableArguments;
        using Scalar = rhi::CommandScalarArguments;
        using K = rhi::CommandUsageKind;
        const char* diagnostic = nullptr;
        rhi::BufferDesc desc;
        desc.sizeBytes = 64u;
        desc.initialState = rhi::ResourceState::CopySrc;
        desc.usage = rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst;
        auto buffer = device.createBuffer(desc);
        assert(buffer);
        auto lease = rhi::OriginalCommandBufferLease::prepareOriginal(
            buffer, {1u, true, 2u, bytes}, sourceQuota, diagnostic);
        assert(lease);
        rhi::OriginalCommandAdmissionBudget budget;
        budget.contractVersion = 1u;
        budget.prototypeOptIn = true;
        budget.maxCommands = 2u;
        budget.maxReferences = 1u;
        budget.maxGraphOwners = 7u;
        budget.maxStateObservations = 1u;
        budget.maxStateTransactions = 1u;
        budget.maxNativePayloadObjects = 1u;
        budget.maxInputDescriptors = 1u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] = 1u;
        budget.maxCpuBytes[0] = budget.maxCpuBytes[1] = budget.maxCpuBytes[2] = bytes;
        const Sources::Source original = lease;
        auto sources = Sources::prepareOriginal(&original, 1u, budget, sourceQuota, diagnostic);
        assert(sources);
        const std::array<rhi::CommandUsage, 2> commands{
            {{K::BufferBarrier, 0u, 1u}, {K::Finish, 1u, 0u}}};
        const Usage::Reference reference{0u};
        auto usage = Usage::prepareOriginal(
            sources, commands.data(), commands.size(), &reference, 1u, inputQuota, diagnostic);
        assert(usage);
        const std::array<Store::Input, 2> fields{
            {{*Scalar::capture(K::BufferBarrier, Scalar::None{})},
                {*Scalar::capture(K::Finish, Scalar::None{})}}};
        auto store = Store::prepareOriginal(fields.data(),
            fields.size(),
            {1u, true, 2u, 0u, 0u, 0u, bytes},
            inputQuota,
            diagnostic);
        assert(store);
        auto scalars = Scalars::prepareOriginal(usage, store, inputQuota, diagnostic);
        assert(scalars);
        rhi::ResourceBarrierDesc barrier{
            lease->resource(), {}, rhi::ResourceState::CopySrc, rhi::ResourceState::CopyDst};
        barrier.sourceStages = barrier.destinationStages = rhi::PipelineStage::Transfer;
        barrier.sourceAccess = rhi::ResourceAccess::TransferRead;
        barrier.destinationAccess = rhi::ResourceAccess::TransferWrite;
        const Inputs::OriginalDescriptor descriptor{
            0u, *rhi::ResourceDependencyArguments::capture(K::BufferBarrier, barrier)};
        auto inputs = Inputs::prepareOriginal(scalars, &descriptor, 1u, inputQuota, diagnostic);
        assert(inputs && inputs->retainedOwnerCount() == 7u);
        auto graph = Graph::prepareOriginal(
            inputs, device, {1u, true, 1u, 1u, 1u, 0u, 12u, bytes}, stateQuota, diagnostic);
        assert(graph);
        auto root = Root::prepareOriginalOwnership(
            graph, device, {1u, true, 1u, 1u, 1u, 0u, 13u, bytes}, recordingQuota, diagnostic);
        assert(root && root->retainedOwnerCount() == 13u && root->originalControlCount() == 1u);
        const auto sequence = root->preflightOriginalStateSequence({1u, true, 2u, 0u, 1u, 1u});
        assert(!sequence.diagnostic && sequence.stateCells == 1u && sequence.steps == 1u);
        rhi::ResourceState final;
        assert(!rhi::common::OriginalCommandStateSequence::resolveOriginalBuffer(
                   root->originalGraph(), lease->resource(), 2u, final)
            && final == rhi::ResourceState::CopyDst && !root->originalGraph()->validate());
        return root;
    }

    template <class Factory, class Transition>
    static void verify(std::shared_ptr<const Graph>& graph,
        rhi::GpuDevice& device,
        Factory&& rebuild,
        Transition&& transition,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 5>& originalQuotas)
    {
        auto recording = quota();
        const char* diagnostic = nullptr;
        rhi::OriginalCommandStateAdmissionBudget budget{1u, true, 1u, 4u, 6u, 5u, 25u, bytes};
        const auto source =
            graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
        const auto make = [&] {
            return Root::prepareOriginalOwnership(graph, device, budget, recording, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(recording);
        const auto peak = pressure.measure(make);
        pressure.verify(peak, make);
        assert(source->originalStateTransactionCount() == 0u && !recording->liveBytes());
        {
            core::CpuMemoryQuotaLease full(recording, bytes);
            assert(full.charged() && !make());
            is(diagnostic, "RHI-ORIGINAL-TRANSACTION-ALLOCATION");
        }
        assert(source->originalStateTransactionCount() == 0u && !graph->validate());
        const auto original = budget;
        --budget.maxResourceRegistrations;
        assert(!make());
        is(diagnostic, "RHI-ORIGINAL-STATE-REGISTRATION-CAPACITY");
        budget = original;
        --budget.maxStateObservations;
        assert(!make());
        is(diagnostic, "RHI-ORIGINAL-TRANSACTION-OBSERVATION-CAPACITY");
        budget = original;
        --budget.maxTextureRanges;
        assert(!make());
        is(diagnostic, "RHI-ORIGINAL-TRANSACTION-RANGE-CAPACITY");
        budget = original;
        --budget.maxRetainedOwners;
        assert(!make());
        is(diagnostic, "RHI-ORIGINAL-STATE-OWNER-CAPACITY");
        budget = original;
        for (const auto& other : {source->originalMetadataQuota(),
                 graph->originalInputs()->originalMetadataQuota(),
                 graph->originalMetadataQuota()}) {
            assert(!Root::prepareOriginalOwnership(graph, device, budget, other, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-TRANSACTION-QUOTA-DOMAIN");
        }
        auto root = make();
        assert(root && root->retainedOwnerCount() == 25u && root->originalControlCount() == 3u
            && root->journalEmpty());
        assert(source->originalStateTransactionCount() == 1u && !make());
        is(diagnostic, "RHI-ORIGINAL-TRANSACTION-COUNT-CAPACITY");
        auto equalGraph = rebuild();
        assert(equalGraph);
        assert(!Root::prepareOriginalOwnership(equalGraph, device, budget, recording, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-TRANSACTION-COUNT-CAPACITY");
        is(root->compareOriginalGraph(equalGraph, device), "RHI-ORIGINAL-TRANSACTION-GRAPH");
        equalGraph.reset();
        auto forged = std::shared_ptr<const Root>(recording, root.get());
        is(Root::validateOriginal(forged), "RHI-ORIGINAL-TRANSACTION-OWNER");
        forged.reset();
        const auto& textureEntry = graph->textureEntries()[0];
        const auto* texture = rhi::common::OriginalCommandTextureRanges::originalTexture(
            source->sources()[textureEntry.point.sourceIndex]);
        assert(texture);
        const auto baseline = texture->subresourceState(textureEntry.point.range);
        recording->setLimitBytes(bytes - 1u);
        is(root->validate(), "RHI-ORIGINAL-TRANSACTION-QUOTA");
        recording->setLimitBytes(bytes);
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 6> quotas{originalQuotas[0],
            originalQuotas[1],
            originalQuotas[2],
            originalQuotas[3],
            originalQuotas[4],
            recording};
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
            const auto sequence =
                root->preflightOriginalStateSequence({1u, true, 8u, 0u, 21u, 21u});
            if (!sequence.diagnostic || sequence.commandIndex != 1u
                || std::string_view(sequence.diagnostic)
                    != "RHI-ORIGINAL-SEQUENCE-TEXTURE-ACCESS-STATE")
                return "TEST-SEQUENCE-INVALID-OWNERSHIP-FIXTURE";
            if (const auto* id = root->beginOriginalPendingOwnership())
                return id;
            if (root->journalEmpty() || root->status() != Root::Status::Pending
                || !graph->validate())
                return "TEST-TRANSACTION-SNAPSHOT";
            if (std::string_view(
                    root->preflightOriginalStateSequence({1u, true, 8u, 0u, 21u, 21u}).diagnostic)
                != "RHI-ORIGINAL-TRANSACTION-STATUS")
                return "TEST-SEQUENCE-PENDING";
            if (const auto* id = root->validate())
                return id;
            if (const auto* id = root->compareOriginalGraph(graph, device))
                return id;
            if (!root->beginOriginalPendingOwnership())
                return "TEST-TRANSACTION-REBEGIN";
            for (const auto backend : {rhi::Backend::Null,
                     rhi::Backend::Vulkan,
                     rhi::Backend::Metal,
                     rhi::Backend::WebGPU})
                if (root->nativeExecutionProven() || !root->executionDiagnostic(backend))
                    return "TEST-TRANSACTION-EXECUTION";
            return root->commitOriginalPendingOwnership();
        }));
        assert(root->status() == Root::Status::Committed && root->journalEmpty()
            && !graph->validate()
            && texture->subresourceState(textureEntry.point.range) == baseline);
        assert(!make());
        is(diagnostic, "RHI-ORIGINAL-TRANSACTION-COUNT-CAPACITY");
        root.reset();
        assert(source->originalStateTransactionCount() == 0u);
        root = make();
        assert(root && !root->beginOriginalPendingOwnership());
        core::MutationJournal foreign;
        const auto after = baseline == rhi::ResourceState::CopySrc ? rhi::ResourceState::CopyDst
                                                                   : rhi::ResourceState::CopySrc;
        assert(!transition(*const_cast<rhi::GpuTexture*>(texture),
                   textureEntry.point.range,
                   baseline,
                   after,
                   &foreign)
                   .success
            && foreign.empty());
        assert(!transition(*const_cast<rhi::GpuTexture*>(texture),
            textureEntry.point.range,
            baseline,
            after,
            nullptr)
                .success);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
            root->abandonOriginalPendingOwnership();
            return root->journalEmpty() && root->status() == Root::Status::Abandoned
                    && root->validate() && graph->validate()
                    && texture->subresourceState(textureEntry.point.range) == baseline
                ? nullptr
                : "TEST-TRANSACTION-ABANDON";
        }));
        assert(source->originalStateTransactionCount() == 1u);
        root.reset();
        assert(source->originalStateTransactionCount() == 0u && !recording->liveBytes());
        graph = rebuild();
        assert(graph);
        root = make();
        assert(root && !root->beginOriginalPendingOwnership());
        std::weak_ptr<const Graph> retained = graph;
        graph.reset();
        assert(!retained.expired() && !root->validate());
        root.reset();
        assert(retained.expired() && source->originalStateTransactionCount() == 0u
            && !recording->liveBytes());
        graph = rebuild();
        assert(graph && !graph->validate());
        std::printf(
            "BINDLESS_RHI_ORIGINAL_TRANSACTION_OWNERSHIP_OK %llu canonical-input-state-graph control3 observations6 owner25 embedded-journal source-count1 shared-graph-bound exact-less-exhausted foreign-writer-reject no-write-commit abandon caller-drop destructor hot-allocation0 gates4-closed\n",
            static_cast<unsigned long long>(peak));
    }
};
} // namespace arp::test
