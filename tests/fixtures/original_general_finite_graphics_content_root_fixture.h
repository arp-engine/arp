#pragma once
#include "original_general_finite_graphics_content_contract_fixture.h"
#include "original_general_finite_graphics_usage_fixture.h"
#include "original_general_finite_physical_loss_fixture.h"
#include "original_general_finite_submission_terminal_fixture.h"

namespace arp::test {
class OriginalGeneralFiniteGraphicsContentRootFixture final {
    using Root = rhi::OriginalCommandStateTransaction;
    using Usage = rhi::common::OriginalTypedUsageProjection;
    using Dependencies = rhi::common::OriginalTypedDependencyProjection;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Graphics ContentPlan6 root: %s\n", id);
        assert(!id);
    }

public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    template <class Contents>
    static void verifyActual(OriginalGeneralFiniteGraphicsStateStreamFixture::Retained& retained,
        const rhi::GpuDevice& device,
        Contents& contents)
    {
        verifyActual(retained, device, contents, [](auto&, const auto&) {});
    }

    template <class Contents, class Prepared>
    static void verifyActual(OriginalGeneralFiniteGraphicsStateStreamFixture::Retained& retained,
        const rhi::GpuDevice& device,
        Contents& contents,
        Prepared&& inspectPrepared)
    {
        verifyActual(
            retained, device, contents, std::forward<Prepared>(inspectPrepared), [](const auto&) {
            });
    }

    template <class Contents, class Prepared, class Pending>
    static void verifyActual(OriginalGeneralFiniteGraphicsStateStreamFixture::Retained& retained,
        const rhi::GpuDevice& device,
        Contents& contents,
        Prepared&& inspectPrepared,
        Pending&& inspectPending)
    {
        verifyActual(
            retained,
            device,
            contents,
            std::forward<Prepared>(inspectPrepared),
            std::forward<Pending>(inspectPending),
            [](auto&, const auto&) {},
            false);
    }

    template <class Contents, class Prepared, class Pending, class ColdCandidate>
    static void verifyActual(OriginalGeneralFiniteGraphicsStateStreamFixture::Retained& retained,
        const rhi::GpuDevice& device,
        Contents& contents,
        Prepared&& inspectPrepared,
        Pending&& inspectPending,
        ColdCandidate&& inspectColdCandidate,
        bool coldCandidate)
    {
        // Legacy callbacks keep their existing two-argument contract.
        verifyActualWithRebuild(
            retained,
            device,
            contents,
            std::forward<Prepared>(inspectPrepared),
            std::forward<Pending>(inspectPending),
            [&](auto& root, const auto& stream, auto&&) {
                inspectColdCandidate(root, stream);
            },
            coldCandidate);
    }

