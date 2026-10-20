#pragma once

#include "original_typed_native_cold_contract_fixture.h"
#include "rhi/frontend/command_shader_validation.h"
#include "rhi/frontend/command_transfer_validation.h"

namespace arp::test {
// L2 composition checks. These values cannot create native custody, a receipt,
// replacement resource graph or public bindless execution authorization.
class OriginalGeometryReadbackCompositionFixture final {
    using Cold = rhi::OriginalTypedNativeColdContract;
    using Payload = rhi::OriginalTypedNativePayloadContract;
    using Callback = rhi::OriginalTypedCallbackCustodyContract;
    using Execution = rhi::OriginalTypedNativeExecutionContract;
    using Dependency = rhi::OriginalTypedDependencyProjectionContract;
    using Join = rhi::OriginalCommandReadbackJoinContract;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || core::StringView(actual) != expected)
            std::fprintf(stderr,
                "geometry/readback expected %s got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && core::StringView(actual) == expected);
    }

public:
    static void verifyContract()
    {
        assert(Cold::readbackJoinVersion(3u) == 2u && Cold::readbackJoinVersion(5u) == 3u);
        for (const auto version : {0u, 1u, 2u, 4u, 6u})
            assert(!Cold::readbackJoinVersion(version));
        auto required = OriginalTypedNativeColdContractFixture::required(true);
        required.contractVersion = 5u;
        auto& work = required.work;
        work = {};
        work.renderPasses = work.pipelineBinds = work.descriptorBinds = 1u;
        work.indexBinds = work.indexedDraws = 1u;
        work.vertexBinds = 2u;
        work.dependencies = 4u;
        work.viewports = work.scissors = work.attachmentReferences = 1u;
        work.implicitAccesses = 2u;
        required.transfers = {1u, 1u, 1u};
        assert(!Cold::inspectMixedWorkTotals(
            work, required.transfers, work.commands, work.nativeCalls));
        assert(work.commands == 18u && work.nativeCalls == 13u);
        required.readback = {1u, 1u, 1u, 512u, 512u, 512u};
        required.readbackMap = {1u, 1u, 3u, 12u, 12u};
        required.capacity.callbackTickets = required.capacity.callbackRegistrations = 12u;
        auto request = OriginalTypedNativeColdContractFixture::request(required);
        request.maxTransfers = required.transfers;
        request.readback = {2u, true, 1u, 1u, 1u, 512u, 512u, 512u};
        request.readbackMap = {1u, true, 1u, 1u, 3u, 12u, 12u};
        assert(!Cold::validateRequirements(required, request));
        assert(!Cold::validatePayloadProfile(request, 5u)
            && !Cold::validateLifecycleProfile(request, 2u));
        for (const auto old : {1u, 2u, 3u, 4u}) {
            is(Cold::validatePayloadProfile(request, old), "RHI-WEBGPU-TYPED-COLD-PAYLOAD-PROFILE");
            auto legacy = request;
            legacy.contractVersion = old;
            assert(Cold::validateIntent(legacy));
        }
        is(Cold::validateLifecycleProfile(request, 1u), "RHI-WEBGPU-TYPED-COLD-LIFECYCLE-PROFILE");
        auto bad = request;
        bad.readback = {};
        bad.readbackMap = {};
        is(Cold::validateIntent(bad), "RHI-ORIGINAL-TYPED-COLD-COMPOSED-READBACK-REQUIRED");
        bad = request;
        bad.readbackMap.prototypeOptIn = false;
        is(Cold::validateIntent(bad), "RHI-ORIGINAL-READBACK-MAP-OPT-IN");
        bad = request;
        bad.maxWork.vertexBinds = 1u;
        is(Cold::validateRequirements(required, bad), "RHI-ORIGINAL-TYPED-COLD-WORK-CAPACITY");
        bad = request;
        bad.maxTransfers.copies = 0u;
        is(Cold::validateIntent(bad), "RHI-ORIGINAL-TYPED-COLD-TRANSFER-BUDGET");
        bad = request;
        --bad.readbackMap.maxTotalCallbackTickets;
        assert(Cold::validateRequirements(required, bad));
        auto stale = required;
        --stale.capacity.callbackRegistrations;
        is(Cold::validateRequirements(stale, request),
            "RHI-ORIGINAL-TYPED-COLD-CALLBACK-RESERVATION");
        stale = required;
        ++stale.readback.nativeCopyCommands;
        assert(Cold::validateRequirements(stale, request));

        const Payload::Request payload{5u, true, work, 1u, 64u, 65536u, 1u, 1u, 1u};
        assert(!Payload::validateWork(work, 1u, 1u, 1u, payload));
        assert(Payload::profileMatches(payload, 5u, 7u));
        for (const auto freeze : {0u, 1u, 2u, 3u, 4u, 6u})
            assert(!Payload::profileMatches(payload, freeze, 7u));
        for (const auto closure : {0u, 2u, 3u, 4u, 5u, 6u, 8u})
            assert(!Payload::profileMatches(payload, 5u, closure));
        is(Payload::validateWork(work, 1u, 1u, 0u, payload),
            "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-REQUIREMENTS");
        auto fewerIndices = work;
        fewerIndices.indexBinds = 0u;
        assert(Payload::validateWork(fewerIndices, 1u, 1u, 1u, payload));
        auto less = payload;
        less.maxWork.indexedDraws = 0u;
        assert(Payload::validateWork(work, 1u, 1u, 1u, less));

        const Callback::Request custody{5u,
            true,
            rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
            1u,
            12u,
            1u,
            128u,
            65536u,
            65536u,
            {1u, true}};
        Callback::Participants participants;
        assert(!Callback::inspect(required, custody, participants));
        assert(participants.commandNativeCallbacks == 11u
            && participants.localObserverCallbacks == 1u
            && participants.sharedNativeLossCallbacks == 1u
            && participants.reservedCallbacks == 12u);
        auto missingMap = custody;
        missingMap.readbackMap = {};
        is(Callback::inspect(required, missingMap, participants),
            "RHI-TYPED-CALLBACK-CUSTODY-READBACK-MAP-VERSION");
        assert(!participants.reservedCallbacks && !participants.reservedRegistrations);
        stale = required;
        stale.readback = {};
        stale.readbackMap = {};
        is(Callback::inspect(stale, custody, participants),
            "RHI-TYPED-CALLBACK-CUSTODY-COMPOSED-READBACK-REQUIRED");
        auto fewerSlots = custody;
        --fewerSlots.maxCallbackSlots;
        is(Callback::inspect(required, fewerSlots, participants),
            "RHI-TYPED-CALLBACK-CUSTODY-PARTICIPANTS");
        const Execution::Request execution{5u,
            true,
            rhi::OriginalTypedSynchronizationPolicy::FrozenOriginalScopeVisibility1,
            {1u, true}};
        assert(!Execution::validateProfile(execution, 5u));
        assert(!Execution::validateReadbackProfile(execution, required, custody));
        auto noCompletion = execution;
        noCompletion.readbackCompletion = {};
        is(Execution::validateIntent(noCompletion),
            "RHI-TYPED-NATIVE-READBACK-COMPLETION-REQUIRED");
        for (const auto old : {1u, 2u, 3u, 4u})
            is(Execution::validateProfile(execution, old),
                "RHI-TYPED-NATIVE-EXECUTION-ISSUER-PROFILE");
        auto oldCompletion = execution;
        oldCompletion.contractVersion = 3u;
        is(Execution::validateReadbackProfile(oldCompletion, required, custody),
            "RHI-TYPED-NATIVE-READBACK-COMPLETION-BINDING");

        const rhi::OriginalGeometryUsageBudget geometry{
            1u, true, 18u, 3u, 15u, 1u, true, 1u, 3u, 54u};
        rhi::OriginalTypedUsageProjectionContract::Budget projection{4u,
            true,
            {3u, true, 0u, 1u, false, true, 1u, 1u, 1u, true, 1u},
            {3u, true, 0u, 1u, false, true, 18u, 1u, 1u, 1u, true, 1u},
            18u,
            1u,
            12u,
            1u,
            2u,
            1u,
            1u,
            512u,
            geometry};
        const Dependency::FreezeRequest freeze{5u,
            true,
            {4u, true, {4u, true, projection}, 4u, 24u, 288u, 66u, 12u, 2u},
            128u,
            65536u};
        assert(
            !Dependency::validateFreezeIntent(freeze) && Dependency::closureProfileMatches(5u, 7u));
        auto absent = freeze;
        absent.projection.usages.projection.maxReadbacks = 0u;
        absent.projection.usages.projection.maxReadbackMips = 0u;
        absent.projection.usages.projection.maxReadbackDeclaredBytes = 0u;
        is(Dependency::validateFreezeIntent(absent),
            "RHI-ORIGINAL-DEPENDENCY-FREEZE-COMPOSED-READBACK-REQUIRED");
        absent = freeze;
        absent.projection.usages.projection.geometryUsage.drawConsumptionContractVersion = 0u;
        absent.projection.usages.projection.geometryUsage.drawConsumptionOptIn = false;
        absent.projection.usages.projection.geometryUsage.maxDraws = 0u;
        absent.projection.usages.projection.geometryUsage.maxDrawAccesses = 0u;
        absent.projection.usages.projection.geometryUsage.maxBindingScans = 0u;
        is(Dependency::validateFreezeIntent(absent),
            "RHI-ORIGINAL-DEPENDENCY-FREEZE-COMPOSED-GEOMETRY-REQUIRED");
        absent = freeze;
        absent.projection.contractVersion = 3u;
        is(Dependency::validateFreezeIntent(absent), "RHI-ORIGINAL-DEPENDENCY-FREEZE-PROFILE");

        rhi::OriginalCommandStreamBudget stream;
        stream.contractVersion = 4u;
        stream.prototypeOptIn = true;
        stream.maxCommands = 18u;
        stream.maxRetainedOwners = 128u;
        stream.maxCpuMetadataBytes = 65536u;
        stream.maxPipelineBinds = stream.maxDescriptorBinds = stream.maxConsumptions = 1u;
        stream.maxImplicitAccesses = 8u;
        stream.descriptorConsumption = projection.shaderConsumption;
        stream.renderDescriptorUsage = projection.renderBindings;
        stream.geometryUsage = geometry;
        stream.stateSequence = {4u, true, 18u, 0u, 7u, 18u, 8u};
        Join::Budget join{3u, true, 1u, 1u, 1u, 128u, 512u, 512u, 65536u, stream, request.readback};
        assert(!Join::validateProgramIntent(join) && Join::programProfileMatches(join, 4u, 5u, 3u));
        assert(!Join::programProfileMatches(join, 3u, 4u, 3u)
            && !Join::programProfileMatches(join, 4u, 4u, 3u)
            && !Join::programProfileMatches(join, 4u, 5u, 2u));
        auto legacyJoin = join;
        legacyJoin.contractVersion = 2u;
        is(Join::validateProgramIntent(legacyJoin),
            "RHI-ORIGINAL-READBACK-PROGRAM-JOIN-STREAM-PROFILE");
        auto noGeometry = join;
        noGeometry.stream.geometryUsage = geometry;
        noGeometry.stream.geometryUsage.drawConsumptionContractVersion = 0u;
        noGeometry.stream.geometryUsage.drawConsumptionOptIn = false;
        noGeometry.stream.geometryUsage.maxDraws = 0u;
        noGeometry.stream.geometryUsage.maxDrawAccesses = 0u;
        noGeometry.stream.geometryUsage.maxBindingScans = 0u;
        is(Join::validateProgramIntent(noGeometry),
            "RHI-ORIGINAL-READBACK-PROGRAM-JOIN-GEOMETRY-REQUIRED");

        rhi::OriginalCommandNativeClosureBudget closure{
            7u, true, 7u, 9u, 128u, 2048u, 65536u, 1u, 1u, 1u, 1u};
        closure.geometryContent = {
            1u, true, 3u, 128u, 152u, geometry, {1u, true, 18u, 1u, 2u, 54u, 3u}};
        assert(!rhi::OriginalCommandNativeClosureContract::validateIntent(closure));
        using ClosureContract = rhi::OriginalCommandNativeClosureContract;
        assert(ClosureContract::lossJoinVersion(7u, false, true) == 1u);
        assert(ClosureContract::lossJoinVersion(7u, true, false) == 2u);
        assert(!ClosureContract::lossJoinVersion(7u, true, true)
            && !ClosureContract::lossJoinVersion(7u, false, false));
        assert(!ClosureContract::lossJoinVersion(6u, true, false)
            && !ClosureContract::lossJoinVersion(4u, false, true));
        assert(ClosureContract::lossJoinVersion(8u, false, true) == 1u);
        assert(!ClosureContract::lossJoinVersion(8u, true, false));
        assert(ClosureContract::lossJoinVersion(10u, false, true) == 1u);
        assert(!ClosureContract::lossJoinVersion(10u, true, false)
            && !ClosureContract::lossJoinVersion(10u, true, true)
            && !ClosureContract::lossJoinVersion(10u, false, false));
        assert(ClosureContract::lossJoinVersion(9u, false, true) == 1u);
        assert(!ClosureContract::lossJoinVersion(9u, true, false));
        assert(ClosureContract::pipelineSourceVersion(7u, false, true) == 1u);
        assert(ClosureContract::pipelineSourceVersion(7u, true, false) == 2u);
        assert(!ClosureContract::pipelineSourceVersion(7u, true, true));
        assert(!ClosureContract::pipelineSourceVersion(7u, false, false));
        assert(ClosureContract::pipelineSourceVersion(3u, true, false) == 2u);
        assert(ClosureContract::pipelineSourceVersion(4u, true, false) == 2u);
        assert(ClosureContract::pipelineSourceVersion(6u, false, true) == 1u);
        assert(!ClosureContract::pipelineSourceVersion(4u, false, true));
        assert(!ClosureContract::pipelineSourceVersion(6u, true, false));
        assert(ClosureContract::pipelineSourceVersion(8u, false, true) == 1u);
        assert(!ClosureContract::pipelineSourceVersion(8u, true, false));
        assert(ClosureContract::pipelineSourceVersion(10u, false, true) == 1u);
        assert(!ClosureContract::pipelineSourceVersion(10u, true, false)
            && !ClosureContract::pipelineSourceVersion(10u, true, true)
            && !ClosureContract::pipelineSourceVersion(10u, false, false));
        assert(ClosureContract::pipelineSourceVersion(9u, false, true) == 1u);
        assert(!ClosureContract::pipelineSourceVersion(9u, true, false));
        auto partialClosure = closure;
        partialClosure.geometryContent = {};
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(partialClosure),
            "RHI-ORIGINAL-GEOMETRY-CUSTODY-VERSION");
        partialClosure = closure;
        partialClosure.maxReadbackCopies = 0u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(partialClosure),
            "RHI-ORIGINAL-NATIVE-CLOSURE-READBACK-BUDGET");
        partialClosure = closure;
        partialClosure.contractVersion = 6u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(partialClosure),
            "RHI-ORIGINAL-NATIVE-CLOSURE-READBACK-SCHEMA");

        verifyLowering();
        std::puts(
            "BINDLESS_RHI_GEOMETRY_READBACK_COMPOSITION_CONTRACT_OK Join3 Closure7 Freeze5 Payload5 Cold5 exact-profiles finite-counts map12 legacy-refused private-composed-execution-optin public-closed");
    }

    static void verifyLowering()
    {
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto closure = rhi::semantics::OriginalCommandNativeClosureLowering::
                geometryReadbackLossBoundLowering(backend);
            const auto join =
                rhi::semantics::OriginalCommandReadbackJoinLowering::lowering(backend, 3u);
            const auto freeze =
                rhi::semantics::OriginalTypedDependencyProjectionLowering::freezeLowering(
                    backend, 5u);
            const auto payload =
                rhi::semantics::OriginalTypedNativePayloadLowering::lowering(backend, 5u);
            const auto cold =
                rhi::semantics::OriginalTypedNativeColdLowering::admissionLowering(backend, 5u);
            const auto command =
                rhi::semantics::OriginalTypedNativeColdCommandLowering::lowering(backend, 5u);
            const auto callback =
                rhi::semantics::OriginalTypedCallbackCustodyLowering::lowering(backend, 5u);
            const auto issuer =
                rhi::semantics::OriginalTypedIssuerPreparationLowering::lowering(backend, 5u);
            const auto execution =
                rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 5u, 1u);
            assert(closure.resourceClosureAvailable == (backend == rhi::Backend::WebGPU));
            assert(!closure.recordingIssuerAvailable && !closure.executionAvailable
                && closure.diagnostic);
            assert(join.privateJoinAvailable == (backend == rhi::Backend::WebGPU));
            assert(!join.recordingAvailable && !join.mapAvailable && !join.publicationAvailable
                && join.diagnostic);
            assert(freeze.projectionAvailable == (backend == rhi::Backend::WebGPU));
            assert(!freeze.executionAvailable
                && bool(freeze.diagnostic) != freeze.projectionAvailable);
            assert(payload.payloadAvailable == (backend == rhi::Backend::WebGPU));
            assert(
                !payload.recordingAvailable && !payload.executionAvailable && payload.diagnostic);
            assert(cold.admissionAvailable == (backend == rhi::Backend::WebGPU));
            assert(!cold.recordingAvailable && !cold.executionAvailable && cold.diagnostic);
            assert(command.connectionAvailable == (backend == rhi::Backend::WebGPU));
            assert(
                !command.recordingAvailable && !command.executionAvailable && command.diagnostic);
            assert(callback.observerPreparationAvailable == (backend == rhi::Backend::WebGPU));
            assert(!callback.recordingAvailable && !callback.executionAvailable
                && callback.diagnostic);
            assert(issuer.preparationAvailable == (backend == rhi::Backend::WebGPU));
            assert(!issuer.recordingAvailable && !issuer.submissionAvailable && issuer.diagnostic);
            assert(execution.recordingAvailable == (backend == rhi::Backend::WebGPU));
            assert(execution.submissionAvailable == execution.recordingAvailable
                && execution.prototype == execution.recordingAvailable);
            assert(bool(execution.diagnostic) != execution.recordingAvailable);
            assert(execution.validatorOnly == (backend == rhi::Backend::Null));
            assert(!rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 5u)
                    .recordingAvailable);
        }
    }
};
} // namespace arp::test
