#pragma once
#include "original_command_texture_readback_inputs_fixture.h"
#include "rhi/backends/webgpu/webgpu_canonical_issuer_preparation.h"
#include "rhi/backends/webgpu/webgpu_canonical_readback_joins.h"
#include "rhi/backends/webgpu/webgpu_canonical_readback_result_storage.h"
#include "rhi/backends/webgpu/webgpu_native_buffer_map_receipt.h"
#include "webgpu_async_wait_fixture.h"
#include "webgpu_canonical_copy_pass_lowering_fixture.h"
#include "webgpu_canonical_submission_fixture.h"
#include "webgpu_canonical_upload_joins_fixture.h"
#include "webgpu_native_map_execution_probe_fixture.h"
#include "webgpu_native_readback_seed_fixture.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class WebGpuCanonicalReadbackJoinsFixture final {
    using Joins = rhi_webgpu::WebGpuCanonicalReadbackJoins;
    using Root = rhi::OriginalCommandStateTransaction;
    static constexpr std::uint64_t bytes = 65536u;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "readback joins actual=%s expected=%s\n",
                actual ? actual : "<none>",
                expected);
        assert(actual && std::string_view(actual) == expected);
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    class Context final {
    public:
        template <class Factory> explicit Context(Factory&& create) : device(create())
        {
            assert(device);
            for (auto& q : quotas)
                q = core::makeShared<core::CpuMemoryQuota>(bytes);
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, quotas[5]));
            assert(!device->prepareOriginalDeviceNativeLoss(
                {{1u, true, 1u, 1u, bytes}, 1u}, quotas[7]));
            const auto owners = rhi::OriginalResourceRegistrationContract::fixedOwnerCount
                + 2u * rhi::OriginalResourceRegistrationContract::perResourceOwnerCount;
            assert(!device->prepareOriginalResourceRegistration(
                {1u, true, 1u, 1u, 0u, owners, bytes}, quotas[6]));
        }

        std::shared_ptr<Root> prepare(rhi::MemoryUsage memory,
            const char*& diagnostic,
            std::uint32_t payloads = 2u,
            std::uint64_t staging = 768u,
            bool nativeRecording = false,
            bool mapProfile = false,
            std::uint32_t mapCapacity = 0u)
        {
            core::CpuMemoryQuotaScope resource(quotas[4]);
            auto capacity = nativeRecording
                ? rhi_webgpu::WebGpuCanonicalColdAdmission::selectedRequirements().capacity
                : rhi::OriginalCommandLifecycleCapacityRequest{};
            if (mapProfile) {
                capacity.callbackTickets +=
                    rhi::OriginalCommandReadbackMapContract::participantsPerMap;
                capacity.callbackRegistrations +=
                    rhi::OriginalCommandReadbackMapContract::participantsPerMap;
            }
            if (mapCapacity)
                capacity.callbackTickets = capacity.callbackRegistrations = mapCapacity;
            auto inputs = OriginalCommandTextureReadbackInputsFixture::prepareCanonicalInputs(
                *device, quotas[0], quotas[1], diagnostic, memory, payloads, staging, capacity);
            if (!inputs)
                std::fprintf(stderr,
                    "readback canonical input diagnostic %s\n",
                    diagnostic ? diagnostic : "<none>");
            assert(inputs && !diagnostic);
            auto root = WebGpuCanonicalUploadJoinsFixture::prepareRoot(inputs,
                *device,
                quotas[2],
                quotas[3],
                false,
                2u,
                3u,
                2u,
                {1u, true, 5u, 0u, 5u, 6u},
                diagnostic);
            if (!root)
                std::fprintf(stderr, "readback root %s\n", diagnostic);
            assert(root && !diagnostic);
            return root;
        }

        std::array<std::shared_ptr<core::CpuMemoryQuota>, 8> quotas;
        core::UniquePtr<rhi::GpuDevice> device;
    };

    using Admission = rhi_webgpu::WebGpuCanonicalColdAdmission;
    using Lowering = rhi_webgpu::WebGpuCanonicalCopyPassLowering;
    using Command = rhi_webgpu::WebGpuCanonicalColdCommand;
    using Custody = rhi_webgpu::WebGpuCanonicalCallbackCustody;
    using Issuers = rhi_webgpu::WebGpuCanonicalIssuerPreparation;

    class Prepared final {
    public:
        explicit Prepared(Context& context,
            const std::shared_ptr<Root>& original,
            bool exhaustLoweringQuota = true,
            bool mapProfile = false,
            bool prepareCustody = true,
            bool publish = false)
            : root(original)
        {
            const char* diagnostic = nullptr;
            const auto entries = inputs(*root);
            const auto joinBudget = budget(*root);
            joins = Joins::prepareOriginal(
                root, *context.device, entries.data(), 2u, joinBudget, diagnostic);
            assert(joins);
            const auto loss = context.device->observeOriginalDeviceNativeLoss(diagnostic);
            assert(loss && !diagnostic);
            auto closure = rhi_webgpu::WebGpuCanonicalNativeClosure::prepareOriginalLossBound(root,
                *context.device,
                {1u, true, 2u, 2u, root->retainedOwnerCount() + 2u + 2u * 3u, 788u, bytes},
                *loss,
                diagnostic);
            assert(closure);
            using Seal = rhi::OriginalCommandRecordingSeal;
            using Reservation = rhi::OriginalCommandLifecycleReservation;
            constexpr auto downstream =
                rhi::OriginalCommandNativeColdCommandContract::additionalOwnerCount
                + rhi::OriginalCommandCallbackCustodyContract::additionalOwnerCount
                + rhi::OriginalCommandCopyPassLoweringContract::additionalOwnerCount
                + rhi::OriginalCommandIssuerPreparationContract::additionalOwnerCount;
            ownerLimit = closure->retainedOwnerCount() + Seal::additionalOwnerCount
                + Reservation::additionalOwnerCount + 1u + joins->retainedOwnerCount()
                - root->retainedOwnerCount() + downstream;
            if (mapProfile)
                ownerLimit += rhi::OriginalCommandReadbackMapContract::participantsPerMap
                    + rhi::OriginalCommandReadbackResultStorageContract::fixedAdditionalOwners + 1u;
            auto required = Admission::selectedRequirements();
            Admission::Budget b;
            b.contractVersion = mapProfile ? 5u : 4u;
            b.prototypeOptIn = true;
            b.maxOriginalCommands = 5u;
            b.maxEncoders = b.maxCommandBuffers = 1u;
            b.maxRecordingScopeCallbacks = required.recordingScopeCallbacks;
            b.maxSubmissionScopeCallbacks = required.submissionScopeCallbacks;
            b.maxQueueCallbacks = required.queueCallbacks;
            b.maxRuntimeLossCallbacks = required.runtimeLossCallbacks;
            b.maxRuntimeLossRegistrations = required.runtimeLossRegistrations;
            b.maxRetainedOwners = ownerLimit;
            b.maxCpuMetadataBytes = bytes;
            b.readback = joinBudget.result;
            if (mapProfile)
                b.readbackMap = {1u, true, 1u, 1u, 3u, 12u, 12u};
            assert(!Admission::deriveRequirements(root, b, required));
            assert(required.contractVersion == (mapProfile ? 5u : 4u)
                && required.readback.readbackCommands == 2u
                && required.readback.nativeCopyCommands == 4u
                && required.readback.resultDeclarations == 1u && !required.bufferUploads
                && !required.renderPasses);
            auto invalid = b;
            --invalid.readback.maxDeclaredReadbackBytes;
            Admission::Requirements refused = required;
            assert(Admission::deriveRequirements(root, invalid, refused)
                && !refused.originalCommands && !refused.readback.nativeCopyCommands);
            submission = core::makeShared<core::CpuMemoryQuota>(bytes);
            callback = core::makeShared<core::CpuMemoryQuota>(bytes);
            auto seal = Seal::prepareOriginal(root,
                *context.device,
                {1u,
                    true,
                    required.capacity.pendingSubmissions,
                    required.capacity.callbackTickets,
                    required.capacity.callbackRegistrations,
                    ownerLimit,
                    ownerLimit,
                    ownerLimit,
                    bytes,
                    bytes,
                    bytes},
                submission,
                callback,
                diagnostic);
            assert(seal);
            auto reservation = Reservation::prepareOriginal(seal, required.capacity, diagnostic);
            assert(reservation);
            assert(!Admission::prepareOriginal(closure, reservation, b, diagnostic));
            is(diagnostic, "RHI-WEBGPU-READBACK-JOIN-OWNER");
            auto forged = std::shared_ptr<const Joins>(submission, joins.get());
            assert(!Admission::prepareOriginal(closure, reservation, b, diagnostic, {}, forged));
            is(diagnostic, "RHI-WEBGPU-READBACK-JOIN-OWNER");
            forged.reset();
            auto admission =
                Admission::prepareOriginal(closure, reservation, b, diagnostic, {}, joins);
            assert(admission);
            if (mapProfile) {
                resultQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
                results =
                    rhi_webgpu::WebGpuCanonicalReadbackResultStorage::prepareOriginal(admission,
                        *context.device,
                        {1u, true, 1u, 768u, bytes, ownerLimit},
                        resultQuota,
                        diagnostic);
                assert(results);
                assert(required.readbackMap.maps == 1u && required.capacity.callbackTickets == 12u
                    && required.capacity.callbackRegistrations == 12u);
            }
            command =
                Command::prepareOriginal(admission, {1u, true, 1u, ownerLimit, bytes}, diagnostic);
            if (!command)
                std::printf("BINDLESS_RHI_CANONICAL_MAP_COMMAND_DIAGNOSTIC %s\n",
                    diagnostic ? diagnostic : "<none>");
            assert(command);
            quotas = {context.quotas[0],
                context.quotas[1],
                context.quotas[2],
                context.quotas[3],
                context.quotas[4],
                context.quotas[5],
                context.quotas[6],
                context.quotas[7],
                submission,
                callback};
            if (!prepareCustody)
                return;
            const Custody::Budget cb{mapProfile ? 2u : 1u,
                true,
                1u,
                required.capacity.callbackTickets,
                ownerLimit,
                bytes,
                bytes};
            if (mapProfile) {
                assert(!Custody::prepareOriginal(command, cb, diagnostic));
                is(diagnostic, "RHI-WEBGPU-READBACK-STORAGE-OWNER");
                auto alias =
                    std::shared_ptr<const rhi_webgpu::WebGpuCanonicalReadbackResultStorage>(
                        resultQuota, results.get());
                assert(!Custody::prepareOriginal(command, cb, diagnostic, alias));
                is(diagnostic, "RHI-WEBGPU-READBACK-STORAGE-OWNER");
            }
            if (publish) {
                assert(mapProfile);
                assert(!Custody::prepareOriginal(
                    command, cb, diagnostic, results, Custody::PublicationIntent{}));
                is(diagnostic, "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-VERSION");
                assert(!Custody::prepareOriginal(
                    command, cb, diagnostic, results, Custody::PublicationIntent{1u, false}));
                is(diagnostic, "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-OPT-IN");
            }
            custody = Custody::prepareOriginal(command,
                cb,
                diagnostic,
                results,
                publish ? std::optional<Custody::PublicationIntent>{{1u, true}} : std::nullopt);
            assert(custody);
            lowerBudget = {b.contractVersion, true, 0u, 0u, ownerLimit, bytes, 0u};
            lowerBudget.readback = b.readback;
            lowerBudget.readbackMap = b.readbackMap;
            const auto lower = [&](const Lowering::Budget& limits) {
                return Lowering::prepareOriginal(custody, limits, diagnostic);
            };
            auto bad = lowerBudget;
            --bad.readback.maxNativeCopyCommands;
            assert(!lower(bad) && diagnostic);
            bad = lowerBudget;
            bad.contractVersion = 3u;
            assert(!lower(bad) && diagnostic);
            const auto exactOwners = command->retainedOwnerCount()
                + rhi::OriginalCommandCallbackCustodyContract::additionalOwners(cb)
                + (results ? results->retainedOwnerCount() - admission->retainedOwnerCount() : 0u)
                + rhi::OriginalCommandCopyPassLoweringContract::additionalOwnerCount;
            bad = lowerBudget;
            bad.maxRetainedOwners = exactOwners - 1u;
            assert(!lower(bad));
            is(diagnostic, "RHI-WEBGPU-CANONICAL-LOWERING-OWNER-CAPACITY");
            if (exhaustLoweringQuota) {
                core::CpuMemoryQuotaLease full(root->originalMetadataQuota(),
                    bytes - root->originalMetadataQuota()->liveBytes());
                assert(full.charged());
                assert(!lower(lowerBudget));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-LOWERING-ALLOCATION");
            }
            lowering = lower(lowerBudget);
            if (!lowering)
                std::fprintf(stderr, "readback lowering diagnostic %s\n", diagnostic);
            assert(lowering);
            assert(!lowering->projectedCopyCount() && !lowering->projectedUploadCopyCount()
                && lowering->projectedReadbackCopyCount() == 4u);
            assert(!lower(lowerBudget));
            is(diagnostic, "RHI-WEBGPU-CANONICAL-LOWERING-DUPLICATE");
            issuers = Issuers::prepareOriginal(
                lowering, {1u, true, 1u, 1u, ownerLimit, bytes, bytes}, diagnostic);
            assert(issuers && !diagnostic);
            quotas = {context.quotas[0],
                context.quotas[1],
                context.quotas[2],
                context.quotas[3],
                context.quotas[4],
                context.quotas[5],
                context.quotas[6],
                context.quotas[7],
                submission,
                callback};
            queue = wgpuDeviceGetQueue(static_cast<WGPUDevice>(closure->nativeDevice()));
            assert(queue);
        }

        ~Prepared()
        {
            if (queue)
                wgpuQueueRelease(queue);
        }

        void drop()
        {
            issuers.reset();
            lowering.reset();
            custody.reset();
            command.reset();
            joins.reset();
            root.reset();
            results.reset();
            resultQuota.reset();
        }

        std::shared_ptr<Root> root;
        std::shared_ptr<const Joins> joins;
        std::shared_ptr<const Command> command;
        std::shared_ptr<const Custody> custody;
        std::shared_ptr<const Lowering> lowering;
        std::shared_ptr<const Issuers> issuers;
        std::shared_ptr<core::CpuMemoryQuota> submission, callback, resultQuota;
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalReadbackResultStorage> results;
        Lowering::Budget lowerBudget;
        std::uint32_t ownerLimit = 0u;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 10> quotas;
        WGPUQueue queue = nullptr;
    };

    static void observeMaySubmit(void* data) noexcept
    {
        const auto& p = *static_cast<const Prepared*>(data);
        assert(p.root->status() == Root::Status::Pending && !p.root->journalEmpty());
        assert(!p.issuers->hasOriginalCompletedSubmission());
        is(Issuers::submitOriginal(p.issuers, {1u, true}),
            "RHI-WEBGPU-CANONICAL-SUBMISSION-ONE-SHOT");
    }

    static inline std::array<core::UniquePtr<Context>, 5> orderedLossContexts_;
    static inline std::array<core::UniquePtr<Prepared>, 5> orderedLoss_;
    static inline bool orderedScopeUncertain_ = false;
    static inline core::UniquePtr<Context> resultLossContext_;
    static inline core::UniquePtr<Prepared> resultLossPrepared_;
    static inline std::shared_ptr<const rhi_webgpu::WebGpuCanonicalReadbackResultStorage>
        resultLossStorage_;
    static inline std::shared_ptr<const rhi::OriginalCommandReadbackResultSnapshot>
        publicationSurvivor_;
    static inline std::array<std::shared_ptr<core::CpuMemoryQuota>, 11> publicationSurvivorQuotas_;
    static inline std::array<unsigned char, 20> publicationSurvivorBytes_;
    static inline std::array<core::UniquePtr<Context>, 4> executionLossContexts_;
    static inline std::array<core::UniquePtr<Prepared>, 4> executionLoss_;

    class ExecutionObservation final {
    public:
        Prepared* prepared;
        bool cancel, observed, drop;
    };

    static void observeMapExecution(void* userdata) noexcept
    {
        auto& observation = *static_cast<ExecutionObservation*>(userdata);
        auto& p = *observation.prepared;
        observation.observed = true;
        assert(p.root->status() == Root::Status::Pending && !p.root->journalEmpty()
            && p.custody->originalNativeLossArmed() && !p.issuers->hasOriginalCompletedSubmission()
            && !p.results->snapshots()[0]->publishedBytes());
        if (observation.cancel)
            is(p.custody->cancelOriginal(), "RHI-WEBGPU-CANONICAL-CALLBACK-QUARANTINED");
        if (observation.drop)
            p.drop();
    }

    static inline ExecutionObservation pendingMapObservation_{nullptr, false, false};
    static inline core::UniquePtr<Context> mapLossContext_;
    static inline core::UniquePtr<Prepared> mapLossPrepared_;

    class MapProbe final {
    public:
        struct Scope {
            MapProbe* owner = nullptr;
            std::uint32_t participant = 0u;
        };

        rhi_webgpu::WebGpuNativeBufferMapReceipt map;
        core::runtime::CompletionEvidenceBatch evidence{4u};
        std::array<Scope, 3> scopes;
        std::array<std::uintptr_t, 3> receipts{};

        ~MapProbe()
        {
            for (const auto receipt : receipts)
                rhi_webgpu::WebGpuNativeCompletionReceipt::release(receipt);
        }

        static void mapped(
            void* userdata, rhi_webgpu::WebGpuNativeBufferMapReceipt::Outcome outcome) noexcept
        {
            auto& self = *static_cast<MapProbe*>(userdata);
            assert(self.evidence.observe(
                0u, outcome == rhi_webgpu::WebGpuNativeBufferMapReceipt::Outcome::Success));
        }

        static void scoped(
            void* userdata, rhi_webgpu::WebGpuNativeCompletionReceipt::Outcome outcome) noexcept
        {
            auto& slot = *static_cast<Scope*>(userdata);
            assert(slot.owner->evidence.observe(slot.participant,
                outcome == rhi_webgpu::WebGpuNativeCompletionReceipt::Outcome::Success));
        }

        void prepare(void* device, void* queue, void* buffer)
        {
            assert(!map.prepareCold(buffer, 768u, this, &mapped));
            for (std::size_t i = 0u; i < scopes.size(); ++i) {
                scopes[i] = {this, static_cast<std::uint32_t>(i + 1u)};
                auto receipt = rhi_webgpu::WebGpuNativeCompletionReceipt::prepare(device,
                    queue,
                    rhi_webgpu::WebGpuNativeCompletionReceipt::Kind::Scope,
                    &scopes[i],
                    &scoped);
                assert(receipt);
                receipts[i] = receipt.release();
            }
        }
    };

    static inline core::UniquePtr<Context> nativeMapLossContext_;
    static inline std::shared_ptr<Root> nativeMapLossRoot_;
    static inline core::UniquePtr<MapProbe> nativeMapLossProbe_;
    static inline core::UniquePtr<Context> lossContext_;
    static inline std::shared_ptr<const Joins> lossJoin_;

    static std::array<Joins::Input, 2> inputs(const Root& root)
    {
        const auto& usage =
            *root.originalGraph()->originalInputs()->originalScalars()->originalUsage();
        const auto source =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureReadbackSource>>(
                usage.originalSources()->sources()[2u]);
        return std::array<Joins::Input, 2>{{{1u, source}, {2u, source}}};
    }

    static Joins::Budget budget(const Root& root)
    {
        const auto& original = root.originalGraph()->originalInputs();
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
            original->retainedOwnerCount() + 1u,
            bytes};
        // Native texture owns payload/allocation; buffer adds its write seal.
        // Four frozen decisions each retain the exact original layout owner.
        return Joins::Budget{1u,
            true,
            2u,
            1u,
            1u,
            root.retainedOwnerCount() + 1u + 2u * (1u + 2u + 3u) + 4u,
            20u,
            768u,
            bytes,
            stream,
            {1u, true, 2u, 4u, 1u, 1536u, 768u, bytes}};
    }
