#include "rhi/backends/metal/metal_staging_upload_pool.h"

#include "core/cpu_allocator.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/frontend/memory_staging_upload_pool.h"

#include <Metal/Metal.h>
#include <TargetConditionals.h>
#include <limits>
#include <utility>

namespace arp::rhi_metal {

namespace {

id<MTLDevice> defaultMetalDevice()
{
    return reinterpret_cast<id<MTLDevice>>(metalRhiNativeDeviceHandle());
}

class MetalRhiStagingBufferPayload final {
public:
    MetalRhiStagingBufferPayload(id<MTLBuffer> buffer, std::shared_ptr<void> nativeDevice)
        : buffer_(buffer), nativeDevice_(std::move(nativeDevice))
    {}

    ~MetalRhiStagingBufferPayload()
    {
        if (buffer_ != nil) {
            [buffer_ release];
        }
    }

    void* mappedData() const { return buffer_ != nil ? [buffer_ contents] : nullptr; }

    id<MTLBuffer> buffer() const { return buffer_; }

    const std::shared_ptr<void>& nativeDevice() const { return nativeDevice_; }

private:
    id<MTLBuffer> buffer_ = nil;
    std::shared_ptr<void> nativeDevice_;
};

} // namespace

MetalRhiNativeStagingBufferInfo inspectMetalRhiNativeStagingBuffer(
    const std::shared_ptr<void>& opaquePayload)
{
    MetalRhiNativeStagingBufferInfo result;
    if (!opaquePayload) {
        return result;
    }
    const auto payload = std::static_pointer_cast<MetalRhiStagingBufferPayload>(opaquePayload);
    id<MTLBuffer> buffer = payload ? payload->buffer() : nil;
    if (buffer == nil || buffer.contents == nullptr) {
        return result;
    }
    // The page owns this immutable native buffer. An alias preserves both its
    // exact pointer and original ownership without a new control block.
    result.nativeBuffer = std::shared_ptr<void>(opaquePayload, reinterpret_cast<void*>(buffer));
    result.sizeBytes = buffer.length;
    result.nativeDevice = payload->nativeDevice();
    result.nativeRuntimeBacked = true;
#if TARGET_OS_IPHONE
    result.managedStorage = false;
#else
    result.managedStorage = buffer.storageMode == MTLStorageModeManaged;
#endif
    return result;
}

bool prepareMetalRhiNativeStagingBufferForGpu(const std::shared_ptr<void>& opaquePayload,
    std::uint64_t offsetBytes,
    std::uint64_t sizeBytes,
    std::pmr::string& diagnostics)
{
    if (!opaquePayload || sizeBytes == 0u) {
        diagnostics = "RHI-METAL-STAGING-BUFFER-FLUSH-ARGUMENTS-INVALID";
        return false;
    }
    const auto payload = std::static_pointer_cast<MetalRhiStagingBufferPayload>(opaquePayload);
    id<MTLBuffer> buffer = payload ? payload->buffer() : nil;
    if (buffer == nil || offsetBytes > buffer.length || sizeBytes > buffer.length - offsetBytes
        || offsetBytes > static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max())
        || sizeBytes > static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max())) {
        diagnostics = "RHI-METAL-STAGING-BUFFER-FLUSH-RANGE-INVALID";
        return false;
    }
#if !TARGET_OS_IPHONE
    if (buffer.storageMode == MTLStorageModeManaged) {
        [buffer didModifyRange:NSMakeRange(static_cast<NSUInteger>(offsetBytes),
                                   static_cast<NSUInteger>(sizeBytes))];
    }
#endif
    return true;
}

bool MetalRhiStagingUploadPool::createPage(std::uint32_t pageIndex, std::uint64_t capacityBytes)
{
    if (!rhi::stagingCreatePageInvariant(pageIndex, pages_.size(), capacityBytes)) {
        return false;
    }

    id<MTLDevice> device = defaultMetalDevice();
    if (device == nil) {
        lastDiagnostics_ = "Metal RHI upload staging requires a Metal device";
        return false;
    }

#if TARGET_OS_IPHONE
    const MTLResourceOptions options = MTLResourceStorageModeShared;
#else
    const MTLResourceOptions options =
        desc_.preferSharedMemory ? MTLResourceStorageModeShared : MTLResourceStorageModeManaged;
#endif
    if (capacityBytes > static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max())) {
        lastDiagnostics_ = "Metal RHI upload staging capacity exceeds native size range";
        return false;
    }
    id<MTLBuffer> buffer = [device newBufferWithLength:static_cast<NSUInteger>(capacityBytes)
                                               options:options];
    if (buffer == nil) {
        lastDiagnostics_ = "Metal RHI upload staging failed to allocate MTLBuffer";
        return false;
    }

    auto payload =
        arp::core::makeShared<MetalRhiStagingBufferPayload>(buffer, metalRhiNativeDeviceIdentity());
    if (payload->mappedData() == nullptr) {
        lastDiagnostics_ = "Metal RHI upload staging buffer is not CPU-mappable";
        return false;
    }

    Page page;
    page.mappedData = payload->mappedData();
    page.payload = payload;
    pages_.push_back(std::move(page));
    return true;
}

} // namespace arp::rhi_metal

#include "rhi/backends/metal/metal_staging_upload_pool_common.inl"
