#pragma once

#include "assets/image_decoder.h"

#include <array>

namespace arp::assets {

// Cold tooling projection, explicitly opted into by editor.terrain-preview/v1.
// All images are RGBA8: albedo sRGB, OpenGL tangent normal/roughness/AO linear.
struct TerrainPreviewLayer final {
    DecodedImage albedo, normal, roughness, occlusion;
    float physicalSizeMeters = 0;
};

struct TerrainSurfacePreviewInput final {
    std::array<TerrainPreviewLayer, 6> layers;
    float snowLine = 0, snowFeather = 0;
};

struct TerrainSurfacePreviewTile final {
    std::uint32_t samplesPerEdge = 0, outputResolution = 0;
    float originX = 0, originZ = 0, worldSize = 0;
    core::ArrayView<const float> heights;
    core::ArrayView<const std::uint8_t> pageA, pageB;
    std::size_t maxOutputBytes = 0;
};

struct TerrainSurfacePreviewResult final {
    bool success = false;
    core::pmr::String diagnostic;
    // PBR v1 order: base color, normal, metallic-roughness, AO, emissive.
    std::array<DecodedImage, 5> textures;
};

class ARP_API TerrainSurfacePreviewBaker final {
public:
    TerrainSurfacePreviewResult bake(
        const TerrainSurfacePreviewInput& input, const TerrainSurfacePreviewTile& tile) const;
};

} // namespace arp::assets
