#include "rhi/backends/webgpu/webgpu_staging_upload_pool.h"

#include "core/diagnostics/diagnostic_error.h"
#include "core/memory/cpu_allocator.h"
#include "rhi/backends/webgpu/webgpu_upload_preparation.h"
#include "rhi/frontend/memory_upload_staging_readiness.h"
#include "rhi/frontend/transfer_upload_destination_validation.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

namespace arp::rhi_webgpu {

namespace {

std::uint64_t nonZeroAlignment(std::uint64_t alignment)
{
    return alignment == 0 ? 1 : alignment;
}

} // namespace

WebGpuRhiStagingUploadPool::WebGpuRhiStagingUploadPool(WebGpuRhiStagingUploadPoolDesc desc)
    : desc_(std::move(desc)),
      originalSourceAllocator_({{}, desc_.maxOriginalSourceGpuBytes, false}),
      slices_({desc_.byteCapacity, 1}),
      completedSubmissionSerial_(core::makeShared<std::atomic<std::uint64_t>>(0u))
{
    desc_.alignmentBytes = nonZeroAlignment(desc_.alignmentBytes);
    maxLiveSliceCount_ = desc_.byteCapacity / desc_.alignmentBytes;
    if (maxLiveSliceCount_ == 0) {
        maxLiveSliceCount_ = 1;
    }
    maxLiveSliceCount_ = std::min<std::uint64_t>(maxLiveSliceCount_, 4096);
}

WebGpuRhiStagingUploadSlice WebGpuRhiStagingUploadPool::allocate(
    const WebGpuRhiStagingUploadRequest& request)
{
    // A queue completion callback updates this serial asynchronously. Read it
    // before every allocation so the ring can reclaim only GPU-complete work.
    retireCompletedFrames(completedSubmissionSerial_->load(std::memory_order_acquire));

    // The backend-owned WebGPU upload ring maps successful slices to queue.writeBuffer
    // or wgpuQueueWriteBuffer only when explicit upload bytes are available.
    if (request.sizeBytes == 0) {
        return fail("WebGPU RHI upload staging write size must be greater than zero",
            core::kFrameRetiredPageSliceDiagnosticSizeZero);
    }
    if (desc_.byteCapacity == 0) {
        return fail("WebGPU RHI upload staging has no bounded capacity",
            core::kFrameRetiredPageSliceDiagnosticNoCapacity);
    }
    if (slices_.stats().liveSliceCount >= maxLiveSliceCount_) {
        return fail("WebGPU RHI upload staging live slice count exceeds bounded capacity",
            rhi::semantics::kUploadStagingInFlightCapacityDiagnostic);
    }

    const auto alignment = nonZeroAlignment(
        request.alignmentBytes == 0 ? desc_.alignmentBytes : request.alignmentBytes);
    const auto accountedBytes = alignUp(request.sizeBytes, alignment);
    const auto currentStats = slices_.stats();
    if (accountedBytes > desc_.byteCapacity) {
        return fail("WebGPU RHI upload staging write exceeds bounded capacity",
            core::kFrameRetiredPageSliceDiagnosticRequestExceedsPage);
    }
    if (currentStats.liveBytes > desc_.byteCapacity - accountedBytes) {
        return fail("WebGPU RHI upload staging write exceeds bounded capacity",
            rhi::semantics::kUploadStagingInFlightCapacityDiagnostic);
    }

    const auto allocation = slices_.allocate(
        core::FrameRetiredPageSliceRequest{accountedBytes, alignment, request.frameSerial},
        [](std::uint32_t pageIndex, std::uint64_t) {
            return pageIndex == 0;
        });
    if (!allocation.success) {
        const auto diagnostics = currentStats.liveSliceCount == 0
            ? "WebGPU RHI upload staging write exceeds bounded capacity"
            : "WebGPU RHI upload staging ring cannot wrap before frame retirement";
        return fail(diagnostics,
            currentStats.liveSliceCount == 0
                ? core::kFrameRetiredPageSliceDiagnosticPageCapacity
                : rhi::semantics::kUploadStagingInFlightCapacityDiagnostic);
    }

    peakLiveBytes_ =
        std::max(peakLiveBytes_, slices_.stats().liveBytes + immediateQueueWriteLiveBytes_);

    WebGpuRhiStagingUploadSlice slice;
    slice.success = true;
    slice.offsetBytes = allocation.offsetBytes;
    slice.sizeBytes = request.sizeBytes;
    slice.accountedBytes = accountedBytes;
    slice.frameSerial = allocation.frameSerial;
    slice.pageIndex = allocation.pageIndex;
    slice.nativeHandleExposedToUpperLayer = false;
    slice.queueWritePath = desc_.preferQueueWrite;
    return slice;
}

bool WebGpuRhiStagingUploadPool::cancel(const WebGpuRhiStagingUploadSlice& slice)
{
    if (!slice.success || slice.immediateQueueWrite)
        return false;
    return cancel({
        slice.pageIndex,
        slice.offsetBytes,
        slice.accountedBytes,
        slice.frameSerial,
    });
}

bool WebGpuRhiStagingUploadPool::cancel(const rhi::RecordedStagingUploadSlice& slice)
{
    return slices_.cancelAllocation({
        true,
        nullptr,
        slice.pageIndex,
        slice.offsetBytes,
        slice.sizeBytes,
        slice.frameSerial,
        nullptr,
    });
}

WebGpuRhiStagingUploadSlice WebGpuRhiStagingUploadPool::reserveImmediateQueueWrite(
    const WebGpuRhiStagingUploadRequest& request)
{
    retireCompletedFrames(completedSubmissionSerial_->load(std::memory_order_acquire));

    if (!desc_.preferQueueWrite) {
        return fail("WebGPU RHI immediate queue-write reservation requires the queue-write path",
            core::kFrameRetiredPageSliceDiagnosticNoCapacity);
    }
    if (request.sizeBytes == 0) {
        return fail("WebGPU RHI queue-write staging size must be greater than zero",
            core::kFrameRetiredPageSliceDiagnosticSizeZero);
    }
    if (desc_.byteCapacity == 0) {
        return fail("WebGPU RHI queue-write staging has no bounded capacity",
            core::kFrameRetiredPageSliceDiagnosticNoCapacity);
    }

    const auto alignment = nonZeroAlignment(
        request.alignmentBytes == 0 ? desc_.alignmentBytes : request.alignmentBytes);
    const auto accountedBytes = alignUp(request.sizeBytes, alignment);
    const auto liveBytes = slices_.stats().liveBytes + immediateQueueWriteLiveBytes_;
    if (accountedBytes > desc_.byteCapacity) {
        return fail("WebGPU RHI queue-write staging exceeds bounded capacity",
            core::kFrameRetiredPageSliceDiagnosticRequestExceedsPage);
    }
    if (liveBytes > desc_.byteCapacity - accountedBytes) {
        return fail("WebGPU RHI queue-write staging exceeds bounded capacity",
            rhi::semantics::kUploadStagingInFlightCapacityDiagnostic);
    }

    immediateQueueWriteLiveBytes_ += accountedBytes;
    peakImmediateQueueWriteBytes_ =
        std::max(peakImmediateQueueWriteBytes_, immediateQueueWriteLiveBytes_);
    peakLiveBytes_ = std::max(peakLiveBytes_, liveBytes + accountedBytes);
    ++immediateQueueWriteCount_;

    WebGpuRhiStagingUploadSlice slice;
    slice.success = true;
    slice.sizeBytes = request.sizeBytes;
    slice.accountedBytes = accountedBytes;
    slice.frameSerial = request.frameSerial;
    slice.nativeHandleExposedToUpperLayer = false;
    slice.queueWritePath = true;
    slice.immediateQueueWrite = true;
    return slice;
}

void WebGpuRhiStagingUploadPool::releaseImmediateQueueWrite(
    const WebGpuRhiStagingUploadSlice& slice)
{
    if (!slice.success || !slice.immediateQueueWrite) {
        return;
    }
    immediateQueueWriteLiveBytes_ = slice.accountedBytes > immediateQueueWriteLiveBytes_
        ? 0u
        : immediateQueueWriteLiveBytes_ - slice.accountedBytes;
}

std::uint64_t WebGpuRhiStagingUploadPool::beginSubmission()
{
    return nextSubmissionSerial_.fetch_add(1u, std::memory_order_relaxed) + 1u;
}

std::shared_ptr<std::atomic<std::uint64_t>>
WebGpuRhiStagingUploadPool::completedSubmissionSerial() const
{
    return completedSubmissionSerial_;
}

void WebGpuRhiStagingUploadPool::retireCompletedFrames(std::uint64_t completedFrameSerial)
{
    slices_.retireCompletedFrames(completedFrameSerial);
}

WebGpuRhiStagingUploadPoolStats WebGpuRhiStagingUploadPool::stats() const
{
    return buildStats();
}

std::uint64_t WebGpuRhiStagingUploadPool::alignUp(std::uint64_t value, std::uint64_t alignment)
{
    const auto nonZero = nonZeroAlignment(alignment);
    const auto remainder = value % nonZero;
    if (remainder == 0) {
        return value;
    }
    const auto increment = nonZero - remainder;
    return value > std::numeric_limits<std::uint64_t>::max() - increment
        ? std::numeric_limits<std::uint64_t>::max()
        : value + increment;
}

WebGpuRhiStagingUploadSlice WebGpuRhiStagingUploadPool::fail(
    const std::pmr::string& diagnostics, const char* diagnosticId)
{
    ++failedWriteCount_;
    WebGpuRhiStagingUploadSlice slice;
    slice.diagnostics = diagnostics;
    if (diagnosticId != nullptr)
        slice.diagnosticId = diagnosticId;
    slice.queueWritePath = desc_.preferQueueWrite;
    return slice;
}

WebGpuRhiStagingUploadPoolStats WebGpuRhiStagingUploadPool::buildStats() const
{
    const auto coreStats = slices_.stats();
    WebGpuRhiStagingUploadPoolStats stats;
    stats.writeCount = coreStats.successfulAllocationCount + immediateQueueWriteCount_;
    stats.liveSliceCount = coreStats.liveSliceCount;
    stats.retiredSliceCount = coreStats.retiredSliceCount;
    stats.failedWriteCount = failedWriteCount_;
    stats.liveBytes = coreStats.liveBytes + immediateQueueWriteLiveBytes_;
    stats.peakBytes = std::max(coreStats.peakLiveBytes, peakLiveBytes_);
    stats.liveImmediateQueueWriteBytes = immediateQueueWriteLiveBytes_;
    stats.peakImmediateQueueWriteBytes = peakImmediateQueueWriteBytes_;
    stats.immediateQueueWriteCount = immediateQueueWriteCount_;
    stats.boundedCapacity = coreStats.boundedCapacity;
    stats.nativeHandlesHiddenFromUpperLayer = true;
    stats.queueWritePath = desc_.preferQueueWrite;
    return stats;
}

WebGpuOriginalBufferUploadSource::WebGpuOriginalBufferUploadSource(ConstructionKey,
    std::shared_ptr<WebGpuRhiStagingUploadPool> pool,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry,
    std::uint64_t frame,
    std::shared_ptr<core::CpuMemoryQuota> resourceQuota)
    : pool_(std::move(pool)), registry_(std::move(registry)), frame_(frame),
      resourceId_(rhi::allocateGpuResourceId()), resourceQuota_(std::move(resourceQuota))
{
    nativeDiagnostic_.reserve(256u);
}

WebGpuOriginalBufferUploadSource::~WebGpuOriginalBufferUploadSource()
{
    buffer_.reset();
    registeredObservation_ = WebGpuOrdinaryResourceObservation{};
    observation_ = WebGpuTransferResourceObservation{};
    allocation_ = WebGpuRhiAllocation{};
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!webGpuNativeDeviceLost()) {
        if (queue_)
            wgpuQueueRelease(queue_);
        if (device_)
            wgpuDeviceRelease(device_);
    }
#endif
}

