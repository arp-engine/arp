#include "game.h"

#include <algorithm>
#include <cmath>

namespace arp::build_a_queen {
namespace {
constexpr std::uint32_t kMatchingPickupScore = 20;
constexpr std::uint32_t kConflictingPickupScore = 8;
constexpr std::uint32_t kObstacleHitPenalty = 16;
constexpr std::uint32_t kObstacleAvoidScore = 4;
constexpr std::uint32_t kCorrectSlotBonus = 18;

constexpr std::array<LevelDesc, Game::kLevelCount> kLevels{{
    {Style::Rose, {Style::Rose, Style::Rose, Style::Gold, Style::Rose, Style::Gold}, 5, 48},
    {Style::Ocean, {Style::Ocean, Style::Ocean, Style::Ocean, Style::Gold, Style::Ocean}, 5, 56},
    {Style::Lime, {Style::Lime, Style::Gold, Style::Lime, Style::Lime, Style::Lime}, 5, 62},
    {Style::Violet,
        {Style::Violet, Style::Violet, Style::Gold, Style::Violet, Style::Violet},
        5,
        68},
    {Style::Gold, {Style::Gold, Style::Rose, Style::Gold, Style::Gold, Style::Gold}, 5, 74},
    {Style::Ocean, {Style::Ocean, Style::Gold, Style::Ocean, Style::Ocean, Style::Gold}, 5, 80},
    {Style::Violet,
        {Style::Violet, Style::Violet, Style::Violet, Style::Gold, Style::Violet},
        5,
        86},
    {Style::Gold, {Style::Gold, Style::Gold, Style::Gold, Style::Gold, Style::Gold}, 5, 92},
}};

constexpr std::array<Pickup, 16> kCourse{{
    {10, 0, Slot::Hair, Style::Rose, false},
    {10, 2, Slot::Hair, Style::Violet, false},
    {19, 1, Slot::Top, Style::Ocean, false},
    {19, 2, Slot::Top, Style::Rose, false},
    {28, 0, Slot::Bottom, Style::Lime, false},
    {28, 1, Slot::Bottom, Style::Gold, false},
    {37, 1, Slot::Shoes, Style::Gold, false},
    {37, 2, Slot::Shoes, Style::Violet, false},
    {46, 0, Slot::Accessory, Style::Rose, false},
    {46, 2, Slot::Accessory, Style::Gold, false},
    {55, 1, Slot::Hair, Style::Violet, false},
    {55, 2, Slot::Hair, Style::Ocean, false},
    {64, 0, Slot::Top, Style::Gold, false},
    {64, 1, Slot::Top, Style::Lime, false},
    {73, 0, Slot::Bottom, Style::Ocean, false},
    {73, 2, Slot::Bottom, Style::Violet, false},
}};
constexpr std::array<Pickup, 4> kObstacles{{
    {15, 1, Slot::Hair, Style::Rose, true},
    {33, 0, Slot::Top, Style::Rose, true},
    {60, 2, Slot::Shoes, Style::Rose, true},
    {82, 1, Slot::Accessory, Style::Rose, true},
}};
} // namespace

bool Game::start(std::uint32_t level)
{
    if (level == 0 || level > state_.unlockedLevel || level > kLevelCount)
        return false;
    beginLevel(level);
    return true;
}

bool Game::update(const Input& input, float deltaSeconds)
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0 || deltaSeconds > 0.25f)
        return false;
    if (input.restart) {
        restart();
        return true;
    }
    if (state_.phase == Phase::Menu) {
        if (input.start)
            return start(state_.unlockedLevel);
        return true;
    }
    if (state_.phase == Phase::Result) {
        if (input.advance) {
            if (state_.won && state_.level < kLevelCount)
                beginLevel(state_.level + 1);
            else
                state_.phase = Phase::Menu;
        }
        return true;
    }
    if (state_.phase == Phase::RunwayBattle) {
        state_.battleSeconds = std::min(kBattleDuration, state_.battleSeconds + deltaSeconds);
        if (state_.battleSeconds >= kBattleDuration) {
            state_.phase = Phase::Result;
            if (state_.won)
                state_.unlockedLevel =
                    std::max(state_.unlockedLevel, std::min(kLevelCount, state_.level + 1));
        }
        return true;
    }

    state_.lane = static_cast<std::uint8_t>(std::clamp(int(state_.lane) + input.laneDelta, 0, 2));
    const auto previous = state_.distance;
    state_.distance = std::min(kRunDistance, state_.distance + deltaSeconds * kRunSpeed);
    for (std::uint32_t i = 0; i < pickupCount_; ++i) {
        if (resolved_[i] || pickups_[i].distance < previous
            || pickups_[i].distance > state_.distance)
            continue;
        resolved_[i] = true;
        const auto& pickup = pickups_[i];
        if (pickup.lane != state_.lane) {
            if (pickup.obstacle)
                ++state_.avoided;
            continue;
        }
        if (pickup.obstacle) {
            ++state_.obstacleHits;
            state_.styleScore = state_.styleScore > kObstacleHitPenalty
                ? state_.styleScore - kObstacleHitPenalty
                : 0;
            continue;
        }
        ++state_.collected;
        const auto slot = static_cast<std::size_t>(pickup.slot);
        state_.outfit[slot] = pickup.style;
        state_.equipped[slot] = true;
        const auto& level = levelDesc();
        const bool match = level.target[slot] == pickup.style;
        state_.styleScore += match ? kMatchingPickupScore : kConflictingPickupScore;
    }
    if (state_.distance >= kRunDistance)
        resolveBattle();
    return true;
}

