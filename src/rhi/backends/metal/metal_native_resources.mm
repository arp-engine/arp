#include "rhi/backends/metal/metal_native_resources.h"

#include "core/memory/cpu_allocator.h"
#include "core/ownership/shared_owner_projection.h"
#include "rhi/backends/metal/buffer_validation.h"
#include "rhi/backends/metal/metal_native_format.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"
#include "rhi/frontend/bindless_produced_image.h"
#include "rhi/frontend/buffer_placement.h"
#include "rhi/frontend/resource_native_allocation_observation.h"
#include "rhi/frontend/resource_texture_extent.h"
#include "rhi/frontend/transfer_upload_destination_validation.h"

#include <Metal/Metal.h>
#include <TargetConditionals.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>

namespace arp::rhi_metal {

MTLPixelFormat nativePixelFormat(rhi::TextureFormat format) noexcept
{
    switch (format) {
    case rhi::TextureFormat::Bgra8Unorm:
        return MTLPixelFormatBGRA8Unorm;
    case rhi::TextureFormat::Rgba8Unorm:
        return MTLPixelFormatRGBA8Unorm;
    case rhi::TextureFormat::Rgba8UnormSrgb:
        return MTLPixelFormatRGBA8Unorm_sRGB;
    case rhi::TextureFormat::R8Unorm:
        return MTLPixelFormatR8Unorm;
    case rhi::TextureFormat::Rgba16Float:
        return MTLPixelFormatRGBA16Float;
    case rhi::TextureFormat::Rg16Float:
        return MTLPixelFormatRG16Float;
    case rhi::TextureFormat::R32Float:
        return MTLPixelFormatR32Float;
    case rhi::TextureFormat::D32Float:
        return MTLPixelFormatDepth32Float;
    case rhi::TextureFormat::Depth24Stencil8:
        return MTLPixelFormatDepth24Unorm_Stencil8;
    case rhi::TextureFormat::Bc1Unorm:
        return MTLPixelFormatBC1_RGBA;
    case rhi::TextureFormat::Bc3Unorm:
        return MTLPixelFormatBC3_RGBA;
    case rhi::TextureFormat::Bc7Unorm:
        return MTLPixelFormatBC7_RGBAUnorm;
    case rhi::TextureFormat::Bc7UnormSrgb:
        return MTLPixelFormatBC7_RGBAUnorm_sRGB;
    case rhi::TextureFormat::Unknown:
        break;
    }
    return MTLPixelFormatInvalid;
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

std::optional<MTLResourceOptions> nativeResourceOptions(MetalRhiNativeStorageMode storageMode)
{
    switch (storageMode) {
    case MetalRhiNativeStorageMode::Private:
        return MTLResourceStorageModePrivate;
    case MetalRhiNativeStorageMode::Shared:
        return MTLResourceStorageModeShared;
    case MetalRhiNativeStorageMode::Managed:
#if TARGET_OS_IPHONE
        return MTLResourceStorageModeShared;
#else
        return MTLResourceStorageModeManaged;
#endif
    case MetalRhiNativeStorageMode::Memoryless:
    case MetalRhiNativeStorageMode::None:
        break;
    }
    return std::nullopt;
}

MTLStorageMode nativeStorageMode(MetalRhiNativeStorageMode storageMode)
{
    switch (storageMode) {
    case MetalRhiNativeStorageMode::Private:
        return MTLStorageModePrivate;
    case MetalRhiNativeStorageMode::Shared:
        return MTLStorageModeShared;
    case MetalRhiNativeStorageMode::Managed:
#if TARGET_OS_IPHONE
        return MTLStorageModeShared;
#else
        return MTLStorageModeManaged;
#endif
    case MetalRhiNativeStorageMode::Memoryless:
        return MTLStorageModeMemoryless;
    case MetalRhiNativeStorageMode::None:
        break;
    }
    return MTLStorageModePrivate;
}

MTLTextureUsage nativeTextureUsage(rhi::TextureUsage usage)
{
    MTLTextureUsage nativeUsage = MTLTextureUsageUnknown;
    if (static_cast<std::uint32_t>(usage & rhi::TextureUsage::Sampled) != 0u) {
        nativeUsage |= MTLTextureUsageShaderRead;
    }
    if (static_cast<std::uint32_t>(usage & rhi::TextureUsage::Storage) != 0u) {
        // MetalStorageTextureUsage/1: Slang RWTexture parameters carry a
        // native read_write signature, independently of L2 Sampled usage.
        nativeUsage |= MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    }
    if (static_cast<std::uint32_t>(usage & rhi::TextureUsage::RenderAttachment) != 0u) {
        nativeUsage |= MTLTextureUsageRenderTarget;
    }
    // Metal's MTLBlitCommandEncoder does not require a texture-usage flag for
    // copy operations (unlike Vulkan's VK_IMAGE_USAGE_TRANSFER_SRC_BIT or
    // WebGPU's WGPUTextureUsage_CopySrc). The CopySrc/CopyDst bits are handled
    // here explicitly so every backend's usage-mapping function covers the
    // full TextureUsage enum symmetrically (RHI contract); the copy path is
    // authorized by the copyTextureToTexture command-object layer routing
    // through recordBlitTextureCopy, not by a native usage flag. Note that
    // MTLStorageModeMemoryless textures still cannot be blit sources - that
    // memoryless limitation is enforced at texture-creation time, not here.
    (void)static_cast<std::uint32_t>(usage & rhi::TextureUsage::CopySrc);
    (void)static_cast<std::uint32_t>(usage & rhi::TextureUsage::CopyDst);
    return nativeUsage;
}

MTLTextureDescriptor* nativeTextureDescriptor(const rhi::TextureDesc& desc)
{
    if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0
        || desc.mipLevelCount == 0 || desc.arrayLayerCount == 0 || desc.sampleCount == 0) {
        return nil;
    }

    const bool isCube = desc.cubeCompatible;
    const bool is3D = desc.extent.depth > 1;
    const bool isArray = desc.arrayLayerCount > 1;
    if ((isCube
            && (desc.extent.width != desc.extent.height || desc.extent.depth != 1
                || desc.sampleCount != 1 || desc.arrayLayerCount % 6 != 0))
        || (is3D && (isCube || isArray))
        || (desc.sampleCount > 1 && (is3D || desc.mipLevelCount != 1))) {
        return nil;
    }

    auto* textureDesc = [[MTLTextureDescriptor alloc] init];
    textureDesc.pixelFormat = nativePixelFormat(desc.format);
    if (textureDesc.pixelFormat == MTLPixelFormatInvalid) {
        [textureDesc release];
        return nil;
    }
    textureDesc.width = desc.extent.width;
    textureDesc.height = desc.extent.height;
    textureDesc.depth = is3D ? desc.extent.depth : 1;
    textureDesc.mipmapLevelCount = desc.mipLevelCount;
    textureDesc.sampleCount = desc.sampleCount;
    // Preserve the RHI's physical array-layer indexing in the resource. A
    // cube view is created from this 2D-array storage below; using Metal's
    // CubeArray resource here would make MTLTexture::arrayLength count cubes
    // instead of the six physical layers required by the RHI contract.
    if (is3D) {
        textureDesc.textureType = MTLTextureType3D;
        textureDesc.arrayLength = 1;
    } else if (desc.sampleCount > 1) {
        textureDesc.textureType =
            isArray ? MTLTextureType2DMultisampleArray : MTLTextureType2DMultisample;
        textureDesc.arrayLength = isArray ? desc.arrayLayerCount : 1;
    } else {
        textureDesc.textureType = isArray ? MTLTextureType2DArray : MTLTextureType2D;
        textureDesc.arrayLength = isArray ? desc.arrayLayerCount : 1;
    }
    textureDesc.usage = nativeTextureUsage(desc.usage);
    return textureDesc;
}

MTLSamplerMinMagFilter nativeFilter(rhi::SamplerFilter filter)
{
    return filter == rhi::SamplerFilter::Nearest ? MTLSamplerMinMagFilterNearest
                                                 : MTLSamplerMinMagFilterLinear;
}

MTLSamplerAddressMode nativeAddressMode(rhi::SamplerAddressMode mode)
{
    return mode == rhi::SamplerAddressMode::Repeat ? MTLSamplerAddressModeRepeat
                                                   : MTLSamplerAddressModeClampToEdge;
}

MTLCompareFunction nativeCompareFunction(rhi::CompareOp compare)
{
    switch (compare) {
    case rhi::CompareOp::Never:
        return MTLCompareFunctionNever;
    case rhi::CompareOp::Less:
        return MTLCompareFunctionLess;
    case rhi::CompareOp::LessEqual:
        return MTLCompareFunctionLessEqual;
    case rhi::CompareOp::Equal:
        return MTLCompareFunctionEqual;
    case rhi::CompareOp::Greater:
        return MTLCompareFunctionGreater;
    case rhi::CompareOp::GreaterEqual:
        return MTLCompareFunctionGreaterEqual;
    case rhi::CompareOp::NotEqual:
        return MTLCompareFunctionNotEqual;
    case rhi::CompareOp::Always:
        return MTLCompareFunctionAlways;
    }
    // Defensive default for an out-of-range enum value. Unified across backends
    // to Never (most conservative: reject all comparisons) to avoid divergent
    // fallback semantics — previously Vulkan/WebGPU returned Always here while
    // Metal returned Never, an inconsistent silent-fallback that violates the
    // "no guesswork fallback" constraint.
    return MTLCompareFunctionNever;
}

template <typename Payload>
std::shared_ptr<Payload> makePayload(MetalRhiNativeStorageMode storageMode,
    std::shared_ptr<void> nativeObject,
    const char* diagnostics)
{
    auto payload = arp::core::makeShared<Payload>();
    payload->resource.storageMode = storageMode;
    payload->resource.nativeDevice = metalRhiNativeDeviceIdentity();
    payload->resource.nativeObject = std::move(nativeObject);
    payload->resource.nativeRuntimeBacked = static_cast<bool>(payload->resource.nativeObject);
    payload->resource.nativeRuntimeDiagnosticOnly = !payload->resource.nativeRuntimeBacked;
    if (!payload->resource.nativeRuntimeBacked) {
        payload->resource.diagnostics = diagnostics;
    }
    return payload;
}

} // namespace