const char* WebGpuOriginalBufferUploadSource::validateBudget(
    const WebGpuOriginalBufferUploadSourceBudget& b) noexcept
{
    if (b.contractVersion != 1u)
        return "RHI-WEBGPU-UPLOAD-SOURCE-VERSION";
    if (!b.prototypeOptIn)
        return "RHI-WEBGPU-UPLOAD-SOURCE-OPT-IN";
    if (b.maxCallbacks < callbackCount || !b.maxCpuMetadataBytes || !b.maxCpuPayloadBytes
        || !b.maxCpuResourceBytes)
        return "RHI-WEBGPU-UPLOAD-SOURCE-BUDGET";
    return nullptr;
}

std::shared_ptr<WebGpuOriginalBufferUploadSource> WebGpuOriginalBufferUploadSource::prepareOriginal(
    std::shared_ptr<WebGpuRhiStagingUploadPool> pool,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry,
    const void* bytes,
    std::uint64_t size,
    std::uint64_t frame,
    const WebGpuOriginalBufferUploadSourceBudget& b,
    std::shared_ptr<core::CpuMemoryQuota> metadataQuota,
    std::shared_ptr<core::CpuMemoryQuota> payloadQuota,
    std::shared_ptr<core::CpuMemoryQuota> resourceQuota,
    const char*& diagnostic)
{
    diagnostic = validateBudget(b);
    if (diagnostic)
        return {};
    if (!metadataQuota || metadataQuota->limitBytes() != b.maxCpuMetadataBytes || !payloadQuota
        || payloadQuota->limitBytes() != b.maxCpuPayloadBytes || !resourceQuota
        || resourceQuota->limitBytes() != b.maxCpuResourceBytes) {
        diagnostic = "RHI-WEBGPU-UPLOAD-SOURCE-QUOTA";
        return {};
    }
    if (!pool || !pool->sharedOwnerMatches(pool)) {
        diagnostic = "RHI-WEBGPU-UPLOAD-SOURCE-POOL-OWNER";
        return {};
    }
    if (!registry || !registry->sharedOwnerMatches(registry)) {
        diagnostic = "RHI-WEBGPU-UPLOAD-SOURCE-REGISTRY-OWNER";
        return {};
    }
    const auto& p = pool->desc_;
    if (p.originalSourceContractVersion != 1u) {
        diagnostic = "RHI-WEBGPU-UPLOAD-SOURCE-POOL-VERSION";
        return {};
    }
    if (!p.originalSourcePrototypeOptIn) {
        diagnostic = "RHI-WEBGPU-UPLOAD-SOURCE-POOL-OPT-IN";
        return {};
    }
    if (!p.maxOriginalSourceBuffers || !p.maxOriginalSourceGpuBytes) {
        diagnostic = "RHI-WEBGPU-UPLOAD-SOURCE-POOL-BUDGET";
        return {};
    }
    if (!bytes || !size || size > SIZE_MAX || size % queueWriteSizeAlignment
        || size > b.maxCpuPayloadBytes) {
        diagnostic = "RHI-WEBGPU-UPLOAD-SOURCE-PAYLOAD";
        return {};
    }
    if (pool->originalSourceCount() >= p.maxOriginalSourceBuffers) {
        diagnostic = "RHI-WEBGPU-UPLOAD-SOURCE-COUNT-CAPACITY";
        return {};
    }
    const auto live = pool->originalSourceAllocator_.memoryStats().allocatedBytes;
    if (size > p.maxOriginalSourceGpuBytes || live > p.maxOriginalSourceGpuBytes - size) {
        diagnostic = "RHI-WEBGPU-UPLOAD-SOURCE-GPU-BUDGET";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)frame;
    diagnostic = "RHI-WEBGPU-UPLOAD-SOURCE-NATIVE-UNAVAILABLE";
    return {};
#else
    if (webGpuNativeDeviceLost()) {
        diagnostic = "RHI-WEBGPU-UPLOAD-SOURCE-DEVICE-LOST";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope metadata(metadataQuota);
        auto result = core::makeUnique<WebGpuOriginalBufferUploadSource>(
            ConstructionKey{}, pool, registry, frame, resourceQuota);
        for (std::uint32_t i = 0u; i < callbackCount; ++i) {
            result->callbacks_[i] = core::makeUnique<Callback>();
            result->callbacks_[i]->participant = i;
        }
        {
            core::CpuMemoryQuotaScope payload(payloadQuota);
            result->payload_.resize(static_cast<std::size_t>(size));
            std::memcpy(result->payload_.data(), bytes, static_cast<std::size_t>(size));
        }
        {
            core::CpuMemoryQuotaScope resource(resourceQuota);
            result->allocation_ =
                pool->originalSourceAllocator_.allocate({{}, size, rhi::MemoryUsage::CpuToGpu});
        }
        if (!result->allocation_.success || !result->allocation_.releaseToken) {
            diagnostic = "RHI-WEBGPU-UPLOAD-SOURCE-GPU-BUDGET";
            return {};
        }
        auto shared = shareOriginal(std::move(result));
        if (!shared) {
            diagnostic = "RHI-WEBGPU-UPLOAD-SOURCE-ALLOCATION";
            return {};
        }
        return shared;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-UPLOAD-SOURCE-ALLOCATION";
        return {};
    } catch (const core::DiagnosticError&) {
        diagnostic = "RHI-RESOURCE-ID-EXHAUSTED";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-UPLOAD-SOURCE-ALLOCATION";
        return {};
    }
#endif
}