void Game::restart()
{
    state_.phase = Phase::Menu;
    state_.level = state_.unlockedLevel;
    state_.distance = 0;
    state_.lane = 1;
    state_.styleScore = state_.battleScore = state_.opponentScore = 0;
    state_.collected = state_.avoided = 0;
    state_.obstacleHits = 0;
    state_.battleSeconds = 0;
    state_.equipped.fill(false);
    state_.won = false;
}

const LevelDesc& Game::levelDesc() const
{
    return kLevels[std::clamp(state_.level, 1u, kLevelCount) - 1];
}

void Game::beginLevel(std::uint32_t level)
{
    const auto unlocked = state_.unlockedLevel;
    state_ = Snapshot{};
    state_.phase = Phase::Running;
    state_.level = level;
    state_.unlockedLevel = std::max(unlocked, level);
    state_.lane = 1;
    pickupCount_ = 0;
    resolved_.fill(false);
    for (const auto& item : kCourse)
        pickups_[pickupCount_++] = item;
    for (const auto& item : kObstacles)
        pickups_[pickupCount_++] = item;
}

void Game::resolveBattle()
{
    state_.phase = Phase::RunwayBattle;
    state_.opponentScore = levelDesc().opponentScore;
    std::uint32_t matched = 0;
    for (std::size_t slot = 0; slot < state_.equipped.size(); ++slot)
        matched += state_.equipped[slot] && state_.outfit[slot] == levelDesc().target[slot];
    state_.battleScore =
        state_.styleScore + matched * kCorrectSlotBonus + state_.avoided * kObstacleAvoidScore;
    state_.won = state_.battleScore >= state_.opponentScore;
    state_.phase = Phase::RunwayBattle;
}

const char* styleName(Style style)
{
    constexpr const char* names[] = {"ROSE", "OCEAN", "LIME", "VIOLET", "GOLD"};
    return names[static_cast<std::size_t>(style)];
}

const char* phaseName(Phase phase)
{
    constexpr const char* names[] = {"MENU", "RUNNING", "RUNWAY BATTLE", "RESULT"};
    return names[static_cast<std::size_t>(phase)];
}

} // namespace arp::build_a_queen