#endif
public:
    template <class Factory, class QueueWait>
    static void verifySnapshotPublication(Factory&& create, QueueWait&& wait)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        using NativeProbe = WebGpuNativeMapExecutionProbeFixture;
        using Probe = NativeProbe::Probe;
        for (unsigned probe = 0u; probe < 6u; ++probe) {
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 11> quotas;
            {
                Context context(create);
                const char* diagnostic = nullptr;
                auto root =
                    context.prepare(rhi::MemoryUsage::GpuToCpu, diagnostic, 2u, 768u, true, true);
                Prepared p(context, root, false, true, true, true);
                std::copy(p.quotas.begin(), p.quotas.end(), quotas.begin());
                quotas[10] = p.resultQuota;
                const auto snapshot = p.results->snapshots()[0];
                const auto request = snapshot->originalRequest();
                const auto source = inputs(*root)[0].source;
                auto nativeSource =
                    rhi_webgpu::webGpuNativePayloadFor(*source->originalSource()->originalObject());
                auto target = rhi_webgpu::webGpuNativePayloadFor(
                    *source->originalDestinationLease()->originalObject());
                std::array<unsigned char, 20> expected;
                for (std::size_t i = 0u; i < expected.size(); ++i)
                    expected[i] = static_cast<unsigned char>(111u + i);
                const auto& layout = snapshot->originalLayout();
                WebGpuNativeReadbackSeedFixture::seed(
                    p.queue, nativeSource->texture, *layout, expected.data(), expected.size());
                assert(wait(p.queue));
                ExecutionObservation observation{&p, probe == 2u, false, false};
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    quotas, [&]() noexcept -> const char* {
                        assert(!snapshot->publishedBytes() && !snapshot->nativeExecutionProven());
                        assert(!Issuers::recordOriginal(p.issuers, {1u, true}));
                        assert(WebGpuAsyncWaitFixture::until([&] {
                            return p.issuers->hasOriginalRecordedCommand();
                        }));
                        NativeProbe::instrument(nativeSource->ownerDevice,
                            target->buffer,
                            probe == 5u ? Probe::ScopeNativeError : static_cast<Probe>(probe),
                            &observation,
                            &observeMapExecution);
                        if (probe == 4u)
                            NativeProbe::usePublicationReadDiagnostic();
                        assert(!Issuers::submitOriginal(p.issuers, {1u, true}));
                        assert(WebGpuAsyncWaitFixture::until([&] {
                            return !p.custody->pendingOriginalRecords();
                        }));
                        if (probe == 4u) {
                            NativeProbe::verifyReadFault();
                            is(p.issuers->submissionDiagnostic(),
                                "RHI-WEBGPU-NATIVE-MAP-READ-NATIVE-ERROR");
                            assert(!p.custody->pendingOriginalRecords()
                                && !p.custody->originalNativeLossArmed());
                        }
                        NativeProbe::restore();
                        const bool success = probe < 2u;
                        assert(observation.observed
                            && p.issuers->hasOriginalCompletedSubmission() == success
                            && bool(snapshot->publishedBytes()) == success
                            && snapshot->nativeExecutionProven() == success && root->journalEmpty()
                            && root->status()
                                == (success ? Root::Status::Committed : Root::Status::Invalidated));
                        if (success) {
                            WebGpuNativeReadbackSeedFixture::verify(snapshot->publishedBytes(),
                                768u,
                                *layout,
                                expected.data(),
                                expected.size());
                            assert(core::SharedOwnerProjection::sameOwner(
                                snapshot->originalRequest(), request));
                            is(rhi::OriginalCommandReadbackResultSnapshot::validateOriginalCold(
                                   snapshot, request),
                                "RHI-ORIGINAL-READBACK-SNAPSHOT-IMMUTABLE");
                            const auto seal = p.command->originalAdmission()
                                                  ->originalReservation()
                                                  ->originalSeal();
                            rhi_webgpu::WebGpuNativeBufferMapReceipt unused;
                            is(rhi_webgpu::WebGpuCanonicalReadbackResultStorage::
                                    readOriginalUnpublished(p.results, seal, 0u, unused),
                                "RHI-WEBGPU-READBACK-STORAGE-SNAPSHOT-IMMUTABLE");
                            is(Issuers::submitOriginal(p.issuers, {1u, true}),
                                "RHI-WEBGPU-CANONICAL-SUBMISSION-ONE-SHOT");
                        }
                        return nullptr;
                    }));
                if (!probe) {
                    publicationSurvivor_ = snapshot;
                    publicationSurvivorQuotas_ = quotas;
                    publicationSurvivorBytes_ = expected;
                }
                p.drop();
                root.reset();
                nativeSource.reset();
                target.reset();
                context.device.reset();
            }
            if (probe)
                for (const auto& quota : quotas)
                    assert(!quota->liveBytes());
            else {
                // Survivor keeps CPU Request/layout/Result only; all runtime,
                // native allocation, graph, seal and issuer domains are gone.
                assert(core::SharedOwnerProjection::sameOwner(
                    publicationSurvivor_->originalRequest()->originalMetadataQuota(), quotas[1]));
                for (std::size_t i = 0u; i < 10u; ++i)
                    if (i != 1u)
                        assert(!quotas[i]->liveBytes());
                assert(quotas[1]->liveBytes() && quotas[10]->liveBytes());
            }
            if (probe == 4u)
                std::puts(
                    "BINDLESS_RHI_CANONICAL_PUBLICATION_READ_FAULT_OK intercepted-read-once exact-rhi-refusal unpublished callbacks-drained quotas11-zero");
            std::printf(
                "BINDLESS_RHI_CANONICAL_SNAPSHOT_PUBLICATION_OK probe%u explicit-cold-opt-in same-State-original-Submission commit-map-scopes-unmap exact-Request immutable-Result768 no-hot-allocation Prototype\n",
                probe);
        }
        {
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 11> quotas;
            {
                Context context(create);
                const char* diagnostic = nullptr;
                auto root =
                    context.prepare(rhi::MemoryUsage::GpuToCpu, diagnostic, 2u, 768u, true, true);
                Prepared p(context, root, false, true, true, true);
                const auto snapshot = p.results->snapshots()[0];
                std::copy(p.quotas.begin(), p.quotas.end(), quotas.begin());
                quotas[10] = p.resultQuota;
                const auto source = inputs(*root)[0].source;
                const auto nativeSource =
                    rhi_webgpu::webGpuNativePayloadFor(*source->originalSource()->originalObject());
                const auto target = rhi_webgpu::webGpuNativePayloadFor(
                    *source->originalDestinationLease()->originalObject());
                auto observer = p.custody->originalStateForInspection();
                ExecutionObservation observation{&p, false, false, true};
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    quotas, [&]() noexcept -> const char* {
                        assert(!Issuers::recordOriginal(p.issuers, {1u, true}));
                        assert(WebGpuAsyncWaitFixture::until([&] {
                            return p.issuers->hasOriginalRecordedCommand();
                        }));
                        NativeProbe::instrument(nativeSource->ownerDevice,
                            target->buffer,
                            Probe::Success,
                            &observation,
                            &observeMapExecution);
                        assert(!Issuers::submitOriginal(p.issuers, {1u, true}));
                        assert(WebGpuAsyncWaitFixture::until([&] {
                            return observer.expired();
                        }));
                        NativeProbe::restore();
                        observer.reset();
                        assert(observation.observed && !snapshot->publishedBytes()
                            && !snapshot->nativeExecutionProven()
                            && root->status() == Root::Status::Invalidated && root->journalEmpty());
                        return nullptr;
                    }));
            }
            for (const auto& quota : quotas)
                assert(!quota->liveBytes());
            std::puts(
                "BINDLESS_RHI_CANONICAL_SNAPSHOT_PUBLICATION_OK probe6 caller-drop-cancels publication-suppressed same-State-safe-drain core-hot-allocation0 quotas11-zero");
        }