std::shared_ptr<WebGpuOriginalBufferUploadSource>
WebGpuOriginalBufferUploadSource::prepareOriginalTexture(
    std::shared_ptr<const WebGpuOriginalTextureUploadDestination> destination,
    std::shared_ptr<WebGpuRhiStagingUploadPool> pool,
    const WebGpuOriginalBufferUploadSourceBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> metadataQuota,
    std::shared_ptr<core::CpuMemoryQuota> payloadQuota,
    std::shared_ptr<core::CpuMemoryQuota> resourceQuota,
    const char*& diagnostic)
{
    if (!destination || !destination->hasQuotaBoundSharedOwner(destination)) {
        diagnostic = "RHI-TEXTURE-SOURCE-DESTINATION-OWNER";
        return {};
    }
    diagnostic = destination->validate();
    if (diagnostic)
        return {};
    const auto& content = destination->source();
    const auto& bytes = content->packing()->packedBytes();
    rhi::UploadTextureDesc fields;
    content->packing()->inputOwner()->scalarFields().writeScalarFields(fields);
    auto result = prepareOriginal(std::move(pool),
        destination->registry(),
        bytes.data(),
        bytes.size(),
        fields.frameSerial,
        budget,
        std::move(metadataQuota),
        std::move(payloadQuota),
        std::move(resourceQuota),
        diagnostic);
    if (result)
        result->textureDestination_ = std::move(destination);
    return result;
}

const char* WebGpuOriginalBufferUploadSource::validateTextureDestination(
    ValidationScope scope) const noexcept
{
    if (!textureDestination_)
        return nullptr;
    if (!textureDestination_->hasQuotaBoundSharedOwner(textureDestination_)
        || !core::SharedOwnerProjection::sameOwner(registry_, textureDestination_->registry()))
        return "RHI-TEXTURE-SOURCE-DESTINATION-OWNER";
    if (const auto* id = scope == ValidationScope::InitialAdmission
            ? textureDestination_->validate()
            : textureDestination_->validateReadyDeclaration())
        return id;
    rhi::UploadTextureDesc fields;
    textureDestination_->source()->packing()->inputOwner()->scalarFields().writeScalarFields(
        fields);
    if (frame_ != fields.frameSerial
        || payload_.size() != textureDestination_->source()->packing()->packedBytes().size())
        return "RHI-TEXTURE-SOURCE-ORIGINAL-JOIN";
    return nullptr;
}

