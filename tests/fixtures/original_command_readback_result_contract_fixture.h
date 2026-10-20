#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/frontend/command_transfer_validation.h"
#include "rhi/semantics/command_lowering.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandReadbackResultContractFixture final {
    using Contract = rhi::OriginalCommandReadbackResultContract;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static void verify(const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs)
    {
        const rhi::OriginalCommandStreamBudget stream{1u,
            true,
            5u,
            2u,
            4u,
            0u,
            0u,
            0u,
            4u,
            0u,
            0u,
            0u,
            0u,
            1536u,
            inputs->retainedOwnerCount() + 1u,
            inputs->originalMetadataQuota()->limitBytes()};
        const Contract::Budget budget{1u, true, 2u, 4u, 1u, 1536u, 768u, 65536u};
        assert(!Contract::validateIntent(budget));
        auto bad = budget;
        bad.contractVersion = 0u;
        is(Contract::validateIntent(bad), "RHI-ORIGINAL-READBACK-RESULT-VERSION");
        bad = budget;
        bad.prototypeOptIn = false;
        is(Contract::validateIntent(bad), "RHI-ORIGINAL-READBACK-RESULT-OPT-IN");
        bad = budget;
        bad.maxUniqueResultBytes = 0u;
        is(Contract::validateIntent(bad), "RHI-ORIGINAL-READBACK-RESULT-BUDGET");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                arp::rhi::semantics::OriginalCommandReadbackResultLowering::lowering(backend);
            assert(d.diagnostic && !d.executionAvailable && !d.mapAvailable
                && !d.publicationAvailable);
            assert((d.disposition
                       == arp::rhi::semantics::OriginalCommandReadbackResultLowering::Disposition::
                           ValidatorOnly)
                == (backend == rhi::Backend::Null));
        }
        assert(!Contract::supports(rhi::CommandUsageKind::TextureUpload)
            && !Contract::supports(rhi::CommandUsageKind::BufferCopy));
        using Join = rhi::OriginalCommandReadbackJoinContract;
        const Join::Budget join{1u, true, 2u, 1u, 1u, 64u, 20u, 768u, 65536u, stream, budget};
        verifyGeneralOutputJoin(join);
        assert(!Join::validateIntent(join));
        auto invalidJoin = join;
        invalidJoin.contractVersion = 0u;
        is(Join::validateIntent(invalidJoin), "RHI-ORIGINAL-READBACK-JOIN-VERSION");
        invalidJoin = join;
        invalidJoin.prototypeOptIn = false;
        is(Join::validateIntent(invalidJoin), "RHI-ORIGINAL-READBACK-JOIN-OPT-IN");
        invalidJoin = join;
        invalidJoin.maxStagingResourceBytes = 0u;
        is(Join::validateIntent(invalidJoin), "RHI-ORIGINAL-READBACK-JOIN-BUDGET");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                arp::rhi::semantics::OriginalCommandReadbackJoinLowering::lowering(backend);
            assert(d.diagnostic && !d.recordingAvailable && !d.mapAvailable
                && !d.publicationAvailable);
            assert(d.privateJoinAvailable == (backend == rhi::Backend::WebGPU));
            const auto mixed =
                arp::rhi::semantics::OriginalCommandReadbackJoinLowering::lowering(backend, 2u);
            assert(mixed.diagnostic && !mixed.recordingAvailable && !mixed.mapAvailable
                && !mixed.publicationAvailable
                && mixed.privateJoinAvailable == (backend == rhi::Backend::WebGPU)
                && (mixed.disposition
                       == arp::rhi::semantics::OriginalCommandReadbackJoinLowering::Disposition::
                           ValidatorOnly)
                    == (backend == rhi::Backend::Null));
            assert(arp::rhi::semantics::OriginalCommandReadbackJoinLowering::lowering(backend, 4u)
                       .privateJoinAvailable
                == (backend == rhi::Backend::WebGPU));
        }
        assert(!arp::rhi::semantics::OriginalCommandReadbackJoinLowering::lowering(
            static_cast<rhi::Backend>(99), 2u)
                .privateJoinAvailable);
        auto mixedJoin = join;
        mixedJoin.contractVersion = 2u;
        is(Join::validateIntent(mixedJoin), "RHI-ORIGINAL-READBACK-JOIN-VERSION");
        is(Join::validateProgramIntent(mixedJoin),
            "RHI-ORIGINAL-READBACK-PROGRAM-JOIN-STREAM-PROFILE");
        mixedJoin.stream.contractVersion = 3u;
        is(Join::validateProgramIntent(mixedJoin), "RHI-ORIGINAL-MIXED-READBACK-RESULT-VERSION");
        const auto missingRoot = Join::inspectProgram({}, {}, mixedJoin);
        assert(missingRoot.diagnostic && !missingRoot.required.readbackCommands
            && !missingRoot.required.uniqueResultBytes);
        rhi::BufferResourceLayout destination{768u,
            rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::GpuToCpu,
            rhi::ResourceState::CopyDst};
        assert(!Join::destinationIntent(destination));
        destination.memory = rhi::MemoryUsage::GpuOnly;
        is(Join::destinationIntent(destination), "RHI-ORIGINAL-READBACK-JOIN-MAP-INTENT");
        destination.memory = rhi::MemoryUsage::GpuToCpu;
        destination.usage = destination.usage | rhi::ResourceUsage::CopySrc;
        is(Join::destinationIntent(destination), "RHI-ORIGINAL-READBACK-JOIN-MAP-INTENT");
        const auto inspect = [&](const Contract::Budget& b) {
            return Contract::inspect(inputs, stream, b);
        };
        const auto accepted = inspect(budget);
        assert(!accepted.diagnostic);
        const auto& r = accepted.required;
        assert(r.readbackCommands == 2u && r.nativeCopyCommands == 4u && r.resultDeclarations == 1u
            && r.declaredReadbackBytes == 1536u && r.uniqueResultBytes == 768u
            && r.cpuResultBytes == 65536u);
        verifyColdStorage(inputs, r);
        assert(!Contract::validateRequirements(r, budget));
        auto invalidRequirements = r;
        invalidRequirements.resultDeclarations = 3u;
        is(Contract::validateRequirements(invalidRequirements, budget),
            "RHI-ORIGINAL-READBACK-RESULT-REQUIREMENTS");
        rhi::OriginalCommandNativeColdBudget cold;
        cold.contractVersion = 4u;
        cold.prototypeOptIn = true;
        cold.maxOriginalCommands = 5u;
        cold.maxEncoders = cold.maxCommandBuffers = 1u;
        cold.maxRecordingScopeCallbacks = cold.maxSubmissionScopeCallbacks = 3u;
        cold.maxQueueCallbacks = 1u;
        cold.maxRuntimeLossCallbacks = cold.maxRuntimeLossRegistrations = 1u;
        cold.maxRetainedOwners = 128u;
        cold.maxCpuMetadataBytes = 65536u;
        cold.readback = budget;
        assert(!rhi::OriginalCommandNativeColdContract::validateIntent(cold));
        auto badCold = cold;
        badCold.contractVersion = 3u;
        assert(rhi::OriginalCommandNativeColdContract::validateIntent(badCold));
        badCold = cold;
        badCold.maxBufferUploads = 1u;
        assert(rhi::OriginalCommandNativeColdContract::validateIntent(badCold));
        rhi::OriginalCommandCopyPassLoweringBudget lowering{4u, true, 0u, 0u, 128u, 65536u, 0u};
        lowering.readback = budget;
        assert(!rhi::OriginalCommandCopyPassLoweringContract::validateIntent(lowering));
        auto badLowering = lowering;
        badLowering.maxCopyOperations = 1u;
        assert(rhi::OriginalCommandCopyPassLoweringContract::validateIntent(badLowering));
        badLowering = lowering;
        badLowering.contractVersion = 3u;
        assert(rhi::OriginalCommandCopyPassLoweringContract::validateIntent(badLowering));
        auto mapCold = cold;
        mapCold.contractVersion = 5u;
        mapCold.readbackMap = {1u, true, 1u, 1u, 3u, 12u, 12u};
        assert(!rhi::OriginalCommandNativeColdContract::validateIntent(mapCold));
        auto legacyCold = mapCold;
        legacyCold.contractVersion = 4u;
        is(rhi::OriginalCommandNativeColdContract::validateIntent(legacyCold),
            "RHI-ORIGINAL-NATIVE-COLD-MAP-PROFILE");
        rhi::OriginalCommandNativeColdRequirements mapRequired{
            5u, 5u, 1u, 1u, 0u, 3u, 3u, 1u, 1u, 1u, 1u, 1u, {1u, true, 1u, 12u, 12u}};
        mapRequired.readback = r;
        mapRequired.readbackMap = {1u, 1u, 3u, 12u, 12u};
        assert(!rhi::OriginalCommandNativeColdContract::validateRequirements(mapRequired, mapCold));
        auto wrongRequired = mapRequired;
        wrongRequired.capacity.callbackTickets = 8u;
        assert(
            rhi::OriginalCommandNativeColdContract::validateRequirements(wrongRequired, mapCold));
        wrongRequired = mapRequired;
        --wrongRequired.readbackMap.scopeCallbacks;
        assert(
            rhi::OriginalCommandNativeColdContract::validateRequirements(wrongRequired, mapCold));
        auto mapLowering = lowering;
        mapLowering.contractVersion = 5u;
        mapLowering.readbackMap = mapCold.readbackMap;
        assert(!rhi::OriginalCommandCopyPassLoweringContract::validateIntent(mapLowering));
        mapLowering.contractVersion = 4u;
        is(rhi::OriginalCommandCopyPassLoweringContract::validateIntent(mapLowering),
            "RHI-ORIGINAL-COPY-PASS-LOWERING-MAP-PROFILE");
        const rhi::OriginalCommandCallbackCustodyBudget mapCustody{
            2u, true, 1u, 12u, 128u, 65536u, 65536u};
        assert(!rhi::OriginalCommandCallbackCustodyContract::validateIntent(mapCustody)
            && rhi::OriginalCommandCallbackCustodyContract::additionalOwners(mapCustody) == 15u);
        auto invalidCustody = mapCustody;
        invalidCustody.contractVersion = 1u;
        assert(rhi::OriginalCommandCallbackCustodyContract::validateIntent(invalidCustody));
        invalidCustody = mapCustody;
        --invalidCustody.maxCallbackSlots;
        assert(rhi::OriginalCommandCallbackCustodyContract::validateIntent(invalidCustody));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                arp::rhi::semantics::OriginalCommandOrderedReadbackLowering::lowering(backend);
            assert(!d.mapAvailable && !d.publicationAvailable
                && d.recordingAvailable == (backend == rhi::Backend::WebGPU)
                && d.executionAvailable == (backend == rhi::Backend::WebGPU));
        }
        for (unsigned limit = 0u; limit < 6u; ++limit) {
            auto bad = budget;
            if (!limit)
                --bad.maxReadbackCommands;
            if (limit == 1u)
                --bad.maxNativeCopyCommands;
            if (limit == 2u)
                --bad.maxDeclaredReadbackBytes;
            if (limit == 3u)
                --bad.maxUniqueResultBytes;
            if (limit == 4u)
                --bad.maxCpuResultBytes;
            if (limit == 5u)
                --bad.maxResultDeclarations;
            const auto refused = inspect(bad);
            assert(refused.diagnostic && !refused.required.readbackCommands
                && !refused.required.uniqueResultBytes && !refused.required.nativeCopyCommands
                && !refused.required.resultDeclarations && !refused.required.cpuResultBytes);
        }
        const auto& source = inputs->originalScalars()->originalUsage()->originalSources();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{inputs->originalMetadataQuota(), source->originalMetadataQuota()},
            [&]() noexcept -> const char* {
                const auto verified = inspect(budget);
                assert(!verified.diagnostic && verified.required.uniqueResultBytes == 768u);
                return nullptr;
            }));
        std::puts(
            "BINDLESS_RHI_CANONICAL_READBACK_RESULT_REQUIREMENTS_OK repeated-original-request1 commands2 mip-copies4 declared1536 unique-result768 independent-Result-CPU65536 exact-less no-partial-requirements hot-allocation0 gates4-closed no-map-publication");
    }

    static void verifyTypedMapDrainContract()
    {
        // Mixed Callback3 reserves the original eight participants plus the
        // same single-map/four-receipt demand; reuse the complete drain matrix.
        verifyMapDrain({1u, 1u, 3u, 12u, 12u});
    }

    static void verifyGeneralOutputJoin(
        const rhi::OriginalCommandReadbackJoinContract::Budget& original)
    {
        verifyGeneralOutputCold();
        using Join = rhi::OriginalCommandReadbackJoinContract;
        auto b = original;
        b.contractVersion = 4u;
        b.generalOutput = {1u, true};
        assert(!Join::validateGeneralOutputIntent(b));
        auto finiteJoin = b;
        finiteJoin.generalOutput = {2u, true};
        assert(!Join::validateGeneralOutputIntent(finiteJoin));
        is(Join::validateIntent(b), "RHI-ORIGINAL-READBACK-JOIN-VERSION");
        assert(Join::validateProgramIntent(b));
        for (const auto version : {0u, 1u, 2u, 3u, 5u}) {
            auto wrong = b;
            wrong.contractVersion = version;
            is(Join::validateGeneralOutputIntent(wrong),
                "RHI-GENERAL-OUTPUT-READBACK-JOIN-VERSION");
        }
        auto graphicsJoin = b;
        graphicsJoin.generalOutput = {3u, true};
        assert(!Join::validateGeneralOutputIntent(graphicsJoin));
        for (const auto version : {0u, 4u}) {
            auto wrong = b;
            wrong.generalOutput.contractVersion = version;
            is(Join::validateGeneralOutputIntent(wrong), "RHI-GENERAL-OUTPUT-READBACK-VERSION");
        }
        auto wrong = b;
        wrong.generalOutput.prototypeOptIn = false;
        is(Join::validateGeneralOutputIntent(wrong), "RHI-GENERAL-OUTPUT-READBACK-OPT-IN");
        wrong = b;
        wrong.prototypeOptIn = false;
        assert(Join::validateGeneralOutputIntent(wrong));
        wrong = b;
        wrong.stream.contractVersion = 3u;
        is(Join::validateGeneralOutputIntent(wrong),
            "RHI-GENERAL-OUTPUT-READBACK-TRANSFER-PROFILE");
        wrong = b;
        wrong.result.contractVersion = 2u;
        is(Join::validateGeneralOutputIntent(wrong),
            "RHI-GENERAL-OUTPUT-READBACK-TRANSFER-PROFILE");
        for (unsigned field = 0u; field < 7u; ++field) {
            wrong = b;
            if (field == 0u)
                wrong.maxReadbacks = 0u;
            if (field == 1u)
                wrong.maxNativeSources = 0u;
            if (field == 2u)
                wrong.maxNativeDestinations = 0u;
            if (field == 3u)
                wrong.maxRetainedOwners = 0u;
            if (field == 4u)
                wrong.maxSourceResourceBytes = 0u;
            if (field == 5u)
                wrong.maxStagingResourceBytes = 0u;
            if (field == 6u)
                wrong.maxCpuMetadataBytes = 0u;
            assert(Join::validateGeneralOutputIntent(wrong));
        }
        for (const auto version : {1u, 2u, 3u}) {
            wrong = b;
            wrong.contractVersion = version;
            if (version == 1u)
                is(Join::validateIntent(wrong),
                    "RHI-ORIGINAL-READBACK-JOIN-GENERAL-OUTPUT-PROFILE");
            else {
                wrong.stream.contractVersion = version == 2u ? 3u : 4u;
                wrong.result.contractVersion = 2u;
                // General output cannot be laundered into the attachment profile.
                assert(Join::validateProgramIntent(wrong));
            }
        }
        const auto contentVersion = b.generalOutput.contractVersion == 3u ? 3u : 0u;
        assert(Join::generalOutputProfileMatches(b, 1u, 1u, contentVersion, 2u, 1u));
        for (unsigned profile = 0u; profile < 5u; ++profile)
            assert(!Join::generalOutputProfileMatches(b,
                profile == 0u ? 3u : 1u,
                profile == 1u ? 4u : 1u,
                profile == 2u ? (contentVersion ? 0u : 3u) : contentVersion,
                profile == 3u ? 1u : 2u,
                profile == 4u ? 0u : 1u));
        const auto missing = Join::inspectGeneralOutput({}, b);
        assert(missing.diagnostic && !missing.required.readbackCommands
            && !missing.required.uniqueResultBytes);
        rhi::ResourceBarrierDesc barrier;
        barrier.resource = {1u};
        barrier.before = rhi::ResourceState::ShaderWrite;
        barrier.after = rhi::ResourceState::CopySrc;
        barrier.sourceStages = rhi::PipelineStage::ComputeShader;
        barrier.sourceAccess = rhi::ResourceAccess::ShaderWrite;
        barrier.destinationStages = rhi::PipelineStage::Transfer;
        barrier.destinationAccess = rhi::ResourceAccess::TransferRead;
        assert(!Join::validateGeneralOutputDependency(barrier, barrier.before, {}));
        using Pending = rhi::OriginalGeneralOutputReadbackDependencyContract;
        const auto validatePending = [](rhi::ResourceState captured,
                                         rhi::ResourceState initial,
                                         rhi::ResourceState final,
                                         rhi::ResourceState pending,
                                         std::uint64_t before,
                                         std::uint64_t after) {
            return Pending::validatePendingStates(captured, initial, final, pending, before, after);
        };
        for (const auto revision : std::array<std::uint64_t, 3u>{{1u, 7u, UINT64_MAX - 1u}})
            assert(!validatePending(barrier.before,
                barrier.before,
                barrier.after,
                barrier.after,
                revision,
                revision + 1u));
        for (unsigned field = 0u; field < 8u; ++field) {
            auto captured = barrier.before, initial = barrier.before, final = barrier.after,
                 pending = barrier.after;
            std::uint64_t before = 7u, after = 8u;
            if (field == 0u)
                captured = rhi::ResourceState::ShaderRead;
            if (field == 1u)
                initial = rhi::ResourceState::CopyDst;
            if (field == 2u)
                final = rhi::ResourceState::ShaderRead;
            if (field == 3u)
                pending = rhi::ResourceState::ShaderWrite;
            if (field == 4u)
                after = before;
            if (field == 5u)
                after = before + 2u;
            if (field == 6u)
                before = 0u;
            if (field == 7u) {
                before = UINT64_MAX;
                after = 0u;
            }
            assert(validatePending(captured, initial, final, pending, before, after));
        }
        assert(rhi::semantics::TransferDependencyLowering::transition(
            rhi::Backend::WebGPU, 1u, true, barrier, barrier.before, true)
                .diagnostic);
        for (unsigned field = 0u; field < 7u; ++field) {
            auto wrong = barrier;
            if (field == 0u)
                wrong.sourceStages = rhi::PipelineStage::Transfer;
            if (field == 1u)
                wrong.sourceAccess = rhi::ResourceAccess::ShaderRead;
            if (field == 2u)
                wrong.destinationStages = rhi::PipelineStage::ComputeShader;
            if (field == 3u)
                wrong.destinationAccess = rhi::ResourceAccess::TransferWrite;
            if (field == 4u)
                wrong.before = rhi::ResourceState::CopyDst;
            if (field == 5u)
                wrong.after = rhi::ResourceState::ShaderRead;
            if (field == 6u)
                wrong.textureSubresources.baseMipLevel = 1u;
            assert(Join::validateGeneralOutputDependency(wrong, barrier.before, {}));
        }
        assert(Join::validateGeneralOutputDependency(barrier, rhi::ResourceState::CopySrc, {}));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            using L = rhi::semantics::OriginalCommandReadbackJoinLowering;
            const auto d = L::lowering(backend, 4u);
            assert(d.diagnostic && d.privateJoinAvailable == (backend == rhi::Backend::WebGPU)
                && !d.recordingAvailable && !d.mapAvailable && !d.publicationAvailable);
            assert((d.disposition == L::Disposition::ValidatorOnly)
                == (backend == rhi::Backend::Null));
            assert(!L::lowering(backend, 5u).privateJoinAvailable);
            using Dependency = rhi::semantics::OriginalGeneralOutputReadbackDependencyLowering;
            const auto dependency =
                Dependency::transition(backend, 1u, true, barrier, barrier.before, {});
            assert((dependency.outcome == rhi::CapabilityMode::Equivalent)
                == (backend == rhi::Backend::WebGPU));
            assert(bool(dependency.diagnostic) == (backend != rhi::Backend::WebGPU));
            assert(
                Dependency::transition(backend, 0u, true, barrier, barrier.before, {}).diagnostic);
            assert(
                Dependency::transition(backend, 1u, false, barrier, barrier.before, {}).diagnostic);
            const auto finiteDependency =
                Dependency::transition(backend, 2u, true, barrier, barrier.before, {});
            assert(finiteDependency.outcome == dependency.outcome
                && bool(finiteDependency.diagnostic) == bool(dependency.diagnostic));
            assert(
                Dependency::transition(backend, 2u, false, barrier, barrier.before, {}).diagnostic);
            assert(
                Dependency::transition(backend, 3u, true, barrier, barrier.before, {}).diagnostic);
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_OUTPUT_READBACK_JOIN4_CONTRACT_OK explicit-output1 Source2 correspondence1 State1 Inputs1 Content0 Stream1 Result1 legacy-attachment-refused four-lowerings native-copy-map-publication-closed");
    }

    static void verifyGeneralOutputCold()
    {
        verifyGeneralOutputPairing();
        using Cold = rhi::OriginalCommandNativeColdContract;
        Cold::Budget b;
        b.contractVersion = 5u;
        b.prototypeOptIn = true;
        b.generalOutput = {1u, true};
        b.maxOriginalCommands = 5u;
        b.maxEncoders = b.maxCommandBuffers = 1u;
        b.maxRecordingScopeCallbacks = b.maxSubmissionScopeCallbacks = 3u;
        b.maxQueueCallbacks = 1u;
        b.maxRuntimeLossCallbacks = b.maxRuntimeLossRegistrations = 1u;
        b.maxRetainedOwners = 320u;
        b.maxCpuMetadataBytes = 65536u;
        b.readback = {1u, true, 1u, 1u, 1u, 256u, 256u, 65536u};
        b.readbackMap = {1u, true, 1u, 1u, 3u, 12u, 12u};
        assert(!Cold::validateIntent(b));
        auto finiteCold = b;
        finiteCold.generalOutput = {2u, true};
        assert(!Cold::validateIntent(finiteCold));
        auto graphicsCold = b;
        graphicsCold.generalOutput = {3u, true};
        assert(!Cold::validateIntent(graphicsCold));
        for (const auto version : {0u, 4u}) {
            auto wrong = b;
            wrong.generalOutput.contractVersion = version;
            assert(Cold::validateIntent(wrong));
        }
        auto wrong = b;
        wrong.generalOutput.prototypeOptIn = false;
        is(Cold::validateIntent(wrong), "RHI-GENERAL-OUTPUT-COLD-OPT-IN");
        for (const auto version : {0u, 1u, 2u, 3u, 4u, 6u}) {
            wrong = b;
            wrong.contractVersion = version;
            is(Cold::validateIntent(wrong), "RHI-GENERAL-OUTPUT-COLD-READBACK-MAP-PROFILE");
        }
        wrong = b;
        wrong.generalOutput = {};
        assert(!Cold::validateIntent(wrong));
        assert(Cold::validateGeneralOutputIntent(wrong));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                rhi::semantics::OriginalCommandNativeColdLowering::generalOutputLowering(
                    backend, 1u);
            assert(decision.coldJoinAvailable == (backend == rhi::Backend::WebGPU)
                && !decision.recordingAvailable && !decision.executionAvailable
                && decision.diagnostic);
            assert((decision.disposition
                       == rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::
                           ValidatorOnly)
                == (backend == rhi::Backend::Null));
            assert(!rhi::semantics::OriginalCommandNativeColdLowering::generalOutputLowering(
                backend, 0u)
                    .coldJoinAvailable);
            const auto finite =
                rhi::semantics::OriginalCommandNativeColdLowering::generalOutputLowering(
                    backend, 2u);
            assert(finite.coldJoinAvailable == decision.coldJoinAvailable
                && !finite.recordingAvailable && !finite.executionAvailable);
            assert(rhi::semantics::OriginalCommandNativeColdLowering::generalOutputLowering(
                       backend, 3u)
                       .coldJoinAvailable
                == (backend == rhi::Backend::WebGPU));
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_OUTPUT_COLD1_CONTRACT_OK independent-Cold5-Join4 missing-legacy-opt-refused four-backends execution-closed");
    }

    static void verifyGeneralOutputPairing()
    {
        using P = rhi::OriginalGeneralOutputPairingContract;
        const P::Intent general{1u, true}, legacy{};
        assert(!P::selected(legacy) && P::selected(general));
        assert(!P::validatePair(general, general) && !P::validatePair(legacy, legacy));
        const P::Intent finite{2u, true};
        assert(!P::validatePair(finite, finite) && !P::validateRecorded(finite, 2u, finite));
        assert(P::validatePair(general, finite) && P::validatePair(finite, general));
        assert(
            P::validateRecorded(finite, 1u, finite) && P::validateRecorded(general, 2u, general));
        is(P::validatePair(general, legacy), "RHI-GENERAL-OUTPUT-PROFILE-MISMATCH");
        is(P::validatePair(legacy, general), "RHI-GENERAL-OUTPUT-PROFILE-MISMATCH");
        for (const P::Intent bad : {P::Intent{0u, true}, P::Intent{1u, false}, P::Intent{2u, true}})
            assert(P::validatePair(general, bad) && P::validatePair(bad, general));
        is(P::validateRecorded(general, UINT32_MAX, general),
            "RHI-GENERAL-OUTPUT-PROFILE-UNRECORDED");
        is(P::validateRecorded(general, 0u, general),
            "RHI-GENERAL-OUTPUT-PROFILE-RECORDED-MISMATCH");
        is(P::validateRecorded(legacy, 1u, legacy), "RHI-GENERAL-OUTPUT-PROFILE-RECORDED-MISMATCH");
        assert(
            !P::validateRecorded(general, 1u, general) && !P::validateRecorded(legacy, 0u, legacy));
        assert(
            P::validateRecorded(general, 1u, legacy) && P::validateRecorded(legacy, 0u, general));
        rhi::OriginalCommandNativeColdCommandBudget command{1u, true, 1u, 320u, 65536u};
        rhi::OriginalCommandCallbackCustodyBudget callbacks{
            2u, true, 1u, 12u, 320u, 65536u, 65536u};
        rhi::OriginalCommandCopyPassLoweringBudget lowering;
        lowering.contractVersion = 5u;
        lowering.prototypeOptIn = true;
        lowering.maxRetainedOwners = 320u;
        lowering.maxCpuMetadataBytes = 65536u;
        lowering.readback = {1u, true, 1u, 1u, 1u, 256u, 256u, 65536u};
        lowering.readbackMap = {1u, true, 1u, 1u, 3u, 12u, 12u};
        rhi::OriginalCommandIssuerPreparationBudget issuer{1u, true, 1u, 1u, 320u, 65536u, 65536u};
        rhi::OriginalCommandScopedRecordingIntent recording{1u, true};
        rhi::OriginalCommandQueueSubmissionIntent submission{1u, true};
        const auto verify = [&](auto budget, auto validator) {
            assert(!validator(budget)); // Appended default preserves the legacy schema.
            budget.generalOutput = general;
            assert(!validator(budget));
            budget.generalOutput = {2u, true};
            assert(!validator(budget));
            budget.generalOutput = {3u, true};
            assert(!validator(budget));
            for (const P::Intent bad :
                {P::Intent{0u, true}, P::Intent{1u, false}, P::Intent{4u, true}}) {
                budget.generalOutput = bad;
                assert(validator(budget));
            }
            budget.generalOutput = general;
            budget.contractVersion = 0u;
            assert(validator(budget));
            budget.contractVersion = 1u;
            budget.prototypeOptIn = false;
            assert(validator(budget));
        };
        verify(command, rhi::OriginalCommandNativeColdCommandContract::validateIntent);
        verify(callbacks, rhi::OriginalCommandCallbackCustodyContract::validateIntent);
        verify(lowering, rhi::OriginalCommandCopyPassLoweringContract::validateIntent);
        verify(issuer, rhi::OriginalCommandIssuerPreparationContract::validateIntent);
        verify(recording, rhi::OriginalCommandScopedRecordingContract::validateIntent);
        verify(submission, rhi::OriginalCommandQueueSubmissionContract::validateIntent);
        callbacks.generalOutput = general;
        for (const auto slots : {8u, 16u}) {
            callbacks.maxCallbackSlots = slots;
            is(rhi::OriginalCommandCallbackCustodyContract::validateIntent(callbacks),
                "RHI-GENERAL-OUTPUT-CALLBACK-MAP-PROFILE");
        }
        callbacks.maxCallbackSlots = 12u;
        callbacks.contractVersion = 1u;
        assert(rhi::OriginalCommandCallbackCustodyContract::validateIntent(callbacks));
        lowering.generalOutput = general;
        for (const auto version : {1u, 2u, 3u, 4u, 6u}) {
            lowering.contractVersion = version;
            is(rhi::OriginalCommandCopyPassLoweringContract::validateIntent(lowering),
                "RHI-GENERAL-OUTPUT-LOWERING-MAP-PROFILE");
        }
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            using L = rhi::semantics::OriginalGeneralOutputExecutionLowering;
            const auto decision = L::lowering(backend, 1u);
            assert(decision.profilePairingAvailable == (backend == rhi::Backend::WebGPU)
                && decision.recordingAvailable == (backend == rhi::Backend::WebGPU)
                && decision.submissionAvailable == (backend == rhi::Backend::WebGPU)
                && !decision.nativeSyncAvailable && decision.diagnostic);
            assert((decision.disposition
                       == rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::
                           ValidatorOnly)
                == (backend == rhi::Backend::Null));
            for (const auto version : {0u, 4u})
                assert(!L::lowering(backend, version).profilePairingAvailable);
            assert(L::lowering(backend, 3u).profilePairingAvailable
                == (backend == rhi::Backend::WebGPU));
            const auto finite = L::lowering(backend, 2u);
            assert(finite.profilePairingAvailable == decision.profilePairingAvailable
                && finite.recordingAvailable == decision.recordingAvailable
                && finite.submissionAvailable == decision.submissionAvailable
                && !finite.nativeSyncAvailable);
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_OUTPUT_PAIRING1_CONTRACT_OK stages6 independent-intent missing-mixed-recorded-refused four-backends Pending-execution-closed");
    }

