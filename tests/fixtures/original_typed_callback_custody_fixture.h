#pragma once

#include "original_observed_native_loss_binding_fixture.h"
#include "original_typed_issuer_preparation_fixture.h"
#include "original_typed_shader_dependency_probe_fixture.h"
#include "rhi/backends/webgpu/webgpu_canonical_readback_result_storage.h"
#include "rhi/backends/webgpu/webgpu_original_typed_callback_custody.h"
#include "rhi/semantics/command_lowering.h"
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
#include "webgpu_error_scope_handoff_fixture.h"
#endif

namespace arp::test {
class OriginalTypedCallbackCustodyFixture final {
    friend class OriginalTypedNativeExecutionFixture;
    using C = rhi::OriginalTypedCallbackCustodyContract;
    using Custody = rhi_webgpu::WebGpuOriginalTypedCallbackCustody;
    using Command = rhi_webgpu::WebGpuOriginalTypedColdCommand;
    using Admission = rhi_webgpu::WebGpuOriginalTypedColdAdmission;
    using Seal = rhi::OriginalCommandRecordingSeal;
    using Reservation = rhi::OriginalCommandLifecycleReservation;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || core::StringView(actual) != expected)
            std::fprintf(stderr,
                "typed custody expected %s got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && core::StringView(actual) == expected);
    }

public:
    static void verifyContract()
    {
        auto cold = Admission::lifecycleRequirements();
        C::Participants participants;
        C::Request request{1u,
            true,
            rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
            1u,
            8u,
            1u,
            128u,
            65536u,
            65536u};
        assert(!C::inspect(cold, request, participants));
        assert(participants.sharedNativeLossRegistrations == 1u
            && participants.sharedNativeLossCallbacks == 1u
            && participants.commandNativeRegistrations == 7u
            && participants.commandNativeCallbacks == 7u
            && participants.localObserverRegistrations == 1u
            && participants.localObserverCallbacks == 1u && participants.reservedRegistrations == 8u
            && participants.reservedCallbacks == 8u);
        auto cold2 = cold;
        cold2.contractVersion = 2u;
        is(C::inspect(cold2, request, participants),
            "RHI-TYPED-CALLBACK-CUSTODY-ORIGINAL-SUBSCRIPTION");
        assert(!participants.reservedRegistrations && !participants.sharedNativeLossRegistrations);
        auto request2 = request;
        request2.contractVersion = 2u;
        assert(!C::inspect(cold2, request2, participants) && participants.reservedCallbacks == 8u);
        is(C::inspect(cold, request2, participants),
            "RHI-TYPED-CALLBACK-CUSTODY-ORIGINAL-SUBSCRIPTION");
        assert(!participants.reservedCallbacks && !participants.sharedNativeLossRegistrations);
        auto cold3 = cold;
        cold3.contractVersion = 3u;
        cold3.transfers = {1u, 1u, 1u};
        auto request3 = request;
        request3.contractVersion = 3u;
        assert(!C::inspect(cold3, request3, participants) && participants.reservedCallbacks == 8u);
        auto withMaps = cold3;
        withMaps.readback.resultDeclarations = 1u;
        withMaps.readbackMap = {1u, 1u, 3u, 12u, 12u};
        withMaps.capacity.callbackTickets = withMaps.capacity.callbackRegistrations = 12u;
        auto mapRequest = request3;
        mapRequest.maxCallbackSlots = 12u;
        is(C::inspect(withMaps, mapRequest, participants),
            "RHI-TYPED-CALLBACK-CUSTODY-READBACK-MAP-VERSION");
        assert(!participants.reservedCallbacks);
        mapRequest.readbackMap = {1u, false};
        is(C::inspect(withMaps, mapRequest, participants),
            "RHI-TYPED-CALLBACK-CUSTODY-READBACK-MAP-OPT-IN");
        mapRequest.readbackMap.prototypeOptIn = true;
        assert(!C::inspect(withMaps, mapRequest, participants));
        assert(participants.commandNativeCallbacks == 11u && participants.reservedCallbacks == 12u
            && participants.localObserverCallbacks == 1u && C::additionalOwners(mapRequest) == 15u);
        is(C::inspect(cold3, mapRequest, participants),
            "RHI-TYPED-CALLBACK-CUSTODY-READBACK-MAP-PROFILE");
        for (const auto defect : {0u, 1u, 2u, 3u, 4u}) {
            auto wrong = withMaps;
            if (defect == 0u)
                --wrong.readbackMap.mapCallbacks;
            if (defect == 1u)
                --wrong.readbackMap.scopeCallbacks;
            if (defect == 2u)
                --wrong.readbackMap.totalCallbackTickets;
            if (defect == 3u)
                --wrong.readbackMap.totalCallbackRegistrations;
            if (defect == 4u)
                wrong.readbackMap.maps = UINT32_MAX;
            is(C::inspect(wrong, mapRequest, participants),
                "RHI-TYPED-CALLBACK-CUSTODY-READBACK-MAP-REQUIREMENTS");
            assert(!participants.reservedCallbacks && !participants.sharedNativeLossRegistrations);
        }
        auto shortMap = mapRequest;
        --shortMap.maxCallbackSlots;
        is(C::inspect(withMaps, shortMap, participants), "RHI-TYPED-CALLBACK-CUSTODY-PARTICIPANTS");
        for (const auto version : {1u, 2u, 3u}) {
            auto candidate = request;
            candidate.contractVersion = version;
            for (const auto other : {1u, 2u, 3u}) {
                auto required = cold;
                required.contractVersion = other;
                if (other == 3u)
                    required.transfers = {1u, 1u, 1u};
                if (version == other)
                    assert(!C::inspect(required, candidate, participants));
                else {
                    is(C::inspect(required, candidate, participants),
                        "RHI-TYPED-CALLBACK-CUSTODY-ORIGINAL-SUBSCRIPTION");
                    assert(!participants.reservedCallbacks
                        && !participants.sharedNativeLossRegistrations);
                }
            }
        }
        auto cold4 = Admission::lifecycleRequirements(4u);
        cold4.work.renderPasses = 1u;
        cold4.work.indexBinds = cold4.work.indexedDraws = 1u;
        auto request4 = request;
        request4.contractVersion = 4u;
        assert(!C::inspect(cold4, request4, participants) && participants.reservedCallbacks == 8u);
        auto noGeometry = cold4;
        noGeometry.work = {};
        is(C::inspect(noGeometry, request4, participants),
            "RHI-TYPED-CALLBACK-CUSTODY-GEOMETRY-PROFILE");
        assert(!participants.reservedCallbacks);
        auto mixedGeometry = cold4;
        mixedGeometry.transfers = {1u, 1u, 1u};
        is(C::inspect(mixedGeometry, request4, participants),
            "RHI-TYPED-CALLBACK-CUSTODY-TRANSFER-PROFILE");
        for (const auto version : {1u, 2u, 3u}) {
            auto legacy = request;
            legacy.contractVersion = version;
            is(C::inspect(cold4, legacy, participants),
                "RHI-TYPED-CALLBACK-CUSTODY-ORIGINAL-SUBSCRIPTION");
        }
        auto absentTransfer = cold3;
        absentTransfer.transfers = {};
        is(C::inspect(absentTransfer, request3, participants),
            "RHI-TYPED-CALLBACK-CUSTODY-TRANSFER-PROFILE");
        assert(!participants.reservedCallbacks && !participants.sharedNativeLossRegistrations);
        auto cold6 = Admission::lifecycleRequirements(6u);
        auto request6 = request;
        request6.contractVersion = 6u;
        request6.maxRetainedOwners = 237u;
        assert(!C::inspect(cold6, request6, participants) && participants.reservedCallbacks == 8u
            && participants.commandNativeCallbacks == 7u && C::additionalOwners(request6) == 11u);
        for (const auto old : {1u, 2u, 3u, 4u, 5u}) {
            auto legacy = request6;
            legacy.contractVersion = old;
            assert(C::inspect(cold6, legacy, participants) && !participants.reservedCallbacks);
        }
        auto unexpected = cold6;
        unexpected.transfers = {1u, 1u, 1u};
        assert(C::inspect(unexpected, request6, participants) && !participants.reservedCallbacks);
        unexpected = cold6;
        unexpected.work.indexBinds = 1u;
        assert(C::inspect(unexpected, request6, participants) && !participants.reservedCallbacks);
        auto map6 = request6;
        map6.readbackMap = {1u, true};
        assert(C::inspect(cold6, map6, participants) && !participants.reservedCallbacks);
        rhi::OriginalCommandAdmissionBudget source6;
        source6.maxGraphOwners = 48u;
        rhi::OriginalCommandNativeLifecycleBudget lifecycle6{
            3u, true, 1u, 8u, 8u, 237u, 237u, 237u, 65536u, 65536u, 65536u};
        assert(!C::validateOwners(226u, 0u, request6, 237u, 237u, source6, lifecycle6, 46u));
        auto short6 = request6;
        --short6.maxRetainedOwners;
        assert(C::validateOwners(226u, 0u, short6, 237u, 237u, source6, lifecycle6, 46u));
        short6 = request6;
        ++short6.maxRetainedOwners;
        assert(C::validateOwners(226u, 0u, short6, 237u, 237u, source6, lifecycle6, 46u));
        assert(C::validateOwners(226u, 0u, request6, 236u, 237u, source6, lifecycle6, 46u));
        assert(C::validateOwners(226u, 0u, request6, 237u, 236u, source6, lifecycle6, 46u));
        assert(C::validateOwners(226u, 0u, request6, 237u, 237u, source6, lifecycle6, 49u));
        assert(C::validateOwners(226u, 1u, request6, 237u, 237u, source6, lifecycle6, 46u));
        assert(C::validateOwners(UINT32_MAX, 0u, request6, 237u, 237u, source6, lifecycle6, 46u));
        assert(C::validateOwners(226u, UINT32_MAX, request6, 237u, 237u, source6, lifecycle6, 46u));
        for (const auto member : {&rhi::OriginalCommandNativeLifecycleBudget::maxRecordingOwners,
                 &rhi::OriginalCommandNativeLifecycleBudget::maxSubmissionOwners,
                 &rhi::OriginalCommandNativeLifecycleBudget::maxCallbackOwners}) {
            auto shortLifecycle = lifecycle6;
            --(shortLifecycle.*member);
            assert(C::validateOwners(226u, 0u, request6, 237u, 237u, source6, shortLifecycle, 46u));
        }
        lifecycle6.contractVersion = 2u;
        assert(C::validateOwners(226u, 0u, request6, 237u, 237u, source6, lifecycle6, 46u));
        auto legacyTransfer = cold;
        legacyTransfer.transfers = {1u, 1u, 1u};
        is(C::inspect(legacyTransfer, request, participants),
            "RHI-TYPED-CALLBACK-CUSTODY-TRANSFER-PROFILE");
        assert(!participants.reservedCallbacks && !participants.sharedNativeLossRegistrations);
        const auto negative = [&](auto edit, const char* expected) {
            auto wrong = request;
            edit(wrong);
            participants = {1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u};
            is(C::inspect(cold, wrong, participants), expected);
            assert(
                !participants.reservedRegistrations && !participants.sharedNativeLossRegistrations);
        };
        negative(
            [](auto& r) {
                r.contractVersion = 0u;
            },
            "RHI-TYPED-CALLBACK-CUSTODY-VERSION");
        negative(
            [](auto& r) {
                r.contractVersion = 9u;
            },
            "RHI-TYPED-CALLBACK-CUSTODY-VERSION");
        negative(
            [](auto& r) {
                r.prototypeOptIn = false;
            },
            "RHI-TYPED-CALLBACK-CUSTODY-OPT-IN");
        negative(
            [](auto& r) {
                r.lossPolicy = rhi::OriginalTypedLossObserverPolicy::Unspecified;
            },
            "RHI-TYPED-CALLBACK-CUSTODY-LOSS-POLICY");
        negative(
            [](auto& r) {
                --r.maxCallbackSlots;
            },
            "RHI-TYPED-CALLBACK-CUSTODY-PARTICIPANTS");
        negative(
            [](auto& r) {
                r.maxLocalLossObservers = 2u;
            },
            "RHI-TYPED-CALLBACK-CUSTODY-PARTICIPANTS");
        negative(
            [](auto& r) {
                r.maxCpuCallbackBytes = 0u;
            },
            "RHI-TYPED-CALLBACK-CUSTODY-BUDGET");
        assert(C::additionalOwners(request) == 11u);
        request.maxCallbackSlots = UINT32_MAX;
        assert(!C::additionalOwners(request));
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            const auto mapLowering =
                arp::rhi::semantics::OriginalTypedReadbackMapCustodyLowering::lowering(backend);
            assert(mapLowering.validatorOnly == (backend == rhi::Backend::Null)
                && mapLowering.preparationAvailable == (backend == rhi::Backend::WebGPU)
                && !mapLowering.mappingAvailable && !mapLowering.completionAvailable
                && mapLowering.diagnostic);
            assert(
                !arp::rhi::semantics::OriginalTypedReadbackMapCustodyLowering::lowering(backend, 0u)
                    .preparationAvailable);
            const auto lowering =
                arp::rhi::semantics::OriginalTypedCallbackCustodyLowering::lowering(backend);
            assert(lowering.observerPreparationAvailable == (backend == rhi::Backend::WebGPU));
            assert(!lowering.recordingAvailable && !lowering.executionAvailable
                && lowering.diagnostic);
            const auto v2 =
                arp::rhi::semantics::OriginalTypedCallbackCustodyLowering::lowering(backend, 2u);
            assert(v2.validatorOnly == lowering.validatorOnly
                && v2.observerPreparationAvailable == lowering.observerPreparationAvailable);
            assert(!v2.recordingAvailable && !v2.executionAvailable && v2.diagnostic);
            const auto v3 =
                arp::rhi::semantics::OriginalTypedCallbackCustodyLowering::lowering(backend, 3u);
            assert(v3.observerPreparationAvailable == lowering.observerPreparationAvailable
                && !v3.recordingAvailable && !v3.executionAvailable);
            const auto v4 =
                arp::rhi::semantics::OriginalTypedCallbackCustodyLowering::lowering(backend, 4u);
            assert(v4.observerPreparationAvailable == lowering.observerPreparationAvailable
                && !v4.recordingAvailable && !v4.executionAvailable);
            const auto v6 =
                arp::rhi::semantics::OriginalTypedCallbackCustodyLowering::lowering(backend, 6u);
            assert(v6.observerPreparationAvailable == lowering.observerPreparationAvailable
                && v6.validatorOnly == lowering.validatorOnly && !v6.recordingAvailable
                && !v6.executionAvailable && v6.diagnostic);
            const auto graphics =
                arp::rhi::semantics::OriginalTypedCallbackCustodyLowering::lowering(backend, 8u);
            assert(graphics.observerPreparationAvailable == (backend == rhi::Backend::WebGPU)
                && !graphics.recordingAvailable && !graphics.executionAvailable);
        }
        const char* id = nullptr;
        assert(!Custody::prepareOriginal({}, request, id));
        is(id, "RHI-WEBGPU-TYPED-COLD-COMMAND-OWNER");
        is(Custody::validateOriginal({}), "RHI-WEBGPU-TYPED-CALLBACK-OWNER");
        std::puts(
            "BINDLESS_RHI_TYPED_CALLBACK_CONTRACT_OK explicit-policy1 shared-native-loss1 native-scope-queue7 local-observer1 demand8 owner-add11 four-recording-execution-gates-closed");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
private:
    using Builder = OriginalTypedShaderDependencyProbeFixture;
    using Context = OriginalAcceptedComputePipelineFactoryFixture::PreparedContext;
    using Lease = rhi::OriginalCommandBufferLease;
    static constexpr std::uint64_t bytes = Builder::fixtureMetadataBytes;
    static constexpr std::uint32_t owners = Builder::fixtureOwnerCeiling;

