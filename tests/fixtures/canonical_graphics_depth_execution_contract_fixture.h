#pragma once
#include "rhi/frontend/bindless_root_execution.h"
#include "rhi/semantics/bindless_original_execution_lowering.h"
#include "rhi/semantics/bindless_root_program_lowering.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
// Value contracts cannot issue native callbacks, mutate a journal or authorize
// execution. Test every legacy profile against the new canonical composition.
class CanonicalGraphicsDepthExecutionContractFixture final {
    using Life = rhi::OriginalCommandNativeLifecycleContract;
    using Completion = rhi::OriginalGeneralSubmissionCompletionContract;
    using Depth = rhi::OriginalCompletedDepthRootReadPlanContract;
    using Color = rhi::OriginalCompletedTextureReadPlanContract;
    using Callback = rhi::OriginalBindlessRootCallbackColdContract;
    using Execution = rhi::OriginalBindlessRootNativeExecutionContract;

    static constexpr bool isolated() noexcept
    {
        for (std::uint32_t version = 0u; version <= 21u; ++version) {
            if ((Life::validateContentProfile({version}, 19u) == nullptr) != (version == 20u))
                return false;
            if ((Completion::validateProfile(version, false, false, 19u, true) == nullptr)
                != (version == 14u))
                return false;
            if ((Depth::validatePendingProfile(4u, version, 19u, true, true) == nullptr)
                != (version == 20u))
                return false;
            if ((Callback::matchesCompletionProfile(version, 6u, 19u)) != (version == 20u))
                return false;
        }
        for (std::uint32_t content = 0u; content <= 20u; ++content)
            if ((Life::validateContentProfile({20u}, content) == nullptr) != (content == 19u)
                || (Completion::validateProfile(14u, false, false, content, true) == nullptr)
                    != (content == 19u))
                return false;
        return !Life::isFenceProfile(20u) && Life::isFenceProfile(19u)
            && Execution::lifecycleVersion(14u) == 20u && Execution::admissionVersion(14u) == 6u
            && Execution::closureVersion(14u) == 20u;
    }

public:
    static constexpr bool legacyIsolation() noexcept { return isolated(); }

