#include "completed_depth_texture_source_fixture.h"
#pragma once
#include "core/math/half_float.h"
#include "original_command_typed_stream_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "original_depth_table_geometry_composition_contract_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "original_typed_callback_custody_fixture.h"
#include "original_typed_native_payload_fixture.h"
#include "original_vertex_geometry_execution_fixture.h"
#include "rhi/backends/webgpu/webgpu_canonical_readback_joins.h"
#include "rhi/backends/webgpu/webgpu_canonical_readback_result_storage.h"
#include "rhi/backends/webgpu/webgpu_original_typed_callback_custody.h"
#include "rhi/backends/webgpu/webgpu_original_typed_cold_admission.h"
#include "rhi/backends/webgpu/webgpu_original_typed_cold_command.h"
#include "rhi/backends/webgpu/webgpu_original_typed_dependency_decision.h"
#include "rhi/backends/webgpu/webgpu_original_typed_issuer_preparation.h"
#include "rhi/backends/webgpu/webgpu_original_typed_native_payload.h"
#include "rhi/frontend/command_usage_projection.h"
#include "rhi/frontend/resource_texture_validation.h"
#include "texture_readback_retained_source_fixture.h"
#include "webgpu_error_scope_handoff_fixture.h"
#include "webgpu_native_map_execution_probe_fixture.h"

namespace arp::test {
// A fixed original table workload, not a replacement ownership or content graph.
class OriginalDepthTableContentPlanFixture final {
    static constexpr std::uint64_t bytes = 65536u;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || core::StringView(actual) != expected)
            std::fprintf(stderr,
                "table ContentPlan2 expected %s got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && core::StringView(actual) == expected);
    }

public:
    static inline bool mixedIssuersRequested = false;
    static inline bool mixedRecordingRequested = false;
    static inline bool mixedResultsRequested = false;
    static inline bool mixedCompletionRequested = false;
    static inline bool mixedReadbackLossRequested = false;
    static inline bool mixedReadbackMapLossRequested = false;
    static inline bool geometryCompositionRequested = false;
    static inline bool geometryRecordingCancelRequested = false;
    static inline OriginalVertexGeometryExecutionFixture::LifecycleCase geometryInvocationCase =
        OriginalVertexGeometryExecutionFixture::LifecycleCase::Complete;
    static constexpr char readbackLabel[] = "table-output";
    static constexpr std::size_t readbackLabelBytes = sizeof(readbackLabel) - 1u;

    struct NativeCustody {
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalNativeClosure> closure;
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedDependencyDecision> dependencies;
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedNativePayload> payload;
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedColdAdmission> cold;
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedColdCommand> command;
        std::shared_ptr<core::CpuMemoryQuota> submissionQuota, callbackQuota;
    };

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    // Test observation of the original issuers; no additional command graph,
    // native callback, result writer or completion authority is introduced.
    struct PendingReadbackLoss {
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedCallbackCustody> custody;
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedIssuerPreparation> issuers;
        std::weak_ptr<const rhi_webgpu::WebGpuCanonicalReadbackResultStorage> results;
        std::shared_ptr<const rhi::OriginalCommandReadbackResultSnapshot> snapshot;
        std::shared_ptr<core::CpuMemoryQuota> resultQuota;
        const rhi::OriginalCommandStateTransaction* root = nullptr;
        std::uint64_t submissionBaseline = 0u, callbackBaseline = 0u;
        bool mapInvoked = false;
    };

    static inline core::UniquePtr<PendingReadbackLoss> pendingReadbackLoss;
#endif

