#include "rhi/backends/metal/metal_native_commands.h"

#include "core/algorithm/preflight_replay.h"
#include "core/memory/cpu_allocator.h"
#include "core/ownership/monotonic_id_sequence.h"
#include "core/ownership/shared_owner_projection.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_binding_validation.h"
#include "rhi/backends/metal/metal_closed_consumer_native_preparation.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_graphics_preparation.h"
#include "rhi/backends/metal/metal_indirect_preparation.h"
#include "rhi/backends/metal/metal_native_descriptors.h"
#include "rhi/backends/metal/metal_native_pipelines.h"
#include "rhi/backends/metal/metal_native_queries.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_native_secondary.h"
#include "rhi/backends/metal/metal_native_sync.h"
#include "rhi/backends/metal/metal_ordinary_binding.h"
#include "rhi/backends/metal/metal_ordinary_graphics_read_consumer.h"
#include "rhi/backends/metal/metal_ordinary_graphics_write_execution.h"
#include "rhi/backends/metal/metal_original_bindless_command.h"
#include "rhi/backends/metal/metal_original_bindless_graphics_pass.h"
#include "rhi/backends/metal/metal_original_device_native_loss.h"
#include "rhi/backends/metal/metal_original_ordinary_command.h"
#include "rhi/backends/metal/metal_original_texture_upload.h"
#include "rhi/backends/metal/metal_staging_upload_pool.h"
#include "rhi/frontend/bindless_graphics_binding.h"
#include "rhi/frontend/bindless_produced_program.h"
#include "rhi/frontend/bindless_root_callback.h"
#include "rhi/frontend/command_attachment_validation.h"
#include "rhi/frontend/command_indirect_commands.h"
#include "rhi/frontend/command_push_consumption.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/frontend/transfer_upload_copy_mapping.h"
#include "rhi/semantics/bindless_push_consumption_lowering.h"
#include "rhi/semantics/metal_direct_binding_validation.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"
#include "rhi/semantics/original_submission_read.h"
#include "rhi/semantics/push_constant_validation.h"

#include <Metal/Metal.h>
#include <TargetConditionals.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

namespace arp::rhi_metal {
bool metalRhiNativeDeviceRemovalResultsAvailable() noexcept
{
#if TARGET_OS_OSX
    if (@available(macOS 10.13, *))
        return true;
#endif
    return false;
}

MetalReadbackCompletion MetalRhiNativeCommandPayload::readbackCompletionStatus() const
{
    // abandonUnsubmitted() deliberately releases the native command buffer
    // after recording its terminal semantic state. Preserve that state for
    // bounded readback finalization instead of misclassifying the released
    // payload as a generic GPU failure.
    if (abandoned_)
        return MetalReadbackCompletion::Abandoned;
    if (recordingPoisoned_ || !nativeCommandBuffer_
        || submissionFailed_.load(std::memory_order_acquire))
        return MetalReadbackCompletion::Failed;
    auto command = reinterpret_cast<id<MTLCommandBuffer>>(nativeCommandBuffer_.get());
    auto waitCommand = reinterpret_cast<id<MTLCommandBuffer>>(nativeSubmitWaitCommandBuffer_.get());
    if (waitCommand && waitCommand.status == MTLCommandBufferStatusError)
        return MetalReadbackCompletion::Failed;
    if (command.status == MTLCommandBufferStatusError)
        return MetalReadbackCompletion::Failed;
    if (command.status == MTLCommandBufferStatusCompleted && waitCommand
        && waitCommand.status != MTLCommandBufferStatusCompleted)
        return MetalReadbackCompletion::Failed;
    return command.status == MTLCommandBufferStatusCompleted ? MetalReadbackCompletion::Completed
                                                             : MetalReadbackCompletion::Pending;
}

namespace {

id<MTLDevice> defaultMetalDevice()
{
    return reinterpret_cast<id<MTLDevice>>(metalRhiNativeDeviceHandle());
}

std::shared_ptr<void> retainNativeObject(id object)
{
    if (object == nil) {
        return {};
    }
    return std::shared_ptr<void>(
        reinterpret_cast<void*>(object),
        [](void* nativeObject) {
            if (nativeObject) {
                id object = reinterpret_cast<id>(nativeObject);
                [object release];
            }
        },
        std::pmr::polymorphic_allocator<std::byte>(&core::defaultCpuMemoryResource()));
}

std::shared_ptr<void> retainBorrowedNativeObject(id object)
{
    if (object == nil) {
        return {};
    }
    [object retain];
    return retainNativeObject(object);
}

bool hasStage(rhi::ShaderStage stages, rhi::ShaderStage stage)
{
    return (static_cast<std::uint32_t>(stages) & static_cast<std::uint32_t>(stage)) != 0u;
}

bool hasResourceUsage(rhi::ResourceUsage usage, rhi::ResourceUsage flag)
{
    return static_cast<std::uint32_t>(usage & flag) != 0u;
}

bool hasTextureUsage(rhi::TextureUsage usage, rhi::TextureUsage flag)
{
    return static_cast<std::uint32_t>(usage & flag) != 0u;
}

bool checkedMultiply(std::uint64_t lhs, std::uint64_t rhs, std::uint64_t& result)
{
    if (lhs != 0u && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

bool checkedAdd(std::uint64_t lhs, std::uint64_t rhs, std::uint64_t& result)
{
    if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

bool fitsNSUInteger(std::uint64_t value)
{
    return value <= static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max());
}

// SubmittedResourceRetention/1 keeps backend-native resource payloads alive
// exactly until Metal reports completion. The command payload itself may be
// released immediately after queue acceptance, so it cannot be the final
// owner of resources referenced by an in-flight native command buffer.
struct MetalRhiSubmittedResourceRetention final {
    MetalRhiSubmittedResourceRetention()
        : resources(&core::defaultCpuMemoryResource()), staging(&core::defaultCpuMemoryResource()),
          argumentBuffers(&core::defaultCpuMemoryResource())
    {}

    core::pmr::Vector<std::shared_ptr<void>> resources;
    core::pmr::Vector<std::shared_ptr<void>> staging;
    core::pmr::Vector<std::shared_ptr<void>> argumentBuffers;
    core::BoundedRetainer<void> bindlessPipelines;
    core::BoundedRetainer<void> bindlessSnapshots;
    core::BoundedRetainer<void> preparedBindings;

    void releaseAfterCompletion()
    {
        // MTLCommandBuffer may retain its completed-handler block until the
        // command buffer object itself is destroyed. Clear the ownership
        // payload here so native resources retire at GPU completion, not at a
        // later logical-command lifetime boundary.
        resources.clear();
        staging.clear();
        argumentBuffers.clear();
        bindlessPipelines.clear();
        bindlessSnapshots.clear();
        preparedBindings.clear();
    }
};

bool textureAspectMatchesFormat(rhi::TextureFormat format, rhi::TextureAspect aspect)
{
    if (format == rhi::TextureFormat::Depth24Stencil8) {
        return aspect == rhi::TextureAspect::Depth || aspect == rhi::TextureAspect::Stencil;
    }
    if (rhi::isDepthTextureFormat(format)) {
        return aspect == rhi::TextureAspect::Depth;
    }
    return aspect == rhi::TextureAspect::Color;
}

std::uint64_t minimumTextureRowBytes(rhi::TextureFormat format, std::uint32_t width);

std::uint32_t textureRowCount(rhi::TextureFormat format, std::uint32_t height);

bool validTextureCopyLayout(rhi::TextureFormat format,
    const rhi::Extent3D& extent,
    std::uint32_t bytesPerRow,
    std::uint32_t rowsPerImage,
    std::uint64_t& bytesPerImage,
    std::uint64_t& requiredBytes)
{
    if (format == rhi::TextureFormat::Unknown || extent.width == 0u || extent.height == 0u
        || extent.depth == 0u || bytesPerRow == 0u || rowsPerImage == 0u) {
        return false;
    }
    const auto requiredRowBytes = minimumTextureRowBytes(format, extent.width);
    const auto requiredRows = textureRowCount(format, extent.height);
    if (requiredRowBytes == 0u || requiredRows == 0u || bytesPerRow < requiredRowBytes
        || rowsPerImage < requiredRows) {
        return false;
    }
    std::uint64_t lastSliceOffset = 0u;
    std::uint64_t lastRowBytes = 0u;
    if (!checkedMultiply(bytesPerRow, rowsPerImage, bytesPerImage)
        || !checkedMultiply(bytesPerImage, extent.depth - 1u, lastSliceOffset)
        || !checkedMultiply(bytesPerRow, requiredRows, lastRowBytes)
        || !checkedAdd(lastSliceOffset, lastRowBytes, requiredBytes)
        || !fitsNSUInteger(bytesPerImage) || !fitsNSUInteger(requiredBytes)) {
        return false;
    }
    return true;
}

id<MTLBuffer> nativeBuffer(const std::shared_ptr<MetalRhiNativeBufferPayload>& payload)
{
    return payload && payload->resource.nativeRuntimeBacked
        ? reinterpret_cast<id<MTLBuffer>>(payload->resource.nativeObject.get())
        : nil;
}

bool sameNativeDevice(const std::shared_ptr<void>& lhs, const std::shared_ptr<void>& rhs)
{
    return lhs && rhs && lhs.get() == rhs.get();
}

bool commandDeviceMatches(
    const std::shared_ptr<void>& commandDevice, const std::shared_ptr<void>& payloadDevice)
{
    return sameNativeDevice(payloadDevice, commandDevice);
}

template <typename Payload>
void retainResourcePayload(core::pmr::Vector<std::shared_ptr<void>>& retained,
    core::pmr::Vector<const void*>& retainedIdentities,
    const std::shared_ptr<Payload>& payload)
{
    if (payload
        && std::find(retainedIdentities.begin(), retainedIdentities.end(), payload.get())
            == retainedIdentities.end()) {
        retainedIdentities.push_back(payload.get());
        retained.emplace_back(payload);
    }
}

void retainRegisteredResourcePayloads(core::pmr::Vector<std::shared_ptr<void>>& retained,
    core::pmr::Vector<const void*>& retainedIdentities,
    const MetalRhiRegisteredResource& resource)
{
    retainResourcePayload(retained, retainedIdentities, resource.buffer);
    retainResourcePayload(retained, retainedIdentities, resource.texture);
    retainResourcePayload(retained, retainedIdentities, resource.textureView);
    retainResourcePayload(retained, retainedIdentities, resource.sampler);
}

bool sameDescriptorGroupSnapshot(const MetalRhiNativeDescriptorGroupPayload& lhs,
    const MetalRhiNativeDescriptorGroupPayload& rhs)
{
    if (lhs.bindings.size() != rhs.bindings.size())
        return false;
    for (std::size_t index = 0u; index < lhs.bindings.size(); ++index) {
        const auto& left = lhs.bindings[index];
        const auto& right = rhs.bindings[index];
        if (left.binding != right.binding || left.arrayCount != right.arrayCount
            || left.arrayElement != right.arrayElement || left.type != right.type
            || left.stages != right.stages || left.depthTexture != right.depthTexture
            || left.resourceKind != right.resourceKind || left.argumentKind != right.argumentKind
            || left.dynamicOffsetIndex != right.dynamicOffsetIndex
            || left.buffer.get() != right.buffer.get()
            || left.textureView.get() != right.textureView.get()
            || left.sampler.get() != right.sampler.get())
            return false;
    }
    return true;
}

bool sameDescriptorTableSnapshot(const MetalRhiNativeDescriptorTablePayload& lhs,
    const MetalRhiNativeDescriptorTablePayload& rhs)
{
    if (lhs.desc.type != rhs.desc.type || lhs.desc.capacity != rhs.desc.capacity
        || lhs.desc.stages != rhs.desc.stages || lhs.entries.size() != rhs.entries.size()) {
        return false;
    }
    for (std::size_t index = 0u; index < lhs.entries.size(); ++index) {
        const auto& left = lhs.entries[index];
        const auto& right = rhs.entries[index];
        if (left.index != right.index || left.type != right.type
            || left.resourceKind != right.resourceKind || left.buffer.get() != right.buffer.get()
            || left.textureView.get() != right.textureView.get()
            || left.sampler.get() != right.sampler.get())
            return false;
    }
    return true;
}

bool validTessellationFactorBinding(const std::shared_ptr<MetalRhiNativePipelinePayload>& pipeline,
    const std::shared_ptr<MetalRhiNativeBufferPayload>& factorBuffer,
    std::uint64_t factorOffset,
    std::uint32_t instanceStride,
    std::uint32_t instanceCount,
    std::pmr::string& diagnostics)
{
    id<MTLBuffer> native = nativeBuffer(factorBuffer);
    if (!pipeline || pipeline->primitiveTopology != rhi::PrimitiveTopology::PatchList
        || !pipeline->metalTessellationAbiRecorded
        || pipeline->metalTessellationFactorBytesPerPatch == 0u
        || instanceStride != pipeline->metalTessellationFactorInstanceStrideBytes || native == nil
        || instanceCount == 0u || (factorOffset & 3u) != 0u || (instanceStride & 3u) != 0u
        || !fitsNSUInteger(factorOffset)) {
        diagnostics = "RHI-METAL-TESSELLATION-FACTOR-BUFFER-ABI-MISSING";
        return false;
    }
    std::uint64_t lastInstanceOffset = 0u;
    if (!checkedMultiply(
            static_cast<std::uint64_t>(instanceCount - 1u), instanceStride, lastInstanceOffset)
        || !checkedAdd(
            lastInstanceOffset, pipeline->metalTessellationFactorBytesPerPatch, lastInstanceOffset)
        || !checkedAdd(factorOffset, lastInstanceOffset, lastInstanceOffset)
        || lastInstanceOffset > native.length) {
        diagnostics = "RHI-METAL-TESSELLATION-FACTOR-BUFFER-RANGE-INVALID";
        return false;
    }
    return true;
}

id<MTLTexture> nativeTexture(const std::shared_ptr<MetalRhiNativeTextureViewPayload>& payload)
{
    return payload && payload->resource.nativeRuntimeBacked
        ? reinterpret_cast<id<MTLTexture>>(payload->resource.nativeObject.get())
        : nil;
}

id<MTLTexture> nativeRegisteredTexture(const MetalRhiRegisteredResource& resource)
{
    if (resource.texture && resource.texture->resource.nativeRuntimeBacked) {
        return reinterpret_cast<id<MTLTexture>>(resource.texture->resource.nativeObject.get());
    }
    if (resource.textureView && resource.textureView->resource.nativeRuntimeBacked) {
        return reinterpret_cast<id<MTLTexture>>(resource.textureView->resource.nativeObject.get());
    }
    return nil;
}

bool isMemorylessAttachment(const MetalRhiRegisteredResource& resource)
{
    return (resource.texture
               && resource.texture->resource.storageMode == MetalRhiNativeStorageMode::Memoryless)
        || (resource.textureView
            && resource.textureView->resource.storageMode == MetalRhiNativeStorageMode::Memoryless);
}

std::shared_ptr<void> nativeRegisteredDevice(const MetalRhiRegisteredResource& resource)
{
    if (resource.buffer)
        return resource.buffer->resource.nativeDevice;
    if (resource.texture)
        return resource.texture->resource.nativeDevice;
    if (resource.textureView)
        return resource.textureView->resource.nativeDevice;
    if (resource.sampler)
        return resource.sampler->resource.nativeDevice;
    return {};
}

id<MTLSamplerState> nativeSampler(const std::shared_ptr<MetalRhiNativeSamplerPayload>& payload)
{
    return payload && payload->resource.nativeRuntimeBacked
        ? reinterpret_cast<id<MTLSamplerState>>(payload->resource.nativeObject.get())
        : nil;
}

struct MetalRhiPreparedArgumentEncoding {
    id<MTLArgumentEncoder> encoder = nil;
    id<MTLBuffer> argumentBuffer = nil;
    std::uint32_t argumentBufferSlot = 0u;
    rhi::ShaderStage stage = rhi::ShaderStage::None;
};

void releasePreparedArgumentEncodings(
    core::pmr::Vector<MetalRhiPreparedArgumentEncoding>& encodings)
{
    for (auto& encoding : encodings) {
        if (encoding.encoder != nil) {
            [encoding.encoder release];
            encoding.encoder = nil;
        }
        if (encoding.argumentBuffer != nil) {
            [encoding.argumentBuffer release];
            encoding.argumentBuffer = nil;
        }
    }
}

bool prepareArgumentEncoding(id<MTLFunction> function,
    std::uint32_t argumentBufferSlot,
    rhi::ShaderStage stage,
    MetalRhiPreparedArgumentEncoding& result,
    std::pmr::string& diagnostics)
{
    if (!@available(macOS 10.13, iOS 11.0, *)) {
        diagnostics = "RHI-METAL-DESCRIPTOR-ARGUMENT-BUFFER-API-UNAVAILABLE";
        return false;
    }
    if (function == nil || argumentBufferSlot >= 31u) {
        diagnostics = "RHI-METAL-DESCRIPTOR-ARGUMENT-ENCODER-INPUT-INVALID";
        return false;
    }
    id<MTLArgumentEncoder> encoder =
        [function newArgumentEncoderWithBufferIndex:argumentBufferSlot];
    if (encoder == nil || encoder.encodedLength == 0u) {
        [encoder release];
        diagnostics = "RHI-METAL-DESCRIPTOR-ARGUMENT-ENCODER-MISSING";
        return false;
    }
    id<MTLDevice> device = defaultMetalDevice();
    id<MTLBuffer> argumentBuffer = device != nil
        ? [device newBufferWithLength:encoder.encodedLength options:MTLResourceStorageModeShared]
        : nil;
    if (argumentBuffer == nil) {
        [encoder release];
        diagnostics = "RHI-METAL-DESCRIPTOR-ARGUMENT-BUFFER-CREATE-FAILED";
        return false;
    }
    result.encoder = encoder;
    result.argumentBuffer = argumentBuffer;
    result.argumentBufferSlot = argumentBufferSlot;
    result.stage = stage;
    return true;
}

bool validTextureRegion(id<MTLTexture> texture,
    rhi::TextureFormat format,
    const rhi::TextureSubresourceRange& range,
    const rhi::Extent3D& extent)
{
    if (texture == nil || range.mipLevelCount == 0 || range.arrayLayerCount == 0
        || range.mipLevelCount != 1 || range.arrayLayerCount != 1
        || range.baseMipLevel >= texture.mipmapLevelCount
        || range.baseArrayLayer >= texture.arrayLength
        || !textureAspectMatchesFormat(format, range.aspect) || extent.width == 0
        || extent.height == 0 || extent.depth == 0) {
        return false;
    }
    const auto mipWidth = std::max<NSUInteger>(1u, texture.width >> range.baseMipLevel);
    const auto mipHeight = std::max<NSUInteger>(1u, texture.height >> range.baseMipLevel);
    const auto mipDepth = std::max<NSUInteger>(1u, texture.depth >> range.baseMipLevel);
    return extent.width <= mipWidth && extent.height <= mipHeight && extent.depth <= mipDepth;
}

bool validTextureRegionAt(id<MTLTexture> texture,
    rhi::TextureFormat format,
    const rhi::TextureSubresourceRange& range,
    const rhi::Extent3D& extent,
    const rhi::Extent3D& origin)
{
    if (!validTextureRegion(texture, format, range, extent)) {
        return false;
    }
    const auto mipWidth = std::max<NSUInteger>(1u, texture.width >> range.baseMipLevel);
    const auto mipHeight = std::max<NSUInteger>(1u, texture.height >> range.baseMipLevel);
    const auto mipDepth = std::max<NSUInteger>(1u, texture.depth >> range.baseMipLevel);
    return origin.width <= mipWidth && origin.height <= mipHeight && origin.depth <= mipDepth
        && extent.width <= mipWidth - origin.width && extent.height <= mipHeight - origin.height
        && extent.depth <= mipDepth - origin.depth;
}

struct MetalTextureCopyPlan {
    std::uint32_t sourceMipLevel = 0u;
    std::uint32_t destinationMipLevel = 0u;
    rhi::Extent3D extent;
    std::uint32_t bytesPerRow = 0u;
    std::uint64_t bytesPerImage = 0u;
    std::uint64_t requiredBytes = 0u;
    std::uint64_t bufferOffsetBytes = 0u;
};

std::uint64_t minimumTextureRowBytes(rhi::TextureFormat format, std::uint32_t width)
{
    if (rhi::isBlockCompressed(format)) {
        return static_cast<std::uint64_t>(rhi::blocksForExtent(width, rhi::blockExtent(format)))
            * rhi::bytesPerBlock(format);
    }
    return static_cast<std::uint64_t>(width) * rhi::bytesPerPixel(format);
}

std::uint32_t textureRowCount(rhi::TextureFormat format, std::uint32_t height)
{
    return rhi::isBlockCompressed(format) ? rhi::blocksForExtent(height, rhi::blockExtent(format))
                                          : height;
}

MTLCullMode nativeCullMode(rhi::CullMode mode)
{
    switch (mode) {
    case rhi::CullMode::None:
        return MTLCullModeNone;
    case rhi::CullMode::Front:
        return MTLCullModeFront;
    case rhi::CullMode::Back:
        return MTLCullModeBack;
    }
    return MTLCullModeNone;
}

MTLWinding nativeWinding(rhi::FrontFace face)
{
    return face == rhi::FrontFace::Clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise;
}

MTLIndexType nativeIndexType(rhi::IndexFormat format)
{
    return format == rhi::IndexFormat::Uint16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
}

} // namespace

class MetalRhiNativeCommandPayload::SubmissionRetention final {
public:
    MetalOriginalQueueAcceptance queueAcceptance;
    core::UniquePtr<rhi::RecordedStagingUploadJournal> stagingUploads;
    std::shared_ptr<MetalOriginalCommandCustody> originalCommandCustody;
    MetalRhiSubmittedResourceRetention resources;
    MetalRhiSubmittedQueryRetention queries;
    core::UniquePtr<rhi::common::CommandRetentionStorage> commandRetention;
    core::pmr::Vector<PendingBindlessFaultReadback> faultReadbacks{
        &core::defaultCpuMemoryResource()};
    MetalRhiCompletionState defaultCompletion;
    std::shared_ptr<MetalRhiOrdinaryPreparedWriteReadback> ordinaryWriteReadback;
    std::shared_ptr<const MetalRhiIndirectExecutionPreparation> indirectExecution;
    MetalRhiIndirectOutputRecordingLease indirectLease;
    std::shared_ptr<const MetalRhiCanonicalProducerRecordingPreparation> canonicalProducer;
    rhi::BindlessFaultSubmissionLease canonicalFaultLease;
    std::shared_ptr<const MetalRhiClosedConsumerNativePreparation> closedConsumer;
    std::shared_ptr<core::AtomicCapacityLease> commandCapacity;
    std::shared_ptr<std::atomic<std::uint64_t>> stagingEpoch;

    static bool resolveOrdinary(void* opaque, bool success) noexcept
    {
        auto& retained = *static_cast<SubmissionRetention*>(opaque);
        const bool valid = retained.originalCommandCustody
            && retained.originalCommandCustody->nativeCompleted(success);
        retained.originalCommandCustody.reset();
        const bool hadStaging = retained.stagingUploads && !retained.stagingUploads->empty();
        if (retained.stagingUploads) {
            (void)retained.stagingUploads->retireCompleted();
            retained.stagingUploads.reset();
        }
        if (hadStaging && retained.stagingEpoch)
            retained.stagingEpoch->fetch_add(1u, std::memory_order_release);
        retained.resources.releaseAfterCompletion();
        if (retained.commandRetention) {
            retained.commandRetention->retireAfterCompletion(true);
            retained.commandRetention.reset();
        }
        if (retained.commandCapacity)
            retained.commandCapacity->release();
        retained.commandCapacity.reset();
        retained.stagingEpoch.reset();
        retained.defaultCompletion.complete(success && valid);
        return success && valid;
    }
};

std::shared_ptr<MetalRhiCompletionState>
MetalRhiNativeCommandPayload::preparedCompletionState() const
{
    return submissionRetention_ ? std::shared_ptr<MetalRhiCompletionState>(submissionRetention_,
                                      &submissionRetention_->defaultCompletion)
                                : nullptr;
}

std::shared_ptr<const MetalOriginalQueueAcceptance>
MetalRhiNativeCommandPayload::originalQueueAcceptance() const noexcept
{
    if (!submissionRetention_ || !submissionRetention_->queueAcceptance.ordinal())
        return {};
    return {submissionRetention_, &submissionRetention_->queueAcceptance};
}

bool MetalRhiNativeCommandPayload::publishOriginalQueueAcceptance() const noexcept
{
    if (!submissionRetention_)
        return false;
    auto& receipt = submissionRetention_->queueAcceptance;
    if (!receipt.nativeAccepted_ || !receipt.ordinal_ || receipt.device_.diagnostic()
        || !receipt.completion_
        || receipt.completion_->outcome_.load(std::memory_order_acquire)
            == MetalRhiCompletionState::Outcome::Failed)
        return false;
    // Ownership acknowledgement must not recursively poll/commit the journal.
    // Owner completion observation starts only after this acceptance is issued.
    receipt.published_.store(true, std::memory_order_release);
    return true;
}

std::shared_ptr<void> sharedMetalRhiNativeCommandQueue()
{
    static const std::shared_ptr<void> queue = [] {
        // Once-only process owner, never a caller's package/preparation charge.
        core::CpuMemoryQuotaScope sharedScope(nullptr);
        id<MTLDevice> device = defaultMetalDevice();
        id<MTLCommandQueue> nativeQueue = device != nil
            ? [device newCommandQueueWithMaxCommandBufferCount:kMetalNativeCommandBufferCapacity]
            : nil;
        return retainNativeObject(nativeQueue);
    }();
    return queue;
}

namespace {
struct OriginalQueueCommitOrder {
    std::mutex mutex;
    core::MonotonicIdSequence ordinals;
};

OriginalQueueCommitOrder& originalQueueCommitOrder()
{
    // Exactly the one native queue above, no map, guessed limit or hot allocation.
    static OriginalQueueCommitOrder order;
    return order;
}
} // namespace

std::shared_ptr<core::AtomicCapacityBudget> sharedMetalRhiCommandCapacityBudget()
{
    static const auto capacity = [] {
        core::CpuMemoryQuotaScope sharedScope(nullptr);
        return core::makeShared<core::AtomicCapacityBudget>(kMetalNativeCommandBufferCapacity);
    }();
    return capacity;
}

std::shared_ptr<core::AtomicCapacityLease> reserveSharedMetalRhiCommandCapacity(
    std::uint64_t count, std::pmr::string& diagnostics)
{
    auto reservation =
        core::makeShared<core::AtomicCapacityLease>(sharedMetalRhiCommandCapacityBudget(), count);
    if (!reservation->charged()) {
        diagnostics = "RHI-METAL-COMMAND-CAPACITY-EXHAUSTED";
        return {};
    }
    return reservation;
}

MetalRhiCommandCapacityStats inspectSharedMetalRhiCommandCapacity() noexcept
{
    const auto capacity = sharedMetalRhiCommandCapacityBudget();
    return {
        capacity->capacity(),
        capacity->used(),
        capacity->peak(),
        capacity->rejectionCount(),
    };
}

namespace {

bool ordinaryComputePipelineValid(const MetalRhiNativePipelinePayload& pipeline,
    const std::shared_ptr<void>& commandDevice) noexcept
{
    const auto device = reinterpret_cast<id<MTLDevice>>(commandDevice.get());
    const auto state = reinterpret_cast<id<MTLComputePipelineState>>(pipeline.pipelineState.get());
    const auto function = reinterpret_cast<id<MTLFunction>>(pipeline.computeFunction.get());
    return pipeline.nativeRuntimeBacked && pipeline.computePipelinePayloadPresent && device != nil
        && commandDeviceMatches(commandDevice, pipeline.nativeDevice) && state != nil
        && state.device == device && function != nil && function.device == device
        && function.functionType == MTLFunctionTypeKernel && pipeline.computeWorkgroupSizeX
        && pipeline.computeWorkgroupSizeY && pipeline.computeWorkgroupSizeZ;
}

class MetalRhiNativeArgumentPreparationBudget final {
public:
    MetalRhiNativeArgumentPreparationBudget(const rhi::BindlessBindingBudget& budget, bool compute)
        : counts(budget.maxArgumentBuffers), encodedBytes(budget.maxArgumentBufferBytes),
          allocatedBytes(budget.maxArgumentBufferBytes), compute_(compute)
    {}

    bool fail(core::pmr::String& diagnostic, std::string_view suffix) const
    {
        diagnostic = compute_ ? "RHI-METAL-PREPARED-COMPUTE-" : "RHI-METAL-PREPARED-GRAPHICS-";
        diagnostic.append(suffix.data(), suffix.size());
        return false;
    }

    core::AtomicCapacityBudget counts, encodedBytes, allocatedBytes;

private:
    bool compute_;
};

// Both domains use the same function-specific preflight and encoding algorithm.
// Graphics preflights every selected function before allocating any storage.
class MetalRhiNativeFunctionBindingPreparation final {
public:
    MetalRhiNativeFunctionBindingPreparation(
        std::shared_ptr<MetalRhiNativePipelinePayload> pipeline,
        std::shared_ptr<const MetalRhiNativeDescriptorGroupPayload> descriptors,
        std::uint32_t set,
        rhi::ShaderStage stage,
        std::shared_ptr<void> function,
        MetalRhiPreparedBindingLayoutValidator layout,
        std::string_view entry = {})
        : pipeline_(std::move(pipeline)), descriptors_(std::move(descriptors)), set_(set),
          stage_(stage), function_(std::move(function)), layout_(layout), entry_(entry)
    {}

    bool preflight(MetalRhiNativeArgumentPreparationBudget& budget, core::pmr::String& diagnostic)
    {
        if (!@available(macOS 10.13, iOS 11.0, *))
            return budget.fail(diagnostic, "API-UNAVAILABLE");
        if (!pipeline_ || !descriptors_ || !pipeline_->nativeRuntimeBacked
            || !descriptors_->nativeRuntimeBacked || !function_ || !pipeline_->nativeDevice
            || pipeline_->nativeDevice != descriptors_->nativeDevice)
            return budget.fail(diagnostic, "PAYLOAD-MISSING");
        if (const auto* id = MetalRhiPreparedDescriptorIdentityValidator::validate(*descriptors_)) {
            diagnostic = id;
            return false;
        }
        const auto valid = layout_.validate(*descriptors_, set_);
        if (!valid.success) {
            diagnostic = valid.diagnostics;
            return false;
        }
        const auto function = reinterpret_cast<id<MTLFunction>>(function_.get());
        const auto device = reinterpret_cast<id<MTLDevice>>(pipeline_->nativeDevice.get());
        if (function.device != device)
            return budget.fail(diagnostic, "DEVICE-MISMATCH");
        const bool expectedType = stage_ == rhi::ShaderStage::Compute
            ? function.functionType == MTLFunctionTypeKernel
            : stage_ == rhi::ShaderStage::Vertex ? function.functionType == MTLFunctionTypeVertex
                                                 : stage_ == rhi::ShaderStage::Fragment
                && function.functionType == MTLFunctionTypeFragment;
        if (!expectedType
            || (!entry_.empty()
                && (!function.name.UTF8String || entry_ != function.name.UTF8String)))
            return budget.fail(diagnostic, "FUNCTION-STAGE-ENTRY-MISMATCH");
        std::size_t arrayCount = 0u;
        for (const auto& binding : descriptors_->bindings) {
            MetalRhiPreparedBindingLocation location;
            const auto mapped = layout_.resolve(set_, binding, location);
            if (!mapped.success) {
                diagnostic = mapped.diagnostics;
                return false;
            }
            if (!location.active)
                continue;
            id<MTLDevice> actualDevice = nil;
            std::shared_ptr<void> resourceDevice;
            switch (binding.argumentKind) {
            case MetalRhiArgumentKind::Buffer:
                if (const auto buffer = nativeBuffer(binding.buffer))
                    actualDevice = buffer.device;
                if (binding.buffer)
                    resourceDevice = binding.buffer->resource.nativeDevice;
                break;
            case MetalRhiArgumentKind::Texture:
                if (const auto texture = nativeTexture(binding.textureView))
                    actualDevice = texture.device;
                if (binding.textureView)
                    resourceDevice = binding.textureView->resource.nativeDevice;
                break;
            case MetalRhiArgumentKind::Sampler:
                if (location.array && binding.sampler && !binding.sampler->argumentBufferCompatible)
                    return budget.fail(diagnostic, "SAMPLER-ARGUMENT-BUFFER-INCOMPATIBLE");
                if (const auto sampler = nativeSampler(binding.sampler))
                    actualDevice = sampler.device;
                if (binding.sampler)
                    resourceDevice = binding.sampler->resource.nativeDevice;
                break;
            }
            if (!resourceDevice || resourceDevice != pipeline_->nativeDevice
                || actualDevice != device)
                return budget.fail(diagnostic, "RESOURCE-MISSING");
            if (location.array && binding.arrayElement == 0u)
                ++arrayCount;
        }
        if (arrayCount && !budget.counts.tryAcquire(arrayCount))
            return budget.fail(diagnostic, "ARGUMENT-COUNT-BUDGET");
        encoders_.reserve(arrayCount);
        for (const auto& binding : descriptors_->bindings) {
            MetalRhiPreparedBindingLocation location;
            layout_.resolve(set_, binding, location);
            if (!location.active || !location.array || binding.arrayElement != 0u)
                continue;
            const auto encoder = [function newArgumentEncoderWithBufferIndex:location.slot];
            auto owner = retainNativeObject(encoder);
            if (!owner || !encoder.encodedLength)
                return budget.fail(diagnostic, "ARGUMENT-ENCODER-MISSING");
            if (!budget.encodedBytes.tryAcquire(encoder.encodedLength))
                return budget.fail(diagnostic, "ARGUMENT-BYTE-BUDGET");
            encoders_.push_back(
                {binding.binding, location.slot, location.argumentIndex, std::move(owner)});
        }
        return true;
    }

    template <class Prepared>
    std::shared_ptr<const Prepared> build(
        MetalRhiNativeArgumentPreparationBudget& budget, core::pmr::String& diagnostic) const
    {
        if (const auto* id = MetalRhiPreparedDescriptorIdentityValidator::validate(*descriptors_)) {
            diagnostic = id;
            return {};
        }
        auto result = core::makeShared<Prepared>();
        result->pipeline = pipeline_;
        result->descriptors = descriptors_;
        result->set = set_;
        result->stage = stage_;
        result->function = function_;
        result->arrays.reserve(encoders_.size());
        result->direct.reserve(descriptors_->bindings.size());
        const auto fail = [&](std::string_view suffix) -> std::shared_ptr<const Prepared> {
            budget.fail(diagnostic, suffix);
            return {};
        };
        for (const auto& prepared : encoders_) {
            const auto encoder = reinterpret_cast<id<MTLArgumentEncoder>>(prepared.encoder.get());
            auto storage = createMetalRhiTrackedNativeBufferObject(pipeline_->nativeDevice,
                encoder.encodedLength,
                MetalRhiNativeStorageMode::Shared,
                diagnostic);
            if (!storage)
                return {};
            const auto buffer = reinterpret_cast<id<MTLBuffer>>(storage.get());
            if (!buffer.allocatedSize || !budget.allocatedBytes.tryAcquire(buffer.allocatedSize))
                return fail("ARGUMENT-BYTE-BUDGET");
            result->argumentBytes += buffer.allocatedSize;
            if (!buffer.contents)
                return fail("ARGUMENT-BUFFER-NOT-HOST-VISIBLE");
            std::memset(buffer.contents, 0, buffer.length);
            [encoder setArgumentBuffer:buffer offset:0u];
            for (const auto& binding : descriptors_->bindings) {
                if (binding.binding != prepared.binding)
                    continue;
                const auto index = prepared.firstIndex + binding.arrayElement;
                switch (binding.argumentKind) {
                case MetalRhiArgumentKind::Buffer:
                    [encoder setBuffer:nativeBuffer(binding.buffer) offset:0u atIndex:index];
                    break;
                case MetalRhiArgumentKind::Texture:
                    [encoder setTexture:nativeTexture(binding.textureView) atIndex:index];
                    break;
                case MetalRhiArgumentKind::Sampler:
                    [encoder setSamplerState:nativeSampler(binding.sampler) atIndex:index];
                    break;
                }
            }
            MetalRhiPreparedFunctionArgumentBuffer argument{
                prepared.binding, prepared.slot, std::move(storage)};
            argument.preparedNativeIdentity =
                MetalRhiNativeResourceObservation(pipeline_->nativeDevice, argument.buffer);
            result->arrays.push_back(std::move(argument));
        }
        for (std::size_t i = 0u; i < descriptors_->bindings.size(); ++i) {
            MetalRhiPreparedBindingLocation location;
            layout_.resolve(set_, descriptors_->bindings[i], location);
            if (location.active && !location.array)
                result->direct.push_back({static_cast<std::uint32_t>(i), location.slot});
        }
        return result;
    }

private:
    struct ArgumentEncoder {
        std::uint32_t binding, slot, firstIndex;
        std::shared_ptr<void> encoder;
    };

    std::shared_ptr<MetalRhiNativePipelinePayload> pipeline_;
    std::shared_ptr<const MetalRhiNativeDescriptorGroupPayload> descriptors_;
    std::uint32_t set_;
    rhi::ShaderStage stage_;
    std::shared_ptr<void> function_;
    MetalRhiPreparedBindingLayoutValidator layout_;
    std::string_view entry_;
    core::pmr::Vector<ArgumentEncoder> encoders_{&core::defaultCpuMemoryResource()};
};

class MetalRhiNativeFunctionBindingRecording final {
public:
    MetalRhiNativeFunctionBindingRecording(const MetalRhiNativePreparedFunctionBindings& prepared,
        MetalRhiPreparedBindingLayoutValidator layout)
        : prepared_(prepared), layout_(layout)
    {}

    bool validate(core::pmr::String& diagnostic) const
    {
        const auto fail = [&] {
            diagnostic = prepared_.stage == rhi::ShaderStage::Compute
                ? "RHI-METAL-PREPARED-COMPUTE-STORAGE-MISMATCH"
                : "RHI-METAL-PREPARED-GRAPHICS-STORAGE-MISMATCH";
            return false;
        };
        if (!prepared_.pipeline || !prepared_.descriptors || !prepared_.function)
            return fail();
        if (const auto* id =
                MetalRhiPreparedDescriptorIdentityValidator::validate(*prepared_.descriptors)) {
            diagnostic = id;
            return false;
        }
        const auto valid = layout_.validate(*prepared_.descriptors, prepared_.set);
        if (!valid.success) {
            diagnostic = valid.diagnostics;
            return false;
        }
        const auto device = reinterpret_cast<id<MTLDevice>>(prepared_.pipeline->nativeDevice.get());
        const auto function = reinterpret_cast<id<MTLFunction>>(prepared_.function.get());
        if (device == nil || function.device != device
            || prepared_.descriptors->nativeDevice != prepared_.pipeline->nativeDevice)
            return fail();
        std::size_t expectedDirect = 0u, expectedArrays = 0u;
        for (std::size_t i = 0u; i < prepared_.descriptors->bindings.size(); ++i) {
            const auto& binding = prepared_.descriptors->bindings[i];
            MetalRhiPreparedBindingLocation location;
            const auto mapped = layout_.resolve(prepared_.set, binding, location);
            if (!mapped.success) {
                diagnostic = mapped.diagnostics;
                return false;
            }
            if (!location.active)
                continue;
            if (prepared_.stage == rhi::ShaderStage::Compute
                && (binding.bufferOffsetBytes || binding.bufferOffsetAlignment
                    || binding.bufferRequiredBytes)) {
                if (location.array || binding.arrayCount != 1u
                    || binding.type != rhi::DescriptorType::UniformBuffer || !binding.buffer)
                    return fail();
                const auto buffer = nativeBuffer(binding.buffer);
                const MetalRhiBufferBindingFact* fact = nullptr;
                if (const auto* id =
                        validateMetalRhiBufferOffset(prepared_.pipeline->computeBufferFacts,
                            prepared_.pipeline->computeBufferFactsVersion,
                            location.slot,
                            binding.bufferOffsetBytes,
                            buffer ? buffer.length : 0u,
                            fact)) {
                    diagnostic = id;
                    return false;
                }
                if (fact->alignment != binding.bufferOffsetAlignment
                    || fact->requiredBytes != binding.bufferRequiredBytes)
                    return fail();
            }
            if (prepared_.stage == rhi::ShaderStage::Vertex
                || prepared_.stage == rhi::ShaderStage::Fragment) {
                const auto stageIndex = prepared_.stage == rhi::ShaderStage::Vertex ? 0u : 1u;
                const auto& pointee = binding.graphicsBufferPointeeRanges[stageIndex];
                if (pointee.stage != rhi::ShaderStage::None) {
                    if (!location.array
                        || !MetalRhiOrdinaryGraphicsResourceDomain::isReadOnlyBufferArray(
                            binding.type, binding.arrayCount)
                        || !binding.buffer || binding.bufferOffsetBytes
                        || binding.bufferOffsetAlignment || binding.bufferRequiredBytes
                        || binding.graphicsBufferRanges[stageIndex].stage != rhi::ShaderStage::None)
                        return fail();
                    if (!pointee.nativeIdentity.matches(binding.buffer->resource)) {
                        diagnostic = "RHI-METAL-ORDINARY-POINTEE-NATIVE-IDENTITY-MISMATCH";
                        return false;
                    }
                    const auto buffer = nativeBuffer(binding.buffer);
                    if (const auto* id = MetalRhiStageBufferPointeeRangeValidator::validate(
                            prepared_.pipeline->graphicsBufferPointeeFacts[stageIndex],
                            prepared_.stage,
                            location.slot,
                            location.argumentIndex + binding.arrayElement,
                            binding.arrayCount,
                            binding.arrayElement,
                            binding.bufferOffsetBytes,
                            buffer ? buffer.length : 0u,
                            pointee)) {
                        diagnostic = id;
                        return false;
                    }
                }
                const auto& captured = binding.graphicsBufferRanges[stageIndex];
                if (captured.stage != rhi::ShaderStage::None || binding.bufferOffsetBytes
                    || binding.bufferOffsetAlignment || binding.bufferRequiredBytes) {
                    if (location.array || binding.arrayCount != 1u
                        || !MetalRhiOrdinaryGraphicsResourceDomain::isReadOnlyBuffer(binding.type)
                        || !binding.buffer || binding.bufferOffsetAlignment
                        || binding.bufferRequiredBytes)
                        return fail();
                    if (!captured.nativeIdentity.matches(binding.buffer->resource)) {
                        diagnostic = "RHI-METAL-ORDINARY-OFFSET-NATIVE-IDENTITY-MISMATCH";
                        return false;
                    }
                    const auto buffer = nativeBuffer(binding.buffer);
                    if (const auto* id = MetalRhiStageBufferRangeValidator::validate(
                            prepared_.pipeline->graphicsBufferFacts[stageIndex],
                            prepared_.stage,
                            location.slot,
                            binding.bufferOffsetBytes,
                            buffer ? buffer.length : 0u,
                            captured)) {
                        diagnostic = id;
                        return false;
                    }
                }
            }
            id<MTLDevice> actualDevice = nil;
            switch (binding.argumentKind) {
            case MetalRhiArgumentKind::Buffer:
                if (const auto buffer = nativeBuffer(binding.buffer))
                    actualDevice = buffer.device;
                break;
            case MetalRhiArgumentKind::Texture:
                if (const auto texture = nativeTexture(binding.textureView))
                    actualDevice = texture.device;
                break;
            case MetalRhiArgumentKind::Sampler:
                if (const auto sampler = nativeSampler(binding.sampler))
                    actualDevice = sampler.device;
                break;
            }
            if (actualDevice != device)
                return fail();
            unsigned matches = 0u;
            if (location.array) {
                if (binding.arrayElement != 0u)
                    continue;
                ++expectedArrays;
                for (const auto& array : prepared_.arrays) {
                    if (array.binding != binding.binding || array.slot != location.slot)
                        continue;
                    if (!array.preparedNativeIdentity.matches(
                            prepared_.pipeline->nativeDevice, array.buffer)) {
                        diagnostic = "RHI-METAL-PREPARED-ARGUMENT-STORAGE-IDENTITY-MISMATCH";
                        return false;
                    }
                    const auto storage = reinterpret_cast<id<MTLBuffer>>(array.buffer.get());
                    if (storage == nil || storage.device != device || !storage.length)
                        return fail();
                    ++matches;
                }
            } else {
                ++expectedDirect;
                for (const auto& direct : prepared_.direct)
                    if (direct.element == i && direct.slot == location.slot)
                        ++matches;
            }
            if (matches != 1u)
                return fail();
        }
        return expectedDirect == prepared_.direct.size()
                && expectedArrays == prepared_.arrays.size()
            ? true
            : fail();
    }

    void record(id<MTLComputeCommandEncoder> compute, id<MTLRenderCommandEncoder> render) const
    {
        const bool vertex = prepared_.stage == rhi::ShaderStage::Vertex;
        const auto stages = vertex ? MTLRenderStageVertex : MTLRenderStageFragment;
        for (const auto& binding : prepared_.descriptors->bindings) {
            MetalRhiPreparedBindingLocation location;
            layout_.resolve(prepared_.set, binding, location);
            if (!location.active || !location.array)
                continue;
            const auto use = metalRhiArgumentResourceUse(binding.type);
            id<MTLResource> resource = nil;
            if (binding.buffer)
                resource = nativeBuffer(binding.buffer);
            else if (binding.textureView)
                resource = nativeTexture(binding.textureView);
            if (resource == nil)
                continue; // Samplers have no MTLResource residency.
            const auto usage = use == MetalRhiArgumentResourceUse::ReadWrite
                ? MTLResourceUsageRead | MTLResourceUsageWrite
                : use == MetalRhiArgumentResourceUse::ReadSample
                ? MTLResourceUsageRead | MTLResourceUsageSample
                : MTLResourceUsageRead;
            if (compute)
                [compute useResource:resource usage:usage];
            else
                [render useResource:resource usage:usage stages:stages];
        }
        for (const auto& binding : prepared_.arrays) {
            const auto buffer = reinterpret_cast<id<MTLBuffer>>(binding.buffer.get());
            if (compute)
                [compute setBuffer:buffer offset:0u atIndex:binding.slot];
            else if (vertex)
                [render setVertexBuffer:buffer offset:0u atIndex:binding.slot];
            else
                [render setFragmentBuffer:buffer offset:0u atIndex:binding.slot];
        }
        for (const auto& physical : prepared_.direct) {
            const auto& binding = prepared_.descriptors->bindings[physical.element];
            switch (binding.argumentKind) {
            case MetalRhiArgumentKind::Buffer:
                if (compute)
                    [compute setBuffer:nativeBuffer(binding.buffer)
                                offset:static_cast<NSUInteger>(binding.bufferOffsetBytes)
                               atIndex:physical.slot];
                else if (vertex)
                    [render setVertexBuffer:nativeBuffer(binding.buffer)
                                     offset:static_cast<NSUInteger>(binding.bufferOffsetBytes)
                                    atIndex:physical.slot];
                else
                    [render setFragmentBuffer:nativeBuffer(binding.buffer)
                                       offset:static_cast<NSUInteger>(binding.bufferOffsetBytes)
                                      atIndex:physical.slot];
                break;
            case MetalRhiArgumentKind::Texture:
                if (compute)
                    [compute setTexture:nativeTexture(binding.textureView) atIndex:physical.slot];
                else if (vertex)
                    [render setVertexTexture:nativeTexture(binding.textureView)
                                     atIndex:physical.slot];
                else
                    [render setFragmentTexture:nativeTexture(binding.textureView)
                                       atIndex:physical.slot];
                break;
            case MetalRhiArgumentKind::Sampler:
                if (compute)
                    [compute setSamplerState:nativeSampler(binding.sampler) atIndex:physical.slot];
                else if (vertex)
                    [render setVertexSamplerState:nativeSampler(binding.sampler)
                                          atIndex:physical.slot];
                else
                    [render setFragmentSamplerState:nativeSampler(binding.sampler)
                                            atIndex:physical.slot];
                break;
            }
        }
    }

private:
    const MetalRhiNativePreparedFunctionBindings& prepared_;
    MetalRhiPreparedBindingLayoutValidator layout_;
};

} // namespace

std::shared_ptr<const MetalRhiNativePreparedComputeBindings> prepareMetalRhiNativeComputeBindings(
    std::shared_ptr<MetalRhiNativePipelinePayload> pipeline,
    std::shared_ptr<MetalRhiNativeDescriptorGroupPayload> descriptors,
    std::uint32_t set,
    const rhi::BindlessComputeBindingBudget& budget,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!pipeline || !pipeline->computePipelinePayloadPresent) {
        diagnostic = "RHI-METAL-PREPARED-COMPUTE-PAYLOAD-MISSING";
        return {};
    }
    if (!budget.maxArgumentBuffers || !budget.maxArgumentBufferBytes) {
        diagnostic = "RHI-BINDLESS-PREPARED-COMPUTE-BUDGET";
        return {};
    }
    MetalRhiNativeArgumentPreparationBudget reservation(budget, true);
    if (!descriptors) {
        diagnostic = "RHI-METAL-PREPARED-COMPUTE-PAYLOAD-MISSING";
        return {};
    }
    auto snapshot = core::makeShared<MetalRhiNativeDescriptorGroupPayload>(*descriptors);
    if (const auto* id = MetalRhiPreparedDescriptorIdentityValidator::capture(*snapshot)) {
        diagnostic = id;
        return {};
    }
    MetalRhiNativeFunctionBindingPreparation preparation(pipeline,
        std::move(snapshot),
        set,
        rhi::ShaderStage::Compute,
        pipeline->computeFunction,
        metalRhiPreparedComputeLayoutValidator(*pipeline));
    if (!preparation.preflight(reservation, diagnostic))
        return {};
    return preparation.build<MetalRhiNativePreparedComputeBindings>(reservation, diagnostic);
}

std::shared_ptr<const MetalRhiNativePreparedComputeBindings>
prepareMetalRhiNativeScalarComputeBindings(std::shared_ptr<MetalRhiNativePipelinePayload> pipeline,
    std::shared_ptr<MetalRhiNativeDescriptorGroupPayload> descriptors,
    std::uint32_t set,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!pipeline || !pipeline->computePipelinePayloadPresent || !descriptors) {
        diagnostic = "RHI-METAL-PREPARED-COMPUTE-PAYLOAD-MISSING";
        return {};
    }
    for (const auto& binding : descriptors->bindings) {
        if (binding.arrayCount != 1u || binding.arrayElement
            || binding.stages != rhi::ShaderStage::Compute
            || (binding.type != rhi::DescriptorType::UniformBuffer
                && binding.type != rhi::DescriptorType::ReadOnlyStorageBuffer
                && binding.type != rhi::DescriptorType::StorageBuffer)) {
            diagnostic = "RHI-METAL-BINDLESS-ROOT-ORDINARY-SCALAR-PROFILE";
            return {};
        }
    }
    auto snapshot = core::makeShared<MetalRhiNativeDescriptorGroupPayload>(*descriptors);
    if (const auto* id = MetalRhiPreparedDescriptorIdentityValidator::capture(*snapshot)) {
        diagnostic = id;
        return {};
    }
    MetalRhiNativeArgumentPreparationBudget reservation({}, true);
    MetalRhiNativeFunctionBindingPreparation preparation(pipeline,
        std::move(snapshot),
        set,
        rhi::ShaderStage::Compute,
        pipeline->computeFunction,
        metalRhiPreparedComputeLayoutValidator(*pipeline));
    if (!preparation.preflight(reservation, diagnostic))
        return {};
    return preparation.build<MetalRhiNativePreparedComputeBindings>(reservation, diagnostic);
}

bool validateMetalRhiNativePreparedComputeBindings(
    const std::shared_ptr<const MetalRhiNativePreparedComputeBindings>& prepared,
    core::pmr::String& diagnostic)
{
    if (!prepared || !prepared->sharedOwnerMatches(prepared) || !prepared->pipeline
        || !prepared->descriptors || prepared->stage != rhi::ShaderStage::Compute
        || !core::SharedOwnerProjection::sameOwner(
            prepared->function, prepared->pipeline->computeFunction)) {
        diagnostic = "RHI-METAL-PREPARED-COMPUTE-OWNER-MISMATCH";
        return false;
    }
    return MetalRhiNativeFunctionBindingRecording(
        *prepared, metalRhiPreparedComputeLayoutValidator(*prepared->pipeline))
        .validate(diagnostic);
}

std::shared_ptr<MetalRhiNativeCommandPayload> MetalOriginalTextureUploadCommand::prepareOriginal(
    std::shared_ptr<const Reservation> reservation,
    std::shared_ptr<const Program> program,
    std::shared_ptr<const Staging> staging,
    rhi::GpuDevice& device,
    core::pmr::String& diagnostic)
{
    using namespace rhi;
    const auto reject = [&](const char* id) -> std::shared_ptr<MetalRhiNativeCommandPayload> {
        diagnostic = id;
        return {};
    };
    if (const auto* id = Reservation::validateOriginal(reservation))
        return reject(id);
    if (const auto* id = Program::validateOriginal(program))
        return reject(id);
    if (const auto* id = Staging::validateOriginal(staging))
        return reject(id);
    const auto& seal = reservation->originalSeal();
    const auto& root = seal->originalRoot();
    const auto& graph = root->originalGraph();
    const auto& inputs = program->originalInputs();
    const auto& usage = *inputs->originalScalars()->originalUsage();
    const auto& inventory = usage.originalSources();
    const auto& packing = staging->originalPacking();
    const auto lowering =
        semantics::OriginalCommandNativeLifecycleLowering::lowering(device.backend(), 7u);
    if (lowering.diagnostic || !lowering.sealedIssuerAvailable || !lowering.executionAvailable)
        return reject(lowering.diagnostic);
    if (seal->originalBudget().contractVersion != 7u
        || reservation->originalRequest().contractVersion != 3u
        || program->originalBudget().contractVersion != 1u
        || !program->originalBudget().bindlessRoots.empty() || seal->nativeColdCommandClaimed_
        || !core::SharedOwnerProjection::sameOwner(inputs, graph->originalInputs()))
        return reject("RHI-METAL-UPLOAD-FENCE-COLD-CORRESPONDENCE");
    if (const auto* id = root->compareOriginalGraph(graph, device))
        return reject(id);
    const auto stagingIndex = inventory->originalLeaseIndex(staging->originalStagingLease());
    const auto textureIndex = inventory->originalLeaseIndex(packing->originalDestinationLease());
    const auto uploadIndex = inventory->originalLeaseIndex(packing->originalSource());
    if (stagingIndex == SIZE_MAX || textureIndex == SIZE_MAX || uploadIndex == SIZE_MAX
        || inventory->sources().size() != 3u)
        return reject("RHI-METAL-UPLOAD-FENCE-INVENTORY");
    // Exact whole-upload profile. No uninspected operation is silently ignored.
    constexpr std::array kinds{CommandUsageKind::BufferMemoryBarrier,
        CommandUsageKind::TextureBarrier,
        CommandUsageKind::BeginTransferPass,
        CommandUsageKind::TextureUpload,
        CommandUsageKind::EndTransferPass,
        CommandUsageKind::Finish};
    if (program->operations().size() != kinds.size())
        return reject("RHI-METAL-UPLOAD-FENCE-OPERATIONS");
    for (std::size_t i = 0u; i < kinds.size(); ++i) {
        const auto& op = program->operations()[i];
        if (op.kind != kinds[i] || op.commandIndex != i)
            return reject("RHI-METAL-UPLOAD-FENCE-OPERATIONS");
        const auto expected = i < 2u ? 1u : i == 3u ? 2u : 0u;
        if (op.referenceCount != expected)
            return reject("RHI-METAL-UPLOAD-FENCE-REFERENCE");
        if (i < 2u || i == 3u) {
            if (usage.references()[op.firstReference].sourceIndex
                    != (i == 0u ? stagingIndex : textureIndex)
                || (i == 3u
                    && usage.references()[op.firstReference + 1u].sourceIndex != uploadIndex))
                return reject("RHI-METAL-UPLOAD-FENCE-REFERENCE");
        }
    }
    ResourceMemoryBarrierDesc dependency;
    if (!std::get<ResourceDependencyArguments>(*inputs->descriptorAt(0u))
            .write(CommandUsageKind::BufferMemoryBarrier, dependency)
        || dependency.resource != staging->originalStagingLease()->resource()
        || dependency.sourceStages != PipelineStage::Transfer
        || dependency.destinationStages != PipelineStage::Transfer
        || dependency.sourceAccess != ResourceAccess::TransferRead
        || dependency.destinationAccess != ResourceAccess::TransferRead)
        return reject("RHI-METAL-UPLOAD-FENCE-STAGING-DEPENDENCY");
    if (const auto* id = OriginalTextureUploadStagingContract::validateExecutionBudget(
            inventory->originalBudget(),
            packing->packing()->mipLayouts().size(),
            staging->nativeStagingBytes()))
        return reject(id);
    const auto& quota = seal->originalSubmissionQuota();
    const auto owners = std::uint64_t(reservation->retainedOwnerCount())
        + program->retainedOwnerCount() + staging->retainedOwnerCount() + 3u;
    if (owners > seal->originalBudget().maxSubmissionOwners)
        return reject("RHI-METAL-UPLOAD-FENCE-OWNER-CAPACITY");
    for (const auto& q : {quota, program->originalMetadataQuota()})
        if (packing->overlapsCpuQuota(q.get())
            || core::SharedOwnerProjection::sameOwner(q, staging->originalMetadataQuota())
            || core::SharedOwnerProjection::sameOwner(
                q, packing->originalDestinationLease()->originalObject()->originalContentCpuQuota())
            || core::SharedOwnerProjection::sameOwner(
                q, staging->originalStagingLease()->originalMetadataQuota())
            || core::inspectCpuAllocationCharge(staging->originalStagingLease()->originalObject())
                    .quota
                == q.get())
            return reject("RHI-METAL-UPLOAD-FENCE-QUOTA-DOMAIN");
    for (const auto& q : {inputs->originalMetadataQuota(),
             graph->originalMetadataQuota(),
             root->originalMetadataQuota()})
        if (core::SharedOwnerProjection::sameOwner(q, staging->originalMetadataQuota())
            || core::inspectCpuAllocationCharge(staging->originalStagingLease()->originalObject())
                    .quota
                == q.get())
            return reject("RHI-METAL-UPLOAD-FENCE-QUOTA-DOMAIN");
    if (!core::SharedOwnerProjection::sameOwner(
            metalRhiIssuingResourceRegistry(device), staging->registry_))
        return reject("RHI-METAL-UPLOAD-FENCE-DEVICE");
    // Metal encodes one copy per physical array layer. Charge those actual
    // calls and the begin/end boundary in the same canonical admission budget.
    const auto layers =
        packing->originalDestinationLease()->originalObject()->desc().arrayLayerCount;
    const auto mips = packing->packing()->mipLayouts().size();
    if (mips > (UINT64_MAX - 4u) / layers
        || inventory->originalBudget().maxTypedNativeCommandCalls < mips * layers + 4u)
        return reject("RHI-METAL-UPLOAD-FENCE-NATIVE-CALL-CAPACITY");
    try {
        {
            core::CpuMemoryQuotaScope scope(nullptr);
            sharedMetalRhiNativeCommandQueue();
            sharedMetalRhiCommandCapacityBudget();
        }
        core::CpuMemoryQuotaScope scope(quota);
        auto command = createMetalRhiNativeCommandPayload(
            "OriginalTextureUpload", staging->registry_, &diagnostic);
        if (!command || !commandDeviceMatches(command->nativeDevice_, staging->device_))
            return reject("RHI-METAL-UPLOAD-FENCE-NATIVE-DEVICE");
        auto custody = shareOriginal(core::makeUnique<MetalOriginalTextureUploadCommand>(
            ConstructionKey{}, reservation, std::move(program), std::move(staging)));
        custody->encoderSlot_ =
            core::makeShared<core::ExternalObjectSlot>([](void* object) noexcept {
                [reinterpret_cast<id>(object) release];
            });
        custody->command_ = command.get();
        seal->nativeColdCommandClaimed_ = true;
        command->originalCommandCustody_ = custody;
        if (!custody->record(*command, diagnostic))
            return {};
        command->originalRecordingClosed_ = true;
        diagnostic.clear();
        return command;
    } catch (const std::bad_alloc&) {
        return reject("RHI-METAL-UPLOAD-FENCE-ALLOCATION");
    } catch (const std::length_error&) {
        return reject("RHI-METAL-UPLOAD-FENCE-ALLOCATION");
    }
}

bool MetalOriginalTextureUploadCommand::record(
    MetalRhiNativeCommandPayload& command, core::pmr::String& diagnostic)
{
    using namespace rhi;
    const auto fail = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    const auto& seal = reservation_->originalSeal();
    if (const auto* id = seal->applyOriginalRecordingPlan())
        return fail(id);
    if (const auto* id = advance(Life::Event::BeginRecording))
        return fail(id);
    if (const auto* id = validatePending())
        return fail(id);
    if (!command.recordBufferBarrier(diagnostic) || !command.recordTextureBarrier(diagnostic))
        return false;
    auto nativeCommand = reinterpret_cast<id<MTLCommandBuffer>>(command.nativeCommandBuffer_.get());
    auto buffer = reinterpret_cast<id<MTLBuffer>>(staging_->bufferObject_.get());
    auto image = reinterpret_cast<id<MTLTexture>>(staging_->imageObject_.get());
    const auto& packing = staging_->packing_->packing();
    const auto map = [&](std::size_t mip, semantics::TextureUploadPackedCopy& copy) -> const char* {
        if (const auto* id = semantics::TextureUploadPackedCopyMapping::mapOriginal(
                1u, true, packing, staging_->staging_->resource(), mip, copy))
            return id;
        const auto bytesPerImage = std::uint64_t(copy.bytesPerRow) * copy.rowsPerImage;
        if (!buffer || !image || !bytesPerImage || !copy.subresources.arrayLayerCount
            || copy.subresources.mipLevelCount != 1u || copy.extent.depth != 1u
            || copy.bufferOffsetBytes > buffer.length
            || copy.packedFootprintBytes > buffer.length - copy.bufferOffsetBytes
            || bytesPerImage > copy.packedFootprintBytes / copy.subresources.arrayLayerCount
            || copy.subresources.baseArrayLayer > image.arrayLength
            || copy.subresources.arrayLayerCount
                > image.arrayLength - copy.subresources.baseArrayLayer
            || copy.subresources.baseMipLevel >= image.mipmapLevelCount
            || !fitsNSUInteger(bytesPerImage) || !fitsNSUInteger(copy.bufferOffsetBytes))
            return "RHI-METAL-UPLOAD-FENCE-COPY-MAPPING";
        return nullptr;
    };
    // Validate every mapping before any native copy. Packed source owns all
    // original dimensions, pitches and offsets; no default LOD/layout exists.
    for (std::size_t i = 0u; i < packing->mipLayouts().size(); ++i) {
        semantics::TextureUploadPackedCopy copy;
        if (const auto* id = map(i, copy))
            return fail(id);
    }
    @autoreleasepool {
        auto encoder = [nativeCommand blitCommandEncoder];
        if (!encoder)
            return fail("RHI-METAL-UPLOAD-FENCE-ENCODER");
        [encoder retain];
        if (!encoderSlot_->adopt(reinterpret_cast<void*>(encoder))) {
            [encoder endEncoding];
            [encoder release];
            return fail("RHI-METAL-UPLOAD-FENCE-ENCODER-SLOT");
        }
        command.nativeBlitEncoder_ = std::shared_ptr<void>(encoderSlot_, encoderSlot_->get());
        command.stats_->nativeBlitEncoderCreated = true;
        for (std::size_t i = 0u; i < packing->mipLayouts().size(); ++i) {
            semantics::TextureUploadPackedCopy copy;
            if (const auto* id = map(i, copy))
                return fail(id);
            const auto bytesPerImage = std::uint64_t(copy.bytesPerRow) * copy.rowsPerImage;
            for (std::uint32_t layer = 0u; layer < copy.subresources.arrayLayerCount; ++layer)
                [encoder copyFromBuffer:buffer
                           sourceOffset:static_cast<NSUInteger>(
                                            copy.bufferOffsetBytes + bytesPerImage * layer)
                      sourceBytesPerRow:copy.bytesPerRow
                    sourceBytesPerImage:static_cast<NSUInteger>(bytesPerImage)
                             sourceSize:MTLSizeMake(copy.extent.width,
                                            copy.extent.height,
                                            copy.extent.depth)
                              toTexture:image
                       destinationSlice:copy.subresources.baseArrayLayer + layer
                       destinationLevel:copy.subresources.baseMipLevel
                      destinationOrigin:MTLOriginMake(copy.origin.width,
                                            copy.origin.height,
                                            copy.origin.depth)];
        }
        command.stats_->nativeBlitCopyEncoded = true;
        if (!command.recordBlitPassEnd(diagnostic))
            return false;
        encoderSlot_->reset();
    }
    if (!command.finish(diagnostic))
        return false;
    if (const auto* id = advance(Life::Event::RecordingValidatedAndDrained))
        return fail(id);
    return true;
}

std::shared_ptr<MetalRhiNativeCommandPayload> MetalOriginalBindlessCommand::prepareOriginal(
    std::shared_ptr<const Closure> closure,
    std::shared_ptr<const Reservation> reservation,
    std::shared_ptr<const Binding> binding,
    rhi::GpuDevice& device,
    core::pmr::String& diagnostic,
    bool deferRecording,
    std::shared_ptr<const MetalOriginalBindlessGraphicsPass> graphics,
    std::shared_ptr<const Closure> nextClosure,
    std::shared_ptr<core::AtomicCapacityBudget> attachmentCapacity,
    std::shared_ptr<const Closure> finalClosure,
    const rhi::OriginalIndexedDrawFetchContract::Budget& geometryFetch,
    const rhi::OriginalBindlessRootReadbackSequence::Input& readbackInput,
    const rhi::OriginalBindlessRootNativeSequence<Closure>::Input& rootInput,
    const PassInput& passInput,
    const rhi::OriginalBindlessProducedGeometryInputs& producedGeometry)
{
    using namespace rhi;
    const auto reject = [&](const char* id) -> std::shared_ptr<MetalRhiNativeCommandPayload> {
        diagnostic = id;
        return {};
    };
    if (const auto* id = Closure::validateOriginal(closure))
        return reject(id);
    if (const auto* id = Reservation::validateOriginal(reservation))
        return reject(id);
    if (const auto* id = OriginalBindlessRootReadbackSequence::inspectInput(binding, readbackInput))
        return reject(id);
    const bool produced =
        reservation->originalSeal()->originalRoot()->hasOriginalBindlessProducedGeometryProfile();
    if (produced) {
        constexpr auto gridLimit =
            UINT32_MAX / visibility::GpuVisibilityDispatchContract::localSizeX;
        auto* extension =
            static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
        if (!extension)
            return reject("RHI-METAL-PRODUCED-GEOMETRY-DEVICE-ISSUER");
        if (const auto* id = producedGeometry.inspectNative(rootInput.closures,
                rootInput.count,
                reservation->originalSeal()->originalRoot()->originalGraph(),
                binding->originalProgram(),
                binding,
                readbackInput,
                gridLimit,
                extension->capabilities().indirectFirstInstance))
            return reject(id);
    } else if (!producedGeometry.empty())
        return reject("RHI-BINDLESS-PRODUCED-EXECUTION-INPUT-SCHEMA");
    const auto executionVersion =
        closure->originalRoot()->originalSnapshot()->plan().originalExecutionContractVersion;
    const bool mixed = !produced && executionVersion == 11u;
    const bool storage = !produced && (executionVersion == 9u || mixed);
    const bool graphicsDraw = bool(graphics);
    const bool colorRead = !graphicsDraw
        && closure->originalRoot()->originalSnapshot()->plan().originalExecutionContractVersion
            == 8u;
    const bool depthRead = !graphicsDraw
        && closure->originalRoot()->originalSnapshot()->plan().originalExecutionContractVersion
            == 6u;
    const bool sampledGraphics = graphicsDraw && !closure->groups_.empty()
        && !closure->groups_.front().descriptors()->originalCompletedTextureSources.empty();
    const bool ordinaryDepthGraphics = graphicsDraw && closure->budget_.contractVersion == 16u;
    const bool ordinaryColorGraphics = graphicsDraw && !ordinaryDepthGraphics
        && std::any_of(closure->groups_.begin(),
            closure->groups_.end(),
            [](const auto& group) {
                return !group.descriptors()->originalCompletedTextureSources.empty();
            })
        && !sampledGraphics;
    if (produced) {
        if (graphics || attachmentCapacity || passInput.empty() || !rootInput.closures
            || rootInput.count < 5u)
            return reject("RHI-METAL-PRODUCED-COMMAND-INPUTS");
    } else if (mixed) {
        if (graphics || !nextClosure || !finalClosure || !attachmentCapacity)
            return reject("RHI-METAL-ROOT-MIXED-INPUTS");
        for (const auto& entry : {closure, nextClosure, finalClosure}) {
            if (const auto* id = entry->storageExecutionDiagnostic())
                return reject(id);
            if (entry->budget_.contractVersion != 14u
                || !core::SharedOwnerProjection::sameOwner(
                    entry->originalInventory(), closure->originalInventory()))
                return reject("RHI-METAL-ROOT-MIXED-CORRESPONDENCE");
        }
        const auto visits = std::uint64_t(closure->root_->size()) + nextClosure->root_->size()
            + finalClosure->root_->size();
        const auto flow = OriginalBindlessMixedEntryFlowContract::inspect(
            {closure->root_, nextClosure->root_, finalClosure->root_},
            closure->inventory_,
            {1u, true, visits});
        if (flow.diagnostic)
            return reject(flow.diagnostic);
    } else if (finalClosure)
        return reject("RHI-METAL-ROOT-COMMAND-INPUT-SCHEMA");
    else if (storage) {
        if (graphics || !nextClosure || !attachmentCapacity)
            return reject("RHI-METAL-ROOT-STORAGE-INPUTS");
        if (const auto* id = closure->storageExecutionDiagnostic())
            return reject(id);
        if (const auto* id = nextClosure->storageExecutionDiagnostic())
            return reject(id);
        if (const auto* id = OriginalBindlessStorageExecutionContract::validateEntry(
                *closure->originalRoot(), true))
            return reject(id);
        if (const auto* id = OriginalBindlessStorageExecutionContract::validateEntry(
                *nextClosure->originalRoot(), false))
            return reject(id);
        if (!core::SharedOwnerProjection::sameOwner(
                closure->originalInventory(), nextClosure->originalInventory())
            || !core::SharedOwnerProjection::sameOwner(closure->originalRoot()->originalSnapshot(),
                nextClosure->originalRoot()->originalSnapshot())
            || core::SharedOwnerProjection::sameOwner(
                closure->originalRoot(), nextClosure->originalRoot()))
            return reject("RHI-METAL-ROOT-STORAGE-CORRESPONDENCE");
    } else if (nextClosure || attachmentCapacity)
        return reject("RHI-METAL-ROOT-COMMAND-INPUT-SCHEMA");
    if (const auto* id = storage || produced ? nullptr
            : depthRead                      ? closure->completedDepthExecutionDiagnostic()
            : ordinaryDepthGraphics          ? closure->ordinaryDepthGraphicsExecutionDiagnostic()
            : ordinaryColorGraphics          ? closure->ordinaryColorGraphicsExecutionDiagnostic()
            : colorRead || sampledGraphics
            ? closure->completedColorExecutionDiagnostic(sampledGraphics
                      ? OriginalBindlessCompletedImageScope::SampledGraphics1
                      : OriginalBindlessCompletedImageScope::ComputeColor8)
            : closure->bufferExecutionDiagnostic())
        return reject(id);
    if (closure->groups_.empty()
        || (graphicsDraw ? !closure->groups_.front().graphics : !closure->groups_.front().prepared))
        return reject("RHI-METAL-ROOT-COMMAND-COMPUTE-PROFILE");
    if (const auto* id = Reservation::validateOriginal(reservation))
        return reject(id);
    if (const auto* id = OriginalBindlessRootReadbackSequence::inspectInput(binding, readbackInput))
        return reject(id);
    OriginalBindlessGraphicsPassSequenceContract::Footprint passFootprint;
    if (!passInput.empty()) {
        if (reservation->originalSeal()->originalBudget().contractVersion != 21u || graphics
            || attachmentCapacity)
            return reject("RHI-METAL-PRODUCED-PASS-LIFECYCLE");
        if (const auto* id = OriginalBindlessGraphicsPassSequenceContract::inspectPrepared<
                MetalOriginalBindlessGraphicsPass,
                OriginalCommandStateTransaction,
                Closure>(passInput,
                reservation->originalSeal()->originalRoot(),
                binding->originalProgram(),
                rootInput,
                passFootprint))
            return reject(id);
    }
    using Roots = OriginalBindlessRootNativeSequence<Closure>;
    if (const auto* id = Roots::inspectHistorical(rootInput, {closure, nextClosure, finalClosure}))
        return reject(id);
    if (const auto* id = Roots::inspectInput(rootInput,
            closure->originalInventory(),
            binding->originalProgram(),
            [](const auto& selected) noexcept {
                return Closure::validateOriginal(selected);
            }))
        return reject(id);
    const auto& seal = reservation->originalSeal();
    const auto& tx = seal->originalRoot();
    const auto& program = binding->originalProgram();
    const bool geometryDraw = graphicsDraw && tx->hasOriginalBindlessGraphicsGeometryProfile();
    if (geometryDraw) {
        const auto geometry =
            semantics::OriginalBindlessGeometryExecutionLowering::current(device.backend());
        if (!geometry.executionAvailable || geometry.diagnostic)
            return reject(geometry.diagnostic);
        if (const auto* id = validateGeometryFetch(closure, program, geometryFetch))
            return reject(id);
        if (closure->budget_.contractVersion != 3u)
            return reject("RHI-METAL-ROOT-GEOMETRY-CLOSURE");
    } else if (geometryFetch.contractVersion || geometryFetch.prototypeOptIn
        || geometryFetch.maxCommands || geometryFetch.maxDraws || geometryFetch.maxStreams
        || geometryFetch.maxBindingScans || geometryFetch.maxAnalyzedIndices
        || closure->budget_.contractVersion == 3u)
        return reject("RHI-METAL-ROOT-GEOMETRY-FETCH-SCHEMA");
    const auto& inputs = program->originalInputs();
    const auto& usage = *inputs->originalScalars()->originalUsage();
    const auto lifecycle = produced ? 21u
        : mixed                     ? 17u
        : storage                   ? 15u
        : ordinaryDepthGraphics     ? 19u
        : graphicsDraw              ? 9u
        : depthRead                 ? 14u
        : colorRead                 ? 8u
                                    : 6u;
    const auto lowering =
        semantics::OriginalCommandNativeLifecycleLowering::lowering(device.backend(), lifecycle);
    if (device.backend() != Backend::Metal || !lowering.executionAvailable || lowering.diagnostic
        || seal->originalBudget().contractVersion != lifecycle
        || reservation->originalRequest().contractVersion
            != (produced                    ? 11u
                    : mixed                 ? 9u
                    : storage               ? 8u
                    : ordinaryDepthGraphics ? 10u
                    : graphicsDraw          ? 5u
                    : depthRead             ? 7u
                    : colorRead             ? 4u
                                            : 2u)
        || (storage
            && (tx->originalContentPlanContractVersion() != (mixed ? 15u : 13u)
                || !tx->hasOriginalBindlessRootShaderResourceProfile()))
        || (colorRead && tx->originalContentPlanContractVersion() != 8u)
        || (depthRead
            && (tx->originalContentPlanContractVersion() != 12u
                || !tx->hasOriginalBindlessRootShaderResourceProfile()))
        || program->originalBudget().bindlessRoots.contractVersion
            != (produced           ? 6u
                    : geometryDraw ? 5u
                                   : 4u)
        || !core::SharedOwnerProjection::sameOwner(tx->originalGraph(), binding->originalGraph())
        || !core::SharedOwnerProjection::sameOwner(
            usage.originalSources(), closure->originalInventory())
        || seal->nativeColdCommandClaimed_)
        return reject("RHI-METAL-ROOT-COMMAND-CORRESPONDENCE");
    if (!usage.originalSources()->originalBudget().maxNativePayloadObjects)
        return reject("RHI-METAL-ROOT-COMMAND-NATIVE-CAPACITY");
    if (!passInput.empty()
        && (passFootprint.nativeObjects
                >= usage.originalSources()->originalBudget().maxNativePayloadObjects
            || passFootprint.owners > seal->originalBudget().maxSubmissionOwners
            || passFootprint.owners > seal->originalBudget().maxRecordingOwners))
        return reject("RHI-METAL-PRODUCED-PASS-CAPACITY");
    if (graphicsDraw) {
        if (const auto* id = MetalOriginalBindlessGraphicsPass::validateOriginal(graphics))
            return reject(id);
        if (!core::SharedOwnerProjection::sameOwner(graphics->closure_, closure)
            || !core::SharedOwnerProjection::sameOwner(graphics->transaction_, tx)
            || !core::SharedOwnerProjection::sameOwner(graphics->stream_, program)
            || tx->originalContentPlanContractVersion() != (ordinaryDepthGraphics ? 18u : 9u)
            || usage.originalSources()->originalBudget().maxNativePayloadObjects
                < graphics->nativeObjectCount() + 1u)
            return reject("RHI-METAL-ROOT-GRAPHICS-COLD-CORRESPONDENCE");
        if (ordinaryColorGraphics || ordinaryDepthGraphics)
            for (std::size_t group = 1u; group < closure->groups_.size(); ++group)
                for (const auto& source :
                    closure->groups_[group].descriptors()->originalCompletedTextureSources) {
                    const auto& texture = *source->originalParentLease()->originalObject();
                    if (ordinaryDepthGraphics
                        && source->originalObject()->desc().aspect == TextureAspect::Depth)
                        continue; // Closure16 authenticates Source2; Pending3 consumes its proof.
                    const auto readVersion =
                        OriginalCompletedTextureReadPlanContract::graphicsReadVersion(
                            tx->originalBindlessCompletedReadContractVersion());
                    if (const auto* id =
                            OriginalCompletedTextureReadPlanContract::inspectViewBinding(
                                texture.desc(),
                                source->originalObject()->desc(),
                                ResourceState::ShaderRead,
                                ordinaryDepthGraphics
                                    ? PipelineStage::FragmentShader
                                    : PipelineStage::VertexShader | PipelineStage::FragmentShader,
                                ResourceAccess::ShaderRead,
                                readVersion))
                        return reject(id);
                }
    }
    if (binding->rootSourceIndex() >= usage.originalSources()->sources().size())
        return reject("RHI-METAL-ROOT-COMMAND-ROOT-INDEX");
    const auto* root = std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
        &usage.originalSources()->sources()[binding->rootSourceIndex()]);
    if (!root || !core::SharedOwnerProjection::sameOwner(*root, closure->originalRoot()))
        return reject("RHI-METAL-ROOT-COMMAND-ROOT");
    auto registry = metalRhiIssuingResourceRegistry(device);
    if (!registry)
        return reject("RHI-METAL-ROOT-COMMAND-DEVICE");
    for (const auto& entry : closure->entries_) {
        const auto resource = registry->find(entry.resource);
        if (!resource
            || (entry.kind == DescriptorResourceKind::Buffer ? !entry.buffer
                        || !core::SharedOwnerProjection::sameOwner(resource->buffer, entry.buffer)
                        || !entry.buffer->resource.hazardsExplicitlyTracked
                    : entry.kind == DescriptorResourceKind::TextureView
                    ? !entry.texture
                        || !core::SharedOwnerProjection::sameOwner(
                            resource->textureView, entry.view)
                        || !entry.texture->resource.hazardsExplicitlyTracked
                    : !core::SharedOwnerProjection::sameOwner(resource->sampler, entry.sampler)))
            return reject("RHI-METAL-ROOT-COMMAND-NATIVE-RESOURCE");
    }
    if (const auto* id = tx->compareOriginalGraph(binding->originalGraph(), device))
        return reject(id);
    std::size_t clears = 0u, binds = 0u, dispatches = 0u, copies = 0u;
    std::size_t begins = 0u, ends = 0u, draws = 0u, viewports = 0u, scissors = 0u, snapshots = 0u,
                textures = 0u, vertexBinds = 0u, indexBinds = 0u;
    OriginalCommandPushConsumption pushConsumer;
    const auto& pushBudget = program->originalBudget().bindlessRoots.pushConsumption;
    const auto* selectedPipeline = graphicsDraw
        ? std::get<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
              closure->root_->originalPrepared())
              ->owners()
              ->desc()
              .pipeline.get()
        : nullptr;
    if (selectedPipeline && selectedPipeline->shaderReflection().pushConstantByteCount) {
        const auto* extension =
            static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
        if (!pushBudget.recordingOptIn || pushBudget.recordingContractVersion != 1u || !extension
            || extension->capabilities().originalGraphicsPushContractVersion != 1u)
            return reject("RHI-METAL-ROOT-PUSH-RECORDING-PROFILE");
        if (const auto* id = pushBudget.diagnostic())
            return reject(id);
        const auto native = semantics::nativeBackendCapabilityContract(Backend::Metal);
        if (const auto* id = semantics::OriginalBindlessPushConsumptionLowering::validateNativeSize(
                selectedPipeline->shaderReflection().pushConstantByteCount,
                kL2AtomicPushConstantLimitBytes,
                native.maxPushConstantSizeBytes))
            return reject(id);
    }
    if (produced) {
        const auto* extension =
            static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
        const auto native = semantics::nativeBackendCapabilityContract(Backend::Metal);
        if (const auto* id =
                OriginalBindlessProducedProgramContract::inspectNativePushRecording<Closure>(
                    program,
                    rootInput,
                    extension
                        && extension->capabilities().originalGraphicsPushContractVersion == 1u,
                    native.maxPushConstantSizeBytes))
            return reject(id);
        const auto report = OriginalBindlessProducedProgramContract::inspectNative<Closure>(
            program, rootInput, binding, readbackInput, producedGeometry.commands);
        if (report.diagnostic)
            return reject(report.diagnostic);
        clears = 1u;
        binds = report.bindings;
        dispatches = report.dispatches;
        begins = ends = report.renderPasses;
        draws = report.draws;
        copies = report.copies;
    } else
        for (const auto& op : program->operations()) {
            using K = CommandUsageKind;
            if (!graphicsDraw) {
                if (const auto* id = OriginalBindlessRootProgramContract::validateOperation(op.kind,
                        mixed                        ? 5u
                            : storage                ? 4u
                            : colorRead || depthRead ? 3u
                                                     : 1u))
                    return reject(id);
                if (op.kind == K::TextureBarrier) {
                    if (op.referenceCount != 1u)
                        return reject("RHI-METAL-ROOT-COLOR-BARRIER");
                    const auto index = usage.references()[op.firstReference].sourceIndex;
                    const auto* parent =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureLease>>(
                            &usage.originalSources()->sources()[index]);
                    bool found = false;
                    if (parent)
                        for (const auto& entry : closure->entries_)
                            found = found
                                || (entry.texture
                                    && core::SharedOwnerProjection::sameOwner(entry.texture,
                                        metalRhiNativePayloadFor(*(*parent)->originalObject())));
                    if (!found)
                        return reject("RHI-METAL-ROOT-COLOR-BARRIER-CLOSURE");
                }
            } else {
                switch (op.kind) {
                case K::ClearSnapshotFault:
                case K::BufferBarrier:
                case K::BufferCopy:
                case K::BeginTransferPass:
                case K::EndTransferPass:
                case K::Finish:
                    break;
                case K::RetainIndexedSnapshot:
                    ++snapshots;
                    break;
                case K::BeginRenderPass:
                    if (op.commandIndex != graphics->commandIndex())
                        return reject("RHI-METAL-ROOT-GRAPHICS-PASS");
                    ++begins;
                    break;
                case K::EndRenderPass:
                    ++ends;
                    break;
                case K::BindPreparedGraphicsBinding:
                    pushConsumer.onBind();
                    ++binds;
                    break;
                case K::SetViewport:
                    ++viewports;
                    break;
                case K::SetScissor:
                    ++scissors;
                    break;
                case K::BindVertexBuffer:
                    if (!geometryDraw)
                        return reject("RHI-METAL-ROOT-GEOMETRY-PROFILE");
                    ++vertexBinds;
                    break;
                case K::BindIndexBuffer:
                    if (!geometryDraw)
                        return reject("RHI-METAL-ROOT-GEOMETRY-PROFILE");
                    ++indexBinds;
                    break;
                case K::Draw:
                case K::DrawIndexed:
                    if ((op.kind == K::DrawIndexed) != geometryDraw)
                        return reject("RHI-METAL-ROOT-GEOMETRY-DRAW-PROFILE");
                    if (const auto* id =
                            pushConsumer.consume(selectedPipeline->shaderReflection(), pushBudget))
                        return reject(id);
                    ++draws;
                    break;
                case K::PushConstants: {
                    if (!selectedPipeline || !selectedPipeline->layout() || !binds
                        || !pushBudget.recordingOptIn || pushBudget.recordingContractVersion != 1u)
                        return reject("RHI-METAL-ROOT-PUSH-RECORDING-PROFILE");
                    PushConstantsDesc block;
                    if (const auto* id =
                            pushConsumer.update(inputs->originalScalars()->originalArguments(),
                                op.commandIndex,
                                selectedPipeline->layout()->desc(),
                                selectedPipeline->shaderReflection(),
                                ShaderStage::Vertex | ShaderStage::Fragment,
                                pushBudget,
                                block))
                        return reject(id);
                    break;
                }
                case K::TextureBarrier: {
                    if (op.referenceCount != 1u)
                        return reject("RHI-METAL-ROOT-GRAPHICS-BARRIER");
                    const auto index = usage.references()[op.firstReference].sourceIndex;
                    const auto* source =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureLease>>(
                            &usage.originalSources()->sources()[index]);
                    bool found = false;
                    if (source)
                        for (std::uint32_t i = 0u; i < graphics->attachmentCount_; ++i)
                            found = found
                                || core::SharedOwnerProjection::sameOwner(*source,
                                    graphics->attachments_[i].source->originalParentLease());
                    if (found)
                        ++textures;
                    else if ((sampledGraphics || ordinaryColorGraphics || ordinaryDepthGraphics)
                        && source)
                        for (const auto& entry : closure->entries_)
                            found = found
                                || (entry.texture
                                    && core::SharedOwnerProjection::sameOwner(entry.texture,
                                        metalRhiNativePayloadFor(*(*source)->originalObject())));
                    if (!found)
                        return reject("RHI-METAL-ROOT-GRAPHICS-BARRIER-CLOSURE");
                    break;
                }
                default:
                    return reject("RHI-METAL-ROOT-GRAPHICS-OPERATION");
                }
            }
            if (storage && op.kind == K::BeginRenderPass)
                ++begins;
            if (storage && op.kind == K::EndRenderPass)
                ++ends;
            if (op.kind == K::ClearSnapshotFault)
                ++clears;
            if (op.kind == K::BindPreparedComputeBinding) {
                if (storage
                    && (op.referenceCount != 1u || binds >= (mixed ? 3u : 2u) || binds != dispatches
                        || usage.references()[op.firstReference].sourceIndex
                            != closure->inventory_->originalLeaseIndex(binds == 2u
                                    ? finalClosure->root_
                                    : binds ? nextClosure->root_
                                            : closure->root_)))
                    return reject("RHI-METAL-ROOT-STORAGE-ENTRY-ORDER");
                ++binds;
            }
            if (op.kind == K::Dispatch) {
                if (storage && binds != dispatches + 1u)
                    return reject("RHI-METAL-ROOT-STORAGE-ENTRY-ORDER");
                ++dispatches;
            }
            if (op.kind == K::BufferCopy) {
                ++copies;
                if (!OriginalBindlessRootReadbackSequence::containsCopy(
                        binding, readbackInput, op.commandIndex))
                    return reject("RHI-METAL-ROOT-COMMAND-COPY");
            }
            if (op.kind == K::ClearSnapshotFault || op.kind == K::BindPreparedComputeBinding
                || op.kind == K::BindPreparedGraphicsBinding || op.kind == K::RetainIndexedSnapshot
                || op.kind == K::PushConstants) {
                if (op.referenceCount != 1u
                    || (usage.references()[op.firstReference].sourceIndex
                            != binding->rootSourceIndex()
                        && !(storage && op.kind == K::BindPreparedComputeBinding
                            && (usage.references()[op.firstReference].sourceIndex
                                    == closure->originalInventory()->originalLeaseIndex(
                                        nextClosure->originalRoot())
                                || (mixed
                                    && usage.references()[op.firstReference].sourceIndex
                                        == closure->originalInventory()->originalLeaseIndex(
                                            finalClosure->originalRoot()))))))
                    return reject("RHI-METAL-ROOT-COMMAND-FOREIGN-ROOT");
            }
            if (op.kind == K::BufferBarrier || op.kind == K::BufferCopy
                || op.kind == K::BindVertexBuffer || op.kind == K::BindIndexBuffer) {
                for (std::size_t i = 0u; i < op.referenceCount; ++i) {
                    const auto index = usage.references()[op.firstReference + i].sourceIndex;
                    if ((op.kind == K::BindVertexBuffer || op.kind == K::BindIndexBuffer)
                        && !closure->initializedGeometry(index))
                        return reject("RHI-METAL-ROOT-GEOMETRY-INITIALIZATION");
                    bool found = false;
                    for (const auto& entry : closure->entries_)
                        found = found || entry.sourceIndex == index;
                    if (!found && op.kind == K::BufferCopy && i == 1u)
                        if (const auto* lease =
                                std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                                    &usage.originalSources()->sources()[index]))
                            found = OriginalBindlessRootReadbackSequence::containsCopyDestination(
                                binding, readbackInput, op.commandIndex, *lease);
                    if (!found)
                        return reject("RHI-METAL-ROOT-COMMAND-BUFFER-CLOSURE");
                }
            }
        }
    if (!produced
        && (clears != 1u
            || binds
                != (mixed         ? 3u
                        : storage ? 2u
                                  : 1u)
            || dispatches
                != (mixed              ? 3u
                        : storage      ? 2u
                        : graphicsDraw ? 0u
                                       : 1u)
            || copies != OriginalBindlessRootReadbackSequence::resultCount(readbackInput)
            || (geometryDraw && (!vertexBinds || indexBinds != 1u))
            || (storage
                && (!begins || begins != ends || begins > closure->budget_.maxAttachmentViews
                    || attachmentCapacity->capacity() != closure->budget_.maxAttachmentViews
                    || usage.originalSources()->originalBudget().maxNativePayloadObjects
                        < begins + 1u))
            || (graphicsDraw
                && (begins != 1u || ends != 1u || draws != 1u || viewports != 1u
                    || scissors != 1u
                    // Closure16 retains the exact snapshot through its original Root.
                    // An optional retain command is authenticated above; older
                    // Graphics streams keep their explicit single-retain contract.
                    || (ordinaryDepthGraphics ? snapshots > 1u : snapshots != 1u)
                    || textures != graphics->attachmentCount_))))
        return reject("RHI-METAL-ROOT-COMMAND-COUNTS");
    const auto owners = passFootprint.owners
        + submissionOwnerCount(*closure,
            reservation->retainedOwnerCount(),
            *binding,
            graphicsDraw ? graphics.get() : nullptr,
            storage ? nextClosure.get() : nullptr,
            begins,
            mixed ? finalClosure.get() : nullptr);
    const auto readbackOwners =
        OriginalBindlessRootReadbackSequence::additionalOwnerBound(readbackInput);
    const auto rootOwners = Roots::additionalOwnerBound(rootInput, produced ? 1u : 3u);
    if (owners > seal->originalBudget().maxSubmissionOwners
        || readbackOwners > seal->originalBudget().maxSubmissionOwners - owners
        || rootOwners > seal->originalBudget().maxSubmissionOwners - owners - readbackOwners)
        return reject("RHI-METAL-ROOT-COMMAND-OWNER-CAPACITY");
    const auto& quota = seal->originalSubmissionQuota();
    if (core::SharedOwnerProjection::sameControlBlock(quota, closure->originalMetadataQuota())
        || core::SharedOwnerProjection::sameControlBlock(quota, binding->originalMetadataQuota())
        || core::SharedOwnerProjection::sameControlBlock(
            quota, binding->originalResult()->originalMetadataQuota())
        || (graphicsDraw
            && core::SharedOwnerProjection::sameControlBlock(
                quota, graphics->originalMetadataQuota())))
        return reject("RHI-METAL-ROOT-COMMAND-QUOTA-DOMAIN");
    auto fault = Binding::acquireOriginalFaultCustody(binding, diagnostic);
    if (!fault.valid())
        return {};
    try {
        // Shared backend infrastructure is not charged to the first command.
        {
            core::CpuMemoryQuotaScope scope(nullptr);
            sharedMetalRhiNativeCommandQueue();
            sharedMetalRhiCommandCapacityBudget();
        }
        core::CpuMemoryQuotaScope scope(quota);
        auto command = createMetalRhiNativeCommandPayload(
            "OriginalBindlessRoot", registry, &diagnostic, 1u, 1u);
        if (!command
            || !commandDeviceMatches(command->nativeDevice_, closure->pipeline_->nativeDevice))
            return reject("RHI-METAL-ROOT-COMMAND-NATIVE-DEVICE");
        auto custody =
            shareOriginal(core::makeUnique<MetalOriginalBindlessCommand>(ConstructionKey{},
                std::move(closure),
                std::move(reservation),
                std::move(binding),
                std::move(fault),
                std::move(graphics),
                std::move(nextClosure),
                std::move(finalClosure),
                readbackInput,
                rootInput,
                passInput));
        if (const auto* id = custody->readbacks_.diagnostic())
            return reject(id);
        if (const auto* id = custody->additionalFaults_.prepare(custody->readbacks_))
            return reject(id);
        if (const auto* id = custody->additionalFaults_.acquire(
                custody->readbacks_, custody->fault_, diagnostic))
            return reject(id);
        if (const auto* id = custody->roots_.validate(custody->closure_->originalInventory(),
                custody->binding_->originalProgram(),
                [](const auto& selected) noexcept {
                    return Closure::validateOriginal(selected);
                }))
            return reject(id);
        if (storage && !custody->prepareStoragePasses(device, attachmentCapacity, diagnostic))
            return {};
        for (const auto& result : custody->readbacks_.bindings()) {
            const auto payload =
                metalRhiNativePayloadFor(*result->originalReadbackSource()->originalObject());
            if (!payload
                || !core::SharedOwnerProjection::sameOwner(
                    payload->resource.nativeDevice, custody->closure_->pipeline_->nativeDevice))
                return reject("RHI-METAL-ROOT-COMMAND-READBACK-DEVICE");
            retainResourcePayload(command->retainedResourcePayloads_,
                command->retainedResourcePayloadIdentities_,
                payload);
        }
        custody->encoderSlot_ =
            core::makeShared<core::ExternalObjectSlot>([](void* object) noexcept {
                [reinterpret_cast<id>(object) release];
            });
        // The same cold custody retains every pipeline and buffer that recording
        // can select. Hot binding only finds existing payload identities.
        for (const auto& selected : custody->roots_.closures()) {
            retainResourcePayload(command->retainedResourcePayloads_,
                command->retainedResourcePayloadIdentities_,
                selected->pipeline_);
            for (const auto& entry : selected->entries_)
                retainResourcePayload(command->retainedResourcePayloads_,
                    command->retainedResourcePayloadIdentities_,
                    entry.buffer);
        }
        custody->command_ = command.get();
        seal->nativeColdCommandClaimed_ = true;
        command->originalCommandCustody_ = custody;
        if (!deferRecording) {
            if (!custody->record(*command, diagnostic))
                return {};
            command->originalRecordingClosed_ = true;
        }
        diagnostic.clear();
        return command;
    } catch (const std::bad_alloc&) {
        return reject("RHI-METAL-ROOT-COMMAND-ALLOCATION");
    } catch (const std::length_error&) {
        return reject("RHI-METAL-ROOT-COMMAND-ALLOCATION");
    }
}

bool MetalOriginalBindlessCommand::record(
    MetalRhiNativeCommandPayload& command, core::pmr::String& diagnostic)
{
    using namespace rhi;
    const auto fail = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    const auto& seal = reservation_->originalSeal();
    if (const auto* id = seal->applyOriginalRecordingPlan())
        return fail(id);
    if (const auto* id = advance(Life::Event::BeginRecording))
        return fail(id);
    if (const auto* id = validatePending())
        return fail(id);
    const auto& program = binding_->originalProgram();
    const auto& inputs = program->originalInputs();
    const Closure* graphicsClosure = nullptr;
    for (const auto& op : program->operations()) {
        using K = CommandUsageKind;
        switch (op.kind) {
        case K::BufferBarrier:
            if (!command.recordBufferBarrier(diagnostic))
                return false;
            break;
        case K::TextureBarrier:
            if ((!graphics_ && producedPasses_.empty()
                    && seal->originalBudget().contractVersion != 8u
                    && seal->originalBudget().contractVersion != 14u
                    && seal->originalBudget().contractVersion != 15u
                    && seal->originalBudget().contractVersion != 17u)
                || !command.recordTextureBarrier(diagnostic))
                return false;
            break;
        case K::RetainIndexedSnapshot:
            break; // Custody retains this exact Root.
        case K::BeginRenderPass:
            graphicsClosure = nullptr;
            if (!producedPasses_.empty()) {
                const auto pass = std::find_if(
                    producedPasses_.begin(), producedPasses_.end(), [&](const auto& p) {
                        return p->commandIndex() == op.commandIndex;
                    });
                if (pass == producedPasses_.end()
                    || !command.beginPreparedNativeRenderPass(
                        (*pass)->native_, encoderSlot_, diagnostic))
                    return false;
            } else if (nextClosure_) {
                const auto pass =
                    std::find_if(clearPasses_.begin(), clearPasses_.end(), [&](const auto& p) {
                        return p.commandIndex == op.commandIndex;
                    });
                if (pass == clearPasses_.end()
                    || !command.beginPreparedNativeRenderPass(
                        pass->native, encoderSlot_, diagnostic))
                    return false;
            } else if (!graphics_
                || !command.beginPreparedNativeRenderPass(
                    graphics_->native_, encoderSlot_, diagnostic))
                return false;
            break;
        case K::BindPreparedGraphicsBinding: {
            const auto& usage = *inputs->originalScalars()->originalUsage();
            const auto index = usage.references()[op.firstReference].sourceIndex;
            const auto* selected = roots_.select(closure_->inventory_, index);
            if (!selected)
                return fail("RHI-METAL-ROOT-COMMAND-UNRETAINED-ROOT");
            if ((!graphics_ && producedPasses_.empty())
                || !command.recordGraphicsPipelineBind(selected->pipeline_, diagnostic))
                return false;
            graphicsClosure = selected;
            auto encoder =
                reinterpret_cast<id<MTLRenderCommandEncoder>>(command.nativeRenderEncoder_.get());
            for (const auto& group : selected->groups_)
                for (std::size_t i = 0u; i < 2u; ++i) {
                    const auto& stage = group.frozenStages[i];
                    if (!stage)
                        continue;
                    const auto& layout = *group.frozenLayouts[i];
                    MetalRhiNativeFunctionBindingRecording(*stage,
                        MetalRhiPreparedBindingLayoutValidator(layout.direct,
                            layout.arrays,
                            selected->pipeline_->nativeBindingStages,
                            stage->stage,
                            layout.pushSlot))
                        .record(nil, encoder);
                }
            break;
        }
        case K::PushConstants: {
            const auto& arguments = inputs->originalScalars()->originalArguments();
            const auto* push =
                arguments->records()[op.commandIndex].scalar.get<CommandScalarArguments::Push>();
            if (!push
                || !command.recordPushConstants(push->stages,
                    push->offset,
                    push->size,
                    arguments->bytes(op.commandIndex),
                    diagnostic))
                return false;
            break;
        }
        case K::SetViewport: {
            const auto* value = inputs->originalScalars()
                                    ->originalArguments()
                                    ->records()[op.commandIndex]
                                    .scalar.get<ViewportState>();
            if (!value || !command.recordSetViewport(*value, diagnostic))
                return false;
            break;
        }
        case K::SetScissor: {
            const auto* value = inputs->originalScalars()
                                    ->originalArguments()
                                    ->records()[op.commandIndex]
                                    .scalar.get<ScissorState>();
            if (!value || !command.recordSetScissor(*value, diagnostic))
                return false;
            break;
        }
        case K::BindVertexBuffer:
        case K::BindIndexBuffer: {
            const auto& usage = *inputs->originalScalars()->originalUsage();
            if (op.referenceCount != 1u)
                return fail("RHI-METAL-ROOT-GEOMETRY-BIND-REFERENCE");
            const auto index = usage.references()[op.firstReference].sourceIndex;
            if (!graphicsClosure)
                return fail("RHI-METAL-ROOT-GEOMETRY-PIPELINE-NOT-BOUND");
            const auto& initialized = graphicsClosure->initializedGeometry(index);
            if (!initialized)
                return fail("RHI-METAL-ROOT-GEOMETRY-INITIALIZATION");
            const auto payload =
                metalRhiNativePayloadFor(*initialized->originalLease()->originalObject());
            const auto& scalar =
                inputs->originalScalars()->originalArguments()->records()[op.commandIndex].scalar;
            if (op.kind == K::BindVertexBuffer) {
                const auto* fields = scalar.get<CommandScalarArguments::VertexBinding>();
                if (!fields
                    || !command.recordVertexBufferBind(
                        fields->slot, payload, fields->offset, diagnostic))
                    return false;
            } else {
                const auto* fields = scalar.get<CommandScalarArguments::IndexBinding>();
                if (!fields
                    || !command.recordIndexBufferBind(
                        payload, fields->format, fields->offset, diagnostic))
                    return false;
            }
            break;
        }
        case K::DrawIndexed: {
            const auto* draw = inputs->originalScalars()
                                   ->originalArguments()
                                   ->records()[op.commandIndex]
                                   .scalar.get<CommandScalarArguments::DrawIndexed>();
            if (!draw
                || !command.recordDrawIndexed(draw->indices,
                    draw->instances,
                    draw->firstIndex,
                    draw->vertexOffset,
                    draw->firstInstance,
                    diagnostic))
                return false;
            break;
        }
        case K::DrawIndexedIndirect: {
            const auto& usage = *inputs->originalScalars()->originalUsage();
            const auto* draw = inputs->originalScalars()
                                   ->originalArguments()
                                   ->records()[op.commandIndex]
                                   .scalar.get<CommandScalarArguments::DrawIndirect>();
            if (!draw || op.referenceCount != 1u)
                return fail("RHI-METAL-ROOT-PRODUCED-DRAW-OPERAND");
            const auto index = usage.references()[op.firstReference].sourceIndex;
            const auto* lease = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                &closure_->inventory_->sources()[index]);
            if (!lease)
                return fail("RHI-METAL-ROOT-PRODUCED-DRAW-CUSTODY");
            const auto arguments = metalRhiNativePayloadFor(*(*lease)->originalObject());
            if (!arguments
                || !command.recordDrawIndexedIndirect(
                    arguments, draw->offset, draw->count, draw->stride, diagnostic))
                return false;
            break;
        }
        case K::Draw: {
            const auto* value = inputs->originalScalars()
                                    ->originalArguments()
                                    ->records()[op.commandIndex]
                                    .scalar.get<CommandScalarArguments::Draw>();
            if (!value
                || !command.recordDraw(value->vertices,
                    value->instances,
                    value->firstVertex,
                    value->firstInstance,
                    diagnostic))
                return false;
            break;
        }
        case K::EndRenderPass:
            if (!command.recordRenderPassEnd(diagnostic))
                return false;
            encoderSlot_->reset();
            graphicsClosure = nullptr;
            break;
        case K::ClearSnapshotFault: {
            if (op.referenceCount != 1u)
                return fail("RHI-METAL-ROOT-COMMAND-FAULT-CLEAR-ROOT");
            const auto& usage = *inputs->originalScalars()->originalUsage();
            const auto source = usage.references()[op.firstReference].sourceIndex;
            const auto* selected = roots_.select(closure_->inventory_, source);
            if (!selected)
                return fail("RHI-METAL-ROOT-COMMAND-FAULT-CLEAR-ROOT");
            if (!command.beginPreparedNativeBlitPass(encoderSlot_, diagnostic))
                return false;
            auto encoder =
                reinterpret_cast<id<MTLBlitCommandEncoder>>(command.nativeBlitEncoder_.get());
            const auto& latch = selected->originalRoot()->originalSnapshot()->originalBufferSource(
                BindlessSnapshot::OriginalBufferRole::FaultLatch);
            auto buffer = nativeBuffer(metalRhiNativePayloadFor(*latch->originalObject()));
            if (!encoder || !buffer)
                return fail("RHI-METAL-ROOT-COMMAND-FAULT-CLEAR");
            [encoder fillBuffer:buffer range:NSMakeRange(0u, sizeof(std::uint32_t)) value:0u];
            if (!command.recordBlitPassEnd(diagnostic))
                return false;
            encoderSlot_->reset();
            break;
        }
        case K::BeginComputePass:
            if (!command.beginPreparedNativeComputePass(encoderSlot_, diagnostic))
                return false;
            break;
        case K::BindPreparedComputeBinding: {
            const auto& usage = *inputs->originalScalars()->originalUsage();
            const auto index = usage.references()[op.firstReference].sourceIndex;
            const auto* selected = roots_.select(closure_->inventory_, index);
            if (!selected) {
                diagnostic = "RHI-METAL-ROOT-COMMAND-UNRETAINED-ROOT";
                return false;
            }
            if (!command.recordComputePipelineBind(selected->pipeline_, diagnostic))
                return false;
            auto encoder =
                reinterpret_cast<id<MTLComputeCommandEncoder>>(command.nativeComputeEncoder_.get());
            for (const auto& group : selected->groups_)
                MetalRhiNativeFunctionBindingRecording(
                    *group.prepared, metalRhiPreparedComputeLayoutValidator(*selected->pipeline_))
                    .record(encoder, nil);
            break;
        }
        case K::Dispatch: {
            const auto* dispatch = inputs->originalScalars()
                                       ->originalArguments()
                                       ->records()[op.commandIndex]
                                       .scalar.get<CommandScalarArguments::Dispatch>();
            if (!dispatch
                || !command.recordDispatch(dispatch->x, dispatch->y, dispatch->z, diagnostic))
                return false;
            break;
        }
        case K::EndComputePass:
            if (!command.recordComputePassEnd(diagnostic))
                return false;
            encoderSlot_->reset();
            break;
        case K::BeginTransferPass:
            if (!command.beginPreparedNativeBlitPass(encoderSlot_, diagnostic))
                return false;
            break;
        case K::BufferCopy: {
            BufferCopyDesc copy;
            if (!std::get<TransferCopyArguments>(*inputs->descriptorAt(op.commandIndex)).write(copy)
                || !command.recordBlitCopy(copy, diagnostic))
                return false;
            break;
        }
        case K::EndTransferPass:
            if (!command.recordBlitPassEnd(diagnostic))
                return false;
            encoderSlot_->reset();
            break;
        case K::Finish:
            if (!command.finish(diagnostic))
                return false;
            break;
        default:
            return fail("RHI-METAL-ROOT-COMMAND-OPERATION");
        }
    }
    if (const auto* id = advance(Life::Event::RecordingValidatedAndDrained))
        return fail(id);
    return true;
}

bool MetalOriginalBindlessCommand::nativeCompleted(bool success) noexcept
{
    if (phase_ != Life::Phase::SubmitPending)
        return false;
    const auto& seal = reservation_->originalSeal();
    bool published = false;
    try {
        core::CpuMemoryQuotaScope scope(originalMetadataQuota());
        core::pmr::String diagnostic;
        bool read = success && !validatePending();
        for (const auto& binding : readbacks_.bindings()) {
            if (!read)
                break;
            const auto& source = binding->originalReadbackSource();
            const auto native = metalRhiNativePayloadFor(*source->originalObject());
            const auto& request = binding->originalRequest();
            const auto& result = binding->originalResult();
            read = readMetalRhiNativeBuffer(native,
                result->bytes_.data(),
                result->bytes_.size(),
                request->offsetBytes(),
                source->originalObject()->desc().sizeBytes,
                diagnostic);
        }
        if (read && !seal->commitOriginalRootSubmission()
            && !advance(Life::Event::CompletionProven)) {
            for (const auto& binding : readbacks_.bindings())
                binding->originalResult()->published_ = true;
            published = true;
        }
    } catch (const std::bad_alloc&) {
    } catch (const std::length_error&) {
    }
    if (!published) {
        (void)advance(Life::Event::CompletionUnproven);
        seal->invalidateOriginalOwnership();
        (void)advance(Life::Event::SubmittedQueueAndCallbacksDrained);
    }
    additionalFaults_.release();
    fault_.release();
    return published;
}

std::shared_ptr<const MetalRhiOrdinaryPreparedComputePush> prepareMetalRhiOrdinaryComputePush(
    const rhi::ComputePipeline& pipeline,
    const rhi::PushConstantsDesc& push,
    const MetalRhiOrdinaryBindingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (const auto* id = budget.validate(quota)) {
        diagnostic = id;
        return {};
    }
    const auto command = rhi::semantics::validatePushConstantCommand(
        push.stages, push.offsetBytes, push.sizeBytes, push.data);
    if (!command.success) {
        diagnostic = command.diagnostics;
        return {};
    }
    const auto bound = rhi::semantics::validatePushConstantBinding(pipeline.layout(),
        rhi::ShaderStage::Compute,
        push.stages,
        push.offsetBytes,
        push.sizeBytes,
        &pipeline.shaderReflection());
    if (!bound.success) {
        diagnostic = bound.diagnostics;
        return {};
    }
    auto native = metalRhiNativePayloadFor(pipeline);
    if (!native || !ordinaryComputePipelineValid(*native, native->nativeDevice)
        || pipeline.bindlessAuthorization()) {
        diagnostic = "RHI-METAL-ORDINARY-PREPARED-PIPELINE-INVALID";
        return {};
    }
    const auto slot = native->pushConstantMetalBufferIndex;
    if (slot != pipeline.shaderReflection().pushConstantMetalBufferIndex
        || !rhi::semantics::validMetalDirectBindingSlot(rhi::DescriptorType::UniformBuffer, slot)
        || (native->nativeBindingStages.stages(rhi::DescriptorType::UniformBuffer, slot)
               & rhi::ShaderStage::Compute)
            == rhi::ShaderStage::None) {
        diagnostic = "[PC-E020] RHI-METAL-ORDINARY-PUSH-SLOT-PIPELINE-MISMATCH";
        return {};
    }
    const MetalRhiBufferBindingFact* fact = nullptr;
    if (const auto* id = validateMetalRhiBufferOffset(native->computeBufferFacts,
            native->computeBufferFactsVersion,
            slot,
            0u,
            push.sizeBytes,
            fact)) {
        diagnostic = rhi::semantics::PushConstantDiagCode::kNativeSlot;
        diagnostic += id;
        return {};
    }
    if (fact->requiredBytes != push.sizeBytes) {
        diagnostic = "[PC-E020] RHI-METAL-ORDINARY-PUSH-NATIVE-SIZE-MISMATCH";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = core::makeShared<MetalRhiOrdinaryPreparedComputePush>(
            MetalRhiOrdinaryPreparedComputePush::ConstructionKey{},
            std::move(native),
            slot,
            fact->alignment,
            push,
            budget,
            std::move(quota));
        const auto valid = result->validate();
        if (!valid.success) {
            diagnostic = valid.diagnostics;
            return {};
        }
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED";
        return {};
    }
}

std::shared_ptr<const MetalRhiOrdinaryPreparedComputeBinding> prepareMetalRhiOrdinaryComputeBinding(
    const rhi::ComputePipeline& pipeline,
    const rhi::DescriptorSet& descriptor,
    std::uint32_t set,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    const MetalRhiOrdinaryBindingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    core::pmr::String& diagnostic,
    const std::uint32_t* dynamicOffsets,
    std::uint32_t dynamicOffsetCount)
{
    diagnostic.clear();
    if (const auto* id = budget.validate(quota)) {
        diagnostic = id;
        return {};
    }
    if (!registry || !metalRhiDescriptorRegistryMatches(descriptor, registry)) {
        diagnostic = "RHI-METAL-ORDINARY-PREPARED-REGISTRY-MISMATCH";
        return {};
    }
    const auto binding = rhi::validateDescriptorSetBinding(pipeline.layout(), set, descriptor);
    if (!binding.success) {
        diagnostic = binding.diagnostics;
        return {};
    }
    const auto* slot = rhi::findDescriptorSetSlot(pipeline.layout()->desc(), set);
    const auto* expected =
        slot ? rhi::findDescriptorSetLayout(pipeline.layout()->desc(), slot->layoutName) : nullptr;
    if (!expected || !rhi::validateExactDescriptorSetLayout(*expected, descriptor).success) {
        diagnostic = "RHI-METAL-ORDINARY-PREPARED-LAYOUT-MISMATCH";
        return {};
    }
    const auto offsets = rhi::validateDynamicOffsetArguments(
        descriptor.layout(), dynamicOffsets, dynamicOffsetCount);
    if (!offsets.success) {
        diagnostic = "RHI-METAL-ORDINARY-OFFSET-COUNT";
        return {};
    }
    auto native = metalRhiNativePayloadFor(pipeline);
    if (!native || !ordinaryComputePipelineValid(*native, native->nativeDevice)
        || pipeline.bindlessAuthorization()) {
        diagnostic = "RHI-METAL-ORDINARY-PREPARED-PIPELINE-INVALID";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto snapshot = metalRhiNativePayloadFor(descriptor);
        if (!snapshot) {
            diagnostic = "RHI-METAL-ORDINARY-PREPARED-SNAPSHOT-MISSING";
            return {};
        }
        if (const auto* id = MetalRhiPreparedDescriptorIdentityValidator::capture(*snapshot)) {
            diagnostic = id;
            return {};
        }
        for (const auto& binding : snapshot->bindings) {
            if (binding.type != rhi::DescriptorType::StorageTexture)
                continue;
            const auto* declared =
                rhi::findDescriptorSetBinding(descriptor.layout(), binding.binding);
            const auto* id = rhi::validateStorageTextureViewFormat(
                declared ? declared->storageTextureFormat : rhi::TextureFormat::Unknown,
                binding.textureView ? binding.textureView->format : rhi::TextureFormat::Unknown);
            if (id) {
                diagnostic = id;
                return {};
            }
        }
        const auto mapper = metalRhiPreparedComputeLayoutValidator(*native);
        for (auto& binding : snapshot->bindings) {
            if (binding.dynamicOffsetIndex == UINT32_MAX)
                continue;
            if (binding.type != rhi::DescriptorType::UniformBuffer || binding.arrayCount != 1u
                || binding.argumentKind != MetalRhiArgumentKind::Buffer || !binding.buffer
                || binding.dynamicOffsetIndex >= dynamicOffsetCount) {
                diagnostic = "RHI-METAL-ORDINARY-OFFSET-DOMAIN-UNIMPLEMENTED";
                return {};
            }
            const auto offset = dynamicOffsets[binding.dynamicOffsetIndex];
            binding.dynamicOffsetIndex = UINT32_MAX;
            MetalRhiPreparedBindingLocation location;
            const auto mapped = mapper.resolve(set, binding, location);
            if (!mapped.success) {
                diagnostic = mapped.diagnostics;
                return {};
            }
            if (!location.active || location.array) {
                diagnostic = "RHI-METAL-ORDINARY-OFFSET-MAPPING-MISSING";
                return {};
            }
            const auto buffer = nativeBuffer(binding.buffer);
            const MetalRhiBufferBindingFact* fact = nullptr;
            if (const auto* id = validateMetalRhiBufferOffset(native->computeBufferFacts,
                    native->computeBufferFactsVersion,
                    location.slot,
                    offset,
                    buffer ? buffer.length : 0u,
                    fact)) {
                diagnostic = id;
                return {};
            }
            binding.bufferOffsetBytes = offset;
            binding.bufferOffsetAlignment = fact->alignment;
            binding.bufferRequiredBytes = fact->requiredBytes;
        }
        rhi::BindlessBindingBudget arguments;
        arguments.maxArgumentBuffers = budget.maxArgumentBuffers;
        arguments.maxArgumentBufferBytes = budget.maxArgumentBufferBytes;
        MetalRhiNativeArgumentPreparationBudget reservation(arguments, true);
        MetalRhiNativeFunctionBindingPreparation preparation(native,
            std::move(snapshot),
            set,
            rhi::ShaderStage::Compute,
            native->computeFunction,
            metalRhiPreparedComputeLayoutValidator(*native));
        if (!preparation.preflight(reservation, diagnostic))
            return {};
        auto function =
            preparation.build<MetalRhiNativePreparedComputeBindings>(reservation, diagnostic);
        if (!function)
            return {};
        auto result = core::makeShared<MetalRhiOrdinaryPreparedComputeBinding>(
            MetalRhiOrdinaryPreparedComputeBinding::ConstructionKey{},
            std::move(function),
            pipeline,
            descriptor,
            std::move(registry),
            budget,
            std::move(quota));
        const auto valid = result->validateObservation();
        if (!valid.success) {
            diagnostic = valid.diagnostics;
            return {};
        }
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED";
        return {};
    }
}

std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings> prepareMetalRhiNativeGraphicsBindings(
    std::shared_ptr<MetalRhiNativePipelinePayload> pipeline,
    std::shared_ptr<MetalRhiNativeDescriptorGroupPayload> descriptors,
    std::uint32_t set,
    const std::array<std::shared_ptr<const MetalRhiPreparedGraphicsStageLayout>, 2>& layouts,
    const rhi::BindlessGraphicsBindingBudget& budget,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    MetalRhiNativeArgumentPreparationBudget reservation(budget, false);
    if (!pipeline || !pipeline->nativeRuntimeBacked || !pipeline->renderPipelinePayloadPresent
        || !pipeline->vertexFunction || !descriptors) {
        reservation.fail(diagnostic, "PAYLOAD-MISSING");
        return {};
    }
    const bool hasFragment =
        pipeline->preparedGraphicsStages == (rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment);
    const bool vertexOnly = pipeline->preparedGraphicsStages == rhi::ShaderStage::Vertex
        && pipeline->depthOnlyRenderPipeline
        && rhi::isDepthTextureFormat(pipeline->depthStencilFormat);
    if ((!hasFragment && !vertexOnly) || !layouts[0]
        || layouts[0]->stage != rhi::ShaderStage::Vertex
        || (hasFragment
                ? (!pipeline->fragmentFunction || !layouts[1]
                      || layouts[1]->stage != rhi::ShaderStage::Fragment)
                : (pipeline->fragmentFunction || pipeline->fragmentShaderLibrary || layouts[1]))) {
        reservation.fail(diagnostic, "ENTRY-STAGE-MISMATCH");
        return {};
    }
    if (static_cast<bool>(budget.maxArgumentBuffers)
        != static_cast<bool>(budget.maxArgumentBufferBytes)) {
        diagnostic = "RHI-BINDLESS-PREPARED-GRAPHICS-BUDGET";
        return {};
    }
    auto result = core::makeShared<MetalRhiNativePreparedGraphicsBindings>(
        MetalRhiNativePreparedGraphicsBindings::ConstructionKey{});
    // Freeze descriptor identity before encoding argument bytes. Later edits
    // to the caller's mutable group cannot alter residency or direct bindings.
    result->pipeline = pipeline;
    result->descriptors = core::makeShared<MetalRhiNativeDescriptorGroupPayload>(*descriptors);
    // Capture before exposing a const snapshot or accessing native members.
    auto snapshot =
        std::const_pointer_cast<MetalRhiNativeDescriptorGroupPayload>(result->descriptors);
    if (const auto* id = MetalRhiPreparedDescriptorIdentityValidator::capture(*snapshot)) {
        diagnostic = id;
        return {};
    }
    result->set = set;
    std::array<std::optional<MetalRhiNativeFunctionBindingPreparation>, 2> preparations;
    const std::array functions{pipeline->vertexFunction, pipeline->fragmentFunction};
    for (std::size_t i = 0u; i < layouts.size(); ++i) {
        if (!layouts[i])
            continue;
        auto retained = core::makeShared<MetalRhiPreparedGraphicsStageLayout>();
        retained->stage = layouts[i]->stage;
        retained->entry = layouts[i]->entry;
        retained->direct = layouts[i]->direct;
        retained->arrays = layouts[i]->arrays;
        retained->pushSlot = layouts[i]->pushSlot;
        const auto valid = retained->validate(
            *result->descriptors, set, pipeline->nativeBindingStages, pipeline->vertexStreams);
        if (!valid.success) {
            diagnostic = valid.diagnostics;
            return {};
        }
        result->layouts[i] = std::move(retained);
        const auto& layout = *result->layouts[i];
        preparations[i].emplace(pipeline,
            result->descriptors,
            set,
            layout.stage,
            functions[i],
            MetalRhiPreparedBindingLayoutValidator(layout.direct,
                layout.arrays,
                pipeline->nativeBindingStages,
                layout.stage,
                layout.pushSlot),
            layout.entry);
        if (!preparations[i]->preflight(reservation, diagnostic))
            return {};
    }
    for (std::size_t i = 0u; i < preparations.size(); ++i) {
        if (!preparations[i])
            continue;
        result->stages[i] =
            preparations[i]->build<MetalRhiNativePreparedFunctionBindings>(reservation, diagnostic);
        if (!result->stages[i])
            return {};
    }
    result->argumentCount = static_cast<std::uint32_t>(reservation.counts.used());
    result->encodedArgumentBytes = reservation.encodedBytes.used();
    result->argumentBytes = reservation.allocatedBytes.used();
    return result;
}

std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>
MetalRhiOrdinaryPreparedGraphicsBinding::prepare(
    std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider,
    const rhi::DescriptorSet& descriptor,
    std::uint32_t set,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    const MetalRhiOrdinaryBindingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    core::pmr::String& diagnostic,
    const std::uint32_t* dynamicOffsets,
    std::uint32_t dynamicOffsetCount)
{
    diagnostic.clear();
    if (const auto* id = budget.validate(quota)) {
        diagnostic = id;
        return {};
    }
    if (!provider || provider->quota() != quota) {
        diagnostic = "RHI-METAL-ORDINARY-GRAPHICS-PROVIDER-QUOTA-MISMATCH";
        return {};
    }
    if (!registry || !metalRhiDescriptorRegistryMatches(descriptor, registry)) {
        diagnostic = "RHI-METAL-ORDINARY-PREPARED-REGISTRY-MISMATCH";
        return {};
    }
    const auto layout = provider->setLayouts().validate(set, descriptor);
    if (!layout.success) {
        diagnostic = layout.diagnostics;
        return {};
    }
    const auto offsets = rhi::validateDynamicOffsetArguments(
        descriptor.layout(), dynamicOffsets, dynamicOffsetCount);
    if (!offsets.success) {
        diagnostic = "RHI-METAL-ORDINARY-OFFSET-COUNT";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto snapshot = metalRhiNativePayloadFor(descriptor);
        if (!snapshot) {
            diagnostic = "RHI-METAL-ORDINARY-PREPARED-SNAPSHOT-MISSING";
            return {};
        }
        if (const auto* id = MetalRhiPreparedDescriptorIdentityValidator::capture(*snapshot)) {
            diagnostic = id;
            return {};
        }
        for (auto& binding : snapshot->bindings) {
            const auto offsetIndex = binding.dynamicOffsetIndex;
            if (offsetIndex != UINT32_MAX
                && (binding.type != rhi::DescriptorType::UniformBuffer
                    || offsetIndex >= dynamicOffsetCount)) {
                diagnostic = "RHI-METAL-ORDINARY-OFFSET-DOMAIN-UNIMPLEMENTED";
                return {};
            }
            if (!MetalRhiOrdinaryGraphicsResourceDomain::isReadOnlyBuffer(binding.type))
                continue;
            if ((binding.arrayCount != 1u
                    && !MetalRhiOrdinaryGraphicsResourceDomain::isReadOnlyBufferArray(
                        binding.type, binding.arrayCount))
                || binding.argumentKind != MetalRhiArgumentKind::Buffer || !binding.buffer) {
                diagnostic = "RHI-METAL-ORDINARY-OFFSET-DOMAIN-UNIMPLEMENTED";
                return {};
            }
            binding.bufferOffsetBytes =
                offsetIndex == UINT32_MAX ? 0u : dynamicOffsets[offsetIndex];
            binding.dynamicOffsetIndex = UINT32_MAX;
            const auto buffer = nativeBuffer(binding.buffer);
            bool active = false;
            for (std::size_t i = 0u; i < provider->stages().size(); ++i) {
                const auto& stage = provider->stages()[i];
                if (!stage)
                    continue;
                MetalRhiPreparedBindingLocation location;
                const auto mapped =
                    stage->resolve(binding, set, provider->native()->nativeBindingStages, location);
                if (!mapped.success) {
                    diagnostic = mapped.diagnostics;
                    return {};
                }
                if (!location.active)
                    continue;
                active = true;
                if (location.array) {
                    if (!MetalRhiOrdinaryGraphicsResourceDomain::isReadOnlyBufferArray(
                            binding.type, binding.arrayCount)
                        || binding.bufferOffsetBytes) {
                        diagnostic = "RHI-METAL-ORDINARY-OFFSET-DOMAIN-UNIMPLEMENTED";
                        return {};
                    }
                    auto& captured = binding.graphicsBufferPointeeRanges[i];
                    if (const auto* id = MetalRhiStageBufferPointeeRangeValidator::capture(
                            provider->native()->graphicsBufferPointeeFacts[i],
                            stage->stage,
                            location.slot,
                            location.argumentIndex + binding.arrayElement,
                            binding.arrayCount,
                            binding.arrayElement,
                            binding.bufferOffsetBytes,
                            buffer ? buffer.length : 0u,
                            captured)) {
                        diagnostic = id;
                        return {};
                    }
                    captured.nativeIdentity =
                        MetalRhiNativeResourceObservation(binding.buffer->resource);
                    continue;
                }
                if (binding.arrayCount != 1u) {
                    diagnostic = "RHI-METAL-ORDINARY-POINTEE-MAP-MISMATCH";
                    return {};
                }
                if (const auto* id = MetalRhiStageBufferRangeValidator::capture(
                        provider->native()->graphicsBufferFacts[i],
                        stage->stage,
                        location.slot,
                        binding.bufferOffsetBytes,
                        buffer ? buffer.length : 0u,
                        binding.graphicsBufferRanges[i])) {
                    diagnostic = id;
                    return {};
                }
                binding.graphicsBufferRanges[i].nativeIdentity =
                    MetalRhiNativeResourceObservation(binding.buffer->resource);
            }
            if (!active) {
                diagnostic = "RHI-METAL-ORDINARY-OFFSET-MAPPING-MISSING";
                return {};
            }
        }
        MetalRhiOrdinaryDescriptorObservation observation(descriptor, registry);
        const auto live = observation.validate(*snapshot);
        if (!live.success) {
            diagnostic = live.diagnostics;
            return {};
        }
        rhi::BindlessGraphicsBindingBudget arguments;
        arguments.maxArgumentBuffers = budget.maxArgumentBuffers;
        arguments.maxArgumentBufferBytes = budget.maxArgumentBufferBytes;
        auto prepared = prepareMetalRhiNativeGraphicsBindings(provider->native(),
            std::move(snapshot),
            set,
            provider->stages(),
            arguments,
            diagnostic);
        if (!prepared)
            return {};
        auto result = core::makeShared<MetalRhiOrdinaryPreparedGraphicsBinding>(ConstructionKey{},
            std::move(provider),
            std::move(prepared),
            descriptor,
            std::move(registry),
            budget,
            std::move(quota));
        const auto valid = result->validateObservation();
        if (!valid.success) {
            diagnostic = valid.diagnostics;
            return {};
        }
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED";
        return {};
    }
}

MetalRhiNativeCommandPayload::MetalRhiNativeCommandPayload(
    std::shared_ptr<MetalRhiNativeCommandStats> stats,
    std::shared_ptr<void> nativeQueue,
    std::shared_ptr<void> nativeCommandBuffer,
    std::shared_ptr<MetalRhiResourceRegistry> resourceRegistry,
    std::shared_ptr<core::AtomicCapacityLease> commandCapacity,
    std::uint32_t maxBindlessPipelines,
    std::uint32_t maxBindlessSnapshots,
    std::uint32_t maxBindlessGraphicsAccesses)
    : stats_(std::move(stats)), nativeDevice_(metalRhiNativeDeviceIdentity()),
      nativeQueue_(std::move(nativeQueue)), commandCapacity_(std::move(commandCapacity)),
      nativeCommandBuffer_(std::move(nativeCommandBuffer)),
      resourceRegistry_(std::move(resourceRegistry)),
      retainedResourcePayloads_(&core::defaultCpuMemoryResource()),
      retainedResourcePayloadIdentities_(&core::defaultCpuMemoryResource()),
      retainedStagingPayloads_(&core::defaultCpuMemoryResource()),
      retainedArgumentBuffers_(&core::defaultCpuMemoryResource()),
      retainedBindlessPipelines_(maxBindlessPipelines),
      retainedBindlessSnapshots_(maxBindlessSnapshots),
      retainedPreparedBindings_(maxBindlessSnapshots),
      submissionRetention_(core::makeShared<SubmissionRetention>()),
      graphicsAccesses_(maxBindlessGraphicsAccesses, rhi::BindlessAccessRetention::GraphicsPass),
      leasedBindlessFaults_(&core::defaultCpuMemoryResource()),
      pendingBindlessFaultReadbacks_(&core::defaultCpuMemoryResource()),
      bindlessFaultOutcomes_(&core::defaultCpuMemoryResource()),
      preparedFaultOutcomes_(&core::defaultCpuMemoryResource()),
      maxBindlessPipelines_(maxBindlessPipelines), maxBindlessSnapshots_(maxBindlessSnapshots),
      cachedArgumentBuffers_(&core::defaultCpuMemoryResource()),
      retainedQueryPools_(&core::defaultCpuMemoryResource()),
      recordedTimestampQueries_(&core::defaultCpuMemoryResource()),
      recordedPipelineStatisticsQueries_(&core::defaultCpuMemoryResource())
{
    leasedBindlessFaults_.reserve(maxBindlessSnapshots_);
    pendingBindlessFaultReadbacks_.reserve(maxBindlessSnapshots_);
    bindlessFaultOutcomes_.reserve(maxBindlessSnapshots_);
    preparedFaultOutcomes_.reserve(maxBindlessSnapshots_);
    for (std::uint32_t index = 0u; index < maxBindlessSnapshots_; ++index) {
        preparedFaultOutcomes_.push_back(core::makeShared<MetalRhiBindlessFaultReadback>());
        ++stats_->nativeBindlessFaultOutcomePreparationCount;
    }
}

bool MetalRhiNativeCommandPayload::recordBufferBarrier(std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    if (!nativeCommandBuffer_ || nativeRenderEncoder_ || nativeComputeEncoder_
        || nativeBlitEncoder_) {
        diagnostics = "RHI-METAL-BUFFER-BARRIER-ENCODER-BOUNDARY-INVALID";
        return false;
    }
    // Metal's hazard tracking provides the equivalent ordering guarantee for
    // resources in one command buffer. Explicit transitions are represented
    // by this encoder-boundary lowering; no native resource layout is guessed.
    stats_->nativeResourceBarrierEncoded = true;
    return true;
}

bool MetalRhiNativeCommandPayload::recordTextureBarrier(std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    if (!nativeCommandBuffer_ || nativeRenderEncoder_ || nativeComputeEncoder_
        || nativeBlitEncoder_) {
        diagnostics = "RHI-METAL-TEXTURE-BARRIER-ENCODER-BOUNDARY-INVALID";
        return false;
    }
    stats_->nativeResourceBarrierEncoded = true;
    return true;
}

bool MetalRhiNativeCommandPayload::recordAliasingBarrier(const rhi::AliasingBarrierDesc& desc,
    const std::shared_ptr<MetalRhiNativeTexturePayload>& beforeTexture,
    const std::shared_ptr<MetalRhiNativeTexturePayload>& afterTexture,
    std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    if (!@available(macOS 10.13, iOS 11.0, *)) {
        diagnostics = "RHI-METAL-ALIAS-BARRIER-API-UNAVAILABLE";
        return false;
    }
    if (desc.aliasGroup == 0 || desc.ownershipHandoff
        || desc.sourceQueueRole != desc.destinationQueueRole
        || desc.sourceStages == rhi::PipelineStage::None
        || desc.destinationStages == rhi::PipelineStage::None) {
        diagnostics = "RHI-METAL-ALIAS-BARRIER-DESC-INVALID";
        return false;
    }
    if (!nativeCommandBuffer_ || nativeRenderEncoder_ || nativeComputeEncoder_ || nativeBlitEncoder_
        || !beforeTexture || !afterTexture
        || !commandDeviceMatches(nativeDevice_, beforeTexture->resource.nativeDevice)
        || !commandDeviceMatches(nativeDevice_, afterTexture->resource.nativeDevice)
        || !beforeTexture->resource.nativeRuntimeBacked
        || !afterTexture->resource.nativeRuntimeBacked || !beforeTexture->resource.nativeObject
        || !afterTexture->resource.nativeObject) {
        diagnostics = "RHI-METAL-ALIAS-BARRIER-NATIVE-PAYLOAD-MISSING";
        return false;
    }
    if (!beforeTexture->aliasHeap || !afterTexture->aliasHeap
        || beforeTexture->aliasHeap.get() != afterTexture->aliasHeap.get()) {
        diagnostics = "RHI-METAL-ALIAS-BARRIER-HEAP-MISMATCH";
        return false;
    }
    @autoreleasepool {
        id<MTLTexture> before =
            reinterpret_cast<id<MTLTexture>>(beforeTexture->resource.nativeObject.get());
        if (before == nil) {
            diagnostics = "RHI-METAL-ALIAS-BARRIER-BEFORE-TEXTURE-MISSING";
            return false;
        }
        id<MTLCommandBuffer> commandBuffer =
            reinterpret_cast<id<MTLCommandBuffer>>(nativeCommandBuffer_.get());
        id<MTLDevice> device = commandBuffer == nil ? nil : commandBuffer.device;
        id<MTLFence> fence = device == nil ? nil : [device newFence];
        id<MTLBlitCommandEncoder> producer =
            commandBuffer == nil ? nil : [commandBuffer blitCommandEncoder];
        if (fence == nil || producer == nil) {
            if (fence != nil) {
                [fence release];
            }
            diagnostics = "RHI-METAL-ALIAS-BARRIER-PRODUCER-ENCODER-FAILED";
            return false;
        }
        [producer updateFence:fence];
        [producer endEncoding];

        id<MTLBlitCommandEncoder> consumer = [commandBuffer blitCommandEncoder];
        if (consumer == nil) {
            [fence release];
            // The producer encoder has already been ended. Metal cannot rewind
            // that command, so make the command buffer permanently non-submittable
            // instead of allowing a partial alias barrier to finish successfully.
            poisonRecording("RHI-METAL-ALIAS-BARRIER-CONSUMER-ENCODER-FAILED");
            diagnostics = "RHI-METAL-ALIAS-BARRIER-CONSUMER-ENCODER-FAILED";
            return false;
        }
        retainResourcePayload(
            retainedResourcePayloads_, retainedResourcePayloadIdentities_, beforeTexture);
        retainResourcePayload(
            retainedResourcePayloads_, retainedResourcePayloadIdentities_, afterTexture);
        [before makeAliasable];
        [consumer waitForFence:fence];
        [consumer endEncoding];
        [fence release];
        stats_->nativeAliasingBarrierEncoded = true;
        stats_->nativeAliasableTransitionEncoded = true;
        return true;
    }
}

namespace {
template <class Authorize>
bool prepareAuthorizedNativeRenderPass(const rhi::RenderPassDesc& desc,
    const std::shared_ptr<MetalRhiResourceRegistry>& registry,
    const std::shared_ptr<void>& device,
    MetalRhiNativeRenderPassPreparation& result,
    core::pmr::String& diagnostics,
    Authorize&& authorize)
{
    if (const auto* id = authorize()) {
        diagnostics = id;
        return false;
    }
    if (!registry || !device) {
        diagnostics = "RHI-METAL-RENDER-PASS-NATIVE-PAYLOAD-MISSING";
        return false;
    }
    const auto operations = rhi::semantics::validateRenderPassAttachmentOperations(desc);
    if (!operations.success) {
        diagnostics = operations.diagnostics;
        return false;
    }
    const auto observe = [&](rhi::GpuResourceId id, const MetalRhiRegisteredResource& resource) {
        const auto* native = resource.nativeResource();
        if (!native || result.attachmentCount == result.attachments.size()) {
            diagnostics = "RHI-METAL-RENDER-PASS-ATTACHMENT-OWNERSHIP-INVALID";
            return false;
        }
        result.attachments[result.attachmentCount++] = {id,
            resource,
            native->nativeDevice,
            native->nativeObject,
            native->allocationReleaseToken};
        return true;
    };
    if ((desc.colorAttachments.empty() && !desc.hasDepthStencilAttachment)
        || desc.colorAttachments.size() > kMetalRenderPassColorSlots
        || desc.subpasses.size() > rhi::RenderPassCompatibilityDesc::kMaxSubpasses
        || (desc.subpasses.empty() && !desc.subpassDependencies.empty())) {
        diagnostics = "RHI-METAL-RENDER-PASS-DESC-UNSUPPORTED-OR-INVALID";
        return false;
    }
    const auto colorCount = static_cast<std::uint32_t>(desc.colorAttachments.size());
    const auto subpassCount =
        desc.subpasses.empty() ? 1u : static_cast<std::uint32_t>(desc.subpasses.size());
    const auto sameExtent = [](const rhi::Extent3D& lhs, const rhi::Extent3D& rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height && lhs.depth == rhs.depth;
    };
    for (const auto& subpass : desc.subpasses) {
        if (subpass.colorAttachmentIndices.size() > colorCount) {
            diagnostics = "RHI-METAL-RENDER-PASS-SUBPASS-COLOR-MAPPING-EXCEEDS-ATTACHMENTS";
            return false;
        }
        for (std::uint32_t targetIndex = 0; targetIndex < subpass.colorAttachmentIndices.size();
            ++targetIndex) {
            // Generated Metal fragment outputs and framebuffer-fetch inputs
            // use the attachment's physical color slot. There is no approved
            // shader output remapping ABI, so a reorder or sparse mapping must
            // fail instead of silently writing another attachment.
            if (subpass.colorAttachmentIndices[targetIndex] != targetIndex) {
                diagnostics = "RHI-METAL-RENDER-PASS-SUBPASS-COLOR-MAPPING-NOT-LOWERED";
                return false;
            }
        }
        for (const auto index : subpass.colorAttachmentIndices) {
            if (index >= colorCount) {
                diagnostics = "RHI-METAL-RENDER-PASS-SUBPASS-COLOR-INDEX-OUT-OF-RANGE";
                return false;
            }
        }
        for (const auto index : subpass.inputAttachmentIndices) {
            if (index >= colorCount) {
                diagnostics = "RHI-METAL-RENDER-PASS-SUBPASS-INPUT-INDEX-OUT-OF-RANGE";
                return false;
            }
        }
        if (subpass.readDepthInputAttachment && !desc.hasDepthStencilAttachment) {
            diagnostics = "RHI-METAL-RENDER-PASS-SUBPASS-DEPTH-INPUT-WITHOUT-DEPTH-ATTACHMENT";
            return false;
        }
        if (subpass.readDepthInputAttachment && !subpass.useDepthStencilAttachment) {
            diagnostics = "RHI-METAL-RENDER-PASS-SUBPASS-DEPTH-INPUT-WITHOUT-DEPTH-USE";
            return false;
        }
        for (const auto index : subpass.inputAttachmentIndices) {
            if (index >= subpass.colorAttachmentIndices.size()) {
                diagnostics = "RHI-METAL-RENDER-PASS-SUBPASS-INPUT-NOT-ACTIVE-COLOR-ATTACHMENT";
                return false;
            }
        }
    }
    for (const auto& dependency : desc.subpassDependencies) {
        if (dependency.srcSubpass >= subpassCount || dependency.dstSubpass >= subpassCount
            || dependency.srcSubpass >= dependency.dstSubpass) {
            diagnostics = "RHI-METAL-RENDER-PASS-SUBPASS-DEPENDENCY-OUT-OF-RANGE";
            return false;
        }
    }
    // The render thread is a long-lived std::thread. Use an owning allocation
    // instead of an autoreleased descriptor, and release it at this function
    // boundary after Metal has copied its render-pass state into the encoder.
    MTLRenderPassDescriptor* renderPass = [[MTLRenderPassDescriptor alloc] init];
    result.descriptor = retainNativeObject(renderPass);
    if (!result.descriptor) {
        diagnostics = "RHI-METAL-RENDER-PASS-DESCRIPTOR-CREATE-FAILED";
        return false;
    }
    std::uint32_t renderSampleCount = 0u;
    rhi::Extent3D attachmentExtent;
    std::uint32_t attachmentArrayLayerCount = 0u;
    const auto validAttachmentLayering = [&](const MetalRhiRegisteredResource& resource) {
        if (desc.layeredRendering) {
            return resource.textureArrayLayerCount > 1u
                && resource.textureViewDimension == rhi::TextureViewDimension::Texture2DArray;
        }
        return resource.textureArrayLayerCount == 1u;
    };
    for (std::size_t index = 0; index < desc.colorAttachments.size(); ++index) {
        const auto& source = desc.colorAttachments[index];
        const auto resource = registry->find(source.target);
        id<MTLTexture> native = resource ? nativeRegisteredTexture(*resource) : nil;
        if (native == nil || !resource || resource->kind != rhi::DescriptorResourceKind::TextureView
            || !commandDeviceMatches(device, nativeRegisteredDevice(*resource))
            || !hasTextureUsage(resource->textureUsage, rhi::TextureUsage::RenderAttachment)
            || resource->textureFormat != source.format || resource->textureSampleCount == 0u
            || !validAttachmentLayering(*resource)) {
            diagnostics = "RHI-METAL-RENDER-PASS-COLOR-ATTACHMENT-MISSING";
            return false;
        }
        if (index == 0u) {
            attachmentExtent = resource->textureExtent;
            attachmentArrayLayerCount = resource->textureArrayLayerCount;
        } else if (!sameExtent(attachmentExtent, resource->textureExtent)
            || attachmentArrayLayerCount != resource->textureArrayLayerCount) {
            diagnostics = "RHI-METAL-RENDER-PASS-ATTACHMENT-EXTENT-MISMATCH";
            return false;
        }
        if (renderSampleCount == 0u) {
            renderSampleCount = resource->textureSampleCount;
        } else if (renderSampleCount != resource->textureSampleCount) {
            diagnostics = "RHI-METAL-RENDER-PASS-SAMPLE-COUNT-MISMATCH";
            return false;
        }
        if (isMemorylessAttachment(*resource)
            && (source.loadOp == rhi::LoadOp::Load
                || (!source.hasResolveTarget && source.storeOp != rhi::StoreOp::DontCare))) {
            diagnostics = "RHI-METAL-MEMORYLESS-ATTACHMENT-LOAD-STORE-INVALID";
            return false;
        }
        auto* attachment = renderPass.colorAttachments[index];
        attachment.texture = native;
        attachment.loadAction = source.loadOp == rhi::LoadOp::Clear ? MTLLoadActionClear
            : source.loadOp == rhi::LoadOp::DontCare                ? MTLLoadActionDontCare
                                                                    : MTLLoadActionLoad;
        attachment.storeAction =
            source.storeOp == rhi::StoreOp::DontCare ? MTLStoreActionDontCare : MTLStoreActionStore;
        attachment.clearColor = MTLClearColorMake(
            source.clearColor.r, source.clearColor.g, source.clearColor.b, source.clearColor.a);
        if (!source.hasResolveTarget && source.resolveTarget.isValid()) {
            diagnostics = "RHI-METAL-RENDER-PASS-RESOLVE-TARGET-FLAG-MISMATCH";
            return false;
        }
        if (source.hasResolveTarget) {
            const auto resolveResource = registry->find(source.resolveTarget);
            attachment.resolveTexture =
                resolveResource ? nativeRegisteredTexture(*resolveResource) : nil;
            if (attachment.resolveTexture == nil || !resolveResource
                || resolveResource->kind != rhi::DescriptorResourceKind::TextureView
                || !commandDeviceMatches(device, nativeRegisteredDevice(*resolveResource))
                || !hasTextureUsage(
                    resolveResource->textureUsage, rhi::TextureUsage::RenderAttachment)
                || resolveResource->textureFormat != source.format
                || resolveResource->textureSampleCount != 1u
                || resolveResource->textureArrayLayerCount != resource->textureArrayLayerCount
                || !validAttachmentLayering(*resolveResource) || resource->textureSampleCount <= 1u
                || !sameExtent(resource->textureExtent, resolveResource->textureExtent)) {
                diagnostics = "RHI-METAL-RENDER-PASS-RESOLVE-ATTACHMENT-MISSING";
                return false;
            }
            if (!observe(source.resolveTarget, *resolveResource))
                return false;
            attachment.storeAction = MTLStoreActionMultisampleResolve;
        }
        if (!observe(source.target, *resource))
            return false;
    }
    if (desc.hasDepthStencilAttachment) {
        const auto resource = registry->find(desc.depthStencilAttachment.target);
        id<MTLTexture> native = resource ? nativeRegisteredTexture(*resource) : nil;
        if (native == nil || !resource || resource->kind != rhi::DescriptorResourceKind::TextureView
            || !commandDeviceMatches(device, nativeRegisteredDevice(*resource))
            || !hasTextureUsage(resource->textureUsage, rhi::TextureUsage::RenderAttachment)
            || resource->textureFormat != desc.depthStencilAttachment.format
            || (desc.depthStencilAttachment.format != rhi::TextureFormat::D32Float
                && desc.depthStencilAttachment.format != rhi::TextureFormat::Depth24Stencil8)
            || resource->textureSampleCount == 0u || !validAttachmentLayering(*resource)) {
            diagnostics = "RHI-METAL-RENDER-PASS-DEPTH-ATTACHMENT-MISSING";
            return false;
        }
        if (renderSampleCount != 0u && renderSampleCount != resource->textureSampleCount) {
            diagnostics = "RHI-METAL-RENDER-PASS-SAMPLE-COUNT-MISMATCH";
            return false;
        }
        renderSampleCount = resource->textureSampleCount;
        if (isMemorylessAttachment(*resource)
            && (desc.depthStencilAttachment.depthLoadOp == rhi::LoadOp::Load
                || desc.depthStencilAttachment.depthStoreOp != rhi::StoreOp::DontCare
                || desc.depthStencilAttachment.stencilLoadOp == rhi::LoadOp::Load
                || desc.depthStencilAttachment.stencilStoreOp != rhi::StoreOp::DontCare)) {
            diagnostics = "RHI-METAL-MEMORYLESS-DEPTH-LOAD-STORE-INVALID";
            return false;
        }
        if (!desc.colorAttachments.empty()
            && !sameExtent(attachmentExtent, resource->textureExtent)) {
            diagnostics = "RHI-METAL-RENDER-PASS-DEPTH-EXTENT-MISMATCH";
            return false;
        }
        if (desc.colorAttachments.empty()) {
            attachmentExtent = resource->textureExtent;
            attachmentArrayLayerCount = resource->textureArrayLayerCount;
        } else if (attachmentArrayLayerCount != resource->textureArrayLayerCount) {
            diagnostics = "RHI-METAL-RENDER-PASS-ATTACHMENT-LAYER-COUNT-MISMATCH";
            return false;
        }
        if (!observe(desc.depthStencilAttachment.target, *resource))
            return false;
        auto* depth = renderPass.depthAttachment;
        depth.texture = native;
        depth.loadAction = desc.depthStencilAttachment.depthLoadOp == rhi::LoadOp::Clear
            ? MTLLoadActionClear
            : desc.depthStencilAttachment.depthLoadOp == rhi::LoadOp::DontCare
            ? MTLLoadActionDontCare
            : MTLLoadActionLoad;
        depth.storeAction = desc.depthStencilAttachment.depthStoreOp == rhi::StoreOp::DontCare
            ? MTLStoreActionDontCare
            : MTLStoreActionStore;
        depth.clearDepth = desc.depthStencilAttachment.depthClearValue;
        if (desc.depthStencilAttachment.format == rhi::TextureFormat::Depth24Stencil8) {
            auto* stencil = renderPass.stencilAttachment;
            stencil.texture = native;
            stencil.loadAction = desc.depthStencilAttachment.stencilLoadOp == rhi::LoadOp::Clear
                ? MTLLoadActionClear
                : desc.depthStencilAttachment.stencilLoadOp == rhi::LoadOp::DontCare
                ? MTLLoadActionDontCare
                : MTLLoadActionLoad;
            stencil.storeAction =
                desc.depthStencilAttachment.stencilStoreOp == rhi::StoreOp::DontCare
                ? MTLStoreActionDontCare
                : MTLStoreActionStore;
            stencil.clearStencil = desc.depthStencilAttachment.stencilClearValue;
        }
    }
    if (desc.layeredRendering && attachmentArrayLayerCount <= 1u) {
        diagnostics = "RHI-METAL-LAYERED-RENDER-PASS-REQUIRES-MULTIPLE-LAYERS";
        return false;
    }
    if (attachmentExtent.width == 0u || attachmentExtent.height == 0u
        || attachmentExtent.width > std::numeric_limits<std::uint32_t>::max()
        || attachmentExtent.height > std::numeric_limits<std::uint32_t>::max()) {
        diagnostics = "RHI-METAL-RENDER-PASS-ATTACHMENT-EXTENT-INVALID";
        return false;
    }
    renderPass.renderTargetArrayLength = desc.layeredRendering ? attachmentArrayLayerCount : 0u;
    result.width = attachmentExtent.width;
    result.height = attachmentExtent.height;
    result.subpassCount = subpassCount;
    result.sampleCount = renderSampleCount;
    result.colorCount = colorCount;
    result.layered = desc.layeredRendering;
    result.depthFormat = desc.hasDepthStencilAttachment ? desc.depthStencilAttachment.format
                                                        : rhi::TextureFormat::Unknown;
    for (std::size_t i = 0; i < desc.colorAttachments.size(); ++i)
        result.colorFormats[i] = desc.colorAttachments[i].format;
    if (!desc.colorAttachments.empty())
        result.firstClearColor = desc.colorAttachments.front().clearColor;
    return true;
}
} // namespace

const char* MetalOriginalBindlessCommand::validateStoragePasses() const noexcept
{
    using namespace rhi;
    if (!nextClosure_ || !attachmentLease_ || !attachmentLease_->charged() || clearPasses_.empty())
        return "RHI-METAL-ROOT-STORAGE-PASS-CUSTODY";
    for (const auto& pass : clearPasses_) {
        if (const auto* id = OriginalCommandTextureViewSource::validateOriginal(pass.source))
            return id;
        const auto& parent = *pass.source->originalParentLease()->originalObject();
        const auto nativeParent = metalRhiNativePayloadFor(parent);
        const auto nativeView = metalRhiNativePayloadFor(*pass.source->originalObject());
        if (!nativeParent || !nativeView || !nativeParent->originalContentExclusive()
            || !nativeView->originalContentExclusive() || nativeParent->aliasHeap
            || nativeParent->immutableRevision() || nativeView->immutableRevision()
            || !pass.parentIdentity.matches(nativeParent->resource)
            || !pass.viewIdentity.matches(nativeView->resource)
            || !core::SharedOwnerProjection::sameOwner(
                nativeParent->resource.nativeDevice, closure_->pipeline_->nativeDevice)
            || !core::SharedOwnerProjection::sameOwner(
                nativeView->resource.nativeDevice, closure_->pipeline_->nativeDevice))
            return "RHI-METAL-ROOT-STORAGE-PASS-NATIVE-IDENTITY";
        bool retained = false;
        const auto& view = pass.source->originalObject()->desc();
        for (const auto& element : closure_->root_->elements())
            if (const auto* source =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                        &element.owner)) {
                const auto& d = (*source)->originalObject()->desc();
                retained = retained
                    || (core::SharedOwnerProjection::sameOwner(
                            (*source)->originalParentLease(), pass.source->originalParentLease())
                        && d.baseMipLevel == view.baseMipLevel
                        && d.mipLevelCount == view.mipLevelCount
                        && d.baseArrayLayer == view.baseArrayLayer
                        && d.arrayLayerCount == view.arrayLayerCount && d.aspect == view.aspect);
            }
        if (!retained)
            return "RHI-METAL-ROOT-STORAGE-PASS-RANGE-CLOSURE";
    }
    return nullptr;
}

bool MetalOriginalBindlessCommand::prepareStoragePasses(rhi::GpuDevice& device,
    const std::shared_ptr<core::AtomicCapacityBudget>& capacity,
    core::pmr::String& diagnostic)
{
    using namespace rhi;
    const auto reject = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    const auto registry = metalRhiIssuingResourceRegistry(device);
    const auto& program = binding_->originalProgram();
    const auto& usage = *program->originalInputs()->originalScalars()->originalUsage();
    const auto passes = std::count_if(
        program->operations().begin(), program->operations().end(), [](const auto& op) {
            return op.kind == CommandUsageKind::BeginRenderPass;
        });
    if (!registry || !capacity || !passes || passes > closure_->budget_.maxAttachmentViews)
        return reject("RHI-METAL-ROOT-STORAGE-PASS-BUDGET");
    attachmentLease_ = core::makeUnique<core::AtomicCapacityLease>(capacity, passes);
    if (!attachmentLease_->charged())
        return reject("RHI-METAL-ROOT-STORAGE-PASS-CAPACITY");
    clearPasses_.reserve(passes);
    for (const auto& op : program->operations()) {
        if (op.kind != CommandUsageKind::BeginRenderPass)
            continue;
        const auto* value = program->originalInputs()->descriptorAt(op.commandIndex);
        const auto* args =
            value ? std::get_if<std::shared_ptr<const RenderPassArguments>>(value) : nullptr;
        if (!args || !*args || op.referenceCount != 1u)
            return reject("RHI-METAL-ROOT-STORAGE-PASS-DESCRIPTOR");
        const auto& desc = (*args)->desc();
        if (desc.colorAttachments.size() != 1u || desc.hasDepthStencilAttachment
            || desc.layeredRendering || desc.colorAttachments.front().loadOp != LoadOp::Clear
            || desc.colorAttachments.front().storeOp != StoreOp::Store
            || desc.colorAttachments.front().hasResolveTarget || !desc.subpasses.empty())
            return reject("RHI-METAL-ROOT-STORAGE-PASS-CLEAR-STORE");
        const auto index = usage.references()[op.firstReference].sourceIndex;
        const auto* source = std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
            &closure_->inventory_->sources()[index]);
        if (!source || !*source)
            return reject("RHI-METAL-ROOT-STORAGE-PASS-SOURCE");
        const auto& view = (*source)->originalObject()->desc();
        const auto& parent = *(*source)->originalParentLease()->originalObject();
        if ((*source)->resource() != desc.colorAttachments.front().target
            || view.format != TextureFormat::Rgba16Float
            || desc.colorAttachments.front().format != view.format
            || view.usage != TextureUsage::RenderAttachment
            || view.dimension != TextureViewDimension::Texture2D
            || view.aspect != TextureAspect::Color || view.mipLevelCount != 1u
            || view.arrayLayerCount != 1u || view.baseArrayLayer || parent.desc().sampleCount != 1u
            || parent.desc().arrayLayerCount != 1u)
            return reject("RHI-METAL-ROOT-STORAGE-PASS-VIEW");
        const auto nativeParent = metalRhiNativePayloadFor(parent);
        const auto nativeView = metalRhiNativePayloadFor(*(*source)->originalObject());
        if (!nativeParent || !nativeView)
            return reject("RHI-METAL-ROOT-STORAGE-PASS-PAYLOAD");
        ClearPass pass;
        pass.commandIndex = op.commandIndex;
        pass.source = *source;
        pass.parentIdentity = MetalRhiNativeResourceObservation(nativeParent->resource);
        pass.viewIdentity = MetalRhiNativeResourceObservation(nativeView->resource);
        clearPasses_.push_back(std::move(pass));
    }
    if (const auto* id = validateStoragePasses())
        return reject(id);
    for (auto& pass : clearPasses_) {
        const auto& args = std::get<std::shared_ptr<const RenderPassArguments>>(
            *program->originalInputs()->descriptorAt(pass.commandIndex));
        if (!prepareAuthorizedNativeRenderPass(args->desc(),
                registry,
                closure_->pipeline_->nativeDevice,
                pass.native,
                diagnostic,
                [&] {
                    return validateStoragePasses();
                }))
            return false;
    }
    return true;
}

bool MetalOriginalBindlessGraphicsPass::createNativeObjects(core::pmr::String& diagnostic)
{
    const auto* desc = descriptor();
    return desc
        && prepareAuthorizedNativeRenderPass(
            *desc, registry_, closure_->pipeline_->nativeDevice, native_, diagnostic, [&] {
                return validateAttachments();
            });
}

std::shared_ptr<MetalOriginalOrdinaryCommand> MetalOriginalOrdinaryCommand::prepare(
    const std::shared_ptr<const Program>& program,
    const rhi::GpuDevice& device,
    const rhi::OriginalRenderPassPreparationRequest& request,
    MetalRhiNativeCommandPayload& command,
    core::pmr::String& diagnostic)
{
    const auto reject = [&](const char* id) -> std::shared_ptr<MetalOriginalOrdinaryCommand> {
        diagnostic = id;
        return {};
    };
    if (const auto* id = inspect(program, device, request))
        return reject(id);
    if (!core::SharedOwnerProjection::sameOwner(
            command.resourceRegistry_, metalRhiIssuingResourceRegistry(device))
        || !command.nativeDevice_ || !command.nativeCommandBuffer_
        || !command.stats_->nativeRuntimeBacked)
        return reject("RHI-METAL-ORDINARY-NATIVE-ISSUER");
    const char* id = nullptr;
    auto meter = rhi::CommandRecordingMeter::prepare(*program->recordingBudget(), id);
    if (!meter)
        return reject(id);
    auto owner = shareOriginal(
        core::makeUnique<MetalOriginalOrdinaryCommand>(ConstructionKey{}, program, *meter));
    if (!owner)
        return reject("RHI-METAL-ORDINARY-OWNER-ALLOCATION");
    owner->command_ = &command;
    owner->registry_ = command.resourceRegistry_;
    owner->nativeDevice_ = command.nativeDevice_;
    owner->passes_.reserve(program->counts().renderPasses);
    owner->attachments_.reserve(program->counts().attachmentReferences);
    owner->dispatches_.reserve(
        program->recordingBudget()->limits.commands[static_cast<std::size_t>(K::Dispatch)]);
    owner->encoderSlot_ = core::makeShared<core::ExternalObjectSlot>([](void* object) noexcept {
        [reinterpret_cast<::id>(object) release];
    });
    const auto& sources =
        program->originalInputs()->originalScalars()->originalUsage()->originalSources()->sources();
    std::size_t pipelineIndex = SIZE_MAX;
    for (const auto& operation : program->operations()) {
        if (operation.kind == K::BeginRenderPass)
            owner->passes_.emplace_back(operation.commandIndex);
        if (operation.kind == K::BindComputePipeline)
            pipelineIndex = std::get<rhi::OriginalCommandPipelineBinding>(
                *program->originalInputs()->descriptorAt(operation.commandIndex))
                                .sourceIndex;
        if (operation.kind == K::Dispatch) {
            const auto& source =
                std::get<std::shared_ptr<const rhi::OriginalCommandComputePipelineSource>>(
                    sources[pipelineIndex]);
            const auto& groups = *program->originalInputs()
                                      ->originalScalars()
                                      ->originalArguments()
                                      ->records()[operation.commandIndex]
                                      .scalar.get<rhi::CommandScalarArguments::Dispatch>();
            auto frozen = rhi::OriginalComputeDispatchRecordingAdmission::freeze(
                {device, request.dispatchDomain}, source, {groups.x, groups.y, groups.z}, id);
            if (!frozen)
                return reject(id);
            owner->dispatches_.push_back(
                {operation.commandIndex, pipelineIndex, std::move(*frozen)});
        }
    }
    const auto content = rhi::OriginalRenderPassContentContract::visit(program,
        request.storage,
        *request.content,
        [&](std::size_t at,
            std::size_t operand,
            const auto& initial,
            const auto& state,
            bool stored) noexcept -> const char* {
            const auto& operation = program->operations()[at];
            const auto& usage = *program->originalInputs()->originalScalars()->originalUsage();
            const auto& source =
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                    sources[usage.references()[operation.firstReference + operand].sourceIndex]);
            bool first = true;
            for (const auto& previous : owner->attachments_)
                if (owner->attachmentSource(previous).originalParentLease()->originalObject()
                    == source->originalParentLease()->originalObject())
                    first = false;
            owner->attachments_.push_back(
                {at, operand, initial, state, stored, first, {}, std::nullopt});
            return nullptr;
        });
    if (content.diagnostic)
        return reject(content.diagnostic);
    for (auto& pass : owner->passes_) {
        const auto& args = std::get<std::shared_ptr<const rhi::RenderPassArguments>>(
            *program->originalInputs()->descriptorAt(pass.command));
        if (!prepareAuthorizedNativeRenderPass(args->desc(),
                command.resourceRegistry_,
                command.nativeDevice_,
                pass.native,
                diagnostic,
                [&] {
                    return inspect(program, device, request);
                }))
            return {};
    }
    const auto& budget = *program->recordingBudget();
    std::uint64_t retainers = budget.limits.resourceReferences;
    for (const auto extra : {budget.limits.descriptorElements, program->counts().pipelineBinds}) {
        if (!core::canAddUnsigned(retainers, extra))
            return reject("RHI-METAL-ORDINARY-RETENTION-CAPACITY");
        retainers += extra;
    }
    if (retainers > SIZE_MAX)
        return reject("RHI-METAL-ORDINARY-RETENTION-CAPACITY");
    command.retainedResourcePayloads_.reserve(static_cast<std::size_t>(retainers));
    command.retainedResourcePayloadIdentities_.reserve(static_cast<std::size_t>(retainers));
    if (const auto* error = owner->validate())
        return reject(error);
    return owner;
}

bool MetalRhiNativeCommandPayload::prepareOriginalOrdinary(
    std::shared_ptr<const rhi::OriginalCommandStreamProgram> program,
    const rhi::GpuDevice& device,
    const rhi::OriginalRenderPassPreparationRequest& request,
    core::UniquePtr<core::MutationJournal>& journal,
    core::pmr::String& diagnostic)
{
    if (!journal || !journal->empty() || originalCommandCustody_ || originalOrdinary_
        || usesRecordingRetention_ || nativeRenderEncoder_ || nativeComputeEncoder_
        || nativeBlitEncoder_ || stats_->nativeCommandBufferFinished || abandoned_
        || recordingPoisoned_ || readbackWasSubmitted()) {
        diagnostic = "RHI-METAL-ORDINARY-JOURNAL-ADOPTION";
        return false;
    }
    if (const auto* id = MetalOriginalOrdinaryCommand::inspect(program, device, request)) {
        diagnostic = id;
        return false;
    }
    const auto acceptanceMapping =
        rhi::semantics::OriginalQueueAcceptanceContract::lowering(device.backend(), 1u);
    if (!acceptanceMapping.backendPrivateMappingImplemented || acceptanceMapping.diagnostic) {
        diagnostic = acceptanceMapping.diagnostic;
        return false;
    }
    try {
        core::CpuMemoryQuotaScope scope(program->originalMetadataQuota());
        auto owner =
            MetalOriginalOrdinaryCommand::prepare(program, device, request, *this, diagnostic);
        if (!owner)
            return false;
        auto& acceptance = submissionRetention_->queueAcceptance;
        acceptance.self_ =
            std::shared_ptr<const MetalOriginalQueueAcceptance>(submissionRetention_, &acceptance);
        acceptance.device_ = device.originalDeviceLifetimeTicket();
        acceptance.queue_ = nativeQueue_;
        acceptance.registry_ = resourceRegistry_;
        acceptance.program_ = program;
        acceptance.completion_ = &submissionRetention_->defaultCompletion;
        const auto domain = metalRhiOriginalSubmissionDomain(device);
        if (domain) {
            if (core::SharedOwnerProjection::overlapsOwner(
                    program->originalMetadataQuota(), domain->storage_->originalMetadataQuota())) {
                diagnostic = "RHI-SUBMISSION-COMPLETION-QUOTA-DOMAIN";
                return false;
            }
            const char* id = domain->poll();
            if (id) {
                diagnostic = id;
                return false;
            }
            const auto ticket = domain->storage_->prepareValidatorCommand(
                submissionRetention_,
                &SubmissionRetention::resolveOrdinary,
                [](void*) noexcept {},
                nullptr,
                0u,
                id);
            if (id) {
                diagnostic = id;
                return false;
            }
            submissionDomain_ = domain;
            submissionTicket_ = ticket;
            submissionRetention_->defaultCompletion.domain_ = domain;
        }
        owner->journal_ = std::move(journal);
        originalOrdinary_ = owner;
        originalCommandCustody_ = std::move(owner);
        diagnostic.clear();
        return true;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        diagnostic = "RHI-METAL-ORDINARY-COLD-ALLOCATION";
        return false;
    }
}

namespace {
bool prepareNativeRenderPass(const rhi::RenderPassDesc& desc,
    const std::shared_ptr<MetalRhiResourceRegistry>& registry,
    const std::shared_ptr<void>& device,
    MetalRhiNativeRenderPassPreparation& result,
    core::pmr::String& diagnostic)
{
    return prepareAuthorizedNativeRenderPass(desc, registry, device, result, diagnostic, [&] {
        return metalRhiRenderPassWriteDiagnostic(registry, desc);
    });
}
} // namespace

const char* MetalRhiClosedConsumerNativePreparation::validate() const noexcept
{
    return validateStorage(false, false);
}

const char* MetalRhiClosedConsumerNativePreparation::validateRecorded(bool finished) const noexcept
{
    return validateStorage(true, finished);
}

const char* MetalRhiClosedConsumerNativePreparation::validateStorage(
    bool recorded, bool finished) const noexcept
{
    if (!reservation_ || !reservation_->sharedOwnerMatches(reservation_))
        return "RHI-METAL-CLOSED-CONSUMER-NATIVE-ORIGINAL-OWNER";
    const auto* reservationId =
        recorded ? reservation_->validateRecorded(finished) : reservation_->validate();
    if (reservationId)
        return reservationId;
    Contract::Facts checked;
    if (const auto* id = Contract::verify(input_, budget_.values, checked))
        return id;
    if (checked.targetBytes != facts_.targetBytes || !(checked.original == facts_.original)
        || !budget_.maxChildOwners || owners_.size() > budget_.maxChildOwners
        || !owners_.matches([&](auto&& visit) {
               projectOwners(visit);
           }))
        return "RHI-METAL-CLOSED-CONSUMER-NATIVE-PROJECTION";
    if (!target_ || !resource_.texture || resource_.textureWrapper.get() != target_.get()
        || !native_.descriptor || !states_ || states_->empty() == recorded)
        return "RHI-METAL-CLOSED-CONSUMER-NATIVE-COLD-STATE";
    for (const auto& slot : slots_)
        if (!slot || slot->get())
            return "RHI-METAL-CLOSED-CONSUMER-NATIVE-ENCODER-SLOT";
    const auto& issuance = reservation_->preparation()->issuance();
    const auto& cold = issuance->cold();
    const auto& registry = cold->producer()->packet()->pipeline()->source()->index()->registry();
    const auto& pipeline = *cold->pipeline()->native();
    if (const auto* id =
            validateMetalRhiNativeRenderPassResources(native_, registry, pipeline.nativeDevice))
        return id;
    const auto current = registry->find(target_->id());
    if (!current || !current->texture
        || !core::SharedOwnerProjection::sameOwner(
            native_.attachments[0u].resource.texture, resource_.texture)
        || !core::SharedOwnerProjection::sameOwner(
            current->texture, metalRhiNativePayloadFor(*target_))
        || !targetObservation_.matches(current->texture->resource)
        || !core::SharedOwnerProjection::sameOwner(
            pipeline.nativeDevice, current->texture->resource.nativeDevice)
        || current->texture->aliasHeap || current->gpuWritesSealed
        || current->gpuTransferWritesSealed || current->texture->immutableRevision())
        return "RHI-METAL-CLOSED-CONSUMER-NATIVE-TARGET-IDENTITY";
    const auto& desc = target_->desc();
    const auto expected = Contract::textureDescriptor(input_);
    if (desc.extent.width != expected.extent.width || desc.extent.height != expected.extent.height
        || desc.extent.depth != expected.extent.depth || desc.format != expected.format
        || desc.usage != expected.usage || desc.memory != expected.memory
        || desc.initialState != expected.initialState
        || desc.mipLevelCount != expected.mipLevelCount
        || desc.arrayLayerCount != expected.arrayLayerCount
        || desc.sampleCount != expected.sampleCount
        || desc.cubeCompatible != expected.cubeCompatible
        || target_->state() != expected.initialState
        || target_->allocation().estimatedSizeBytes != ownedBytes_
        || ownedBytes_ < facts_.targetBytes || ownedBytes_ > budget_.values.maxTargetBytes)
        return "RHI-METAL-CLOSED-CONSUMER-NATIVE-TARGET-DESCRIPTOR";
    if (pipeline.colorFormats.size() != 1u || pipeline.colorFormats[0u] != expected.format
        || pipeline.sampleCount != expected.sampleCount
        || pipeline.depthStencilFormat != rhi::TextureFormat::Unknown
        || pipeline.usesDepthStencilAttachment || pipeline.hasViewport || pipeline.hasScissor)
        return "RHI-METAL-CLOSED-CONSUMER-NATIVE-PIPELINE-TARGET";
    const auto texture =
        reinterpret_cast<id<MTLTexture>>(current->texture->resource.nativeObject.get());
    const auto& payload = current->texture->resource;
    if (payload.storageMode != MetalRhiNativeStorageMode::Private)
        return "RHI-METAL-CLOSED-CONSUMER-NATIVE-TARGET-PAYLOAD";
    if (const auto* id = validateMetalRhiTrackedNativeTextureObject(payload, expected))
        return id;
    const auto descriptor = reinterpret_cast<MTLRenderPassDescriptor*>(native_.descriptor.get());
    const auto attachment = descriptor.colorAttachments[0u];
    if (native_.attachmentCount != 1u || native_.attachments[0u].id != target_->id()
        || native_.width != input_.extent.width || native_.height != input_.extent.height
        || native_.colorCount != 1u || native_.sampleCount != expected.sampleCount
        || native_.subpassCount != 1u || native_.layered
        || native_.depthFormat != rhi::TextureFormat::Unknown
        || native_.colorFormats[0u] != expected.format || attachment.texture != texture
        || attachment.level || attachment.slice || attachment.depthPlane
        || attachment.resolveTexture || attachment.loadAction != MTLLoadActionClear
        || attachment.storeAction != MTLStoreActionStore
        || attachment.clearColor.red != input_.clear.r
        || attachment.clearColor.green != input_.clear.g
        || attachment.clearColor.blue != input_.clear.b
        || attachment.clearColor.alpha != input_.clear.a || descriptor.depthAttachment.texture
        || descriptor.stencilAttachment.texture || descriptor.renderTargetArrayLength)
        return "RHI-METAL-CLOSED-CONSUMER-NATIVE-RENDER-DESCRIPTOR";
    for (std::size_t i = 1u; i < kMetalRenderPassColorSlots; ++i)
        if (descriptor.colorAttachments[i].texture)
            return "RHI-METAL-CLOSED-CONSUMER-NATIVE-RENDER-DESCRIPTOR";
    return nullptr;
}

std::shared_ptr<const MetalRhiClosedConsumerNativePreparation>
MetalRhiClosedConsumerNativePreparation::prepare(std::shared_ptr<const Reservation> reservation,
    rhi::GpuDevice& device,
    Contract::Input input,
    Budget budget,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](const char* id) -> std::shared_ptr<const MetalRhiClosedConsumerNativePreparation> {
        diagnostic = id;
        return {};
    };
    const auto decision = Contract::lowering(device.backend());
    if (!decision.prototypeColdIntent)
        return fail(decision.diagnosticId);
    if (!@available(macOS 10.15, iOS 13.0, *))
        return fail("RHI-METAL-TRACKED-TEXTURE-NATIVE-API-UNAVAILABLE");
    Contract::Facts facts;
    if (const auto* id = Contract::verify(input, budget.values, facts))
        return fail(id);
    if (!reservation || !reservation->sharedOwnerMatches(reservation))
        return fail("RHI-METAL-CLOSED-CONSUMER-NATIVE-ORIGINAL-OWNER");
    if (const auto* id = reservation->validate())
        return fail(id);
    const auto& cold = reservation->preparation()->issuance()->cold();
    const auto& source = cold->producer()->packet()->pipeline()->source();
    const auto& registry = source->index()->registry();
    if (!core::SharedOwnerProjection::sameOwner(registry, metalRhiIssuingResourceRegistry(device)))
        return fail("RHI-METAL-CLOSED-CONSUMER-NATIVE-ISSUER");
    const auto requiredOwners = 1u + reservation->childOwnerCount() + 1u
        + MetalRhiResourceOwnerProjection::independentResourceOwnerCount + 1u + 1u
        + encoderSlotCount; // reservation graph, target graph, descriptor, journal, slots
    if (requiredOwners > budget.maxChildOwners)
        return fail("RHI-METAL-CLOSED-CONSUMER-NATIVE-CHILD-OWNER-BUDGET");
    const auto expected = Contract::textureDescriptor(input);
    const auto bytes = estimateMetalRhiTextureBytes(expected);
    if (!bytes || bytes < facts.targetBytes || bytes > budget.values.maxTargetBytes
        || budget.values.maxTargetBytes > device.memoryStats().budgetCapacityBytes)
        return fail("RHI-METAL-CLOSED-CONSUMER-NATIVE-TARGET-BUDGET");
    try {
        core::CpuMemoryQuotaScope scope(source->quota());
        auto target = core::shareUnique(device.createTexture(expected));
        if (!target)
            return fail("RHI-METAL-CLOSED-CONSUMER-NATIVE-TARGET-CREATION-FAILED");
        const auto resource = registry->find(target->id());
        if (!resource || !resource->texture)
            return fail("RHI-METAL-CLOSED-CONSUMER-NATIVE-TARGET-IDENTITY");
        rhi::RenderPassDesc pass;
        pass.colorAttachments.push_back(Contract::attachmentDescriptor(input, target->id()));
        MetalRhiNativeRenderPassPreparation native;
        if (!prepareNativeRenderPass(
                pass, registry, cold->pipeline()->native()->nativeDevice, native, diagnostic))
            return {};
        auto states = core::makeShared<core::MutationJournal>();
        Slots slots;
        for (auto& slot : slots)
            slot = core::makeShared<core::ExternalObjectSlot>([](void* object) noexcept {
                [reinterpret_cast<id>(object) release];
            });
        auto root = core::makeShared<MetalRhiClosedConsumerNativePreparation>(ConstructionKey{},
            std::move(reservation),
            std::move(target),
            *resource,
            std::move(native),
            std::move(states),
            std::move(slots),
            input,
            facts,
            budget);
        if (root->childOwnerCount() != requiredOwners)
            return fail("RHI-METAL-CLOSED-CONSUMER-NATIVE-PROJECTION");
        if (const auto* id = root->validate())
            return fail(id);
        return root;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-CLOSED-CONSUMER-NATIVE-ALLOCATION-FAILED");
    }
}

std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass>
MetalRhiOrdinaryPreparedRenderPass::prepare(const rhi::RenderPassDesc& desc,
    std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    const MetalRhiOrdinaryBindingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (const auto* id = budget.validate(quota)) {
        diagnostic = id;
        return {};
    }
    if (!provider || provider->quota() != quota) {
        diagnostic = "RHI-METAL-ORDINARY-PASS-PROVIDER-QUOTA-MISMATCH";
        return {};
    }
    if (desc.layeredRendering || desc.subpasses.size() > 1u || !desc.subpassDependencies.empty()) {
        diagnostic = "RHI-METAL-ORDINARY-PASS-DOMAIN-UNIMPLEMENTED";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        MetalRhiNativeRenderPassPreparation native;
        if (!prepareNativeRenderPass(
                desc, registry, provider->native()->nativeDevice, native, diagnostic))
            return {};
        auto result = core::makeShared<MetalRhiOrdinaryPreparedRenderPass>(ConstructionKey{},
            std::move(provider),
            std::move(native),
            std::move(registry),
            std::move(quota));
        const auto valid = result->validateObservation();
        if (!valid.success) {
            diagnostic = valid.diagnostics;
            return {};
        }
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED";
        return {};
    }
}

bool MetalRhiNativeCommandPayload::recordRenderPassBegin(
    const rhi::RenderPassDesc& desc, std::pmr::string& diagnostics)
{
    if (const auto* id = metalRhiRenderPassWriteDiagnostic(resourceRegistry_, desc)) {
        diagnostics = id;
        return false;
    }
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    if (!nativeCommandBuffer_ || !resourceRegistry_ || nativeRenderEncoder_ || nativeComputeEncoder_
        || nativeBlitEncoder_) {
        diagnostics = "RHI-METAL-RENDER-PASS-NATIVE-PAYLOAD-MISSING";
        return false;
    }
    if (pendingGraphicsBinding_) {
        if (!validatePreparedGraphicsStorage(pendingGraphicsBinding_, diagnostics))
            return false;
        const auto& pipeline = *pendingGraphicsBinding_->pipeline;
        bool compatible = !desc.layeredRendering && desc.subpasses.size() <= 1u
            && desc.colorAttachments.size() == pipeline.colorFormats.size()
            && (desc.hasDepthStencilAttachment ? desc.depthStencilAttachment.format
                                               : rhi::TextureFormat::Unknown)
                == pipeline.depthStencilFormat;
        for (std::size_t i = 0u; compatible && i < desc.colorAttachments.size(); ++i) {
            const auto resource = resourceRegistry_->find(desc.colorAttachments[i].target);
            compatible = desc.colorAttachments[i].format == pipeline.colorFormats[i] && resource
                && resource->textureSampleCount == pipeline.sampleCount;
        }
        if (compatible && desc.hasDepthStencilAttachment) {
            const auto resource = resourceRegistry_->find(desc.depthStencilAttachment.target);
            compatible = resource && resource->textureSampleCount == pipeline.sampleCount;
        }
        if (!compatible) {
            diagnostics = "RHI-METAL-GRAPHICS-SCOPE-PASS-MISMATCH";
            return false;
        }
    }
    MetalRhiNativeRenderPassPreparation prepared;
    if (!prepareNativeRenderPass(desc, resourceRegistry_, nativeDevice_, prepared, diagnostics))
        return false;
    for (std::size_t i = 0; i < prepared.attachmentCount; ++i)
        retainRegisteredResourcePayloads(retainedResourcePayloads_,
            retainedResourcePayloadIdentities_,
            prepared.attachments[i].resource);
    return beginPreparedNativeRenderPass(prepared, {}, diagnostics);
}

bool MetalRhiNativeCommandPayload::beginPreparedNativeRenderPass(
    const MetalRhiNativeRenderPassPreparation& prepared,
    const std::shared_ptr<core::ExternalObjectSlot>& slot,
    core::pmr::String& diagnostics)
{
    if (!nativeCommandBuffer_ || nativeRenderEncoder_ || nativeComputeEncoder_ || nativeBlitEncoder_
        || !prepared.descriptor || (slot && slot->get())) {
        diagnostics = "RHI-METAL-RENDER-PASS-ENCODER-BOUNDARY";
        return false;
    }
    id<MTLCommandBuffer> commandBuffer =
        reinterpret_cast<id<MTLCommandBuffer>>(nativeCommandBuffer_.get());
    if (!commandBuffer) {
        diagnostics = "RHI-METAL-RENDER-PASS-COMMAND-BUFFER-MISSING";
        return false;
    }
    auto* renderPass = reinterpret_cast<MTLRenderPassDescriptor*>(prepared.descriptor.get());
    @autoreleasepool {
        id<MTLRenderCommandEncoder> encoder =
            [commandBuffer renderCommandEncoderWithDescriptor:renderPass];
        if (encoder == nil) {
            diagnostics = "RHI-METAL-RENDER-PASS-ENCODER-CREATE-FAILED";
            return false;
        }
        // The native factory returns an autoreleased encoder. Convert it to
        // backend-owned lifetime before this local pool drains so end/poison/
        // abandon releases it immediately instead of pinning the command
        // buffer until an outer application autorelease pool drains.
        if (slot) {
            [encoder retain];
            if (!slot->adopt(reinterpret_cast<void*>(encoder))) {
                [encoder endEncoding];
                [encoder release];
                diagnostics = "RHI-METAL-ROOT-GRAPHICS-ENCODER-SLOT";
                return false;
            }
            nativeRenderEncoder_ = std::shared_ptr<void>(slot, slot->get());
        } else
            nativeRenderEncoder_ = retainBorrowedNativeObject(encoder);
    }
    vertexStreams_ = {};
    ordinaryVertexBindings_ = {};
    indexBuffer_.reset();
    indexFormat_ = rhi::IndexFormat::Uint32;
    indexBufferOffset_ = 0u;
    tessellationFactorBuffer_.reset();
    tessellationFactorBufferOffset_ = 0u;
    tessellationFactorInstanceStrideBytes_ = 0u;
    renderSubpassCount_ = prepared.subpassCount;
    renderSubpassIndex_ = 0u;
    renderPassWidth_ = prepared.width;
    renderPassHeight_ = prepared.height;
    renderPassLayered_ = prepared.layered;
    stats_->nativeRenderEncoderCreated = true;
    stats_->nativeDepthStencilAttachmentRecorded =
        prepared.depthFormat != rhi::TextureFormat::Unknown;
    if (prepared.colorCount != 0u) {
        stats_->nativeColorClearValueRecorded = true;
        stats_->nativeColorClearValue = prepared.firstClearColor;
    }
    if (pendingGraphicsBinding_) {
        activeGraphicsBinding_ = std::move(pendingGraphicsBinding_);
        activeGraphicsFaultDomain_ = std::move(pendingGraphicsFaultDomain_);
        graphicsScopeBound_ = false;
    }
    return true;
}

bool MetalRhiNativeCommandPayload::recordGraphicsPipelineBind(
    const std::shared_ptr<MetalRhiNativePipelinePayload>& pipeline, core::pmr::String& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    if (!validateGraphicsPipelineBind(pipeline, diagnostics))
        return false;
    return recordGraphicsPipelineBindUnchecked(pipeline, diagnostics);
}

bool MetalRhiNativeCommandPayload::validateGraphicsPipelineBind(
    const std::shared_ptr<MetalRhiNativePipelinePayload>& pipeline,
    core::pmr::String& diagnostics,
    bool requireEncoder) const
{
    if (activeGraphicsBinding_ && activeGraphicsBinding_->pipeline != pipeline) {
        diagnostics = "RHI-METAL-GRAPHICS-SCOPE-PIPELINE-MISMATCH";
        return false;
    }
    id<MTLRenderCommandEncoder> encoder =
        reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    id<MTLRenderPipelineState> state = pipeline && pipeline->nativeRuntimeBacked
        ? reinterpret_cast<id<MTLRenderPipelineState>>(pipeline->pipelineState.get())
        : nil;
    if ((requireEncoder && encoder == nil) || state == nil || !pipeline
        || !commandDeviceMatches(nativeDevice_, pipeline->nativeDevice)) {
        diagnostics = "RHI-METAL-RENDER-PIPELINE-EXECUTABLE-PAYLOAD-MISSING";
        return false;
    }
    if ((pipeline->depthTestEnabled || pipeline->depthWriteEnabled || pipeline->stencilEnabled)
        && (!pipeline->depthStencilStateRecorded || pipeline->depthStencilState == nullptr)) {
        diagnostics = "RHI-METAL-DEPTH-STENCIL-STATE-PAYLOAD-MISSING";
        return false;
    }
    if (renderPassLayered_ && !pipeline->writesRenderTargetArrayIndex) {
        diagnostics = "RHI-METAL-LAYERED-RENDER-PASS-ARRAY-INDEX-ABI-MISSING";
        return false;
    }
    id<MTLBuffer> tessellationFactor = nil;
    if (tessellationFactorBuffer_
        && pipeline->primitiveTopology == rhi::PrimitiveTopology::PatchList) {
        if (pipeline->metalTessellationFactorBufferIndex >= 31u) {
            diagnostics = "RHI-METAL-TESSELLATION-FACTOR-BUFFER-SLOT-INVALID";
            return false;
        }
        tessellationFactor = nativeBuffer(tessellationFactorBuffer_);
        if (tessellationFactor == nil || !fitsNSUInteger(tessellationFactorBufferOffset_)) {
            diagnostics = "RHI-METAL-TESSELLATION-FACTOR-BUFFER-EXECUTABLE-PAYLOAD-MISSING";
            return false;
        }
        if (!pipeline->metalTessellationAbiRecorded
            || pipeline->metalTessellationFactorBytesPerPatch == 0u
            || tessellationFactorInstanceStrideBytes_
                != pipeline->metalTessellationFactorInstanceStrideBytes
            || tessellationFactorBufferOffset_ > tessellationFactor.length
            || tessellationFactor.length - tessellationFactorBufferOffset_
                < pipeline->metalTessellationFactorBytesPerPatch) {
            diagnostics = "RHI-METAL-TESSELLATION-FACTOR-BUFFER-ABI-MISMATCH";
            return false;
        }
    } else if (tessellationFactorBuffer_
        && pipeline->primitiveTopology != rhi::PrimitiveTopology::PatchList) {
        diagnostics = "RHI-METAL-TESSELLATION-FACTOR-BUFFER-REQUIRES-PATCHLIST";
        return false;
    }
    if (!rhi::semantics::sampleMaskHasFullCoverage(pipeline->sampleCount, pipeline->sampleMask)) {
        diagnostics = "RHI-METAL-SAMPLE-MASK-SHADER-LOWERING-REQUIRED";
        return false;
    }
    if (!pipeline->vertexStreams.success) {
        diagnostics = "RHI-METAL-VERTEX-STREAM-LOWERING-MISSING";
        return false;
    }
    if (pipeline->hasScissor && (pipeline->scissor.x < 0 || pipeline->scissor.y < 0)) {
        diagnostics = "RHI-METAL-SET-SCISSOR-ARGUMENTS-INVALID";
        return false;
    }
    return true;
}

bool MetalRhiNativeCommandPayload::recordGraphicsPipelineBindUnchecked(
    const std::shared_ptr<MetalRhiNativePipelinePayload>& pipeline, core::pmr::String& diagnostics)
{
    const auto encoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    const auto state = reinterpret_cast<id<MTLRenderPipelineState>>(pipeline->pipelineState.get());
    const auto tessellationFactor = nativeBuffer(tessellationFactorBuffer_);
    [encoder setRenderPipelineState:state];
    // StaticViewportScissor/1: static L2 state is encoder state on Metal.
    // Restore it on every bind, including after a dynamic pipeline.
    if (pipeline->hasViewport && !recordSetViewport(pipeline->viewport, diagnostics))
        return false;
    if (pipeline->hasScissor && !recordSetScissor(pipeline->scissor, diagnostics))
        return false;
    if (pipeline->depthStencilState) {
        [encoder setDepthStencilState:reinterpret_cast<id<MTLDepthStencilState>>(
                                          pipeline->depthStencilState.get())];
    }
    [encoder setCullMode:nativeCullMode(pipeline->cullMode)];
    [encoder setFrontFacingWinding:nativeWinding(pipeline->frontFace)];
    stats_->nativeGraphicsPipelineBound = true;
    stats_->nativeDepthStencilStateBound =
        pipeline->depthStencilStateRecorded && pipeline->depthStencilState != nullptr;
    stats_->nativeDepthBiasSet = pipeline->depthBiasEnabled;
    [encoder setDepthBias:pipeline->depthBiasEnabled ? pipeline->depthBiasConstantFactor : 0.0f
               slopeScale:pipeline->depthBiasEnabled ? pipeline->depthBiasSlopeFactor : 0.0f
                    clamp:pipeline->depthBiasEnabled ? pipeline->depthBiasClamp : 0.0f];
    renderPipeline_ = pipeline;
    for (std::uint32_t slot = 0; slot < vertexStreams_.size(); ++slot) {
        const auto physicalSlot = pipeline->vertexStreams.slots[slot];
        const auto& binding = vertexStreams_[slot];
        if (physicalSlot != UINT32_MAX && binding.buffer) {
            [encoder setVertexBuffer:nativeBuffer(binding.buffer)
                              offset:static_cast<NSUInteger>(binding.offsetBytes)
                             atIndex:physicalSlot];
        }
    }
    if (tessellationFactor != nil) {
        [encoder setVertexBuffer:tessellationFactor
                          offset:static_cast<NSUInteger>(tessellationFactorBufferOffset_)
                         atIndex:pipeline->metalTessellationFactorBufferIndex];
    }
    return true;
}

bool MetalRhiNativeCommandPayload::recordSetViewport(
    const rhi::ViewportState& viewport, std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(static_cast<bool>(ordinaryGraphicsPass_), diagnostics))
        return false;
    if (ordinaryWriteStates_) {
        diagnostics = "RHI-METAL-ORDINARY-WRITE-DYNAMIC-STATE-DOMAIN";
        return false;
    }
    auto* encoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    if (encoder == nil) {
        diagnostics = "RHI-METAL-SET-VIEWPORT-ENCODER-MISSING";
        return false;
    }
    const MTLViewport nativeViewport{viewport.x,
        viewport.y,
        viewport.width,
        viewport.height,
        viewport.minDepth,
        viewport.maxDepth};
    [encoder setViewport:nativeViewport];
    stats_->nativeViewportSet = true;
    ++stats_->nativeViewportSetCount;
    return true;
}

bool MetalRhiNativeCommandPayload::recordSetScissor(
    const rhi::ScissorState& scissor, std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(static_cast<bool>(ordinaryGraphicsPass_), diagnostics))
        return false;
    if (ordinaryWriteStates_) {
        diagnostics = "RHI-METAL-ORDINARY-WRITE-DYNAMIC-STATE-DOMAIN";
        return false;
    }
    auto* encoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    if (encoder == nil) {
        diagnostics = "RHI-METAL-SET-SCISSOR-ENCODER-MISSING";
        return false;
    }
    if (scissor.x < 0 || scissor.y < 0) {
        diagnostics = "RHI-METAL-SET-SCISSOR-ARGUMENTS-INVALID";
        return false;
    }
    const MTLScissorRect nativeScissor{static_cast<NSUInteger>(scissor.x),
        static_cast<NSUInteger>(scissor.y),
        static_cast<NSUInteger>(scissor.width),
        static_cast<NSUInteger>(scissor.height)};
    [encoder setScissorRect:nativeScissor];
    stats_->nativeScissorSet = true;
    ++stats_->nativeScissorSetCount;
    return true;
}

bool MetalRhiNativeCommandPayload::recordVertexBufferBind(std::uint32_t slot,
    const std::shared_ptr<MetalRhiNativeBufferPayload>& buffer,
    std::uint64_t offsetBytes,
    std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    auto* encoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    id<MTLBuffer> native = nativeBuffer(buffer);
    if (encoder == nil || native == nil || !buffer
        || !commandDeviceMatches(nativeDevice_, buffer->resource.nativeDevice) || slot >= 31u
        || !fitsNSUInteger(offsetBytes) || offsetBytes >= native.length
        || (offsetBytes & 3u) != 0u) {
        diagnostics = "RHI-METAL-VERTEX-BUFFER-EXECUTABLE-PAYLOAD-MISSING";
        return false;
    }
    vertexStreams_[slot] = {buffer, offsetBytes};
    if (renderPipeline_) {
        const auto physicalSlot = renderPipeline_->vertexStreams.slots[slot];
        if (physicalSlot != UINT32_MAX)
            [encoder setVertexBuffer:native
                              offset:static_cast<NSUInteger>(offsetBytes)
                             atIndex:physicalSlot];
    }
    retainResourcePayload(retainedResourcePayloads_, retainedResourcePayloadIdentities_, buffer);
    return true;
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedVertexBinding(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedVertexBinding>& binding,
    core::pmr::String& diagnostic)
{
    if (!retentionRecordingAllows(true, diagnostic, true))
        return false;
    if (ordinaryIndirectRecording_)
        if (const auto* id = ordinaryIndirectRecording_->canStateCommand()) {
            diagnostic = id;
            return false;
        }
    if (ordinaryWriteStates_) {
        diagnostic = "RHI-METAL-ORDINARY-WRITE-VERTEX-DOMAIN";
        return false;
    }
    if (!recordingRetention_ || !ordinaryGraphicsPass_ || !ordinaryGraphicsContract_
        || !nativeRenderEncoder_ || !binding
        || binding->provider() != ordinaryGraphicsContract_->provider()
        || binding->registry() != resourceRegistry_
        || binding->quota() != recordingRetention_->quota()) {
        diagnostic = "RHI-METAL-ORDINARY-VERTEX-RECORDING-MISMATCH";
        return false;
    }
    const auto pass = ordinaryGraphicsPass_->validateObservation();
    if (!pass.success) {
        diagnostic = pass.diagnostics;
        return false;
    }
    if (const auto* id = binding->validate()) {
        diagnostic = id;
        return false;
    }
    const auto native = nativeBuffer(binding->buffer());
    if (!native || !commandDeviceMatches(nativeDevice_, binding->buffer()->resource.nativeDevice)
        || !fitsNSUInteger(binding->offset()) || native.length != binding->bufferBytes()
        || binding->provider()->vertexInput().validateRange(
            binding->slot(), native.length, binding->offset(), 1u, 0u, 1u, 0u)) {
        diagnostic = "RHI-METAL-ORDINARY-VERTEX-NATIVE-EXTENT-MISMATCH";
        return false;
    }
    const auto ownerAt = [&](std::size_t i) noexcept -> std::shared_ptr<const void> {
        return !i ? std::shared_ptr<const void>(binding) : binding->childOwnerAt(i - 1u);
    };
    if (const auto* id = recordingRetention_->retainResourceProjection(
            binding->childOwnerCount() + 1u, ownerAt)) {
        diagnostic = id;
        return false;
    }
    const auto encoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    [encoder setVertexBuffer:native
                      offset:static_cast<NSUInteger>(binding->offset())
                     atIndex:binding->physicalSlot()];
    ordinaryVertexBindings_[binding->slot()] = binding;
    vertexStreams_[binding->slot()] = {binding->buffer(), binding->offset()};
    if (ordinaryIndirectRecording_) {
        ordinaryIndirectRecording_->stateCommand();
        publishOrdinaryIndirectCounters();
    }
    return true;
}

bool MetalRhiNativeCommandPayload::validateVertexStreams(std::pmr::string& diagnostics) const
{
    if (!renderPipeline_ || !renderPipeline_->vertexStreams.success) {
        diagnostics = "RHI-METAL-VERTEX-STREAM-LOWERING-MISSING";
        return false;
    }
    for (std::uint32_t slot = 0; slot < vertexStreams_.size(); ++slot) {
        if (renderPipeline_->vertexStreams.slots[slot] != UINT32_MAX
            && !vertexStreams_[slot].buffer) {
            diagnostics = "RHI-METAL-VERTEX-STREAM-BINDING-MISSING";
            return false;
        }
    }
    return true;
}

bool MetalRhiNativeCommandPayload::recordIndexBufferBind(
    const std::shared_ptr<MetalRhiNativeBufferPayload>& buffer,
    rhi::IndexFormat format,
    std::uint64_t offsetBytes,
    std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    id<MTLBuffer> native = nativeBuffer(buffer);
    const auto alignment = format == rhi::IndexFormat::Uint16 ? 2u : 4u;
    if (!nativeRenderEncoder_ || native == nil || !buffer
        || !commandDeviceMatches(nativeDevice_, buffer->resource.nativeDevice)
        || !fitsNSUInteger(offsetBytes) || offsetBytes >= native.length
        || (offsetBytes % alignment) != 0u) {
        diagnostics = "RHI-METAL-INDEX-BUFFER-EXECUTABLE-PAYLOAD-MISSING";
        return false;
    }
    indexBuffer_ = buffer;
    indexFormat_ = format;
    indexBufferOffset_ = offsetBytes;
    retainResourcePayload(retainedResourcePayloads_, retainedResourcePayloadIdentities_, buffer);
    return true;
}

bool MetalRhiNativeCommandPayload::recordTessellationFactorBufferBind(
    const std::shared_ptr<MetalRhiNativeBufferPayload>& buffer,
    std::uint64_t offsetBytes,
    std::uint32_t instanceStrideBytes,
    std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    if (!@available(macOS 10.13, iOS 11.0, *)) {
        diagnostics = "RHI-METAL-PATCH-LIST-API-UNAVAILABLE";
        return false;
    }
    auto* encoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    id<MTLBuffer> native = nativeBuffer(buffer);
    if (encoder == nil || native == nil || !buffer
        || !commandDeviceMatches(nativeDevice_, buffer->resource.nativeDevice)
        || instanceStrideBytes == 0u || (offsetBytes & 3u) != 0u || (instanceStrideBytes & 3u) != 0u
        || !fitsNSUInteger(offsetBytes) || offsetBytes > native.length
        || native.length - offsetBytes < sizeof(std::uint32_t)) {
        diagnostics = "RHI-METAL-TESSELLATION-FACTOR-BUFFER-EXECUTABLE-PAYLOAD-MISSING";
        return false;
    }
    if (renderPipeline_
        && renderPipeline_->primitiveTopology == rhi::PrimitiveTopology::PatchList) {
        if (!renderPipeline_->metalTessellationAbiRecorded
            || renderPipeline_->metalTessellationFactorBytesPerPatch == 0u
            || instanceStrideBytes != renderPipeline_->metalTessellationFactorInstanceStrideBytes
            || native.length - offsetBytes
                < renderPipeline_->metalTessellationFactorBytesPerPatch) {
            diagnostics = "RHI-METAL-TESSELLATION-FACTOR-BUFFER-ABI-MISMATCH";
            return false;
        }
    } else if (renderPipeline_) {
        diagnostics = "RHI-METAL-TESSELLATION-FACTOR-BUFFER-REQUIRES-PATCHLIST";
        return false;
    }
    [encoder setTessellationFactorBuffer:native
                                  offset:static_cast<NSUInteger>(offsetBytes)
                          instanceStride:instanceStrideBytes];
    if (renderPipeline_ && renderPipeline_->primitiveTopology == rhi::PrimitiveTopology::PatchList
        && renderPipeline_->metalTessellationFactorBufferIndex < 31u) {
        [encoder setVertexBuffer:native
                          offset:static_cast<NSUInteger>(offsetBytes)
                         atIndex:renderPipeline_->metalTessellationFactorBufferIndex];
    }
    tessellationFactorBuffer_ = buffer;
    tessellationFactorBufferOffset_ = offsetBytes;
    tessellationFactorInstanceStrideBytes_ = instanceStrideBytes;
    retainResourcePayload(retainedResourcePayloads_, retainedResourcePayloadIdentities_, buffer);
    return true;
}

bool MetalRhiNativeCommandPayload::validateTessellationFactorBufferRange(
    std::uint32_t instanceCount, std::pmr::string& diagnostics)
{
    if (!renderPipeline_
        || renderPipeline_->primitiveTopology != rhi::PrimitiveTopology::PatchList) {
        diagnostics = "RHI-METAL-TESSELLATION-FACTOR-BUFFER-REQUIRES-PATCHLIST";
        return false;
    }
    return validTessellationFactorBinding(renderPipeline_,
        tessellationFactorBuffer_,
        tessellationFactorBufferOffset_,
        tessellationFactorInstanceStrideBytes_,
        instanceCount,
        diagnostics);
}

bool MetalRhiNativeCommandPayload::validateOrdinaryGraphicsBinding(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>& binding,
    core::pmr::String& diagnostic,
    const MetalRhiOrdinaryPreparedGraphicsWrite* write) const
{
    if (!binding || !recordingRetention_ || binding->registry() != resourceRegistry_
        || !binding->payload() || !binding->provider()
        || !commandDeviceMatches(nativeDevice_, binding->provider()->native()->nativeDevice)) {
        diagnostic = "RHI-METAL-ORDINARY-GRAPHICS-RECORDING-MISMATCH";
        return false;
    }
    if (binding->quota() != recordingRetention_->quota()) {
        diagnostic = "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
        return false;
    }
    const auto& native = *binding->provider()->native();
    if (write) {
        if (!ordinaryWriteStates_ || write->binding() != binding
            || write->resource().state() != rhi::ResourceState::ShaderWrite) {
            diagnostic = "RHI-METAL-ORDINARY-WRITE-RECORDING-MISMATCH";
            return false;
        }
        const auto writable = write->validateObservation();
        if (!writable.success) {
            diagnostic = writable.diagnostics;
            return false;
        }
    }
    for (const auto& resource : binding->provider()->interface().reflection.descriptorBindings) {
        if (write && resource.type == rhi::DescriptorType::StorageBuffer)
            continue;
        if (!MetalRhiOrdinaryGraphicsResourceDomain::allowsDescriptor(
                resource.type, resource.arrayCount, resource.depthTexture)) {
            diagnostic = "RHI-METAL-ORDINARY-GRAPHICS-RESOURCE-DOMAIN-UNIMPLEMENTED";
            return false;
        }
    }
    if (native.hasViewport || native.hasScissor
        || native.primitiveTopology != rhi::PrimitiveTopology::TriangleList
        || native.sampleCount != 1u) {
        diagnostic = "RHI-METAL-ORDINARY-GRAPHICS-DOMAIN-UNIMPLEMENTED";
        return false;
    }
    const auto observed = binding->validateObservation();
    if (!observed.success) {
        diagnostic = observed.diagnostics;
        return false;
    }
    if (native.graphicsTextureFactsVersion != 1u) {
        diagnostic = "RHI-METAL-ORDINARY-GRAPHICS-TEXTURE-FACTS-UNQUERIED";
        return false;
    }
    const auto& prepared = *binding->payload();
    for (const auto& resource : prepared.descriptors->bindings) {
        if (write && resource.type == rhi::DescriptorType::StorageBuffer)
            continue;
        if (!MetalRhiOrdinaryGraphicsResourceDomain::allowsDescriptor(
                resource.type, resource.arrayCount, resource.depthTexture)
            || (resource.sampler && resource.sampler->comparisonEnabled)) {
            diagnostic = "RHI-METAL-ORDINARY-GRAPHICS-RESOURCE-DOMAIN-UNIMPLEMENTED";
            return false;
        }
        if (resource.type != rhi::DescriptorType::SampledTexture)
            continue;
        for (const auto& layout : prepared.layouts) {
            if (!layout)
                continue;
            MetalRhiPreparedBindingLocation location;
            const auto resolved =
                layout->resolve(resource, prepared.set, native.nativeBindingStages, location);
            if (!resolved.success) {
                diagnostic = resolved.diagnostics;
                return false;
            }
            if (!location.active)
                continue;
            const auto index =
                location.array ? location.argumentIndex + resource.arrayElement : UINT32_MAX;
            const MetalRhiTextureBindingFact* fact = nullptr;
            for (const auto& candidate : native.graphicsTextureFacts) {
                if (candidate.stage != layout->stage || candidate.slot != location.slot
                    || candidate.argumentIndex != index)
                    continue;
                if (fact) {
                    diagnostic = "RHI-METAL-ORDINARY-GRAPHICS-TEXTURE-FACT-AMBIGUOUS";
                    return false;
                }
                fact = &candidate;
            }
            const auto texture = resource.textureView
                ? reinterpret_cast<id<MTLTexture>>(
                      resource.textureView->resource.nativeObject.get())
                : nil;
            if (!fact || !texture || fact->depth
                || rhi::isDepthTextureFormat(resource.textureView->format)
                || fact->scalarDataType != MTLDataTypeFloat
                || fact->textureType != texture.textureType) {
                diagnostic = "RHI-METAL-ORDINARY-GRAPHICS-TEXTURE-ABI-MISMATCH";
                return false;
            }
        }
    }
    if (ordinaryGraphicsPass_) {
        const auto compatible = ordinaryGraphicsPass_->validateProvider(*binding->provider());
        if (!compatible.success) {
            diagnostic = compatible.diagnostics;
            return false;
        }
    }
    return true;
}

bool MetalRhiNativeCommandPayload::validateOrdinaryGraphicsPush(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>& binding,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsPush>& push,
    core::pmr::String& diagnostic) const
{
    const bool changed =
        !ordinaryGraphicsContract_ || ordinaryGraphicsContract_->provider() != binding->provider();
    if ((push || changed) && ordinaryGraphicsPushState_.hasUnconsumedUpdate()) {
        diagnostic = "[PC-E013] RHI-METAL-ORDINARY-GRAPHICS-PUSH-DUPLICATE-UNCONSUMED";
        return false;
    }
    if (!push)
        return true;
    if (push->quota() != recordingRetention_->quota()) {
        diagnostic = "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
        return false;
    }
    if (push->provider() != binding->provider()) {
        diagnostic = "[PC-E020] RHI-METAL-ORDINARY-GRAPHICS-PUSH-BINDING-MISMATCH";
        return false;
    }
    const auto valid = push->validate();
    if (!valid.success) {
        diagnostic = valid.diagnostics;
        return false;
    }
    return true;
}

bool MetalRhiNativeCommandPayload::bindOrdinaryGraphicsUnchecked(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>& binding,
    core::pmr::String& diagnostic,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsPush>& push)
{
    const bool changed =
        !ordinaryGraphicsContract_ || ordinaryGraphicsContract_->provider() != binding->provider();
    if (changed) {
        ordinaryGraphicsDescriptors_.reset();
        ordinaryGraphicsBindings_ = {};
        ordinaryVertexBindings_ = {};
        vertexStreams_ = {};
        ordinaryGraphicsPush_.reset();
        ordinaryGraphicsPushState_.onPassEnd();
    }
    const auto& prepared = *binding->payload();
    if (!recordGraphicsPipelineBindUnchecked(prepared.pipeline, diagnostic)) {
        poisonRecording(diagnostic);
        return false;
    }
    const auto encoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    for (std::size_t i = 0; i < prepared.stages.size(); ++i) {
        const auto& stage = prepared.stages[i];
        if (!stage)
            continue;
        const auto& layout = *prepared.layouts[i];
        MetalRhiNativeFunctionBindingRecording(*stage,
            MetalRhiPreparedBindingLayoutValidator(layout.direct,
                layout.arrays,
                prepared.pipeline->nativeBindingStages,
                stage->stage,
                layout.pushSlot))
            .record(nil, encoder);
    }
    if (push) {
        for (const auto& range : push->ranges()) {
            if (range.stage == rhi::ShaderStage::Vertex)
                [encoder setVertexBytes:push->bytes().data()
                                 length:push->bytes().size()
                                atIndex:range.fact.slot];
            else if (range.stage == rhi::ShaderStage::Fragment)
                [encoder setFragmentBytes:push->bytes().data()
                                   length:push->bytes().size()
                                  atIndex:range.fact.slot];
        }
        ordinaryGraphicsPush_ = push;
        ordinaryGraphicsPushState_.onPush();
        stats_->nativePushConstantSetBytesWritten = true;
        stats_->nativePushConstantUploadBufferWritten = false;
        stats_->nativePushConstantUploadBytes = static_cast<std::uint32_t>(push->bytes().size());
    }
    const auto tracked =
        ordinaryGraphicsDescriptors_.bindSet(prepared.set, *binding->descriptor(), nullptr, 0u);
    if (!tracked.success) {
        diagnostic = tracked.diagnostics;
        poisonRecording(diagnostic);
        return false;
    }
    for (auto& previous : ordinaryGraphicsBindings_) {
        if (!previous || previous->payload()->set == prepared.set) {
            previous = binding;
            break;
        }
    }
    ordinaryGraphicsContract_ = binding;
    stats_->nativeDescriptorArgumentBound = true;
    stats_->nativeArgumentBufferReuseCount += prepared.argumentCount;
    return true;
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedRenderPassBegin(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass>& pass,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>& binding,
    core::pmr::String& diagnostic,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsPush>& push)
{
    return recordOrdinaryPreparedRenderPassBeginAt(pass, binding, diagnostic, push, {});
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedWriteRenderPassBegin(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass>& pass,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite>& write,
    core::pmr::String& diagnostic)
{
    if (!write || !ordinaryWriteStates_) {
        diagnostic = "RHI-METAL-ORDINARY-WRITE-PROTOTYPE-OPT-IN-REQUIRED";
        return false;
    }
    return recordOrdinaryPreparedRenderPassBeginAt(pass, write->binding(), diagnostic, {}, write);
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedReadConsumerBegin(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsReadConsumer>& consumer,
    core::pmr::String& diagnostic)
{
    if (!retentionRecordingAllows(true, diagnostic))
        return false;
    if (!ordinaryWriteStates_ || !maxOrdinaryConsumerDraws_ || !consumer || !ordinaryWrite_
        || consumer->write() != ordinaryWrite_ || ordinaryReadConsumer_) {
        diagnostic = "RHI-METAL-ORDINARY-READ-CONSUMER-RECORDING-MISMATCH";
        return false;
    }
    if (const auto* id = ordinaryWriteSequence_.canConsume(ordinaryWrite_->resource().id())) {
        diagnostic = id;
        return false;
    }
    if (ordinaryWrite_->resource().state() != rhi::ResourceState::ShaderRead
        || !canPrepareMetalRhiRecordedState(ordinaryWrite_->resource(), *ordinaryWriteStates_)) {
        diagnostic = "RHI-METAL-ORDINARY-READ-CONSUMER-STATE-MISMATCH";
        return false;
    }
    const auto valid = consumer->validateObservation();
    if (!valid.success) {
        diagnostic = valid.diagnostics;
        return false;
    }
    return recordOrdinaryPreparedRenderPassBeginAt(
        consumer->pass(), consumer->binding(), diagnostic, {}, {}, consumer);
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedRenderPassBeginAt(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass>& pass,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>& binding,
    core::pmr::String& diagnostic,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsPush>& push,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite>& write,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsReadConsumer>& consumer)
{
    if (!retentionRecordingAllows(true, diagnostic, true))
        return false;
    if (ordinaryIndirectRecording_) {
        if (write || consumer) {
            diagnostic = "RHI-METAL-ORDINARY-INDIRECT-OPERATION-DOMAIN";
            return false;
        }
        if (const auto* id = ordinaryIndirectRecording_->canBegin()) {
            diagnostic = id;
            return false;
        }
    }
    if (ordinaryWriteStates_ && !write && !consumer) {
        diagnostic = "RHI-METAL-ORDINARY-WRITE-CONSUMER-DOMAIN-UNIMPLEMENTED";
        return false;
    }
    if (write
        && ordinaryWriteSequence_.phase() != rhi::GraphicsStorageWriteSequence::Phase::Empty) {
        diagnostic = "RHI-GRAPHICS-WRITE-SEQUENCE-BEGIN";
        return false;
    }
    if (write
        && (!maxOrdinaryConsumerDraws_
                ? (write->consumer().state != rhi::ResourceState::CopySrc
                      || write->consumer().stages != rhi::ShaderStage::None)
                : (write->consumer().state != rhi::ResourceState::ShaderRead
                      || write->consumer().stages != rhi::ShaderStage::Fragment))) {
        diagnostic = "RHI-METAL-ORDINARY-WRITE-TRANSFER-CONSUMER-REQUIRED";
        return false;
    }
    if (!recordingRetention_ || !pass || !binding || pass->provider() != binding->provider()
        || pass->registry() != resourceRegistry_ || !nativeCommandBuffer_ || nativeRenderEncoder_
        || nativeComputeEncoder_ || nativeBlitEncoder_ || pendingGraphicsBinding_
        || activeGraphicsBinding_ || !preparedRenderEncoderSlot_
        || preparedRenderEncoderSlot_->get()) {
        diagnostic = "RHI-METAL-ORDINARY-PASS-RECORDING-MISMATCH";
        return false;
    }
    if (pass->quota() != recordingRetention_->quota()) {
        diagnostic = "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
        return false;
    }
    const auto valid = pass->validateObservation();
    if (!valid.success) {
        diagnostic = valid.diagnostics;
        return false;
    }
    if (!validateOrdinaryGraphicsBinding(binding, diagnostic, write.get())
        || !validateGraphicsPipelineBind(binding->provider()->native(), diagnostic, false)
        || !validateOrdinaryGraphicsPush(binding, push, diagnostic))
        return false;
    if (write && !canPrepareMetalRhiRecordedState(write->resource(), *ordinaryWriteStates_)) {
        diagnostic = "RHI-RESOURCE-STATE-RECORDING-CONFLICT";
        return false;
    }
    const auto a = pass->childOwnerCount(),
               b = write ? write->childOwnerCount()
        : consumer       ? consumer->childOwnerCount()
                         : binding->childOwnerCount();
    const auto p = push ? push->childOwnerCount() + 1u : 0u;
    const std::size_t extra = write ? 1u : 0u;
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    if (p > maximum - 3u - extra || b > maximum - 3u - extra - p
        || a > maximum - 3u - extra - p - b) {
        diagnostic = "RHI-METAL-ORDINARY-PREPARED-OWNER-COUNT-OVERFLOW";
        return false;
    }
    const auto ownerAt = [&](std::size_t i) noexcept -> std::shared_ptr<const void> {
        if (!i)
            return pass;
        if (i <= a)
            return pass->childOwnerAt(i - 1u);
        i -= a + 1u;
        if (!i)
            return write   ? std::shared_ptr<const void>(write)
                : consumer ? std::shared_ptr<const void>(consumer)
                           : std::shared_ptr<const void>(binding);
        if (i <= b)
            return write   ? write->childOwnerAt(i - 1u)
                : consumer ? consumer->childOwnerAt(i - 1u)
                           : binding->childOwnerAt(i - 1u);
        i -= b + 1u;
        if (push && i < p)
            return !i ? std::shared_ptr<const void>(push) : push->childOwnerAt(i - 1u);
        if (write && !i)
            return ordinaryWriteStates_;
        return preparedRenderEncoderSlot_;
    };
    if (const auto* id =
            recordingRetention_->retainResourceProjection(a + b + p + extra + 3u, ownerAt)) {
        diagnostic = id;
        if (std::string_view(id) == "RHI-COMMAND-RETENTION-OWNER-MAPPING-CHANGED")
            poisonRecording(diagnostic);
        return false;
    }
    if (write && !prepareMetalRhiRecordedState(*write->buffer_, *ordinaryWriteStates_)) {
        diagnostic = "RHI-RESOURCE-STATE-RECORDING-CONFLICT";
        poisonRecording(diagnostic);
        return false;
    }
    if (!encodeOrdinaryPreparedRenderPass(pass, binding, diagnostic, push)) {
        poisonRecording(diagnostic);
        return false;
    }
    if (consumer) {
        ordinaryReadConsumer_ = consumer;
        if (const auto* id =
                ordinaryWriteSequence_.beginConsumer(ordinaryWrite_->resource().id())) {
            diagnostic = id;
            poisonRecording(diagnostic);
            return false;
        }
        ++stats_->ordinaryWriteCommandCount;
    }
    if (write) {
        ordinaryWrite_ = write;
        if (const auto* id = ordinaryWriteSequence_.begin(write->resource().id())) {
            diagnostic = id;
            poisonRecording(diagnostic);
            return false;
        }
        ++stats_->ordinaryWriteCommandCount;
    }
    if (ordinaryIndirectRecording_) {
        ordinaryIndirectRecording_->begin();
        publishOrdinaryIndirectCounters();
    }
    return true;
}

bool MetalRhiNativeCommandPayload::encodeOrdinaryPreparedRenderPass(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass>& pass,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>& binding,
    core::pmr::String& diagnostic,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsPush>& push)
{
    @autoreleasepool {
        const auto command = reinterpret_cast<id<MTLCommandBuffer>>(nativeCommandBuffer_.get());
        const auto descriptor =
            reinterpret_cast<MTLRenderPassDescriptor*>(pass->native().descriptor.get());
        const auto encoder = [command renderCommandEncoderWithDescriptor:descriptor];
        if (!encoder) {
            diagnostic = "RHI-METAL-RENDER-PASS-ENCODER-CREATE-FAILED";
            poisonRecording(diagnostic);
            return false;
        }
        [encoder retain];
        if (!preparedRenderEncoderSlot_->adopt(reinterpret_cast<void*>(encoder))) {
            [encoder endEncoding];
            [encoder release];
            diagnostic = "RHI-METAL-ORDINARY-PASS-ENCODER-ADOPTION-FAILED";
            poisonRecording(diagnostic);
            return false;
        }
        nativeRenderEncoder_ =
            std::shared_ptr<void>(preparedRenderEncoderSlot_, preparedRenderEncoderSlot_->get());
        [encoder setViewport:MTLViewport{0.0,
                                 0.0,
                                 static_cast<double>(pass->native().width),
                                 static_cast<double>(pass->native().height),
                                 0.0,
                                 1.0}];
        [encoder
            setScissorRect:MTLScissorRect{0u, 0u, pass->native().width, pass->native().height}];
    }
    ordinaryGraphicsPass_ = pass;
    ordinaryGraphicsDescriptors_.reset();
    ordinaryGraphicsBindings_ = {};
    ordinaryGraphicsContract_.reset();
    renderPassWidth_ = pass->native().width;
    renderPassHeight_ = pass->native().height;
    renderSubpassCount_ = 1u;
    renderSubpassIndex_ = 0u;
    renderPassLayered_ = false;
    vertexStreams_ = {};
    ordinaryVertexBindings_ = {};
    indexBuffer_.reset();
    tessellationFactorBuffer_.reset();
    stats_->nativeRenderEncoderCreated = true;
    ++stats_->nativeViewportSetCount;
    ++stats_->nativeScissorSetCount;
    stats_->nativeDepthStencilAttachmentRecorded =
        pass->native().depthFormat != rhi::TextureFormat::Unknown;
    if (pass->native().colorCount) {
        stats_->nativeColorClearValueRecorded = true;
        stats_->nativeColorClearValue = pass->native().firstClearColor;
    }
    return bindOrdinaryGraphicsUnchecked(binding, diagnostic, push);
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedWriteDependency(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite>& write,
    core::pmr::String& diagnostic)
{
    if (!retentionRecordingAllows(true, diagnostic))
        return false;
    if (!ordinaryWriteStates_ || !write || write != ordinaryWrite_ || !nativeCommandBuffer_
        || nativeRenderEncoder_ || nativeComputeEncoder_ || nativeBlitEncoder_) {
        diagnostic = "RHI-METAL-ORDINARY-WRITE-DEPENDENCY-RECORDING-MISMATCH";
        return false;
    }
    if (const auto* id = ordinaryWriteSequence_.canDepend(write->resource().id())) {
        diagnostic = id;
        return false;
    }
    const auto observed = write->validateObservation();
    if (!observed.success) {
        diagnostic = observed.diagnostics;
        return false;
    }
    if (write->resource().state() != write->dependency().before
        || !canPrepareMetalRhiRecordedState(write->resource(), *ordinaryWriteStates_)) {
        diagnostic = "RHI-METAL-ORDINARY-WRITE-DEPENDENCY-STATE-MISMATCH";
        return false;
    }
    if (const auto* id = write->contract().validateDependency(
            write->resource().id(), write->dependency(), write->consumer(), true)) {
        diagnostic = id;
        return false;
    }
    const auto state = setMetalRhiBufferStateForBarrier(
        *write->buffer_, write->dependency(), ordinaryWriteStates_.get());
    if (!state.success) {
        diagnostic = state.diagnostics;
        poisonRecording(diagnostic);
        return false;
    }
    // Approved Equivalent native dependency: same actual tracked buffer, an
    // actually ended producer encoder and a dedicated subsequent consumer.
    // No generic payload-free/stat-only barrier is enabled by this operation.
    ordinaryWriteSequence_.depend(write->resource().id());
    stats_->nativeResourceBarrierEncoded = true;
    ++stats_->ordinaryWriteCommandCount;
    return true;
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedWriteReadback(
    const std::shared_ptr<MetalRhiOrdinaryPreparedWriteReadback>& readback,
    core::pmr::String& diagnostic)
{
    if (!retentionRecordingAllows(true, diagnostic))
        return false;
    if (!ordinaryWriteStates_ || !ordinaryWrite_ || !readback || readback->write() != ordinaryWrite_
        || readback->quota() != recordingRetention_->quota() || readback->recorded()
        || readback->status() != MetalRhiOrdinaryPreparedWriteReadback::Status::Pending
        || !nativeCommandBuffer_ || nativeRenderEncoder_ || nativeComputeEncoder_
        || nativeBlitEncoder_ || !preparedWriteBlitEncoderSlot_
        || preparedWriteBlitEncoderSlot_->get()) {
        diagnostic = "RHI-METAL-ORDINARY-WRITE-READBACK-RECORDING-MISMATCH";
        return false;
    }
    if (const auto* id = ordinaryWriteSequence_.canConsume(ordinaryWrite_->resource().id())) {
        diagnostic = id;
        return false;
    }
    const auto observed = readback->validateObservation();
    if (!observed.success) {
        diagnostic = observed.diagnostics;
        return false;
    }
    if (ordinaryWrite_->resource().state() != rhi::ResourceState::CopySrc
        || !canPrepareMetalRhiRecordedState(*readback->buffer_, *ordinaryWriteStates_)) {
        diagnostic = "RHI-METAL-ORDINARY-WRITE-READBACK-STATE-MISMATCH";
        return false;
    }
    const auto source = metalRhiNativePayloadFor(*ordinaryWrite_->buffer_);
    const auto destination = metalRhiNativePayloadFor(*readback->buffer_);
    if (!source || !destination
        || !hasResourceUsage(
            ordinaryWrite_->resource().desc().usage, rhi::ResourceUsage::CopySrc)) {
        diagnostic = "RHI-METAL-ORDINARY-WRITE-READBACK-USAGE";
        return false;
    }
#if !TARGET_OS_IPHONE
    if (destination->resource.storageMode == MetalRhiNativeStorageMode::Managed) {
        if (@available(macOS 10.11, *)) {
        } else {
            diagnostic = "RHI-METAL-MANAGED-READBACK-SYNCHRONIZE-API-UNAVAILABLE";
            return false;
        }
    }
#endif
    const auto count = readback->childOwnerCount();
    if (count > std::numeric_limits<std::size_t>::max() - 2u) {
        diagnostic = "RHI-METAL-ORDINARY-PREPARED-OWNER-COUNT-OVERFLOW";
        return false;
    }
    const auto ownerAt = [&](std::size_t i) noexcept -> std::shared_ptr<const void> {
        if (!i)
            return readback;
        return i <= count ? readback->childOwnerAt(i - 1u) : preparedWriteBlitEncoderSlot_;
    };
    if (const auto* id = recordingRetention_->retainResourceProjection(count + 2u, ownerAt)) {
        diagnostic = id;
        if (std::string_view(id) == "RHI-COMMAND-RETENTION-OWNER-MAPPING-CHANGED")
            poisonRecording(diagnostic);
        return false;
    }
    bool unrecorded = false;
    if (!readback->recorded_.compare_exchange_strong(unrecorded, true, std::memory_order_acq_rel)
        || !prepareMetalRhiRecordedState(*readback->buffer_, *ordinaryWriteStates_)) {
        diagnostic = "RHI-METAL-ORDINARY-WRITE-READBACK-ADMISSION-CONFLICT";
        poisonRecording(diagnostic);
        return false;
    }
    ordinaryWriteReadback_ = readback;
    @autoreleasepool {
        const auto command = reinterpret_cast<id<MTLCommandBuffer>>(nativeCommandBuffer_.get());
        const auto encoder = [command blitCommandEncoder];
        if (!encoder) {
            diagnostic = "RHI-METAL-BLIT-PASS-ENCODER-CREATE-FAILED";
            poisonRecording(diagnostic);
            return false;
        }
        [encoder retain];
        if (!preparedWriteBlitEncoderSlot_->adopt(reinterpret_cast<void*>(encoder))) {
            [encoder endEncoding];
            [encoder release];
            diagnostic = "RHI-METAL-ORDINARY-WRITE-READBACK-ENCODER-ADOPTION-FAILED";
            poisonRecording(diagnostic);
            return false;
        }
        [encoder copyFromBuffer:nativeBuffer(source)
                   sourceOffset:0u
                       toBuffer:nativeBuffer(destination)
              destinationOffset:0u
                           size:rhi::GraphicsStorageWriteContract::footprintBytes];
#if !TARGET_OS_IPHONE
        if (destination->resource.storageMode == MetalRhiNativeStorageMode::Managed)
            [encoder synchronizeResource:nativeBuffer(destination)];
#endif
        [encoder endEncoding];
        preparedWriteBlitEncoderSlot_->reset();
    }
    ordinaryWriteSequence_.consume(ordinaryWrite_->resource().id());
    ++stats_->ordinaryWriteCommandCount;
    stats_->nativeBlitEncoderCreated = true;
    stats_->nativeBlitCopyEncoded = true;
    return true;
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedGraphicsBinding(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>& binding,
    core::pmr::String& diagnostic,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsPush>& push)
{
    if (!retentionRecordingAllows(true, diagnostic, true))
        return false;
    if (ordinaryIndirectRecording_)
        if (const auto* id = ordinaryIndirectRecording_->canStateCommand()) {
            diagnostic = id;
            return false;
        }
    if (ordinaryWriteStates_) {
        diagnostic = "RHI-METAL-ORDINARY-WRITE-REBIND-DOMAIN";
        return false;
    }
    if (!ordinaryGraphicsPass_ || !nativeRenderEncoder_) {
        diagnostic = "RHI-METAL-ORDINARY-GRAPHICS-PASS-REQUIRED";
        return false;
    }
    const auto pass = ordinaryGraphicsPass_->validateObservation();
    if (!pass.success) {
        diagnostic = pass.diagnostics;
        return false;
    }
    if (!validateOrdinaryGraphicsBinding(binding, diagnostic)
        || !validateGraphicsPipelineBind(binding->provider()->native(), diagnostic)
        || !validateOrdinaryGraphicsPush(binding, push, diagnostic))
        return false;
    const bool changed =
        !ordinaryGraphicsContract_ || ordinaryGraphicsContract_->provider() != binding->provider();
    if (!changed) {
        const auto capacity =
            ordinaryGraphicsDescriptors_.validateSetCapacity(binding->payload()->set);
        if (!capacity.success) {
            diagnostic = capacity.diagnostics;
            return false;
        }
    }
    const auto p = push ? push->childOwnerCount() + 1u : 0u;
    if (p == std::numeric_limits<std::size_t>::max()
        || binding->childOwnerCount() >= std::numeric_limits<std::size_t>::max() - p) {
        diagnostic = "RHI-METAL-ORDINARY-PREPARED-OWNER-COUNT-OVERFLOW";
        return false;
    }
    const auto b = binding->childOwnerCount() + 1u;
    const auto ownerAt = [&](std::size_t i) noexcept -> std::shared_ptr<const void> {
        if (i < b)
            return !i ? std::shared_ptr<const void>(binding) : binding->childOwnerAt(i - 1u);
        i -= b;
        return !i ? std::shared_ptr<const void>(push) : push->childOwnerAt(i - 1u);
    };
    if (const auto* id = recordingRetention_->retainResourceProjection(b + p, ownerAt)) {
        diagnostic = id;
        if (std::string_view(id) == "RHI-COMMAND-RETENTION-OWNER-MAPPING-CHANGED")
            poisonRecording(diagnostic);
        return false;
    }
    const auto bound = bindOrdinaryGraphicsUnchecked(binding, diagnostic, push);
    if (ordinaryIndirectRecording_) {
        if (!bound) {
            poisonRecording(diagnostic);
            return false;
        }
        ordinaryIndirectRecording_->stateCommand();
        publishOrdinaryIndirectCounters();
    }
    return bound;
}

bool MetalRhiNativeCommandPayload::validateOrdinaryGraphicsDraw(core::pmr::String& diagnostic,
    std::uint64_t count,
    std::uint64_t first,
    std::uint64_t instances,
    std::uint64_t firstInstance) const
{
    if (ordinaryReadConsumer_
        && ordinaryWriteSequence_.phase()
            == rhi::GraphicsStorageWriteSequence::Phase::ConsumerOpen) {
        if (ordinaryConsumerDraws_ >= maxOrdinaryConsumerDraws_) {
            diagnostic = "RHI-METAL-ORDINARY-READ-CONSUMER-DRAW-BUDGET";
            return false;
        }
        const auto observed = ordinaryReadConsumer_->validateObservation();
        if (!observed.success) {
            diagnostic = observed.diagnostics;
            return false;
        }
        if (ordinaryWrite_->resource().state() != rhi::ResourceState::ShaderRead) {
            diagnostic = "RHI-METAL-ORDINARY-READ-CONSUMER-STATE-MISMATCH";
            return false;
        }
        if (const auto* id =
                ordinaryWrite_->contract().validateDraw(count, instances, false, false)) {
            diagnostic = id;
            return false;
        }
    }
    const auto* write =
        ordinaryWriteSequence_.phase() == rhi::GraphicsStorageWriteSequence::Phase::ProducerOpen
        ? ordinaryWrite_.get()
        : nullptr;
    if (write) {
        if (ordinaryWriteDraws_ >= maxOrdinaryWriteDraws_) {
            diagnostic = "RHI-METAL-ORDINARY-WRITE-DRAW-BUDGET";
            return false;
        }
        if (const auto* id = write->contract().validateDraw(count, instances, false, false)) {
            diagnostic = id;
            return false;
        }
    }
    if (!validateOrdinaryGraphicsFixed(diagnostic, write))
        return false;
    for (const auto& stream : ordinaryGraphicsContract_->provider()->vertexInput().streams()) {
        const auto& vertex = ordinaryVertexBindings_[stream.slot];
        if (const auto* id = vertex->validateDraw(count, first, instances, firstInstance)) {
            diagnostic = id;
            return false;
        }
    }
    return true;
}

bool MetalRhiNativeCommandPayload::validateOrdinaryGraphicsFixed(
    core::pmr::String& diagnostic, const MetalRhiOrdinaryPreparedGraphicsWrite* write) const
{
    if (!ordinaryGraphicsPass_ || !ordinaryGraphicsContract_ || !renderPipeline_
        || ordinaryGraphicsContract_->provider()->native() != renderPipeline_) {
        diagnostic = "RHI-METAL-ORDINARY-GRAPHICS-BINDING-REQUIRED";
        return false;
    }
    const auto pass = ordinaryGraphicsPass_->validateObservation();
    if (!pass.success) {
        diagnostic = pass.diagnostics;
        return false;
    }
    for (const auto& binding : ordinaryGraphicsBindings_) {
        if (!binding)
            continue;
        if (binding->provider() != ordinaryGraphicsContract_->provider()) {
            diagnostic = "RHI-METAL-ORDINARY-GRAPHICS-PROVIDER-IDENTITY-MISMATCH";
            return false;
        }
        if (!validateOrdinaryGraphicsBinding(binding, diagnostic, write))
            return false;
    }
    const auto complete = ordinaryGraphicsDescriptors_.validateComplete(
        ordinaryGraphicsContract_->provider()->consumption(),
        renderPipeline_->preparedGraphicsStages);
    if (!complete.success) {
        diagnostic = complete.diagnostics;
        return false;
    }
    for (const auto& stream : ordinaryGraphicsContract_->provider()->vertexInput().streams()) {
        const auto& vertex = ordinaryVertexBindings_[stream.slot];
        if (!vertex || vertex->provider() != ordinaryGraphicsContract_->provider()) {
            diagnostic = "RHI-METAL-ORDINARY-VERTEX-BINDING-MISSING";
            return false;
        }
        if (const auto* id = vertex->validate()) {
            diagnostic = id;
            return false;
        }
    }
    if (ordinaryGraphicsContract_->provider()->interface().reflection.pushConstantByteCount) {
        if (!ordinaryGraphicsPush_
            || ordinaryGraphicsPush_->provider() != ordinaryGraphicsContract_->provider()) {
            diagnostic = "[PC-E011] RHI-METAL-ORDINARY-GRAPHICS-PUSH-FRESH-BLOCK-REQUIRED";
            return false;
        }
        const auto push = ordinaryGraphicsPush_->validate();
        if (!push.success) {
            diagnostic = push.diagnostics;
            return false;
        }
    }
    return true;
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedComputeBinding(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedComputeBinding>& prepared,
    core::pmr::String& diagnostic,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedComputePush>& push)
{
    if (!retentionRecordingAllows(true, diagnostic))
        return false;
    if (!recordingRetention_) {
        diagnostic = "RHI-METAL-ORDINARY-PREPARED-COMMAND-RETENTION-REQUIRED";
        return false;
    }
    const auto encoder =
        reinterpret_cast<id<MTLComputeCommandEncoder>>(nativeComputeEncoder_.get());
    if (!prepared || encoder == nil || prepared->registry() != resourceRegistry_
        || !commandDeviceMatches(nativeDevice_, prepared->function().pipeline->nativeDevice)) {
        diagnostic = "RHI-METAL-ORDINARY-PREPARED-RECORDING-MISMATCH";
        return false;
    }
    if (prepared->quota() != recordingRetention_->quota()
        || prepared->budget().maxCpuMetadataBytes
            != recordingRetention_->budget().maxCpuMetadataBytes) {
        diagnostic = "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
        return false;
    }
    const auto observation = prepared->validateObservation();
    if (!observation.success) {
        diagnostic = observation.diagnostics;
        return false;
    }
    const auto& function = prepared->function();
    if (!ordinaryComputePipelineValid(*function.pipeline, nativeDevice_)
        || function.stage != rhi::ShaderStage::Compute
        || function.function != function.pipeline->computeFunction) {
        diagnostic = "RHI-METAL-ORDINARY-PREPARED-PIPELINE-INVALID";
        return false;
    }
    MetalRhiNativeFunctionBindingRecording recording(
        function, metalRhiPreparedComputeLayoutValidator(*function.pipeline));
    if (!recording.validate(diagnostic))
        return false;
    const bool changedPipeline = computePipeline_ != function.pipeline;
    if ((push || changedPipeline) && ordinaryComputePushState_.hasUnconsumedUpdate()) {
        diagnostic = "[PC-E013] RHI-METAL-ORDINARY-PUSH-DUPLICATE-UNCONSUMED";
        return false;
    }
    if (push) {
        if (push->quota() != recordingRetention_->quota()) {
            diagnostic = "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
            return false;
        }
        if (push->pipeline() != function.pipeline
            || push->bytes().size() != prepared->requiredPushBytes()) {
            diagnostic = "[PC-E020] RHI-METAL-ORDINARY-PUSH-BINDING-MISMATCH";
            return false;
        }
        const auto valid = push->validate();
        if (!valid.success) {
            diagnostic = valid.diagnostics;
            return false;
        }
    }
    if (!changedPipeline) {
        const auto capacity = ordinaryComputeDescriptors_.validateSetCapacity(function.set);
        if (!capacity.success) {
            diagnostic = capacity.diagnostics;
            return false;
        }
    }
    std::size_t bindingIndex = 0u;
    if (!changedPipeline) {
        for (; bindingIndex < ordinaryComputeBindings_.size(); ++bindingIndex) {
            const auto& previous = ordinaryComputeBindings_[bindingIndex];
            if (!previous || previous->function().set == function.set)
                break;
        }
    }
    if (bindingIndex == ordinaryComputeBindings_.size()) {
        diagnostic = "RHI-DESCRIPTOR-TRACKING-CAPACITY";
        return false;
    }
    if (prepared->childOwnerCount() >= std::numeric_limits<std::size_t>::max() - (push ? 2u : 0u)) {
        diagnostic = "RHI-METAL-ORDINARY-PREPARED-OWNER-COUNT-OVERFLOW";
        return false;
    }
    const auto descriptorOwners = prepared->childOwnerCount() + 1u;
    auto ownerAt = [&](std::size_t index) noexcept -> std::shared_ptr<const void> {
        if (index < descriptorOwners)
            return index == 0u ? std::shared_ptr<const void>(prepared)
                               : prepared->childOwnerAt(index - 1u);
        return index == descriptorOwners ? std::shared_ptr<const void>(push)
                                         : std::shared_ptr<const void>(push->pipeline());
    };
    if (const auto* id = recordingRetention_->retainResourceProjection(
            descriptorOwners + (push ? 2u : 0u), ownerAt)) {
        diagnostic = id;
        if (std::string_view(id) == "RHI-COMMAND-RETENTION-OWNER-MAPPING-CHANGED")
            poisonRecording(diagnostic);
        return false;
    }
    [encoder setComputePipelineState:reinterpret_cast<id<MTLComputePipelineState>>(
                                         function.pipeline->pipelineState.get())];
    recording.record(encoder, nil);
    if (changedPipeline) {
        ordinaryComputeDescriptors_.reset();
        ordinaryComputeBindings_ = {};
        ordinaryComputePush_.reset();
        ordinaryComputePushState_.onPassEnd();
    }
    if (push) {
        [encoder setBytes:push->bytes().data() length:push->bytes().size() atIndex:push->slot()];
        ordinaryComputePush_ = push;
        ordinaryComputePushState_.onPush();
        stats_->nativePushConstantSetBytesWritten = true;
        stats_->nativePushConstantUploadBufferWritten = false;
        stats_->nativePushConstantUploadBytes = static_cast<std::uint32_t>(push->bytes().size());
    }
    const auto tracked =
        ordinaryComputeDescriptors_.bindSet(function.set, *prepared->descriptor(), nullptr, 0u);
    if (!tracked.success) {
        diagnostic = tracked.diagnostics;
        poisonRecording(diagnostic);
        return false;
    }
    ordinaryComputeBindings_[bindingIndex] = prepared;
    computePipeline_ = function.pipeline;
    ordinaryComputeContract_ = prepared;
    stats_->nativeComputePipelineBound = true;
    stats_->nativeDescriptorArgumentBound = true;
    stats_->nativeArgumentBufferReuseCount += static_cast<std::uint32_t>(function.arrays.size());
    return true;
}

bool MetalRhiNativeCommandPayload::validatePreparedComputeBinding(
    const std::shared_ptr<const MetalRhiNativePreparedComputeBindings>& prepared,
    const std::shared_ptr<const rhi::BindlessPreparedComputeBinding>& owner,
    core::pmr::String& diagnostic) const
{
    if (prepared && prepared->descriptors
        && !prepared->descriptors->originalTextureCellSources.empty()) {
        diagnostic = "RHI-METAL-STORAGE-CELL-PREPARATION-NOT-EXECUTABLE";
        return false;
    }

    if ((owner && owner->owners()
            && owner->owners()->desc().plan.originalExecutionContractVersion == 8u)
        || (prepared && prepared->descriptors
            && !prepared->descriptors->originalCompletedTextureSources.empty())) {
        diagnostic = "RHI-METAL-COMPLETED-TEXTURE-ORIGINAL-ROOT-REQUIRED";
        return false;
    }
    if ((prepared && !prepared->sharedOwnerMatches(prepared))
        || (owner && !owner->sharedOwnerMatches(owner))) {
        diagnostic = "RHI-METAL-PREPARED-COMPUTE-OWNER-MISMATCH";
        return false;
    }
    const auto encoder =
        reinterpret_cast<id<MTLComputeCommandEncoder>>(nativeComputeEncoder_.get());
    if (!prepared || !owner || encoder == nil || abandoned_ || recordingPoisoned_
        || submitted_.load(std::memory_order_acquire) || !prepared->pipeline
        || !prepared->descriptors || metalRhiPreparedComputePayloadFor(*owner) != prepared
        || computePipeline_ != prepared->pipeline
        || !commandDeviceMatches(nativeDevice_, prepared->pipeline->nativeDevice)) {
        diagnostic = "RHI-METAL-PREPARED-COMPUTE-RECORDING-MISMATCH";
        return false;
    }
    if (!retainedPreparedBindings_.canRetain(owner)) {
        diagnostic = "RHI-BINDLESS-PREPARED-COMPUTE-COMMAND-BUDGET";
        return false;
    }
    const auto valid = owner->validatePayload();
    if (!valid.success) {
        diagnostic = valid.diagnostics;
        return false;
    }
    return validateMetalRhiNativePreparedComputeBindings(prepared, diagnostic);
}

bool MetalRhiNativeCommandPayload::recordPreparedComputeBinding(
    const std::shared_ptr<const MetalRhiNativePreparedComputeBindings>& prepared,
    const std::shared_ptr<const rhi::BindlessPreparedComputeBinding>& owner,
    core::pmr::String& diagnostic)
{
    if (!retentionRecordingAllows(false, diagnostic))
        return false;
    if (!validatePreparedComputeBinding(prepared, owner, diagnostic))
        return false;
    const auto encoder =
        reinterpret_cast<id<MTLComputeCommandEncoder>>(nativeComputeEncoder_.get());
    MetalRhiNativeFunctionBindingRecording(
        *prepared, metalRhiPreparedComputeLayoutValidator(*prepared->pipeline))
        .record(encoder, nil);
    retainedPreparedBindings_.retain(owner);
    stats_->nativeDescriptorArgumentBound = true;
    stats_->nativeArgumentBufferReuseCount += static_cast<std::uint32_t>(prepared->arrays.size());
    return true;
}

bool MetalRhiNativeCommandPayload::validatePreparedGraphicsBinding(
    const std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>& prepared,
    core::pmr::String& diagnostic) const
{
    if (!@available(macOS 10.15, iOS 13.0, *)) {
        diagnostic = "RHI-METAL-PREPARED-GRAPHICS-RESIDENCY-API-UNAVAILABLE";
        return false;
    }
    if (!prepared || !nativeRenderEncoder_ || abandoned_ || recordingPoisoned_
        || stats_->nativeCommandBufferFinished || submitted_.load(std::memory_order_acquire)
        || !prepared->pipeline || !prepared->descriptors || renderPipeline_ != prepared->pipeline
        || !commandDeviceMatches(nativeDevice_, prepared->pipeline->nativeDevice)) {
        diagnostic = "RHI-METAL-PREPARED-GRAPHICS-RECORDING-MISMATCH";
        return false;
    }
    if (!retainedPreparedBindings_.canRetain(prepared)) {
        diagnostic = "RHI-BINDLESS-PREPARED-GRAPHICS-COMMAND-BUDGET";
        return false;
    }
    return validatePreparedGraphicsStorage(prepared, diagnostic);
}

bool MetalRhiNativeCommandPayload::validatePreparedGraphicsStorage(
    const std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>& prepared,
    core::pmr::String& diagnostic) const
{
    if (prepared && prepared->descriptors
        && !prepared->descriptors->originalTextureCellSources.empty()) {
        diagnostic = "RHI-METAL-STORAGE-CELL-PREPARATION-NOT-EXECUTABLE";
        return false;
    }

    if (prepared && prepared->descriptors
        && !prepared->descriptors->originalCompletedTextureSources.empty()) {
        diagnostic = "RHI-METAL-COMPLETED-TEXTURE-ORIGINAL-ROOT-REQUIRED";
        return false;
    }
    if (!@available(macOS 10.15, iOS 13.0, *)) {
        diagnostic = "RHI-METAL-PREPARED-GRAPHICS-RESIDENCY-API-UNAVAILABLE";
        return false;
    }
    if (!prepared || !prepared->pipeline || !prepared->descriptors
        || !prepared->pipeline->nativeRuntimeBacked
        || !prepared->pipeline->renderPipelinePayloadPresent
        || !commandDeviceMatches(nativeDevice_, prepared->pipeline->nativeDevice)) {
        diagnostic = "RHI-METAL-PREPARED-GRAPHICS-RECORDING-MISMATCH";
        return false;
    }
    return validateMetalRhiNativeGraphicsBindings(prepared, diagnostic);
}

bool validateMetalRhiNativeGraphicsBindings(
    const std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>& prepared,
    core::pmr::String& diagnostic)
{
    if (prepared && !prepared->sharedOwnerMatches(prepared)) {
        diagnostic = "RHI-METAL-PREPARED-GRAPHICS-OWNER-MISMATCH";
        return false;
    }
    if (!prepared || !prepared->pipeline || !prepared->descriptors
        || !prepared->pipeline->nativeRuntimeBacked
        || !prepared->pipeline->renderPipelinePayloadPresent
        || !prepared->pipeline->pipelineState) {
        diagnostic = "RHI-METAL-PREPARED-GRAPHICS-RECORDING-MISMATCH";
        return false;
    }
    const auto state =
        reinterpret_cast<id<MTLRenderPipelineState>>(prepared->pipeline->pipelineState.get());
    if (!prepared->pipeline->nativeDevice
        || state.device
            != reinterpret_cast<id<MTLDevice>>(prepared->pipeline->nativeDevice.get())) {
        diagnostic = "RHI-METAL-PREPARED-GRAPHICS-DEVICE-MISMATCH";
        return false;
    }
    const auto& pipeline = *prepared->pipeline;
    const bool hasFragment =
        pipeline.preparedGraphicsStages == (rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment);
    const bool vertexOnly = pipeline.preparedGraphicsStages == rhi::ShaderStage::Vertex
        && pipeline.depthOnlyRenderPipeline
        && rhi::isDepthTextureFormat(pipeline.depthStencilFormat);
    if (!hasFragment && !vertexOnly) {
        diagnostic = "RHI-METAL-PREPARED-GRAPHICS-ENTRY-STAGE-MISMATCH";
        return false;
    }
    const std::array functions{pipeline.vertexFunction, pipeline.fragmentFunction};
    const std::array stages{rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment};
    for (std::size_t i = 0u; i < stages.size(); ++i) {
        const auto& stage = prepared->stages[i];
        const auto& layout = prepared->layouts[i];
        if (i == 1u && !hasFragment) {
            if (!stage && !layout && !functions[i] && !pipeline.fragmentShaderLibrary)
                continue;
            diagnostic = "RHI-METAL-PREPARED-GRAPHICS-ENTRY-STAGE-MISMATCH";
            return false;
        }
        if (!stage || !layout || stage->stage != stages[i] || layout->stage != stages[i]
            || stage->pipeline != prepared->pipeline || stage->descriptors != prepared->descriptors
            || stage->set != prepared->set || stage->function != functions[i] || !functions[i]) {
            diagnostic = "RHI-METAL-PREPARED-GRAPHICS-FUNCTION-STAGE-ENTRY-MISMATCH";
            return false;
        }
        const auto function = reinterpret_cast<id<MTLFunction>>(stage->function.get());
        const bool typeMatches = i == 0u ? function.functionType == MTLFunctionTypeVertex
                                         : function.functionType == MTLFunctionTypeFragment;
        if (!typeMatches || !function.name.UTF8String
            || layout->entry != function.name.UTF8String) {
            diagnostic = "RHI-METAL-PREPARED-GRAPHICS-FUNCTION-STAGE-ENTRY-MISMATCH";
            return false;
        }
        const auto valid = layout->validate(*prepared->descriptors,
            prepared->set,
            pipeline.nativeBindingStages,
            pipeline.vertexStreams);
        if (!valid.success) {
            diagnostic = valid.diagnostics;
            return false;
        }
        if (!MetalRhiNativeFunctionBindingRecording(*stage,
                MetalRhiPreparedBindingLayoutValidator(layout->direct,
                    layout->arrays,
                    pipeline.nativeBindingStages,
                    stage->stage,
                    layout->pushSlot))
                .validate(diagnostic))
            return false;
    }
    return true;
}

bool MetalRhiNativeCommandPayload::recordPreparedGraphicsBinding(
    const std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>& prepared,
    core::pmr::String& diagnostic)
{
    if (!retentionRecordingAllows(false, diagnostic))
        return false;
    return recordPreparedGraphicsBindingAt(prepared, false, diagnostic);
}

bool MetalRhiNativeCommandPayload::recordScopedGraphicsBinding(
    const std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>& prepared,
    const rhi::BindlessAccessDeclarations& accesses,
    core::pmr::String& diagnostic)
{
    if (!retentionRecordingAllows(false, diagnostic))
        return false;
    if (!activeGraphicsBinding_ || prepared != activeGraphicsBinding_
        || !graphicsAccesses_.matches(accesses)) {
        diagnostic = "RHI-METAL-GRAPHICS-SCOPE-BINDING-MISMATCH";
        return false;
    }
    return recordPreparedGraphicsBindingAt(prepared, true, diagnostic);
}

bool MetalRhiNativeCommandPayload::recordPreparedGraphicsBindingAt(
    const std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>& prepared,
    bool scoped,
    core::pmr::String& diagnostic)
{
    if (!retentionRecordingAllows(false, diagnostic))
        return false;
    if (activeGraphicsBinding_ && (!scoped || activeGraphicsBinding_ != prepared)) {
        diagnostic = "RHI-METAL-GRAPHICS-SCOPE-BINDING-MISMATCH";
        return false;
    }
    if (!validatePreparedGraphicsBinding(prepared, diagnostic))
        return false;
    const auto encoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    for (std::size_t i = 0u; i < prepared->stages.size(); ++i) {
        const auto& stage = prepared->stages[i];
        if (!stage)
            continue;
        const auto& layout = *prepared->layouts[i];
        MetalRhiNativeFunctionBindingRecording(*stage,
            MetalRhiPreparedBindingLayoutValidator(layout.direct,
                layout.arrays,
                prepared->pipeline->nativeBindingStages,
                stage->stage,
                layout.pushSlot))
            .record(nil, encoder);
    }
    retainedPreparedBindings_.retain(prepared);
    stats_->nativeDescriptorArgumentBound = true;
    stats_->nativeArgumentBufferReuseCount += prepared->argumentCount;
    if (scoped)
        graphicsScopeBound_ = true;
    return true;
}

bool MetalRhiNativeCommandPayload::prepareGraphicsPassScope(
    std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings> prepared,
    std::shared_ptr<const rhi::BindlessFaultDomain> domain,
    const rhi::BindlessAccessDeclarations& accesses,
    core::pmr::String& diagnostic)
{
    if (!retentionRecordingAllows(false, diagnostic))
        return false;
    const std::shared_ptr<const void> owner = prepared;
    return prepareGraphicsPassScopeWithOwner(
        std::move(prepared), std::move(domain), owner, accesses, diagnostic);
}

bool MetalRhiNativeCommandPayload::prepareGraphicsPassScope(
    std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding> binding,
    const rhi::BindlessAccessDeclarations& accesses,
    core::pmr::String& diagnostic)
{
    if (!retentionRecordingAllows(false, diagnostic))
        return false;
    const auto prepared = binding && binding->sharedOwnerMatches(binding)
        ? metalRhiPreparedGraphicsPayloadFor(*binding)
        : nullptr;
    if (!prepared || !binding->owners()) {
        diagnostic = "RHI-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH";
        return false;
    }
    const auto valid = binding->validateForRecord(*binding->owners()->desc().pipeline, accesses);
    if (!valid.success) {
        diagnostic = valid.diagnostics;
        return false;
    }
    // Same bounded identity as storage, but ownership includes public budget leases.
    const std::shared_ptr<const void> owner(binding, prepared.get());
    for (const auto& retained : retainedPreparedBindings_.values()) {
        if (retained.get() == prepared.get()
            && (retained.owner_before(owner) || owner.owner_before(retained))) {
            diagnostic = "RHI-METAL-GRAPHICS-SCOPE-OWNER-MISMATCH";
            return false;
        }
    }
    return prepareGraphicsPassScopeWithOwner(
        prepared, binding->owners()->desc().snapshot, owner, accesses, diagnostic);
}

bool MetalRhiNativeCommandPayload::prepareGraphicsPassScopeWithOwner(
    std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings> prepared,
    std::shared_ptr<const rhi::BindlessFaultDomain> domain,
    std::shared_ptr<const void> owner,
    const rhi::BindlessAccessDeclarations& accesses,
    core::pmr::String& diagnostic)
{
    if (!retentionRecordingAllows(false, diagnostic))
        return false;
    const auto fail = [&](const char* message) {
        graphicsAccesses_.clear();
        diagnostic = message;
        return false;
    };
    if (!nativeCommandBuffer_ || nativeRenderEncoder_ || nativeComputeEncoder_ || nativeBlitEncoder_
        || pendingGraphicsBinding_ || activeGraphicsBinding_ || abandoned_ || recordingPoisoned_
        || stats_->nativeCommandBufferFinished || submitted_.load(std::memory_order_acquire)) {
        // A competing preparation must preserve an already-authorized intent.
        diagnostic = "RHI-METAL-GRAPHICS-SCOPE-RECORDING-INVALID";
        return false;
    }
    if (!validatePreparedGraphicsStorage(prepared, diagnostic))
        return false;
    if (domain && !domain->sharedOwnerMatches(domain))
        return fail("RHI-BINDLESS-FAULT-SUBMISSION-OWNER-MISMATCH");
    if (!domain || !domain->validate().success || !domain->faultLatchBuffer()
        || !domain->faultReadbackBuffer())
        return fail("RHI-METAL-GRAPHICS-SCOPE-FAULT-DOMAIN-INVALID");
    if (!retainedBindlessPipelines_.canRetain(prepared->pipeline)
        || !retainedBindlessSnapshots_.canRetain(domain)
        || !retainedPreparedBindings_.canRetain(owner))
        return fail("RHI-METAL-GRAPHICS-SCOPE-COMMAND-BUDGET");
    const auto captured = graphicsAccesses_.capture(accesses);
    if (!captured.success) {
        diagnostic = captured.diagnostics;
        return false;
    }
    for (std::size_t i = 0u; i < graphicsAccesses_.size(); ++i) {
        rhi::BindlessResourceAccess access;
        graphicsAccesses_.read(i, access);
        const auto registered =
            resourceRegistry_ ? resourceRegistry_->find(access.resource) : std::nullopt;
        const auto buffer = registered ? nativeBuffer(registered->buffer) : nil;
        if (access.kind != rhi::BindlessAccessKind::Buffer || !registered || !registered->buffer
            || buffer == nil
            || !commandDeviceMatches(nativeDevice_, registered->buffer->resource.nativeDevice)
            || !access.bufferSize || access.bufferOffset > buffer.length
            || access.bufferSize > buffer.length - access.bufferOffset)
            return fail("RHI-METAL-GRAPHICS-SCOPE-ACCESS-RESOURCE-MISMATCH");
    }
    bool latchBound = false;
    for (const auto& binding : prepared->descriptors->bindings) {
        if (binding.argumentKind != MetalRhiArgumentKind::Buffer)
            return fail("RHI-METAL-GRAPHICS-SCOPE-RESOURCE-TYPE-UNSUPPORTED");
        const auto stages = binding.stages & prepared->pipeline->preparedGraphicsStages;
        if (stages == rhi::ShaderStage::None)
            continue;
        const auto registered =
            resourceRegistry_ ? resourceRegistry_->find(binding.resourceId) : std::nullopt;
        if (!registered || registered->buffer != binding.buffer)
            return fail("RHI-METAL-GRAPHICS-SCOPE-ACCESS-RESOURCE-MISMATCH");
        rhi::BindlessResourceAccess required;
        required.resource = binding.resourceId;
        required.kind = rhi::BindlessAccessKind::Buffer;
        required.stages = stages;
        required.pipelineStages = rhi::bindlessPipelineStagesForShader(stages);
        required.bufferSize = nativeBuffer(binding.buffer).length;
        const bool writable = binding.type == rhi::DescriptorType::StorageBuffer;
        required.state =
            writable ? rhi::ResourceState::ShaderWrite : rhi::ResourceState::ShaderRead;
        required.access = writable
            ? rhi::ResourceAccess::ShaderRead | rhi::ResourceAccess::ShaderWrite
            : rhi::ResourceAccess::ShaderRead;
        const auto valid = rhi::validateBindlessBufferAccess(required, graphicsAccesses_);
        if (!valid.success) {
            graphicsAccesses_.clear();
            diagnostic = valid.diagnostics;
            return false;
        }
        if (binding.resourceId == domain->faultLatchBuffer()->id()) {
            if (!writable
                || binding.buffer != metalRhiNativePayloadFor(*domain->faultLatchBuffer()))
                return fail("RHI-METAL-GRAPHICS-SCOPE-FAULT-DOMAIN-MISMATCH");
            latchBound = true;
        }
    }
    if (!latchBound)
        return fail("RHI-METAL-GRAPHICS-SCOPE-FAULT-DOMAIN-MISMATCH");
    rhi::BindlessResourceAccess readback;
    readback.resource = domain->faultReadbackBuffer()->id();
    readback.kind = rhi::BindlessAccessKind::Buffer;
    readback.bufferSize = sizeof(std::uint32_t);
    readback.state = rhi::ResourceState::CopyDst;
    readback.pipelineStages = rhi::PipelineStage::Transfer;
    readback.access = rhi::ResourceAccess::TransferWrite;
    const auto declared = rhi::validateBindlessBufferAccess(readback, graphicsAccesses_);
    if (!declared.success) {
        graphicsAccesses_.clear();
        diagnostic = declared.diagnostics;
        return false;
    }
    const auto destination = resourceRegistry_->find(readback.resource);
    if (!destination
        || destination->buffer != metalRhiNativePayloadFor(*domain->faultReadbackBuffer()))
        return fail("RHI-METAL-GRAPHICS-SCOPE-FAULT-DOMAIN-MISMATCH");
    if (!recordBindlessFaultLatchClearBeforePass(
            domain, domain->faultLatchPriorState(), diagnostic)) {
        graphicsAccesses_.clear();
        return false;
    }
    retainedBindlessPipelines_.retain(prepared->pipeline);
    retainedPreparedBindings_.retain(owner);
    pendingGraphicsBinding_ = std::move(prepared);
    pendingGraphicsFaultDomain_ = std::move(domain);
    diagnostic.clear();
    return true;
}

bool MetalRhiNativeCommandPayload::recordDescriptorSetBind(std::uint32_t set,
    const std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>& descriptorSet,
    rhi::ShaderStage stages,
    const std::uint32_t* dynamicOffsets,
    std::uint32_t dynamicOffsetCount,
    std::pmr::string& diagnostics)
{
    return recordDescriptorSetBindContents(
        set, descriptorSet, stages, dynamicOffsets, dynamicOffsetCount, diagnostics, false);
}

bool MetalRhiNativeCommandPayload::recordDescriptorSetBindContents(std::uint32_t set,
    const std::shared_ptr<const MetalRhiNativeDescriptorGroupPayload>& descriptorSet,
    rhi::ShaderStage stages,
    const std::uint32_t* dynamicOffsets,
    std::uint32_t dynamicOffsetCount,
    core::pmr::String& diagnostics,
    bool originalSynchronous)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    if (!originalSynchronous && descriptorSet
        && !descriptorSet->originalTextureCellSources.empty()) {
        diagnostics = "RHI-METAL-STORAGE-CELL-PREPARATION-NOT-EXECUTABLE";
        return false;
    }
    if (!originalSynchronous && descriptorSet
        && !descriptorSet->originalCompletedTextureSources.empty()) {
        diagnostics = "RHI-METAL-COMPLETED-TEXTURE-ORIGINAL-ROOT-REQUIRED";
        return false;
    }
    if (activeGraphicsBinding_) {
        diagnostics = "RHI-METAL-GRAPHICS-SCOPE-BINDING-MISMATCH";
        return false;
    }
    if (originalSynchronous) {
        if (!originalOrdinary_ || !originalOrdinaryEncoding_ || dynamicOffsets || dynamicOffsetCount
            || !descriptorSet || !descriptorSet->originalCompletedDepthSources.empty()) {
            diagnostics = "RHI-METAL-ORDINARY-DESCRIPTOR-PROFILE";
            return false;
        }
        for (const auto& binding : descriptorSet->bindings)
            if (binding.arrayCount != 1u || binding.arrayElement
                || binding.dynamicOffsetIndex != UINT32_MAX
                || binding.type == rhi::DescriptorType::Sampler
                || binding.type == rhi::DescriptorType::InputAttachment) {
                diagnostics = "RHI-METAL-ORDINARY-DESCRIPTOR-PROFILE";
                return false;
            }
    }
    auto* renderEncoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    auto* computeEncoder =
        reinterpret_cast<id<MTLComputeCommandEncoder>>(nativeComputeEncoder_.get());
    if (!descriptorSet || !descriptorSet->nativeRuntimeBacked
        || !commandDeviceMatches(nativeDevice_, descriptorSet->nativeDevice)
        || (renderEncoder == nil && computeEncoder == nil)
        || (!renderPipeline_ && !computePipeline_)) {
        diagnostics = "RHI-METAL-DESCRIPTOR-EXECUTABLE-PAYLOAD-MISSING";
        return false;
    }
    if (stages == rhi::ShaderStage::None
        || (renderEncoder != nil && hasStage(stages, rhi::ShaderStage::Compute))
        || (computeEncoder != nil
            && (hasStage(stages, rhi::ShaderStage::Vertex)
                || hasStage(stages, rhi::ShaderStage::Fragment)))) {
        diagnostics = "RHI-METAL-DESCRIPTOR-STAGE-ENCODER-MISMATCH";
        return false;
    }
    const auto& pipeline = renderPipeline_ ? renderPipeline_ : computePipeline_;
    for (const auto& binding : descriptorSet->bindings) {
        const auto bindingDevice = binding.buffer ? binding.buffer->resource.nativeDevice
            : binding.textureView                 ? binding.textureView->resource.nativeDevice
            : binding.sampler                     ? binding.sampler->resource.nativeDevice
                                                  : nullptr;
        if (bindingDevice && !commandDeviceMatches(nativeDevice_, bindingDevice)) {
            diagnostics = "RHI-METAL-DESCRIPTOR-DEVICE-MISMATCH";
            return false;
        }
    }
    auto findArrayElement =
        [&](std::uint32_t binding,
            std::uint32_t arrayElement) -> const MetalRhiNativeArgumentBindingPayload* {
        for (const auto& candidate : descriptorSet->bindings) {
            if (candidate.binding == binding && candidate.arrayElement == arrayElement) {
                return &candidate;
            }
        }
        return nullptr;
    };
    const auto expectedArgumentKind = [](rhi::DescriptorType type) {
        switch (type) {
        case rhi::DescriptorType::UniformBuffer:
        case rhi::DescriptorType::StorageBuffer:
        case rhi::DescriptorType::ReadOnlyStorageBuffer:
            return MetalRhiArgumentKind::Buffer;
        case rhi::DescriptorType::SampledTexture:
        case rhi::DescriptorType::StorageTexture:
        case rhi::DescriptorType::InputAttachment:
            return MetalRhiArgumentKind::Texture;
        case rhi::DescriptorType::Sampler:
            return MetalRhiArgumentKind::Sampler;
        }
        return MetalRhiArgumentKind::Texture;
    };
    const auto expectedResourceKind = [](rhi::DescriptorType type) {
        switch (type) {
        case rhi::DescriptorType::UniformBuffer:
        case rhi::DescriptorType::StorageBuffer:
        case rhi::DescriptorType::ReadOnlyStorageBuffer:
            return rhi::DescriptorResourceKind::Buffer;
        case rhi::DescriptorType::SampledTexture:
        case rhi::DescriptorType::StorageTexture:
        case rhi::DescriptorType::InputAttachment:
            return rhi::DescriptorResourceKind::TextureView;
        case rhi::DescriptorType::Sampler:
            return rhi::DescriptorResourceKind::Sampler;
        }
        return rhi::DescriptorResourceKind::TextureView;
    };

    struct PreparedDescriptorArrayEncoding {
        const MetalRhiNativeArgumentBindingPayload* first = nullptr;
        const MetalRhiNativeArgumentBufferSlot* mapped = nullptr;
        MetalRhiPreparedArgumentEncoding encoding;
    };

    core::pmr::Vector<PreparedDescriptorArrayEncoding> preparedEncodings{
        &core::defaultCpuMemoryResource()};
    if (descriptorSet->bindings.size() > std::numeric_limits<std::size_t>::max() / 2u) {
        diagnostics = "RHI-METAL-DESCRIPTOR-BINDING-COUNT-OVERFLOW";
        return false;
    }
    const auto findCachedArgumentBuffer = [&](const MetalRhiNativeArgumentBindingPayload& first,
                                              const MetalRhiNativeArgumentBufferSlot& mapped,
                                              rhi::ShaderStage stage) {
        return std::find_if(cachedArgumentBuffers_.begin(),
            cachedArgumentBuffers_.end(),
            [&](const CachedArgumentBuffer& candidate) {
                const auto* cachedSnapshot =
                    static_cast<const MetalRhiNativeDescriptorGroupPayload*>(
                        candidate.descriptorSnapshot.get());
                return dynamicOffsetCount == 0u && cachedSnapshot
                    && candidate.pipeline.get() == pipeline.get()
                    && sameDescriptorGroupSnapshot(*cachedSnapshot, *descriptorSet)
                    && candidate.set == set && candidate.binding == first.binding
                    && candidate.argumentBufferSlot == mapped.argumentBufferSlot
                    && candidate.stage == stage;
            });
    };
    const auto bindArgumentBuffer = [&](id<MTLBuffer> argumentBuffer,
                                        std::uint32_t argumentBufferSlot,
                                        rhi::ShaderStage stage) {
        if (stage == rhi::ShaderStage::Vertex && renderEncoder != nil) {
            [renderEncoder setVertexBuffer:argumentBuffer offset:0u atIndex:argumentBufferSlot];
        } else if (stage == rhi::ShaderStage::Fragment && renderEncoder != nil) {
            [renderEncoder setFragmentBuffer:argumentBuffer offset:0u atIndex:argumentBufferSlot];
        } else if (stage == rhi::ShaderStage::Compute && computeEncoder != nil) {
            [computeEncoder setBuffer:argumentBuffer offset:0u atIndex:argumentBufferSlot];
        }
    };
    const auto cleanupPrepared = [&] {
        for (auto& prepared : preparedEncodings) {
            if (prepared.encoding.encoder != nil) {
                [prepared.encoding.encoder release];
                prepared.encoding.encoder = nil;
            }
            if (prepared.encoding.argumentBuffer != nil) {
                [prepared.encoding.argumentBuffer release];
                prepared.encoding.argumentBuffer = nil;
            }
        }
    };
    const auto fail = [&](const char* message) {
        cleanupPrepared();
        diagnostics = message;
        return false;
    };
    const auto preflightArray = [&](const MetalRhiNativeArgumentBindingPayload& first,
                                    const MetalRhiNativeArgumentBufferSlot& mapped) {
        if (first.type == rhi::DescriptorType::InputAttachment) {
            return fail("RHI-METAL-FRAMEBUFFER-FETCH-ARRAY-UNSUPPORTED");
        }
        if (mapped.argumentBufferSlot >= 31u || mapped.argumentIndex == UINT32_MAX
            || mapped.capacity != first.arrayCount || first.arrayCount == 0u
            || first.arrayCount - 1u > UINT32_MAX - mapped.argumentIndex) {
            return fail("RHI-METAL-DESCRIPTOR-ARRAY-ARGUMENT-MAPPING-INVALID");
        }
        for (std::uint32_t arrayElement = 0u; arrayElement < first.arrayCount; ++arrayElement) {
            const auto* element = findArrayElement(first.binding, arrayElement);
            if (!element || element->type != first.type
                || element->resourceKind != first.resourceKind
                || element->argumentKind != first.argumentKind
                || first.argumentKind != expectedArgumentKind(first.type)
                || first.resourceKind != expectedResourceKind(first.type)) {
                return fail("RHI-METAL-DESCRIPTOR-ARRAY-ELEMENT-MISSING");
            }
            switch (element->argumentKind) {
            case MetalRhiArgumentKind::Buffer: {
                const auto buffer = nativeBuffer(element->buffer);
                if (buffer == nil) {
                    return fail("RHI-METAL-DESCRIPTOR-ARRAY-BUFFER-MISSING");
                }
                if (element->dynamicOffsetIndex != UINT32_MAX) {
                    if (!dynamicOffsets || element->dynamicOffsetIndex >= dynamicOffsetCount) {
                        return fail("RHI-METAL-DESCRIPTOR-DYNAMIC-OFFSET-MISSING");
                    }
                    const auto offset = dynamicOffsets[element->dynamicOffsetIndex];
                    if ((offset & 3u) != 0u || offset >= buffer.length || !fitsNSUInteger(offset)) {
                        return fail("RHI-METAL-DESCRIPTOR-DYNAMIC-OFFSET-OUT-OF-RANGE");
                    }
                }
                break;
            }
            case MetalRhiArgumentKind::Texture:
                if (nativeTexture(element->textureView) == nil) {
                    return fail("RHI-METAL-DESCRIPTOR-ARRAY-TEXTURE-MISSING");
                }
                if (element->dynamicOffsetIndex != UINT32_MAX) {
                    return fail("RHI-METAL-DESCRIPTOR-DYNAMIC-OFFSET-TYPE-INVALID");
                }
                break;
            case MetalRhiArgumentKind::Sampler:
                if (nativeSampler(element->sampler) == nil) {
                    return fail("RHI-METAL-DESCRIPTOR-ARRAY-SAMPLER-MISSING");
                }
                if (element->dynamicOffsetIndex != UINT32_MAX) {
                    return fail("RHI-METAL-DESCRIPTOR-DYNAMIC-OFFSET-TYPE-INVALID");
                }
                break;
            }
        }
        const auto prepareStage = [&](id<MTLFunction> function, rhi::ShaderStage stage) {
            if (!hasStage(pipeline->nativeBindingStages.stages(
                              rhi::DescriptorType::UniformBuffer, mapped.argumentBufferSlot)
                        & stages,
                    stage))
                return true;
            if (findCachedArgumentBuffer(first, mapped, stage) != cachedArgumentBuffers_.end())
                return true;
            preparedEncodings.push_back({&first, &mapped, {}});
            if (!prepareArgumentEncoding(function,
                    mapped.argumentBufferSlot,
                    stage,
                    preparedEncodings.back().encoding,
                    diagnostics)) {
                cleanupPrepared();
                return false;
            }
            return true;
        };
        if (renderEncoder != nil) {
            return prepareStage(reinterpret_cast<id<MTLFunction>>(pipeline->vertexFunction.get()),
                       rhi::ShaderStage::Vertex)
                && prepareStage(reinterpret_cast<id<MTLFunction>>(pipeline->fragmentFunction.get()),
                    rhi::ShaderStage::Fragment);
        }
        return prepareStage(reinterpret_cast<id<MTLFunction>>(pipeline->computeFunction.get()),
            rhi::ShaderStage::Compute);
    };
    for (const auto& binding : descriptorSet->bindings) {
        if (binding.arrayCount > 1u) {
            if (binding.arrayElement != 0u)
                continue;
            const auto mapped = std::find_if(pipeline->metalArgumentBuffer.begin(),
                pipeline->metalArgumentBuffer.end(),
                [&](const auto& candidate) {
                    return candidate.set == set && candidate.binding == binding.binding
                        && candidate.active && candidate.type == binding.type
                        && candidate.capacity == binding.arrayCount;
                });
            const auto mappedCount = std::count_if(pipeline->metalArgumentBuffer.begin(),
                pipeline->metalArgumentBuffer.end(),
                [&](const auto& candidate) {
                    return candidate.set == set && candidate.binding == binding.binding
                        && candidate.active && candidate.type == binding.type
                        && candidate.capacity == binding.arrayCount;
                });
            if (mapped == pipeline->metalArgumentBuffer.end() || mappedCount != 1u) {
                return fail("RHI-METAL-DESCRIPTOR-ARRAY-MISSING-ARGUMENT-MAPPING");
            }
            if (!preflightArray(binding, *mapped))
                return false;
            continue;
        }
        if (binding.type == rhi::DescriptorType::InputAttachment) {
            if (binding.argumentKind != MetalRhiArgumentKind::Texture
                || binding.resourceKind != rhi::DescriptorResourceKind::TextureView) {
                return fail("RHI-METAL-FRAMEBUFFER-FETCH-PAYLOAD-KIND-MISMATCH");
            }
            const auto fetch = std::find_if(pipeline->metalFramebufferFetch.begin(),
                pipeline->metalFramebufferFetch.end(),
                [&](const auto& candidate) {
                    return candidate.set == set && candidate.binding == binding.binding
                        && candidate.active && candidate.depth == binding.depthTexture;
                });
            if (fetch == pipeline->metalFramebufferFetch.end()) {
                return fail("RHI-METAL-FRAMEBUFFER-FETCH-MISSING-PHYSICAL-BINDING");
            }
            continue;
        }
        if (binding.argumentKind != expectedArgumentKind(binding.type)
            || binding.resourceKind != expectedResourceKind(binding.type)) {
            return fail("RHI-METAL-DESCRIPTOR-ARGUMENT-KIND-MISMATCH");
        }
        const auto physical = std::find_if(pipeline->metalBindingSpace.begin(),
            pipeline->metalBindingSpace.end(),
            [&](const MetalRhiNativeBindingSlot& candidate) {
                return candidate.set == set && candidate.binding == binding.binding
                    && candidate.active;
            });
        if (physical == pipeline->metalBindingSpace.end() || physical->type != binding.type
            || physical->metalSlot == UINT32_MAX
            || !rhi::semantics::validMetalDirectBindingSlot(binding.type, physical->metalSlot)) {
            return fail("RHI-METAL-DESCRIPTOR-MISSING-PHYSICAL-BINDING");
        }
        switch (binding.argumentKind) {
        case MetalRhiArgumentKind::Buffer: {
            const auto buffer = nativeBuffer(binding.buffer);
            if (buffer == nil) {
                return fail("RHI-METAL-DESCRIPTOR-BUFFER-PAYLOAD-MISSING");
            }
            if (originalSynchronous
                && (!binding.bufferRequiredBytes || !binding.bufferOffsetAlignment
                    || binding.bufferOffsetBytes % binding.bufferOffsetAlignment
                    || !fitsNSUInteger(binding.bufferOffsetBytes)
                    || binding.bufferOffsetBytes > buffer.length
                    || binding.bufferRequiredBytes > buffer.length - binding.bufferOffsetBytes)) {
                return fail("RHI-METAL-ORDINARY-DESCRIPTOR-BUFFER-RANGE");
            }
            if (binding.dynamicOffsetIndex != UINT32_MAX) {
                if (!dynamicOffsets || binding.dynamicOffsetIndex >= dynamicOffsetCount) {
                    return fail("RHI-METAL-DESCRIPTOR-DYNAMIC-OFFSET-MISSING");
                }
                const auto offset = dynamicOffsets[binding.dynamicOffsetIndex];
                if ((offset & 3u) != 0u || offset >= buffer.length || !fitsNSUInteger(offset)) {
                    return fail("RHI-METAL-DESCRIPTOR-DYNAMIC-OFFSET-OUT-OF-RANGE");
                }
            }
            break;
        }
        case MetalRhiArgumentKind::Texture:
            if (nativeTexture(binding.textureView) == nil) {
                return fail("RHI-METAL-DESCRIPTOR-TEXTURE-PAYLOAD-MISSING");
            }
            if (binding.dynamicOffsetIndex != UINT32_MAX) {
                return fail("RHI-METAL-DESCRIPTOR-DYNAMIC-OFFSET-TYPE-INVALID");
            }
            break;
        case MetalRhiArgumentKind::Sampler:
            if (nativeSampler(binding.sampler) == nil) {
                return fail("RHI-METAL-DESCRIPTOR-SAMPLER-PAYLOAD-MISSING");
            }
            if (binding.dynamicOffsetIndex != UINT32_MAX) {
                return fail("RHI-METAL-DESCRIPTOR-DYNAMIC-OFFSET-TYPE-INVALID");
            }
            break;
        }
    }
    auto encodeDescriptorArray = [&](const MetalRhiNativeArgumentBufferSlot& mapped,
                                     const MetalRhiNativeArgumentBindingPayload& first,
                                     rhi::ShaderStage stage) {
        if (!hasStage(pipeline->nativeBindingStages.stages(
                          rhi::DescriptorType::UniformBuffer, mapped.argumentBufferSlot)
                    & stages,
                stage)) {
            return;
        }
        // Argument buffers retain references, but their indirect resources
        // must also be declared resident on each encoder that can access them.
        for (std::uint32_t arrayElement = 0u; arrayElement < first.arrayCount; ++arrayElement) {
            const auto* element = findArrayElement(first.binding, arrayElement);
            id<MTLResource> resource = nil;
            if (element->buffer)
                resource = nativeBuffer(element->buffer);
            else if (element->textureView)
                resource = nativeTexture(element->textureView);
            if (resource == nil)
                continue;
            MTLResourceUsage usage = MTLResourceUsageRead;
            switch (metalRhiArgumentResourceUse(element->type)) {
            case MetalRhiArgumentResourceUse::ReadSample:
                usage = MTLResourceUsageRead | MTLResourceUsageSample;
                break;
            case MetalRhiArgumentResourceUse::ReadWrite:
                usage = MTLResourceUsageRead | MTLResourceUsageWrite;
                break;
            case MetalRhiArgumentResourceUse::Read:
                break;
            case MetalRhiArgumentResourceUse::None:
                continue;
            }
            if (renderEncoder != nil) {
                const MTLRenderStages renderStage = stage == rhi::ShaderStage::Vertex
                    ? MTLRenderStageVertex
                    : MTLRenderStageFragment;
                [renderEncoder useResource:resource usage:usage stages:renderStage];
            } else {
                [computeEncoder useResource:resource usage:usage];
            }
        }
        const auto cached = findCachedArgumentBuffer(first, mapped, stage);
        if (cached != cachedArgumentBuffers_.end()) {
            bindArgumentBuffer(reinterpret_cast<id<MTLBuffer>>(cached->argumentBuffer.get()),
                mapped.argumentBufferSlot,
                stage);
            ++stats_->nativeArgumentBufferReuseCount;
            return;
        }
        auto& preparedEncoding = *std::find_if(
            preparedEncodings.begin(), preparedEncodings.end(), [&](const auto& candidate) {
                return candidate.first == &first && candidate.mapped == &mapped
                    && candidate.encoding.stage == stage;
            });
        id<MTLArgumentEncoder> encoder = preparedEncoding.encoding.encoder;
        id<MTLBuffer> argumentBuffer = preparedEncoding.encoding.argumentBuffer;
        [encoder setArgumentBuffer:argumentBuffer offset:0u];
        for (std::uint32_t arrayElement = 0; arrayElement < first.arrayCount; ++arrayElement) {
            const auto* element = findArrayElement(first.binding, arrayElement);
            const auto argumentIndex = mapped.argumentIndex + arrayElement;
            switch (element->argumentKind) {
            case MetalRhiArgumentKind::Buffer: {
                const auto buffer = nativeBuffer(element->buffer);
                std::uint64_t offset = 0u;
                if (element->dynamicOffsetIndex != UINT32_MAX) {
                    offset = dynamicOffsets[element->dynamicOffsetIndex];
                }
                [encoder setBuffer:buffer
                            offset:static_cast<NSUInteger>(offset)
                           atIndex:argumentIndex];
                break;
            }
            case MetalRhiArgumentKind::Texture: {
                const auto texture = nativeTexture(element->textureView);
                [encoder setTexture:texture atIndex:argumentIndex];
                break;
            }
            case MetalRhiArgumentKind::Sampler: {
                const auto sampler = nativeSampler(element->sampler);
                [encoder setSamplerState:sampler atIndex:argumentIndex];
                break;
            }
            }
        }
        const auto retained = retainNativeObject(argumentBuffer);
        retainedArgumentBuffers_.push_back(retained);
        if (dynamicOffsetCount == 0u) {
            cachedArgumentBuffers_.push_back({pipeline,
                descriptorSet,
                retained,
                set,
                first.binding,
                mapped.argumentBufferSlot,
                stage});
        }
        ++stats_->nativeArgumentBufferAllocationCount;
        bindArgumentBuffer(argumentBuffer, mapped.argumentBufferSlot, stage);
        // retainNativeObject takes ownership of the +1 returned by
        // newBufferWithLength; keep that ownership in retainedArgumentBuffers_.
        [encoder release];
        preparedEncoding.encoding.encoder = nil;
        preparedEncoding.encoding.argumentBuffer = nil;
    };
    for (const auto& binding : descriptorSet->bindings) {
        if (binding.arrayCount > 1u) {
            if (binding.arrayElement != 0u) {
                continue;
            }
            const auto mapped = std::find_if(pipeline->metalArgumentBuffer.begin(),
                pipeline->metalArgumentBuffer.end(),
                [&](const auto& candidate) {
                    return candidate.set == set && candidate.binding == binding.binding
                        && candidate.active && candidate.type == binding.type
                        && candidate.capacity == binding.arrayCount;
                });
            if (renderEncoder != nil) {
                encodeDescriptorArray(*mapped, binding, rhi::ShaderStage::Vertex);
                encodeDescriptorArray(*mapped, binding, rhi::ShaderStage::Fragment);
            } else {
                encodeDescriptorArray(*mapped, binding, rhi::ShaderStage::Compute);
            }
            continue;
        }
        if (binding.type == rhi::DescriptorType::InputAttachment) {
            // Framebuffer fetch is a fragment input produced by the active
            // render attachment. It is deliberately not rebound as a sampled
            // texture; doing so would violate the native lowering contract.
            continue;
        }
        const auto physical = std::find_if(pipeline->metalBindingSpace.begin(),
            pipeline->metalBindingSpace.end(),
            [&](const MetalRhiNativeBindingSlot& candidate) {
                return candidate.set == set && candidate.binding == binding.binding
                    && candidate.active;
            });
        const auto metalSlot = physical->metalSlot;
        switch (binding.argumentKind) {
        case MetalRhiArgumentKind::Buffer: {
            id<MTLBuffer> buffer = nativeBuffer(binding.buffer);
            const auto offset = originalSynchronous ? binding.bufferOffsetBytes
                : binding.dynamicOffsetIndex == UINT32_MAX
                ? 0u
                : dynamicOffsets[binding.dynamicOffsetIndex];
            const auto visibleStages =
                pipeline->nativeBindingStages.stages(binding.type, metalSlot) & stages;
            if (hasStage(visibleStages, rhi::ShaderStage::Vertex) && renderEncoder != nil) {
                [renderEncoder setVertexBuffer:buffer offset:offset atIndex:metalSlot];
            }
            if (hasStage(visibleStages, rhi::ShaderStage::Fragment) && renderEncoder != nil) {
                [renderEncoder setFragmentBuffer:buffer offset:offset atIndex:metalSlot];
            }
            if (hasStage(visibleStages, rhi::ShaderStage::Compute) && computeEncoder != nil) {
                [computeEncoder setBuffer:buffer offset:offset atIndex:metalSlot];
            }
            break;
        }
        case MetalRhiArgumentKind::Texture: {
            id<MTLTexture> texture = nativeTexture(binding.textureView);
            const auto visibleStages =
                pipeline->nativeBindingStages.stages(binding.type, metalSlot) & stages;
            if (hasStage(visibleStages, rhi::ShaderStage::Vertex) && renderEncoder != nil) {
                [renderEncoder setVertexTexture:texture atIndex:metalSlot];
            }
            if (hasStage(visibleStages, rhi::ShaderStage::Fragment) && renderEncoder != nil) {
                [renderEncoder setFragmentTexture:texture atIndex:metalSlot];
            }
            if (hasStage(visibleStages, rhi::ShaderStage::Compute) && computeEncoder != nil) {
                [computeEncoder setTexture:texture atIndex:metalSlot];
            }
            break;
        }
        case MetalRhiArgumentKind::Sampler: {
            id<MTLSamplerState> sampler = nativeSampler(binding.sampler);
            const auto visibleStages =
                pipeline->nativeBindingStages.stages(binding.type, metalSlot) & stages;
            if (hasStage(visibleStages, rhi::ShaderStage::Vertex) && renderEncoder != nil) {
                [renderEncoder setVertexSamplerState:sampler atIndex:metalSlot];
            }
            if (hasStage(visibleStages, rhi::ShaderStage::Fragment) && renderEncoder != nil) {
                [renderEncoder setFragmentSamplerState:sampler atIndex:metalSlot];
            }
            if (hasStage(visibleStages, rhi::ShaderStage::Compute) && computeEncoder != nil) {
                [computeEncoder setSamplerState:sampler atIndex:metalSlot];
            }
            break;
        }
        }
    }
    for (const auto& binding : descriptorSet->bindings) {
        retainResourcePayload(
            retainedResourcePayloads_, retainedResourcePayloadIdentities_, binding.buffer);
        retainResourcePayload(
            retainedResourcePayloads_, retainedResourcePayloadIdentities_, binding.textureView);
        retainResourcePayload(
            retainedResourcePayloads_, retainedResourcePayloadIdentities_, binding.sampler);
    }
    stats_->nativeDescriptorArgumentBound = true;
    return true;
}

bool MetalRhiNativeCommandPayload::recordDescriptorTableBind(std::uint32_t set,
    const std::shared_ptr<MetalRhiNativeDescriptorTablePayload>& descriptorTable,
    rhi::ShaderStage stages,
    std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    if (activeGraphicsBinding_) {
        diagnostics = "RHI-METAL-GRAPHICS-SCOPE-BINDING-MISMATCH";
        return false;
    }
    auto* renderEncoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    auto* computeEncoder =
        reinterpret_cast<id<MTLComputeCommandEncoder>>(nativeComputeEncoder_.get());
    const auto pipeline = renderPipeline_ ? renderPipeline_ : computePipeline_;
    if (!descriptorTable || !descriptorTable->nativeRuntimeBacked || !pipeline
        || !commandDeviceMatches(nativeDevice_, descriptorTable->nativeDevice)
        || (renderEncoder == nil && computeEncoder == nil)) {
        diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-EXECUTABLE-PAYLOAD-MISSING";
        return false;
    }
    for (const auto& entry : descriptorTable->entries) {
        const auto entryDevice = entry.buffer ? entry.buffer->resource.nativeDevice
            : entry.textureView               ? entry.textureView->resource.nativeDevice
            : entry.sampler                   ? entry.sampler->resource.nativeDevice
                                              : nullptr;
        if (entryDevice && !commandDeviceMatches(nativeDevice_, entryDevice)) {
            diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-DEVICE-MISMATCH";
            return false;
        }
    }
    const auto effectiveStages = stages & descriptorTable->desc.stages;
    if (effectiveStages == rhi::ShaderStage::None) {
        diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-STAGE-VISIBILITY-MISMATCH";
        return false;
    }
    const auto mapped = std::find_if(pipeline->metalArgumentBuffer.begin(),
        pipeline->metalArgumentBuffer.end(),
        [&](const auto& candidate) {
            return candidate.set == set && candidate.active
                && candidate.type == descriptorTable->desc.type
                && candidate.capacity == descriptorTable->desc.capacity;
        });
    const auto mappedCount = std::count_if(pipeline->metalArgumentBuffer.begin(),
        pipeline->metalArgumentBuffer.end(),
        [&](const auto& candidate) {
            return candidate.set == set && candidate.active
                && candidate.type == descriptorTable->desc.type
                && candidate.capacity == descriptorTable->desc.capacity;
        });
    if (mapped == pipeline->metalArgumentBuffer.end()) {
        diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-MISSING-ARGUMENT-BUFFER-MAPPING";
        return false;
    }
    if (mappedCount != 1u) {
        diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-AMBIGUOUS-ARGUMENT-BUFFER-MAPPING";
        return false;
    }
    if (mapped->argumentBufferSlot >= 31u || mapped->argumentIndex == UINT32_MAX
        || mapped->capacity != descriptorTable->desc.capacity || mapped->capacity == 0u
        || mapped->capacity - 1u > UINT32_MAX - mapped->argumentIndex) {
        diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-ARGUMENT-MAPPING-INVALID";
        return false;
    }
    if (descriptorTable->desc.label.empty() || descriptorTable->desc.capacity == 0u
        || descriptorTable->desc.type == rhi::DescriptorType::InputAttachment
        || descriptorTable->entries.size() != descriptorTable->desc.capacity) {
        diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-ABI-INCOMPLETE";
        return false;
    }
    if (renderEncoder != nil
        && (hasStage(effectiveStages, rhi::ShaderStage::Compute)
            || (!hasStage(effectiveStages, rhi::ShaderStage::Vertex)
                && !hasStage(effectiveStages, rhi::ShaderStage::Fragment)))) {
        diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-STAGE-ENCODER-MISMATCH";
        return false;
    }
    if (computeEncoder != nil && !hasStage(effectiveStages, rhi::ShaderStage::Compute)) {
        diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-STAGE-ENCODER-MISMATCH";
        return false;
    }

    const auto expectedResourceKind = [](rhi::DescriptorType type) {
        switch (type) {
        case rhi::DescriptorType::UniformBuffer:
        case rhi::DescriptorType::StorageBuffer:
        case rhi::DescriptorType::ReadOnlyStorageBuffer:
            return rhi::DescriptorResourceKind::Buffer;
        case rhi::DescriptorType::SampledTexture:
        case rhi::DescriptorType::StorageTexture:
            return rhi::DescriptorResourceKind::TextureView;
        case rhi::DescriptorType::Sampler:
            return rhi::DescriptorResourceKind::Sampler;
        case rhi::DescriptorType::InputAttachment:
            return rhi::DescriptorResourceKind::TextureView;
        }
        return rhi::DescriptorResourceKind::TextureView;
    };
    for (std::size_t entryIndex = 0u; entryIndex < descriptorTable->entries.size(); ++entryIndex) {
        const auto& entry = descriptorTable->entries[entryIndex];
        if (entry.index != entryIndex || entry.index >= mapped->capacity
            || entry.type != mapped->type || entry.type != descriptorTable->desc.type
            || entry.resourceKind != expectedResourceKind(entry.type)) {
            diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-ENTRY-INVALID";
            return false;
        }
        const auto entryDevice = entry.buffer ? entry.buffer->resource.nativeDevice
            : entry.textureView               ? entry.textureView->resource.nativeDevice
            : entry.sampler                   ? entry.sampler->resource.nativeDevice
                                              : nullptr;
        if (!entryDevice || !commandDeviceMatches(nativeDevice_, entryDevice)) {
            diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-DEVICE-MISMATCH";
            return false;
        }
        switch (entry.resourceKind) {
        case rhi::DescriptorResourceKind::Buffer:
            if (nativeBuffer(entry.buffer) == nil) {
                diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-BUFFER-MISSING";
                return false;
            }
            break;
        case rhi::DescriptorResourceKind::TextureView:
            if (nativeTexture(entry.textureView) == nil) {
                diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-TEXTURE-MISSING";
                return false;
            }
            break;
        case rhi::DescriptorResourceKind::Sampler:
            if (nativeSampler(entry.sampler) == nil) {
                diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-SAMPLER-MISSING";
                return false;
            }
            break;
        }
    }

    // Prepare every encoder and argument buffer before touching a native
    // encoder. The commit phase below has no expected failure points after
    // this validation, so a rejected table cannot leave partial bindings.
    core::pmr::Vector<MetalRhiPreparedArgumentEncoding> preparedEncodings{
        &core::defaultCpuMemoryResource()};
    const auto findCachedArgumentBuffer = [&](rhi::ShaderStage stage) {
        return std::find_if(cachedArgumentBuffers_.begin(),
            cachedArgumentBuffers_.end(),
            [&](const CachedArgumentBuffer& candidate) {
                const auto* cachedSnapshot =
                    static_cast<const MetalRhiNativeDescriptorTablePayload*>(
                        candidate.descriptorSnapshot.get());
                return cachedSnapshot && candidate.pipeline.get() == pipeline.get()
                    && sameDescriptorTableSnapshot(*cachedSnapshot, *descriptorTable)
                    && candidate.set == set && candidate.binding == mapped->binding
                    && candidate.argumentBufferSlot == mapped->argumentBufferSlot
                    && candidate.stage == stage;
            });
    };
    const auto bindArgumentBuffer = [&](id<MTLBuffer> argumentBuffer, rhi::ShaderStage stage) {
        if (stage == rhi::ShaderStage::Vertex && renderEncoder != nil) {
            [renderEncoder setVertexBuffer:argumentBuffer
                                    offset:0u
                                   atIndex:mapped->argumentBufferSlot];
        } else if (stage == rhi::ShaderStage::Fragment && renderEncoder != nil) {
            [renderEncoder setFragmentBuffer:argumentBuffer
                                      offset:0u
                                     atIndex:mapped->argumentBufferSlot];
        } else if (stage == rhi::ShaderStage::Compute && computeEncoder != nil) {
            [computeEncoder setBuffer:argumentBuffer offset:0u atIndex:mapped->argumentBufferSlot];
        }
    };
    const auto fail = [&](const char* message) {
        releasePreparedArgumentEncodings(preparedEncodings);
        diagnostics = message;
        return false;
    };
    const auto prepareForFunction = [&](id<MTLFunction> function, rhi::ShaderStage stage) -> bool {
        if (!hasStage(pipeline->nativeBindingStages.stages(
                          rhi::DescriptorType::UniformBuffer, mapped->argumentBufferSlot)
                    & stages,
                stage))
            return true;
        if (findCachedArgumentBuffer(stage) != cachedArgumentBuffers_.end())
            return true;
        preparedEncodings.push_back({});
        if (!prepareArgumentEncoding(function,
                mapped->argumentBufferSlot,
                stage,
                preparedEncodings.back(),
                diagnostics)) {
            releasePreparedArgumentEncodings(preparedEncodings);
            return false;
        }
        return true;
    };
    if (renderEncoder != nil) {
        if (!prepareForFunction(reinterpret_cast<id<MTLFunction>>(pipeline->vertexFunction.get()),
                rhi::ShaderStage::Vertex)
            || !prepareForFunction(
                reinterpret_cast<id<MTLFunction>>(pipeline->fragmentFunction.get()),
                rhi::ShaderStage::Fragment)) {
            releasePreparedArgumentEncodings(preparedEncodings);
            return false;
        }
    } else if (!prepareForFunction(
                   reinterpret_cast<id<MTLFunction>>(pipeline->computeFunction.get()),
                   rhi::ShaderStage::Compute)) {
        releasePreparedArgumentEncodings(preparedEncodings);
        return false;
    }
    auto encodeForFunction = [&](rhi::ShaderStage stage) {
        if (!hasStage(effectiveStages, stage))
            return;
        for (const auto& entry : descriptorTable->entries) {
            id<MTLResource> resource = nil;
            if (entry.buffer)
                resource = nativeBuffer(entry.buffer);
            else if (entry.textureView)
                resource = nativeTexture(entry.textureView);
            if (resource == nil)
                continue;
            MTLResourceUsage usage = MTLResourceUsageRead;
            switch (metalRhiArgumentResourceUse(entry.type)) {
            case MetalRhiArgumentResourceUse::ReadSample:
                usage = MTLResourceUsageRead | MTLResourceUsageSample;
                break;
            case MetalRhiArgumentResourceUse::ReadWrite:
                usage = MTLResourceUsageRead | MTLResourceUsageWrite;
                break;
            case MetalRhiArgumentResourceUse::Read:
                break;
            case MetalRhiArgumentResourceUse::None:
                continue;
            }
            if (renderEncoder != nil) {
                const MTLRenderStages renderStage = stage == rhi::ShaderStage::Vertex
                    ? MTLRenderStageVertex
                    : MTLRenderStageFragment;
                [renderEncoder useResource:resource usage:usage stages:renderStage];
            } else {
                [computeEncoder useResource:resource usage:usage];
            }
        }
        const auto cached = findCachedArgumentBuffer(stage);
        if (cached != cachedArgumentBuffers_.end()) {
            bindArgumentBuffer(
                reinterpret_cast<id<MTLBuffer>>(cached->argumentBuffer.get()), stage);
            ++stats_->nativeArgumentBufferReuseCount;
            return;
        }
        auto prepared = std::find_if(
            preparedEncodings.begin(), preparedEncodings.end(), [&](const auto& candidate) {
                return candidate.stage == stage;
            });
        id<MTLArgumentEncoder> encoder = prepared->encoder;
        id<MTLBuffer> argumentBuffer = prepared->argumentBuffer;
        [encoder setArgumentBuffer:argumentBuffer offset:0u];
        for (const auto& entry : descriptorTable->entries) {
            const auto index = mapped->argumentIndex + entry.index;
            switch (entry.resourceKind) {
            case rhi::DescriptorResourceKind::Buffer:
                [encoder setBuffer:nativeBuffer(entry.buffer) offset:0u atIndex:index];
                break;
            case rhi::DescriptorResourceKind::TextureView:
                [encoder setTexture:nativeTexture(entry.textureView) atIndex:index];
                break;
            case rhi::DescriptorResourceKind::Sampler:
                [encoder setSamplerState:nativeSampler(entry.sampler) atIndex:index];
                break;
            }
        }
        const auto retained = retainNativeObject(argumentBuffer);
        retainedArgumentBuffers_.push_back(retained);
        cachedArgumentBuffers_.push_back({pipeline,
            descriptorTable,
            retained,
            set,
            mapped->binding,
            mapped->argumentBufferSlot,
            stage});
        ++stats_->nativeArgumentBufferAllocationCount;
        bindArgumentBuffer(argumentBuffer, stage);
        // retainNativeObject takes ownership of the +1 returned by
        // newBufferWithLength; keep that ownership in retainedArgumentBuffers_.
        [encoder release];
        prepared->encoder = nil;
        prepared->argumentBuffer = nil;
    };

    if (renderEncoder != nil) {
        encodeForFunction(rhi::ShaderStage::Vertex);
        encodeForFunction(rhi::ShaderStage::Fragment);
    } else {
        encodeForFunction(rhi::ShaderStage::Compute);
    }
    for (const auto& entry : descriptorTable->entries) {
        retainResourcePayload(
            retainedResourcePayloads_, retainedResourcePayloadIdentities_, entry.buffer);
        retainResourcePayload(
            retainedResourcePayloads_, retainedResourcePayloadIdentities_, entry.textureView);
        retainResourcePayload(
            retainedResourcePayloads_, retainedResourcePayloadIdentities_, entry.sampler);
    }
    stats_->nativeDescriptorArgumentBound = true;
    return true;
}

bool MetalRhiNativeCommandPayload::recordPushConstants(rhi::ShaderStage stages,
    std::uint32_t offsetBytes,
    std::uint32_t sizeBytes,
    const void* data,
    std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    // Native setBytes lowering uses the reflected physical Metal buffer slot.
    //
    if (offsetBytes != 0 || sizeBytes == 0 || data == nullptr) {
        diagnostics = rhi::semantics::PushConstantDiagCode::kNativeRange;
        diagnostics += "Metal push constants require an atomic full block";
        return false;
    }
    auto* renderEncoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    auto* computeEncoder =
        reinterpret_cast<id<MTLComputeCommandEncoder>>(nativeComputeEncoder_.get());
    if ((hasStage(stages, rhi::ShaderStage::Vertex) || hasStage(stages, rhi::ShaderStage::Fragment))
        && renderEncoder == nil) {
        diagnostics = rhi::semantics::PushConstantDiagCode::kNativeEncoder;
        diagnostics += "Metal render push constants require a render encoder";
        return false;
    }
    if (hasStage(stages, rhi::ShaderStage::Compute) && computeEncoder == nil) {
        diagnostics = rhi::semantics::PushConstantDiagCode::kNativeEncoder;
        diagnostics += "Metal compute push constants require a compute encoder";
        return false;
    }
    const auto pipeline = renderPipeline_ ? renderPipeline_ : computePipeline_;
    if (!pipeline || pipeline->pushConstantMetalBufferIndex == UINT32_MAX) {
        diagnostics = rhi::semantics::PushConstantDiagCode::kNativeBinding;
        diagnostics += "Metal push constants are missing reflected buffer index";
        return false;
    }
    const auto bufferIndex = pipeline->pushConstantMetalBufferIndex;
    if (bufferIndex >= 31u) {
        diagnostics = rhi::semantics::PushConstantDiagCode::kNativeSlot;
        diagnostics += "Metal push constants use an out-of-range buffer slot";
        return false;
    }
    if (hasStage(stages, rhi::ShaderStage::Vertex)) {
        [renderEncoder setVertexBytes:data length:sizeBytes atIndex:bufferIndex];
    }
    if (hasStage(stages, rhi::ShaderStage::Fragment)) {
        [renderEncoder setFragmentBytes:data length:sizeBytes atIndex:bufferIndex];
    }
    if (hasStage(stages, rhi::ShaderStage::Compute)) {
        [computeEncoder setBytes:data length:sizeBytes atIndex:bufferIndex];
    }
    stats_->nativePushConstantUploadBufferWritten = false;
    stats_->nativePushConstantSetBytesWritten = true;
    stats_->nativePushConstantUploadBytes = sizeBytes;
    return true;
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedIndexedDraw(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedIndexedDraw>& draw,
    core::pmr::String& diagnostic)
{
    if (ordinaryWriteStates_) {
        diagnostic = "RHI-METAL-ORDINARY-WRITE-DRAW-DOMAIN";
        return false;
    }
    if (!retentionRecordingAllows(true, diagnostic))
        return false;
    if (!recordingRetention_ || !draw || !ordinaryGraphicsContract_ || !ordinaryGraphicsPass_
        || !nativeRenderEncoder_
        || draw->index()->provider() != ordinaryGraphicsContract_->provider()
        || draw->index()->registry() != resourceRegistry_
        || draw->index()->quota() != recordingRetention_->quota()) {
        diagnostic = "RHI-METAL-ORDINARY-INDEX-DRAW-RECORDING-MISMATCH";
        return false;
    }
    if (const auto* id = draw->validate()) {
        diagnostic = id;
        return false;
    }
    const auto& range = draw->range();
    if (!validateOrdinaryGraphicsDraw(diagnostic,
            range.vertexCount,
            range.firstVertex,
            draw->instances(),
            draw->firstInstance()))
        return false;
    const auto index = nativeBuffer(draw->index()->buffer());
    if (!index
        || !commandDeviceMatches(nativeDevice_, draw->index()->buffer()->resource.nativeDevice)
        || !fitsNSUInteger(draw->offset())
        || index.length != draw->index()->content().bytes().size()) {
        diagnostic = "RHI-METAL-ORDINARY-INDEX-NATIVE-EXTENT-MISMATCH";
        return false;
    }
    const auto ownerAt = [&](std::size_t i) noexcept -> std::shared_ptr<const void> {
        return !i ? std::shared_ptr<const void>(draw) : draw->childOwnerAt(i - 1u);
    };
    if (const auto* id =
            recordingRetention_->retainResourceProjection(draw->childOwnerCount() + 1u, ownerAt)) {
        diagnostic = id;
        return false;
    }
    const auto encoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    const auto format = draw->index()->content().format() == rhi::IndexFormat::Uint16
        ? MTLIndexTypeUInt16
        : MTLIndexTypeUInt32;
    [encoder drawIndexedPrimitives:static_cast<MTLPrimitiveType>(renderPipeline_->primitiveType)
                        indexCount:range.indexCount
                         indexType:format
                       indexBuffer:index
                 indexBufferOffset:draw->offset()
                     instanceCount:draw->instances()
                        baseVertex:range.baseVertex
                      baseInstance:draw->firstInstance()];
    stats_->nativeIndexedDrawEncoded = true;
    ordinaryGraphicsPushState_.onConsume();
    return true;
}

namespace {
// Same existing ten native operations consumed by both fixed producer protocols.
// Callback types remain concrete: recording performs no type-erasure allocation.
class MetalRhiCanonicalProducerNativeEncoding final {
public:
    template <class Barrier, class Consume, class Fail>
    static bool prefix(const MetalRhiZeroBaseIndirectColdBindings& cold,
        const std::array<std::shared_ptr<MetalRhiNativeBufferPayload>, 3u>& outputs,
        const std::shared_ptr<void>& commandBuffer,
        const std::array<std::shared_ptr<core::ExternalObjectSlot>, 3u>& slots,
        std::shared_ptr<void>& blit,
        std::shared_ptr<void>& computeEncoder,
        MetalRhiNativeCommandStats& stats,
        Barrier&& barrier,
        Consume&& consume,
        Fail&& fail)
    {
        using C = rhi::CanonicalZeroBaseIndirectProducerRecordingContract::Command;
        const auto nativeCommand = reinterpret_cast<id<MTLCommandBuffer>>(commandBuffer.get());
        const auto& pipeline = *cold.packet()->pipeline();
        const std::array<rhi::GpuBuffer*, 3u> logical{cold.uploads()[1u].buffer.get(),
            cold.uploads()[2u].buffer.get(),
            cold.faultDomain()->faultLatchBuffer()};
        if (!barrier(*logical[2u],
                rhi::ResourceState::ShaderWrite,
                rhi::ResourceState::CopyDst,
                rhi::PipelineStage::ComputeShader,
                rhi::ResourceAccess::ShaderWrite,
                rhi::PipelineStage::Transfer,
                rhi::ResourceAccess::TransferWrite))
            return false;
        auto clear = [nativeCommand blitCommandEncoder];
        if (!clear)
            return fail("RHI-METAL-CANONICAL-PRODUCER-CLEAR-ENCODER-CREATE");
        [clear retain];
        if (!slots[0u]->adopt(reinterpret_cast<void*>(clear))) {
            [clear endEncoding];
            [clear release];
            return fail("RHI-METAL-CANONICAL-PRODUCER-CLEAR-ENCODER-ADOPTION");
        }
        blit = std::shared_ptr<void>(slots[0u], clear);
        stats.nativeBlitEncoderCreated = true;
        if (!consume(C::ClearBegin))
            return false;
        [clear fillBuffer:nativeBuffer(outputs[2u])
                    range:NSMakeRange(0u, sizeof(std::uint32_t))
                    value:0u];
        if (!consume(C::ClearZero))
            return false;
        [clear endEncoding];
        blit.reset();
        slots[0u]->reset();
        if (!consume(C::ClearEnd))
            return false;
        if (!barrier(*logical[2u],
                rhi::ResourceState::CopyDst,
                rhi::ResourceState::ShaderWrite,
                rhi::PipelineStage::Transfer,
                rhi::ResourceAccess::TransferWrite,
                rhi::PipelineStage::ComputeShader,
                rhi::ResourceAccess::ShaderWrite)
            || !consume(C::ClearDependency))
            return false;
        auto compute = [nativeCommand computeCommandEncoder];
        if (!compute)
            return fail("RHI-METAL-CANONICAL-PRODUCER-COMPUTE-ENCODER-CREATE");
        [compute retain];
        if (!slots[1u]->adopt(reinterpret_cast<void*>(compute))) {
            [compute endEncoding];
            [compute release];
            return fail("RHI-METAL-CANONICAL-PRODUCER-COMPUTE-ENCODER-ADOPTION");
        }
        computeEncoder = std::shared_ptr<void>(slots[1u], compute);
        stats.nativeComputeEncoderCreated = true;
        if (!consume(C::ComputeBegin))
            return false;
        [compute setComputePipelineState:reinterpret_cast<id<MTLComputePipelineState>>(
                                             pipeline.native()->pipelineState.get())];
        for (std::size_t i = 0u; i < cold.bindings().size(); ++i)
            [compute setBuffer:nativeBuffer(cold.resources()[i].buffer)
                        offset:0u
                       atIndex:static_cast<NSUInteger>(cold.bindings()[i].abi.slot)];
        stats.nativeComputePipelineBound = true;
        [compute dispatchThreadgroups:MTLSizeMake(1u, 1u, 1u)
                threadsPerThreadgroup:MTLSizeMake(pipeline.native()->computeWorkgroupSizeX,
                                          pipeline.native()->computeWorkgroupSizeY,
                                          pipeline.native()->computeWorkgroupSizeZ)];
        stats.nativeDispatchEncoded = true;
        if (!consume(C::Dispatch))
            return false;
        [compute endEncoding];
        computeEncoder.reset();
        slots[1u]->reset();
        if (!consume(C::ComputeEnd))
            return false;
        const std::array<C, 3u> dependencies{
            C::ArgumentsDependency, C::MappingDependency, C::FaultDependency};
        for (std::size_t i = 0u; i < logical.size(); ++i)
            if (!barrier(*logical[i],
                    rhi::ResourceState::ShaderWrite,
                    rhi::ResourceState::CopySrc,
                    rhi::PipelineStage::ComputeShader,
                    rhi::ResourceAccess::ShaderWrite,
                    rhi::PipelineStage::Transfer,
                    rhi::ResourceAccess::TransferRead)
                || !consume(dependencies[i]))
                return false;
        return true;
    }
};
} // namespace

bool MetalRhiNativeCommandPayload::recordCanonicalProducer(
    const std::shared_ptr<const MetalRhiCanonicalProducerRecordingPreparation>& root,
    core::pmr::String& diagnostic)
{
    using Contract = rhi::CanonicalZeroBaseIndirectProducerRecordingContract;
    using Sequence = rhi::CanonicalZeroBaseIndirectProducerSequence;
    using C = Contract::Command;
    using Status = Sequence::Status;
    diagnostic.clear();
    if (!canonicalProducerBudget_ || !canonicalProducerLowering_
        || canonicalProducerLowering_->contractVersion != Contract::version
        || !canonicalProducerLowering_->allowsPrototypePayload() || !root
        || !root->sharedOwnerMatches(root) || canonicalProducer_ || canonicalProducerSequence_
        || nativeRenderEncoder_ || nativeComputeEncoder_ || nativeBlitEncoder_ || abandoned_
        || recordingPoisoned_ || submitted_.load(std::memory_order_acquire)
        || stats_->nativeCommandBufferFinished) {
        diagnostic = "RHI-METAL-CANONICAL-PRODUCER-RECORDING-DOMAIN";
        return false;
    }
    const auto command = root->command_.lock();
    if (!command || command.get() != this || !sharedOwnerMatches(command)) {
        diagnostic = "RHI-METAL-CANONICAL-PRODUCER-COMMAND-MISMATCH";
        return false;
    }
    if (const auto* id = root->validate()) {
        diagnostic = id;
        return false;
    }
    if (!core::SharedOwnerProjection::sameOwner(root->states_, canonicalProducerStates_)) {
        diagnostic = "RHI-METAL-CANONICAL-PRODUCER-JOURNAL-MISMATCH";
        return false;
    }
    for (std::size_t i = 0u; i < canonicalProducerSlots_.size(); ++i)
        if (!canonicalProducerSlots_[i] || canonicalProducerSlots_[i]->get()
            || !core::SharedOwnerProjection::sameOwner(
                root->slots_[i], canonicalProducerSlots_[i])) {
            diagnostic = "RHI-METAL-CANONICAL-PRODUCER-ENCODER-SLOT";
            return false;
        }
    const auto& cold = *root->cold_->producer();
    const auto& pipeline = *cold.packet()->pipeline();
    if (!ordinaryComputePipelineValid(*pipeline.native(), nativeDevice_)) {
        diagnostic = "RHI-METAL-CANONICAL-PRODUCER-NATIVE-PIPELINE";
        return false;
    }
    const auto nativeCommand = reinterpret_cast<id<MTLCommandBuffer>>(nativeCommandBuffer_.get());
    if (!nativeCommand || nativeCommand.status != MTLCommandBufferStatusNotEnqueued
        || nativeCommand.commandQueue != reinterpret_cast<id<MTLCommandQueue>>(nativeQueue_.get())
        || nativeCommand.device != reinterpret_cast<id<MTLDevice>>(nativeDevice_.get())) {
        diagnostic = "RHI-METAL-CANONICAL-PRODUCER-NATIVE-COMMAND";
        return false;
    }
    std::array<rhi::GpuBuffer*, 3u> logical{cold.uploads()[1u].buffer.get(),
        cold.uploads()[2u].buffer.get(),
        cold.faultDomain()->faultLatchBuffer()};
    const std::array<std::shared_ptr<MetalRhiNativeBufferPayload>, 3u> outputs{
        cold.resources()[3u].buffer, cold.resources()[4u].buffer, cold.resources()[1u].buffer};
    const std::array<std::shared_ptr<MetalRhiNativeBufferPayload>, 3u> destinations{
        root->resources_[0u].buffer, root->resources_[1u].buffer, cold.readback().buffer};
    for (std::size_t i = 0u; i < logical.size(); ++i) {
        if (!canPrepareMetalRhiRecordedState(*logical[i], *canonicalProducerStates_)) {
            diagnostic = "RHI-RESOURCE-STATE-RECORDING-CONFLICT";
            return false;
        }
        if (!nativeBuffer(outputs[i]) || !nativeBuffer(destinations[i])
            || nativeBuffer(outputs[i]).length != logical[i]->desc().sizeBytes
            || nativeBuffer(destinations[i]).length != logical[i]->desc().sizeBytes) {
            diagnostic = "RHI-METAL-CANONICAL-PRODUCER-NATIVE-EXTENT";
            return false;
        }
#if !TARGET_OS_IPHONE
        if (destinations[i]->resource.storageMode == MetalRhiNativeStorageMode::Managed) {
            if (@available(macOS 10.11, *)) {
            } else {
                diagnostic = "RHI-METAL-MANAGED-READBACK-SYNCHRONIZE-API-UNAVAILABLE";
                return false;
            }
        }
#endif
    }
    for (const auto& binding : cold.bindings())
        if (!fitsNSUInteger(binding.abi.slot)) {
            diagnostic = "RHI-METAL-CANONICAL-PRODUCER-NATIVE-SLOT";
            return false;
        }
    Sequence prospective(*canonicalProducerBudget_, root->facts().identity);
    for (const auto operation : Contract::protocol)
        if (const auto* id = prospective.record(operation, root->facts().identity)) {
            diagnostic = id;
            return false;
        }
    if (const auto* id = prospective.finish()) {
        diagnostic = id;
        return false;
    }
    const auto ownerAt = [&](std::size_t i) noexcept -> std::shared_ptr<const void> {
        return i ? root->childOwnerAt(i - 1u) : root;
    };
    if (const auto* id =
            recordingRetention_->retainResourceProjection(root->childOwnerCount() + 1u, ownerAt)) {
        diagnostic = id;
        return false;
    }
    canonicalProducer_ = root;
    canonicalProducerSequence_.emplace(*canonicalProducerBudget_, root->facts().identity);
    const auto fail = [&](const char* id) {
        diagnostic = id;
        poisonRecording(diagnostic);
        return false;
    };
    const auto consume = [&](C operation) {
        if (const auto* id = canonicalProducerSequence_->record(operation, root->facts().identity))
            return fail(id);
        stats_->canonicalProducerCommandCount = canonicalProducerSequence_->commands();
        return true;
    };
    const auto barrier = [&](rhi::GpuBuffer& buffer,
                             rhi::ResourceState before,
                             rhi::ResourceState after,
                             rhi::PipelineStage sourceStage,
                             rhi::ResourceAccess sourceAccess,
                             rhi::PipelineStage destStage,
                             rhi::ResourceAccess destAccess) {
        rhi::ResourceBarrierDesc intent;
        intent.resource = buffer.id();
        intent.before = before;
        intent.after = after;
        intent.sourceStages = sourceStage;
        intent.sourceAccess = sourceAccess;
        intent.destinationStages = destStage;
        intent.destinationAccess = destAccess;
        const auto result =
            setMetalRhiBufferStateForBarrier(buffer, intent, canonicalProducerStates_.get());
        if (!result.success) {
            diagnostic = result.diagnostics;
            poisonRecording(diagnostic);
            return false;
        }
        stats_->nativeResourceBarrierEncoded = true;
        return true;
    };
    for (auto* buffer : logical)
        if (!prepareMetalRhiRecordedState(*buffer, *canonicalProducerStates_))
            return fail("RHI-RESOURCE-STATE-RECORDING-CONFLICT");
    @autoreleasepool {
        if (!MetalRhiCanonicalProducerNativeEncoding::prefix(cold,
                outputs,
                nativeCommandBuffer_,
                canonicalProducerSlots_,
                nativeBlitEncoder_,
                nativeComputeEncoder_,
                *stats_,
                barrier,
                consume,
                fail))
            return false;
        auto readback = [nativeCommand blitCommandEncoder];
        if (!readback)
            return fail("RHI-METAL-CANONICAL-PRODUCER-READBACK-ENCODER-CREATE");
        [readback retain];
        if (!canonicalProducerSlots_[2u]->adopt(reinterpret_cast<void*>(readback))) {
            [readback endEncoding];
            [readback release];
            return fail("RHI-METAL-CANONICAL-PRODUCER-READBACK-ENCODER-ADOPTION");
        }
        nativeBlitEncoder_ = std::shared_ptr<void>(canonicalProducerSlots_[2u], readback);
        if (!consume(C::ReadbackBegin))
            return false;
        const std::array<C, 3u> copies{C::ArgumentsCopy, C::MappingCopy, C::FaultCopy};
        for (std::size_t i = 0u; i < logical.size(); ++i) {
            [readback copyFromBuffer:nativeBuffer(outputs[i])
                        sourceOffset:0u
                            toBuffer:nativeBuffer(destinations[i])
                   destinationOffset:0u
                                size:static_cast<NSUInteger>(logical[i]->desc().sizeBytes)];
#if !TARGET_OS_IPHONE
            if (destinations[i]->resource.storageMode == MetalRhiNativeStorageMode::Managed)
                [readback synchronizeResource:nativeBuffer(destinations[i])];
#endif
            if (!consume(copies[i]))
                return false;
        }
        stats_->nativeBlitCopyEncoded = true;
        [readback endEncoding];
        nativeBlitEncoder_.reset();
        canonicalProducerSlots_[2u]->reset();
        if (!consume(C::ReadbackEnd))
            return false;
    }
    if (const auto* id =
            canonicalProducerSequence_->transition(Status::Recorded, root->facts().identity))
        return fail(id);
    root->status_.store(Status::Recorded, std::memory_order_release);
    return true;
}

bool MetalRhiNativeCommandPayload::recordClosedConsumer(
    const std::shared_ptr<const MetalRhiClosedConsumerNativePreparation>& root,
    core::pmr::String& diagnostic)
{
    using Contract = rhi::CanonicalClosedConsumerRecordingContract;
    using Sequence = rhi::CanonicalClosedConsumerSequence;
    using C = Contract::Command;
    using Status = Sequence::Status;
    using Slot = MetalRhiClosedConsumerNativePreparation::EncoderSlot;
    diagnostic.clear();
    if (!closedConsumerBudget_ || !closedConsumerLowering_
        || closedConsumerLowering_->contractVersion != Contract::version
        || !closedConsumerLowering_->allowsPrototypePayload() || !root
        || !root->sharedOwnerMatches(root) || closedConsumerNative_ || closedConsumerSequence_
        || !closedConsumerReservation_
        || !core::SharedOwnerProjection::sameOwner(closedConsumerReservation_, root->reservation())
        || abandoned_ || recordingPoisoned_ || submitted_.load(std::memory_order_acquire)
        || stats_->nativeCommandBufferFinished) {
        diagnostic = "RHI-METAL-CLOSED-CONSUMER-RECORDING-DOMAIN";
        return false;
    }
    const auto& admission = root->reservation()->preparation();
    const auto originalCommand = admission->command_.lock();
    if (!originalCommand || originalCommand.get() != this || !sharedOwnerMatches(originalCommand)) {
        diagnostic = "RHI-METAL-CLOSED-CONSUMER-COMMAND-MISMATCH";
        return false;
    }
    if (const auto* id = root->validate()) {
        diagnostic = id;
        return false;
    }
    if (const auto* id = closedConsumerBudget_->validate()) {
        diagnostic = id;
        return false;
    }
    const auto& issuance = admission->issuance();
    const auto& consumer = *issuance->cold();
    const auto& producer = *consumer.producer();
    const auto& geometry = *issuance->images()->geometry();
    const auto& source = *consumer.pipeline()->source();
    const auto& packet = *producer.packet();
    const auto& pipeline = consumer.pipeline()->native();
    if (!ordinaryComputePipelineValid(*producer.packet()->pipeline()->native(), nativeDevice_)
        || !closedConsumerIndirectNative_
        || !validateGraphicsPipelineBind(pipeline, diagnostic, false)) {
        if (diagnostic.empty())
            diagnostic = "RHI-METAL-CLOSED-CONSUMER-NATIVE-PIPELINE";
        return false;
    }
    const auto nativeCommand = reinterpret_cast<id<MTLCommandBuffer>>(nativeCommandBuffer_.get());
    if (!nativeCommand || nativeCommand.status != MTLCommandBufferStatusNotEnqueued
        || nativeCommand.commandQueue != reinterpret_cast<id<MTLCommandQueue>>(nativeQueue_.get())
        || nativeCommand.device != reinterpret_cast<id<MTLDevice>>(nativeDevice_.get())) {
        diagnostic = "RHI-METAL-CLOSED-CONSUMER-NATIVE-COMMAND";
        return false;
    }
    const std::array<rhi::GpuBuffer*, 6u> logical{producer.uploads()[1u].buffer.get(),
        producer.uploads()[2u].buffer.get(),
        producer.faultDomain()->faultLatchBuffer(),
        packet.packet()->physicalArgs(),
        packet.packet()->instanceMappings(),
        consumer.faultDomain()->faultLatchBuffer()};
    const std::array<std::shared_ptr<MetalRhiNativeBufferPayload>, 3u> outputs{
        producer.resources()[3u].buffer,
        producer.resources()[4u].buffer,
        producer.resources()[1u].buffer};
    const std::array<std::shared_ptr<MetalRhiNativeBufferPayload>, 4u> destinations{
        admission->resources()[0u].buffer,
        admission->resources()[1u].buffer,
        producer.readback().buffer,
        consumer.readback().buffer};
    const std::array<std::shared_ptr<MetalRhiNativeBufferPayload>, 6u> nativeLogical{outputs[0u],
        outputs[1u],
        outputs[2u],
        packet.resources()[0u].buffer,
        packet.resources()[1u].buffer,
        consumer.resources()[2u].buffer};
    for (std::size_t i = 0u; i < logical.size(); ++i) {
        if (!canPrepareMetalRhiRecordedState(*logical[i], *root->states())) {
            diagnostic = "RHI-RESOURCE-STATE-RECORDING-CONFLICT";
            return false;
        }
        if (!nativeBuffer(nativeLogical[i])
            || nativeBuffer(nativeLogical[i]).length != logical[i]->desc().sizeBytes) {
            diagnostic = "RHI-METAL-CLOSED-CONSUMER-NATIVE-EXTENT";
            return false;
        }
    }
    for (std::size_t i = 0u; i < destinations.size(); ++i) {
        const auto bytes = i < 2u ? logical[i]->desc().sizeBytes : sizeof(std::uint32_t);
        if (!nativeBuffer(destinations[i]) || nativeBuffer(destinations[i]).length != bytes) {
            diagnostic = "RHI-METAL-CLOSED-CONSUMER-NATIVE-EXTENT";
            return false;
        }
#if !TARGET_OS_IPHONE
        if (destinations[i]->resource.storageMode == MetalRhiNativeStorageMode::Managed) {
            if (@available(macOS 10.11, *)) {
            } else {
                diagnostic = "RHI-METAL-MANAGED-READBACK-SYNCHRONIZE-API-UNAVAILABLE";
                return false;
            }
        }
#endif
    }
    if (!nativeBuffer(geometry.resource().buffer)
        || nativeBuffer(geometry.resource().buffer).length != geometry.buffer()->desc().sizeBytes
        || !nativeBuffer(source.index()->buffer())
        || nativeBuffer(source.index()->buffer()).length
            != source.index()->resource().desc().sizeBytes
        || !fitsNSUInteger(geometry.range().offset)
        || !fitsNSUInteger(geometry.range().fact.slot)) {
        diagnostic = "RHI-METAL-CLOSED-CONSUMER-NATIVE-GEOMETRY";
        return false;
    }
    for (const auto& binding : consumer.buffers())
        if (!fitsNSUInteger(binding.range.fact.slot) || !fitsNSUInteger(binding.range.offset)) {
            diagnostic = "RHI-METAL-CLOSED-CONSUMER-NATIVE-SLOT";
            return false;
        }
    for (const auto& binding : producer.bindings())
        if (!fitsNSUInteger(binding.abi.slot)) {
            diagnostic = "RHI-METAL-CLOSED-CONSUMER-NATIVE-SLOT";
            return false;
        }
    for (std::size_t i = 0u; i < consumer.images().size(); ++i)
        if (!nativeTexture(consumer.images()[i].view().textureView)
            || !fitsNSUInteger(consumer.textures()[i].abi.slot)) {
            diagnostic = "RHI-METAL-CLOSED-CONSUMER-NATIVE-IMAGE";
            return false;
        }
    Sequence prospective(*closedConsumerBudget_, admission->facts().identity);
    for (const auto operation : Contract::protocol)
        if (const auto* id = prospective.record(operation, admission->facts().identity)) {
            diagnostic = id;
            return false;
        }
    if (const auto* id = prospective.finish()) {
        diagnostic = id;
        return false;
    }
    const auto ownerAt = [&](std::size_t i) noexcept -> std::shared_ptr<const void> {
        return i ? root->childOwnerAt(i - 1u) : root;
    };
    if (const auto* id = recordingRetention_->preflightResourceProjection(
            root->childOwnerCount() + 1u, ownerAt)) {
        diagnostic = id;
        return false;
    }
    // All bounded preflight succeeds before owner or logical/native mutation.
    closedConsumerNative_ = root;
    closedConsumerSequence_.emplace(*closedConsumerBudget_, admission->facts().identity);
    const auto fail = [&](const char* id) {
        diagnostic = id;
        poisonRecording(diagnostic);
        return false;
    };
    if (const auto* id =
            recordingRetention_->retainResourceProjection(root->childOwnerCount() + 1u, ownerAt))
        return fail(id);
    const auto consume = [&](C operation) {
        if (const auto* id =
                closedConsumerSequence_->record(operation, admission->facts().identity))
            return fail(id);
        stats_->closedConsumerCommandCount = closedConsumerSequence_->commands();
        return true;
    };
    const auto barrier = [&](rhi::GpuBuffer& buffer,
                             rhi::ResourceState before,
                             rhi::ResourceState after,
                             rhi::PipelineStage sourceStage,
                             rhi::ResourceAccess sourceAccess,
                             rhi::PipelineStage destinationStage,
                             rhi::ResourceAccess destinationAccess) {
        rhi::ResourceBarrierDesc intent;
        intent.resource = buffer.id();
        intent.before = before;
        intent.after = after;
        intent.sourceStages = sourceStage;
        intent.sourceAccess = sourceAccess;
        intent.destinationStages = destinationStage;
        intent.destinationAccess = destinationAccess;
        const auto result = setMetalRhiBufferStateForBarrier(buffer, intent, root->states().get());
        if (!result.success)
            return fail("RHI-METAL-CLOSED-CONSUMER-STATE-TRANSITION");
        stats_->nativeResourceBarrierEncoded = true;
        return true;
    };
    for (auto* buffer : logical)
        if (!prepareMetalRhiRecordedState(*buffer, *root->states()))
            return fail("RHI-RESOURCE-STATE-RECORDING-CONFLICT");
    const auto& slots = root->slots();
    const auto beginBlit = [&](Slot selected) -> id<MTLBlitCommandEncoder> {
        auto encoder = [nativeCommand blitCommandEncoder];
        if (!encoder)
            return nil;
        [encoder retain];
        const auto& slot = slots[static_cast<std::size_t>(selected)];
        if (!slot->adopt(reinterpret_cast<void*>(encoder))) {
            [encoder endEncoding];
            [encoder release];
            return nil;
        }
        nativeBlitEncoder_ = std::shared_ptr<void>(slot, encoder);
        stats_->nativeBlitEncoderCreated = true;
        return encoder;
    };
    const auto endBlit = [&](id<MTLBlitCommandEncoder> encoder, Slot selected) {
        [encoder endEncoding];
        nativeBlitEncoder_.reset();
        slots[static_cast<std::size_t>(selected)]->reset();
    };
    @autoreleasepool {
        const std::array<std::shared_ptr<core::ExternalObjectSlot>, 3u> prefixSlots{
            slots[0u], slots[1u], slots[2u]};
        const auto prefixConsume =
            [&](rhi::CanonicalZeroBaseIndirectProducerRecordingContract::Command operation) {
                const auto index = static_cast<std::size_t>(operation);
                return index < static_cast<std::size_t>(
                           rhi::CanonicalZeroBaseIndirectProducerRecordingContract::Command::
                               ReadbackBegin)
                    ? consume(Contract::protocol[index])
                    : fail("RHI-METAL-CLOSED-CONSUMER-PREFIX-ORDER");
            };
        if (!MetalRhiCanonicalProducerNativeEncoding::prefix(producer,
                outputs,
                nativeCommandBuffer_,
                prefixSlots,
                nativeBlitEncoder_,
                nativeComputeEncoder_,
                *stats_,
                barrier,
                prefixConsume,
                fail))
            return false;
        const std::array<C, 2u> prepareOps{C::PacketArgumentsPrepare, C::PacketMappingPrepare};
        for (std::size_t i = 0u; i < 2u; ++i)
            if (!barrier(*logical[3u + i],
                    rhi::ResourceState::ShaderWrite,
                    rhi::ResourceState::CopyDst,
                    rhi::PipelineStage::ComputeShader,
                    rhi::ResourceAccess::ShaderWrite,
                    rhi::PipelineStage::Transfer,
                    rhi::ResourceAccess::TransferWrite)
                || !consume(prepareOps[i]))
                return false;
        auto copy = beginBlit(Slot::Blit);
        if (!copy)
            return fail("RHI-METAL-CLOSED-CONSUMER-PACKET-ENCODER-CREATE");
        if (!consume(C::PacketCopyBegin))
            return false;
        const std::array<C, 2u> copyOps{C::PacketArgumentsCopy, C::PacketMappingCopy};
        for (std::size_t i = 0u; i < 2u; ++i) {
            [copy copyFromBuffer:nativeBuffer(outputs[i])
                     sourceOffset:0u
                         toBuffer:nativeBuffer(nativeLogical[3u + i])
                destinationOffset:0u
                             size:static_cast<NSUInteger>(logical[i]->desc().sizeBytes)];
            if (!consume(copyOps[i]))
                return false;
        }
        stats_->nativeBlitCopyEncoded = true;
        endBlit(copy, Slot::Blit);
        if (!consume(C::PacketCopyEnd))
            return false;
        if (!barrier(*logical[3u],
                rhi::ResourceState::CopyDst,
                rhi::ResourceState::ShaderRead,
                rhi::PipelineStage::Transfer,
                rhi::ResourceAccess::TransferWrite,
                rhi::PipelineStage::DrawIndirect,
                rhi::ResourceAccess::IndirectCommandRead)
            || !consume(C::PacketArgumentsDependency)
            || !barrier(*logical[4u],
                rhi::ResourceState::CopyDst,
                rhi::ResourceState::ShaderRead,
                rhi::PipelineStage::Transfer,
                rhi::ResourceAccess::TransferWrite,
                rhi::PipelineStage::VertexShader,
                rhi::ResourceAccess::ShaderRead)
            || !consume(C::PacketMappingDependency))
            return false;
        if (!barrier(*logical[5u],
                rhi::ResourceState::ShaderWrite,
                rhi::ResourceState::CopyDst,
                rhi::PipelineStage::FragmentShader,
                rhi::ResourceAccess::ShaderWrite,
                rhi::PipelineStage::Transfer,
                rhi::ResourceAccess::TransferWrite))
            return false;
        auto clear = beginBlit(Slot::Clear);
        if (!clear)
            return fail("RHI-METAL-CLOSED-CONSUMER-CLEAR-ENCODER-CREATE");
        if (!consume(C::ConsumerClearBegin))
            return false;
        [clear fillBuffer:nativeBuffer(nativeLogical[5u])
                    range:NSMakeRange(0u, logical[5u]->desc().sizeBytes)
                    value:0u];
        if (!consume(C::ConsumerClearZero))
            return false;
        endBlit(clear, Slot::Clear);
        if (!consume(C::ConsumerClearEnd))
            return false;
        if (!barrier(*logical[5u],
                rhi::ResourceState::CopyDst,
                rhi::ResourceState::ShaderWrite,
                rhi::PipelineStage::Transfer,
                rhi::ResourceAccess::TransferWrite,
                rhi::PipelineStage::FragmentShader,
                rhi::ResourceAccess::ShaderWrite)
            || !consume(C::ConsumerClearDependency))
            return false;
        auto render = [nativeCommand
            renderCommandEncoderWithDescriptor:reinterpret_cast<MTLRenderPassDescriptor*>(
                                                   root->native().descriptor.get())];
        if (!render)
            return fail("RHI-METAL-CLOSED-CONSUMER-RENDER-ENCODER-CREATE");
        [render retain];
        if (!slots[static_cast<std::size_t>(Slot::Render)]->adopt(
                reinterpret_cast<void*>(render))) {
            [render endEncoding];
            [render release];
            return fail("RHI-METAL-CLOSED-CONSUMER-RENDER-ENCODER-ADOPTION");
        }
        nativeRenderEncoder_ =
            std::shared_ptr<void>(slots[static_cast<std::size_t>(Slot::Render)], render);
        stats_->nativeRenderEncoderCreated = true;
        if (!consume(C::RenderBegin))
            return false;
        if (!recordGraphicsPipelineBindUnchecked(pipeline, diagnostic))
            return fail("RHI-METAL-CLOSED-CONSUMER-PIPELINE-ENCODING");
        if (!consume(C::Pipeline))
            return false;
        for (std::size_t i = 0u; i < consumer.buffers().size(); ++i) {
            const auto& binding = consumer.buffers()[i];
            if (binding.range.stage == rhi::ShaderStage::Vertex)
                [render setVertexBuffer:nativeBuffer(consumer.resources()[i].buffer)
                                 offset:binding.range.offset
                                atIndex:binding.range.fact.slot];
            else
                [render setFragmentBuffer:nativeBuffer(consumer.resources()[i].buffer)
                                   offset:binding.range.offset
                                  atIndex:binding.range.fact.slot];
        }
        for (std::size_t i = 0u; i < consumer.images().size(); ++i)
            [render setFragmentTexture:nativeTexture(consumer.images()[i].view().textureView)
                               atIndex:consumer.textures()[i].abi.slot];
        if (!consume(C::Descriptors))
            return false;
        [render setVertexBuffer:nativeBuffer(geometry.resource().buffer)
                         offset:geometry.range().offset
                        atIndex:geometry.range().fact.slot];
        if (!consume(C::Vertex) || !consume(C::Index))
            return false; // Metal index binding is supplied by its draw call.
        const auto& viewport = root->input().viewport;
        [render setViewport:MTLViewport{viewport.x,
                                viewport.y,
                                viewport.width,
                                viewport.height,
                                viewport.minDepth,
                                viewport.maxDepth}];
        stats_->nativeViewportSet = true;
        ++stats_->nativeViewportSetCount;
        if (!consume(C::Viewport))
            return false;
        const auto& scissor = root->input().scissor;
        [render setScissorRect:MTLScissorRect{static_cast<NSUInteger>(scissor.x),
                                   static_cast<NSUInteger>(scissor.y),
                                   scissor.width,
                                   scissor.height}];
        stats_->nativeScissorSet = true;
        ++stats_->nativeScissorSetCount;
        if (!consume(C::Scissor))
            return false;
        [render drawIndexedPrimitives:static_cast<MTLPrimitiveType>(pipeline->primitiveType)
                            indexType:nativeIndexType(source.index()->content().format())
                          indexBuffer:nativeBuffer(source.index()->buffer())
                    indexBufferOffset:0u
                       indirectBuffer:nativeBuffer(nativeLogical[3u])
                 indirectBufferOffset:0u];
        stats_->nativeIndexedDrawEncoded = true;
        if (!consume(C::Indirect))
            return false;
        [render endEncoding];
        nativeRenderEncoder_.reset();
        slots[static_cast<std::size_t>(Slot::Render)]->reset();
        renderPipeline_.reset();
        if (!consume(C::RenderEnd))
            return false;
        if (!barrier(*logical[5u],
                rhi::ResourceState::ShaderWrite,
                rhi::ResourceState::CopySrc,
                rhi::PipelineStage::FragmentShader,
                rhi::ResourceAccess::ShaderWrite,
                rhi::PipelineStage::Transfer,
                rhi::ResourceAccess::TransferRead)
            || !consume(C::ConsumerFaultDependency))
            return false;
        auto readback = beginBlit(Slot::Blit);
        if (!readback)
            return fail("RHI-METAL-CLOSED-CONSUMER-READBACK-ENCODER-CREATE");
        if (!consume(C::ReadbackBegin))
            return false;
        const std::array<std::shared_ptr<MetalRhiNativeBufferPayload>, 4u> readSources{
            outputs[0u], outputs[1u], outputs[2u], nativeLogical[5u]};
        const std::array<C, 4u> readOps{
            C::ArgumentsCopy, C::MappingCopy, C::ProducerFaultCopy, C::ConsumerFaultCopy};
        for (std::size_t i = 0u; i < destinations.size(); ++i) {
            [readback copyFromBuffer:nativeBuffer(readSources[i])
                        sourceOffset:0u
                            toBuffer:nativeBuffer(destinations[i])
                   destinationOffset:0u
                                size:static_cast<NSUInteger>(i < 2u ? logical[i]->desc().sizeBytes
                                                                    : sizeof(std::uint32_t))];
#if !TARGET_OS_IPHONE
            if (destinations[i]->resource.storageMode == MetalRhiNativeStorageMode::Managed)
                [readback synchronizeResource:nativeBuffer(destinations[i])];
#endif
            if (!consume(readOps[i]))
                return false;
        }
        endBlit(readback, Slot::Blit);
        if (!consume(C::ReadbackEnd))
            return false;
    }
    if (const auto* id =
            closedConsumerSequence_->transition(Status::Recorded, admission->facts().identity))
        return fail(id);
    closedConsumerReservation_->status_.store(Status::Recorded, std::memory_order_release);
    if (const auto* id = root->validateRecorded(false))
        return fail(id);
    return true;
}

std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiClosedConsumerNativeCommandPayload(
    const rhi::CanonicalClosedConsumerRecordingContract::Budget& intent,
    const core::pmr::String& label,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    core::UniquePtr<rhi::common::CommandRetentionStorage> retention,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto decision = rhi::CanonicalClosedConsumerRecordingContract::privateRecordingLowering(
        rhi::Backend::Metal, intent);
    if (!decision.allowsPrototypePayload()) {
        diagnostic = decision.diagnosticId;
        return {};
    }
    if (!registry || !retention || retention->budget().maxQueryPools
        || retention->budget().maxTimestampWrites
        || retention->budget().maxPipelineStatisticsWrites) {
        diagnostic = "RHI-METAL-CLOSED-CONSUMER-COMMAND-PREPARATION";
        return {};
    }
    if (!@available(macOS 10.15, iOS 13.0, *)) {
        diagnostic = "RHI-METAL-CLOSED-CONSUMER-NATIVE-API-UNAVAILABLE";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(retention->quota());
        const auto capability =
            rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal);
        if (!rhi::semantics::hasExecutableNativeFeature(
                capability, rhi::semantics::NativeBackendFeature::IndirectDraw)) {
            diagnostic = "RHI-METAL-CLOSED-CONSUMER-INDIRECT-NATIVE-CONTRACT-MISSING";
            return {};
        }
        auto result = createMetalRhiNativeCommandPayload(
            label, std::move(registry), &diagnostic, 0u, 0u, 0u, std::move(retention));
        if (!result)
            return {};
        result->closedConsumerBudget_ = intent;
        result->closedConsumerLowering_ = decision;
        result->closedConsumerIndirectNative_ = true;
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        diagnostic = "RHI-METAL-CLOSED-CONSUMER-COMMAND-ALLOCATION-FAILED";
        return {};
    }
}

std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiCanonicalProducerNativeCommandPayload(
    const rhi::CanonicalZeroBaseIndirectProducerRecordingContract::Budget& intent,
    const core::pmr::String& label,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    core::UniquePtr<rhi::common::CommandRetentionStorage> retention,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto decision =
        rhi::CanonicalZeroBaseIndirectProducerRecordingContract::privateRecordingLowering(
            rhi::Backend::Metal, intent);
    if (!decision.allowsPrototypePayload()) {
        diagnostic = decision.diagnosticId;
        return {};
    }
    if (!retention || !registry || retention->budget().maxQueryPools
        || retention->budget().maxTimestampWrites
        || retention->budget().maxPipelineStatisticsWrites) {
        diagnostic = "RHI-METAL-CANONICAL-PRODUCER-COMMAND-PREPARATION";
        return {};
    }
    if (!@available(macOS 10.13, iOS 10.0, *)) {
        diagnostic = "RHI-METAL-CANONICAL-PRODUCER-NATIVE-API-UNAVAILABLE";
        return {};
    }
    const auto quota = retention->quota();
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = createMetalRhiNativeCommandPayload(
            label, std::move(registry), &diagnostic, 0u, 0u, 0u, std::move(retention));
        if (!result)
            return {};
        result->canonicalProducerStates_ = core::makeShared<core::MutationJournal>();
        for (auto& slot : result->canonicalProducerSlots_)
            slot = core::makeShared<core::ExternalObjectSlot>([](void* object) noexcept {
                [reinterpret_cast<id>(object) release];
            });
        result->canonicalProducerLowering_ = decision;
        result->canonicalProducerBudget_ = intent;
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        diagnostic = "RHI-METAL-CANONICAL-PRODUCER-COMMAND-ALLOCATION-FAILED";
        return {};
    }
}

bool MetalRhiNativeCommandPayload::recordProtectedIndirectExecution(
    const std::shared_ptr<const MetalRhiIndirectExecutionPreparation>& root,
    const rhi::BindlessResourceAccess& producerScope,
    const rhi::ResourceBarrierDesc& dependency,
    const rhi::BindlessResourceAccess& consumerScope,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!indirectOutputBudget_ || !root || !root->sharedOwnerMatches(root) || indirectExecution_
        || indirectOutputSequence_ || abandoned_ || recordingPoisoned_
        || submitted_.load(std::memory_order_acquire) || stats_->nativeCommandBufferFinished) {
        diagnostic = "RHI-METAL-INDIRECT-EXECUTION-RECORDING-MISMATCH";
        return false;
    }
    const auto command = root->command_.lock();
    if (!command || command.get() != this || !sharedOwnerMatches(command)) {
        diagnostic = "RHI-METAL-INDIRECT-EXECUTION-COMMAND-MISMATCH";
        return false;
    }
    if (const auto* id = root->validate()) {
        diagnostic = id;
        return false;
    }
    if (const auto* id = rhi::GpuIndirectOutputLifetimeContract::validateTransition(
            root->status(), MetalRhiIndirectExecutionPreparation::Status::Recorded)) {
        diagnostic = id;
        return false;
    }
    const auto& output = *root->output_;
    const auto& producer = *output.producer();
    const auto resource = output.resource().id();
    const auto& contract = producer.contract();
    if (const auto* id = contract.validateScope(resource, producerScope, true)) {
        diagnostic = id;
        return false;
    }
    if (const auto* id = contract.validateDependency(resource, dependency, consumerScope, true)) {
        diagnostic = id;
        return false;
    }
    if (!canPrepareMetalRhiRecordedState(output.resource(), *indirectOutputStates_)) {
        diagnostic = "RHI-RESOURCE-STATE-RECORDING-CONFLICT";
        return false;
    }
    if (!ordinaryComputePipelineValid(*producer.native(), nativeDevice_)
        || !validateOrdinaryGraphicsBinding(root->binding_, diagnostic)
        || !validateGraphicsPipelineBind(root->binding_->provider()->native(), diagnostic, false)
        || !validateOrdinaryGraphicsPush(root->binding_, {}, diagnostic)) {
        if (diagnostic.empty())
            diagnostic = "RHI-METAL-INDIRECT-EXECUTION-PIPELINE-MISMATCH";
        return false;
    }
    const auto compatible = root->pass_->validateProvider(*root->binding_->provider());
    if (!compatible.success) {
        diagnostic = compatible.diagnostics;
        return false;
    }
    rhi::BoundDescriptorSlots descriptors;
    const auto bound = descriptors.bindSet(
        root->binding_->payload()->set, *root->binding_->descriptor(), nullptr, 0u);
    if (!bound.success) {
        diagnostic = bound.diagnostics;
        return false;
    }
    const auto complete = descriptors.validateComplete(root->binding_->provider()->consumption(),
        root->binding_->provider()->native()->preparedGraphicsStages);
    if (!complete.success) {
        diagnostic = complete.diagnostics;
        return false;
    }
    if (root->binding_->provider()->interface().reflection.pushConstantByteCount) {
        diagnostic = "[PC-E011] RHI-METAL-ORDINARY-GRAPHICS-PUSH-FRESH-BLOCK-REQUIRED";
        return false;
    }
    const auto arguments = nativeBuffer(output.buffer()),
               index = nativeBuffer(output.index()->buffer());
    if (!arguments || !index || arguments.length != rhi::GpuIndirectProducerContract::footprintBytes
        || index.length != output.index()->content().bytes().size()
        || !fitsNSUInteger(producer.fact().slot)) {
        diagnostic = "RHI-METAL-INDIRECT-EXECUTION-NATIVE-EXTENT";
        return false;
    }
    for (const auto& stream : output.streams())
        if (!nativeBuffer(stream.binding->buffer()) || !fitsNSUInteger(stream.binding->offset())) {
            diagnostic = "RHI-METAL-INDIRECT-EXECUTION-STREAM-NATIVE-EXTENT";
            return false;
        }
    const auto ownerAt = [&](std::size_t i) noexcept -> std::shared_ptr<const void> {
        return i ? root->childOwnerAt(i - 1u) : root;
    };
    if (const auto* id =
            recordingRetention_->retainResourceProjection(root->childOwnerCount() + 1u, ownerAt)) {
        diagnostic = id;
        if (std::string_view(id) == "RHI-COMMAND-RETENTION-OWNER-MAPPING-CHANGED")
            poisonRecording(diagnostic);
        return false;
    }
    indirectExecution_ = root;
    root->status_.store(
        MetalRhiIndirectExecutionPreparation::Status::Recorded, std::memory_order_release);
    indirectOutputSequence_.emplace(*indirectOutputBudget_, root->identity_);
    const auto fail = [&](const char* id) {
        diagnostic = id;
        poisonRecording(diagnostic);
        return false;
    };
    const auto consume = [&](rhi::GpuIndirectOutputSequence::Command operation) {
        if (const auto* id = indirectOutputSequence_->record(operation, root->identity_))
            return fail(id);
        stats_->protectedIndirectCommandCount = indirectOutputSequence_->commands();
        stats_->protectedIndirectCallCount = indirectOutputSequence_->calls();
        stats_->protectedIndirectRecordCount = indirectOutputSequence_->records();
        return true;
    };
    using C = rhi::GpuIndirectOutputSequence::Command;
    if (!prepareMetalRhiRecordedState(*output.storage_.buffer, *indirectOutputStates_))
        return fail("RHI-RESOURCE-STATE-RECORDING-CONFLICT");
    @autoreleasepool {
        const auto nativeCommand =
            reinterpret_cast<id<MTLCommandBuffer>>(nativeCommandBuffer_.get());
        const auto encoder = [nativeCommand computeCommandEncoder];
        if (!encoder)
            return fail("RHI-METAL-COMPUTE-PASS-ENCODER-CREATE-FAILED");
        [encoder retain];
        if (!preparedIndirectComputeEncoderSlot_->adopt(reinterpret_cast<void*>(encoder))) {
            [encoder endEncoding];
            [encoder release];
            return fail("RHI-METAL-INDIRECT-COMPUTE-ENCODER-ADOPTION-FAILED");
        }
        nativeComputeEncoder_ = std::shared_ptr<void>(
            preparedIndirectComputeEncoderSlot_, preparedIndirectComputeEncoderSlot_->get());
        stats_->nativeComputeEncoderCreated = true;
        if (!consume(C::ProducerBegin))
            return false;
        [encoder setComputePipelineState:reinterpret_cast<id<MTLComputePipelineState>>(
                                             producer.native()->pipelineState.get())];
        [encoder setBuffer:arguments offset:0u atIndex:producer.fact().slot];
        stats_->nativeComputePipelineBound = true;
        [encoder dispatchThreadgroups:MTLSizeMake(1u, 1u, 1u)
                threadsPerThreadgroup:MTLSizeMake(1u, 1u, 1u)];
        stats_->nativeDispatchEncoded = true;
        if (!consume(C::Dispatch))
            return false;
        [encoder endEncoding];
        nativeComputeEncoder_.reset();
        preparedIndirectComputeEncoderSlot_->reset();
    }
    if (!consume(C::ProducerEnd))
        return false;
    // Verified tracked Shared resource + ended serial encoders gives native
    // hazard ordering. The existing journal owns the L2 state transition.
    const auto state = setMetalRhiBufferStateForBarrier(
        *output.storage_.buffer, dependency, indirectOutputStates_.get());
    if (!state.success) {
        diagnostic = state.diagnostics;
        poisonRecording(diagnostic);
        return false;
    }
    stats_->nativeResourceBarrierEncoded = true;
    if (!consume(C::Dependency))
        return false;
    if (!encodeOrdinaryPreparedRenderPass(root->pass_, root->binding_, diagnostic, {})) {
        poisonRecording(diagnostic);
        return false;
    }
    if (!consume(C::ConsumerBegin))
        return false;
    const auto encoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    for (const auto& stream : output.streams()) {
        const auto& binding = stream.binding;
        [encoder setVertexBuffer:nativeBuffer(binding->buffer())
                          offset:static_cast<NSUInteger>(binding->offset())
                         atIndex:binding->physicalSlot()];
        ordinaryVertexBindings_[binding->slot()] = binding;
        vertexStreams_[binding->slot()] = {binding->buffer(), binding->offset()};
        if (!consume(C::StreamBinding))
            return false;
    }
    if (!validateOrdinaryGraphicsFixed(diagnostic)) {
        poisonRecording(diagnostic);
        return false;
    }
    const auto type = output.index()->content().format() == rhi::IndexFormat::Uint16
        ? MTLIndexTypeUInt16
        : MTLIndexTypeUInt32;
    [encoder drawIndexedPrimitives:static_cast<MTLPrimitiveType>(renderPipeline_->primitiveType)
                         indexType:type
                       indexBuffer:index
                 indexBufferOffset:0u
                    indirectBuffer:arguments
              indirectBufferOffset:0u];
    stats_->nativeIndexedDrawEncoded = true;
    if (!consume(C::Indirect))
        return false;
    [encoder endEncoding];
    nativeRenderEncoder_.reset();
    preparedRenderEncoderSlot_->reset();
    ordinaryGraphicsPass_.reset();
    ordinaryGraphicsContract_.reset();
    ordinaryGraphicsBindings_ = {};
    ordinaryGraphicsDescriptors_.reset();
    ordinaryVertexBindings_ = {};
    vertexStreams_ = {};
    ordinaryGraphicsPushState_.onPassEnd();
    renderPipeline_.reset();
    return consume(C::ConsumerEnd);
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedIndirectDraw(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedIndirectArguments>& args,
    const rhi::BindlessResourceAccess& access,
    core::pmr::String& diagnostic)
{
    if (!retentionRecordingAllows(true, diagnostic, true))
        return false;
    if (!ordinaryIndirectRecording_) {
        diagnostic = "RHI-METAL-ORDINARY-INDIRECT-PROTOTYPE-REQUIRED";
        return false;
    }
    if (!args || !recordingRetention_ || !ordinaryGraphicsContract_ || !ordinaryGraphicsPass_
        || !nativeRenderEncoder_
        || args->index()->provider() != ordinaryGraphicsContract_->provider()
        || args->index()->registry() != resourceRegistry_
        || args->index()->quota() != recordingRetention_->quota()) {
        diagnostic = "RHI-METAL-ORDINARY-INDIRECT-RECORDING-MISMATCH";
        return false;
    }
    if (const auto* id = args->validate()) {
        diagnostic = id;
        return false;
    }
    if (args->index()->resource().state() != rhi::ResourceState::ShaderRead) {
        diagnostic = "RHI-METAL-ORDINARY-INDIRECT-INDEX-STATE";
        return false;
    }
    if (const auto* id = args->content().validateConsumerScope(args->resource().id(), access)) {
        diagnostic = id;
        return false;
    }
    if (const auto* id = ordinaryIndirectRecording_->canDraw(args->content().drawCount())) {
        diagnostic = id;
        return false;
    }
    if (!validateOrdinaryGraphicsFixed(diagnostic))
        return false;
    for (std::size_t ordinal = 0u; ordinal < args->content().drawCount(); ++ordinal)
        for (const auto& stream : args->index()->provider()->vertexInput().streams()) {
            const auto& vertex = ordinaryVertexBindings_[stream.slot];
            if (const auto* id = args->content().validateStream(ordinal,
                    args->index()->provider()->vertexInput(),
                    stream.slot,
                    vertex->bufferBytes(),
                    vertex->offset())) {
                diagnostic = id;
                return false;
            }
        }
    const auto index = nativeBuffer(args->index()->buffer()),
               arguments = nativeBuffer(args->buffer());
    if (!index || !arguments
        || !commandDeviceMatches(nativeDevice_, args->buffer()->resource.nativeDevice)
        || !commandDeviceMatches(nativeDevice_, args->index()->buffer()->resource.nativeDevice)
        || index.length != args->index()->content().bytes().size()
        || arguments.length != args->content().bytes().size()
        || renderPipeline_->primitiveTopology == rhi::PrimitiveTopology::PatchList) {
        diagnostic = "RHI-METAL-ORDINARY-INDIRECT-NATIVE-EXTENT-OR-TOPOLOGY";
        return false;
    }
    const auto ownerAt = [&](std::size_t i) noexcept -> std::shared_ptr<const void> {
        return !i ? std::shared_ptr<const void>(args) : args->childOwnerAt(i - 1u);
    };
    if (const auto* id =
            recordingRetention_->retainResourceProjection(args->childOwnerCount() + 1u, ownerAt)) {
        diagnostic = id;
        return false;
    }
    const auto encoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    const auto type = args->index()->content().format() == rhi::IndexFormat::Uint16
        ? MTLIndexTypeUInt16
        : MTLIndexTypeUInt32;
    for (std::size_t ordinal = 0u; ordinal < args->content().drawCount(); ++ordinal) {
        // Native reads the sealed record. The CPU observes only prepared bounds;
        // it neither reads argument bytes here nor substitutes direct commands.
        [encoder
            drawIndexedPrimitives:static_cast<MTLPrimitiveType>(renderPipeline_->primitiveType)
                        indexType:type
                      indexBuffer:index
                indexBufferOffset:0u
                   indirectBuffer:arguments
             indirectBufferOffset:ordinal * rhi::IndirectArgumentConsumptionContract::recordBytes];
    }
    ordinaryIndirectRecording_->draw(args->content().drawCount());
    publishOrdinaryIndirectCounters();
    stats_->nativeIndexedDrawEncoded = true;
    ordinaryGraphicsPushState_.onConsume();
    return true;
}

bool MetalRhiNativeCommandPayload::recordDrawIndexed(std::uint32_t indexCount,
    std::uint32_t instanceCount,
    std::uint32_t firstIndex,
    std::int32_t vertexOffset,
    std::uint32_t firstInstance,
    std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    if (activeGraphicsBinding_ && !graphicsScopeBound_) {
        diagnostics = "RHI-METAL-GRAPHICS-SCOPE-BINDING-REQUIRED";
        return false;
    }
    auto* encoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    id<MTLBuffer> indexBuffer = nativeBuffer(indexBuffer_);
    if (encoder == nil || indexBuffer == nil || renderPipeline_ == nullptr) {
        diagnostics = "RHI-METAL-DRAW-INDEXED-EXECUTABLE-PAYLOAD-MISSING";
        return false;
    }
    if (!validateVertexStreams(diagnostics))
        return false;
    const std::uint64_t indexSize = indexFormat_ == rhi::IndexFormat::Uint16 ? 2u : 4u;
    if (firstIndex > (std::numeric_limits<std::uint64_t>::max() - indexBufferOffset_) / indexSize
        || indexCount > (std::numeric_limits<std::uint64_t>::max() - indexBufferOffset_
                            - firstIndex * indexSize)
                / indexSize) {
        diagnostics = "RHI-METAL-DRAW-INDEXED-ARGUMENTS-OVERFLOW";
        return false;
    }
    const std::uint64_t offset = indexBufferOffset_ + firstIndex * indexSize;
    const std::uint64_t indexBytes = static_cast<std::uint64_t>(indexCount) * indexSize;
    if (!fitsNSUInteger(offset) || offset > indexBuffer.length
        || indexBytes > indexBuffer.length - offset || instanceCount == 0) {
        diagnostics = "RHI-METAL-DRAW-INDEXED-ARGUMENTS-INVALID";
        return false;
    }
    if (renderPipeline_->primitiveTopology == rhi::PrimitiveTopology::PatchList) {
        if (!@available(macOS 10.13, iOS 11.0, *)) {
            diagnostics = "RHI-METAL-PATCH-LIST-API-UNAVAILABLE";
            return false;
        }
        if (!validTessellationFactorBinding(renderPipeline_,
                tessellationFactorBuffer_,
                tessellationFactorBufferOffset_,
                tessellationFactorInstanceStrideBytes_,
                instanceCount,
                diagnostics)) {
            return false;
        }
        if (renderPipeline_->patchControlPointCount == 0u
            || indexCount % renderPipeline_->patchControlPointCount != 0u || vertexOffset != 0) {
            diagnostics = "RHI-METAL-PATCH-DRAW-INDEXED-ARGUMENTS-INVALID";
            return false;
        }
        [encoder drawIndexedPatches:renderPipeline_->patchControlPointCount
                               patchCount:indexCount / renderPipeline_->patchControlPointCount
                         patchIndexBuffer:nil
                   patchIndexBufferOffset:0u
                  controlPointIndexBuffer:indexBuffer
            controlPointIndexBufferOffset:static_cast<NSUInteger>(offset)
                            instanceCount:instanceCount
                             baseInstance:firstInstance];
        stats_->nativeIndexedDrawEncoded = true;
        return true;
    }
    [encoder drawIndexedPrimitives:static_cast<MTLPrimitiveType>(renderPipeline_->primitiveType)
                        indexCount:indexCount
                         indexType:nativeIndexType(indexFormat_)
                       indexBuffer:indexBuffer
                 indexBufferOffset:static_cast<NSUInteger>(offset)
                     instanceCount:instanceCount
                        baseVertex:vertexOffset
                      baseInstance:firstInstance];
    stats_->nativeIndexedDrawEncoded = indexCount > 0;
    return indexCount > 0;
}

bool MetalRhiNativeCommandPayload::recordDraw(std::uint32_t vertexCount,
    std::uint32_t instanceCount,
    std::uint32_t firstVertex,
    std::uint32_t firstInstance,
    std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(static_cast<bool>(ordinaryGraphicsPass_), diagnostics))
        return false;
    if (activeGraphicsBinding_ && !graphicsScopeBound_) {
        diagnostics = "RHI-METAL-GRAPHICS-SCOPE-BINDING-REQUIRED";
        return false;
    }
    auto* encoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    if (encoder == nil || renderPipeline_ == nullptr || vertexCount == 0 || instanceCount == 0) {
        diagnostics = "RHI-METAL-DRAW-EXECUTABLE-PAYLOAD-MISSING";
        return false;
    }
    if (recordingRetention_
        && !validateOrdinaryGraphicsDraw(
            diagnostics, vertexCount, firstVertex, instanceCount, firstInstance))
        return false;
    if (!validateVertexStreams(diagnostics))
        return false;
    if (renderPipeline_->primitiveTopology == rhi::PrimitiveTopology::PatchList) {
        if (!@available(macOS 10.13, iOS 11.0, *)) {
            diagnostics = "RHI-METAL-PATCH-LIST-API-UNAVAILABLE";
            return false;
        }
        if (!validTessellationFactorBinding(renderPipeline_,
                tessellationFactorBuffer_,
                tessellationFactorBufferOffset_,
                tessellationFactorInstanceStrideBytes_,
                instanceCount,
                diagnostics)) {
            return false;
        }
        if (renderPipeline_->patchControlPointCount == 0u
            || vertexCount % renderPipeline_->patchControlPointCount != 0u
            || firstVertex % renderPipeline_->patchControlPointCount != 0u) {
            diagnostics = "RHI-METAL-PATCH-DRAW-ARGUMENTS-INVALID";
            return false;
        }
        [encoder drawPatches:renderPipeline_->patchControlPointCount
                        patchStart:firstVertex / renderPipeline_->patchControlPointCount
                        patchCount:vertexCount / renderPipeline_->patchControlPointCount
                  patchIndexBuffer:nil
            patchIndexBufferOffset:0u
                     instanceCount:instanceCount
                      baseInstance:firstInstance];
        stats_->nativeDrawEncoded = true;
        return true;
    }
    [encoder drawPrimitives:static_cast<MTLPrimitiveType>(renderPipeline_->primitiveType)
                vertexStart:firstVertex
                vertexCount:vertexCount
              instanceCount:instanceCount
               baseInstance:firstInstance];
    stats_->nativeDrawEncoded = vertexCount > 0;
    if (ordinaryWrite_
        && ordinaryWriteSequence_.phase()
            == rhi::GraphicsStorageWriteSequence::Phase::ProducerOpen) {
        if (const auto* id = ordinaryWriteSequence_.draw(ordinaryWrite_->resource().id())) {
            diagnostics = id;
            poisonRecording(diagnostics);
            return false;
        }
        ++ordinaryWriteDraws_;
        ++stats_->ordinaryWriteCommandCount;
    }
    if (ordinaryReadConsumer_
        && ordinaryWriteSequence_.phase()
            == rhi::GraphicsStorageWriteSequence::Phase::ConsumerOpen) {
        if (const auto* id = ordinaryWriteSequence_.drawConsumer(ordinaryWrite_->resource().id())) {
            diagnostics = id;
            poisonRecording(diagnostics);
            return false;
        }
        ++ordinaryConsumerDraws_;
        ++stats_->ordinaryWriteCommandCount;
    }
    if (ordinaryGraphicsPass_)
        ordinaryGraphicsPushState_.onConsume();
    return vertexCount > 0;
}

bool MetalRhiNativeCommandPayload::recordDrawIndexedIndirect(
    const std::shared_ptr<MetalRhiNativeBufferPayload>& indirectBuffer,
    std::uint64_t indirectOffset,
    std::uint32_t drawCount,
    std::uint32_t stride,
    std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    if (activeGraphicsBinding_) {
        diagnostics = "RHI-METAL-GRAPHICS-SCOPE-INDIRECT-UNSUPPORTED";
        return false;
    }
    if (!validateVertexStreams(diagnostics))
        return false;
    auto* encoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    id<MTLBuffer> indexBuffer = nativeBuffer(indexBuffer_);
    id<MTLBuffer> indirect = nativeBuffer(indirectBuffer);
    const std::uint64_t recordSize = sizeof(rhi::DrawIndexedIndirectCommand);
    if (encoder == nil || indexBuffer == nil || indirect == nil || renderPipeline_ == nullptr
        || drawCount == 0 || stride < recordSize
        || !commandDeviceMatches(nativeDevice_, indexBuffer_->resource.nativeDevice)
        || !commandDeviceMatches(
            nativeDevice_, indirectBuffer ? indirectBuffer->resource.nativeDevice : nullptr)
        || (stride & 3u) != 0u || (indirectOffset & 3u) != 0u || !fitsNSUInteger(indexBufferOffset_)
        || !fitsNSUInteger(indirectOffset) || indexBufferOffset_ >= indexBuffer.length
        || indirectOffset > indirect.length || indirect.length - indirectOffset < recordSize
        || drawCount - 1u > (indirect.length - indirectOffset - recordSize) / stride) {
        diagnostics = "RHI-METAL-DRAW-INDEXED-INDIRECT-ARGUMENTS-INVALID";
        return false;
    }
    if (renderPipeline_->primitiveTopology == rhi::PrimitiveTopology::PatchList) {
        diagnostics = "RHI-METAL-PATCH-INDIRECT-DRAW-LOWERING-UNAVAILABLE";
        return false;
    }
    for (std::uint32_t drawIndex = 0; drawIndex < drawCount; ++drawIndex) {
        const auto offset = indirectOffset + static_cast<std::uint64_t>(drawIndex) * stride;
        [encoder drawIndexedPrimitives:static_cast<MTLPrimitiveType>(renderPipeline_->primitiveType)
                             indexType:nativeIndexType(indexFormat_)
                           indexBuffer:indexBuffer
                     indexBufferOffset:static_cast<NSUInteger>(indexBufferOffset_)
                        indirectBuffer:indirect
                  indirectBufferOffset:static_cast<NSUInteger>(offset)];
    }
    retainResourcePayload(
        retainedResourcePayloads_, retainedResourcePayloadIdentities_, indirectBuffer);
    stats_->nativeIndexedDrawEncoded = true;
    return true;
}

bool MetalRhiNativeCommandPayload::recordGpuCountedIndirectCountBuffer(
    const std::shared_ptr<MetalRhiNativeBufferPayload>& countBuffer,
    std::uint64_t countOffset,
    std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    auto* encoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    id<MTLBuffer> count = nativeBuffer(countBuffer);
    if (encoder == nil || count == nil || renderPipeline_ == nullptr
        || renderPipeline_->metalGpuCountedIndirectCountBufferIndex >= 31u
        || !commandDeviceMatches(
            nativeDevice_, countBuffer ? countBuffer->resource.nativeDevice : nullptr)
        || renderPipeline_->metalGpuCountedIndirectContractVersion
            != rhi::kMetalGpuCountedIndirectContractVersion
        || renderPipeline_->metalGpuCountedIndirectStatus != "Available"
        || !renderPipeline_->metalGpuCountedIndirectUsesDrawId
        || !renderPipeline_->metalGpuCountedIndirectMasksOutOfRange || countOffset > count.length
        || count.length - countOffset < sizeof(std::uint32_t) || !fitsNSUInteger(countOffset)) {
        diagnostics = "RHI-METAL-GPU-COUNTED-INDIRECT-MASK-ABI-MISSING";
        return false;
    }
    [encoder setVertexBuffer:count
                      offset:static_cast<NSUInteger>(countOffset)
                     atIndex:renderPipeline_->metalGpuCountedIndirectCountBufferIndex];
    retainResourcePayload(
        retainedResourcePayloads_, retainedResourcePayloadIdentities_, countBuffer);
    return true;
}

bool MetalRhiNativeCommandPayload::recordDrawIndexedIndirectCount(
    const std::shared_ptr<MetalRhiNativeBufferPayload>& argsBuffer,
    std::uint64_t argsOffset,
    const std::shared_ptr<MetalRhiNativeBufferPayload>& countBuffer,
    std::uint64_t countOffset,
    std::uint32_t maxDrawCount,
    std::uint32_t stride,
    std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    if (activeGraphicsBinding_) {
        diagnostics = "RHI-METAL-GRAPHICS-SCOPE-INDIRECT-UNSUPPORTED";
        return false;
    }
    if (!validateVertexStreams(diagnostics))
        return false;
    auto* encoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    id<MTLBuffer> args = nativeBuffer(argsBuffer);
    id<MTLBuffer> count = nativeBuffer(countBuffer);
    const std::uint64_t recordSize = sizeof(rhi::DrawIndexedIndirectCommand);
    if (encoder == nil || args == nil || count == nil || renderPipeline_ == nullptr
        || maxDrawCount == 0u || stride < recordSize || (stride & 3u) != 0u
        || !commandDeviceMatches(
            nativeDevice_, argsBuffer ? argsBuffer->resource.nativeDevice : nullptr)
        || !commandDeviceMatches(
            nativeDevice_, countBuffer ? countBuffer->resource.nativeDevice : nullptr)
        || (argsOffset & 3u) != 0u || (countOffset & 3u) != 0u || argsOffset > args.length
        || countOffset > count.length || args.length - argsOffset < recordSize
        || count.length - countOffset < sizeof(std::uint32_t)
        || renderPipeline_->primitiveTopology == rhi::PrimitiveTopology::PatchList
        || renderPipeline_->metalGpuCountedIndirectContractVersion
            != rhi::kMetalGpuCountedIndirectContractVersion
        || renderPipeline_->metalGpuCountedIndirectStatus != "Available"
        || !renderPipeline_->metalGpuCountedIndirectUsesDrawId
        || !renderPipeline_->metalGpuCountedIndirectMasksOutOfRange
        || renderPipeline_->metalGpuCountedIndirectCountBufferIndex >= 31u) {
        diagnostics = "RHI-METAL-DRAW-INDEXED-INDIRECT-COUNT-ARGUMENTS-INVALID";
        return false;
    }
    if (maxDrawCount - 1u > (std::numeric_limits<std::uint64_t>::max() - argsOffset) / stride
        || argsOffset + static_cast<std::uint64_t>(maxDrawCount - 1u) * stride > args.length
        || args.length - (argsOffset + static_cast<std::uint64_t>(maxDrawCount - 1u) * stride)
            < recordSize
        || !fitsNSUInteger(argsOffset)) {
        diagnostics = "RHI-METAL-DRAW-INDEXED-INDIRECT-COUNT-ARGUMENTS-INVALID";
        return false;
    }
    if (indexBuffer_ == nullptr || nativeBuffer(indexBuffer_) == nil
        || !commandDeviceMatches(nativeDevice_, indexBuffer_->resource.nativeDevice)
        || !fitsNSUInteger(indexBufferOffset_)) {
        diagnostics = "RHI-METAL-DRAW-INDEXED-INDIRECT-COUNT-INDEX-BUFFER-MISSING";
        return false;
    }
    id<MTLBuffer> indexBuffer = nativeBuffer(indexBuffer_);
    for (std::uint32_t drawIndex = 0u; drawIndex < maxDrawCount; ++drawIndex) {
        const auto offset = argsOffset + static_cast<std::uint64_t>(drawIndex) * stride;
        if (!fitsNSUInteger(offset)) {
            diagnostics = "RHI-METAL-DRAW-INDEXED-INDIRECT-COUNT-OFFSET-OVERFLOW";
            return false;
        }
    }
    // Bind the reflected count-mask input only after every validation that
    // precedes the native draw loop has succeeded. This keeps a failed
    // counted-indirect command from leaving native vertex state behind.
    if (!recordGpuCountedIndirectCountBuffer(countBuffer, countOffset, diagnostics)) {
        return false;
    }
    retainResourcePayload(
        retainedResourcePayloads_, retainedResourcePayloadIdentities_, argsBuffer);
    for (std::uint32_t drawIndex = 0u; drawIndex < maxDrawCount; ++drawIndex) {
        const auto offset = argsOffset + static_cast<std::uint64_t>(drawIndex) * stride;
        [encoder drawIndexedPrimitives:static_cast<MTLPrimitiveType>(renderPipeline_->primitiveType)
                             indexType:nativeIndexType(indexFormat_)
                           indexBuffer:indexBuffer
                     indexBufferOffset:static_cast<NSUInteger>(indexBufferOffset_)
                        indirectBuffer:args
                  indirectBufferOffset:static_cast<NSUInteger>(offset)];
    }
    stats_->nativeIndexedDrawEncoded = true;
    return true;
}

bool MetalRhiNativeCommandPayload::recordTimestamp(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& queryPool,
    std::uint32_t queryIndex,
    std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    auto encoder =
        std::shared_ptr<void>(nativeRenderEncoder_ ? nativeRenderEncoder_ : nativeComputeEncoder_);
    if (!encoder) {
        diagnostics = "RHI-METAL-TIMESTAMP-QUERY-ACTIVE-ENCODER-MISSING";
        return false;
    }
    if (!queryPool || !commandDeviceMatches(nativeDevice_, queryPool->nativeDevice)) {
        diagnostics = "RHI-METAL-QUERY-POOL-DEVICE-MISMATCH";
        return false;
    }
    if (!recordMetalRhiNativeTimestamp(encoder, queryPool, queryIndex, diagnostics)) {
        return false;
    }
    bool retained = false;
    for (const auto& retainedPool : retainedQueryPools_) {
        if (retainedPool == queryPool) {
            retained = true;
            break;
        }
    }
    if (!retained) {
        retainedQueryPools_.push_back(queryPool);
    }
    recordedTimestampQueries_.push_back({queryPool, queryIndex});
    if (timingFrameSerial_ == 0u) {
        timingFrameSerial_ = allocateMetalRhiNativeTimingFrameSerial();
    }
    return true;
}

bool MetalRhiNativeCommandPayload::recordPipelineStatistics(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& queryPool,
    std::uint32_t queryIndex,
    std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    auto encoder =
        std::shared_ptr<void>(nativeRenderEncoder_ ? nativeRenderEncoder_ : nativeComputeEncoder_);
    if (!encoder) {
        diagnostics = "RHI-METAL-PIPELINE-STATISTICS-QUERY-ACTIVE-ENCODER-MISSING";
        return false;
    }
    if (!queryPool || !commandDeviceMatches(nativeDevice_, queryPool->nativeDevice)) {
        diagnostics = "RHI-METAL-QUERY-POOL-DEVICE-MISMATCH";
        return false;
    }
    if (!recordMetalRhiNativePipelineStatistics(encoder, queryPool, queryIndex, diagnostics)) {
        return false;
    }
    bool retained = false;
    for (const auto& retainedPool : retainedQueryPools_) {
        if (retainedPool == queryPool) {
            retained = true;
            break;
        }
    }
    if (!retained) {
        retainedQueryPools_.push_back(queryPool);
    }
    recordedPipelineStatisticsQueries_.push_back({queryPool, queryIndex});
    if (timingFrameSerial_ == 0u) {
        timingFrameSerial_ = allocateMetalRhiNativeTimingFrameSerial();
    }
    return true;
}

bool MetalRhiNativeCommandPayload::executeSecondary(
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& secondary,
    std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    if (activeGraphicsBinding_) {
        diagnostics = "RHI-METAL-GRAPHICS-SCOPE-SECONDARY-UNSUPPORTED";
        return false;
    }
    if (!renderPipeline_ || !renderPipeline_->supportsIndirectCommands) {
        diagnostics = "RHI-METAL-SECONDARY-PIPELINE-BINDINGS-UNSUPPORTED";
        return false;
    }
    if (!validateVertexStreams(diagnostics))
        return false;
    if (!nativeRenderEncoder_) {
        diagnostics = "RHI-METAL-SECONDARY-RENDER-ENCODER-MISSING";
        return false;
    }
    if (!secondary || !commandDeviceMatches(nativeDevice_, secondary->nativeDevice)) {
        diagnostics = "RHI-METAL-SECONDARY-DEVICE-MISMATCH";
        return false;
    }
    if (secondary && secondary->primitiveTopology == rhi::PrimitiveTopology::PatchList
        && secondary->maxTessellationInstanceCount != 0u
        && !validateTessellationFactorBufferRange(
            secondary->maxTessellationInstanceCount, diagnostics)) {
        return false;
    }
    // The consuming command buffer owns the ICB and every resource retained
    // by it until GPU completion. The caller may release the secondary
    // command object immediately after recording this execute operation.
    retainResourcePayload(retainedResourcePayloads_, retainedResourcePayloadIdentities_, secondary);
    return executeMetalRhiNativeSecondary(nativeRenderEncoder_, secondary, diagnostics);
}

bool MetalRhiNativeCommandPayload::executeSecondaryCommand(
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& secondary,
    std::uint32_t commandIndex,
    std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    if (activeGraphicsBinding_) {
        diagnostics = "RHI-METAL-GRAPHICS-SCOPE-SECONDARY-UNSUPPORTED";
        return false;
    }
    if (!renderPipeline_ || !renderPipeline_->supportsIndirectCommands) {
        diagnostics = "RHI-METAL-SECONDARY-PIPELINE-BINDINGS-UNSUPPORTED";
        return false;
    }
    if (!validateVertexStreams(diagnostics))
        return false;
    if (!nativeRenderEncoder_) {
        diagnostics = "RHI-METAL-SECONDARY-RENDER-ENCODER-MISSING";
        return false;
    }
    if (!secondary || !commandDeviceMatches(nativeDevice_, secondary->nativeDevice)) {
        diagnostics = "RHI-METAL-SECONDARY-DEVICE-MISMATCH";
        return false;
    }
    if (secondary && secondary->primitiveTopology == rhi::PrimitiveTopology::PatchList
        && secondary->maxTessellationInstanceCount != 0u
        && !validateTessellationFactorBufferRange(
            secondary->maxTessellationInstanceCount, diagnostics)) {
        return false;
    }
    retainResourcePayload(retainedResourcePayloads_, retainedResourcePayloadIdentities_, secondary);
    return executeMetalRhiNativeSecondaryCommand(
        nativeRenderEncoder_, secondary, commandIndex, diagnostics);
}

bool MetalRhiNativeCommandPayload::recordRenderPassEnd(std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(static_cast<bool>(ordinaryGraphicsPass_), diagnostics, true))
        return false;
    if (ordinaryIndirectRecording_)
        if (const auto* id = ordinaryIndirectRecording_->canEnd()) {
            diagnostics = id;
            return false;
        }
    if (activeGraphicsBinding_ && !graphicsScopeBound_) {
        diagnostics = "RHI-METAL-GRAPHICS-SCOPE-BINDING-REQUIRED";
        return false;
    }
    auto* encoder = reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get());
    if (encoder == nil) {
        diagnostics = "RHI-METAL-RENDER-PASS-END-ENCODER-MISSING";
        return false;
    }
    if (ordinaryGraphicsPass_) {
        const auto valid = ordinaryGraphicsPass_->validateObservation();
        if (!valid.success) {
            diagnostics = valid.diagnostics;
            return false;
        }
    }
    if (ordinaryWrite_
        && ordinaryWriteSequence_.phase()
            == rhi::GraphicsStorageWriteSequence::Phase::ProducerOpen) {
        const auto valid = ordinaryWrite_->validateObservation();
        if (!valid.success) {
            diagnostics = valid.diagnostics;
            return false;
        }
        if (ordinaryWrite_->resource().state() != rhi::ResourceState::ShaderWrite) {
            diagnostics = "RHI-METAL-ORDINARY-WRITE-PRODUCER-STATE";
            return false;
        }
        if (const auto* id = ordinaryWriteSequence_.canEnd(ordinaryWrite_->resource().id())) {
            diagnostics = id;
            return false;
        }
    }
    if (ordinaryReadConsumer_
        && ordinaryWriteSequence_.phase()
            == rhi::GraphicsStorageWriteSequence::Phase::ConsumerOpen) {
        const auto valid = ordinaryReadConsumer_->validateObservation();
        if (!valid.success) {
            diagnostics = valid.diagnostics;
            return false;
        }
        if (ordinaryWrite_->resource().state() != rhi::ResourceState::ShaderRead) {
            diagnostics = "RHI-METAL-ORDINARY-READ-CONSUMER-STATE-MISMATCH";
            return false;
        }
        if (const auto* id =
                ordinaryWriteSequence_.canEndConsumer(ordinaryWrite_->resource().id())) {
            diagnostics = id;
            return false;
        }
    }
    [encoder endEncoding];
    if (ordinaryIndirectRecording_) {
        ordinaryIndirectRecording_->end();
        publishOrdinaryIndirectCounters();
    }
    if (ordinaryWrite_
        && ordinaryWriteSequence_.phase()
            == rhi::GraphicsStorageWriteSequence::Phase::ProducerOpen) {
        ordinaryWriteSequence_.end(ordinaryWrite_->resource().id());
        ++stats_->ordinaryWriteCommandCount;
    }
    if (ordinaryReadConsumer_
        && ordinaryWriteSequence_.phase()
            == rhi::GraphicsStorageWriteSequence::Phase::ConsumerOpen) {
        ordinaryWriteSequence_.endConsumer(ordinaryWrite_->resource().id());
        ++stats_->ordinaryWriteCommandCount;
    }
    nativeRenderEncoder_.reset();
    if (preparedRenderEncoderSlot_)
        preparedRenderEncoderSlot_->reset();
    ordinaryGraphicsPass_.reset();
    ordinaryGraphicsContract_.reset();
    ordinaryGraphicsDescriptors_.reset();
    ordinaryGraphicsBindings_ = {};
    ordinaryGraphicsPush_.reset();
    ordinaryGraphicsPushState_.onPassEnd();
    ordinaryVertexBindings_ = {};
    vertexStreams_ = {};
    renderPipeline_.reset();
    vertexStreams_ = {};
    ordinaryVertexBindings_ = {};
    indexBuffer_.reset();
    indexFormat_ = rhi::IndexFormat::Uint32;
    indexBufferOffset_ = 0u;
    tessellationFactorBuffer_.reset();
    tessellationFactorBufferOffset_ = 0u;
    tessellationFactorInstanceStrideBytes_ = 0u;
    renderPassWidth_ = 0u;
    renderPassHeight_ = 0u;
    renderPassLayered_ = false;
    if (activeGraphicsBinding_) {
        auto domain = std::move(activeGraphicsFaultDomain_);
        activeGraphicsBinding_.reset();
        graphicsScopeBound_ = false;
        graphicsAccesses_.clear();
        if (!scheduleBindlessFaultReadback(std::move(domain), diagnostics)) {
            poisonRecording(diagnostics);
            return false;
        }
    }
    return true;
}

bool MetalRhiNativeCommandPayload::recordNextSubpass(std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    if (activeGraphicsBinding_) {
        diagnostics = "RHI-METAL-GRAPHICS-SCOPE-SUBPASS-UNSUPPORTED";
        return false;
    }
    if (!nativeRenderEncoder_) {
        diagnostics = "RHI-METAL-NEXT-SUBPASS-RENDER-ENCODER-MISSING";
        return false;
    }
    if (renderSubpassCount_ <= 1u) {
        return true;
    }
    if (renderSubpassIndex_ + 1u >= renderSubpassCount_) {
        diagnostics = "RHI-METAL-NEXT-SUBPASS-EXCEEDS-SUBPASS-COUNT";
        return false;
    }
    // Metal framebuffer fetch is ordered within one render encoder. There is
    // no native encoder transition to issue; the explicit RHI boundary is
    // represented by this checked index advance.
    if (renderPassWidth_ == 0u || renderPassHeight_ == 0u) {
        diagnostics = "RHI-METAL-NEXT-SUBPASS-ATTACHMENT-EXTENT-MISSING";
        return false;
    }
    ++renderSubpassIndex_;
    // Viewport and scissor are encoder state on Metal. Restore the RHI
    // default for the new subpass so the previous subpass cannot leak a
    // dynamic rectangle across the explicit boundary.
    const MTLViewport defaultViewport{0.0,
        0.0,
        static_cast<double>(renderPassWidth_),
        static_cast<double>(renderPassHeight_),
        0.0,
        1.0};
    [reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get())
        setViewport:defaultViewport];
    const MTLScissorRect defaultScissor{0u, 0u, renderPassWidth_, renderPassHeight_};
    [reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get())
        setScissorRect:defaultScissor];
    ++stats_->nativeViewportSetCount;
    ++stats_->nativeScissorSetCount;
    tessellationFactorBuffer_.reset();
    tessellationFactorBufferOffset_ = 0u;
    tessellationFactorInstanceStrideBytes_ = 0u;
    return true;
}

bool MetalRhiNativeCommandPayload::recordComputePassBegin(std::pmr::string& diagnostics)
{
    return beginPreparedNativeComputePass({}, diagnostics);
}

bool MetalRhiNativeCommandPayload::beginPreparedNativeComputePass(
    const std::shared_ptr<core::ExternalObjectSlot>& slot, core::pmr::String& diagnostics)
{
    if (ordinaryWriteStates_) {
        diagnostics = "RHI-METAL-ORDINARY-WRITE-CONSUMER-DOMAIN-UNIMPLEMENTED";
        return false;
    }
    if (!retentionRecordingAllows(true, diagnostics))
        return false;
    if (pendingGraphicsBinding_) {
        diagnostics = "RHI-METAL-GRAPHICS-SCOPE-NOT-CONSUMED";
        return false;
    }
    if (!nativeCommandBuffer_ || nativeRenderEncoder_ || nativeComputeEncoder_ || nativeBlitEncoder_
        || (slot && slot->get())) {
        diagnostics = "RHI-METAL-COMPUTE-PASS-COMMAND-BUFFER-MISSING";
        return false;
    }
    id<MTLCommandBuffer> commandBuffer =
        reinterpret_cast<id<MTLCommandBuffer>>(nativeCommandBuffer_.get());
    @autoreleasepool {
        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
        if (encoder == nil) {
            diagnostics = "RHI-METAL-COMPUTE-PASS-ENCODER-CREATE-FAILED";
            return false;
        }
        try {
            std::optional<core::CpuMemoryQuotaScope> quotaScope;
            if (recordingRetention_)
                quotaScope.emplace(recordingRetention_->quota());
            if (slot) {
                [encoder retain];
                if (!slot->adopt(reinterpret_cast<void*>(encoder))) {
                    [encoder endEncoding];
                    [encoder release];
                    diagnostics = "RHI-METAL-ROOT-COMPUTE-ENCODER-SLOT";
                    return false;
                }
                nativeComputeEncoder_ = std::shared_ptr<void>(slot, slot->get());
            } else
                nativeComputeEncoder_ = retainBorrowedNativeObject(encoder);
        } catch (const std::bad_alloc&) {
            [encoder endEncoding];
            poisonRecording("RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED");
            diagnostics = "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED";
            return false;
        }
    }
    stats_->nativeComputeEncoderCreated = true;
    return true;
}

bool MetalRhiNativeCommandPayload::recordComputePipelineBind(
    const std::shared_ptr<MetalRhiNativePipelinePayload>& pipeline, std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    auto* encoder = reinterpret_cast<id<MTLComputeCommandEncoder>>(nativeComputeEncoder_.get());
    id<MTLComputePipelineState> state = pipeline && pipeline->nativeRuntimeBacked
        ? reinterpret_cast<id<MTLComputePipelineState>>(pipeline->pipelineState.get())
        : nil;
    if (encoder == nil || state == nil || !pipeline
        || !commandDeviceMatches(nativeDevice_, pipeline->nativeDevice)) {
        diagnostics = "RHI-METAL-COMPUTE-PIPELINE-EXECUTABLE-PAYLOAD-MISSING";
        return false;
    }
    [encoder setComputePipelineState:state];
    retainResourcePayload(retainedResourcePayloads_, retainedResourcePayloadIdentities_, pipeline);
    computePipeline_ = pipeline;
    stats_->nativeComputePipelineBound = true;
    return true;
}

bool MetalRhiNativeCommandPayload::retainIndexedPipeline(
    const std::shared_ptr<const void>& pipeline, std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    if (!pipeline || abandoned_ || submitted_.load(std::memory_order_acquire)) {
        diagnostics = "RHI-BINDLESS-PIPELINE-RETENTION-INVALID";
        return false;
    }
    if (!retainedBindlessPipelines_.canRetain(pipeline)) {
        diagnostics = "RHI-BINDLESS-PIPELINE-RETENTION-CAPACITY";
        return false;
    }
    return retainedBindlessPipelines_.retain(pipeline);
}

bool MetalRhiNativeCommandPayload::retainIndexedSnapshot(
    const std::shared_ptr<const void>& snapshot, std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    if (!snapshot || abandoned_ || submitted_.load(std::memory_order_acquire)) {
        diagnostics = "RHI-BINDLESS-SNAPSHOT-RETENTION-INVALID";
        return false;
    }
    if (!retainedBindlessSnapshots_.canRetain(snapshot)) {
        diagnostics = "RHI-BINDLESS-SNAPSHOT-RETENTION-CAPACITY";
        return false;
    }
    return retainedBindlessSnapshots_.retain(snapshot);
}

bool MetalRhiNativeCommandPayload::recordBindlessFaultLatchClear(
    std::shared_ptr<const rhi::BindlessSnapshot> snapshot,
    rhi::ResourceState before,
    std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    return recordBindlessFaultLatchClearAt(
        std::move(snapshot), before, BindlessFaultClearPlacement::ResumeCompute, diagnostics);
}

bool MetalRhiNativeCommandPayload::recordBindlessFaultLatchClearBeforePass(
    std::shared_ptr<const rhi::BindlessFaultDomain> snapshot,
    rhi::ResourceState before,
    core::pmr::String& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    return recordBindlessFaultLatchClearAt(
        std::move(snapshot), before, BindlessFaultClearPlacement::BetweenEncoders, diagnostics);
}

bool MetalRhiNativeCommandPayload::recordBindlessFaultLatchClearAt(
    std::shared_ptr<const rhi::BindlessFaultDomain> snapshot,
    rhi::ResourceState before,
    BindlessFaultClearPlacement placement,
    core::pmr::String& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    if (snapshot && !snapshot->sharedOwnerMatches(snapshot)) {
        diagnostics = "RHI-BINDLESS-FAULT-SUBMISSION-OWNER-MISMATCH";
        return false;
    }
    const bool resumeCompute = placement == BindlessFaultClearPlacement::ResumeCompute;
    if (!snapshot || !nativeCommandBuffer_ || nativeRenderEncoder_ || nativeBlitEncoder_
        || (resumeCompute ? (!nativeComputeEncoder_ || !computePipeline_)
                          : bool(nativeComputeEncoder_))
        || abandoned_ || recordingPoisoned_ || stats_->nativeCommandBufferFinished
        || submitted_.load(std::memory_order_acquire)) {
        diagnostics = "RHI-METAL-BINDLESS-FAULT-CLEAR-RECORDING-INVALID";
        return false;
    }
    if (before != rhi::ResourceState::Undefined && before != rhi::ResourceState::CopySrc) {
        diagnostics = "RHI-METAL-BINDLESS-FAULT-CLEAR-PRIOR-STATE";
        return false;
    }
    for (const auto& fault : leasedBindlessFaults_) {
        if (fault.snapshot && fault.snapshot->generation() == snapshot->generation()) {
            diagnostics = "RHI-METAL-BINDLESS-FAULT-GENERATION-DUPLICATE";
            return false;
        }
    }
    for (const auto& pending : pendingBindlessFaultReadbacks_) {
        if (pending.generation == snapshot->generation()) {
            diagnostics = "RHI-METAL-BINDLESS-FAULT-GENERATION-DUPLICATE";
            return false;
        }
    }
    for (const auto& outcome : bindlessFaultOutcomes_) {
        if (outcome.generation == snapshot->generation()) {
            diagnostics = "RHI-METAL-BINDLESS-FAULT-GENERATION-DUPLICATE";
            return false;
        }
    }
    if (leasedBindlessFaults_.size() == leasedBindlessFaults_.capacity()) {
        diagnostics = "RHI-METAL-BINDLESS-FAULT-LEASE-CAPACITY";
        return false;
    }
    auto lease = rhi::BindlessFaultSubmissionLease::acquire(snapshot, diagnostics);
    if (!lease.valid())
        return false;
    if (before != lease.faultLatchPriorState()) {
        diagnostics = "RHI-METAL-BINDLESS-FAULT-CLEAR-PRIOR-STATE";
        return false;
    }
    const auto* latch = lease.faultLatchBuffer();
    auto nativeLatch = latch ? metalRhiNativePayloadFor(*latch) : nullptr;
    const auto registeredLatch =
        latch && resourceRegistry_ ? resourceRegistry_->find(latch->id()) : std::nullopt;
    if (!registeredLatch || registeredLatch->buffer != nativeLatch) {
        diagnostics = "RHI-METAL-BINDLESS-FAULT-DOMAIN-DEVICE-MISMATCH";
        return false;
    }
    id<MTLBuffer> latchBuffer = nativeLatch && nativeLatch->resource.nativeRuntimeBacked
        ? reinterpret_cast<id<MTLBuffer>>(nativeLatch->resource.nativeObject.get())
        : nil;
    if (!latch || !nativeLatch || latchBuffer == nil
        || !commandDeviceMatches(nativeDevice_, nativeLatch->resource.nativeDevice)
        || latch->desc().sizeBytes < sizeof(std::uint32_t)
        || (latch->desc().usage
               & (rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopySrc
                   | rhi::ResourceUsage::CopyDst))
            != (rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopySrc
                | rhi::ResourceUsage::CopyDst)
        || latchBuffer.length < sizeof(std::uint32_t)) {
        diagnostics = "RHI-METAL-BINDLESS-FAULT-CLEAR-BUFFER-INVALID";
        return false;
    }
    if (!retainIndexedSnapshot(snapshot, diagnostics))
        return false;

    // Only the existing Compute contract permits ending/resuming an encoder.
    // Graphics preparation reaches this point with no attachment work open.
    if (resumeCompute) {
        [reinterpret_cast<id<MTLComputeCommandEncoder>>(nativeComputeEncoder_.get()) endEncoding];
        nativeComputeEncoder_.reset();
    }
    id<MTLCommandBuffer> commandBuffer =
        reinterpret_cast<id<MTLCommandBuffer>>(nativeCommandBuffer_.get());
    id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
    if (blit == nil) {
        poisonRecording("RHI-METAL-BINDLESS-FAULT-CLEAR-BLIT-ENCODER-CREATE-FAILED");
        diagnostics = "RHI-METAL-BINDLESS-FAULT-CLEAR-BLIT-ENCODER-CREATE-FAILED";
        return false;
    }
    [blit fillBuffer:latchBuffer range:NSMakeRange(0u, sizeof(std::uint32_t)) value:0u];
    [blit endEncoding];
    if (resumeCompute) {
        id<MTLComputeCommandEncoder> resumed = [commandBuffer computeCommandEncoder];
        id<MTLComputePipelineState> pipelineState = computePipeline_->nativeRuntimeBacked
            ? reinterpret_cast<id<MTLComputePipelineState>>(computePipeline_->pipelineState.get())
            : nil;
        if (resumed == nil || pipelineState == nil) {
            if (resumed != nil)
                [resumed endEncoding];
            poisonRecording("RHI-METAL-BINDLESS-FAULT-CLEAR-COMPUTE-RESUME-FAILED");
            diagnostics = "RHI-METAL-BINDLESS-FAULT-CLEAR-COMPUTE-RESUME-FAILED";
            return false;
        }
        [resumed setComputePipelineState:pipelineState];
        nativeComputeEncoder_ = retainBorrowedNativeObject(resumed);
    }
    retainResourcePayload(
        retainedResourcePayloads_, retainedResourcePayloadIdentities_, nativeLatch);
    leasedBindlessFaults_.push_back({std::move(snapshot), std::move(lease)});
    stats_->nativeBlitEncoderCreated = true;
    stats_->nativeBlitCopyEncoded = true;
    diagnostics.clear();
    return true;
}

std::shared_ptr<MetalRhiBindlessFaultReadback>
MetalRhiNativeCommandPayload::scheduleBindlessFaultReadback(
    std::shared_ptr<const rhi::BindlessFaultDomain> snapshot, std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return {};
    if (snapshot && !snapshot->sharedOwnerMatches(snapshot)) {
        diagnostics = "RHI-BINDLESS-FAULT-SUBMISSION-OWNER-MISMATCH";
        return {};
    }
    if (!snapshot || !nativeCommandBuffer_ || nativeComputeEncoder_ || nativeRenderEncoder_
        || nativeBlitEncoder_ || abandoned_ || recordingPoisoned_
        || stats_->nativeCommandBufferFinished || submitted_.load(std::memory_order_acquire)) {
        diagnostics = "RHI-METAL-BINDLESS-FAULT-READBACK-RECORDING-INVALID";
        return {};
    }
    if (preparedFaultOutcomes_.empty()
        || pendingBindlessFaultReadbacks_.size() >= maxBindlessSnapshots_
        || bindlessFaultOutcomes_.size() >= maxBindlessSnapshots_) {
        diagnostics = "RHI-METAL-BINDLESS-FAULT-READBACK-CAPACITY";
        return {};
    }
    auto leaseIt = leasedBindlessFaults_.end();
    for (auto it = leasedBindlessFaults_.begin(); it != leasedBindlessFaults_.end(); ++it) {
        if (it->snapshot == snapshot) {
            leaseIt = it;
            break;
        }
    }
    if (leaseIt == leasedBindlessFaults_.end()) {
        diagnostics = "RHI-METAL-BINDLESS-FAULT-LEASE-MISSING";
        return {};
    }
    for (const auto& outcome : bindlessFaultOutcomes_) {
        if (outcome.generation == snapshot->generation()) {
            diagnostics = "RHI-METAL-BINDLESS-FAULT-GENERATION-DUPLICATE";
            return {};
        }
    }
    auto* latch = leaseIt->lease.faultLatchBuffer();
    auto* destination = snapshot->faultReadbackBuffer();
    auto nativeLatch = latch ? metalRhiNativePayloadFor(*latch) : nullptr;
    auto nativeDestination = destination ? metalRhiNativePayloadFor(*destination) : nullptr;
    const auto registeredDestination = destination && resourceRegistry_
        ? resourceRegistry_->find(destination->id())
        : std::nullopt;
    if (!registeredDestination || registeredDestination->buffer != nativeDestination) {
        diagnostics = "RHI-METAL-BINDLESS-FAULT-DOMAIN-DEVICE-MISMATCH";
        return {};
    }
    id<MTLBuffer> source = nativeLatch && nativeLatch->resource.nativeRuntimeBacked
        ? reinterpret_cast<id<MTLBuffer>>(nativeLatch->resource.nativeObject.get())
        : nil;
    id<MTLBuffer> target = nativeDestination && nativeDestination->resource.nativeRuntimeBacked
        ? reinterpret_cast<id<MTLBuffer>>(nativeDestination->resource.nativeObject.get())
        : nil;
    if (!latch || !destination || !nativeLatch || !nativeDestination || source == nil
        || target == nil || !commandDeviceMatches(nativeDevice_, nativeLatch->resource.nativeDevice)
        || !commandDeviceMatches(nativeDevice_, nativeDestination->resource.nativeDevice)
        || latch->desc().sizeBytes < sizeof(std::uint32_t)
        || destination->desc().sizeBytes < sizeof(std::uint32_t)
        || (latch->desc().usage & (rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopySrc))
            != (rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopySrc)
        || (destination->desc().usage & rhi::ResourceUsage::CopyDst) != rhi::ResourceUsage::CopyDst
        || destination->desc().memory != rhi::MemoryUsage::GpuToCpu
        || (nativeDestination->resource.storageMode != MetalRhiNativeStorageMode::Shared
            && nativeDestination->resource.storageMode != MetalRhiNativeStorageMode::Managed)) {
        diagnostics = "RHI-METAL-BINDLESS-FAULT-READBACK-BUFFER-INVALID";
        return {};
    }
    const auto result = preparedFaultOutcomes_.back();
    if (pendingGraphicsBinding_) {
        diagnostics = "RHI-METAL-GRAPHICS-SCOPE-NOT-CONSUMED";
        return {};
    }
    // This copy is recorded and ended in one native ownership scope. The
    // pending destination/domain below provide the asynchronous GPU lifetime.
    @autoreleasepool {
        const auto command = reinterpret_cast<id<MTLCommandBuffer>>(nativeCommandBuffer_.get());
        id<MTLBlitCommandEncoder> encoder = [command blitCommandEncoder];
        if (encoder == nil) {
            diagnostics = "RHI-METAL-BLIT-PASS-ENCODER-CREATE-FAILED";
            return {};
        }
        stats_->nativeBlitEncoderCreated = true;
        [encoder copyFromBuffer:source
                   sourceOffset:0u
                       toBuffer:target
              destinationOffset:0u
                           size:sizeof(std::uint32_t)];
#if !TARGET_OS_IPHONE
        if (nativeDestination->resource.storageMode == MetalRhiNativeStorageMode::Managed) {
            if (@available(macOS 10.11, *)) {
                [encoder synchronizeResource:target];
            } else {
                [encoder endEncoding];
                poisonRecording("RHI-METAL-BINDLESS-FAULT-READBACK-SYNCHRONIZE-UNAVAILABLE");
                diagnostics = "RHI-METAL-BINDLESS-FAULT-READBACK-SYNCHRONIZE-UNAVAILABLE";
                return {};
            }
        }
#endif
        [encoder endEncoding];
    }
    const auto generation = snapshot->generation();
    pendingBindlessFaultReadbacks_.push_back(
        {generation, snapshot, std::move(leaseIt->lease), nativeDestination, result});
    bindlessFaultOutcomes_.push_back({generation, result});
    preparedFaultOutcomes_.pop_back();
    ++stats_->nativeBindlessFaultOutcomeConsumeCount;
    leasedBindlessFaults_.erase(leaseIt);
    stats_->nativeBlitCopyEncoded = true;
    diagnostics.clear();
    return result;
}

rhi::BindlessFaultReport MetalRhiNativeCommandPayload::bindlessFaultReport(
    std::uint64_t generation) const noexcept
{
    for (const auto& outcome : bindlessFaultOutcomes_) {
        if (outcome.generation == generation && outcome.result) {
            return {generation, outcome.result->status(), outcome.result->faultBits()};
        }
    }
    return {generation, rhi::BindlessFaultValidationStatus::Unavailable, 0u};
}

bool MetalRhiNativeCommandPayload::recordDispatch(
    std::uint32_t x, std::uint32_t y, std::uint32_t z, std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(true, diagnostics))
        return false;
    auto* encoder = reinterpret_cast<id<MTLComputeCommandEncoder>>(nativeComputeEncoder_.get());
    if (encoder == nil || computePipeline_ == nullptr || x == 0 || y == 0 || z == 0) {
        diagnostics = "RHI-METAL-DISPATCH-EXECUTABLE-PAYLOAD-MISSING";
        return false;
    }
    id<MTLComputePipelineState> pipeline =
        reinterpret_cast<id<MTLComputePipelineState>>(computePipeline_->pipelineState.get());
    if (pipeline == nil || computePipeline_->computeWorkgroupSizeX == 0
        || computePipeline_->computeWorkgroupSizeY == 0
        || computePipeline_->computeWorkgroupSizeZ == 0) {
        diagnostics = "RHI-METAL-DISPATCH-MISSING-WORKGROUP-LOWERING-METADATA";
        return false;
    }
    if (recordingRetention_) {
        if (!ordinaryComputeContract_
            || ordinaryComputeContract_->function().pipeline != computePipeline_) {
            diagnostics = "RHI-METAL-ORDINARY-PREPARED-BINDING-REQUIRED";
            return false;
        }
        for (const auto& binding : ordinaryComputeBindings_) {
            if (!binding)
                continue;
            const auto valid = binding->validateObservation();
            if (!valid.success) {
                diagnostics = valid.diagnostics;
                return false;
            }
            const auto& function = binding->function();
            if (function.pipeline != computePipeline_
                || function.function != computePipeline_->computeFunction
                || binding->quota() != recordingRetention_->quota()) {
                diagnostics = "RHI-METAL-ORDINARY-PREPARED-RECORDING-MISMATCH";
                return false;
            }
            MetalRhiNativeFunctionBindingRecording recording(
                function, metalRhiPreparedComputeLayoutValidator(*computePipeline_));
            if (!recording.validate(diagnostics))
                return false;
        }
        if (!ordinaryComputePipelineValid(*computePipeline_, nativeDevice_)) {
            diagnostics = "RHI-METAL-ORDINARY-PREPARED-PIPELINE-INVALID";
            return false;
        }
        if (ordinaryComputeContract_->requiredPushBytes()) {
            if (!ordinaryComputePush_ || ordinaryComputePush_->pipeline() != computePipeline_
                || ordinaryComputePush_->bytes().size()
                    != ordinaryComputeContract_->requiredPushBytes()
                || ordinaryComputePush_->quota() != recordingRetention_->quota()) {
                diagnostics = "[PC-E011] RHI-METAL-ORDINARY-PUSH-REQUIRED";
                return false;
            }
            const auto valid = ordinaryComputePush_->validate();
            if (!valid.success) {
                diagnostics = valid.diagnostics;
                return false;
            }
        }
        const auto complete = ordinaryComputeDescriptors_.validateComplete(
            ordinaryComputeContract_->consumption(), rhi::ShaderStage::Compute);
        if (!complete.success) {
            diagnostics = complete.diagnostics;
            return false;
        }
    }
    const MTLSize groups = MTLSizeMake(x, y, z);
    const MTLSize threads = MTLSizeMake(computePipeline_->computeWorkgroupSizeX,
        computePipeline_->computeWorkgroupSizeY,
        computePipeline_->computeWorkgroupSizeZ);
    [encoder dispatchThreadgroups:groups threadsPerThreadgroup:threads];
    if (recordingRetention_)
        ordinaryComputePushState_.onConsume();
    stats_->nativeDispatchEncoded = true;
    return true;
}

bool MetalRhiNativeCommandPayload::recordDispatchIndirect(
    const std::shared_ptr<MetalRhiNativeBufferPayload>& indirectBuffer,
    std::uint64_t indirectOffset,
    std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    auto* encoder = reinterpret_cast<id<MTLComputeCommandEncoder>>(nativeComputeEncoder_.get());
    id<MTLBuffer> indirect = nativeBuffer(indirectBuffer);
    if (encoder == nil || indirect == nil || computePipeline_ == nullptr
        || !commandDeviceMatches(
            nativeDevice_, indirectBuffer ? indirectBuffer->resource.nativeDevice : nullptr)
        || (indirectOffset & 3u) != 0u || indirectOffset > indirect.length
        || !fitsNSUInteger(indirectOffset)
        || sizeof(std::uint32_t) * 3u > indirect.length - indirectOffset
        || computePipeline_->computeWorkgroupSizeX == 0
        || computePipeline_->computeWorkgroupSizeY == 0
        || computePipeline_->computeWorkgroupSizeZ == 0) {
        diagnostics = "RHI-METAL-DISPATCH-INDIRECT-EXECUTABLE-PAYLOAD-MISSING";
        return false;
    }
    const MTLSize threads = MTLSizeMake(computePipeline_->computeWorkgroupSizeX,
        computePipeline_->computeWorkgroupSizeY,
        computePipeline_->computeWorkgroupSizeZ);
    [encoder dispatchThreadgroupsWithIndirectBuffer:indirect
                               indirectBufferOffset:static_cast<NSUInteger>(indirectOffset)
                              threadsPerThreadgroup:threads];
    retainResourcePayload(
        retainedResourcePayloads_, retainedResourcePayloadIdentities_, indirectBuffer);
    stats_->nativeDispatchEncoded = true;
    return true;
}

bool MetalRhiNativeCommandPayload::recordComputePassEnd(std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(true, diagnostics))
        return false;
    auto* encoder = reinterpret_cast<id<MTLComputeCommandEncoder>>(nativeComputeEncoder_.get());
    if (encoder == nil) {
        diagnostics = "RHI-METAL-COMPUTE-PASS-END-ENCODER-MISSING";
        return false;
    }
    [encoder endEncoding];
    nativeComputeEncoder_.reset();
    computePipeline_.reset();
    ordinaryComputeContract_.reset();
    ordinaryComputeDescriptors_.reset();
    ordinaryComputeBindings_ = {};
    ordinaryComputePush_.reset();
    ordinaryComputePushState_.onPassEnd();
    return true;
}

bool MetalRhiNativeCommandPayload::recordBlitPassBegin(std::pmr::string& diagnostics)
{
    return beginPreparedNativeBlitPass({}, diagnostics);
}

bool MetalRhiNativeCommandPayload::beginPreparedNativeBlitPass(
    const std::shared_ptr<core::ExternalObjectSlot>& slot, core::pmr::String& diagnostics)
{
    if (ordinaryWriteStates_) {
        diagnostics = "RHI-METAL-ORDINARY-WRITE-CONSUMER-DOMAIN-UNIMPLEMENTED";
        return false;
    }
    if (!retentionRecordingAllows(true, diagnostics))
        return false;
    if (pendingGraphicsBinding_) {
        diagnostics = "RHI-METAL-GRAPHICS-SCOPE-NOT-CONSUMED";
        return false;
    }
    if (!nativeCommandBuffer_ || nativeRenderEncoder_ || nativeComputeEncoder_ || nativeBlitEncoder_
        || (slot && slot->get())) {
        diagnostics = "RHI-METAL-BLIT-PASS-ENCODER-ALREADY-ACTIVE";
        return false;
    }
    auto commandBuffer = reinterpret_cast<id<MTLCommandBuffer>>(nativeCommandBuffer_.get());
    @autoreleasepool {
        id<MTLBlitCommandEncoder> encoder =
            commandBuffer == nil ? nil : [commandBuffer blitCommandEncoder];
        if (encoder == nil) {
            diagnostics = "RHI-METAL-BLIT-PASS-ENCODER-CREATE-FAILED";
            return false;
        }
        try {
            std::optional<core::CpuMemoryQuotaScope> quotaScope;
            if (recordingRetention_)
                quotaScope.emplace(recordingRetention_->quota());
            if (slot) {
                [encoder retain];
                if (!slot->adopt(reinterpret_cast<void*>(encoder))) {
                    [encoder endEncoding];
                    [encoder release];
                    diagnostics = "RHI-METAL-ROOT-BLIT-ENCODER-SLOT";
                    return false;
                }
                nativeBlitEncoder_ = std::shared_ptr<void>(slot, slot->get());
            } else
                nativeBlitEncoder_ = retainBorrowedNativeObject(encoder);
        } catch (const std::bad_alloc&) {
            [encoder endEncoding];
            poisonRecording("RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED");
            diagnostics = "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED";
            return false;
        }
    }
    stats_->nativeBlitEncoderCreated = true;
    return true;
}

bool MetalRhiNativeCommandPayload::retainTransferResources(const MetalRhiRegisteredResource* source,
    const MetalRhiRegisteredResource& destination,
    const std::shared_ptr<void>& staging,
    core::pmr::String& diagnostics)
{
    if ((source && staging) || (!source && !staging) || (source && !source->payloadOwner())
        || !destination.payloadOwner()) {
        diagnostics = "RHI-METAL-TRANSFER-PAYLOAD-OWNER-INVALID";
        return false;
    }
    if (recordingRetention_) {
        // Fixed schema slots, compacted by nonnull ownership; no dynamic list.
        constexpr auto ownerSlots =
            2u * std::tuple_size<MetalRhiResourceOwnerProjection::Owners>::value + 1u;
        std::array<std::shared_ptr<const void>, ownerSlots> owners{};
        std::size_t count = 0u;
        const auto add = [&](std::shared_ptr<const void> owner) {
            if (owner)
                owners[count++] = std::move(owner);
        };
        add(resourceRegistry_);
        if (source) {
            for (auto& owner : MetalRhiResourceOwnerProjection::owners(source->kind,
                     source->buffer,
                     source->texture,
                     source->textureView,
                     source->sampler))
                add(std::move(owner));
        } else {
            const auto inspected = inspectMetalRhiNativeStagingBuffer(staging);
            if (!inspected.nativeRuntimeBacked || !inspected.nativeDevice
                || !inspected.nativeBuffer) {
                diagnostics = "RHI-METAL-TRANSFER-STAGING-OWNER-INVALID";
                return false;
            }
            add(inspected.nativeDevice);
            add(inspected.nativeBuffer);
            add(staging);
        }
        for (auto& owner : MetalRhiResourceOwnerProjection::owners(destination.kind,
                 destination.buffer,
                 destination.texture,
                 destination.textureView,
                 destination.sampler))
            add(std::move(owner));
        if (const auto* diagnostic = recordingRetention_->retainResources(owners.data(), count)) {
            diagnostics = diagnostic;
            return false;
        }
    } else {
        if (source)
            retainRegisteredResourcePayloads(
                retainedResourcePayloads_, retainedResourcePayloadIdentities_, *source);
        else
            retainedStagingPayloads_.push_back(staging);
        retainRegisteredResourcePayloads(
            retainedResourcePayloads_, retainedResourcePayloadIdentities_, destination);
    }
    return true;
}

bool MetalRhiNativeCommandPayload::recordBlitCopy(
    const rhi::BufferCopyDesc& desc, std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(true, diagnostics))
        return false;
    auto encoder = reinterpret_cast<id<MTLBlitCommandEncoder>>(nativeBlitEncoder_.get());
    const auto source =
        resourceRegistry_ ? resourceRegistry_->find(desc.sourceBuffer) : std::nullopt;
    const auto destination =
        resourceRegistry_ ? resourceRegistry_->find(desc.destinationBuffer) : std::nullopt;
    id<MTLBuffer> sourceBuffer =
        source && source->buffer && source->buffer->resource.nativeRuntimeBacked
        ? reinterpret_cast<id<MTLBuffer>>(source->buffer->resource.nativeObject.get())
        : nil;
    id<MTLBuffer> destinationBuffer =
        destination && destination->buffer && destination->buffer->resource.nativeRuntimeBacked
        ? reinterpret_cast<id<MTLBuffer>>(destination->buffer->resource.nativeObject.get())
        : nil;
    if (encoder == nil || sourceBuffer == nil || destinationBuffer == nil || !source || !destination
        || !source->payloadOwner() || !destination->payloadOwner()
        || !commandDeviceMatches(nativeDevice_, nativeRegisteredDevice(*source))
        || !commandDeviceMatches(nativeDevice_, nativeRegisteredDevice(*destination))
        || source->kind != rhi::DescriptorResourceKind::Buffer
        || destination->kind != rhi::DescriptorResourceKind::Buffer
        || destination->gpuTransferWritesSealed
        || !hasResourceUsage(source->bufferUsage, rhi::ResourceUsage::CopySrc)
        || !hasResourceUsage(destination->bufferUsage, rhi::ResourceUsage::CopyDst)
        || desc.sizeBytes == 0 || desc.sourceOffsetBytes > sourceBuffer.length
        || desc.destinationOffsetBytes > destinationBuffer.length
        || !fitsNSUInteger(desc.sourceOffsetBytes) || !fitsNSUInteger(desc.destinationOffsetBytes)
        || !fitsNSUInteger(desc.sizeBytes)
        || desc.sizeBytes > sourceBuffer.length - desc.sourceOffsetBytes
        || desc.sizeBytes > destinationBuffer.length - desc.destinationOffsetBytes) {
        diagnostics = "RHI-METAL-BLIT-BUFFER-COPY-RESOURCES-INVALID";
        return false;
    }
    // Availability rejection must precede the copy as well as owner mutation.
#if !TARGET_OS_IPHONE
    if (destination->buffer->resource.storageMode == MetalRhiNativeStorageMode::Managed) {
        if (@available(macOS 10.11, *)) {
        } else {
            diagnostics = "RHI-METAL-MANAGED-READBACK-SYNCHRONIZE-API-UNAVAILABLE";
            return false;
        }
    }
#endif
    if (!retainTransferResources(&*source, *destination, {}, diagnostics))
        return false;
    [encoder copyFromBuffer:sourceBuffer
               sourceOffset:static_cast<NSUInteger>(desc.sourceOffsetBytes)
                   toBuffer:destinationBuffer
          destinationOffset:static_cast<NSUInteger>(desc.destinationOffsetBytes)
                       size:static_cast<NSUInteger>(desc.sizeBytes)];
#if !TARGET_OS_IPHONE
    if (destination->buffer->resource.storageMode == MetalRhiNativeStorageMode::Managed) {
        if (@available(macOS 10.11, *)) {
            [encoder synchronizeResource:destinationBuffer];
        }
    }
#endif
    stats_->nativeBlitCopyEncoded = true;
    return true;
}

bool MetalRhiNativeCommandPayload::recordBlitBufferToTexture(
    const rhi::BufferToTextureCopyDesc& desc, std::pmr::string& diagnostics)
{
    if (const auto* id =
            metalRhiTextureWriteDiagnostic(resourceRegistry_, desc.destinationTexture)) {
        diagnostics = id;
        return false;
    }
    if (!retentionRecordingAllows(true, diagnostics))
        return false;
    auto encoder = reinterpret_cast<id<MTLBlitCommandEncoder>>(nativeBlitEncoder_.get());
    const auto source =
        resourceRegistry_ ? resourceRegistry_->find(desc.sourceBuffer) : std::nullopt;
    const auto destination =
        resourceRegistry_ ? resourceRegistry_->find(desc.destinationTexture) : std::nullopt;
    id<MTLBuffer> sourceBuffer =
        source && source->buffer && source->buffer->resource.nativeRuntimeBacked
        ? reinterpret_cast<id<MTLBuffer>>(source->buffer->resource.nativeObject.get())
        : nil;
    id<MTLTexture> destinationTexture = destination ? nativeRegisteredTexture(*destination) : nil;
    if (encoder == nil || sourceBuffer == nil || destinationTexture == nil || !source
        || !destination || !source->payloadOwner() || !destination->payloadOwner()
        || !commandDeviceMatches(nativeDevice_, nativeRegisteredDevice(*source))
        || !commandDeviceMatches(nativeDevice_, nativeRegisteredDevice(*destination))
        || source->kind != rhi::DescriptorResourceKind::Buffer
        || destination->kind != rhi::DescriptorResourceKind::TextureView
        || !hasResourceUsage(source->bufferUsage, rhi::ResourceUsage::CopySrc)
        || !hasTextureUsage(destination->textureUsage, rhi::TextureUsage::CopyDst)
        || desc.textureSubresources.mipLevelCount == 0u
        || desc.textureSubresources.arrayLayerCount == 0u
        || (desc.mipLayouts.empty()
                ? desc.textureSubresources.mipLevelCount != 1u
                : desc.mipLayouts.size() != desc.textureSubresources.mipLevelCount)) {
        diagnostics = "RHI-METAL-BLIT-BUFFER-TEXTURE-COPY-RESOURCES-INVALID";
        return false;
    }
    if (desc.textureSubresources.baseMipLevel >= destination->textureMipLevelCount
        || desc.textureSubresources.mipLevelCount
            > destination->textureMipLevelCount - desc.textureSubresources.baseMipLevel
        || desc.textureSubresources.baseArrayLayer >= destination->textureArrayLayerCount
        || desc.textureSubresources.arrayLayerCount
            > destination->textureArrayLayerCount - desc.textureSubresources.baseArrayLayer) {
        diagnostics = "RHI-METAL-BLIT-BUFFER-TEXTURE-COPY-SUBRESOURCE-RANGE-INVALID";
        return false;
    }
    auto makeMapper = [&] {
        return [&](std::size_t mipIndex, MetalTextureCopyPlan& plan) mutable -> bool {
            std::uint32_t mipLevel = desc.textureSubresources.baseMipLevel;
            rhi::Extent3D extent = desc.extent;
            std::uint32_t bytesPerRow = desc.bytesPerRow;
            std::uint32_t rowsPerImage = desc.rowsPerImage;
            std::uint64_t mipOffset = 0u;
            if (!desc.mipLayouts.empty()) {
                const auto& layout = desc.mipLayouts[mipIndex];
                mipLevel = layout.mipLevel;
                extent = layout.extent;
                bytesPerRow = layout.bytesPerRow;
                rowsPerImage = layout.rowsPerImage;
                mipOffset = layout.bufferOffsetBytes;
                if (mipIndex > std::numeric_limits<std::uint32_t>::max()
                            - desc.textureSubresources.baseMipLevel
                    || mipLevel != desc.textureSubresources.baseMipLevel + mipIndex) {
                    diagnostics = "RHI-METAL-BLIT-BUFFER-TEXTURE-MIP-LAYOUT-ORDER-INVALID";
                    return false;
                }
            }
            std::uint64_t bytesPerImage = 0u;
            std::uint64_t requiredBytes = 0u;
            if (!validTextureCopyLayout(destination->textureFormat,
                    extent,
                    bytesPerRow,
                    rowsPerImage,
                    bytesPerImage,
                    requiredBytes)
                || (bytesPerRow % rhi::kTextureUploadRowPitchAlignment) != 0u) {
                diagnostics = "RHI-METAL-BLIT-BUFFER-TEXTURE-COPY-LAYOUT-INVALID";
                return false;
            }
            auto range = desc.textureSubresources;
            range.baseMipLevel = mipLevel;
            range.mipLevelCount = 1u;
            range.arrayLayerCount = 1u;
            if (!validTextureRegion(
                    destinationTexture, destination->textureFormat, range, extent)) {
                diagnostics = "RHI-METAL-BLIT-BUFFER-TEXTURE-COPY-REGION-INVALID";
                return false;
            }
            std::uint64_t baseSourceOffset = 0u;
            if (!checkedAdd(desc.bufferOffsetBytes, mipOffset, baseSourceOffset)) {
                diagnostics = "RHI-METAL-BLIT-BUFFER-TEXTURE-COPY-BUFFER-RANGE-INVALID";
                return false;
            }
            for (std::uint32_t layerIndex = 0u;
                layerIndex < desc.textureSubresources.arrayLayerCount;
                ++layerIndex) {
                if (layerIndex > std::numeric_limits<std::uint32_t>::max()
                        - desc.textureSubresources.baseArrayLayer) {
                    diagnostics = "RHI-METAL-BLIT-BUFFER-TEXTURE-COPY-LAYER-INVALID";
                    return false;
                }
                const auto layer = desc.textureSubresources.baseArrayLayer + layerIndex;
                if (layer < desc.textureSubresources.baseArrayLayer
                    || layer >= destinationTexture.arrayLength) {
                    diagnostics = "RHI-METAL-BLIT-BUFFER-TEXTURE-COPY-LAYER-INVALID";
                    return false;
                }
                std::uint64_t layerOffset = 0u;
                std::uint64_t sourceOffset = 0u;
                if (!checkedMultiply(bytesPerImage, layerIndex, layerOffset)
                    || !checkedAdd(baseSourceOffset, layerOffset, sourceOffset)
                    || sourceOffset > sourceBuffer.length || !fitsNSUInteger(sourceOffset)
                    || requiredBytes > sourceBuffer.length - sourceOffset) {
                    diagnostics = "RHI-METAL-BLIT-BUFFER-TEXTURE-COPY-BUFFER-RANGE-INVALID";
                    return false;
                }
            }
            plan = {
                mipLevel,
                mipLevel,
                extent,
                bytesPerRow,
                bytesPerImage,
                requiredBytes,
                baseSourceOffset,
            };
            return true;
        };
    };
    auto admit = [&] {
        return retainTransferResources(&*source, *destination, {}, diagnostics);
    };
    auto consume = [&](const MetalTextureCopyPlan& plan) {
        const MTLSize size = MTLSizeMake(plan.extent.width, plan.extent.height, plan.extent.depth);
        for (std::uint32_t layerIndex = 0u; layerIndex < desc.textureSubresources.arrayLayerCount;
            ++layerIndex) {
            const auto layer = desc.textureSubresources.baseArrayLayer + layerIndex;
            const auto sourceOffset = plan.bufferOffsetBytes + plan.bytesPerImage * layerIndex;
            [encoder copyFromBuffer:sourceBuffer
                       sourceOffset:static_cast<NSUInteger>(sourceOffset)
                  sourceBytesPerRow:plan.bytesPerRow
                sourceBytesPerImage:static_cast<NSUInteger>(plan.bytesPerImage)
                         sourceSize:size
                          toTexture:destinationTexture
                   destinationSlice:layer
                   destinationLevel:plan.destinationMipLevel
                  destinationOrigin:MTLOriginMake(0, 0, 0)];
        }
    };
    const auto result = core::PreflightReplay<MetalTextureCopyPlan>::run(
        desc.textureSubresources.mipLevelCount, makeMapper, admit, consume, preflightOnly_);
    if (result == core::PreflightReplayStatus::ValidatedOnly)
        return true;
    if (result == core::PreflightReplayStatus::ReplayRejected) {
        diagnostics = "RHI-METAL-TEXTURE-COPY-REPLAY-MAPPING-CHANGED";
        poisonRecording(diagnostics);
        return false;
    }
    if (result != core::PreflightReplayStatus::Completed)
        return false;
    stats_->nativeBlitCopyEncoded = true;
    return true;
}

bool MetalRhiNativeCommandPayload::recordUploadStagingBufferCopy(
    const std::shared_ptr<void>& stagingPayload,
    std::uint64_t stagingOffset,
    std::uint64_t sizeBytes,
    rhi::GpuResourceId destinationBuffer,
    std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(true, diagnostics))
        return false;
    auto encoder = reinterpret_cast<id<MTLBlitCommandEncoder>>(nativeBlitEncoder_.get());
    const auto staging = inspectMetalRhiNativeStagingBuffer(stagingPayload);
    const auto destination =
        resourceRegistry_ ? resourceRegistry_->find(destinationBuffer) : std::nullopt;
    id<MTLBuffer> sourceBuffer = staging.nativeRuntimeBacked
        ? reinterpret_cast<id<MTLBuffer>>(staging.nativeBuffer.get())
        : nil;
    id<MTLBuffer> destinationNative =
        destination && destination->buffer && destination->buffer->resource.nativeRuntimeBacked
        ? reinterpret_cast<id<MTLBuffer>>(destination->buffer->resource.nativeObject.get())
        : nil;
    if (encoder == nil || sourceBuffer == nil || destinationNative == nil || !destination
        || !destination->payloadOwner()
        || !commandDeviceMatches(nativeDevice_, staging.nativeDevice)
        || !commandDeviceMatches(nativeDevice_, nativeRegisteredDevice(*destination))
        || destination->kind != rhi::DescriptorResourceKind::Buffer
        || destination->gpuTransferWritesSealed
        || !hasResourceUsage(destination->bufferUsage, rhi::ResourceUsage::CopyDst)
        || sizeBytes == 0 || !fitsNSUInteger(stagingOffset) || !fitsNSUInteger(sizeBytes)
        || stagingOffset > staging.sizeBytes || sizeBytes > staging.sizeBytes - stagingOffset
        || sizeBytes > destinationNative.length) {
        diagnostics = "RHI-METAL-STAGING-BUFFER-COPY-RESOURCES-INVALID";
        return false;
    }
#if !TARGET_OS_IPHONE
    if (destination->buffer->resource.storageMode == MetalRhiNativeStorageMode::Managed) {
        if (@available(macOS 10.11, *)) {
        } else {
            diagnostics = "RHI-METAL-MANAGED-READBACK-SYNCHRONIZE-API-UNAVAILABLE";
            return false;
        }
    }
#endif
    if (!retainTransferResources(nullptr, *destination, stagingPayload, diagnostics))
        return false;
    [encoder copyFromBuffer:sourceBuffer
               sourceOffset:static_cast<NSUInteger>(stagingOffset)
                   toBuffer:destinationNative
          destinationOffset:0
                       size:static_cast<NSUInteger>(sizeBytes)];
#if !TARGET_OS_IPHONE
    if (destination->buffer->resource.storageMode == MetalRhiNativeStorageMode::Managed) {
        if (@available(macOS 10.11, *))
            [encoder synchronizeResource:destinationNative];
    }
#endif
    stats_->nativeBlitCopyEncoded = true;
    return true;
}

bool MetalRhiNativeCommandPayload::recordUploadStagingTextureCopy(
    const std::shared_ptr<void>& stagingPayload,
    std::uint64_t stagingOffset,
    const rhi::UploadTextureDesc& desc,
    std::pmr::string& diagnostics)
{
    if (const auto* id =
            metalRhiTextureWriteDiagnostic(resourceRegistry_, desc.destinationTexture)) {
        diagnostics = id;
        return false;
    }
    if (!retentionRecordingAllows(true, diagnostics))
        return false;
    auto encoder = reinterpret_cast<id<MTLBlitCommandEncoder>>(nativeBlitEncoder_.get());
    const auto staging = inspectMetalRhiNativeStagingBuffer(stagingPayload);
    const auto destination =
        resourceRegistry_ ? resourceRegistry_->find(desc.destinationTexture) : std::nullopt;
    id<MTLBuffer> sourceBuffer = staging.nativeRuntimeBacked
        ? reinterpret_cast<id<MTLBuffer>>(staging.nativeBuffer.get())
        : nil;
    id<MTLTexture> destinationTexture = destination ? nativeRegisteredTexture(*destination) : nil;
    const auto range = desc.subresources;
    if (encoder == nil || sourceBuffer == nil || destinationTexture == nil || !destination
        || !destination->payloadOwner()
        || !commandDeviceMatches(nativeDevice_, staging.nativeDevice)
        || !commandDeviceMatches(nativeDevice_, nativeRegisteredDevice(*destination))
        || destination->kind != rhi::DescriptorResourceKind::TextureView
        || !hasTextureUsage(destination->textureUsage, rhi::TextureUsage::CopyDst)
        || destination->textureFormat != desc.format || desc.format == rhi::TextureFormat::Unknown
        || range.mipLevelCount == 0u || range.arrayLayerCount == 0u
        || !fitsNSUInteger(stagingOffset) || stagingOffset > staging.sizeBytes
        || desc.pixelDataSize > staging.sizeBytes - stagingOffset) {
        diagnostics = "RHI-METAL-STAGING-TEXTURE-COPY-RESOURCES-INVALID";
        return false;
    }
    if (range.baseArrayLayer > destination->textureArrayLayerCount
        || range.arrayLayerCount > destination->textureArrayLayerCount - range.baseArrayLayer
        || range.baseMipLevel >= destination->textureMipLevelCount
        || range.mipLevelCount > destination->textureMipLevelCount - range.baseMipLevel
        || (desc.mipLayouts.empty() ? range.mipLevelCount != 1u
                                    : desc.mipLayouts.size() != range.mipLevelCount)) {
        diagnostics = "RHI-METAL-STAGING-TEXTURE-COPY-RESOURCES-INVALID";
        return false;
    }
    auto makeMapper = [&] {
        return [&, previousMipEnd = std::uint64_t{0}](
                   std::size_t mipIndex, MetalTextureCopyPlan& plan) mutable -> bool {
            std::uint32_t mipLevel = range.baseMipLevel;
            rhi::Extent3D extent = desc.extent;
            std::uint32_t bytesPerRow = desc.bytesPerRow;
            std::uint32_t rowsPerImage = desc.rowsPerImage;
            std::uint64_t mipOffset = 0u;
            if (!desc.mipLayouts.empty()) {
                const auto& layout = desc.mipLayouts[mipIndex];
                if (mipIndex > std::numeric_limits<std::uint32_t>::max() - range.baseMipLevel
                    || layout.mipLevel != range.baseMipLevel + mipIndex) {
                    diagnostics = "RHI-METAL-STAGING-TEXTURE-COPY-MIP-LAYOUT-ORDER-INVALID";
                    return false;
                }
                mipLevel = layout.mipLevel;
                extent = layout.extent;
                bytesPerRow = layout.bytesPerRow;
                rowsPerImage = layout.rowsPerImage;
                mipOffset = layout.bufferOffsetBytes;
            }
            std::uint64_t bytesPerImage = 0u;
            std::uint64_t requiredBytes = 0u;
            std::uint64_t mipEnd = 0u;
            if (!validTextureCopyLayout(
                    desc.format, extent, bytesPerRow, rowsPerImage, bytesPerImage, requiredBytes)
                || bytesPerRow % rhi::kTextureUploadRowPitchAlignment != 0u) {
                diagnostics = "RHI-METAL-STAGING-TEXTURE-COPY-LAYOUT-INVALID";
                return false;
            }
            auto singleLayerRange = range;
            singleLayerRange.baseMipLevel = mipLevel;
            singleLayerRange.mipLevelCount = 1u;
            singleLayerRange.arrayLayerCount = 1u;
            if (!validTextureRegionAt(destinationTexture,
                    destination->textureFormat,
                    singleLayerRange,
                    extent,
                    desc.destinationOffset)) {
                diagnostics = "RHI-METAL-STAGING-TEXTURE-COPY-REGION-INVALID";
                return false;
            }
            std::uint64_t lastLayerOffset = 0u;
            std::uint64_t mipRequiredBytes = 0u;
            if (!checkedMultiply(bytesPerImage, range.arrayLayerCount - 1u, lastLayerOffset)
                || !checkedAdd(lastLayerOffset, requiredBytes, mipRequiredBytes)
                || !checkedAdd(mipOffset, mipRequiredBytes, mipEnd)
                || !fitsNSUInteger(bytesPerImage) || !fitsNSUInteger(mipOffset)
                || !fitsNSUInteger(mipEnd)
                || (!desc.mipLayouts.empty() && mipOffset < previousMipEnd)
                || mipEnd > desc.pixelDataSize || mipEnd > staging.sizeBytes - stagingOffset) {
                diagnostics = "RHI-METAL-STAGING-TEXTURE-COPY-BUFFER-RANGE-INVALID";
                return false;
            }
            if (!desc.mipLayouts.empty()) {
                previousMipEnd = mipEnd;
            }
            std::uint64_t baseSourceOffset = 0u;
            if (!checkedAdd(stagingOffset, mipOffset, baseSourceOffset)) {
                diagnostics = "RHI-METAL-STAGING-TEXTURE-COPY-BUFFER-RANGE-INVALID";
                return false;
            }
            for (std::uint32_t layerIndex = 0u; layerIndex < range.arrayLayerCount; ++layerIndex) {
                std::uint64_t layerOffset = 0u;
                std::uint64_t relativeOffset = 0u;
                std::uint64_t sourceOffset = 0u;
                if (!checkedMultiply(bytesPerImage, layerIndex, layerOffset)
                    || !checkedAdd(mipOffset, layerOffset, relativeOffset)
                    || !checkedAdd(stagingOffset, relativeOffset, sourceOffset)
                    || !fitsNSUInteger(sourceOffset) || sourceOffset > staging.sizeBytes
                    || requiredBytes > staging.sizeBytes - sourceOffset) {
                    diagnostics = "RHI-METAL-STAGING-TEXTURE-COPY-BUFFER-RANGE-INVALID";
                    return false;
                }
            }
            plan = {
                mipLevel,
                mipLevel,
                extent,
                bytesPerRow,
                bytesPerImage,
                requiredBytes,
                baseSourceOffset,
            };
            return true;
        };
    };
    auto admit = [&] {
        return retainTransferResources(nullptr, *destination, stagingPayload, diagnostics);
    };
    auto consume = [&](const MetalTextureCopyPlan& plan) {
        const MTLSize size = MTLSizeMake(plan.extent.width, plan.extent.height, plan.extent.depth);
        for (std::uint32_t layerIndex = 0u; layerIndex < range.arrayLayerCount; ++layerIndex) {
            const auto destinationLayer = range.baseArrayLayer + layerIndex;
            const auto sourceOffset = plan.bufferOffsetBytes + plan.bytesPerImage * layerIndex;
            [encoder copyFromBuffer:sourceBuffer
                       sourceOffset:static_cast<NSUInteger>(sourceOffset)
                  sourceBytesPerRow:plan.bytesPerRow
                sourceBytesPerImage:static_cast<NSUInteger>(plan.bytesPerImage)
                         sourceSize:size
                          toTexture:destinationTexture
                   destinationSlice:destinationLayer
                   destinationLevel:plan.destinationMipLevel
                  destinationOrigin:MTLOriginMake(desc.destinationOffset.width,
                                        desc.destinationOffset.height,
                                        desc.destinationOffset.depth)];
        }
    };
    const auto result = core::PreflightReplay<MetalTextureCopyPlan>::run(
        range.mipLevelCount, makeMapper, admit, consume, preflightOnly_);
    if (result == core::PreflightReplayStatus::ValidatedOnly)
        return true;
    if (result == core::PreflightReplayStatus::ReplayRejected) {
        diagnostics = "RHI-METAL-TEXTURE-COPY-REPLAY-MAPPING-CHANGED";
        poisonRecording(diagnostics);
        return false;
    }
    if (result != core::PreflightReplayStatus::Completed)
        return false;
    stats_->nativeBlitCopyEncoded = true;
    return true;
}

bool MetalRhiNativeCommandPayload::recordBlitTextureToBuffer(
    const rhi::TextureToBufferCopyDesc& desc, std::pmr::string& diagnostics)
{
    if (const auto* id =
            metalRhiOriginalTextureAccessDiagnostic(resourceRegistry_, desc.sourceTexture)) {
        diagnostics = id;
        return false;
    }
    return recordBlitTextureToBufferContents(desc, diagnostics);
}

bool MetalRhiNativeCommandPayload::recordBlitTextureToBufferContents(
    const rhi::TextureToBufferCopyDesc& desc, core::pmr::String& diagnostics)
{
    if (!retentionRecordingAllows(true, diagnostics))
        return false;
    auto encoder = reinterpret_cast<id<MTLBlitCommandEncoder>>(nativeBlitEncoder_.get());
    const auto source =
        resourceRegistry_ ? resourceRegistry_->find(desc.sourceTexture) : std::nullopt;
    const auto destination =
        resourceRegistry_ ? resourceRegistry_->find(desc.destinationBuffer) : std::nullopt;
    id<MTLTexture> sourceTexture = source ? nativeRegisteredTexture(*source) : nil;
    id<MTLBuffer> destinationBuffer =
        destination && destination->buffer && destination->buffer->resource.nativeRuntimeBacked
        ? reinterpret_cast<id<MTLBuffer>>(destination->buffer->resource.nativeObject.get())
        : nil;
    if (encoder == nil || sourceTexture == nil || destinationBuffer == nil || !source
        || !destination || !source->payloadOwner() || !destination->payloadOwner()
        || !commandDeviceMatches(nativeDevice_, nativeRegisteredDevice(*source))
        || !commandDeviceMatches(nativeDevice_, nativeRegisteredDevice(*destination))
        || source->kind != rhi::DescriptorResourceKind::TextureView
        || destination->kind != rhi::DescriptorResourceKind::Buffer
        || destination->gpuTransferWritesSealed
        || !hasTextureUsage(source->textureUsage, rhi::TextureUsage::CopySrc)
        || !hasResourceUsage(destination->bufferUsage, rhi::ResourceUsage::CopyDst)
        || desc.textureSubresources.mipLevelCount == 0u
        || desc.textureSubresources.arrayLayerCount == 0u
        || (desc.mipLayouts.empty()
                ? desc.textureSubresources.mipLevelCount != 1u
                : desc.mipLayouts.size() != desc.textureSubresources.mipLevelCount)) {
        diagnostics = "RHI-METAL-BLIT-TEXTURE-BUFFER-COPY-RESOURCES-INVALID";
        return false;
    }
    if (desc.textureSubresources.baseMipLevel >= source->textureMipLevelCount
        || desc.textureSubresources.mipLevelCount
            > source->textureMipLevelCount - desc.textureSubresources.baseMipLevel
        || desc.textureSubresources.baseArrayLayer >= source->textureArrayLayerCount
        || desc.textureSubresources.arrayLayerCount
            > source->textureArrayLayerCount - desc.textureSubresources.baseArrayLayer) {
        diagnostics = "RHI-METAL-BLIT-TEXTURE-BUFFER-COPY-SUBRESOURCE-RANGE-INVALID";
        return false;
    }
#if TARGET_OS_IPHONE
    const bool managedReadback = false;
#else
    const bool managedReadback =
        destination->buffer->resource.storageMode == MetalRhiNativeStorageMode::Managed;
#endif
    if (managedReadback) {
        if (!@available(macOS 10.11, *)) {
            diagnostics = "RHI-METAL-MANAGED-READBACK-SYNCHRONIZE-API-UNAVAILABLE";
            return false;
        }
    }
    auto makeMapper = [&] {
        return [&](std::size_t mipIndex, MetalTextureCopyPlan& plan) mutable -> bool {
            std::uint32_t mipLevel = desc.textureSubresources.baseMipLevel;
            rhi::Extent3D extent = desc.extent;
            std::uint32_t bytesPerRow = desc.bytesPerRow;
            std::uint32_t rowsPerImage = desc.rowsPerImage;
            std::uint64_t mipOffset = 0u;
            if (!desc.mipLayouts.empty()) {
                const auto& layout = desc.mipLayouts[mipIndex];
                mipLevel = layout.mipLevel;
                extent = layout.extent;
                bytesPerRow = layout.bytesPerRow;
                rowsPerImage = layout.rowsPerImage;
                mipOffset = layout.bufferOffsetBytes;
                if (mipIndex > std::numeric_limits<std::uint32_t>::max()
                            - desc.textureSubresources.baseMipLevel
                    || mipLevel != desc.textureSubresources.baseMipLevel + mipIndex) {
                    diagnostics = "RHI-METAL-BLIT-TEXTURE-BUFFER-MIP-LAYOUT-ORDER-INVALID";
                    return false;
                }
            }
            std::uint64_t bytesPerImage = 0u;
            std::uint64_t requiredBytes = 0u;
            if (!validTextureCopyLayout(source->textureFormat,
                    extent,
                    bytesPerRow,
                    rowsPerImage,
                    bytesPerImage,
                    requiredBytes)
                || (bytesPerRow % rhi::kTextureUploadRowPitchAlignment) != 0u) {
                diagnostics = "RHI-METAL-BLIT-TEXTURE-BUFFER-COPY-LAYOUT-INVALID";
                return false;
            }
            auto range = desc.textureSubresources;
            range.baseMipLevel = mipLevel;
            range.mipLevelCount = 1u;
            range.arrayLayerCount = 1u;
            if (!validTextureRegion(sourceTexture, source->textureFormat, range, extent)) {
                diagnostics = "RHI-METAL-BLIT-TEXTURE-BUFFER-COPY-REGION-INVALID";
                return false;
            }
            std::uint64_t baseDestinationOffset = 0u;
            if (!checkedAdd(desc.bufferOffsetBytes, mipOffset, baseDestinationOffset)) {
                diagnostics = "RHI-METAL-BLIT-TEXTURE-BUFFER-COPY-BUFFER-RANGE-INVALID";
                return false;
            }
            for (std::uint32_t layerIndex = 0u;
                layerIndex < desc.textureSubresources.arrayLayerCount;
                ++layerIndex) {
                if (layerIndex > std::numeric_limits<std::uint32_t>::max()
                        - desc.textureSubresources.baseArrayLayer) {
                    diagnostics = "RHI-METAL-BLIT-TEXTURE-BUFFER-COPY-LAYER-INVALID";
                    return false;
                }
                const auto layer = desc.textureSubresources.baseArrayLayer + layerIndex;
                if (layer < desc.textureSubresources.baseArrayLayer
                    || layer >= sourceTexture.arrayLength) {
                    diagnostics = "RHI-METAL-BLIT-TEXTURE-BUFFER-COPY-LAYER-INVALID";
                    return false;
                }
                std::uint64_t layerOffset = 0u;
                std::uint64_t destinationOffset = 0u;
                if (!checkedMultiply(bytesPerImage, layerIndex, layerOffset)
                    || !checkedAdd(baseDestinationOffset, layerOffset, destinationOffset)
                    || destinationOffset > destinationBuffer.length
                    || !fitsNSUInteger(destinationOffset)
                    || requiredBytes > destinationBuffer.length - destinationOffset) {
                    diagnostics = "RHI-METAL-BLIT-TEXTURE-BUFFER-COPY-BUFFER-RANGE-INVALID";
                    return false;
                }
            }
            plan = {
                mipLevel,
                mipLevel,
                extent,
                bytesPerRow,
                bytesPerImage,
                requiredBytes,
                baseDestinationOffset,
            };
            return true;
        };
    };
    auto admit = [&] {
        return retainTransferResources(&*source, *destination, {}, diagnostics);
    };
    auto consume = [&](const MetalTextureCopyPlan& plan) {
        const MTLSize size = MTLSizeMake(plan.extent.width, plan.extent.height, plan.extent.depth);
        for (std::uint32_t layerIndex = 0u; layerIndex < desc.textureSubresources.arrayLayerCount;
            ++layerIndex) {
            const auto layer = desc.textureSubresources.baseArrayLayer + layerIndex;
            const auto destinationOffset = plan.bufferOffsetBytes + plan.bytesPerImage * layerIndex;
            [encoder copyFromTexture:sourceTexture
                             sourceSlice:layer
                             sourceLevel:plan.sourceMipLevel
                            sourceOrigin:MTLOriginMake(0, 0, 0)
                              sourceSize:size
                                toBuffer:destinationBuffer
                       destinationOffset:static_cast<NSUInteger>(destinationOffset)
                  destinationBytesPerRow:plan.bytesPerRow
                destinationBytesPerImage:static_cast<NSUInteger>(plan.bytesPerImage)];
        }
    };
    const auto result = core::PreflightReplay<MetalTextureCopyPlan>::run(
        desc.textureSubresources.mipLevelCount, makeMapper, admit, consume, preflightOnly_);
    if (result == core::PreflightReplayStatus::ValidatedOnly)
        return true;
    if (result == core::PreflightReplayStatus::ReplayRejected) {
        diagnostics = "RHI-METAL-TEXTURE-COPY-REPLAY-MAPPING-CHANGED";
        poisonRecording(diagnostics);
        return false;
    }
    if (result != core::PreflightReplayStatus::Completed)
        return false;
    if (managedReadback) {
        // Managed resources have distinct GPU and CPU copies on macOS. The
        // synchronization must be encoded after the copy and before the
        // command buffer is committed; a completion callback alone does not
        // make the CPU-side allocation current.
#if !TARGET_OS_IPHONE
        [encoder synchronizeResource:destinationBuffer];
#endif
    }
    stats_->nativeBlitCopyEncoded = true;
    return true;
}

bool MetalRhiNativeCommandPayload::preflightBlitTextureToBuffer(
    const rhi::TextureToBufferCopyDesc& desc, std::pmr::string& diagnostics)
{
    preflightOnly_ = true;
    const bool valid = recordBlitTextureToBuffer(desc, diagnostics);
    preflightOnly_ = false;
    return valid;
}

bool MetalRhiNativeCommandPayload::recordBlitTextureCopy(
    const rhi::TextureToTextureCopyDesc& desc, std::pmr::string& diagnostics)
{
    if (const auto* id =
            metalRhiOriginalTextureAccessDiagnostic(resourceRegistry_, desc.sourceTexture)) {
        diagnostics = id;
        return false;
    }
    if (const auto* id =
            metalRhiTextureWriteDiagnostic(resourceRegistry_, desc.destinationTexture)) {
        diagnostics = id;
        return false;
    }
    if (!retentionRecordingAllows(true, diagnostics))
        return false;
    auto encoder = reinterpret_cast<id<MTLBlitCommandEncoder>>(nativeBlitEncoder_.get());
    const auto source =
        resourceRegistry_ ? resourceRegistry_->find(desc.sourceTexture) : std::nullopt;
    const auto destination =
        resourceRegistry_ ? resourceRegistry_->find(desc.destinationTexture) : std::nullopt;
    id<MTLTexture> sourceTexture = source ? nativeRegisteredTexture(*source) : nil;
    id<MTLTexture> destinationTexture = destination ? nativeRegisteredTexture(*destination) : nil;
    if (encoder == nil || sourceTexture == nil || destinationTexture == nil || !source
        || !destination || !source->payloadOwner() || !destination->payloadOwner()
        || !commandDeviceMatches(nativeDevice_, nativeRegisteredDevice(*source))
        || !commandDeviceMatches(nativeDevice_, nativeRegisteredDevice(*destination))
        || source->kind != rhi::DescriptorResourceKind::TextureView
        || destination->kind != rhi::DescriptorResourceKind::TextureView
        || !hasTextureUsage(source->textureUsage, rhi::TextureUsage::CopySrc)
        || !hasTextureUsage(destination->textureUsage, rhi::TextureUsage::CopyDst)
        || source->textureFormat != destination->textureFormat
        || desc.sourceSubresources.aspect != desc.destinationSubresources.aspect
        || desc.sourceSubresources.mipLevelCount == 0u
        || desc.sourceSubresources.arrayLayerCount == 0u
        || desc.sourceSubresources.mipLevelCount != desc.destinationSubresources.mipLevelCount
        || desc.sourceSubresources.arrayLayerCount != desc.destinationSubresources.arrayLayerCount
        || (desc.mipLayouts.empty()
                ? desc.sourceSubresources.mipLevelCount != 1u
                : desc.mipLayouts.size() != desc.sourceSubresources.mipLevelCount)
        || sourceTexture.pixelFormat != destinationTexture.pixelFormat
        || desc.sourceSubresources.baseArrayLayer > source->textureArrayLayerCount
        || desc.sourceSubresources.arrayLayerCount
            > source->textureArrayLayerCount - desc.sourceSubresources.baseArrayLayer
        || desc.destinationSubresources.baseArrayLayer > destination->textureArrayLayerCount
        || desc.destinationSubresources.arrayLayerCount
            > destination->textureArrayLayerCount - desc.destinationSubresources.baseArrayLayer) {
        diagnostics = "RHI-METAL-BLIT-TEXTURE-COPY-RESOURCES-INVALID";
        return false;
    }
    auto makeMapper = [&] {
        return [&](std::size_t mipIndex, MetalTextureCopyPlan& plan) mutable -> bool {
            std::uint32_t sourceLevel = desc.sourceSubresources.baseMipLevel;
            std::uint32_t destinationLevel = desc.destinationSubresources.baseMipLevel;
            rhi::Extent3D extent = desc.extent;
            if (!desc.mipLayouts.empty()) {
                const auto& layout = desc.mipLayouts[mipIndex];
                if (mipIndex > std::numeric_limits<std::uint32_t>::max()
                            - desc.sourceSubresources.baseMipLevel
                    || mipIndex > std::numeric_limits<std::uint32_t>::max()
                            - desc.destinationSubresources.baseMipLevel) {
                    diagnostics = "RHI-METAL-BLIT-TEXTURE-COPY-MIP-LAYOUT-ORDER-INVALID";
                    return false;
                }
                sourceLevel = desc.sourceSubresources.baseMipLevel + mipIndex;
                destinationLevel = desc.destinationSubresources.baseMipLevel + mipIndex;
                extent = layout.extent;
                if (layout.mipLevel != sourceLevel) {
                    diagnostics = "RHI-METAL-BLIT-TEXTURE-COPY-MIP-LAYOUT-ORDER-INVALID";
                    return false;
                }
            }
            auto sourceRange = desc.sourceSubresources;
            sourceRange.baseMipLevel = sourceLevel;
            sourceRange.mipLevelCount = 1u;
            sourceRange.arrayLayerCount = 1u;
            auto destinationRange = desc.destinationSubresources;
            destinationRange.baseMipLevel = destinationLevel;
            destinationRange.mipLevelCount = 1u;
            destinationRange.arrayLayerCount = 1u;
            if (!validTextureRegion(sourceTexture, source->textureFormat, sourceRange, extent)
                || !validTextureRegion(
                    destinationTexture, destination->textureFormat, destinationRange, extent)) {
                diagnostics = "RHI-METAL-BLIT-TEXTURE-COPY-REGION-INVALID";
                return false;
            }
            for (std::uint32_t layerIndex = 0u;
                layerIndex < desc.sourceSubresources.arrayLayerCount;
                ++layerIndex) {
                if (layerIndex > std::numeric_limits<std::uint32_t>::max()
                            - desc.sourceSubresources.baseArrayLayer
                    || layerIndex > std::numeric_limits<std::uint32_t>::max()
                            - desc.destinationSubresources.baseArrayLayer) {
                    diagnostics = "RHI-METAL-BLIT-TEXTURE-COPY-LAYER-INVALID";
                    return false;
                }
                const auto sourceLayer = desc.sourceSubresources.baseArrayLayer + layerIndex;
                const auto destinationLayer =
                    desc.destinationSubresources.baseArrayLayer + layerIndex;
                if (sourceLayer >= sourceTexture.arrayLength
                    || destinationLayer >= destinationTexture.arrayLength) {
                    diagnostics = "RHI-METAL-BLIT-TEXTURE-COPY-LAYER-INVALID";
                    return false;
                }
            }
            plan = {
                sourceLevel,
                destinationLevel,
                extent,
                0u,
                0u,
                0u,
                0u,
            };
            return true;
        };
    };
    auto admit = [&] {
        return retainTransferResources(&*source, *destination, {}, diagnostics);
    };
    auto consume = [&](const MetalTextureCopyPlan& plan) {
        const MTLSize size = MTLSizeMake(plan.extent.width, plan.extent.height, plan.extent.depth);
        for (std::uint32_t layerIndex = 0u; layerIndex < desc.sourceSubresources.arrayLayerCount;
            ++layerIndex) {
            const auto sourceLayer = desc.sourceSubresources.baseArrayLayer + layerIndex;
            const auto destinationLayer = desc.destinationSubresources.baseArrayLayer + layerIndex;
            [encoder copyFromTexture:sourceTexture
                         sourceSlice:sourceLayer
                         sourceLevel:plan.sourceMipLevel
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:size
                           toTexture:destinationTexture
                    destinationSlice:destinationLayer
                    destinationLevel:plan.destinationMipLevel
                   destinationOrigin:MTLOriginMake(0, 0, 0)];
        }
    };
    const auto result = core::PreflightReplay<MetalTextureCopyPlan>::run(
        desc.sourceSubresources.mipLevelCount, makeMapper, admit, consume, preflightOnly_);
    if (result == core::PreflightReplayStatus::ValidatedOnly)
        return true;
    if (result == core::PreflightReplayStatus::ReplayRejected) {
        diagnostics = "RHI-METAL-TEXTURE-COPY-REPLAY-MAPPING-CHANGED";
        poisonRecording(diagnostics);
        return false;
    }
    if (result != core::PreflightReplayStatus::Completed)
        return false;
    stats_->nativeBlitCopyEncoded = true;
    return true;
}

bool MetalRhiNativeCommandPayload::recordBlitPassEnd(std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(true, diagnostics))
        return false;
    auto encoder = reinterpret_cast<id<MTLBlitCommandEncoder>>(nativeBlitEncoder_.get());
    if (encoder == nil) {
        diagnostics = "RHI-METAL-BLIT-PASS-END-ENCODER-MISSING";
        return false;
    }
    [encoder endEncoding];
    nativeBlitEncoder_.reset();
    return true;
}

bool MetalRhiNativeCommandPayload::finish(std::pmr::string& diagnostics)
{
    if (originalRecordingClosed_) {
        diagnostics = "RHI-METAL-ROOT-COMMAND-RECORDING-CLOSED";
        return false;
    }
    if ((closedConsumerBudget_ || closedConsumerReservation_)
        && (!closedConsumerNative_ || !closedConsumerSequence_)) {
        diagnostics = "RHI-METAL-CLOSED-CONSUMER-RECORDING-PAYLOAD-MISSING";
        return false;
    }
    if (canonicalProducerBudget_ && (!canonicalProducer_ || !canonicalProducerSequence_)) {
        diagnostics = "RHI-METAL-CANONICAL-PRODUCER-PREPARATION-ONLY";
        return false;
    }
    if (indirectOutputBudget_ && (!indirectExecution_ || !indirectOutputSequence_)) {
        diagnostics = "RHI-METAL-INDIRECT-EXECUTION-PREPARATION-ONLY";
        return false;
    }
    if (abandoned_) {
        diagnostics = "RHI-METAL-COMMAND-BUFFER-ABANDONED";
        return false;
    }
    if (recordingPoisoned_) {
        diagnostics = "RHI-METAL-COMMAND-BUFFER-RECORDING-POISONED";
        return false;
    }
    if (closedConsumerNative_) {
        if (const auto* id = closedConsumerNative_->validateRecorded(false)) {
            diagnostics = id;
            poisonRecording(diagnostics);
            return false;
        }
    }
    if (canonicalProducer_) {
        if (const auto* id = canonicalProducerSequence_->finish()) {
            diagnostics = id;
            return false;
        }
        if (canonicalProducer_->status()
                != MetalRhiCanonicalProducerRecordingPreparation::Status::Recorded
            || !canonicalProducer_->lease_.valid() || !canonicalProducerStates_
            || canonicalProducerStates_->empty()) {
            diagnostics = "RHI-METAL-CANONICAL-PRODUCER-FINISH-STATE";
            return false;
        }
        if (const auto* id =
                canonicalProducer_->validateForState(rhi::ResourceState::CopySrc, false)) {
            diagnostics = id;
            return false;
        }
    }
    if (indirectExecution_) {
        if (const auto* id = indirectOutputSequence_->finish()) {
            diagnostics = id;
            return false;
        }
        if (indirectExecution_->status() != MetalRhiIndirectExecutionPreparation::Status::Recorded
            || indirectExecution_->output_->generationConsumed()
            || !indirectExecution_->lease_.valid() || indirectOutputStates_->empty()) {
            diagnostics = "RHI-METAL-INDIRECT-EXECUTION-FINISH-STATE";
            return false;
        }
        if (const auto* id =
                indirectExecution_->output_->validateForState(rhi::ResourceState::ShaderRead)) {
            diagnostics = id;
            return false;
        }
    }
    if (ordinaryIndirectRecording_)
        if (const auto* id = ordinaryIndirectRecording_->finish()) {
            diagnostics = id;
            return false;
        }
    if (ordinaryWriteStates_) {
        if (const auto* id = ordinaryWriteSequence_.finish()) {
            diagnostics = id;
            return false;
        }
    }
    if (pendingGraphicsBinding_) {
        diagnostics = "RHI-METAL-GRAPHICS-SCOPE-NOT-CONSUMED";
        return false;
    }
    if (!leasedBindlessFaults_.empty()) {
        diagnostics = "RHI-METAL-BINDLESS-FAULT-READBACK-NOT-SCHEDULED";
        return false;
    }
    if (nativeRenderEncoder_ || nativeComputeEncoder_ || nativeBlitEncoder_) {
        diagnostics = "RHI-METAL-COMMAND-BUFFER-HAS-OPEN-ENCODER";
        return false;
    }
    if (usesRecordingRetention_
        && (!recordingRetention_ || !recordingRetention_->sealForSubmission())) {
        diagnostics = "RHI-COMMAND-RETENTION-RECORDING-CLOSED";
        return false;
    }
    stats_->nativeCommandBufferFinished = stats_->nativeCommandBufferCreated;
    return stats_->nativeCommandBufferFinished;
}

void MetalRhiNativeCommandPayload::cancelCanonicalProducer(bool failure) noexcept
{
    if (canonicalProducerStates_)
        canonicalProducerStates_->rollback();
    if (canonicalProducer_
        && (canonicalProducer_->status()
                == MetalRhiCanonicalProducerRecordingPreparation::Status::Cold
            || canonicalProducer_->status()
                == MetalRhiCanonicalProducerRecordingPreparation::Status::Recorded)) {
        const auto after = failure
            ? MetalRhiCanonicalProducerRecordingPreparation::Status::Failed
            : MetalRhiCanonicalProducerRecordingPreparation::Status::Abandoned;
        if (canonicalProducerSequence_)
            canonicalProducerSequence_->transition(after, canonicalProducer_->facts().identity);
        canonicalProducer_->status_.store(after, std::memory_order_release);
        canonicalProducer_->lease_.release();
    }
    canonicalProducer_.reset();
}

void MetalRhiNativeCommandPayload::cancelIndirectExecution(bool failure) noexcept
{
    if (indirectOutputStates_)
        indirectOutputStates_->rollback();
    if (indirectExecution_
        && indirectExecution_->status() == MetalRhiIndirectExecutionPreparation::Status::Recorded) {
        indirectExecution_->status_.store(failure
                ? MetalRhiIndirectExecutionPreparation::Status::Failed
                : MetalRhiIndirectExecutionPreparation::Status::Abandoned,
            std::memory_order_release);
        indirectExecution_->lease_ = MetalRhiIndirectOutputRecordingLease{};
    }
    indirectExecution_.reset();
}

void MetalRhiNativeCommandPayload::closeUnsubmittedNativeScopes() noexcept
{
    if (nativeRenderEncoder_)
        [reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder_.get()) endEncoding];
    if (nativeComputeEncoder_)
        [reinterpret_cast<id<MTLComputeCommandEncoder>>(nativeComputeEncoder_.get()) endEncoding];
    if (nativeBlitEncoder_)
        [reinterpret_cast<id<MTLBlitCommandEncoder>>(nativeBlitEncoder_.get()) endEncoding];
    nativeRenderEncoder_.reset();
    nativeComputeEncoder_.reset();
    nativeBlitEncoder_.reset();
    nativeCommandBuffer_.reset();
    nativeSubmitWaitCommandBuffer_.reset();
}

void MetalRhiNativeCommandPayload::abandonUnsubmitted()
{
    if (readbackWasSubmitted() || abandoned_) {
        return;
    }
    abandoned_ = true;
    closeUnsubmittedNativeScopes();
    if (submissionDomain_)
        (void)submissionDomain_->storage_->cancelValidator(submissionTicket_);
    if (originalCommandCustody_)
        originalCommandCustody_->abandonUnsubmitted();
    originalCommandCustody_.reset();
    stagingUploads_.reset();
    cancelIndirectExecution(false);
    cancelCanonicalProducer(false);
    if (ordinaryWriteStates_)
        ordinaryWriteStates_->rollback();
    if (ordinaryWriteReadback_)
        ordinaryWriteReadback_->status_.store(
            MetalRhiOrdinaryPreparedWriteReadback::Status::Abandoned, std::memory_order_release);
    if (recordingRetention_)
        recordingRetention_->abandonUnsubmitted();
    // Native scopes are already closed; release their prepared storage next.
    if (preparedRenderEncoderSlot_)
        preparedRenderEncoderSlot_->reset();
    ordinaryGraphicsPass_.reset();
    ordinaryGraphicsContract_.reset();
    ordinaryGraphicsDescriptors_.reset();
    ordinaryGraphicsBindings_ = {};
    ordinaryGraphicsPush_.reset();
    ordinaryGraphicsPushState_.onPassEnd();
    ordinaryVertexBindings_ = {};
    vertexStreams_ = {};
    nativeComputeEncoder_.reset();
    if (preparedIndirectComputeEncoderSlot_)
        preparedIndirectComputeEncoderSlot_->reset();
    computePipeline_.reset();
    ordinaryComputeContract_.reset();
    ordinaryComputeDescriptors_.reset();
    ordinaryComputeBindings_ = {};
    ordinaryComputePush_.reset();
    ordinaryComputePushState_.onPassEnd();
    nativeBlitEncoder_.reset();
    for (auto& slot : canonicalProducerSlots_)
        if (slot)
            slot->reset();
    cancelClosedConsumerReservation(false);
    for (auto& pending : pendingBindlessFaultReadbacks_) {
        if (pending.result)
            pending.result->abandon();
        pending.lease.release();
    }
    pendingBindlessFaultReadbacks_.clear();
    for (auto& fault : leasedBindlessFaults_)
        fault.lease.release();
    leasedBindlessFaults_.clear();
    nativeCommandBuffer_.reset();
    ordinaryReadConsumer_.reset();
    ordinaryWriteReadback_.reset();
    ordinaryWrite_.reset();
    if (preparedWriteBlitEncoderSlot_)
        preparedWriteBlitEncoderSlot_->reset();
    commandCapacity_.reset();
    retainedBindlessPipelines_.clear();
    retainedBindlessSnapshots_.clear();
    retainedPreparedBindings_.clear();
    recordedTimestampQueries_.clear();
    recordedPipelineStatisticsQueries_.clear();
    retainedQueryPools_.clear();
    pendingGraphicsBinding_.reset();
    activeGraphicsBinding_.reset();
    pendingGraphicsFaultDomain_.reset();
    activeGraphicsFaultDomain_.reset();
    graphicsScopeBound_ = false;
    graphicsAccesses_.clear();
}

void MetalRhiNativeCommandPayload::poisonRecording(std::string_view)
{
    if (readbackWasSubmitted() || recordingPoisoned_) {
        return;
    }
    recordingPoisoned_ = true;
    closeUnsubmittedNativeScopes();
    if (submissionDomain_)
        (void)submissionDomain_->storage_->cancelValidator(submissionTicket_);
    if (originalCommandCustody_)
        originalCommandCustody_->abandonUnsubmitted();
    originalCommandCustody_.reset();
    cancelIndirectExecution(true);
    cancelCanonicalProducer(true);
    if (ordinaryWriteStates_)
        ordinaryWriteStates_->rollback();
    if (ordinaryWriteReadback_)
        ordinaryWriteReadback_->status_.store(
            MetalRhiOrdinaryPreparedWriteReadback::Status::Failed, std::memory_order_release);
    if (recordingRetention_)
        recordingRetention_->abandonUnsubmitted();
    // Native scopes and command-buffer ownership were removed before rollback.
    if (preparedRenderEncoderSlot_)
        preparedRenderEncoderSlot_->reset();
    ordinaryGraphicsPass_.reset();
    ordinaryGraphicsContract_.reset();
    ordinaryGraphicsDescriptors_.reset();
    ordinaryGraphicsBindings_ = {};
    ordinaryGraphicsPush_.reset();
    ordinaryGraphicsPushState_.onPassEnd();
    ordinaryVertexBindings_ = {};
    vertexStreams_ = {};
    nativeComputeEncoder_.reset();
    if (preparedIndirectComputeEncoderSlot_)
        preparedIndirectComputeEncoderSlot_->reset();
    computePipeline_.reset();
    ordinaryComputeContract_.reset();
    ordinaryComputeDescriptors_.reset();
    ordinaryComputeBindings_ = {};
    ordinaryComputePush_.reset();
    ordinaryComputePushState_.onPassEnd();
    nativeBlitEncoder_.reset();
    for (auto& slot : canonicalProducerSlots_)
        if (slot)
            slot->reset();
    cancelClosedConsumerReservation(true);
    nativeCommandBuffer_.reset();
    ordinaryReadConsumer_.reset();
    ordinaryWriteReadback_.reset();
    ordinaryWrite_.reset();
    if (preparedWriteBlitEncoderSlot_)
        preparedWriteBlitEncoderSlot_->reset();
    retainedBindlessPipelines_.clear();
    retainedBindlessSnapshots_.clear();
    retainedPreparedBindings_.clear();
    recordedTimestampQueries_.clear();
    recordedPipelineStatisticsQueries_.clear();
    retainedQueryPools_.clear();
    pendingGraphicsBinding_.reset();
    activeGraphicsBinding_.reset();
    pendingGraphicsFaultDomain_.reset();
    activeGraphicsFaultDomain_.reset();
    graphicsScopeBound_ = false;
    graphicsAccesses_.clear();
}

bool MetalRhiNativeCommandPayload::recordPushDebugGroup(
    std::string_view label, std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    auto* encoder =
        reinterpret_cast<id<MTLCommandEncoder>>(nativeRenderEncoder_ ? nativeRenderEncoder_.get()
                : nativeComputeEncoder_                              ? nativeComputeEncoder_.get()
                                                                     : nativeBlitEncoder_.get());
    if (encoder == nil) {
        diagnostics = "RHI-METAL-DEBUG-GROUP-ENCODER-MISSING";
        return false;
    }
    NSString* value = [[NSString alloc] initWithBytes:label.data()
                                               length:label.size()
                                             encoding:NSUTF8StringEncoding];
    [encoder pushDebugGroup:value ?: @""];
    [value release];
    return true;
}

bool MetalRhiNativeCommandPayload::recordPopDebugGroup(std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    auto* encoder =
        reinterpret_cast<id<MTLCommandEncoder>>(nativeRenderEncoder_ ? nativeRenderEncoder_.get()
                : nativeComputeEncoder_                              ? nativeComputeEncoder_.get()
                                                                     : nativeBlitEncoder_.get());
    if (encoder == nil) {
        diagnostics = "RHI-METAL-DEBUG-GROUP-ENCODER-MISSING";
        return false;
    }
    [encoder popDebugGroup];
    return true;
}

bool MetalRhiNativeCommandPayload::recordInsertDebugMarker(
    std::string_view label, std::pmr::string& diagnostics)
{
    if (!retentionRecordingAllows(false, diagnostics))
        return false;
    auto* encoder =
        reinterpret_cast<id<MTLCommandEncoder>>(nativeRenderEncoder_ ? nativeRenderEncoder_.get()
                : nativeComputeEncoder_                              ? nativeComputeEncoder_.get()
                                                                     : nativeBlitEncoder_.get());
    if (encoder == nil) {
        diagnostics = "RHI-METAL-DEBUG-MARKER-ENCODER-MISSING";
        return false;
    }
    NSString* value = [[NSString alloc] initWithBytes:label.data()
                                               length:label.size()
                                             encoding:NSUTF8StringEncoding];
    [encoder insertDebugSignpost:value ?: @""];
    [value release];
    return true;
}

bool MetalRhiNativeCommandPayload::submit(MetalRhiNativeQueueStats& queueStats,
    const std::shared_ptr<MetalRhiCompletionState>& completion,
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>& waitEvent,
    std::uint64_t waitValue,
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>& signalEvent,
    std::uint64_t signalValue,
    std::pmr::string& diagnostics)
{
    if (submissionDomain_)
        if (const auto* id = submissionDomain_->storage_->diagnostic()) {
            diagnostics = id;
            return false;
        }
    if (submitted_.load(std::memory_order_acquire)) {
        diagnostics = "RHI-METAL-COMMAND-BUFFER-ALREADY-SUBMITTED";
        return false;
    }
    // Once this protected sequence is encoded, any submit admission rejection
    // discards it. Never leave a failed submit holding the generation lease.
    const auto rejectBeforeCommit = [&]() {
        if (originalCommandCustody_) {
            poisonRecording(diagnostics);
            if (completion)
                completion->complete(false);
        }
        if (indirectExecution_ || canonicalProducer_ || closedConsumerReservation_) {
            poisonRecording(diagnostics);
            const auto prepared = preparedCompletionState();
            if (prepared)
                prepared->complete(false);
            if (completion && completion != prepared)
                completion->complete(false);
        }
        return false;
    };
    if (originalCommandCustody_ && (waitEvent || signalEvent)) {
        diagnostics = "RHI-METAL-ROOT-COMMAND-EXTERNAL-SYNC-PROFILE";
        return rejectBeforeCommit();
    }
    if ((closedConsumerBudget_ || closedConsumerReservation_)
        && (!closedConsumerNative_ || !closedConsumerSequence_)) {
        diagnostics = "RHI-METAL-CLOSED-CONSUMER-RECORDING-PAYLOAD-MISSING";
        return rejectBeforeCommit();
    }
    if (closedConsumerNative_) {
        if (const auto* id = closedConsumerNative_->validateRecorded(true)) {
            diagnostics = id;
            return rejectBeforeCommit();
        }
        auto prospective = *closedConsumerSequence_;
        if (const auto* id = prospective.transition(
                MetalRhiClosedConsumerRecordingReservation::Status::Submitted,
                closedConsumerNative_->reservation()->preparation()->facts().identity)) {
            diagnostics = id;
            return rejectBeforeCommit();
        }
    }
    if (indirectOutputBudget_ && (!indirectExecution_ || !indirectOutputSequence_)) {
        diagnostics = "RHI-METAL-INDIRECT-EXECUTION-PREPARATION-ONLY";
        return rejectBeforeCommit();
    }
    if (canonicalProducerBudget_ && (!canonicalProducer_ || !canonicalProducerSequence_)) {
        diagnostics = "RHI-METAL-CANONICAL-PRODUCER-PREPARATION-ONLY";
        return rejectBeforeCommit();
    }
    if (abandoned_) {
        diagnostics = "RHI-METAL-COMMAND-BUFFER-ABANDONED";
        return rejectBeforeCommit();
    }
    if (recordingPoisoned_) {
        diagnostics = "RHI-METAL-COMMAND-BUFFER-RECORDING-POISONED";
        return rejectBeforeCommit();
    }
    if (canonicalProducer_) {
        if (canonicalProducer_->status()
                != MetalRhiCanonicalProducerRecordingPreparation::Status::Recorded
            || !canonicalProducer_->lease_.valid() || canonicalProducerSequence_->finish()) {
            diagnostics = "RHI-METAL-CANONICAL-PRODUCER-SUBMIT-STATE";
            return rejectBeforeCommit();
        }
        if (const auto* id =
                canonicalProducer_->validateForState(rhi::ResourceState::CopySrc, true)) {
            diagnostics = id;
            return rejectBeforeCommit();
        }
        auto prospective = *canonicalProducerSequence_;
        if (const auto* id = prospective.transition(
                MetalRhiCanonicalProducerRecordingPreparation::Status::Submitted,
                canonicalProducer_->facts().identity)) {
            diagnostics = id;
            return rejectBeforeCommit();
        }
    }
    if (indirectExecution_
        && (indirectExecution_->status() != MetalRhiIndirectExecutionPreparation::Status::Recorded
            || rhi::GpuIndirectOutputLifetimeContract::validateTransition(
                indirectExecution_->status(),
                MetalRhiIndirectExecutionPreparation::Status::Submitted)
            || indirectExecution_->output_->generationConsumed()
            || !indirectExecution_->lease_.valid()
            || indirectExecution_->output_->resource().state() != rhi::ResourceState::ShaderRead
            || !indirectExecution_->output_->resource().hostWritesSealed()
            || !indirectExecution_->output_->resource().gpuWritesSealed()
            || !indirectExecution_->output_->resource().gpuTransferWritesSealed()
            || indirectOutputSequence_->finish())) {
        diagnostics = "RHI-METAL-INDIRECT-EXECUTION-SUBMIT-STATE";
        return rejectBeforeCommit();
    }
    if (ordinaryWriteStates_
        && (!ordinaryWrite_ || ordinaryWrite_->resource().gpuWritesSealed()
            || (maxOrdinaryConsumerDraws_
                    ? (!ordinaryReadConsumer_ || ordinaryWriteReadback_
                          || ordinaryWrite_->resource().state() != rhi::ResourceState::ShaderRead)
                    : (!ordinaryWriteReadback_ || ordinaryReadConsumer_
                          || ordinaryWriteReadback_->resource().gpuTransferWritesSealed()
                          || ordinaryWrite_->resource().state() != rhi::ResourceState::CopySrc
                          || ordinaryWriteReadback_->resource().state()
                              != rhi::ResourceState::CopyDst)))) {
        diagnostics = "RHI-METAL-ORDINARY-WRITE-SUBMIT-STATE-OR-SEAL-MISMATCH";
        return rejectBeforeCommit();
    }
    if (!nativeCommandBuffer_ || !completion) {
        diagnostics = "RHI-METAL-COMPLETION-PAYLOAD-MISSING";
        return rejectBeforeCommit();
    }
    if (originalOrdinary_
        && !core::SharedOwnerProjection::sameOwner(completion, preparedCompletionState())) {
        diagnostics = "RHI-QUEUE-ACCEPTANCE-COMPLETION-OWNER";
        return rejectBeforeCommit();
    }
    if ((waitEvent && !commandDeviceMatches(nativeDevice_, waitEvent->nativeDevice))
        || (signalEvent && !commandDeviceMatches(nativeDevice_, signalEvent->nativeDevice))) {
        diagnostics = "RHI-METAL-SHARED-EVENT-DEVICE-MISMATCH";
        return rejectBeforeCommit();
    }
    if (!stats_->nativeCommandBufferFinished) {
        diagnostics = "RHI-METAL-COMMAND-BUFFER-MUST-BE-FINISHED-BEFORE-SUBMIT";
        return rejectBeforeCommit();
    }
    id<MTLCommandBuffer> commandBuffer =
        reinterpret_cast<id<MTLCommandBuffer>>(nativeCommandBuffer_.get());
    if (originalOrdinary_
        && commandBuffer.commandQueue
            != reinterpret_cast<id<MTLCommandQueue>>(nativeQueue_.get())) {
        diagnostics = "RHI-QUEUE-ACCEPTANCE-NATIVE-QUEUE";
        return rejectBeforeCommit();
    }
    if (commandBuffer == nil || commandBuffer.commandQueue == nil) {
        diagnostics = "RHI-METAL-COMMAND-BUFFER-NATIVE-OBJECT-MISSING";
        return rejectBeforeCommit();
    }
    if ((waitEvent && (!waitEvent->nativeRuntimeBacked || !waitEvent->nativeEvent))
        || (signalEvent
            && (!signalEvent->nativeRuntimeBacked || !signalEvent->nativeEvent
                || !signalEvent->nativeListener || !signalEvent->state))) {
        diagnostics = "RHI-METAL-SHARED-EVENT-PAYLOAD-MISSING";
        return rejectBeforeCommit();
    }
    if ((waitEvent && !commandDeviceMatches(nativeDevice_, waitEvent->nativeDevice))
        || (signalEvent && !commandDeviceMatches(nativeDevice_, signalEvent->nativeDevice))) {
        diagnostics = "RHI-METAL-SHARED-EVENT-DEVICE-MISMATCH";
        return rejectBeforeCommit();
    }
    if (!submissionRetention_) {
        diagnostics = "RHI-METAL-SUBMISSION-RETENTION-MISSING";
        return rejectBeforeCommit();
    }
    const auto submittedQueries = std::shared_ptr<MetalRhiSubmittedQueryRetention>(
        submissionRetention_, &submissionRetention_->queries);
    if (submitted_.load(std::memory_order_acquire)) {
        diagnostics = "RHI-METAL-COMMAND-BUFFER-ALREADY-SUBMITTED";
        return rejectBeforeCommit();
    }
    if (!submittedQueries->canTakeRecorded(
            retainedQueryPools_, recordedTimestampQueries_, recordedPipelineStatisticsQueries_)) {
        diagnostics = "RHI-METAL-QUERY-RETENTION-TRANSFER-INVALID";
        return rejectBeforeCommit();
    }
    if (usesRecordingRetention_
        && (!recordingRetention_
            || recordingRetention_->state()
                != rhi::common::CommandRetentionStorage::State::Sealed)) {
        diagnostics = "RHI-COMMAND-RETENTION-RECORDING-CLOSED";
        return rejectBeforeCommit();
    }
    if (recordingRetention_
        && recordingRetention_->quota()->limitBytes()
            != recordingRetention_->budget().maxCpuMetadataBytes) {
        diagnostics = "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
        return rejectBeforeCommit();
    }
    if (submissionDomain_
        && (!retainedQueryPools_.empty() || !recordedTimestampQueries_.empty()
            || !recordedPipelineStatisticsQueries_.empty()
            || !pendingBindlessFaultReadbacks_.empty() || ordinaryWriteReadback_
            || indirectExecution_ || canonicalProducer_ || closedConsumerNative_)) {
        diagnostics = "RHI-SUBMISSION-COMPLETION-ORDINARY-PROFILE";
        return rejectBeforeCommit();
    }
    if (originalCommandCustody_ && !originalCommandCustody_->beforeSubmit(*this, diagnostics))
        return rejectBeforeCommit();
    auto& queueOrder = originalQueueCommitOrder();
    std::unique_lock queueCommitLock(queueOrder.mutex, std::try_to_lock);
    if (!queueCommitLock.owns_lock()) {
        diagnostics = "RHI-QUEUE-ACCEPTANCE-BUSY";
        return false;
    }
    const auto queueOrdinal = queueOrder.ordinals.tryAcquire();
    if (!queueOrdinal) {
        diagnostics = "RHI-QUEUE-ACCEPTANCE-ORDINAL-EXHAUSTED";
        return rejectBeforeCommit();
    }
    bool expectedSubmitted = false;
    if (!submitted_.compare_exchange_strong(
            expectedSubmitted, true, std::memory_order_acq_rel, std::memory_order_acquire)) {
        diagnostics = "RHI-METAL-COMMAND-BUFFER-ALREADY-SUBMITTED";
        return false;
    }
    // Once native encoding begins, this command buffer is terminal even when
    // validation of a later pre-commit operation fails.  A Metal command
    // buffer cannot be safely rewound for retry, so publish a completed
    // failure state instead of leaving a fence permanently pending.
    completion->reset();
    const auto failBeforeCommit = [&]() {
        closeUnsubmittedNativeScopes();
        if (submissionDomain_)
            (void)submissionDomain_->storage_->cancelValidator(submissionTicket_);
        stagingUploads_.reset();
        if (originalCommandCustody_)
            originalCommandCustody_->abandonUnsubmitted();
        originalCommandCustody_.reset();
        submissionFailed_.store(true, std::memory_order_release);
        cancelIndirectExecution(true);
        cancelCanonicalProducer(true);
        cancelClosedConsumerReservation(true);
        if (ordinaryWriteStates_)
            ordinaryWriteStates_->rollback();
        if (ordinaryWriteReadback_)
            ordinaryWriteReadback_->status_.store(
                MetalRhiOrdinaryPreparedWriteReadback::Status::Failed, std::memory_order_release);
        if (recordingRetention_)
            recordingRetention_->abandonUnsubmitted();
        for (auto& pending : pendingBindlessFaultReadbacks_) {
            if (pending.result)
                pending.result->fail();
            pending.lease.release();
        }
        pendingBindlessFaultReadbacks_.clear();
        for (auto& fault : leasedBindlessFaults_)
            fault.lease.release();
        leasedBindlessFaults_.clear();
        if (ordinaryWriteStates_) {
            nativeCommandBuffer_.reset();
            ordinaryReadConsumer_.reset();
            ordinaryWriteReadback_.reset();
            ordinaryWrite_.reset();
        }
        recordedTimestampQueries_.clear();
        recordedPipelineStatisticsQueries_.clear();
        retainedQueryPools_.clear();
        completion->complete(false);
        return false;
    };
    if (!commandCapacity_ || commandCapacity_->units() < kMetalPrimaryCommandReservation) {
        diagnostics = "RHI-METAL-COMMAND-CAPACITY-RESERVATION-MISSING";
        return failBeforeCommit();
    }
    if (waitEvent) {
        // Recorded encoders already precede submit: appending a wait to their
        // command buffer only delays completion. The queue is serial, so place
        // the wait in its own preceding native command buffer instead.
        try {
            @autoreleasepool {
                id<MTLCommandBuffer> waitCommand = [commandBuffer.commandQueue commandBuffer];
                if (waitCommand == nil) {
                    diagnostics = "RHI-METAL-SUBMIT-WAIT-COMMAND-BUFFER-CREATE-FAILED";
                    return failBeforeCommit();
                }
                waitCommand.label = @"ARP submit wait";
                std::optional<core::CpuMemoryQuotaScope> quotaScope;
                if (recordingRetention_)
                    quotaScope.emplace(recordingRetention_->quota());
                nativeSubmitWaitCommandBuffer_ = retainBorrowedNativeObject(waitCommand);
                if (!encodeMetalRhiNativeSharedEventWait(
                        nativeSubmitWaitCommandBuffer_, waitEvent, waitValue, diagnostics)) {
                    return failBeforeCommit();
                }
            }
        } catch (const std::bad_alloc&) {
            diagnostics = "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED";
            return failBeforeCommit();
        }
    }
    if (signalEvent
        && !encodeMetalRhiNativeSharedEventSignal(
            nativeCommandBuffer_, signalEvent, signalValue, diagnostics)) {
        return failBeforeCommit();
    }
    std::size_t markedTimestampQueryCount = 0u;
    std::size_t markedPipelineStatisticsQueryCount = 0u;
    const auto rollbackMarkedQueries = [&]() {
        bool rollbackSuccess = true;
        const auto appendRollbackDiagnostic = [&](const std::pmr::string& value) {
            if (value.empty()) {
                return;
            }
            if (!diagnostics.empty()) {
                diagnostics += "; ";
            }
            diagnostics += value;
        };
        for (std::size_t index = 0u; index < markedTimestampQueryCount; ++index) {
            std::pmr::string rollbackDiagnostics;
            if (!rollbackMetalRhiNativeTimestampQuerySubmitted(
                    recordedTimestampQueries_[index].pool,
                    recordedTimestampQueries_[index].queryIndex,
                    rollbackDiagnostics)) {
                rollbackSuccess = false;
                appendRollbackDiagnostic(rollbackDiagnostics);
            }
        }
        for (std::size_t index = 0u; index < markedPipelineStatisticsQueryCount; ++index) {
            std::pmr::string rollbackDiagnostics;
            if (!rollbackMetalRhiNativePipelineStatisticsQuerySubmitted(
                    recordedPipelineStatisticsQueries_[index].pool,
                    recordedPipelineStatisticsQueries_[index].queryIndex,
                    rollbackDiagnostics)) {
                rollbackSuccess = false;
                appendRollbackDiagnostic(rollbackDiagnostics);
            }
        }
        return rollbackSuccess;
    };
    for (const auto& query : recordedTimestampQueries_) {
        if (!markMetalRhiNativeTimestampQuerySubmitted(query.pool, query.queryIndex, diagnostics)) {
            (void)rollbackMarkedQueries();
            return failBeforeCommit();
        }
        ++markedTimestampQueryCount;
    }
    for (const auto& query : recordedPipelineStatisticsQueries_) {
        if (!markMetalRhiNativeQueryPoolSubmitted(query.pool, query.queryIndex, diagnostics)) {
            (void)rollbackMarkedQueries();
            return failBeforeCommit();
        }
        ++markedPipelineStatisticsQueryCount;
    }
    // Transfer every allocation-owning payload into the native completion
    // closure before commit. This removes duplicate payload references from
    // the recorded command object while preserving the exact GPU lifetime.
    // Aliasing pointers share the one preparation-time control block. The
    // callback owns its contents without retaining this command payload.
    if (!submittedQueries->takeRecorded(
            retainedQueryPools_, recordedTimestampQueries_, recordedPipelineStatisticsQueries_)) {
        diagnostics = "RHI-METAL-QUERY-RETENTION-TRANSFER-INVALID";
        (void)rollbackMarkedQueries();
        return failBeforeCommit();
    }
    if (recordingRetention_ && !recordingRetention_->markSubmitted()) {
        submittedQueries->releaseAfterResolution();
        diagnostics = "RHI-COMMAND-RETENTION-RECORDING-CLOSED";
        return failBeforeCommit();
    }
    if (originalCommandCustody_)
        if (const auto* id = originalCommandCustody_->beforeNativeCommit()) {
            diagnostics = id;
            return failBeforeCommit();
        }
    if (submissionDomain_)
        if (const auto* id = submissionDomain_->storage_->acceptValidator(submissionTicket_)) {
            diagnostics = id;
            return failBeforeCommit();
        }
    submissionRetention_->originalCommandCustody = std::move(originalCommandCustody_);
    const auto submittedOriginal = std::shared_ptr<std::shared_ptr<MetalOriginalCommandCustody>>(
        submissionRetention_, &submissionRetention_->originalCommandCustody);
    submissionRetention_->commandRetention = std::move(recordingRetention_);
    const auto submittedCommandRetention =
        std::shared_ptr<core::UniquePtr<rhi::common::CommandRetentionStorage>>(
            submissionRetention_, &submissionRetention_->commandRetention);
    const auto submittedResources = std::shared_ptr<MetalRhiSubmittedResourceRetention>(
        submissionRetention_, &submissionRetention_->resources);
    submittedResources->resources = std::move(retainedResourcePayloads_);
    submittedResources->staging = std::move(retainedStagingPayloads_);
    submittedResources->argumentBuffers = std::move(retainedArgumentBuffers_);
    submittedResources->bindlessPipelines = std::move(retainedBindlessPipelines_);
    submittedResources->bindlessSnapshots = std::move(retainedBindlessSnapshots_);
    submittedResources->preparedBindings = std::move(retainedPreparedBindings_);
    // The completion owner now has every native resource needed by encoded
    // work. Drop recording-only cache references before commit so they cannot
    // extend an in-flight lifetime past completion.
    cachedArgumentBuffers_.clear();
    retainedResourcePayloadIdentities_.clear();
    const auto timingFrameSerial = timingFrameSerial_;
    const auto completionState = completion;
    const auto stagingCompletedEpoch = stagingCompletedEpoch_;
    submissionRetention_->stagingUploads = std::move(stagingUploads_);
    const auto submittedStaging =
        std::shared_ptr<core::UniquePtr<rhi::RecordedStagingUploadJournal>>(
            submissionRetention_, &submissionRetention_->stagingUploads);
    const auto retainedSubmitWait = nativeSubmitWaitCommandBuffer_;
    const auto retainedCommandCapacity = commandCapacity_;
    const auto pendingBindlessFaultReadbacks =
        std::shared_ptr<core::pmr::Vector<PendingBindlessFaultReadback>>(
            submissionRetention_, &submissionRetention_->faultReadbacks);
    *pendingBindlessFaultReadbacks = std::move(pendingBindlessFaultReadbacks_);
    submissionRetention_->ordinaryWriteReadback = ordinaryWriteReadback_;
    // Retain on this owner thread through journal commit; completion may race
    // native commit. The callback owns the moved lease without owning command.
    const auto committingCanonical = canonicalProducer_;
    const auto committingClosed = closedConsumerNative_;
    submissionRetention_->closedConsumer = closedConsumerNative_;
    if (closedConsumerNative_) {
        const auto& reservation = closedConsumerNative_->reservation();
        (void)closedConsumerSequence_->transition(
            MetalRhiClosedConsumerRecordingReservation::Status::Submitted,
            reservation->preparation()->facts().identity);
        reservation->preparation()->issuance()->cold()->producer()->consumed_.store(
            true, std::memory_order_release);
        reservation->status_.store(MetalRhiClosedConsumerRecordingReservation::Status::Submitted,
            std::memory_order_release);
    }
    const auto submittedClosed =
        std::shared_ptr<std::shared_ptr<const MetalRhiClosedConsumerNativePreparation>>(
            submissionRetention_, &submissionRetention_->closedConsumer);
    submissionRetention_->canonicalProducer = canonicalProducer_;
    if (canonicalProducer_) {
        canonicalProducerSequence_->transition(
            MetalRhiCanonicalProducerRecordingPreparation::Status::Submitted,
            canonicalProducer_->facts().identity);
        submissionRetention_->canonicalFaultLease = std::move(canonicalProducer_->lease_);
        canonicalProducer_->cold_->producer()->consumed_.store(true, std::memory_order_release);
        canonicalProducer_->status_.store(
            MetalRhiCanonicalProducerRecordingPreparation::Status::Submitted,
            std::memory_order_release);
    }
    const auto submittedCanonical =
        std::shared_ptr<std::shared_ptr<const MetalRhiCanonicalProducerRecordingPreparation>>(
            submissionRetention_, &submissionRetention_->canonicalProducer);
    const auto submittedCanonicalLease = std::shared_ptr<rhi::BindlessFaultSubmissionLease>(
        submissionRetention_, &submissionRetention_->canonicalFaultLease);
    const auto committingIndirect = indirectExecution_;
    submissionRetention_->indirectExecution = indirectExecution_;
    if (indirectExecution_) {
        submissionRetention_->indirectLease = std::move(indirectExecution_->lease_);
        indirectExecution_->status_.store(
            MetalRhiIndirectExecutionPreparation::Status::Submitted, std::memory_order_release);
        indirectExecution_->output_->consumed_.store(true, std::memory_order_release);
    }
    const auto submittedIndirect =
        std::shared_ptr<std::shared_ptr<const MetalRhiIndirectExecutionPreparation>>(
            submissionRetention_, &submissionRetention_->indirectExecution);
    const auto submittedIndirectLease = std::shared_ptr<MetalRhiIndirectOutputRecordingLease>(
        submissionRetention_, &submissionRetention_->indirectLease);
    const auto submittedWriteReadback =
        std::shared_ptr<std::shared_ptr<MetalRhiOrdinaryPreparedWriteReadback>>(
            submissionRetention_, &submissionRetention_->ordinaryWriteReadback);
    const auto originalResultSource = sharedMetalRhiOriginalResultLossSource();
    const auto originalDomain = submissionDomain_;
    const auto originalTicket = submissionTicket_;
    if (originalDomain) {
        submissionRetention_->commandCapacity = commandCapacity_;
        submissionRetention_->stagingEpoch = stagingCompletedEpoch_;
    }
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completedCommandBuffer) {
        // Keep the moved payloads alive through completion-side query
        // resolution, then explicitly release them. A completed Metal
        // command buffer may retain this handler block beyond callback return.
        auto waitCommand = reinterpret_cast<id<MTLCommandBuffer>>(retainedSubmitWait.get());
#if TARGET_OS_OSX
        if (@available(macOS 10.13, *)) {
            for (const auto native : {completedCommandBuffer, waitCommand}) {
                if (originalResultSource && native && native.status == MTLCommandBufferStatusError
                    && [native.error.domain isEqualToString:MTLCommandBufferErrorDomain]
                    && native.error.code == MTLCommandBufferErrorDeviceRemoved)
                    originalResultSource->observeDeviceRemoval(
                        reinterpret_cast<const void*>(native.commandQueue.device));
            }
        }
#endif
        const bool commandBufferSucceeded =
            completedCommandBuffer.status == MTLCommandBufferStatusCompleted
            && (!waitCommand || waitCommand.status == MTLCommandBufferStatusCompleted);
        if (originalDomain) {
            // Actual original callback publishes only its once-delivered atom.
            // The device/owner fence joins and retires the existing journal.
            if (rhi::OriginalSubmissionCompletionDomain::deliverValidatorResult(
                    originalTicket, commandBufferSucceeded))
                std::terminate();
            completionState->notifyNativeResult();
            return;
        }
        const bool originalLifetimeValid =
            !*submittedOriginal || (*submittedOriginal)->nativeCompleted(commandBufferSucceeded);
        submittedOriginal->reset();
        if (*submittedWriteReadback)
            (*submittedWriteReadback)
                ->status_.store(commandBufferSucceeded
                        ? MetalRhiOrdinaryPreparedWriteReadback::Status::Completed
                        : MetalRhiOrdinaryPreparedWriteReadback::Status::Failed,
                    std::memory_order_release);
        for (const auto& queryPool : submittedQueries->pools()) {
            resolveMetalRhiNativeQueryPool(queryPool,
                timingFrameSerial,
                submittedQueries->timestamps(),
                commandBufferSucceeded);
        }
        for (const auto& query : submittedQueries->statistics()) {
            resolveMetalRhiNativePipelineStatistics(
                query.pool, timingFrameSerial, query.queryIndex, commandBufferSucceeded);
        }
        submittedQueries->releaseAfterResolution();
        for (auto& pending : *pendingBindlessFaultReadbacks) {
            if (!pending.result) {
                pending.lease.release();
                continue;
            }
            if (!commandBufferSucceeded || !pending.snapshot || !pending.destination) {
                pending.result->fail();
                pending.lease.release();
                continue;
            }
            std::uint32_t bits = 0u;
            std::pmr::string readDiagnostics;
            const auto* readback = pending.snapshot->faultReadbackBuffer();
            if (!readback
                || !readMetalRhiNativeBuffer(pending.destination,
                    &bits,
                    sizeof(bits),
                    0u,
                    readback->desc().sizeBytes,
                    readDiagnostics)) {
                pending.result->fail();
                pending.lease.release();
                continue;
            }
            pending.result->complete(bits);
            pending.lease.markFaultReadbackCompleted();
        }
        pendingBindlessFaultReadbacks->clear();
        retainedCommandCapacity->release();
        // The terminal callback owns only this command's exact reservations.
        // Token resolution is atomic; allocation/reclamation stays on its owner.
        const bool hadStaging = *submittedStaging && !(*submittedStaging)->empty();
        if (*submittedStaging) {
            (void)(*submittedStaging)->retireCompleted();
            submittedStaging->reset();
        }
        if (stagingCompletedEpoch && hadStaging)
            stagingCompletedEpoch->fetch_add(1u, std::memory_order_release);
        submittedResources->releaseAfterCompletion();
        bool canonicalLifetimeValid = true;
        bool closedLifetimeValid = true;
        if (*submittedClosed) {
            using ClosedStatus = MetalRhiClosedConsumerRecordingReservation::Status;
            const auto& reservation = (*submittedClosed)->reservation();
            const auto after =
                commandBufferSucceeded ? ClosedStatus::Completed : ClosedStatus::Failed;
            closedLifetimeValid = !rhi::GpuIndirectOutputLifetimeContract::validateTransition(
                reservation->status(), after, true);
            if (commandBufferSucceeded && closedLifetimeValid) {
                reservation->producerLease_.markFaultReadbackCompleted();
                reservation->consumerLease_.markFaultReadbackCompleted();
            }
            reservation->producerLease_.release();
            reservation->consumerLease_.release();
            reservation->packetLease_.release();
            reservation->status_.store(
                closedLifetimeValid ? after : ClosedStatus::Failed, std::memory_order_release);
        }
        if (*submittedCanonical) {
            const auto after = commandBufferSucceeded
                ? MetalRhiCanonicalProducerRecordingPreparation::Status::Completed
                : MetalRhiCanonicalProducerRecordingPreparation::Status::Failed;
            canonicalLifetimeValid = !rhi::GpuIndirectOutputLifetimeContract::validateTransition(
                (*submittedCanonical)->status(), after, true);
            if (commandBufferSucceeded && canonicalLifetimeValid)
                submittedCanonicalLease->markFaultReadbackCompleted();
            submittedCanonicalLease->release();
            (*submittedCanonical)
                ->status_.store(canonicalLifetimeValid
                        ? after
                        : MetalRhiCanonicalProducerRecordingPreparation::Status::Failed,
                    std::memory_order_release);
        }
        bool indirectLifetimeValid = true;
        if (*submittedIndirect) {
            *submittedIndirectLease = MetalRhiIndirectOutputRecordingLease{};
            const auto after = commandBufferSucceeded
                ? MetalRhiIndirectExecutionPreparation::Status::Completed
                : MetalRhiIndirectExecutionPreparation::Status::Failed;
            indirectLifetimeValid = !rhi::GpuIndirectOutputLifetimeContract::validateTransition(
                (*submittedIndirect)->status(), after, true);
            (*submittedIndirect)
                ->status_.store(indirectLifetimeValid
                        ? after
                        : MetalRhiIndirectExecutionPreparation::Status::Failed,
                    std::memory_order_release);
        }
        if (*submittedCommandRetention) {
            (*submittedCommandRetention)->retireAfterCompletion(true);
            submittedCommandRetention->reset();
        }
        submittedWriteReadback->reset();
        submittedIndirect->reset();
        submittedCanonical->reset();
        submittedClosed->reset();
        completionState->complete(commandBufferSucceeded && indirectLifetimeValid
            && canonicalLifetimeValid && closedLifetimeValid && originalLifetimeValid);
    }];
    // All pre-commit validation, signal encoding and query marking succeeded.
    // Retention belongs to the native payload/completion, not an upper layer.
    if (nativeSubmitWaitCommandBuffer_) {
        [reinterpret_cast<id<MTLCommandBuffer>>(nativeSubmitWaitCommandBuffer_.get()) commit];
    }
    [commandBuffer commit];
    if (originalOrdinary_) {
        submissionRetention_->queueAcceptance.ordinal_ = queueOrdinal;
        submissionRetention_->queueAcceptance.nativeAccepted_ = true;
    }
    queueCommitLock.unlock();
    // Keep both actual wrappers alive on this owner thread until commit has
    // detached their snapshots; a fast completion cannot race their destruction
    // against journal mutation. All original GPU owners still belong to the
    // completion retention projection after these recording fields retire.
    if (ordinaryWriteStates_)
        ordinaryWriteStates_->commit();
    if (indirectOutputStates_)
        indirectOutputStates_->commit();
    if (canonicalProducerStates_)
        canonicalProducerStates_->commit();
    if (committingClosed)
        committingClosed->states()->commit();
    closedConsumerNative_.reset();
    closedConsumerReservation_.reset();
    canonicalProducer_.reset();
    indirectExecution_.reset();
    ordinaryReadConsumer_.reset();
    ordinaryWriteReadback_.reset();
    ordinaryWrite_.reset();
    ++queueStats.nativeQueueSubmitCount;
    queueStats.boundedCompletionTelemetry = true;
    return stats_->nativeCommandBufferFinished;
}

std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiOrdinaryWriteNativeCommandPayload(
    const MetalRhiOrdinaryWritePrototypeIntent& intent,
    const core::pmr::String& label,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    core::UniquePtr<rhi::common::CommandRetentionStorage> retention,
    std::shared_ptr<core::MutationJournal> states,
    core::pmr::String& diagnostic)
{
    if (const auto* id = intent.validate()) {
        diagnostic = id;
        return {};
    }
    if (!retention || !states || !states->empty() || !registry) {
        diagnostic = "RHI-METAL-ORDINARY-WRITE-COMMAND-PREPARATION-INVALID";
        return {};
    }
    const auto quota = retention->quota();
    try {
        {
            core::CpuMemoryQuotaScope sharedScope(nullptr);
            sharedMetalRhiNativeCommandQueue();
            sharedMetalRhiCommandCapacityBudget();
        }
        core::CpuMemoryQuotaScope scope(quota);
        auto payload = createMetalRhiNativeCommandPayload(
            label, std::move(registry), &diagnostic, 0u, 0u, 0u, std::move(retention));
        if (!payload)
            return {};
        payload->preparedWriteBlitEncoderSlot_ =
            core::makeShared<core::ExternalObjectSlot>([](void* object) noexcept {
                [reinterpret_cast<id>(object) release];
            });
        payload->ordinaryWriteStates_ = std::move(states);
        payload->maxOrdinaryWriteDraws_ = intent.maxProducerDraws;
        payload->maxOrdinaryConsumerDraws_ = intent.maxConsumerDraws;
        return payload;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        diagnostic = "RHI-METAL-ORDINARY-WRITE-COMMAND-ALLOCATION-FAILED";
        return {};
    }
}

std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiOrdinaryIndirectNativeCommandPayload(
    const rhi::IndirectRecordingBudget& intent,
    const core::pmr::String& label,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    core::UniquePtr<rhi::common::CommandRetentionStorage> retention,
    core::pmr::String& diagnostic)
{
    if (const auto* id = intent.validate()) {
        diagnostic = id;
        return {};
    }
    if (!retention || !registry || retention->budget().maxQueryPools
        || retention->budget().maxTimestampWrites
        || retention->budget().maxPipelineStatisticsWrites) {
        diagnostic = "RHI-METAL-ORDINARY-INDIRECT-COMMAND-PREPARATION-INVALID";
        return {};
    }
    if (!@available(macOS 10.11, iOS 9.0, *)) {
        diagnostic = "RHI-METAL-ORDINARY-INDIRECT-NATIVE-API-UNAVAILABLE";
        return {};
    }
    const auto quota = retention->quota();
    try {
        {
            core::CpuMemoryQuotaScope sharedScope(nullptr);
            sharedMetalRhiNativeCommandQueue();
            sharedMetalRhiCommandCapacityBudget();
        }
        core::CpuMemoryQuotaScope scope(quota);
        auto result = createMetalRhiNativeCommandPayload(
            label, std::move(registry), &diagnostic, 0u, 0u, 0u, std::move(retention));
        if (result)
            result->ordinaryIndirectRecording_.emplace(intent);
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        diagnostic = "RHI-METAL-ORDINARY-INDIRECT-COMMAND-ALLOCATION-FAILED";
        return {};
    }
}

std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiIndirectOutputNativeCommandPayload(
    const rhi::GpuIndirectOutputRecordingBudget& intent,
    const core::pmr::String& label,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    core::UniquePtr<rhi::common::CommandRetentionStorage> retention,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (const auto* id = intent.validate()) {
        diagnostic = id;
        return {};
    }
    if (!retention || !registry || retention->budget().maxQueryPools
        || retention->budget().maxTimestampWrites
        || retention->budget().maxPipelineStatisticsWrites) {
        diagnostic = "RHI-METAL-INDIRECT-EXECUTION-COMMAND-PREPARATION-INVALID";
        return {};
    }
    if (!@available(macOS 10.11, iOS 9.0, *)) {
        diagnostic = "RHI-METAL-INDIRECT-EXECUTION-NATIVE-API-UNAVAILABLE";
        return {};
    }
    const auto quota = retention->quota();
    try {
        {
            core::CpuMemoryQuotaScope sharedScope(nullptr);
            sharedMetalRhiNativeCommandQueue();
            sharedMetalRhiCommandCapacityBudget();
        }
        core::CpuMemoryQuotaScope scope(quota);
        auto result = createMetalRhiNativeCommandPayload(
            label, std::move(registry), &diagnostic, 0u, 0u, 0u, std::move(retention));
        if (!result)
            return {};
        result->indirectOutputStates_ = core::makeShared<core::MutationJournal>();
        result->preparedIndirectComputeEncoderSlot_ =
            core::makeShared<core::ExternalObjectSlot>([](void* object) noexcept {
                [reinterpret_cast<id>(object) release];
            });
        result->indirectOutputBudget_ = intent;
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        diagnostic = "RHI-METAL-INDIRECT-EXECUTION-COMMAND-ALLOCATION-FAILED";
        return {};
    }
}

void MetalRhiNativeCommandPayload::markStagingUpload(
    std::shared_ptr<std::atomic<std::uint64_t>> completedEpoch)
{
    stagingCompletedEpoch_ = std::move(completedEpoch);
}

std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiNativeCommandPayload(
    const std::pmr::string& label,
    std::shared_ptr<MetalRhiResourceRegistry> resourceRegistry,
    std::pmr::string* diagnostics,
    std::uint32_t maxBindlessPipelines,
    std::uint32_t maxBindlessSnapshots,
    std::uint32_t maxBindlessGraphicsAccesses,
    core::UniquePtr<rhi::common::CommandRetentionStorage> preparedRetention)
{
    @autoreleasepool {
        try {
            if (preparedRetention
                && (preparedRetention->state()
                        != rhi::common::CommandRetentionStorage::State::Recording
                    || preparedRetention->resourceOwnerCount()
                    || preparedRetention->queryPoolOwnerCount()
                    || !preparedRetention->timestamps().empty()
                    || !preparedRetention->statistics().empty()
                    || preparedRetention->quota()->limitBytes()
                        != preparedRetention->budget().maxCpuMetadataBytes
                    || maxBindlessPipelines || maxBindlessSnapshots || maxBindlessGraphicsAccesses
                    || !resourceRegistry)) {
                if (diagnostics)
                    *diagnostics = "RHI-METAL-COMMAND-RETENTION-PREPARATION-INVALID";
                return {};
            }
            // Shared backend queue ownership is not command-local metadata.
            const auto queuePayload = sharedMetalRhiNativeCommandQueue();
            // Same cold shared infrastructure as the native queue. Submitting
            // or resolving a completed result never initializes a source.
            (void)sharedMetalRhiOriginalResultLossSource();
            // Initialize the one shared capacity controller outside the command
            // quota too; only this command's lease belongs to command metadata.
            const auto sharedCapacity = sharedMetalRhiCommandCapacityBudget();
            std::optional<core::CpuMemoryQuotaScope> quotaScope;
            if (preparedRetention)
                quotaScope.emplace(preparedRetention->quota());
            std::pmr::string capacityDiagnostics;
            auto reservation = reserveSharedMetalRhiCommandCapacity(
                kMetalPrimaryCommandReservation, capacityDiagnostics);
            if (!reservation) {
                if (diagnostics)
                    *diagnostics = capacityDiagnostics;
                return {};
            }
            auto stats = arp::core::makeShared<MetalRhiNativeCommandStats>();
            stats->backendPayloadPresent = true;
            id<MTLCommandQueue> queue =
                queuePayload ? reinterpret_cast<id<MTLCommandQueue>>(queuePayload.get()) : nil;
            id<MTLCommandBuffer> commandBuffer = queue == nil ? nil : [queue commandBuffer];
            if (commandBuffer == nil) {
                if (diagnostics)
                    *diagnostics = "RHI-METAL-COMMAND-BUFFER-CREATE-FAILED";
                return {};
            }
            commandBuffer.label = [NSString stringWithUTF8String:label.c_str()];
            stats->nativeCommandBufferCreated = commandBuffer != nil;
            stats->nativeRuntimeBacked = commandBuffer != nil;
            stats->nativeRuntimeDiagnosticOnly = !stats->nativeRuntimeBacked;
            auto payload = arp::core::makeShared<MetalRhiNativeCommandPayload>(stats,
                queuePayload,
                retainBorrowedNativeObject(commandBuffer),
                std::move(resourceRegistry),
                std::move(reservation),
                maxBindlessPipelines,
                maxBindlessSnapshots,
                maxBindlessGraphicsAccesses);
            if (preparedRetention)
                payload->preparedRenderEncoderSlot_ =
                    core::makeShared<core::ExternalObjectSlot>([](void* object) noexcept {
                        [reinterpret_cast<id>(object) release];
                    });
            payload->usesRecordingRetention_ = static_cast<bool>(preparedRetention);
            payload->recordingRetention_ = std::move(preparedRetention);
            payload->recordingIdentity_ = rhi::allocateGpuResourceId().value;
            if (!payload->recordingIdentity_) {
                if (diagnostics)
                    *diagnostics = "RHI-METAL-COMMAND-RECORDING-ID-EXHAUSTED";
                return {};
            }
            return payload;
        } catch (const std::bad_alloc&) {
            if (diagnostics)
                *diagnostics = "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED";
            return {};
        }
    }
}

std::shared_ptr<MetalRhiNativeQueueStats> createMetalRhiNativeQueueStats()
{
    auto stats = arp::core::makeShared<MetalRhiNativeQueueStats>();
    stats->backendPayloadPresent = true;
    stats->nativeRuntimeBacked = defaultMetalDevice() != nil;
    stats->nativeRuntimeDiagnosticOnly = !stats->nativeRuntimeBacked;
    return stats;
}

bool waitMetalRhiNativeQueueIdle(std::pmr::string& diagnostics)
{
    const auto reservation = reserveSharedMetalRhiCommandCapacity(1u, diagnostics);
    if (!reservation)
        return false;
    const auto queuePayload = sharedMetalRhiNativeCommandQueue();
    id<MTLCommandQueue> queue =
        queuePayload ? reinterpret_cast<id<MTLCommandQueue>>(queuePayload.get()) : nil;
    if (queue == nil) {
        diagnostics = "RHI-METAL-WAIT-IDLE-COMMAND-QUEUE-MISSING";
        return false;
    }
    id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
    if (commandBuffer == nil) {
        diagnostics = "RHI-METAL-WAIT-IDLE-COMMAND-BUFFER-MISSING";
        return false;
    }
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
    if (commandBuffer.status != MTLCommandBufferStatusCompleted) {
        diagnostics = "RHI-METAL-WAIT-IDLE-COMMAND-BUFFER-FAILED";
        return false;
    }
    return true;
}

MetalRhiNativeQueueDrainState drainMetalRhiNativeQueueForShutdown(
    const rhi::semantics::ShutdownDrainRequest& request, std::pmr::string& diagnostics)
{
    const auto started = std::chrono::steady_clock::now();
    if (!request.validAt(started)) {
        diagnostics = rhi::semantics::kShutdownDrainDeadlineExpiredDiagnostic;
        return MetalRhiNativeQueueDrainState::DeadlineExpired;
    }

    const auto reservation = reserveSharedMetalRhiCommandCapacity(1u, diagnostics);
    if (!reservation)
        return MetalRhiNativeQueueDrainState::DiagnosticFailure;
    const auto queuePayload = sharedMetalRhiNativeCommandQueue();
    id<MTLCommandQueue> queue =
        queuePayload ? reinterpret_cast<id<MTLCommandQueue>>(queuePayload.get()) : nil;
    if (queue == nil) {
        diagnostics = "ARP-RHI-METAL-SHUTDOWN-DRAIN-COMMAND-QUEUE-MISSING";
        return MetalRhiNativeQueueDrainState::DiagnosticFailure;
    }
    id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
    if (commandBuffer == nil) {
        diagnostics = "ARP-RHI-METAL-SHUTDOWN-DRAIN-COMMAND-BUFFER-MISSING";
        return MetalRhiNativeQueueDrainState::DiagnosticFailure;
    }

    struct Completion final {
        void complete(const MTLCommandBufferStatus completedStatus)
        {
            {
                std::lock_guard lock(mutex);
                status = completedStatus;
                completed = true;
            }
            condition.notify_all();
        }

        bool waitUntil(const std::chrono::steady_clock::time_point deadline)
        {
            std::unique_lock lock(mutex);
            return condition.wait_until(lock, deadline, [this] {
                return completed;
            });
        }

        MTLCommandBufferStatus completedStatus() const
        {
            std::lock_guard lock(mutex);
            return status;
        }

        mutable std::mutex mutex;
        std::condition_variable condition;
        MTLCommandBufferStatus status = MTLCommandBufferStatusNotEnqueued;
        bool completed = false;
    };

    const auto completion = arp::core::makeShared<Completion>();
    // Keep the capacity charge until Metal retires the marker. A deadline
    // result means unknown completion, never that the marker disappeared.
    const auto retainedReservation = reservation;
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completedCommandBuffer) {
        retainedReservation->release();
        completion->complete(completedCommandBuffer.status);
    }];
    [commandBuffer commit];

    if (!completion->waitUntil(request.deadline)) {
        diagnostics = rhi::semantics::kShutdownDrainDeadlineExpiredDiagnostic;
        return MetalRhiNativeQueueDrainState::DeadlineExpired;
    }
    if (completion->completedStatus() != MTLCommandBufferStatusCompleted) {
        diagnostics = "ARP-RHI-METAL-SHUTDOWN-DRAIN-COMMAND-BUFFER-FAILED";
        return MetalRhiNativeQueueDrainState::DiagnosticFailure;
    }
    return MetalRhiNativeQueueDrainState::Completed;
}

bool metalRhiNativeCompletionAndAliasingCommandsAvailable()
{
    if (@available(macOS 10.13, iOS 10.0, *)) {
        return defaultMetalDevice() != nil;
    }
    return false;
}

} // namespace arp::rhi_metal