const char* validateMetalRhiNativeSampledTextureInventory(
    const MetalRhiNativeTexturePayload& parent,
    const MetalRhiNativeTextureViewPayload& view,
    const rhi::BindlessTextureInventoryResource& values) noexcept
{
    if (const auto* diagnostic = values.diagnostic())
        return diagnostic;
    const auto& p = parent.resource;
    const auto& v = view.resource;
    if (!p.nativeRuntimeBacked || p.nativeRuntimeDiagnosticOnly || !v.nativeRuntimeBacked
        || v.nativeRuntimeDiagnosticOnly || p.nativeHandleExposedToUpperLayer
        || v.nativeHandleExposedToUpperLayer || !p.nativeObject || !v.nativeObject
        || !p.nativeDevice || !v.nativeDevice)
        return "RHI-METAL-TEXTURE-INVENTORY-NATIVE-MISSING";
    const auto texture = reinterpret_cast<id<MTLTexture>>(p.nativeObject.get());
    const auto textureView = reinterpret_cast<id<MTLTexture>>(v.nativeObject.get());
    const auto device = reinterpret_cast<id<MTLDevice>>(p.nativeDevice.get());
    if (![texture conformsToProtocol:@protocol(MTLTexture)]
        || ![textureView conformsToProtocol:@protocol(MTLTexture)]
        || ![device conformsToProtocol:@protocol(MTLDevice)])
        return "RHI-METAL-TEXTURE-INVENTORY-NATIVE-KIND";
    if (texture.device != device || textureView.device != device || v.nativeDevice.get() != device
        || texture.textureType != MTLTextureType2D || textureView.textureType != MTLTextureType2D
        || texture.pixelFormat != MTLPixelFormatRGBA8Unorm
        || textureView.pixelFormat != MTLPixelFormatRGBA8Unorm || texture.sampleCount != 1u
        || textureView.sampleCount != 1u || texture.arrayLength != 1u
        || textureView.arrayLength != 1u || !(texture.usage & MTLTextureUsageShaderRead)
        || !(textureView.usage & MTLTextureUsageShaderRead)
        || texture.storageMode != nativeStorageMode(p.storageMode)
        || textureView.storageMode != nativeStorageMode(v.storageMode)
        || textureView.parentTexture != texture
        || textureView.parentRelativeLevel != values.range.baseMipLevel
        || textureView.parentRelativeSlice != values.range.baseArrayLayer)
        return "RHI-METAL-TEXTURE-INVENTORY-NATIVE-DOMAIN";
    if (texture.width != values.parentExtent.width || texture.height != values.parentExtent.height
        || texture.depth != values.parentExtent.depth
        || texture.mipmapLevelCount != values.parentMipLevelCount
        || textureView.mipmapLevelCount != values.range.mipLevelCount
        || texture.width != parent.extent.width || texture.height != parent.extent.height
        || texture.depth != parent.extent.depth || texture.mipmapLevelCount != parent.mipLevelCount
        || parent.arrayLayerCount != texture.arrayLength
        || parent.sampleCount != texture.sampleCount || parent.format != values.parentFormat
        || view.format != values.format || !textureView.width || !textureView.height
        || textureView.width != view.extent.width || textureView.height != view.extent.height
        || textureView.depth != view.extent.depth
        || textureView.mipmapLevelCount != view.mipLevelCount
        || textureView.arrayLength != view.arrayLayerCount
        || textureView.sampleCount != view.sampleCount)
        return "RHI-METAL-TEXTURE-INVENTORY-NATIVE-METADATA";
    return nullptr;
}