    static void verifyUnavailable(const NativeCustody& custody, const char* expected)
    {
        if (!custody.closure)
            return;
        is(custody.closure->validate(), expected);
        is(custody.dependencies->validate(), expected);
        is(custody.payload->validate(), expected);
        if (custody.cold)
            is(custody.cold->validate(), expected);
        if (custody.command)
            is(custody.command->validate(), expected);
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static NativeCustody verifyCustody(OriginalCommandTypedStreamFixture::Retained& retained,
        const rhi::GpuDevice& device,
        bool reserveExecution = false)
    {
        using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
        using Decision = rhi_webgpu::WebGpuOriginalTypedDependencyDecision;
        using P = rhi::common::OriginalTypedDependencyProjection;
        const auto sources =
            retained.graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
        const auto& inventory = sources->sources();
        const char* id = nullptr;
        auto loss = device.observeOriginalDeviceNativeLoss(id);
        assert(loss && !id);
        is(device.validateOriginalTypedNativeLossJoin(*loss),
            "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-LOSS-WEBGPU-JOIN-MISSING");
        assert(!device.validateOriginalTypedNativeLossJoin(*loss, {2u, true}));
        is(device.validateOriginalTypedNativeLossJoin(*loss, {2u, false}),
            "RHI-ORIGINAL-TYPED-LOSS-JOIN-OPT-IN");
        std::uint64_t resourceBytes = 0u;
        for (const auto index : {0u, 2u, 4u, 6u})
            resourceBytes +=
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(inventory[index])
                    ->originalObject()
                    ->allocation()
                    .estimatedSizeBytes;
        assert(resourceBytes == 3056u);
        rhi::OriginalCommandNativeClosureBudget budget{3u,
            true,
            11u,
            13u,
            sources->originalBudget().maxGraphOwners,
            resourceBytes,
            bytes,
            1u,
            1u};
        const auto factory = [&] {
            return Closure::prepareOriginalTableLossBound(
                retained.transaction, retained.program, device, budget, *loss, id);
        };
        auto closure = factory();
        if (!closure)
            std::fprintf(stderr, "table Closure3: %s\n", id ? id : "missing");
        assert(closure && !id && closure->closureContractVersion() == 3u
            && closure->resourceBindingCount() == 11u && closure->pipelineBindingCount() == 1u
            && closure->descriptorBindingCount() == 1u && closure->nativePayloadCount() == 13u
            && closure->retainedResourceBytes() == resourceBytes
            && !closure->nativeExecutionProven());
        is(closure->executionDiagnostic(),
            "RHI-ORIGINAL-NATIVE-CLOSURE-TABLE-WEBGPU-RECORDING-UNPROVEN");
        budget.maxRetainedOwners = closure->retainedOwnerCount();
        closure.reset();
        CpuQuotaFactoryFixture pressure(retained.recordingQuota);
        const auto closureBytes = pressure.measureRetained(factory);
        pressure.verify(closureBytes, factory);
        const auto reject = [&](auto changed, const char* expected) {
            const auto saved = budget;
            budget = changed;
            assert(!factory());
            is(id, expected);
            budget = saved;
        };
        auto bad = budget;
        bad.contractVersion = 2u;
        reject(bad, "RHI-WEBGPU-CANONICAL-CLOSURE-TABLE-VERSION");
        bad = budget;
        bad.prototypeOptIn = false;
        reject(bad, "RHI-ORIGINAL-NATIVE-CLOSURE-OPT-IN");
        bad = budget;
        --bad.maxResourceBindings;
        reject(bad, "RHI-WEBGPU-CANONICAL-CLOSURE-BINDING-CAPACITY");
        bad = budget;
        --bad.maxNativePayloadObjects;
        reject(bad, "RHI-WEBGPU-CANONICAL-CLOSURE-PAYLOAD-CAPACITY");
        bad = budget;
        --bad.maxRetainedOwners;
        reject(bad, "RHI-WEBGPU-CANONICAL-CLOSURE-OWNER-CAPACITY");
        bad = budget;
        --bad.maxRetainedResourceBytes;
        reject(bad, "RHI-WEBGPU-CANONICAL-CLOSURE-ALLOCATION-CAPACITY");
        bad = budget;
        --bad.maxCpuMetadataBytes;
        reject(bad, "RHI-WEBGPU-CANONICAL-CLOSURE-QUOTA");
        bad = budget;
        bad.contractVersion = 2u;
        assert(!Closure::prepareOriginalTypedLossBound(
            retained.transaction, retained.program, device, bad, *loss, id));
        is(id, "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-LOSS-WEBGPU-JOIN-MISSING");
        assert(!Closure::prepareOriginalTyped(
            retained.transaction, retained.program, device, budget, id));
        is(id, "RHI-WEBGPU-CANONICAL-CLOSURE-TYPED-VERSION");
        closure = factory();
        assert(closure && !id);
        const auto& stateBudget = retained.program->originalBudget();
        const P::Request projection{2u,
            true,
            {2u,
                true,
                {2u,
                    true,
                    stateBudget.descriptorConsumption,
                    stateBudget.renderDescriptorUsage,
                    21u,
                    4u,
                    10u,
                    6u,
                    10u}},
            7u,
            25u,
            250u,
            45u,
            13u,
            9u};
        Decision::Request request{2u, true, projection, closure->retainedOwnerCount() + 1u, bytes};
        const auto freeze = [&] {
            return Decision::prepareOriginal(closure, request, id);
        };
        const auto freezeBytes = pressure.measureRetained(freeze);
        pressure.verify(freezeBytes, freeze);
        auto dependencies = freeze();
        if (!dependencies)
            std::fprintf(stderr, "table Freeze2: %s\n", id ? id : "missing");
        assert(dependencies && !id && dependencies->records().size() == 7u
            && dependencies->counts().initial == 4u && dependencies->counts().between == 3u
            && dependencies->counts().textureCells == 7u && !dependencies->synchronizationProven());
        is(dependencies->executionDiagnostic(),
            "RHI-WEBGPU-TYPED-DEPENDENCY-EXECUTOR-PAYLOAD-MISSING");
        const rhi::OriginalTypedNativePayloadRequest oldPayload{1u,
            true,
            {21u, 0u, 4u, 1u, 1u, 1u, 0u, 7u, 0u, 6u, 1u, 1u, 4u, 15u},
            1u,
            dependencies->retainedOwnerCount() + 1u,
            bytes};
        const auto refusedPayload =
            rhi_webgpu::WebGpuOriginalTypedNativePayload::inspectOriginal(dependencies, oldPayload);
        is(refusedPayload.diagnostic, "RHI-WEBGPU-TYPED-PAYLOAD-DEPENDENCY-PROFILE");
        assert(refusedPayload.work.matches(rhi::OriginalTypedNativeColdWorkCounts{})
            && !refusedPayload.colorAttachments);
        auto payload = OriginalTypedNativePayloadFixture::verify(dependencies,
            std::array{sources->originalMetadataQuota(),
                retained.graph->originalInputs()->originalMetadataQuota(),
                retained.stateQuota,
                retained.quota,
                retained.recordingQuota},
            "original-table-content2",
            2u);
        assert(payload->work().matches(oldPayload.maxWork) && payload->passes().size() == 4u);
        for (std::size_t i = 0u; i < 3u; ++i) {
            const auto& pass = payload->passes()[i];
            assert(pass.colors.values().empty() && pass.maxDraw.maxDrawCount == 0u
                && pass.renderPass.depthStencilAttachment == &pass.depth && pass.depth.view
                && pass.depth.depthLoadOp == WGPULoadOp_Clear
                && pass.depth.depthStoreOp == WGPUStoreOp_Store
                && pass.depth.depthClearValue == static_cast<float>(i + 1u) * 0.25f);
        }
        assert(payload->passes()[3u].maxDraw.maxDrawCount == 1u);
        const auto oldCold = OriginalTypedNativeColdContractFixture::request(
            OriginalTypedNativeColdContractFixture::required(true));
        const auto coldRefused =
            rhi_webgpu::WebGpuOriginalTypedColdAdmission::inspectOriginalPayload(payload, oldCold);
        is(coldRefused.diagnostic, "RHI-WEBGPU-TYPED-COLD-PAYLOAD-PROFILE");
        assert(!coldRefused.required.contractVersion && !coldRefused.required.work.commands);
        using Admission = rhi_webgpu::WebGpuOriginalTypedColdAdmission;
        using Command = rhi_webgpu::WebGpuOriginalTypedColdCommand;
        using Seal = rhi::OriginalCommandRecordingSeal;
        using Reservation = rhi::OriginalCommandLifecycleReservation;
        using Root = rhi::OriginalCommandStateTransaction;
        auto coldRequest = oldCold;
        coldRequest.contractVersion = 2u;
        coldRequest.maxWork = payload->work();
        coldRequest.maxCallbacks = Admission::lifecycleRequirements(2u).callbacks;
        coldRequest.maxRetainedOwners = payload->retainedOwnerCount() + Admission::additionalOwners;
        const auto coldOwnerCount = coldRequest.maxRetainedOwners;
        if (reserveExecution)
            coldRequest.maxRetainedOwners +=
                rhi::OriginalTypedNativeColdCommandContract::additionalOwners
                + rhi::OriginalTypedCallbackCustodyContract::additionalOwners({2u,
                    true,
                    rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
                    1u,
                    8u,
                    1u,
                    1u,
                    bytes,
                    bytes})
                + rhi::OriginalTypedIssuerPreparationContract::additionalOwners;
        coldRequest.maxCpuMetadataBytes = bytes;
        const auto preclaim = Admission::inspectOriginalPayload(payload, coldRequest);
        if (preclaim.diagnostic)
            std::fprintf(stderr, "table Cold2 preclaim: %s\n", preclaim.diagnostic);
        assert(!preclaim.diagnostic && preclaim.required.contractVersion == 2u
            && preclaim.required.work.matches(payload->work()) && preclaim.required.encoders == 1u
            && preclaim.required.commandBuffers == 1u
            && preclaim.required.capacity.callbackTickets == 8u);
        const auto rejectCold = [&](auto changed, const char* expected) {
            const auto result = Admission::inspectOriginalPayload(payload, changed);
            is(result.diagnostic, expected);
            assert(!result.required.contractVersion && !result.required.work.commands);
        };
        auto invalidCold = coldRequest;
        invalidCold.prototypeOptIn = false;
        rejectCold(invalidCold, "RHI-ORIGINAL-TYPED-COLD-OPT-IN");
        invalidCold = coldRequest;
        invalidCold.maxRetainedOwners = coldOwnerCount - 1u;
        rejectCold(invalidCold, "RHI-ORIGINAL-TYPED-COLD-OWNER-CPU-CAPACITY");
        invalidCold = coldRequest;
        --invalidCold.maxWork.nativeCalls;
        rejectCold(invalidCold, "RHI-ORIGINAL-TYPED-COLD-WORK-CAPACITY");
        invalidCold = coldRequest;
        --invalidCold.maxCallbacks.recordingScopes;
        rejectCold(invalidCold, "RHI-ORIGINAL-TYPED-COLD-CALLBACK-CAPACITY");
        const auto& authored = sources->originalBudget();
        for (const auto member :
            std::array{&rhi::OriginalCommandAdmissionBudget::maxNativePayloadObjects,
                &rhi::OriginalCommandAdmissionBudget::maxCallbackTickets,
                &rhi::OriginalCommandAdmissionBudget::maxCallbackRegistrations}) {
            auto shortSource = authored;
            --(shortSource.*member);
            assert(rhi::OriginalTypedNativeColdContract::validateSourceAuthority(
                preclaim.required, 13u, shortSource));
        }
        {
            auto shortSource = authored;
            --shortSource.maxTypedNativeCommandCalls;
            is(rhi::OriginalTypedNativeColdContract::validateSourceAuthority(
                   preclaim.required, 13u, shortSource),
                "RHI-ORIGINAL-TYPED-COLD-SOURCE-NATIVE-CALL-CAPACITY");
        }
        const rhi::OriginalTypedNativeColdCommandRequest commandRequest{2u,
            true,
            1u,
            reserveExecution ? coldRequest.maxRetainedOwners
                             : coldRequest.maxRetainedOwners
                    + rhi::OriginalTypedNativeColdCommandContract::additionalOwners,
            bytes};
        const auto commandOwnerCount =
            coldOwnerCount + rhi::OriginalTypedNativeColdCommandContract::additionalOwners;
        auto submission =
            core::makeShared<core::CpuMemoryQuota>(authored.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Submission)]);
        auto callback =
            core::makeShared<core::CpuMemoryQuota>(authored.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Callback)]);
        const rhi::OriginalCommandNativeLifecycleBudget lifecycle{1u,
            true,
            1u,
            8u,
            8u,
            commandRequest.maxRetainedOwners,
            commandRequest.maxRetainedOwners,
            commandRequest.maxRetainedOwners,
            bytes,
            submission->limitBytes(),
            callback->limitBytes()};
        std::shared_ptr<const Seal> seal;
        std::shared_ptr<const Reservation> reservation;
        std::shared_ptr<const Admission> cold;
        const auto bindCold = [&] {
            seal = Seal::prepareOriginal(
                retained.transaction, device, lifecycle, submission, callback, id);
            if (!seal)
                std::fprintf(stderr, "table Cold2 seal: %s\n", id ? id : "missing");
            assert(seal && !id);
            reservation = Reservation::prepareOriginal(seal, preclaim.required.capacity, id);
            assert(reservation && !id);
            cold = Admission::prepareOriginal(payload, reservation, coldRequest, id);
            assert(cold && !id);
        };
        bindCold();
        const auto admit = [&] {
            return Admission::prepareOriginal(payload, reservation, coldRequest, id);
        };
        // Publication smoke retains the complete Cold2 quota/rollback matrix.
        // Execution smoke needs its authentic positive chain before Execution2,
        // and reserves its remaining bounded runtime for mixed Cold3 coverage.
        const auto admissionBytes = reserveExecution ? 0u : pressure.measureRetained(admit);
        if (!reserveExecution)
            pressure.verify(admissionBytes, admit);
        auto wrongCommand = commandRequest;
        wrongCommand.contractVersion = 1u;
        assert(!Command::prepareOriginal(cold, wrongCommand, id));
        is(id, "RHI-ORIGINAL-TYPED-COLD-COMMAND-ADMISSION-PROFILE");
        const auto payloadRequest = payload->originalRequest();
        const auto originalInputs = retained.graph->originalInputs();
        std::uint32_t graphOwners = 0u, graphRegistrations = 0u;
        assert(!rhi::OriginalCommandResourceSources::registeredOwnerUnion(sources,
            originalInputs->retainedOwnerCount() + 1u,
            UINT32_MAX,
            UINT32_MAX,
            graphOwners,
            graphRegistrations));
        const rhi::OriginalCommandStateAdmissionBudget rootBudget{3u,
            true,
            1u,
            graphRegistrations,
            17u,
            17u,
            graphOwners + 6u,
            bytes,
            stateBudget.descriptorConsumption,
            stateBudget.renderDescriptorUsage};
        const rhi::OriginalTextureContentPlanBudget contentBudget{
            2u, true, 4u, 0u, 28u, 11u, 609u, 20000u};
        const auto rebuild = [&] {
            cold.reset();
            reservation.reset();
            seal.reset();
            assert(retained.transaction->status() == Root::Status::Abandoned
                && retained.transaction->journalEmpty());
            payload.reset();
            dependencies.reset();
            closure.reset();
            retained.transaction.reset();
            assert(!sources->originalStateTransactionCount()
                && !sources->originalReservedSubmissionCount());
            retained.transaction = Root::prepareOriginalOwnership(
                retained.graph, device, rootBudget, retained.recordingQuota, id);
            assert(retained.transaction && !id);
            auto exactContent = contentBudget;
            exactContent.maxProjectionVisits =
                retained.transaction->preflightOriginalContentCells(exactContent).projectionVisits;
            assert(!retained.transaction->materializeOriginalPrototypePlan(
                stateBudget.stateSequence, exactContent));
            closure = factory();
            assert(closure && !id);
            dependencies = freeze();
            assert(dependencies && !id);
            payload = rhi_webgpu::WebGpuOriginalTypedNativePayload::prepareOriginal(
                dependencies, payloadRequest, id);
            assert(payload && !id);
            bindCold();
            assert(core::SharedOwnerProjection::sameOwner(
                       retained.graph->originalInputs(), originalInputs)
                && core::SharedOwnerProjection::sameOwner(
                    closure->originalStream(), retained.program));
        };
        const auto connect = [&] {
            return Command::prepareOriginal(cold, commandRequest, id);
        };
        const auto commandBytes = reserveExecution ? 0u : pressure.measureRetained(connect);
        if (!reserveExecution)
            pressure.verifySingleUse(commandBytes, rebuild, connect);
        {
            auto shortRequest = commandRequest;
            shortRequest.maxRetainedOwners = commandOwnerCount - 1u;
            assert(!Command::prepareOriginal(cold, shortRequest, id));
            is(id, "RHI-ORIGINAL-TYPED-COLD-COMMAND-OWNER-CAPACITY");
        }
        {
            auto shortRequest = commandRequest;
            --shortRequest.maxCpuMetadataBytes;
            assert(!Command::prepareOriginal(cold, shortRequest, id));
            is(id, "RHI-WEBGPU-TYPED-COLD-COMMAND-QUOTA");
        }
        {
            auto forged = std::shared_ptr<const Admission>(callback, cold.get());
            assert(!Command::prepareOriginal(forged, commandRequest, id));
            is(id, "RHI-WEBGPU-TYPED-COLD-OWNER");
        }
        // The exhausted probe leaves the same genuine seal unclaimed.
        auto command = connect();
        if (!command)
            std::fprintf(stderr, "table Command2: %s\n", id ? id : "missing");
        assert(command && !id && !command->validate()
            && command->retainedOwnerCount() == commandOwnerCount);
        assert(!connect());
        is(id, "RHI-WEBGPU-TYPED-COLD-COMMAND-DUPLICATE");
        auto peer = admit();
        assert(peer && !id);
        assert(!Command::prepareOriginal(peer, commandRequest, id));
        is(id, "RHI-WEBGPU-TYPED-COLD-COMMAND-DUPLICATE");
        peer.reset();
        const rhi::OriginalTypedCallbackCustodyRequest oldCallback{1u,
            true,
            rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
            1u,
            8u,
            1u,
            commandRequest.maxRetainedOwners,
            bytes,
            bytes};
        assert(!rhi_webgpu::WebGpuOriginalTypedCallbackCustody::prepareOriginal(
            command, oldCallback, id));
        is(id, "RHI-TYPED-CALLBACK-CUSTODY-ORIGINAL-SUBSCRIPTION");
        assert(retained.transaction->applyOriginalPrototypePlan());
        std::weak_ptr<const Admission> retainedCold = cold;
        cold.reset();
        reservation.reset();
        seal.reset();
        assert(!retainedCold.expired() && !command->validate()
            && sources->originalReservedCallbackTicketCount() == 8u);
        cold = command->originalAdmission();
        auto old = request;
        old.contractVersion = 1u;
        assert(!Decision::prepareOriginal(closure, old, id));
        is(id, "RHI-ORIGINAL-DEPENDENCY-FREEZE-PROFILE");
        old = request;
        --old.maxRetainedOwners;
        assert(!Decision::prepareOriginal(closure, old, id));
        is(id, "RHI-WEBGPU-TYPED-DEPENDENCY-OWNER-CAPACITY");
        old = request;
        --old.projection.maxDependencies;
        assert(!Decision::prepareOriginal(closure, old, id));
        is(id, "RHI-ORIGINAL-DEPENDENCY-PROJECTION-DEPENDENCY-CAPACITY");
        const auto table =
            std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorTableSource>>(
                inventory[12u]);
        const auto pipeline =
            std::get<std::shared_ptr<const rhi::OriginalCommandGraphicsPipelineSource>>(
                inventory[11u]);
        auto native = rhi_webgpu::webGpuNativePayloadFor(*table->originalObject());
        auto pipelineNative = rhi_webgpu::webGpuNativePayloadFor(*pipeline->originalObject());
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sources->originalMetadataQuota(),
                    retained.graph->originalInputs()->originalMetadataQuota(),
                    retained.stateQuota,
                    retained.quota,
                    retained.recordingQuota,
                    submission,
                    callback},
                [&]() noexcept -> const char* {
                    if (const auto* diagnostic = closure->validate())
                        return diagnostic;
                    if (const auto* diagnostic = dependencies->validate())
                        return diagnostic;
                    auto forged =
                        std::shared_ptr<const Closure>(retained.recordingQuota, closure.get());
                    assert(Closure::validateOriginal(forged));
                    auto forgedDecision = std::shared_ptr<const Decision>(
                        retained.recordingQuota, dependencies.get());
                    assert(Decision::validateOriginal(forgedDecision));
                    auto forgedCommand = std::shared_ptr<const Command>(callback, command.get());
                    is(Command::validateOriginal(forgedCommand),
                        "RHI-WEBGPU-TYPED-COLD-COMMAND-OWNER");
                    const auto group = native->group;
                    native->group = nullptr;
                    assert(closure->validate() && dependencies->validate() && payload->validate()
                        && cold->validate() && command->validate() && !factory());
                    native->group = group;
                    const auto layout = native->layout;
                    native->layout = nullptr;
                    assert(closure->validate() && dependencies->validate() && payload->validate()
                        && cold->validate() && command->validate() && !factory());
                    native->layout = layout;
                    const auto view = native->bindings[1u].textureView;
                    native->bindings[1u].textureView = native->bindings[0u].textureView;
                    assert(closure->validate() && dependencies->validate() && payload->validate()
                        && cold->validate() && command->validate() && !factory());
                    native->bindings[1u].textureView = view;
                    const auto pipelineObject = pipelineNative->pipeline;
                    pipelineNative->pipeline = nullptr;
                    assert(closure->validate() && dependencies->validate() && payload->validate()
                        && cold->validate() && command->validate() && !factory());
                    pipelineNative->pipeline = pipelineObject;
                    auto forgedStream = std::shared_ptr<const rhi::OriginalCommandStreamProgram>(
                        retained.quota, retained.program.get());
                    assert(!Closure::prepareOriginalTableLossBound(
                        retained.transaction, forgedStream, device, budget, *loss, id));
                    auto forgedRoot = std::shared_ptr<rhi::OriginalCommandStateTransaction>(
                        retained.recordingQuota, retained.transaction.get());
                    assert(!Closure::prepareOriginalTableLossBound(
                        forgedRoot, retained.program, device, budget, *loss, id));
                    if (const auto* diagnostic = closure->validate())
                        return diagnostic;
                    if (const auto* diagnostic = dependencies->validate())
                        return diagnostic;
                    if (const auto* diagnostic = payload->validate())
                        return diagnostic;
                    if (const auto* diagnostic = cold->validate())
                        return diagnostic;
                    return command->validate();
                }));
        });
        std::printf(
            "ORIGINAL_TABLE_NATIVE_CUSTODY_OK Graphics closure3 freeze2 resourceBindings11 pipelines1 tables1 payloads13 backing3056 owners%u closureBytes%llu freezeBytes%llu dependencies7 initial4 between3 same-original-content-root quota-exact-less hot0 query0 native-execution-closed\n",
            closure->retainedOwnerCount(),
            static_cast<unsigned long long>(closureBytes),
            static_cast<unsigned long long>(freezeBytes));
        std::printf(
            "ORIGINAL_TABLE_NATIVE_PAYLOAD2_OK Graphics commands21 calls15 render4 tables1 dependencies7 depth-clears3 same-original-content-root cold1-rejected native-execution-closed\n");
        std::printf(
            "ORIGINAL_TABLE_NATIVE_COLD2_OK Graphics cold2 command2 closure3 freeze2 payload2 source15 calls15 callbacks8 owners%u admissionBytes%llu commandBytes%llu quota-pressure=%s sticky-claim forged-native-mutation caller-drop hot0 query0 callback1-rejected same-original-Inputs4-State3-Stream3 pre-record-execution-closed\n",
            command->retainedOwnerCount(),
            static_cast<unsigned long long>(admissionBytes),
            static_cast<unsigned long long>(commandBytes),
            reserveExecution ? "covered-by-publication-smoke" : "exact-less-exhausted");
        return {std::move(closure),
            std::move(dependencies),
            std::move(payload),
            std::move(cold),
            std::move(command),
            std::move(submission),
            std::move(callback)};
    }

    static void verifyExecution(
        NativeCustody& native, OriginalCommandTypedStreamFixture::Retained& retained)
    {
        using Custody = rhi_webgpu::WebGpuOriginalTypedCallbackCustody;
        using Issuers = rhi_webgpu::WebGpuOriginalTypedIssuerPreparation;
        using Root = rhi::OriginalCommandStateTransaction;
        using Scopes = WebGpuErrorScopeHandoffFixture<31u>;
        using Content = rhi::common::OriginalTextureContentControl;
        constexpr rhi::OriginalTypedNativeExecutionRequest execution{
            2u, true, rhi::OriginalTypedSynchronizationPolicy::FrozenOriginalScopeVisibility1};
        const auto& source =
            retained.graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
        const auto& inventory = source->sources();
        for (const auto index : {1u, 3u, 5u})
            CompletedDepthTextureSourceFixture::verify(
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                    inventory[index]),
                false);
        const auto owners = native.command->originalRequest().maxRetainedOwners;
        const auto submissionBytes = native.submissionQuota->limitBytes(),
                   callbackBytes = native.callbackQuota->limitBytes();
        const char* id = nullptr;
        const rhi::OriginalTypedCallbackCustodyRequest callback{2u,
            true,
            rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
            1u,
            8u,
            1u,
            owners,
            submissionBytes,
            callbackBytes};
        auto custody = Custody::prepareOriginal(native.command, callback, id);
        if (!custody)
            std::fprintf(stderr, "table Callback2: %s\n", id ? id : "missing");
        assert(custody && !id);
        const rhi::OriginalTypedIssuerPreparationRequest request{2u,
            true,
            1u,
            1u,
            7u,
            owners,
            retained.recordingQuota->limitBytes(),
            submissionBytes,
            callbackBytes};
        auto wrong = request;
        wrong.contractVersion = 1u;
        assert(!Issuers::prepareOriginal(custody, wrong, id));
        is(id, "RHI-TYPED-ISSUER-PREPARATION-CALLBACK-PROFILE");
        auto issuers = Issuers::prepareOriginal(custody, request, id);
        if (!issuers)
            std::fprintf(stderr, "table Issuer2: %s\n", id ? id : "missing");
        assert(issuers && !id && issuers->issuersClosedForInspection());
        auto wrongExecution = execution;
        wrongExecution.contractVersion = 1u;
        is(Issuers::recordOriginal(issuers, wrongExecution),
            "RHI-TYPED-NATIVE-EXECUTION-ISSUER-PROFILE");
        is(Issuers::submitOriginal(issuers, wrongExecution),
            "RHI-TYPED-NATIVE-EXECUTION-ISSUER-PROFILE");
        const auto quotas =
            std::array{retained.recordingQuota, native.submissionQuota, native.callbackQuota};
        Scopes::holdScopes();
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            id = CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                return Issuers::recordOriginal(issuers, execution);
            });
        });
        if (id)
            std::fprintf(stderr, "table Execution2 record: %s\n", id);
        assert(!id);
        Scopes::restoreScopes();
        Scopes::waitHeldScopes();
        assert(retained.transaction->status() == Root::Status::Pending
            && !retained.transaction->journalEmpty() && !issuers->hasOriginalRecordedCommand()
            && !issuers->hasOriginalCompletedSubmission());
        for (const auto index : {0u, 2u, 4u, 6u}) {
            const auto* texture =
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(inventory[index])
                    ->originalObject();
            is(texture->originalContentDiagnostic(), "RHI-CANONICAL-CONTENT-CONTROL-PENDING");
        }
        Scopes::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return issuers->hasOriginalRecordedCommand();
        }));
        const auto table =
            std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorTableSource>>(
                inventory[12u]);
        const auto payload = rhi_webgpu::webGpuNativePayloadFor(*table->originalObject());
        const auto group = payload->group;
        payload->group = nullptr;
        assert(Issuers::submitOriginal(issuers, execution));
        payload->group = group;
        assert(retained.transaction->status() == Root::Status::Pending
            && !issuers->hasOriginalCompletedSubmission());
        Scopes::holdScopes();
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            id = CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                return Issuers::submitOriginal(issuers, execution);
            });
        });
        if (id)
            std::fprintf(stderr, "table Execution2 submit: %s\n", id);
        assert(!id);
        Scopes::restoreScopes();
        Scopes::waitHeldScopes();
        assert(retained.transaction->status() == Root::Status::Pending
            && !issuers->hasOriginalCompletedSubmission());
        // Genuine scopes may arrive partially; neither CPU observation nor queue
        // completion alone can publish the exact original cell-content journal.
        Scopes::releaseScope(0u);
        Scopes::releaseScope(1u);
        assert(retained.transaction->status() == Root::Status::Pending
            && !issuers->hasOriginalCompletedSubmission());
        Scopes::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return issuers->hasOriginalCompletedSubmission();
        }));
        assert(retained.transaction->status() == Root::Status::Committed
            && retained.transaction->journalEmpty() && !retained.transaction->validate()
            && !custody->pendingOriginalRecords() && !custody->physicalLossArmed());
        for (std::uint32_t element = 0u; element < 3u; ++element) {
            const auto* texture = std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                inventory[2u * element])
                                      ->originalObject();
            for (std::uint32_t mip = 0u; mip < 3u; ++mip)
                for (std::uint32_t layer = 0u; layer < 3u; ++layer) {
                    const auto observation = texture->observeOriginalContent(
                        {mip, 1u, layer, 1u, rhi::TextureAspect::Depth});
                    assert(observation && !observation->diagnostic());
                    const bool stored = mip == element && layer == element;
                    assert(observation->state()
                            == (stored ? Content::State::Completed : Content::State::Uninitialized)
                        && bool(observation->contentGeneration()) == stored);
                }
        }
        for (const auto index : {1u, 3u, 5u})
            CompletedDepthTextureSourceFixture::verify(
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                    inventory[index]),
                true);
        const auto* color =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(inventory[6u])
                ->originalObject();
        const auto observedColor =
            color->observeOriginalContent({0u, 1u, 0u, 1u, rhi::TextureAspect::Color});
        assert(observedColor && !observedColor->diagnostic()
            && observedColor->state() == Content::State::Completed);
        TextureReadbackRetainedSourceFixture::verifyOutputOwner(
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(inventory[6u]));
        std::puts(
            "ORIGINAL_TABLE_NATIVE_EXECUTION2_OK Graphics callback2 issuer2 execution2 original-table3 ClearStore3 draw1 scopes3-3 queue1 same-journal-Pending-commit cells28 untouched24 native-mutation-refused mixed-profiles-refused hot0 query0 public-closed readback-unproven");
    }

    static void verifyReadbackExecution(NativeCustody& native,
        OriginalCommandTypedStreamFixture::Retained& retained,
        const rhi::GpuDevice& device)
    {
        using Custody = rhi_webgpu::WebGpuOriginalTypedCallbackCustody;
        using Issuers = rhi_webgpu::WebGpuOriginalTypedIssuerPreparation;
        using Storage = rhi_webgpu::WebGpuCanonicalReadbackResultStorage;
        using Root = rhi::OriginalCommandStateTransaction;
        using Scopes = WebGpuErrorScopeHandoffFixture<32u>;
        using MapScopes = WebGpuErrorScopeHandoffFixture<33u>;
        using Content = rhi::common::OriginalTextureContentControl;
        const char* id = nullptr;
        const auto owners = native.command->originalRequest().maxRetainedOwners;
        auto resultQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto results = Storage::prepareOriginalTyped(
            native.cold, device, {2u, true, 1u, 512u, bytes, owners}, resultQuota, id);
        if (!results)
            std::fprintf(stderr, "table readback Result2: %s\n", id ? id : "missing");
        assert(results && !id && results->snapshots().size() == 1u);
        const auto submissionBaseline = native.submissionQuota->liveBytes();
        const auto callbackBaseline = native.callbackQuota->liveBytes();
        const rhi::OriginalTypedCallbackCustodyRequest callback{3u,
            true,
            rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
            1u,
            12u,
            1u,
            owners,
            native.submissionQuota->limitBytes(),
            native.callbackQuota->limitBytes(),
            {1u, true}};
        assert(!Custody::prepareOriginal(native.command,
            callback,
            id,
            results,
            rhi::OriginalCommandReadbackSnapshotPublicationContract::Intent{1u, false}));
        is(id, "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-OPT-IN");
        auto custody = Custody::prepareOriginal(native.command,
            callback,
            id,
            results,
            rhi::OriginalCommandReadbackSnapshotPublicationContract::Intent{1u, true});
        assert(custody && !id);
        auto issuers = Issuers::prepareOriginal(custody,
            {3u,
                true,
                1u,
                1u,
                7u,
                owners,
                retained.recordingQuota->limitBytes(),
                native.submissionQuota->limitBytes(),
                native.callbackQuota->limitBytes()},
            id);
        if (!issuers)
            std::fprintf(stderr, "table readback Issuer3: %s\n", id ? id : "missing");
        assert(issuers && !id);
        const rhi::OriginalTypedNativeExecutionRequest execution{3u,
            true,
            rhi::OriginalTypedSynchronizationPolicy::FrozenOriginalScopeVisibility1,
            {1u, true}};
        auto wrong = execution;
        wrong.readbackCompletion = {};
        is(Issuers::recordOriginal(issuers, wrong),
            "RHI-TYPED-NATIVE-READBACK-COMPLETION-REQUIRED");
        wrong = execution;
        wrong.readbackCompletion.prototypeOptIn = false;
        is(Issuers::recordOriginal(issuers, wrong), "RHI-TYPED-NATIVE-READBACK-COMPLETION-OPT-IN");
        assert(retained.transaction->status() == Root::Status::Prepared
            && retained.transaction->journalEmpty());
        const auto quotas = std::array{
            retained.recordingQuota, native.submissionQuota, native.callbackQuota, resultQuota};
        Scopes::holdScopes();
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            id = CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                return Issuers::recordOriginal(issuers, execution);
            });
        });
        if (id)
            std::fprintf(stderr, "table readback record: %s\n", id);
        assert(!id);
        Scopes::restoreScopes();
        Scopes::waitHeldScopes();
        assert(retained.transaction->status() == Root::Status::Pending
            && !retained.transaction->journalEmpty() && !results->snapshots()[0]->publishedBytes()
            && native.cold->validate());
        is(retained.transaction->commitOriginalPendingOwnership(),
            "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
        auto forged = std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedColdAdmission>(
            resultQuota, native.cold.get());
        is(rhi_webgpu::WebGpuNativeCommandPreparation::validateTypedAdmissionRetained(forged),
            "RHI-WEBGPU-TYPED-COLD-OWNER");
        forged.reset();
        assert(!rhi_webgpu::WebGpuNativeCommandPreparation::validateTypedAdmissionRetained(
            native.cold));
        Scopes::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return issuers->hasOriginalRecordedCommand();
        }));
        wrong = execution;
        wrong.readbackCompletion = {};
        assert(Issuers::submitOriginal(issuers, wrong));
        const auto& destination = native.closure->originalReadbackJoins()
                                      ->originalProgram()
                                      ->originalInputs()
                                      ->originalScalars()
                                      ->originalUsage()
                                      ->originalSources()
                                      ->sources()[11u];
        const auto destinationLease =
            std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(destination);
        const auto payload =
            rhi_webgpu::webGpuNativePayloadFor(*destinationLease->originalObject());
        const auto savedBuffer = payload->buffer;
        payload->buffer = nullptr;
        assert(Issuers::submitOriginal(issuers, execution));
        payload->buffer = savedBuffer;
        assert(retained.transaction->status() == Root::Status::Pending
            && !issuers->hasOriginalCompletedSubmission());
        auto weakResult = std::weak_ptr<const Storage>(results);
        auto snapshot = results->snapshots()[0];
        if (mixedReadbackLossRequested) {
            assert(!pendingReadbackLoss);
            pendingReadbackLoss = core::makeUnique<PendingReadbackLoss>();
            auto& pending = *pendingReadbackLoss;
            pending.custody = custody;
            pending.issuers = issuers;
            pending.results = weakResult;
            pending.snapshot = snapshot;
            pending.resultQuota = resultQuota;
            pending.root = retained.transaction.get();
            pending.submissionBaseline = submissionBaseline;
            pending.callbackBaseline = callbackBaseline;
            if (mixedReadbackMapLossRequested)
                WebGpuNativeMapExecutionProbeFixture::instrument(
                    static_cast<WGPUDevice>(native.closure->nativeDevice()),
                    payload->buffer,
                    WebGpuNativeMapExecutionProbeFixture::Probe::PendingLoss,
                    &pending.mapInvoked,
                    [](void* value) noexcept {
                        *static_cast<bool*>(value) = true;
                        // Reuse genuine map-scope promise custody. Native unmap
                        // may synchronously cancel/drain the C map future on loss;
                        // undelivered scope callbacks must still retain this root.
                        MapScopes::holdScopes();
                    });
        }
        results.reset();
        custody.reset();
        assert(!weakResult.expired());
        Scopes::holdScopes();
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            id = CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                return Issuers::submitOriginal(issuers, execution);
            });
        });
        if (id)
            std::fprintf(stderr, "table readback submit: %s\n", id);
        assert(!id);
        Scopes::restoreScopes();
        Scopes::waitHeldScopes();
        assert(
            retained.transaction->status() == Root::Status::Pending && !snapshot->publishedBytes());
        if (mixedReadbackLossRequested) {
            if (mixedReadbackMapLossRequested) {
                Scopes::releaseScopes();
                assert(WebGpuAsyncWaitFixture::until([] {
                    return EM_ASM_INT({
                        // clang-format off
                            const p = Module.canonicalMapProbe;
                            return !!(p && p.pendingResolve);
                        // clang-format on
                    });
                }));
                assert(pendingReadbackLoss->mapInvoked && !issuers->hasOriginalCompletedSubmission()
                    && retained.transaction->status() == Root::Status::Pending
                    && !snapshot->publishedBytes());
                MapScopes::waitHeldScopes();
                MapScopes::restoreScopes();
                WebGpuNativeMapExecutionProbeFixture::restore(true);
            }
            assert(!weakResult.expired()
                && pendingReadbackLoss->custody->pendingOriginalRecords() == 1u);
            std::printf(
                "ORIGINAL_TABLE_READBACK_LOSS1_ARMED Graphics same-original-Cold3-Callback3-Result2-Join2 Execution3 completion1 queue1 %s original-owners-retained publication0 public-closed\n",
                mixedReadbackMapLossRequested ? "actual-map1-C-callback-held map-scopes3-held"
                                              : "submission-scopes3-held map0");
            return;
        }
        Scopes::releaseScope(0u);
        Scopes::releaseScope(1u);
        assert(!issuers->hasOriginalCompletedSubmission() && !snapshot->publishedBytes());
        Scopes::releaseScopes();
        const bool completed = WebGpuAsyncWaitFixture::until([&] {
            return issuers->hasOriginalCompletedSubmission();
        });
        if (!completed)
            std::fprintf(
                stderr, "table readback completion: %s\n", issuers->submissionDiagnostic());
        assert(completed && retained.transaction->status() == Root::Status::Committed
            && retained.transaction->journalEmpty() && !retained.transaction->validate());
        const auto* output = snapshot->publishedBytes();
        assert(output && snapshot->byteCapacity() == 512u);
        for (std::size_t y = 0u; y < 2u; ++y)
            for (std::size_t x = 0u; x < 4u; ++x)
                for (std::size_t channel = 0u; channel < 4u; ++channel) {
                    std::uint16_t half;
                    std::memcpy(&half, output + y * 256u + x * 8u + channel * 2u, sizeof(half));
                    const float expected = channel == 3u ? 1.0f : float(x % 3u + 1u) * 0.25f;
                    assert(core::halfToFloat(half) == expected);
                }
        const auto& inventory = retained.graph->originalInputs()
                                    ->originalScalars()
                                    ->originalUsage()
                                    ->originalSources()
                                    ->sources();
        for (std::uint32_t element = 0u; element < 3u; ++element) {
            const auto* texture = std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                inventory[2u * element])
                                      ->originalObject();
            for (std::uint32_t mip = 0u; mip < 3u; ++mip)
                for (std::uint32_t layer = 0u; layer < 3u; ++layer) {
                    const auto observed = texture->observeOriginalContent(
                        {mip, 1u, layer, 1u, rhi::TextureAspect::Depth});
                    const bool written = mip == element && layer == element;
                    assert(observed && !observed->diagnostic()
                        && observed->state()
                            == (written ? Content::State::Completed : Content::State::Uninitialized)
                        && bool(observed->contentGeneration()) == written);
                }
        }
        issuers.reset();
        native.command.reset();
        assert(weakResult.expired() && native.submissionQuota->liveBytes() == submissionBaseline
            && native.callbackQuota->liveBytes() == callbackBaseline);
        native.cold.reset();
        assert(weakResult.expired() && !native.submissionQuota->liveBytes()
            && !native.callbackQuota->liveBytes());
        snapshot.reset();
        assert(!resultQuota->liveBytes());
        std::puts(
            "ORIGINAL_TABLE_READBACK_COMPLETION1_OK Graphics same-original-Cold3-Callback3-Result2-Join2 Execution3 completion1 queue1 scopes3 map1-scope3 native-unmap1 numeric4x2-rgb-quarter-half-threequarters-quarter-alpha1 cells28-untouched24 same-journal-commit independent-publication1 caller-drop retained-release hot0 query0 encoder1 copy1 submit1 public-closed");
    }

    static void verifyReadbackLoss(
        NativeCustody& native, OriginalCommandTypedStreamFixture::Retained& retained)
    {
        using Root = rhi::OriginalCommandStateTransaction;
        using Scopes = WebGpuErrorScopeHandoffFixture<32u>;
        using MapScopes = WebGpuErrorScopeHandoffFixture<33u>;
        using Issuers = rhi_webgpu::WebGpuOriginalTypedIssuerPreparation;
        assert(pendingReadbackLoss && pendingReadbackLoss->root == retained.transaction.get());
        auto& pending = *pendingReadbackLoss;
        assert(WebGpuAsyncWaitFixture::until([&] {
            return pending.custody->physicalLossDelivered();
        }));
        std::printf(
            "ORIGINAL_TABLE_READBACK_LOSS1_OBSERVED Graphics pending%u resultAlive%u completed%u invalidated%u published%u\n",
            pending.custody->pendingOriginalRecords(),
            unsigned(!pending.results.expired()),
            unsigned(pending.issuers->hasOriginalCompletedSubmission()),
            unsigned(retained.transaction->status() == Root::Status::Invalidated),
            unsigned(bool(pending.snapshot->publishedBytes())));
        assert(pending.custody->pendingOriginalRecords() == 1u);
        assert(!pending.results.expired());
        assert(!pending.issuers->hasOriginalCompletedSubmission());
        assert(retained.transaction->status() == Root::Status::Invalidated);
        assert(!pending.snapshot->publishedBytes());
        const rhi::OriginalTypedNativeExecutionRequest execution{3u,
            true,
            rhi::OriginalTypedSynchronizationPolicy::FrozenOriginalScopeVisibility1,
            {1u, true}};
        assert(Issuers::submitOriginal(pending.issuers, execution));
        if (mixedReadbackMapLossRequested) {
            WebGpuNativeMapExecutionProbeFixture::retirePendingAfterLoss();
            MapScopes::releaseScope(0u);
            MapScopes::releaseScope(1u);
            // Let genuine promise deliveries run; the third native scope still
            // holds foreign userdata, so neither observation can retire the root.
            emscripten_sleep(0u);
            assert(pending.custody->pendingOriginalRecords() == 1u && !pending.results.expired()
                && !pending.issuers->hasOriginalCompletedSubmission()
                && !pending.snapshot->publishedBytes());
            std::puts(
                "ORIGINAL_TABLE_READBACK_MAP_LOSS1_PARTIAL_DRAIN_OK native-map-cancel-unmap scope2-released scope1-held same-root-retained unpublished");
            MapScopes::releaseScopes();
        } else {
            Scopes::releaseScope(0u);
            Scopes::releaseScope(1u);
            assert(pending.custody->pendingOriginalRecords() == 1u
                && !pending.snapshot->publishedBytes());
            Scopes::releaseScopes();
        }
        assert(WebGpuAsyncWaitFixture::until([&] {
            return !pending.custody->pendingOriginalRecords();
        }));
        assert(!pending.issuers->hasOriginalCompletedSubmission()
            && !pending.snapshot->publishedBytes()
            && retained.transaction->status() == Root::Status::Invalidated
            && retained.transaction->journalEmpty());
        const auto quotas = std::array{retained.recordingQuota,
            native.submissionQuota,
            native.callbackQuota,
            pending.resultQuota};
        assert(
            !CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept -> const char* {
                pending.issuers.reset();
                pending.custody.reset();
                native.command.reset();
                return nullptr;
            }));
        assert(pending.results.expired()
            && native.submissionQuota->liveBytes() == pending.submissionBaseline
            && native.callbackQuota->liveBytes() == pending.callbackBaseline);
        pending.results.reset();
        native.cold.reset();
        pending.snapshot.reset();
        assert(!native.submissionQuota->liveBytes() && !native.callbackQuota->liveBytes()
            && !pending.resultQuota->liveBytes());
        pendingReadbackLoss.reset();
        std::printf(
            "ORIGINAL_TABLE_READBACK_%s_LOSS1_OK Graphics same-original-Cold3-Callback3-Result2-Join2 Execution3 completion1 genuine-device-loss invalidated-before-drain foreign-callback-retention authentic-drain publication0 completion0 caller-drop result-submission-callback-quotas-zero hot-release0 encoder1 copy1 submit1 public-closed\n",
            mixedReadbackMapLossRequested ? "MAP" : "SUBMITTED");
    }
