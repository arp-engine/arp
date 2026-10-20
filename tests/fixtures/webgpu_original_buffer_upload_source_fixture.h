#pragma once
#include "rhi/backends/webgpu/webgpu_upload_preparation.h"
#include "webgpu_ordinary_submission_fixture.h"
#include "webgpu_original_transfer_batch_fixture.h"

namespace arp::test {
class WebGpuOriginalBufferUploadSourceFixture final {
    using Source = rhi_webgpu::WebGpuOriginalBufferUploadSource;

public:
    static void verifyContract()
    {
        using namespace rhi_webgpu;
        WebGpuOriginalBufferUploadSourceBudget b;
        assert(std::string_view(Source::validateBudget(b)) == "RHI-WEBGPU-UPLOAD-SOURCE-VERSION");
        b.contractVersion = 1u;
        assert(std::string_view(Source::validateBudget(b)) == "RHI-WEBGPU-UPLOAD-SOURCE-OPT-IN");
        b.prototypeOptIn = true;
        assert(std::string_view(Source::validateBudget(b)) == "RHI-WEBGPU-UPLOAD-SOURCE-BUDGET");
        b = budget();
        assert(!Source::validateBudget(b));
        assert(std::string_view(Source::initialize({})) == "RHI-WEBGPU-UPLOAD-SOURCE-OWNER");
        // A failed CPU release-token factory must not publish ledger accounting.
        WebGpuRhiAllocator allocator({{}, dataBytes, false});
        auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease exhausted(quota, 1u);
            assert(exhausted.charged());
            bool failed = false;
            try {
                (void)allocator.allocate({{}, dataBytes, rhi::MemoryUsage::CpuToGpu});
            } catch (const std::bad_alloc&) {
                failed = true;
            }
            assert(failed && !allocator.memoryStats().allocatedBytes
                && !allocator.memoryStats().liveResourceCount);
        }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        Context context(1u, dataBytes);
        auto bytes = data();
        const char* diagnostic = nullptr;
        assert(!prepare(context, bytes, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-UPLOAD-SOURCE-NATIVE-UNAVAILABLE");
        context.verifyColdEmpty();
#endif
        std::puts(
            "BINDLESS_RHI_UPLOAD_SOURCE_CONTRACT_OK allocator-failure-atomic metadata-only execution-closed");
        {
            auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
            const char* diagnostic = nullptr;
            auto graph = WebGpuGenericCommandProjection::prepare({},
                nullptr,
                0u,
                {1u, true, 0u, 0u, 0u, 0u, 0u, 0u, {}, metadataBytes, 0u},
                quota,
                diagnostic);
            assert(graph);
            rhi::UploadBufferDesc input;
            const auto fields = rhi::BufferUploadArguments::captureScalarFields(input);
            assert(fields.kind() == rhi::CommandUsageKind::BufferUpload && fields.matches(fields));
            assert(
                std::string_view(WebGpuOriginalBufferUploadValidation::validate(*graph, fields, 0u))
                == "RHI-WEBGPU-ORIGINAL-UPLOAD-REFERENCE");
        }
        auto nativeBudget = recordingBudget();
        assert(!WebGpuOrdinaryRecording::validateBudget(nativeBudget));
        nativeBudget.prototypeOptIn = false;
        assert(std::string_view(WebGpuOrdinaryRecording::validateBudget(nativeBudget))
            == "RHI-WEBGPU-ORIGINAL-UPLOAD-OPT-IN");
        assert(std::string_view(WebGpuOrdinaryRecording::recordOriginalBufferUpload({}))
            == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER");
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        nativeBudget = recordingBudget();
        auto nativeQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* nativeDiagnostic = nullptr;
        assert(!WebGpuOrdinaryRecording::prepareOriginalBufferUpload(
                   {}, {}, nativeBudget, nativeQuota, nativeQuota, nativeDiagnostic)
            && std::string_view(nativeDiagnostic)
                == "RHI-WEBGPU-ORIGINAL-UPLOAD-NATIVE-UNAVAILABLE");
#endif
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual()
    {
        verifyCold();
        verifyReadyAndDrop();
        verifyNativeErrors();
        verifyProjection();
        std::puts(
            "BINDLESS_RHI_UPLOAD_SOURCE_NATIVE_OK ready4 scopes queue bytes16 sealed source-owner allocation0");
    }

    static void prepareActualLoss()
    {
        assert(!lossContext_);
        lossContext_ = core::makeShared<Context>(4u, 4u * dataBytes);
        auto bytes = data();
        const char* diagnostic = nullptr;
        readyLoss_ = prepare(*lossContext_, bytes, diagnostic);
        assert(readyLoss_ && !Source::initialize(readyLoss_));
        waitReady(readyLoss_);
        lossGraphQuota_ = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        lossTransferQuota_ = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const auto sourceId = readyLoss_->resource().id;
        lossTargetAllocator_ = core::makeShared<rhi_webgpu::WebGpuRhiAllocator>(
            rhi_webgpu::WebGpuRhiAllocatorDesc{{}, dataBytes, false});
        {
            core::CpuMemoryQuotaScope scope(lossContext_->quotas[2]);
            const auto allocation =
                lossTargetAllocator_->allocate({{}, dataBytes, rhi::MemoryUsage::GpuToCpu});
            assert(allocation.success);
            rhi::BufferDesc desc;
            desc.sizeBytes = dataBytes;
            desc.usage = rhi::ResourceUsage::CopyDst;
            desc.memory = rhi::MemoryUsage::GpuToCpu;
            desc.initialState = rhi::ResourceState::CopyDst;
            lossTarget_ = rhi_webgpu::createWebGpuRhiBuffer(rhi::allocateGpuResourceId(),
                desc,
                allocation,
                {true, true},
                lossContext_->registry);
            assert(lossTarget_);
            lossContext_->registry->registerBuffer(*lossTarget_);
        }
        const std::array<rhi::GpuResourceId, 2> ids{{sourceId, lossTarget_->id()}};
        auto transfer = rhi_webgpu::WebGpuOrdinaryTransferRetention::shareOriginal(
            rhi_webgpu::WebGpuOrdinaryTransferRetention::prepare(*lossContext_->registry,
                ids.data(),
                ids.size(),
                {1u,
                    true,
                    {rhi::kCommandRetentionBudgetContractVersion,
                        true,
                        6u,
                        0u,
                        0u,
                        0u,
                        metadataBytes},
                    2u,
                    2u * dataBytes},
                lossTransferQuota_,
                diagnostic));
        assert(transfer && !diagnostic);
        std::shared_ptr<const Source> source = readyLoss_;
        rhi_webgpu::WebGpuGenericCommandProjection::Sources sources;
        sources.transfer = transfer;
        sources.uploads = &source;
        sources.uploadCount = 1u;
        const std::array<rhi_webgpu::WebGpuCommandReference, 3> references{
            {{rhi::CommandReferenceDomain::Buffer,
                 rhi_webgpu::WebGpuCommandReferenceSource::Transfer,
                 ids[0]},
                {rhi::CommandReferenceDomain::Buffer,
                    rhi_webgpu::WebGpuCommandReferenceSource::Transfer,
                    ids[1]},
                {rhi::CommandReferenceDomain::Upload,
                    rhi_webgpu::WebGpuCommandReferenceSource::OriginalBufferUpload,
                    ids[0]}}};
        auto b = projectionBudget();
        lossGraph_ = rhi_webgpu::WebGpuGenericCommandProjection::shareOriginal(
            rhi_webgpu::WebGpuGenericCommandProjection::prepare(std::move(sources),
                references.data(),
                references.size(),
                b,
                lossGraphQuota_,
                diagnostic));
        assert(lossGraph_ && !diagnostic);
        lossInputs_ = verifyFullInputs(lossGraph_, readyLoss_, ids[1]);
        prepareNativeLoss(lossInputs_.states);
        verifyNativeQuarantine(lossInputs_.states);
        pendingLoss_ = prepare(*lossContext_, bytes, diagnostic);
        assert(pendingLoss_);
        Promises::holdActualPromises();
        assert(!Source::initialize(pendingLoss_));
        Promises::restorePromiseMethods();
        Promises::waitHeldPromises();
        assert(pendingLoss_->status() == Source::Status::Pending
            && !pendingLoss_->queueCompletionObserved());
        preparedLoss_ = prepare(*lossContext_, bytes, diagnostic);
        assert(preparedLoss_ && preparedLoss_->status() == Source::Status::Prepared);
        pristineReadyLoss_ = prepare(*lossContext_, bytes, diagnostic);
        assert(pristineReadyLoss_ && !Source::initialize(pristineReadyLoss_));
        waitReady(pristineReadyLoss_);
        assert(lossContext_->pool->originalSourceCount() == 4u
            && lossContext_->pool->originalSourceMemoryStats().allocatedBytes == 4u * dataBytes);
    }

    static void verifyActualDeviceLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost() && lossContext_);
        for (const auto& source : {readyLoss_, pendingLoss_, preparedLoss_, pristineReadyLoss_}) {
            assert(source->status() == Source::Status::DeviceLost
                && std::string_view(source->validate()) == "RHI-WEBGPU-UPLOAD-SOURCE-DEVICE-LOST");
            assert(std::string_view(Source::initialize(source))
                == "RHI-WEBGPU-UPLOAD-SOURCE-DEVICE-LOST");
        }
        Promises::releaseQueue();
        Promises::releaseScopes();
        assert(std::string_view(lossGraph_->validate())
            == "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE-LOST");
        assert(std::string_view(lossInputs_.states->validate())
            == "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE-LOST");
        assert(lossSubmission_->status() == rhi_webgpu::WebGpuOrdinarySubmission::Status::DeviceLost
            && !lossSignal_->isComplete());
        assert(lossUnsubmittedRecording_->originalTransaction()->status()
            == rhi_webgpu::WebGpuOriginalStateTransaction::Status::Abandoned);
        lossUnsubmittedRecording_.reset();
        NativePromises::releaseQueue();
        NativePromises::releaseScopes();
        lossSubmission_.reset();
        lossRecording_.reset();
        lossSignal_.reset();
        assert(WebGpuAsyncWaitFixture::until([] {
            return !lossNativeQuotas_[0]->liveBytes() && !lossNativeQuotas_[1]->liveBytes()
                && !lossNativeQuotas_[2]->liveBytes() && !lossNativeQuotas_[4]->liveBytes();
        }));
        lossNativeOwner_.reset();
        for (const auto& quota : lossNativeQuotas_)
            assert(!quota->liveBytes());
        lossNativeQuotas_ = {};
        assert(quarantineOwner_->retained.closed() && !quarantineOwner_->retained.pending());
        quarantineOwner_.reset();
        for (const auto& quota : quarantineQuotas_)
            assert(!quota->liveBytes());
        quarantineQuotas_ = {};
        assert(nativeOwner_.expired());
        nativeOwner_.reset();
        assert(!nativeOwnerQuota_->liveBytes());
        nativeOwnerQuota_.reset();
        readyLoss_.reset();
        pendingLoss_.reset();
        preparedLoss_.reset();
        pristineReadyLoss_.reset();
        assert(lossContext_->pool->originalSourceCount() >= 1u
            && lossGraph_->originalUploads().size() == 1u);
        lossGraph_.reset();
        assert(lossGraphQuota_->liveBytes());
        lossInputs_.states.reset();
        lossInputs_.verifyRetired();
        lossTarget_.reset();
        assert(!lossTargetAllocator_->memoryStats().allocatedBytes);
        lossTargetAllocator_.reset();
        assert(!lossGraphQuota_->liveBytes() && !lossTransferQuota_->liveBytes());
        lossGraphQuota_.reset();
        lossTransferQuota_.reset();
        lossContext_->verifyRetired();
        lossContext_.reset();
        std::puts(
            "BINDLESS_RHI_UPLOAD_SOURCE_LOSS_OK prepared pending ready no-promotion exact-count-gpu-cpu-retire");
        std::puts(
            "BINDLESS_RHI_UPLOAD_PROJECTION_LOSS_OK original-graph source-owner exact-retire");
        std::puts(
            "BINDLESS_RHI_UPLOAD_INPUTS_LOSS_OK full-input state2 original-source exact-retire");
        std::puts(
            "BINDLESS_RHI_UPLOAD_BATCH_LOSS_OK actual-submitted pending-evidence no-signal source-controls2 exact-retire");
    }
#endif
private:
    static constexpr std::uint64_t dataBytes = 16u, metadataBytes = 65536u, payloadBytes = 1024u,
                                   resourceBytes = 65536u;

    static std::array<unsigned char, dataBytes> data()
    {
        return {{1u,
            19u,
            37u,
            53u,
            71u,
            89u,
            107u,
            127u,
            149u,
            167u,
            181u,
            199u,
            211u,
            227u,
            239u,
            251u}};
    }

    static rhi_webgpu::WebGpuOriginalBufferUploadSourceBudget budget()
    {
        return {1u, true, Source::callbackCount, metadataBytes, payloadBytes, resourceBytes};
    }

    static rhi_webgpu::WebGpuOriginalBufferUploadRecordingBudget recordingBudget()
    {
        return {1u,
            true,
            {1u,
                true,
                4u,
                1u,
                1u,
                2u,
                1u,
                rhi_webgpu::WebGpuOrdinaryRecording::callbackCount,
                metadataBytes},
            {1u, true, 2u, 0u, 0u, metadataBytes}};
    }

    struct Context {
        std::shared_ptr<rhi_webgpu::WebGpuRhiStagingUploadPool> pool;
        std::shared_ptr<rhi_webgpu::WebGpuRhiResourceRegistry> registry;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> quotas;

        Context(std::uint32_t count, std::uint64_t gpuBytes)
        {
            rhi_webgpu::WebGpuRhiStagingUploadPoolDesc p;
            p.originalSourceContractVersion = 1u;
            p.originalSourcePrototypeOptIn = true;
            p.maxOriginalSourceBuffers = count;
            p.maxOriginalSourceGpuBytes = gpuBytes;
            pool = core::makeShared<rhi_webgpu::WebGpuRhiStagingUploadPool>(p);
            registry = core::makeShared<rhi_webgpu::WebGpuRhiResourceRegistry>();
            quotas = {core::makeShared<core::CpuMemoryQuota>(metadataBytes),
                core::makeShared<core::CpuMemoryQuota>(payloadBytes),
                core::makeShared<core::CpuMemoryQuota>(resourceBytes)};
        }

        void verifyColdEmpty() const
        {
            for (const auto& q : quotas)
                assert(!q->liveBytes());
            assert(
                !pool->originalSourceCount() && !pool->originalSourceMemoryStats().allocatedBytes);
        }

        void verifyRetired()
        {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            assert(WebGpuAsyncWaitFixture::until([&] {
                return !quotas[0]->liveBytes() && !quotas[1]->liveBytes()
                    && !pool->originalSourceCount();
            }));
#endif
            assert(!pool->originalSourceMemoryStats().allocatedBytes);
            // Registry bucket storage belongs to its original owner, not the
            // source wrapper. Release that final owner before quota retirement.
            registry.reset();
            verifyColdEmpty();
        }
    };

    static std::shared_ptr<Source> prepare(
        Context& c, const std::array<unsigned char, dataBytes>& bytes, const char*& diagnostic)
    {
        return Source::prepareOriginal(c.pool,
            c.registry,
            bytes.data(),
            bytes.size(),
            UINT64_MAX,
            budget(),
            c.quotas[0],
            c.quotas[1],
            c.quotas[2],
            diagnostic);
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    struct PromiseDomain {
        static constexpr unsigned graphics = 3u;
    };

    using Promises = WebGpuOrdinarySubmissionFixture<PromiseDomain>;

    struct NativePromiseDomain {
        static constexpr unsigned graphics = 4u;
    };

    using NativePromises = WebGpuOrdinarySubmissionFixture<NativePromiseDomain>;

    static void waitReady(const std::shared_ptr<Source>& source)
    {
        assert(WebGpuAsyncWaitFixture::until([&] {
            return source->status() != Source::Status::Pending;
        }));
        if (source->diagnostic())
            std::printf("UPLOAD_SOURCE_READY_FAILURE %s\n", source->diagnostic());
        assert(source->status() == Source::Status::Ready && !source->validate()
            && source->queueCompletionObserved());
    }

    static void verifyCold()
    {
        using namespace rhi_webgpu;
        Context c(2u, 2u * dataBytes);
        const auto bytes = data();
        const char* diagnostic = nullptr;
        EM_ASM({
            // clang-format off
                Module.arpUploadSourceCreates = 0;
                Module.arpUploadOriginalCreate = Module.preinitializedWebGPUDevice.createBuffer;
                Module.preinitializedWebGPUDevice.createBuffer = function(desc) {
                    ++Module.arpUploadSourceCreates;
                    return Module.arpUploadOriginalCreate.call(this, desc);
                };
            // clang-format on
        });
        const auto factory = [&] {
            return prepare(c, bytes, diagnostic);
        };
        for (const auto& q : c.quotas) {
            CpuQuotaFactoryFixture pressure(q);
            const auto peak = pressure.measure(factory);
            pressure.verify(peak, factory);
            c.verifyColdEmpty();
            {
                core::CpuMemoryQuotaLease exhausted(q, q->limitBytes());
                assert(exhausted.charged());
                assert(!factory());
            }
            c.verifyColdEmpty();
        }
        auto forgedPool = std::shared_ptr<WebGpuRhiStagingUploadPool>(c.quotas[0], c.pool.get());
        assert(!Source::prepareOriginal(forgedPool,
                   c.registry,
                   bytes.data(),
                   bytes.size(),
                   0u,
                   budget(),
                   c.quotas[0],
                   c.quotas[1],
                   c.quotas[2],
                   diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-UPLOAD-SOURCE-POOL-OWNER");
        auto forgedRegistry =
            std::shared_ptr<WebGpuRhiResourceRegistry>(c.quotas[0], c.registry.get());
        assert(!Source::prepareOriginal(c.pool,
                   forgedRegistry,
                   bytes.data(),
                   bytes.size(),
                   0u,
                   budget(),
                   c.quotas[0],
                   c.quotas[1],
                   c.quotas[2],
                   diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-UPLOAD-SOURCE-REGISTRY-OWNER");
        for (auto invalidSize : {0u, 1u, 15u, 17u})
            assert(!Source::prepareOriginal(c.pool,
                       c.registry,
                       bytes.data(),
                       invalidSize,
                       0u,
                       budget(),
                       c.quotas[0],
                       c.quotas[1],
                       c.quotas[2],
                       diagnostic)
                && std::string_view(diagnostic) == "RHI-WEBGPU-UPLOAD-SOURCE-PAYLOAD");
        assert(!Source::prepareOriginal(c.pool,
            c.registry,
            nullptr,
            dataBytes,
            0u,
            budget(),
            c.quotas[0],
            c.quotas[1],
            c.quotas[2],
            diagnostic));
        auto first = factory();
        auto second = factory();
        assert(first && second && c.pool->originalSourceCount() == 2u);
        assert(!factory()
            && std::string_view(diagnostic) == "RHI-WEBGPU-UPLOAD-SOURCE-COUNT-CAPACITY");
        first.reset();
        second.reset();
        c.verifyColdEmpty();
        Context gpu(2u, dataBytes);
        auto held = prepare(gpu, bytes, diagnostic);
        assert(held);
        assert(!prepare(gpu, bytes, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-UPLOAD-SOURCE-GPU-BUDGET");
        held.reset();
        gpu.verifyColdEmpty();
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpUploadSourceCreates;
            // clang-format on
        }) == 0);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createBuffer = Module.arpUploadOriginalCreate;
                delete Module.arpUploadOriginalCreate;
            // clang-format on
        });
        std::puts(
            "BINDLESS_RHI_UPLOAD_SOURCE_COLD_OK metadata payload resource exact-quota gpu-count owner create0");
    }

    static void verifyReadyAndDrop()
    {
        Context c(1u, dataBytes);
        const auto original = data();
        auto caller = original;
        const char* diagnostic = nullptr;
        auto source = prepare(c, caller, diagnostic);
        assert(source && source->status() == Source::Status::Prepared
            && std::string_view(source->validate()) == "RHI-WEBGPU-UPLOAD-SOURCE-UNINITIALIZED");
        caller.fill(0u);
        auto forged = std::shared_ptr<Source>(c.quotas[0], source.get());
        assert(std::string_view(Source::initialize(forged)) == "RHI-WEBGPU-UPLOAD-SOURCE-OWNER");
        forged.reset();
        Promises::holdActualPromises();
        assert(!Source::initialize(source));
        Promises::restorePromiseMethods();
        Promises::waitHeldPromises();
        assert(source->status() == Source::Status::Pending && !source->queueCompletionObserved());
        Promises::releaseQueue();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return source->queueCompletionObserved();
        }));
        assert(source->status() == Source::Status::Pending
            && std::string_view(source->validate()) == "RHI-WEBGPU-UPLOAD-SOURCE-PENDING");
        Promises::releaseScopes();
        waitReady(source);
        assert(source->sizeBytes() == dataBytes && source->frameSerial() == UINT64_MAX);
        verifyReadback(source, original);
        verifyHot(source, c);
        auto originalNative = source->resource().buffer;
        std::weak_ptr<Source> weak = source;
        source.reset();
        assert(weak.expired());
        weak.reset();
        assert(c.pool->originalSourceCount() == 1u
            && c.pool->originalSourceMemoryStats().allocatedBytes == dataBytes);
        originalNative.reset();
        c.verifyRetired();
        Context dropped(1u, dataBytes);
        source = prepare(dropped, original, diagnostic);
        assert(source);
        Promises::holdActualPromises();
        assert(!Source::initialize(source));
        Promises::restorePromiseMethods();
        Promises::waitHeldPromises();
        weak = source;
        source.reset();
        assert(!weak.expired() && dropped.pool->originalSourceCount() == 1u);
        Promises::releaseQueue();
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return weak.expired();
        }));
        weak.reset();
        dropped.verifyRetired();
    }

    static void verifyHot(const std::shared_ptr<Source>& source, Context& c)
    {
        core::CpuMemoryQuotaScope scope(c.quotas[0]);
        core::CpuMemoryQuotaLease exhausted(
            c.quotas[0], c.quotas[0]->limitBytes() - c.quotas[0]->liveBytes());
        assert(exhausted.charged());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!source->validate()
            && std::string_view(source->executionDiagnostic())
                == rhi_webgpu::webGpuGenericRecordingUsageDiagnostic);
        assert(std::string_view(Source::initialize(source)) == "RHI-WEBGPU-UPLOAD-SOURCE-STATE");
        auto& native = *source->resource().buffer;
        ++native.sizeBytes;
        assert(source->validate());
        --native.sizeBytes;
        const auto device = native.ownerDevice;
        native.ownerDevice = nullptr;
        assert(source->validate());
        native.ownerDevice = device;
        auto allocation = native.allocationReleaseToken;
        native.allocationReleaseToken.reset();
        assert(source->validate());
        native.allocationReleaseToken = std::move(allocation);
        assert(!source->validate()
            && core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        c.registry->unregisterResource(source->resource().id);
        assert(std::string_view(source->validate()) == "RHI-WEBGPU-ORDINARY-RESOURCE-MISSING");
    }