    template <class Contents, class Prepared, class Pending, class ColdCandidate>
    static void verifyActualWithRebuild(
        OriginalGeneralFiniteGraphicsStateStreamFixture::Retained& retained,
        const rhi::GpuDevice& device,
        Contents& contents,
        Prepared&& inspectPrepared,
        Pending&& inspectPending,
        ColdCandidate&& inspectColdCandidate,
        bool coldCandidate,
        bool recordingCandidate = false)
    {
        assert(!recordingCandidate || coldCandidate);
        auto graph = retained.graph;
        auto stream = retained.program;
        const auto inputs = graph->originalInputs();
        const auto& source = inputs->originalScalars()->originalUsage()->originalSources();
        const auto quota = core::makeShared<core::CpuMemoryQuota>(
            source->originalBudget().maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Recording)]);
        auto state = OriginalGeneralFiniteGraphicsStateStreamFixture::stateBudget(true, true);
        auto graphBudget = state;
        graphBudget.maxRetainedOwners = graph->retainedOwnerCount();
        std::uint32_t graphOwners = 0u, registrations = 0u;
        const auto* registrationId =
            rhi::OriginalCommandResourceSources::registeredOwnerUnion(source,
                inputs->retainedOwnerCount() + 1u,
                graphBudget.maxRetainedOwners,
                graphBudget.maxResourceRegistrations,
                graphOwners,
                registrations);
        require(registrationId);
        assert(graphOwners == graphBudget.maxRetainedOwners);
        graphBudget.maxResourceRegistrations = registrations;
        const auto stateQuota = graph->originalMetadataQuota(),
                   streamQuota = stream->originalMetadataQuota();
        const auto streamBudget = stream->originalBudget();
        state.maxRetainedOwners =
            graph->retainedOwnerCount() + 4u; // root, retained Stream, two distinct controls
        // This separately selected workload authors one completed two-mip control
        // and one attachment cell. Three command traversals cover 14 commands;
        // the bounded projection ceiling includes owner scans and cell timeline.
        rhi::OriginalFiniteGraphicsContentPlanBudget content{
            6u, true, 1u, 1u, 1u, 2u, 2u, 42u, 256u};
        const auto sequence = stream->originalBudget().stateSequence;
        auto dependency = OriginalGeneralFiniteGraphicsUsageFixture::dependencyBudget(true, true);
        const char* id = nullptr;
        const auto make = [&] {
            return Root::prepareOriginalGraphicsOwnership<Usage, Dependencies>(
                graph, stream, device, state, sequence, content, dependency, quota, id);
        };
        const auto checkUnchanged = [&] {
            require(graph->validate());
            contents.verifyCapturedContent();
            assert(!quota->liveBytes() && !source->originalStateTransactionCount());
        };
        // Both terminal paths reuse the original graph/stream factory and quotas.
        const auto recaptureGraph = [&] {
            stream.reset();
            retained.program.reset();
            graph.reset();
            retained.graph.reset();
            assert(!stateQuota->liveBytes() && !streamQuota->liveBytes() && !quota->liveBytes()
                && !source->originalStateTransactionCount());
            graph = rhi::OriginalCommandResourceStates::prepareOriginal(
                inputs, device, graphBudget, stateQuota, id);
            require(id);
            assert(graph);
            stream = rhi::OriginalCommandStreamProgram::prepareOriginalTyped(
                graph, streamBudget, streamQuota, id);
            require(id);
            assert(stream);
            retained.graph = graph;
            retained.program = stream;
            checkUnchanged();
        };
        const auto recaptureAfterRollback = [&] {
            assert(graph->validate() && stream->validate());
            contents.verifyRollbackContent();
            recaptureGraph();
        };
        const auto recaptureUnchangedCandidate = [&] {
            require(graph->validate());
            require(stream->validate());
            contents.verifyCapturedContent();
            recaptureGraph();
        };
        auto measured = make();
        require(id);
        assert(measured && measured->originalMixedContentCounts());
        const auto counts = *measured->originalMixedContentCounts();
        assert(counts.completedControls == 1u && counts.attachmentControls == 1u
            && counts.attachmentCells == 1u && counts.renderBindingReads == 1u
            && counts.drawReads == 1u && counts.attachmentSteps == 2u && counts.commandVisits == 42u
            && counts.projectionVisits <= content.maxProjectionVisits);
        content.maxProjectionVisits = counts.projectionVisits;
        measured.reset();
        checkUnchanged();
        const auto reject = [&] {
            assert(!make() && id);
            checkUnchanged();
        };
        for (const auto member :
            {&rhi::OriginalFiniteGraphicsContentPlanBudget::maxCompletedControls,
                &rhi::OriginalFiniteGraphicsContentPlanBudget::maxAttachmentControls,
                &rhi::OriginalFiniteGraphicsContentPlanBudget::maxAttachmentCells,
                &rhi::OriginalFiniteGraphicsContentPlanBudget::maxSampledSteps,
                &rhi::OriginalFiniteGraphicsContentPlanBudget::maxAttachmentSteps,
                &rhi::OriginalFiniteGraphicsContentPlanBudget::maxCommandVisits,
                &rhi::OriginalFiniteGraphicsContentPlanBudget::maxProjectionVisits}) {
            --(content.*member);
            reject();
            ++(content.*member);
        }
        --state.maxRetainedOwners;
        reject();
        ++state.maxRetainedOwners;
        --dependency.maxDependencies;
        reject();
        ++dependency.maxDependencies;
        content.prototypeOptIn = false;
        reject();
        content.prototypeOptIn = true;
        for (const auto& alias : std::array{source->originalMetadataQuota(),
                 inputs->originalMetadataQuota(),
                 graph->originalMetadataQuota(),
                 stream->originalMetadataQuota()}) {
            assert((!Root::prepareOriginalGraphicsOwnership<Usage, Dependencies>(
                        graph, stream, device, state, sequence, content, dependency, alias, id)
                && id));
            checkUnchanged();
        }
        auto forged = std::shared_ptr<const rhi::OriginalCommandStreamProgram>(quota, stream.get());
        assert((!Root::prepareOriginalGraphicsOwnership<Usage, Dependencies>(
                    graph, forged, device, state, sequence, content, dependency, quota, id)
            && id));
        forged.reset();
        checkUnchanged();
        CpuQuotaFactoryFixture pressure(quota);
        const auto exactBytes = pressure.measureSingleUse([] {}, make);
        pressure.verify(exactBytes, make);
        {
            core::CpuMemoryQuotaLease full(quota, quota->limitBytes());
            assert(full.charged() && !make() && id);
        }
        checkUnchanged();
        const auto verifyGenerationExhausted = [&] {
            auto exhausted = make();
            require(id);
            assert(exhausted);
            const auto* exhaustion = exhausted->applyOriginalPrototypePlan();
            assert(exhaustion
                && core::StringView(exhaustion) == "RHI-CANONICAL-CONTENT-GENERATION-CAPACITY"
                && exhausted->status() == Root::Status::Prepared && exhausted->journalEmpty());
            exhausted.reset();
        };
        if (coldCandidate) {
            // A separate candidate is sealed and retired before any proposal.
            // Its same root/stream remains authenticated throughout Cold8; the
            // ordinary CPU baseline below must never apply this sealed root.
            auto candidate = make();
            require(id);
            assert(candidate);
            const auto rebuildOriginalCandidate = [&] {
                candidate.reset();
                recaptureUnchangedCandidate();
                candidate = make();
                require(id);
                assert(candidate);
                assert(candidate->status() == Root::Status::Prepared && candidate->journalEmpty());
            };
            inspectColdCandidate(candidate, stream, rebuildOriginalCandidate);
            if (recordingCandidate && EM_ASM_INT({
                    // clang-format off
                    return new URLSearchParams(location.search).has("general-graphics-native-submission");
                    // clang-format on
                })) {
                assert(candidate && candidate->journalEmpty());
                const bool physicalLoss =
                    OriginalGeneralFinitePhysicalLossFixture::selectedTerminal().has_value();
                if (physicalLoss
                    || OriginalGeneralFiniteSubmissionTerminalFixture::invalidates(
                        OriginalGeneralFiniteSubmissionTerminalFixture::selectedTerminal())) {
                    assert(candidate->status() == Root::Status::Invalidated);
                    contents.verifyFailedContent(physicalLoss
                        || OriginalGeneralFiniteSubmissionTerminalFixture::lifetimeTerminates(
                            OriginalGeneralFiniteSubmissionTerminalFixture::selectedTerminal()));
                    candidate.reset();
                    assert(!quota->liveBytes() && !source->originalStateTransactionCount());
                    assert(graph->validate() && stream->validate());
                    std::puts(
                        "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_SUBMISSION_FAILED_RETIRE_OK original-root-invalidated transaction0 quota0 no-replay no-readback");
                    return;
                }
                assert(candidate->status() == Root::Status::Committed);
                contents.verifyCommittedContent();
                candidate.reset();
                assert(!quota->liveBytes() && !source->originalStateTransactionCount());
                std::puts(EM_ASM_INT({
                    // clang-format off
                        return new URLSearchParams(location.search).has("general-graphics-output-readback");
                    // clang-format on
                })
                        ? "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_SUBMISSION_RETIRE_OK original-root-committed transaction0 quota0 no-replay readback3"
                        : "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_SUBMISSION_RETIRE_OK original-root-committed transaction0 quota0 no-replay no-readback");
                return;
            }
            assert(candidate && candidate->status() == Root::Status::Abandoned
                && candidate->journalEmpty());
            if (recordingCandidate && EM_ASM_INT({
                    // clang-format off
                    const value = new URLSearchParams(location.search).get("general-graphics-recording-terminal");
                    return value === "retired" || value === "shutdown";
                    // clang-format on
                })) {
                contents.verifyFailedContent(true);
                candidate.reset();
                assert(!quota->liveBytes() && !source->originalStateTransactionCount());
                assert(graph->validate() && stream->validate());
                std::puts(
                    "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_RECORDING_LIFETIME_RETIRE_OK original-root-Abandoned transaction0 quota0 no-replay no-submit no-readback");
                return;
            }
            if (recordingCandidate) {
                candidate.reset();
                recaptureAfterRollback();
                verifyGenerationExhausted();
                checkUnchanged();
                std::puts(
                    "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_RECORDING_ROLLBACK_OK stale-observations-refused original-Inputs-recaptured generation-one-exhausted transaction0 quota0 no-submit-or-completion");
                return;
            }
            candidate.reset();
            checkUnchanged();
            std::puts(
                "BINDLESS_RHI_GENERAL_GRAPHICS_COLD8_CANDIDATE_RETIRE_OK before-any-proposal original-factory transaction0 quota0 generation-unchanged CPU-baseline-separate");
        }
        auto root = make();
        require(id);
        assert(root);
        assert(root->originalContentPlanContractVersion() == 6u
            && root->plannedContentControlCount() == 2u && root->plannedContentUploadCount() == 0u
            && root->plannedFinalCellCount() == 5u && root->plannedAccessStepCount() == 17u
            && root->journalEmpty() && root->status() == Root::Status::Prepared
            && !root->nativeExecutionProven()
            && root->retainedOwnerCount() == state.maxRetainedOwners);
        assert(!make() && id && source->originalStateTransactionCount() == 1u);
        std::weak_ptr<const rhi::OriginalCommandStreamProgram> streamObserver = stream;
        std::weak_ptr<const rhi::OriginalCommandResourceStates> graphObserver = graph;
        retained.program.reset();
        stream.reset();
        retained.graph.reset();
        graph.reset();
        assert(!streamObserver.expired() && !graphObserver.expired());
        require(root->validate());
        retained.graph = graphObserver.lock();
        graph = retained.graph;
        retained.program = streamObserver.lock();
        stream = retained.program;
        streamObserver.reset();
        graphObserver.reset();
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{quota, graph->originalMetadataQuota(), stream->originalMetadataQuota()},
                [&]() noexcept -> const char* {
                    return root->validate();
                }));
        });
        inspectPrepared(root, stream);
        require(root->validate());
        assert(root->journalEmpty() && root->status() == Root::Status::Prepared);
        require(root->applyOriginalPrototypePlan());
        assert(root->status() == Root::Status::Pending && !root->journalEmpty());
        inspectPending(root);
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{quota, graph->originalMetadataQuota(), stream->originalMetadataQuota()},
                [&]() noexcept -> const char* {
                    return root->validate();
                }));
        });
        contents.verifyPendingContent();
        assert(root->commitOriginalPendingOwnership()); // no native completion authority
        const bool drop = EM_ASM_INT({
            // clang-format off
                return new URLSearchParams(location.search).get("general-graphics-root-terminal") === "drop";
            // clang-format on
        });
        if (!drop) {
            require(root->abandonOriginalPendingOwnership());
            assert(root->status() == Root::Status::Abandoned && root->journalEmpty());
        }
        root.reset();
        recaptureAfterRollback();
        // The single authored generation is consumed by proposal even when
        // rolled back. Never increase that ceiling to run another mutation.
        verifyGenerationExhausted();
        checkUnchanged();
        std::printf(
            "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_ROLLBACK_OK %s stale-observations-refused original-Inputs-recaptured sampled-stamp-current attachment-uninitialized generation-one-exhausted\n",
            drop ? "drop" : "abandon");
        std::printf(
            "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_ROOT_OK completed1 attachment1 cells1 bind1 draw1 attachmentSteps2 commands42 projections%llu finalCells5 stateSteps17 rootBytes%llu exact-less-exhausted same-original-Stream-Usage-Dependency pending-rollback content-unchanged hot0-query0 native-closed\n",
            static_cast<unsigned long long>(counts.projectionVisits),
            static_cast<unsigned long long>(exactBytes));
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_ROOT_RETIRE_OK original-journal-rollback transaction-count0 quota0 no-native-draw-or-completion");
    }
#endif
};
} // namespace arp::test
