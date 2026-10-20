#pragma once
#include "rhi/frontend/bindless_root_fence_execution.h"
#include "rhi/semantics/bindless_original_execution_lowering.h"

namespace arp::test {
class OriginalStorageExecutionContractFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        BindlessCapabilities caps;
        caps.limitsQueried = true;
        caps.execution = BindlessExecution::Native;
        assert(!semantics::OriginalBindlessExecutionLowering::capabilityAvailable(caps, 9u));
        caps.originalNativeStorageComputeExecutionContractVersion = 9u;
        assert(semantics::OriginalBindlessExecutionLowering::capabilityAvailable(caps, 9u));
        caps.execution = BindlessExecution::Emulated;
        assert(!semantics::OriginalBindlessExecutionLowering::capabilityAvailable(caps, 9u));
        BindlessLoweringPlan plan;
        plan.success = true;
        plan.execution = BindlessExecution::Native;
        plan.originalPreparationContractVersion = 8u;
        assert(semantics::OriginalBindlessPreparationLowering::hasStoragePlan(plan));
        assert(!OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 9u));
        plan.originalPreparationContractVersion = 0u;
        plan.originalExecutionContractVersion = 9u;
        plan.executable = true;
        assert(semantics::OriginalBindlessPreparationLowering::hasStoragePlan(plan));
        assert(OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 9u));
        assert(!OriginalBindlessRootNativeClosureContract::storageProfileDiagnostic(plan, 11u));
        assert(OriginalBindlessRootNativeClosureContract::storageProfileDiagnostic(plan, 10u));
        OriginalCommandNativeLifecycleBudget life{
            15u, true, 1u, 0u, 0u, 32u, 64u, 0u, 65536u, 65536u, 0u};
        assert(!OriginalCommandNativeLifecycleContract::validateIntent(life));
        assert(!OriginalCommandNativeLifecycleContract::validateContentProfile(life, 13u));
        assert(OriginalCommandNativeLifecycleContract::validateContentProfile(life, 14u));
        assert(!OriginalCommandNativeLifecycleContract::validateCapacityRequest(
            {8u, true, 1u, 0u, 0u}, life));
        assert(OriginalCommandNativeLifecycleContract::validateCapacityRequest(
            {7u, true, 1u, 0u, 0u}, life));
        life.maxCallbackTickets = 1u;
        assert(OriginalCommandNativeLifecycleContract::validateIntent(life));
        assert(!OriginalGeneralSubmissionCompletionContract::validateProfile(
            9u, false, false, 13u, true));
        for (auto version : {0u, 5u, 6u, 7u, 8u})
            assert(OriginalGeneralSubmissionCompletionContract::validateProfile(
                version, false, false, 13u, true));
        assert(OriginalGeneralSubmissionCompletionContract::validateProfile(
            9u, false, false, 14u, true));
        assert(OriginalGeneralSubmissionCompletionContract::validateProfile(
            9u, false, false, 13u, false));
        for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const bool native = backend == Backend::Metal || backend == Backend::Vulkan;
            const auto e = semantics::OriginalBindlessExecutionLowering::lowering(backend, 9u);
            const auto l =
                semantics::OriginalCommandNativeLifecycleLowering::lowering(backend, 15u);
            const auto c =
                semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 11u);
            const auto idle =
                semantics::OriginalCommandNativeLifecycleLowering::deviceIdleLowering(backend, 15u);
            assert(idle.completionAvailable == (backend == Backend::Vulkan));
            assert(bool(idle.diagnostic) != idle.completionAvailable);
            assert(e.executionAvailable == native && e.completionAvailable == native
                && bool(e.diagnostic) != native);
            assert(l.executionAvailable == native && l.sealedIssuerAvailable == native
                && bool(l.diagnostic) != native);
            assert(c.nativeCorrespondenceAvailable == native && !c.executionAvailable
                && bool(c.diagnostic) != native);
            const bool emulated = backend == Backend::WebGPU;
            const auto ee = semantics::OriginalBindlessExecutionLowering::lowering(backend, 10u);
            const auto el =
                semantics::OriginalCommandNativeLifecycleLowering::lowering(backend, 16u);
            const auto ec =
                semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 12u);
            assert(ee.executionAvailable == emulated && ee.completionAvailable == emulated
                && bool(ee.diagnostic) != emulated);
            assert(el.executionAvailable == emulated && el.sealedIssuerAvailable == emulated
                && bool(el.diagnostic) != emulated);
            assert(ec.nativeCorrespondenceAvailable == emulated && !ec.executionAvailable
                && bool(ec.diagnostic) != emulated);
            assert(
                !semantics::OriginalCommandNativeLifecycleLowering::deviceIdleLowering(backend, 16u)
                    .completionAvailable);
        }
        caps.execution = BindlessExecution::Emulated;
        assert(!semantics::OriginalBindlessExecutionLowering::capabilityAvailable(caps, 10u));
        caps.originalEmulatedStorageComputeExecutionContractVersion = 10u;
        assert(semantics::OriginalBindlessExecutionLowering::capabilityAvailable(caps, 10u));
        caps.execution = BindlessExecution::Native;
        assert(!semantics::OriginalBindlessExecutionLowering::capabilityAvailable(caps, 10u));
        plan.execution = BindlessExecution::Emulated;
        plan.originalExecutionContractVersion = 10u;
        assert(semantics::OriginalBindlessPreparationLowering::hasStoragePlan(plan));
        assert(OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 10u));
        assert(!OriginalBindlessRootNativeClosureContract::storageProfileDiagnostic(plan, 12u));
        assert(OriginalBindlessRootNativeClosureContract::storageProfileDiagnostic(plan, 11u));
        life = {16u, true, 1u, 9u, 9u, 512u, 512u, 512u, 65536u, 65536u, 65536u};
        assert(!OriginalCommandNativeLifecycleContract::validateIntent(life));
        assert(!OriginalCommandNativeLifecycleContract::validateContentProfile(life, 14u));
        assert(OriginalCommandNativeLifecycleContract::validateContentProfile(life, 13u));
        assert(!OriginalCommandNativeLifecycleContract::validateCapacityRequest(
            {6u, true, 1u, 9u, 9u}, life));
        assert(OriginalCommandNativeLifecycleContract::validateCapacityRequest(
            {8u, true, 1u, 0u, 0u}, life));
        assert(!OriginalGeneralSubmissionCompletionContract::validateProfile(
            10u, false, false, 14u, true));
        assert(OriginalGeneralSubmissionCompletionContract::validateProfile(
            10u, false, false, 13u, true));
        assert(OriginalGeneralSubmissionCompletionContract::validateProfile(
            9u, false, false, 14u, true));
        assert(OriginalBindlessRootCallbackColdContract::matchesCompletionProfile(16u, 4u, 14u));
        assert(!OriginalBindlessRootCallbackColdContract::matchesCompletionProfile(15u, 4u, 14u));
        assert(!OriginalBindlessRootCallbackColdContract::matchesCompletionProfile(16u, 3u, 14u));
        assert(!OriginalBindlessRootCallbackColdContract::matchesCompletionProfile(16u, 4u, 13u));
        assert(!OriginalBindlessRootProgramContract::validateIntent({4u, true, 32u, 4096u}));
        assert(OriginalBindlessRootNativeExecutionContract::lifecycleVersion(10u) == 16u);
        assert(OriginalBindlessRootNativeExecutionContract::closureVersion(10u) == 12u);
        assert(OriginalBindlessRootNativeExecutionContract::admissionVersion(10u) == 4u);
        for (const auto content : {15u, 16u}) {
            for (std::uint32_t lifecycle = 0u; lifecycle <= 16u; ++lifecycle)
                assert(OriginalCommandNativeLifecycleContract::validateContentProfile(
                    {lifecycle}, content));
            for (std::uint32_t completion = 0u; completion <= 10u; ++completion)
                assert(OriginalGeneralSubmissionCompletionContract::validateProfile(
                    completion, false, false, content, true));
            for (std::uint32_t lifecycle = 0u; lifecycle <= 16u; ++lifecycle)
                assert(!OriginalBindlessRootCallbackColdContract::matchesCompletionProfile(
                    lifecycle, 4u, content));
        }
        std::puts(
            "BINDLESS_STORAGE_EXECUTION9_CONTRACT_OK cold-separated same-journal Completion9 four-backend refusal");
    }
};
} // namespace arp::test