private:
    static void verifyColdStorage(const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const Contract::Requirements& required)
    {
        using Storage = rhi::OriginalCommandReadbackResultStorageContract;
        using Snapshot = rhi::OriginalCommandReadbackResultSnapshot;
        const auto owners = inputs->retainedOwnerCount();
        Storage::Budget b{1u, true, 1u, 768u, 65536u, owners + Storage::fixedAdditionalOwners + 1u};
        verifyMapContract(inputs, required);
        assert(!Storage::validateRequirements(required, owners, b));
        for (unsigned probe = 0u; probe < 6u; ++probe) {
            auto bad = b;
            if (!probe)
                bad.contractVersion = 0u;
            if (probe == 1u)
                bad.prototypeOptIn = false;
            if (probe == 2u)
                --bad.maxSnapshots;
            if (probe == 3u)
                --bad.maxResultBytes;
            if (probe == 4u)
                --bad.maxCpuResultBytes;
            if (probe == 5u)
                --bad.maxRetainedOwners;
            assert(Storage::validateRequirements(required, owners, bad));
        }
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                arp::rhi::semantics::OriginalCommandReadbackResultStorageLowering::lowering(
                    backend);
            assert(decision.coldStorageAvailable && !decision.mapAvailable
                && !decision.publicationAvailable
                && decision.disposition
                    == arp::rhi::semantics::OriginalCommandAdmissionLowering::Disposition::
                        ValidatorOnly);
        }
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        const auto source =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureReadbackSource>>(
                sources->sources()[2u]);
        const auto request = source->originalSource()->originalRequest();
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        const char* diagnostic = nullptr;
        assert(!Snapshot::prepareOriginalCold(request, request->originalMetadataQuota(), diagnostic)
            && diagnostic);
        auto alias =
            std::shared_ptr<core::CpuMemoryQuota>(request->originalMetadataQuota(), quota.get());
        assert(!Snapshot::prepareOriginalCold(request, alias, diagnostic) && diagnostic);
        alias.reset();
        const auto make = [&] {
            return Snapshot::prepareOriginalCold(request, quota, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(make);
        pressure.verify(peak, make);
        auto snapshot = make();
        assert(snapshot && !snapshot->publishedBytes() && snapshot->byteCapacity() == 768u
            && !snapshot->nativeExecutionProven());
        assert(core::SharedOwnerProjection::sameOwner(snapshot->originalRequest(), request)
            && core::SharedOwnerProjection::sameOwner(snapshot->originalMetadataQuota(), quota));
        auto forged = std::shared_ptr<const Snapshot>(quota, snapshot.get());
        assert(Snapshot::validateOriginalCold(forged, request));
        forged.reset();
        auto equal =
            rhi::TextureReadbackRequestDeclaration::prepareOriginal(request->originalLayout(),
                request->identity(),
                {1u, true, 2u, 4u, 768u, 65536u},
                request->originalMetadataQuota(),
                diagnostic);
        assert(equal && Snapshot::validateOriginalCold(snapshot, equal));
        equal.reset();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota, inputs->originalMetadataQuota(), sources->originalMetadataQuota()},
            [&]() noexcept -> const char* {
                assert(!snapshot->publishedBytes());
                return Snapshot::validateOriginalCold(snapshot, request);
            }));
        snapshot.reset();
        assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_CANONICAL_RESULT_SNAPSHOT_COLD_OK peak%llu exact-original-request immutable-layout padded768 independent-Result forged-equal-aliased-refused exact-less hot-allocation0 Result-zero map-publication-closed\n",
            static_cast<unsigned long long>(peak));
    }

    static void verifyMapContract(const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const Contract::Requirements& readback)
    {
        using Map = rhi::OriginalCommandReadbackMapContract;
        using D = rhi::OriginalCommandAdmissionBudget::CpuDomain;
        auto source =
            inputs->originalScalars()->originalUsage()->originalSources()->originalBudget();
        source.maxPendingSubmissions = 1u;
        source.maxCallbackTickets = source.maxCallbackRegistrations = 12u;
        source.maxCpuBytes[static_cast<std::size_t>(D::Submission)] =
            source.maxCpuBytes[static_cast<std::size_t>(D::Callback)] = 65536u;
        const Map::Budget budget{1u, true, 1u, 1u, 3u, 12u, 12u};
        Map::Requirements r;
        assert(!Map::deriveRequirements(readback, source, 8u, 8u, budget, r));
        verifyMapDrain(r);
        assert(r.maps == 1u && r.mapCallbacks == 1u && r.scopeCallbacks == 3u
            && r.totalCallbackTickets == 12u && r.totalCallbackRegistrations == 12u);
        for (unsigned probe = 0u; probe < 7u; ++probe) {
            auto bad = budget;
            if (!probe)
                bad.contractVersion = 0u;
            if (probe == 1u)
                bad.prototypeOptIn = false;
            if (probe == 2u)
                --bad.maxMaps;
            if (probe == 3u)
                --bad.maxMapCallbacks;
            if (probe == 4u)
                --bad.maxScopeCallbacks;
            if (probe == 5u)
                --bad.maxTotalCallbackTickets;
            if (probe == 6u)
                --bad.maxTotalCallbackRegistrations;
            assert(Map::deriveRequirements(readback, source, 8u, 8u, bad, r) && !r.maps
                && !r.scopeCallbacks && !r.totalCallbackTickets);
        }
        for (unsigned probe = 0u; probe < 4u; ++probe) {
            auto bad = source;
            if (!probe)
                bad.maxCallbackTickets = 8u;
            if (probe == 1u)
                bad.maxCallbackRegistrations = 8u;
            if (probe == 2u)
                bad.maxUnpublishedResultBytes = 767u;
            if (probe == 3u)
                --bad.maxCpuBytes[static_cast<std::size_t>(D::Result)];
            assert(Map::deriveRequirements(readback, bad, 8u, 8u, budget, r) && !r.maps
                && !r.totalCallbackRegistrations);
        }
        assert(Map::deriveRequirements(readback, source, UINT64_MAX, UINT64_MAX, budget, r)
            && !r.maps);
        auto missingCopies = readback;
        missingCopies.nativeCopyCommands = 0u;
        assert(Map::deriveRequirements(missingCopies, source, 8u, 8u, budget, r) && !r.maps);
        auto noSubmission = source;
        noSubmission.maxPendingSubmissions = 0u;
        assert(Map::deriveRequirements(readback, noSubmission, 8u, 8u, budget, r) && !r.maps);
        using P = Map::Phase;
        using E = Map::Event;
        using A = Map::Action;
        assert(Map::inspectTransition(P::Prepared, E::SameRootSubmissionProven).diagnostic);
        assert(Map::inspectTransition(P::AwaitingSubmission, E::MapPixelsCopiedAndUnmapped)
                .diagnostic);
        assert(Map::inspectTransition(P::Mapping, E::MapAndScopesProven).diagnostic);
        assert(Map::inspectTransition(static_cast<P>(UINT32_MAX), E::Cancel).diagnostic);
        assert(Map::inspectTransition(P::Mapping, static_cast<E>(UINT32_MAX)).diagnostic);
        for (const auto phase : {P::Prepared,
                 P::AwaitingSubmission,
                 P::Mapping,
                 P::AwaitingMapScopes,
                 P::Quarantined,
                 P::Published,
                 P::Retired}) {
            assert(Map::inspectTransition(phase, E::CallbackSuppressionProven).diagnostic);
            if (phase != P::AwaitingMapScopes)
                assert(Map::inspectTransition(phase, E::MapAndScopesProven).diagnostic);
            if (phase == P::Published || phase == P::Retired)
                for (const auto event :
                    {E::BeginSubmission, E::SameRootSubmissionProven, E::Cancel, E::NativeLoss})
                    assert(Map::inspectTransition(phase, event).diagnostic);
        }
        assert(Map::inspectTransition(P::AwaitingSubmission, E::SameRootSubmissionProven).action
            == A::RetainRootAndIssueMap);
        assert(Map::inspectTransition(P::Mapping, E::MapPixelsCopiedAndUnmapped).action
            == A::KeepUnpublished);
        assert(Map::inspectTransition(P::AwaitingMapScopes, E::MapAndScopesProven).action
            == A::PublishImmutableSnapshot);
        assert(Map::inspectTransition(P::Mapping, E::Cancel).action == A::InvalidateAndRetain);
        assert(Map::inspectTransition(P::Quarantined, E::AllForeignCallbacksDrained).action
            == A::ReleaseRetiredOwners);
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                arp::rhi::semantics::OriginalCommandReadbackMapLowering::lowering(backend);
            assert(d.mapAvailable == (backend == rhi::Backend::WebGPU) && !d.publicationAvailable
                && d.diagnostic);
        }
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{inputs->originalMetadataQuota()}, [&]() noexcept -> const char* {
                return Map::deriveRequirements(readback, source, 8u, 8u, budget, r);
            }));
        std::puts(
            "BINDLESS_RHI_CANONICAL_MAP_CONTRACT_OK repeated-Requests1 map1 scopes3 original-slots8 total12 Source-authored-capacity exact-less-overflow no-partial-requirements no-before-Submission no-early-publication no-suppression-proof no-terminal-revival hot-allocation0 WebGPU-Prototype-unpublished other3-closed");
    }

    static void verifyMapDrain(
        const rhi::OriginalCommandReadbackMapContract::Requirements& required)
    {
        using Drain = rhi::OriginalCommandReadbackMapDrainContract;
        using A = Drain::Action;
        Drain::Facts complete{1u, true, 1u, 1u, 1u, 1u, 3u, 3u, true, true, true, true};
        assert(Drain::inspect(required, complete).action == A::RetireUnpublished
            && !Drain::inspect(required, complete).mayPublish);
        for (unsigned probe = 0u; probe < 12u; ++probe) {
            auto bad = complete;
            if (!probe)
                bad.contractVersion = 0u;
            if (probe == 1u)
                bad.prototypeOptIn = false;
            if (probe == 2u)
                ++bad.mapsIssued;
            if (probe == 3u)
                ++bad.mapCallbacks;
            if (probe == 4u)
                ++bad.copiedMaps;
            if (probe == 5u)
                ++bad.safeUnmaps;
            if (probe == 6u)
                ++bad.scopeCallbacksDrained;
            if (probe == 7u)
                ++bad.successfulScopes;
            if (probe == 8u)
                bad.submissionProven = false;
            if (probe == 9u)
                bad.maySubmit = false;
            if (probe == 10u)
                bad.queueLifetimeProven = false;
            if (probe == 11u)
                bad.originalForeignCallbacksDrained = false;
            const auto d = Drain::inspect(required, bad);
            assert(d.diagnostic && !d.mayPublish && d.action != A::RetireUnpublished
                && d.action != A::RetireTerminal);
        }
        auto overflow = required;
        overflow.maps = UINT64_MAX;
        assert(Drain::inspect(overflow, complete).diagnostic);
        for (auto terminal : {&Drain::Facts::cancellation,
                 &Drain::Facts::nativeError,
                 &Drain::Facts::invocationUncertain,
                 &Drain::Facts::nativeLoss}) {
            auto facts = complete;
            facts.*terminal = true;
            assert(Drain::inspect(required, facts).action == A::RetireTerminal);
            for (unsigned probe = 0u; probe < 3u; ++probe) {
                auto pending = facts;
                if (!probe) {
                    pending.mapCallbacks = 0u;
                    pending.safeUnmaps = pending.copiedMaps = 0u;
                }
                if (probe == 1u)
                    pending.safeUnmaps = 0u;
                if (probe == 2u) {
                    pending.scopeCallbacksDrained = 2u;
                    pending.successfulScopes = 2u;
                }
                assert(Drain::inspect(required, pending).action == A::InvalidateAndRetain);
            }
        }
        // Scope listener suppression can drain userdata; it cannot prove output.
        auto suppressed = complete;
        suppressed.successfulScopes = 0u;
        assert(Drain::inspect(required, suppressed).action == A::InvalidateAndRetain);
        auto queueSuppressed = complete;
        queueSuppressed.submissionProven = false;
        queueSuppressed.queueLifetimeProven = false;
        queueSuppressed.mapsIssued = queueSuppressed.mapCallbacks = queueSuppressed.copiedMaps =
            queueSuppressed.safeUnmaps = 0u;
        queueSuppressed.scopeCallbacksDrained = queueSuppressed.successfulScopes = 0u;
        queueSuppressed.invocationUncertain = true;
        assert(Drain::inspect(required, queueSuppressed).action == A::InvalidateAndRetain);
        queueSuppressed.nativeLoss = true;
        assert(Drain::inspect(required, queueSuppressed).action == A::RetireTerminal);
        auto missingCopies = complete;
        missingCopies.copiedMaps = 0u;
        assert(Drain::inspect(required, missingCopies).action == A::InvalidateAndRetain);
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                arp::rhi::semantics::OriginalCommandReadbackMapDrainLowering::lowering(backend);
            assert(d.diagnostic && d.mapAvailable == (backend == rhi::Backend::WebGPU)
                && !d.publicationAvailable);
        }
        verifySnapshotPublicationContract(required);
        std::puts(
            "BINDLESS_RHI_CANONICAL_MAP_DRAIN_CONTRACT_OK no-early-retirement original-queue-or-loss required-C-map-callback-unmap scopes3 suppression-not-success uncertainty-retained unpublished-only WebGPU-Prototype-unpublished other3-closed");
    }

    static void verifySnapshotPublicationContract(
        const rhi::OriginalCommandReadbackMapContract::Requirements& required)
    {
        using Contract = rhi::OriginalCommandReadbackSnapshotPublicationContract;
        using A = Contract::Action;
        using Drain = rhi::OriginalCommandReadbackMapDrainContract;
        const Contract::Intent intent{1u, true};
        const Drain::Facts complete{1u, true, 1u, 1u, 1u, 1u, 3u, 3u, true, true, true, true};
        const Contract::BindingFacts bound{1u, 1u, 1u, true, true, true, true, false};
        assert(Contract::inspect(required, complete, intent, bound).action
            == A::FreezeIndependentSnapshots);
        assert(Contract::inspect(required, complete, {}, bound).diagnostic);
        assert(Contract::inspect(required, complete, {1u, false}, bound).diagnostic);
        for (unsigned probe = 0u; probe < 10u; ++probe) {
            auto bad = bound;
            if (!probe)
                bad.validatedOriginalRequests = 0u;
            if (probe == 1u)
                bad.validatedIndependentSnapshots = 0u;
            if (probe == 2u)
                bad.unpublishedSnapshots = 0u;
            if (probe == 3u)
                bad.originalSealAndRootCommitted = false;
            if (probe == 4u)
                bad.originalJournalEmpty = false;
            if (probe == 5u)
                bad.exactOriginalBindings = false;
            if (probe == 6u)
                bad.independentResultQuota = false;
            if (probe == 7u)
                bad.terminalObserved = true;
            if (probe == 8u)
                bad.validatedOriginalRequests = UINT64_MAX;
            if (probe == 9u)
                ++bad.validatedIndependentSnapshots;
            const auto d = Contract::inspect(required, complete, intent, bad);
            assert(d.action == A::Reject && d.diagnostic);
        }
        for (unsigned probe = 0u; probe < 12u; ++probe) {
            auto bad = complete;
            if (!probe)
                bad.mapCallbacks = bad.safeUnmaps = bad.copiedMaps = 0u;
            if (probe == 1u)
                bad.safeUnmaps = 0u;
            if (probe == 2u)
                bad.copiedMaps = 0u;
            if (probe == 3u)
                bad.scopeCallbacksDrained = bad.successfulScopes = 2u;
            if (probe == 4u)
                bad.successfulScopes = 0u;
            if (probe == 5u)
                bad.queueLifetimeProven = false;
            if (probe == 6u)
                bad.originalForeignCallbacksDrained = false;
            if (probe == 7u)
                bad.submissionProven = false;
            if (probe == 8u)
                bad.cancellation = true;
            if (probe == 9u)
                bad.nativeError = true;
            if (probe == 10u)
                bad.invocationUncertain = true;
            if (probe == 11u)
                bad.nativeLoss = true;
            const auto d = Contract::inspect(required, bad, intent, bound);
            assert(d.action == A::Reject && d.diagnostic);
        }
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                arp::rhi::semantics::OriginalCommandReadbackSnapshotPublicationLowering::lowering(
                    backend);
            assert(d.publicationAvailable == (backend == rhi::Backend::WebGPU) && d.diagnostic);
        }
        // This validator accepts only data; it cannot publish a cold snapshot.
        std::puts(
            "BINDLESS_RHI_CANONICAL_SNAPSHOT_PUBLICATION_CONTRACT_OK separate-opt-in exact-original-Request Result-quota original-commit map-scopes-unmap-drain no-terminal-or-republication WebGPU-Prototype-opt-in other3-closed validator-not-authority");
    }
};
} // namespace arp::test
