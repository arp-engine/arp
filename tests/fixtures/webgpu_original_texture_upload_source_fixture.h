#pragma once
#include "rhi/backends/webgpu/webgpu_original_texture_upload_copy_validation.h"
#include "rhi/backends/webgpu/webgpu_original_texture_upload_transaction.h"
#include "rhi/backends/webgpu/webgpu_upload_preparation.h"
#include "texture_readback_owned_preparation_fixture.h"
#include "webgpu_completed_texture_readback_destination_fixture.h"
#include "webgpu_completed_texture_readback_source_fixture.h"
#include "webgpu_original_buffer_upload_source_fixture.h"
#include "webgpu_original_texture_readback_completion_fixture.h"
#include "webgpu_original_texture_readback_mapping_fixture.h"
#include "webgpu_original_texture_readback_recording_fixture.h"
#include "webgpu_original_texture_upload_destination_fixture.h"
#include "webgpu_texture_readback_snapshot_fixture.h"

namespace arp::test {
class WebGpuOriginalTextureUploadSourceFixture final {
    using Source = rhi_webgpu::WebGpuOriginalTextureUploadSource;
    using Destination = rhi_webgpu::WebGpuOriginalTextureUploadDestination;
    using Budget = rhi_webgpu::WebGpuOriginalTextureUploadSourceBudget;
    using Authoring = WebGpuOriginalTextureUploadDestinationFixture::Context;
    static constexpr std::uint64_t cpuBytes = 65536u, packedBytes = 1536u;

    static Budget budget()
    {
        return {1u,
            true,
            cpuBytes,
            1696u,
            1360u,
            packedBytes,
            {1u,
                true,
                rhi_webgpu::WebGpuOriginalBufferUploadSource::callbackCount,
                cpuBytes,
                cpuBytes,
                cpuBytes}};
    }

    using Transaction = rhi_webgpu::WebGpuOriginalTextureUploadTransaction;
    using TransactionBudget = rhi_webgpu::WebGpuOriginalTextureUploadTransactionBudget;

    static TransactionBudget transactionBudget()
    {
        return {1u, true, 1u, 2u, 1696u, cpuBytes, {1u, true, cpuBytes}};
    }

    static rhi_webgpu::WebGpuOriginalTextureUploadRecordingBudget recordingBudget()
    {
        return {1u,
            true,
            {1u,
                true,
                4u,
                3u,
                1u,
                2u,
                1u,
                rhi_webgpu::WebGpuOrdinaryRecording::callbackCount,
                cpuBytes},
            transactionBudget(),
            2u};
    }

    class Context final {
    public:
        Authoring authoring;
        std::shared_ptr<Destination> destination;
        std::shared_ptr<rhi_webgpu::WebGpuRhiStagingUploadPool> pool;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 4> quotas;

        Context(std::uint32_t count = 1u, std::uint64_t bytes = packedBytes)
        {
            rhi_webgpu::WebGpuRhiStagingUploadPoolDesc p;
            p.originalSourceContractVersion = 1u;
            p.originalSourcePrototypeOptIn = true;
            p.maxOriginalSourceBuffers = count;
            p.maxOriginalSourceGpuBytes = bytes;
            pool = core::makeShared<rhi_webgpu::WebGpuRhiStagingUploadPool>(p);
            for (auto& q : quotas)
                q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        }

        void coldEmpty() const
        {
            for (const auto& q : quotas)
                assert(!q->liveBytes());
            assert(
                !pool->originalSourceCount() && !pool->originalSourceMemoryStats().allocatedBytes);
        }

        void retire()
        {
            destination.reset();
            authoring.registry.reset();
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            assert(WebGpuAsyncWaitFixture::until([&] {
                return !quotas[0]->liveBytes() && !quotas[1]->liveBytes()
                    && !pool->originalSourceCount();
            }));
#endif
            coldEmpty();
            authoring.retire();
        }
    };

    static auto prepare(Context& c, const Budget& b, const char*& diagnostic)
    {
        return Source::prepareOriginal(c.destination,
            c.pool,
            b,
            c.quotas[0],
            c.quotas[1],
            c.quotas[2],
            c.quotas[3],
            diagnostic);
    }

public:
    static void verifyContract()
    {
        TextureReadbackOwnedPreparationFixture::verifyContract();
        TransactionBudget tb;
        assert(std::string_view(Transaction::validateBudget(tb))
            == "RHI-WEBGPU-TEXTURE-TRANSACTION-VERSION");
        tb.contractVersion = 1u;
        assert(std::string_view(Transaction::validateBudget(tb))
            == "RHI-WEBGPU-TEXTURE-TRANSACTION-OPT-IN");
        tb.prototypeOptIn = true;
        assert(std::string_view(Transaction::validateBudget(tb))
            == "RHI-WEBGPU-TEXTURE-TRANSACTION-BUDGET");
        tb = transactionBudget();
        assert(!Transaction::validateBudget(tb));
        assert(std::string_view(Transaction::apply({})) == "RHI-WEBGPU-TEXTURE-TRANSACTION-OWNER");
        assert(std::string_view(Transaction::validateCompletedDeclarationOriginal({}))
            == "RHI-WEBGPU-TEXTURE-COMPLETED-OWNER");
        static_assert(
            !std::is_default_constructible_v<rhi_webgpu::WebGpuCompletedTextureUploadDeclaration>);
        static_assert(
            !std::is_copy_constructible_v<rhi_webgpu::WebGpuCompletedTextureUploadDeclaration>);
        static_assert(std::is_final_v<Transaction>);
        assert(std::string_view(
                   rhi_webgpu::WebGpuCompletedTextureUploadDeclaration::validateOriginal({}))
            == "RHI-WEBGPU-TEXTURE-COMPLETED-OWNER");
        assert(std::string_view(Transaction::validateInitialStream({}))
            == "RHI-WEBGPU-TEXTURE-TRANSACTION-STATES-OWNER");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto outcome = Transaction::lowering(backend);
            assert(outcome.diagnostic);
            assert(outcome.validatorOnly == (backend == rhi::Backend::Null));
            assert(outcome.currentMetadataImplemented == (backend == rhi::Backend::WebGPU));
        }
        assert(!Transaction::lowering(static_cast<rhi::Backend>(99)).currentMetadataImplemented);
        rhi_webgpu::WebGpuOriginalTextureUploadRecordingBudget rb;
        assert(std::string_view(rhi_webgpu::WebGpuOrdinaryRecording::validateBudget(rb))
            == "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-VERSION");
        rb.contractVersion = 1u;
        assert(std::string_view(rhi_webgpu::WebGpuOrdinaryRecording::validateBudget(rb))
            == "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-OPT-IN");
        rb = recordingBudget();
        assert(!rhi_webgpu::WebGpuOrdinaryRecording::validateBudget(rb));
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        {
            const char* id = nullptr;
            auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            assert(!rhi_webgpu::WebGpuOrdinaryRecording::prepareOriginalTextureUpload(
                       {}, {}, rb, quota, quota, quota, id)
                && std::string_view(id) == "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-NATIVE-UNAVAILABLE"
                && !quota->liveBytes());
        }
#endif
        std::puts(
            "BINDLESS_RHI_TEXTURE_UPLOAD_NATIVE_CONTRACT_OK explicit-prototype four-command copies-bounded private-content proof-issuers2");
        std::puts(
            "BINDLESS_RHI_TEXTURE_CURRENT_CONTRACT_OK backends4 initial-closed current-owned no-native-authority");
        std::puts(
            "BINDLESS_RHI_TEXTURE_COPY_LOWERING_CONTRACT_OK backends4 origin layer3d block-edge exact-decision metadata-only");
        Budget b;
        assert(std::string_view(Source::validateBudget(b)) == "RHI-TEXTURE-SOURCE-VERSION");
        b.contractVersion = 1u;
        assert(std::string_view(Source::validateBudget(b)) == "RHI-TEXTURE-SOURCE-OPT-IN");
        b.prototypeOptIn = true;
        assert(std::string_view(Source::validateBudget(b)) == "RHI-TEXTURE-SOURCE-BUDGET");
        b = budget();
        assert(!Source::validateBudget(b));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto result = Source::lowering(backend);
            assert(result.diagnostic);
            assert(result.validatorOnly == (backend == rhi::Backend::Null));
            assert(result.nativeSourceImplemented == (backend == rhi::Backend::WebGPU));
        }
        assert(!Source::lowering(static_cast<rhi::Backend>(99)).nativeSourceImplemented);
        assert(std::string_view(Source::initialize({})) == "RHI-TEXTURE-SOURCE-OWNER");
        {
            using Graph = rhi_webgpu::WebGpuGenericCommandProjection;
            rhi_webgpu::WebGpuGenericCommandProjectionBudget b;
            b.contractVersion = 1u;
            b.prototypeOptIn = true;
            b.maxCpuMetadataBytes = cpuBytes;
            b.maxOriginalTextureUploadSources = 1u;
            auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            const char* diagnostic = nullptr;
            std::shared_ptr<const Source> missing;
            Graph::Sources sources;
            sources.textureUploads = &missing;
            sources.textureUploadCount = 1u;
            assert(!Graph::prepare(std::move(sources), nullptr, 0u, b, quota, diagnostic)
                && std::string_view(diagnostic)
                    == "RHI-WEBGPU-COMMAND-PROJECTION-TEXTURE-UPLOAD-OWNER"
                && !quota->liveBytes());
        }
        Context c;
        const char* diagnostic = nullptr;
        assert(!prepare(c, b, diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-SOURCE-DESTINATION-OWNER");
        using OriginalInput = rhi::TextureUploadOriginalInputArguments;
        assert(!OriginalInput::captureOriginal({}));
        const auto& snapshot = c.authoring.source->packing()->inputOwner();
        auto forgedInput =
            std::shared_ptr<const rhi::TextureUploadInputSnapshot>(c.quotas[0], snapshot.get());
        assert(!OriginalInput::captureOriginal(forgedInput));
        forgedInput.reset();
        auto argument = OriginalInput::captureOriginal(snapshot);
        assert(argument && argument->kind() == rhi::CommandUsageKind::TextureUpload
            && argument->matches(*argument));
        argument.reset();
        TextureUploadPackedCopyFixture::verify(c.authoring.source->packing());
        c.retire();
        std::puts(
            "BINDLESS_RHI_TEXTURE_PACKED_COPY_CONTRACT_OK backends4 fields20 mips arrays3d bc-edge untouched allocation0 execution-closed");
        std::puts(
            "BINDLESS_RHI_TEXTURE_SOURCE_CONTRACT_OK four-backend private-factory readiness-only gates-closed");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual()
    {
        verifyCold();
        verifyReady();
        verifyProjection();
        verifyFullInputStream();
        verifyCurrentTransactions();
        verifyNativeUploadCases();
        verifyCompletedReadbackSource();
        verifyPendingMutation();
        verifyContentMutation();
        verifyErrors();
        verifyCollision();
        verifyRegistryReplacement();
        verifyPendingDrop();
        std::puts(
            "BINDLESS_RHI_TEXTURE_SOURCE_NATIVE_OK bytes1536 padding scopes3 queue fresh identity drop allocation0");
    }

    static void prepareActualLoss()
    {
        prepareCompletedDeclarationLoss();
        prepareCompletedReadbackSourceLoss();
        prepareNativeUploadLoss();
        for (std::size_t i = 0u; i < lossContexts_.size(); ++i) {
            lossContexts_[i] = core::makeUnique<Context>();
            readyDestination(*lossContexts_[i]);
            const char* diagnostic = nullptr;
            lossSources_[i] = prepare(*lossContexts_[i], budget(), diagnostic);
            assert(lossSources_[i]);
        }
        assert(!Source::initialize(lossSources_[0]));
        waitReady(lossSources_[0]);
        for (auto& quota : lossInputQuotas_)
            quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        lossInputStates_ =
            prepareFullInputStream(*lossContexts_[0], lossSources_[0], lossInputQuotas_, false);
        lossGraphQuota_ = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* graphDiagnostic = nullptr;
        lossGraph_ = prepareGraph(lossSources_[0], lossGraphQuota_, graphDiagnostic);
        assert(lossGraph_);
        for (auto& q : lossTransactionQuotas_)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        lossTransaction_ = Transaction::prepareOriginal(lossInputStates_,
            transactionBudget(),
            lossTransactionQuotas_[0],
            lossTransactionQuotas_[1],
            graphDiagnostic);
        assert(lossTransaction_);
        assert(!Transaction::apply(lossTransaction_) && !lossTransaction_->validate());
        Promises::holdActualPromises();
        assert(!Source::initialize(lossSources_[1]));
        Promises::restorePromiseMethods();
        Promises::waitHeldPromises();
        assert(lossSources_[1]->status() == Source::Status::Pending
            && lossSources_[2]->status() == Source::Status::Prepared);
    }

    static void verifyActualDeviceLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost());
        verifyCompletedDeclarationLoss();
        verifyCompletedReadbackSourceLoss();
        verifyNativeUploadLoss();
        for (const auto& s : lossSources_) {
            rhi::semantics::TextureUploadPackedCopy output, sentinel;
            sentinel.contentGeneration = UINT64_MAX;
            output = sentinel;
            assert(std::string_view(
                       Source::mapPackedCopyOriginal(s, s->packing()->inputOwner(), 0u, output))
                    == "RHI-TEXTURE-SOURCE-DEVICE-LOST"
                && output.matches(sentinel));
            assert(s->status() == Source::Status::DeviceLost
                && std::string_view(s->validate()) == "RHI-TEXTURE-SOURCE-DEVICE-LOST");
            assert(std::string_view(Source::initialize(s)) == "RHI-TEXTURE-SOURCE-DEVICE-LOST");
            assert(s->destination()->contentLedger()->state(*s->packing())
                == rhi::TextureUploadContentLedger::ContentState::Invalidated);
        }
        assert(std::string_view(lossInputStates_->validate())
            == "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE-LOST");
        rhi::semantics::TextureUploadPackedCopy current, sentinel;
        sentinel.contentGeneration = UINT64_MAX;
        current = sentinel;
        assert(std::string_view(Transaction::mapPackedCopyCurrent(lossTransaction_, 0u, current))
                == "RHI-TEXTURE-SOURCE-DEVICE-LOST"
            && current.matches(sentinel));
        rhi::semantics::TextureUploadCopyLowering::Decision lostDecision;
        assert(
            std::string_view(rhi_webgpu::WebGpuOriginalTextureUploadCopyValidation::validateCurrent(
                lossTransaction_, 0u, lostDecision, nullptr))
            == "RHI-TEXTURE-SOURCE-DEVICE-LOST");
        std::puts("BINDLESS_RHI_TEXTURE_COPY_LOWERING_LOSS_OK actual-loss current-gate no-emit");
        lossTransaction_->abandon();
        assert(lossTransaction_->status() == Transaction::Status::Invalidated);
        lossTransaction_.reset();
        for (const auto& q : lossTransactionQuotas_)
            assert(!q->liveBytes());
        lossTransactionQuotas_ = {};
        std::puts(
            "BINDLESS_RHI_TEXTURE_CURRENT_LOSS_OK original-pending terminal no-completion exact-retire");
        lossInputStates_.reset();
        for (const auto& q : lossInputQuotas_)
            assert(!q->liveBytes());
        lossInputQuotas_ = {};
        std::puts(
            "BINDLESS_RHI_TEXTURE_UPLOAD_INPUTS_LOSS_OK original-full-input states2 no-target-authority exact-retire");
        assert(std::string_view(lossGraph_->validate())
            == "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE-LOST");
        for (auto& s : lossSources_)
            s.reset();
        assert(lossGraph_->originalTextureUploads().size() == 1u);
        lossGraph_.reset();
        assert(!lossGraphQuota_->liveBytes());
        lossGraphQuota_.reset();
        Promises::releaseQueue();
        Promises::releaseScopes();
        for (auto& c : lossContexts_) {
            c->retire();
            c.reset();
        }
        std::puts("BINDLESS_RHI_TEXTURE_UPLOAD_PROJECTION_LOSS_OK original-pair gate exact-retire");
        std::puts(
            "BINDLESS_RHI_TEXTURE_PACKED_COPY_LOSS_OK no-target-authority untouched exact-retire");
        std::puts(
            "BINDLESS_RHI_TEXTURE_SOURCE_LOSS_OK prepared-pending-ready no-content-promotion exact-retire");
    }

private:
    struct PromiseDomain {
        static constexpr unsigned graphics = 5u;
    };