const char* validateMetalRhiTrackedNativeBufferObject(
    const MetalRhiNativeResourcePayload& resource, std::uint64_t expectedBytes) noexcept
{
    const auto buffer = reinterpret_cast<id<MTLBuffer>>(resource.nativeObject.get());
    const auto device = reinterpret_cast<id<MTLDevice>>(resource.nativeDevice.get());
    if (!buffer || !device || !resource.nativeRuntimeBacked || resource.nativeRuntimeDiagnosticOnly
        || !resource.hazardsExplicitlyTracked)
        return "RHI-METAL-TRACKED-BUFFER-OBSERVATION-MISSING";
    if (![buffer conformsToProtocol:@protocol(MTLBuffer)]
        || ![device conformsToProtocol:@protocol(MTLDevice)])
        return "RHI-METAL-TRACKED-BUFFER-OBSERVATION-KIND";
    if (resource.storageMode != MetalRhiNativeStorageMode::Private
        && resource.storageMode != MetalRhiNativeStorageMode::Shared
        && resource.storageMode != MetalRhiNativeStorageMode::Managed)
        return "RHI-METAL-TRACKED-BUFFER-STORAGE-POLICY";
    if (!expectedBytes || buffer.device != device || buffer.length != expectedBytes
        || buffer.storageMode != nativeStorageMode(resource.storageMode)
        || buffer.hazardTrackingMode != MTLHazardTrackingModeTracked)
        return "RHI-METAL-TRACKED-BUFFER-OBSERVATION-MISMATCH";
    return nullptr;
}

static const char* validateMetalRhiNativeTextureViewIdentity(
    const MetalRhiNativeTexturePayload& parent,
    const MetalRhiNativeTextureViewPayload& payload,
    const rhi::TextureDesc& td,
    const rhi::TextureViewDesc& vd,
    bool cell,
    bool depth = false,
    bool fullMipChain = false) noexcept
{
    if (const auto* id = validateMetalRhiTrackedNativeTextureObject(parent.resource, td))
        return id;
    const auto& resource = payload.resource;
    if (!resource.nativeRuntimeBacked || resource.nativeRuntimeDiagnosticOnly
        || !resource.nativeObject || !parent.originalContentExclusive()
        || !payload.originalContentExclusive()
        || !core::SharedOwnerProjection::sameOwner(
            parent.resource.nativeDevice, resource.nativeDevice)
        || !core::SharedOwnerProjection::sameOwner(
            parent.resource.allocationReleaseToken, resource.allocationReleaseToken))
        return "RHI-METAL-COMPLETED-TEXTURE-NATIVE-OWNER";
    const auto view = reinterpret_cast<id<MTLTexture>>(resource.nativeObject.get());
    const auto texture = reinterpret_cast<id<MTLTexture>>(parent.resource.nativeObject.get());
    if (![view conformsToProtocol:@protocol(MTLTexture)])
        return "RHI-METAL-COMPLETED-TEXTURE-NATIVE-KIND";
    const auto type = vd.dimension == rhi::TextureViewDimension::TextureCube ? MTLTextureTypeCube
        : vd.dimension == rhi::TextureViewDimension::Texture2DArray          ? MTLTextureType2DArray
                                                                             : MTLTextureType2D;
    const auto layers = type == MTLTextureTypeCube ? vd.arrayLayerCount / 6u : vd.arrayLayerCount;
    auto width = td.extent.width, height = td.extent.height;
    if (cell || depth) {
        if (fullMipChain) {
            if (!cell || depth)
                return "RHI-BINDLESS-PRODUCED-IMAGE-DESCRIPTOR-PROFILE";
            if (const auto* id =
                    rhi::OriginalBindlessProducedImageReadContract::inspectView(td, vd))
                return id;
        }
        if (td.format != (depth ? rhi::TextureFormat::D32Float : rhi::TextureFormat::Rgba16Float)
            || vd.format != td.format || vd.dimension != rhi::TextureViewDimension::Texture2D
            || (!fullMipChain && vd.mipLevelCount != 1u) || vd.baseMipLevel >= td.mipLevelCount
            || vd.baseArrayLayer || vd.arrayLayerCount != 1u
            || vd.aspect != (depth ? rhi::TextureAspect::Depth : rhi::TextureAspect::Color)
            || (vd.usage != rhi::TextureUsage::Sampled
                && (depth || vd.usage != rhi::TextureUsage::Storage)))
            return "RHI-METAL-TEXTURE-CELL-NATIVE-SHAPE";
        rhi::Extent3D extent;
        if (const auto* id = rhi::semantics::TextureSubresourceExtent::resolve(
                td.extent, td.mipLevelCount, vd.baseMipLevel, extent))
            return id;
        width = extent.width;
        height = extent.height;
    }
    const auto requiredUsage = cell && vd.usage == rhi::TextureUsage::Storage
        ? MTLTextureUsageShaderWrite
        : MTLTextureUsageShaderRead;
    if (view.device != texture.device || view.parentTexture != texture
        || view.parentRelativeLevel != vd.baseMipLevel
        || view.parentRelativeSlice != vd.baseArrayLayer || view.textureType != type
        || view.pixelFormat != nativePixelFormat(vd.format) || view.width != width
        || view.height != height || view.depth != 1u || view.mipmapLevelCount != vd.mipLevelCount
        || view.arrayLength != layers || view.sampleCount != 1u
        || view.storageMode != nativeStorageMode(resource.storageMode)
        || !(view.usage & requiredUsage) || payload.format != vd.format
        || payload.extent.width != view.width || payload.extent.height != view.height
        || payload.extent.depth != view.depth || payload.mipLevelCount != view.mipmapLevelCount
        || payload.arrayLayerCount != vd.arrayLayerCount || payload.sampleCount != view.sampleCount)
        return "RHI-METAL-COMPLETED-TEXTURE-NATIVE-VIEW";
    return nullptr;
}

const char* validateMetalRhiNativeCompletedTextureView(const MetalRhiNativeTexturePayload& parent,
    const MetalRhiNativeTextureViewPayload& view,
    const rhi::TextureDesc& td,
    const rhi::TextureViewDesc& vd) noexcept
{
    return validateMetalRhiNativeTextureViewIdentity(parent, view, td, vd, false);
}

