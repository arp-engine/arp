#pragma once
#include "rhi/backends/webgpu/webgpu_owned_texture_readback_recording_preparation.h"
#include "rhi/frontend/transfer_readback_borrowed_retirement.h"
#include "rhi/frontend/transfer_readback_owned_preparation.h"
#include "texture_readback_cold_budget_fixture.h"
#include "texture_readback_owned_snapshot_fixture.h"
#include "webgpu_original_texture_readback_source_fixture.h"

namespace arp::test {
class TextureReadbackOwnedPreparationFixture final {
public:
    static void verifyContract()
    {
        using Borrowed = rhi::BorrowedTextureReadbackRetirementContract;
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto cold = Borrowed::lowering(backend),
                       configured = Borrowed::lowering(backend, true);
            assert(cold.contractVersion == 1u && !cold.executionAvailable && cold.diagnostic);
            assert(configured.executionAvailable == (backend == rhi::Backend::Metal));
            assert((cold.disposition == Borrowed::Disposition::RetiredDiagnosticFailure)
                == (backend == rhi::Backend::Vulkan || backend == rhi::Backend::WebGPU));
        }
        assert(!Borrowed::lowering(static_cast<rhi::Backend>(99u), true).executionAvailable);
        std::puts(
            "BINDLESS_RHI_BORROWED_READBACK_RETIREMENT_CONTRACT_OK backends4 version1 WebGPU-Vulkan-retired Null-unavailable Metal-explicit-bounded no-owned-inference");
        using namespace rhi;
        using Contract = TextureReadbackOwnedPreparationContract;
        const auto coldAllocations = core::defaultCpuAllocator().stats().allocationCount;
        const char* coldDiagnostic = nullptr;
        assert(!rhi_webgpu::WebGpuOwnedTextureReadbackRecordingPreparation::prepareOriginal(
                   {}, {}, {}, {}, {}, {}, coldDiagnostic)
            && coldDiagnostic);
        assert(coldAllocations == core::defaultCpuAllocator().stats().allocationCount);
        WebGpuOriginalTextureReadbackSourceFixture::Context caller(512u, "owned 原始");
        caller.makeTexture(false);
        auto b = TextureReadbackColdBudgetFixture::budget();
        auto recipe = TextureReadbackColdBudgetFixture::prepare(b);
        const auto q = recipe.originalPlan->quotas();
        const char* diagnostic = nullptr;
        ReadbackTextureDesc d;
        caller.layout->inputOwner()->scalarFields().writeScalarFields(d);
        d.label = caller.layout->inputOwner()->label();
        caller.layout.reset();
        caller.quotas.fill(q[1]);
        auto input =
            TextureReadbackInputSnapshot::prepareOriginal(d, recipe.input, q[1], q[1], diagnostic);
        assert(input);
        auto layout = TextureReadbackLayout::prepareOriginal(
            input, caller.desc, recipe.layout, q[1], diagnostic);
        assert(layout);
        auto request = TextureReadbackRequestDeclaration::prepareOriginal(
            layout, {17u, 23u, 29u}, recipe.request, q[1], diagnostic);
        assert(request);
        auto statistics = TextureReadbackStatisticsPlan::prepareOriginal(
            layout, recipe.statistics, q[1], diagnostic);
        assert(statistics);
        TextureReadbackOwnedSnapshotFixture::verifyCold(
            recipe.originalPlan, request, statistics, d, caller.desc);
        TextureReadbackOwnedPreparationIntent intent;
        intent.contractVersion = 1u;
        intent.prototypeOptIn = true;
        intent.content = {1u, true, TextureReadbackContentIntent::Mode::QueueOrderedSnapshot};
        intent.passLabel = "原始读回";
        intent.sourceBarrier.resource = caller.id;
        intent.sourceBarrier.before = intent.sourceBarrier.after = ResourceState::CopySrc;
        intent.sourceBarrier.sourceStages = intent.sourceBarrier.destinationStages =
            PipelineStage::Transfer;
        intent.sourceBarrier.sourceAccess = intent.sourceBarrier.destinationAccess =
            ResourceAccess::TransferRead;
        intent.sourceBarrier.textureSubresources = defaultTextureSubresourceRange(caller.desc);
        const auto* source = caller.texture.get();
        auto plan = recipe.originalPlan;
        const auto validate = [&] {
            return Contract::validateOriginal(plan, caller.texture, request, statistics, intent);
        };
        assert(!validate());
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto a = core::defaultCpuAllocator().stats().allocationCount;
        const auto reject = [&](const char* expected) {
            assert(std::string_view(validate()) == expected && caller.texture.get() == source);
        };
        const auto original = intent;
        intent.contractVersion = 0u;
        reject("RHI-READBACK-OWNED-VERSION");
        intent = original;
        intent.prototypeOptIn = false;
        reject("RHI-READBACK-OWNED-OPT-IN");
        intent = original;
        intent.content.mode = TextureReadbackContentIntent::Mode::Unspecified;
        reject("RHI-READBACK-CONTENT-UNSPECIFIED");
        intent = original;
        intent.content.mode = TextureReadbackContentIntent::Mode::CompletedOriginalUpload;
        reject("RHI-READBACK-CONTENT-COMPLETED-EVIDENCE-REQUIRED");
        intent = original;
        intent.passLabel = "1234567890123";
        reject("RHI-READBACK-OWNED-PASS-LABEL-CAPACITY");
        intent = original;
        const char nul[]{'a', '\0', 'b'}, invalid[]{char(0xff)};
        intent.passLabel = {nul, sizeof(nul)};
        reject("RHI-READBACK-OWNED-LABEL-ENCODING");
        intent = original;
        intent.passLabel = {invalid, sizeof(invalid)};
        reject("RHI-READBACK-OWNED-LABEL-ENCODING");
        intent = original;
        intent.sourceBarrier.resource = d.destinationReadbackBuffer;
        reject("RHI-READBACK-OWNED-SOURCE-BARRIER");
        intent = original;
        intent.sourceBarrier.after = ResourceState::CopyDst;
        reject("RHI-READBACK-OWNED-SOURCE-BARRIER");
        intent = original;
        intent.sourceBarrier.textureSubresources.mipLevelCount = 0u;
        assert(validate());
        intent = original;
        intent.sourceBarrier.sourceStages = PipelineStage::None;
        assert(validate());
        intent = original;
        plan =
            std::shared_ptr<const TextureReadbackColdBudgetPlan>(q[1], recipe.originalPlan.get());
        assert(validate());
        plan = recipe.originalPlan;
        assert(!TextureReadbackOwnedSnapshotContract::validateColdOriginal(
            plan, request, statistics, intent.content.mode));
        assert(TextureReadbackOwnedSnapshotContract::validateColdOriginal(
            plan, request, statistics, TextureReadbackContentIntent::Mode::Unspecified));
        assert(TextureReadbackOwnedSnapshotContract::validatePublishedOriginal({}));
        auto saved = request;
        request = std::shared_ptr<const TextureReadbackRequestDeclaration>(q[1], saved.get());
        assert(validate());
        request = saved;
        saved.reset();
        auto moved = std::move(caller.texture);
        assert(std::string_view(validate()) == "RHI-READBACK-OWNED-SOURCE-MISSING"
            && moved.get() == source);
        caller.texture = std::move(moved);
        auto& desc = const_cast<TextureDesc&>(caller.texture->desc());
        ++desc.extent.width;
        assert(validate());
        --desc.extent.width;
        assert(!validate() && caller.texture.get() == source);
        for (unsigned bits = 0u; bits < 32u; ++bits) {
            const Contract::ContextFacts facts{bool(bits & 1u),
                bool(bits & 2u),
                bool(bits & 4u),
                bool(bits & 8u),
                bool(bits & 16u)};
            assert((Contract::validateExecutionContext(facts) == nullptr) == (bits == 15u));
            for (const auto backend :
                {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
                const auto decision = Contract::resolveLowering(backend, facts);
                const bool admitted = backend == Backend::WebGPU && bits == 15u;
                assert(decision.contractVersion == 1u && decision.executionAvailable == admitted
                    && (decision.diagnostic == nullptr) == admitted
                    && (decision.disposition == Contract::Disposition::PrototypeContextReady)
                        == admitted);
                if (backend == Backend::WebGPU)
                    assert(decision.diagnostic == Contract::validateExecutionContext(facts));
                else
                    assert(decision.disposition == Contract::lowering(backend).disposition);
            }
        }
        assert(
            !Contract::resolveLowering(static_cast<Backend>(99u), {true, true, true, true, false})
                .executionAvailable);
        std::puts(
            "BINDLESS_RHI_OWNED_RESOLVED_LOWERING_CONTRACT_OK backends4 contexts32 static-closed Prototype-context-only no-base-promotion allocation0");
        using Phase = Contract::Phase;
        using Action = Contract::Action;
        constexpr std::array<Phase, 6> required{{Phase::CreationPrepared,
            Phase::CreationReady,
            Phase::RecordingPrepared,
            Phase::RecordingReady,
            Phase::SubmissionPrepared,
            Phase::SubmissionCompleted}};
        for (unsigned action = 0u; action < required.size(); ++action)
            for (unsigned phase = 0u; phase <= static_cast<unsigned>(Phase::Retired); ++phase)
                assert((Contract::validateAction(
                            static_cast<Phase>(phase), static_cast<Action>(action))
                           == nullptr)
                    == (phase == static_cast<unsigned>(required[action])));
        assert(Contract::validateAction(Phase::CreationPrepared, static_cast<Action>(99u)));
        using Shutdown = Contract::ShutdownDisposition;
        for (unsigned bits = 0u; bits < 16u; ++bits) {
            const Contract::ShutdownFacts facts{
                bits & 1u, (bits >> 1u) & 1u, (bits >> 2u) & 1u, bool(bits & 8u)};
            const auto expected = facts.outstandingCallbacks ? Shutdown::DrainIssuedCallbacks
                : facts.quarantinedSubmissions && !facts.actualDeviceLossObserved
                ? Shutdown::AwaitNativeLoss
                : facts.unretiredRequests ? Shutdown::DrainUnretiredRequests
                                          : Shutdown::DetachIdleOrCompleted;
            assert(Contract::shutdownDisposition(facts) == expected);
        }
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto lower = Contract::lowering(backend);
            assert(lower.contractVersion == 1u && !lower.executionAvailable && lower.diagnostic);
            assert((lower.disposition == Contract::Disposition::ValidatorOnly)
                == (backend == Backend::Null));
            assert((lower.disposition == Contract::Disposition::PrototypeRequiresDeviceContext)
                == (backend == Backend::WebGPU));
        }
        assert(!Contract::lowering(static_cast<Backend>(99u)).executionAvailable);
        TextureReadbackOwnedPreparationExtension closed;
        assert(closed.executionDecision().contractVersion == 1u
            && !closed.executionDecision().executionAvailable
            && closed.executionDecision().disposition == Contract::Disposition::DiagnosticFailure);
        assert(closed.configureOriginalPlan(plan) && !closed.executionAvailable());
        assert(!closed.prepareOwnedOriginal(caller.texture, request, statistics, intent, diagnostic)
            && std::string_view(diagnostic) == "RHI-READBACK-OWNED-DEVICE-CONTEXT-UNIMPLEMENTED"
            && caller.texture.get() == source);
        TextureReadbackOwnedDelivery delivery;
        delivery.originalRequest = request;
        auto originalOutput = delivery.originalRequest;
        assert(!closed.pollOriginal(delivery, diagnostic)
            && core::SharedOwnerProjection::sameOwner(delivery.originalRequest, originalOutput));
        assert(closed.shutdownOriginal());
        delivery = {};
        originalOutput.reset();
        assert(a == core::defaultCpuAllocator().stats().allocationCount);
        std::pmr::set_default_resource(previous);
        {
            auto foreignQuota = core::makeShared<core::CpuMemoryQuota>(q[1]->limitBytes());
            auto foreignStats = TextureReadbackStatisticsPlan::prepareOriginal(
                layout, recipe.statistics, foreignQuota, diagnostic);
            assert(foreignStats);
            statistics.swap(foreignStats);
            reject("RHI-READBACK-OWNED-REQUEST-QUOTA");
            statistics.swap(foreignStats);
        }
        plan.reset();
        input.reset();
        layout.reset();
        statistics.reset();
        request.reset();
        recipe = {};
        caller.retire();
        for (const auto& quota : q)
            assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_READBACK_OWNED_CONTRACT_OK backends4 explicit-Prototype phases15 actions6 shutdown16 exact-Request original-source failure-preserving allocation0 public-execution-closed");
    }
};
} // namespace arp::test