    using Promises = WebGpuOrdinarySubmissionFixture<PromiseDomain>;

    struct NativePromiseDomain {
        static constexpr unsigned graphics = 6u;
    };

    using NativePromises = WebGpuOrdinarySubmissionFixture<NativePromiseDomain>;
    enum class NativeCase : std::size_t {
        Array,
        Volume,
        BC,
        FinishError,
        CopyError,
        Stale,
        Quarantine,
        Loss,
        CompletedDeclarationLoss,
        CompletedReadback,
        CompletedReadbackLoss,
        ReadbackCurrentInvalidation,
        ReadbackColdDrop,
        ReadbackRecordingLoss,
        ReadbackCompletionPendingLoss,
        ReadbackCompletionQuarantine,
        ReadbackCompletionCompletedLoss,
        ReadbackCompletionMutation,
        ReadbackColdMapLoss,
        ReadbackColdMapDrop,
        ReadbackNativeMapPendingLoss,
        ReadbackNativeMapPostMapLoss,
        ReadbackNativeMapSnapshotLoss,
        ReadbackNativeMapDrop,
        Count
    };
    static constexpr std::size_t nativeCases = static_cast<std::size_t>(NativeCase::Count);
    using CompletedView = rhi_webgpu::WebGpuCompletedTextureUploadDeclaration;
    using Recording = rhi_webgpu::WebGpuOrdinaryRecording;
    using Submission = rhi_webgpu::WebGpuOrdinarySubmission;

    static void configureGeometry(Context& c, NativeCase kind)
    {
        if (kind == NativeCase::CompletedReadback || kind == NativeCase::CompletedReadbackLoss
            || kind == NativeCase::ReadbackCurrentInvalidation
            || kind == NativeCase::ReadbackColdDrop || kind == NativeCase::ReadbackRecordingLoss
            || kind == NativeCase::ReadbackCompletionPendingLoss
            || kind == NativeCase::ReadbackCompletionQuarantine
            || kind == NativeCase::ReadbackCompletionCompletedLoss
            || kind == NativeCase::ReadbackCompletionMutation
            || kind == NativeCase::ReadbackColdMapLoss || kind == NativeCase::ReadbackColdMapDrop
            || kind == NativeCase::ReadbackNativeMapPendingLoss
            || kind == NativeCase::ReadbackNativeMapPostMapLoss
            || kind == NativeCase::ReadbackNativeMapSnapshotLoss
            || kind == NativeCase::ReadbackNativeMapDrop) {
            const auto claim = c.authoring.source->claim();
            c.authoring.source.reset();
            std::array<std::uint8_t, 160> pixels{};
            for (std::size_t i = 0u; i < pixels.size(); ++i)
                pixels[i] = static_cast<std::uint8_t>(i + 1u);
            auto d = TextureUploadPackingFixture::contentInput(pixels.data());
            d.destinationTexture = claim.destination;
            d.destinationOffset = {0u, 0u, 0u};
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 5> q;
            for (std::size_t i = 0u; i < q.size(); ++i)
                q[i] = c.authoring.quotas[i];
            const char* diagnostic = nullptr;
            auto packing = TextureUploadPackingFixture::prepareContentPacking(
                d, c.authoring.desc, q, diagnostic);
            assert(packing);
            c.authoring.source = rhi::TextureUploadContentSource::prepareOriginal(
                packing, claim, {1u, true, 1696u, cpuBytes}, c.authoring.quotas[5], diagnostic);
            assert(c.authoring.source);
            return;
        }
        if (kind != NativeCase::Volume && kind != NativeCase::BC)
            return;
        using namespace rhi;
        const char* diagnostic = nullptr;
        const auto claim = c.authoring.source->claim();
        c.authoring.source.reset();
        std::array<std::uint8_t, 160> pixels{};
        for (std::size_t i = 0u; i < pixels.size(); ++i)
            pixels[i] = static_cast<std::uint8_t>(i + 1u);
        UploadTextureDesc d;
        d.destinationTexture = claim.destination;
        d.pixelData = pixels.data();
        d.pixelDataSize = pixels.size();
        d.frameSerial = 53u;
        d.resourceGeneration = 59u;
        d.subresources = {0u, 1u, 0u, 1u, TextureAspect::Color};
        d.format = kind == NativeCase::BC ? TextureFormat::Bc1Unorm : TextureFormat::Rgba8Unorm;
        d.extent = kind == NativeCase::BC ? Extent3D{6u, 6u, 1u} : Extent3D{2u, 2u, 2u};
        d.destinationOffset = kind == NativeCase::BC ? Extent3D{4u, 0u, 0u} : Extent3D{1u, 1u, 1u};
        d.bytesPerRow = kind == NativeCase::BC ? 16u : 8u;
        d.rowsPerImage = 2u;
        d.label = "original texture native geometry";
        auto& desc = c.authoring.desc;
        desc.extent = kind == NativeCase::BC ? Extent3D{10u, 6u, 1u} : Extent3D{4u, 4u, 4u};
        desc.format = d.format;
        desc.mipLevelCount = 1u;
        desc.arrayLayerCount = 1u;
        desc.cubeCompatible = false;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 5> q;
        for (std::size_t i = 0u; i < q.size(); ++i)
            q[i] = c.authoring.quotas[i];
        auto packed = TextureUploadPackingFixture::prepareContentPacking(d, desc, q, diagnostic);
        assert(packed);
        c.authoring.source = TextureUploadContentSource::prepareOriginal(
            packed, claim, {1u, true, 1696u, cpuBytes}, c.authoring.quotas[5], diagnostic);
        assert(c.authoring.source);
    }

    class NativeProbe final {
    public:
        Context context;
        std::shared_ptr<Source> source;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 6> inputQuotas, nativeQuotas;
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates> states;
        std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner> owner;
        std::shared_ptr<rhi_webgpu::WebGpuNativeCompletionState> signal;
        std::shared_ptr<Recording> recording;
        std::shared_ptr<Submission> submission;
        NativeCase kind;

        explicit NativeProbe(NativeCase value) : kind(value)
        {
            const char* diagnostic = nullptr;
            configureGeometry(context, value);
            readyDestination(context);
            source = prepare(context, budget(), diagnostic);
            assert(source && !Source::initialize(source));
            waitReady(source);
            for (auto& q : inputQuotas)
                q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            for (auto& q : nativeQuotas)
                q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            states = prepareFullInputStream(context, source, inputQuotas, false);
            owner = rhi_webgpu::prepareWebGpuOrdinaryCompletionOwner(
                {1u, true, 1u, cpuBytes}, nativeQuotas[4], diagnostic);
            assert(owner);
            signal = rhi_webgpu::WebGpuNativeCompletionState::prepareOrdinarySignal(
                {1u, true, 1u, cpuBytes}, nativeQuotas[5], diagnostic);
            assert(signal);
            const auto index = static_cast<std::size_t>(value);
            assert(!nativeOwnerQuotas_[index]);
            nativeOwnerQuotas_[index] = nativeQuotas[4];
            nativeOwners_[index] = owner;
        }

        auto prepareRecording(
            rhi_webgpu::WebGpuOriginalTextureUploadRecordingBudget b, const char*& diagnostic)
        {
            return Recording::prepareOriginalTextureUpload(states,
                owner,
                b,
                nativeQuotas[0],
                nativeQuotas[1],
                nativeQuotas[2],
                diagnostic,
                signal);
        }