#else
        (void)create;
        (void)wait;
#endif
    }

    template <class Factory, class QueueWait>
    static void verifyMapExecution(Factory&& create, QueueWait&& wait)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        using NativeProbe = WebGpuNativeMapExecutionProbeFixture;
        using Probe = NativeProbe::Probe;
        for (unsigned probe = 0u; probe < 6u; ++probe) {
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 11> quotas;
            {
                Context context(create);
                const char* diagnostic = nullptr;
                auto root =
                    context.prepare(rhi::MemoryUsage::GpuToCpu, diagnostic, 2u, 768u, true, true);
                Prepared p(context, root, false, true);
                std::copy(p.quotas.begin(), p.quotas.end(), quotas.begin());
                quotas[10] = p.resultQuota;
                const auto source = inputs(*root)[0].source;
                auto nativeSource =
                    rhi_webgpu::webGpuNativePayloadFor(*source->originalSource()->originalObject());
                auto nativeTarget = rhi_webgpu::webGpuNativePayloadFor(
                    *source->originalDestinationLease()->originalObject());
                const auto& layout = source->originalSource()->originalRequest()->originalLayout();
                std::array<unsigned char, 20> expected;
                for (std::size_t i = 0u; i < expected.size(); ++i)
                    expected[i] = static_cast<unsigned char>(31u + i);
                WebGpuNativeReadbackSeedFixture::seed(
                    p.queue, nativeSource->texture, *layout, expected.data(), expected.size());
                assert(wait(p.queue));
                ExecutionObservation observation{&p, probe == 2u, false};
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    quotas, [&]() noexcept -> const char* {
                        assert(!Issuers::recordOriginal(p.issuers, {1u, true}));
                        assert(WebGpuAsyncWaitFixture::until([&] {
                            return p.issuers->hasOriginalRecordedCommand();
                        }));
                        NativeProbe::instrument(nativeSource->ownerDevice,
                            nativeTarget->buffer,
                            (probe == 5u ? Probe::ScopeNativeError : static_cast<Probe>(probe)),
                            &observation,
                            &observeMapExecution);
                        assert(!Issuers::submitOriginal(p.issuers, {1u, true}));
                        assert(WebGpuAsyncWaitFixture::until([&] {
                            return !p.custody->pendingOriginalRecords();
                        }));
                        if (probe == 4u) {
                            NativeProbe::verifyReadFault();
                            is(p.issuers->submissionDiagnostic(),
                                "RHI-WEBGPU-NATIVE-MAP-READ-NATIVE-ERROR");
                            assert(!p.custody->pendingOriginalRecords()
                                && !p.custody->originalNativeLossArmed());
                        }
                        NativeProbe::restore();
                        assert(observation.observed);
                        const bool success = probe < 2u;
                        assert(p.issuers->hasOriginalCompletedSubmission() == success
                            && root->journalEmpty()
                            && root->status()
                                == (success ? Root::Status::Committed : Root::Status::Invalidated));
                        assert(!p.results->snapshots()[0]->publishedBytes()
                            && !p.custody->originalNativeLossArmed());
                        if (success)
                            WebGpuNativeReadbackSeedFixture::verify(
                                p.results->unpublishedBytesForInspection(0u),
                                768u,
                                *layout,
                                expected.data(),
                                expected.size());
                        else
                            assert(p.issuers->submissionDiagnostic());
                        p.drop();
                        root.reset();
                        nativeTarget.reset();
                        nativeSource.reset();
                        context.device.reset();
                        return nullptr;
                    }));
            }
            for (const auto& quota : quotas)
                assert(!quota->liveBytes());
            if (probe == 4u)
                std::puts(
                    "BINDLESS_RHI_CANONICAL_MAP_EXECUTION_READ_FAULT_OK intercepted-read-once exact-rhi-refusal unpublished callbacks-drained quotas11-zero");
            std::printf(
                "BINDLESS_RHI_CANONICAL_MAP_EXECUTION_OK probe%u original-Submission State-seal-journal-loss-retained Requests1 writes2 copies4 unpublished-Result768 native-map-scopes-unmap MapDrain core-hot-allocation0 quotas11-zero Prototype\n",
                probe);
        }
        {
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 11> quotas;
            {
                Context context(create);
                const char* diagnostic = nullptr;
                auto root =
                    context.prepare(rhi::MemoryUsage::GpuToCpu, diagnostic, 2u, 768u, true, true);
                Prepared p(context, root, false, true);
                const auto storage = p.results;
                std::copy(p.quotas.begin(), p.quotas.end(), quotas.begin());
                quotas[10] = p.resultQuota;
                const auto source = inputs(*root)[0].source;
                const auto nativeSource =
                    rhi_webgpu::webGpuNativePayloadFor(*source->originalSource()->originalObject());
                const auto target = rhi_webgpu::webGpuNativePayloadFor(
                    *source->originalDestinationLease()->originalObject());
                const auto& layout = source->originalSource()->originalRequest()->originalLayout();
                std::array<unsigned char, 20> expected;
                for (std::size_t i = 0u; i < expected.size(); ++i)
                    expected[i] = static_cast<unsigned char>(71u + i);
                WebGpuNativeReadbackSeedFixture::seed(
                    p.queue, nativeSource->texture, *layout, expected.data(), expected.size());
                assert(wait(p.queue));
                auto stateObserver = p.custody->originalStateForInspection();
                ExecutionObservation observation{&p, false, false, true};
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    quotas, [&]() noexcept -> const char* {
                        assert(!Issuers::recordOriginal(p.issuers, {1u, true}));
                        assert(WebGpuAsyncWaitFixture::until([&] {
                            return p.issuers->hasOriginalRecordedCommand();
                        }));
                        NativeProbe::instrument(nativeSource->ownerDevice,
                            target->buffer,
                            Probe::Success,
                            &observation,
                            &observeMapExecution);
                        assert(!Issuers::submitOriginal(p.issuers, {1u, true}));
                        assert(WebGpuAsyncWaitFixture::until([&] {
                            return stateObserver.expired();
                        }));
                        NativeProbe::restore();
                        assert(observation.observed && !p.issuers && !p.custody && !p.command
                            && root->journalEmpty() && !storage->snapshots()[0]->publishedBytes());
                        assert(root->status() == Root::Status::Invalidated);
                        stateObserver.reset();
                        return nullptr;
                    }));
            }
            for (const auto& quota : quotas)
                assert(!quota->liveBytes());
            std::puts(
                "BINDLESS_RHI_CANONICAL_MAP_EXECUTION_OK probe6 caller-shells-dropped original-native-cycle genuine-C-future drop-cancelled safe-drain unpublished core-hot-allocation0 quotas11-zero Prototype");
        }
        for (unsigned probe = 0u; probe < executionLoss_.size(); ++probe) {
            std::printf("BINDLESS_RHI_CANONICAL_MAP_UNCERTAIN_BEGIN probe%u\n", probe);
            executionLossContexts_[probe] = core::makeUnique<Context>(create);
            auto& context = *executionLossContexts_[probe];
            const char* diagnostic = nullptr;
            auto root =
                context.prepare(rhi::MemoryUsage::GpuToCpu, diagnostic, 2u, 768u, true, true);
            executionLoss_[probe] =
                core::makeUnique<Prepared>(context, root, false, true, true, true);
            auto& p = *executionLoss_[probe];
            assert(!Issuers::recordOriginal(p.issuers, {1u, true}));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return p.issuers->hasOriginalRecordedCommand();
            }));
            if (probe == 3u) {
                const auto source = inputs(*root)[0].source;
                const auto nativeSource =
                    rhi_webgpu::webGpuNativePayloadFor(*source->originalSource()->originalObject());
                const auto target = rhi_webgpu::webGpuNativePayloadFor(
                    *source->originalDestinationLease()->originalObject());
                pendingMapObservation_ = {&p, false, false, false};
                auto& observation = pendingMapObservation_;
                NativeProbe::instrument(nativeSource->ownerDevice,
                    target->buffer,
                    Probe::PendingLoss,
                    &observation,
                    &observeMapExecution);
                assert(!Issuers::submitOriginal(p.issuers, {1u, true}));
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return observation.observed;
                }));
                NativeProbe::restore(true);
                assert(root->status() == Root::Status::Pending && !root->journalEmpty()
                    && p.custody->pendingOriginalRecords() == 1u);
                continue;
            }
            const auto source = inputs(*root)[0].source;
            const auto nativeSource =
                rhi_webgpu::webGpuNativePayloadFor(*source->originalSource()->originalObject());
            const auto target = rhi_webgpu::webGpuNativePayloadFor(
                *source->originalDestinationLease()->originalObject());
            const auto kind = probe == 0u ? Probe::ThrowBefore
                : probe == 1u             ? Probe::ThrowAfter
                                          : Probe::UnmapThrow;
            ExecutionObservation observation{&p, false, false};
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 11> quotas;
            std::copy(p.quotas.begin(), p.quotas.end(), quotas.begin());
            quotas[10] = p.resultQuota;
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    NativeProbe::instrument(nativeSource->ownerDevice,
                        target->buffer,
                        kind,
                        &observation,
                        &observeMapExecution);
                    assert(!Issuers::submitOriginal(p.issuers, {1u, true}));
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return WebGpuCanonicalSubmissionFixture::originalSubmissionSlotsDrained(
                            *p.issuers);
                    }));
                    NativeProbe::restore();
                    assert(observation.observed && p.issuers->submissionDiagnostic()
                        && !p.issuers->hasOriginalCompletedSubmission()
                        && p.custody->pendingOriginalRecords() == 1u
                        && root->status() == Root::Status::Invalidated && root->journalEmpty()
                        && p.custody->originalNativeLossArmed());
                    assert(!p.results->snapshots()[0]->publishedBytes());
                    return nullptr;
                }));
        }
