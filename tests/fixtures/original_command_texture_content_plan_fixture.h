#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_state.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandTextureContentPlanFixture final {
    struct NoObservation {
        void operator()(bool) const noexcept {}
    };

public:
    template <class Observe = NoObservation>
    static void verify(const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        rhi::GpuDevice& device,
        const rhi::GpuTexture& texture,
        const std::shared_ptr<core::CpuMemoryQuota>& contentQuota,
        Observe observe = {})
    {
        using Graph = rhi::OriginalCommandResourceStates;
        using Root = rhi::OriginalCommandStateTransaction;
        using State = rhi::common::OriginalTextureContentControl::State;
        constexpr std::uint64_t bytes = 65536u;
        const char* diagnostic = nullptr;
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes),
             recordingQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const rhi::OriginalCommandStateAdmissionBudget graphBudget{
            1u, true, 1u, 1u, 2u, 2u, 15u, bytes};
        auto graph = Graph::prepareOriginal(inputs, device, graphBudget, stateQuota, diagnostic);
        assert(graph);
        auto rootBudget = graphBudget;
        rootBudget.maxRetainedOwners = 17u;
        const auto make = [&] {
            return Root::prepareOriginalOwnership(
                graph, device, rootBudget, recordingQuota, diagnostic);
        };
        auto root = make();
        assert(root);
        const auto original = texture.observeOriginalContent();
        assert(original);
        assert(original->state() == State::Uninitialized && !original->contentGeneration());
        const rhi::OriginalCommandStateSequenceBudget sequence{1u, true, 5u, 0u, 4u, 4u};
        const rhi::OriginalTextureContentPlanBudget content{1u, true, 1u, 2u};
        assert(contentQuota);
        {
            const auto baseline = contentQuota->liveBytes();
            auto aliased =
                Root::prepareOriginalOwnership(graph, device, rootBudget, contentQuota, diagnostic);
            // The fixture's Source budget permits one live root at a time.
            assert(!aliased
                && std::string_view(diagnostic) == "RHI-ORIGINAL-TRANSACTION-COUNT-CAPACITY");
            assert(contentQuota->liveBytes() == baseline);
            root.reset();
            aliased =
                Root::prepareOriginalOwnership(graph, device, rootBudget, contentQuota, diagnostic);
            assert(aliased);
            assert(std::string_view(aliased->materializeOriginalPrototypePlan(sequence, content))
                == "RHI-CANONICAL-CONTENT-PLAN-QUOTA-DOMAIN");
            assert(!aliased->hasOriginalPrototypePlan() && aliased->journalEmpty());
            aliased.reset();
            assert(contentQuota->liveBytes() == baseline);
            root = make();
            assert(root);
        }
        assert(std::string_view(root->beginOriginalPendingOwnership())
            == "RHI-CANONICAL-CONTENT-PLAN-REQUIRED");
        assert(std::string_view(root->materializeOriginalPrototypePlan(sequence))
            == "RHI-CANONICAL-CONTENT-PLAN-VERSION");
        auto bad = content;
        bad.prototypeOptIn = false;
        assert(std::string_view(root->materializeOriginalPrototypePlan(sequence, bad))
            == "RHI-CANONICAL-CONTENT-PLAN-OPT-IN");
        bad = content;
        --bad.maxUploadCommands;
        assert(std::string_view(root->materializeOriginalPrototypePlan(sequence, bad))
            == "RHI-CANONICAL-CONTENT-PLAN-UPLOAD-CAPACITY");
        assert(
            !root->hasOriginalPrototypePlan() && root->journalEmpty() && !original->diagnostic());
        root.reset();
        const auto planned = [&] {
            auto result = make();
            if (result && result->materializeOriginalPrototypePlan(sequence, content))
                return std::shared_ptr<Root>{};
            return result;
        };
        CpuQuotaFactoryFixture pressure(recordingQuota);
        const auto peak = pressure.measure(planned);
        pressure.verify(peak, planned);
        root = planned();
        assert(root && root->plannedContentControlCount() == 1u
            && root->plannedContentUploadCount() == 2u && root->retainedOwnerCount() == 17u
            && root->journalEmpty());
        const auto first = [&]() noexcept -> const char* {
            if (const auto* id = root->applyOriginalPrototypePlan())
                return id;
            assert(root->plannedContentGeneration(1u) == 1u
                && root->plannedContentGeneration(2u) == 2u);
            assert(root->status() == Root::Status::Pending && !root->journalEmpty()
                && !texture.observeOriginalContent());
            assert(std::string_view(original->diagnostic())
                == "RHI-CANONICAL-CONTENT-CONTROL-PENDING");
            observe(true);
            assert(std::string_view(root->commitOriginalPendingOwnership())
                == "RHI-CANONICAL-CONTENT-NATIVE-COMPLETION-REQUIRED");
            if (const auto* id = root->validate())
                return id;
            return root->abandonOriginalPendingOwnership();
        };
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{stateQuota,
                                                                  recordingQuota,
                                                                  inputs->originalMetadataQuota(),
                                                                  inputs->originalScalars()
                                                                      ->originalUsage()
                                                                      ->originalSources()
                                                                      ->originalMetadataQuota()},
            first));
        const auto restored = texture.observeOriginalContent();
        assert(restored && restored->state() == State::Uninitialized
            && !restored->contentGeneration() && restored->revision() > original->revision());
        assert(
            std::string_view(original->diagnostic()) == "RHI-CANONICAL-CONTENT-EXPECTATION-STALE");
        observe(false);
        root.reset();
        graph.reset();
        graph = Graph::prepareOriginal(inputs, device, graphBudget, stateQuota, diagnostic);
        assert(graph);
        root = planned();
        assert(root);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{stateQuota, recordingQuota}, [&]() noexcept -> const char* {
                if (const auto* id = root->applyOriginalPrototypePlan())
                    return id;
                assert(root->plannedContentGeneration(1u) == 3u
                    && root->plannedContentGeneration(2u) == 4u);
                root.reset();
                return nullptr;
            }));
        assert(restored->diagnostic()
            && texture.observeOriginalContent()->revision() > restored->revision());
        graph.reset();
        graph = Graph::prepareOriginal(inputs, device, graphBudget, stateQuota, diagnostic);
        assert(graph);
        root = planned();
        assert(root);
        assert(std::string_view(root->applyOriginalPrototypePlan())
            == "RHI-CANONICAL-CONTENT-GENERATION-CAPACITY");
        assert(root->status() == Root::Status::Prepared && root->journalEmpty()
            && texture.observeOriginalContent());
        root.reset();
        graph.reset();
        assert(!stateQuota->liveBytes() && !recordingQuota->liveBytes());
        std::printf(
            "BINDLESS_RHI_CANONICAL_CONTENT_PLAN_OK peak%llu whole-texture uploads2 generations1-4 caller-generation-frame-independent same-root-journal public-commit-refused rollback-drop-revisions hot-allocation0 content-execution-closed\n",
            static_cast<unsigned long long>(peak));
    }
};
} // namespace arp::test