#endif
    static OriginalCommandTypedStreamFixture::Retained verify(
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
        const rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& inputQuota,
        bool retainedReadback = false,
        NativeCustody* deferredCompletion = nullptr,
        const std::array<std::shared_ptr<const rhi_webgpu::WebGpuCanonicalBufferUploadSource>, 3u>*
            geometry = nullptr)
    {
        using K = rhi::CommandUsageKind;
        using S = rhi::ResourceState;
        using Scalar = rhi::CommandScalarArguments;
        using Inputs = rhi::OriginalCommandInputs;
        using Graph = rhi::OriginalCommandResourceStates;
        using Root = rhi::OriginalCommandStateTransaction;
        using Content = rhi::common::OriginalTextureContentControl;
        const auto& inventory = sources->sources();
        assert(!geometry || retainedReadback);
        assert(inventory.size() == (geometry ? 18u : retainedReadback ? 15u : 13u));
        const auto pipelineIndex = static_cast<std::uint32_t>(inventory.size() - 2u);
        const auto tableIndex = pipelineIndex + 1u;
        const auto& pipeline = std::get<
            std::shared_ptr<const rhi::OriginalCommandPipelineSource<rhi::RenderPipeline>>>(
            inventory[pipelineIndex]);
        const auto& table =
            std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorTableSource>>(
                inventory[tableIndex]);
        const auto width = retainedReadback ? 4u : 2u;
        assert(!table->originalObject()->originalDepthTablePipelineDiagnostic(
            *pipeline->originalObject()));
        const char* id = nullptr;
        enum class Probe { Valid, MissingClear, WrongClearCell, Discard };
        const auto assemble = [&](Probe probe) {
            core::pmr::Vector<rhi::CommandUsage> commands;
            core::pmr::Vector<rhi::OriginalCommandUsage::Reference> refs;
            core::pmr::Vector<rhi::CommandVariableArguments::Input> fields;
            core::pmr::Vector<Inputs::OriginalDescriptor> records;
            const auto command = [&](K kind, std::optional<std::uint32_t> source = {}) {
                const auto ordinal = commands.size();
                commands.push_back(
                    {kind, static_cast<std::uint32_t>(refs.size()), source ? 1u : 0u});
                if (source)
                    refs.push_back({*source});
                fields.push_back({kind == K::Draw
                        ? *Scalar::capture(kind, Scalar::Draw{3u, 1u, 0u, 0u})
                        : kind == K::DrawIndexed
                        ? *Scalar::capture(kind, Scalar::DrawIndexed{3u, 2u, 0u, 0, 1u})
                        : kind == K::BindIndexBuffer
                        ? *Scalar::capture(kind, Scalar::IndexBinding{rhi::IndexFormat::Uint16, 0u})
                        : kind == K::BindVertexBuffer
                        ? *Scalar::capture(kind, Scalar::VertexBinding{0u, 0u})
                        : kind == K::SetViewport
                        ? *Scalar::capture(kind,
                              rhi::ViewportState{
                                  0.0f, 0.0f, static_cast<float>(width), 2.0f, 0.0f, 1.0f})
                        : kind == K::SetScissor
                        ? *Scalar::capture(kind, rhi::ScissorState{0u, 0u, width, 2u})
                        : kind == K::BindRenderDescriptorTable
                        ? *Scalar::capture(kind, Scalar::DescriptorBinding{0u})
                        : *Scalar::capture(kind, Scalar::None{}),
                    {}});
                return ordinal;
            };
            if (geometry)
                for (std::uint32_t i = 0u; i < 3u; ++i) {
                    const auto& buffer =
                        std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                            inventory[13u + i]);
                    rhi::ResourceMemoryBarrierDesc memory;
                    memory.resource = buffer->resource();
                    memory.sourceStages = memory.destinationStages =
                        rhi::PipelineStage::VertexInput;
                    memory.sourceAccess = memory.destinationAccess = i
                        ? rhi::ResourceAccess::VertexAttributeRead
                        : rhi::ResourceAccess::IndexRead;
                    records.push_back({command(K::BufferMemoryBarrier, 13u + i),
                        *rhi::ResourceDependencyArguments::capture(
                            K::BufferMemoryBarrier, memory)});
                }
            const auto barrier = [&](std::uint32_t parentIndex,
                                     std::uint32_t cell,
                                     S before,
                                     S after) {
                const auto& parent =
                    std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                        inventory[parentIndex]);
                rhi::ResourceBarrierDesc d{parent->resource(),
                    {cell,
                        1u,
                        cell,
                        1u,
                        parentIndex == 6u ? rhi::TextureAspect::Color : rhi::TextureAspect::Depth},
                    before,
                    after};
                d.sourceStages = before == S::Undefined ? rhi::PipelineStage::TopOfPipe
                    : before == S::ColorAttachment      ? rhi::PipelineStage::ColorAttachmentOutput
                                                        : rhi::PipelineStage::DepthStencil;
                d.sourceAccess = before == S::Undefined ? rhi::ResourceAccess::None
                    : before == S::ColorAttachment      ? rhi::ResourceAccess::ColorAttachmentRead
                        | rhi::ResourceAccess::ColorAttachmentWrite
                                                   : rhi::ResourceAccess::DepthStencilRead
                        | rhi::ResourceAccess::DepthStencilWrite;
                d.destinationStages = after == S::CopySrc ? rhi::PipelineStage::Transfer
                    : after == S::ShaderRead              ? rhi::PipelineStage::FragmentShader
                    : after == S::DepthAttachment         ? rhi::PipelineStage::DepthStencil
                                                  : rhi::PipelineStage::ColorAttachmentOutput;
                d.destinationAccess = after == S::CopySrc ? rhi::ResourceAccess::TransferRead
                    : after == S::ShaderRead              ? rhi::ResourceAccess::ShaderRead
                    : after == S::DepthAttachment
                    ? rhi::ResourceAccess::DepthStencilRead | rhi::ResourceAccess::DepthStencilWrite
                    : rhi::ResourceAccess::ColorAttachmentRead
                        | rhi::ResourceAccess::ColorAttachmentWrite;
                records.push_back({command(K::TextureBarrier, parentIndex),
                    *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, d)});
            };
            for (std::uint32_t i = 0u; i < 3u; ++i) {
                barrier(2u * i, i, S::Undefined, S::DepthAttachment);
                const auto viewIndex = probe == Probe::WrongClearCell && i == 2u ? 9u : 8u + i;
                const auto& view =
                    std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                        inventory[viewIndex]);
                rhi::RenderPassDesc pass;
                pass.hasDepthStencilAttachment = true;
                auto& depth = pass.depthStencilAttachment;
                depth.target = view->resource();
                depth.format = rhi::TextureFormat::D32Float;
                depth.depthLoadOp = probe == Probe::MissingClear && i == 2u ? rhi::LoadOp::DontCare
                                                                            : rhi::LoadOp::Clear;
                depth.depthStoreOp = probe == Probe::Discard && i == 2u ? rhi::StoreOp::DontCare
                                                                        : rhi::StoreOp::Store;
                depth.stencilLoadOp = rhi::LoadOp::DontCare;
                depth.stencilStoreOp = rhi::StoreOp::DontCare;
                depth.depthClearValue = static_cast<float>(i + 1u) * 0.25f;
                auto frozen = rhi::RenderPassArguments::prepareOriginal(
                    pass, {1u, true, 0u, 0u, 0u, 0u, 0u, bytes}, inputQuota, id);
                assert(frozen && !id);
                records.push_back({command(K::BeginRenderPass, viewIndex), frozen});
                command(K::EndRenderPass);
                barrier(2u * i, i, S::DepthAttachment, S::ShaderRead);
            }
            barrier(6u, 0u, S::Undefined, S::ColorAttachment);
            const auto& target =
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                    inventory[7u]);
            rhi::RenderPassDesc pass;
            rhi::RenderPassColorAttachmentDesc color;
            color.target = target->resource();
            color.format = rhi::TextureFormat::Rgba16Float;
            color.loadOp = rhi::LoadOp::Clear;
            color.storeOp = rhi::StoreOp::Store;
            pass.colorAttachments.push_back(color);
            auto frozen = rhi::RenderPassArguments::prepareOriginal(
                pass, {1u, true, 1u, 0u, 0u, 0u, 0u, bytes}, inputQuota, id);
            assert(frozen && !id);
            const auto begin = command(K::BeginRenderPass, 7u);
            records.push_back({begin, frozen});
            const auto pipelineBind = command(K::BindGraphicsPipeline, pipelineIndex);
            records.push_back({pipelineBind,
                rhi::OriginalCommandPipelineBinding{K::BindGraphicsPipeline,
                    pipelineIndex,
                    begin,
                    pipeline->originalObject()->originalResourceRegistration().generation()}});
            command(K::SetViewport);
            command(K::SetScissor);
            records.push_back({command(K::BindRenderDescriptorTable, tableIndex),
                rhi::OriginalCommandDescriptorBinding{K::BindRenderDescriptorTable,
                    tableIndex,
                    pipelineIndex,
                    begin,
                    table->originalObject()->originalResourceRegistration().generation(),
                    table->frozenContentRevision()}});
            if (geometry)
                for (std::uint32_t i = 0u; i < 3u; ++i) {
                    const auto kind = i ? K::BindVertexBuffer : K::BindIndexBuffer;
                    const auto ordinal = command(kind, 13u + i);
                    if (i)
                        fields[ordinal].scalar =
                            *Scalar::capture(kind, Scalar::VertexBinding{i - 1u, 0u});
                    const auto& buffer =
                        std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                            inventory[13u + i]);
                    records.push_back({ordinal,
                        rhi::OriginalCommandGeometryBinding{kind,
                            13u + i,
                            pipelineIndex,
                            pipelineBind,
                            begin,
                            buffer->originalObject()->originalResourceRegistration().generation(),
                            pipeline->originalObject()
                                ->originalResourceRegistration()
                                .generation()}});
                }
            command(geometry ? K::DrawIndexed : K::Draw);
            command(K::EndRenderPass);
            if (retainedReadback) {
                barrier(6u, 0u, S::ColorAttachment, S::CopySrc);
                command(K::BeginTransferPass);
                const auto readback =
                    std::get<std::shared_ptr<const rhi::OriginalCommandTextureReadbackSource>>(
                        inventory[12u]);
                const auto ordinal = commands.size();
                commands.push_back(
                    {K::TextureReadback, static_cast<std::uint32_t>(refs.size()), 2u});
                refs.push_back({6u});
                refs.push_back({12u});
                fields.push_back(
                    {*Scalar::capture(K::TextureReadback, Scalar::None{}), readbackLabel});
                records.push_back({ordinal,
                    *rhi::TextureReadbackOriginalInputArguments::captureOriginal(
                        readback->originalInput())});
                command(K::EndTransferPass);
            }
            command(K::Finish);
            assert(commands.size()
                    == (geometry               ? 31u
                            : retainedReadback ? 25u
                                               : 21u)
                && refs.size()
                    == (geometry               ? 22u
                            : retainedReadback ? 16u
                                               : 13u)
                && records.size()
                    == (geometry               ? 21u
                            : retainedReadback ? 15u
                                               : 13u));
            if (geometry) {
                const auto expected =
                    OriginalDepthTableGeometryCompositionContractFixture::commands();
                const auto expectedRefs =
                    OriginalDepthTableGeometryCompositionContractFixture::references();
                for (std::size_t i = 0u; i < commands.size(); ++i)
                    assert(commands[i].kind == expected[i].kind
                        && commands[i].firstReference == expected[i].firstReference
                        && commands[i].referenceCount == expected[i].referenceCount);
                for (std::size_t i = 0u; i < refs.size(); ++i)
                    assert(refs[i].sourceIndex == expectedRefs[i].sourceIndex);
            }
            rhi::OriginalCommandInputsBudget inputBudget{4u, true, 1u, 0u, 1u, 0u};
            if (geometry) {
                inputBudget.contractVersion = 5u;
                inputBudget.maxIndexBindings = 1u;
                inputBudget.maxVertexBindings = 2u;
            }
            inputBudget.retainedReadback = sources->retainedReadbackCorrespondence();
            auto inputs = OriginalCommandUploadStreamBuilderFixture::prepareRetained(sources,
                commands.data(),
                commands.size(),
                refs.data(),
                refs.size(),
                fields.data(),
                records.data(),
                records.size(),
                inputBudget,
                inputQuota,
                id);
            if (!inputs)
                std::fprintf(stderr, "table ContentPlan2 Inputs: %s\n", id ? id : "missing");
            assert(inputs && !id);
            return inputs;
        };
        auto inputs = assemble(Probe::Valid);
        if (geometry) {
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)                                \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
            assert(deferredCompletion);
            return verifyGeometryGraph(
                sources, device, inputs, inputQuota, *geometry, *deferredCompletion);
