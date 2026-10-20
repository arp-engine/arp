#pragma once
#include "core/api.h"
#include "core/cpu_allocator.h"
#include "rhi/gpu_common.h"

namespace arp::rhi {

enum class ResourceUsage : std::uint32_t {
    None = 0,
    VertexBuffer = 1u << 0,
    IndexBuffer = 1u << 1,
    UniformBuffer = 1u << 2,
    StorageBuffer = 1u << 3,
    CopySrc = 1u << 4,
    CopyDst = 1u << 5,
    /// Buffer can be used as the source of indirect draw/dispatch commands
    /// (VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT). Required by P5 GPU-driven cull:
    /// a compute shader writes DrawIndexedIndirectCommand[] into a buffer
    /// created with StorageBuffer | IndirectBuffer, then drawIndexedIndirect
    /// reads from the same buffer.
    IndirectBuffer = 1u << 6
};

inline ResourceUsage operator|(ResourceUsage lhs, ResourceUsage rhs)
{
    return static_cast<ResourceUsage>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

inline ResourceUsage operator&(ResourceUsage lhs, ResourceUsage rhs)
{
    return static_cast<ResourceUsage>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

enum class TextureUsage : std::uint32_t {
    None = 0,
    Sampled = 1u << 0,
    Storage = 1u << 1,
    RenderAttachment = 1u << 2,
    CopySrc = 1u << 3,
    CopyDst = 1u << 4
};

inline TextureUsage operator|(TextureUsage lhs, TextureUsage rhs)
{
    return static_cast<TextureUsage>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

inline TextureUsage operator&(TextureUsage lhs, TextureUsage rhs)
{
    return static_cast<TextureUsage>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

enum class MemoryUsage { GpuOnly, CpuToGpu, GpuToCpu, Transient };

enum class ResourceMemoryClass { DeviceLocal, Upload, Readback, Transient };

/// ResourceIdentity/1: engine-created IDs are opaque, runtime-wide, nonzero,
/// and never recycled across devices, resource kinds, heaps or swapchains.
/// Identity does not imply liveness or permission to bind a foreign resource.
struct GpuResourceId {
    std::uint64_t value = 0;

    bool isValid() const { return value != 0; }
};

/// Backend creation entry point, shared by all implementations. Throws
/// core::DiagnosticError("RHI-RESOURCE-ID-EXHAUSTED") rather than wrapping.
/// Consumers must not synthesize IDs or depend on allocation order.
ARP_API GpuResourceId allocateGpuResourceId();

inline bool operator==(GpuResourceId lhs, GpuResourceId rhs)
{
    return lhs.value == rhs.value;
}

inline bool operator!=(GpuResourceId lhs, GpuResourceId rhs)
{
    return !(lhs == rhs);
}

struct ResourceAllocation {
    ResourceMemoryClass memoryClass = ResourceMemoryClass::DeviceLocal;
    std::uint64_t estimatedSizeBytes = 0;
    bool nativeHandleExposed = false;
    bool backendFallbackHiddenFromUpperLayer = true;
};

struct Extent3D {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 1;
};

inline bool operator==(const Extent3D& lhs, const Extent3D& rhs) noexcept
{
    return lhs.width == rhs.width && lhs.height == rhs.height && lhs.depth == rhs.depth;
}

inline bool operator!=(const Extent3D& lhs, const Extent3D& rhs) noexcept
{
    return !(lhs == rhs);
}

enum class TextureViewDimension { Texture2D, Texture2DArray, Texture3D, TextureCube };

enum class TextureAspect { Color, Depth, Stencil };

struct TextureSubresourceRange {
    std::uint32_t baseMipLevel = 0;
    std::uint32_t mipLevelCount = 1;
    std::uint32_t baseArrayLayer = 0;
    std::uint32_t arrayLayerCount = 1;
    TextureAspect aspect = TextureAspect::Color;
};

inline bool operator==(const TextureSubresourceRange& lhs, const TextureSubresourceRange& rhs)
{
    return lhs.baseMipLevel == rhs.baseMipLevel && lhs.mipLevelCount == rhs.mipLevelCount
        && lhs.baseArrayLayer == rhs.baseArrayLayer && lhs.arrayLayerCount == rhs.arrayLayerCount
        && lhs.aspect == rhs.aspect;
}

inline bool operator!=(const TextureSubresourceRange& lhs, const TextureSubresourceRange& rhs)
{
    return !(lhs == rhs);
}

struct TextureSubresourceState {
    TextureSubresourceRange range;
    ResourceState state = ResourceState::Undefined;
};

struct BufferDesc {
    core::pmr::String label;
    std::uint64_t sizeBytes = 0;
    ResourceUsage usage = ResourceUsage::None;
    MemoryUsage memory = MemoryUsage::GpuOnly;
    ResourceState initialState = ResourceState::Undefined;
};

struct TextureDesc {
    core::pmr::String label;
    Extent3D extent;
    TextureFormat format = TextureFormat::Unknown;
    TextureUsage usage = TextureUsage::None;
    MemoryUsage memory = MemoryUsage::GpuOnly;
    ResourceState initialState = ResourceState::Undefined;
    std::uint32_t mipLevelCount = 1;
    std::uint32_t arrayLayerCount = 1;
    std::uint32_t sampleCount = 1;
    bool cubeCompatible = false;
};

/// L2 placement requirements for a transient texture. Backends with native
/// placement query (Vulkan vkGetImageMemoryRequirements2, Metal
/// heapTextureSizeAndAlignWithDescriptor) override
/// GpuDevice::queryTexturePlacementRequirements to return precise values; the
/// default L2 estimate (estimateTexturePlacementRequirements) sums the mip
/// chain and multiplies by array layers, cube faces and MSAA samples.
struct TexturePlacementRequirements {
    std::uint64_t sizeBytes = 0;
    std::uint64_t alignmentBytes = 0;
};

/// L2 placement size estimate. Pure function shared by the render graph
/// transient allocator and the GpuDevice default query. Sums the mip chain,
/// multiplies by the physical arrayLayerCount and sampleCount. A cube
/// compatible texture already reports its six physical faces through
/// arrayLayerCount; cubeCompatible only controls view compatibility and must
/// not multiply the allocation again. It returns {0,0} for Unknown formats and
/// this is an L2 estimate —
/// not a native placement query (no row pitch, compression block size, or
/// backend alignment); backends with a native query should override
/// GpuDevice::queryTexturePlacementRequirements.
inline TexturePlacementRequirements estimateTexturePlacementRequirements(
    const TextureDesc& desc) noexcept
{
    // Block-compressed formats (BC1/BC3/BC7) cannot use bytesPerPixel: a 1x1
    // BC7 surface still consumes a full 4x4 block. Route through the
    // block-aware bytesForTextureMip() helper which handles both cases.
    const auto perMip = [&desc](std::uint32_t w, std::uint32_t h, std::uint32_t d) {
        return bytesForTextureMip(desc.format, w, h, d);
    };
    std::uint64_t mipChainBytes = 0;
    for (std::uint32_t level = 0; level < desc.mipLevelCount; ++level) {
        const auto w = (desc.extent.width >> level) != 0u ? (desc.extent.width >> level) : 1u;
        const auto h = (desc.extent.height >> level) != 0u ? (desc.extent.height >> level) : 1u;
        const auto d = (desc.extent.depth >> level) != 0u ? (desc.extent.depth >> level) : 1u;
        mipChainBytes += perMip(w, h, d);
    }
    TexturePlacementRequirements req;
    req.sizeBytes = mipChainBytes * desc.arrayLayerCount * desc.sampleCount;
    // Alignment: block-compressed formats align to their block byte size
    // (BC1=8, BC3/BC7=16) but 256 is always larger, so the existing
    // max(bytes, 256) logic still holds. Use bytesPerBlock for compressed,
    // bytesPerPixel for uncompressed; both floor to 256 below.
    const auto alignUnit = isBlockCompressed(desc.format)
        ? static_cast<std::uint64_t>(bytesPerBlock(desc.format))
        : bytesPerPixel(desc.format);
    req.alignmentBytes = alignUnit > 256u ? alignUnit : 256u;
    return req;
}

inline bool isDepthTextureFormat(TextureFormat format)
{
    return format == TextureFormat::D32Float || format == TextureFormat::Depth24Stencil8;
}

inline TextureSubresourceRange defaultTextureSubresourceRange(const TextureDesc& desc)
{
    TextureSubresourceRange range;
    range.mipLevelCount = desc.mipLevelCount;
    range.arrayLayerCount = desc.arrayLayerCount;
    range.aspect = isDepthTextureFormat(desc.format) ? TextureAspect::Depth : TextureAspect::Color;
    return range;
}

struct BufferViewDesc {
    core::pmr::String label;
    std::uint64_t offsetBytes = 0;
    std::uint64_t sizeBytes = 0;
    std::uint32_t strideBytes = 0;
    ResourceUsage usage = ResourceUsage::None;
};

struct TextureViewDesc {
    core::pmr::String label;
    TextureFormat format = TextureFormat::Unknown;
    TextureUsage usage = TextureUsage::None;
    TextureViewDimension dimension = TextureViewDimension::Texture2D;
    std::uint32_t baseMipLevel = 0;
    std::uint32_t mipLevelCount = 1;
    std::uint32_t baseArrayLayer = 0;
    std::uint32_t arrayLayerCount = 1;
    TextureAspect aspect = TextureAspect::Color;
};

enum class SamplerFilter { Nearest, Linear };

enum class SamplerAddressMode { ClampToEdge, Repeat };

/// Mipmap filter mode for texture sampling.
///   Linear  - blend between adjacent mip levels (trilinear). Default.
///   Nearest - select the nearest mip level, no inter-level blending.
///   None    - do not minify across mip levels (sample only level 0).
enum class SamplerMipFilter { Linear, Nearest, None };

struct SamplerDesc {
    core::pmr::String label;
    SamplerFilter minFilter = SamplerFilter::Linear;
    SamplerFilter magFilter = SamplerFilter::Linear;
    SamplerAddressMode addressU = SamplerAddressMode::ClampToEdge;
    SamplerAddressMode addressV = SamplerAddressMode::ClampToEdge;
    SamplerAddressMode addressW = SamplerAddressMode::ClampToEdge;
    /// Compare operation for hardware depth comparison sampling (shadow maps).
    /// When set to a value other than Never, the sampler performs a compare
    /// operation between the sampled value and the reference value (provided
    /// by the shader via SampleCmpLevelZero or equivalent), returning 1.0 or
    /// 0.0. This enables hardware-accelerated PCF for shadow mapping.
    /// Default: Never (no comparison - standard texture sampling).
    CompareOp compareOp = CompareOp::Never;
    /// Mipmap filter mode. Anisotropic filtering, when enabled via
    /// maxAnisotropy, implicitly uses Linear mip filtering regardless of this
    /// value. Default: Linear (trilinear).
    SamplerMipFilter mipFilter = SamplerMipFilter::Linear;
    /// Anisotropic filtering multiplier. 0 or 1 disables anisotropy; values
    /// greater than 1 request the anisotropy level. Backends clamp the value
    /// to the device's supported maximum (e.g. Vulkan's
    /// limits.maxSamplerAnisotropy). Default: 0 (anisotropy disabled).
    float maxAnisotropy = 0.0f;
};

struct MemoryStats {
    // RetainedAllocationBudget/1: a charged allocation stays charged while
    // backend-owned commands, views or placed resources retain its storage,
    // even after the public resource wrapper is destroyed. Shared aliases
    // carry one charge; allocation release is tied to the final owner, not
    // to resource-ID validity. This does not keep destroyed IDs bindable.
    // Lowering: Null counts its validator-owned allocations (no native
    // retention); Vulkan/Metal/WebGPU must retain the existing allocation
    // release token with their native payload/parent storage ownership.
    // Native backend evidence is required separately from this invariant.
    std::uint64_t allocatedBytes = 0;
    // ResourceAllocationBudget/1. These values are sourced from the
    // backend-owned fixed allocation ledger, not from a native handle or a
    // best-effort driver estimate. A zero capacity means that this backend
    // did not expose this ledger for the current device.
    std::uint64_t budgetCapacityBytes = 0;
    std::uint64_t peakAllocatedBytes = 0;
    std::uint64_t budgetRejectionCount = 0;
    std::uint32_t liveResourceCount = 0;
    bool backendFallbacksHiddenFromUpperLayer = true;
};

} // namespace arp::rhi