const char* WebGpuOriginalBufferUploadSource::initialize(
    const std::shared_ptr<WebGpuOriginalBufferUploadSource>& self) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-UPLOAD-SOURCE-OWNER";
    if (webGpuNativeDeviceLost())
        return "RHI-WEBGPU-UPLOAD-SOURCE-DEVICE-LOST";
    if (self->status_ != Status::Prepared)
        return "RHI-WEBGPU-UPLOAD-SOURCE-STATE";
    if (!self->allocation_.success || !self->allocation_.releaseToken
        || !self->pool_->sharedOwnerMatches(self->pool_)
        || !self->registry_->sharedOwnerMatches(self->registry_))
        return "RHI-WEBGPU-UPLOAD-SOURCE-ORIGINAL-OWNER";
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-WEBGPU-UPLOAD-SOURCE-NATIVE-UNAVAILABLE";
#else
    if (const auto* id = self->validateTextureDestination())
        return id;
    if (self->registry_->findDescriptorFacts(self->resourceId_)) {
        self->failure_ = "RHI-WEBGPU-UPLOAD-SOURCE-ID-OCCUPIED";
        self->status_ = Status::Rejected;
        return self->failure_;
    }
    self->start(self);
    return self->status_ == Status::Rejected ? self->failure_ : nullptr;
#endif
}

void WebGpuOriginalBufferUploadSource::start(
    const std::shared_ptr<WebGpuOriginalBufferUploadSource>& self) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    device_ = emscripten_webgpu_get_device();
    queue_ = device_ ? wgpuDeviceGetQueue(device_) : nullptr;
    if (!device_ || !queue_) {
        failure_ = "RHI-WEBGPU-UPLOAD-SOURCE-NATIVE-DEVICE";
        status_ = Status::Rejected;
        return;
    }
    status_ = Status::Pending;
    WebGpuNativeErrorScopeBatch::push(device_);
    try {
        core::CpuMemoryQuotaScope resource(resourceQuota_);
        rhi::BufferDesc desc;
        desc.sizeBytes = payload_.size();
        desc.usage = rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst;
        desc.memory = rhi::MemoryUsage::CpuToGpu;
        desc.initialState = rhi::ResourceState::CopySrc;
        buffer_ = createWebGpuRhiBuffer(resourceId_, desc, allocation_, {true, true}, registry_);
        if (!buffer_)
            failure_ = "RHI-WEBGPU-UPLOAD-SOURCE-NATIVE-CREATE";
        else {
            buffer_->sealWrites();
            if (!registry_->tryRegisterFreshBuffer(*buffer_))
                failure_ = "RHI-WEBGPU-UPLOAD-SOURCE-ID-OCCUPIED";
            const auto id = buffer_->id();
            if (!failure_)
                failure_ = registry_->observeTransferResources(&id, 1u, &observation_);
            if (!failure_)
                failure_ = registry_->observeOrdinaryResources(&id, 1u, &registeredObservation_);
            if (!failure_)
                failure_ = validateOriginalSource();
            if (!failure_
                && !writeWebGpuNativeBufferPayload(
                    *observation_.buffer, payload_.data(), payload_.size(), 0u, nativeDiagnostic_))
                failure_ = "RHI-WEBGPU-UPLOAD-SOURCE-NATIVE-WRITE";
        }
    } catch (const std::bad_alloc&) {
        failure_ = "RHI-WEBGPU-UPLOAD-SOURCE-RESOURCE-ALLOCATION";
    } catch (const std::length_error&) {
        failure_ = "RHI-WEBGPU-UPLOAD-SOURCE-RESOURCE-ALLOCATION";
    }
    if (failure_)
        status_ = Status::Rejected;
    else {
        callbacks_[0]->owner = self;
        auto info = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
        info.mode = WGPUCallbackMode_AllowSpontaneous;
        info.callback = &completeQueue;
        info.userdata1 = callbacks_[0].release();
        (void)wgpuQueueOnSubmittedWorkDone(queue_, info);
    }
    WebGpuNativeErrorScopeBatch::pop(device_, self, callbacks_, 1u, &completeScope);
#else
    (void)self;
#endif
}

WebGpuOriginalBufferUploadSource::Status WebGpuOriginalBufferUploadSource::status() const noexcept
{
    return webGpuNativeDeviceLost() ? Status::DeviceLost : status_;
}

const char* WebGpuOriginalBufferUploadSource::validateOriginalSource(
    ValidationScope scope) const noexcept
{
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)scope;
    return "RHI-WEBGPU-UPLOAD-SOURCE-NATIVE-UNAVAILABLE";
#else
    if (webGpuNativeDeviceLost())
        return "RHI-WEBGPU-UPLOAD-SOURCE-DEVICE-LOST";
    if (!pool_->sharedOwnerMatches(pool_) || !registry_->sharedOwnerMatches(registry_) || !buffer_
        || !allocation_.success || allocation_.sizeBytes != payload_.size()
        || !allocation_.releaseToken || !observation_.matches() || observation_.id != buffer_->id()
        || observation_.id != resourceId_ || !observation_.buffer || observation_.texture
        || observation_.buffer->ownerDevice != device_ || !queue_
        || !core::SharedOwnerProjection::sameOwner(
            allocation_.releaseToken, observation_.allocationOwner())
        || observation_.allocationBytes != payload_.size()
        || observation_.bufferDesc.sizeBytes != payload_.size()
        || observation_.bufferDesc.usage
            != (rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst)
        || observation_.bufferDesc.memory != rhi::MemoryUsage::CpuToGpu
        || observation_.bufferDesc.initialState != rhi::ResourceState::CopySrc
        || !buffer_->hostWritesSealed() || !buffer_->gpuWritesSealed()
        || !buffer_->gpuTransferWritesSealed())
        return "RHI-WEBGPU-UPLOAD-SOURCE-IDENTITY-CHANGED";
    if (const auto* id = validateTextureDestination(scope))
        return id;
    if (textureDestination_
        && observation_.buffer->ownerDevice != textureDestination_->resource().texture->ownerDevice)
        return "RHI-TEXTURE-SOURCE-DEVICE-MISMATCH";
    if (evidence_.proven()
        && (wgpuBufferGetSize(observation_.buffer->buffer) != payload_.size()
            || wgpuBufferGetUsage(observation_.buffer->buffer)
                != (WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst)))
        return "RHI-WEBGPU-UPLOAD-SOURCE-NATIVE-LAYOUT";
    if (!observation_.buffer->recordedState().active())
        return "RHI-WEBGPU-UPLOAD-SOURCE-RESOURCE-INVALIDATED";
    return registry_->validateOrdinaryResources(&registeredObservation_, 1u);