#else
            assert(false);
            return {};
#endif
        }
        std::uint32_t owners = 0u, registrations = 0u;
        assert(!rhi::OriginalCommandResourceSources::registeredOwnerUnion(sources,
            inputs->retainedOwnerCount() + 1u,
            UINT32_MAX,
            UINT32_MAX,
            owners,
            registrations));
        const auto commandCount = retainedReadback ? 25u : 21u;
        const rhi::OriginalCommandStateAdmissionBudget stateBudget{3u,
            true,
            1u,
            registrations,
            retainedReadback ? 20u : 17u,
            retainedReadback ? 19u : 17u,
            owners,
            bytes,
            {2u, true, 0u, 0u, false, false, 1u, 3u, 1u, true, 3u},
            {2u, true, 0u, 0u, false, false, commandCount, 1u, 3u, 1u, true, 3u},
            retainedReadback ? 1u : 0u,
            retainedReadback};
        OriginalCommandTypedStreamFixture::Retained retained;
        retained.stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        retained.recordingQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        if (retainedReadback) {
            auto ordinary = stateBudget;
            ordinary.retainedReadbackStateVersion = 0u;
            ordinary.retainedReadbackStateOptIn = false;
            assert(!Graph::prepareOriginal(inputs, device, ordinary, retained.stateQuota, id));
            is(id, "RHI-ORIGINAL-TYPED-STATE-SOURCE-PROFILE");
            assert(!retained.stateQuota->liveBytes());
            ordinary = stateBudget;
            ordinary.retainedReadbackStateOptIn = false;
            assert(!Graph::prepareOriginal(inputs, device, ordinary, retained.stateQuota, id));
            is(id, "RHI-ORIGINAL-STATE-READBACK-OPT-IN");
            assert(!retained.stateQuota->liveBytes());
        }
        const auto graph = [&](const auto& originalInputs) {
            auto result = Graph::prepareOriginal(
                originalInputs, device, stateBudget, retained.stateQuota, id);
            if (!result)
                std::fprintf(stderr, "table ContentPlan2 State3: %s\n", id ? id : "missing");
            assert(result && !id);
            return result;
        };
        retained.graph = graph(inputs);
        const rhi::OriginalCommandStateSequenceBudget sequence{3u,
            true,
            commandCount,
            0u,
            retainedReadback ? 20u : 17u,
            retainedReadback ? 21u : 17u,
            6u};
        auto streamBudget = OriginalCommandTypedStreamFixture::budget(
            retained.graph, sequence, stateBudget.descriptorConsumption, 1u);
        streamBudget.contractVersion = 3u;
        streamBudget.renderDescriptorUsage = stateBudget.renderDescriptorUsage;
        streamBudget.maxImplicitAccesses = 6u;
        streamBudget.maxRenderPasses = streamBudget.maxAttachmentReferences = 4u;
        if (retainedReadback) {
            streamBudget.maxTextureReadbackMips = 1u;
            streamBudget.maxReadbackDeclaredBytes = 512u;
        }
        auto stream = OriginalCommandTypedStreamFixture::prepare(retained.graph, streamBudget);
        retained.program = std::move(stream.program);
        retained.quota = std::move(stream.quota);
        // /2 can inspect this exact shader-bearing Stream3; publication or real
        // drawing without an authored readback is never a result declaration.
        const rhi::OriginalCommandReadbackResultContract::Budget resultBudget{
            2u, true, 1u, 1u, 1u, 512u, 512u, bytes};
        const auto missingResult = rhi::OriginalCommandReadbackResultContract::inspectProgram(
            retained.program, inputs, resultBudget);
        if (retainedReadback)
            assert(!missingResult.diagnostic && missingResult.required.readbackCommands == 1u
                && missingResult.required.nativeCopyCommands == 1u
                && missingResult.required.resultDeclarations == 1u
                && missingResult.required.uniqueResultBytes == 512u);
        else {
            is(missingResult.diagnostic, "RHI-ORIGINAL-READBACK-RESULT-MISSING");
            assert(!missingResult.required.readbackCommands
                && !missingResult.required.nativeCopyCommands
                && !missingResult.required.resultDeclarations
                && !missingResult.required.uniqueResultBytes);
        }
        using Usage = rhi::common::OriginalTypedUsageProjection;
        using Dependency = rhi::common::OriginalTypedDependencyProjection;
        const Usage::StateBoundRequest usages{retainedReadback ? 3u : 2u,
            true,
            {retainedReadback ? 3u : 2u,
                true,
                stateBudget.descriptorConsumption,
                stateBudget.renderDescriptorUsage,
                commandCount,
                4u,
                retainedReadback ? 12u : 10u,
                6u,
                retainedReadback ? 11u : 10u,
                retainedReadback ? 1u : 0u,
                retainedReadback ? 1u : 0u,
                retainedReadback ? 512u : 0u}};
        const rhi::OriginalTypedDependencyProjectionContract::Request dependencies{
            retainedReadback ? 3u : 2u,
            true,
            usages,
            retainedReadback ? 8u : 7u,
            retainedReadback ? 29u : 25u,
            retainedReadback ? 348u : 250u,
            retainedReadback ? 66u : 45u,
            retainedReadback ? 15u : 13u,
            9u};
        const auto project = [&]() noexcept -> const char* {
            std::uint32_t sampled = 0u, attachments = 0u, readbackTextures = 0u,
                          readbackBuffers = 0u;
            const auto usage = Usage::visitStateBound(retained.program,
                retained.graph,
                usages,
                [&](const Usage::Point& point) noexcept -> const char* {
                    if (const auto* texture = std::get_if<Usage::TexturePoint>(&point)) {
                        const auto element = sampled % 3u;
                        assert(texture->original.commandIndex == 17u + sampled / 3u
                            && texture->original.textureParentSourceIndex == 2u * element
                            && texture->original.textureViewSourceIndex == 2u * element + 1u
                            && texture->original.descriptorSourceIndex
                                == (retainedReadback ? 14u : 12u)
                            && texture->use.range.baseMipLevel == element
                            && texture->use.range.baseArrayLayer == element
                            && texture->use.scope.first == 13u && texture->use.scope.last == 19u);
                        ++sampled;
                    } else if (const auto* readback =
                                   std::get_if<Usage::ReadbackTexturePoint>(&point)) {
                        assert(retainedReadback && readback->sourceIndex == 12u
                            && readback->parentSourceIndex == 6u
                            && readback->original
                                == std::get<std::shared_ptr<
                                    const rhi::OriginalCommandTextureReadbackSource>>(
                                    inventory[12u])
                                    .get()
                            && readback->use.commandIndex == 22u && readback->use.scope.first == 22u
                            && readback->use.scope.last == 22u
                            && readback->use.scope.kind
                                == rhi::semantics::TypedShaderUsageScopeContract::Kind::TransferCopy
                            && readback->use.access.state == rhi::ResourceState::CopySrc
                            && readback->use.access.access == rhi::ResourceAccess::TransferRead);
                        ++readbackTextures;
                    } else if (const auto* readback =
                                   std::get_if<Usage::ReadbackBufferPoint>(&point)) {
                        assert(retainedReadback && readback->sourceIndex == 12u
                            && readback->destinationSourceIndex == 11u
                            && readback->use.commandIndex == 22u && readback->use.offsetBytes == 0u
                            && readback->use.sizeBytes == 512u && readback->use.bufferBytes == 512u
                            && readback->use.access.state == rhi::ResourceState::CopyDst
                            && readback->use.access.access == rhi::ResourceAccess::TransferWrite);
                        ++readbackBuffers;
                    } else {
                        assert(std::holds_alternative<Usage::AttachmentPoint>(point));
                        ++attachments;
                    }
                    return nullptr;
                });
            if (usage.diagnostic)
                return usage.diagnostic;
            assert(sampled == 6u && attachments == 4u
                && readbackTextures == (retainedReadback ? 1u : 0u)
                && readbackBuffers == readbackTextures
                && usage.counts.contributions == (retainedReadback ? 12u : 10u)
                && usage.counts.readbacks == readbackTextures
                && usage.counts.textureCells == (retainedReadback ? 11u : 10u)
                && !usage.nativeExecutionProven() && !usage.synchronizationProven());
            std::uint32_t emitted = 0u;
            const auto dependency = Dependency::visit(retained.program,
                retained.graph,
                dependencies,
                [&](const Dependency::Record& record) noexcept -> const char* {
                    if (emitted == 7u) {
                        assert(retainedReadback && record.texture && record.projectionVersion == 3u
                            && record.commandIndex == 20u && record.sourceIndex == 6u
                            && record.preceding && record.following
                            && record.preceding->scope.first == 13u
                            && record.preceding->scope.last == 19u
                            && record.following->scope.first == 22u
                            && record.following->scope.last == 22u
                            && record.following->access.stages == rhi::PipelineStage::Transfer
                            && record.following->access.access
                                == rhi::ResourceAccess::TransferRead);
                        auto bad = record;
                        bad.projectionVersion = 2u;
                        is(Dependency::validateCoverage(bad, 9u),
                            "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-POSITION");
                        bad = record;
                        bad.following->scope.last = 23u;
                        is(Dependency::validateCoverage(bad, 9u),
                            "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-POSITION");
                        bad = record;
                        bad.following->access.state = rhi::ResourceState::CopyDst;
                        bad.following->access.access = rhi::ResourceAccess::TransferWrite;
                        is(Dependency::validateCoverage(bad, 9u),
                            "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-COVERAGE");
                        ++emitted;
                        return nullptr;
                    }
                    const auto element = emitted / 2u;
                    assert(record.texture
                        && record.projectionVersion == (retainedReadback ? 3u : 2u)
                        && record.commandIndex
                            == (emitted == 6u ? 12u : element * 4u + (emitted % 2u ? 3u : 0u))
                        && record.sourceIndex == (emitted == 6u ? 6u : 2u * element)
                        && record.following
                        && (emitted == 6u || !(emitted % 2u) ? !record.preceding
                                                             : bool(record.preceding)));
                    ++emitted;
                    return nullptr;
                });
            if (dependency.diagnostic)
                return dependency.diagnostic;
            assert(emitted == (retainedReadback ? 8u : 7u) && dependency.counts.initial == 4u
                && dependency.counts.between == (retainedReadback ? 4u : 3u)
                && dependency.counts.projectionPasses == (retainedReadback ? 29u : 25u)
                && dependency.counts.contributionVisits == (retainedReadback ? 348u : 250u)
                && dependency.counts.pairChecks == (retainedReadback ? 66u : 45u)
                && dependency.counts.endpointContributions == (retainedReadback ? 15u : 13u)
                && !dependency.nativeExecutionProven() && !dependency.synchronizationProven());
            return nullptr;
        };
        if (const auto* diagnostic = project())
            std::fprintf(stderr,
                "table ContentPlan%u dependencies: %s\n",
                retainedReadback ? 3u : 2u,
                diagnostic);
        assert(!project());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{
                sources->originalMetadataQuota(), inputQuota, retained.stateQuota, retained.quota},
            project));
        if (retainedReadback) {
            const auto refused = [&](auto invalid, const char* expected) {
                std::uint32_t emitted = 0u;
                const auto rejected = Usage::visitStateBound(retained.program,
                    retained.graph,
                    invalid,
                    [&](const Usage::Point&) noexcept -> const char* {
                        ++emitted;
                        return nullptr;
                    });
                is(rejected.diagnostic, expected);
                assert(!emitted && !rejected.counts.contributions);
            };
            auto invalid = usages;
            --invalid.projection.maxReadbackDeclaredBytes;
            refused(invalid, "RHI-ORIGINAL-USAGE-PROJECTION-READBACK-CAPACITY");
            invalid = usages;
            --invalid.projection.maxContributions;
            refused(invalid, "RHI-ORIGINAL-USAGE-PROJECTION-CONTRIBUTION-CAPACITY");
            invalid = usages;
            --invalid.projection.maxTextureCells;
            refused(invalid, "RHI-ORIGINAL-USAGE-PROJECTION-TEXTURE-CELL-CAPACITY");
            invalid = usages;
            invalid.contractVersion = invalid.projection.contractVersion = 2u;
            invalid.projection.maxReadbacks = invalid.projection.maxReadbackMips =
                invalid.projection.maxReadbackDeclaredBytes = 0u;
            refused(invalid, "RHI-TYPED-USAGE-SCOPE-COMMAND-PROFILE");
            std::puts(
                "ORIGINAL_TABLE_READBACK_USAGE3_OK Graphics same-original-State3-Stream3 source-read-destination-write contributions12 cells11 dependencies8 between4 passes29 visits348 pairs66 endpoints15 exact-less legacy-freeze-closed hot0 native-execution-closed");
        }
        auto rootBudget = stateBudget;
        rootBudget.maxRetainedOwners += 6u;
        const auto make = [&] {
            return Root::prepareOriginalOwnership(
                retained.graph, device, rootBudget, retained.recordingQuota, id);
        };
        auto root = make();
        if (!root)
            std::fprintf(stderr, "table ContentPlan2 root: %s\n", id ? id : "missing");
        assert(root && !id);
        if (retainedReadback) {
            auto ordinary = rootBudget;
            ordinary.retainedReadbackStateVersion = 0u;
            ordinary.retainedReadbackStateOptIn = false;
            assert(!Root::prepareOriginalOwnership(
                retained.graph, device, ordinary, retained.recordingQuota, id));
            is(id, "RHI-ORIGINAL-TRANSACTION-READBACK-PROFILE");
        }
        rhi::OriginalTextureContentPlanBudget contentBudget{retainedReadback ? 3u : 2u,
            true,
            4u,
            0u,
            28u,
            retainedReadback ? 12u : 11u,
            std::uint64_t(commandCount) * 29u,
            20000u,
            retainedReadback ? 1u : 0u};
        const auto preflight = root->preflightOriginalContentCells(contentBudget);
        if (preflight.diagnostic)
            std::fprintf(stderr, "table ContentPlan2 content: %s\n", preflight.diagnostic);
        assert(!preflight.diagnostic && preflight.controls == 4u && preflight.cells == 28u
            && preflight.steps == (retainedReadback ? 12u : 11u)
            && preflight.commandVisits == std::uint64_t(commandCount) * 29u
            && !preflight.nativeExecutionProven());
        contentBudget.maxProjectionVisits = preflight.projectionVisits;
        const auto reject = [&](auto changed, const char* expected) {
            const auto result = root->preflightOriginalContentCells(changed);
            is(result.diagnostic, expected);
            assert(!result.controls && !result.cells && !result.steps && !result.commandVisits
                && !result.projectionVisits && root->journalEmpty()
                && !root->hasOriginalPrototypePlan());
        };
        auto bad = contentBudget;
        --bad.maxContentControls;
        reject(bad, "RHI-CANONICAL-CONTENT-PLAN-CONTROL-CAPACITY");
        bad = contentBudget;
        --bad.maxContentCells;
        reject(bad, "RHI-ORIGINAL-CONTENT-PLAN-CELL-CAPACITY");
        bad = contentBudget;
        --bad.maxContentSteps;
        reject(bad, "RHI-ORIGINAL-CONTENT-PLAN-STEP-CAPACITY");
        bad = contentBudget;
        --bad.maxCommandVisits;
        reject(bad, "RHI-ORIGINAL-CONTENT-PLAN-COMMAND-CAPACITY");
        bad = contentBudget;
        --bad.maxProjectionVisits;
        reject(bad, "RHI-ORIGINAL-CONTENT-PLAN-PROJECTION-CAPACITY");
        root.reset();
        const auto planned = [&] {
            auto result = make();
            if (result && (id = result->materializeOriginalPrototypePlan(sequence, contentBudget)))
                result.reset();
            return result;
        };
        CpuQuotaFactoryFixture pressure(retained.recordingQuota);
        const auto peak = pressure.measure(planned);
        pressure.verify(peak, planned);
        root = planned();
        if (!root)
            std::fprintf(stderr, "table ContentPlan2 plan: %s\n", id ? id : "missing");
        assert(root && !id && root->plannedContentControlCount() == 4u
            && root->plannedContentCellCount() == 28u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{sources->originalMetadataQuota(),
                inputQuota,
                retained.stateQuota,
                retained.quota,
                retained.recordingQuota},
            [&]() noexcept -> const char* {
                if (const auto* diagnostic = root->applyOriginalPrototypePlan())
                    return diagnostic;
                assert(root->status() == Root::Status::Pending && !root->journalEmpty());
                is(root->commitOriginalPendingOwnership(),
                    "RHI-ORIGINAL-TEXTURE-TRANSACTION-NATIVE-COMPLETION-REQUIRED");
                if (const auto* diagnostic = root->validate())
                    return diagnostic;
                return root->abandonOriginalPendingOwnership();
            }));
        root.reset();
        retained.program.reset();
        retained.graph.reset();
        const auto uninitialized = [&] {
            for (const auto index : {0u, 2u, 4u, 6u}) {
                const auto* object =
                    std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                        inventory[index])
                        ->originalObject();
                assert(object->subresourceState(rhi::defaultTextureSubresourceRange(object->desc()))
                    == S::Undefined);
                for (std::uint32_t mip = 0u; mip < object->desc().mipLevelCount; ++mip)
                    for (std::uint32_t layer = 0u; layer < object->desc().arrayLayerCount;
                        ++layer) {
                        auto observation = object->observeOriginalContent({mip,
                            1u,
                            layer,
                            1u,
                            index == 6u ? rhi::TextureAspect::Color : rhi::TextureAspect::Depth});
                        assert(observation && observation->state() == Content::State::Uninitialized
                            && !observation->contentGeneration());
                    }
            }
        };
        uninitialized();
        retained.graph = graph(inputs);
        root = planned();
        assert(root && !root->applyOriginalPrototypePlan());
        root.reset();
        uninitialized();
        retained.graph.reset();
        for (const auto probe : {Probe::MissingClear, Probe::WrongClearCell, Probe::Discard}) {
            retained.graph = graph(assemble(probe));
            root = make();
            assert(root);
            is(root->preflightOriginalContentCells(contentBudget).diagnostic,
                "RHI-ORIGINAL-CONTENT-SEQUENCE-READ-UNINITIALIZED");
            assert(root->journalEmpty() && !root->hasOriginalPrototypePlan());
            root.reset();
            retained.graph.reset();
        }
        retained.graph = graph(inputs);
        stream = OriginalCommandTypedStreamFixture::prepare(retained.graph, streamBudget);
        retained.program = std::move(stream.program);
        retained.quota = std::move(stream.quota);
        retained.transaction = planned();
        assert(retained.transaction && !id);
        if (retainedReadback) {
            verifyReadbackJoins(retained, device, stateBudget, contentBudget, deferredCompletion);
            std::puts(
                "ORIGINAL_TABLE_READBACK_CONTENT3_OK Graphics target4x2 Source2 destination512 commands25 references16 descriptors15 original-Inputs4-State3-Stream3 ContentPlan3 cells28 readonly1 same-journal rollback hot0 native-execution-closed");
            return retained;
        }
        std::printf(
            "ORIGINAL_TABLE_CONTENT_PLAN2_OK Graphics canonical-depth3 target-CopySrc attachment-views3 commands21 viewport-scissor-explicit controls4 cells28 steps11 commandVisits609 projectionVisits%llu recording%llu dependencies7 pairs45 visits250 same-Inputs4-State3-Stream3-TableSource2-journal missing-clear-wrong-cell-discard-refused exact-less rollback-drop hot0 native-closed\n",
            static_cast<unsigned long long>(contentBudget.maxProjectionVisits),
            static_cast<unsigned long long>(peak));
        return retained;
    }

