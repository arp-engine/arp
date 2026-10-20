#pragma once
#include "rhi/frontend/bindless_mixed_entry_flow.h"
#include "rhi/frontend/bindless_root_callback.h"
#include "rhi/frontend/bindless_root_fence_execution.h"
#include "rhi/semantics/bindless_original_execution_lowering.h"
#include "rhi/semantics/bindless_root_program_lowering.h"

#include <cassert>

namespace arp::test {
class OriginalMixedEntryFlowFixture final {
public:
    static void verifyContract()
    {
        using namespace rhi;
        using Flow = OriginalBindlessMixedEntryFlowContract;
        assert(!Flow::validateIntent({1u, true, 1u}));
        assert(Flow::validateIntent({0u, true, 1u}));
        assert(Flow::validateIntent({2u, true, 1u}));
        assert(Flow::validateIntent({1u, false, 1u}));
        assert(Flow::validateIntent({1u, true, 0u}));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto d = Flow::lowering(backend);
            assert(d.validationAvailable && !d.diagnostic
                && d.validatorOnly == (backend == Backend::Null));
            assert(!d.executionAvailable && !d.completionAvailable);
            const auto unknown = Flow::lowering(backend, 2u);
            assert(!unknown.validationAvailable && unknown.diagnostic && !unknown.executionAvailable
                && !unknown.completionAvailable);
        }
        const auto webgl = Flow::lowering(Backend::WebGL);
        assert(webgl.diagnostic && !webgl.validationAvailable && !webgl.executionAvailable
            && !webgl.completionAvailable);
        const auto absent = Flow::inspect({}, nullptr, {1u, true, 1u});
        assert(absent.diagnostic && !absent.nativeExecutionProven() && !absent.declarationVisits
            && absent.rootSources.front() == SIZE_MAX && absent.depthSource == SIZE_MAX
            && absent.copyOutputSource == SIZE_MAX && absent.reduceOutputSource == SIZE_MAX
            && absent.resultsSource == SIZE_MAX);
        // Independent profiles require queried device capabilities and their
        // matching Native-fence or Emulated-callback lowering. Cold preparation
        // and the authenticated dataflow check grant no execution authority.
        BindlessCapabilities caps;
        caps.limitsQueried = true;
        for (const auto execution : {11u, 12u}) {
            const bool native = execution == 11u;
            const auto lifecycle = native ? 17u : 18u;
            const auto content = native ? 15u : 16u;
            const auto closure = native ? 14u : 15u;
            assert(!OriginalBindlessRootNativeExecutionContract::validateIntent({execution, true}));
            assert(OriginalBindlessRootNativeExecutionContract::lifecycleVersion(execution)
                == lifecycle);
            assert(
                OriginalBindlessRootNativeExecutionContract::closureVersion(execution) == closure);
            assert(!semantics::OriginalBindlessExecutionLowering::capabilityAvailable(
                caps, execution));
            OriginalCommandNativeLifecycleBudget life{lifecycle,
                true,
                1u,
                native ? 0u : 9u,
                native ? 0u : 9u,
                512u,
                512u,
                native ? 0u : 512u,
                65536u,
                65536u,
                native ? 0u : 65536u};
            assert(!OriginalCommandNativeLifecycleContract::validateIntent(life));
            assert(!OriginalCommandNativeLifecycleContract::validateContentProfile(life, content));
            for (unsigned other = 0u; other <= 16u; ++other)
                if (other != content)
                    assert(OriginalCommandNativeLifecycleContract::validateContentProfile(
                        life, other));
            assert(!OriginalCommandNativeLifecycleContract::validateCapacityRequest(
                {native ? 9u : 6u, true, 1u, native ? 0u : 9u, native ? 0u : 9u}, life));
            assert(!OriginalGeneralSubmissionCompletionContract::validateProfile(
                execution, false, false, content, true));
            assert(OriginalGeneralSubmissionCompletionContract::validateProfile(
                execution, false, false, content, false));
            assert(OriginalGeneralSubmissionCompletionContract::validateProfile(
                native ? 12u : 11u, false, false, content, true));
            OriginalBindlessRootNativeClosureBudget b{closure, true, 9u, 256u, 256u, 65536u};
            b.completedDepthInputs = {native ? 2u : 1u,
                true,
                1u,
                4u,
                OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
            b.maxAttachmentViews = 2u;
            assert(!OriginalBindlessRootNativeClosureContract::validateBudget(b));
            auto wrong = b;
            wrong.completedDepthInputs.contractVersion = native ? 1u : 2u;
            assert(OriginalBindlessRootNativeClosureContract::validateBudget(wrong));
            wrong = b;
            wrong.maxAttachmentViews = 0u;
            assert(OriginalBindlessRootNativeClosureContract::validateBudget(wrong));
            for (const auto backend :
                {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
                const auto e =
                    semantics::OriginalBindlessExecutionLowering::lowering(backend, execution);
                const auto l =
                    semantics::OriginalCommandNativeLifecycleLowering::lowering(backend, lifecycle);
                const auto c = semantics::OriginalBindlessRootNativeClosureLowering::lowering(
                    backend, closure);
                const bool available = native
                    ? (backend == Backend::Vulkan || backend == Backend::Metal)
                    : backend == Backend::WebGPU;
                assert(e.executionAvailable == available && e.completionAvailable == available
                    && bool(e.diagnostic) == !available);
                assert(l.executionAvailable == available && l.sealedIssuerAvailable == available
                    && bool(l.diagnostic) == !available);
                assert(c.nativeCorrespondenceAvailable == available && !c.executionAvailable
                    && bool(c.diagnostic) == !available);
                // Only Vulkan's callback-free mixed stream may consume a real
                // idle receipt from the exact runtime retaining its command.
                // Idle never proves callback drain or revives invalid content.
                const auto idle =
                    semantics::OriginalCommandNativeLifecycleLowering::deviceIdleLowering(
                        backend, lifecycle);
                const bool idleAvailable = native && backend == Backend::Vulkan;
                assert(idle.completionAvailable == idleAvailable
                    && bool(idle.diagnostic) == !idleAvailable);
            }
            auto supported = caps;
            supported.execution = native ? BindlessExecution::Native : BindlessExecution::Emulated;
            supported.originalNativeMixedComputeExecutionContractVersion = native ? 11u : 0u;
            supported.originalEmulatedMixedComputeExecutionContractVersion = native ? 0u : 12u;
            assert(semantics::OriginalBindlessExecutionLowering::capabilityAvailable(
                supported, execution));
            supported.limitsQueried = false;
            assert(!semantics::OriginalBindlessExecutionLowering::capabilityAvailable(
                supported, execution));
        }
        assert(!OriginalBindlessRootNativeExecutionContract::validateIntent({14u, true}));
        assert(!OriginalBindlessRootNativeExecutionContract::validateIntent({15u, true}));
        assert(!OriginalBindlessRootNativeExecutionContract::validateIntent({16u, true}));
        assert(OriginalBindlessRootNativeExecutionContract::validateIntent({17u, true}));
        assert(OriginalBindlessRootCallbackColdContract::matchesCompletionProfile(18u, 5u, 16u));
        assert(!OriginalBindlessRootCallbackColdContract::matchesCompletionProfile(16u, 5u, 16u));
        assert(!OriginalBindlessRootCallbackColdContract::matchesCompletionProfile(18u, 4u, 16u));
        // Public inputs must opt into the complete independent profile. A
        // storage budget or cold preparation version cannot lend its authority.
        OriginalBindlessRootNativeClosureBudget closure{15u, true, 11u, 256u, 512u, 65536u};
        closure.maxAttachmentViews = 2u;
        closure.completedDepthInputs = {
            1u, true, 2u, 8u, OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
        OriginalBindlessRootExecutionBudget callback{{12u, true},
            closure,
            {5u,
                true,
                40u,
                32768u,
                512u,
                65536u,
                {1u, true, 1u, 1u, 1u, 4u, 4u, 65536u},
                {1u, true, 1u, 1u, 3u, 12u, 12u}},
            {18u, true, 1u, 12u, 12u, 512u, 512u, 512u, 65536u, 65536u, 65536u},
            {3u, true, 1u, 4u, 65536u, 512u},
            {2u, true, 512u, 65536u, {5u, true, 40u, 32768u}},
            {1u, true, 1u, 12u, 512u, 65536u, 65536u},
            {1u, true, 1u, 1u, 512u, 65536u, 65536u, 65536u},
            {1u, true, 27u}};
        assert(!callback.validate());
        OriginalCommandAdmissionBudget coldSources;
        coldSources.maxNativePayloadObjects = 1u;
        coldSources.maxGraphOwners = 512u;
        using ColdCommand = OriginalBindlessRootColdCommandContract;
        assert(
            !ColdCommand::validateOwners(16u, callback.command, coldSources, callback.lifecycle));
        auto mismatchedCommand = callback.command;
        mismatchedCommand.program.contractVersion = 4u;
        assert(
            ColdCommand::validateOwners(16u, mismatchedCommand, coldSources, callback.lifecycle));
        auto oldLifecycle = callback.lifecycle;
        oldLifecycle.contractVersion = 16u;
        assert(ColdCommand::validateOwners(16u, callback.command, coldSources, oldLifecycle));
        assert(!ColdCommand::validateOwners(16u, mismatchedCommand, coldSources, oldLifecycle));
        auto nativeLifecycle = callback.lifecycle;
        nativeLifecycle.contractVersion = 17u;
        assert(ColdCommand::validateOwners(16u, callback.command, coldSources, nativeLifecycle));
        const OriginalCommandLifecycleCapacityRequest callbackCapacity{6u, true, 1u, 12u, 12u};
        assert(!OriginalBindlessRootCallbackCustodyContract::validateRequirements(
            16u, callbackCapacity, coldSources, callback.lifecycle, callback.custody));
        assert(!OriginalBindlessRootIssuerContract::validateRequirements(
            16u, coldSources, callback.lifecycle, callback.issuers));
        assert(OriginalBindlessRootCallbackCustodyContract::validateRequirements(
            16u, callbackCapacity, coldSources, nativeLifecycle, callback.custody));
        assert(OriginalBindlessRootIssuerContract::validateRequirements(
            16u, coldSources, nativeLifecycle, callback.issuers));
        for (unsigned field = 0u; field < 7u; ++field) {
            auto bad = callback;
            if (field == 0u)
                bad.mixedEntries = {};
            if (field == 1u)
                --bad.closure.contractVersion;
            if (field == 2u)
                bad.lifecycle.contractVersion = 16u;
            if (field == 3u)
                bad.admission.contractVersion = 4u;
            if (field == 4u)
                bad.command.program.contractVersion = 4u;
            if (field == 5u)
                bad.intent.contractVersion = 10u;
            if (field == 6u)
                bad.mixedEntries.maxDeclarationVisits = 0u;
            assert(bad.validate());
        }
        closure.contractVersion = 14u;
        closure.completedDepthInputs.contractVersion = 2u;
        OriginalBindlessRootFenceExecutionBudget fence{{11u, true},
            closure,
            {17u, true, 1u, 0u, 0u, 512u, 4096u, 0u, 65536u, 65536u, 0u},
            {9u, true, 1u, 0u, 0u},
            65536u,
            512u,
            {1u, true, 27u}};
        assert(!fence.validate());
        for (unsigned field = 0u; field < 6u; ++field) {
            auto bad = fence;
            if (field == 0u)
                bad.mixedEntries = {};
            if (field == 1u)
                --bad.closure.contractVersion;
            if (field == 2u)
                bad.lifecycle.contractVersion = 15u;
            if (field == 3u)
                bad.capacity.contractVersion = 8u;
            if (field == 4u)
                bad.intent.contractVersion = 9u;
            if (field == 5u)
                bad.closure.completedDepthInputs.contractVersion = 1u;
            assert(bad.validate());
        }
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            const auto cold =
                semantics::OriginalBindlessRootCallbackColdLowering::lowering(backend, 5u);
            assert(cold.preparationAvailable == (backend == Backend::WebGPU));
            assert(bool(cold.diagnostic) == (backend != Backend::WebGPU));
            const auto program =
                semantics::OriginalBindlessRootProgramLowering::lowering(backend, 5u);
            assert(program.preparationAvailable == (backend == Backend::WebGPU));
            assert(!program.executionAvailable && !program.completionAvailable);
        }
    }

    static void verify(const rhi::OriginalBindlessMixedEntryFlowContract::Roots& roots,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        rhi::Backend backend)
    {
        using namespace rhi;
        using Flow = OriginalBindlessMixedEntryFlowContract;
        const auto count = roots[0u]->size() + roots[1u]->size() + roots[2u]->size();
        const Flow::Budget budget{1u, true, count};
        const auto before = inventory->originalMetadataQuota()->liveBytes();
        const auto flow = Flow::inspect(roots, inventory, budget);
        if (flow.diagnostic)
            std::fprintf(stderr, "mixed entry flow: %s\n", flow.diagnostic);
        assert(
            !flow.diagnostic && !flow.nativeExecutionProven() && flow.declarationVisits == count);
        assert(flow.depthSource != SIZE_MAX && flow.copyOutputSource != SIZE_MAX
            && flow.reduceOutputSource != SIZE_MAX && flow.resultsSource != SIZE_MAX);
        assert(flow.copyOutputSource != flow.reduceOutputSource);
        // Mixed entries cannot borrow the existing two-entry storage authority.
        assert(OriginalBindlessStorageExecutionContract::validatePair(*roots[0u], *roots[1u]));
        assert(OriginalBindlessStorageExecutionContract::validatePair(*roots[1u], *roots[2u]));
        for (std::size_t i = 0u; i < roots.size(); ++i)
            assert(flow.rootSources[i] == inventory->originalLeaseIndex(roots[i]));
        const auto refuse = [&](const auto& candidates, const auto& sources, const auto& request) {
            const auto rejected = Flow::inspect(candidates, sources, request);
            assert(rejected.diagnostic && !rejected.nativeExecutionProven()
                && !rejected.declarationVisits);
            for (const auto index : rejected.rootSources)
                assert(index == SIZE_MAX);
            assert(rejected.depthSource == SIZE_MAX && rejected.copyOutputSource == SIZE_MAX
                && rejected.reduceOutputSource == SIZE_MAX && rejected.resultsSource == SIZE_MAX);
        };
        refuse(roots, inventory, Flow::Budget{1u, true, count - 1u});
        refuse(roots, inventory, Flow::Budget{1u, false, count});
        refuse(roots, inventory, Flow::Budget{2u, true, count});
        refuse(roots, std::shared_ptr<const OriginalCommandResourceSources>{}, budget);
        // Wrong phase order must not derive execution roles from entry names or
        // the table-wide retained resource set (identical on all three Roots).
        constexpr unsigned orders[][3u] = {
            {0u, 2u, 1u}, {1u, 0u, 2u}, {1u, 2u, 0u}, {2u, 0u, 1u}, {2u, 1u, 0u}};
        for (const auto& order : orders)
            refuse(Flow::Roots{roots[order[0u]], roots[order[1u]], roots[order[2u]]},
                inventory,
                budget);
        for (unsigned index = 0u; index < roots.size(); ++index) {
            auto bad = roots;
            bad[index].reset();
            refuse(bad, inventory, budget);
            bad = roots;
            bad[index] =
                std::shared_ptr<const Flow::Root>(roots[index].get(), [](const Flow::Root*) {});
            refuse(bad, inventory, budget);
        }
        refuse(Flow::Roots{roots[0u], roots[0u], roots[2u]}, inventory, budget);
        auto forgedInventory = std::shared_ptr<const OriginalCommandResourceSources>(
            inventory.get(), [](const auto*) {});
        refuse(roots, forgedInventory, budget);
        assert(inventory->originalMetadataQuota()->liveBytes() == before);
        std::printf(
            "BINDLESS_MIXED_ENTRY_FLOW1_OK backend=%u authenticated-order3 exact-less forged-refused execution-closed\n",
            unsigned(backend));
    }
};
} // namespace arp::test
