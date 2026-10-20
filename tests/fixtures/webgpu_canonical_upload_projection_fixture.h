#pragma once
#include "rhi/backends/webgpu/webgpu_canonical_issuer_preparation.h"
#include "webgpu_canonical_copy_pass_lowering_fixture.h"
#include "webgpu_canonical_submission_fixture.h"
#include "webgpu_canonical_upload_cold_fixture.h"
#include "webgpu_native_submission_probe_fixture.h"

namespace arp::test {
class WebGpuCanonicalUploadProjectionFixture final {
    using Root = rhi::OriginalCommandStateTransaction;
    using Joins = rhi_webgpu::WebGpuCanonicalUploadJoins;
    using Admission = rhi_webgpu::WebGpuCanonicalColdAdmission;
    using Lowering = rhi_webgpu::WebGpuCanonicalCopyPassLowering;
    using Command = rhi_webgpu::WebGpuCanonicalColdCommand;
    using Custody = rhi_webgpu::WebGpuCanonicalCallbackCustody;
    using Issuers = rhi_webgpu::WebGpuCanonicalIssuerPreparation;
    static constexpr std::uint64_t bytes = 65536u;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "upload projection actual=%s expected=%s\n",
                actual ? actual : "<none>",
                expected);
        assert(actual && std::string_view(actual) == expected);
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
public:
    struct SubmissionObservation {
        const Root* root;
        const std::shared_ptr<const Issuers>* issuers;
    };

    static void observeMaySubmit(void* data) noexcept
    {
        const auto& observed = *static_cast<const SubmissionObservation*>(data);
        assert(observed.root->status() == Root::Status::Pending && !observed.root->journalEmpty());
        assert(!(*observed.issuers)->hasOriginalCompletedSubmission());
        is(Issuers::submitOriginal(*observed.issuers, {1u, true}),
            "RHI-WEBGPU-CANONICAL-SUBMISSION-ONE-SHOT");
    }

    class Prepared final {
    public:
        Prepared(std::shared_ptr<Root> root,
            rhi::GpuDevice& device,
            Joins::Source source,
            const rhi::OriginalDeviceNativeLossObservation& loss,
            bool texture)
            : Prepared(root,
                  device,
                  {{{1u, source}, {2u, source}}},
                  loss,
                  texture,
                  2u,
                  texture ? 788u : 32u,
                  WebGpuCanonicalUploadJoinsFixture::budget(*root, texture),
                  5u,
                  0u)
        {}