#else
        (void)create;
        (void)wait;
#endif
    }

    template <class Factory, class QueueWait>
    static void verifyRetainedMapping(Factory&& create, QueueWait&& wait)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        using Storage = rhi_webgpu::WebGpuCanonicalReadbackResultStorage;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 11> quotas;
        {
            Context context(create);
            const char* diagnostic = nullptr;
            auto root = context.prepare(rhi::MemoryUsage::GpuToCpu, diagnostic, 2u, 768u, true);
            Prepared p(context, root, false);
            const auto admission = p.command->originalAdmission();
            const auto seal = admission->originalReservation()->originalSeal();
            auto resultQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
            auto storage = Storage::prepareOriginal(admission,
                *context.device,
                {1u,
                    true,
                    1u,
                    768u,
                    bytes,
                    admission->retainedOwnerCount()
                        + rhi::OriginalCommandReadbackResultStorageContract::fixedAdditionalOwners
                        + 1u},
                resultQuota,
                diagnostic);
            assert(storage);
            const auto request = storage->snapshots()[0]->originalRequest();
            auto target = rhi_webgpu::webGpuNativePayloadFor(
                *inputs(*root)[0].source->originalDestinationLease()->originalObject());
            std::array<unsigned char, 768> expected{}, actual{};
            for (std::size_t i = 0u; i < expected.size(); ++i)
                expected[i] = static_cast<unsigned char>(i);
            wgpuQueueWriteBuffer(p.queue, target->buffer, 0u, expected.data(), expected.size());
            assert(wait(p.queue));
            MapProbe map;
            map.prepare(target->ownerDevice, p.queue, target->buffer);
            is(Storage::validateOriginalRetained(storage, seal),
                "RHI-WEBGPU-READBACK-STORAGE-RETAINED-PENDING");
            assert(!Issuers::recordOriginal(p.issuers, {1u, true}));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return p.issuers->hasOriginalRecordedCommand();
            }));
            assert(root->status() == Root::Status::Pending && !root->journalEmpty()
                && storage->validate());
            assert(!Storage::validateOriginalRetained(storage, seal));
            auto forged = std::shared_ptr<const Storage>(resultQuota, storage.get());
            is(Storage::validateOriginalRetained(forged, seal),
                "RHI-WEBGPU-READBACK-STORAGE-OWNER");
            forged.reset();
            auto aliasSeal =
                std::shared_ptr<const rhi::OriginalCommandRecordingSeal>(p.callback, seal.get());
            is(Storage::validateOriginalRetained(storage, aliasSeal),
                "RHI-WEBGPU-READBACK-STORAGE-RETAINED-SEAL");
            aliasSeal.reset();
            is(Storage::validateOriginalRetained(storage, seal, &request),
                "RHI-WEBGPU-READBACK-STORAGE-MAP-BINDING");
            auto aliasRequest = std::shared_ptr<const rhi::TextureReadbackRequestDeclaration>(
                resultQuota, request.get());
            is(Storage::validateOriginalRetained(storage, seal, &aliasRequest, &map.map),
                "RHI-WEBGPU-READBACK-JOIN-MAP-REQUEST-OWNER");
            aliasRequest.reset();
            {
                Context foreign(create);
                auto foreignRoot =
                    foreign.prepare(rhi::MemoryUsage::GpuToCpu, diagnostic, 2u, 768u, true);
                Prepared other(foreign, foreignRoot, false);
                const auto foreignSeal =
                    other.command->originalAdmission()->originalReservation()->originalSeal();
                is(Storage::validateOriginalRetained(storage, foreignSeal),
                    "RHI-WEBGPU-READBACK-STORAGE-RETAINED-SEAL");
                const auto foreignEntry = inputs(*foreignRoot)[0];
                const auto foreignRequest =
                    foreignEntry.source->originalSource()->originalRequest();
                auto foreignTarget = rhi_webgpu::webGpuNativePayloadFor(
                    *foreignEntry.source->originalDestinationLease()->originalObject());
                MapProbe otherMap;
                otherMap.prepare(foreignTarget->ownerDevice, other.queue, foreignTarget->buffer);
                is(Storage::validateOriginalRetained(storage, seal, &foreignRequest, &otherMap.map),
                    "RHI-WEBGPU-READBACK-JOIN-MAP-REQUEST");
                is(Storage::validateOriginalRetained(storage, seal, &request, &otherMap.map),
                    "RHI-WEBGPU-NATIVE-MAP-RECEIPT-BINDING");
            }
            std::copy(p.quotas.begin(), p.quotas.end(), quotas.begin());
            quotas[10] = resultQuota;
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    assert(!Storage::validateOriginalRetained(storage, seal, &request, &map.map));
                    assert(
                        rhi_webgpu::WebGpuNativeCompletionReceipt::pushScopes(target->ownerDevice));
                    assert(!map.map.start());
                    assert(!Storage::validateOriginalRetained(storage, seal, &request, &map.map));
                    for (const auto receipt : map.receipts)
                        assert(rhi_webgpu::WebGpuNativeCompletionReceipt::bind(receipt));
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return map.evidence.allReceived();
                    }));
                    assert(map.map.nativeMapSucceeded() && !map.map.nativeUnmapProven());
                    assert(!Storage::validateOriginalRetained(storage, seal, &request, &map.map));
                    is(Storage::validateOriginalRetained(storage, seal),
                        "RHI-WEBGPU-READBACK-JOIN-NATIVE-MAP-STATE");
                    assert(
                        !map.map.readAndUnmap(actual.data(), actual.size()) && actual == expected);
                    assert(!Storage::validateOriginalRetained(storage, seal, &request, &map.map)
                        && !Storage::validateOriginalRetained(storage, seal));
                    assert(!p.issuers->hasOriginalCompletedSubmission()
                        && !storage->snapshots()[0]->publishedBytes()
                        && root->status() == Root::Status::Pending && !root->journalEmpty());
                    assert(!p.custody->cancelOriginal());
                    assert(Storage::validateOriginalRetained(storage, seal, &request, &map.map));
                    return nullptr;
                }));
        }
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_CANONICAL_RETAINED_MAP_VALIDATION_OK genuine-Prepared-Pending-Mapped-Unmapped-C-receipt original-seal-Admission-Request staging768 repeated-writes2 native-bytes-match forged-foreign-pair-refused cancel-stale no-Submission-or-publication-authority core-hot-allocation0 quotas11-zero canonical-map-gates-closed");
#else
        (void)create;
        (void)wait;