private:
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)                                \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    static OriginalCommandTypedStreamFixture::Retained verifyGeometryGraph(
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
        const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const std::shared_ptr<core::CpuMemoryQuota>& inputQuota,
        const std::array<std::shared_ptr<const rhi_webgpu::WebGpuCanonicalBufferUploadSource>, 3u>&
            initialized,
        NativeCustody& native)
    {
        using C = OriginalDepthTableGeometryCompositionContractFixture;
        using Source = rhi_webgpu::WebGpuCanonicalBufferUploadSource;
        using Graph = rhi::OriginalCommandResourceStates;
        using Root = rhi::OriginalCommandStateTransaction;
        using Joins = rhi_webgpu::WebGpuCanonicalReadbackJoins;
        using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
        const auto require = [](const char* id) {
            if (id)
                std::fprintf(stderr, "table geometry: %s\n", id);
            assert(!id);
        };
        const auto& authored = sources->originalBudget();
        const auto& inventory = sources->sources();
        const char* id = nullptr;
        std::uint32_t owners = 0u, registrations = 0u;
        require(rhi::OriginalCommandResourceSources::registeredOwnerUnion(sources,
            inputs->retainedOwnerCount() + 1u,
            authored.maxGraphOwners,
            authored.maxGraphOwners,
            owners,
            registrations));
        rhi::OriginalCommandStateAdmissionBudget state{4u,
            true,
            1u,
            registrations,
            23u,
            19u,
            owners,
            bytes,
            {3u, true, 0u, 0u, false, false, 1u, 3u, 1u, true, 3u},
            {3u, true, 0u, 0u, false, false, C::commandCount, 1u, 3u, 1u, true, 3u},
            1u,
            true};
        state.descriptorConsumption.indexedDrawConsumptionVersion = 1u;
        state.descriptorConsumption.indexedDrawConsumptionOptIn = true;
        state.descriptorConsumption.vertexStreamConsumptionVersion = 1u;
        state.descriptorConsumption.vertexStreamConsumptionOptIn = true;
        state.descriptorConsumption.maxVertexStreams = 2u;
        state.geometryUsage = {
            1u, true, C::commandCount, 3u, 15u, 1u, true, 1u, 3u, C::commandCount * 3u};
        OriginalCommandTypedStreamFixture::Retained retained;
        retained.stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        retained.quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        retained.recordingQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto wrong = state;
        wrong.descriptorConsumption.vertexStreamConsumptionOptIn = false;
        assert(!Graph::prepareOriginal(inputs, device, wrong, retained.stateQuota, id) && id
            && !retained.stateQuota->liveBytes());
        retained.graph = Graph::prepareOriginal(inputs, device, state, retained.stateQuota, id);
        require(id);
        assert(retained.graph);
        rhi::OriginalCommandStreamBudget stream;
        stream.contractVersion = 4u;
        stream.prototypeOptIn = true;
        stream.maxCommands = C::commandCount;
        stream.maxDescriptors = C::descriptorCount;
        stream.maxReferences = C::referenceCount;
        stream.maxRenderPasses = stream.maxAttachmentReferences = 4u;
        stream.maxRetainedOwners = retained.graph->retainedOwnerCount() + 1u;
        stream.maxCpuMetadataBytes = bytes;
        stream.maxPipelineBinds = stream.maxDescriptorBinds = stream.maxConsumptions = 1u;
        // Procedural table/readback's21-step ceiling plus six explicit geometry
        // buffer operands and three indexed-draw fetches. Binding footprints
        // are already represented by those operands, not charged twice.
        stream.maxImplicitAccesses = 12u;
        stream.stateSequence = {4u, true, C::commandCount, 0u, 23u, 30u, 12u};
        stream.maxTextureReadbackMips = 1u;
        stream.maxReadbackDeclaredBytes = 512u;
        stream.descriptorConsumption = state.descriptorConsumption;
        stream.renderDescriptorUsage = state.renderDescriptorUsage;
        stream.geometryUsage = state.geometryUsage;
        const auto sequence = rhi::common::OriginalCommandStateSequence::inspect(
            retained.graph, stream.stateSequence);
        require(sequence.diagnostic);
        std::printf("ORIGINAL_TABLE_GEOMETRY_SEQUENCE_OK cells%llu steps%llu authored-ceiling30\n",
            static_cast<unsigned long long>(sequence.stateCells),
            static_cast<unsigned long long>(sequence.steps));
        retained.program = rhi::OriginalCommandStreamProgram::prepareOriginalTyped(
            retained.graph, stream, retained.quota, id);
        require(id);
        assert(retained.program);
        using Usage = rhi::common::OriginalTypedUsageProjection;
        const Usage::Budget projected{4u,
            true,
            state.descriptorConsumption,
            state.renderDescriptorUsage,
            C::commandCount,
            4u,
            18u,
            6u,
            11u,
            1u,
            1u,
            512u,
            state.geometryUsage};
        const auto accesses =
            Usage::inspectStateBound(retained.program, retained.graph, {4u, true, projected});
        require(accesses.diagnostic);
        assert(accesses.counts.contributions == 18u && accesses.counts.textureCells == 11u
            && accesses.counts.readbacks == 1u);
        using Dependency = rhi::common::OriginalTypedDependencyProjection;
        const Dependency::Request dependencies{
            4u, true, {4u, true, projected}, 11u, 41u, 738u, 153u, 21u, 9u};
        const auto inspected = Dependency::inspect(retained.program, retained.graph, dependencies);
        require(inspected.diagnostic);
        assert(inspected.counts.dependencies == 11u && inspected.counts.initial == 7u
            && inspected.counts.between == 4u && inspected.counts.projectionPasses == 41u
            && inspected.counts.contributionVisits == 738u && inspected.counts.pairChecks == 153u
            && inspected.counts.endpointContributions == 21u);
        const auto hot = [&]() noexcept {
            return Dependency::inspect(retained.program, retained.graph, dependencies).diagnostic;
        };
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{inputQuota, retained.stateQuota, retained.quota}, hot));
        const auto failed = [&](auto bad) {
            const auto observed = Dependency::inspect(retained.program, retained.graph, bad);
            assert(observed.diagnostic && !observed.counts.dependencies);
        };
        auto bad = dependencies;
        --bad.maxDependencies;
        failed(bad);
        bad = dependencies;
        --bad.maxContributionVisits;
        failed(bad);
        bad = dependencies;
        --bad.maxPairChecks;
        failed(bad);
        state.maxRetainedOwners = retained.graph->retainedOwnerCount() + 6u;
        retained.transaction = Root::prepareOriginalOwnership(
            retained.graph, device, state, retained.recordingQuota, id);
        require(id);
        assert(retained.transaction);
        const rhi::OriginalTextureContentPlanBudget content{
            3u, true, 4u, 0u, 28u, 12u, C::commandCount * 29u, 20000u, 1u};
        require(
            retained.transaction->materializeOriginalPrototypePlan(stream.stateSequence, content));
        const auto readback =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureReadbackSource>>(
                inventory[12u]);
        Joins::Budget join{3u,
            true,
            1u,
            1u,
            1u,
            authored.maxGraphOwners,
            64u,
            512u,
            bytes,
            stream,
            {2u, true, 1u, 1u, 1u, 512u, 512u, bytes}};
        const Joins::Input entry{C::readbackCommand, readback};
        auto joins = Joins::prepareOriginalProgram(
            retained.transaction, retained.program, device, &entry, 1u, join, id);
        require(id);
        assert(joins);
        std::uint64_t resourceBytes = 512u + 152u;
        for (const auto index : {0u, 2u, 4u, 6u})
            resourceBytes +=
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(inventory[index])
                    ->originalObject()
                    ->allocation()
                    .estimatedSizeBytes;
        rhi::OriginalCommandNativeClosureBudget custody{
            7u, true, 15u, 17u, authored.maxGraphOwners, resourceBytes, bytes, 1u, 1u, 1u, 1u};
        custody.geometryContent = {1u,
            true,
            3u,
            3u * Source::initializedIndexOwnerBound,
            152u,
            state.geometryUsage,
            {1u, true, C::commandCount, 1u, 2u, C::commandCount * 3u, 3u}};
        const std::array<Closure::GeometryContentInput, 3u> contents{
            {{13u, initialized[0]}, {14u, initialized[1]}, {15u, initialized[2]}}};
        auto loss = device.observeOriginalDeviceNativeLoss(id);
        require(id);
        assert(loss);
        native.closure = Closure::prepareOriginalGeometryReadbackLossBound(retained.transaction,
            retained.program,
            joins,
            contents.data(),
            contents.size(),
            device,
            custody,
            *loss,
            id);
        require(id);
        assert(native.closure);
        using Frozen = rhi_webgpu::WebGpuOriginalTypedDependencyDecision;
        native.dependencies = Frozen::prepareOriginal(native.closure,
            {5u, true, dependencies, native.closure->retainedOwnerCount() + 1u, bytes},
            id);
        require(id);
        assert(native.dependencies);
        // Reuse the shared immutable request projection and real factory. The
        // existing ordinary payload suite retains its exhaustive member probes;
        // this execution fixture checks the combined table/geometry endpoints.
        using Payload = rhi_webgpu::WebGpuOriginalTypedNativePayload;
        const auto payloadRequest =
            OriginalTypedNativePayloadFixture::requestFor(native.dependencies, 5u);
        assert(payloadRequest.maxWork.commands == 31u && payloadRequest.maxWork.nativeCalls == 19u);
        native.payload = Payload::prepareOriginal(native.dependencies, payloadRequest, id);
        require(id);
        assert(native.payload);
        const auto& selected = native.payload->records()[C::tableBind];
        std::uint32_t physicalGroup = UINT32_MAX;
        require(Payload::originalPhysicalGroup(selected, physicalGroup));
        assert(physicalGroup == 0u && selected.binding->object.table && selected.group);
        const auto& copied = native.payload->records()[C::readbackCommand];
        assert(copied.readback && copied.readback->source == readback && copied.destination
            && copied.destination->sourceIndex == C::destination && copied.readbackCopyCount == 1u);
        auto lessPayload = payloadRequest;
        --lessPayload.maxReadbackCopies;
        assert(!Payload::prepareOriginal(native.dependencies, lessPayload, id) && id);
        lessPayload = payloadRequest;
        --lessPayload.maxWork.vertexBinds;
        assert(!Payload::prepareOriginal(native.dependencies, lessPayload, id) && id);
        assert(native.payload->work().commands == 31u && native.payload->work().nativeCalls == 19u
            && native.payload->work().indexedDraws == 1u && native.payload->work().vertexBinds == 2u
            && native.payload->work().indexBinds == 1u
            && native.payload->readbackCopyCount() == 1u);
        native.cold =
            OriginalTypedColdAdmissionFixture::verifyComposed(native.payload, device, false);
        native.command = OriginalTypedColdCommandFixture::verifyComposed(native.cold);
        const auto& seal = native.cold->originalReservation()->originalSeal();
        native.submissionQuota = seal->originalSubmissionQuota();
        native.callbackQuota = seal->originalCallbackQuota();
        std::puts(
            "ORIGINAL_TABLE_GEOMETRY_CUSTODY_OK same-original31 Inputs5 State4 Stream4 Join3 Closure7 Freeze5 Payload5 Cold5 Command5 table-only vertex2 indexedDraw1 readback1 contributions18 dependencies11 numeric-execution-unproven public-closed");
        return retained;
    }

