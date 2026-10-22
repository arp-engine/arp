#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/metal/metal_allocator.h"
#include "rhi/gpu_resources.h"

#include <cstdint>
#include <memory>
#include <string>

namespace arp::rhi {
struct BindlessTextureInventoryResource;
}

namespace arp::rhi_metal {
class MetalRhiImmutableTextureRevision;
class MetalRhiClosedConsumerImages;
struct MetalRhiNativeTextureViewPayload;
struct MetalRhiNativeTexturePayload;

enum class MetalRhiNativeStorageMode { None, Private, Shared, Managed, Memoryless };

struct MetalRhiNativeResourcePayload {
    // RetainedAllocationBudget/1. Declared before nativeObject so storage is
    // released before its final accounting token. Commands/views share this
    // ownership; destroying an L2 wrapper does not release an in-use charge.
    std::shared_ptr<void> allocationReleaseToken;
    std::shared_ptr<void> nativeDevice;
    MetalRhiNativeStorageMode storageMode = MetalRhiNativeStorageMode::None;
    bool nativeRuntimeBacked = false;
    bool nativeRuntimeDiagnosticOnly = true;
    bool nativeHandleExposedToUpperLayer = false;
    // Only true after explicit tracked buffer creation and real native query.
    bool hazardsExplicitlyTracked = false;
    std::shared_ptr<void> nativeObject;
    std::pmr::string diagnostics;
};

struct MetalRhiNativeBufferPayload {
    MetalRhiNativeResourcePayload resource;
};

// Reobserve the real independent buffer substrate; never infer tracked mode or
// physical extent from logical registry values or a caller-provided boolean.
const char* validateMetalRhiTrackedNativeBufferObject(
    const MetalRhiNativeResourcePayload&, std::uint64_t expectedBytes) noexcept;

// Cold observation of the actual independent allocation, including native
// rounding. It never substitutes logical buffer length for physical storage.
std::uint64_t observeMetalRhiNativeBufferAllocation(
    const MetalRhiNativeResourcePayload&, std::uint64_t expectedBytes, const char*&) noexcept;

// Allocation-free native observation of explicit tracked independent 2D/array/3D storage.
const char* validateMetalRhiTrackedNativeTextureObject(
    const MetalRhiNativeResourcePayload&, const rhi::TextureDesc&) noexcept;

// Publication/1: authenticate the actual whole sampled view and its original
// parent. This observation grants neither execution nor a state transition.
const char* validateMetalRhiNativeCompletedTextureView(const MetalRhiNativeTexturePayload&,
    const MetalRhiNativeTextureViewPayload&,
    const rhi::TextureDesc&,
    const rhi::TextureViewDesc&) noexcept;
const char* validateMetalRhiNativeCompletedDepthView(const MetalRhiNativeTexturePayload&,
    const MetalRhiNativeTextureViewPayload&,
    const rhi::TextureDesc&,
    const rhi::TextureViewDesc&) noexcept;
// Independent cold cell publication: one RGBA16F mip, sampled or WriteOnly.
const char* validateMetalRhiNativeTextureCellView(const MetalRhiNativeTexturePayload&,
    const MetalRhiNativeTextureViewPayload&,
    const rhi::TextureDesc&,
    const rhi::TextureViewDesc&,
    bool fullMipChain = false) noexcept;

// Cold independent original upload/attachment reservation from a native descriptor query.
// The allocation is accepted only when its actual native size fits this bound.
std::uint64_t queryMetalRhiOriginalContentAllocation(
    const rhi::TextureDesc&, const char*&) noexcept;
const char* validateMetalRhiOriginalContentAllocation(const MetalRhiNativeResourcePayload&,
    const rhi::TextureDesc&,
    std::uint64_t reservationBytes) noexcept;
std::uint64_t observeMetalRhiOriginalContentAllocation(const MetalRhiNativeResourcePayload&,
    const rhi::TextureDesc&,
    std::uint64_t reservationBytes,
    const char*&) noexcept;

struct MetalRhiNativeTexturePayload {
    MetalRhiNativeResourcePayload resource;
    // Keeps the placement heap alive and lets alias barriers prove that both
    // logical resources are backed by the same native heap.
    std::shared_ptr<void> aliasHeap;
    rhi::TextureFormat format = rhi::TextureFormat::Unknown;
    rhi::Extent3D extent;
    std::uint32_t mipLevelCount = 0;
    std::uint32_t arrayLayerCount = 0;
    std::uint32_t sampleCount = 0;
    bool cubeCompatible = false;

