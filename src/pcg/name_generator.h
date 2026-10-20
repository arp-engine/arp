#pragma once
// Phase 13: Procedural names via template-based generation.
#include "pcg/simplex_noise.h"

#include <cstdint>
#include <string>
#include <vector>

namespace arp::pcg {

/// Procedural name generator using syllable tables.
/// Deterministic given a seed and entity type.
struct NameGen {
    static constexpr const char* prefixes[] = {"East",
        "West",
        "North",
        "South",
        "Iron",
        "Dark",
        "Gold",
        "Storm",
        "Silver",
        "Red",
        "Black",
        "White",
        "Old",
        "New",
        "High",
        "Deep",
        "Wild",
        "Lost",
        "Fallen",
        "Crimson"};
    static constexpr const char* roots[] = {"wood",
        "brook",
        "moor",
        "ford",
        "haven",
        "watch",
        "forge",
        "stone",
        "glen",
        "marsh",
        "ridge",
        "crest",
        "hollow",
        "peak",
        "vale",
        "cairn",
        "thane",
        "drake",
        "wolf",
        "raven"};
    static constexpr const char* suffixes[] = {"ton",
        "burg",
        "holm",
        "stead",
        "shire",
        "field",
        "gate",
        "wall",
        "march",
        "keep",
        "hold",
        "fell",
        "dale",
        "ham",
        "wick",
        "by",
        "thorpe",
        "cliff",
        "vale",
        "isle"};

    static const char* genPrefix(std::uint32_t seed)
    {
        return prefixes[static_cast<int>(simplex2D(0.1f, 0.1f, seed) * 20) % 20];
    }

    static const char* genRoot(std::uint32_t seed)
    {
        return roots[static_cast<int>(simplex2D(0, 0, seed) * 20) % 20];
    }

    static const char* genSuffix(std::uint32_t seed)
    {
        return suffixes[static_cast<int>(simplex2D(0, 0, seed + 50u) * 20) % 20];
    }

    static const char* genAdj(std::uint32_t seed)
    {
        constexpr const char* adjs[] = {"Cursed",
            "Ancient",
            "Forgotten",
            "Whispering",
            "Shattered",
            "Burning",
            "Frozen",
            "Shadowed",
            "Blighted",
            "Howling"};
        return adjs[static_cast<int>(simplex2D(0.1f, 0.0f, seed) * 10) % 10];
    }

    static const char* genType(std::uint32_t seed)
    {
        constexpr const char* types[] = {"Crypt",
            "Tower",
            "Fortress",
            "Ruins",
            "Catacombs",
            "Temple",
            "Mine",
            "Grove",
            "Spire",
            "Sanctum"};
        return types[static_cast<int>(simplex2D(0.0f, 0.1f, seed) * 10) % 10];
    }

    /// Generate a settlement name: "PrefixRootSuffix" e.g. "Eastbrookton"
    static std::string settlement(std::uint32_t seed)
    {
        return std::string(genPrefix(seed)) + genRoot(seed + 1u) + genSuffix(seed + 2u);
    }

    /// Generate a landmark name: "Adjective Noun" e.g. "Cursed Crypt"
    static std::string landmark(std::uint32_t seed)
    {
        return std::string(genAdj(seed)) + " " + genType(seed + 1u);
    }

    /// Generate an NPC name: "Title First Last"
    static std::string npc(std::uint32_t seed)
    {
        constexpr const char* titles[] = {"Captain", "Lord", "Lady", "Elder", "Master", "Warden"};
        constexpr const char* firsts[] = {"Aldric",
            "Brynn",
            "Cedric",
            "Danica",
            "Elara",
            "Fynn",
            "Greta",
            "Horik",
            "Isolde",
            "Joren",
            "Kael",
            "Lyra",
            "Merek",
            "Nissa",
            "Orin"};
        constexpr const char* lasts[] = {"Thorne",
            "Blackwood",
            "Ironfist",
            "Stormwind",
            "Ashford",
            "Briarwood",
            "Coldwater",
            "Drakeson",
            "Emberfall",
            "Frostbane"};
        auto ti = static_cast<int>(simplex2D(0, 0, seed) * 6) % 6;
        auto fi = static_cast<int>(simplex2D(0.1f, 0, seed) * 15) % 15;
        auto li = static_cast<int>(simplex2D(0, 0.1f, seed) * 10) % 10;
        return std::string(titles[ti]) + " " + firsts[fi] + " " + lasts[li];
    }
};

} // namespace arp::pcg
