#pragma once

#include "rhi/gpu_resources.h"

// region Resource buffer resource layout
namespace arp::rhi {
// Complete physical BufferDesc fields; label is debug metadata. This neither
// validates creation legality nor proves registry/native/state authority.
struct BufferResourceLayout {
    std::uint64_t sizeBytes;
    ResourceUsage usage;
    MemoryUsage memory;
    ResourceState initialState;

    static BufferResourceLayout capture(const BufferDesc& d) noexcept
    {
        return {d.sizeBytes, d.usage, d.memory, d.initialState};
    }

    bool matches(const BufferDesc& d) const noexcept
    {
        return sizeBytes == d.sizeBytes && usage == d.usage && memory == d.memory
            && initialState == d.initialState;
    }

    void write(BufferDesc& d) const noexcept
    {
        d.sizeBytes = sizeBytes;
        d.usage = usage;
        d.memory = memory;
        d.initialState = initialState;
    }
};

// endregion Resource buffer resource layout

// region Resource texture view resource layout

// Complete physical view fields; debug label stays with the original wrapper.
// Parent ID is frozen separately. Neither observation proves registry/native origin.
struct TextureViewResourceLayout {
    TextureFormat format;
    TextureUsage usage;
    TextureViewDimension dimension;
    std::uint32_t baseMipLevel, mipLevelCount, baseArrayLayer, arrayLayerCount;
    TextureAspect aspect;

    static TextureViewResourceLayout capture(const TextureViewDesc& d) noexcept
    {
        return {d.format,
            d.usage,
            d.dimension,
            d.baseMipLevel,
            d.mipLevelCount,
            d.baseArrayLayer,
            d.arrayLayerCount,
            d.aspect};
    }

    bool matches(const TextureViewDesc& d) const noexcept
    {
        return format == d.format && usage == d.usage && dimension == d.dimension
            && baseMipLevel == d.baseMipLevel && mipLevelCount == d.mipLevelCount
            && baseArrayLayer == d.baseArrayLayer && arrayLayerCount == d.arrayLayerCount
            && aspect == d.aspect;
    }
};
} // namespace arp::rhi

// endregion Resource texture view resource layout

namespace arp::rhi {
// Complete physical sampler layout; labels do not participate in its identity.
struct SamplerResourceLayout {
    SamplerFilter minFilter, magFilter;
    SamplerAddressMode addressU, addressV, addressW;
    CompareOp compareOp;
    SamplerMipFilter mipFilter;
    float maxAnisotropy;

    static SamplerResourceLayout capture(const SamplerDesc& d) noexcept
    {
        return {d.minFilter,
            d.magFilter,
            d.addressU,
            d.addressV,
            d.addressW,
            d.compareOp,
            d.mipFilter,
            d.maxAnisotropy};
    }

    bool matches(const SamplerDesc& d) const noexcept
    {
        return minFilter == d.minFilter && magFilter == d.magFilter && addressU == d.addressU
            && addressV == d.addressV && addressW == d.addressW && compareOp == d.compareOp
            && mipFilter == d.mipFilter && maxAnisotropy == d.maxAnisotropy;
    }
};
} // namespace arp::rhi