#endif
}

void WebGpuOriginalBufferUploadSource::observe(std::uint32_t participant, bool proven) noexcept
{
    if (status() != Status::Pending)
        return;
    if (!evidence_.observe(participant, proven) || !proven) {
        if (!failure_)
            failure_ = "RHI-WEBGPU-UPLOAD-SOURCE-NATIVE-ERROR";
        status_ = Status::Rejected;
        return;
    }
    if (!evidence_.allReceived())
        return;
    failure_ = validateOriginalSource();
    status_ = !failure_ && evidence_.proven() ? Status::Ready : Status::Rejected;
}

const char* WebGpuOriginalBufferUploadSource::diagnostic() const noexcept
{
    switch (status()) {
    case Status::Prepared:
        return "RHI-WEBGPU-UPLOAD-SOURCE-UNINITIALIZED";
    case Status::Pending:
        return "RHI-WEBGPU-UPLOAD-SOURCE-PENDING";
    case Status::Ready:
        return validateOriginalSource();
    case Status::Rejected:
        return failure_ ? failure_ : "RHI-WEBGPU-UPLOAD-SOURCE-NATIVE-ERROR";
    case Status::DeviceLost:
        return "RHI-WEBGPU-UPLOAD-SOURCE-DEVICE-LOST";
    }
    return "RHI-WEBGPU-UPLOAD-SOURCE-STATE";
}

const char* WebGpuOriginalBufferUploadSource::validate() const noexcept
{
    return diagnostic();
}

const char* WebGpuOriginalBufferUploadSource::validateReadyDeclaration() const noexcept
{
    if (status() != Status::Ready)
        return diagnostic();
    if (!evidence_.proven())
        return "RHI-WEBGPU-UPLOAD-SOURCE-NATIVE-ERROR";
    return validateOriginalSource(ValidationScope::OriginalDeclaration);
}

const char* WebGpuOriginalBufferUploadSource::executionDiagnostic() const noexcept
{
    if (const auto* id = validate())
        return id;
    return webGpuGenericRecordingUsageDiagnostic;
}

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
void WebGpuOriginalBufferUploadSource::completeQueue(
    WGPUQueueWorkDoneStatus status, WGPUStringView, void* data, void*) noexcept
{
    auto* original = static_cast<Callback*>(data);
    core::UniquePtr<Callback> callback(original,
        core::CpuAllocatedDeleter<Callback>{original, &core::destroyCpuAllocatedUnique<Callback>});
    if (callback && callback->owner)
        callback->owner->observe(callback->participant, status == WGPUQueueWorkDoneStatus_Success);
}

void WebGpuOriginalBufferUploadSource::completeScope(
    WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView, void* data, void*) noexcept
{
    auto* original = static_cast<Callback*>(data);
    core::UniquePtr<Callback> callback(original,
        core::CpuAllocatedDeleter<Callback>{original, &core::destroyCpuAllocatedUnique<Callback>});
    if (callback && callback->owner)
        callback->owner->observe(callback->participant,
            status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_NoError);
}
#endif

WebGpuOriginalTextureUploadDestination::WebGpuOriginalTextureUploadDestination(ConstructionKey,
    std::shared_ptr<const rhi::TextureUploadContentSource> source,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry,
    std::shared_ptr<core::CpuMemoryQuota> resourceQuota)
    : source_(std::move(source)), registry_(std::move(registry)),
      resourceQuota_(std::move(resourceQuota))
{}

WebGpuOriginalTextureUploadDestination::~WebGpuOriginalTextureUploadDestination()
{
    if (ledger_)
        ledger_->invalidate();
    texture_.reset();
    observation_ = WebGpuTransferResourceObservation{};
    allocation_ = WebGpuRhiAllocation{};
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (device_ && !webGpuNativeDeviceLost())
        wgpuDeviceRelease(device_);
#endif
}

const char* WebGpuOriginalTextureUploadDestination::validateBudget(
    const WebGpuOriginalTextureUploadDestinationBudget& b) noexcept
{
    if (b.contractVersion != 1u)
        return "RHI-TEXTURE-DESTINATION-VERSION";
    if (!b.prototypeOptIn)
        return "RHI-TEXTURE-DESTINATION-OPT-IN";
    if (b.maxCallbacks < callbackCount || !b.maxRetainedSourceBytes || !b.maxGpuTexelBytes
        || !b.maxCpuMetadataBytes || !b.maxCpuResourceBytes)
        return "RHI-TEXTURE-DESTINATION-BUDGET";
    // One original region domain; callers independently bound future transactions.
    if (b.content.contractVersion != 1u || !b.content.prototypeOptIn || b.content.maxDomains != 1u
        || !b.content.maxTransactions || !b.content.maxRetainedDomainBytes
        || !b.content.maxPendingSourceBytes || !b.content.maxCpuMetadataBytes)
        return "RHI-TEXTURE-DESTINATION-CONTENT-BUDGET";
    return nullptr;
}

const char* WebGpuOriginalTextureUploadDestination::validateSelectedLayout(
    const rhi::TextureDesc& desc, std::uint64_t& bytes) noexcept
{
    return rhi::semantics::TextureUploadDestinationValidation::validate(desc, bytes);
}