const char* validateMetalRhiNativeCompletedDepthView(const MetalRhiNativeTexturePayload& parent,
    const MetalRhiNativeTextureViewPayload& view,
    const rhi::TextureDesc& td,
    const rhi::TextureViewDesc& vd) noexcept
{
    return validateMetalRhiNativeTextureViewIdentity(parent, view, td, vd, false, true);
}

const char* validateMetalRhiNativeTextureCellView(const MetalRhiNativeTexturePayload& parent,
    const MetalRhiNativeTextureViewPayload& view,
    const rhi::TextureDesc& td,
    const rhi::TextureViewDesc& vd,
    bool fullMipChain) noexcept
{
    return validateMetalRhiNativeTextureViewIdentity(
        parent, view, td, vd, true, false, fullMipChain);
}

const char* validateMetalRhiTrackedNativeTextureObject(
    const MetalRhiNativeResourcePayload& resource, const rhi::TextureDesc& expected) noexcept
{
    if (!@available(macOS 10.15, iOS 13.0, *))
        return "RHI-METAL-TRACKED-TEXTURE-NATIVE-API-UNAVAILABLE";
    const auto texture = reinterpret_cast<id<MTLTexture>>(resource.nativeObject.get());
    const auto device = reinterpret_cast<id<MTLDevice>>(resource.nativeDevice.get());
    if (!texture || !device || !resource.nativeRuntimeBacked || resource.nativeRuntimeDiagnosticOnly
        || !resource.hazardsExplicitlyTracked)
        return "RHI-METAL-TRACKED-TEXTURE-OBSERVATION-MISSING";
    if (![texture conformsToProtocol:@protocol(MTLTexture)]
        || ![device conformsToProtocol:@protocol(MTLDevice)])
        return "RHI-METAL-TRACKED-TEXTURE-OBSERVATION-KIND";
    // Cube-compatible resources preserve RHI physical layer indexing as 2D
    // array storage; only their views use Metal's cube type.
    const bool is3D = expected.extent.depth > 1u;
    if (!expected.extent.width || !expected.extent.height || !expected.extent.depth
        || !expected.arrayLayerCount || expected.sampleCount != 1u || !expected.mipLevelCount
        || (is3D && (expected.arrayLayerCount != 1u || expected.cubeCompatible))
        || (expected.cubeCompatible
            && (expected.extent.width != expected.extent.height || expected.arrayLayerCount % 6u)))
        return "RHI-METAL-TRACKED-TEXTURE-OBSERVATION-DOMAIN";
    const auto type = is3D              ? MTLTextureType3D
        : expected.arrayLayerCount > 1u ? MTLTextureType2DArray
                                        : MTLTextureType2D;
    if (texture.device != device || texture.width != expected.extent.width
        || texture.height != expected.extent.height || texture.depth != expected.extent.depth
        || texture.pixelFormat != nativePixelFormat(expected.format) || texture.textureType != type
        || texture.sampleCount != expected.sampleCount
        || texture.mipmapLevelCount != expected.mipLevelCount
        || texture.arrayLength != expected.arrayLayerCount
        || texture.storageMode != nativeStorageMode(resource.storageMode)
        || texture.usage != nativeTextureUsage(expected.usage)
        || texture.hazardTrackingMode != MTLHazardTrackingModeTracked)
        return "RHI-METAL-TRACKED-TEXTURE-OBSERVATION-MISMATCH";
    return nullptr;
}

std::uint64_t queryMetalRhiOriginalContentAllocation(
    const rhi::TextureDesc& desc, const char*& diagnostic) noexcept
{
    diagnostic = "RHI-METAL-CONTENT-CREATION-PROFILE";
    if (desc.memory != rhi::MemoryUsage::GpuOnly || !desc.extent.depth || !desc.arrayLayerCount
        || desc.sampleCount != 1u || desc.initialState != rhi::ResourceState::Undefined)
        return 0u;
    const bool attachment =
        (desc.usage & rhi::TextureUsage::RenderAttachment) != rhi::TextureUsage::None;
    if (attachment) {
        // Preserve the existing ContentCreation4 native attachment scope.
        if (desc.extent.depth != 1u || desc.arrayLayerCount != 1u || desc.cubeCompatible)
            return 0u;
    } else {
        std::uint64_t texelBytes = 0u;
        if ((diagnostic = rhi::semantics::TextureUploadDestinationValidation::validate(
                 desc, texelBytes, rhi::ResourceState::Undefined)))
            return 0u;
    }
    rhi::TexturePlacementRequirements requirements;
    if (!queryMetalRhiNativeTexturePlacementRequirements(desc, requirements)) {
        diagnostic = "RHI-METAL-CONTENT-CREATION-NATIVE-QUERY";
        return 0u;
    }
    diagnostic = nullptr;
    return requirements.sizeBytes;
}

const char* validateMetalRhiOriginalContentAllocation(const MetalRhiNativeResourcePayload& resource,
    const rhi::TextureDesc& desc,
    std::uint64_t reservationBytes) noexcept
{
    if (const auto* id = validateMetalRhiTrackedNativeTextureObject(resource, desc))
        return id;
    if (!@available(macOS 10.15, iOS 13.0, *))
        return "RHI-METAL-CONTENT-CREATION-NATIVE-UNAVAILABLE";
    const auto texture = reinterpret_cast<id<MTLTexture>>(resource.nativeObject.get());
    // Heap descriptor requirements reserve a conservative bound before native
    // creation. Query the independent allocation as well; no texel estimate or
    // heap-size assumption may authorize an allocation beyond that reservation.
    if (!texture.allocatedSize || texture.allocatedSize > reservationBytes || texture.heap)
        return "RHI-METAL-CONTENT-CREATION-NATIVE-ALLOCATION-BOUND";
    return nullptr;
}

std::uint64_t observeMetalRhiOriginalContentAllocation(
    const MetalRhiNativeResourcePayload& resource,
    const rhi::TextureDesc& desc,
    std::uint64_t reservationBytes,
    const char*& diagnostic) noexcept
{
    diagnostic = validateMetalRhiOriginalContentAllocation(resource, desc, reservationBytes);
    if (diagnostic)
        return 0u;
    const auto texture = reinterpret_cast<id<MTLTexture>>(resource.nativeObject.get());
    const auto actual = static_cast<std::uint64_t>(texture.allocatedSize);
    diagnostic = rhi::OriginalNativeAllocationObservationContract::validateObserved(
        reservationBytes, actual);
    return diagnostic ? 0u : actual;
}