        void record()
        {
            const char* diagnostic = nullptr;
            recording = prepareRecording(recordingBudget(), diagnostic);
            assert(recording);
            assert(!Recording::recordOriginalTextureUpload(recording));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return recording->status() != Recording::Status::RecordingPending;
            }));
        }

        void retire()
        {
            submission.reset();
            recording.reset();
            signal.reset();
            owner.reset();
            states.reset();
            source.reset();
            context.destination.reset();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return !nativeQuotas[0]->liveBytes() && !nativeQuotas[3]->liveBytes();
            }));
            for (std::size_t i = 0u; i < nativeQuotas.size(); ++i)
                if (i != 4u)
                    assert(!nativeQuotas[i]->liveBytes());
            for (const auto& q : inputQuotas)
                assert(!q->liveBytes());
            context.retire();
        }
    };

    static void verifyTargetReadback(const std::shared_ptr<Source>& source)
    {
        using namespace rhi;
        const auto& packing = *source->packing();
        const auto& input = *packing.inputOwner();
        const auto& desc = source->destination()->resource().textureDesc;
        EM_ASM(
            {
                // clang-format off
                    Module.arpTextureTargetPlans = [];
                    Module.arpTextureTargetExpected = HEAPU8.slice(Number($0), Number($0) + Number($1));
                // clang-format on
            },
            input.pixelBytes().data(),
            input.pixelBytes().size());
        for (const auto& mip : packing.mipLayouts()) {
            Extent3D full;
            assert(!semantics::TextureSubresourceExtent::resolve(
                desc.extent, desc.mipLevelCount, mip.range.baseMipLevel, full));
            const auto block = blockExtent(desc.format),
                       unit = static_cast<std::uint32_t>(isBlockCompressed(desc.format)
                               ? bytesPerBlock(desc.format)
                               : bytesPerPixel(desc.format));
            const auto width = blocksForExtent(full.width, block),
                       height = blocksForExtent(full.height, block);
            const auto aligned = core::alignCpuAllocationOffsetUp(
                static_cast<std::size_t>(width) * unit, kTextureUploadRowPitchAlignment);
            assert(aligned.success);
            const bool volume = desc.extent.depth > 1u;
            const auto slices = volume ? full.depth : desc.arrayLayerCount;
            EM_ASM(
                {
                    // clang-format off
                        Module.arpTextureTargetPlans.push({
                            mip : $0,
                            width : $1,
                            height : $2,
                            slices : $3,
                            pitch : $4,
                            unit : $5,
                            block : $6,
                            x : $7,
                            y : $8,
                            z : $9,
                            regionWidth : $10,
                            regionHeight : $11,
                            regionSlices : $12,
                            sourceOffset : Number($13),
                            sourcePitch : $14,
                            sourceRows : $15
                        });
                    // clang-format on
                },
                mip.range.baseMipLevel,
                width,
                height,
                slices,
                aligned.alignedValue,
                unit,
                block,
                mip.origin.width / block,
                mip.origin.height / block,
                volume ? mip.origin.depth : mip.range.baseArrayLayer,
                blocksForExtent(mip.extent.width, block),
                blocksForExtent(mip.extent.height, block),
                mip.sliceCount,
                mip.sourceOffset,
                mip.sourceBytesPerRow,
                mip.sourceRowsPerImage);
            EM_ASM(
                {
                    // clang-format off
                        Module.arpTextureTargetPlans[Module.arpTextureTargetPlans.length - 1].zeroUntouched = !!$0;
                    // clang-format on
                },
                !isBlockCompressed(desc.format));
        }
        EM_ASM(
            {
                // clang-format off
                    const device = Module.preinitializedWebGPUDevice;
                    const texture = WebGPU.getJsObject(Number($0));
                    Module.arpTextureTargetDone = false;
                    Module.arpTextureTargetPassed = false;
                    (async function() {
                        try {
                            for (const p of Module.arpTextureTargetPlans) {
                                const readback = device.createBuffer({
                                    size : p.pitch * p.height * p.slices,
                                    usage : GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ
                                });
                                const encoder = device.createCommandEncoder();
                                encoder.copyTextureToBuffer(
                                    {
                                        texture : texture,
                                        mipLevel : p.mip,
                                        origin : {x : 0, y : 0, z : 0},
                                        aspect : "all"
                                    },
                                    {buffer : readback, offset : 0, bytesPerRow : p.pitch, rowsPerImage : p.height},
                                    {
                                        width : p.width * p.block,
                                        height : p.height * p.block,
                                        depthOrArrayLayers : p.slices
                                    });
                                device.queue.submit([ encoder.finish() ]);
                                await readback.mapAsync(GPUMapMode.READ);
                                const data = new Uint8Array(readback.getMappedRange());
                                for (let z = 0; z < p.slices; ++z)
                                    for (let y = 0; y < p.height; ++y)
                                        for (let x = 0; x < p.width; ++x) {
                                            const inside = z >= p.z && z < p.z + p.regionSlices && y >= p.y &&
                                                           y < p.y + p.regionHeight && x >= p.x &&
                                                           x < p.x + p.regionWidth;
                                            for (let b = 0; b < p.unit; ++b) {
                                                const offset = (z * p.height + y) * p.pitch + x * p.unit + b;
                                                const expected =
                                                    inside
                                                        ? Module
                                                              .arpTextureTargetExpected[p.sourceOffset +
                                                                                        ((z - p.z) * p.sourceRows +
                                                                                         y - p.y) *
                                                                                            p.sourcePitch +
                                                                                        (x - p.x) * p.unit + b]
                                                        : 0;
                                                if ((inside || p.zeroUntouched) && data[offset] !== expected)
                                                    throw new Error("original texture target mismatch " +
                                                                    JSON.stringify({
                                                                        mip : p.mip,
                                                                        z,
                                                                        y,
                                                                        x,
                                                                        b,
                                                                        actual : data[offset],
                                                                        expected
                                                                    }));
                                            }
                                        }
                                readback.unmap();
                                readback.destroy();
                            }
                            Module.arpTextureTargetPassed = true;
                        } catch (error) {
                            Module.arpTextureTargetError = String(error);
                        } finally {
                            Module.arpTextureTargetDone = true;
                        }
                    })();
                // clang-format on
            },
            reinterpret_cast<std::uintptr_t>(source->destination()->resource().texture->texture));
        assert(WebGpuAsyncWaitFixture::until([] {
            return EM_ASM_INT({
                // clang-format off
                    return Module.arpTextureTargetDone;
                // clang-format on
            }) == 1;
        }));
        if (!EM_ASM_INT({
                // clang-format off
                return Module.arpTextureTargetPassed;
                // clang-format on
            }))
            EM_ASM({
                // clang-format off
                    console.error(Module.arpTextureTargetError);
                // clang-format on
            });
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpTextureTargetPassed;
            // clang-format on
        }) == 1);
    }

    static void verifyNativeUploadCases()
    {
        using CS = rhi::TextureUploadContentLedger::ContentState;
        for (const auto kind : {NativeCase::Array,
                 NativeCase::Volume,
                 NativeCase::BC,
                 NativeCase::FinishError,
                 NativeCase::CopyError,
                 NativeCase::Stale}) {
            if (kind == NativeCase::BC
                && !rhi_webgpu::webGpuNativeDeviceSupportsTextureCompressionBc()) {
                Context c;
                configureGeometry(c, kind);
                const char* diagnostic = nullptr;
                assert(!WebGpuOriginalTextureUploadDestinationFixture::prepareDestination(
                           c.authoring, diagnostic)
                    && std::string_view(diagnostic) == "RHI-TEXTURE-DESTINATION-BC-FEATURE");
                c.retire();
                std::puts(
                    "BINDLESS_RHI_TEXTURE_UPLOAD_NATIVE_BC_DEGRADED RHI-TEXTURE-DESTINATION-BC-FEATURE query false creation0");
                continue;
            }
            NativeProbe p(kind);
            const char* diagnostic = nullptr;
            auto b = recordingBudget();
            const auto factory = [&] {
                return p.prepareRecording(b, diagnostic);
            };
            std::array<std::uint64_t, 3> peaks{};
            if (kind == NativeCase::Array) {
                EM_ASM({
                    // clang-format off
                        Module.arpTextureNativeEncoderCount = 0;
                        Module.arpTextureNativeWrites = 0;
                        Module.arpTextureNativeCopies = [];
                        Module.arpTextureNativeLabels = [];
                        Module.arpTextureNativeEncoder = Module.preinitializedWebGPUDevice.createCommandEncoder;
                        Module.arpTextureNativeWrite = Module.preinitializedWebGPUDevice.queue.writeTexture;
                        Module.preinitializedWebGPUDevice.queue.writeTexture = function() {
                            ++Module.arpTextureNativeWrites;
                            return Module.arpTextureNativeWrite.apply(this, arguments);
                        };
                        Module.preinitializedWebGPUDevice.createCommandEncoder = function(desc) {
                            ++Module.arpTextureNativeEncoderCount;
                            const encoder = Module.arpTextureNativeEncoder.call(this, desc);
                            const push = encoder.pushDebugGroup;
                            encoder.pushDebugGroup = function(label) {
                                Module.arpTextureNativeLabels.push(label);
                                return push.call(this, label);
                            };
                            const copy = encoder.copyBufferToTexture;
                            encoder.copyBufferToTexture = function(s, d, e) {
                                Module.arpTextureNativeCopies.push({
                                    buffer : s.buffer,
                                    offset : s.offset,
                                    bytesPerRow : s.bytesPerRow,
                                    rowsPerImage : s.rowsPerImage,
                                    texture : d.texture,
                                    mip : d.mipLevel,
                                    aspect : d.aspect,
                                    origin : {...d.origin},
                                    extent : {...e}
                                });
                                return copy.call(this, s, d, e);
                            };
                            return encoder;
                        };
                    // clang-format on
                });
                for (std::size_t i = 0u; i < 3u; ++i) {
                    CpuQuotaFactoryFixture pressure(p.nativeQuotas[i]);
                    peaks[i] = pressure.measure(factory);
                    pressure.verify(peaks[i], factory);
                    {
                        core::CpuMemoryQuotaLease full(p.nativeQuotas[i], cpuBytes);
                        assert(full.charged() && !factory());
                    }
                }
                --b.maxMipCopies;
                assert(!factory()
                    && std::string_view(diagnostic)
                        == "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-COPY-CAPACITY");
                ++b.maxMipCopies;
                --b.native.maxCommands;
                assert(!factory()
                    && std::string_view(diagnostic)
                        == "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-COPY-CAPACITY");
                ++b.native.maxCommands;
                --b.native.maxDebugScopes;
                assert(!factory()
                    && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-BUDGET");
                ++b.native.maxDebugScopes;
                auto validStates = p.states;
                for (const auto label :
                    {std::string_view("bad\0label", 9u), std::string_view("\xc0\xaf", 2u)}) {
                    p.states =
                        prepareFullInputStream(p.context, p.source, p.inputQuotas, false, label);
                    assert(!factory()
                        && std::string_view(diagnostic)
                            == "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-LABEL-ENCODING");
                }
                p.states = std::move(validStates);
                assert(EM_ASM_INT({
                    // clang-format off
                        return Module.arpTextureNativeEncoderCount;
                    // clang-format on
                }) == 0
                    && EM_ASM_INT({
                           // clang-format off
                            return Module.arpTextureNativeWrites;
                           // clang-format on
                       })
                        == 0);
            }
            p.recording = factory();
            assert(p.recording);
            auto transaction = p.recording->originalTextureTransaction();
            assert(transaction && !transaction->validate());
            assert(Recording::recordOriginalBufferUpload(p.recording)
                && Recording::recordOriginalTransfer(p.recording)
                && Recording::recordOriginalTransferDependency(p.recording));
            if (kind == NativeCase::FinishError || kind == NativeCase::CopyError)
                EM_ASM(
                    {
                        // clang-format off
                            Module.arpTextureBadEncoder = Module.preinitializedWebGPUDevice.createCommandEncoder;
                            Module.preinitializedWebGPUDevice.createCommandEncoder = function(d) {
                                const encoder = Module.arpTextureBadEncoder.call(this, d);
                                if ($0)
                                    encoder.finish();
                                else {
                                    const copy = encoder.copyBufferToTexture;
                                    encoder.copyBufferToTexture = function(s, t, e) {
                                        return copy.call(this, {...s, offset : 1}, t, e);
                                    };
                                }
                                return encoder;
                            };
                        // clang-format on
                    },
                    kind == NativeCase::FinishError);
            {
                auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
                const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
                assert(!Recording::recordOriginalTextureUpload(p.recording));
                assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
                std::pmr::set_default_resource(previous);
            }
            if (kind == NativeCase::FinishError || kind == NativeCase::CopyError)
                EM_ASM({
                    // clang-format off
                        Module.preinitializedWebGPUDevice.createCommandEncoder = Module.arpTextureBadEncoder;
                        delete Module.arpTextureBadEncoder;
                    // clang-format on
                });
            assert(WebGpuAsyncWaitFixture::until([&] {
                return p.recording->status() != Recording::Status::RecordingPending;
            }));
            if (kind == NativeCase::Array) {
                assert(EM_ASM_INT({
                    // clang-format off
                        return Module.arpTextureNativeEncoderCount === 1 &&
                               Module.arpTextureNativeCopies.length === 2 && Module.arpTextureNativeWrites === 0 &&
                               Module.arpTextureNativeLabels.length === 2;
                    // clang-format on
                }) == 1);
                for (std::size_t i = 0u; i < transaction->decisions().size(); ++i) {
                    const auto& d = transaction->decisions()[i];
                    assert(EM_ASM_INT(
                               {
                                   // clang-format off
                                       const c = Module.arpTextureNativeCopies[$0];
                                       return c.buffer === WebGPU.getJsObject(Number($1)) &&
                                              c.texture === WebGPU.getJsObject(Number($2)) && c.offset === Number($3) &&
                                              c.bytesPerRow === $4 && c.rowsPerImage === $5 && c.mip === $6 && c.aspect === "all" &&
                                              c.origin.x === $7 && c.origin.y === $8 && c.origin.z === $9 && c.extent.width === $10 &&
                                              c.extent.height === $11 && c.extent.depthOrArrayLayers === $12;
                                   // clang-format on
                               },
                               i,
                               p.source->resource().buffer->buffer,
                               p.source->destination()->resource().texture->texture,
                               d.original.bufferOffsetBytes,
                               d.original.bytesPerRow,
                               d.original.rowsPerImage,
                               d.original.subresources.baseMipLevel,
                               d.physicalOrigin.width,
                               d.physicalOrigin.height,
                               d.physicalOrigin.depth,
                               d.physicalExtent.width,
                               d.physicalExtent.height,
                               d.physicalExtent.depth)
                        == 1);
                }
                EM_ASM({
                    // clang-format off
                        Module.preinitializedWebGPUDevice.createCommandEncoder = Module.arpTextureNativeEncoder;
                        Module.preinitializedWebGPUDevice.queue.writeTexture = Module.arpTextureNativeWrite;
                        delete Module.arpTextureNativeEncoder;
                        delete Module.arpTextureNativeWrite;
                        delete Module.arpTextureNativeCopies;
                    // clang-format on
                });
            }
            if (kind == NativeCase::FinishError || kind == NativeCase::CopyError) {
                assert(p.recording->status() == Recording::Status::Rejected
                    && !p.recording->hasRecordedCommand()
                    && transaction->status() == Transaction::Status::Abandoned);
                assert(p.source->destination()->contentLedger()->state(*p.source->packing())
                        == CS::Uninitialized
                    && p.source->validate());
                assert(!Submission::prepare(p.recording,
                    {1u, true, 1u, Submission::callbackCount, cpuBytes},
                    p.nativeQuotas[3],
                    diagnostic));
                transaction.reset();
                p.retire();
                continue;
            }
            assert(p.recording->hasRecordedCommand()
                && transaction->status() == Transaction::Status::Pending);
            p.submission = Submission::prepare(p.recording,
                {1u, true, 1u, Submission::callbackCount, cpuBytes},
                p.nativeQuotas[3],
                diagnostic);
            assert(p.submission);
            if (kind == NativeCase::Stale) {
                core::MutationJournal pending;
                assert(!p.source->resource().buffer->recordState(
                    rhi::ResourceState::CopySrc, &pending));
                assert(
                    Submission::submit(p.submission) && !p.submission->queueCompletionObserved());
                pending.rollback();
                assert(Submission::submit(p.submission)
                    && p.submission->status() == Submission::Status::Ready);
                transaction.reset();
                p.retire();
                continue;
            }
            NativePromises::holdActualPromises();
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Submission::submit(p.submission)
                && transaction->status() == Transaction::Status::Submitted
                && !p.signal->isComplete());
            assert(std::string_view(Transaction::validateCompletedDeclarationOriginal(transaction))
                == "RHI-WEBGPU-TEXTURE-COMPLETED-STATUS");
            std::shared_ptr<const CompletedView> submittedView = transaction;
            assert(std::string_view(CompletedView::validateOriginal(submittedView))
                == "RHI-WEBGPU-TEXTURE-COMPLETED-STATUS");
            submittedView.reset();
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            NativePromises::restorePromiseMethods();
            NativePromises::waitHeldPromises();
            NativePromises::releaseQueue();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return p.submission->queueCompletionObserved();
            }));
            assert(p.submission->status() == Submission::Status::Pending && !p.signal->isComplete()
                && p.source->destination()->contentLedger()->state(*p.source->packing())
                    == CS::Submitted);
            // Read actual submitted GPU bytes while completion scopes remain held.
            verifyTargetReadback(p.source);
            auto content = p.source->destination()->contentLedger();
            auto packing = p.source->packing();
            std::weak_ptr<Source> observedSource = p.source;
            std::weak_ptr<Recording> observed = p.recording;
            std::weak_ptr<Submission> observedSubmission = p.submission;
            p.recording.reset();
            p.submission.reset();
            if (kind == NativeCase::Array) {
                transaction.reset();
                p.states.reset();
                p.source.reset();
                p.context.destination.reset();
                p.context.authoring.source.reset();
                assert(!observedSource.expired());
            }
            assert(!observed.expired() && !observedSubmission.expired());
            NativePromises::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return observedSubmission.expired();
            }));
            observed.reset();
            observedSubmission.reset();
            assert(p.signal->isComplete() && !p.owner->retained.pending());
            // Final native destination retirement invalidates its content ledger.
            // A surviving CPU ledger must never claim live completed native bytes.
            if (kind == NativeCase::Array)
                assert(observedSource.expired() && content->state(*packing) == CS::Invalidated);
            else
                assert(content->state(*packing) == CS::Completed
                    && transaction->status() == Transaction::Status::Completed
                    && !transaction->validate());
            if (kind == NativeCase::Volume)
                verifyCompletedDeclaration(transaction, p.source);
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            observedSource.reset();
            content.reset();
            packing.reset();
            if (kind == NativeCase::Array)
                std::printf(
                    "BINDLESS_RHI_TEXTURE_UPLOAD_BATCH_OK %llu %llu %llu mips2 native-fields labels2 scopes3 submitted completed target-readback untouched drop allocation0 writeTexture0\n",
                    static_cast<unsigned long long>(peaks[0]),
                    static_cast<unsigned long long>(peaks[1]),
                    static_cast<unsigned long long>(peaks[2]));
            if (kind == NativeCase::Volume)
                std::puts(
                    "BINDLESS_RHI_TEXTURE_UPLOAD_NATIVE_3D_OK nonzero-origin slices2 target-readback untouched");
            if (kind == NativeCase::BC)
                std::puts(
                    "BINDLESS_RHI_TEXTURE_UPLOAD_NATIVE_BC_OK feature-query physical-edge target-block-readback");
            transaction.reset();
            p.retire();
        }
        std::puts(
            "BINDLESS_RHI_TEXTURE_UPLOAD_NATIVE_ERRORS_OK recording2 no-proof no-submit rollback-stale resource-stale");
        nativeQuarantine_ = core::makeUnique<NativeProbe>(NativeCase::Quarantine);
        auto& q = *nativeQuarantine_;
        q.record();
        assert(q.recording->hasRecordedCommand());
        const char* diagnostic = nullptr;
        q.submission = Submission::prepare(q.recording,
            {1u, true, 1u, Submission::callbackCount, cpuBytes},
            q.nativeQuotas[3],
            diagnostic);
        assert(q.submission);
        EM_ASM({
            // clang-format off
                Module.arpTextureDuplicateSubmit = Module.preinitializedWebGPUDevice.queue.submit;
                Module.preinitializedWebGPUDevice.queue.submit = function(buffers) {
                    return Module.arpTextureDuplicateSubmit.call(this, [ buffers[0], buffers[0] ]);
                };
            // clang-format on
        });
        assert(!Submission::submit(q.submission));
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.queue.submit = Module.arpTextureDuplicateSubmit;
                delete Module.arpTextureDuplicateSubmit;
            // clang-format on
        });
        assert(WebGpuAsyncWaitFixture::until([&] {
            return q.submission->status() == Submission::Status::Unproven;
        }));
        assert(!q.signal->isComplete() && q.owner->retained.failed()
            && q.owner->retained.pending() == 1u
            && q.recording->originalTextureTransaction()->status()
                == Transaction::Status::Invalidated);
        assert(!q.source->resource().buffer->recordedState().active()
            && !q.source->destination()->resource().texture->recordedState().active()
            && q.source->destination()->contentLedger()->state(*q.source->packing())
                == CS::Invalidated);
        q.submission.reset();
        q.recording.reset();
        q.states.reset();
        q.source.reset();
        q.context.destination.reset();
        q.context.authoring.source.reset();
        q.signal.reset();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return !q.nativeQuotas[3]->liveBytes();
        }));
        assert(q.nativeQuotas[0]->liveBytes() && q.nativeQuotas[1]->liveBytes()
            && q.context.pool->originalSourceCount() == 1u);
        std::puts(
            "BINDLESS_RHI_TEXTURE_UPLOAD_QUARANTINE_OK native-submit-error content controls2 no-signal exact-graph-retained");
    }

    static void verifyCompletedDeclaration(
        const std::shared_ptr<Transaction>& tx, const std::shared_ptr<Source>& source)
    {
        assert(tx->status() == Transaction::Status::Completed
            && !Transaction::validateCompletedDeclarationOriginal(tx));
        std::shared_ptr<const CompletedView> view = tx;
        assert(!CompletedView::validateOriginal(view));
        assert(view.get() == static_cast<const CompletedView*>(tx.get())
            && core::SharedOwnerProjection::sameOwner(view, tx)
            && view->originalMetadataQuota() == tx->originalMetadataQuota());
        assert(view->completedTexture() == source->destination()->texture()
            && &view->completedTextureResource() == &source->destination()->resource()
            && core::SharedOwnerProjection::sameOwner(
                view->completedRegistry(), source->destination()->registry())
            && core::SharedOwnerProjection::sameOwner(view->completedPacking(), source->packing())
            && view->retainedContentBytes() == source->contentSource()->retainedBytes()
            && view->retainedNativeAllocationBytes()
                == source->resource().allocationBytes
                    + source->destination()->resource().allocationBytes);
        assert(view->retainedNativeAllocationRecordCount() == 2u);
        const auto staging = view->retainedNativeAllocationRecordAt(0u),
                   targetRecord = view->retainedNativeAllocationRecordAt(1u);
        assert(core::SharedOwnerProjection::sameOwner(
                   staging.allocation, source->resource().allocationOwner())
            && core::SharedOwnerProjection::sameOwner(staging.backing, source->resource().buffer)
            && core::SharedOwnerProjection::sameOwner(
                targetRecord.allocation, source->destination()->resource().allocationOwner())
            && core::SharedOwnerProjection::sameOwner(
                targetRecord.backing, source->destination()->resource().texture)
            && staging.bytes + targetRecord.bytes == view->retainedNativeAllocationBytes()
            && !view->retainedNativeAllocationRecordAt(2u).allocation);
        auto forgedView =
            std::shared_ptr<const CompletedView>(tx->originalMetadataQuota(), view.get());
        assert(std::string_view(CompletedView::validateOriginal(forgedView))
            == "RHI-WEBGPU-TEXTURE-COMPLETED-OWNER");
        forgedView.reset();
        auto forged = std::shared_ptr<const Transaction>(tx->originalMetadataQuota(), tx.get());
        assert(std::string_view(Transaction::validateCompletedDeclarationOriginal(forged))
            == "RHI-WEBGPU-TEXTURE-COMPLETED-OWNER");
        forged.reset();
        auto& target = *source->destination()->resource().texture;
        auto extent = target.extent;
        ++target.extent.width;
        assert(Transaction::validateCompletedDeclarationOriginal(tx)
            && CompletedView::validateOriginal(view));
        target.extent = extent;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        assert(!target.recordState(rhi::ResourceState::CopySrc));
        assert(std::string_view(tx->validate()) == "RHI-WEBGPU-TEXTURE-TRANSACTION-RESOURCE-STALE");
        assert(!Transaction::validateCompletedDeclarationOriginal(tx)
            && !CompletedView::validateOriginal(view));
        core::MutationJournal journal;
        assert(!target.recordState(rhi::ResourceState::CopySrc, &journal));
        assert(!Transaction::validateCompletedDeclarationOriginal(tx));
        journal.rollback();
        assert(!Transaction::validateCompletedDeclarationOriginal(tx));
        assert(target.contentAccessPolicy().ordinaryAccessDiagnostic());
        source->destination()->contentLedger()->invalidate();
        assert(Transaction::validateCompletedDeclarationOriginal(tx)
            && CompletedView::validateOriginal(view));
        tx->invalidate();
        assert(std::string_view(Transaction::validateCompletedDeclarationOriginal(tx))
            == "RHI-WEBGPU-TEXTURE-COMPLETED-STATUS");
        assert(std::string_view(CompletedView::validateOriginal(view))
            == "RHI-WEBGPU-TEXTURE-COMPLETED-STATUS");
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
        std::pmr::set_default_resource(previous);
        std::puts(
            "BINDLESS_RHI_TEXTURE_COMPLETED_VIEW_OK sealed actual-completed same-control original-object resource registry packing budgets allocation0 access-closed");
        std::puts(
            "BINDLESS_RHI_TEXTURE_COMPLETED_DECLARATION_OK actual-completed exact-content native-mutation state-independent initial-stale ordinary-closed allocation0");
    }

    using ReadbackFixture = WebGpuCompletedTextureReadbackSourceFixture;
    using ReadbackDestinationFixture = WebGpuCompletedTextureReadbackDestinationFixture;

    static void completeOriginalUpload(NativeProbe& p)
    {
        p.record();
        const char* diagnostic = nullptr;
        std::shared_ptr<const CompletedView> pending = p.recording->originalTextureTransaction();
        {
            ReadbackFixture::Context c(pending);
            assert(!c.prepare(pending, ReadbackFixture::budget(), diagnostic)
                && std::string_view(diagnostic) == "RHI-WEBGPU-TEXTURE-COMPLETED-STATUS");
            c.retire();
        }
        p.submission = Submission::prepare(p.recording,
            {1u, true, 1u, Submission::callbackCount, cpuBytes},
            p.nativeQuotas[3],
            diagnostic);
        assert(p.submission && !Submission::submit(p.submission));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return p.signal->isComplete();
        }));
        verifyTargetReadback(p.source);
    }

    static void dropOriginalUploadCallers(NativeProbe& p)
    {
        p.recording.reset();
        p.submission.reset();
        p.states.reset();
        p.source.reset();
        p.context.destination.reset();
        p.context.authoring.source.reset();
    }

    static void verifyCompletedReadbackSource()
    {
        NativeProbe p(NativeCase::CompletedReadback);
        completeOriginalUpload(p);
        std::shared_ptr<const CompletedView> view = p.recording->originalTextureTransaction();
        ReadbackDestinationFixture::verifyActual(view);
        ReadbackFixture::verifyActual(view);
        ReadbackDestinationFixture::verifyCurrent(view);
        ReadbackDestinationFixture::withInitialReadbackStates(view, [&](const auto& states) {
            WebGpuOriginalTextureReadbackRecordingFixture::verifyActual(states, p.owner);
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> q;
            for (auto& quota : q)
                quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            const char* diagnostic = nullptr;
            auto rec = Recording::prepareOriginalTextureReadback(states,
                p.owner,
                WebGpuOriginalTextureReadbackRecordingFixture::budget(),
                q[0],
                q[1],
                q[2],
                diagnostic);
            assert(rec);
            WebGpuOriginalTextureReadbackMappingFixture::verifyActual(
                rec->originalReadbackCopies());
            rec.reset();
            for (const auto& quota : q)
                assert(!quota->liveBytes());
        });
        for (unsigned mode = 0u; mode < 4u; ++mode)
            ReadbackDestinationFixture::withInitialReadbackStates(view, [&](const auto& states) {
                WebGpuOriginalTextureReadbackRecordingFixture::verifyRecordingActual(
                    states, p.owner, mode);
            });
        for (unsigned order = 0u; order < 2u; ++order)
            ReadbackDestinationFixture::withInitialReadbackStates(view, [&](const auto& states) {
                WebGpuOriginalTextureReadbackCompletionFixture::verifyActual(
                    states, p.owner, view->completedPacking(), order);
            });
        ReadbackDestinationFixture::withInitialReadbackStates(view, [&](const auto& states) {
            WebGpuOriginalTextureReadbackCompletionFixture::verifyLowered(
                states, p.owner, view->completedPacking());
        });
        for (unsigned order = 0u; order < 2u; ++order)
            WebGpuOriginalTextureReadbackCompletionFixture::verifyEarly(view, p.owner, order);
        for (unsigned mode : {0u, 2u, 6u})
            WebGpuOriginalTextureReadbackCompletionFixture::verifyEarlyFailure(view, p.owner, mode);
        WebGpuTextureReadbackSnapshotFixture::verifyActual(p.owner);
        for (unsigned mode = 2u; mode <= 7u; ++mode)
            ReadbackDestinationFixture::withInitialReadbackStates(view, [&](const auto& states) {
                std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> q;
                for (auto& quota : q)
                    quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
                const char* diagnostic = nullptr;
                auto rec = Recording::prepareOriginalTextureReadback(states,
                    p.owner,
                    WebGpuOriginalTextureReadbackRecordingFixture::budget(),
                    q[0],
                    q[1],
                    q[2],
                    diagnostic);
                assert(rec);
                WebGpuOriginalTextureReadbackNativeMappingFixture::verifyFailure(
                    rec, view->completedPacking(), mode);
                rec.reset();
                for (const auto& quota : q)
                    assert(!quota->liveBytes());
            });
        verifyNativeMapDrop();
        ReadbackFixture::Context c(view);
        const char* diagnostic = nullptr;
        auto source = c.prepare(view, ReadbackFixture::budget(), diagnostic);
        assert(source);
        const auto* original = view->completedTexture();
        dropOriginalUploadCallers(p);
        view.reset();
        assert(!source->validate() && source->sourceTexture() == original
            && source->registry()->find(original->id()));
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> quotas;
        for (auto& q : quotas)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto destination = ReadbackDestinationFixture::Destination::prepareCompletedOriginal(source,
            ReadbackDestinationFixture::budget(),
            quotas[0],
            quotas[1],
            quotas[2],
            diagnostic);
        assert(destination);
        assert(!ReadbackDestinationFixture::Destination::initialize(destination));
        ReadbackDestinationFixture::waitReady(destination);
        source.reset();
        c.layout.reset();
        assert(!destination->validate() && destination->source()->sourceTexture() == original);
        auto pairQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto pair = WebGpuOriginalTextureReadbackPairFixture::Pair::prepareOriginal(
            destination, WebGpuOriginalTextureReadbackPairFixture::budget(), pairQuota, diagnostic);
        assert(pair);
        WebGpuOriginalTextureReadbackStateIntentFixture::Quotas intentQuotas;
        for (auto& q : intentQuotas)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto intentStates = WebGpuOriginalTextureReadbackStateIntentFixture::prepare(pair,
            WebGpuOriginalTextureReadbackStateIntentFixture::barrier(pair),
            intentQuotas,
            diagnostic);
        assert(intentStates);
        destination.reset();
        assert(!pair->validate() && pair->destination()->source()->sourceTexture() == original);
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto graph = WebGpuOriginalTextureReadbackProjectionFixture::prepare(
            pair, WebGpuOriginalTextureReadbackProjectionFixture::budget(), graphQuota, diagnostic);
        assert(graph);
        pair.reset();
        assert(!graph->validate()
            && graph->originalReadbacks()[0]->destination()->source()->sourceTexture() == original);
        WebGpuOriginalTextureReadbackInputsFixture::Quotas inputQuotas;
        for (auto& q : inputQuotas)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto states =
            WebGpuOriginalTextureReadbackInputsFixture::prepare(graph, inputQuotas, false);
        graph.reset();
        assert(!states->validate()
            && states->inputs()
                    ->scalars()
                    ->usage()
                    ->projection()
                    ->originalReadbacks()[0]
                    ->destination()
                    ->source()
                    ->sourceTexture()
                == original);
        assert(!states->inputs()
                ->scalars()
                ->usage()
                ->projection()
                ->originalReadbacks()[0]
                ->validateReadyDeclaration());
        assert(!rhi_webgpu::WebGpuOriginalTransferStateValidation::validate(intentStates, 0u)
                .diagnostic);
        auto currentQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto current =
            WebGpuOriginalReadbackStateTransactionFixture::prepare(intentStates, currentQuota);
        auto copyQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto copies = WebGpuOriginalTextureReadbackCopiesFixture::Copies::prepareOriginal(
            current, WebGpuOriginalTextureReadbackCopiesFixture::budget(), copyQuota, diagnostic);
        assert(copies);
        assert(!WebGpuOriginalReadbackStateTransactionFixture::Transaction::apply(current));
        current.reset();
        intentStates.reset();
        states.reset();
        assert(!copies->validate()
            && copies->transaction()
                    ->states()
                    ->inputs()
                    ->scalars()
                    ->usage()
                    ->projection()
                    ->originalReadbacks()[0]
                    ->destination()
                    ->source()
                    ->sourceTexture()
                == original);
        copies.reset();
        assert(!copyQuota->liveBytes() && !currentQuota->liveBytes());
        for (const auto& q : intentQuotas)
            assert(!q->liveBytes());
        states.reset();
        for (const auto& q : inputQuotas)
            assert(!q->liveBytes());
        assert(!graphQuota->liveBytes() && !pairQuota->liveBytes());
        for (const auto& q : quotas)
            assert(!q->liveBytes());
        c.retire();
        p.retire();
        std::puts(
            "BINDLESS_RHI_READBACK_INPUTS_DROP_OK state-owner-only full-original-graph texture registry private-buffer exact-retire");
        std::puts(
            "BINDLESS_RHI_READBACK_DECLARATION_DROP_OK state-owner-only immutable-original full-graph exact-retire");
        std::puts(
            "BINDLESS_RHI_READBACK_STATE_INTENT_DROP_OK full-caller-drop original-sequence exact-retire no-proof");
        std::puts(
            "BINDLESS_RHI_READBACK_CURRENT_DROP_OK transaction-only full-original-graph pending-destructor-rollback exact-retire");
        std::puts(
            "BINDLESS_RHI_READBACK_COPIES_DROP_OK copies-only current-original-graph destructor-rollback exact-retire");
        std::puts(
            "BINDLESS_RHI_READBACK_PROJECTION_DROP_OK graph-only retains-original-texture registry layout private-buffer exact-retire");
        std::puts(
            "BINDLESS_RHI_READBACK_PAIR_DROP_OK pair-only original-object registry layout destination graph exact-retire");
        std::puts(
            "BINDLESS_RHI_COMPLETED_READBACK_DESTINATION_DROP_OK destination-only original-upload-graph exact-retire unpublished");
        std::puts(
            "BINDLESS_RHI_COMPLETED_READBACK_SOURCE_DROP_OK source-only retains-original-object registry graph exact-retire");
        NativeProbe invalidation(NativeCase::ReadbackCurrentInvalidation);
        completeOriginalUpload(invalidation);
        std::shared_ptr<const CompletedView> invalidationView =
            invalidation.recording->originalTextureTransaction();
        ReadbackDestinationFixture::withInitialReadbackStates(
            invalidationView, WebGpuOriginalReadbackStateTransactionFixture::verifyInvalidation);
        invalidationView.reset();
        invalidation.retire();
        verifyColdReadbackDrop();
        verifyColdMapDrop();
    }

    class ReadbackRecordingProbe final {
    public:
        NativeProbe upload;
        std::shared_ptr<const CompletedView> view;
        core::UniquePtr<ReadbackDestinationFixture::Context> context;
        std::shared_ptr<ReadbackDestinationFixture::Destination> destination;
        std::shared_ptr<const WebGpuOriginalTextureReadbackPairFixture::Pair> pair;
        std::shared_ptr<core::CpuMemoryQuota> pairQuota;
        WebGpuOriginalTextureReadbackStateIntentFixture::Quotas inputQuotas;
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates> states;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> quotas;
        std::shared_ptr<Recording> recording;

        explicit ReadbackRecordingProbe(NativeCase kind) : upload(kind)
        {
            completeOriginalUpload(upload);
            view = upload.recording->originalTextureTransaction();
            context = core::makeUnique<ReadbackDestinationFixture::Context>(view);
            const char* diagnostic = nullptr;
            destination = context->prepare(ReadbackDestinationFixture::budget(), diagnostic);
            assert(destination);
            assert(!ReadbackDestinationFixture::Destination::initialize(destination));
            ReadbackDestinationFixture::waitReady(destination);
            pairQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            pair = WebGpuOriginalTextureReadbackPairFixture::Pair::prepareOriginal(destination,
                WebGpuOriginalTextureReadbackPairFixture::budget(),
                pairQuota,
                diagnostic);
            assert(pair);
            for (auto& q : inputQuotas)
                q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            states = WebGpuOriginalTextureReadbackStateIntentFixture::prepare(pair,
                WebGpuOriginalTextureReadbackStateIntentFixture::barrier(pair),
                inputQuotas,
                diagnostic);
            assert(states);
            for (auto& q : quotas)
                q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            recording = Recording::prepareOriginalTextureReadback(states,
                upload.owner,
                WebGpuOriginalTextureReadbackRecordingFixture::budget(),
                quotas[0],
                quotas[1],
                quotas[2],
                diagnostic);
            assert(recording);
        }

        void dropCallers()
        {
            dropOriginalUploadCallers(upload);
            view.reset();
            states.reset();
            pair.reset();
            destination.reset();
            context->source.reset();
            context->authoring.layout.reset();
        }

        void retire()
        {
            recording.reset();
            dropCallers();
            for (const auto& q : quotas)
                assert(!q->liveBytes());
            for (const auto& q : inputQuotas)
                assert(!q->liveBytes());
            assert(!pairQuota->liveBytes());
            context->retire();
            context.reset();
            upload.retire();
        }
    };

    static void verifyColdReadbackDrop()
    {
        ReadbackRecordingProbe p(NativeCase::ReadbackColdDrop);
        const auto* original = p.view->completedTexture();
        p.dropCallers();
        const auto& retained = p.recording->originalReadbackCopies()
                                   ->transaction()
                                   ->states()
                                   ->inputs()
                                   ->scalars()
                                   ->usage()
                                   ->projection()
                                   ->originalReadbacks()[0];
        assert(retained->destination()->source()->sourceTexture() == original
            && retained->destination()->source()->registry()->find(original->id()));
        assert(p.recording->status() == Recording::Status::Prepared
            && !p.recording->hasRecordedCommand() && !retained->destination()->resultPublished());
        assert(std::string_view(p.recording->diagnostic())
            == "RHI-WEBGPU-ORDINARY-RECORDING-UNRECORDED");
        p.retire();
        std::puts(
            "BINDLESS_RHI_READBACK_RECORDING_COLD_DROP_OK recorder-only full-original-graph exact-retire unrecorded-unpublished");
    }

    static void verifyColdMapDrop()
    {
        ReadbackRecordingProbe p(NativeCase::ReadbackColdMapDrop);
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 2> q;
        for (auto& quota : q)
            quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* diagnostic = nullptr;
        auto mapping = WebGpuOriginalTextureReadbackMappingFixture::Mapping::prepareOriginal(
            p.recording->originalReadbackCopies(),
            WebGpuOriginalTextureReadbackMappingFixture::budget(),
            q[0],
            q[1],
            diagnostic);
        assert(mapping);
        p.dropCallers();
        p.recording.reset();
        const auto& pair = mapping->copies()
                               ->transaction()
                               ->states()
                               ->inputs()
                               ->scalars()
                               ->usage()
                               ->projection()
                               ->originalReadbacks()[0];
        assert(!pair->validateReadyDeclaration() && !mapping->resultDeclaration()->publishedBytes()
            && WebGpuOriginalTextureReadbackMappingFixture::Mapping::start(mapping));
        mapping.reset();
        for (const auto& quota : q)
            assert(!quota->liveBytes());
        p.retire();
        std::puts(
            "BINDLESS_RHI_READBACK_COLD_MAP_DROP_OK map-only original-native-cpu-graph exclusive-ticket exact-retire unpublished");
    }

    static void verifyNativeMapDrop()
    {
        using Fixture = WebGpuOriginalTextureReadbackNativeMappingFixture;
        ReadbackRecordingProbe p(NativeCase::ReadbackNativeMapDrop);
        auto packing = p.view->completedPacking();
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 2> q;
        auto mapping = Fixture::prepare(p.recording, q);
        Fixture::completeOriginal(p.recording);
        const auto buffer = p.destination->nativeResource()->buffer;
        Fixture::MapPromises::install(buffer, 0u, true);
        Fixture::Scopes::holdScopes();
        assert(!Fixture::Mapping::start(mapping));
        Fixture::MapPromises::restoreMap();
        Fixture::Scopes::restoreScopes();
        Fixture::MapPromises::waitHeld();
        Fixture::Scopes::waitHeldScopes();
        auto result = mapping->resultDeclaration();
        std::weak_ptr<Fixture::Mapping> weak = mapping;
        p.dropCallers();
        p.recording.reset();
        mapping.reset();
        assert(!weak.expired() && !result->publishedBytes());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        Fixture::MapPromises::release();
        Fixture::Scopes::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return weak.expired();
        }));
        weak.reset();
        assert(result->publishedBytes()
            && !rhi_webgpu::WebGpuOriginalTextureReadbackResult::validatePublishedOriginal(result)
            && core::defaultCpuAllocator().stats().allocationCount == allocations);
        auto forgedResult = std::shared_ptr<const rhi_webgpu::WebGpuOriginalTextureReadbackResult>(
            q[1], result.get());
        assert(std::string_view(
                   rhi_webgpu::WebGpuOriginalTextureReadbackResult::validatePublishedOriginal(
                       forgedResult))
            == "RHI-WEBGPU-READBACK-RESULT-OWNER");
        forgedResult.reset();
        Fixture::verifyRows(result->publishedBytes(), result->layout(), packing);
        Fixture::MapPromises::restoreAll();
        assert(!q[0]->liveBytes() && q[1]->liveBytes());
        result.reset();
        packing.reset();
        for (const auto& quota : q)
            assert(!quota->liveBytes());
        p.retire();
        std::puts(
            "BINDLESS_RHI_READBACK_NATIVE_MAP_DROP_OK callbacks-only then-result-only full-original-graph immutable-bytes exact-retire allocation0");
    }

