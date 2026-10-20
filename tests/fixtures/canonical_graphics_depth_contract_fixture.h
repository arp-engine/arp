#pragma once
#include "rhi/frontend/command_sources.h"
#include "rhi/semantics/bindless_original_execution_lowering.h"

#include <cassert>

namespace arp::test {
class CanonicalGraphicsDepthContractFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using Contract = OriginalBindlessRootNativeClosureContract;
        OriginalBindlessRootNativeClosureBudget budget{19u, true, 12u, 24u, 64u, 65536u};
        budget.maxAttachmentViews = 1u;
        budget.completedDepthInputs = {
            1u, true, 2u, 20u, OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
        assert(!Contract::validateBudget(budget));
        for (const auto version : {0u, 2u, 3u, UINT32_MAX}) {
            auto invalid = budget;
            invalid.completedDepthInputs.contractVersion = version;
            assert(Contract::validateBudget(invalid));
        }
        for (unsigned field = 0u; field < 8u; ++field) {
            auto invalid = budget;
            switch (field) {
            case 0u:
                invalid.prototypeOptIn = false;
                break;
            case 1u:
                invalid.maxResourceEntries = 0u;
                break;
            case 2u:
                invalid.maxNativeOwners = 0u;
                break;
            case 3u:
                invalid.maxRetainedOwners = 0u;
                break;
            case 4u:
                invalid.maxAttachmentViews = 0u;
                break;
            case 5u:
                invalid.completedDepthInputs.prototypeOptIn = false;
                break;
            case 6u:
                invalid.completedDepthInputs.maxCells = 0u;
                break;
            case 7u:
                --invalid.completedDepthInputs.maxRetainedOwners;
                break;
            }
            assert(Contract::validateBudget(invalid));
        }
        BindlessLoweringPlan plan;
        plan.success = true;
        plan.execution = BindlessExecution::Emulated;
        plan.originalPreparationContractVersion = 12u;
        assert(!Contract::canonicalGraphicsDepthProfileDiagnostic(plan, 2u, true, true));
        for (const auto version : {0u, 1u, 3u})
            assert(Contract::canonicalGraphicsDepthProfileDiagnostic(plan, version, true, true));
        assert(Contract::canonicalGraphicsDepthProfileDiagnostic(plan, 2u, false, true));
        assert(Contract::canonicalGraphicsDepthProfileDiagnostic(plan, 2u, true, false));
        for (const auto version : {0u, 2u, 5u, 11u, 13u}) {
            auto invalid = plan;
            invalid.originalPreparationContractVersion = version;
            assert(Contract::canonicalGraphicsDepthProfileDiagnostic(invalid, 2u, true, true));
        }
        for (unsigned field = 0u; field < 4u; ++field) {
            auto invalid = plan;
            if (field == 0u)
                invalid.executable = true;
            if (field == 1u)
                invalid.execution = BindlessExecution::Native;
            if (field == 2u)
                invalid.success = false;
            if (field == 3u)
                invalid.originalExecutionContractVersion = 13u;
            assert(Contract::canonicalGraphicsDepthProfileDiagnostic(invalid, 2u, true, true));
        }
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            const auto decision =
                semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 19u);
            assert(decision.contractVersion == 19u
                && decision.nativeCorrespondenceAvailable == (backend == Backend::WebGPU)
                && decision.validatorOnly == (backend == Backend::Null)
                && !decision.executionAvailable
                && bool(decision.diagnostic) == (backend != Backend::WebGPU));
            const auto input = OriginalCompletedDepthReadInputContract::lowering(backend, 1u);
            assert(input.nativeInputValidationAvailable == (backend == Backend::WebGPU)
                && !input.executionAvailable);
            assert(!semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 21u)
                    .nativeCorrespondenceAvailable);
        }
        // Ordinary native depth and canonical color profiles retain their own
        // plan/budget gates; the new closure is never an execution request.
        assert(Contract::canonicalGraphicsColorProfileDiagnostic(plan, 2u, true, true));
        auto legacy = budget;
        legacy.contractVersion = 17u;
        assert(Contract::validateBudget(legacy));
        legacy.contractVersion = 18u;
        assert(Contract::validateBudget(legacy));
        legacy.contractVersion = 16u;
        assert(Contract::validateBudget(legacy));
    }
};
} // namespace arp::test