#endif
    }

    template <class Factory, class QueueWait>
    static void verifyMapReceipts(Factory&& create, QueueWait&& wait)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        using Receipt = rhi_webgpu::WebGpuNativeBufferMapReceipt;
        using NativeScope = rhi_webgpu::WebGpuNativeCompletionReceipt;
        for (unsigned probe = 0u; probe < 8u; ++probe) {
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 8> quotas;
            {
                Context context(create);
                const char* diagnostic = nullptr;
                auto root = context.prepare(rhi::MemoryUsage::GpuToCpu, diagnostic);
                const auto entry = inputs(*root)[0];
                auto target = rhi_webgpu::webGpuNativePayloadFor(
                    *entry.source->originalDestinationLease()->originalObject());
                auto queue = wgpuDeviceGetQueue(target->ownerDevice);
                assert(queue);
                std::array<unsigned char, 768> expected{}, result{};
                for (std::size_t i = 0u; i < expected.size(); ++i)
                    expected[i] = static_cast<unsigned char>(i);
                wgpuQueueWriteBuffer(queue, target->buffer, 0u, expected.data(), expected.size());
                assert(wait(queue));
                MapProbe p;
                p.prepare(target->ownerDevice, queue, target->buffer);
                assert(!p.map.validateRetainedBinding(target->buffer, 768u));
                is(p.map.validateRetainedBinding(target->buffer, 764u),
                    "RHI-WEBGPU-NATIVE-MAP-RECEIPT-BINDING");
                Receipt invalid;
                is(invalid.prepareCold(nullptr, 768u, &p, &MapProbe::mapped),
                    "RHI-WEBGPU-NATIVE-MAP-RECEIPT-BINDING");
                is(invalid.prepareCold(target->buffer, 767u, &p, &MapProbe::mapped),
                    "RHI-BUFFER-READBACK-MAP-RANGE-ALIGNMENT");
                is(invalid.prepareCold(target->buffer, 764u, &p, &MapProbe::mapped),
                    "RHI-WEBGPU-NATIVE-MAP-RECEIPT-LAYOUT");
                is(p.map.prepareCold(target->buffer, 768u, &p, &MapProbe::mapped),
                    "RHI-WEBGPU-NATIVE-MAP-RECEIPT-ONE-SHOT");
                if (probe == 2u || probe == 3u)
                    // clang-format off
                    EM_ASM({
                        // clang-format off
                            const buffer = WebGPU.getJsObject($0);
                            Module.arpMapReceiptBuffer = buffer;
                            Module.arpMapReceiptOriginal = buffer.mapAsync;
                            buffer.mapAsync = function(...args) {
                                if ($1)
                                    Module.arpMapReceiptOriginal.apply(this, args).catch(function() {});
                                throw new Error('Authored map invocation uncertainty');
                            };
                            // clang-format on
                        },
                        target->buffer,
                        probe == 3u);
                // clang-format on
                if (probe == 5u)
                    wgpuBufferDestroy(target->buffer);
                quotas = context.quotas;
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    quotas, [&]() noexcept -> const char* {
                        assert(NativeScope::pushScopes(target->ownerDevice));
                        const auto* start = p.map.start();
                        if (probe == 2u || probe == 3u) {
                            is(start, "RHI-WEBGPU-NATIVE-MAP-INVOCATION-UNCERTAIN");
                            assert(p.map.invocationUncertain());
                            is(p.map.validateRetainedBinding(target->buffer, 768u),
                                "RHI-WEBGPU-NATIVE-MAP-RECEIPT-UNPROVEN");
                            // clang-format off
                            EM_ASM({
                                // clang-format off
                                    Module.arpMapReceiptBuffer.mapAsync = Module.arpMapReceiptOriginal;
                                    delete Module.arpMapReceiptBuffer;
                                    delete Module.arpMapReceiptOriginal;
                                // clang-format on
                            });
                            // clang-format on
                            assert(!p.map.abortAndDrain());
                        } else
                            assert(!start);
                        if (probe == 7u)
                            assert(!p.map.abortAndDrain());
                        // Releasing a native adapter handle suppresses its callbacks;
                        // keep the actual handles owned until genuine delivery below.
                        for (const auto receipt : p.receipts)
                            assert(NativeScope::bind(receipt));
                        assert(WebGpuAsyncWaitFixture::until([&] {
                            return p.evidence.allReceived();
                        }));
                        const bool success =
                            probe != 2u && probe != 3u && probe != 5u && probe != 7u;
                        assert(p.map.nativeMapSucceeded() == success);
                        if (success) {
                            assert(!p.map.validateRetainedBinding(target->buffer, 768u));
                            if (probe == 1u) {
                                is(p.map.readAndUnmap(result.data(), 767u),
                                    "RHI-WEBGPU-NATIVE-MAP-READ-RANGE");
                                assert(!p.map.nativeUnmapProven());
                            }
                            if (probe == 4u)
                                // clang-format off
                                EM_ASM({
                                    // clang-format off
                                        const buffer = WebGPU.getJsObject($0);
                                        Module.arpMapReceiptBuffer = buffer;
                                        Module.arpMapReceiptOriginal = buffer.unmap;
                                        buffer.unmap = function() {
                                            throw new Error('Authored unmap invocation uncertainty');
                                        };
                                        // clang-format on
                                    },
                                    target->buffer);
                            // clang-format on
                            if (probe == 6u)
                                // clang-format off
                                EM_ASM({
                                    // clang-format off
                                        const buffer = WebGPU.getJsObject($0);
                                        Module.arpMapReceiptBuffer = buffer;
                                        Module.arpMapReceiptOriginal = buffer.getMappedRange;
                                        Module.arpMapReceiptReadFaultInvocations = 0;
                                        buffer.getMappedRange = function() {
                                            ++Module.arpMapReceiptReadFaultInvocations;
                                            throw new RangeError('CanonicalMapReceipt authored read failure');
                                        };
                                        Module.arpExpectedCanonicalMapReadDiagnostic =
                                            'buffer.getMappedRange(0, 768) failed: RangeError: CanonicalMapReceipt authored read failure';
                                        // clang-format on
                                    },
                                    target->buffer);
                            // clang-format on
                            const auto* read = p.map.readAndUnmap(result.data(), result.size());
                            if (probe == 4u) {
                                is(read, "RHI-WEBGPU-NATIVE-MAP-UNMAP-UNCERTAIN");
                                assert(!p.map.nativeUnmapProven() && p.map.invocationUncertain());
                                // clang-format off
                                EM_ASM({
                                    // clang-format off
                                        Module.arpMapReceiptBuffer.unmap = Module.arpMapReceiptOriginal;
                                        delete Module.arpMapReceiptBuffer;
                                        delete Module.arpMapReceiptOriginal;
                                    // clang-format on
                                });
                                // clang-format on
                                // The audited emdawn adapter clears its onUnmap list
                                // before the throwing native call. A retry cannot
                                // restore native semantics: require explicit teardown.
                                is(p.map.abortAndDrain(), "RHI-WEBGPU-NATIVE-MAP-UNMAP-UNPROVEN");
                                wgpuBufferDestroy(target->buffer);
                                assert(!p.map.abortAndDrain());
                            } else if (probe == 6u) {
                                is(read, "RHI-WEBGPU-NATIVE-MAP-READ-NATIVE-ERROR");
                                // clang-format off
                                assert(EM_ASM_INT({
                                    // clang-format off
                                        return Module.arpMapReceiptReadFaultInvocations === 1;
                                    // clang-format on
                                }));
                                // clang-format on
                                assert((result == std::array<unsigned char, 768>{}));
                                // clang-format off
                                EM_ASM({
                                    // clang-format off
                                        Module.arpMapReceiptBuffer.getMappedRange = Module.arpMapReceiptOriginal;
                                        delete Module.arpMapReceiptBuffer;
                                        delete Module.arpMapReceiptOriginal;
                                        delete Module.arpMapReceiptReadFaultInvocations;
                                    // clang-format on
                                });
                                // clang-format on
                            } else {
                                assert(!read && result == expected);
                            }
                        } else {
                            assert(!p.map.abortAndDrain());
                            is(p.map.readAndUnmap(result.data(), result.size()),
                                "RHI-WEBGPU-NATIVE-MAP-READ-STATUS");
                        }
                        assert(p.map.nativeUnmapProven());
                        return nullptr;
                    }));
                wgpuQueueRelease(queue);
                target.reset();
                root.reset();
                context.device.reset();
            }
            for (const auto& quota : quotas)
                assert(!quota->liveBytes());
            if (probe == 6u)
                std::puts(
                    "BINDLESS_RHI_NATIVE_MAP_RECEIPT_READ_FAULT_OK intercepted-read-once exact-rhi-refusal output-unchanged native-unmapped quotas8-zero");
            std::printf(
                "BINDLESS_RHI_NATIVE_MAP_RECEIPT_PROBE_OK probe%u genuine-C-map-scopes exact-read768 bridge-native-state unknown-invocation-retained synchronous-abort-drain core-hot-allocation0 bridge-Prototype quotas8-zero canonical-map-publication-closed\n",
                probe);
        }
        nativeMapLossContext_ = core::makeUnique<Context>(create);
        const char* diagnostic = nullptr;
        nativeMapLossRoot_ = nativeMapLossContext_->prepare(rhi::MemoryUsage::GpuToCpu, diagnostic);
        const auto entry = inputs(*nativeMapLossRoot_)[0];
        const auto target = rhi_webgpu::webGpuNativePayloadFor(
            *entry.source->originalDestinationLease()->originalObject());
        const auto queue = wgpuDeviceGetQueue(target->ownerDevice);
        assert(queue);
        {
            core::CpuMemoryQuotaScope cold(nativeMapLossContext_->quotas[3]);
            nativeMapLossProbe_ = core::makeUnique<MapProbe>();
        }
        nativeMapLossProbe_->prepare(target->ownerDevice, queue, target->buffer);
        wgpuQueueRelease(queue);
#else
        (void)create;
        (void)wait;
#endif
    }

    template <class Factory> static void verifyMapCold(Factory&& create)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 11> quotas;
        {
            Context context(create);
            const char* diagnostic = nullptr;
            auto root =
                context.prepare(rhi::MemoryUsage::GpuToCpu, diagnostic, 2u, 768u, true, true);
            Prepared p(context, root, true, true);
            std::copy(p.quotas.begin(), p.quotas.end(), quotas.begin());
            quotas[10] = p.resultQuota;
            for (const auto capacity : {8u, 11u}) {
                Context undersized(create);
                auto smallRoot = undersized.prepare(
                    rhi::MemoryUsage::GpuToCpu, diagnostic, 2u, 768u, true, true, capacity);
                Admission::Requirements refused = p.command->originalAdmission()->requirements();
                is(diagnostic = Admission::deriveRequirements(
                       smallRoot, p.command->originalAdmission()->originalBudget(), refused),
                    "RHI-ORIGINAL-READBACK-MAP-SOURCE-CALLBACK-CAPACITY");
                assert(!refused.originalCommands && !refused.capacity.callbackTickets
                    && !refused.readbackMap.maps);
            }
            {
                Context foreign(create);
                auto foreignRoot =
                    foreign.prepare(rhi::MemoryUsage::GpuToCpu, diagnostic, 2u, 768u, true, true);
                Prepared other(foreign, foreignRoot, false, true, false);
                assert(!Custody::prepareOriginal(p.command,
                    {2u, true, 1u, 12u, p.ownerLimit, bytes, bytes},
                    diagnostic,
                    other.results));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-CALLBACK-RESULT-ADMISSION");
            }
            const auto& source = root->originalGraph()
                                     ->originalInputs()
                                     ->originalScalars()
                                     ->originalUsage()
                                     ->originalSources();
            assert(source->originalReservedSubmissionCount() == 1u
                && source->originalReservedCallbackTicketCount() == 12u
                && source->originalReservedCallbackRegistrationCount() == 12u
                && p.custody->pendingOriginalRecords() == 1u);
            assert(!p.results->snapshots()[0]->publishedBytes()
                && root->status() == Root::Status::Prepared && root->journalEmpty());
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    assert(!p.custody->validate() && !p.lowering->validate()
                        && !p.results->validate());
                    assert(!Issuers::prepareOriginal(
                        p.lowering, {1u, true, 1u, 1u, p.ownerLimit, bytes, bytes}, diagnostic));
                    is(diagnostic, "RHI-WEBGPU-CANONICAL-ISSUER-DUPLICATE");
                    return nullptr;
                }));
            p.drop();
            root.reset();
            context.device.reset();
        }
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
        std::uint64_t submissionPeak = 0u, callbackPeak = 0u;
        {
            Context context(create);
            const char* diagnostic = nullptr;
            auto root =
                context.prepare(rhi::MemoryUsage::GpuToCpu, diagnostic, 2u, 768u, true, true);
            Prepared p(context, root, false, true, false);
            core::CpuMemoryQuotaLease sMarker(
                p.submission, p.submission->peakBytes() - p.submission->liveBytes() + 1u);
            core::CpuMemoryQuotaLease cMarker(
                p.callback, p.callback->peakBytes() - p.callback->liveBytes() + 1u);
            assert(sMarker.charged() && cMarker.charged());
            const auto sBase = p.submission->liveBytes(), cBase = p.callback->liveBytes();
            auto custody = Custody::prepareOriginal(
                p.command, {2u, true, 1u, 12u, p.ownerLimit, bytes, bytes}, diagnostic, p.results);
            assert(custody);
            submissionPeak = p.submission->peakBytes() - sBase;
            callbackPeak = p.callback->peakBytes() - cBase;
        }
        for (const bool callbackDomain : {false, true})
            for (const int difference : {0, -1, -2}) {
                Context context(create);
                const char* diagnostic = nullptr;
                auto root =
                    context.prepare(rhi::MemoryUsage::GpuToCpu, diagnostic, 2u, 768u, true, true);
                Prepared p(context, root, false, true, false);
                const auto quota = callbackDomain ? p.callback : p.submission;
                const auto peak = callbackDomain ? callbackPeak : submissionPeak;
                const auto baseline = quota->liveBytes();
                {
                    const auto available = difference == -2 ? 0u : peak + difference;
                    core::CpuMemoryQuotaLease pressure(
                        quota, quota->limitBytes() - baseline - available);
                    assert(pressure.charged());
                    auto custody = Custody::prepareOriginal(p.command,
                        {2u, true, 1u, 12u, p.ownerLimit, bytes, bytes},
                        diagnostic,
                        p.results);
                    assert(bool(custody) == (difference == 0));
                    if (!custody)
                        is(diagnostic, "RHI-WEBGPU-CANONICAL-CALLBACK-ALLOCATION");
                }
                assert(quota->liveBytes() == baseline);
                if (difference) {
                    auto retry = Custody::prepareOriginal(p.command,
                        {2u, true, 1u, 12u, p.ownerLimit, bytes, bytes},
                        diagnostic,
                        p.results);
                    assert(retry);
                }
            }
        {
            Context context(create);
            const char* diagnostic = nullptr;
            auto root =
                context.prepare(rhi::MemoryUsage::GpuToCpu, diagnostic, 2u, 768u, true, true);
            Prepared p(context, root, false, true, false);
            const auto submissionBase = p.submission->liveBytes(),
                       callbackBase = p.callback->liveBytes();
            const auto native = p.command->originalAdmission()->originalClosure()->nativeDevice();
            // clang-format off
            EM_ASM({
                // clang-format off
                    Module.arpMapColdScopeDevice = WebGPU.getJsObject($0);
                    Module.arpMapColdScopeOriginal = Emval.toHandle;
                    Module.arpMapColdScopeCount = 0;
                    Emval.toHandle = function(value) {
                        if (value && value.scope && value.device === Module.arpMapColdScopeDevice &&
                            ++Module.arpMapColdScopeCount === 2)
                            throw new Error('Authored second cold map scope receipt allocation failure');
                        return Module.arpMapColdScopeOriginal.call(this, value);
                    };
                    // clang-format on
                },
                native);
            // clang-format on
            assert(!Custody::prepareOriginal(
                p.command, {2u, true, 1u, 12u, p.ownerLimit, bytes, bytes}, diagnostic, p.results));
            is(diagnostic, "RHI-WEBGPU-CANONICAL-MAP-SCOPE-PREPARATION");
            assert(p.submission->liveBytes() == submissionBase
                && p.callback->liveBytes() == callbackBase
                && root->status() == Root::Status::Prepared && root->journalEmpty());
            // clang-format off
            EM_ASM({
                // clang-format off
                    assert(Module.arpMapColdScopeCount === 2);
                    Emval.toHandle = Module.arpMapColdScopeOriginal;
                    delete Module.arpMapColdScopeDevice;
                    delete Module.arpMapColdScopeOriginal;
                    delete Module.arpMapColdScopeCount;
                // clang-format on
            });
            // clang-format on
            auto retry = Custody::prepareOriginal(
                p.command, {2u, true, 1u, 12u, p.ownerLimit, bytes, bytes}, diagnostic, p.results);
            assert(retry);
        }
        std::puts(
            "BINDLESS_RHI_CANONICAL_MAP_RECEIPT_COLD_OK same-Request-staging-slot map1-native-scopes3 native-scope-preparation-failure-unwinds-no-claim retry canonical-map-publication-closed");
        std::printf(
            "BINDLESS_RHI_CANONICAL_MAP_COLD_PRESSURE_OK Submission-peak%llu Callback-peak%llu exact-less-exhausted no-claim-leak retry Source8-11-refused foreign-genuine-Result-refused\n",
            static_cast<unsigned long long>(submissionPeak),
            static_cast<unsigned long long>(callbackPeak));
        mapLossContext_ = core::makeUnique<Context>(create);
        const char* diagnostic = nullptr;
        auto root =
            mapLossContext_->prepare(rhi::MemoryUsage::GpuToCpu, diagnostic, 2u, 768u, true, true);
        mapLossPrepared_ = core::makeUnique<Prepared>(*mapLossContext_, root, false, true);
        std::puts(
            "BINDLESS_RHI_CANONICAL_MAP_COLD_OK profile5 CallbackCustody2 same-root-seal-Admission Result1 slots12 Source1-12-12 exact-native-copy-projection4 issuer-refused map-publication-closed hot-allocation0 quotas11-zero");