public:
    static void verifyPackedReadback(
        const std::shared_ptr<Source>& source, const unsigned char* expected, std::uint64_t bytes)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        auto registry = source->registry();
        WebGpuRhiAllocator allocator({{}, bytes, false});
        auto allocation = allocator.allocate({{}, bytes, MemoryUsage::GpuToCpu});
        assert(allocation.success);
        BufferDesc d;
        d.sizeBytes = bytes;
        d.usage = ResourceUsage::CopyDst;
        d.memory = MemoryUsage::GpuToCpu;
        d.initialState = ResourceState::CopyDst;
        auto target =
            createWebGpuRhiBuffer(allocateGpuResourceId(), d, allocation, {true, true}, registry);
        assert(target);
        registry->registerBuffer(*target);
        const std::array<GpuResourceId, 2> ids{{source->resource().id, target->id()}};
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* diagnostic = nullptr;
        auto root = WebGpuOrdinaryTransferRetention::prepare(*registry,
            ids.data(),
            ids.size(),
            {1u,
                true,
                {kCommandRetentionBudgetContractVersion, true, 6u, 0u, 0u, 0u, metadataBytes},
                2u,
                2u * bytes},
            quota,
            diagnostic);
        assert(root);
        auto copy = *TransferCopyArguments::capture(BufferCopyDesc{ids[0], ids[1], 0u, 0u, bytes});
        assert(!WebGpuTransferCopyValidation::validate(*root, copy));
        EM_ASM(
            {
                // clang-format off
                    var device = Module.preinitializedWebGPUDevice;
                    var encoder = device.createCommandEncoder();
                    encoder.copyBufferToBuffer(WebGPU.getJsObject(Number($0)), 0, WebGPU.getJsObject(Number($1)), 0,
                                               Number($2));
                    device.queue.submit([ encoder.finish() ]);
                    Module.arpOriginalTransferSource = HEAPU8.slice(Number($3), Number($3) + Number($2));
                // clang-format on
            },
            source->resource().buffer->buffer,
            root->find(target->id())->buffer->buffer,
            bytes,
            expected);
        WebGpuOriginalTransferBatchFixture::verifyNativeReadback(*root, copy);
    }