    struct Live {
        core::UniquePtr<Context> context;
        Builder::Actor actor;
        OriginalCommandDescriptorStateFixture::Retained retained;
        std::shared_ptr<core::CpuMemoryQuota> sourceQuota, graphQuota, submission, callback;
        std::array<std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>, Builder::groupCount>
            writes;
        std::optional<rhi::OriginalDeviceNativeLossObservation> observation;
        std::shared_ptr<const Seal> seal;
        std::shared_ptr<const Reservation> reservation;
        std::shared_ptr<const Admission> admission;
        std::shared_ptr<const Command> command;
        std::shared_ptr<const Custody> custody;
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedIssuerPreparation> coldIssuers;
        C::Request request{1u,
            true,
            rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
            1u,
            8u,
            1u,
            owners,
            bytes,
            bytes};

        void bind()
        {
            const char* id = nullptr;
            OriginalTypedColdAdmissionFixture::freezeComputeOriginal(
                retained, *context->device, *observation, id);
            auto admissionRequest =
                OriginalTypedColdAdmissionFixture::requestFor(retained.typedNativePayload);
            admissionRequest.maxRetainedOwners = owners;
            const auto demand =
                Admission::inspectOriginalPayload(retained.typedNativePayload, admissionRequest);
            assert(!demand.diagnostic);
            seal = Seal::prepareOriginal(retained.transaction,
                *context->device,
                {1u, true, 1u, 8u, 8u, owners, owners, owners, bytes, bytes, bytes},
                submission,
                callback,
                id);
            assert(seal);
            reservation = Reservation::prepareOriginal(seal, demand.required.capacity, id);
            assert(reservation);
            admission = Admission::prepareOriginal(
                retained.typedNativePayload, reservation, admissionRequest, id);
            assert(admission);
            command = Command::prepareOriginal(admission, {1u, true, 1u, owners, bytes}, id);
            assert(command && !id);
        }

