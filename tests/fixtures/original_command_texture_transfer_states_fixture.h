#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_state.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandTextureTransferStatesFixture final {
public:
    static void verify(const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        rhi::GpuDevice& device,
        std::uint32_t buffers,
        std::uint32_t registrations,
        std::uint32_t owners,
        const rhi::TextureSubresourceRange& expected,
        const char* label)
    {
        using Graph = rhi::OriginalCommandResourceStates;
        constexpr std::uint64_t bytes = 65536u;
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const char* diagnostic = nullptr;
        const rhi::OriginalCommandStateAdmissionBudget budget{
            1u, true, 1u, registrations, buffers + 2u, 2u, owners, bytes};
        const auto factory = [&] {
            return Graph::prepareOriginal(inputs, device, budget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            auto graph = factory();
            assert(graph && !graph->validate() && graph->retainedOwnerCount() == owners
                && graph->bufferEntries().size() == buffers
                && graph->textureEntries().size() == 2u);
            for (std::size_t i = 0u; i < 2u; ++i) {
                const auto& entry = graph->textureEntries()[i];
                assert(entry.point.commandIndex == i + 1u && entry.point.referenceIndex == i * 2u
                    && entry.point.sourceIndex == 0u && entry.observation.range() == expected);
            }
            if (std::string_view(label) == "upload") {
                const auto accepted = rhi::common::OriginalCommandAccessValidation::inspect(
                    graph, {1u, true, 5u, 0u, 4u, 4u});
                assert(!accepted.diagnostic && accepted.stateCells == 4u && accepted.steps == 4u);
                std::puts(
                    "BINDLESS_RHI_ORIGINAL_ACCESS_UPLOAD_OK original-destination copied-pixels full-mip-packing cells4 steps4 no-mutation gates4-closed");
            }
            if (std::string_view(label) == "readback") {
                // Two explicit two-mip reads plus the original registered
                // destination's two write accesses; no fabricated destination.
                using Sequence = rhi::common::OriginalCommandStateSequence;
                rhi::OriginalCommandStateSequenceBudget sequenceBudget{1u, true, 5u, 0u, 5u, 6u};
                const auto complete =
                    rhi::common::OriginalCommandAccessValidation::inspect(graph, sequenceBudget);
                assert(!complete.diagnostic);
                const auto accepted = Sequence::inspect(graph, sequenceBudget);
                assert(!accepted.diagnostic && accepted.stateCells == 5u && accepted.steps == 6u);
                --sequenceBudget.maxSteps;
                assert(std::string_view(Sequence::inspect(graph, sequenceBudget).diagnostic)
                    == "RHI-ORIGINAL-SEQUENCE-STEP-CAPACITY");
                ++sequenceBudget.maxSteps;
                verifyReadbackDestinationTransition(inputs, device, budget, quota);
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{quota,
                        inputs->originalMetadataQuota(),
                        inputs->originalScalars()
                            ->originalUsage()
                            ->originalSources()
                            ->originalMetadataQuota()},
                    [&]() noexcept {
                        return Sequence::inspect(graph, sequenceBudget).diagnostic;
                    }));
                std::puts(
                    "BINDLESS_RHI_ORIGINAL_ACCESS_READBACK_OK exact-source-destination full-mip-layout-binding cells5 steps6 original-committed-graph no-mutation gates4-closed");
                std::puts(
                    "BINDLESS_RHI_ORIGINAL_STATE_SEQUENCE_READBACK_OK original-registered-destination cells5 steps6 source-CopySrc destination-CopyDst hidden-write-step-bound no-mutation hot-allocation0 gates4-closed");
            }
            auto recording = core::makeShared<core::CpuMemoryQuota>(bytes);
            auto planBudget = budget;
            ++planBudget.maxRetainedOwners;
            auto root = rhi::OriginalCommandStateTransaction::prepareOriginalOwnership(
                graph, device, planBudget, recording, diagnostic);
            assert(root);
            const bool upload = std::string_view(label) == "upload";
            assert(!root->materializeOriginalPrototypePlan(
                {1u, true, 5u, 0u, upload ? 4u : 5u, upload ? 4u : 6u}));
            assert(root->plannedFinalCellCount() == (upload ? 2u : 3u)
                && !root->plannedTransitionCount());
            assert(
                !CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota,
                                                                   inputs->originalMetadataQuota(),
                                                                   inputs->originalScalars()
                                                                       ->originalUsage()
                                                                       ->originalSources()
                                                                       ->originalMetadataQuota(),
                                                                   recording},
                    [&]() noexcept {
                        if (const auto* id = root->applyOriginalPrototypePlan())
                            return id;
                        if (const auto* id = root->commitOriginalPendingOwnership())
                            return id;
                        return root->validate();
                    }));
            std::printf(
                "BINDLESS_RHI_ORIGINAL_PLAN_TRANSFER_OK %s unique-cells%zu original-source-destination hidden-write-steps exact-mips sealed-finals commit hot-allocation0 gates4-closed\n",
                label,
                root->plannedFinalCellCount());
            root.reset();
            assert(
                !CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota,
                                                                   inputs->originalMetadataQuota(),
                                                                   inputs->originalScalars()
                                                                       ->originalUsage()
                                                                       ->originalSources()
                                                                       ->originalMetadataQuota()},
                    [&]() noexcept {
                        if (const auto* id = Graph::validateOriginal(graph))
                            return id;
                        return graph->compareOriginalInputs(inputs, device);
                    }));
        }
        assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_ORIGINAL_TEXTURE_TRANSFER_STATES_OK %s ranges2 buffers%u registrations%u owner-union%u exact-original-input-source cold-exact-less hot-allocation0 gates4-closed\n",
            label,
            buffers,
            registrations,
            owners);
    }