#else
        (void)create;
#endif
    }

    template <class Factory> static void verifyResultStorage(Factory&& create)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        using Storage = rhi_webgpu::WebGpuCanonicalReadbackResultStorage;
        using Snapshot = rhi::OriginalCommandReadbackResultSnapshot;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 11> quotas;
        std::shared_ptr<const Snapshot> snapshot;
        {
            Context context(create);
            const char* diagnostic = nullptr;
            auto root = context.prepare(rhi::MemoryUsage::GpuToCpu, diagnostic, 2u, 768u, true);
            Prepared p(context, root, false);
            auto admission = p.command->originalAdmission();
            auto resultQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
            std::copy(p.quotas.begin(), p.quotas.end(), quotas.begin());
            quotas[10] = resultQuota;
            const Storage::Budget budget{1u,
                true,
                1u,
                768u,
                bytes,
                admission->retainedOwnerCount()
                    + rhi::OriginalCommandReadbackResultStorageContract::fixedAdditionalOwners
                    + 1u};
            const auto make = [&](const auto& q, const auto& b) {
                return Storage::prepareOriginal(admission, *context.device, b, q, diagnostic);
            };
            for (const auto& q : p.quotas) {
                assert(!make(q, budget));
                is(diagnostic, "RHI-WEBGPU-READBACK-STORAGE-QUOTA-DOMAIN");
            }
            auto forgedQuota =
                std::shared_ptr<core::CpuMemoryQuota>(p.quotas[0], resultQuota.get());
            assert(!make(forgedQuota, budget));
            forgedQuota.reset();
            for (unsigned probe = 0u; probe < 4u; ++probe) {
                auto bad = budget;
                if (!probe)
                    --bad.maxSnapshots;
                if (probe == 1u)
                    --bad.maxResultBytes;
                if (probe == 2u)
                    --bad.maxCpuResultBytes;
                if (probe == 3u)
                    --bad.maxRetainedOwners;
                assert(!make(resultQuota, bad) && diagnostic);
            }
            {
                Context foreign(create);
                assert(!Storage::prepareOriginal(
                           admission, *foreign.device, budget, resultQuota, diagnostic)
                    && diagnostic);
            }
            const auto factory = [&] {
                return make(resultQuota, budget);
            };
            CpuQuotaFactoryFixture resultPressure(resultQuota);
            const auto resultPeak = resultPressure.measure(factory);
            resultPressure.verify(resultPeak, factory);
            CpuQuotaFactoryFixture recordingPressure(root->originalMetadataQuota());
            const auto recordingPeak = recordingPressure.measure(factory);
            recordingPressure.verify(recordingPeak, factory);
            auto storage = factory();
            assert(storage && !diagnostic && !storage->validate()
                && storage->snapshots().size() == 1u
                && storage->retainedOwnerCount() == budget.maxRetainedOwners
                && !storage->nativeExecutionProven());
            snapshot = storage->snapshots()[0];
            assert(!snapshot->publishedBytes() && snapshot->byteCapacity() == 768u);
            auto fake = std::shared_ptr<const Storage>(resultQuota, storage.get());
            assert(Storage::validateOriginal(fake));
            fake.reset();
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    is(storage->executionDiagnostic(),
                        "RHI-WEBGPU-READBACK-STORAGE-MAP-PUBLICATION-CLOSED");
                    return Storage::validateOriginal(storage);
                }));
            std::weak_ptr<Root> observed = root;
            storage.reset();
            admission.reset();
            p.drop();
            root.reset();
            assert(observed.expired());
            observed.reset();
            context.device.reset();
            std::printf(
                "BINDLESS_RHI_CANONICAL_RESULT_STORAGE_COLD_OK recording-peak%llu Result-peak%llu requests1 repeated-writes2 snapshot768 same-admission-root-seal exact-less forged-foreign-aliased-domains-refused hot-allocation0 native-map-publication-closed\n",
                static_cast<unsigned long long>(recordingPeak),
                static_cast<unsigned long long>(resultPeak));
        }
        for (std::size_t i = 0u; i < quotas.size(); ++i)
            assert((i == 1u || i == 10u) ? quotas[i]->liveBytes() > 0u : !quotas[i]->liveBytes());
        assert(snapshot && !snapshot->publishedBytes() && !snapshot->originalRequest()->validate()
            && snapshot->byteCapacity() == 768u);
        snapshot.reset();
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_CANONICAL_RESULT_STORAGE_DROP_OK snapshot-retains-only-original-Request-layout-and-Result no-native-root-retention all-eleven-quotas-zero map-publication-closed");
        resultLossContext_ = core::makeUnique<Context>(create);
        const char* diagnostic = nullptr;
        auto root =
            resultLossContext_->prepare(rhi::MemoryUsage::GpuToCpu, diagnostic, 2u, 768u, true);
        resultLossPrepared_ = core::makeUnique<Prepared>(*resultLossContext_, root);
        root.reset();
        const auto admission = resultLossPrepared_->command->originalAdmission();
        resultLossStorage_ = Storage::prepareOriginal(admission,
            *resultLossContext_->device,
            {1u,
                true,
                1u,
                768u,
                bytes,
                admission->retainedOwnerCount()
                    + rhi::OriginalCommandReadbackResultStorageContract::fixedAdditionalOwners
                    + 1u},
            core::makeShared<core::CpuMemoryQuota>(bytes),
            diagnostic);
        assert(resultLossStorage_ && !diagnostic);
#else
        (void)create;
