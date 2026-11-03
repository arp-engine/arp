#pragma once

#include <array>
#include <cstddef>

namespace arp::survivors {

// ContentBindings/1 formalizes the original sample's fixed filenames/UUIDs.
// Atlas inputs alone are authored by content.yaml. No directory inference.
struct ContentBindings final {
    static constexpr const char* manifest = "content.yaml";
    static constexpr const char* tileSet = "dungeon.tileset.yaml";
    static constexpr const char* tileMap = "dungeon.tilemap.yaml";
    static constexpr const char* prefab = "orbit.prefab.yaml";
    static constexpr const char* prefabId = "20000000-0000-4000-8000-000000005000";
    static constexpr std::size_t maxFiles = 10;
    static constexpr std::size_t maxAudioDecoderBytes = 64u * 1024u;

    struct Sound {
        const char* path;
        const char* id;
    };

    static constexpr std::array<Sound, 4> sounds{
        {{"hit.wav", "20000000-0000-4000-8000-000000004000"},
            {"upgrade.wav", "20000000-0000-4000-8000-000000004001"},
            {"menu.wav", "20000000-0000-4000-8000-000000004002"},
            {"music.wav", "20000000-0000-4000-8000-000000004003"}}};
};
} // namespace arp::survivors