        Prepared(std::shared_ptr<Root> root,
            rhi::GpuDevice& device,
            const std::array<Joins::Input, 2>& entries,
            const rhi::OriginalDeviceNativeLossObservation& loss,
            bool texture,
            std::uint32_t resources,
            std::uint64_t nativeBytes,
            const Joins::Budget& joinBudget,
            std::uint32_t commands,
            std::uint32_t copies)
        {
            const char* diagnostic = nullptr;
            if (resources > 2u) {
                const auto make = [&] {
                    return Joins::prepareOriginal(
                        root, device, entries.data(), entries.size(), joinBudget, diagnostic);
                };
                CpuQuotaFactoryFixture pressure(root->originalMetadataQuota());
                const auto peak = pressure.measure(make);
                pressure.verify(peak, make);
                for (unsigned limit = 0u; limit < 5u; ++limit) {
                    auto bad = joinBudget;
                    if (!limit)
                        --bad.maxNativeSources;
                    if (limit == 1u)
                        --bad.maxStagingResourceBytes;
                    if (limit == 2u)
                        --bad.maxDestinationResourceBytes;
                    if (limit == 3u)
                        --bad.maxRetainedOwners;
                    if (limit == 4u)
                        --bad.maxUploads;
                    assert(!Joins::prepareOriginal(
                               root, device, entries.data(), entries.size(), bad, diagnostic)
                        && diagnostic);
                    assert(root->status() == Root::Status::Prepared && root->journalEmpty());
                }
            }
            auto joins = Joins::prepareOriginal(
                root, device, entries.data(), entries.size(), joinBudget, diagnostic);
            assert(joins);
            auto closure = rhi_webgpu::WebGpuCanonicalNativeClosure::prepareOriginalLossBound(root,
                device,
                {1u,
                    true,
                    resources,
                    resources,
                    root->retainedOwnerCount() + 2u + resources * 3u,
                    nativeBytes,
                    bytes},
                loss,
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
            auto b = WebGpuCanonicalUploadColdFixture::budget(texture, ownerLimit);
            if (texture) {
                b.maxTextureUploadMips = b.maxNativeCopyCommands = 0u;
                b.maxPackedTextureUploadBytes = b.maxTotalUploadBytes = 0u;
                b.maxTextureUploadPixelBytes = b.maxTextureWriteTexelBytes = 0u;
                for (const auto& entry : entries) {
                    const auto& content = std::get<std::shared_ptr<
                        const rhi_webgpu::WebGpuCanonicalTextureContentUploadSource>>(entry.source);
                    const auto& packing = content->originalStaging()->originalPackingSource();
                    b.maxTextureUploadMips += packing->packing()->mipLayouts().size();
                    b.maxNativeCopyCommands += packing->packing()->mipLayouts().size();
                    b.maxPackedTextureUploadBytes += packing->packing()->packedBytes().size();
                    b.maxTextureUploadPixelBytes += packing->originalInput()->pixelBytes().size();
                    b.maxTextureWriteTexelBytes += packing->originalDestinationLease()
                                                       ->originalObject()
                                                       ->allocation()
                                                       .estimatedSizeBytes;
                }
                b.maxTotalUploadBytes = b.maxPackedTextureUploadBytes;
            }
            b.maxOriginalCommands = commands;
            b.maxCopyElements = copies;
            b.maxNativeCopyCommands += copies;
            Admission::Requirements required;
            assert(!Admission::deriveRequirements(root, b, required));
            auto submission = core::makeShared<core::CpuMemoryQuota>(bytes),
                 callback = core::makeShared<core::CpuMemoryQuota>(bytes);
            auto seal = Seal::prepareOriginal(root,
                device,
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
            auto admission = Admission::prepareOriginal(closure, reservation, b, diagnostic, joins);
            assert(admission);
            command =
                Command::prepareOriginal(admission, {1u, true, 1u, ownerLimit, bytes}, diagnostic);
            assert(command);
            custody = Custody::prepareOriginal(command,
                {1u, true, 1u, required.capacity.callbackTickets, ownerLimit, bytes, bytes},
                diagnostic);
            assert(custody);
            lowerBudget = {3u,
                true,
                copies,
                0u,
                command->retainedOwnerCount()
                    + rhi::OriginalCommandCallbackCustodyContract::additionalOwnerCount
                    + rhi::OriginalCommandCopyPassLoweringContract::additionalOwnerCount,
                bytes,
                0u};
            lowerBudget.maxUploadCopyOperations =
                static_cast<std::uint32_t>(b.maxNativeCopyCommands) - copies;
            lowerBudget.maxBufferUploadCommands = b.maxBufferUploads;
            lowerBudget.maxTextureUploadMips = b.maxTextureUploadMips;
            lowerBudget.maxBufferUploadDeclaredBytes = b.maxBufferUploadBytes;
            lowerBudget.maxTextureUploadPixelBytes = b.maxTextureUploadPixelBytes;
            lowerBudget.uploadStaging = {1u,
                true,
                b.maxBufferUploadBytes,
                b.maxPackedTextureUploadBytes,
                b.maxTotalUploadBytes};
            lowerBudget.uploadWork = {1u,
                true,
                b.maxTextureUploads,
                b.maxNativeCopyCommands,
                b.maxTextureWriteTexelBytes};
            quotas = {root->originalMetadataQuota(), submission, callback};
        }

        void drop() noexcept
        {
            custody.reset();
            command.reset();
        }

        std::shared_ptr<const Command> command;
        std::shared_ptr<const Custody> custody;
        Lowering::Budget lowerBudget;
        std::uint32_t ownerLimit = 0u;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> quotas;
    };

    class LossExecution final {
    public:
        LossExecution(std::shared_ptr<Root> original,
            rhi::GpuDevice& device,
            Joins::Source source,
            const rhi::OriginalDeviceNativeLossObservation& loss,
            bool texture)
            : root(std::move(original)), prepared(root, device, source, loss, texture)
        {
            const char* diagnostic = nullptr;
            lowering =
                Lowering::prepareOriginal(prepared.custody, prepared.lowerBudget, diagnostic);
            assert(lowering);
            issuers = Issuers::prepareOriginal(
                lowering, {1u, true, 1u, 1u, prepared.ownerLimit, bytes, bytes}, diagnostic);
            assert(issuers);
            nativeDevice = static_cast<WGPUDevice>(
                prepared.command->originalAdmission()->originalClosure()->nativeDevice());
            queue = wgpuDeviceGetQueue(nativeDevice);
            assert(queue);
        }

        ~LossExecution() { wgpuQueueRelease(queue); }

        void drop()
        {
            issuers.reset();
            lowering.reset();
            prepared.drop();
            root.reset();
        }

        std::shared_ptr<Root> root;
        Prepared prepared;
        std::shared_ptr<const Lowering> lowering;
        std::shared_ptr<const Issuers> issuers;
        WGPUDevice nativeDevice = nullptr;
        WGPUQueue queue = nullptr;
    };

    static inline std::array<std::array<core::UniquePtr<LossExecution>, 5>, 2> lossExecutions_;

    static void assertNoCompletedContent(const Root& root, bool texture)
    {
        if (!texture)
            return;
        const auto& inventory = root.originalGraph()
                                    ->originalInputs()
                                    ->originalScalars()
                                    ->originalUsage()
                                    ->originalSources()
                                    ->sources();
        const auto& target =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(inventory[0u]);
        const auto content = target->originalObject()->observeOriginalContent();
        assert(!content
            || content->state() != rhi::common::OriginalTextureContentControl::State::Completed);
    }
#endif
public:
    static void prepareLossExecution(std::shared_ptr<Root>& root,
        rhi::GpuDevice& device,
        Joins::Source source,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        bool texture,
        unsigned probe)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        auto& live = lossExecutions_[texture ? 1u : 0u][probe];
        assert(!live);
        live = core::makeUnique<LossExecution>(root, device, source, loss, texture);
        root.reset();
        assertNoCompletedContent(*live->root, texture);
        if (probe >= 2u) {
            assert(!Issuers::recordOriginal(live->issuers, {1u, true}));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return live->issuers->hasOriginalRecordedCommand();
            }));
        }
        if (probe == 3u) {
            WebGpuNativeSubmissionProbeFixture::instrumentLoss(
                live->nativeDevice, live->queue, true);
            is(Issuers::submitOriginal(live->issuers, {1u, true}),
                "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
            WebGpuNativeSubmissionProbeFixture::restoreLoss();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return WebGpuCanonicalSubmissionFixture::originalSubmissionSlotsDrained(
                    *live->issuers);
            }));
            assert(live->root->status() == Root::Status::Invalidated && live->root->journalEmpty()
                && live->prepared.custody->pendingOriginalRecords() == 1u
                && !live->issuers->hasOriginalCompletedSubmission());
            assertNoCompletedContent(*live->root, texture);
            std::printf(
                "BINDLESS_RHI_CANONICAL_%s_UPLOAD_QUEUE_UNCERTAIN_RETAINED real-native-listeners-suppressed scopes-drained shell-retained requires-physical-loss\n",
                texture ? "TEXTURE" : "BUFFER");
        }
#else
        (void)root;
        (void)device;
        (void)source;
        (void)loss;
        (void)texture;
        (void)probe;
#endif
    }

    static void beginUploadLoss(bool texture)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        for (unsigned probe = 1u; probe < 3u; ++probe) {
            auto& live = *lossExecutions_[texture ? 1u : 0u][probe];
            WebGpuNativeSubmissionProbeFixture::instrumentLoss(live.nativeDevice, live.queue);
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                live.prepared.quotas, [&]() noexcept -> const char* {
                    assert(!(probe == 1u ? Issuers::recordOriginal(live.issuers, {1u, true})
                                         : Issuers::submitOriginal(live.issuers, {1u, true})));
                    return nullptr;
                }));
            WebGpuNativeSubmissionProbeFixture::restoreLoss();
            assert(live.root->status() == Root::Status::Pending && !live.root->journalEmpty()
                && !live.issuers->hasOriginalCompletedSubmission());
            assertNoCompletedContent(*live.root, texture);
        }
#else
        (void)texture;
#endif
    }

    static unsigned scopeProbeMode() noexcept
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        return EM_ASM_INT({
            // clang-format off
                return Module.canonicalUploadScopeUncertainty === "buffer"    ? 1
                       : Module.canonicalUploadScopeUncertainty === "texture" ? 2
                                                                              : 0;
            // clang-format on
        });
#else
        return 0u;
