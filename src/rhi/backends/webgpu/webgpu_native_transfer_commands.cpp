#include "core/cpu_allocator.h"
#include "core/unsigned_range.h"
#include "rhi/backends/webgpu/webgpu_native_command_payload_impl.h"
#include "rhi/backends/webgpu/webgpu_native_transfer_copy_lowering.h"
#include "rhi/backends/webgpu/webgpu_ordinary_resource_retention.h"
#include "rhi/backends/webgpu/webgpu_transfer_retention.h"
#include "rhi/frontend/transfer_upload_layout_validation.h"
#include "rhi/frontend/transfer_validation.h"

#include <cstring>

#if defined(__EMSCRIPTEN__)
#include <cstdint>
#include <emscripten/emscripten.h>
#endif

namespace arp::rhi_webgpu {

namespace {

#if defined(__EMSCRIPTEN__)
// emdawnwebgpu currently rejects a view backed directly by the resizable
// Emscripten heap. Copy the upload into a fixed ArrayBuffer before calling
// the browser queue API; the WebGPU handles remain backend-local.
EM_JS(void,
    arpWebGpuQueueWriteTextureFixed,
    (uintptr_t queuePtr,
        uintptr_t texturePtr,
        uintptr_t dataPtr,
        uint32_t dataSize,
        uint32_t mipLevel,
        uint32_t originX,
        uint32_t originY,
        uint32_t originZ,
        uint32_t aspect,
        uint32_t bytesPerRow,
        uint32_t rowsPerImage,
        uint32_t width,
        uint32_t height,
        uint32_t depth),
    {
        // clang-format off
        const queue = WebGPU.getJsObject(queuePtr);
        const texture = WebGPU.getJsObject(texturePtr);
        const source = HEAPU8.subarray(dataPtr, dataPtr + dataSize);
        const fixedData = new Uint8Array(dataSize);
        fixedData.set(source);
        const aspectName = aspect === 2 ? 'stencil-only' : aspect === 3 ? 'depth-only' : 'all';
        queue.writeTexture({
            texture,
            mipLevel,
            origin : {x : originX, y : originY, z : originZ},
            aspect : aspectName,
        },
                           fixedData, {bytesPerRow, rowsPerImage}, {width, height, depth});
        // clang-format on
    });
#endif

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
class WebGpuOrdinaryTransferRecording final {
public:
    explicit WebGpuOrdinaryTransferRecording(WebGpuNativeCommandPayload::Impl& impl) : impl_(impl)
    {}

    const char* preflight(rhi::GpuResourceId source, rhi::GpuResourceId destination) noexcept
    {
        if (!impl_.ordinaryTransfer || impl_.ordinaryUnsupported || impl_.finished
            || impl_.submitted || !impl_.encoder || !impl_.transferCompletion)
            return "RHI-WEBGPU-TRANSFER-RECORDING-CLOSED";
        auto& callback = *impl_.transferCompletion;
        if (webGpuNativeDeviceLost() || !callback.owner->ordinaryTransfers.accepting())
            return "RHI-WEBGPU-TRANSFER-COMPLETION-CLOSED";
        root_ = callback.owner->ordinaryTransfers.get(callback.ticket);
        if (!root_
            || root_->storage().state() != rhi::common::CommandRetentionStorage::State::Recording)
            return "RHI-WEBGPU-TRANSFER-RECORDING-CLOSED";
        if (!root_->matches()) {
            impl_.ordinaryUnsupported = true;
            return "RHI-WEBGPU-TRANSFER-IDENTITY-CHANGED";
        }
        source_ = root_->find(source);
        destination_ = root_->find(destination);
        if (!source_ || !destination_)
            return "RHI-WEBGPU-TRANSFER-RESOURCE-NOT-PREPARED";
        if (source == destination)
            return "RHI-WEBGPU-TRANSFER-SELF-COPY-UNSUPPORTED";
        if (destination_->writeSeal && destination_->writeSeal->transferWritesSealed())
            return "RHI-BUFFER-GPU-WRITES-SEALED";
        return nullptr;
    }

    const auto& source() const noexcept { return *source_; }

    const auto& destination() const noexcept { return *destination_; }