        void rebuild()
        {
            coldIssuers.reset();
            custody.reset();
            command.reset();
            admission.reset();
            reservation.reset();
            seal.reset();
            retained.typedNativePayload.reset();
            retained.dependencyDecision.reset();
            retained.nativeClosure.reset();
            retained.transaction.reset();
            const char* id = nullptr;
            OriginalCommandDescriptorStateFixture::prepareRetainedPlan(
                retained, *context->device, id);
            bind();
        }
    };

    static inline std::array<core::UniquePtr<Live>, 3u> losses_;
    static inline core::UniquePtr<Live> deferred_;

    static void countStart()
    {
        EM_ASM({
            // clang-format off
                const p = Module.preinitializedWebGPUDevice.lost;
                Module.arpTypedCustodyThen = p.then;
                Module.arpTypedCustodyRegistrations = 0;
                p.then = function(callback) {
                    ++Module.arpTypedCustodyRegistrations;
                    return Module.arpTypedCustodyThen.call(this, callback);
                };
            // clang-format on
        });
    }

    static void countEnd(unsigned expected)
    {
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpTypedCustodyRegistrations;
            // clang-format on
        }) == static_cast<int>(expected));
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.lost.then = Module.arpTypedCustodyThen;
                delete Module.arpTypedCustodyThen;
                delete Module.arpTypedCustodyRegistrations;
            // clang-format on
        });
    }

    template <bool finite = false,
        bool targetMemory = false,
        class Factory,
        class Input,
        class Layout>
    static auto prepare(Factory&& create,
        const Input& input,
        const Layout& layout,
        bool observed = true,
        std::uint32_t dispatchX = 1u,
        bool materialize = true)
    {
        auto live = core::makeUnique<Live>();
        const rhi::OriginalDeviceNativeLossObserverRequest request{1u, true, 3u, 4u, bytes};
        live->context = OriginalAcceptedComputePipelineFactoryFixture::prepareContext(
            create, input, layout, 3u, 4u, 7u, observed ? &request : nullptr);
        live->context->ready();
        const char* id = nullptr;
        auto pipeline =
            rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(live->context->publish(), id);
        assert(pipeline);
        live->sourceQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        live->graphQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        live->submission = core::makeShared<core::CpuMemoryQuota>(bytes);
        live->callback = core::makeShared<core::CpuMemoryQuota>(bytes);
        std::array<std::shared_ptr<const Lease>, 3u> buffers;
        for (std::size_t i = 0u; i < buffers.size(); ++i) {
            rhi::BufferDesc desc;
            desc.sizeBytes = i == 2u ? 8u : 16u;
            desc.usage = i == 2u ? rhi::ResourceUsage::CopyDst
                                 : rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopySrc
                    | rhi::ResourceUsage::CopyDst;
            desc.memory = i == 2u ? rhi::MemoryUsage::GpuToCpu : rhi::MemoryUsage::GpuOnly;
            desc.initialState = rhi::ResourceState::Undefined;
            core::UniquePtr<rhi::GpuBuffer> object;
            {
                core::CpuMemoryQuotaScope scope(live->context->quotas[2]);
                object = live->context->device->createBuffer(desc);
            }
            assert(object);
            buffers[i] =
                Lease::prepareOriginal(object, {1u, true, 2u, bytes}, live->sourceQuota, id);
            assert(buffers[i]);
        }
        live->actor = Builder::prepareActor<finite, targetMemory>(*live->context,
            pipeline,
            buffers,
            layout,
            live->sourceQuota,
            live->graphQuota,
            live->writes,
            true,
            dispatchX);
        if (live->actor.diagnostic || !materialize)
            return live;
        live->retained =
            OriginalCommandDescriptorStateFixture::verify<rhi::ComputePipeline>(live->actor.inputs,
                *live->context->device,
                true,
                false,
                false,
                {1u, true, 1u, 1u, true, true, 2u, 4u});
        auto observation = live->context->device->observeOriginalDeviceNativeLoss(id);
        assert(observation);
        live->observation.emplace(*observation);
        live->bind();
        return live;
    }

    static void release(core::UniquePtr<Live>& live)
    {
        auto quotas = live->context->quotas;
        const auto loss = live->context->lossQuota;
        const auto source = live->sourceQuota, graph = live->graphQuota,
                   submission = live->submission, callback = live->callback;
        auto writes = live->writes;
        const auto recording = live->retained.recordingQuota, state = live->retained.stateQuota,
                   stream = live->retained.stream.quota;
        live.reset();
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
        for (const auto& quota :
            std::array{loss, source, graph, submission, callback, recording, state, stream})
            if (quota)
                assert(!quota->liveBytes());
        for (const auto& row : writes)
            for (const auto& quota : row)
                if (quota)
                    assert(!quota->liveBytes());
    }

    template <class Visitor> static void withOriginalInput(Visitor&& visitor)
    {
        const char* path = "/descriptor-state-slots/descriptor_state_slots.computeMain.entry.yaml";
        auto budget = OriginalTargetMemoryFixture::entryBudget();
        auto artifactQuota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        auto inputQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        core::pmr::String diagnostic;
        auto artifact =
            shader::SingleEntryShaderArtifact::load(path, budget, artifactQuota, diagnostic);
        assert(artifact);
        TargetMemoryRequestFixture target;
        target.prepare(artifact);
        budget.targetMemory = &target.request();
        artifact = shader::SingleEntryShaderArtifact::load(path, budget, artifactQuota, diagnostic);
        assert(artifact);
        auto input = rhi::OriginalSingleEntryPipelineInput::prepare(
            artifact, {1u, bytes}, inputQuota, diagnostic);
        assert(input);
        auto layout = rhi::buildPipelineLayoutDescFromShaderReflection(input->package().reflection);
        visitor(input, layout);
    }