MetalRhiNativeStorageMode metalRhiNativeStorageMode(MetalRhiMemoryPolicy policy)
{
    switch (policy) {
    case MetalRhiMemoryPolicy::PrivateStorage:
        return MetalRhiNativeStorageMode::Private;
    case MetalRhiMemoryPolicy::SharedUpload:
        return MetalRhiNativeStorageMode::Shared;
    case MetalRhiMemoryPolicy::ManagedReadback:
#if TARGET_OS_IPHONE
        // Managed storage is unavailable on iOS-family devices. Shared
        // storage preserves the CPU-visible readback contract there.
        return MetalRhiNativeStorageMode::Shared;
#else
        return MetalRhiNativeStorageMode::Managed;
#endif
    case MetalRhiMemoryPolicy::MemorylessTransient:
        return MetalRhiNativeStorageMode::Memoryless;
    }
    return MetalRhiNativeStorageMode::Private;
}

std::shared_ptr<MetalRhiNativeBufferPayload> createMetalRhiNativeBufferPayload(
    const rhi::BufferDesc& desc, MetalRhiMemoryPolicy policy)
{
    if (const auto* id =
            rhi::HostVisibleDeviceLocalBufferContract::validate(desc, rhi::Backend::Metal, false)) {
        rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error, id);
        return {};
    }
    switch (policy) {
    case MetalRhiMemoryPolicy::PrivateStorage:
    case MetalRhiMemoryPolicy::SharedUpload:
    case MetalRhiMemoryPolicy::ManagedReadback:
    case MetalRhiMemoryPolicy::MemorylessTransient:
        break;
    default:
        rhi::RhiErrorHandler::get().report(
            rhi::RhiSeverity::Error, "RHI-METAL-TRACKED-BUFFER-STORAGE-POLICY");
        return {};
    }
    const auto storageMode = metalRhiNativeStorageMode(policy);
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    auto object = createMetalRhiTrackedNativeBufferObject(
        metalRhiNativeDeviceIdentity(), desc.sizeBytes, storageMode, diagnostic);
    if (!object) {
        rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error, diagnostic.c_str());
        return {};
    }
    const auto buffer = reinterpret_cast<id<MTLBuffer>>(object.get());
    buffer.label = [NSString stringWithUTF8String:desc.label.c_str()];
    auto payload = makePayload<MetalRhiNativeBufferPayload>(storageMode, std::move(object), "");
    payload->resource.hazardsExplicitlyTracked = true;
    return payload;
}

std::uint64_t observeMetalRhiNativeBufferAllocation(const MetalRhiNativeResourcePayload& payload,
    std::uint64_t bytes,
    const char*& diagnostic) noexcept
{
    diagnostic = validateMetalRhiTrackedNativeBufferObject(payload, bytes);
    if (diagnostic)
        return 0u;
    const auto buffer = reinterpret_cast<id<MTLBuffer>>(payload.nativeObject.get());
    diagnostic = "RHI-METAL-BUFFER-ALLOCATION-UNPROVEN";
    if (!buffer || ![buffer respondsToSelector:@selector(allocatedSize)] || buffer.heap)
        return 0u;
    const auto actual = static_cast<std::uint64_t>(buffer.allocatedSize);
    if (!actual || actual < bytes)
        return 0u;
    diagnostic = nullptr;
    return actual;
}

std::shared_ptr<void> createMetalRhiTrackedNativeBufferObject(
    const std::shared_ptr<void>& nativeDevice,
    std::uint64_t sizeBytes,
    MetalRhiNativeStorageMode storage,
    core::pmr::String& diagnostics)
{
    diagnostics.clear();
    const auto device = reinterpret_cast<id<MTLDevice>>(nativeDevice.get());
    if (!device || ![device respondsToSelector:@selector(maxBufferLength)]) {
        diagnostics = "RHI-METAL-TRACKED-BUFFER-DEVICE-UNAVAILABLE";
        return {};
    }
    const auto maximum = std::min(static_cast<std::uint64_t>(device.maxBufferLength),
        static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max()));
    if (const auto* diagnostic =
            MetalRhiTrackedBufferContract::validateCreation(sizeBytes, storage, maximum)) {
        diagnostics = diagnostic;
        return {};
    }
    const auto options = nativeResourceOptions(storage);
    if (!options) {
        diagnostics = "RHI-METAL-TRACKED-BUFFER-STORAGE-POLICY";
        return {};
    }
    auto buffer = [device newBufferWithLength:static_cast<NSUInteger>(sizeBytes)
                                      options:*options | MTLResourceHazardTrackingModeTracked];
    auto owner = retainNativeObject(buffer);
    if (!owner) {
        diagnostics = "RHI-METAL-TRACKED-BUFFER-CREATE-FAILED";
        return {};
    }
    if (buffer.storageMode != nativeStorageMode(storage) || buffer.length != sizeBytes) {
        diagnostics = "RHI-METAL-TRACKED-BUFFER-STORAGE-QUERY-MISMATCH";
        return {};
    }
    const bool available = [buffer respondsToSelector:@selector(hazardTrackingMode)];
    const auto mode =
        available ? static_cast<std::uint32_t>(buffer.hazardTrackingMode) : UINT32_MAX;
    if (const auto* diagnostic = MetalRhiTrackedBufferContract::validateQuery(available, mode)) {
        diagnostics = diagnostic;
        return {};
    }
    return owner;
}

bool writeMetalRhiNativeBuffer(const std::shared_ptr<MetalRhiNativeBufferPayload>& payload,
    const void* data,
    std::uint64_t sizeBytes,
    std::uint64_t offsetBytes,
    std::uint64_t bufferSizeBytes,
    std::pmr::string& diagnostics)
{
    if (!payload || !data || sizeBytes == 0 || offsetBytes > bufferSizeBytes
        || sizeBytes > bufferSizeBytes - offsetBytes || !payload->resource.nativeRuntimeBacked
        || (payload->resource.storageMode != MetalRhiNativeStorageMode::Shared
            && payload->resource.storageMode != MetalRhiNativeStorageMode::Managed)) {
        diagnostics = "RHI-METAL-BUFFER-WRITE-NATIVE-PAYLOAD-INVALID";
        return false;
    }
    auto buffer = reinterpret_cast<id<MTLBuffer>>(payload->resource.nativeObject.get());
    if (buffer == nil || buffer.contents == nullptr || offsetBytes > buffer.length
        || sizeBytes > buffer.length - offsetBytes
        || offsetBytes > static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max())
        || sizeBytes > static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max())) {
        diagnostics = "RHI-METAL-BUFFER-WRITE-NATIVE-BUFFER-INVALID";
        return false;
    }
    std::memcpy(static_cast<std::uint8_t*>(buffer.contents) + offsetBytes, data, sizeBytes);
    if (payload->resource.storageMode == MetalRhiNativeStorageMode::Managed) {
#if !TARGET_OS_IPHONE
        [buffer didModifyRange:NSMakeRange(static_cast<NSUInteger>(offsetBytes),
                                   static_cast<NSUInteger>(sizeBytes))];
#endif
    }
    return true;
}