#endif
    }

    static void beginUploadScopeUncertainty()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const auto mode = scopeProbeMode();
        assert(mode == 1u || mode == 2u);
        auto& live = *lossExecutions_[mode - 1u][4u];
        WebGpuNativeSubmissionProbeFixture::instrumentLoss(
            live.nativeDevice, live.queue, false, true);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            live.prepared.quotas, [&]() noexcept -> const char* {
                is(Issuers::submitOriginal(live.issuers, {1u, true}),
                    "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
                WebGpuNativeSubmissionProbeFixture::restoreLoss();
                return nullptr;
            }));
        assert(live.root->status() == Root::Status::Invalidated && live.root->journalEmpty()
            && live.prepared.custody->pendingOriginalRecords() == 1u
            && !live.issuers->hasOriginalCompletedSubmission());
        assertNoCompletedContent(*live.root, mode == 2u);
        std::printf(
            "BINDLESS_RHI_CANONICAL_%s_UPLOAD_SCOPE_UNCERTAIN_RETAINED genuine-pop-then-throw userdata-suppressed subsequent-recording-refused requires-physical-replacement\n",
            mode == 2u ? "TEXTURE" : "BUFFER");
#endif
    }

    static void verifyUploadLoss(bool texture)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        for (unsigned probe = 0u; probe < 5u; ++probe) {
            auto& owner = lossExecutions_[texture ? 1u : 0u][probe];
            auto& live = *owner;
            assert(WebGpuAsyncWaitFixture::until([&] {
                return !live.prepared.custody->pendingOriginalRecords();
            }));
            assert(live.root->status() == Root::Status::Invalidated && live.root->journalEmpty()
                && !live.issuers->hasOriginalCompletedSubmission());
            assertNoCompletedContent(*live.root, texture);
            const auto outstanding = probe == 1u
                ? WebGpuCanonicalScopedRecordingFixture::originalRecordingScopesAtLoss(
                      *live.issuers)
                : WebGpuCanonicalSubmissionFixture::originalSubmissionSlotsAtLoss(*live.issuers);
            if (probe == 1u)
                assert(outstanding == 3u);
            if (probe == 2u)
                assert(outstanding == 4u);
            if (probe == 4u)
                assert(outstanding == (scopeProbeMode() == (texture ? 2u : 1u) ? 3u : 0u));
            const auto quotas = live.prepared.quotas;
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    live.drop();
                    return nullptr;
                }));
            owner.reset();
            for (const auto& quota : quotas)
                assert(!quota->liveBytes());
            std::printf(
                "BINDLESS_RHI_CANONICAL_%s_UPLOAD_EXECUTION_LOSS_OK probe%u actual-physical-loss outstanding%u root-invalidated no-completed genuine-callback-drain shell-retire hot-allocation0\n",
                texture ? "TEXTURE" : "BUFFER",
                probe,
                outstanding);
        }
#else
        (void)texture;
