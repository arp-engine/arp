#pragma once
#include "2d/tile_map_runtime.h"
#include "tests/fixtures/scene_2d_fixture.h"

namespace arp::tests {
inline constexpr const char* tileSetId = "00000000-0000-4000-8000-000000000020";
inline constexpr const char* tileMapId = "00000000-0000-4000-8000-000000000021";

inline bool loadTileFixture(resource::ResourceManager& resources)
{
    core::pmr::Vector<scene2d::SpriteAnimationFrame> frames;
    frames.push_back({core::pmr::String(spriteA), .125, 1});
    frames.push_back({core::pmr::String(spriteB), .125, 2});
    const core::pmr::String animationId = "00000000-0000-4000-8000-000000000010";
    if (!resources.load<scene2d::SpriteAnimationResource>({}, animationId, frames, true).success)
        return false;
    const core::pmr::String root = ARP_SOURCE_DIR "/tests/fixtures/scene_2d/";
    auto set = resources.load<scene2d::TileSetResource>(
        {}, core::pmr::String(tileSetId), root + "tileset.yaml", 65536);
    if (!set.success) {
        std::fprintf(stderr, "%s\n", set.diagnostics.c_str());
        return false;
    }
    auto map = resources.load<scene2d::TileMapResource>({},
        core::pmr::String(tileMapId),
        core::pmr::String(tileSetId),
        root + "tilemap.yaml",
        65536);
    if (!map.success)
        std::fprintf(stderr, "%s\n", map.diagnostics.c_str());
    return map.success;
}
} // namespace arp::tests