#endif
    }

    template <class Factory, class MapRead> static void verify(Factory&& create, MapRead&& map)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const char* diagnostic = nullptr;
        for (unsigned probe = 0u; probe < 3u; ++probe) {
            Context context(create);
            auto root =
                context.prepare(probe ? rhi::MemoryUsage::GpuToCpu : rhi::MemoryUsage::GpuOnly,
                    diagnostic,
                    probe == 1u ? 1u : 2u,
                    probe == 2u ? 767u : 768u);
            const auto entries = inputs(*root);
            const auto limits = budget(*root);
            assert(!Joins::prepareOriginal(
                root, *context.device, entries.data(), entries.size(), limits, diagnostic));
            is(diagnostic,
                !probe            ? "RHI-ORIGINAL-READBACK-JOIN-MAP-INTENT"
                    : probe == 1u ? "RHI-WEBGPU-READBACK-JOIN-ORIGINAL-PAYLOAD-CAPACITY"
                                  : "RHI-WEBGPU-READBACK-JOIN-ORIGINAL-STAGING-CAPACITY");
        }
        {
            Context context(create);
            auto root = context.prepare(rhi::MemoryUsage::GpuToCpu, diagnostic);
            auto entries = inputs(*root);
            const auto limits = budget(*root);
            const auto make = [&] {
                return Joins::prepareOriginal(
                    root, *context.device, entries.data(), entries.size(), limits, diagnostic);
            };
            CpuQuotaFactoryFixture pressure(context.quotas[3]);
            const auto peak = pressure.measure(make);
            pressure.verify(peak, make);
            {
                core::CpuMemoryQuotaLease full(
                    context.quotas[3], bytes - context.quotas[3]->liveBytes());
                assert(full.charged());
                assert(!make());
                is(diagnostic, "RHI-WEBGPU-READBACK-JOIN-ALLOCATION");
            }
            auto joined = make();
            assert(joined && !diagnostic && !joined->validate() && joined->readbackCount() == 2u
                && joined->projectedCopyCount() == 4u && joined->sourceResourceBytes() == 20u
                && joined->stagingResourceBytes() == 768u
                && joined->retainedOwnerCount() == limits.maxRetainedOwners
                && !joined->nativeExecutionProven());
            for (unsigned limit = 0u; limit < 4u; ++limit) {
                auto bad = limits;
                if (!limit)
                    --bad.maxReadbacks;
                if (limit == 1u)
                    --bad.maxRetainedOwners;
                if (limit == 2u)
                    --bad.maxSourceResourceBytes;
                if (limit == 3u)
                    --bad.maxStagingResourceBytes;
                assert(!Joins::prepareOriginal(
                    root, *context.device, entries.data(), entries.size(), bad, diagnostic));
                is(diagnostic,
                    !limit            ? "RHI-WEBGPU-READBACK-JOIN-COUNT"
                        : limit == 1u ? "RHI-WEBGPU-READBACK-JOIN-OWNER-CAPACITY"
                                      : "RHI-WEBGPU-READBACK-JOIN-NATIVE-BYTE-CAPACITY");
            }
            std::swap(entries[0], entries[1]);
            assert(!make());
            is(diagnostic, "RHI-WEBGPU-READBACK-JOIN-ORDER-COVERAGE");
            std::swap(entries[0], entries[1]);
            assert(!Joins::prepareOriginal(
                root, *context.device, entries.data(), 1u, limits, diagnostic));
            is(diagnostic, "RHI-WEBGPU-READBACK-JOIN-ORDER-COVERAGE");
            auto source = entries[0].source;
            entries[0].source = std::shared_ptr<const rhi::OriginalCommandTextureReadbackSource>(
                context.quotas[0], source.get());
            assert(!make());
            is(diagnostic, "RHI-ORIGINAL-TEXTURE-READBACK-OWNER");
            entries[0].source = source;
            auto equal =
                rhi::OriginalCommandTextureReadbackSource::prepareOriginal(source->originalSource(),
                    source->originalDestinationLease(),
                    {1u,
                        true,
                        rhi::OriginalCommandTextureReadbackSource::completeRetainedOwnerCount,
                        2u,
                        4u,
                        768u,
                        bytes,
                        bytes},
                    context.quotas[0],
                    diagnostic);
            assert(equal);
            entries[0].source = equal;
            assert(!make());
            is(diagnostic, "RHI-WEBGPU-READBACK-JOIN-ORIGINAL-SOURCE");
            entries[0].source = source;
            equal.reset();
            {
                Context foreign(create);
                assert(!Joins::prepareOriginal(
                    root, *foreign.device, entries.data(), 2u, limits, diagnostic));
                assert(diagnostic);
            }
            auto fake = std::shared_ptr<const Joins>(context.quotas[3], joined.get());
            is(Joins::validateOriginal(fake), "RHI-WEBGPU-READBACK-JOIN-OWNER");
            fake.reset();
            auto native = rhi_webgpu::webGpuNativePayloadFor(
                *source->originalDestinationLease()->originalObject());
            assert(native);
            const auto size = native->sizeBytes;
            --native->sizeBytes;
            assert(joined->validate());
            native->sizeBytes = size;
            assert(!joined->validate());
            assert(map(native->buffer, 768u));
            is(joined->validate(), "RHI-WEBGPU-READBACK-JOIN-NATIVE-MAP-STATE");
            wgpuBufferUnmap(native->buffer);
            assert(!joined->validate());
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                context.quotas, [&]() noexcept -> const char* {
                    if (const auto* id = joined->validate())
                        return id;
                    is(joined->executionDiagnostic(),
                        "RHI-ORIGINAL-READBACK-JOIN-ORDERED-EXECUTION-MISSING");
                    if (const auto* id = root->applyOriginalPrototypePlan())
                        return id;
                    assert(joined->validate());
                    if (const auto* id = root->abandonOriginalPendingOwnership())
                        return id;
                    assert(joined->validate());
                    return nullptr;
                }));
            std::weak_ptr<Root> observed = root;
            root.reset();
            assert(!observed.expired());
            source.reset();
            entries = {};
            native.reset();
            joined.reset();
            assert(observed.expired());
            observed.reset();
            context.device.reset();
            for (const auto& q : context.quotas)
                assert(!q->liveBytes());
            std::printf(
                "BINDLESS_RHI_CANONICAL_READBACK_JOIN_OK peak%llu original-ordinals1-2 requests1 copies4 native-source20 staging768 exact-CopyDst-GpuToCpu-native-MapRead no-Result-storage exact-less-exhausted forged-equal-foreign-order-mapped-refused same-root-pending-stale caller-drop hot-allocation0 quotas8-zero execution-map-publication-closed\n",
                static_cast<unsigned long long>(peak));
        }
        {
            Context context(create);
            auto root = context.prepare(rhi::MemoryUsage::GpuToCpu, diagnostic);
            const auto entries = inputs(*root);
            auto joined = Joins::prepareOriginal(
                root, *context.device, entries.data(), 2u, budget(*root), diagnostic);
            assert(joined);
            context.device.reset();
            assert(joined->validate());
            joined.reset();
            root.reset();
        }
        lossContext_ = core::makeUnique<Context>(create);
        auto root = lossContext_->prepare(rhi::MemoryUsage::GpuToCpu, diagnostic);
        const auto entries = inputs(*root);
        lossJoin_ = Joins::prepareOriginal(
            root, *lossContext_->device, entries.data(), 2u, budget(*root), diagnostic);
        assert(lossJoin_ && !lossJoin_->validate());
#else
        (void)create;
        (void)map;
#endif
    }

    template <class Factory, class QueueWait, class MapRead>
    static void verifyOrdered(Factory&& create, QueueWait&& wait, MapRead&& map)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        for (unsigned probe = 0u; probe < 10u; ++probe) {
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 10> finalQuotas;
            {
                Context context(create);
                const char* diagnostic = nullptr;
                auto root = context.prepare(rhi::MemoryUsage::GpuToCpu, diagnostic, 2u, 768u, true);
                Prepared p(context, root);
                const auto entries = inputs(*root);
                const auto source = entries[0].source;
                auto nativeSource =
                    rhi_webgpu::webGpuNativePayloadFor(*source->originalSource()->originalObject());
                auto nativeTarget = rhi_webgpu::webGpuNativePayloadFor(
                    *source->originalDestinationLease()->originalObject());
                const auto& layout = source->originalSource()->originalRequest()->originalLayout();
                std::array<unsigned char, 20> expected;
                for (std::size_t i = 0u; i < expected.size(); ++i)
                    expected[i] = static_cast<unsigned char>(i + 1u);
                WebGpuNativeReadbackSeedFixture::seed(
                    p.queue, nativeSource->texture, *layout, expected.data(), expected.size());
                assert(wait(p.queue));
                std::uint32_t copy = 0u;
                for (std::size_t command = 0u; command < entries.size(); ++command)
                    for (const auto& mip : layout->mipCopies()) {
                        rhi::TextureToBufferCopyDesc d;
                        d.sourceTexture = source->originalSource()->resource();
                        d.destinationBuffer = source->originalDestinationLease()->resource();
                        d.extent = mip.extent;
                        d.textureSubresources = mip.range;
                        d.bufferOffsetBytes = mip.bufferOffsetBytes;
                        d.bytesPerRow = mip.bytesPerRow;
                        d.rowsPerImage = mip.rowsPerImage;
                        const auto fields = rhi::TransferCopyArguments::captureScalarFields(d);
                        assert(WebGpuCanonicalCopyPassLoweringFixture::inspectReadbackProjection(
                            p.lowering, copy, command + 1u, command, fields));
                        assert(!WebGpuCanonicalCopyPassLoweringFixture::inspectReadbackProjection(
                            p.lowering, copy, command + 2u, command, fields));
                        ++copy;
                    }
                if (!probe) {
                    assert(map(nativeTarget->buffer, 768u));
                    is(Issuers::recordOriginal(p.issuers, {1u, true}),
                        "RHI-WEBGPU-READBACK-JOIN-NATIVE-MAP-STATE");
                    wgpuBufferUnmap(nativeTarget->buffer);
                    assert(root->status() == Root::Status::Prepared && root->journalEmpty());
                }
                if (probe == 1u || probe == 2u)
                    WebGpuNativeSubmissionProbeFixture::instrumentRecording(
                        nativeSource->ownerDevice, probe == 1u);
                is(Issuers::submitOriginal(p.issuers, {1u, true}),
                    "RHI-WEBGPU-CANONICAL-SUBMISSION-UNRECORDED");
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    p.quotas, [&]() noexcept -> const char* {
                        assert(!Issuers::recordOriginal(p.issuers, {1u, true}));
                        if (probe == 1u || probe == 2u)
                            WebGpuNativeSubmissionProbeFixture::restoreRecording();
                        assert(root->status() == Root::Status::Pending && !root->journalEmpty());
                        assert(p.joins->validate() && !p.issuers->hasOriginalCompletedSubmission());
                        is(root->commitOriginalPendingOwnership(),
                            "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
                        assert(WebGpuAsyncWaitFixture::until([&] {
                            return WebGpuCanonicalScopedRecordingFixture::
                                originalRecordingSlotsDrained(*p.issuers);
                        }));
                        if (probe == 2u) {
                            assert(!p.issuers->hasOriginalRecordedCommand()
                                && p.issuers->recordingDiagnostic()
                                && root->status() == Root::Status::Abandoned
                                && root->journalEmpty());
                            return nullptr;
                        }
                        assert(p.issuers->hasOriginalRecordedCommand());
                        if (probe == 3u) {
                            assert(!p.custody->cancelOriginal());
                            assert(root->status() == Root::Status::Abandoned);
                            return nullptr;
                        }
                        using NativeProbe = WebGpuNativeSubmissionProbeFixture;
                        const auto kind = probe == 4u ? NativeProbe::Probe::ThrowBefore
                            : probe == 5u             ? NativeProbe::Probe::ThrowAfter
                            : probe == 6u             ? NativeProbe::Probe::Reverse
                            : probe == 7u             ? NativeProbe::Probe::PartialScope
                                                      : NativeProbe::Probe::Success;
                        if (probe >= 4u)
                            NativeProbe::instrument(
                                nativeSource->ownerDevice, p.queue, kind, &p, &observeMaySubmit);
                        const auto* submitted = Issuers::submitOriginal(p.issuers, {1u, true});
                        if (probe == 4u || probe == 5u)
                            is(submitted, "RHI-WEBGPU-CANONICAL-SUBMISSION-INVOCATION-UNCERTAIN");
                        else if (probe == 7u)
                            is(submitted, "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
                        else
                            assert(!submitted);
                        if (probe >= 4u)
                            NativeProbe::restore();
                        if (probe == 8u)
                            --nativeTarget->sizeBytes;
                        if (probe == 9u)
                            context.device.reset();
                        assert(WebGpuAsyncWaitFixture::until([&] {
                            return WebGpuCanonicalSubmissionFixture::originalSubmissionSlotsDrained(
                                *p.issuers);
                        }));
                        const bool success = probe == 0u || probe == 1u || probe == 6u;
                        assert(p.issuers->hasOriginalCompletedSubmission() == success);
                        assert(root->journalEmpty()
                            && root->status()
                                == (success ? Root::Status::Committed : Root::Status::Invalidated));
                        return nullptr;
                    }));
                if (probe == 0u || probe == 1u || probe == 6u) {
                    assert(wait(p.queue) && map(nativeTarget->buffer, 768u));
                    const auto* data = static_cast<const unsigned char*>(
                        wgpuBufferGetConstMappedRange(nativeTarget->buffer, 0u, 768u));
                    WebGpuNativeReadbackSeedFixture::verify(
                        data, 768u, *layout, expected.data(), expected.size());
                    wgpuBufferUnmap(nativeTarget->buffer);
                }
                finalQuotas = p.quotas;
                p.drop();
                root.reset();
                nativeSource.reset();
                nativeTarget.reset();
                context.device.reset();
            }
            for (const auto& quota : finalQuotas)
                assert(!quota->liveBytes());
            std::printf(
                "BINDLESS_RHI_CANONICAL_ORDERED_READBACK_PROBE_OK probe%u same-root-seal-journal original-ordinals1-2 copies4 genuine-scopes-queue no-early-commit hot-allocation0 quotas10-zero native-map-publication-gates-closed\n",
                probe);
        }
#else
        (void)create;
        (void)wait;
        (void)map;