public:
    static void verifyGeometryExecution(NativeCustody& native, const rhi::GpuDevice& device)
    {
        assert(native.command);
        auto resultQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto command = std::move(native.command);
        native.cold.reset();
        using Case = OriginalVertexGeometryExecutionFixture::LifecycleCase;
        const auto mode = geometryInvocationCase != Case::Complete ? geometryInvocationCase
            : geometryRecordingCancelRequested                     ? Case::RecordingCancel
            : mixedReadbackMapLossRequested                        ? Case::MapLoss
            : mixedReadbackLossRequested                           ? Case::SubmissionLoss
                                                                   : Case::Complete;
        OriginalVertexGeometryExecutionFixture::executeReadback(
            std::move(command),
            device,
            resultQuota,
            mode,
            11u,
            [](const std::uint8_t* output) {
                constexpr std::array<std::uint16_t, 4u> depths{
                    {0x3400u, 0x3800u, 0x3a00u, 0x3400u}};
                for (std::size_t y = 0u; y < 2u; ++y)
                    for (std::size_t x = 0u; x < 4u; ++x)
                        for (std::size_t channel = 0u; channel < 4u; ++channel) {
                            const std::uint16_t expected = channel == 0u ? depths[x]
                                : channel == 1u                          ? 0u
                                                                         : 0x3c00u;
                            const auto offset = y * 256u + x * 8u + channel * 2u;
                            assert(output[offset] == (expected & 255u)
                                && output[offset + 1u] == (expected >> 8u));
                        }
            },
            "ORIGINAL_TABLE_GEOMETRY_EXECUTION5_OK same-original31 indexedDraw3 instances2 firstInstance1 selected-depth-quarter-half-threequarters-quarter instance-green0-blue1 RGBA16Float4x2 encoder1 copy1 queue1 map1 real-completion numeric-readback hot0-query0 public-closed");
        assert(!resultQuota->liveBytes());
        if (mode != Case::Complete)
            std::printf(
                "ORIGINAL_TABLE_GEOMETRY_LIFECYCLE_OK %s same-original31 unpublished completion0 result-quota-zero public-closed\n",
                OriginalVertexGeometryExecutionFixture::lifecycleName(mode));
    }

private:
#endif
    static auto verifyReadbackCustody(OriginalCommandTypedStreamFixture::Retained& retained,
        const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi_webgpu::WebGpuCanonicalReadbackJoins>& joins)
    {
        using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
        using Decision = rhi_webgpu::WebGpuOriginalTypedDependencyDecision;
        const auto sources =
            retained.graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
        const auto& inventory = sources->sources();
        const char* id = nullptr;
        auto loss = device.observeOriginalDeviceNativeLoss(id);
        assert(loss && !id);
        std::uint64_t resourceBytes = 0u;
        for (const auto index : {0u, 2u, 4u, 6u})
            resourceBytes +=
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(inventory[index])
                    ->originalObject()
                    ->allocation()
                    .estimatedSizeBytes;
        resourceBytes +=
            std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(inventory[11u])
                ->originalObject()
                ->allocation()
                .estimatedSizeBytes;
        rhi::OriginalCommandNativeClosureBudget budget{4u,
            true,
            12u,
            14u,
            sources->originalBudget().maxGraphOwners,
            resourceBytes,
            bytes,
            1u,
            1u,
            1u,
            1u};
        const auto make = [&] {
            return Closure::prepareOriginalTableReadbackLossBound(
                retained.transaction, retained.program, joins, device, budget, *loss, id);
        };
        auto closure = make();
        if (!closure)
            std::fprintf(stderr, "table Closure4: %s\n", id ? id : "missing");
        assert(closure && !id && closure->closureContractVersion() == 4u
            && closure->resourceBindingCount() == 12u && closure->nativePayloadCount() == 14u
            && closure->retainedResourceBytes() == resourceBytes
            && core::SharedOwnerProjection::sameOwner(closure->originalReadbackJoins(), joins));
        is(closure->executionDiagnostic(),
            "RHI-ORIGINAL-NATIVE-CLOSURE-TABLE-READBACK-WEBGPU-RECORDING-UNPROVEN");
        budget.maxRetainedOwners = closure->retainedOwnerCount();
        closure.reset();
        CpuQuotaFactoryFixture pressure(retained.recordingQuota);
        const auto closureBytes = pressure.measureRetained(make);
        pressure.verify(closureBytes, make);
        closure = make();
        assert(closure && !id);
        const auto live = retained.recordingQuota->liveBytes();
        const auto reject = [&](auto bad, const char* expected) {
            const auto saved = budget;
            budget = bad;
            assert(!make());
            is(id, expected);
            budget = saved;
            assert(retained.recordingQuota->liveBytes() == live && !closure->validate());
        };
        auto bad = budget;
        bad.contractVersion = 3u;
        reject(bad, "RHI-WEBGPU-CANONICAL-CLOSURE-TABLE-READBACK-VERSION");
        bad = budget;
        bad.prototypeOptIn = false;
        reject(bad, "RHI-ORIGINAL-NATIVE-CLOSURE-OPT-IN");
        bad = budget;
        --bad.maxReadbackSources;
        reject(bad, "RHI-ORIGINAL-NATIVE-CLOSURE-READBACK-BUDGET");
        bad = budget;
        --bad.maxReadbackCopies;
        reject(bad, "RHI-ORIGINAL-NATIVE-CLOSURE-READBACK-BUDGET");
        bad = budget;
        --bad.maxResourceBindings;
        reject(bad, "RHI-WEBGPU-CANONICAL-CLOSURE-BINDING-CAPACITY");
        bad = budget;
        --bad.maxNativePayloadObjects;
        reject(bad, "RHI-WEBGPU-CANONICAL-CLOSURE-PAYLOAD-CAPACITY");
        bad = budget;
        --bad.maxRetainedOwners;
        reject(bad, "RHI-WEBGPU-CANONICAL-CLOSURE-OWNER-CAPACITY");
        bad = budget;
        --bad.maxRetainedResourceBytes;
        reject(bad, "RHI-WEBGPU-CANONICAL-CLOSURE-ALLOCATION-CAPACITY");
        bad = budget;
        --bad.maxCpuMetadataBytes;
        reject(bad, "RHI-WEBGPU-CANONICAL-CLOSURE-QUOTA");
        assert(!Closure::prepareOriginalTableReadbackLossBound(
            retained.transaction, retained.program, {}, device, budget, *loss, id));
        is(id, "RHI-WEBGPU-READBACK-JOIN-OWNER");
        auto forgedJoin = std::shared_ptr<const rhi_webgpu::WebGpuCanonicalReadbackJoins>(
            retained.quota, joins.get());
        assert(!Closure::prepareOriginalTableReadbackLossBound(
            retained.transaction, retained.program, forgedJoin, device, budget, *loss, id));
        is(id, "RHI-WEBGPU-READBACK-JOIN-OWNER");
        auto old = budget;
        old.contractVersion = 3u;
        old.maxReadbackSources = old.maxReadbackCopies = 0u;
        assert(!Closure::prepareOriginalTableLossBound(
            retained.transaction, retained.program, device, old, *loss, id));
        is(id, "RHI-WEBGPU-CANONICAL-CLOSURE-CONTENT-READBACK-PROFILE");
        const auto& authored = retained.program->originalBudget();
        const rhi::OriginalTypedDependencyProjectionContract::Request projection{3u,
            true,
            {3u,
                true,
                {3u,
                    true,
                    authored.descriptorConsumption,
                    authored.renderDescriptorUsage,
                    25u,
                    4u,
                    12u,
                    6u,
                    11u,
                    1u,
                    1u,
                    512u}},
            8u,
            29u,
            348u,
            66u,
            15u,
            9u};
        Decision::Request request{3u, true, projection, closure->retainedOwnerCount() + 1u, bytes};
        const auto freeze = [&] {
            return Decision::prepareOriginal(closure, request, id);
        };
        const auto freezeBytes = pressure.measureRetained(freeze);
        pressure.verify(freezeBytes, freeze);
        auto decision = freeze();
        assert(decision && !id && decision->records().size() == 8u
            && decision->counts().between == 4u && !decision->synchronizationProven());
        bool transfer = false;
        for (const auto& record : decision->records())
            if (record.original.following
                && record.original.following->scope.kind
                    == rhi::semantics::TypedShaderUsageScopeContract::Kind::TransferCopy)
                transfer = true;
        assert(transfer);
        const auto saved = request;
        request.contractVersion = request.projection.contractVersion = 2u;
        assert(!freeze());
        is(id, "RHI-ORIGINAL-DEPENDENCY-PROJECTION-PROFILE");
        request = saved;
        --request.maxRetainedOwners;
        assert(!freeze());
        is(id, "RHI-WEBGPU-TYPED-DEPENDENCY-OWNER-CAPACITY");
        request = saved;
        --request.maxCpuMetadataBytes;
        assert(!freeze());
        is(id, "RHI-WEBGPU-TYPED-DEPENDENCY-QUOTA");
        request = saved;
        const rhi::OriginalTypedNativePayloadRequest oldPayload{2u,
            true,
            {25u, 0u, 4u, 1u, 1u, 1u, 0u, 8u, 0u, 6u, 1u, 1u, 4u, 16u},
            1u,
            request.maxRetainedOwners + 1u,
            bytes};
        // Previous payload profiles cannot consume the new frozen transfer join.
        is(rhi_webgpu::WebGpuOriginalTypedNativePayload::inspectOriginal(decision, oldPayload)
                .diagnostic,
            "RHI-WEBGPU-TYPED-PAYLOAD-DEPENDENCY-PROFILE");
        using Payload = rhi_webgpu::WebGpuOriginalTypedNativePayload;
        auto payloadRequest = oldPayload;
        payloadRequest.contractVersion = 3u;
        payloadRequest.maxTransferPasses = payloadRequest.maxReadbackCommands =
            payloadRequest.maxReadbackCopies = 1u;
        const auto payloadMake = [&] {
            return Payload::prepareOriginal(decision, payloadRequest, id);
        };
        auto payload = payloadMake();
        if (!payload)
            std::fprintf(stderr, "table Payload3: %s\n", id ? id : "missing");
        assert(payload && !id && payload->work().matches(payloadRequest.maxWork)
            && payload->readbackCommandCount() == 1u && payload->readbackCopyCount() == 1u
            && payload->passes().size() == 4u && payload->records().size() == 25u);
        const auto& copy = payload->records()[22u];
        assert(copy.original.kind == rhi::CommandUsageKind::TextureReadback && copy.readback
            && copy.readback->commandIndex == 22u && copy.readbackCopyCount == 1u
            && copy.readbackCopies && copy.readbackCopies[0u].commandIndex == 22u && copy.binding
            && copy.destination && copy.binding != copy.destination);
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
        assert(copy.texture && copy.buffer
            && copy.texture == copy.binding->backing().texture->texture
            && copy.buffer == copy.destination->backing().buffer->buffer);
#endif
        const auto& layout =
            copy.readback->source->originalSource()->originalRequest()->originalLayout();
        assert(!rhi::semantics::TextureReadbackCopyLowering::validateOriginal(rhi::Backend::WebGPU,
            1u,
            true,
            layout,
            0u,
            layout->mipCopies()[0u],
            copy.readbackCopies[0u].decision));
        const auto payloadBytes = retained.recordingQuota->liveBytes();
        payload.reset();
        const auto payloadCost = payloadBytes - retained.recordingQuota->liveBytes();
        payload = payloadMake();
        assert(payload && !id);
        {
            core::CpuMemoryQuotaLease exhausted(retained.recordingQuota,
                retained.recordingQuota->limitBytes() - retained.recordingQuota->liveBytes());
            assert(exhausted.charged() && !payloadMake() && id);
        }
        const auto refused = [&](auto bad, const char* expected) {
            const auto savedPayload = payloadRequest;
            payloadRequest = bad;
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            const auto inspected = Payload::inspectOriginal(decision, payloadRequest);
            is(inspected.diagnostic, expected);
            assert(!inspected.work.commands && !inspected.colorAttachments
                && !inspected.transferPasses && !inspected.readbackCommands
                && !inspected.readbackCopies
                && allocations == core::defaultCpuAllocator().stats().allocationCount);
            payloadRequest = savedPayload;
        };
        auto badPayload = payloadRequest;
        --badPayload.maxWork.commands;
        refused(badPayload, "RHI-ORIGINAL-TYPED-COLD-WORK-CAPACITY");
        badPayload = payloadRequest;
        --badPayload.maxWork.nativeCalls;
        refused(badPayload, "RHI-ORIGINAL-TYPED-COLD-WORK-CAPACITY");
        badPayload = payloadRequest;
        --badPayload.maxRetainedOwners;
        refused(badPayload, "RHI-WEBGPU-TYPED-PAYLOAD-OWNER-CAPACITY");
        badPayload = payloadRequest;
        --badPayload.maxCpuMetadataBytes;
        refused(badPayload, "RHI-WEBGPU-TYPED-PAYLOAD-QUOTA");
        badPayload = payloadRequest;
        --badPayload.maxReadbackCopies;
        refused(badPayload, "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-BUDGET");
        auto forgedPayload = std::shared_ptr<const Payload>(retained.quota, payload.get());
        is(Payload::validateOriginal(forgedPayload), "RHI-WEBGPU-TYPED-PAYLOAD-OWNER");
        auto oldCold = OriginalTypedNativeColdContractFixture::request(
            OriginalTypedNativeColdContractFixture::required(true));
        for (const auto version : {1u, 2u}) {
            oldCold.contractVersion = version;
            const auto rejected =
                rhi_webgpu::WebGpuOriginalTypedColdAdmission::inspectOriginalPayload(
                    payload, oldCold);
            is(rejected.diagnostic, "RHI-WEBGPU-TYPED-COLD-PAYLOAD-PROFILE");
            assert(!rejected.required.work.commands && !rejected.required.encoders);
        }
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{sources->originalMetadataQuota(),
                retained.graph->originalInputs()->originalMetadataQuota(),
                retained.stateQuota,
                retained.quota,
                retained.recordingQuota},
            [&]() noexcept -> const char* {
                if (const auto* diagnostic = closure->validate())
                    return diagnostic;
                if (const auto* diagnostic = decision->validate())
                    return diagnostic;
                return payload->validate();
            }));
        std::printf(
            "ORIGINAL_TABLE_READBACK_CUSTODY4_OK Graphics same-Join2-root-Stream3 native%llu payload14 owners%u closure%llu freeze%llu dependencies8 transfer-copy hot0 recording-map-result-closed\n",
            static_cast<unsigned long long>(resourceBytes),
            closure->retainedOwnerCount(),
            static_cast<unsigned long long>(closureBytes),
            static_cast<unsigned long long>(freezeBytes));
        std::printf(
            "ORIGINAL_TABLE_READBACK_PAYLOAD3_OK Graphics same-Closure4-Freeze3-Join2 commands25 calls16 passes4 transfer1 readback1 copy1 payloadBytes%llu original-native-endpoints exact-mip-decisions exhaustion forged-owner legacy-cold-closed hot0 recording-map-result-closed\n",
            static_cast<unsigned long long>(payloadCost));

        struct ReadbackCustody {
            std::shared_ptr<const Closure> first;
            std::shared_ptr<const Decision> second;
            std::shared_ptr<const Payload> payload;
        };

        return ReadbackCustody{std::move(closure), std::move(decision), std::move(payload)};
    }

    static void verifyTypedReadbackStorage(
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedColdAdmission>& originalCold,
        const rhi::GpuDevice& device,
        std::uint32_t maxOwners)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        using Storage = rhi_webgpu::WebGpuCanonicalReadbackResultStorage;
        auto cold = originalCold;
        const auto& root = cold->originalRoot();
        const auto sources = root->originalGraph()
                                 ->originalInputs()
                                 ->originalScalars()
                                 ->originalUsage()
                                 ->originalSources();
        assert(cold->requirements().readback.resultDeclarations == 1u
            && cold->requirements().readbackMap.maps == 1u
            && cold->requirements().capacity.callbackTickets == 12u
            && sources->originalReservedCallbackTicketCount() == 12u);
        auto shortSource = sources->originalBudget();
        shortSource.maxCallbackTickets = 11u;
        rhi::OriginalCommandReadbackMapContract::Requirements failedMap;
        is(rhi::OriginalCommandReadbackMapContract::deriveRequirements(
               cold->requirements().readback,
               shortSource,
               8u,
               8u,
               cold->originalRequest().readbackMap,
               failedMap),
            "RHI-ORIGINAL-READBACK-MAP-SOURCE-CALLBACK-CAPACITY");
        assert(!failedMap.maps && !failedMap.totalCallbackTickets);
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        Storage::Budget budget{2u, true, 1u, 512u, bytes, maxOwners};
        const char* id = nullptr;
        const auto make = [&] {
            return Storage::prepareOriginalTyped(cold, device, budget, quota, id);
        };
        const auto metadata = root->originalMetadataQuota();
        const auto baseline = metadata->liveBytes();
        auto storage = make();
        if (!storage)
            std::fprintf(stderr, "table typed Result storage: %s\n", id ? id : "missing");
        assert(storage && !id && !storage->originalAdmission()
            && core::SharedOwnerProjection::sameOwner(storage->originalTypedAdmission(), cold)
            && core::SharedOwnerProjection::sameOwner(storage->originalRoot(), root)
            && storage->snapshots().size() == 1u && storage->snapshots()[0]->byteCapacity() == 512u
            && !storage->snapshots()[0]->publishedBytes() && !storage->nativeExecutionProven());
        const auto metadataCost = metadata->liveBytes() - baseline;
        const auto resultCost = quota->liveBytes();
        const auto reject = [&](auto changed, const char* expected) {
            const auto saved = budget;
            budget = changed;
            assert(!make());
            is(id, expected);
            budget = saved;
            assert(metadata->liveBytes() == baseline + metadataCost
                && quota->liveBytes() == resultCost);
        };
        auto bad = budget;
        bad.contractVersion = 1u;
        reject(bad, "RHI-ORIGINAL-TYPED-READBACK-STORAGE-VERSION");
        bad = budget;
        bad.prototypeOptIn = false;
        reject(bad, "RHI-ORIGINAL-READBACK-STORAGE-OPT-IN");
        bad = budget;
        --bad.maxResultBytes;
        reject(bad, "RHI-ORIGINAL-READBACK-STORAGE-CAPACITY");
        bad = budget;
        bad.maxRetainedOwners = storage->retainedOwnerCount() - 1u;
        reject(bad, "RHI-ORIGINAL-READBACK-STORAGE-OWNER-CAPACITY");
        auto forgedCold =
            std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedColdAdmission>(quota, cold.get());
        assert(!Storage::prepareOriginalTyped(forgedCold, device, budget, quota, id));
        is(id, "RHI-WEBGPU-TYPED-COLD-OWNER");
        forgedCold.reset();
        assert(!Storage::prepareOriginalTyped(cold, device, budget, metadata, id));
        is(id, "RHI-WEBGPU-READBACK-STORAGE-QUOTA-DOMAIN");
        auto forgedStorage = std::shared_ptr<const Storage>(quota, storage.get());
        is(Storage::validateOriginal(forgedStorage), "RHI-WEBGPU-READBACK-STORAGE-OWNER");
        forgedStorage.reset();
        {
            core::CpuMemoryQuotaLease exhausted(quota, quota->limitBytes() - quota->liveBytes());
            assert(exhausted.charged() && !make());
            is(id, "RHI-ORIGINAL-READBACK-SNAPSHOT-ALLOCATION");
        }
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{metadata, quota}, [&]() noexcept {
                    return storage->validate();
                }));
        });
        auto weak = std::weak_ptr<const rhi_webgpu::WebGpuOriginalTypedColdAdmission>(cold);
        originalCold.reset();
        cold.reset();
        assert(
            !weak.expired() && !storage->validate() && !storage->snapshots()[0]->publishedBytes());
        originalCold = storage->originalTypedAdmission();
        storage.reset();
        assert(!quota->liveBytes() && metadata->liveBytes() == baseline);
        std::printf(
            "ORIGINAL_TABLE_READBACK_STORAGE2_OK Graphics same-Cold3-root-seal-Join2 unique1 bytes512 callbacks12 base8-map4 metadata%llu result%llu exact-less-exhausted forged-owner independent-quota caller-drop hot0 query0 map-publication-completion-closed\n",
            static_cast<unsigned long long>(metadataCost),
            static_cast<unsigned long long>(resultCost));