public:
    static std::shared_ptr<const Custody> verifyGraphics8(
        const std::shared_ptr<const Command>& command, std::uint32_t declaredIssuerOwners = 0u)
    {
        assert(declaredIssuerOwners == 0u
            || declaredIssuerOwners == rhi::OriginalTypedIssuerPreparationContract::additionalOwners
            || declaredIssuerOwners
                == rhi::OriginalTypedIssuerPreparationContract::additionalOwners
                    + rhi::OriginalGeneralOutputSnapshotContract::additionalOwnerCount);
        assert(command && !command->validate() && command->originalRequest().contractVersion == 8u);
        const auto& admission = command->originalAdmission();
        const auto& root = admission->originalRoot();
        const auto& seal = admission->originalReservation()->originalSeal();
        const auto submission = seal->originalSubmissionQuota(),
                   callback = seal->originalCallbackQuota();
        C::Request request{8u,
            true,
            rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
            1u,
            8u,
            1u,
            command->originalRequest().maxRetainedOwners,
            submission->limitBytes(),
            callback->limitBytes()};
        const auto owners = command->retainedOwnerCount() + C::additionalOwners(request);
        const auto commandOwnerCeiling = request.maxRetainedOwners;
        const auto offered = std::uint64_t(owners) + declaredIssuerOwners;
        assert(offered <= commandOwnerCeiling);
        request.maxRetainedOwners = static_cast<std::uint32_t>(offered);
        const char* id = nullptr;
        const auto factory = [&] {
            return Custody::prepareOriginal(command, request, id);
        };
        const auto reject = [&](auto edit, const char* expected) {
            auto wrong = request;
            edit(wrong);
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Custody::prepareOriginal(command, wrong, id));
            is(id, expected);
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        };
        for (const auto version : {1u, 2u, 3u, 4u, 5u, 6u, 7u})
            reject(
                [&](auto& r) {
                    r.contractVersion = version;
                },
                "RHI-TYPED-CALLBACK-CUSTODY-ORIGINAL-SUBSCRIPTION");
        reject(
            [](auto& r) {
                r.contractVersion = 9u;
            },
            "RHI-TYPED-CALLBACK-CUSTODY-VERSION");
        reject(
            [](auto& r) {
                r.prototypeOptIn = false;
            },
            "RHI-TYPED-CALLBACK-CUSTODY-OPT-IN");
        reject(
            [](auto& r) {
                --r.maxCallbackSlots;
            },
            "RHI-TYPED-CALLBACK-CUSTODY-PARTICIPANTS");
        reject(
            [](auto& r) {
                ++r.maxCallbackSlots;
            },
            "RHI-TYPED-CALLBACK-CUSTODY-PARTICIPANTS");
        reject(
            [&](auto& r) {
                r.maxRetainedOwners = owners - 1u;
            },
            "RHI-WEBGPU-TYPED-CALLBACK-OWNER-CAPACITY");
        reject(
            [&](auto& r) {
                r.maxRetainedOwners = commandOwnerCeiling + 1u;
            },
            "RHI-WEBGPU-TYPED-CALLBACK-OWNER-CAPACITY");
        reject(
            [](auto& r) {
                --r.maxCpuSubmissionBytes;
            },
            "RHI-WEBGPU-TYPED-CALLBACK-QUOTA");
        reject(
            [](auto& r) {
                --r.maxCpuCallbackBytes;
            },
            "RHI-WEBGPU-TYPED-CALLBACK-QUOTA");
        for (const auto& quota : std::array{submission, callback}) {
            core::CpuMemoryQuotaLease exhausted(quota, quota->limitBytes() - quota->liveBytes());
            assert(exhausted.charged() && !factory());
            is(id, "RHI-WEBGPU-TYPED-CALLBACK-ALLOCATION");
        }
        countStart();
        auto custody = factory();
        countEnd(0u);
        assert(custody && !id && !custody->validate()
            && std::uint64_t(owners) + declaredIssuerOwners == request.maxRetainedOwners);
        assert(custody->pendingOriginalRecords() == 1u && custody->physicalLossArmed()
            && !custody->physicalLossDelivered() && !custody->nativeExecutionProven());
        is(custody->executionDiagnostic(), "RHI-ORIGINAL-GRAPHICS-CALLBACK-WEBGPU-OBSERVER-ONLY");
        assert(!factory());
        is(id, "RHI-WEBGPU-TYPED-CALLBACK-DUPLICATE");
        auto forgedCommand = std::shared_ptr<const Command>(callback, command.get());
        assert(!Custody::prepareOriginal(forgedCommand, request, id));
        is(id, "RHI-WEBGPU-TYPED-COLD-COMMAND-OWNER");
        forgedCommand.reset();
        auto forged = std::shared_ptr<const Custody>(callback, custody.get());
        is(Custody::validateOriginal(forged), "RHI-WEBGPU-TYPED-CALLBACK-OWNER");
        forged.reset();
        using Issuer = rhi_webgpu::WebGpuOriginalTypedIssuerPreparation;
        const auto refusedAllocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Issuer::prepareOriginal(custody,
                   {8u,
                       true,
                       1u,
                       1u,
                       7u,
                       owners,
                       root->originalMetadataQuota()->limitBytes(),
                       submission->limitBytes(),
                       callback->limitBytes()},
                   id)
            && id);
        is(id, "RHI-WEBGPU-TYPED-ISSUER-OWNER-CAPACITY");
        assert(core::defaultCpuAllocator().stats().allocationCount == refusedAllocations);
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{root->originalMetadataQuota(), submission, callback},
                [&]() noexcept -> const char* {
                    const auto& sources = root->originalGraph()
                                              ->originalInputs()
                                              ->originalScalars()
                                              ->originalUsage()
                                              ->originalSources();
                    for (const auto& entry : sources->sources())
                        std::visit(
                            [&](const auto& object) {
                                using T = std::remove_const_t<
                                    typename std::decay_t<decltype(object)>::element_type>;
                                if constexpr (std::is_same_v<T,
                                                  rhi::OriginalCommandGraphicsPipelineSource>) {
                                    auto native = rhi_webgpu::webGpuNativePayloadFor(
                                        *object->originalObject());
                                    const auto handle = native->pipeline;
                                    native->pipeline = nullptr;
                                    assert(custody->validate());
                                    native->pipeline = handle;
                                } else if constexpr (std::is_same_v<T,
                                                         rhi::OriginalCommandDescriptorSetSource>)
                                    OriginalDescriptorNativeIdentityFixture::mutateActual(
                                        rhi_webgpu::webGpuNativePayloadFor(
                                            *object->originalObject()),
                                        [&] {
                                            assert(custody->validate());
                                        });
                            },
                            entry);
                    return custody->validate();
                }));
        });
        assert(root->status() == rhi::OriginalCommandStateTransaction::Status::Prepared
            && root->journalEmpty());
        std::printf(
            "BINDLESS_RHI_GENERAL_GRAPHICS_CALLBACK8_OK Command8 Cold8 Lifecycle5 ContentPlan6 callbacks8 native7 local1 shared1 owners%u owner-add11 legacy-short-enlarged-exhausted-forged-native-mutated-refused original-subscription-no-new-registration hot0-query0 no-issuer-record-execution\n",
            owners);
        return custody;
    }

    static void verifyMixedMapCustody(const std::shared_ptr<const Command>& command,
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalReadbackResultStorage> results,
        const C::Request& request,
        const std::shared_ptr<core::CpuMemoryQuota>& submission,
        const std::shared_ptr<core::CpuMemoryQuota>& callback,
        const std::shared_ptr<core::CpuMemoryQuota>& resultQuota)
    {
        using Storage = rhi_webgpu::WebGpuCanonicalReadbackResultStorage;
        const char* id = nullptr;
        const auto submissionBase = submission->liveBytes(), callbackBase = callback->liveBytes();
        const auto reject = [&](const auto& candidate, const auto& storage, const char* expected) {
            assert(!Custody::prepareOriginal(command, candidate, id, storage));
            is(id, expected);
            assert(
                submission->liveBytes() == submissionBase && callback->liveBytes() == callbackBase);
        };
        auto wrong = request;
        wrong.readbackMap = {};
        reject(wrong, results, "RHI-TYPED-CALLBACK-CUSTODY-READBACK-MAP-VERSION");
        wrong = request;
        wrong.readbackMap.prototypeOptIn = false;
        reject(wrong, results, "RHI-TYPED-CALLBACK-CUSTODY-READBACK-MAP-OPT-IN");
        reject(request, std::shared_ptr<const Storage>{}, "RHI-WEBGPU-READBACK-STORAGE-OWNER");
        auto forged = std::shared_ptr<const Storage>(resultQuota, results.get());
        reject(request, forged, "RHI-WEBGPU-READBACK-STORAGE-OWNER");
        forged.reset();
        wrong = request;
        --wrong.maxCallbackSlots;
        reject(wrong, results, "RHI-TYPED-CALLBACK-CUSTODY-PARTICIPANTS");
        wrong = request;
        wrong.maxRetainedOwners = command->retainedOwnerCount() + C::additionalOwners(request)
            + results->retainedOwnerCount() - command->originalAdmission()->retainedOwnerCount()
            - 1u;
        reject(wrong, results, "RHI-WEBGPU-TYPED-CALLBACK-OWNER-CAPACITY");
        {
            core::CpuMemoryQuotaLease pressure(
                submission, submission->limitBytes() - submission->liveBytes());
            assert(pressure.charged() && !Custody::prepareOriginal(command, request, id, results));
            is(id, "RHI-WEBGPU-TYPED-CALLBACK-ALLOCATION");
        }
        {
            core::CpuMemoryQuotaLease pressure(
                callback, callback->limitBytes() - callback->liveBytes());
            assert(pressure.charged() && !Custody::prepareOriginal(command, request, id, results));
            is(id, "RHI-WEBGPU-TYPED-CALLBACK-ALLOCATION");
        }
        assert(submission->liveBytes() == submissionBase && callback->liveBytes() == callbackBase);
        auto custody = Custody::prepareOriginal(command, request, id, results);
        if (!custody)
            std::fprintf(stderr, "typed map custody: %s\n", id ? id : "missing");
        assert(custody && !id && !custody->validate() && custody->pendingOriginalRecords() == 1u
            && custody->physicalLossArmed() && !custody->nativeExecutionProven());
        const auto submissionCost = submission->liveBytes() - submissionBase;
        const auto callbackCost = callback->liveBytes() - callbackBase;
        auto weak = std::weak_ptr<const Storage>(results);
        results.reset();
        assert(!weak.expired());
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{submission, callback, resultQuota}, [&]() noexcept {
                    return custody->validate();
                }));
        });
        is(custody->executionDiagnostic(), "RHI-TYPED-READBACK-MAP-CUSTODY-COMPLETION-MISSING");
        assert(!Custody::prepareOriginal(command, request, id, weak.lock()));
        is(id, "RHI-WEBGPU-TYPED-CALLBACK-DUPLICATE");
        assert(!custody->cancelOriginal() && !custody->pendingOriginalRecords());
        custody.reset();
        assert(weak.expired());
        weak.reset();
        assert(!resultQuota->liveBytes() && submission->liveBytes() == submissionBase
            && callback->liveBytes() == callbackBase);
        std::printf(
            "ORIGINAL_TABLE_TYPED_MAP_CUSTODY1_OK Graphics same-Cold3-root-seal-Join2 Result2 map1-scope3 slots12 submission%llu callback%llu missing-forged-short-exhausted duplicate caller-drop cancel-release hot0 query0 encoder0 submit0 map-publication-completion-closed\n",
            static_cast<unsigned long long>(submissionCost),
            static_cast<unsigned long long>(callbackCost));
    }

    // Consume the caller's authentic mixed command/root. Reuse issuer receipt
    // capture and rollback tests rather than constructing another fixture graph.
    template <class Live> static void verifyMixed(Live& live, bool recordReadback = false)
    {
        assert(live.request.contractVersion == 3u);
        using Issuers = rhi_webgpu::WebGpuOriginalTypedIssuerPreparation;
        const char* id = nullptr;
        countStart();
        const auto factory = [&] {
            return Custody::prepareOriginal(live.command, live.request, id);
        };
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{live.submission, live.callback}, [&]() noexcept -> const char* {
                auto forged = std::shared_ptr<const Command>(live.callback, live.command.get());
                assert(!Custody::prepareOriginal(forged, live.request, id));
                is(id, "RHI-WEBGPU-TYPED-COLD-COMMAND-OWNER");
                auto shortOwners = live.request;
                shortOwners.maxRetainedOwners =
                    live.command->retainedOwnerCount() + C::additionalOwners(live.request) - 1u;
                assert(!Custody::prepareOriginal(live.command, shortOwners, id));
                is(id, "RHI-WEBGPU-TYPED-CALLBACK-OWNER-CAPACITY");
                for (const auto version : {1u, 2u}) {
                    auto old = live.request;
                    old.contractVersion = version;
                    assert(!Custody::prepareOriginal(live.command, old, id));
                    is(id, "RHI-TYPED-CALLBACK-CUSTODY-ORIGINAL-SUBSCRIPTION");
                }
                return nullptr;
            }));
        const auto submissionBase = live.submission->liveBytes(),
                   callbackBase = live.callback->liveBytes();
        std::uint64_t submissionBytes = 0u, callbackBytes = 0u;
        {
            auto measured = factory();
            assert(measured && !id && !measured->validate());
            submissionBytes = live.submission->liveBytes() - submissionBase;
            callbackBytes = live.callback->liveBytes() - callbackBase;
            assert(submissionBytes && callbackBytes && measured->physicalLossArmed()
                && !measured->physicalLossDelivered() && measured->pendingOriginalRecords() == 1u);
            assert(!measured->nativeExecutionProven());
            is(measured->executionDiagnostic(),
                "RHI-TYPED-CALLBACK-CUSTODY-WEBGPU-RECORDING-MISSING");
            assert(!factory());
            is(id, "RHI-WEBGPU-TYPED-CALLBACK-DUPLICATE");
            assert(!measured->cancelOriginal() && !measured->pendingOriginalRecords()
                && !measured->physicalLossArmed());
        }
        live.rebuild();
        assert(live.submission->liveBytes() == submissionBase
            && live.callback->liveBytes() == callbackBase);
        for (const bool callbackDomain : {false, true}) {
            const auto quota = callbackDomain ? live.callback : live.submission;
            const auto measured = callbackDomain ? callbackBytes : submissionBytes;
            const auto before = quota->liveBytes();
            for (const auto available : std::array<std::uint64_t, 2u>{0u, measured - 1u}) {
                {
                    core::CpuMemoryQuotaLease pressure(
                        quota, quota->limitBytes() - before - available);
                    assert(pressure.charged() && !factory());
                    is(id, "RHI-WEBGPU-TYPED-CALLBACK-ALLOCATION");
                }
                assert(quota->liveBytes() == before && !live.command->validate());
            }
        }
        {
            core::CpuMemoryQuotaLease submissionPressure(
                live.submission, live.submission->limitBytes() - submissionBase - submissionBytes);
            core::CpuMemoryQuotaLease callbackPressure(
                live.callback, live.callback->limitBytes() - callbackBase - callbackBytes);
            assert(submissionPressure.charged() && callbackPressure.charged());
            live.custody = factory();
            assert(live.custody && !id && !live.custody->validate());
        }
        std::puts(
            "ORIGINAL_TABLE_READBACK_CALLBACK3_PROGRESS exact-Submission-Callback same-original-command local-observer1 physical-registration0");
        OriginalTypedIssuerPreparationFixture::verifyCold(live, recordReadback);
        assert(live.coldIssuers && !live.coldIssuers->validate()
            && live.coldIssuers->issuersClosedForInspection());
        for (const auto version : {1u, 2u, 4u}) {
            const rhi::OriginalTypedNativeExecutionRequest execution{version,
                true,
                rhi::OriginalTypedSynchronizationPolicy::FrozenOriginalScopeVisibility1};
            const auto expected = version == 4u ? "RHI-TYPED-NATIVE-EXECUTION-VERSION"
                                                : "RHI-TYPED-NATIVE-EXECUTION-ISSUER-PROFILE";
            is(Issuers::recordOriginal(live.coldIssuers, execution), expected);
            is(Issuers::submitOriginal(live.coldIssuers, execution), expected);
            assert(!live.coldIssuers->hasOriginalRecordedCommand()
                && !live.coldIssuers->hasOriginalCompletedSubmission());
        }
        const auto owners = live.command->retainedOwnerCount() + C::additionalOwners(live.request)
            + rhi::OriginalTypedIssuerPreparationContract::additionalOwners;
        std::weak_ptr<const void> state = live.custody->stateForInspection();
        const rhi::OriginalTypedNativeExecutionRequest recording{
            3u, true, rhi::OriginalTypedSynchronizationPolicy::FrozenOriginalScopeVisibility1};
        is(Issuers::submitOriginal(live.coldIssuers, recording),
            "RHI-TYPED-NATIVE-EXECUTION-READBACK-COMPLETION-MISSING");
        if (recordReadback) {
            using Held = WebGpuErrorScopeHandoffFixture<29u>;
            Held::holdScopes();
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{live.retained.recordingQuota, live.submission, live.callback},
                    [&]() noexcept -> const char* {
                        return Issuers::recordOriginal(live.coldIssuers, recording);
                    }));
            });
            Held::restoreScopes();
            Held::waitHeldScopes();
            assert(live.retained.transaction->status()
                    == rhi::OriginalCommandStateTransaction::Status::Pending
                && !live.retained.transaction->journalEmpty());
            is(Issuers::recordOriginal(live.coldIssuers, recording),
                "RHI-WEBGPU-TYPED-RECORDING-ONE-SHOT");
            auto lateCompletion = recording;
            lateCompletion.readbackCompletion = {1u, true};
            is(Issuers::submitOriginal(live.coldIssuers, lateCompletion),
                "RHI-TYPED-NATIVE-READBACK-COMPLETION-RECORDED-PROFILE");
            assert(!live.coldIssuers->hasOriginalRecordedCommand());
            live.command.reset();
            assert(!state.expired());
            for (const auto index : {0u, 1u})
                Held::releaseScope(index);
            assert(WebGpuAsyncWaitFixture::until([&] {
                return EM_ASM_INT({
                    // clang-format off
                        return Module.arpTypedIssuerLossStates.slice(0, 3)
                                   .filter(s => s.phase === s.delivered)
                                   .length === 2;
                    // clang-format on
                });
            }));
            assert(!live.coldIssuers->hasOriginalRecordedCommand()
                && !live.coldIssuers->hasOriginalCompletedSubmission() && !state.expired());
            Held::releaseScope(2u);
            assert(WebGpuAsyncWaitFixture::until([&] {
                return live.coldIssuers->hasOriginalRecordedCommand()
                    || core::StringView(live.coldIssuers->recordingDiagnostic())
                    != "RHI-WEBGPU-TYPED-RECORDING-PENDING";
            }));
            if (!live.coldIssuers->hasOriginalRecordedCommand())
                std::fprintf(stderr,
                    "mixed recording failed: %s\n",
                    live.coldIssuers->recordingDiagnostic());
            assert(live.coldIssuers->hasOriginalRecordedCommand()
                && !live.coldIssuers->recordingDiagnostic()
                && !live.coldIssuers->hasOriginalCompletedSubmission());
            is(Issuers::submitOriginal(live.coldIssuers, recording),
                "RHI-TYPED-NATIVE-EXECUTION-READBACK-COMPLETION-MISSING");
        }
        live.command.reset();
        assert(!state.expired());
        if (!recordReadback)
            assert(!live.coldIssuers->validate());
        assert(!live.custody->cancelOriginal());
        if (recordReadback)
            assert(live.retained.transaction->status()
                    == rhi::OriginalCommandStateTransaction::Status::Abandoned
                && live.retained.transaction->journalEmpty());
        live.coldIssuers.reset();
        live.custody.reset();
        assert(state.expired());
        state.reset(); // Release the quota-owned makeShared control block too.
        if (recordReadback) {
            assert(EM_ASM_INT({
                // clang-format off
                    return Module.arpTypedIssuerLossStates.length === 7 &&
                           Module.arpTypedIssuerLossStates.every(
                               s => (s.phase === s.delivered || s.phase === s.suppressed) && !s.userdata &&
                                    !s.complete);
                // clang-format on
            }));
            EM_ASM({
                // clang-format off
                    delete Module.arpTypedIssuerLossStates;
                // clang-format on
            });
        }
        // Final caller drop releases Command3 as well as State/wrapper/slots.
        // The parent measured Recording before Command3 creation; Submission
        // and Callback were measured before custody. No object-size estimates.
        if (live.retained.recordingQuota->liveBytes() != live.recordingBeforeCommand
            || live.submission->liveBytes() != submissionBase
            || live.callback->liveBytes() != callbackBase)
            std::printf(
                "ORIGINAL_TABLE_READBACK_ISSUER3_RELEASE recording%llu expected%llu submission%llu expected%llu callback%llu expected%llu\n",
                static_cast<unsigned long long>(live.retained.recordingQuota->liveBytes()),
                static_cast<unsigned long long>(live.recordingBeforeCommand),
                static_cast<unsigned long long>(live.submission->liveBytes()),
                static_cast<unsigned long long>(submissionBase),
                static_cast<unsigned long long>(live.callback->liveBytes()),
                static_cast<unsigned long long>(callbackBase));
        assert(live.retained.recordingQuota->liveBytes() == live.recordingBeforeCommand
            && live.submission->liveBytes() == submissionBase
            && live.callback->liveBytes() == callbackBase);
        countEnd(0u);
        std::printf(
            "ORIGINAL_TABLE_READBACK_ISSUER3_OK Graphics callback3 issuer3 same-Lifecycle2-Cold3-Command3-Join2 owners%u callbackSubmissionBytes%llu callbackBytes%llu native-receipts7 local-observer1 physical-registration0 partial-failure3 native-binding-mutation5 exact-less-exhausted caller-drop cancellation-reclaimed hot0 execution1-2-4-rejected submission3-closed\n",
            owners,
            static_cast<unsigned long long>(submissionBytes),
            static_cast<unsigned long long>(callbackBytes));
        if (recordReadback)
            std::puts(
                "ORIGINAL_TABLE_READBACK_RECORDING3_OK Graphics same-root-seal-Join2 copy1 real-scopes3 partial-drain caller-drop same-journal-Abandoned quotas-reclaimed hot0 query0 encoder1 submit0 map-result-completion-closed");
    }

    template <class Factory> static void verify(Factory&& create)
    {
        withOriginalInput([&](const auto& input, const auto& layout) {
            const char* id = nullptr;
            std::uint64_t submissionBytes = 0u, callbackBytes = 0u;
            countStart();
            auto live = prepare(create, input, layout);
            countEnd(1u);
            countStart();
            const auto factory = [&] {
                return Custody::prepareOriginal(live->command, live->request, id);
            };
            {
                const auto beforeSubmission = live->submission->liveBytes(),
                           beforeCallback = live->callback->liveBytes();
                auto custody = factory();
                assert(custody && !id && !custody->validate());
                submissionBytes = live->submission->liveBytes() - beforeSubmission;
                callbackBytes = live->callback->liveBytes() - beforeCallback;
                assert(submissionBytes && callbackBytes && custody->physicalLossArmed()
                    && !custody->physicalLossDelivered()
                    && custody->pendingOriginalRecords() == 1u);
                assert(!custody->nativeExecutionProven());
                is(custody->executionDiagnostic(),
                    "RHI-TYPED-CALLBACK-CUSTODY-WEBGPU-RECORDING-MISSING");
                assert(!factory());
                is(id, "RHI-WEBGPU-TYPED-CALLBACK-DUPLICATE");
                assert(!custody->cancelOriginal() && !custody->pendingOriginalRecords()
                    && !custody->physicalLossArmed() && !custody->physicalLossDelivered());
                assert(!factory());
                is(id, "RHI-WEBGPU-TYPED-CALLBACK-DUPLICATE");
            }
            // Independent exact/one-less/exhausted Submission and Callback quotas.
            for (const auto domain : std::array{live->submission, live->callback}) {
                CpuQuotaFactoryFixture pressure(domain);
                pressure.verifySingleUse(
                    domain == live->submission ? submissionBytes : callbackBytes,
                    [&] {
                        live->rebuild();
                    },
                    factory);
            }
            auto custody = factory();
            assert(custody && !id); // Last failed allocation has not claimed the seal.
            std::weak_ptr<const void> state = custody->stateForInspection();
            custody.reset();
            assert(state.expired());
            state.reset();
            assert(!factory());
            is(id, "RHI-WEBGPU-TYPED-CALLBACK-DUPLICATE");
            live->rebuild();
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{live->submission, live->callback}, [&]() noexcept -> const char* {
                    auto forged =
                        std::shared_ptr<const Command>(live->callback, live->command.get());
                    assert(!Custody::prepareOriginal(forged, live->request, id));
                    is(id, "RHI-WEBGPU-TYPED-COLD-COMMAND-OWNER");
                    auto wrong = live->request;
                    --wrong.maxRetainedOwners;
                    wrong.maxRetainedOwners =
                        live->command->retainedOwnerCount() + C::additionalOwners(wrong) - 1u;
                    assert(!Custody::prepareOriginal(live->command, wrong, id));
                    is(id, "RHI-WEBGPU-TYPED-CALLBACK-OWNER-CAPACITY");
                    wrong = live->request;
                    --wrong.maxCpuCallbackBytes;
                    assert(!Custody::prepareOriginal(live->command, wrong, id));
                    is(id, "RHI-WEBGPU-TYPED-CALLBACK-QUOTA");
                    return nullptr;
                }));
            const auto issuerCancelledBytes =
                OriginalTypedIssuerPreparationFixture::verifyCold(*live);
            // Logical close plus local suppression safely detaches the original native borrow.
            custody = live->custody;
            assert(custody && live->coldIssuers && !live->coldIssuers->validate());
            live->context->device->retireOriginalDeviceLifetime();
            assert(custody->physicalLossArmed() && !custody->physicalLossDelivered()
                && custody->pendingOriginalRecords() == 1u);
            OriginalObservedNativeLossBindingFixture::verifyDeferredSuppressionFailure(
                *live->observation, [&] {
                    assert(!custody->cancelOriginal());
                });
            live->coldIssuers.reset();
            assert(live->retained.recordingQuota->liveBytes() == issuerCancelledBytes[0]
                && live->submission->liveBytes() == issuerCancelledBytes[1]);
            custody.reset();
            countEnd(0u);
            deferred_ = std::move(live);
            {
                auto old = prepare(create, input, layout, false);
                const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
                assert(!Custody::prepareOriginal(old->command, old->request, id));
                is(id, "RHI-WEBGPU-TYPED-CALLBACK-OBSERVED-PROFILE");
                assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
                release(old);
            }
            for (std::size_t i = 0u; i < losses_.size(); ++i) {
                countStart();
                losses_[i] = prepare(create, input, layout);
                auto& value = *losses_[i];
                value.custody = Custody::prepareOriginal(value.command, value.request, id);
                assert(value.custody && !value.custody->validate());
                OriginalTypedIssuerPreparationFixture::prepareLoss(value);
                countEnd(1u);
                if (i == 1u)
                    value.context->device->retireOriginalDeviceLifetime();
                if (i == 2u)
                    value.context->device.reset();
                assert(value.custody->physicalLossArmed() && !value.custody->physicalLossDelivered()
                    && value.custody->pendingOriginalRecords() == 1u);
                if (i)
                    is(value.observation->diagnostic(),
                        i == 1u ? "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED"
                                : "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
            }
            std::printf(
                "BINDLESS_RHI_TYPED_CALLBACK_NATIVE_OK submission%llu callback%llu exact-less-exhausted failed-claim-recovery forged-old-profile-refuse sticky-drop local-cancel logical-retain three-live-receivers deferred-suppression-failure-retains additional-native-handler0 hot0\n",
                static_cast<unsigned long long>(submissionBytes),
                static_cast<unsigned long long>(callbackBytes));
        });
    }

    static void verifyActualLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost());
        assert(WebGpuAsyncWaitFixture::until([] {
            for (const auto& value : losses_)
                if (!value || !value->custody->physicalLossDelivered())
                    return false;
            return true;
        }));
        for (std::size_t i = 0u; i < losses_.size(); ++i) {
            auto& value = *losses_[i];
            assert(!value.custody->physicalLossArmed() && value.custody->physicalLossDelivered()
                && !value.custody->pendingOriginalRecords());
            is(value.custody->validate(), "RHI-WEBGPU-CANONICAL-CALLBACK-NATIVE-LOST");
            assert(!value.custody->nativeExecutionProven());
            is(value.observation->diagnostic(),
                i == 0u       ? "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST"
                    : i == 1u ? "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED"
                              : "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
            assert(!value.observation->terminalDiagnostic());
            OriginalTypedIssuerPreparationFixture::inspectLoss(value);
            release(losses_[i]);
        }
        OriginalTypedIssuerPreparationFixture::verifyLossReclaimed();
        assert(deferred_);
        is(deferred_->observation->terminalDiagnostic(), "RHI-ORIGINAL-DEVICE-LOSS-NATIVE-DETACH");
        release(deferred_);
        std::puts(
            "BINDLESS_RHI_TYPED_CALLBACK_LOSS_OK actual-physical-loss3 including-Retired-Shutdown pool-drained no-logical-loss-receipt no-submit-completion quotas-zero");
    }
#endif
};
} // namespace arp::test
