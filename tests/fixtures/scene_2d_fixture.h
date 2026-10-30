#pragma once
#include "2d/sprite_draw_plan.h"

#include <cassert>
#include <cstdio>

namespace arp::tests {
inline constexpr const char* atlasA = "00000000-0000-4000-8000-000000000001";
inline constexpr const char* atlasB = "00000000-0000-4000-8000-000000000002";
inline constexpr const char* spriteA = "00000000-0000-4000-8000-000000000003";
inline constexpr const char* spriteB = "00000000-0000-4000-8000-000000000004";

inline bool loadSpriteFixtureAtRoot(resource::ResourceManager& manager, std::string_view rootPath)
{
    const core::pmr::String root(rootPath);
    for (auto pair : {std::pair{atlasA, "atlas.yaml"}, std::pair{atlasB, "atlas_b.yaml"}}) {
        auto result = manager.load<scene2d::SpriteAtlasResource>(
            {}, core::pmr::String(pair.first), root + pair.second, root + "atlas.png", 65536u);
        if (!result.success) {
            std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
            return false;
        }
    }
    auto a = manager.load<scene2d::SpriteResource>(
        {}, core::pmr::String(spriteA), core::pmr::String(atlasA), core::pmr::String("left"));
    auto b = manager.load<scene2d::SpriteResource>(
        {}, core::pmr::String(spriteB), core::pmr::String(atlasB), core::pmr::String("right"));
    if (!a.success || !b.success)
        std::fprintf(stderr, "%s %s\n", a.diagnostics.c_str(), b.diagnostics.c_str());
    return a.success && b.success;
}
#ifdef ARP_SOURCE_DIR
inline bool loadSpriteFixture(resource::ResourceManager& manager)
{
    return loadSpriteFixtureAtRoot(manager, ARP_SOURCE_DIR "/tests/fixtures/scene_2d/");
}
#endif
} // namespace arp::tests