std::shared_ptr<WebGpuOriginalTextureUploadDestination>
WebGpuOriginalTextureUploadDestination::prepareOriginal(
    std::shared_ptr<const rhi::TextureUploadContentSource> source,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry,
    const rhi::TextureDesc& desc,
    const WebGpuOriginalTextureUploadDestinationBudget& b,
    std::shared_ptr<core::CpuMemoryQuota> metadataQuota,
    std::shared_ptr<core::CpuMemoryQuota> resourceQuota,
    std::shared_ptr<core::CpuMemoryQuota> contentQuota,
    const char*& diagnostic)
{
    diagnostic = validateBudget(b);
    if (diagnostic)
        return {};
    if (!metadataQuota || metadataQuota->limitBytes() != b.maxCpuMetadataBytes || !resourceQuota
        || resourceQuota->limitBytes() != b.maxCpuResourceBytes || !contentQuota
        || contentQuota->limitBytes() != b.content.maxCpuMetadataBytes) {
        diagnostic = "RHI-TEXTURE-DESTINATION-QUOTA";
        return {};
    }
    if (!source || !source->hasQuotaBoundSharedOwner(source)) {
        diagnostic = "RHI-TEXTURE-DESTINATION-SOURCE-OWNER";
        return {};
    }
    if (!registry || !registry->sharedOwnerMatches(registry)) {
        diagnostic = "RHI-TEXTURE-DESTINATION-REGISTRY-OWNER";
        return {};
    }
    if (!source->packing()->destinationLayout().matches(desc)) {
        diagnostic = "RHI-TEXTURE-DESTINATION-LAYOUT-MISMATCH";
        return {};
    }
    if (source->retainedBytes() > b.maxRetainedSourceBytes) {
        diagnostic = "RHI-TEXTURE-DESTINATION-SOURCE-BYTE-CAPACITY";
        return {};
    }
    std::uint64_t bytes = 0u;
    diagnostic = validateSelectedLayout(desc, bytes);
    if (diagnostic)
        return {};
    if (bytes > b.maxGpuTexelBytes) {
        diagnostic = "RHI-TEXTURE-DESTINATION-GPU-TEXEL-BUDGET";
        return {};
    }
    if (registry->findDescriptorFacts(source->claim().destination)) {
        diagnostic = "RHI-TEXTURE-DESTINATION-ID-OCCUPIED";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    diagnostic = "RHI-TEXTURE-DESTINATION-NATIVE-UNAVAILABLE";
    return {};
#else
    if (webGpuNativeDeviceLost()) {
        diagnostic = "RHI-TEXTURE-DESTINATION-DEVICE-LOST";
        return {};
    }
    if (rhi::isBlockCompressed(desc.format) && !webGpuNativeDeviceSupportsTextureCompressionBc()) {
        diagnostic = "RHI-TEXTURE-DESTINATION-BC-FEATURE";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope metadata(metadataQuota);
        auto result = core::makeUnique<WebGpuOriginalTextureUploadDestination>(
            ConstructionKey{}, source, registry, resourceQuota);
        for (std::uint32_t i = 0u; i < callbackCount; ++i) {
            result->callbacks_[i] = core::makeUnique<Callback>();
            result->callbacks_[i]->participant = i;
        }
        result->ledger_ = rhi::TextureUploadContentLedger::prepareOriginal(
            source->claim().destination, desc, b.content, contentQuota, diagnostic);
        if (!result->ledger_)
            return {};
        const auto expected = rhi::TextureUploadContentLedger::prepareDomainOriginal(
            result->ledger_, source, diagnostic);
        if (!expected)
            return {};
        result->expected_.emplace(*expected);
        {
            core::CpuMemoryQuotaScope resource(resourceQuota);
            result->desc_ = desc;
            result->allocator_ = core::makeUnique<WebGpuRhiAllocator>(
                WebGpuRhiAllocatorDesc{{}, b.maxGpuTexelBytes, false});
            result->allocation_ =
                result->allocator_->allocate({{}, bytes, rhi::MemoryUsage::GpuOnly});
        }
        if (!result->allocation_.success || !result->allocation_.releaseToken) {
            diagnostic = "RHI-TEXTURE-DESTINATION-GPU-TEXEL-BUDGET";
            return {};
        }
        return shareOriginal(std::move(result));
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-TEXTURE-DESTINATION-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-TEXTURE-DESTINATION-ALLOCATION";
        return {};
    }
#endif
}

const char* WebGpuOriginalTextureUploadDestination::initialize(
    const std::shared_ptr<WebGpuOriginalTextureUploadDestination>& self) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-TEXTURE-DESTINATION-OWNER";
    if (webGpuNativeDeviceLost())
        return "RHI-TEXTURE-DESTINATION-DEVICE-LOST";
    if (self->status_ != Status::Prepared)
        return "RHI-TEXTURE-DESTINATION-STATE";
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-TEXTURE-DESTINATION-NATIVE-UNAVAILABLE";
#else
    try {
        if (self->registry_->findDescriptorFacts(self->source_->claim().destination))
            return "RHI-TEXTURE-DESTINATION-ID-OCCUPIED";
    } catch (const std::bad_alloc&) {
        return "RHI-TEXTURE-DESTINATION-RESOURCE-ALLOCATION";
    }
    if (!self->source_->hasQuotaBoundSharedOwner(self->source_)
        || !self->registry_->sharedOwnerMatches(self->registry_) || !self->allocation_.success
        || !self->allocation_.releaseToken)
        return "RHI-TEXTURE-DESTINATION-ORIGINAL-OWNER";
    const char* diagnostic = nullptr;
    const auto current = rhi::TextureUploadContentLedger::prepareDomainOriginal(
        self->ledger_, self->source_, diagnostic);
    if (!current)
        return diagnostic;
    if (!self->expected_ || current->internalRevision() != self->expected_->internalRevision()
        || current->state() != rhi::TextureUploadContentLedger::ContentState::Uninitialized)
        return "RHI-TEXTURE-DESTINATION-CONTENT-JOIN";
    self->start(self);
    return self->status_ == Status::Rejected ? self->failure_ : nullptr;
#endif
}

void WebGpuOriginalTextureUploadDestination::start(
    const std::shared_ptr<WebGpuOriginalTextureUploadDestination>& self) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    device_ = emscripten_webgpu_get_device();
    if (!device_) {
        failure_ = "RHI-TEXTURE-DESTINATION-NATIVE-DEVICE";
        status_ = Status::Rejected;
        return;
    }
    if (rhi::isBlockCompressed(desc_.format)
        && !wgpuDeviceHasFeature(device_, WGPUFeatureName_TextureCompressionBC)) {
        failure_ = "RHI-TEXTURE-DESTINATION-BC-FEATURE";
        status_ = Status::Rejected;
        ledger_->invalidate();
        return;
    }
    status_ = Status::Pending;
    WebGpuNativeErrorScopeBatch::push(device_);
    try {
        core::CpuMemoryQuotaScope resource(resourceQuota_);
        auto payload = WebGpuNativeTextureFactory::createOriginalUpload(desc_);
        if (!payload)
            failure_ = "RHI-TEXTURE-DESTINATION-NATIVE-CREATE";
        else {
            texture_ = createWebGpuRhiTexture(
                source_->claim().destination, desc_, allocation_, std::move(payload), registry_);
            if (!texture_)
                failure_ = "RHI-TEXTURE-DESTINATION-NATIVE-CREATE";
            else if (!registry_->tryRegisterFreshTexture(*texture_))
                failure_ = "RHI-TEXTURE-DESTINATION-ID-OCCUPIED";
            else {
                const auto id = texture_->id();
                failure_ = registry_->observeTransferResources(&id, 1u, &observation_);
                if (!failure_)
                    failure_ = validateOriginal();
            }
        }
    } catch (const std::bad_alloc&) {
        failure_ = "RHI-TEXTURE-DESTINATION-RESOURCE-ALLOCATION";
    } catch (const std::length_error&) {
        failure_ = "RHI-TEXTURE-DESTINATION-RESOURCE-ALLOCATION";
    }
    if (failure_) {
        status_ = Status::Rejected;
        ledger_->invalidate();
    }
    WebGpuNativeErrorScopeBatch::pop(device_, self, callbacks_, 0u, &completeScope);