#else
        (void)originalCold;
        (void)device;
        (void)maxOwners;
#endif
    }

    static void verifyReadbackJoins(OriginalCommandTypedStreamFixture::Retained& retained,
        const rhi::GpuDevice& device,
        const rhi::OriginalCommandStateAdmissionBudget& graphBudget,
        const rhi::OriginalTextureContentPlanBudget& contentBudget,
        NativeCustody* deferredCompletion)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        using Joins = rhi_webgpu::WebGpuCanonicalReadbackJoins;
        using Contract = rhi::OriginalCommandReadbackJoinContract;
        const auto& inventory = retained.graph->originalInputs()
                                    ->originalScalars()
                                    ->originalUsage()
                                    ->originalSources()
                                    ->sources();
        const auto source =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureReadbackSource>>(
                inventory[12u]);
        const auto sources =
            retained.graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
        const auto& authored = sources->originalBudget();
        Contract::Budget budget{2u,
            true,
            1u,
            1u,
            1u,
            authored.maxGraphOwners,
            64u,
            512u,
            bytes,
            retained.program->originalBudget(),
            {2u, true, 1u, 1u, 1u, 512u, 512u, bytes}};
        const Joins::Input input{22u, source};
        const char* id = nullptr;
        const auto make = [&] {
            return Joins::prepareOriginalProgram(
                retained.transaction, retained.program, device, &input, 1u, budget, id);
        };
        const auto inspected =
            Contract::inspectProgram(retained.transaction, retained.program, budget);
        if (inspected.diagnostic)
            std::fprintf(stderr, "table readback Join2 inspection: %s\n", inspected.diagnostic);
        assert(!inspected.diagnostic && inspected.required.readbackCommands == 1u
            && inspected.required.uniqueResultBytes == 512u);
        auto joins = make();
        if (!joins)
            std::fprintf(stderr, "table native readback Join2: %s\n", id ? id : "missing");
        assert(joins && !id && joins->joinContractVersion() == 2u && !joins->validate()
            && joins->sourceResourceBytes() == 64u && joins->stagingResourceBytes() == 512u
            && joins->projectedCopyCount() == 1u && joins->readbackCount() == 1u
            && core::SharedOwnerProjection::sameOwner(joins->originalProgram(), retained.program)
            && core::SharedOwnerProjection::sameOwner(joins->originalRoot(), retained.transaction));
        budget.maxRetainedOwners = joins->retainedOwnerCount();
        joins.reset();
        CpuQuotaFactoryFixture pressure(retained.recordingQuota);
        const auto peak = pressure.measureRetained(make);
        pressure.verify(peak, make);
        joins = make();
        assert(joins && !id);
        auto custody = verifyReadbackCustody(retained, device, joins);
        const auto live = retained.recordingQuota->liveBytes();
        const auto reject = [&](auto bad, const char* expected) {
            const auto original = budget;
            budget = bad;
            assert(!make());
            is(id, expected);
            budget = original;
            assert(retained.recordingQuota->liveBytes() == live && !joins->validate());
        };
        auto bad = budget;
        bad.contractVersion = 1u;
        reject(bad, "RHI-ORIGINAL-READBACK-PROGRAM-JOIN-VERSION");
        bad = budget;
        bad.prototypeOptIn = false;
        reject(bad, "RHI-ORIGINAL-READBACK-JOIN-OPT-IN");
        bad = budget;
        bad.result.contractVersion = 1u;
        reject(bad, "RHI-ORIGINAL-MIXED-READBACK-RESULT-VERSION");
        bad = budget;
        bad.stream.contractVersion = 2u;
        reject(bad, "RHI-ORIGINAL-READBACK-PROGRAM-JOIN-STREAM-PROFILE");
        bad = budget;
        --bad.maxRetainedOwners;
        reject(bad, "RHI-WEBGPU-READBACK-JOIN-OWNER-CAPACITY");
        bad = budget;
        --bad.maxSourceResourceBytes;
        reject(bad, "RHI-WEBGPU-READBACK-JOIN-NATIVE-BYTE-CAPACITY");
        bad = budget;
        --bad.maxStagingResourceBytes;
        reject(bad, "RHI-WEBGPU-READBACK-JOIN-NATIVE-BYTE-CAPACITY");
        bad = budget;
        --bad.maxCpuMetadataBytes;
        reject(bad, "RHI-WEBGPU-READBACK-JOIN-QUOTA");
        bad = budget;
        --bad.result.maxDeclaredReadbackBytes;
        reject(bad, "RHI-ORIGINAL-READBACK-RESULT-DECLARED-CAPACITY");
        const auto wrongInput = Joins::Input{23u, source};
        assert(!Joins::prepareOriginalProgram(
            retained.transaction, retained.program, device, &wrongInput, 1u, budget, id));
        is(id, "RHI-WEBGPU-READBACK-JOIN-ORDER-COVERAGE");
        auto forgedProgram = std::shared_ptr<const rhi::OriginalCommandStreamProgram>(
            retained.recordingQuota, retained.program.get());
        assert(!Joins::prepareOriginalProgram(
            retained.transaction, forgedProgram, device, &input, 1u, budget, id));
        is(id, "RHI-ORIGINAL-STREAM-OWNER");
        auto forgedRoot = std::shared_ptr<rhi::OriginalCommandStateTransaction>(
            retained.quota, retained.transaction.get());
        assert(!Joins::prepareOriginalProgram(
            forgedRoot, retained.program, device, &input, 1u, budget, id));
        is(id, "RHI-ORIGINAL-TRANSACTION-OWNER");
        auto peer = rhi::OriginalCommandResourceStates::prepareOriginal(
            retained.graph->originalInputs(), device, graphBudget, retained.stateQuota, id);
        assert(peer && !id);
        auto other =
            OriginalCommandTypedStreamFixture::prepare(peer, retained.program->originalBudget());
        const auto foreign = Contract::inspectProgram(retained.transaction, other.program, budget);
        is(foreign.diagnostic, "RHI-ORIGINAL-STREAM-STATE-IDENTITY");
        assert(!foreign.required.readbackCommands && !foreign.required.nativeCopyCommands
            && !foreign.required.uniqueResultBytes);
        assert(!Joins::prepareOriginalProgram(
            retained.transaction, other.program, device, &input, 1u, budget, id));
        is(id, "RHI-ORIGINAL-STREAM-STATE-IDENTITY");
        // The Source manifest permits one state transaction. An independently
        // authenticated program over that same graph is sufficient to prove
        // program identity, without increasing or evading the root ceiling.
        auto sameGraph = OriginalCommandTypedStreamFixture::prepare(
            retained.graph, retained.program->originalBudget());
        auto foreignJoin = Joins::prepareOriginalProgram(
            retained.transaction, sameGraph.program, device, &input, 1u, budget, id);
        assert(foreignJoin && !id);
        auto loss = device.observeOriginalDeviceNativeLoss(id);
        assert(loss && !id);
        rhi::OriginalCommandNativeClosureBudget closureBudget{4u,
            true,
            12u,
            14u,
            authored.maxGraphOwners,
            custody.first->retainedResourceBytes(),
            bytes,
            1u,
            1u,
            1u,
            1u};
        assert(!rhi_webgpu::WebGpuCanonicalNativeClosure::prepareOriginalTableReadbackLossBound(
            retained.transaction, retained.program, foreignJoin, device, closureBudget, *loss, id));
        is(id, "RHI-WEBGPU-CANONICAL-CLOSURE-READBACK-IDENTITY");
        assert(!rhi_webgpu::WebGpuCanonicalNativeClosure::prepareOriginalTableReadbackLossBound(
            retained.transaction, other.program, joins, device, closureBudget, *loss, id));
        is(id, "RHI-ORIGINAL-STREAM-STATE-IDENTITY");
        foreignJoin.reset();
        sameGraph = {};
        other = {};
        peer.reset();
        assert(!joins->nativeExecutionProven());
        is(joins->executionDiagnostic(), "RHI-ORIGINAL-READBACK-PROGRAM-JOIN-EXECUTION-MISSING");
        // All prior native lifecycle/closure consumers must stay closed.
        is(rhi::OriginalCommandNativeLifecycleContract::inspectOriginalPreparation(
               retained.transaction, {}, {}, {}),
            "RHI-ORIGINAL-NATIVE-LIFECYCLE-CONTENT-READBACK-PROFILE");
        assert(joins->overlapsCpuOwner(retained.quota));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{sources->originalMetadataQuota(),
                retained.graph->originalInputs()->originalMetadataQuota(),
                retained.stateQuota,
                retained.quota,
                retained.recordingQuota},
            [&]() noexcept -> const char* {
                if (const auto* diagnostic = joins->validate())
                    return diagnostic;
                const auto projected =
                    Contract::inspectProgram(retained.transaction, retained.program, budget);
                if (projected.diagnostic)
                    return projected.diagnostic;
                const auto native = rhi_webgpu::webGpuNativePayloadFor(
                    *source->originalDestinationLease()->originalObject());
                const auto buffer = native->buffer;
                native->buffer = nullptr;
                assert(joins->validate() && custody.first->validate() && custody.second->validate()
                    && custody.payload->validate());
                native->buffer = buffer;
                assert(!custody.first->validate() && !custody.second->validate()
                    && !custody.payload->validate());
                return joins->validate();
            }));
        assert(!retained.transaction->applyOriginalPrototypePlan());
        is(joins->validate(), "RHI-WEBGPU-READBACK-JOIN-ROOT-STATUS");
        is(custody.first->validate(), "RHI-WEBGPU-CANONICAL-CLOSURE-PLAN-STATUS");
        is(custody.second->validate(), "RHI-WEBGPU-CANONICAL-CLOSURE-PLAN-STATUS");
        is(custody.payload->validate(), "RHI-WEBGPU-CANONICAL-CLOSURE-PLAN-STATUS");
        assert(!retained.transaction->abandonOriginalPendingOwnership());
        is(joins->validate(), "RHI-ORIGINAL-TRANSACTION-ABANDONED");
        is(custody.first->validate(), "RHI-ORIGINAL-TRANSACTION-ABANDONED");
        is(custody.second->validate(), "RHI-ORIGINAL-TRANSACTION-ABANDONED");
        is(custody.payload->validate(), "RHI-ORIGINAL-TRANSACTION-ABANDONED");
        const auto freezeRequest = custody.second->originalRequest();
        const auto payloadRequest = custody.payload->originalRequest();
        custody = {};
        const auto owners = joins->retainedOwnerCount();
        const auto inputs = retained.graph->originalInputs();
        const auto streamBudget = retained.program->originalBudget();
        auto rootBudget = graphBudget;
        rootBudget.maxRetainedOwners += 6u;
        // The terminal negative root cannot revive. Re-admit the same authored
        // Inputs through the existing factories for subsequent publication/loss
        // probes; the Join never constructs or substitutes a graph itself.
        const auto recaptureOriginalPlan = [&] {
            retained.transaction.reset();
            retained.program.reset();
            retained.graph.reset();
            assert(!sources->originalStateTransactionCount());
            retained.graph = rhi::OriginalCommandResourceStates::prepareOriginal(
                inputs, device, graphBudget, retained.stateQuota, id);
            if (!retained.graph)
                std::fprintf(stderr, "table readback State3 recapture: %s\n", id ? id : "missing");
            assert(retained.graph && !id
                && core::SharedOwnerProjection::sameOwner(
                    retained.graph->originalInputs(), inputs));
            retained.program = rhi::OriginalCommandStreamProgram::prepareOriginalTyped(
                retained.graph, streamBudget, retained.quota, id);
            assert(retained.program && !id
                && !retained.program->compareOriginalStates(retained.graph));
            retained.transaction = rhi::OriginalCommandStateTransaction::prepareOriginalOwnership(
                retained.graph, device, rootBudget, retained.recordingQuota, id);
            if (!retained.transaction)
                std::fprintf(stderr, "table readback root recapture: %s\n", id ? id : "missing");
            assert(retained.transaction && !id
                && !retained.transaction->materializeOriginalPrototypePlan(
                    streamBudget.stateSequence, contentBudget));
        };
        joins.reset();
        recaptureOriginalPlan();
        std::printf(
            "ORIGINAL_TABLE_READBACK_JOIN2_OK Graphics original-ordinal22 Source2 same-parent-root-Stream3 native64 staging512 copy1 peak%llu owners%u exact-less-exhausted forged-foreign-state-order-pending-native-refused hot0 cold-only copy-map-result-closed\n",
            static_cast<unsigned long long>(peak),
            owners);
        // Continue on the existing re-admitted original root. Reuse the frozen
        // profiles without replaying the preceding negative-test matrix.
        const auto makeCustody = [&] {
            joins = make();
            assert(joins && !id);
            custody.first =
                rhi_webgpu::WebGpuCanonicalNativeClosure::prepareOriginalTableReadbackLossBound(
                    retained.transaction,
                    retained.program,
                    joins,
                    device,
                    closureBudget,
                    *loss,
                    id);
            assert(custody.first && !id);
            custody.second = rhi_webgpu::WebGpuOriginalTypedDependencyDecision::prepareOriginal(
                custody.first, freezeRequest, id);
            assert(custody.second && !id);
            custody.payload = rhi_webgpu::WebGpuOriginalTypedNativePayload::prepareOriginal(
                custody.second, payloadRequest, id);
            assert(custody.payload && !id);
        };
        makeCustody();
        using Admission = rhi_webgpu::WebGpuOriginalTypedColdAdmission;
        using Command = rhi_webgpu::WebGpuOriginalTypedColdCommand;
        using Seal = rhi::OriginalCommandRecordingSeal;
        using Reservation = rhi::OriginalCommandLifecycleReservation;
        using Root = rhi::OriginalCommandStateTransaction;
        auto coldRequest = OriginalTypedNativeColdContractFixture::request(
            OriginalTypedNativeColdContractFixture::required(true));
        coldRequest.contractVersion = 3u;
        coldRequest.maxWork = custody.payload->work();
        coldRequest.maxTransfers = {1u, 1u, 1u};
        coldRequest.maxCallbacks = Admission::lifecycleRequirements(3u).callbacks;
        coldRequest.maxRetainedOwners =
            custody.payload->retainedOwnerCount() + Admission::additionalOwners;
        if (mixedIssuersRequested)
            coldRequest.maxRetainedOwners +=
                rhi::OriginalTypedNativeColdCommandContract::additionalOwners
                + rhi::OriginalTypedCallbackCustodyContract::additionalOwners({3u,
                    true,
                    rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
                    1u,
                    8u,
                    1u,
                    1u,
                    bytes,
                    bytes})
                + rhi::OriginalTypedIssuerPreparationContract::additionalOwners;
        coldRequest.maxCpuMetadataBytes = bytes;
        if (mixedResultsRequested) {
            coldRequest.readback = {2u, true, 1u, 1u, 1u, 512u, 512u, bytes};
            coldRequest.readbackMap = {1u, true, 1u, 1u, 3u, 12u, 12u};
            coldRequest.maxRetainedOwners +=
                rhi::OriginalCommandReadbackResultStorageContract::fixedAdditionalOwners + 1u + 4u;
        }
        const auto preclaim = Admission::inspectOriginalPayload(custody.payload, coldRequest);
        if (preclaim.diagnostic)
            std::fprintf(stderr, "table Cold3 preclaim: %s\n", preclaim.diagnostic);
        assert(!preclaim.diagnostic && preclaim.required.contractVersion == 3u
            && preclaim.required.work.matches(custody.payload->work())
            && preclaim.required.transfers.matches(coldRequest.maxTransfers));
        const auto rejectCold = [&](auto bad, const char* expected) {
            const auto rejected = Admission::inspectOriginalPayload(custody.payload, bad);
            is(rejected.diagnostic, expected);
            assert(!rejected.required.contractVersion && !rejected.required.work.commands
                && rejected.required.transfers.empty());
        };
        auto badCold = coldRequest;
        --badCold.maxWork.nativeCalls;
        rejectCold(badCold, "RHI-ORIGINAL-TYPED-COLD-WORK-CAPACITY");
        badCold = coldRequest;
        badCold.maxRetainedOwners = preclaim.required.retainedOwners - 1u;
        rejectCold(badCold, "RHI-ORIGINAL-TYPED-COLD-OWNER-CPU-CAPACITY");
        badCold = coldRequest;
        --badCold.maxCallbacks.recordingScopes;
        rejectCold(badCold, "RHI-ORIGINAL-TYPED-COLD-CALLBACK-CAPACITY");
        badCold = coldRequest;
        badCold.maxTransfers = {};
        rejectCold(badCold, "RHI-ORIGINAL-TYPED-COLD-TRANSFER-BUDGET");
        auto shortSource = authored;
        --shortSource.maxTypedNativeCommandCalls;
        is(rhi::OriginalTypedNativeColdContract::validateSourceAuthority(
               preclaim.required, custody.first->nativePayloadCount(), shortSource),
            "RHI-ORIGINAL-TYPED-COLD-SOURCE-NATIVE-CALL-CAPACITY");
        shortSource = authored;
        --shortSource.maxNativePayloadObjects;
        is(rhi::OriginalTypedNativeColdContract::validateSourceAuthority(
               preclaim.required, custody.first->nativePayloadCount(), shortSource),
            "RHI-ORIGINAL-TYPED-COLD-SOURCE-PAYLOAD-CAPACITY");
        const rhi::OriginalTypedNativeColdCommandRequest commandRequest{3u,
            true,
            1u,
            coldRequest.maxRetainedOwners
                + (mixedIssuersRequested
                        ? 0u
                        : rhi::OriginalTypedNativeColdCommandContract::additionalOwners),
            bytes};
        auto submission =
            core::makeShared<core::CpuMemoryQuota>(authored.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Submission)]);
        auto callback =
            core::makeShared<core::CpuMemoryQuota>(authored.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Callback)]);
        const rhi::OriginalCommandNativeLifecycleBudget lifecycle{2u,
            true,
            1u,
            preclaim.required.capacity.callbackTickets,
            preclaim.required.capacity.callbackRegistrations,
            commandRequest.maxRetainedOwners,
            commandRequest.maxRetainedOwners,
            commandRequest.maxRetainedOwners,
            bytes,
            submission->limitBytes(),
            callback->limitBytes()};
        auto legacyLifecycle = lifecycle;
        legacyLifecycle.contractVersion = 1u;
        assert(!Seal::prepareOriginal(
            retained.transaction, device, legacyLifecycle, submission, callback, id));
        is(id, "RHI-ORIGINAL-NATIVE-LIFECYCLE-CONTENT-READBACK-PROFILE");
        std::shared_ptr<const Seal> seal;
        std::shared_ptr<const Reservation> reservation;
        std::shared_ptr<const Admission> cold;
        const auto bindCold = [&] {
            seal = Seal::prepareOriginal(
                retained.transaction, device, lifecycle, submission, callback, id);
            if (!seal)
                std::fprintf(stderr, "table Lifecycle2: %s\n", id ? id : "missing");
            assert(seal && !id);
            reservation = Reservation::prepareOriginal(seal, preclaim.required.capacity, id);
            assert(reservation && !id);
            cold = Admission::prepareOriginal(custody.payload, reservation, coldRequest, id);
            assert(cold && !id);
        };
        bindCold();
        if (mixedResultsRequested)
            verifyTypedReadbackStorage(cold, device, commandRequest.maxRetainedOwners);
        assert(retained.transaction->applyOriginalPrototypePlan());
        assert(retained.transaction->status() == Root::Status::Prepared
            && retained.transaction->journalEmpty());
        std::printf(
            "ORIGINAL_TABLE_READBACK_COLD3_PROGRESS sealed same-root lifecycle2 reservation%u Prepared CPU-apply-refused\n",
            preclaim.required.capacity.callbackTickets);
        const auto admit = [&] {
            return Admission::prepareOriginal(custody.payload, reservation, coldRequest, id);
        };
        const auto admissionBytes = mixedIssuersRequested ? 0u : pressure.measureRetained(admit);
        if (!mixedIssuersRequested)
            pressure.verify(admissionBytes, admit);
        auto wrongCommand = commandRequest;
        wrongCommand.contractVersion = 2u;
        assert(!Command::prepareOriginal(cold, wrongCommand, id));
        is(id, "RHI-ORIGINAL-TYPED-COLD-COMMAND-ADMISSION-PROFILE");
        auto forgedCold = std::shared_ptr<const Admission>(callback, cold.get());
        assert(!Command::prepareOriginal(forgedCold, commandRequest, id));
        is(id, "RHI-WEBGPU-TYPED-COLD-OWNER");
        forgedCold.reset();
        const auto rebuild = [&] {
            cold.reset();
            reservation.reset();
            seal.reset();
            assert(retained.transaction->status() == Root::Status::Abandoned
                && retained.transaction->journalEmpty());
            custody = {};
            joins.reset();
            retained.transaction.reset();
            assert(!sources->originalStateTransactionCount()
                && !sources->originalReservedSubmissionCount()
                && !sources->originalReservedCallbackTicketCount()
                && !sources->originalReservedCallbackRegistrationCount());
            retained.transaction = Root::prepareOriginalOwnership(
                retained.graph, device, rootBudget, retained.recordingQuota, id);
            assert(retained.transaction && !id
                && !retained.transaction->materializeOriginalPrototypePlan(
                    streamBudget.stateSequence, contentBudget));
            makeCustody();
            bindCold();
            assert(core::SharedOwnerProjection::sameOwner(retained.graph->originalInputs(), inputs)
                && core::SharedOwnerProjection::sameOwner(
                    cold->originalRoot(), retained.transaction));
        };
        const auto connect = [&] {
            return Command::prepareOriginal(cold, commandRequest, id);
        };
        const auto commandBytes = mixedIssuersRequested ? 0u : pressure.measureRetained(connect);
        std::puts(mixedIssuersRequested
                ? "ORIGINAL_TABLE_READBACK_COLD3_PROGRESS command-pressure covered-by-native-execution-smoke"
                : "ORIGINAL_TABLE_READBACK_COLD3_PROGRESS command-cost-measured original-claim terminal-rollback");
        if (!mixedIssuersRequested)
            pressure.verifySingleUse(commandBytes, rebuild, connect);
        if (!mixedIssuersRequested)
            std::puts(
                "ORIGINAL_TABLE_READBACK_COLD3_PROGRESS command-pressure exact-less-exhausted same-original-Inputs-root");
        const auto recordingBeforeCommand = retained.recordingQuota->liveBytes();
        auto command = connect();
        assert(command && !id && !command->validate());
        assert(!connect());
        is(id, "RHI-WEBGPU-TYPED-COLD-COMMAND-DUPLICATE");
        for (const auto version : {1u, 2u}) {
            const rhi::OriginalTypedCallbackCustodyRequest oldCallback{version,
                true,
                rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
                1u,
                8u,
                1u,
                commandRequest.maxRetainedOwners,
                bytes,
                bytes};
            assert(!rhi_webgpu::WebGpuOriginalTypedCallbackCustody::prepareOriginal(
                command, oldCallback, id));
            is(id, "RHI-TYPED-CALLBACK-CUSTODY-ORIGINAL-SUBSCRIPTION");
        }
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sources->originalMetadataQuota(),
                    inputs->originalMetadataQuota(),
                    retained.stateQuota,
                    retained.quota,
                    retained.recordingQuota,
                    submission,
                    callback},
                [&]() noexcept -> const char* {
                    auto forgedCommand = std::shared_ptr<const Command>(callback, command.get());
                    is(Command::validateOriginal(forgedCommand),
                        "RHI-WEBGPU-TYPED-COLD-COMMAND-OWNER");
                    const auto native = rhi_webgpu::webGpuNativePayloadFor(
                        *source->originalDestinationLease()->originalObject());
                    const auto buffer = native->buffer;
                    native->buffer = nullptr;
                    assert(cold->validate() && command->validate());
                    native->buffer = buffer;
                    return command->validate();
                }));
        });
        if (mixedCompletionRequested) {
            assert(deferredCompletion);
            *deferredCompletion = {custody.first,
                custody.second,
                custody.payload,
                cold,
                command,
                submission,
                callback};
            std::puts(
                "ORIGINAL_TABLE_READBACK_COLD3_OK Graphics lifecycle2 cold3 command3 same-Closure4-Freeze3-Payload3-Join2 callbacks12 same-original-deferred-completion after-publication-probes");
            return;
        }
        if (mixedIssuersRequested && !mixedResultsRequested) {
            const auto rebuildCommand = [&] {
                command.reset();
                rebuild();
                command = connect();
                assert(command && !id);
            };
            using MixedRetained = std::remove_reference_t<decltype(retained)>;
            using Rebuild = decltype(rebuildCommand);

            // Adapt the same original chain to the reusable custody/issuer test
            // machinery. The adapter owns no resource, graph or native payload.
            struct MixedLive {
                MixedRetained& retained;
                std::shared_ptr<const Command>& command;
                std::uint64_t recordingBeforeCommand;
                std::shared_ptr<core::CpuMemoryQuota> submission, callback;
                rhi::OriginalTypedCallbackCustodyRequest request;
                std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedCallbackCustody> custody;
                std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedIssuerPreparation> coldIssuers;
                Rebuild& restore;

                void rebuild()
                {
                    coldIssuers.reset();
                    custody.reset();
                    restore();
                }
            } live{retained,
                command,
                recordingBeforeCommand,
                submission,
                callback,
                {3u,
                    true,
                    rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
                    1u,
                    8u,
                    1u,
                    commandRequest.maxRetainedOwners,
                    submission->limitBytes(),
                    callback->limitBytes()},
                {},
                {},
                rebuildCommand};

            OriginalTypedCallbackCustodyFixture::verifyMixed(live, mixedRecordingRequested);
        } else if (mixedResultsRequested) {
            using Storage = rhi_webgpu::WebGpuCanonicalReadbackResultStorage;
            auto resultQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
            auto resultStorage = Storage::prepareOriginalTyped(cold,
                device,
                {2u, true, 1u, 512u, bytes, commandRequest.maxRetainedOwners},
                resultQuota,
                id);
            assert(resultStorage && !id);
            const rhi::OriginalTypedCallbackCustodyRequest mapRequest{3u,
                true,
                rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
                1u,
                12u,
                1u,
                commandRequest.maxRetainedOwners,
                submission->limitBytes(),
                callback->limitBytes(),
                {1u, true}};
            OriginalTypedCallbackCustodyFixture::verifyMixedMapCustody(
                command, std::move(resultStorage), mapRequest, submission, callback, resultQuota);
        }
        std::weak_ptr<const Admission> retainedCold = cold;
        cold.reset();
        reservation.reset();
        seal.reset();
        custody = {};
        joins.reset();
        if (!mixedIssuersRequested) {
            assert(!retainedCold.expired() && !command->validate()
                && sources->originalReservedCallbackTicketCount() == 8u);
            std::printf(
                "ORIGINAL_TABLE_READBACK_COLD3_OK Graphics lifecycle2 cold3 command3 same-Closure4-Freeze3-Payload3-Join2 commands25 calls16 transfer1 readback1 copy1 callbacks8 owners%u admissionBytes%llu commandBytes%llu exact-less-exhausted forged-owner native-mutation sticky-claim caller-drop hot0 query0 callback1-2-rejected encoder0 submit0 copy-map-result-closed\n",
                command->retainedOwnerCount(),
                static_cast<unsigned long long>(admissionBytes),
                static_cast<unsigned long long>(commandBytes));
        } else if (mixedResultsRequested) {
            std::puts(
                "ORIGINAL_TABLE_READBACK_COLD3_OK Graphics lifecycle2 cold3 command3 same-Closure4-Freeze3-Payload3-Join2 callbacks12 unique-result1 independent-Result512 exact-map-receipts-prepared encoder0 submit0 completion-closed");
        } else if (mixedRecordingRequested) {
            std::puts(
                "ORIGINAL_TABLE_READBACK_COLD3_OK Graphics lifecycle2 cold3 command3 same-Closure4-Freeze3-Payload3-Join2 quota-pressure=covered-by-native-execution-smoke callbacks3-issuers3-recorded-and-cancelled encoder1 submit0 map-result-completion-closed");
        } else {
            std::puts(
                "ORIGINAL_TABLE_READBACK_COLD3_OK Graphics lifecycle2 cold3 command3 same-Closure4-Freeze3-Payload3-Join2 quota-pressure=covered-by-native-execution-smoke callbacks3-issuers3-cancelled encoder0 submit0 copy-map-result-closed");
        }
        command.reset();
        assert(retainedCold.expired() && retained.transaction->status() == Root::Status::Abandoned
            && retained.transaction->journalEmpty() && !sources->originalReservedSubmissionCount()
            && !sources->originalReservedCallbackTicketCount()
            && !sources->originalReservedCallbackRegistrationCount() && !submission->liveBytes()
            && !callback->liveBytes());
        // Genuine recording applies and rolls back the journal. Resource values
        // recover, but revisions advance: the old captured State/Stream cannot
        // authorize the next candidate. Keep the same Inputs and authored quotas.
        if (mixedRecordingRequested) {
            assert(
                retained.graph->validate() && retained.program->validate() && !inputs->validate());
            recaptureOriginalPlan();
            assert(!retained.graph->validate() && !retained.program->validate()
                && !retained.transaction->validate());
        } else {
            // Preparation-only cancellation never applied the journal; retain
            // its still-valid captured State/Stream for the next candidate.
            retained.transaction.reset();
            assert(!sources->originalStateTransactionCount());
            retained.transaction = Root::prepareOriginalOwnership(
                retained.graph, device, rootBudget, retained.recordingQuota, id);
            assert(retained.transaction && !id
                && !retained.transaction->materializeOriginalPrototypePlan(
                    streamBudget.stateSequence, contentBudget));
        }
        if (mixedRecordingRequested)
            std::puts(
                "ORIGINAL_TABLE_READBACK_RECAPTURE3_OK Graphics terminal-cancel old-State-Stream-refused same-Inputs-resources-budgets fresh-State-Stream-root publication-loss-probes-retained");
#endif
    }
};
} // namespace arp::test
