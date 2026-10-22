#pragma once

#include "original_command_access_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/bindless_root_callback.h"
#include "rhi/frontend/bindless_root_execution.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/bindless_root_program_lowering.h"
#include "rhi/semantics/command_lowering.h"

#include <cstdio>

namespace arp::test {
class OriginalCommandNativeLifecycleFixture final {
    using Contract = rhi::OriginalCommandNativeLifecycleContract;
    using P = Contract::Phase;
    using E = Contract::Event;
    using A = Contract::RootAction;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "native lifecycle expected %s, got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static void configureSourceBudget(rhi::OriginalCommandAdmissionBudget& b)
    {
        // Explicit CPU-contract recipe, not native callback count evidence.
        b.maxPendingSubmissions = b.maxCallbackTickets = b.maxCallbackRegistrations = 1u;
        b.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Submission)] = 65536u;
        b.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Callback)] = 65536u;
    }

    static void verifyContract()
    {
        const rhi::OriginalBindlessRootExecutionInputs noInputs;
        rhi::OriginalBindlessRootExecutionBudget publicBudget{{1u, true},
            {1u, true, 1u, 1u, 128u, 65536u},
            {1u,
                true,
                13u,
                16u,
                128u,
                65536u,
                {1u, true, 1u, 1u, 1u, 4u, 4u, 65536u},
                {1u, true, 1u, 1u, 3u, 12u, 12u}},
            {11u, true, 1u, 12u, 12u, 128u, 128u, 128u, 65536u, 65536u, 65536u},
            {3u, true, 1u, 4u, 65536u, 128u},
            {2u, true, 128u, 65536u, {1u, true, 13u, 86u}},
            {1u, true, 1u, 12u, 128u, 65536u, 65536u},
            {1u, true, 1u, 1u, 128u, 65536u, 65536u, 65536u}};
        assert(!publicBudget.validate());
        auto invalidPublic = publicBudget;
        invalidPublic.issuers.maxSubmissionCpuBytes--;
        is(invalidPublic.validate(), "RHI-BINDLESS-ROOT-EXECUTION-QUOTA");
        invalidPublic = publicBudget;
        invalidPublic.command.contractVersion = 1u;
        invalidPublic.command.program = {};
        is(invalidPublic.validate(), "RHI-ORIGINAL-ROOT-COLD-COMMAND-PROGRAM-REQUIRED");
        invalidPublic = publicBudget;
        invalidPublic.lifecycle.contractVersion = 10u;
        is(invalidPublic.validate(), "RHI-ORIGINAL-ROOT-EXECUTION-LIFECYCLE");
        auto nullDevice = rhi::createNullDevice();
        assert(nullDevice);
        auto* extension = static_cast<rhi::BindlessCrossExtension*>(
            nullDevice->queryInterface(rhi::kExtensionBindlessCross));
        assert(extension);
        core::pmr::String publicDiagnostic;
        assert(!extension->prepareOriginalRootExecution(
                   *nullDevice, noInputs, publicBudget, publicDiagnostic)
            && publicDiagnostic == "RHI-BINDLESS-ORIGINAL-EXECUTION-NULL-PROFILE");
        is(rhi::OriginalBindlessRootExecution::record({}, {1u, true}),
            "RHI-BINDLESS-ROOT-EXECUTION-OWNER");
        is(rhi::OriginalBindlessRootExecution::submit({}, {1u, true}),
            "RHI-BINDLESS-ROOT-EXECUTION-OWNER");
        using Lowering = rhi::semantics::OriginalCommandNativeLifecycleLowering;
        Contract::Budget rootCallback{
            10u, true, 1u, 12u, 12u, 128u, 128u, 128u, 65536u, 65536u, 65536u};
        assert(!Contract::validateIntent(rootCallback));
        auto liveRootCallback = rootCallback;
        liveRootCallback.contractVersion = 11u;
        assert(!Contract::validateIntent(liveRootCallback));
        assert(!Contract::validateContentProfile(liveRootCallback, 0u));
        rhi::BindlessLoweringPlan liveRootPlan;
        liveRootPlan.success = liveRootPlan.executable = true;
        liveRootPlan.execution = rhi::BindlessExecution::Emulated;
        liveRootPlan.originalExecutionContractVersion = 1u;
        using RootExecution = rhi::OriginalBindlessRootNativeExecutionContract;
        assert(!RootExecution::validateProfile({1u, true}, liveRootPlan, liveRootCallback));
        assert(RootExecution::validateProfile({1u, true}, liveRootPlan, rootCallback));
        assert(RootExecution::validateProfile({1u, false}, liveRootPlan, liveRootCallback));
        assert(RootExecution::validateProfile({2u, true}, liveRootPlan, liveRootCallback));
        auto preparationPlan = liveRootPlan;
        preparationPlan.executable = false;
        preparationPlan.originalExecutionContractVersion = 0u;
        preparationPlan.originalPreparationContractVersion = 1u;
        assert(RootExecution::hasPreparedPlan(preparationPlan));
        assert(RootExecution::validateProfile({1u, true}, preparationPlan, liveRootCallback));
        assert(!Contract::validateContentProfile(rootCallback, 0u));
        for (const auto content : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u})
            is(Contract::validateContentProfile(rootCallback, content),
                "RHI-ORIGINAL-ROOT-CALLBACK-CONTENT-PROFILE");
        const rhi::OriginalCommandLifecycleCapacityRequest rootCapacity{6u, true, 1u, 12u, 12u};
        assert(!Contract::validateCapacityRequest(rootCapacity, rootCallback));
        for (const auto version : {0u, 1u, 2u, 3u, 4u, 5u, 7u}) {
            auto invalid = rootCapacity;
            invalid.contractVersion = version;
            is(Contract::validateCapacityRequest(invalid, rootCallback),
                "RHI-ORIGINAL-LIFECYCLE-CAPACITY-VERSION");
        }
        auto invalidRootCapacity = rootCapacity;
        invalidRootCapacity.callbackTickets = 0u;
        is(Contract::validateCapacityRequest(invalidRootCapacity, rootCallback),
            "RHI-ORIGINAL-LIFECYCLE-CAPACITY-REQUEST");
        invalidRootCapacity = rootCapacity;
        ++invalidRootCapacity.callbackTickets;
        is(Contract::validateCapacityRequest(invalidRootCapacity, rootCallback),
            "RHI-ORIGINAL-LIFECYCLE-CAPACITY-CALLBACK-BUDGET");
        invalidRootCapacity = rootCapacity;
        ++invalidRootCapacity.callbackRegistrations;
        is(Contract::validateCapacityRequest(invalidRootCapacity, rootCallback),
            "RHI-ORIGINAL-LIFECYCLE-CAPACITY-REGISTRATION-BUDGET");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto live = Lowering::lowering(backend, 11u);
            assert(live.executionAvailable == (backend == rhi::Backend::WebGPU)
                && live.sealedIssuerAvailable == (backend == rhi::Backend::WebGPU));
            const auto execution = Lowering::lowering(backend, 10u);
            assert(execution.contractVersion == 10u && execution.diagnostic
                && !execution.executionAvailable && !execution.sealedIssuerAvailable);
            const auto cold =
                rhi::semantics::OriginalBindlessRootCallbackColdLowering::lowering(backend);
            assert(cold.preparationAvailable == (backend == rhi::Backend::WebGPU)
                && bool(cold.diagnostic) == (backend != rhi::Backend::WebGPU));
            // /6 is the existing general shader-resource cold contract.
            // Recognized profiles still obey their backend preparation gates.
            for (const auto version : {1u, 2u, 3u, 4u, 5u, 6u}) {
                const auto callback =
                    rhi::semantics::OriginalBindlessRootCallbackColdLowering::lowering(
                        backend, version);
                assert(callback.preparationAvailable == (backend == rhi::Backend::WebGPU)
                    && bool(callback.diagnostic) == (backend != rhi::Backend::WebGPU));
            }
            const auto produced =
                rhi::semantics::OriginalBindlessRootCallbackColdLowering::lowering(backend, 7u);
            assert(!produced.preparationAvailable);
            is(produced.diagnostic,
                backend == rhi::Backend::Null
                    ? "RHI-BINDLESS-PRODUCED-GEOMETRY-COLD-NULL-VALIDATOR"
                    : "RHI-BINDLESS-PRODUCED-GEOMETRY-NATIVE-ADMISSION-REQUIRED");
            for (const auto version : {0u, 8u, UINT32_MAX}) {
                const auto callback =
                    rhi::semantics::OriginalBindlessRootCallbackColdLowering::lowering(
                        backend, version);
                assert(!callback.preparationAvailable);
                is(callback.diagnostic, "RHI-ORIGINAL-ROOT-CALLBACK-COLD-VERSION");
            }
        }
        rhi::OriginalBindlessRootCallbackColdBudget cold{1u,
            true,
            16u,
            19u,
            128u,
            65536u,
            {1u, true, 1u, 1u, 1u, 4u, 4u, 65536u},
            {1u, true, 1u, 1u, 3u, 12u, 12u}};
        assert(!rhi::OriginalBindlessRootCallbackColdContract::validateIntent(cold));
        auto invalidCold = cold;
        invalidCold.contractVersion = 0u;
        is(rhi::OriginalBindlessRootCallbackColdContract::validateIntent(invalidCold),
            "RHI-ORIGINAL-ROOT-CALLBACK-COLD-VERSION");
        invalidCold = cold;
        invalidCold.prototypeOptIn = false;
        is(rhi::OriginalBindlessRootCallbackColdContract::validateIntent(invalidCold),
            "RHI-ORIGINAL-ROOT-CALLBACK-COLD-OPT-IN");
        invalidCold = cold;
        invalidCold.maxOperationVisits = 0u;
        is(rhi::OriginalBindlessRootCallbackColdContract::validateIntent(invalidCold),
            "RHI-ORIGINAL-ROOT-CALLBACK-COLD-BUDGET");
        using Storage = rhi::OriginalCommandReadbackResultStorageContract;
        const Storage::Budget bufferStorage{3u, true, 1u, 4u, 65536u, 13u};
        const rhi::OriginalCommandReadbackResultContract::Requirements bufferResult{
            1u, 1u, 1u, 4u, 4u, 65536u};
        assert(!Storage::validateBufferRequirements(bufferResult, 10u, bufferStorage));
        assert(
            Storage::validateIntent(bufferStorage) && Storage::validateTypedIntent(bufferStorage));
        for (const auto version : {0u, 1u, 2u, 4u}) {
            auto invalid = bufferStorage;
            invalid.contractVersion = version;
            is(Storage::validateBufferIntent(invalid),
                "RHI-ORIGINAL-BUFFER-READBACK-STORAGE-VERSION");
        }
        for (const auto field : {&Storage::Budget::maxSnapshots,
                 &Storage::Budget::maxResultBytes,
                 &Storage::Budget::maxCpuResultBytes}) {
            auto invalid = bufferStorage;
            --(invalid.*field);
            assert(Storage::validateBufferRequirements(bufferResult, 10u, invalid));
        }
        auto storageOwners = bufferStorage;
        --storageOwners.maxRetainedOwners;
        assert(Storage::validateBufferRequirements(bufferResult, 10u, storageOwners));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                rhi::semantics::OriginalCommandReadbackResultStorageLowering::bufferLowering(
                    backend, 3u);
            assert(d.coldStorageAvailable == (backend == rhi::Backend::WebGPU) && !d.mapAvailable
                && !d.publicationAvailable && d.diagnostic);
            assert(!rhi::semantics::OriginalCommandReadbackResultStorageLowering::bufferLowering(
                backend, 2u)
                    .coldStorageAvailable);
        }
        using RootCommand = rhi::OriginalBindlessRootColdCommandContract;
        RootCommand::Request rootCommand{1u, true, 90u, 65536u};
        using RootProgram = rhi::OriginalBindlessRootProgramContract;
        RootProgram::Request program{1u, true, 10u, 40u};
        for (const auto kind : {rhi::CommandUsageKind::BufferBarrier,
                 rhi::CommandUsageKind::ClearSnapshotFault,
                 rhi::CommandUsageKind::BeginComputePass,
                 rhi::CommandUsageKind::BindPreparedComputeBinding,
                 rhi::CommandUsageKind::Dispatch,
                 rhi::CommandUsageKind::EndComputePass,
                 rhi::CommandUsageKind::BeginTransferPass,
                 rhi::CommandUsageKind::BufferCopy,
                 rhi::CommandUsageKind::EndTransferPass,
                 rhi::CommandUsageKind::Finish})
            assert(!RootProgram::validateOperation(kind));
        for (const auto kind : {rhi::CommandUsageKind::BeginRenderPass,
                 rhi::CommandUsageKind::Draw,
                 rhi::CommandUsageKind::DispatchIndirect,
                 rhi::CommandUsageKind::TextureBarrier,
                 rhi::CommandUsageKind::InsertDebugMarker,
                 rhi::CommandUsageKind::Count})
            is(RootProgram::validateOperation(kind), "RHI-ORIGINAL-ROOT-PROGRAM-OPERATION");
        assert(!RootProgram::validateCounts(10u, 40u, program));
        is(RootProgram::validateCounts(11u, 40u, program),
            "RHI-ORIGINAL-ROOT-PROGRAM-COMMAND-CAPACITY");
        is(RootProgram::validateCounts(10u, 41u, program),
            "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY");
        assert(!RootProgram::validateDispatch(4u, 0u, 4u, 4u));
        is(RootProgram::validateDispatch(0u, 0u, 0u, 0u),
            "RHI-ORIGINAL-ROOT-PROGRAM-DISPATCH-LIMIT-UNAVAILABLE");
        for (const auto dimensions :
            {std::array{5u, 1u, 1u}, std::array{1u, 5u, 1u}, std::array{1u, 1u, 5u}})
            is(RootProgram::validateDispatch(dimensions[0], dimensions[1], dimensions[2], 4u),
                "RHI-ORIGINAL-ROOT-PROGRAM-DISPATCH-LIMIT");
        auto programCommand = rootCommand;
        programCommand.program = program;
        is(RootCommand::validateIntent(programCommand),
            "RHI-ORIGINAL-ROOT-COLD-COMMAND-PROGRAM-PROFILE");
        programCommand.contractVersion = 2u;
        assert(!RootCommand::validateIntent(programCommand));
        // Program3/4/5 are independent sampled/depth, storage and mixed
        // profiles. Cold command validation admits their intent, not execution.
        for (const auto version : {1u, 2u, 3u, 4u, 5u}) {
            programCommand.program.contractVersion = version;
            assert(!RootCommand::validateIntent(programCommand));
        }
        for (const auto version : {0u, 6u, UINT32_MAX}) {
            programCommand.program.contractVersion = version;
            is(RootCommand::validateIntent(programCommand), "RHI-ORIGINAL-ROOT-PROGRAM-VERSION");
        }
        programCommand.program = program;
        programCommand.program.prototypeOptIn = false;
        is(RootCommand::validateIntent(programCommand), "RHI-ORIGINAL-ROOT-PROGRAM-OPT-IN");
        programCommand.program = program;
        programCommand.program.maxOperationVisits = 0u;
        is(RootCommand::validateIntent(programCommand), "RHI-ORIGINAL-ROOT-PROGRAM-BUDGET");
        rhi::OriginalCommandAdmissionBudget rootCommandSource;
        rootCommandSource.maxNativePayloadObjects = 1u;
        rootCommandSource.maxGraphOwners = 128u;
        assert(!RootCommand::validateOwners(88u, rootCommand, rootCommandSource, rootCallback));
        auto rootCommandInvalid = rootCommand;
        --rootCommandInvalid.maxRetainedOwners;
        assert(
            RootCommand::validateOwners(88u, rootCommandInvalid, rootCommandSource, rootCallback));
        rootCommandInvalid = rootCommand;
        rootCommandInvalid.prototypeOptIn = false;
        assert(RootCommand::validateIntent(rootCommandInvalid));
        rootCommandSource.maxNativePayloadObjects = 0u;
        is(RootCommand::validateOwners(88u, rootCommand, rootCommandSource, rootCallback),
            "RHI-ORIGINAL-ROOT-COLD-COMMAND-NATIVE-CAPACITY");
        rootCommandSource.maxNativePayloadObjects = 1u;
        auto wrongLifecycle = rootCallback;
        wrongLifecycle.contractVersion = 1u;
        is(RootCommand::validateOwners(88u, rootCommand, rootCommandSource, wrongLifecycle),
            "RHI-ORIGINAL-ROOT-COLD-COMMAND-LIFECYCLE");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                rhi::semantics::OriginalBindlessRootColdCommandLowering::lowering(backend);
            assert(d.preparationAvailable == (backend == rhi::Backend::WebGPU)
                && !d.executionAvailable
                && bool(d.diagnostic) == (backend != rhi::Backend::WebGPU));
            const auto programShell =
                rhi::semantics::OriginalBindlessRootColdCommandLowering::lowering(backend, 2u);
            assert(programShell.preparationAvailable == d.preparationAvailable
                && !programShell.executionAvailable);
            assert(!rhi::semantics::OriginalBindlessRootColdCommandLowering::lowering(backend, 3u)
                    .preparationAvailable);
            const auto nativeProgram =
                rhi::semantics::OriginalBindlessRootProgramLowering::lowering(backend);
            assert(nativeProgram.preparationAvailable == (backend == rhi::Backend::WebGPU)
                && !nativeProgram.executionAvailable && !nativeProgram.completionAvailable);
            const auto graphicsProgram =
                rhi::semantics::OriginalBindlessRootProgramLowering::lowering(backend, 2u);
            assert(graphicsProgram.preparationAvailable == (backend == rhi::Backend::WebGPU)
                && !graphicsProgram.executionAvailable && !graphicsProgram.completionAvailable);
            for (const auto version : {3u, 4u, 5u}) {
                const auto independentProgram =
                    rhi::semantics::OriginalBindlessRootProgramLowering::lowering(backend, version);
                assert(independentProgram.preparationAvailable == (backend == rhi::Backend::WebGPU)
                    && !independentProgram.executionAvailable
                    && !independentProgram.completionAvailable);
            }
            for (const auto version : {0u, 6u, UINT32_MAX}) {
                const auto invalidProgram =
                    rhi::semantics::OriginalBindlessRootProgramLowering::lowering(backend, version);
                assert(!invalidProgram.preparationAvailable && !invalidProgram.executionAvailable
                    && !invalidProgram.completionAvailable);
                is(invalidProgram.diagnostic, "RHI-ORIGINAL-ROOT-PROGRAM-VERSION");
            }
        }
        using RootCustody = rhi::OriginalBindlessRootCallbackCustodyContract;
        using RootIssuer = rhi::OriginalBindlessRootIssuerContract;
        RootIssuer::Request rootIssuer{1u, true, 1u, 1u, 108u, 65536u, 65536u, 65536u};
        assert(
            !RootIssuer::validateRequirements(105u, rootCommandSource, rootCallback, rootIssuer));
        auto invalidIssuer = rootIssuer;
        --invalidIssuer.maxRetainedOwners;
        assert(
            RootIssuer::validateRequirements(105u, rootCommandSource, rootCallback, invalidIssuer));
        for (const auto member : {&RootIssuer::Request::maxRecordingCpuBytes,
                 &RootIssuer::Request::maxSubmissionCpuBytes,
                 &RootIssuer::Request::maxCallbackCpuBytes}) {
            invalidIssuer = rootIssuer;
            --(invalidIssuer.*member);
            assert(RootIssuer::validateRequirements(
                105u, rootCommandSource, rootCallback, invalidIssuer));
        }
        for (const auto version : {0u, 2u}) {
            invalidIssuer = rootIssuer;
            invalidIssuer.contractVersion = version;
            assert(RootIssuer::validateIntent(invalidIssuer));
        }
        invalidIssuer = rootIssuer;
        invalidIssuer.prototypeOptIn = false;
        assert(RootIssuer::validateIntent(invalidIssuer));
        invalidIssuer = rootIssuer;
        invalidIssuer.maxRecordingIssuers = 2u;
        assert(RootIssuer::validateIntent(invalidIssuer));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d = rhi::semantics::OriginalBindlessRootIssuerLowering::lowering(backend);
            assert(d.preparationAvailable == (backend == rhi::Backend::WebGPU)
                && !d.executionAvailable && !d.completionAvailable
                && bool(d.diagnostic) == (backend != rhi::Backend::WebGPU));
        }
        RootCustody::Request rootCustody{1u, true, 1u, 12u, 105u, 65536u, 65536u};
        assert(RootCustody::additionalOwners(rootCustody) == 15u);
        assert(!RootCustody::validateRequirements(
            90u, rootCapacity, rootCommandSource, rootCallback, rootCustody));
        auto invalidCustody = rootCustody;
        --invalidCustody.maxRetainedOwners;
        assert(RootCustody::validateRequirements(
            90u, rootCapacity, rootCommandSource, rootCallback, invalidCustody));
        for (const auto version : {0u, 2u}) {
            invalidCustody = rootCustody;
            invalidCustody.contractVersion = version;
            assert(RootCustody::validateIntent(invalidCustody));
        }
        invalidCustody = rootCustody;
        invalidCustody.prototypeOptIn = false;
        assert(RootCustody::validateIntent(invalidCustody));
        invalidCustody = rootCustody;
        --invalidCustody.maxCallbackSlots;
        assert(RootCustody::validateIntent(invalidCustody));
        invalidCustody = rootCustody;
        --invalidCustody.maxCpuCallbackBytes;
        assert(RootCustody::validateRequirements(
            90u, rootCapacity, rootCommandSource, rootCallback, invalidCustody));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                rhi::semantics::OriginalBindlessRootCallbackCustodyLowering::lowering(backend);
            assert(d.preparationAvailable == (backend == rhi::Backend::WebGPU)
                && !d.executionAvailable
                && bool(d.diagnostic) == (backend != rhi::Backend::WebGPU));
        }
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            for (const auto version : {6u, 7u, 8u, 9u}) {
                const auto idle = Lowering::deviceIdleLowering(backend, version);
                assert(idle.completionAvailable == (backend == rhi::Backend::Vulkan));
                assert(bool(idle.diagnostic) == !idle.completionAvailable);
                assert((idle.disposition == Lowering::Disposition::ValidatorOnly)
                    == (backend == rhi::Backend::Null));
            }
            for (const auto version : {0u, 1u, 2u, 3u, 4u, 5u, 10u}) {
                const auto unsupported = Lowering::deviceIdleLowering(backend, version);
                assert(!unsupported.completionAvailable && unsupported.diagnostic);
            }
        }
        Contract::Budget fence{6u, true, 1u, 0u, 0u, 32u, 64u, 0u, 65536u, 65536u, 0u};
        assert(!Contract::validateIntent(fence));
        assert(!Contract::validateContentProfile(fence, 0u));
        for (auto version : {1u, 2u, 3u, 4u, 5u, 6u})
            assert(Contract::validateContentProfile(fence, version));
        rhi::OriginalCommandLifecycleCapacityRequest fenceCapacity{2u, true, 1u, 0u, 0u};
        assert(!Contract::validateCapacityRequest(fenceCapacity, fence));
        auto foreignCapacity = fenceCapacity;
        foreignCapacity.contractVersion = 1u;
        assert(Contract::validateCapacityRequest(foreignCapacity, fence));
        foreignCapacity = fenceCapacity;
        foreignCapacity.callbackTickets = 1u;
        assert(Contract::validateCapacityRequest(foreignCapacity, fence));
        auto foreignFence = fence;
        foreignFence.maxCallbackCpuBytes = 1u;
        assert(Contract::validateIntent(foreignFence));
        foreignFence = fence;
        foreignFence.maxCallbackOwners = 1u;
        assert(Contract::validateIntent(foreignFence));
        foreignFence = fence;
        foreignFence.maxPendingSubmissions = 0u;
        assert(Contract::validateIntent(foreignFence));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                rhi::semantics::OriginalCommandNativeLifecycleLowering::lowering(backend, 6u);
            const bool native = backend == rhi::Backend::Vulkan || backend == rhi::Backend::Metal;
            assert(d.contractVersion == 6u && d.executionAvailable == native
                && d.sealedIssuerAvailable == native && bool(d.diagnostic) == !native);
        }
        // Full transition matrix: no pre-submit commit, post-submit rollback,
        // fabricated out-of-order success, terminal revival or receipt-as-success.
        for (unsigned p = 0u; p <= static_cast<unsigned>(P::DeviceLost); ++p)
            for (unsigned e = 0u; e <= static_cast<unsigned>(E::LossCallbacksDrainedOrDetached);
                ++e) {
                const auto phase = static_cast<P>(p);
                const auto event = static_cast<E>(e);
                const auto result = Contract::inspectTransition(phase, event);
                if (!result.accepted()) {
                    assert(result.next == phase && result.action == A::Preserve);
                    continue;
                }
                if (result.action == A::Commit)
                    assert(phase == P::SubmitPending && event == E::CompletionProven
                        && result.next == P::Completed);
                if (result.action == A::Rollback)
                    assert(phase == P::Claimed || phase == P::Recorded || phase == P::SubmitReserved
                        || (phase == P::QuarantinedBeforeSubmit
                            && event == E::BeforeSubmitScopesDrained));
                if (phase == P::SubmitPending || phase == P::QuarantinedAfterSubmit
                    || phase == P::DeviceLostRetained)
                    assert(result.action != A::Rollback);
                assert(phase != P::Completed && phase != P::Abandoned && phase != P::DeviceLost);
            }
        const auto step = [](P phase, E event, P next, A action) {
            const auto result = Contract::inspectTransition(phase, event);
            assert(result.accepted() && result.next == next && result.action == action);
            return result.next;
        };
        auto phase = step(P::Unclaimed, E::Claim, P::Claimed, A::Preserve);
        phase = step(phase, E::BeginRecording, P::RecordingPending, A::ApplyPlan);
        assert(!Contract::inspectTransition(phase, E::CompletionProven).accepted());
        phase = step(phase, E::RecordingValidatedAndDrained, P::Recorded, A::KeepPending);
        phase = step(phase, E::ReserveSubmission, P::SubmitReserved, A::KeepPending);
        phase = step(phase, E::MaySubmit, P::SubmitPending, A::KeepPending);
        step(phase, E::CompletionProven, P::Completed, A::Commit);
        phase = step(phase, E::Cancel, P::QuarantinedAfterSubmit, A::InvalidateAndRetain);
        assert(!Contract::inspectTransition(phase, E::CompletionProven).accepted());
        step(phase, E::SubmittedQueueAndCallbacksDrained, P::Abandoned, A::InvalidateAndRelease);
        phase = step(
            P::RecordingPending, E::RecordingFailed, P::QuarantinedBeforeSubmit, A::KeepPending);
        assert(
            !Contract::inspectTransition(phase, E::SubmittedQueueAndCallbacksDrained).accepted());
        step(phase, E::BeforeSubmitScopesDrained, P::Abandoned, A::Rollback);
        for (const auto pending : {P::Claimed,
                 P::RecordingPending,
                 P::Recorded,
                 P::SubmitReserved,
                 P::SubmitPending,
                 P::QuarantinedBeforeSubmit,
                 P::QuarantinedAfterSubmit}) {
            const auto lost =
                step(pending, E::CapturedDeviceLoss, P::DeviceLostRetained, A::InvalidateAndRetain);
            assert(!Contract::inspectTransition(lost, E::CompletionProven).accepted());
            step(lost, E::LossCallbacksDrainedOrDetached, P::DeviceLost, A::InvalidateAndRelease);
        }
        assert(!Contract::inspectTransition(static_cast<P>(999u), E::Claim).accepted());
        assert(!Contract::inspectTransition(P::Unclaimed, static_cast<E>(999u)).accepted());
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                arp::rhi::semantics::OriginalCommandNativeLifecycleLowering::lowering(backend);
            assert(!decision.sealedIssuerAvailable && !decision.executionAvailable
                && decision.diagnostic);
            assert((decision.disposition
                       == arp::rhi::semantics::OriginalCommandNativeLifecycleLowering::Disposition::
                           ValidatorOnly)
                == (backend == rhi::Backend::Null));
            const auto mixed =
                arp::rhi::semantics::OriginalCommandNativeLifecycleLowering::lowering(backend, 2u);
            assert(mixed.contractVersion == 2u && !mixed.sealedIssuerAvailable
                && !mixed.executionAvailable && mixed.diagnostic);
        }
        Contract::Budget legacy;
        legacy.contractVersion = 1u;
        auto mixed = legacy;
        mixed.contractVersion = 2u;
        auto general = legacy;
        general.contractVersion = 3u;
        assert(!Contract::validateContentProfile(general, 4u));
        for (const auto old : {0u, 1u, 2u, 3u})
            is(Contract::validateContentProfile(general, old),
                "RHI-ORIGINAL-NATIVE-LIFECYCLE-GENERAL-CONTENT-PROFILE");
        is(Contract::validateContentProfile(general, 5u),
            "RHI-ORIGINAL-NATIVE-LIFECYCLE-FINITE-CONTENT-PROFILE-UNAVAILABLE");
        for (const auto old : {legacy, mixed})
            is(Contract::validateContentProfile(old, 4u),
                "RHI-ORIGINAL-NATIVE-LIFECYCLE-COMPLETED-READ-PROFILE-UNAVAILABLE");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                rhi::semantics::OriginalCommandNativeLifecycleLowering::lowering(backend, 3u);
            assert(d.contractVersion == 3u && d.diagnostic && !d.sealedIssuerAvailable
                && !d.executionAvailable);
        }
        is(Contract::validateContentProfile(legacy, 3u),
            "RHI-ORIGINAL-NATIVE-LIFECYCLE-CONTENT-READBACK-PROFILE");
        assert(!Contract::validateContentProfile(mixed, 3u));
        for (const auto old : {0u, 1u, 2u}) {
            assert(!Contract::validateContentProfile(legacy, old));
            is(Contract::validateContentProfile(mixed, old),
                "RHI-ORIGINAL-NATIVE-LIFECYCLE-MIXED-CONTENT-PROFILE");
        }
        assert(!arp::rhi::semantics::OriginalCommandNativeLifecycleLowering::lowering(
            rhi::Backend::WebGPU, 3u)
                .sealedIssuerAvailable);
        Contract::Budget budget;
        is(Contract::validateIntent(budget), "RHI-ORIGINAL-NATIVE-LIFECYCLE-VERSION");
        budget.contractVersion = 1u;
        is(Contract::validateIntent(budget), "RHI-ORIGINAL-NATIVE-LIFECYCLE-OPT-IN");
        budget.prototypeOptIn = true;
        is(Contract::validateIntent(budget), "RHI-ORIGINAL-NATIVE-LIFECYCLE-COUNT-BUDGET");
        budget.maxPendingSubmissions = budget.maxCallbackTickets = budget.maxCallbackRegistrations =
            1u;
        is(Contract::validateIntent(budget), "RHI-ORIGINAL-NATIVE-LIFECYCLE-OWNER-BUDGET");
        budget.maxRecordingOwners = budget.maxSubmissionOwners = budget.maxCallbackOwners = 1u;
        is(Contract::validateIntent(budget), "RHI-ORIGINAL-NATIVE-LIFECYCLE-CPU-BUDGET");
        std::puts(
            "BINDLESS_RHI_CANONICAL_NATIVE_LIFECYCLE_CONTRACT_OK version-opt-in four-backend-closed transition-matrix no-pre-submit-commit no-post-submit-rollback loss-before-release terminal-no-revival inspection-not-authority");
    }

    template <class Factory> static void verify(Factory&& create)
    {
        verifyContract();
        OriginalCommandAccessFixture::withConfiguredOriginalPlan(
            create, configureSourceBudget, [&](auto& context, auto& root) {
                Contract::Budget budget{1u, true, 1u, 1u, 1u, 30u, 1u, 1u, 65536u, 65536u, 65536u};
                auto submission =
                    core::makeShared<core::CpuMemoryQuota>(budget.maxSubmissionCpuBytes);
                auto callback = core::makeShared<core::CpuMemoryQuota>(budget.maxCallbackCpuBytes);
                const auto inspect = [&](const auto& b, const auto& s, const auto& c) noexcept {
                    return Contract::inspectOriginalPreparation(root, b, s, c);
                };
                const auto ownership = [&]() noexcept {
                    return Contract::inspectOriginalOwnership(root, budget, submission, callback);
                };
                assert(!inspect(budget, submission, callback));
                const auto& source = root->originalGraph()
                                         ->originalInputs()
                                         ->originalScalars()
                                         ->originalUsage()
                                         ->originalSources();
                auto authored = source->originalBudget();
                --authored.maxPendingSubmissions;
                is(Contract::validateSourceBudget(authored, budget),
                    "RHI-ORIGINAL-NATIVE-LIFECYCLE-SUBMISSION-CAPACITY");
                authored = source->originalBudget();
                --authored.maxCallbackTickets;
                is(Contract::validateSourceBudget(authored, budget),
                    "RHI-ORIGINAL-NATIVE-LIFECYCLE-CALLBACK-CAPACITY");
                authored = source->originalBudget();
                --authored.maxCallbackRegistrations;
                is(Contract::validateSourceBudget(authored, budget),
                    "RHI-ORIGINAL-NATIVE-LIFECYCLE-REGISTRATION-CAPACITY");
                auto invalid = budget;
                --invalid.maxSubmissionCpuBytes;
                is(inspect(invalid, submission, callback),
                    "RHI-ORIGINAL-NATIVE-LIFECYCLE-SOURCE-CPU-BUDGET");
                invalid = budget;
                --invalid.maxRecordingOwners;
                is(inspect(invalid, submission, callback),
                    "RHI-ORIGINAL-NATIVE-LIFECYCLE-RECORDING-OWNER-CAPACITY");
                auto wrong =
                    core::makeShared<core::CpuMemoryQuota>(budget.maxCallbackCpuBytes - 1u);
                is(inspect(budget, submission, wrong), "RHI-ORIGINAL-NATIVE-LIFECYCLE-QUOTA");
                for (const auto& quota :
                    {context.quotas[2], context.quotas[3], context.quotas[4], context.quotas[5]}) {
                    is(inspect(budget, quota, callback),
                        "RHI-ORIGINAL-NATIVE-LIFECYCLE-QUOTA-DOMAIN");
                    is(inspect(budget, submission, quota),
                        "RHI-ORIGINAL-NATIVE-LIFECYCLE-QUOTA-DOMAIN");
                }
                is(inspect(budget, submission, submission),
                    "RHI-ORIGINAL-NATIVE-LIFECYCLE-QUOTA-DOMAIN");
                auto forged = std::shared_ptr<core::CpuMemoryQuota>(callback, submission.get());
                is(inspect(budget, submission, forged),
                    "RHI-ORIGINAL-NATIVE-LIFECYCLE-QUOTA-DOMAIN");
                forged.reset();
                const std::array quotas{context.quotas[0],
                    context.quotas[1],
                    context.quotas[2],
                    context.quotas[3],
                    context.quotas[4],
                    context.quotas[5],
                    submission,
                    callback};
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    if (const auto* id = inspect(budget, submission, callback))
                        return id;
                    return ownership();
                }));
                assert(root->journalEmpty()
                    && root->status() == rhi::OriginalCommandStateTransaction::Status::Prepared
                    && source->originalStateTransactionCount() == 1u);
                assert(!root->applyOriginalPrototypePlan());
                is(inspect(budget, submission, callback),
                    "RHI-ORIGINAL-NATIVE-LIFECYCLE-PLAN-STATUS");
                assert(!root->journalEmpty());
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, ownership));
                const auto& bufferLease =
                    std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                        context.leases[0]);
                auto& fields = const_cast<rhi::BufferDesc&>(bufferLease->originalObject()->desc());
                ++fields.sizeBytes;
                is(ownership(), "RHI-ORIGINAL-SOURCE-LEASE-OBJECT-MISMATCH");
                --fields.sizeBytes;
                assert(!ownership() && !root->journalEmpty());
                auto forgedRoot = std::shared_ptr<const rhi::OriginalCommandStateTransaction>(
                    callback, root.get());
                is(Contract::inspectOriginalOwnership(forgedRoot, budget, submission, callback),
                    "RHI-ORIGINAL-TRANSACTION-OWNER");
                forgedRoot.reset();
                invalid = budget;
                --invalid.maxRecordingOwners;
                is(Contract::inspectOriginalOwnership(root, invalid, submission, callback),
                    "RHI-ORIGINAL-NATIVE-LIFECYCLE-RECORDING-OWNER-CAPACITY");
                is(Contract::inspectOriginalOwnership(root, budget, submission, submission),
                    "RHI-ORIGINAL-NATIVE-LIFECYCLE-QUOTA-DOMAIN");
                assert(!root->commitOriginalPendingOwnership());
                is(inspect(budget, submission, callback),
                    "RHI-ORIGINAL-NATIVE-LIFECYCLE-PLAN-STATUS");
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, ownership));
                assert(!root->nativeExecutionProven() && !submission->liveBytes()
                    && !callback->liveBytes());
                std::puts(
                    "BINDLESS_RHI_CANONICAL_NATIVE_LIFECYCLE_OK genuine-original-plan read-only-source-budget independent-quota-owner-boundaries alias-forgery-reject exhausted-hot-allocation0 no-state-count-mutation pending-committed-reject issuer-execution-closed");
                context.device.reset();
                is(ownership(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
                std::puts(
                    "BINDLESS_RHI_CANONICAL_RETAINED_OWNERSHIP_OK prepared-pending-committed genuine-finals forged-root-quota-owner-refuse shutdown-reject cold-pending-committed-still-refuse hot-allocation0 inspection-not-proof");
            });
    }
};
} // namespace arp::test