bool readMetalRhiNativeBuffer(const std::shared_ptr<MetalRhiNativeBufferPayload>& payload,
    void* data,
    std::uint64_t sizeBytes,
    std::uint64_t offsetBytes,
    std::uint64_t bufferSizeBytes,
    std::pmr::string& diagnostics)
{
    if (!payload || !data || sizeBytes == 0u || offsetBytes > bufferSizeBytes
        || sizeBytes > bufferSizeBytes - offsetBytes || !payload->resource.nativeRuntimeBacked
        || (payload->resource.storageMode != MetalRhiNativeStorageMode::Shared
            && payload->resource.storageMode != MetalRhiNativeStorageMode::Managed)) {
        diagnostics = "RHI-METAL-BUFFER-READ-NATIVE-PAYLOAD-INVALID";
        return false;
    }
    auto buffer = reinterpret_cast<id<MTLBuffer>>(payload->resource.nativeObject.get());
    if (buffer == nil || buffer.contents == nullptr || offsetBytes > buffer.length
        || sizeBytes > buffer.length - offsetBytes
        || offsetBytes > static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max())
        || sizeBytes > static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max())) {
        diagnostics = "RHI-METAL-BUFFER-READ-NATIVE-BUFFER-INVALID";
        return false;
    }
    std::memcpy(data,
        static_cast<const std::uint8_t*>(buffer.contents) + offsetBytes,
        static_cast<std::size_t>(sizeBytes));
    return true;
}

std::shared_ptr<MetalRhiNativeTexturePayload> createMetalRhiNativeTexturePayload(
    const rhi::TextureDesc& desc, MetalRhiMemoryPolicy policy)
{
    const auto storageMode = metalRhiNativeStorageMode(policy);
    id<MTLDevice> device = defaultMetalDevice();
    id<MTLTexture> texture = nil;
    const auto pixelFormat = nativePixelFormat(desc.format);
    if (!@available(macOS 10.15, iOS 13.0, *))
        return {};
    if (device != nil && pixelFormat != MTLPixelFormatInvalid) {
        MTLTextureDescriptor* textureDesc = nativeTextureDescriptor(desc);
        if (textureDesc == nil) {
            return makePayload<MetalRhiNativeTexturePayload>(storageMode,
                {},
                "Metal RHI texture descriptor has no executable Metal dimension lowering");
        }
        textureDesc.hazardTrackingMode = MTLHazardTrackingModeTracked;
        textureDesc.storageMode = nativeStorageMode(storageMode);
        texture = [device newTextureWithDescriptor:textureDesc];
        texture.label = [NSString stringWithUTF8String:desc.label.c_str()];
        [textureDesc release];
    }
    auto payload = makePayload<MetalRhiNativeTexturePayload>(
        storageMode, retainNativeObject(texture), "Metal RHI failed to create a native texture");
    if (@available(macOS 10.15, iOS 13.0, *))
        payload->resource.hazardsExplicitlyTracked =
            texture && texture.hazardTrackingMode == MTLHazardTrackingModeTracked;
    payload->format = desc.format;
    payload->extent = desc.extent;
    payload->mipLevelCount = desc.mipLevelCount;
    payload->arrayLayerCount = desc.arrayLayerCount;
    payload->sampleCount = desc.sampleCount;
    payload->cubeCompatible = desc.cubeCompatible;
    return payload;
}

std::shared_ptr<MetalRhiNativeTexturePayload> createMetalRhiOriginalContentTexturePayload(
    const rhi::TextureDesc& desc)
{
    auto payload = createMetalRhiNativeTexturePayload(desc, MetalRhiMemoryPolicy::PrivateStorage);
    if (payload)
        payload->originalContentExclusive_ = true;
    return payload;
}

std::shared_ptr<MetalRhiNativeTextureViewPayload> createMetalRhiNativeTextureViewPayload(
    const std::shared_ptr<MetalRhiNativeTexturePayload>& texturePayload,
    const rhi::TextureViewDesc& desc)
{
    const auto storageMode =
        texturePayload ? texturePayload->resource.storageMode : MetalRhiNativeStorageMode::None;
    id<MTLTexture> texture = texturePayload
        ? reinterpret_cast<id<MTLTexture>>(texturePayload->resource.nativeObject.get())
        : nil;
    id<MTLTexture> view = nil;
    const auto pixelFormat = nativePixelFormat(desc.format);
    const auto* parent = texturePayload.get();
    const bool parentIs3D = parent && parent->extent.depth > 1u;
    const bool dimensionMatches = parent
        && (parentIs3D ? (desc.dimension == rhi::TextureViewDimension::Texture3D
                             && desc.baseArrayLayer == 0u && desc.arrayLayerCount == 1u)
                       : (desc.dimension != rhi::TextureViewDimension::Texture3D
                             && (desc.dimension != rhi::TextureViewDimension::TextureCube
                                 || (parent->cubeCompatible && desc.arrayLayerCount == 6u
                                     && (desc.baseArrayLayer % 6u) == 0u))
                             && (desc.arrayLayerCount == 1u
                                 || desc.dimension == rhi::TextureViewDimension::Texture2DArray
                                 || desc.dimension == rhi::TextureViewDimension::TextureCube)));
    if (texture != nil && pixelFormat != MTLPixelFormatInvalid && parent != nullptr
        && desc.format == parent->format && dimensionMatches && desc.mipLevelCount > 0
        && desc.arrayLayerCount > 0 && desc.baseMipLevel < parent->mipLevelCount
        && desc.mipLevelCount <= parent->mipLevelCount - desc.baseMipLevel
        && desc.baseArrayLayer < parent->arrayLayerCount
        && desc.arrayLayerCount <= parent->arrayLayerCount - desc.baseArrayLayer) {
        MTLTextureType textureType = MTLTextureType2D;
        switch (desc.dimension) {
        case rhi::TextureViewDimension::Texture2D:
            textureType = MTLTextureType2D;
            break;
        case rhi::TextureViewDimension::Texture2DArray:
            textureType = MTLTextureType2DArray;
            break;
        case rhi::TextureViewDimension::Texture3D:
            textureType = MTLTextureType3D;
            break;
        case rhi::TextureViewDimension::TextureCube:
            textureType = MTLTextureTypeCube;
            break;
        }
        view = [texture
            newTextureViewWithPixelFormat:pixelFormat
                              textureType:textureType
                                   levels:NSMakeRange(desc.baseMipLevel, desc.mipLevelCount)
                                   slices:NSMakeRange(desc.baseArrayLayer, desc.arrayLayerCount)];
        view.label = [NSString stringWithUTF8String:desc.label.c_str()];
    }
    auto payload = makePayload<MetalRhiNativeTextureViewPayload>(
        storageMode, retainNativeObject(view), "Metal RHI failed to create a native texture view");
    if (parent != nullptr) {
        payload->originalContentExclusive_ = parent->originalContentExclusive();
        payload->immutableRevision_ = parent->immutableRevision();
        payload->resource.allocationReleaseToken = parent->resource.allocationReleaseToken;
        payload->format = parent->format;
        const auto mipExtent = [](std::uint32_t value, std::uint32_t level) {
            const auto shifted = value >> level;
            return shifted == 0u ? 1u : shifted;
        };
        payload->extent = {
            mipExtent(parent->extent.width, desc.baseMipLevel),
            mipExtent(parent->extent.height, desc.baseMipLevel),
            parentIs3D ? mipExtent(parent->extent.depth, desc.baseMipLevel) : 1u,
        };
        payload->mipLevelCount = desc.mipLevelCount;
        payload->arrayLayerCount = desc.arrayLayerCount;
        payload->sampleCount = parent->sampleCount;
    }
    return payload;
}