#else
    (void)self;
#endif
}

WebGpuOriginalTextureUploadDestination::Status
WebGpuOriginalTextureUploadDestination::status() const noexcept
{
    if (webGpuNativeDeviceLost()) {
        if (ledger_)
            ledger_->invalidate();
        return Status::DeviceLost;
    }
    return status_;
}

const char* WebGpuOriginalTextureUploadDestination::validateNativeDeclaration() const noexcept
{
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-TEXTURE-DESTINATION-NATIVE-UNAVAILABLE";
#else
    if (webGpuNativeDeviceLost())
        return "RHI-TEXTURE-DESTINATION-DEVICE-LOST";
    if (!source_->hasQuotaBoundSharedOwner(source_) || !registry_->sharedOwnerMatches(registry_)
        || !texture_ || !observation_.matches() || observation_.id != source_->claim().destination
        || observation_.id != texture_->id() || !observation_.texture || observation_.buffer
        || !device_ || observation_.texture->ownerDevice != device_
        || !observation_.texture->texture || observation_.texture->browserSurfaceTexture
        || !observation_.texture->contentAccessPolicy().originalUploadExclusive()
        || !source_->packing()->destinationLayout().matches(observation_.textureDesc)
        || observation_.textureDesc.initialState != rhi::ResourceState::CopyDst
        || !core::SharedOwnerProjection::sameOwner(
            allocation_.releaseToken, observation_.allocationOwner())
        || !allocation_.success || allocation_.sizeBytes != observation_.allocationBytes)
        return "RHI-TEXTURE-DESTINATION-IDENTITY-CHANGED";
    if (evidence_.proven())
        if (const auto* id =
                WebGpuNativeTextureFactory::validateOriginalUpload(*observation_.texture, desc_))
            return id;
    if (!observation_.texture->recordedState().active())
        return "RHI-TEXTURE-DESTINATION-RESOURCE-INVALIDATED";
    if (const auto* id = rhi::TextureUploadContentLedger::originalDiagnostic(ledger_, source_))
        return id;
    if (!expected_
        || expected_->state() != rhi::TextureUploadContentLedger::ContentState::Uninitialized
        || expected_->contentGeneration() != 0u || ledger_->domainCount() != 1u)
        return "RHI-TEXTURE-DESTINATION-CONTENT-JOIN";
    return registry_->validateTransferResources(&observation_, 1u);
#endif
}

const char* WebGpuOriginalTextureUploadDestination::validateReadyDeclaration() const noexcept
{
    if (status() != Status::CreationReady)
        return validate();
    if (!evidence_.proven())
        return "RHI-TEXTURE-DESTINATION-NATIVE-ERROR";
    return validateNativeDeclaration();
}

const char* WebGpuOriginalTextureUploadDestination::validateOriginal() const noexcept
{
    if (const auto* id = validateNativeDeclaration())
        return id;
    if (ledger_->state(*source_->packing())
        != rhi::TextureUploadContentLedger::ContentState::Uninitialized)
        return "RHI-TEXTURE-DESTINATION-CONTENT-JOIN";
    const char* diagnostic = nullptr;
    const auto current =
        rhi::TextureUploadContentLedger::prepareDomainOriginal(ledger_, source_, diagnostic);
    if (!current)
        return diagnostic;
    if (current->internalRevision() != expected_->internalRevision())
        return "RHI-TEXTURE-DESTINATION-CONTENT-JOIN";
    return nullptr;
}

const char* WebGpuOriginalTextureUploadDestination::validateContentJoin(
    const std::shared_ptr<const rhi::TextureUploadContentSource>& source,
    const std::shared_ptr<rhi::TextureUploadContentLedger>& ledger) const noexcept
{
    if (!core::SharedOwnerProjection::sameOwner(source_, source) || source_.get() != source.get()
        || !core::SharedOwnerProjection::sameOwner(ledger_, ledger)
        || ledger_.get() != ledger.get())
        return "RHI-TEXTURE-DESTINATION-FOREIGN-CONTENT";
    return validate();
}

void WebGpuOriginalTextureUploadDestination::observe(
    std::uint32_t participant, bool proven) noexcept
{
    // Receipt remains observable after rejection/loss; it is never readiness.
    const bool accepted = evidence_.observe(participant, proven);
    if (status() != Status::Pending)
        return;
    if (!accepted || !proven) {
        failure_ = "RHI-TEXTURE-DESTINATION-NATIVE-ERROR";
        status_ = Status::Rejected;
        ledger_->invalidate();
        return;
    }
    if (!evidence_.allReceived())
        return;
    failure_ = validateOriginal();
    status_ = !failure_ && evidence_.proven() ? Status::CreationReady : Status::Rejected;
    if (failure_)
        ledger_->invalidate();
}

const char* WebGpuOriginalTextureUploadDestination::validate() const noexcept
{
    switch (status()) {
    case Status::Prepared:
        return "RHI-TEXTURE-DESTINATION-UNINITIALIZED";
    case Status::Pending:
        return "RHI-TEXTURE-DESTINATION-PENDING";
    case Status::CreationReady:
        return validateOriginal();
    case Status::Rejected:
        return failure_ ? failure_ : "RHI-TEXTURE-DESTINATION-NATIVE-ERROR";
    case Status::DeviceLost:
        return "RHI-TEXTURE-DESTINATION-DEVICE-LOST";
    }
    return "RHI-TEXTURE-DESTINATION-STATE";
}

const char* WebGpuOriginalTextureUploadDestination::executionDiagnostic() const noexcept
{
    if (const auto* id = validate())
        return id;
    return "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN";
}
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
void WebGpuOriginalTextureUploadDestination::completeScope(
    WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView, void* data, void*) noexcept
{
    auto* original = static_cast<Callback*>(data);
    core::UniquePtr<Callback> callback(original,
        core::CpuAllocatedDeleter<Callback>{original, &core::destroyCpuAllocatedUnique<Callback>});
    if (callback && callback->owner)
        callback->owner->observe(callback->participant,
            status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_NoError);
}
#endif