private:
    static void verifyReadbackDestinationTransition(
        const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        rhi::GpuDevice& device,
        const rhi::OriginalCommandStateAdmissionBudget& stateBudget,
        const std::shared_ptr<core::CpuMemoryQuota>& stateQuota)
    {
        using K = rhi::CommandUsageKind;
        using S = rhi::ResourceState;
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        using Inputs = rhi::OriginalCommandInputs;
        using Store = rhi::CommandVariableArguments;
        using Scalar = rhi::CommandScalarArguments;
        const char* diagnostic = nullptr;
        const auto& original = inputs->originalScalars()->originalUsage()->originalSources();
        const auto& retained = original->sources();
        const auto quota = inputs->originalMetadataQuota();
        auto budget = original->originalBudget();
        budget.maxCommands = 6u;
        budget.maxReferences = 5u;
        budget.maxInputDescriptors = 3u;
        auto sources = Sources::prepareOriginal(retained.data(),
            retained.size(),
            budget,
            original->originalMetadataQuota(),
            diagnostic);
        assert(sources);
        const auto& destination =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureReadbackSource>>(retained[2])
                ->originalDestinationLease();
        const std::array<rhi::CommandUsage, 6> commands{{{K::BufferBarrier, 0u, 1u},
            {K::BeginTransferPass, 1u, 0u},
            {K::TextureReadback, 1u, 2u},
            {K::TextureReadback, 3u, 2u},
            {K::EndTransferPass, 5u, 0u},
            {K::Finish, 5u, 0u}}};
        const std::array<Usage::Reference, 5> references{{{1u}, {0u}, {2u}, {0u}, {2u}}};
        auto usage = Usage::prepareOriginal(sources,
            commands.data(),
            commands.size(),
            references.data(),
            references.size(),
            quota,
            diagnostic);
        assert(usage);
        const std::array<Store::Input, 6> fields{
            {{*Scalar::capture(K::BufferBarrier, Scalar::None{})},
                {*Scalar::capture(K::BeginTransferPass, Scalar::None{})},
                {*Scalar::capture(K::TextureReadback, Scalar::None{}), "read"},
                {*Scalar::capture(K::TextureReadback, Scalar::None{}), "read"},
                {*Scalar::capture(K::EndTransferPass, Scalar::None{})},
                {*Scalar::capture(K::Finish, Scalar::None{})}}};
        auto store = Store::prepareOriginal(fields.data(),
            fields.size(),
            {1u, true, 6u, 0u, 8u, 0u, quota->limitBytes()},
            quota,
            diagnostic);
        assert(store);
        auto scalars =
            rhi::OriginalCommandScalars::prepareOriginal(usage, store, quota, diagnostic);
        assert(scalars);
        rhi::ResourceBarrierDesc barrier{destination->resource(), {}, S::CopyDst, S::CopySrc};
        barrier.sourceStages = barrier.destinationStages = rhi::PipelineStage::Transfer;
        barrier.sourceAccess = rhi::ResourceAccess::TransferWrite;
        barrier.destinationAccess = rhi::ResourceAccess::TransferRead;
        const std::array<Inputs::OriginalDescriptor, 3> descriptors{
            {{0u, *rhi::ResourceDependencyArguments::capture(K::BufferBarrier, barrier)},
                {2u, inputs->descriptors()[0].arguments},
                {3u, inputs->descriptors()[1].arguments}}};
        auto changed = Inputs::prepareOriginal(
            scalars, descriptors.data(), descriptors.size(), quota, diagnostic);
        assert(changed);
        auto graph = rhi::OriginalCommandResourceStates::prepareOriginal(
            changed, device, stateBudget, stateQuota, diagnostic);
        assert(graph);
        const auto rejected =
            rhi::common::OriginalCommandStateSequence::inspect(graph, {1u, true, 6u, 0u, 5u, 7u});
        assert(rejected.diagnostic
            && std::string_view(rejected.diagnostic)
                == "RHI-ORIGINAL-SEQUENCE-READBACK-DESTINATION-STATE"
            && rejected.commandIndex == 2u && !graph->validate()
            && destination->originalObject()->state() == S::CopyDst);
    }
};
} // namespace arp::test