std::shared_ptr<MetalRhiNativeSamplerPayload> createMetalRhiNativeSamplerPayload(
    const rhi::SamplerDesc& desc, bool argumentBufferCompatible)
{
    if (argumentBufferCompatible) {
        if (@available(macOS 10.13, iOS 11.0, *)) {
        } else {
            return makePayload<MetalRhiNativeSamplerPayload>(MetalRhiNativeStorageMode::None,
                {},
                "RHI-METAL-SAMPLER-ARGUMENT-BUFFER-API-UNAVAILABLE");
        }
    }
    constexpr float kMetalMaximumSamplerAnisotropy = 16.0f;
    if (!std::isfinite(desc.maxAnisotropy)) {
        return makePayload<MetalRhiNativeSamplerPayload>(
            MetalRhiNativeStorageMode::None, {}, "RHI-METAL-SAMPLER-ANISOTROPY-MUST-BE-FINITE");
    }
    id<MTLDevice> device = defaultMetalDevice();
    id<MTLSamplerState> sampler = nil;
    if (device != nil) {
        MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
        if (@available(macOS 10.13, iOS 11.0, *))
            samplerDesc.supportArgumentBuffers = argumentBufferCompatible;
        samplerDesc.minFilter = nativeFilter(desc.minFilter);
        samplerDesc.magFilter = nativeFilter(desc.magFilter);
        samplerDesc.sAddressMode = nativeAddressMode(desc.addressU);
        samplerDesc.tAddressMode = nativeAddressMode(desc.addressV);
        samplerDesc.rAddressMode = nativeAddressMode(desc.addressW);
        // Mipmap filter. Metal's MTLSamplerDescriptor defaults to
        // MTLSamplerMipFilterNotMipmapped, so we must explicitly set Linear
        // for trilinear filtering (the RHI default). None maps to the Metal
        // NotMipmapped default.
        switch (desc.mipFilter) {
        case rhi::SamplerMipFilter::Nearest:
            samplerDesc.mipFilter = MTLSamplerMipFilterNearest;
            break;
        case rhi::SamplerMipFilter::None:
            samplerDesc.mipFilter = MTLSamplerMipFilterNotMipmapped;
            break;
        default:
            samplerDesc.mipFilter = MTLSamplerMipFilterLinear;
            break;
        }
        // Anisotropic filtering. Only applied when greater than 1; Metal
        // requires mip filtering to be active for anisotropy to take effect.
        if (desc.maxAnisotropy > 1.0f) {
            const auto clampedAnisotropy =
                std::min(desc.maxAnisotropy, kMetalMaximumSamplerAnisotropy);
            samplerDesc.maxAnisotropy = static_cast<NSUInteger>(std::ceil(clampedAnisotropy));
        }
        // Hardware depth comparison for shadow map sampling (shadow PCF).
        // When compareOp is not Never, Metal performs the compare function
        // between the sampled depth and the shader reference value.
        if (desc.compareOp != rhi::CompareOp::Never) {
            samplerDesc.compareFunction = nativeCompareFunction(desc.compareOp);
        }
        samplerDesc.label = [NSString stringWithUTF8String:desc.label.c_str()];
        sampler = [device newSamplerStateWithDescriptor:samplerDesc];
        [samplerDesc release];
    }
    auto payload = makePayload<MetalRhiNativeSamplerPayload>(MetalRhiNativeStorageMode::None,
        retainNativeObject(sampler),
        "Metal RHI failed to create a native sampler");
    payload->argumentBufferCompatible =
        argumentBufferCompatible && payload->resource.nativeRuntimeBacked;
    payload->comparisonEnabled = desc.compareOp != rhi::CompareOp::Never;
    return payload;
}

std::shared_ptr<MetalRhiNativeHeapPayload> createMetalRhiNativeHeap(
    std::uint64_t sizeBytes, std::uint64_t alignmentBytes, std::pmr::string* diagnostics)
{
    if (diagnostics != nullptr) {
        diagnostics->clear();
    }
    if (!@available(macOS 10.15, iOS 13.0, *)) {
        if (diagnostics != nullptr) {
            *diagnostics = "RHI-METAL-MEMORY-HEAP-API-UNAVAILABLE";
        }
        return {};
    }
    id<MTLDevice> device = defaultMetalDevice();
    if (device == nil || sizeBytes == 0 || alignmentBytes == 0
        || sizeBytes > static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max())
        || alignmentBytes > static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max())) {
        if (diagnostics != nullptr) {
            *diagnostics = "RHI-METAL-MEMORY-HEAP-INVALID-REQUEST";
        }
        return {};
    }

    MTLHeapDescriptor* heapDesc = [[MTLHeapDescriptor alloc] init];
    heapDesc.size = sizeBytes;
    heapDesc.storageMode = MTLStorageModePrivate;
    heapDesc.type = MTLHeapTypePlacement;
    // MTLHeap's default is Untracked. The L2 texture barrier lowering uses
    // encoder boundaries, so all accesses on this heap must be tracked,
    // including different logical textures sharing physical storage.
    heapDesc.hazardTrackingMode = MTLHazardTrackingModeTracked;

    id<MTLHeap> heap = [device newHeapWithDescriptor:heapDesc];
    [heapDesc release];

    if (heap == nil) {
        if (diagnostics != nullptr) {
            *diagnostics = "RHI-METAL-MEMORY-HEAP-NATIVE-CREATE-FAILED";
        }
        return {};
    }
    if (heap.type != MTLHeapTypePlacement || heap.size < sizeBytes
        || heap.hazardTrackingMode != MTLHazardTrackingModeTracked) {
        [heap release];
        if (diagnostics != nullptr) {
            *diagnostics = "RHI-METAL-MEMORY-HEAP-NATIVE-CONTRACT-MISMATCH";
        }
        return {};
    }

    auto payload = makePayload<MetalRhiNativeHeapPayload>(MetalRhiNativeStorageMode::Private,
        retainNativeObject(heap),
        "Metal RHI failed to create a native heap");
    payload->sizeBytes = sizeBytes;
    payload->allocationSizeBytes = heap.size;
    payload->hazardsTracked = heap.hazardTrackingMode == MTLHazardTrackingModeTracked;
    payload->alignmentBytes = alignmentBytes;
    payload->resource.nativeRuntimeBacked = true;
    payload->resource.nativeRuntimeDiagnosticOnly = false;
    return payload;
}

