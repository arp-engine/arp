#include "game.h"

#include <cassert>
#include <cstdio>
using namespace arp;

int main()
{
    core::pmr::String root = ARP_SOURCE_DIR "/examples/survivors/assets", diagnostic;
    survivors::GameConfig config;
    survivors::Rules rules;
    assert(config.load(root, diagnostic)
        && rules.load(root, config.maxSourceBytes, diagnostic));
    const auto flowPosition = rules.source().find("class RunFlow(ScriptComponent):");
    const auto combatPosition = rules.source().find("class PlayerController(ScriptComponent):");
    const auto hudPosition = rules.source().find("class Hud(ScriptComponent):");
    const auto entryPosition = rules.source().find("def initialize(");
    assert(flowPosition < combatPosition && combatPosition < hudPosition
        && hudPosition < entryPosition);
    resource::ResourceManager resources;
    survivors::Content content(resources);
    assert(content.load(root, config, diagnostic));
    std::uint64_t expected = 0;
    for (int backend = 0; backend < 2; ++backend) {
        rules.useInterpreter(backend == 1);
        survivors::GameRun game(resources, content, rules, config, 42, nullptr);
        if (!game.valid()) {
            std::fprintf(stderr, "%s\n", game.diagnostic().c_str());
            return 1;
        }
        assert(game.setRecord({100, 200, 2, 5, .5}));
        scene2d::SpriteDrawPlan drawPlan(config.maxDrawItems);
        auto validateDraw = [&]() {
            assert(game.extract({}, game.interpolation()));
            assert(drawPlan.build(resources, game.items()));
        };
        validateDraw(); // Menu repeats the help text at two distinct placements.
        for (int tick = 0; tick < 180; ++tick) {
            if (!game.update({}, 1. / 60, true)) {
                std::fprintf(stderr, "%s\n", game.diagnostic().c_str());
                return 1;
            }
            assert(game.extract({}, game.interpolation()));
        }
        if (!backend)
            expected = game.checksum();
        else
            assert(expected == game.checksum());
        auto seconds = game.telemetry().seconds;
        assert(game.update({{}, false, true}, 2) && game.telemetry().seconds == seconds);
        validateDraw();
        assert(game.update({{}, false, true}, 0));
        assert(game.grantExperience(config.xpPerUpgrade) && game.update({}, 0));
        validateDraw();
        survivors::RunInput choose;
        choose.upgrade = 0;
        assert(game.update(choose, 0));
        game.forceDefeat();
        assert(game.update({}, 1. / 60));
        validateDraw();
        assert(game.telemetry().state == survivors::RunState::Defeat && game.recordRevision() == 1);
        assert(game.record().bestKills >= 100 && game.record().bestSeconds >= 200
            && game.record().wins == 2);
        assert(game.update({}, 0) && game.recordRevision() == 1);
        survivors::RunInput input;
        input.volumeDelta = 1;
        input.restart = true;
        assert(game.update(input, 0) && game.recordRevision() == 2 && game.restartRequested());
        assert(game.record().volume == .6);
    }
    // AOT never silently interprets modified source.
    rules.useInterpreter(false);
    rules.appendSource("\n# modified package\n");
    survivors::GameRun mismatch(resources, content, rules, config, 42, nullptr);
    assert(!mismatch.valid()
        && mismatch.diagnostic().find("SOURCE-MISMATCH") != core::pmr::String::npos);
    std::puts(
        "PASS AOT/Interpreter seed parity, script settlement once, volume/restart policy and source identity gate");
}
