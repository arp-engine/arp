#pragma once

#include "core/memory/cpu_allocator.h"

#include <array>
#include <cstdint>

namespace arp::build_a_queen {

enum class Phase : std::uint8_t { Menu, Running, RunwayBattle, Result };
enum class Style : std::uint8_t { Rose, Ocean, Lime, Violet, Gold };
enum class Slot : std::uint8_t { Hair, Top, Bottom, Shoes, Accessory, Count };

struct LevelDesc {
    Style theme;
    std::array<Style, 5> target;
    std::uint8_t targetCount;
    std::uint32_t opponentScore;
};

struct Pickup {
    float distance;
    std::uint8_t lane;
    Slot slot;
    Style style;
    bool obstacle;
};

struct Input {
    int laneDelta = 0;
    bool start = false;
    bool advance = false;
    bool restart = false;
};

struct Snapshot {
    Phase phase = Phase::Menu;
    std::uint32_t level = 1;
    std::uint32_t unlockedLevel = 1;
    float distance = 0;
    std::uint8_t lane = 1;
    std::uint32_t styleScore = 0;
    std::uint32_t battleScore = 0;
    std::uint32_t opponentScore = 0;
    std::uint32_t collected = 0;
    std::uint32_t avoided = 0;
    std::uint32_t obstacleHits = 0;
    float battleSeconds = 0;
    std::array<Style, static_cast<std::size_t>(Slot::Count)> outfit{};
    std::array<bool, static_cast<std::size_t>(Slot::Count)> equipped{};
    bool won = false;
};

class Game final {
public:
    static constexpr std::uint32_t kLevelCount = 8;
    static constexpr float kRunDistance = 100.0f;
    static constexpr std::uint32_t kMaxPickups = 32;
    static constexpr float kRunSpeed = 12.5f;
    static constexpr float kBattleDuration = 2.5f;

    bool start(std::uint32_t level);
    bool update(const Input& input, float deltaSeconds);
    void restart();

    const Snapshot& snapshot() const { return state_; }

    const LevelDesc& levelDesc() const;

    const Pickup* pickups() const { return pickups_.data(); }

    const bool* resolvedPickups() const { return resolved_.data(); }

    std::uint32_t pickupCount() const { return pickupCount_; }

private:
    void beginLevel(std::uint32_t level);
    void resolveBattle();

    Snapshot state_;
    std::array<Pickup, kMaxPickups> pickups_{};
    std::array<bool, kMaxPickups> resolved_{};
    std::uint32_t pickupCount_ = 0;
};

const char* styleName(Style style);
const char* phaseName(Phase phase);

} // namespace arp::build_a_queen