    const auto& immutableRevision() const noexcept { return immutableRevision_; }

    bool originalContentExclusive() const noexcept { return originalContentExclusive_; }

private:
    friend class MetalRhiClosedConsumerImages;
    friend std::shared_ptr<MetalRhiNativeTexturePayload>
    createMetalRhiOriginalContentTexturePayload(const rhi::TextureDesc&);
    bool originalContentExclusive_ = false;
    std::shared_ptr<const MetalRhiImmutableTextureRevision> immutableRevision_;
};

struct MetalRhiNativeTextureViewPayload {
    MetalRhiNativeResourcePayload resource;
    rhi::TextureFormat format = rhi::TextureFormat::Unknown;
    rhi::Extent3D extent;
    std::uint32_t mipLevelCount = 0;
    std::uint32_t arrayLayerCount = 0;
    std::uint32_t sampleCount = 0;

    const auto& immutableRevision() const noexcept { return immutableRevision_; }

    bool originalContentExclusive() const noexcept { return originalContentExclusive_; }

private:
    friend class MetalRhiClosedConsumerImages;
    friend std::shared_ptr<MetalRhiNativeTextureViewPayload> createMetalRhiNativeTextureViewPayload(
        const std::shared_ptr<MetalRhiNativeTexturePayload>&, const rhi::TextureViewDesc&);
    bool originalContentExclusive_ = false;
    std::shared_ptr<const MetalRhiImmutableTextureRevision> immutableRevision_;
};

// Actual sampled 2D inventory substrate, including native parent/range queries.
const char* validateMetalRhiNativeSampledTextureInventory(const MetalRhiNativeTexturePayload&,
    const MetalRhiNativeTextureViewPayload&,
    const rhi::BindlessTextureInventoryResource&) noexcept;

struct MetalRhiNativeSamplerPayload {
    MetalRhiNativeResourcePayload resource;
    bool argumentBufferCompatible = false;
    bool comparisonEnabled = false;
};

/// Opaque native placement-heap payload. Multiple placed textures share one
/// native heap allocation without exposing the framework type in this header.
struct MetalRhiNativeHeapPayload {
    MetalRhiNativeResourcePayload resource;
    // HeapCapacity/1 lowering: L2 placement bounds remain the requested size.
    // Metal may round MTLHeap.size up; charge that queried capacity to the
    // device budget without exposing the extra bytes as placement space.
    // Null uses its logical validator size, Vulkan its native allocation
    // contract, and WebGPU rejects placed heaps (no native primitive).
    std::uint64_t sizeBytes = 0;
    std::uint64_t allocationSizeBytes = 0;
    std::uint64_t alignmentBytes = 0;
    // MetalHeapSynchronization/1: encoder-boundary L2 barriers rely on
    // native hazard tracking, including access through aliased resources.
    // Queried from the real heap, never inferred from OS/backend identity.
    bool hazardsTracked = false;
};

struct MetalRhiNativeResourceInspection {
    bool backendPayloadPresent = false;
    bool nativeRuntimeBacked = false;
    bool nativeRuntimeDiagnosticOnly = true;
    bool nativeHandleExposedToUpperLayer = false;
    MetalRhiNativeStorageMode storageMode = MetalRhiNativeStorageMode::None;
};

MetalRhiNativeStorageMode metalRhiNativeStorageMode(MetalRhiMemoryPolicy policy);

// Backend-owned independent buffer creation reused by ordinary resources and
// prepared argument storage. Never returns default/untracked or dummy storage.
std::shared_ptr<void> createMetalRhiTrackedNativeBufferObject(
    const std::shared_ptr<void>& nativeDevice,
    std::uint64_t sizeBytes,
    MetalRhiNativeStorageMode storage,
    core::pmr::String& diagnostics);

std::shared_ptr<MetalRhiNativeBufferPayload> createMetalRhiNativeBufferPayload(
    const rhi::BufferDesc& desc, MetalRhiMemoryPolicy policy);

/// Writes CPU data into a native shared/managed buffer. Private and
/// memoryless buffers cannot be directly written by the CPU and fail closed.
bool writeMetalRhiNativeBuffer(const std::shared_ptr<MetalRhiNativeBufferPayload>& buffer,
    const void* data,
    std::uint64_t sizeBytes,
    std::uint64_t offsetBytes,
    std::uint64_t bufferSizeBytes,
    std::pmr::string& diagnostics);

/// Reads CPU-visible bytes from a shared/managed buffer after the caller has
/// crossed an explicit GPU completion boundary. Native objects stay private
/// to this backend helper; validation code receives only copied bytes.
bool readMetalRhiNativeBuffer(const std::shared_ptr<MetalRhiNativeBufferPayload>& buffer,
    void* data,
    std::uint64_t sizeBytes,
    std::uint64_t offsetBytes,
    std::uint64_t bufferSizeBytes,
    std::pmr::string& diagnostics);

std::shared_ptr<MetalRhiNativeTexturePayload> createMetalRhiNativeTexturePayload(
    const rhi::TextureDesc& desc, MetalRhiMemoryPolicy policy);
std::shared_ptr<MetalRhiNativeTexturePayload> createMetalRhiOriginalContentTexturePayload(
    const rhi::TextureDesc&);

std::shared_ptr<MetalRhiNativeTextureViewPayload> createMetalRhiNativeTextureViewPayload(
    const std::shared_ptr<MetalRhiNativeTexturePayload>& texturePayload,
    const rhi::TextureViewDesc& desc);

std::shared_ptr<MetalRhiNativeSamplerPayload> createMetalRhiNativeSamplerPayload(
    const rhi::SamplerDesc& desc, bool argumentBufferCompatible = false);

std::shared_ptr<MetalRhiNativeHeapPayload> createMetalRhiNativeHeap(
    std::uint64_t sizeBytes, std::uint64_t alignmentBytes, std::pmr::string* diagnostics = nullptr);

std::shared_ptr<MetalRhiNativeTexturePayload> createPlacedMetalRhiNativeTexturePayload(
    const MetalRhiNativeHeapPayload& heap, const rhi::TextureDesc& desc, std::uint64_t offsetBytes);

/// Returns true only when this translation unit can create placement heaps and
/// placed textures with the native Metal runtime. Non-Apple stubs return false.
bool metalRhiNativePlacementAliasingAvailable();

/// Queries Metal's native heap placement requirements for the exact texture
/// descriptor. Returns false when the runtime cannot provide a native query.
bool queryMetalRhiNativeTexturePlacementRequirements(
    const rhi::TextureDesc& desc, rhi::TexturePlacementRequirements& requirements);

MetalRhiNativeResourceInspection inspectMetalRhiNativePayload(const rhi::GpuBuffer& buffer);

MetalRhiNativeResourceInspection inspectMetalRhiNativePayload(const rhi::GpuTexture& texture);

MetalRhiNativeResourceInspection inspectMetalRhiNativePayload(const rhi::GpuTextureView& view);

MetalRhiNativeResourceInspection inspectMetalRhiNativePayload(const rhi::GpuSampler& sampler);

} // namespace arp::rhi_metal