public:
    static void activatePendingNativeMapLoss()
    {
        WebGpuOriginalTextureReadbackNativeMappingFixture::activatePendingLoss();
    }

private:
    static void prepareCompletedReadbackSourceLoss()
    {
        completedReadbackLoss_ = core::makeUnique<NativeProbe>(NativeCase::CompletedReadbackLoss);
        auto& p = *completedReadbackLoss_;
        completeOriginalUpload(p);
        std::shared_ptr<const CompletedView> view = p.recording->originalTextureTransaction();
        completedReadbackContext_ = core::makeUnique<ReadbackFixture::Context>(view);
        const char* diagnostic = nullptr;
        completedReadbackSource_ =
            completedReadbackContext_->prepare(view, ReadbackFixture::budget(), diagnostic);
        assert(completedReadbackSource_);
        ReadbackDestinationFixture::prepareActualLoss(view);
        WebGpuOriginalTextureReadbackCompletionFixture::prepareEarlyLoss(view);
        WebGpuTextureReadbackSnapshotFixture::prepareLoss(p.owner);
        dropOriginalUploadCallers(p);
        view.reset();
        assert(!completedReadbackSource_->validate());
        ReadbackDestinationFixture::activateCurrentLoss(p.owner);
        readbackRecordingLoss_ =
            core::makeUnique<ReadbackRecordingProbe>(NativeCase::ReadbackRecordingLoss);
        WebGpuOriginalTextureReadbackRecordingFixture::prepareRecordingLoss(
            readbackRecordingLoss_->recording);
        readbackRecordingLoss_->dropCallers();
        readbackColdMapLoss_ =
            core::makeUnique<ReadbackRecordingProbe>(NativeCase::ReadbackColdMapLoss);
        WebGpuOriginalTextureReadbackMappingFixture::prepareLoss(
            readbackColdMapLoss_->recording->originalReadbackCopies());
        readbackColdMapLoss_->dropCallers();
        readbackColdMapLoss_->recording.reset();
        const std::array<NativeCase, 4> cases{{NativeCase::ReadbackCompletionPendingLoss,
            NativeCase::ReadbackCompletionQuarantine,
            NativeCase::ReadbackCompletionCompletedLoss,
            NativeCase::ReadbackCompletionMutation}};
        for (unsigned mode = 0u; mode < cases.size(); ++mode) {
            readbackCompletionLoss_[mode] = core::makeUnique<ReadbackRecordingProbe>(cases[mode]);
            WebGpuOriginalTextureReadbackCompletionFixture::prepareLoss(
                readbackCompletionLoss_[mode]->recording, mode);
            readbackCompletionLoss_[mode]->dropCallers();
            readbackCompletionLoss_[mode]->recording.reset();
        }
        const std::array<NativeCase, 3> mapCases{{NativeCase::ReadbackNativeMapPendingLoss,
            NativeCase::ReadbackNativeMapPostMapLoss,
            NativeCase::ReadbackNativeMapSnapshotLoss}};
        for (unsigned mode = 0u; mode < mapCases.size(); ++mode) {
            readbackNativeMapLoss_[mode] = core::makeUnique<ReadbackRecordingProbe>(mapCases[mode]);
            WebGpuOriginalTextureReadbackNativeMappingFixture::prepareLoss(
                readbackNativeMapLoss_[mode]->recording,
                readbackNativeMapLoss_[mode]->view->completedPacking(),
                mode);
            readbackNativeMapLoss_[mode]->dropCallers();
            readbackNativeMapLoss_[mode]->recording.reset();
        }
    }

    static void verifyCompletedReadbackSourceLoss()
    {
        WebGpuOriginalTextureReadbackCompletionFixture::verifyEarlyLoss();
        WebGpuTextureReadbackSnapshotFixture::verifyLoss();
        for (unsigned mode = 0u; mode < readbackNativeMapLoss_.size(); ++mode) {
            WebGpuOriginalTextureReadbackNativeMappingFixture::verifyLoss(mode);
            readbackNativeMapLoss_[mode]->retire();
            readbackNativeMapLoss_[mode].reset();
        }
        WebGpuOriginalTextureReadbackMappingFixture::verifyLoss();
        readbackColdMapLoss_->retire();
        readbackColdMapLoss_.reset();
        for (unsigned mode = 0u; mode < readbackCompletionLoss_.size(); ++mode) {
            WebGpuOriginalTextureReadbackCompletionFixture::verifyLoss(mode);
            readbackCompletionLoss_[mode]->retire();
            readbackCompletionLoss_[mode].reset();
        }
        WebGpuOriginalTextureReadbackRecordingFixture::verifyRecordingLoss(
            readbackRecordingLoss_->recording);
        readbackRecordingLoss_->retire();
        readbackRecordingLoss_.reset();
        assert(std::string_view(completedReadbackSource_->validate())
            == "RHI-READBACK-SOURCE-DEVICE-LOST");
        ReadbackDestinationFixture::verifyActualDeviceLoss();
        completedReadbackSource_.reset();
        completedReadbackContext_->retire();
        completedReadbackContext_.reset();
        completedReadbackLoss_->retire();
        completedReadbackLoss_.reset();
        std::puts(
            "BINDLESS_RHI_COMPLETED_READBACK_SOURCE_LOSS_OK source-only actual-lost reject exact-native-cpu-retire");
    }

    static void prepareCompletedDeclarationLoss()
    {
        completedDeclarationLoss_ =
            core::makeUnique<NativeProbe>(NativeCase::CompletedDeclarationLoss);
        auto& p = *completedDeclarationLoss_;
        p.record();
        const char* diagnostic = nullptr;
        p.submission = Submission::prepare(p.recording,
            {1u, true, 1u, Submission::callbackCount, cpuBytes},
            p.nativeQuotas[3],
            diagnostic);
        assert(p.submission && !Submission::submit(p.submission));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return p.signal->isComplete();
        }));
        completedDeclarationTransaction_ = p.recording->originalTextureTransaction();
        assert(
            !Transaction::validateCompletedDeclarationOriginal(completedDeclarationTransaction_));
        completedDeclarationView_ = completedDeclarationTransaction_;
        ReadbackFixture::verifyNonzeroOrigin(completedDeclarationView_);
        p.recording.reset();
        p.submission.reset();
        p.states.reset();
        p.source.reset();
        p.context.destination.reset();
        p.context.authoring.source.reset();
        assert(
            !Transaction::validateCompletedDeclarationOriginal(completedDeclarationTransaction_));
        completedDeclarationTransaction_.reset();
        assert(!CompletedView::validateOriginal(completedDeclarationView_));
    }

    static void verifyCompletedDeclarationLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost() && completedDeclarationView_);
        assert(std::string_view(CompletedView::validateOriginal(completedDeclarationView_))
            == "RHI-TEXTURE-SOURCE-DEVICE-LOST");
        completedDeclarationView_.reset();
        completedDeclarationTransaction_.reset();
        completedDeclarationLoss_->retire();
        completedDeclarationLoss_.reset();
        std::puts(
            "BINDLESS_RHI_TEXTURE_COMPLETED_VIEW_LOSS_OK view-only caller-drop actual-lost reject exact-retire access-closed");
        std::puts(
            "BINDLESS_RHI_TEXTURE_COMPLETED_DECLARATION_LOSS_OK actual-completed caller-drop lost-reject exact-retire no-readback-authority");
    }

    static void prepareNativeUploadLoss()
    {
        nativeLoss_ = core::makeUnique<NativeProbe>(NativeCase::Loss);
        auto& p = *nativeLoss_;
        p.record();
        assert(p.recording->hasRecordedCommand());
        const char* diagnostic = nullptr;
        p.submission = Submission::prepare(p.recording,
            {1u, true, 1u, Submission::callbackCount, cpuBytes},
            p.nativeQuotas[3],
            diagnostic);
        assert(p.submission);
        NativePromises::holdActualPromises();
        assert(!Submission::submit(p.submission));
        NativePromises::restorePromiseMethods();
        NativePromises::waitHeldPromises();
        assert(p.submission->status() == Submission::Status::Pending && !p.signal->isComplete()
            && p.recording->originalTextureTransaction()->status()
                == Transaction::Status::Submitted);
    }

    static void verifyNativeUploadLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost());
        auto& p = *nativeLoss_;
        assert(p.submission->status() == Submission::Status::DeviceLost && !p.signal->isComplete()
            && p.recording->originalTextureTransaction()->status()
                == Transaction::Status::Invalidated);
        assert(!p.source->resource().buffer->recordedState().active()
            && !p.source->destination()->resource().texture->recordedState().active());
        NativePromises::releaseQueue();
        NativePromises::releaseScopes();
        p.retire();
        nativeLoss_.reset();
        nativeQuarantine_->retire();
        nativeQuarantine_.reset();
        for (std::size_t i = 0u; i < nativeCases; ++i)
            if (nativeOwnerQuotas_[i]) {
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return nativeOwners_[i].expired();
                }));
                nativeOwners_[i].reset();
                assert(!nativeOwnerQuotas_[i]->liveBytes());
                nativeOwnerQuotas_[i].reset();
            }
        std::puts(
            "BINDLESS_RHI_TEXTURE_UPLOAD_BATCH_LOSS_OK actual-submitted pending-evidence content controls2 no-signal quarantine exact-retire");
    }

    static void readyDestination(Context& c)
    {
        const char* diagnostic = nullptr;
        c.destination = WebGpuOriginalTextureUploadDestinationFixture::prepareDestination(
            c.authoring, diagnostic);
        assert(c.destination);
        assert(!Destination::initialize(c.destination));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return c.destination->status() != Destination::Status::Pending;
        }));
        assert(c.destination->status() == Destination::Status::CreationReady
            && !c.destination->validate());
    }

    static void waitReady(const std::shared_ptr<Source>& s)
    {
        assert(WebGpuAsyncWaitFixture::until([&] {
            return s->status() != Source::Status::Pending;
        }));
        if (s->validate())
            std::printf("TEXTURE_SOURCE_FAILURE %s\n", s->validate());
        assert(s->status() == Source::Status::Ready && !s->validate());
    }

    static void verifyCold()
    {
        Context c(2u, 2u * packedBytes);
        const char* diagnostic = nullptr;
        c.destination = WebGpuOriginalTextureUploadDestinationFixture::prepareDestination(
            c.authoring, diagnostic);
        assert(c.destination);
        assert(!prepare(c, budget(), diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-DESTINATION-UNINITIALIZED");
        assert(!Destination::initialize(c.destination));
        assert(!prepare(c, budget(), diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-DESTINATION-PENDING");
        assert(WebGpuAsyncWaitFixture::until([&] {
            return c.destination->status() == Destination::Status::CreationReady;
        }));
        auto b = budget();
        const auto factory = [&] {
            return prepare(c, b, diagnostic);
        };
        std::array<std::uint64_t, 4> peaks{};
        EM_ASM({
            // clang-format off
                Module.arpTextureSourceCreates = 0;
                Module.arpTextureSourceCreateOriginal = Module.preinitializedWebGPUDevice.createBuffer;
                Module.preinitializedWebGPUDevice.createBuffer = function(d) {
                    Module.arpTextureSourceCreates++;
                    return Module.arpTextureSourceCreateOriginal.call(this, d);
                };
            // clang-format on
        });
        for (std::size_t i = 0u; i < peaks.size(); ++i) {
            CpuQuotaFactoryFixture admission(c.quotas[i]);
            peaks[i] = admission.measure(factory);
            admission.verify(peaks[i], factory);
            c.coldEmpty();
            {
                core::CpuMemoryQuotaLease pressure(c.quotas[i], cpuBytes);
                assert(pressure.charged() && !factory());
            }
            c.coldEmpty();
        }
        for (auto field : {&Budget::maxRetainedContentBytes,
                 &Budget::maxRetainedDestinationTexelBytes,
                 &Budget::maxPackedBytes,
                 &Budget::maxCpuMetadataBytes}) {
            --(b.*field);
            assert(!factory());
            ++(b.*field);
            c.coldEmpty();
        }
        for (auto field : {&rhi_webgpu::WebGpuOriginalBufferUploadSourceBudget::maxCpuMetadataBytes,
                 &rhi_webgpu::WebGpuOriginalBufferUploadSourceBudget::maxCpuPayloadBytes,
                 &rhi_webgpu::WebGpuOriginalBufferUploadSourceBudget::maxCpuResourceBytes}) {
            --(b.buffer.*field);
            assert(!factory());
            ++(b.buffer.*field);
            c.coldEmpty();
        }
        auto original = c.destination;
        c.destination = std::shared_ptr<Destination>(c.quotas[0], original.get());
        assert(
            !factory() && std::string_view(diagnostic) == "RHI-TEXTURE-SOURCE-DESTINATION-OWNER");
        c.destination = std::move(original);
        auto pool = c.pool;
        c.pool = std::shared_ptr<rhi_webgpu::WebGpuRhiStagingUploadPool>(c.quotas[0], pool.get());
        assert(!factory() && std::string_view(diagnostic) == "RHI-WEBGPU-UPLOAD-SOURCE-POOL-OWNER");
        c.pool = std::move(pool);
        auto first = factory();
        auto second = factory();
        assert(first && second);
        assert(!factory()
            && std::string_view(diagnostic) == "RHI-WEBGPU-UPLOAD-SOURCE-COUNT-CAPACITY");
        first.reset();
        second.reset();
        c.coldEmpty();
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpTextureSourceCreates;
            // clang-format on
        }) == 0);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createBuffer = Module.arpTextureSourceCreateOriginal;
                delete Module.arpTextureSourceCreateOriginal;
            // clang-format on
        });
        c.retire();
        Context limited(2u, packedBytes);
        readyDestination(limited);
        auto held = prepare(limited, budget(), diagnostic);
        assert(held);
        assert(!prepare(limited, budget(), diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-UPLOAD-SOURCE-GPU-BUDGET");
        held.reset();
        limited.retire();
        Context tooSmall(1u, packedBytes - 1u);
        readyDestination(tooSmall);
        assert(!prepare(tooSmall, budget(), diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-UPLOAD-SOURCE-GPU-BUDGET");
        tooSmall.retire();
        std::printf(
            "BINDLESS_RHI_TEXTURE_SOURCE_COLD_OK %llu %llu %llu %llu packed1536 quotas4 exact-less-exhausted\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]),
            static_cast<unsigned long long>(peaks[2]),
            static_cast<unsigned long long>(peaks[3]));
    }

    static void verifyReady()
    {
        Context c;
        readyDestination(c);
        const char* diagnostic = nullptr;
        auto s = prepare(c, budget(), diagnostic);
        assert(s);
        assert(s->status() == Source::Status::Prepared && !s->resource().id.isValid()
            && s->bufferSource()->reservedResourceId().isValid());
        rhi::semantics::TextureUploadPackedCopy output, sentinel;
        sentinel.contentGeneration = UINT64_MAX;
        output = sentinel;
        const auto& originalInput = s->packing()->inputOwner();
        assert(std::string_view(Source::mapPackedCopyOriginal(s, originalInput, 0u, output))
                == "RHI-WEBGPU-UPLOAD-SOURCE-UNINITIALIZED"
            && output.matches(sentinel));
        assert(s->bufferSource()->frameSerial() == 53u
            && s->contentSource()->claim().contentGeneration == 59u);
        const auto extent = c.destination->resource().texture->extent;
        ++c.destination->resource().texture->extent.width;
        assert(Source::initialize(s));
        c.destination->resource().texture->extent = extent;
        auto forged = std::shared_ptr<Source>(c.quotas[0], s.get());
        assert(std::string_view(Source::initialize(forged)) == "RHI-TEXTURE-SOURCE-OWNER");
        forged.reset();
        Promises::holdActualPromises();
        assert(!Source::initialize(s));
        Promises::restorePromiseMethods();
        Promises::waitHeldPromises();
        assert(s->status() == Source::Status::Pending
            && !s->bufferSource()->queueCompletionObserved());
        ++c.destination->resource().texture->extent.width;
        assert(s->status() == Source::Status::Rejected && s->validate());
        c.destination->resource().texture->extent = extent;
        assert(std::string_view(Source::mapPackedCopyOriginal(s, originalInput, 0u, output))
                == "RHI-WEBGPU-UPLOAD-SOURCE-PENDING"
            && output.matches(sentinel));
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return s->bufferSource()->errorScopesObserved();
        }));
        assert(s->status() == Source::Status::Pending);
        Promises::releaseQueue();
        waitReady(s);
        verifyPackedMapping(s);
        WebGpuOriginalBufferUploadSourceFixture::verifyPackedReadback(
            s->bufferSource(), s->packing()->packedBytes().data(), packedBytes);
        assert(std::string_view(s->executionDiagnostic())
            == "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN");
        assert(c.destination->contentLedger()->state(*s->packing())
            == rhi::TextureUploadContentLedger::ContentState::Uninitialized);
        ++c.destination->resource().texture->extent.width;
        assert(s->validate());
        c.destination->resource().texture->extent = extent;
        auto device = s->resource().buffer->ownerDevice;
        s->resource().buffer->ownerDevice = nullptr;
        assert(s->validate());
        s->resource().buffer->ownerDevice = device;
        auto token = s->resource().buffer->allocationReleaseToken;
        s->resource().buffer->allocationReleaseToken.reset();
        assert(s->validate());
        s->resource().buffer->allocationReleaseToken = token;
        token.reset();
        {
            std::array<core::CpuMemoryQuotaLease, 4> pressure{
                {{c.quotas[0], static_cast<std::size_t>(cpuBytes - c.quotas[0]->liveBytes())},
                    {c.quotas[1], static_cast<std::size_t>(cpuBytes - c.quotas[1]->liveBytes())},
                    {c.quotas[2], static_cast<std::size_t>(cpuBytes - c.quotas[2]->liveBytes())},
                    {c.quotas[3], static_cast<std::size_t>(cpuBytes - c.quotas[3]->liveBytes())}}};
            for (const auto& p : pressure)
                assert(p.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!s->validate() && s->status() == Source::Status::Ready);
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            std::pmr::set_default_resource(previous);
        }
        s.reset();
        c.retire();
    }

    static void verifyPackedMapping(const std::shared_ptr<Source>& s)
    {
        using Copy = rhi::semantics::TextureUploadPackedCopy;
        Copy output, sentinel;
        sentinel.contentGeneration = UINT64_MAX;
        output = sentinel;
        const auto& input = s->packing()->inputOwner();
        assert(std::string_view(Source::mapPackedCopyOriginal({}, input, 0u, output))
                == "RHI-TEXTURE-SOURCE-OWNER"
            && output.matches(sentinel));
        auto forged = std::shared_ptr<const Source>(s->contentSource(), s.get());
        assert(std::string_view(Source::mapPackedCopyOriginal(forged, input, 0u, output))
                == "RHI-TEXTURE-SOURCE-OWNER"
            && output.matches(sentinel));
        forged.reset();
        auto foreign =
            std::shared_ptr<const rhi::TextureUploadInputSnapshot>(s->contentSource(), input.get());
        assert(std::string_view(Source::mapPackedCopyOriginal(s, foreign, 0u, output))
                == "RHI-TEXTURE-SOURCE-FOREIGN-INPUT"
            && output.matches(sentinel));
        foreign.reset();
        Authoring other;
        assert(std::string_view(Source::mapPackedCopyOriginal(
                   s, other.source->packing()->inputOwner(), 0u, output))
                == "RHI-TEXTURE-SOURCE-FOREIGN-INPUT"
            && output.matches(sentinel));
        other.retire();
        assert(std::string_view(Source::mapPackedCopyOriginal(s, input, SIZE_MAX, output))
                == "RHI-TEXTURE-PACKED-COPY-MIP-INDEX"
            && output.matches(sentinel));
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (std::size_t i = 0u; i < 2u; ++i) {
            assert(!Source::mapPackedCopyOriginal(s, input, i, output));
            const auto& mip = s->packing()->mipLayouts()[i];
            assert(output.sourceBuffer == s->resource().id
                && output.destinationTexture == s->destination()->resource().id
                && output.origin == mip.origin && output.subresources == mip.range
                && output.extent == mip.extent && output.contentGeneration == 59u
                && output.frameSerial == 53u && output.bytesPerRow == 256u);
        }
        const auto extent = s->destination()->resource().texture->extent;
        ++s->destination()->resource().texture->extent.width;
        assert(std::string_view(Source::mapPackedCopyOriginal(s, input, 0u, output))
            == "RHI-TEXTURE-DESTINATION-IDENTITY-CHANGED");
        s->destination()->resource().texture->extent = extent;
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        std::puts(
            "BINDLESS_RHI_TEXTURE_PACKED_COPY_NATIVE_BINDING_OK original-input mips2 origin generation-comparison allocation0 execution-closed");
    }

    using Graph = rhi_webgpu::WebGpuGenericCommandProjection;

    static rhi_webgpu::WebGpuGenericCommandProjectionBudget graphBudget()
    {
        rhi_webgpu::WebGpuGenericCommandProjectionBudget b;
        b.contractVersion = 1u;
        b.prototypeOptIn = true;
        b.maxReferences = 3u;
        b.maxAllocationReferences = 2u;
        b.maxCpuMetadataBytes = cpuBytes;
        b.maxRetainedAllocationBytes = packedBytes + 1360u;
        b.maxOriginalTextureUploadSources = 2u;
        b.maxTexturePackedBytes = packedBytes;
        b.maxTextureContentBytes = 1696u;
        b.maxTextureDestinationTexelBytes = 1360u;
        for (const auto domain : {rhi::CommandReferenceDomain::Texture,
                 rhi::CommandReferenceDomain::Upload,
                 rhi::CommandReferenceDomain::Buffer})
            b.maxDomainReferences[static_cast<std::size_t>(domain)] = 1u;
        return b;
    }

    static std::shared_ptr<const rhi_webgpu::WebGpuGenericCommandProjection> prepareGraph(
        const std::shared_ptr<Source>& source,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic)
    {
        using namespace rhi_webgpu;
        using D = rhi::CommandReferenceDomain;
        using S = WebGpuCommandReferenceSource;
        std::shared_ptr<const Source> original = source;
        Graph::Sources sources;
        sources.textureUploads = &original;
        sources.textureUploadCount = 1u;
        const auto destination = source->destination()->resource().id;
        const std::array<WebGpuCommandReference, 3> refs{
            {{D::Texture, S::OriginalTextureUpload, destination},
                {D::Upload, S::OriginalTextureUpload, destination},
                {D::Buffer, S::OriginalTextureUpload, source->resource().id}}};
        return Graph::prepareOriginalShared(
            std::move(sources), refs.data(), refs.size(), graphBudget(), quota, diagnostic);
    }

    static void verifyProjection()
    {
        using namespace rhi_webgpu;
        using D = rhi::CommandReferenceDomain;
        using S = WebGpuCommandReferenceSource;
        Context c(2u, 2u * packedBytes);
        readyDestination(c);
        const char* diagnostic = nullptr;
        auto source = prepare(c, budget(), diagnostic);
        assert(source);
        auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        assert(!prepareGraph(source, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-UPLOAD-SOURCE-UNINITIALIZED");
        Promises::holdActualPromises();
        assert(!Source::initialize(source));
        Promises::restorePromiseMethods();
        Promises::waitHeldPromises();
        assert(!prepareGraph(source, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-UPLOAD-SOURCE-PENDING");
        Promises::releaseQueue();
        Promises::releaseScopes();
        waitReady(source);
        std::array<std::shared_ptr<const Source>, 2> originals{{source, source}};
        std::size_t count = 1u;
        auto b = graphBudget();
        const auto destination = source->destination()->resource().id;
        std::array<WebGpuCommandReference, 3> refs{
            {{D::Texture, S::OriginalTextureUpload, destination},
                {D::Upload, S::OriginalTextureUpload, destination},
                {D::Buffer, S::OriginalTextureUpload, source->resource().id}}};
        const auto factory = [&] {
            Graph::Sources sources;
            sources.textureUploads = originals.data();
            sources.textureUploadCount = count;
            return Graph::prepareOriginalShared(
                std::move(sources), refs.data(), refs.size(), b, quota, diagnostic);
        };
        CpuQuotaFactoryFixture admission(quota);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        assert(!quota->liveBytes());
        {
            core::CpuMemoryQuotaLease exhausted(quota, cpuBytes);
            assert(exhausted.charged() && !factory());
        }
        assert(!quota->liveBytes());
        const auto reject = [&](const char* expected) {
            assert(!factory() && std::string_view(diagnostic) == expected && !quota->liveBytes());
        };
        for (auto field : {&WebGpuGenericCommandProjectionBudget::maxTexturePackedBytes,
                 &WebGpuGenericCommandProjectionBudget::maxTextureContentBytes,
                 &WebGpuGenericCommandProjectionBudget::maxTextureDestinationTexelBytes}) {
            --(b.*field);
            assert(!factory() && !quota->liveBytes());
            ++(b.*field);
        }
        --b.maxRetainedAllocationBytes;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION-BYTES");
        ++b.maxRetainedAllocationBytes;
        --b.maxAllocationReferences;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION-COUNT");
        ++b.maxAllocationReferences;
        --b.maxReferences;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-CAPACITY");
        ++b.maxReferences;
        --b.maxCpuMetadataBytes;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-QUOTA");
        ++b.maxCpuMetadataBytes;
        b.maxOriginalTextureUploadSources = 0u;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-CAPACITY");
        b.maxOriginalTextureUploadSources = 2u;
        auto original = originals[0];
        originals[0] = std::shared_ptr<const Source>(quota, source.get());
        reject("RHI-WEBGPU-COMMAND-PROJECTION-TEXTURE-UPLOAD-OWNER");
        originals[0] = std::move(original);
        count = 2u;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-TEXTURE-UPLOAD-DUPLICATE");
        count = 1u;
        auto secondSource = prepare(c, budget(), diagnostic);
        assert(secondSource && !Source::initialize(secondSource));
        waitReady(secondSource);
        originals[1] = secondSource;
        count = 2u;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-TEXTURE-UPLOAD-DUPLICATE");
        count = 1u;
        originals[1] = source;
        secondSource.reset();
        for (auto& ref : refs) {
            const auto saved = ref;
            ref.sourceIndex = 1u;
            reject("RHI-WEBGPU-COMMAND-PROJECTION-REFERENCE-UNCOVERED");
            ref = saved;
            ref.resource = {};
            reject("RHI-WEBGPU-COMMAND-PROJECTION-REFERENCE-UNCOVERED");
            ref = saved;
        }
        auto graph = factory();
        assert(graph && graph->retainedAllocationBytes() == packedBytes + 1360u
            && graph->originalTextureUploads().size() == 1u);
        {
            core::CpuMemoryQuotaLease exhausted(quota, cpuBytes - quota->liveBytes());
            assert(exhausted.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!graph->validate()
                && !graph->validateReference(
                    0u, D::Texture, source->destination()->resource().texture.get())
                && !graph->validateReference(1u, D::Upload, source.get())
                && !graph->validateReference(2u, D::Buffer, source->resource().buffer.get()));
            const auto extent = source->destination()->resource().texture->extent;
            ++source->destination()->resource().texture->extent.width;
            assert(graph->validate());
            source->destination()->resource().texture->extent = extent;
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            std::pmr::set_default_resource(previous);
        }
        assert(std::string_view(graph->executionDiagnostic())
            == webGpuGenericCompletionResourceGraphDiagnostic);
        std::weak_ptr<Source> retained = source;
        originals = {};
        c.destination.reset();
        source.reset();
        assert(!retained.expired() && !graph->validate());
        graph.reset();
        assert(retained.expired());
        retained.reset();
        assert(!quota->liveBytes());
        c.retire();
        std::printf(
            "BINDLESS_RHI_TEXTURE_UPLOAD_PROJECTION_OK %llu refs3 allocations2 bytes2896 original-pair drop allocation0 recording-closed\n",
            static_cast<unsigned long long>(peak));
    }

    static std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates> prepareFullInputStream(
        Context& c,
        const std::shared_ptr<Source>& source,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 6>& quotas,
        bool verify,
        std::string_view passLabel = "原始纹理上传")
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using K = CommandUsageKind;
        using A = CommandScalarArguments;
        using Store = CommandVariableArguments;
        using Inputs = WebGpuOriginalCommandInputs;
        using States = WebGpuOriginalResourceStates;
        using Arg = TextureUploadOriginalInputArguments;
        const char* diagnostic = nullptr;
        std::shared_ptr<const Source> original = source;
        Graph::Sources sources;
        sources.textureUploads = &original;
        sources.textureUploadCount = 1u;
        const auto target = source->destination()->resource().id;
        using S = WebGpuCommandReferenceSource;
        using D = CommandReferenceDomain;
        const std::array<WebGpuCommandReference, 2> references{
            {{D::Texture, S::OriginalTextureUpload, target},
                {D::Upload, S::OriginalTextureUpload, target}}};
        auto gb = graphBudget();
        gb.maxReferences = 2u;
        auto graph = Graph::prepareOriginalShared(
            std::move(sources), references.data(), references.size(), gb, quotas[0], diagnostic);
        assert(graph);
        const std::array<CommandUsage, 4> commands{{{K::BeginTransferPass, 0u, 0u},
            {K::TextureUpload, 0u, 2u},
            {K::EndTransferPass, 2u, 0u},
            {K::Finish, 2u, 0u}}};
        auto usage = WebGpuGenericCommandUsage::prepareOriginal(graph,
            commands.data(),
            commands.size(),
            {1u, true, 4u, 2u, cpuBytes},
            quotas[1],
            diagnostic);
        assert(usage);
        const auto& snapshot = source->packing()->inputOwner();
        const std::array<Store::Input, 4> raw{
            {{*A::capture(K::BeginTransferPass, A::None{}), passLabel},
                {*A::capture(K::TextureUpload, A::None{}), snapshot->label()},
                {*A::capture(K::EndTransferPass, A::None{})},
                {*A::capture(K::Finish, A::None{})}}};
        const auto labelBytes = raw[0].text.size() + raw[1].text.size();
        auto arguments = Store::prepareOriginal(raw.data(),
            raw.size(),
            {1u, true, 4u, 0u, labelBytes, 0u, cpuBytes},
            quotas[2],
            diagnostic);
        assert(arguments);
        auto scalars = WebGpuFrozenCommandScalars::prepareOriginal(
            usage, arguments, {1u, true, 4u, 0u, cpuBytes}, quotas[3], diagnostic);
        assert(scalars);
        const auto argument = *Arg::captureOriginal(snapshot);
        Inputs::OriginalDescriptor descriptor{1u, argument};
        WebGpuOriginalCommandInputsBudget b{1u, true, 1u, cpuBytes};
        b.maxTextureUploadDescriptors = 1u;
        b.maxTextureUploadMipMappings = 2u;
        b.maxTextureUploadRetainedPixelBytes = 160u;
        const auto factory = [&] {
            return Inputs::prepareOriginal(scalars, {}, &descriptor, 1u, b, quotas[4], diagnostic);
        };
        std::uint64_t inputPeak = 0u, statePeak = 0u;
        if (verify) {
            const auto reject = [&](const char* id) {
                const auto baseline = quotas[4]->liveBytes();
                auto result = factory();
                if (result || !diagnostic || std::string_view(diagnostic) != id
                    || quotas[4]->liveBytes() != baseline)
                    std::printf(
                        "TEXTURE_INPUTS_REJECT_FAILURE expected=%s actual=%s result=%u bytes=%llu\n",
                        id,
                        diagnostic ? diagnostic : "none",
                        bool(result),
                        static_cast<unsigned long long>(quotas[4]->liveBytes()));
                assert(!result && diagnostic && std::string_view(diagnostic) == id
                    && quotas[4]->liveBytes() == baseline);
            };
            --b.maxTextureUploadDescriptors;
            reject("RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-CAPACITY");
            ++b.maxTextureUploadDescriptors;
            --b.maxTextureUploadMipMappings;
            reject("RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-MIP-CAPACITY");
            ++b.maxTextureUploadMipMappings;
            --b.maxTextureUploadRetainedPixelBytes;
            reject("RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-PIXEL-CAPACITY");
            ++b.maxTextureUploadRetainedPixelBytes;
            --b.maxDescriptorRecords;
            reject("RHI-WEBGPU-ORIGINAL-INPUTS-CAPACITY");
            ++b.maxDescriptorRecords;
            --b.maxCpuMetadataBytes;
            reject("RHI-WEBGPU-ORIGINAL-INPUTS-QUOTA");
            ++b.maxCpuMetadataBytes;
            descriptor.commandIndex = 0u;
            reject("RHI-WEBGPU-ORIGINAL-INPUTS-ORDER");
            descriptor.commandIndex = 1u;
            descriptor.arguments = TransferCopyArguments::finish();
            reject("RHI-WEBGPU-ORIGINAL-INPUTS-KIND");
            descriptor.arguments = Arg{};
            reject("RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-INPUT");
            descriptor.arguments = argument;
            // Equal recaptured authoring bytes do not prove original identity.
            UploadTextureDesc clone;
            snapshot->scalarFields().writeScalarFields(clone);
            clone.label.assign(snapshot->label().data(), snapshot->label().size());
            clone.mipLayouts.assign(snapshot->mipLayouts().begin(), snapshot->mipLayouts().end());
            clone.pixelData = snapshot->pixelBytes().data();
            assert(snapshot->matches(clone));
            auto equalInput = TextureUploadInputSnapshot::prepareOriginal(clone,
                {1u, true, 4u, 128u, 4096u, cpuBytes, cpuBytes, cpuBytes},
                c.authoring.quotas[0],
                c.authoring.quotas[1],
                c.authoring.quotas[2],
                diagnostic);
            assert(equalInput && equalInput->matches(clone));
            descriptor.arguments = *Arg::captureOriginal(equalInput);
            reject("RHI-TEXTURE-SOURCE-FOREIGN-INPUT");
            descriptor.arguments = argument;
            equalInput.reset();
            auto changed = raw;
            changed[1].text = "changed texture label";
            auto wrongArguments = Store::prepareOriginal(changed.data(),
                changed.size(),
                {1u, true, 4u, 0u, changed[0].text.size() + changed[1].text.size(), 0u, cpuBytes},
                quotas[2],
                diagnostic);
            assert(wrongArguments);
            auto originalScalars = scalars;
            scalars = WebGpuFrozenCommandScalars::prepareOriginal(
                usage, wrongArguments, {1u, true, 4u, 0u, cpuBytes}, quotas[3], diagnostic);
            assert(scalars);
            reject("RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-LABEL");
            scalars = std::move(originalScalars);
            wrongArguments.reset();
            CpuQuotaFactoryFixture pressure(quotas[4]);
            inputPeak = pressure.measure(factory);
            pressure.verify(inputPeak, factory);
            {
                core::CpuMemoryQuotaLease exhausted(quotas[4], cpuBytes);
                assert(exhausted.charged());
                reject("RHI-WEBGPU-ORIGINAL-INPUTS-ALLOCATION");
            }
        }
        auto inputs = factory();
        assert(inputs && !inputs->validate());
        WebGpuOriginalResourceStatesBudget sb{1u, true, 2u, 2u, cpuBytes};
        const auto stateFactory = [&] {
            return States::prepareOriginal(inputs, sb, quotas[5], diagnostic);
        };
        if (verify) {
            --sb.maxSourceReferences;
            assert(!stateFactory()
                && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATES-REFERENCE-CAPACITY");
            ++sb.maxSourceReferences;
            --sb.maxStateControls;
            assert(!stateFactory()
                && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATES-CONTROL-CAPACITY");
            ++sb.maxStateControls;
            CpuQuotaFactoryFixture pressure(quotas[5]);
            statePeak = pressure.measure(stateFactory);
            pressure.verify(statePeak, stateFactory);
            {
                core::CpuMemoryQuotaLease exhausted(quotas[5], cpuBytes);
                assert(exhausted.charged() && !stateFactory()
                    && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATES-ALLOCATION");
            }
        }
        auto states = stateFactory();
        assert(states && !states->validate() && states->sourceReferences() == 2u
            && states->states().size() == 2u);
        assert(states->find(source->resource().id)->observation.value == ResourceState::CopySrc
            && states->find(target)->observation.value == ResourceState::CopyDst);
        if (verify) {
            const std::array<WebGpuGenericCommandUsage::RawReference, 2> refs{
                {{D::Texture, source->destination()->resource().texture.get()},
                    {D::Upload, source.get()}}};
            Inputs::RawInput incoming{raw[1], argument};
            core::CpuMemoryQuotaLease inputPressure(quotas[4], cpuBytes - quotas[4]->liveBytes()),
                statePressure(quotas[5], cpuBytes - quotas[5]->liveBytes());
            assert(inputPressure.charged() && statePressure.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!inputs->compareRaw(1u, incoming, refs.data(), refs.size())
                && !inputs->validate() && !states->validate());
            incoming.descriptor = Arg{};
            assert(std::string_view(inputs->compareRaw(1u, incoming, refs.data(), refs.size()))
                == "RHI-WEBGPU-ORIGINAL-INPUTS-PAYLOAD");
            assert(std::string_view(inputs->executionDiagnostic())
                == webGpuGenericRecordingUsageDiagnostic);
            assert(std::string_view(
                       WebGpuOriginalTransferStateValidation::validate(states, 0u).diagnostic)
                == "RHI-WEBGPU-ORIGINAL-STATE-COMMAND-UNPROVEN");
            assert(std::string_view(WebGpuOriginalBufferUploadStreamValidation::validate(states))
                == "RHI-WEBGPU-ORIGINAL-UPLOAD-LANGUAGE");
            const auto extent = source->destination()->resource().texture->extent;
            ++source->destination()->resource().texture->extent.width;
            assert(inputs->validate() && states->validate());
            source->destination()->resource().texture->extent = extent;
            core::MutationJournal pending;
            assert(!source->resource().buffer->recordState(ResourceState::CopySrc, &pending));
            assert(std::string_view(states->validate()) == "RHI-WEBGPU-ORIGINAL-STATES-PENDING");
            pending.rollback();
            assert(std::string_view(states->validate()) == "RHI-WEBGPU-ORIGINAL-STATES-STALE");
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            std::pmr::set_default_resource(previous);
        }
        if (verify) {
            states.reset();
            states = stateFactory();
            assert(states && !states->validate());
            std::printf(
                "BINDLESS_RHI_TEXTURE_UPLOAD_INPUTS_OK %llu %llu stream4 refs2 states2 original-input label mip-budget allocation0 recording-closed\n",
                static_cast<unsigned long long>(inputPeak),
                static_cast<unsigned long long>(statePeak));
        }
        return states;
    }

    static void verifyFullInputStream()
    {
        Context c;
        readyDestination(c);
        const char* diagnostic = nullptr;
        auto source = prepare(c, budget(), diagnostic);
        assert(source && !Source::initialize(source));
        waitReady(source);
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 6> quotas;
        for (auto& quota : quotas)
            quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto states = prepareFullInputStream(c, source, quotas, true);
        std::weak_ptr<Source> retained = source;
        c.destination.reset();
        c.authoring.source.reset();
        source.reset();
        assert(!retained.expired() && !states->validate());
        states.reset();
        assert(retained.expired());
        retained.reset();
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
        c.retire();
    }

    static void verifyCurrentTransactions()
    {
        using Content = rhi::TextureUploadContentTransaction;
        using CS = rhi::TextureUploadContentLedger::ContentState;
        for (unsigned mode = 0u; mode < 4u; ++mode) {
            Context c;
            readyDestination(c);
            const char* diagnostic = nullptr;
            auto source = prepare(c, budget(), diagnostic);
            assert(source && !Source::initialize(source));
            waitReady(source);
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 6> quotas;
            for (auto& q : quotas)
                q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            auto states = prepareFullInputStream(c, source, quotas, false);
            auto b = transactionBudget();
            auto wrapperQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes),
                 contentQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            const auto factory = [&] {
                return Transaction::prepareOriginal(
                    states, b, wrapperQuota, contentQuota, diagnostic);
            };
            std::uint64_t wrapperPeak = 0u, contentPeak = 0u;
            if (!mode) {
                EM_ASM({
                    // clang-format off
                        Module.arpTextureCurrentEncoderCalls = 0;
                        Module.arpTextureCurrentWrites = 0;
                        Module.arpTextureCurrentEncoder = Module.preinitializedWebGPUDevice.createCommandEncoder;
                        Module.arpTextureCurrentWrite = Module.preinitializedWebGPUDevice.queue.writeTexture;
                        Module.preinitializedWebGPUDevice.createCommandEncoder = function(d) {
                            Module.arpTextureCurrentEncoderCalls++;
                            return Module.arpTextureCurrentEncoder.call(this, d);
                        };
                        Module.preinitializedWebGPUDevice.queue.writeTexture = function() {
                            Module.arpTextureCurrentWrites++;
                            return Module.arpTextureCurrentWrite.apply(this, arguments);
                        };
                    // clang-format on
                });
                CpuQuotaFactoryFixture wrapperAdmission(wrapperQuota),
                    contentAdmission(contentQuota);
                wrapperPeak = wrapperAdmission.measure(factory);
                contentPeak = contentAdmission.measure(factory);
                wrapperAdmission.verify(wrapperPeak, factory);
                contentAdmission.verify(contentPeak, factory);
                for (const auto& q : {wrapperQuota, contentQuota}) {
                    core::CpuMemoryQuotaLease exhausted(q, cpuBytes);
                    assert(exhausted.charged() && !factory());
                }
                const auto reject = [&](const char* id) {
                    assert(!factory() && diagnostic && std::string_view(diagnostic) == id
                        && !wrapperQuota->liveBytes() && !contentQuota->liveBytes());
                };
                b.contractVersion = 0u;
                reject("RHI-WEBGPU-TEXTURE-TRANSACTION-VERSION");
                b.contractVersion = 1u;
                b.prototypeOptIn = false;
                reject("RHI-WEBGPU-TEXTURE-TRANSACTION-OPT-IN");
                b.prototypeOptIn = true;
                --b.maxOriginalSources;
                reject("RHI-WEBGPU-TEXTURE-TRANSACTION-BUDGET");
                ++b.maxOriginalSources;
                --b.maxMipMappings;
                reject("RHI-WEBGPU-TEXTURE-TRANSACTION-SOURCE-CAPACITY");
                ++b.maxMipMappings;
                --b.maxRetainedContentBytes;
                reject("RHI-WEBGPU-TEXTURE-TRANSACTION-SOURCE-CAPACITY");
                ++b.maxRetainedContentBytes;
                --b.maxCpuMetadataBytes;
                reject("RHI-WEBGPU-TEXTURE-TRANSACTION-QUOTA");
                ++b.maxCpuMetadataBytes;
                b.content.prototypeOptIn = false;
                reject("RHI-WEBGPU-TEXTURE-TRANSACTION-CONTENT-BUDGET");
                b.content.prototypeOptIn = true;
                auto original = states;
                states = std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates>(
                    wrapperQuota, states.get());
                reject("RHI-WEBGPU-TEXTURE-TRANSACTION-STATES-OWNER");
                states = std::move(original);
                const auto& state = source->destination()->resource().texture->recordedState();
                assert(state.state() == rhi::ResourceState::CopyDst);
            }
            auto tx = factory();
            assert(tx && !tx->validate());
            assert(std::string_view(Transaction::validateCompletedDeclarationOriginal(tx))
                == "RHI-WEBGPU-TEXTURE-COMPLETED-STATUS");
            std::shared_ptr<const CompletedView> preparedView = tx;
            assert(std::string_view(CompletedView::validateOriginal(preparedView))
                == "RHI-WEBGPU-TEXTURE-COMPLETED-STATUS");
            preparedView.reset();
            verifyCurrentCopyDecisions(tx, source);
            auto forged = std::shared_ptr<Transaction>(wrapperQuota, tx.get());
            assert(std::string_view(Transaction::apply(forged))
                == "RHI-WEBGPU-TEXTURE-TRANSACTION-OWNER");
            forged.reset();
            if (mode == 1u) {
                auto target = source->destination();
                auto foreign = Content::prepareOriginal(target->contentLedger(),
                    target->source(),
                    *target->contentExpectation(),
                    {1u, true, cpuBytes},
                    contentQuota,
                    diagnostic);
                assert(!foreign
                    && std::string_view(diagnostic)
                        == "RHI-TEXTURE-CONTENT-TRANSACTION-COUNT-CAPACITY");
                tx.reset();
                foreign = Content::prepareOriginal(target->contentLedger(),
                    target->source(),
                    *target->contentExpectation(),
                    {1u, true, cpuBytes},
                    contentQuota,
                    diagnostic);
                assert(foreign);
                assert(!Content::apply(foreign) && !factory()
                    && std::string_view(diagnostic) == "RHI-TEXTURE-DESTINATION-CONTENT-JOIN");
                foreign->abandon();
                assert(!factory()
                    && std::string_view(diagnostic) == "RHI-TEXTURE-DESTINATION-CONTENT-JOIN");
                foreign.reset();
                target.reset();
            } else {
                auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
                const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
                assert(!Transaction::apply(tx) && tx->status() == Transaction::Status::Pending
                    && !tx->validate());
                assert(std::string_view(Transaction::validateCompletedDeclarationOriginal(tx))
                    == "RHI-WEBGPU-TEXTURE-COMPLETED-STATUS");
                std::shared_ptr<const CompletedView> pendingView = tx;
                assert(std::string_view(CompletedView::validateOriginal(pendingView))
                    == "RHI-WEBGPU-TEXTURE-COMPLETED-STATUS");
                pendingView.reset();
                verifyCurrentCopyDecisions(tx, source);
                assert(source->destination()->contentLedger()->state(*source->packing())
                    == CS::Proposed);
                assert(
                    std::string_view(source->validate()) == "RHI-TEXTURE-DESTINATION-CONTENT-JOIN"
                    && states->validate());
                assert(std::string_view(tx->executionDiagnostic())
                    == "RHI-WEBGPU-TEXTURE-TRANSACTION-RECORDING-UNPROVEN");
                rhi::semantics::TextureUploadPackedCopy output, sentinel;
                sentinel.contentGeneration = UINT64_MAX;
                output = sentinel;
                assert(std::string_view(Source::mapPackedCopyOriginal(
                           source, source->packing()->inputOwner(), 0u, output))
                        == "RHI-TEXTURE-DESTINATION-CONTENT-JOIN"
                    && output.matches(sentinel));
                for (std::size_t mip = 0u; mip < source->packing()->mipLayouts().size(); ++mip) {
                    rhi::semantics::TextureUploadPackedCopy expected;
                    assert(!rhi::semantics::TextureUploadPackedCopyMapping::mapOriginal(
                        1u, true, source->packing(), source->resource().id, mip, expected));
                    assert(!Transaction::mapPackedCopyCurrent(tx, mip, output)
                        && output.matches(expected));
                }
                output = sentinel;
                assert(Transaction::mapPackedCopyCurrent(
                           tx, source->packing()->mipLayouts().size(), output)
                    && output.matches(sentinel));
                const auto extent = source->destination()->resource().texture->extent;
                ++source->destination()->resource().texture->extent.width;
                assert(
                    std::string_view(tx->validate()) == "RHI-TEXTURE-DESTINATION-IDENTITY-CHANGED");
                source->destination()->resource().texture->extent = extent;
                auto device = source->resource().buffer->ownerDevice;
                source->resource().buffer->ownerDevice = nullptr;
                assert(tx->validate());
                source->resource().buffer->ownerDevice = device;
                assert(!tx->validate());
                if (mode == 2u) {
                    core::MutationJournal journal;
                    assert(!source->resource().buffer->recordState(
                        rhi::ResourceState::CopySrc, &journal));
                    assert(std::string_view(tx->validate())
                        == "RHI-WEBGPU-TEXTURE-TRANSACTION-RESOURCE-PENDING");
                    output = sentinel;
                    assert(Transaction::mapPackedCopyCurrent(tx, 0u, output)
                        && output.matches(sentinel));
                    journal.rollback();
                    assert(std::string_view(tx->validate())
                        == "RHI-WEBGPU-TEXTURE-TRANSACTION-RESOURCE-STALE");
                } else if (mode == 3u) {
                    source->destination()->contentLedger()->invalidate();
                    assert(std::string_view(tx->validate())
                        == "RHI-TEXTURE-CONTENT-LEDGER-INVALIDATED");
                    tx->invalidate();
                    assert(tx->status() == Transaction::Status::Invalidated
                        && source->destination()->contentLedger()->state(*source->packing())
                            == CS::Invalidated);
                } else {
                    std::weak_ptr<Source> retained = source;
                    c.destination.reset();
                    c.authoring.source.reset();
                    source.reset();
                    states.reset();
                    assert(!retained.expired() && !tx->validate());
                    tx->abandon();
                    assert(std::string_view(tx->validate())
                        == "RHI-WEBGPU-TEXTURE-TRANSACTION-ABANDONED");
                    {
                        auto observed = retained.lock();
                        assert(observed && observed->validate()
                            && observed->destination()->contentLedger()->state(*observed->packing())
                                == CS::Uninitialized);
                    }
                    retained.reset();
                }
                assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
                std::pmr::set_default_resource(previous);
            }
            if (!mode) {
                assert(EM_ASM_INT({
                    // clang-format off
                        return Module.arpTextureCurrentEncoderCalls;
                    // clang-format on
                }) == 0
                    && EM_ASM_INT({
                           // clang-format off
                            return Module.arpTextureCurrentWrites;
                           // clang-format on
                       })
                        == 0);
                EM_ASM({
                    // clang-format off
                        Module.preinitializedWebGPUDevice.createCommandEncoder = Module.arpTextureCurrentEncoder;
                        Module.preinitializedWebGPUDevice.queue.writeTexture = Module.arpTextureCurrentWrite;
                        delete Module.arpTextureCurrentEncoder;
                        delete Module.arpTextureCurrentWrite;
                    // clang-format on
                });
                std::printf(
                    "BINDLESS_RHI_TEXTURE_CURRENT_OK %llu %llu exact-pending initial-closed current-map states2 foreign stale drop allocation0 encoder0\n",
                    static_cast<unsigned long long>(wrapperPeak),
                    static_cast<unsigned long long>(contentPeak));
                std::puts(
                    "BINDLESS_RHI_TEXTURE_COPY_LOWERING_OK mips2 exact-current frozen-decision origin layer device-reject allocation0 copy0");
            }
            tx.reset();
            states.reset();
            source.reset();
            for (const auto& q : quotas)
                assert(!q->liveBytes());
            assert(!wrapperQuota->liveBytes() && !contentQuota->liveBytes());
            c.retire();
        }
    }

    static void verifyCurrentCopyDecisions(
        const std::shared_ptr<Transaction>& tx, const std::shared_ptr<Source>& source)
    {
        using Lowering = rhi::semantics::TextureUploadCopyLowering;
        using Native = rhi_webgpu::WebGpuOriginalTextureUploadCopyValidation;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (std::size_t mip = 0u; mip < source->packing()->mipLayouts().size(); ++mip) {
            rhi::semantics::TextureUploadPackedCopy mapped;
            assert(!Transaction::mapPackedCopyCurrent(tx, mip, mapped));
            Lowering::Decision decision;
            assert(!Lowering::lowerOriginal(rhi::Backend::WebGPU,
                1u,
                true,
                source->packing(),
                source->resource().id,
                mip,
                mapped,
                decision));
            auto device = source->resource().buffer->ownerDevice;
            assert(!Native::validateCurrent(tx, mip, decision, device));
            assert(std::string_view(Native::validateCurrent(tx, mip, decision, nullptr))
                == "RHI-WEBGPU-TEXTURE-COPY-LOWERING-DEVICE");
            assert(std::string_view(Native::validateCurrent(
                       tx, mip, decision, reinterpret_cast<void*>(std::uintptr_t{1u})))
                == "RHI-WEBGPU-TEXTURE-COPY-LOWERING-DEVICE");
            auto altered = decision;
            ++altered.physicalOrigin.width;
            assert(std::string_view(Native::validateCurrent(tx, mip, altered, device))
                == "RHI-TEXTURE-COPY-LOWERING-DECISION-MISMATCH");
            auto dimension = source->destination()->resource().texture->dimension;
            source->destination()->resource().texture->dimension = WGPUTextureDimension_Undefined;
            assert(Native::validateCurrent(tx, mip, decision, device));
            source->destination()->resource().texture->dimension = dimension;
            assert(Native::validateCurrent(
                tx, source->packing()->mipLayouts().size(), decision, device));
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }

    static void verifyPendingMutation()
    {
        Context c;
        readyDestination(c);
        const char* diagnostic = nullptr;
        auto s = prepare(c, budget(), diagnostic);
        assert(s);
        Promises::holdActualPromises();
        assert(!Source::initialize(s));
        Promises::restorePromiseMethods();
        Promises::waitHeldPromises();
        const auto extent = c.destination->resource().texture->extent;
        ++c.destination->resource().texture->extent.width;
        Promises::releaseQueue();
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return s->bufferSource()->status()
                == rhi_webgpu::WebGpuOriginalBufferUploadSource::Status::Rejected;
        }));
        c.destination->resource().texture->extent = extent;
        assert(s->status() == Source::Status::Rejected
            && std::string_view(s->validate()) == "RHI-TEXTURE-DESTINATION-IDENTITY-CHANGED");
        assert(std::string_view(Source::initialize(s)) == "RHI-WEBGPU-UPLOAD-SOURCE-STATE");
        s.reset();
        c.retire();
    }

    static void verifyContentMutation()
    {
        for (unsigned phase = 0u; phase < 3u; ++phase) {
            Context c;
            readyDestination(c);
            const char* diagnostic = nullptr;
            auto s = prepare(c, budget(), diagnostic);
            assert(s);
            if (phase == 1u) {
                Promises::holdActualPromises();
                assert(!Source::initialize(s));
                Promises::restorePromiseMethods();
                Promises::waitHeldPromises();
            }
            if (phase == 2u) {
                assert(!Source::initialize(s));
                waitReady(s);
            }
            auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            auto transaction = rhi::TextureUploadContentTransaction::prepareOriginal(
                c.destination->contentLedger(),
                c.authoring.source,
                *c.destination->contentExpectation(),
                {1u, true, cpuBytes},
                quota,
                diagnostic);
            assert(transaction && !rhi::TextureUploadContentTransaction::apply(transaction));
            assert(std::string_view(s->validate()) == "RHI-TEXTURE-DESTINATION-CONTENT-JOIN");
            if (phase == 0u)
                assert(std::string_view(Source::initialize(s))
                        == "RHI-TEXTURE-DESTINATION-CONTENT-JOIN"
                    && !s->resource().id.isValid());
            if (phase == 1u) {
                Promises::releaseScopes();
                Promises::releaseQueue();
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return s->bufferSource()->status()
                        == rhi_webgpu::WebGpuOriginalBufferUploadSource::Status::Rejected;
                }));
            }
            transaction->abandon();
            assert(std::string_view(s->validate()) == "RHI-TEXTURE-DESTINATION-CONTENT-JOIN");
            assert(c.destination->contentLedger()->state(*s->packing())
                == rhi::TextureUploadContentLedger::ContentState::Uninitialized);
            transaction.reset();
            assert(!quota->liveBytes());
            s.reset();
            c.retire();
        }
        Context c;
        readyDestination(c);
        const char* diagnostic = nullptr;
        auto s = prepare(c, budget(), diagnostic);
        assert(s && !Source::initialize(s));
        waitReady(s);
        c.authoring.registry->unregisterResource(c.destination->resource().id);
        assert(s->validate() && s->status() == Source::Status::Rejected);
        s.reset();
        c.retire();
    }

    static void verifyErrors()
    {
        for (unsigned kind = 0u; kind < 4u; ++kind) {
            Context c;
            readyDestination(c);
            const char* diagnostic = nullptr;
            auto s = prepare(c, budget(), diagnostic);
            assert(s);
            EM_ASM(
                {
                    // clang-format off
                        var d = Module.preinitializedWebGPUDevice;
                        Module.arpTextureSourceBadCreate = d.createBuffer;
                        Module.arpTextureSourceBadWrite = d.queue.writeBuffer;
                        if (Number($0) === 0)
                            d.createBuffer = function(desc) {
                                return Module.arpTextureSourceBadCreate.call(
                                    this, {size : desc.size, usage : GPUBufferUsage.COPY_SRC});
                            };
                        if (Number($0) === 1)
                            d.queue.writeBuffer = function(b, o, bytes) {
                                return Module.arpTextureSourceBadWrite.call(this, b, 1, bytes);
                            };
                        if (Number($0) === 2)
                            d.createBuffer = function(desc) {
                                return Module.arpTextureSourceBadCreate.call(
                                    this, {size : desc.size + 4, usage : desc.usage});
                            };
                        if (Number($0) === 3)
                            d.createBuffer = function(desc) {
                                return Module.arpTextureSourceBadCreate.call(
                                    this, {size : desc.size, usage : desc.usage | GPUBufferUsage.STORAGE});
                            };
                    // clang-format on
                },
                kind);
            assert(!Source::initialize(s));
            EM_ASM({
                // clang-format off
                    var d = Module.preinitializedWebGPUDevice;
                    d.createBuffer = Module.arpTextureSourceBadCreate;
                    d.queue.writeBuffer = Module.arpTextureSourceBadWrite;
                    delete Module.arpTextureSourceBadCreate;
                    delete Module.arpTextureSourceBadWrite;
                // clang-format on
            });
            assert(WebGpuAsyncWaitFixture::until([&] {
                return s->status() != Source::Status::Pending;
            }));
            assert(s->status() == Source::Status::Rejected);
            assert(std::string_view(s->validate())
                == (kind < 2u ? "RHI-WEBGPU-UPLOAD-SOURCE-NATIVE-ERROR"
                              : "RHI-WEBGPU-UPLOAD-SOURCE-NATIVE-LAYOUT"));
            s.reset();
            c.retire();
        }
    }

    static void verifyCollision()
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        Context c;
        readyDestination(c);
        const char* diagnostic = nullptr;
        auto s = prepare(c, budget(), diagnostic);
        assert(s);
        auto allocator =
            core::makeShared<WebGpuRhiAllocator>(WebGpuRhiAllocatorDesc{{}, packedBytes, false});
        const auto a = allocator->allocate({{}, packedBytes, MemoryUsage::GpuToCpu});
        assert(a.success);
        BufferDesc d;
        d.sizeBytes = packedBytes;
        d.usage = ResourceUsage::CopyDst;
        d.memory = MemoryUsage::GpuToCpu;
        d.initialState = ResourceState::CopyDst;
        auto competitor = createWebGpuRhiBuffer(
            s->bufferSource()->reservedResourceId(), d, a, {true, true}, c.authoring.registry);
        assert(competitor);
        c.authoring.registry->registerBuffer(*competitor);
        EM_ASM({
            // clang-format off
                Module.arpTextureSourceCollisionCreates = 0;
                Module.arpTextureSourceCollisionOriginal = Module.preinitializedWebGPUDevice.createBuffer;
                Module.preinitializedWebGPUDevice.createBuffer = function(d) {
                    Module.arpTextureSourceCollisionCreates++;
                    return Module.arpTextureSourceCollisionOriginal.call(this, d);
                };
            // clang-format on
        });
        assert(std::string_view(Source::initialize(s)) == "RHI-WEBGPU-UPLOAD-SOURCE-ID-OCCUPIED"
            && s->status() == Source::Status::Rejected);
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpTextureSourceCollisionCreates;
            // clang-format on
        }) == 0);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createBuffer = Module.arpTextureSourceCollisionOriginal;
                delete Module.arpTextureSourceCollisionOriginal;
            // clang-format on
        });
        s.reset();
        assert(c.authoring.registry->findDescriptorFacts(competitor->id()));
        competitor.reset();
        c.retire();
    }

    static void verifyRegistryReplacement()
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        Context c;
        readyDestination(c);
        const char* diagnostic = nullptr;
        auto s = prepare(c, budget(), diagnostic);
        assert(s && !Source::initialize(s));
        waitReady(s);
        const auto id = s->resource().id;
        WebGpuRhiAllocator allocator({{}, packedBytes, false});
        auto a = allocator.allocate({{}, packedBytes, MemoryUsage::GpuToCpu});
        assert(a.success);
        BufferDesc d;
        d.sizeBytes = packedBytes;
        d.usage = ResourceUsage::CopyDst;
        d.memory = MemoryUsage::GpuToCpu;
        d.initialState = ResourceState::CopyDst;
        auto replacement = createWebGpuRhiBuffer(id, d, a, {true, true}, c.authoring.registry);
        assert(replacement);
        c.authoring.registry->registerBuffer(*replacement);
        assert(s->validate());
        s.reset();
        assert(c.authoring.registry->findDescriptorFacts(id));
        replacement.reset();
        c.retire();
    }

    static void verifyPendingDrop()
    {
        Context c;
        readyDestination(c);
        const char* diagnostic = nullptr;
        auto s = prepare(c, budget(), diagnostic);
        assert(s);
        Promises::holdActualPromises();
        assert(!Source::initialize(s));
        Promises::restorePromiseMethods();
        Promises::waitHeldPromises();
        std::weak_ptr<const Destination> destination = c.destination;
        std::weak_ptr<rhi_webgpu::WebGpuOriginalBufferUploadSource> buffer = s->bufferSource();
        c.destination.reset();
        c.authoring.source.reset();
        s.reset();
        assert(!c.quotas[0]->liveBytes() && !destination.expired() && !buffer.expired());
        Promises::releaseQueue();
        assert(WebGpuAsyncWaitFixture::until([&] {
            auto b = buffer.lock();
            return b && b->queueCompletionObserved();
        }));
        assert(!destination.expired());
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return destination.expired() && buffer.expired();
        }));
        destination.reset();
        buffer.reset();
        c.retire();
    }

    static inline std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates> lossInputStates_;
    static inline core::UniquePtr<ReadbackRecordingProbe> readbackRecordingLoss_,
        readbackColdMapLoss_;
    static inline std::array<core::UniquePtr<ReadbackRecordingProbe>, 4> readbackCompletionLoss_;
    static inline std::array<core::UniquePtr<ReadbackRecordingProbe>, 3> readbackNativeMapLoss_;
    static inline std::array<std::shared_ptr<core::CpuMemoryQuota>, nativeCases> nativeOwnerQuotas_;
    static inline std::array<std::weak_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>, nativeCases>
        nativeOwners_;
    static inline core::UniquePtr<NativeProbe> nativeLoss_, nativeQuarantine_,
        completedDeclarationLoss_;
    static inline std::shared_ptr<Transaction> completedDeclarationTransaction_;
    static inline std::shared_ptr<const CompletedView> completedDeclarationView_;
    static inline core::UniquePtr<NativeProbe> completedReadbackLoss_;
    static inline core::UniquePtr<ReadbackFixture::Context> completedReadbackContext_;
    static inline std::shared_ptr<const ReadbackFixture::Source> completedReadbackSource_;
    static inline std::array<std::shared_ptr<core::CpuMemoryQuota>, 2> lossTransactionQuotas_;
    static inline std::shared_ptr<Transaction> lossTransaction_;
    static inline std::array<std::shared_ptr<core::CpuMemoryQuota>, 6> lossInputQuotas_;
    static inline std::shared_ptr<const rhi_webgpu::WebGpuGenericCommandProjection> lossGraph_;
    static inline std::shared_ptr<core::CpuMemoryQuota> lossGraphQuota_;
    static inline std::array<core::UniquePtr<Context>, 3> lossContexts_;
    static inline std::array<std::shared_ptr<Source>, 3> lossSources_;
#endif
};
} // namespace arp::test