#endif
    }

    template <class Proof, class QueueWait, class MapRead>
    static void verifyInterleaved(std::shared_ptr<Root>& root,
        rhi::GpuDevice& device,
        const std::array<std::shared_ptr<const Proof>, 2>& proofs,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& copied,
        QueueWait&& wait,
        MapRead&& map)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        constexpr bool texture =
            std::is_same_v<Proof, rhi_webgpu::WebGpuCanonicalTextureContentUploadSource>;
        const std::size_t last = texture ? 3u : 9u;
        const auto closure = [] {
            if constexpr (texture)
                return Proof::retainedOwnerBound;
            else
                return rhi::OriginalCommandInitializedBufferStagingContract::retainedOwnerBound;
        }();
        const auto stagingBytes = texture ? 1536u : 32u, targetBytes = texture ? 20u : 16u;
        const Joins::Budget joinBudget{1u,
            true,
            2u,
            2u,
            1u,
            root->retainedOwnerCount() + 1u + 2u * closure,
            stagingBytes,
            targetBytes,
            bytes};
        const std::array<Joins::Input, 2> entries{{{1u, proofs[0]}, {last, proofs[1]}}};
        const char* diagnostic = nullptr;
        // Unique staging, target and checkpoint (or independent copy pair).
        Prepared prepared(root,
            device,
            entries,
            loss,
            texture,
            texture ? 5u : 4u,
            stagingBytes + targetBytes + (texture ? 32u : 16u),
            joinBudget,
            texture ? 6u : 12u,
            1u);
        auto bad = prepared.lowerBudget;
        --bad.maxCopyOperations;
        assert(!Lowering::prepareOriginal(prepared.custody, bad, diagnostic) && diagnostic);
        auto lowering =
            Lowering::prepareOriginal(prepared.custody, prepared.lowerBudget, diagnostic);
        if (!lowering)
            std::fprintf(stderr, "interleaved lowering %s\n", diagnostic);
        assert(lowering);
        assert(lowering->projectedCopyCount() == 1u
            && lowering->projectedUploadCopyCount() == (texture ? 4u : 2u));
        const auto& inputs = root->originalGraph()->originalInputs();
        assert(WebGpuCanonicalCopyPassLoweringFixture::inspectCopyProjection(lowering,
            0u,
            std::get<rhi::TransferCopyArguments>(
                inputs->descriptors()[texture ? 1u : 2u].arguments)));
        for (std::size_t join = 0u; join < proofs.size(); ++join) {
            if constexpr (texture) {
                for (std::size_t mip = 0u; mip < 2u; ++mip) {
                    rhi::semantics::TextureUploadPackedCopy mapped;
                    assert(!rhi_webgpu::WebGpuCanonicalTextureUploadSource::mapPackedCopyOriginal(
                        proofs[join]->originalStaging(),
                        proofs[join]->originalStaging()->originalPackingSource(),
                        mip,
                        mapped));
                    rhi::BufferToTextureCopyDesc copy;
                    copy.sourceBuffer = mapped.sourceBuffer;
                    copy.destinationTexture = mapped.destinationTexture;
                    copy.bufferOffsetBytes = mapped.bufferOffsetBytes;
                    copy.bytesPerRow = mapped.bytesPerRow;
                    copy.rowsPerImage = mapped.rowsPerImage;
                    copy.textureSubresources = mapped.subresources;
                    copy.extent = mapped.extent;
                    const auto fields = rhi::TransferCopyArguments::captureScalarFields(copy);
                    assert(WebGpuCanonicalCopyPassLoweringFixture::inspectUploadProjection(
                        lowering, join * 2u + mip, join ? last : 1u, join, fields));
                    assert(!WebGpuCanonicalCopyPassLoweringFixture::inspectUploadProjection(
                        lowering, join * 2u + mip, join ? last - 1u : 2u, join, fields));
                }
            } else {
                const auto& fields = std::get<rhi::BufferUploadArguments>(
                    inputs->descriptors()[join ? 4u : 0u].arguments)
                                         .copyArguments();
                assert(WebGpuCanonicalCopyPassLoweringFixture::inspectUploadProjection(
                    lowering, join, join ? last : 1u, join, fields));
                assert(!WebGpuCanonicalCopyPassLoweringFixture::inspectUploadProjection(
                    lowering, join, join ? last - 1u : 2u, join, fields));
            }
        }
        auto issuers = Issuers::prepareOriginal(
            lowering, {1u, true, 1u, 1u, prepared.ownerLimit, bytes, bytes}, diagnostic);
        assert(issuers);
        const auto lease = [&](const auto& p) {
            if constexpr (texture)
                return p->originalStaging()->initializedBuffer()->originalLease();
            else
                return p->originalLease();
        };
        const auto a = lease(proofs[0])->originalObject()->observeOriginalCommittedState();
        const auto b = lease(proofs[1])->originalObject()->observeOriginalCommittedState();
        assert(a && b);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            prepared.quotas, [&]() noexcept -> const char* {
                assert(!Issuers::recordOriginal(issuers, {1u, true}));
                assert(root->status() == Root::Status::Pending);
                assert(!proofs[0]->nativeExecutionProven() && !proofs[1]->nativeExecutionProven());
                if constexpr (texture)
                    assert(root->plannedContentGeneration(1u) == 1u
                        && root->plannedContentGeneration(last) == 2u);
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return issuers->hasOriginalRecordedCommand()
                        || root->status() != Root::Status::Pending;
                }));
                assert(issuers->hasOriginalRecordedCommand());
                assert(!Issuers::submitOriginal(issuers, {1u, true}));
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return !prepared.custody->pendingOriginalRecords();
                }));
                assert(issuers->hasOriginalCompletedSubmission()
                    && root->status() == Root::Status::Committed && root->journalEmpty());
                assert(!lease(proofs[0])->originalObject()->validateOriginalCommittedState(*a)
                    && !lease(proofs[1])->originalObject()->validateOriginalCommittedState(*b));
                return nullptr;
            }));
        const auto& inventory = root->originalGraph()
                                    ->originalInputs()
                                    ->originalScalars()
                                    ->originalUsage()
                                    ->originalSources()
                                    ->sources();
        auto target = [&] {
            if constexpr (texture)
                return rhi_webgpu::webGpuNativePayloadFor(*proofs[1]
                        ->originalStaging()
                        ->originalPackingSource()
                        ->originalDestinationLease()
                        ->originalObject());
            else
                return rhi_webgpu::webGpuNativePayloadFor(
                    *std::get<std::shared_ptr<const rhi::OriginalCommandBufferUploadSource>>(
                        inventory[5u])
                        ->originalDestinationLease()
                        ->originalObject());
        }();
        assert(target);
        if constexpr (texture) {
            const auto completed = proofs[1]
                                       ->originalStaging()
                                       ->originalPackingSource()
                                       ->originalDestinationLease()
                                       ->originalObject()
                                       ->observeOriginalContent();
            assert(completed
                && completed->state()
                    == rhi::common::OriginalTextureContentControl::State::Completed
                && completed->contentGeneration() == 2u);
            assert(target->contentAccessPolicy().ordinaryAccessDiagnostic());
        }
        verifyNativeOutput(proofs[1], target, targetBytes, 64u, wait, map);
        auto checkpoint = rhi_webgpu::webGpuNativePayloadFor(*copied->originalObject());
        assert(checkpoint);
        const std::shared_ptr<const rhi_webgpu::WebGpuCanonicalBufferUploadSource> bufferProof;
        verifyNativeOutput(bufferProof, checkpoint, 16u, 0u, wait, map);
        const auto quotas = prepared.quotas;
        checkpoint.reset();
        target.reset();
        issuers.reset();
        lowering.reset();
        prepared.drop();
        root.reset();
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_CANONICAL_%s_UPLOAD_INTERLEAVED_OK distinct-initialized-A-B upload-ordinals1-%zu regular-copy-element0 native-copies%u checkpoint-A final-B same-root genuine-completion source-revisions-unchanged hot-allocation0 domains-zero\n",
            texture ? "TEXTURE" : "BUFFER",
            last,
            texture ? 5u : 3u);
#else
        (void)root;
        (void)device;
        (void)proofs;
        (void)loss;
        (void)copied;
        (void)wait;
        (void)map;
#endif
    }

    template <class Proof, class Native, class QueueWait, class MapRead>
    static void verifyNativeOutput(const std::shared_ptr<const Proof>& proof,
        const std::shared_ptr<Native>& native,
        std::uint64_t size,
        std::uint8_t offset,
        QueueWait&& wait,
        MapRead&& map)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        constexpr bool texture =
            std::is_same_v<Proof, rhi_webgpu::WebGpuCanonicalTextureContentUploadSource>;
        auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = [&] {
            if constexpr (texture)
                return std::uint64_t{768u};
            else
                return size;
        }();
        desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        const auto readback = wgpuDeviceCreateBuffer(native->ownerDevice, &desc);
        assert(readback);
        const auto encoder = wgpuDeviceCreateCommandEncoder(native->ownerDevice, nullptr);
        assert(encoder);
        if constexpr (texture) {
            for (const auto& mip :
                proof->originalStaging()->originalPackingSource()->packing()->mipLayouts()) {
                auto source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
                source.texture = native->texture;
                source.mipLevel = mip.range.baseMipLevel;
                source.aspect = WGPUTextureAspect_All;
                auto destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
                destination.buffer = readback;
                destination.layout.offset = mip.packedOffset;
                destination.layout.bytesPerRow = mip.packedBytesPerRow;
                destination.layout.rowsPerImage = mip.rowCount;
                const WGPUExtent3D extent{mip.extent.width, mip.extent.height, mip.extent.depth};
                wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);
            }
        } else
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder, native->buffer, 0u, readback, 0u, desc.size);
        const auto command = wgpuCommandEncoderFinish(encoder, nullptr);
        assert(command);
        const auto queue = wgpuDeviceGetQueue(native->ownerDevice);
        wgpuQueueSubmit(queue, 1u, &command);
        assert(wait(queue) && map(readback, desc.size));
        const auto* output = static_cast<const std::uint8_t*>(
            wgpuBufferGetConstMappedRange(readback, 0u, desc.size));
        assert(output);
        if constexpr (texture) {
            std::uint64_t pixel = 0u;
            for (const auto& mip :
                proof->originalStaging()->originalPackingSource()->packing()->mipLayouts())
                for (std::uint32_t row = 0u; row < mip.extent.height; ++row)
                    for (std::uint32_t byte = 0u; byte < mip.extent.width * 4u; ++byte)
                        assert(output[mip.packedOffset + row * mip.packedBytesPerRow + byte]
                            == offset + (++pixel));
            assert(pixel == 20u);
        } else
            for (std::uint64_t i = 0u; i < desc.size; ++i)
                assert(output[i] == offset + i + 1u);
        wgpuBufferUnmap(readback);
        wgpuBufferRelease(readback);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        wgpuQueueRelease(queue);