#endif
    }

    template <class Factory> static void prepareOrderedLoss(Factory&& create)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        for (unsigned probe = 0u; probe < orderedLoss_.size(); ++probe) {
            auto& context = orderedLossContexts_[probe];
            context = core::makeUnique<Context>(create);
            const char* diagnostic = nullptr;
            auto root = context->prepare(rhi::MemoryUsage::GpuToCpu, diagnostic, 2u, 768u, true);
            auto& p = orderedLoss_[probe];
            p = core::makeUnique<Prepared>(*context, root);
            root.reset();
            if (probe >= 2u) {
                assert(!Issuers::recordOriginal(p->issuers, {1u, true}));
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return p->issuers->hasOriginalRecordedCommand();
                }));
            }
            if (probe == 3u) {
                const auto native =
                    p->command->originalAdmission()->originalClosure()->nativeDevice();
                WebGpuNativeSubmissionProbeFixture::instrumentLoss(native, p->queue, true);
                is(Issuers::submitOriginal(p->issuers, {1u, true}),
                    "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
                WebGpuNativeSubmissionProbeFixture::restoreLoss();
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return WebGpuCanonicalSubmissionFixture::originalSubmissionSlotsDrained(
                        *p->issuers);
                }));
                assert(p->root->status() == Root::Status::Invalidated && p->root->journalEmpty()
                    && p->custody->pendingOriginalRecords() == 1u
                    && !p->issuers->hasOriginalCompletedSubmission());
            }
        }
#else
        (void)create;
#endif
    }

    static void beginMapExecutionLoss()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const auto& p = *executionLoss_[3u];
        // Prepared early: do not yield here and advance unrelated legacy maps
        // whose genuine physical-loss fixture requires Pending native work.
        assert(p.root->status() == Root::Status::Pending && !p.root->journalEmpty()
            && p.custody->pendingOriginalRecords() == 1u);
#endif
    }

    static void verifyMapExecutionLoss()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        WebGpuNativeMapExecutionProbeFixture::retirePendingAfterLoss();
        assert(publicationSurvivor_ && publicationSurvivor_->publishedBytes()
            && publicationSurvivor_->nativeExecutionProven());
        WebGpuNativeReadbackSeedFixture::verify(publicationSurvivor_->publishedBytes(),
            768u,
            *publicationSurvivor_->originalLayout(),
            publicationSurvivorBytes_.data(),
            publicationSurvivorBytes_.size());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            publicationSurvivorQuotas_, []() noexcept -> const char* {
                publicationSurvivor_.reset();
                return nullptr;
            }));
        for (const auto& quota : publicationSurvivorQuotas_)
            assert(!quota->liveBytes());
        publicationSurvivorQuotas_ = {};
        std::puts(
            "BINDLESS_RHI_CANONICAL_SNAPSHOT_SURVIVOR_LOSS_OK published-CPU-snapshot immutable-after-original-native-owner-drop physical-device-loss exact-bytes-preserved quotas11-zero");
        for (unsigned probe = 0u; probe < executionLoss_.size(); ++probe) {
            auto& p = *executionLoss_[probe];
            assert(WebGpuAsyncWaitFixture::until([&] {
                return !p.custody->pendingOriginalRecords();
            }));
            assert(p.custody->originalNativeLossDelivered()
                && !p.issuers->hasOriginalCompletedSubmission()
                && p.root->status() == Root::Status::Invalidated && p.root->journalEmpty()
                && !p.results->snapshots()[0]->publishedBytes());
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 11> quotas;
            std::copy(p.quotas.begin(), p.quotas.end(), quotas.begin());
            quotas[10] = p.resultQuota;
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    p.drop();
                    executionLossContexts_[probe]->device.reset();
                    return nullptr;
                }));
            executionLoss_[probe].reset();
            executionLossContexts_[probe].reset();
            for (const auto& quota : quotas)
                assert(!quota->liveBytes());
            std::printf(
                "BINDLESS_RHI_CANONICAL_MAP_EXECUTION_LOSS_OK probe%u actual-physical-loss uncertain-or-Pending-C-map same-State-original-root invalidated safe-native-teardown userdata-drained unpublished core-hot-allocation0 quotas11-zero\n",
                probe);
        }
#endif
    }

    static void beginOrderedLoss()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        {
            const auto entry = inputs(*nativeMapLossRoot_)[0];
            const auto target = rhi_webgpu::webGpuNativePayloadFor(
                *entry.source->originalDestinationLease()->originalObject());
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                nativeMapLossContext_->quotas, [&]() noexcept -> const char* {
                    assert(
                        rhi_webgpu::WebGpuNativeCompletionReceipt::pushScopes(target->ownerDevice));
                    assert(!nativeMapLossProbe_->map.start());
                    for (const auto receipt : nativeMapLossProbe_->receipts)
                        assert(rhi_webgpu::WebGpuNativeCompletionReceipt::bind(receipt));
                    return nullptr;
                }));
        }
        for (unsigned probe = 1u; probe < 3u; ++probe) {
            auto& p = *orderedLoss_[probe];
            const auto native = p.command->originalAdmission()->originalClosure()->nativeDevice();
            WebGpuNativeSubmissionProbeFixture::instrumentLoss(native, p.queue);
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                p.quotas, [&]() noexcept -> const char* {
                    return probe == 1u ? Issuers::recordOriginal(p.issuers, {1u, true})
                                       : Issuers::submitOriginal(p.issuers, {1u, true});
                }));
            WebGpuNativeSubmissionProbeFixture::restoreLoss();
            assert(p.root->status() == Root::Status::Pending && !p.root->journalEmpty()
                && !p.issuers->hasOriginalCompletedSubmission());
        }
#endif
    }

    static void beginOrderedScopeUncertainty()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        orderedScopeUncertain_ = true;
        auto& p = *orderedLoss_[4u];
        const auto native = p.command->originalAdmission()->originalClosure()->nativeDevice();
        WebGpuNativeSubmissionProbeFixture::instrumentLoss(native, p.queue, false, true);
        assert(
            !CpuQuotaFactoryFixture::verifyHotNoAllocation(p.quotas, [&]() noexcept -> const char* {
                is(Issuers::submitOriginal(p.issuers, {1u, true}),
                    "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
                return nullptr;
            }));
        WebGpuNativeSubmissionProbeFixture::restoreLoss();
        assert(p.root->status() == Root::Status::Invalidated && p.root->journalEmpty()
            && p.custody->pendingOriginalRecords() == 1u
            && !p.issuers->hasOriginalCompletedSubmission());
#endif
    }

    static void verifyOrderedLoss()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        for (unsigned probe = 0u; probe < orderedLoss_.size(); ++probe) {
            auto& owner = orderedLoss_[probe];
            auto& p = *owner;
            assert(WebGpuAsyncWaitFixture::until([&] {
                return !p.custody->pendingOriginalRecords();
            }));
            assert(p.root->status() == Root::Status::Invalidated && p.root->journalEmpty()
                && !p.issuers->hasOriginalCompletedSubmission());
            const auto outstanding = probe == 1u
                ? WebGpuCanonicalScopedRecordingFixture::originalRecordingScopesAtLoss(*p.issuers)
                : WebGpuCanonicalSubmissionFixture::originalSubmissionSlotsAtLoss(*p.issuers);
            assert(outstanding
                == (probe == 1u                                 ? 3u
                        : probe == 2u                           ? 4u
                        : probe == 4u && orderedScopeUncertain_ ? 3u
                                                                : 0u));
            const auto quotas = p.quotas;
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    p.drop();
                    orderedLossContexts_[probe]->device.reset();
                    return nullptr;
                }));
            owner.reset();
            orderedLossContexts_[probe].reset();
            for (const auto& quota : quotas)
                assert(!quota->liveBytes());
            std::printf(
                "BINDLESS_RHI_CANONICAL_ORDERED_READBACK_LOSS_OK probe%u outstanding%u actual-physical-loss same-root-invalidated no-completed-map-publication genuine-drain shell-retire hot-allocation0 quotas10-zero\n",
                probe,
                outstanding);
        }
#endif
    }

    static void verifyActualLoss()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        assert(rhi_webgpu::webGpuNativeDeviceLost() && nativeMapLossRoot_->validate());
        assert(WebGpuAsyncWaitFixture::until([] {
            return nativeMapLossProbe_->evidence.allReceived();
        }));
        const auto nativeMapQuotas = nativeMapLossContext_->quotas;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            nativeMapQuotas, [&]() noexcept -> const char* {
                assert(!nativeMapLossProbe_->map.abortAndDrain()
                    && nativeMapLossProbe_->map.nativeUnmapProven());
                nativeMapLossProbe_.reset();
                nativeMapLossRoot_.reset();
                nativeMapLossContext_->device.reset();
                return nullptr;
            }));
        nativeMapLossContext_.reset();
        for (const auto& quota : nativeMapQuotas)
            assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_NATIVE_MAP_RECEIPT_LOSS_OK genuine-physical-loss original-root-refused actual-C-map-scopes-drained safe-unmapped core-hot-allocation0 quotas8-zero canonical-publication-closed");
        assert(WebGpuAsyncWaitFixture::until([] {
            return !mapLossPrepared_->custody->pendingOriginalRecords();
        }));
        assert(mapLossPrepared_->custody->originalNativeLossDelivered()
            && mapLossPrepared_->root->status() == Root::Status::Invalidated
            && mapLossPrepared_->root->journalEmpty()
            && !mapLossPrepared_->results->snapshots()[0]->publishedBytes());
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 11> mapQuotas;
        std::copy(
            mapLossPrepared_->quotas.begin(), mapLossPrepared_->quotas.end(), mapQuotas.begin());
        mapQuotas[10] = mapLossPrepared_->resultQuota;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            mapQuotas, [&]() noexcept -> const char* {
                mapLossPrepared_->drop();
                mapLossContext_->device.reset();
                return nullptr;
            }));
        mapLossPrepared_.reset();
        mapLossContext_.reset();
        for (const auto& quota : mapQuotas)
            assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_CANONICAL_MAP_COLD_LOSS_OK slots12 actual-physical-loss same-root-invalidated independent-Result-unpublished hot-allocation0 quotas11-zero");
        assert(resultLossStorage_ && resultLossStorage_->validate()
            && !resultLossStorage_->snapshots()[0]->publishedBytes());
        const auto resultQuota = resultLossStorage_->originalResultQuota();
        const auto originalQuotas = resultLossPrepared_->quotas;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 11> resultQuotas;
        std::copy(originalQuotas.begin(), originalQuotas.end(), resultQuotas.begin());
        resultQuotas[10] = resultQuota;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            resultQuotas, [&]() noexcept -> const char* {
                resultLossStorage_.reset();
                resultLossPrepared_->drop();
                resultLossContext_->device.reset();
                return nullptr;
            }));
        resultLossPrepared_.reset();
        resultLossContext_.reset();
        for (const auto& quota : resultQuotas)
            assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_CANONICAL_RESULT_STORAGE_LOSS_OK genuine-physical-loss same-cold-root-refused no-published-bytes hot-allocation0 all-eleven-quotas-zero");
        assert(lossJoin_ && lossJoin_->validate() && !lossJoin_->nativeExecutionProven());
        const auto quotas = lossContext_->quotas;
        assert(
            !CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept -> const char* {
                lossJoin_.reset();
                lossContext_->device.reset();
                lossContext_.reset();
                return nullptr;
            }));
        for (const auto& q : quotas)
            assert(!q->liveBytes());
        std::puts(
            "BINDLESS_RHI_CANONICAL_READBACK_JOIN_LOSS_OK genuine-physical-loss cold-same-root no-completion-map-publication all-eight-quotas-zero hot-allocation0");
#endif
    }
};
} // namespace arp::test