    template <class Desc, class NativeCall>
    bool execute(const Desc& incoming, NativeCall&& nativeCall, core::pmr::String& diagnostic)
    {
        Desc frozen;
        core::OrderedSpanCursor::Ticket ticket;
        if (impl_.frozenTransferUsage) {
            const char* id = nullptr;
            ticket = impl_.frozenTransferUsage->preflight(*root_, incoming, frozen, id);
            if (!ticket) {
                diagnostic = id;
                if (impl_.frozenTransferUsage->validate())
                    impl_.ordinaryUnsupported = true;
                return false;
            }
        }

        class DispatchScope final {
        public:
            explicit DispatchScope(bool& dispatch) : dispatch_(dispatch) { dispatch_ = true; }

            ~DispatchScope() { dispatch_ = false; }

        private:
            bool& dispatch_;
        } scope(impl_.ordinaryTransferDispatch);

        const bool recorded = nativeCall(impl_.frozenTransferUsage ? frozen : incoming);
        if (impl_.frozenTransferUsage) {
            const char* id = nullptr;
            if (!recorded || !impl_.frozenTransferUsage->commit(ticket, id)) {
                impl_.frozenTransferUsage->abort();
                impl_.ordinaryUnsupported = true;
                if (id)
                    diagnostic = id;
                return false;
            }
        }
        return recorded;
    }

private:
    WebGpuNativeCommandPayload::Impl& impl_;
    WebGpuOrdinaryTransferRetention* root_ = nullptr;
    const WebGpuTransferResourceObservation* source_ = nullptr;
    const WebGpuTransferResourceObservation* destination_ = nullptr;
};

void recordCommand(const std::shared_ptr<WebGpuNativeCommandStats>& stats)
{
    if (stats) {
        ++stats->nativeCommandRecordCount;
    }
}

#endif

} // namespace

bool WebGpuNativeCommandPayload::recordRegisteredTransfer(
    const rhi::BufferCopyDesc& desc, core::pmr::String& diagnostic)
{
    if (const auto* id = preparedRecordingDiagnostic()) {
        diagnostic = id;
        return false;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_) {
        diagnostic = "RHI-WEBGPU-TRANSFER-RECORDING-CLOSED";
        return false;
    }
    WebGpuOrdinaryTransferRecording recording(*impl_);
    if (const auto* id = recording.preflight(desc.sourceBuffer, desc.destinationBuffer)) {
        diagnostic = id;
        return false;
    }
    const auto& source = recording.source();
    const auto& destination = recording.destination();
    if (const auto* id = WebGpuTransferCopyValidation::validate(source, destination, desc)) {
        diagnostic = id;
        return false;
    }
    diagnostic.clear();
    return recording.execute(
        desc,
        [&](const rhi::BufferCopyDesc& original) {
            return recordBufferCopy(*source.buffer, *destination.buffer, original, diagnostic);
        },
        diagnostic);
#else
    (void)desc;
    diagnostic = "RHI-WEBGPU-TRANSFER-NATIVE-UNAVAILABLE";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordRegisteredTransfer(
    const rhi::BufferToTextureCopyDesc& desc, core::pmr::String& diagnostic)
{
    if (const auto* id = preparedRecordingDiagnostic()) {
        diagnostic = id;
        return false;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_) {
        diagnostic = "RHI-WEBGPU-TRANSFER-RECORDING-CLOSED";
        return false;
    }
    WebGpuOrdinaryTransferRecording recording(*impl_);
    if (const auto* id = recording.preflight(desc.sourceBuffer, desc.destinationTexture)) {
        diagnostic = id;
        return false;
    }
    const auto& source = recording.source();
    const auto& destination = recording.destination();
    if (const auto* id = WebGpuTransferCopyValidation::validate(source, destination, desc)) {
        diagnostic = id;
        return false;
    }
    diagnostic.clear();
    return recording.execute(
        desc,
        [&](const rhi::BufferToTextureCopyDesc& original) {
            return recordBufferToTextureCopy(
                *source.buffer, *destination.texture, original, diagnostic);
        },
        diagnostic);
#else
    (void)desc;
    diagnostic = "RHI-WEBGPU-TRANSFER-NATIVE-UNAVAILABLE";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordRegisteredTransfer(
    const rhi::TextureToBufferCopyDesc& desc, core::pmr::String& diagnostic)
{
    if (const auto* id = preparedRecordingDiagnostic()) {
        diagnostic = id;
        return false;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_) {
        diagnostic = "RHI-WEBGPU-TRANSFER-RECORDING-CLOSED";
        return false;
    }
    WebGpuOrdinaryTransferRecording recording(*impl_);
    if (const auto* id = recording.preflight(desc.sourceTexture, desc.destinationBuffer)) {
        diagnostic = id;
        return false;
    }
    const auto& source = recording.source();
    const auto& destination = recording.destination();
    if (const auto* id = WebGpuTransferCopyValidation::validate(source, destination, desc)) {
        diagnostic = id;
        return false;
    }
    diagnostic.clear();
    return recording.execute(
        desc,
        [&](const rhi::TextureToBufferCopyDesc& original) {
            return recordTextureToBufferCopy(
                *source.texture, *destination.buffer, original, diagnostic);
        },
        diagnostic);
#else
    (void)desc;
    diagnostic = "RHI-WEBGPU-TRANSFER-NATIVE-UNAVAILABLE";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordRegisteredTransfer(
    const rhi::TextureToTextureCopyDesc& desc, core::pmr::String& diagnostic)
{
    if (const auto* id = preparedRecordingDiagnostic()) {
        diagnostic = id;
        return false;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_) {
        diagnostic = "RHI-WEBGPU-TRANSFER-RECORDING-CLOSED";
        return false;
    }
    WebGpuOrdinaryTransferRecording recording(*impl_);
    if (const auto* id = recording.preflight(desc.sourceTexture, desc.destinationTexture)) {
        diagnostic = id;
        return false;
    }
    const auto& source = recording.source();
    const auto& destination = recording.destination();
    if (const auto* id = WebGpuTransferCopyValidation::validate(source, destination, desc)) {
        diagnostic = id;
        return false;
    }
    diagnostic.clear();
    return recording.execute(
        desc,
        [&](const rhi::TextureToTextureCopyDesc& original) {
            return recordTextureToTextureCopy(
                *source.texture, *destination.texture, original, diagnostic);
        },
        diagnostic);
#else
    (void)desc;
    diagnostic = "RHI-WEBGPU-TRANSFER-NATIVE-UNAVAILABLE";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordBufferCopy(const WebGpuNativeBufferPayload& source,
    const WebGpuNativeBufferPayload& destination,
    const rhi::BufferCopyDesc& desc,
    std::pmr::string& diagnostics)
{
    if (const auto* id = preparedRecordingDiagnostic()) {
        diagnostics = id;
        return false;
    }
    if (ordinaryTransferOnly()
        && (!impl_->ordinaryTransferDispatch || impl_->finished || impl_->submitted)) {
        diagnostics = "RHI-WEBGPU-TRANSFER-UNSUPPORTED-COMMAND";
        return false;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_) {
        diagnostics = "RHI-WEBGPU-TRANSFER-RECORDING-CLOSED";
        return false;
    }
    const char* copyDiagnostic = nullptr;
    if (!WebGpuNativeTransferCopyLowering::emit(
            impl_->encoder, source, destination, desc, copyDiagnostic)) {
        diagnostics = copyDiagnostic;
        return false;
    }
    recordCommand(impl_->stats);
    if (impl_->stats)
        ++impl_->stats->nativeBufferCopyRecordCount;
    return true;
#else
    (void)source;
    (void)destination;
    (void)desc;
    diagnostics = "RHI-WEBGPU-TRANSFER-NATIVE-UNAVAILABLE";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordBufferToTextureCopy(const WebGpuNativeBufferPayload& source,
    const WebGpuNativeTexturePayload& destination,
    const rhi::BufferToTextureCopyDesc& desc,
    std::pmr::string& diagnostics)
{
    if (const auto* id = destination.ordinaryContentAccessDiagnostic()) {
        diagnostics = id;
        return false;
    }
    if (const auto* id = preparedRecordingDiagnostic()) {
        diagnostics = id;
        return false;
    }
    if (ordinaryTransferOnly()
        && (!impl_->ordinaryTransferDispatch || impl_->finished || impl_->submitted)) {
        diagnostics = "RHI-WEBGPU-TRANSFER-UNSUPPORTED-COMMAND";
        return false;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_) {
        diagnostics = "RHI-WEBGPU-TRANSFER-RECORDING-CLOSED";
        return false;
    }
    const char* copyDiagnostic = nullptr;
    if (!WebGpuNativeTransferCopyLowering::emit(
            impl_->encoder, source, destination, desc, copyDiagnostic)) {
        diagnostics = copyDiagnostic;
        return false;
    }
    recordCommand(impl_->stats);
    if (impl_->stats)
        ++impl_->stats->nativeBufferToTextureCopyRecordCount;
    return true;
#else
    (void)source;
    (void)destination;
    (void)desc;
    diagnostics = "RHI-WEBGPU-TRANSFER-NATIVE-UNAVAILABLE";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordTextureUpload(const WebGpuNativeTexturePayload& destination,
    const rhi::UploadTextureDesc& desc,
    std::pmr::string& diagnostics)
{
    if (const auto* id = destination.ordinaryContentAccessDiagnostic()) {
        diagnostics = id;
        return false;
    }
    if (const auto* id = preparedRecordingDiagnostic()) {
        diagnostics = id;
        return false;
    }
    if (ordinaryTransferOnly()
        && (!impl_->ordinaryTransferDispatch || impl_->finished || impl_->submitted)) {
        diagnostics = "RHI-WEBGPU-TRANSFER-UNSUPPORTED-COMMAND";
        return false;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE) && defined(WGPU_TEXEL_COPY_BUFFER_INFO_INIT)
    if (!impl_ || !impl_->device || !impl_->encoder || !destination.texture || !desc.pixelData
        || desc.pixelDataSize == 0
        || !WebGpuNativeTransferCopyLowering::validCopyExtent(desc.extent) || desc.bytesPerRow == 0
        || desc.rowsPerImage == 0) {
        diagnostics = "WebGPU RHI texture upload requires native encoder, texture, pixels, extent "
                      "and row pitch";
        return false;
    }
    // queue.writeTexture consumes the supplied data directly. Its row pitch
    // follows the source layout; the 256-byte rule applies to buffer copies.
    const auto queueBytesPerRow = static_cast<std::uint64_t>(desc.bytesPerRow);
    if (const auto* id = WebGpuNativeTransferCopyLowering::textureDiagnostic(
            destination, desc.subresources, desc.extent)) {
        diagnostics = id;
        return false;
    }
    // rowsPerImage is expressed in texel rows for the RHI/WebGPU copy
    // contract. A BC texture source, however, contains one physical row per
    // block row. Do not multiply the required source size by the block
    // height: a 1024x1024 BC7 mip has 256 rows of blocks, not 1024 rows.
    const auto blockExtent = rhi::blockExtent(desc.format);
    const auto sourceRowsPerImage = rhi::isBlockCompressed(desc.format)
        ? rhi::blocksForExtent(desc.extent.height, blockExtent)
        : desc.extent.height;
    const auto sliceCount = WebGpuNativeTransferCopyLowering::nativeCopySliceCount(
        destination, desc.subresources, desc.extent);
    const auto rowsPerImage = static_cast<std::uint64_t>(desc.rowsPerImage);
    const auto sourceRows = static_cast<std::uint64_t>(sourceRowsPerImage);
    const auto sliceCount64 = static_cast<std::uint64_t>(sliceCount);
    if (sliceCount64 == 0u || rowsPerImage == 0u || sourceRows == 0u
        || rowsPerImage > UINT64_MAX / sliceCount64) {
        diagnostics = "WebGPU RHI texture upload byte range is invalid";
        return false;
    }
    const auto uploadRows = rowsPerImage * sliceCount64;
    if ((sliceCount64 > 1u && rowsPerImage > UINT64_MAX / (sliceCount64 - 1u))
        || (sliceCount64 > 1u && rowsPerImage * (sliceCount64 - 1u) > UINT64_MAX - sourceRows)) {
        diagnostics = "WebGPU RHI texture upload byte range is invalid";
        return false;
    }
    const auto sourceRowsBeforeLast = rowsPerImage * (sliceCount64 - 1u);
    const auto sourceLastRow = sourceRowsBeforeLast + sourceRows;
    if (static_cast<std::uint64_t>(desc.bytesPerRow) > UINT64_MAX / sourceLastRow
        || queueBytesPerRow > UINT64_MAX / uploadRows) {
        diagnostics = "WebGPU RHI texture upload byte range is invalid";
        return false;
    }
    const auto sourceLastByte = static_cast<std::uint64_t>(desc.bytesPerRow) * sourceLastRow;
    const auto uploadBytes = queueBytesPerRow * uploadRows;
    if (uploadBytes == 0 || uploadBytes > UINT32_MAX || sourceLastByte > desc.pixelDataSize) {
        diagnostics = "WebGPU RHI texture upload byte range is invalid";
        return false;
    }
    const auto effectiveMipLevel =
        desc.mipLevel != 0 ? desc.mipLevel : desc.subresources.baseMipLevel;
    const auto effectiveArrayLayer =
        desc.arrayLayer != 0 ? desc.arrayLayer : desc.subresources.baseArrayLayer;
    auto destinationInfo = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destinationInfo.texture = destination.texture;
    destinationInfo.mipLevel = effectiveMipLevel;
    destinationInfo.origin = {
        desc.destinationOffset.width,
        desc.destinationOffset.height,
        desc.destinationOffset.depth
            + (destination.dimension == WGPUTextureDimension_2D ? effectiveArrayLayer : 0u),
    };
    destinationInfo.aspect =
        WebGpuNativeTransferCopyLowering::nativeTextureAspect(desc.subresources.aspect);
    // WebGPU validates compressed copy extents in physical block units. The
    // RHI upload contract remains expressed in logical texels, so a tail mip
    // such as 2x2 or 1x1 must be rounded up to one complete 4x4 BC block at
    // the native boundary. The staging row/slice pitch already describes the
    // corresponding block footprint.
    WGPUExtent3D extent = WebGpuNativeTransferCopyLowering::nativeCopyExtent(
        destination, desc.subresources, desc.extent);
    if (rhi::isBlockCompressed(desc.format)) {
        const auto blockExtent = rhi::blockExtent(desc.format);
        if (blockExtent == 0u) {
            diagnostics = "WebGPU RHI compressed texture upload has no block extent";
            return false;
        }
        extent.width = ((extent.width + blockExtent - 1u) / blockExtent) * blockExtent;
        extent.height = ((extent.height + blockExtent - 1u) / blockExtent) * blockExtent;
    }
    // Queue writes copy the caller-owned bytes immediately into WebGPU's
    // queue. This avoids creating one mapped native staging buffer per
    // texture and keeps large startup batches out of the command encoder's
    // bounded transfer pool.
    auto dataLayout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    dataLayout.bytesPerRow = queueBytesPerRow;
    dataLayout.rowsPerImage = desc.rowsPerImage;
#if defined(__EMSCRIPTEN__)
    arpWebGpuQueueWriteTextureFixed(reinterpret_cast<std::uintptr_t>(impl_->queue),
        reinterpret_cast<std::uintptr_t>(destination.texture),
        reinterpret_cast<std::uintptr_t>(desc.pixelData),
        desc.pixelDataSize,
        destinationInfo.mipLevel,
        destinationInfo.origin.x,
        destinationInfo.origin.y,
        destinationInfo.origin.z,
        static_cast<std::uint32_t>(destinationInfo.aspect),
        static_cast<std::uint32_t>(dataLayout.bytesPerRow),
        static_cast<std::uint32_t>(dataLayout.rowsPerImage),
        extent.width,
        extent.height,
        extent.depthOrArrayLayers);
#else
    wgpuQueueWriteTexture(
        impl_->queue, &destinationInfo, desc.pixelData, desc.pixelDataSize, &dataLayout, &extent);
#endif
    recordCommand(impl_->stats);
    if (impl_->stats)
        ++impl_->stats->nativeBufferToTextureCopyRecordCount;
    return true;
#else
    (void)destination;
    (void)desc;
    diagnostics = "WebGPU RHI texture upload requires texel-copy API support";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordTextureToBufferCopy(const WebGpuNativeTexturePayload& source,
    const WebGpuNativeBufferPayload& destination,
    const rhi::TextureToBufferCopyDesc& desc,
    std::pmr::string& diagnostics)
{
    if (const auto* id = source.ordinaryContentAccessDiagnostic()) {
        diagnostics = id;
        return false;
    }
    if (const auto* id = preparedRecordingDiagnostic()) {
        diagnostics = id;
        return false;
    }
    if (ordinaryTransferOnly()
        && (!impl_->ordinaryTransferDispatch || impl_->finished || impl_->submitted)) {
        diagnostics = "RHI-WEBGPU-TRANSFER-UNSUPPORTED-COMMAND";
        return false;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_) {
        diagnostics = "RHI-WEBGPU-TRANSFER-RECORDING-CLOSED";
        return false;
    }
    const char* copyDiagnostic = nullptr;
    if (!WebGpuNativeTransferCopyLowering::emit(
            impl_->encoder, source, destination, desc, copyDiagnostic)) {
        diagnostics = copyDiagnostic;
        return false;
    }
    recordCommand(impl_->stats);
    if (impl_->stats)
        ++impl_->stats->nativeTextureToBufferCopyRecordCount;
    return true;
#else
    (void)source;
    (void)destination;
    (void)desc;
    diagnostics = "RHI-WEBGPU-TRANSFER-NATIVE-UNAVAILABLE";
    return false;
#endif
}

bool WebGpuNativeCommandPayload::recordTextureToTextureCopy(
    const WebGpuNativeTexturePayload& source,
    const WebGpuNativeTexturePayload& destination,
    const rhi::TextureToTextureCopyDesc& desc,
    std::pmr::string& diagnostics)
{
    if (const auto* id = source.ordinaryContentAccessDiagnostic()) {
        diagnostics = id;
        return false;
    }
    if (const auto* id = destination.ordinaryContentAccessDiagnostic()) {
        diagnostics = id;
        return false;
    }
    if (const auto* id = preparedRecordingDiagnostic()) {
        diagnostics = id;
        return false;
    }
    if (ordinaryTransferOnly()
        && (!impl_->ordinaryTransferDispatch || impl_->finished || impl_->submitted)) {
        diagnostics = "RHI-WEBGPU-TRANSFER-UNSUPPORTED-COMMAND";
        return false;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_) {
        diagnostics = "RHI-WEBGPU-TRANSFER-RECORDING-CLOSED";
        return false;
    }
    const char* copyDiagnostic = nullptr;
    if (!WebGpuNativeTransferCopyLowering::emit(
            impl_->encoder, source, destination, desc, copyDiagnostic)) {
        diagnostics = copyDiagnostic;
        return false;
    }
    recordCommand(impl_->stats);
    if (impl_->stats)
        ++impl_->stats->nativeTextureCopyRecordCount;
    return true;
#else
    (void)source;
    (void)destination;
    (void)desc;
    diagnostics = "RHI-WEBGPU-TRANSFER-NATIVE-UNAVAILABLE";
    return false;
#endif
}

const char* WebGpuTransferResourceObservation::capture(
    rhi::GpuResourceId resourceId, const WebGpuRhiRegisteredResource& resource) noexcept
{
    if (buffer || texture)
        return "RHI-WEBGPU-TRANSFER-OBSERVATION-ALREADY-CAPTURED";
    const bool isBuffer = resource.kind == rhi::DescriptorResourceKind::Buffer && resource.buffer
        && !resource.texture && !resource.textureView && !resource.sampler;
    const bool isTexture = resource.kind == rhi::DescriptorResourceKind::TextureView
        && resource.textureObject && resource.texture && !resource.buffer && !resource.textureView
        && !resource.sampler;
    if (!isBuffer && !isTexture)
        return "RHI-WEBGPU-TRANSFER-NATIVE-OWNER-MISSING";
    if ((isBuffer
            && (!resource.buffer->sharedOwnerMatches(resource.buffer)
                || resource.buffer->creationState() != resource.bufferDesc.initialState))
        || (isTexture
            && (!resource.texture->sharedOwnerMatches(resource.texture)
                || resource.texture->creationState() != resource.textureDesc.initialState)))
        return "RHI-WEBGPU-TRANSFER-ORIGINAL-STATE-OWNER";
    const auto& allocation = resource.allocationOwner;
    if (!allocation || !resource.allocationBytes)
        return "RHI-WEBGPU-TRANSFER-ALLOCATION-OWNER-MISSING";
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    const auto currentAllocation = isBuffer ? resource.buffer->allocationReleaseToken
                                            : resource.texture->allocationReleaseToken;
    const auto currentObject = isBuffer
        ? reinterpret_cast<std::uintptr_t>(resource.buffer->buffer)
        : reinterpret_cast<std::uintptr_t>(resource.texture->texture);
    const auto currentDevice = isBuffer
        ? reinterpret_cast<std::uintptr_t>(resource.buffer->ownerDevice)
        : reinterpret_cast<std::uintptr_t>(resource.texture->ownerDevice);
    if (!core::SharedOwnerProjection::sameOwner(allocation, currentAllocation)
        || resource.registeredNativeObject != currentObject
        || resource.registeredNativeDevice != currentDevice)
        return "RHI-WEBGPU-TRANSFER-REGISTERED-IDENTITY-CHANGED";
    if (isBuffer
        && (!resource.buffer->buffer || !resource.buffer->ownerDevice
            || resource.buffer->sizeBytes != resource.bufferDesc.sizeBytes))
        return "RHI-WEBGPU-TRANSFER-NATIVE-FACTS-MISMATCH";
    if (isTexture
        && (!resource.texture->texture || !resource.texture->ownerDevice
            || resource.texture->browserSurfaceTexture))
        return "RHI-WEBGPU-TRANSFER-NATIVE-FACTS-MISMATCH";
    if (isBuffer) {
        auto seal = resource.gpuWriteSeal.lock();
        if (!seal)
            return "RHI-WEBGPU-TRANSFER-WRITE-SEAL-MISSING";
        writeSeal = std::move(seal);
        buffer = resource.buffer;
        bufferDesc.sizeBytes = resource.bufferDesc.sizeBytes;
        bufferDesc.usage = resource.bufferDesc.usage;
        bufferDesc.memory = resource.bufferDesc.memory;
        bufferDesc.initialState = resource.bufferDesc.initialState;
        nativeBuffer_ = buffer->buffer;
        device_ = buffer->ownerDevice;
    } else {
        texture = resource.texture;
        textureDesc.extent = resource.textureDesc.extent;
        textureDesc.format = resource.textureDesc.format;
        textureDesc.usage = resource.textureDesc.usage;
        textureDesc.memory = resource.textureDesc.memory;
        textureDesc.initialState = resource.textureDesc.initialState;
        textureDesc.mipLevelCount = resource.textureDesc.mipLevelCount;
        textureDesc.arrayLayerCount = resource.textureDesc.arrayLayerCount;
        textureDesc.sampleCount = resource.textureDesc.sampleCount;
        textureDesc.cubeCompatible = resource.textureDesc.cubeCompatible;
        nativeTexture_ = texture->texture;
        device_ = texture->ownerDevice;
    }
    allocation_ = allocation;
    allocationBytes = resource.allocationBytes;
    id = resourceId;
    if (!matches())
        return "RHI-WEBGPU-TRANSFER-NATIVE-FACTS-MISMATCH";
    return nullptr;
#else
    (void)resourceId;
    return "RHI-WEBGPU-TRANSFER-NATIVE-UNAVAILABLE";
#endif
}

bool WebGpuTransferResourceObservation::sameOwnersAndLayout(
    const WebGpuTransferResourceObservation& other) const noexcept
{
    if (id != other.id || allocationBytes != other.allocationBytes
        || ownerCount() != other.ownerCount()
        || !core::SharedOwnerProjection::sameOwner(buffer, other.buffer)
        || !core::SharedOwnerProjection::sameOwner(texture, other.texture)
        || !core::SharedOwnerProjection::sameOwner(writeSeal, other.writeSeal)
        || !core::SharedOwnerProjection::sameOwner(allocation_, other.allocation_))
        return false;
    if (buffer)
        return bufferDesc.sizeBytes == other.bufferDesc.sizeBytes
            && bufferDesc.usage == other.bufferDesc.usage
            && bufferDesc.memory == other.bufferDesc.memory
            && bufferDesc.initialState == other.bufferDesc.initialState;
    return rhi::semantics::TextureUploadDestinationLayout::capture(textureDesc)
        .matches(other.textureDesc);
}

bool WebGpuTransferResourceObservation::matches() const noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (buffer)
        return !texture && writeSeal && buffer->sharedOwnerMatches(buffer)
            && buffer->creationState() == bufferDesc.initialState && buffer->buffer == nativeBuffer_
            && buffer->ownerDevice == device_ && buffer->sizeBytes == bufferDesc.sizeBytes
            && core::SharedOwnerProjection::sameOwner(allocation_, buffer->allocationReleaseToken);
    return texture && texture->sharedOwnerMatches(texture)
        && texture->creationState() == textureDesc.initialState && !texture->browserSurfaceTexture
        && texture->texture == nativeTexture_ && texture->ownerDevice == device_
        && texture->rhiFormat == textureDesc.format
        && texture->extent.width == textureDesc.extent.width
        && texture->extent.height == textureDesc.extent.height
        && texture->extent.depth == textureDesc.extent.depth
        && texture->mipLevelCount == textureDesc.mipLevelCount
        && texture->arrayLayerCount == textureDesc.arrayLayerCount
        && texture->sampleCount == textureDesc.sampleCount
        && texture->dimension
        == (textureDesc.extent.depth > 1u ? WGPUTextureDimension_3D : WGPUTextureDimension_2D)
        && core::SharedOwnerProjection::sameOwner(allocation_, texture->allocationReleaseToken);
#else
    return false;
#endif
}

const char* WebGpuOrdinaryResourceObservation::capture(rhi::GpuResourceId resourceId,
    const WebGpuRhiRegisteredResource& resource,
    const WebGpuRhiRegisteredResource* parent) noexcept
{
    if (id_.isValid())
        return "RHI-WEBGPU-ORDINARY-RESOURCE-ALREADY-CAPTURED";
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (resource.kind == rhi::DescriptorResourceKind::Buffer) {
        if (!resource.buffer || !resource.buffer->sharedOwnerMatches(resource.buffer))
            return "RHI-WEBGPU-ORDINARY-RESOURCE-ORIGINAL-OWNER";
        if (const auto* id = backing_.capture(resourceId, resource))
            return id;
        device_ = backing_.buffer->ownerDevice;
    } else if (resource.kind == rhi::DescriptorResourceKind::TextureView
        && !resource.textureObject) {
        if (!parent || !parent->textureObject || !parent->texture || !resource.textureView
            || !parent->texture->sharedOwnerMatches(parent->texture)
            || !resource.textureView->sharedOwnerMatches(resource.textureView)
            || !core::SharedOwnerProjection::sameOwner(resource.parentTextureOwner, parent->texture)
            || !core::SharedOwnerProjection::sameOwner(
                resource.textureView->parentTexture, parent->texture)
            || !core::SharedOwnerProjection::sameOwner(
                resource.allocationOwner, parent->allocationOwner)
            || resource.allocationBytes != parent->allocationBytes)
            return "RHI-WEBGPU-ORDINARY-RESOURCE-ORIGINAL-PARENT";
        if (!resource.textureView->view
            || !resource.textureView->matchesDescription(resource.textureViewDesc)
            || resource.registeredNativeObject
                != reinterpret_cast<std::uintptr_t>(resource.textureView->view)
            || resource.registeredNativeDevice
                != reinterpret_cast<std::uintptr_t>(parent->texture->ownerDevice))
            return "RHI-WEBGPU-ORDINARY-RESOURCE-REGISTERED-IDENTITY-CHANGED";
        if (const auto* id = backing_.capture(resource.parentTexture, *parent))
            return id;
        viewDesc_.format = resource.textureViewDesc.format;
        viewDesc_.usage = resource.textureViewDesc.usage;
        viewDesc_.dimension = resource.textureViewDesc.dimension;
        viewDesc_.baseMipLevel = resource.textureViewDesc.baseMipLevel;
        viewDesc_.mipLevelCount = resource.textureViewDesc.mipLevelCount;
        viewDesc_.baseArrayLayer = resource.textureViewDesc.baseArrayLayer;
        viewDesc_.arrayLayerCount = resource.textureViewDesc.arrayLayerCount;
        viewDesc_.aspect = resource.textureViewDesc.aspect;
        view_ = resource.textureView;
        nativeView_ = view_->view;
        device_ = backing_.texture->ownerDevice;
    } else if (resource.kind == rhi::DescriptorResourceKind::Sampler && resource.sampler) {
        if (!resource.sampler->sharedOwnerMatches(resource.sampler))
            return "RHI-WEBGPU-ORDINARY-RESOURCE-ORIGINAL-OWNER";
        if (!resource.sampler->sampler || !resource.sampler->ownerDevice
            || resource.registeredNativeObject
                != reinterpret_cast<std::uintptr_t>(resource.sampler->sampler)
            || resource.registeredNativeDevice
                != reinterpret_cast<std::uintptr_t>(resource.sampler->ownerDevice))
            return "RHI-WEBGPU-ORDINARY-RESOURCE-REGISTERED-IDENTITY-CHANGED";
        sampler_ = resource.sampler;
        nativeSampler_ = sampler_->sampler;
        device_ = sampler_->ownerDevice;
    } else
        return "RHI-WEBGPU-ORDINARY-RESOURCE-KIND-UNSUPPORTED";
    if (!device_)
        return "RHI-WEBGPU-ORDINARY-RESOURCE-DEVICE-MISSING";
    id_ = resourceId;
    return nullptr;
#else
    (void)resourceId;
    (void)resource;
    (void)parent;
    return "RHI-WEBGPU-ORDINARY-RESOURCE-NATIVE-UNAVAILABLE";
#endif
}

bool WebGpuOrdinaryResourceObservation::matches() const noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!id_.isValid() || !device_)
        return false;
    if (sampler_)
        return sampler_->sampler == nativeSampler_ && sampler_->ownerDevice == device_;
    if (!backing_.matches())
        return false;
    if (!view_)
        return backing_.buffer && backing_.buffer->ownerDevice == device_;
    return view_->view == nativeView_ && backing_.texture->ownerDevice == device_
        && core::SharedOwnerProjection::sameOwner(view_->parentTexture, backing_.texture)
        && view_->matchesDescription(viewDesc_);
#else
    return false;
#endif
}

} // namespace arp::rhi_webgpu