#else
        (void)proof;
        (void)native;
        (void)size;
        (void)offset;
        (void)wait;
        (void)map;
#endif
    }

    template <class Proof, class QueueWait, class MapRead>
    static void verifyExecution(std::shared_ptr<Root>& root,
        rhi::GpuDevice& device,
        std::shared_ptr<const Proof> proof,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        const std::shared_ptr<const std::conditional_t<
            std::is_same_v<Proof, rhi_webgpu::WebGpuCanonicalTextureContentUploadSource>,
            rhi::OriginalCommandTextureUploadSource,
            rhi::OriginalCommandBufferUploadSource>>& original,
        QueueWait&& wait,
        MapRead&& map,
        unsigned probe,
        core::UniquePtr<rhi::GpuDevice>* deviceOwner = nullptr)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        constexpr bool texture =
            std::is_same_v<Proof, rhi_webgpu::WebGpuCanonicalTextureContentUploadSource>;
        const auto bufferProof = [&] {
            if constexpr (texture)
                return proof->originalStaging()->initializedBuffer();
            else
                return proof;
        }();
        const auto recording = root->originalMetadataQuota();
        const auto captured =
            bufferProof->originalLease()->originalObject()->observeOriginalCommittedState();
        assert(captured);
        const bool successful = probe == 0u || probe == 8u;
        Prepared prepared(root, device, proof, loss, texture);
        const char* diagnostic = nullptr;
        auto lowering =
            Lowering::prepareOriginal(prepared.custody, prepared.lowerBudget, diagnostic);
        assert(lowering);
        auto issuers = Issuers::prepareOriginal(
            lowering, {1u, true, 1u, 1u, prepared.ownerLimit, bytes, bytes}, diagnostic);
        assert(issuers);
        auto native = rhi_webgpu::webGpuNativePayloadFor(
            *original->originalDestinationLease()->originalObject());
        assert(native);
        if constexpr (texture) {
            const rhi::OriginalCompletedShaderTextureInputContract::Budget inputBudget{
                1u, true, 2u, 2u, 20u, 5u};
            is(Issuers::validateOriginalCompletedShaderTextureInput(
                   issuers, proof, proof->destinationContentExpectation(), inputBudget),
                "RHI-COMPLETED-SHADER-TEXTURE-ISSUER-PENDING");
        }
        const auto queue = wgpuDeviceGetQueue(native->ownerDevice);
        assert(queue);
        SubmissionObservation observation{root.get(), &issuers};
        is(Issuers::submitOriginal(issuers, {1u, true}),
            "RHI-WEBGPU-CANONICAL-SUBMISSION-UNRECORDED");
        if (probe == 5u) {
            if constexpr (texture)
                wgpuTextureDestroy(native->texture);
            else
                wgpuBufferDestroy(native->buffer);
        }
        if (probe == 2u)
            WebGpuNativeSubmissionProbeFixture::instrumentRecording(native->ownerDevice);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            prepared.quotas, [&]() noexcept -> const char* {
                assert(!Issuers::recordOriginal(issuers, {1u, true}));
                if (probe == 2u)
                    WebGpuNativeSubmissionProbeFixture::restoreRecording();
                assert(root->status() == Root::Status::Pending && !root->journalEmpty());
                is(bufferProof->readyDiagnostic(), "RHI-CANONICAL-UPLOAD-SOURCE-STATE-STALE");
                if constexpr (texture) {
                    is(proof->validate(), "RHI-CANONICAL-CONTENT-CONTROL-PENDING");
                    assert(root->plannedContentGeneration(1u) == 1u
                        && root->plannedContentGeneration(2u) == 2u);
                    assert(!original->originalDestinationLease()
                            ->originalObject()
                            ->observeOriginalContent());
                }
                assert(!issuers->hasOriginalCompletedSubmission());
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return issuers->hasOriginalRecordedCommand()
                        || root->status() != Root::Status::Pending;
                }));
                if (probe == 2u) {
                    assert(
                        !issuers->hasOriginalRecordedCommand() && issuers->recordingDiagnostic());
                    assert(root->status() == Root::Status::Abandoned && root->journalEmpty());
                    return nullptr;
                }
                assert(issuers->hasOriginalRecordedCommand() && !issuers->recordingDiagnostic());
                if (probe == 1u) {
                    assert(!prepared.custody->cancelOriginal());
                    assert(root->status() == Root::Status::Abandoned && root->journalEmpty());
                    return nullptr;
                }
                std::weak_ptr<Root> retained = root;
                using NativeProbe = WebGpuNativeSubmissionProbeFixture;
                if (probe >= 6u) {
                    const auto kind = probe == 6u ? NativeProbe::Probe::ThrowBefore
                        : probe == 7u             ? NativeProbe::Probe::ThrowAfter
                        : probe == 8u             ? NativeProbe::Probe::Reverse
                        : probe == 9u             ? NativeProbe::Probe::PartialScope
                                                  : NativeProbe::Probe::Success;
                    NativeProbe::instrument(
                        native->ownerDevice, queue, kind, &observation, &observeMaySubmit);
                }
                const auto* submitted = Issuers::submitOriginal(issuers, {1u, true});
                if (probe == 6u || probe == 7u)
                    is(submitted, "RHI-WEBGPU-CANONICAL-SUBMISSION-INVOCATION-UNCERTAIN");
                else if (probe == 9u)
                    is(submitted, "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
                else
                    assert(!submitted);
                if (probe >= 6u)
                    NativeProbe::restore();
                if (probe == 10u) {
                    if constexpr (texture)
                        --native->extent.width;
                    else
                        --native->sizeBytes;
                }
                if (probe == 11u) {
                    assert(deviceOwner);
                    deviceOwner->reset();
                }
                if (probe == 4u)
                    is(prepared.custody->cancelOriginal(),
                        "RHI-WEBGPU-CANONICAL-CALLBACK-QUARANTINED");
                if (probe == 3u) {
                    root.reset();
                    issuers.reset();
                    lowering.reset();
                    prepared.drop();
                    assert(!retained.expired());
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return retained.expired();
                    }));
                    return nullptr;
                }
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return !prepared.custody->pendingOriginalRecords();
                }));
                assert(issuers->hasOriginalCompletedSubmission() == successful);
                assert(root->status()
                        == (successful ? Root::Status::Committed : Root::Status::Invalidated)
                    && root->journalEmpty());
                if (successful) {
                    assert(!issuers->submissionDiagnostic() && !bufferProof->readyDiagnostic());
                    assert(!bufferProof->originalLease()
                            ->originalObject()
                            ->validateOriginalCommittedState(*captured));
                    if constexpr (texture) {
                        const auto completed = original->originalDestinationLease()
                                                   ->originalObject()
                                                   ->observeOriginalContent();
                        assert(completed);
                        assert(completed->state()
                                == rhi::common::OriginalTextureContentControl::State::Completed
                            && completed->contentGeneration() == 2u
                            && completed->revision()
                                == proof->destinationContentExpectation().revision()
                                    + root->plannedContentUploadCount() + 1u
                            && completed->contentGeneration() != 29u);
                        is(proof->validate(), "RHI-CANONICAL-CONTENT-EXPECTATION-STALE");
                        assert(native->contentAccessPolicy().ordinaryAccessDiagnostic());
                        const rhi::OriginalCompletedShaderTextureInputContract::Budget inputBudget{
                            1u, true, 2u, 2u, 20u, 5u};
                        const auto validate = [&](const auto& issuer,
                                                  const auto& source,
                                                  const auto& expectation,
                                                  const auto& budget) noexcept {
                            return Issuers::validateOriginalCompletedShaderTextureInput(
                                issuer, source, expectation, budget);
                        };
                        assert(!validate(issuers, proof, *completed, inputBudget));
                        assert(validate(
                            issuers, proof, proof->destinationContentExpectation(), inputBudget));
                        auto forgedIssuer =
                            std::shared_ptr<const Issuers>(recording, issuers.get());
                        is(validate(forgedIssuer, proof, *completed, inputBudget),
                            "RHI-COMPLETED-SHADER-TEXTURE-ISSUER-OWNER");
                        forgedIssuer.reset();
                        auto forgedProof = std::shared_ptr<const Proof>(recording, proof.get());
                        is(validate(issuers, forgedProof, *completed, inputBudget),
                            "RHI-CANONICAL-CONTENT-STAGING-OWNER");
                        forgedProof.reset();
                        auto shortBudget = inputBudget;
                        --shortBudget.maxTexelBytes;
                        is(validate(issuers, proof, *completed, shortBudget),
                            "RHI-COMPLETED-SHADER-TEXTURE-CAPACITY");
                        shortBudget = inputBudget;
                        --shortBudget.maxJoinVisits;
                        is(validate(issuers, proof, *completed, shortBudget),
                            "RHI-COMPLETED-SHADER-TEXTURE-CAPACITY");
                        --native->extent.width;
                        assert(validate(issuers, proof, *completed, inputBudget));
                        ++native->extent.width;
                        assert(!validate(issuers, proof, *completed, inputBudget));
                        std::puts(
                            "BINDLESS_RHI_COMPLETED_SHADER_TEXTURE_INPUT_OK original-issuer-root upload-source whole-sampled2D pixels20 mips2 completed-generation2 scopes-queue-drained stale-forged-capacity-native-mutation-refused hot0 shader-execution-closed");
                    }
                    is(Issuers::submitOriginal(issuers, {1u, true}),
                        "RHI-WEBGPU-CANONICAL-SUBMISSION-ONE-SHOT");
                }
                return nullptr;
            }));
        if (successful) {
            if constexpr (texture) {
                if ((original->originalDestinationLease()->originalObject()->desc().usage
                        & rhi::TextureUsage::Sampled)
                    != rhi::TextureUsage::None) {
                    const auto completed = original->originalDestinationLease()
                                               ->originalObject()
                                               ->observeOriginalContent();
                    assert(completed);
                    using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
                    using View = rhi::OriginalCommandTextureViewSource;
                    const auto& sourceQuota =
                        original->originalDestinationLease()->originalMetadataQuota();
                    const auto viewQuota = sourceQuota;
                    std::weak_ptr<const View> wholeRetired, subviewRetired;
                    {
                        rhi::TextureViewDesc desc;
                        desc.format =
                            original->originalDestinationLease()->originalObject()->desc().format;
                        desc.usage = rhi::TextureUsage::Sampled;
                        desc.mipLevelCount = 2u;
                        core::UniquePtr<rhi::GpuTextureView> candidate;
                        {
                            core::CpuMemoryQuotaScope scope(viewQuota);
                            candidate = device.createTextureView(
                                *original->originalDestinationLease()->originalObject(), desc);
                        }
                        assert(candidate);
                        const char* id = nullptr;
                        auto lease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
                            candidate, {1u, true, 2u, bytes}, sourceQuota, id);
                        assert(lease && !id);
                        auto view = View::prepareOriginal(lease,
                            original->originalDestinationLease(),
                            {1u, true, 5u, bytes},
                            sourceQuota,
                            id);
                        assert(view && !id);
                        wholeRetired = view;
                        Closure::ShaderTextureContentInput input{4u,
                            root,
                            issuers,
                            proof,
                            *completed,
                            rhi::ResourceState::ShaderRead,
                            rhi::PipelineStage::ComputeShader,
                            rhi::ResourceAccess::ShaderRead};
                        const rhi::OriginalCommandNativeClosureBudget::ShaderTextureContentCustody
                            binding{1u,
                                true,
                                1u,
                                issuers->retainedOwnerCount() + Proof::retainedOwnerBound + 1u,
                                20u,
                                2u,
                                2u,
                                5u};
                        const auto verify =
                            [&](const auto& content, const auto& source, const auto& b) {
                                return Closure::validateCompletedTextureInput(content, source, b);
                            };
                        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                            std::array{recording, sourceQuota}, [&]() noexcept -> const char* {
                                assert(!verify(input, view, binding));
                                {
                                    auto invalid = input;
                                    invalid.completed.reset();
                                    assert(verify(invalid, view, binding));
                                }
                                {
                                    auto invalid = input;
                                    invalid.shaderState = rhi::ResourceState::CopyDst;
                                    is(verify(invalid, view, binding),
                                        "RHI-COMPLETED-SHADER-TEXTURE-VIEW-ACCESS");
                                }
                                {
                                    auto invalid = input;
                                    invalid.stages = rhi::PipelineStage::None;
                                    is(verify(invalid, view, binding),
                                        "RHI-COMPLETED-SHADER-TEXTURE-VIEW-ACCESS");
                                }
                                {
                                    auto invalid = input;
                                    invalid.access = rhi::ResourceAccess::ShaderWrite;
                                    is(verify(invalid, view, binding),
                                        "RHI-COMPLETED-SHADER-TEXTURE-VIEW-ACCESS");
                                }
                                {
                                    auto invalid = input;
                                    invalid.uploadRoot =
                                        std::shared_ptr<const Root>(recording, root.get());
                                    is(verify(invalid, view, binding),
                                        "RHI-COMPLETED-SHADER-TEXTURE-ORIGINAL-ROOT");
                                }
                                auto forged = std::shared_ptr<const View>(viewQuota, view.get());
                                assert(verify(input, forged, binding));
                                forged.reset();
                                auto shortBudget = binding;
                                --shortBudget.maxRetainedOwners;
                                is(verify(input, view, shortBudget),
                                    "RHI-WEBGPU-GENERAL-TEXTURE-OWNER-CAPACITY");
                                shortBudget = binding;
                                shortBudget.prototypeOptIn = false;
                                assert(verify(input, view, shortBudget));
                                const auto state =
                                    original->originalDestinationLease()
                                        ->originalObject()
                                        ->observeOriginalCommittedState(
                                            {0u, 2u, 0u, 1u, rhi::TextureAspect::Color});
                                assert(state && state->state() == rhi::ResourceState::CopyDst
                                    && !state->diagnostic());
                                return nullptr;
                            }));
                        // A valid native subview cannot replace the declared whole initialized view.
                        desc.mipLevelCount = 1u;
                        {
                            core::CpuMemoryQuotaScope scope(viewQuota);
                            candidate = device.createTextureView(
                                *original->originalDestinationLease()->originalObject(), desc);
                        }
                        assert(candidate);
                        auto sublease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
                            candidate, {1u, true, 2u, bytes}, sourceQuota, id);
                        assert(sublease && !id);
                        auto subview = View::prepareOriginal(sublease,
                            original->originalDestinationLease(),
                            {1u, true, 5u, bytes},
                            sourceQuota,
                            id);
                        assert(subview && !id);
                        subviewRetired = subview;
                        is(verify(input, subview, binding),
                            "RHI-COMPLETED-SHADER-TEXTURE-VIEW-RANGE");
                    }
                    assert(wholeRetired.expired() && subviewRetired.expired());
                    wholeRetired.reset();
                    subviewRetired.reset();
                    // Original registry observations retain native weak control
                    // blocks. This existing source domain is checked for zero
                    // after device retirement by the outer upload fixture.
                    std::puts(
                        "BINDLESS_RHI_COMPLETED_SHADER_TEXTURE_VIEW_INPUT_OK actual-whole2D-view same-upload-parent-root completed-content frozen-read-access subview-forged-owner-budget-refused hot0 view-owners-expired source-quota final-original-retirement committed-CopyDst transition-unissued shader-execution-closed");
                }
            }
            verifyNativeOutput(
                proof,
                native,
                [&] {
                    if constexpr (texture)
                        return std::uint64_t{20u};
                    else
                        return original->sizeBytes();
                }(),
                0u,
                wait,
                map);
            if constexpr (texture)
                std::puts(
                    "BINDLESS_RHI_CANONICAL_TEXTURE_UPLOAD_TARGET_OK pixels20 mips2 original-destination actual-native-readback same-root-state-content-commit minted-generation2 caller-generation29-refused ordinary-access-closed");
            else
                std::puts(
                    "BINDLESS_RHI_CANONICAL_BUFFER_UPLOAD_TARGET_OK bytes16 original-destination actual-native-readback same-root-commit source-revision-unchanged");
        }
        wgpuQueueRelease(queue);
        if (probe == 10u) {
            if constexpr (texture)
                ++native->extent.width;
            else
                ++native->sizeBytes;
        }
        const auto quotas = prepared.quotas;
        native.reset();
        issuers.reset();
        lowering.reset();
        prepared.drop();
        root.reset();
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
        assert(!recording->liveBytes());
        std::printf(
            "BINDLESS_RHI_CANONICAL_%s_UPLOAD_EXECUTION_OK probe%u real-journal-pending public-source-refused initialized-facts exact-native-join recording-scopes3 queue-scopes4 no-second-proof hot-allocation0 recording-submission-callback-quotas-zero\n",
            texture ? "TEXTURE" : "BUFFER",
            probe);