    static void verify()
    {
        using namespace rhi;
        assert(isolated());
        const Life::Budget lifecycle{
            20u, true, 1u, 12u, 12u, 128u, 128u, 128u, 65536u, 65536u, 65536u};
        assert(!Life::validateIntent(lifecycle));
        assert(!Life::validateCapacityRequest({6u, true, 1u, 12u, 12u}, lifecycle));
        for (const auto field : {0u, 1u, 2u}) {
            auto less = lifecycle;
            if (!field)
                --less.maxPendingSubmissions;
            else if (field == 1u)
                --less.maxCallbackTickets;
            else
                --less.maxCallbackRegistrations;
            assert(Life::validateCapacityRequest({6u, true, 1u, 12u, 12u}, less));
        }
        assert(Depth::validatePendingProfile(4u, 20u, 19u, false, true));
        assert(Depth::validatePendingProfile(4u, 20u, 19u, true, false));
        for (const auto pending : {0u, 1u, 2u, 3u, 5u})
            assert(Depth::validatePendingProfile(pending, 20u, 19u, true, true));
        assert(!Color::validatePendingProfile(9u, true, false, 19u, false, true, 3u));
        for (const auto read : {0u, 1u, 2u, 4u})
            assert(Color::validatePendingProfile(9u, true, false, 19u, false, true, read));
        assert(Color::validatePendingProfile(9u, false, false, 19u, false, true, 3u));
        assert(Color::validatePendingProfile(9u, true, false, 19u, false, false, 3u));
        assert(Completion::validateProfile(14u, true, false, 19u, true));
        assert(Completion::validateProfile(14u, false, true, 19u, true));
        assert(Completion::validateProfile(14u, false, false, 19u, false));
        assert(!Completion::validateProfile(13u, false, false, 18u, true));

        using Program = OriginalBindlessRootProgramContract;
        assert(!Program::validateIntent({6u, true, 20u, 100u}));
        for (const auto kind : {CommandUsageKind::TextureBarrier,
                 CommandUsageKind::BeginRenderPass,
                 CommandUsageKind::BindPreparedGraphicsBinding,
                 CommandUsageKind::Draw,
                 CommandUsageKind::EndRenderPass,
                 CommandUsageKind::BufferCopy,
                 CommandUsageKind::Finish})
            assert(!Program::validateOperation(kind, 6u));
        for (const auto kind : {CommandUsageKind::BeginComputePass,
                 CommandUsageKind::BindPreparedComputeBinding,
                 CommandUsageKind::Dispatch,
                 CommandUsageKind::PushConstants})
            assert(Program::validateOperation(kind, 6u));
        assert(Program::validateIntent({7u, true, 20u, 100u}));

        OriginalBindlessRootExecutionBudget b{{14u, true},
            {20u, true, 9u, 32u, 128u, 65536u, 0u, 0u, 1u},
            {6u,
                true,
                20u,
                100u,
                128u,
                65536u,
                {1u, true, 1u, 1u, 1u, 4u, 4u, 65536u},
                {1u, true, 1u, 1u, 3u, 12u, 12u}},
            lifecycle,
            {3u, true, 1u, 4u, 65536u, 128u},
            {2u, true, 128u, 65536u, {6u, true, 20u, 100u}},
            {1u, true, 1u, 12u, 128u, 65536u, 65536u},
            {1u, true, 1u, 1u, 128u, 65536u, 65536u, 65536u}};
        b.closure.completedDepthInputs = {
            1u, true, 2u, 20u, OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
        assert(!b.validate());
        for (const auto version : {1u, 2u, 3u, 4u, 5u}) {
            auto wrong = b;
            wrong.admission.contractVersion = wrong.command.program.contractVersion = version;
            assert(wrong.validate());
        }
        for (const auto version : {16u, 18u, 19u, 21u}) {
            auto wrong = b;
            wrong.closure.contractVersion = version;
            assert(wrong.validate());
        }
        for (const auto input : {0u, 2u, 3u, 4u}) {
            auto wrong = b;
            wrong.closure.completedDepthInputs.contractVersion = input;
            assert(wrong.validate());
        }
        BindlessLoweringPlan plan;
        plan.success = plan.executable = true;
        plan.execution = BindlessExecution::Emulated;
        plan.originalExecutionContractVersion = 14u;
        assert(!Execution::validateProfile({14u, true}, plan, lifecycle));
        using Closure = OriginalBindlessRootNativeClosureContract;
        assert(!Closure::canonicalGraphicsDepthProfileDiagnostic(plan, 2u, true, true, 20u));
        assert(Closure::canonicalGraphicsDepthProfileDiagnostic(plan, 2u, true, true, 19u));
        plan.executable = false;
        plan.originalExecutionContractVersion = 0u;
        plan.originalPreparationContractVersion = 12u;
        assert(!Execution::hasExecutionPlan(plan, 14u));
        assert(!Closure::canonicalGraphicsDepthProfileDiagnostic(plan, 2u, true, true, 19u));
        assert(Closure::canonicalGraphicsDepthProfileDiagnostic(plan, 2u, true, true, 20u));

        BindlessCapabilities caps;
        caps.limitsQueried = true;
        caps.execution = BindlessExecution::DiagnosticFailure;
        caps.originalOrdinaryDepthGraphicsPreparationContractVersion = 12u;
        caps.originalOrdinaryColorGraphicsExecutionContractVersion = 13u;
        using Lowering = semantics::OriginalBindlessExecutionLowering;
        assert(!Lowering::capabilityAvailable(caps, 14u));
        caps.originalOrdinaryDepthGraphicsExecutionContractVersion = 14u;
        assert(Lowering::capabilityAvailable(caps, 14u));
        caps.limitsQueried = false;
        assert(!Lowering::capabilityAvailable(caps, 14u));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            const bool web = backend == Backend::WebGPU;
            const auto execution = Lowering::lowering(backend, 14u);
            assert(execution.executionAvailable == web && execution.completionAvailable == web
                && bool(execution.diagnostic) == !web);
            const auto closure =
                semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 20u);
            assert(closure.nativeCorrespondenceAvailable == web && !closure.executionAvailable);
            const auto callback =
                semantics::OriginalBindlessRootCallbackColdLowering::lowering(backend, 6u);
            const auto program =
                semantics::OriginalBindlessRootProgramLowering::lowering(backend, 6u);
            assert(callback.preparationAvailable == web && program.preparationAvailable == web
                && !program.executionAvailable && !program.completionAvailable);
        }
        std::puts(
            "BINDLESS_CANONICAL_DEPTH_EXECUTION14_CONTRACT_OK Content19 Closure20 Lifecycle20 Pending4 Submission14 five-backend independent-capability legacy-refused");
    }
};

static_assert(CanonicalGraphicsDepthExecutionContractFixture::legacyIsolation());
} // namespace arp::test
