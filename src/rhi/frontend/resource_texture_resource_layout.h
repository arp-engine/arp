#pragma once
#include "rhi/gpu_resource_types.h"

namespace arp::rhi {
// Physical descriptor comparison only, never registered/native resource proof.
struct TextureResourceLayout {
    Extent3D extent;
    TextureFormat format;
    TextureUsage usage;
    MemoryUsage memory;
    ResourceState initialState;
    std::uint32_t mipLevelCount, arrayLayerCount, sampleCount;
    bool cubeCompatible;

    static TextureResourceLayout capture(const TextureDesc& d) noexcept
    {
        return {d.extent,
            d.format,
            d.usage,
            d.memory,
            d.initialState,
            d.mipLevelCount,
            d.arrayLayerCount,
            d.sampleCount,
            d.cubeCompatible};
    }

    bool matches(const TextureDesc& d) const noexcept
    {
        return extent == d.extent && format == d.format && usage == d.usage && memory == d.memory
            && initialState == d.initialState && mipLevelCount == d.mipLevelCount
            && arrayLayerCount == d.arrayLayerCount && sampleCount == d.sampleCount
            && cubeCompatible == d.cubeCompatible;
    }

    void write(TextureDesc& d) const noexcept
    {
        d.extent = extent;
        d.format = format;
        d.usage = usage;
        d.memory = memory;
        d.initialState = initialState;
        d.mipLevelCount = mipLevelCount;
        d.arrayLayerCount = arrayLayerCount;
        d.sampleCount = sampleCount;
        d.cubeCompatible = cubeCompatible;
    }
};
} // namespace arp::rhi
