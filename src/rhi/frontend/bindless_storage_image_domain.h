#pragma once

#include "rhi/gpu_descriptors.h"

namespace arp::rhi {
// A declaration ABI, never an execution capability. The storage conversion is
// performed by the typed image instruction: RGBA16F retains four float lanes;
// it must not reuse the scalar R32 helper or reinterpret texture bytes.
class BindlessStorageImageDomain final {
public:
    static constexpr std::uint32_t contractVersion = 1u;

    struct Declaration {
        TextureFormat format = TextureFormat::Unknown;
        std::uint32_t floatLanes = 0u, spirvImageFormat = 0u;
        const char* wgslFormat = nullptr;
        StorageTextureAccess access = StorageTextureAccess::ReadWrite;
    };

    static constexpr Declaration declaration(
        TextureFormat format, std::uint32_t version = 0u) noexcept
    {
        if (version > contractVersion)
            return {};
        if (format == TextureFormat::R32Float)
            return {format, 1u, 3u, "r32float"};
        if (version == contractVersion && format == TextureFormat::Rgba16Float)
            return {format, 4u, 2u, "rgba16float", StorageTextureAccess::WriteOnly};
        return {};
    }

    static constexpr bool accepts(TextureFormat format,
        TextureViewDimension dimension,
        TextureAspect aspect,
        std::uint32_t samples,
        StorageTextureAccess access,
        std::uint32_t version = 0u) noexcept
    {
        return declaration(format, version).floatLanes
            && dimension == TextureViewDimension::Texture2D && aspect == TextureAspect::Color
            && samples == 1u && access == declaration(format, version).access;
    }
};
} // namespace arp::rhi