private:
    static void verifyReadback(
        const std::shared_ptr<Source>& source, const std::array<unsigned char, dataBytes>& expected)
    {
        verifyPackedReadback(source, expected.data(), expected.size());
    }

    static void verifyNativeErrors()
    {
        for (const bool badCreate : {true, false}) {
            Context c(1u, dataBytes);
            const auto bytes = data();
            const char* diagnostic = nullptr;
            auto source = prepare(c, bytes, diagnostic);
            assert(source);
            EM_ASM(
                {
                    // clang-format off
                        if (Number($0)) {
                            Module.arpBadUploadCreate = Module.preinitializedWebGPUDevice.createBuffer;
                            Module.preinitializedWebGPUDevice.createBuffer = function(desc) {
                                return Module.arpBadUploadCreate.call(
                                    this, {size : desc.size, usage : GPUBufferUsage.COPY_SRC});
                            };
                        } else {
                            Module.arpBadUploadWrite = Module.preinitializedWebGPUDevice.queue.writeBuffer;
                            Module.preinitializedWebGPUDevice.queue.writeBuffer = function(buffer, offset, data) {
                                return Module.arpBadUploadWrite.call(this, buffer, 1, data);
                            };
                        }
                    // clang-format on
                },
                badCreate);
            assert(!Source::initialize(source));
            EM_ASM(
                {
                    // clang-format off
                        if (Number($0)) {
                            Module.preinitializedWebGPUDevice.createBuffer = Module.arpBadUploadCreate;
                            delete Module.arpBadUploadCreate;
                        } else {
                            Module.preinitializedWebGPUDevice.queue.writeBuffer = Module.arpBadUploadWrite;
                            delete Module.arpBadUploadWrite;
                        }
                    // clang-format on
                },
                badCreate);
            assert(WebGpuAsyncWaitFixture::until([&] {
                return source->status() != Source::Status::Pending;
            }));
            assert(source->status() == Source::Status::Rejected
                && std::string_view(source->validate()) == "RHI-WEBGPU-UPLOAD-SOURCE-NATIVE-ERROR");
            assert(
                std::string_view(Source::initialize(source)) == "RHI-WEBGPU-UPLOAD-SOURCE-STATE");
            source.reset();
            c.verifyRetired();
        }
        Context c(1u, dataBytes);
        const auto bytes = data();
        const char* diagnostic = nullptr;
        auto source = prepare(c, bytes, diagnostic);
        assert(source);
        {
            core::CpuMemoryQuotaLease exhausted(
                c.quotas[2], c.quotas[2]->limitBytes() - c.quotas[2]->liveBytes());
            assert(exhausted.charged());
            assert(std::string_view(Source::initialize(source))
                    == "RHI-WEBGPU-UPLOAD-SOURCE-RESOURCE-ALLOCATION"
                && source->status() == Source::Status::Rejected
                && !source->queueCompletionObserved());
        }
        source.reset();
        c.verifyRetired();
    }

    static rhi_webgpu::WebGpuGenericCommandProjectionBudget projectionBudget()
    {
        rhi_webgpu::WebGpuGenericCommandProjectionBudget b{
            1u, true, 1u, 0u, 0u, 0u, 3u, 3u, {}, metadataBytes, 2u * dataBytes};
        b.maxDomainReferences[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] = 2u;
        b.maxDomainReferences[static_cast<std::size_t>(rhi::CommandReferenceDomain::Upload)] = 1u;
        b.maxOriginalUploadSources = 1u;
        b.maxOriginalUploadBytes = dataBytes;
        return b;
    }

    static void verifyProjection()
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using D = WebGpuCommandReferenceDomain;
        using S = WebGpuCommandReferenceSource;
        Context c(1u, dataBytes);
        const auto bytes = data();
        const char* diagnostic = nullptr;
        auto source = prepare(c, bytes, diagnostic);
        assert(source);
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto transferQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto usageQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto b = projectionBudget();
        std::shared_ptr<const WebGpuOrdinaryTransferRetention> transfer;
        std::array<std::shared_ptr<const Source>, 2> uploads{{source, source}};
        std::size_t sourceCount = 1u;
        std::array<WebGpuCommandReference, 3> references;
        const auto factory = [&] {
            WebGpuGenericCommandProjection::Sources sources;
            sources.transfer = transfer;
            sources.uploads = uploads.data();
            sources.uploadCount = sourceCount;
            return WebGpuGenericCommandProjection::prepare(std::move(sources),
                references.data(),
                references.size(),
                b,
                graphQuota,
                diagnostic);
        };
        const auto expect = [&](const char* id) {
            const auto before = graphQuota->liveBytes();
            assert(!factory() && std::string_view(diagnostic) == id
                && graphQuota->liveBytes() == before);
        };
        // No transfer owner means no source adoption, even before readiness.
        references = {{{D::Buffer, S::Transfer, {1u}},
            {D::Buffer, S::Transfer, {2u}},
            {D::Upload, S::OriginalBufferUpload, {1u}}}};
        expect("RHI-WEBGPU-COMMAND-PROJECTION-UPLOAD-TRANSFER");
        Promises::holdActualPromises();
        assert(!Source::initialize(source));
        Promises::restorePromiseMethods();
        Promises::waitHeldPromises();
        const auto sourceId = source->resource().id;
        WebGpuRhiAllocator allocator({{}, dataBytes, false});
        auto allocation = allocator.allocate({{}, dataBytes, MemoryUsage::GpuToCpu});
        assert(allocation.success);
        BufferDesc targetDesc;
        targetDesc.sizeBytes = dataBytes;
        targetDesc.usage = ResourceUsage::CopyDst;
        targetDesc.memory = MemoryUsage::GpuToCpu;
        targetDesc.initialState = ResourceState::CopyDst;
        auto target = createWebGpuRhiBuffer(
            allocateGpuResourceId(), targetDesc, allocation, {true, true}, c.registry);
        assert(target);
        c.registry->registerBuffer(*target);
        const std::array<GpuResourceId, 2> ids{{sourceId, target->id()}};
        transfer = WebGpuOrdinaryTransferRetention::shareOriginal(
            WebGpuOrdinaryTransferRetention::prepare(*c.registry,
                ids.data(),
                ids.size(),
                {1u,
                    true,
                    {kCommandRetentionBudgetContractVersion, true, 6u, 0u, 0u, 0u, metadataBytes},
                    2u,
                    2u * dataBytes},
                transferQuota,
                diagnostic));
        assert(transfer);
        references = {{{D::Buffer, S::Transfer, ids[0]},
            {D::Buffer, S::Transfer, ids[1]},
            {D::Upload, S::OriginalBufferUpload, ids[0]}}};
        expect("RHI-WEBGPU-UPLOAD-SOURCE-PENDING");
        Promises::releaseQueue();
        Promises::releaseScopes();
        waitReady(source);
        {
            const auto original = transfer;
            transfer.reset();
            expect("RHI-WEBGPU-COMMAND-PROJECTION-UPLOAD-TRANSFER");
            transfer =
                std::shared_ptr<const WebGpuOrdinaryTransferRetention>(graphQuota, original.get());
            expect("RHI-WEBGPU-COMMAND-PROJECTION-UPLOAD-TRANSFER");
            transfer = original;
        }
        uploads[0] = std::shared_ptr<const Source>(graphQuota, source.get());
        expect("RHI-WEBGPU-COMMAND-PROJECTION-UPLOAD-OWNER");
        uploads[0] = source;
        uploads[0].reset();
        expect("RHI-WEBGPU-COMMAND-PROJECTION-UPLOAD-OWNER");
        uploads[0] = source;
        {
            const auto original = transfer;
            auto unrelatedQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
            transfer = WebGpuOrdinaryTransferRetention::shareOriginal(
                WebGpuOrdinaryTransferRetention::prepare(*c.registry,
                    &ids[1],
                    1u,
                    {1u,
                        true,
                        {kCommandRetentionBudgetContractVersion,
                            true,
                            3u,
                            0u,
                            0u,
                            0u,
                            metadataBytes},
                        1u,
                        dataBytes},
                    unrelatedQuota,
                    diagnostic));
            assert(transfer);
            expect("RHI-WEBGPU-COMMAND-PROJECTION-UPLOAD-TRANSFER");
            transfer = original;
            assert(!unrelatedQuota->liveBytes());
        }
        --b.maxOriginalUploadBytes;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-UPLOAD-BYTES");
        ++b.maxOriginalUploadBytes;
        b.maxOriginalUploadSources = 0u;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-CAPACITY");
        b.maxOriginalUploadSources = 1u;
        b.maxOriginalUploadBytes = 0u;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-DOMAIN-UNPROVEN");
        b.maxOriginalUploadBytes = dataBytes;
        b.maxOriginalUploadSources = 2u;
        sourceCount = 2u;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-UPLOAD-DUPLICATE");
        sourceCount = 1u;
        b.maxOriginalUploadSources = 1u;
        --b.maxAllocationReferences;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION-COUNT");
        ++b.maxAllocationReferences;
        --b.maxRetainedAllocationBytes;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION-BYTES");
        ++b.maxRetainedAllocationBytes;
        b.maxDomainReferences[static_cast<std::size_t>(D::Upload)] = 0u;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-DOMAIN-CAPACITY");
        b.maxDomainReferences[static_cast<std::size_t>(D::Upload)] = 1u;
        const auto originalReference = references[2];
        references[2].resource = ids[1];
        expect("RHI-WEBGPU-COMMAND-PROJECTION-REFERENCE-UNCOVERED");
        references[2] = originalReference;
        references[2].sourceIndex = 1u;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-REFERENCE-UNCOVERED");
        references[2] = originalReference;
        references[2].source = S::Transfer;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-REFERENCE-UNCOVERED");
        references[2] = originalReference;
        CpuQuotaFactoryFixture admission(graphQuota);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease pressure(graphQuota, graphQuota->limitBytes());
            assert(pressure.charged());
            expect("RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION");
        }
        auto graph = WebGpuGenericCommandProjection::shareOriginal(factory());
        assert(graph && !diagnostic && graph->retainedAllocationBytes() == 2u * dataBytes);
        assert(graph->originalUploads().size() == 1u
            && core::SharedOwnerProjection::sameOwner(graph->originalUploads()[0], source));
        std::array<CommandUsage, 4> commands{{{CommandUsageKind::BeginTransferPass, 0u, 0u},
            {CommandUsageKind::BufferUpload, 0u, 3u},
            {CommandUsageKind::EndTransferPass, 3u, 0u},
            {CommandUsageKind::Finish, 3u, 0u}}};
        auto usage = WebGpuGenericCommandUsage::prepareOriginal(graph,
            commands.data(),
            commands.size(),
            {1u, true, 4u, 3u, metadataBytes},
            usageQuota,
            diagnostic);
        assert(usage && !diagnostic);
        auto full = verifyFullInputs(graph, source, ids[1]);
        verifyNativeBatch(full.states, source, bytes);
        const std::array<WebGpuGenericCommandUsage::RawReference, 3> raw{
            {{D::Buffer, source->resource().buffer.get()},
                {D::Buffer, transfer->find(ids[1])->buffer.get()},
                {D::Upload, source.get()}}};
        {
            core::CpuMemoryQuotaScope scope(graphQuota);
            core::CpuMemoryQuotaLease pressure(
                graphQuota, graphQuota->limitBytes() - graphQuota->liveBytes());
            assert(pressure.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            WebGpuGenericCommandUsage::Consumption cursor(usage);
            for (const auto& command : commands) {
                const auto ticket = cursor.preflight(command.kind,
                    command.referenceCount ? raw.data() : nullptr,
                    command.referenceCount,
                    diagnostic);
                assert(ticket && cursor.commit(ticket, diagnostic));
            }
            assert(cursor.complete() && !graph->validate()
                && !graph->validateReference(2u, D::Upload, source.get()));
            assert(graph->validateReference(2u, D::Upload, source->resource().buffer.get()));
            auto& native = *source->resource().buffer;
            const auto device = native.ownerDevice;
            native.ownerDevice = nullptr;
            assert(graph->validate());
            native.ownerDevice = device;
            auto token = native.allocationReleaseToken;
            native.allocationReleaseToken.reset();
            assert(graph->validate());
            native.allocationReleaseToken = std::move(token);
            assert(!graph->validate()
                && std::string_view(graph->executionDiagnostic())
                    == webGpuGenericCompletionResourceGraphDiagnostic);
            assert(WebGpuOriginalCommandInputs::family(CommandUsageKind::BufferUpload)
                == WebGpuOriginalCommandInputs::Family::BufferUpload);
            assert(core::defaultCpuAllocator().stats().allocationCount == before);
            std::pmr::set_default_resource(previous);
        }
        std::weak_ptr<Source> weak = source;
        uploads = {};
        source.reset();
        transfer.reset();
        target.reset();
        allocation = {};
        assert(!weak.expired() && !graph->validate() && c.pool->originalSourceCount() == 1u);
        references[2].resource = {UINT64_MAX};
        assert(!graph->validateReference(2u, D::Upload, raw[2].original));
        usage.reset();
        assert(!usageQuota->liveBytes());
        graph.reset();
        assert(!weak.expired());
        full.states.reset();
        full.verifyRetired();
        assert(weak.expired());
        weak.reset();
        assert(!graphQuota->liveBytes() && !transferQuota->liveBytes());
        c.verifyRetired();
        std::printf(
            "BINDLESS_RHI_UPLOAD_PROJECTION_OK %llu original-source transfer-join dedup32 frozen-usage allocation0 recording-closed\n",
            static_cast<unsigned long long>(peak));
    }

    struct InputProbe {
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates> states;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 5> quotas;

        void verifyRetired() const
        {
            assert(!states);
            for (const auto& q : quotas)
                assert(!q->liveBytes());
        }
    };

    static void prepareNativeLoss(
        const std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates>& states)
    {
        using namespace rhi_webgpu;
        const char* diagnostic = nullptr;
        for (auto& quota : lossNativeQuotas_)
            quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        lossNativeOwner_ = prepareWebGpuOrdinaryCompletionOwner(
            {1u, true, 2u, metadataBytes}, lossNativeQuotas_[3], diagnostic);
        assert(lossNativeOwner_);
        lossSignal_ = WebGpuNativeCompletionState::prepareOrdinarySignal(
            {1u, true, 1u, metadataBytes}, lossNativeQuotas_[4], diagnostic);
        assert(lossSignal_);
        lossRecording_ = WebGpuOrdinaryRecording::prepareOriginalBufferUpload(states,
            lossNativeOwner_,
            recordingBudget(),
            lossNativeQuotas_[0],
            lossNativeQuotas_[1],
            diagnostic,
            lossSignal_);
        assert(lossRecording_);
        assert(!WebGpuOrdinaryRecording::recordOriginalBufferUpload(lossRecording_));
        assert(WebGpuAsyncWaitFixture::until([] {
            return lossRecording_->status() != WebGpuOrdinaryRecording::Status::RecordingPending;
        }));
        assert(lossRecording_->hasRecordedCommand());
        lossSubmission_ = WebGpuOrdinarySubmission::prepare(lossRecording_,
            {1u, true, 1u, WebGpuOrdinarySubmission::callbackCount, metadataBytes},
            lossNativeQuotas_[2],
            diagnostic);
        assert(lossSubmission_);
        NativePromises::holdActualPromises();
        assert(!WebGpuOrdinarySubmission::submit(lossSubmission_));
        NativePromises::restorePromiseMethods();
        NativePromises::waitHeldPromises();
        assert(lossSubmission_->status() == WebGpuOrdinarySubmission::Status::Pending
            && !lossSignal_->isComplete()
            && lossRecording_->originalTransaction()->status()
                == WebGpuOriginalStateTransaction::Status::Committed);
        lossUnsubmittedRecording_ = WebGpuOrdinaryRecording::prepareOriginalBufferUpload(states,
            lossNativeOwner_,
            recordingBudget(),
            lossNativeQuotas_[0],
            lossNativeQuotas_[1],
            diagnostic);
        assert(lossUnsubmittedRecording_);
        assert(!WebGpuOrdinaryRecording::recordOriginalBufferUpload(lossUnsubmittedRecording_));
        assert(WebGpuAsyncWaitFixture::until([] {
            return lossUnsubmittedRecording_->status()
                != WebGpuOrdinaryRecording::Status::RecordingPending;
        }));
        assert(lossUnsubmittedRecording_->hasRecordedCommand());
    }

    static void verifyNativeQuarantine(
        const std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates>& states)
    {
        using namespace rhi_webgpu;
        using Recording = WebGpuOrdinaryRecording;
        using Submission = WebGpuOrdinarySubmission;
        const char* diagnostic = nullptr;
        for (auto& quota : quarantineQuotas_)
            quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        quarantineOwner_ = prepareWebGpuOrdinaryCompletionOwner(
            {1u, true, 1u, metadataBytes}, quarantineQuotas_[3], diagnostic);
        assert(quarantineOwner_);
        auto signal = WebGpuNativeCompletionState::prepareOrdinarySignal(
            {1u, true, 1u, metadataBytes}, quarantineQuotas_[4], diagnostic);
        assert(signal);
        auto recording = Recording::prepareOriginalBufferUpload(states,
            quarantineOwner_,
            recordingBudget(),
            quarantineQuotas_[0],
            quarantineQuotas_[1],
            diagnostic,
            signal);
        assert(recording);
        assert(!Recording::recordOriginalBufferUpload(recording));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return recording->status() != Recording::Status::RecordingPending;
        }));
        assert(recording->hasRecordedCommand());
        auto submission = Submission::prepare(recording,
            {1u, true, 1u, Submission::callbackCount, metadataBytes},
            quarantineQuotas_[2],
            diagnostic);
        assert(submission);
        EM_ASM({
            // clang-format off
                Module.arpUploadDuplicateSubmit = Module.preinitializedWebGPUDevice.queue.submit;
                Module.preinitializedWebGPUDevice.queue.submit = function(buffers) {
                    return Module.arpUploadDuplicateSubmit.call(this, [ buffers[0], buffers[0] ]);
                };
            // clang-format on
        });
        assert(!Submission::submit(submission));
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.queue.submit = Module.arpUploadDuplicateSubmit;
                delete Module.arpUploadDuplicateSubmit;
            // clang-format on
        });
        assert(WebGpuAsyncWaitFixture::until([&] {
            return submission->status() == Submission::Status::Unproven;
        }));
        assert(recording->originalTransaction()->status()
                == WebGpuOriginalStateTransaction::Status::Invalidated
            && !signal->isComplete());
        for (const auto& state : states->states())
            assert(!state.source.control.active());
        assert(quarantineOwner_->retained.pending() == 1u && quarantineOwner_->retained.failed());
        submission.reset();
        recording.reset();
        signal.reset();
        assert(WebGpuAsyncWaitFixture::until([] {
            return !quarantineQuotas_[2]->liveBytes();
        }));
        assert(quarantineQuotas_[0]->liveBytes() && quarantineQuotas_[1]->liveBytes()
            && quarantineQuotas_[4]->liveBytes());
        std::puts(
            "BINDLESS_RHI_UPLOAD_QUARANTINE_OK actual-native-submit-error controls2 no-signal original-graph-retained");
    }

    static void verifyNativeBatch(
        const std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates>& states,
        const std::shared_ptr<Source>& source,
        const std::array<unsigned char, dataBytes>& bytes)
    {
        using namespace rhi_webgpu;
        using Recording = WebGpuOrdinaryRecording;
        using Submission = WebGpuOrdinarySubmission;
        const char* diagnostic = nullptr;
        auto ownerQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto recordQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto transactionQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto submitQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto signalQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto owner = prepareWebGpuOrdinaryCompletionOwner(
            {1u, true, 1u, metadataBytes}, ownerQuota, diagnostic);
        assert(owner);
        auto b = recordingBudget();
        std::shared_ptr<WebGpuNativeCompletionState> signal;
        const auto prepare = [&] {
            return Recording::prepareOriginalBufferUpload(
                states, owner, b, recordQuota, transactionQuota, diagnostic, signal);
        };
        EM_ASM({
            // clang-format off
                Module.arpUploadEncoderCalls = 0;
                Module.arpUploadEncoderCreate = Module.preinitializedWebGPUDevice.createCommandEncoder;
                Module.preinitializedWebGPUDevice.createCommandEncoder = function(desc) {
                    ++Module.arpUploadEncoderCalls;
                    var encoder = Module.arpUploadEncoderCreate.call(this, desc);
                    Module.arpUploadLabels = [];
                    var push = encoder.pushDebugGroup;
                    encoder.pushDebugGroup = function(label) {
                        Module.arpUploadLabels.push(label);
                        return push.call(this, label);
                    };
                    var copy = encoder.copyBufferToBuffer;
                    encoder.copyBufferToBuffer = function() {
                        Module.arpUploadCopyArgs = Array.from(arguments);
                        return copy.apply(this, arguments);
                    };
                    return encoder;
                };
            // clang-format on
        });
        verifyNativeLabelRejection(states, owner, b, recordQuota, transactionQuota);
        CpuQuotaFactoryFixture recordPressure(recordQuota);
        const auto recordPeak = recordPressure.measure(prepare);
        recordPressure.verify(recordPeak, prepare);
        CpuQuotaFactoryFixture transactionPressure(transactionQuota);
        const auto transactionPeak = transactionPressure.measure(prepare);
        transactionPressure.verify(transactionPeak, prepare);
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpUploadEncoderCalls;
            // clang-format on
        }) == 0);
        auto shortBudget = b;
        shortBudget.native.maxDebugScopes = 1u;
        assert(!Recording::prepareOriginalBufferUpload(
                   states, owner, shortBudget, recordQuota, transactionQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-UPLOAD-DEBUG-CAPACITY");
        signal = WebGpuNativeCompletionState::prepareOrdinarySignal(
            {1u, true, 1u, metadataBytes}, signalQuota, diagnostic);
        assert(signal);
        auto recording = prepare();
        assert(recording && !diagnostic
            && recording->originalTransaction()->status()
                == WebGpuOriginalStateTransaction::Status::Prepared);
        assert(Recording::recordOriginalTransfer(recording)
            && Recording::recordOriginalTransferDependency(recording));
        {
            core::CpuMemoryQuotaScope scope(recordQuota);
            core::CpuMemoryQuotaLease exhausted(
                recordQuota, metadataBytes - recordQuota->liveBytes());
            assert(exhausted.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Recording::recordOriginalBufferUpload(recording)
                && recording->status() == Recording::Status::RecordingPending
                && !recording->hasRecordedCommand());
            assert(core::defaultCpuAllocator().stats().allocationCount == before);
            std::pmr::set_default_resource(previous);
        }
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createCommandEncoder = Module.arpUploadEncoderCreate;
                delete Module.arpUploadEncoderCreate;
            // clang-format on
        });
        assert(WebGpuAsyncWaitFixture::until([&] {
            return recording->status() != Recording::Status::RecordingPending;
        }));
        assert(recording->hasRecordedCommand());
        const auto& upload =
            std::get<rhi::BufferUploadArguments>(states->inputs()->descriptors()[0].arguments);
        const auto& root = *states->inputs()->scalars()->usage()->projection()->transfer();
        const auto& destination = *root.find(upload.destination());
        const auto passLabel = states->inputs()->scalars()->arguments()->text(0u);
        const auto uploadLabel = states->inputs()->scalars()->arguments()->text(1u);
        assert(EM_ASM_INT(
                   {
                       // clang-format off
                           var a = Module.arpUploadCopyArgs;
                           return Module.arpUploadEncoderCalls === 1 && Module.arpUploadLabels.length === 2 &&
                                  Module.arpUploadLabels[0] === UTF8ToString($0, $1) &&
                                  Module.arpUploadLabels[1] === UTF8ToString($2, $3) &&
                                  a[0] === WebGPU.getJsObject(Number($4)) && a[1] === 0 &&
                                  a[2] === WebGPU.getJsObject(Number($5)) && a[3] === 0 && a[4] === Number($6);
                       // clang-format on
                   },
                   passLabel.data(),
                   passLabel.size(),
                   uploadLabel.data(),
                   uploadLabel.size(),
                   source->resource().buffer->buffer,
                   destination.buffer->buffer,
                   dataBytes)
            == 1);
        auto submission = Submission::prepare(recording,
            {1u, true, 1u, Submission::callbackCount, metadataBytes},
            submitQuota,
            diagnostic);
        assert(submission);
        NativePromises::holdActualPromises();
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Submission::submit(submission) && !signal->isComplete());
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
        NativePromises::restorePromiseMethods();
        NativePromises::waitHeldPromises();
        NativePromises::releaseQueue();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return submission->queueCompletionObserved();
        }));
        assert(submission->status() == Submission::Status::Pending && !signal->isComplete());
        NativePromises::releaseScopes();
        assert(recording->originalTransaction()->status()
            == WebGpuOriginalStateTransaction::Status::Committed);
        std::weak_ptr<Recording> weak = recording;
        recording.reset();
        assert(!weak.expired());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return submission->status() != Submission::Status::Pending;
        }));
        assert(submission->status() == Submission::Status::Completed && signal->isComplete());
        submission.reset();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return weak.expired();
        }));
        weak.reset();
        signal.reset();
        assert(!recordQuota->liveBytes() && !transactionQuota->liveBytes()
            && !submitQuota->liveBytes() && !signalQuota->liveBytes());
        EM_ASM(
            {
                // clang-format off
                    Module.arpOriginalTransferSource = HEAPU8.slice(Number($0), Number($0) + Number($1));
                // clang-format on
            },
            bytes.data(),
            bytes.size());
        WebGpuOriginalTransferBatchFixture::verifyNativeReadback(root, upload.copyArguments());
        for (const bool finishEncoder : {true, false}) {
            recording = prepare();
            assert(recording);
            EM_ASM(
                {
                    // clang-format off
                        Module.arpBadUploadEncoder = Module.preinitializedWebGPUDevice.createCommandEncoder;
                        Module.preinitializedWebGPUDevice.createCommandEncoder = function(desc) {
                            var encoder = Module.arpBadUploadEncoder.call(this, desc);
                            if (Number($0))
                                encoder.finish();
                            else {
                                var copy = encoder.copyBufferToBuffer;
                                encoder.copyBufferToBuffer = function(src, offset, dst, dstOffset, size) {
                                    return copy.call(this, src, 1, dst, dstOffset, size);
                                };
                            }
                            return encoder;
                        };
                    // clang-format on
                },
                finishEncoder);
            assert(!Recording::recordOriginalBufferUpload(recording));
            EM_ASM({
                // clang-format off
                    Module.preinitializedWebGPUDevice.createCommandEncoder = Module.arpBadUploadEncoder;
                    delete Module.arpBadUploadEncoder;
                // clang-format on
            });
            assert(WebGpuAsyncWaitFixture::until([&] {
                return recording->status() != Recording::Status::RecordingPending;
            }));
            assert(recording->status() == Recording::Status::Rejected
                && !recording->hasRecordedCommand()
                && recording->originalTransaction()->status()
                    == WebGpuOriginalStateTransaction::Status::Abandoned);
            assert(!Submission::prepare(recording,
                {1u, true, 1u, Submission::callbackCount, metadataBytes},
                submitQuota,
                diagnostic));
            recording.reset();
            assert(!recordQuota->liveBytes() && !transactionQuota->liveBytes()
                && !submitQuota->liveBytes());
        }
        recording = prepare();
        assert(recording);
        auto transaction = recording->originalTransaction();
        recording.reset();
        assert(transaction->status() == WebGpuOriginalStateTransaction::Status::Abandoned);
        transaction.reset();
        assert(!recordQuota->liveBytes() && !transactionQuota->liveBytes());
        recording = prepare();
        assert(recording && !Recording::recordOriginalBufferUpload(recording));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return recording->status() != Recording::Status::RecordingPending;
        }));
        assert(recording->hasRecordedCommand());
        transaction = recording->originalTransaction();
        recording.reset();
        assert(transaction->status() == WebGpuOriginalStateTransaction::Status::Abandoned);
        transaction.reset();
        assert(!recordQuota->liveBytes() && !transactionQuota->liveBytes());
        recording = prepare();
        assert(recording && !Recording::recordOriginalBufferUpload(recording));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return recording->status() != Recording::Status::RecordingPending;
        }));
        assert(recording->hasRecordedCommand());
        submission = Submission::prepare(recording,
            {1u, true, 1u, Submission::callbackCount, metadataBytes},
            submitQuota,
            diagnostic);
        assert(submission);
        core::MutationJournal pending;
        assert(!source->resource().buffer->recordState(rhi::ResourceState::CopySrc, &pending));
        assert(
            std::string_view(Submission::submit(submission)) == "RHI-WEBGPU-ORIGINAL-STATES-PENDING"
            && submission->status() == Submission::Status::Ready);
        pending.rollback();
        assert(
            std::string_view(Submission::submit(submission)) == "RHI-WEBGPU-ORIGINAL-STATES-STALE"
            && !submission->queueCompletionObserved());
        submission.reset();
        recording.reset();
        assert(!recordQuota->liveBytes() && !transactionQuota->liveBytes()
            && !submitQuota->liveBytes());
        assert(!nativeOwnerQuota_);
        nativeOwner_ = owner;
        nativeOwnerQuota_ = ownerQuota;
        std::printf(
            "BINDLESS_RHI_UPLOAD_BATCH_OK %llu %llu scopes labels2 exact-copy readback errors2 drop allocation0\n",
            static_cast<unsigned long long>(recordPeak),
            static_cast<unsigned long long>(transactionPeak));
    }

    static void verifyNativeLabelRejection(
        const std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates>& original,
        const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& owner,
        const rhi_webgpu::WebGpuOriginalBufferUploadRecordingBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& recordQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& transactionQuota)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using Store = CommandVariableArguments;
        using A = CommandScalarArguments;
        using K = CommandUsageKind;
        const auto& inputs = *original->inputs();
        const auto& usage = inputs.scalars()->usage();
        const char* diagnostic = nullptr;
        for (const auto labelIndex : {0u, 1u})
            for (const auto invalid :
                {std::string_view("a\0b", 3u), std::string_view("\xff", 1u)}) {
                auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
                std::array<Store::Input, 4> raw{
                    {{*A::capture(K::BeginTransferPass, A::None{}), "pass"},
                        {*A::capture(K::BufferUpload, A::None{}), "upload"},
                        {*A::capture(K::EndTransferPass, A::None{})},
                        {*A::capture(K::Finish, A::None{})}}};
                raw[labelIndex].text = invalid;
                auto arguments = Store::prepareOriginal(raw.data(),
                    raw.size(),
                    {1u, true, 4u, 0u, 16u, 0u, metadataBytes},
                    quota,
                    diagnostic);
                assert(arguments);
                auto scalars = WebGpuFrozenCommandScalars::prepareOriginal(
                    usage, arguments, {1u, true, 4u, 0u, metadataBytes}, quota, diagnostic);
                assert(scalars);
                auto plan = WebGpuOriginalCommandInputs::prepareOriginal(scalars,
                    {},
                    inputs.descriptors().data(),
                    inputs.descriptors().size(),
                    {1u, true, 1u, metadataBytes},
                    quota,
                    diagnostic);
                assert(plan);
                auto states = WebGpuOriginalResourceStates::prepareOriginal(
                    plan, {1u, true, 2u, 2u, metadataBytes}, quota, diagnostic);
                assert(states);
                assert(!WebGpuOrdinaryRecording::prepareOriginalBufferUpload(
                           states, owner, budget, recordQuota, transactionQuota, diagnostic)
                    && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-UPLOAD-LABEL-ENCODING"
                    && !recordQuota->liveBytes() && !transactionQuota->liveBytes());
            }
    }

    static InputProbe verifyFullInputs(
        const std::shared_ptr<const rhi_webgpu::WebGpuGenericCommandProjection>& graph,
        const std::shared_ptr<Source>& source,
        rhi::GpuResourceId targetId)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using K = CommandUsageKind;
        using A = CommandScalarArguments;
        using Store = CommandVariableArguments;
        using Inputs = WebGpuOriginalCommandInputs;
        using States = WebGpuOriginalResourceStates;
        InputProbe probe;
        for (auto& quota : probe.quotas)
            quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* diagnostic = nullptr;
        const std::array<CommandUsage, 4> commands{{{K::BeginTransferPass, 0u, 0u},
            {K::BufferUpload, 0u, 3u},
            {K::EndTransferPass, 3u, 0u},
            {K::Finish, 3u, 0u}}};
        auto usage = WebGpuGenericCommandUsage::prepareOriginal(graph,
            commands.data(),
            commands.size(),
            {1u, true, 4u, 3u, metadataBytes},
            probe.quotas[0],
            diagnostic);
        assert(usage);
        std::array<Store::Input, 4> raw{{{*A::capture(K::BeginTransferPass, A::None{}), "原始上传"},
            {*A::capture(K::BufferUpload, A::None{}), "上传源 🔒"},
            {*A::capture(K::EndTransferPass, A::None{})},
            {*A::capture(K::Finish, A::None{})}}};
        const auto textBytes = raw[0].text.size() + raw[1].text.size();
        auto arguments = Store::prepareOriginal(raw.data(),
            raw.size(),
            {1u, true, 4u, 0u, textBytes, 0u, metadataBytes},
            probe.quotas[1],
            diagnostic);
        assert(arguments);
        auto scalars = WebGpuFrozenCommandScalars::prepareOriginal(
            usage, arguments, {1u, true, 4u, 0u, metadataBytes}, probe.quotas[2], diagnostic);
        assert(scalars);
        UploadBufferDesc desc;
        desc.sourceUploadBuffer = source->resource().id;
        desc.destinationResource = targetId;
        desc.sizeBytes = source->sizeBytes();
        desc.frameSerial = source->frameSerial();
        const auto upload = BufferUploadArguments::captureScalarFields(desc);
        Inputs::OriginalDescriptor descriptor{1u, upload};
        auto budget = WebGpuOriginalCommandInputsBudget{1u, true, 1u, metadataBytes};
        const auto factory = [&] {
            return Inputs::prepareOriginal(
                scalars, {}, &descriptor, 1u, budget, probe.quotas[3], diagnostic);
        };
        const auto expect = [&](const char* id) {
            const auto before = probe.quotas[3]->liveBytes();
            assert(!factory() && std::string_view(diagnostic) == id
                && probe.quotas[3]->liveBytes() == before);
        };
        for (const auto invalid : {StagingIntent::None,
                 StagingIntent::Readback,
                 static_cast<StagingIntent>(UINT32_MAX)}) {
            desc.stagingIntent = invalid;
            descriptor.arguments = BufferUploadArguments::captureScalarFields(desc);
            expect("RHI-WEBGPU-ORIGINAL-UPLOAD-STAGING-INTENT");
        }
        desc.stagingIntent = StagingIntent::Upload;
        desc.backendOwnedStaging = false;
        descriptor.arguments = BufferUploadArguments::captureScalarFields(desc);
        expect("RHI-WEBGPU-ORIGINAL-UPLOAD-STAGING-INTENT");
        desc.backendOwnedStaging = true;
        --desc.sizeBytes;
        descriptor.arguments = BufferUploadArguments::captureScalarFields(desc);
        expect("RHI-WEBGPU-ORIGINAL-UPLOAD-SOURCE-FACTS");
        ++desc.sizeBytes;
        --desc.frameSerial;
        descriptor.arguments = BufferUploadArguments::captureScalarFields(desc);
        expect("RHI-WEBGPU-ORIGINAL-UPLOAD-SOURCE-FACTS");
        ++desc.frameSerial;
        const auto id = desc.sourceUploadBuffer;
        desc.sourceUploadBuffer = targetId;
        descriptor.arguments = BufferUploadArguments::captureScalarFields(desc);
        expect("RHI-WEBGPU-ORIGINAL-UPLOAD-REFERENCE");
        desc.sourceUploadBuffer = id;
        desc.destinationResource = id;
        descriptor.arguments = BufferUploadArguments::captureScalarFields(desc);
        expect("RHI-WEBGPU-ORIGINAL-UPLOAD-REFERENCE");
        desc.destinationResource = targetId;
        descriptor.arguments = upload;
        descriptor.commandIndex = 0u;
        expect("RHI-WEBGPU-ORIGINAL-INPUTS-ORDER");
        descriptor.commandIndex = 1u;
        descriptor.arguments = upload.copyArguments();
        expect("RHI-WEBGPU-ORIGINAL-INPUTS-KIND");
        descriptor.arguments = upload;
        const auto& target = *graph->transfer()->find(targetId);
        CpuQuotaFactoryFixture pressure(probe.quotas[3]);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease exhausted(probe.quotas[3], metadataBytes);
            assert(exhausted.charged());
            expect("RHI-WEBGPU-ORIGINAL-INPUTS-ALLOCATION");
        }
        auto inputs = factory();
        assert(inputs && !diagnostic);
        auto stateBudget = WebGpuOriginalResourceStatesBudget{1u, true, 2u, 2u, metadataBytes};
        const auto snapshot = [&] {
            return States::prepareOriginal(inputs, stateBudget, probe.quotas[4], diagnostic);
        };
        CpuQuotaFactoryFixture statePressure(probe.quotas[4]);
        const auto statePeak = statePressure.measure(snapshot);
        statePressure.verify(statePeak, snapshot);
        --stateBudget.maxSourceReferences;
        assert(!snapshot()
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATES-REFERENCE-CAPACITY");
        ++stateBudget.maxSourceReferences;
        --stateBudget.maxStateControls;
        assert(!snapshot()
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATES-CONTROL-CAPACITY");
        ++stateBudget.maxStateControls;
        auto states = snapshot();
        assert(states && states->sourceReferences() == 2u && states->states().size() == 2u);
        const std::array<WebGpuGenericCommandUsage::RawReference, 3> refs{
            {{CommandReferenceDomain::Buffer, source->resource().buffer.get()},
                {CommandReferenceDomain::Buffer, target.buffer.get()},
                {CommandReferenceDomain::Upload, source.get()}}};
        Inputs::RawInput incoming{raw[1], upload};
        {
            core::CpuMemoryQuotaScope scope(probe.quotas[3]);
            core::CpuMemoryQuotaLease exhausted(
                probe.quotas[3], metadataBytes - probe.quotas[3]->liveBytes());
            assert(exhausted.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            assert(!inputs->compareRaw(1u, incoming, refs.data(), refs.size())
                && !WebGpuOriginalTransferStateValidation::validate(states, 0u).diagnostic);
            for (const auto mutate :
                std::array<void (*)(UploadBufferDesc&), 6>{{[](auto& d) {
                                                                ++d.sourceUploadBuffer.value;
                                                            },
                    [](auto& d) {
                        ++d.destinationResource.value;
                    },
                    [](auto& d) {
                        --d.sizeBytes;
                    },
                    [](auto& d) {
                        d.stagingIntent = StagingIntent::Readback;
                    },
                    [](auto& d) {
                        d.backendOwnedStaging = false;
                    },
                    [](auto& d) {
                        --d.frameSerial;
                    }}}) {
                UploadBufferDesc changed;
                upload.writeScalarFields(changed);
                mutate(changed);
                incoming.descriptor = BufferUploadArguments::captureScalarFields(changed);
                assert(std::string_view(inputs->compareRaw(1u, incoming, refs.data(), refs.size()))
                    == "RHI-WEBGPU-ORIGINAL-INPUTS-PAYLOAD");
            }
            incoming.descriptor = upload;
            incoming.scalar.text = "changed";
            assert(std::string_view(inputs->compareRaw(1u, incoming, refs.data(), refs.size()))
                == "RHI-WEBGPU-COMMAND-SCALARS-INPUT");
            incoming.scalar = raw[1];
            auto wrong = refs;
            wrong[2].original = source->resource().buffer.get();
            assert(inputs->compareRaw(1u, incoming, wrong.data(), wrong.size()));
            assert(std::string_view(WebGpuOriginalTransferStreamValidation::validate(states))
                == "RHI-WEBGPU-ORIGINAL-TRANSFER-LANGUAGE");
            assert(std::string_view(inputs->executionDiagnostic())
                == webGpuGenericRecordingUsageDiagnostic);
            assert(core::defaultCpuAllocator().stats().allocationCount == before);
            std::pmr::set_default_resource(previous);
        }
        core::MutationJournal pending;
        assert(!source->resource().buffer->recordState(ResourceState::CopySrc, &pending));
        assert(std::string_view(states->validate()) == "RHI-WEBGPU-ORIGINAL-STATES-PENDING");
        pending.rollback();
        assert(std::string_view(states->validate()) == "RHI-WEBGPU-ORIGINAL-STATES-STALE");
        states.reset();
        assert(!source->resource().buffer->recordState(ResourceState::CopyDst));
        states = snapshot();
        assert(states);
        assert(
            std::string_view(WebGpuOriginalTransferStateValidation::validate(states, 0u).diagnostic)
            == "RHI-WEBGPU-ORIGINAL-STATE-COPY-MISMATCH");
        states.reset();
        assert(!source->resource().buffer->recordState(ResourceState::CopySrc));
        assert(!target.buffer->recordState(ResourceState::CopySrc));
        states = snapshot();
        assert(states);
        assert(
            std::string_view(WebGpuOriginalTransferStateValidation::validate(states, 0u).diagnostic)
            == "RHI-WEBGPU-ORIGINAL-STATE-COPY-MISMATCH");
        states.reset();
        assert(!target.buffer->recordState(ResourceState::CopyDst));
        probe.states = snapshot();
        assert(probe.states
            && !WebGpuOriginalTransferStateValidation::validate(probe.states, 0u).diagnostic);
        raw[1].text = "mutated";
        descriptor.arguments = upload.copyArguments();
        assert(inputs->scalars()->arguments()->text(1u) == "上传源 🔒");
        std::printf(
            "BINDLESS_RHI_UPLOAD_INPUTS_OK %llu %llu fields6 label3refs state2 pending stale exact-source allocation0 recording-closed\n",
            static_cast<unsigned long long>(peak),
            static_cast<unsigned long long>(statePeak));
        return probe;
    }

    inline static std::shared_ptr<Context> lossContext_;
    inline static std::shared_ptr<Source> readyLoss_, pendingLoss_, preparedLoss_,
        pristineReadyLoss_;
    inline static std::shared_ptr<const rhi_webgpu::WebGpuGenericCommandProjection> lossGraph_;
    inline static std::shared_ptr<core::CpuMemoryQuota> lossGraphQuota_, lossTransferQuota_;
    inline static InputProbe lossInputs_;
    inline static core::UniquePtr<rhi::GpuBuffer> lossTarget_;
    inline static std::shared_ptr<rhi_webgpu::WebGpuRhiAllocator> lossTargetAllocator_;
    inline static std::weak_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner> nativeOwner_;
    inline static std::shared_ptr<core::CpuMemoryQuota> nativeOwnerQuota_;
    inline static std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner> lossNativeOwner_;
    inline static std::shared_ptr<rhi_webgpu::WebGpuOrdinaryRecording> lossRecording_,
        lossUnsubmittedRecording_;
    inline static std::shared_ptr<rhi_webgpu::WebGpuOrdinarySubmission> lossSubmission_;
    inline static std::shared_ptr<rhi_webgpu::WebGpuNativeCompletionState> lossSignal_;
    inline static std::array<std::shared_ptr<core::CpuMemoryQuota>, 5> lossNativeQuotas_;
    inline static std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner> quarantineOwner_;
    inline static std::array<std::shared_ptr<core::CpuMemoryQuota>, 5> quarantineQuotas_;
#endif
};
} // namespace arp::test
