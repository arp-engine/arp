#pragma once
#include "core/algorithm/strided_index_range.h"
#include "core/math/unsigned_range.h"
#include "rhi/frontend/memory_budget_ledger.h"
#include "rhi/frontend/resource_texture_extent.h"
#include "rhi/gpu_resource_types.h"

#include <array>

namespace arp::rhi::semantics {
// Exact logical texel/block footprint. WebGPU exposes no physical texture heap
// query; this accounting must not be described as native resident memory.
class TextureUploadDestinationValidation final {
public:
    static const char* validate(const TextureDesc& desc,
        std::uint64_t& texelBytes,
        ResourceState creationState = ResourceState::CopyDst) noexcept
    {
        texelBytes = 0u;
        const auto allowed = TextureUsage::CopyDst | TextureUsage::CopySrc | TextureUsage::Sampled;
        if ((creationState != ResourceState::CopyDst && creationState != ResourceState::Undefined)
            || desc.memory != MemoryUsage::GpuOnly || desc.initialState != creationState
            || desc.sampleCount != 1u || (desc.usage & TextureUsage::CopyDst) == TextureUsage::None
            || (static_cast<std::uint32_t>(desc.usage) & ~static_cast<std::uint32_t>(allowed)))
            return "RHI-TEXTURE-DESTINATION-SELECTED-PROFILE";
        if (!desc.extent.width || !desc.extent.height || !desc.extent.depth || !desc.mipLevelCount
            || !desc.arrayLayerCount || isDepthTextureFormat(desc.format)
            || (!bytesPerPixel(desc.format) && !bytesPerBlock(desc.format)))
            return "RHI-TEXTURE-DESTINATION-LAYOUT";
        if ((desc.extent.depth > 1u
                && (desc.arrayLayerCount != 1u || desc.cubeCompatible
                    || isBlockCompressed(desc.format)))
            || (desc.cubeCompatible
                && (desc.extent.width != desc.extent.height || desc.arrayLayerCount % 6u)))
            return "RHI-TEXTURE-DESTINATION-DIMENSION";
        return footprint(desc, texelBytes);
    }

    // Shared descriptor footprint; callers must separately validate their usage
    // profile. It grants neither upload permission nor native allocation facts.
    static const char* footprint(const TextureDesc& desc, std::uint64_t& texelBytes) noexcept
    {
        texelBytes = 0u;
        if (!desc.extent.width || !desc.extent.height || !desc.extent.depth || !desc.mipLevelCount
            || !desc.arrayLayerCount
            || (!bytesPerPixel(desc.format) && !bytesPerBlock(desc.format)))
            return "RHI-TEXTURE-DESTINATION-LAYOUT";
        const auto block = blockExtent(desc.format);
        const auto unit = isBlockCompressed(desc.format) ? bytesPerBlock(desc.format)
                                                         : bytesPerPixel(desc.format);
        std::uint64_t total = 0u;
        for (std::uint32_t mip = 0u; mip < desc.mipLevelCount; ++mip) {
            Extent3D extent;
            if (TextureSubresourceExtent::resolve(desc.extent, desc.mipLevelCount, mip, extent))
                return "RHI-TEXTURE-DESTINATION-MIPS";
            std::uint64_t bytes = unit;
            const std::array<std::uint32_t, 4> factors{{blocksForExtent(extent.width, block),
                blocksForExtent(extent.height, block),
                extent.depth,
                desc.arrayLayerCount}};
            for (const auto factor : factors) {
                // The last index is exactly factor*bytes; the reusable range
                // proves overflow before performing each multiplication.
                const core::StridedIndexRange product(bytes, factor, bytes);
                if (!product.index(factor - 1u, bytes))
                    return "RHI-TEXTURE-DESTINATION-BYTE-OVERFLOW";
            }
            if (!core::MemoryBudgetLedger::canAdd(total, bytes))
                return "RHI-TEXTURE-DESTINATION-BYTE-OVERFLOW";
            total += bytes;
        }
        texelBytes = total;
        return nullptr;
    }
};
} // namespace arp::rhi::semantics
