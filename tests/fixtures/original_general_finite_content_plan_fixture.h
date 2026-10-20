#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "original_general_finite_lifecycle_fixture.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/frontend/command_stream_program.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class OriginalGeneralFiniteContentPlanFixture final {
    using Root = rhi::OriginalCommandStateTransaction;
    using Contract = rhi::OriginalCompletedTextureReadPlanContract;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "General ContentPlan5: %s\n", id);
        assert(!id);
    }

public:
    // Ten-command program: one genuine completed two-mip sampled upload,
    // transition + three memory barriers + four logical shader accesses.
    // Five texture observations: one first completed parent (1+5 visits),
    // two ordinary storage observations (1 each), two duplicates (1+1 each).
    static constexpr rhi::OriginalTextureContentPlanBudget budget() noexcept
    {
        return {5u, true, 1u, 0u, 0u, 1u, 10u, 12u};
    }

    static void verifyContract()
    {
        OriginalGeneralFiniteLifecycleFixture::verifyContract();
        const auto value = budget();
        assert(!Contract::validateIntent(value));
        for (const auto version : {0u, 1u, 2u, 3u, 4u, 5u, 6u, UINT32_MAX}) {
            auto changed = value;
            changed.contractVersion = version;
            assert(bool(Contract::validateIntent(changed)) == !Contract::isReadOnly(version));
            for (const bool whole : {false, true})
                for (const bool finite : {false, true})
                    assert(bool(Contract::validateRootProfile(version, whole, finite))
                        == !((version == 4u && whole && !finite)
                            || (version == 5u && finite && !whole)));
        }
        auto changed = value;
        changed.prototypeOptIn = false;
        assert(Contract::validateIntent(changed));
        changed = value;
        changed.maxUploadCommands = 1u;
        assert(Contract::validateIntent(changed));
        changed = value;
        changed.maxContentCells = 1u;
        assert(Contract::validateIntent(changed));
        changed = value;
        changed.maxReadbackCommands = 1u;
        assert(Contract::validateIntent(changed));
        for (const auto version : {1u, 2u, 3u}) {
            rhi::OriginalCommandNativeLifecycleContract::Budget lifecycle;
            lifecycle.contractVersion = version;
            assert(
                rhi::OriginalCommandNativeLifecycleContract::validateContentProfile(lifecycle, 5u));
            assert(Contract::validatePendingProfile(version, true, true, 5u));
        }
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto root =
                rhi::semantics::OriginalCommandStateAdmissionLowering::textureTransactionLowering(
                    backend, 5u);
            const auto read = rhi::semantics::OriginalCommandStateAdmissionLowering::
                textureCompletedReadPlanLowering(backend, 5u);
            assert(root.contractVersion == 5u && !root.executionAvailable && root.diagnostic
                && root.validatorOnly == (backend == rhi::Backend::Null)
                && root.ownershipAvailable == (backend == rhi::Backend::WebGPU)
                && root.planAvailable == root.ownershipAvailable);
            assert(read.contractVersion == 5u && read.validationAvailable
                && !read.executionAvailable && read.validatorOnly == (backend == rhi::Backend::Null)
                && read.diagnostic);
            assert(
                !rhi::semantics::OriginalCommandStateAdmissionLowering::textureTransactionLowering(
                    backend, 4u)
                    .ownershipAvailable);
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_CONTENT_PLAN5_CONTRACT_OK exact-finite-root readonly-completed legacy-isolated four-backends native-execution-closed");
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    static auto verifyActual(const std::shared_ptr<const rhi::OriginalCommandResourceStates>& graph,
        const rhi::GpuDevice& device,
        rhi::OriginalCommandStateAdmissionBudget state,
        const rhi::OriginalCommandStateSequenceBudget& sequence,
        const std::shared_ptr<core::CpuMemoryQuota>& recording)
    {
        const char* id = nullptr;
        const auto& sources =
            graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
        state.maxRetainedOwners = graph->retainedOwnerCount() + 2u; // root + completed content
        assert(graph->stateContractVersion() == 5u && graph->textureEntries().size() == 5u
            && sequence.contractVersion == 5u && sequence.maxStateCells == 9u
            && sequence.maxSteps == 10u);
        assert(!Root::prepareOriginalOwnership(graph, device, state, recording, id)
            && core::StringView(id)
                == "RHI-ORIGINAL-FINITE-TRANSACTION-CONTENT-PROFILE-UNIMPLEMENTED");
        const auto make = [&] {
            return Root::prepareOriginalOwnership(
                graph, device, state, sequence, budget(), recording, id);
        };
        const auto reject = [&](const auto& intent, const auto& timeline, const auto& content) {
            assert(!Root::prepareOriginalOwnership(
                       graph, device, intent, timeline, content, recording, id)
                && id && !recording->liveBytes() && !sources->originalStateTransactionCount());
            require(graph->validate()); // failed plan enlisted no original control
        };
        for (std::uint32_t field = 0u; field < 10u; ++field) {
            auto content = budget();
            switch (field) {
            case 0u:
                content.contractVersion = 4u;
                break;
            case 1u:
                content.prototypeOptIn = false;
                break;
            case 2u:
                --content.maxContentControls;
                break;
            case 3u:
                --content.maxContentSteps;
                break;
            case 4u:
                --content.maxCommandVisits;
                break;
            case 5u:
                --content.maxProjectionVisits;
                break;
            case 6u:
                content.maxUploadCommands = 1u;
                break;
            case 7u:
                content.maxContentCells = 1u;
                break;
            case 8u:
                content.maxReadbackCommands = 1u;
                break;
            case 9u:
                content.contractVersion = 6u;
                break;
            }
            reject(state, sequence, content);
        }
        auto ownerLess = state;
        --ownerLess.maxRetainedOwners;
        reject(ownerLess, sequence, budget());
        for (const auto member : {&rhi::OriginalCommandStateSequenceBudget::maxStateCells,
                 &rhi::OriginalCommandStateSequenceBudget::maxSteps,
                 &rhi::OriginalCommandStateSequenceBudget::maxImplicitAccesses}) {
            auto less = sequence;
            --(less.*member);
            reject(state, less, budget());
        }
        auto legacy = sequence;
        legacy.contractVersion = 3u;
        reject(state, legacy, budget());
        CpuQuotaFactoryFixture pressure(recording);
        const auto exactBytes = pressure.measureSingleUse([] {}, make);
        pressure.verify(exactBytes, make);
        {
            core::CpuMemoryQuotaLease full(recording, recording->limitBytes());
            assert(full.charged() && !make() && id);
        }
        assert(!sources->originalStateTransactionCount());
        OriginalGeneralFiniteLifecycleFixture::verifyActual(make, device, recording);
        auto root = make();
        require(id);
        assert(root);
        assert(root->hasOriginalPrototypePlan()
            && root->hasOriginalFiniteGeneralShaderResourceProfile()
            && !root->hasOriginalGeneralShaderResourceProfile()
            && root->originalContentPlanContractVersion() == 5u && root->journalEmpty()
            && root->status() == Root::Status::Prepared && root->plannedContentControlCount() == 1u
            && root->plannedContentUploadCount() == 0u && root->plannedAccessStepCount() == 10u
            && root->plannedFinalCellCount() == 4u && !root->nativeExecutionProven()
            && sources->originalStateTransactionCount() == 1u);
        assert(!make() && id && sources->originalStateTransactionCount() == 1u);
        auto forged = std::shared_ptr<const Root>(recording, root.get());
        assert(Root::validateOriginal(forged));
        forged.reset();
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{recording,
                    graph->originalMetadataQuota(),
                    graph->originalInputs()->originalMetadataQuota(),
                    sources->originalMetadataQuota()},
                [&]() noexcept -> const char* {
                    return root->validate();
                }));
        });
        std::printf(
            "BINDLESS_RHI_GENERAL_CONTENT_PLAN5_OK commands10 sequenceCells9 finalCells4 steps10 sampled-mips2 completed-control1 upload0 visits12 rootBytes%llu exact-less-exhausted rollback-no-root-journal-count-quota hot0-query0 native-closed\n",
            static_cast<unsigned long long>(exactBytes));
        return root;
    }
#endif
};
} // namespace arp::test
