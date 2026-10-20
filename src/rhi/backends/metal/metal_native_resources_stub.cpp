#include "core/cpu_allocator.h"
#include "rhi/backends/metal/metal_native_resources.h"

namespace arp::rhi_metal {
std::uint64_t observeMetalRhiOriginalContentAllocation(const MetalRhiNativeResourcePayload&,
    const rhi::TextureDesc&,
    std::uint64_t,
    const char*& diagnostic) noexcept
{
    diagnostic = "RHI-METAL-CONTENT-CREATION-NATIVE-UNAVAILABLE";
    return 0u;
}

const char* validateMetalRhiNativeTextureCellView(const MetalRhiNativeTexturePayload&,
    const MetalRhiNativeTextureViewPayload&,
    const rhi::TextureDesc&,
    const rhi::TextureViewDesc&,
    bool) noexcept
{
    return "RHI-METAL-TEXTURE-CELL-NATIVE-UNAVAILABLE";
}

const char* validateMetalRhiNativeCompletedTextureView(const MetalRhiNativeTexturePayload&,
    const MetalRhiNativeTextureViewPayload&,
    const rhi::TextureDesc&,
    const rhi::TextureViewDesc&) noexcept
{
    return "RHI-METAL-COMPLETED-TEXTURE-NATIVE-UNAVAILABLE";
}

const char* validateMetalRhiNativeCompletedDepthView(const MetalRhiNativeTexturePayload&,
    const MetalRhiNativeTextureViewPayload&,
    const rhi::TextureDesc&,
    const rhi::TextureViewDesc&) noexcept
{
    return "RHI-METAL-COMPLETED-DEPTH-NATIVE-UNAVAILABLE";
}

std::shared_ptr<MetalRhiNativeTexturePayload> createMetalRhiOriginalContentTexturePayload(
    const rhi::TextureDesc&)
{
    return {};
}

std::uint64_t queryMetalRhiOriginalContentAllocation(
    const rhi::TextureDesc&, const char*& diagnostic) noexcept
{
    diagnostic = "RHI-METAL-CONTENT-CREATION-NATIVE-UNAVAILABLE";
    return 0u;
}

const char* validateMetalRhiOriginalContentAllocation(
    const MetalRhiNativeResourcePayload&, const rhi::TextureDesc&, std::uint64_t) noexcept
{
    return "RHI-METAL-CONTENT-CREATION-NATIVE-UNAVAILABLE";
}

namespace {

template <typename Payload>
std::shared_ptr<Payload> makeDiagnosticPayload(MetalRhiNativeStorageMode storageMode)
{
    auto payload = arp::core::makeShared<Payload>();
    payload->resource.storageMode = storageMode;
    payload->resource.nativeRuntimeDiagnosticOnly = true;
    payload->resource.diagnostics =
        "Metal RHI native resources are diagnostic-only outside Apple Objective-C++ builds";
    return payload;
}

} // namespace

const char* validateMetalRhiNativeSampledTextureInventory(const MetalRhiNativeTexturePayload&,
    const MetalRhiNativeTextureViewPayload&,
    const rhi::BindlessTextureInventoryResource&) noexcept
{
    return "RHI-METAL-TEXTURE-INVENTORY-NATIVE-UNAVAILABLE";
}

const char* validateMetalRhiTrackedNativeTextureObject(
    const MetalRhiNativeResourcePayload&, const rhi::TextureDesc&) noexcept
{
    return "RHI-METAL-TRACKED-TEXTURE-NATIVE-UNAVAILABLE";
}

const char* validateMetalRhiTrackedNativeBufferObject(
    const MetalRhiNativeResourcePayload&, std::uint64_t) noexcept
{
    return "RHI-METAL-TRACKED-BUFFER-NATIVE-UNAVAILABLE";
}

std::uint64_t observeMetalRhiNativeBufferAllocation(
    const MetalRhiNativeResourcePayload&, std::uint64_t, const char*& diagnostic) noexcept
{
    diagnostic = "RHI-METAL-BUFFER-ALLOCATION-NATIVE-UNAVAILABLE";
    return 0u;
}

std::shared_ptr<void> createMetalRhiTrackedNativeBufferObject(const std::shared_ptr<void>&,
    std::uint64_t,
    MetalRhiNativeStorageMode,
    core::pmr::String& diagnostics)
{
    diagnostics = "RHI-METAL-TRACKED-BUFFER-NATIVE-UNAVAILABLE";
    return {};
}

MetalRhiNativeStorageMode metalRhiNativeStorageMode(MetalRhiMemoryPolicy policy)
{
    switch (policy) {
    case MetalRhiMemoryPolicy::PrivateStorage:
        return MetalRhiNativeStorageMode::Private;
    case MetalRhiMemoryPolicy::SharedUpload:
        return MetalRhiNativeStorageMode::Shared;
    case MetalRhiMemoryPolicy::ManagedReadback:
        return MetalRhiNativeStorageMode::Managed;
    case MetalRhiMemoryPolicy::MemorylessTransient:
        return MetalRhiNativeStorageMode::Memoryless;
    }
    return MetalRhiNativeStorageMode::Private;
}

std::shared_ptr<MetalRhiNativeBufferPayload> createMetalRhiNativeBufferPayload(
    const rhi::BufferDesc&, MetalRhiMemoryPolicy policy)
{
    return makeDiagnosticPayload<MetalRhiNativeBufferPayload>(metalRhiNativeStorageMode(policy));
}

std::shared_ptr<MetalRhiNativeTexturePayload> createMetalRhiNativeTexturePayload(
    const rhi::TextureDesc&, MetalRhiMemoryPolicy policy)
{
    return makeDiagnosticPayload<MetalRhiNativeTexturePayload>(metalRhiNativeStorageMode(policy));
}

std::shared_ptr<MetalRhiNativeTextureViewPayload> createMetalRhiNativeTextureViewPayload(
    const std::shared_ptr<MetalRhiNativeTexturePayload>& texturePayload,
    const rhi::TextureViewDesc& desc)
{
    const auto storageMode =
        texturePayload ? texturePayload->resource.storageMode : MetalRhiNativeStorageMode::None;
    auto payload = makeDiagnosticPayload<MetalRhiNativeTextureViewPayload>(storageMode);
    if (texturePayload) {
        payload->immutableRevision_ = texturePayload->immutableRevision();
        payload->format = texturePayload->format;
        const auto mipExtent = [](std::uint32_t value, std::uint32_t level) {
            const auto shifted = value >> level;
            return shifted == 0u ? 1u : shifted;
        };
        payload->extent = {
            mipExtent(texturePayload->extent.width, desc.baseMipLevel),
            mipExtent(texturePayload->extent.height, desc.baseMipLevel),
            texturePayload->extent.depth > 1u
                ? mipExtent(texturePayload->extent.depth, desc.baseMipLevel)
                : 1u,
        };
        payload->mipLevelCount = desc.mipLevelCount;
        payload->arrayLayerCount = desc.arrayLayerCount;
        payload->sampleCount = texturePayload->sampleCount;
    }
    return payload;
}

std::shared_ptr<MetalRhiNativeSamplerPayload> createMetalRhiNativeSamplerPayload(
    const rhi::SamplerDesc&, bool)
{
    return makeDiagnosticPayload<MetalRhiNativeSamplerPayload>(MetalRhiNativeStorageMode::None);
}

std::shared_ptr<MetalRhiNativeHeapPayload> createMetalRhiNativeHeap(
    std::uint64_t sizeBytes, std::uint64_t alignmentBytes, std::pmr::string* diagnostics)
{
    if (diagnostics != nullptr) {
        *diagnostics = "RHI-METAL-MEMORY-HEAP-NATIVE-RUNTIME-UNAVAILABLE";
    }
    auto payload =
        makeDiagnosticPayload<MetalRhiNativeHeapPayload>(MetalRhiNativeStorageMode::Private);
    payload->sizeBytes = sizeBytes;
    payload->alignmentBytes = alignmentBytes;
    return payload;
}

bool writeMetalRhiNativeBuffer(const std::shared_ptr<MetalRhiNativeBufferPayload>&,
    const void*,
    std::uint64_t,
    std::uint64_t,
    std::uint64_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-BUFFER-WRITE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool readMetalRhiNativeBuffer(const std::shared_ptr<MetalRhiNativeBufferPayload>&,
    void*,
    std::uint64_t,
    std::uint64_t,
    std::uint64_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-BUFFER-READ-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

std::shared_ptr<MetalRhiNativeTexturePayload> createPlacedMetalRhiNativeTexturePayload(
    const MetalRhiNativeHeapPayload&, const rhi::TextureDesc&, std::uint64_t)
{
    // Stub: placed texture not available without Metal runtime.
    return std::shared_ptr<MetalRhiNativeTexturePayload>();
}

bool metalRhiNativePlacementAliasingAvailable()
{
    return false;
}

bool queryMetalRhiNativeTexturePlacementRequirements(
    const rhi::TextureDesc&, rhi::TexturePlacementRequirements&)
{
    return false;
}

} // namespace arp::rhi_metal
