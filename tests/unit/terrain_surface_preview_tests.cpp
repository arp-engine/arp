#include "assets/terrain/terrain_surface_preview.h"

#include <cassert>
#include <limits>

using namespace arp;

assets::DecodedImage image(std::array<std::uint8_t, 4> rgba, bool srgb)
{
    assets::DecodedImage result;
    result.width = result.height = 1;
    result.srgb = srgb;
    result.rgba8.assign(rgba.begin(), rgba.end());
    return result;
}

int main()
{
    assets::TerrainSurfacePreviewInput source;
    source.snowLine = 0;
    source.snowFeather = 10;
    for (auto& layer : source.layers) {
        layer.physicalSizeMeters = 2;
        layer.albedo = image({0, 0, 0, 255}, true);
        layer.normal = image({128, 128, 255, 255}, false);
        layer.roughness = image({127, 127, 127, 255}, false);
        layer.occlusion = image({255, 255, 255, 255}, false);
    }
    source.layers[0].albedo = image({255, 0, 0, 255}, true);
    source.layers[1].albedo = image({0, 255, 0, 255}, true);
    source.layers[5].albedo = image({255, 255, 255, 255}, true);
    float heights[]{0, 0, 10, 10};
    std::uint8_t a[]{255, 0, 0, 0, 0, 255, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0};
    std::uint8_t b[]{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 0, 0, 0, 255};
    assets::TerrainSurfacePreviewTile tile;
    tile.samplesPerEdge = 2;
    tile.outputResolution = 3;
    tile.worldSize = 2;
    tile.heights = {heights, 4};
    tile.pageA = {a, 16};
    tile.pageB = {b, 16};
    tile.maxOutputBytes = 180;
    assets::TerrainSurfacePreviewBaker baker;
    const auto baked = baker.bake(source, tile);
    assert(baked.success);
    const auto& color = baked.textures[0].rgba8;
    assert(color[0] == 255 && color[1] == 0 && color[8] == 0 && color[9] == 255);
    // Half red/green is filtered in linear space: encoded value is 188, not 128.
    assert(color[4] >= 187 && color[4] <= 188 && color[5] >= 187 && color[5] <= 188);
    assert(color[25] > 200 && color[26] > 200); // snow pulls toward layer 5
    assert(color[32] < 255);                    // wetness pulls red grass toward black mud
    assert(baked.textures[2].rgba8[2] == 0);    // explicit nonmetal preview
    assert(baked.textures[4].rgba8[0] == 0);    // explicit no emission
    const auto repeated = baker.bake(source, tile);
    assert(repeated.success && repeated.textures[0].rgba8 == color);
    --tile.maxOutputBytes;
    assert(!baker.bake(source, tile).success);
    ++tile.maxOutputBytes;
    a[0] = 0;
    assert(!baker.bake(source, tile).success);
    a[0] = 255;
    heights[0] = std::numeric_limits<float>::quiet_NaN();
    assert(!baker.bake(source, tile).success);
    heights[0] = 0;
    source.layers[0].normal.srgb = true;
    assert(!baker.bake(source, tile).success);
    source.layers[0].normal.srgb = false;
    source.layers[0].physicalSizeMeters = 0;
    assert(!baker.bake(source, tile).success);
}
