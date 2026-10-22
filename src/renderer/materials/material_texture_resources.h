#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/gpu_common.h"

#include <cstdint>
#include <string>
#include <vector>

namespace arp::renderer {

enum class MaterialTextureSemantic {
    BaseColor,
    Normal,
    MetallicRoughness,
    Occlusion,
    Emissive,
    Clearcoat,
    ClearcoatRoughness,
    ClearcoatNormal,
    Specular,
    SpecularColor,
    SheenColor,
    SheenRoughness,
    Transmission,
    Thickness,
    Iridescence,
    IridescenceThickness,
};

struct MaterialTextureSource {
    uint32_t materialSlot = 0;
    std::pmr::string name;
    rhi::Extent2D extent;
    std::pmr::vector<uint8_t> rgba8;
    bool srgb = true;
    MaterialTextureSemantic semantic = MaterialTextureSemantic::BaseColor;
    bool alphaIsTransparency = false;
    float alphaTransparencyReference = 0.5f;

    bool valid() const
    {
        const auto expectedBytes =
            static_cast<std::size_t>(extent.width) * static_cast<std::size_t>(extent.height) * 4u;
        return extent.width > 0 && extent.height > 0 && !rgba8.empty()
            && rgba8.size() == expectedBytes;
    }
};

struct MaterialTextureUploadResult {
    bool success = false;
    std::pmr::string diagnostics;
};

inline const char* materialTextureSemanticName(MaterialTextureSemantic semantic)
{
    switch (semantic) {
    case MaterialTextureSemantic::BaseColor:
        return "BaseColor";
    case MaterialTextureSemantic::Normal:
        return "Normal";
    case MaterialTextureSemantic::MetallicRoughness:
        return "MetallicRoughness";
    case MaterialTextureSemantic::Occlusion:
        return "Occlusion";
    case MaterialTextureSemantic::Emissive:
        return "Emissive";
    case MaterialTextureSemantic::Clearcoat:
        return "Clearcoat";
    case MaterialTextureSemantic::ClearcoatRoughness:
        return "ClearcoatRoughness";
    case MaterialTextureSemantic::ClearcoatNormal:
        return "ClearcoatNormal";
    case MaterialTextureSemantic::Specular:
        return "Specular";
    case MaterialTextureSemantic::SpecularColor:
        return "SpecularColor";
    case MaterialTextureSemantic::SheenColor:
        return "SheenColor";
    case MaterialTextureSemantic::SheenRoughness:
        return "SheenRoughness";
    case MaterialTextureSemantic::Transmission:
        return "Transmission";
    case MaterialTextureSemantic::Thickness:
        return "Thickness";
    case MaterialTextureSemantic::Iridescence:
        return "Iridescence";
    case MaterialTextureSemantic::IridescenceThickness:
        return "IridescenceThickness";
    }
    return "Unknown";
}

inline std::pmr::vector<MaterialTextureSemantic> materialTextureSemantics()
{
    return {
        MaterialTextureSemantic::BaseColor,
        MaterialTextureSemantic::Normal,
        MaterialTextureSemantic::MetallicRoughness,
        MaterialTextureSemantic::Occlusion,
        MaterialTextureSemantic::Emissive,
        MaterialTextureSemantic::Clearcoat,
        MaterialTextureSemantic::ClearcoatRoughness,
        MaterialTextureSemantic::ClearcoatNormal,
        MaterialTextureSemantic::Specular,
        MaterialTextureSemantic::SpecularColor,
        MaterialTextureSemantic::SheenColor,
        MaterialTextureSemantic::SheenRoughness,
        MaterialTextureSemantic::Transmission,
        MaterialTextureSemantic::Thickness,
        MaterialTextureSemantic::Iridescence,
        MaterialTextureSemantic::IridescenceThickness,
    };
}

} // namespace arp::renderer