#else
        (void)root;
        (void)device;
        (void)proof;
        (void)loss;
        (void)original;
        (void)wait;
        (void)map;
        (void)probe;
        (void)deviceOwner;
#endif
    }

    static void verify(std::shared_ptr<Root>& root,
        rhi::GpuDevice& device,
        Joins::Source source,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        bool texture)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const auto inputs = root->originalGraph()->originalInputs();
        const auto stateQuota = root->originalGraph()->originalMetadataQuota();
        const auto recordingQuota = root->originalMetadataQuota();
        const char* diagnostic = nullptr;
        core::UniquePtr<Prepared> prepared;
        const auto prepare = [&] {
            if (prepared) {
                prepared->drop();
                prepared.reset();
            }
            root.reset();
            root = WebGpuCanonicalUploadJoinsFixture::prepareRoot(
                inputs, device, stateQuota, recordingQuota, texture, diagnostic);
            assert(root);
            prepared = core::makeUnique<Prepared>(root, device, source, loss, texture);
        };
        const auto make = [&] {
            auto result =
                Lowering::prepareOriginal(prepared->custody, prepared->lowerBudget, diagnostic);
            if (result)
                assert(!prepared->custody->cancelOriginal());
            else
                is(diagnostic, "RHI-WEBGPU-CANONICAL-LOWERING-ALLOCATION");
            return result;
        };
        prepare();
        CpuQuotaFactoryFixture pressure(recordingQuota);
        const auto peak = pressure.measure(make);
        pressure.verifySingleUse(peak, prepare, make);
        prepare();
        const auto limits = prepared->lowerBudget;
        const auto refuse = [&](const Lowering::Budget& bad, const char* id) {
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Lowering::prepareOriginal(prepared->custody, bad, diagnostic));
            is(diagnostic, id);
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations
                && root->status() == Root::Status::Prepared && root->journalEmpty());
        };
        auto bad = limits;
        --bad.maxUploadCopyOperations;
        refuse(bad, "RHI-WEBGPU-CANONICAL-LOWERING-UPLOAD-COPY-CAPACITY");
        bad = limits;
        --bad.maxRetainedOwners;
        refuse(bad, "RHI-WEBGPU-CANONICAL-LOWERING-OWNER-CAPACITY");
        bad = limits;
        --bad.maxCpuMetadataBytes;
        refuse(bad, "RHI-WEBGPU-CANONICAL-LOWERING-QUOTA");
        bad = limits;
        --bad.uploadWork.maxNativeCopyCommands;
        refuse(bad, "RHI-ORIGINAL-ORDERED-UPLOAD-NATIVE-COPY-CAPACITY");
        bad = limits;
        --bad.uploadStaging.maxTotalBytes;
        refuse(bad, "RHI-ORIGINAL-UPLOAD-STAGING-TOTAL-CAPACITY");
        if (texture) {
            bad = limits;
            --bad.maxTextureUploadMips;
            refuse(bad, "RHI-ORIGINAL-STREAM-UPLOAD-MIP-CAPACITY");
            bad = limits;
            --bad.maxTextureUploadPixelBytes;
            refuse(bad, "RHI-ORIGINAL-STREAM-UPLOAD-PIXEL-BYTES");
            bad = limits;
            --bad.uploadWork.maxTextureUploadCommands;
            refuse(bad, "RHI-ORIGINAL-ORDERED-UPLOAD-TEXTURE-COMMAND-CAPACITY");
            bad = limits;
            --bad.uploadWork.maxTextureWriteTexelBytes;
            refuse(bad, "RHI-ORIGINAL-ORDERED-UPLOAD-TEXEL-WORK-CAPACITY");
            bad = limits;
            --bad.uploadStaging.maxPackedTextureBytes;
            refuse(bad, "RHI-ORIGINAL-UPLOAD-STAGING-TEXTURE-CAPACITY");
        } else {
            bad = limits;
            --bad.maxBufferUploadCommands;
            refuse(bad, "RHI-ORIGINAL-STREAM-BUFFER-UPLOAD-CAPACITY");
            bad = limits;
            --bad.maxBufferUploadDeclaredBytes;
            refuse(bad, "RHI-ORIGINAL-STREAM-BUFFER-UPLOAD-BYTES");
            bad = limits;
            --bad.uploadStaging.maxBufferBytes;
            refuse(bad, "RHI-ORIGINAL-UPLOAD-STAGING-BUFFER-CAPACITY");
        }
        for (auto version : {1u, 2u}) {
            bad = limits;
            bad.contractVersion = version;
            refuse(bad, "RHI-ORIGINAL-COPY-PASS-LOWERING-UPLOAD-PROFILE");
        }
        auto lowering = Lowering::prepareOriginal(prepared->custody, limits, diagnostic);
        assert(lowering && !lowering->validate() && !lowering->projectedCopyCount()
            && lowering->projectedUploadCopyCount() == limits.maxUploadCopyOperations
            && !lowering->nativeExecutionProven());
        const auto verifyCopies = [&]() noexcept -> const char* {
            if (const auto* id = lowering->validate())
                return id;
            const auto& inventory =
                inputs->originalScalars()->originalUsage()->originalSources()->sources();
            for (std::size_t join = 0u; join < 2u; ++join) {
                if (texture) {
                    const auto proof = std::get<std::shared_ptr<
                        const rhi_webgpu::WebGpuCanonicalTextureContentUploadSource>>(source);
                    const auto target =
                        std::get<std::shared_ptr<const rhi::OriginalCommandTextureUploadSource>>(
                            inventory[1u]);
                    for (std::size_t mip = 0u; mip < 2u; ++mip) {
                        rhi::BufferToTextureCopyDesc copy;
                        copy.sourceBuffer = proof->originalStaging()
                                                ->initializedBuffer()
                                                ->originalLease()
                                                ->resource();
                        copy.destinationTexture = target->originalDestinationLease()->resource();
                        copy.bufferOffsetBytes = mip ? 512u : 0u;
                        copy.bytesPerRow = 256u;
                        copy.rowsPerImage = mip ? 1u : 2u;
                        copy.textureSubresources = {
                            static_cast<std::uint32_t>(mip), 1u, 0u, 1u, rhi::TextureAspect::Color};
                        copy.extent = mip ? rhi::Extent3D{1u, 1u, 1u} : rhi::Extent3D{2u, 2u, 1u};
                        assert(WebGpuCanonicalCopyPassLoweringFixture::inspectUploadProjection(
                            lowering,
                            join * 2u + mip,
                            join + 1u,
                            join,
                            rhi::TransferCopyArguments::captureScalarFields(copy)));
                        ++copy.bufferOffsetBytes;
                        assert(!WebGpuCanonicalCopyPassLoweringFixture::inspectUploadProjection(
                            lowering,
                            join * 2u + mip,
                            join + 1u,
                            join,
                            rhi::TransferCopyArguments::captureScalarFields(copy)));
                    }
                } else {
                    rhi::BufferCopyDesc copy;
                    std::get<std::shared_ptr<const rhi::OriginalCommandBufferUploadSource>>(
                        inventory[2u])
                        ->arguments()
                        .copyArguments()
                        .write(copy);
                    assert(WebGpuCanonicalCopyPassLoweringFixture::inspectUploadProjection(lowering,
                        join,
                        join + 1u,
                        join,
                        rhi::TransferCopyArguments::captureScalarFields(copy)));
                    assert(
                        !WebGpuCanonicalCopyPassLoweringFixture::inspectUploadProjection(lowering,
                            join,
                            join + 2u,
                            join,
                            rhi::TransferCopyArguments::captureScalarFields(copy)));
                }
            }
            return nullptr;
        };
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(prepared->quotas, verifyCopies));
        assert(!Lowering::prepareOriginal(prepared->custody, limits, diagnostic));
        is(diagnostic, "RHI-WEBGPU-CANONICAL-LOWERING-DUPLICATE");
        const auto quotas = prepared->quotas;
        lowering.reset();
        assert(
            !CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept -> const char* {
                assert(!prepared->custody->cancelOriginal());
                prepared->drop();
                prepared.reset();
                root.reset();
                return nullptr;
            }));
        assert(!recordingQuota->liveBytes() && !quotas[1]->liveBytes() && !quotas[2]->liveBytes());
        std::printf(
            "BINDLESS_RHI_CANONICAL_UPLOAD_PROJECTION_OK %s peak%llu original-ordinals1-2 regular-copies0 upload-copies%u whole-mip-exact-geometry initialized-original-closure same-root cold-exact-less-exhausted-retry independent-work-bounds hot-allocation0 metadata-grants-no-execution\n",
            texture ? "texture" : "buffer",
            static_cast<unsigned long long>(peak),
            limits.maxUploadCopyOperations);
#else
        (void)root;
        (void)device;
        (void)source;
        (void)loss;
        (void)texture;
#endif
    }
};
} // namespace arp::test