const char* WebGpuOriginalTextureUploadSource::validateBudget(
    const WebGpuOriginalTextureUploadSourceBudget& b) noexcept
{
    if (b.contractVersion != 1u)
        return "RHI-TEXTURE-SOURCE-VERSION";
    if (!b.prototypeOptIn)
        return "RHI-TEXTURE-SOURCE-OPT-IN";
    if (!b.maxCpuMetadataBytes || !b.maxRetainedContentBytes || !b.maxRetainedDestinationTexelBytes
        || !b.maxPackedBytes)
        return "RHI-TEXTURE-SOURCE-BUDGET";
    return WebGpuOriginalBufferUploadSource::validateBudget(b.buffer);
}

std::shared_ptr<WebGpuOriginalTextureUploadSource>
WebGpuOriginalTextureUploadSource::prepareOriginal(
    std::shared_ptr<const WebGpuOriginalTextureUploadDestination> destination,
    std::shared_ptr<WebGpuRhiStagingUploadPool> pool,
    const WebGpuOriginalTextureUploadSourceBudget& b,
    std::shared_ptr<core::CpuMemoryQuota> wrapperQuota,
    std::shared_ptr<core::CpuMemoryQuota> metadataQuota,
    std::shared_ptr<core::CpuMemoryQuota> payloadQuota,
    std::shared_ptr<core::CpuMemoryQuota> resourceQuota,
    const char*& diagnostic)
{
    diagnostic = validateBudget(b);
    if (diagnostic)
        return {};
    if (!wrapperQuota || wrapperQuota->limitBytes() != b.maxCpuMetadataBytes) {
        diagnostic = "RHI-TEXTURE-SOURCE-QUOTA";
        return {};
    }
    if (!destination || !destination->hasQuotaBoundSharedOwner(destination)) {
        diagnostic = "RHI-TEXTURE-SOURCE-DESTINATION-OWNER";
        return {};
    }
    diagnostic = destination->validate();
    if (diagnostic)
        return {};
    if (destination->source()->retainedBytes() > b.maxRetainedContentBytes) {
        diagnostic = "RHI-TEXTURE-SOURCE-CONTENT-BYTE-CAPACITY";
        return {};
    }
    if (destination->resource().allocationBytes > b.maxRetainedDestinationTexelBytes) {
        diagnostic = "RHI-TEXTURE-SOURCE-DESTINATION-BYTE-CAPACITY";
        return {};
    }
    const auto& packing = *destination->source()->packing();
    if (packing.packedBytes().size() > b.maxPackedBytes) {
        diagnostic = "RHI-TEXTURE-SOURCE-PACKED-BYTE-CAPACITY";
        return {};
    }
    try {
        auto buffer =
            WebGpuOriginalBufferUploadSource::prepareOriginalTexture(std::move(destination),
                std::move(pool),
                b.buffer,
                std::move(metadataQuota),
                std::move(payloadQuota),
                std::move(resourceQuota),
                diagnostic);
        if (!buffer)
            return {};
        core::CpuMemoryQuotaScope wrapper(wrapperQuota);
        return shareOriginal(core::makeUnique<WebGpuOriginalTextureUploadSource>(
            ConstructionKey{}, std::move(buffer)));
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-TEXTURE-SOURCE-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-TEXTURE-SOURCE-ALLOCATION";
        return {};
    }
}

const char* WebGpuOriginalTextureUploadSource::validateOriginalJoin() const noexcept
{
    if (!buffer_ || !buffer_->hasQuotaBoundSharedOwner(buffer_) || !destination()
        || !destination()->hasQuotaBoundSharedOwner(destination()))
        return "RHI-TEXTURE-SOURCE-ORIGINAL-OWNER";
    if (const auto* id = destination()->validate())
        return id;
    if (!core::SharedOwnerProjection::sameOwner(buffer_->registry(), destination()->registry()))
        return "RHI-TEXTURE-SOURCE-REGISTRY-JOIN";
    rhi::UploadTextureDesc fields;
    packing()->inputOwner()->scalarFields().writeScalarFields(fields);
    if (buffer_->frameSerial() != fields.frameSerial
        || buffer_->sizeBytes() != packing()->packedBytes().size())
        return "RHI-TEXTURE-SOURCE-ORIGINAL-JOIN";
    return nullptr;
}

const char* WebGpuOriginalTextureUploadSource::initialize(
    const std::shared_ptr<WebGpuOriginalTextureUploadSource>& self) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-TEXTURE-SOURCE-OWNER";
    if (webGpuNativeDeviceLost())
        return "RHI-TEXTURE-SOURCE-DEVICE-LOST";
    if (const auto* id = self->validateOriginalJoin())
        return id;
    return WebGpuOriginalBufferUploadSource::initialize(self->buffer_);
}

WebGpuOriginalTextureUploadSource::Status WebGpuOriginalTextureUploadSource::status() const noexcept
{
    if (webGpuNativeDeviceLost()) {
        if (destination())
            (void)destination()->status();
        return Status::DeviceLost;
    }
    if (validateOriginalJoin())
        return Status::Rejected;
    switch (buffer_->status()) {
    case WebGpuOriginalBufferUploadSource::Status::Prepared:
        return Status::Prepared;
    case WebGpuOriginalBufferUploadSource::Status::Pending:
        return Status::Pending;
    case WebGpuOriginalBufferUploadSource::Status::Ready:
        return buffer_->validate() ? Status::Rejected : Status::Ready;
    case WebGpuOriginalBufferUploadSource::Status::Rejected:
        return Status::Rejected;
    case WebGpuOriginalBufferUploadSource::Status::DeviceLost:
        return Status::DeviceLost;
    }
    return Status::Rejected;
}

const char* WebGpuOriginalTextureUploadSource::validate() const noexcept
{
    if (webGpuNativeDeviceLost())
        return "RHI-TEXTURE-SOURCE-DEVICE-LOST";
    if (const auto* id = validateOriginalJoin())
        return id;
    return buffer_->validate();
}

const char* WebGpuOriginalTextureUploadSource::validateReadyDeclaration() const noexcept
{
    if (webGpuNativeDeviceLost())
        return "RHI-TEXTURE-SOURCE-DEVICE-LOST";
    if (!buffer_ || !buffer_->hasQuotaBoundSharedOwner(buffer_) || !destination()
        || !destination()->hasQuotaBoundSharedOwner(destination()))
        return "RHI-TEXTURE-SOURCE-ORIGINAL-OWNER";
    // Buffer declaration includes exact destination identity and both native
    // readiness ledgers. Initial admission still calls validateOriginalJoin().
    return buffer_->validateReadyDeclaration();
}

const char* WebGpuOriginalTextureUploadSource::executionDiagnostic() const noexcept
{
    if (const auto* id = validate())
        return id;
    return "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN";
}
} // namespace arp::rhi_webgpu