std::shared_ptr<MetalRhiNativeTexturePayload> createPlacedMetalRhiNativeTexturePayload(
    const MetalRhiNativeHeapPayload& heapPayload,
    const rhi::TextureDesc& desc,
    std::uint64_t offsetBytes)
{
    if (!@available(macOS 10.15, iOS 13.0, *)) {
        return {};
    }
    id<MTLDevice> device = defaultMetalDevice();
    if (device == nil || !heapPayload.resource.nativeRuntimeBacked) {
        return {};
    }

    // Get the MTLHeap from the payload.
    auto* nativeObj = heapPayload.resource.nativeObject.get();
    if (nativeObj == nullptr) {
        return {};
    }
    id<MTLHeap> heap = reinterpret_cast<id<MTLHeap>>(const_cast<void*>(nativeObj));
    if (heap == nil || heap.device != device || !heapPayload.resource.nativeDevice
        || heapPayload.resource.nativeDevice.get() != metalRhiNativeDeviceIdentity().get()
        || heap.size != heapPayload.allocationSizeBytes
        || heapPayload.allocationSizeBytes < heapPayload.sizeBytes || !heapPayload.hazardsTracked
        || heap.hazardTrackingMode != MTLHazardTrackingModeTracked
        || heapPayload.alignmentBytes == 0u
        || (heapPayload.alignmentBytes & (heapPayload.alignmentBytes - 1u)) != 0u) {
        return {};
    }

    rhi::TexturePlacementRequirements requirements;
    if (!queryMetalRhiNativeTexturePlacementRequirements(desc, requirements)
        || requirements.sizeBytes == 0 || requirements.alignmentBytes == 0
        || (requirements.alignmentBytes & (requirements.alignmentBytes - 1u)) != 0u
        || offsetBytes % requirements.alignmentBytes != 0 || offsetBytes > heapPayload.sizeBytes
        || offsetBytes > static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max())
        || requirements.sizeBytes
            > static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max())
        || requirements.sizeBytes > heapPayload.sizeBytes - offsetBytes) {
        return {};
    }

    const auto pixelFormat = nativePixelFormat(desc.format);
    if (pixelFormat == MTLPixelFormatInvalid) {
        return {};
    }

    MTLTextureDescriptor* textureDesc = nativeTextureDescriptor(desc);
    if (textureDesc == nil) {
        return {};
    }
    textureDesc.storageMode = MTLStorageModePrivate;
    textureDesc.resourceOptions =
        MTLResourceStorageModePrivate | MTLResourceHazardTrackingModeTracked;

    id<MTLTexture> texture = [heap newTextureWithDescriptor:textureDesc offset:offsetBytes];
    [textureDesc release];
    if (texture == nil) {
        return {};
    }
    const auto expectedArrayLength = desc.extent.depth > 1u ? 1u : desc.arrayLayerCount;
    if (texture.pixelFormat != pixelFormat || texture.width != desc.extent.width
        || texture.height != desc.extent.height || texture.mipmapLevelCount != desc.mipLevelCount
        || texture.sampleCount != desc.sampleCount || texture.arrayLength != expectedArrayLength
        || texture.hazardTrackingMode != MTLHazardTrackingModeTracked) {
        [texture release];
        return {};
    }
    texture.label = [NSString stringWithUTF8String:desc.label.c_str()];

    auto payload = makePayload<MetalRhiNativeTexturePayload>(MetalRhiNativeStorageMode::Private,
        retainNativeObject(texture),
        "Metal RHI failed to create a placed native texture");
    payload->format = desc.format;
    payload->aliasHeap = heapPayload.resource.nativeObject;
    payload->resource.allocationReleaseToken = heapPayload.resource.allocationReleaseToken;
    payload->extent = desc.extent;
    payload->mipLevelCount = desc.mipLevelCount;
    payload->arrayLayerCount = desc.arrayLayerCount;
    payload->sampleCount = desc.sampleCount;
    payload->cubeCompatible = desc.cubeCompatible;
    return payload;
}

bool metalRhiNativePlacementAliasingAvailable()
{
    if (@available(macOS 10.15, iOS 13.0, *)) {
        return defaultMetalDevice() != nil;
    }
    return false;
}

bool queryMetalRhiNativeTexturePlacementRequirements(
    const rhi::TextureDesc& desc, rhi::TexturePlacementRequirements& requirements)
{
    id<MTLDevice> device = defaultMetalDevice();
    if (device == nil || !metalRhiNativePlacementAliasingAvailable()) {
        return false;
    }
    MTLTextureDescriptor* textureDesc = nativeTextureDescriptor(desc);
    if (textureDesc == nil) {
        return false;
    }
    textureDesc.storageMode = MTLStorageModePrivate;
    textureDesc.resourceOptions =
        MTLResourceStorageModePrivate | MTLResourceHazardTrackingModeTracked;
    if (@available(macOS 10.13, iOS 10.0, *)) {
        const MTLSizeAndAlign sizeAndAlign =
            [device heapTextureSizeAndAlignWithDescriptor:textureDesc];
        [textureDesc release];
        if (sizeAndAlign.size == 0 || sizeAndAlign.align == 0) {
            return false;
        }
        requirements.sizeBytes = sizeAndAlign.size;
        requirements.alignmentBytes = sizeAndAlign.align;
        return true;
    }
    [textureDesc release];
    return false;
}

} // namespace arp::rhi_metal
