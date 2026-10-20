#include "game.h"

#include <cassert>
#include <cmath>

using arp::build_a_queen::Game;
using arp::build_a_queen::Input;
using arp::build_a_queen::Phase;
using arp::build_a_queen::Slot;
using arp::build_a_queen::Style;

int main()
{
    Game game;
    assert(game.snapshot().phase == Phase::Menu);
    assert(game.snapshot().unlockedLevel == 1);
    assert(!game.start(2));
    assert(game.start(1));

    Input input;
    input.laneDelta = -8;
    assert(game.update(input, 0.0f));
    assert(game.snapshot().lane == 0);
    input.laneDelta = 8;
    assert(game.update(input, 0.0f));
    assert(game.snapshot().lane == 2);
    assert(!game.update({}, std::nanf("")));

    const auto* pickups = game.pickups();
    assert(game.pickupCount() == 20);
    assert(pickups[0].distance == pickups[1].distance);
    assert(!game.resolvedPickups()[0]);
    assert(game.update({}, 0.25f));
    assert(game.snapshot().distance == 3.125f);
    assert(!game.resolvedPickups()[0]);
    input = {};
    for (std::uint32_t frame = 1; frame < 32; ++frame)
        assert(game.update(input, 0.25f));
    assert(game.snapshot().phase == Phase::RunwayBattle);
    std::uint32_t matched = 0;
    for (std::size_t slot = 0; slot < game.snapshot().equipped.size(); ++slot)
        matched += game.snapshot().equipped[slot]
            && game.snapshot().outfit[slot] == game.levelDesc().target[slot];
    assert(game.snapshot().battleScore
        == game.snapshot().styleScore + matched * 18 + game.snapshot().avoided * 4);
    for (std::uint32_t frame = 0; frame < 10; ++frame)
        assert(game.update({}, 0.25f));
    assert(game.snapshot().phase == Phase::Result);
    assert(game.snapshot().unlockedLevel == 2);
    assert(!game.start(3));

    input.advance = true;
    assert(game.update(input, 0.0f));
    assert(game.snapshot().phase == Phase::Running);
    assert(game.snapshot().level == 2);
    input = {};
    input.restart = true;
    assert(game.update(input, 0.0f));
    assert(game.snapshot().phase == Phase::Menu);
    assert(game.snapshot().unlockedLevel == 2);

    Game outfitGame;
    assert(outfitGame.start(1));
    for (std::uint32_t frame = 0; frame < 6; ++frame)
        assert(outfitGame.update({}, 0.25f));
    assert(outfitGame.update({}, 0.25f));
    assert(outfitGame.snapshot().equipped[static_cast<std::size_t>(Slot::Top)]);
    assert(outfitGame.snapshot().outfit[static_cast<std::size_t>(Slot::Top)] == Style::Ocean);
    for (std::uint32_t frame = 0; frame < 2; ++frame)
        assert(outfitGame.update({}, 0.25f));
    const auto& outfit = outfitGame.snapshot();
    assert(outfit.equipped[static_cast<std::size_t>(Slot::Top)]);
    assert(outfit.equipped[static_cast<std::size_t>(Slot::Bottom)]);
    assert(outfit.outfit[static_cast<std::size_t>(Slot::Bottom)] == Style::Gold);
    assert(outfitGame.resolvedPickups()[2]);
    assert(!outfit.equipped[static_cast<std::size_t>(Slot::Hair)]);
    assert(!outfit.equipped[static_cast<std::size_t>(Slot::Shoes)]);
    assert(!outfit.equipped[static_cast<std::size_t>(Slot::Accessory)]);
    return 0;
}
