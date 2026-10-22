#include "core/io/file_io.h"
#include "game.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace arp::survivors {
// Test setup uses real attached components and real PhysicsWorld2D casts.
// No mocked collision result or native success is used.
struct GameRunTestAccess {
    struct Handle {
        int ticket = -1, index = 0, generation = 0;

        explicit operator bool() const { return ticket >= 0; }
    };

    static void checked(GameRun& game, bool ok)
    {
        if (!ok)
            std::fprintf(stderr, "script: %s\n", game.diagnostic().c_str());
        assert(ok);
    }

    static Handle enemy(GameRun& game, core::Vec2 position)
    {
        checked(game,
            game.call("spawn_enemy",
                {scriptvm::makeInt(0),
                    scriptvm::makeBool(false),
                    scriptvm::makeFloat(position.x),
                    scriptvm::makeFloat(position.y)}));
        int id = game.result_.cast<int>();
        assert(id >= 0);
        return {id};
    }

    static float health(GameRun& game, Handle enemy)
    {
        checked(game, game.call("test_enemy_health", {scriptvm::makeInt(enemy.ticket)}));
        return game.result_.cast<double>();
    }

    static Handle bolt(GameRun& game, std::uint32_t pierces, std::uint32_t bounces)
    {
        checked(
            game, game.call("test_bolt", {scriptvm::makeInt(pierces), scriptvm::makeInt(bounces)}));
        return {game.result_.cast<int>()};
    }

    static bool advanceBolts(GameRun& game, float dt)
    {
        return game.call("update_projectiles", {scriptvm::makeFloat(dt)}) && game.refresh();
    }

    static bool fire(GameRun& game, std::uint32_t kind)
    {
        return game.call("fire", {scriptvm::makeInt(kind)}) && game.refresh();
    }

    static bool distinctTargets(GameRun& game)
    {
        checked(game, game.call("test_distinct"));
        return game.result_.cast<bool>();
    }

    static std::uint32_t hits(GameRun& game, Handle bolt)
    {
        checked(game, game.call("test_hits", {scriptvm::makeInt(bolt.ticket)}));
        return game.result_.cast<int>();
    }

    static void removeEnemy(GameRun& game, Handle h)
    {
        checked(game, game.call("test_remove_enemy", {scriptvm::makeInt(h.ticket)}));
    }

    static void diagnoseEnemy(GameRun& game, Handle h)
    {
        auto* object = game.scene_->findObject(scene::GameObjectId::fromValue(h.ticket));
        assert(object);
        const auto* body = object->getComponent<physics2d::RigidBody2DComponent>();
        const auto* collider = object->getComponent<physics2d::Collider2DComponent>();
        assert(body && collider);
        auto state = body->body_state();
        const auto& filter = collider->desc().filter;
        std::fprintf(stderr,
            "enemy %d pose %.3f/%.3f category %llu mask %llu\n",
            h.ticket,
            state.position.x,
            state.position.y,
            (unsigned long long)filter.category,
            (unsigned long long)filter.mask);
    }
};
} // namespace arp::survivors

int main(int argc, char** argv)
{
    using namespace arp;
    const core::pmr::String root = ARP_SOURCE_DIR "/examples/survivors/assets";
    survivors::GameConfig config;
    core::pmr::String diagnostic;
    const survivors::Record record{123, 300, 2, 9, .3};
    const auto encoded = survivors::RecordSchema::encode(record);
    const auto restored = survivors::RecordSchema::decode({encoded.begin(), encoded.end()});
    assert(restored.bestKills == 123 && restored.bestSeconds == 300 && restored.wins == 2
        && restored.unlockPoints == 9 && restored.volume == .3);
    assert(config.load(root, diagnostic));
    survivors::Rules rules;
    if (!rules.load(root, config.maxSourceBytes, diagnostic)) {
        std::fprintf(stderr, "rules: %s\n", diagnostic.c_str());
        return 1;
    }
    rules.useInterpreter(true);
    auto fixture = core::readTextFile(
        std::filesystem::path(ARP_SOURCE_DIR "/tests/fixtures/survivors_test.arp"), 65536);
    assert(fixture.success);
    rules.appendSource(fixture.text);
    resource::ResourceManager resources;
    survivors::Content content(resources);
    if (!content.load(root, config, diagnostic)) {
        std::fprintf(stderr, "content: %s\n", diagnostic.c_str());
        return 1;
    }
    const bool fullMemoryCheck = argc > 1 && std::strcmp(argv[1], "--memory-full-check") == 0;
    if (fullMemoryCheck || (argc > 1 && std::strcmp(argv[1], "--memory-check") == 0)) {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        // Compare after full world destruction, retaining only the same shared
        // content and process metadata. Warm the same workload before sampling.
        std::uint64_t baselineBytes = 0, baselineAllocations = 0;
        const int warmups = fullMemoryCheck ? 1 : 2;
        const int iterations = fullMemoryCheck ? 2 : 22;
        for (int iteration = 1; iteration <= iterations; ++iteration) {
            {
                survivors::GameRun game(resources, content, rules, config, 42, nullptr);
                assert(game.valid());
                for (int tick = 0; tick < (fullMemoryCheck ? 24000 : 180); ++tick) {
                    assert(game.update({}, 1. / 60, true));
                    assert(game.extract({}, game.interpolation()));
                    if (fullMemoryCheck && tick % 3600 == 0)
                        std::printf("fullMemoryProgress: { iteration: %d, seconds: %.3f }\n",
                            iteration,
                            game.telemetry().seconds);
                    if (fullMemoryCheck && game.telemetry().state == survivors::RunState::Victory)
                        break;
                }
                if (fullMemoryCheck) {
                    const auto t = game.telemetry();
                    assert(t.state == survivors::RunState::Victory && t.bossDefeated
                        && t.seconds >= config.duration);
                    std::printf(
                        "fullMemoryRun: { iteration: %d, seconds: %.3f, kills: %u, upgrades: %u, bossDefeated: true }\n",
                        iteration,
                        t.seconds,
                        t.kills,
                        t.upgrades);
                } else {
                    assert(game.telemetry().seconds >= 2.99);
                    assert(game.telemetry().enemies || game.telemetry().kills);
                    assert(game.grantExperience(config.xpPerUpgrade * 2));
                    assert(game.update({}, 1. / 60));
                    assert(game.telemetry().state == survivors::RunState::Upgrade);
                    const auto paused = game.telemetry().seconds;
                    assert(game.update({}, 1) && game.telemetry().seconds == paused);
                    assert(game.update({{}, false, false, 0}, 0));
                    assert(game.update({{}, false, false, 1}, 0));
                    assert(game.telemetry().upgrades >= 2);
                    game.forceDefeat();
                    assert(game.update({}, 1. / 60)
                        && game.telemetry().state == survivors::RunState::Defeat);
                }
            }
            const auto memory = core::defaultCpuAllocator().stats();
            const auto outstanding = memory.allocationCount - memory.deallocationCount;
            if (iteration == warmups) {
                baselineBytes = memory.liveBytes;
                baselineAllocations = outstanding;
            }
            if (iteration > warmups) {
                std::printf(
                    "disposedWorld: { iteration: %d, coreDelta: %lld, outstandingDelta: %lld }\n",
                    iteration,
                    static_cast<long long>(memory.liveBytes)
                        - static_cast<long long>(baselineBytes),
                    static_cast<long long>(outstanding)
                        - static_cast<long long>(baselineAllocations));
                std::fflush(stdout);
                assert(memory.liveBytes == baselineBytes && outstanding == baselineAllocations);
            }
        }
        std::puts(fullMemoryCheck
                ? "PASS: complete 300-second victory/Boss runs return to disposed-world memory baseline"
                : "PASS: 20 ordinary combat/upgrade/pause/death/disposal cycles return to warmed memory baseline");
        return 0;
    }
    {
        survivors::GameRun game(resources, content, rules, config, 42, nullptr);
        if (!game.valid()) {
            std::fprintf(stderr, "run: %s\n", game.diagnostic().c_str());
            return 1;
        }
        assert(game.update({{}, true}, 1.0 / 60));
        const auto before = game.telemetry().seconds;
        assert(game.update({{}, false, true}, 10));
        assert(game.telemetry().state == survivors::RunState::Paused
            && game.telemetry().seconds == before);
        assert(game.update({{}, false, true}, 0));
        assert(game.grantExperience(config.xpPerUpgrade * 2));
        assert(game.update({}, 1.0 / 60) && game.telemetry().pendingUpgrades == 2
            && game.telemetry().state == survivors::RunState::Upgrade);
        const auto paused = game.telemetry().seconds;
        assert(game.update({}, 10) && game.telemetry().seconds == paused);
        assert(game.update({{}, false, false, 0}, 0) && game.telemetry().pendingUpgrades == 1
            && game.telemetry().upgrades == 1);
        assert(game.update({{}, false, false, 1}, 0) && game.telemetry().pendingUpgrades == 0
            && game.telemetry().upgrades == 2);
        assert(game.weapons().boltShots == 2 && game.weapons().orbitBlades == 4
            && game.weapons().pulseRadiusPercent == 100);
        assert(game.extract({}, 1));
        scene2d::SpriteDrawPlan plan(config.maxDrawItems);
        if (!plan.build(resources, game.items())) {
            std::fprintf(stderr, "plan: %s\n", plan.diagnostic());
            return 1;
        }
        game.forceDefeat();
        assert(game.update({}, 1.0 / 60) && game.telemetry().state == survivors::RunState::Defeat);
    }
    const auto choose = [&](survivors::GameRun& game, int choice, std::uint32_t count) {
        assert(game.update({{}, true}, 0));
        assert(game.grantExperience(config.xpPerUpgrade * count));
        assert(game.update({}, 1. / 60) && game.telemetry().state == survivors::RunState::Upgrade);
        for (std::uint32_t i = 0; i < count; ++i)
            assert(game.update({{}, false, false, choice}, 0));
    };
    {
        survivors::GameRun game(resources, content, rules, config, 42, nullptr);
        assert(game.valid());
        choose(game, 0, 1);
        assert(game.weapons().boltShots == 2 && game.weapons().boltPierces == 0
            && game.weapons().boltBounces == 0);
        choose(game, 0, 1);
        assert(game.weapons().boltShots == 3 && game.weapons().boltPierces == 1
            && game.weapons().boltBounces == 0);
        choose(game, 0, 1);
        assert(game.weapons().boltShots == 4 && game.weapons().boltPierces == 1
            && game.weapons().boltBounces == 1);
        choose(game, 0, 17);
        choose(game, 1, 20);
        choose(game, 2, 20);
        const auto& w = game.weapons();
        assert(w.boltShots == 6 && w.boltPierces == 3 && w.boltBounces == 2 && w.orbitBlades == 8
            && w.pulseRadiusPercent == 220);
        assert(game.extract({}, 1));
        unsigned blades = 0;
        for (const auto& item : game.items())
            if (item.sprite.id() == content.sprite("tile_0104").id())
                ++blades;
        assert(blades == w.orbitBlades);
        using Access = survivors::GameRunTestAccess;
        Access::enemy(game, {5, -3});
        const auto before = game.telemetry().projectiles;
        assert(Access::fire(game, 0) && game.telemetry().projectiles == before + 6);
    }
    {
        auto combat = config;
        combat.spawnPerSecond = 0;
        combat.maxProjectiles = 2;
        combat.maxBoltShots = 2;
        survivors::GameRun game(resources, content, rules, combat, 42, nullptr);
        assert(game.valid());
        choose(game, 0, 1);
        using Access = survivors::GameRunTestAccess;
        Access::enemy(game, {4, -2});
        Access::enemy(game, {-4, -2});
        assert(Access::fire(game, 0) && game.telemetry().projectiles == 2
            && Access::distinctTargets(game));
        const auto rejected = game.telemetry().rejectedSpawns;
        assert(Access::fire(game, 0) && game.telemetry().projectiles == 2
            && game.telemetry().rejectedSpawns == rejected + 2);
    }
    {
        auto combat = config;
        combat.enemyHealth = 1000;
        survivors::GameRun game(resources, content, rules, combat, 42, nullptr);
        assert(game.valid());
        using Access = survivors::GameRunTestAccess;
        const auto a = Access::enemy(game, {2, 0}), b = Access::enemy(game, {3, 0}),
                   c = Access::enemy(game, {4, 0});
        Access::bolt(game, 1, 0);
        for (int tick = 0; tick < 120; ++tick)
            assert(Access::advanceBolts(game, 1.f / 60));
        const auto ah = Access::health(game, a), bh = Access::health(game, b),
                   ch = Access::health(game, c);
        if (ah != 990 || bh != 990 || ch != 1000) {
            std::fprintf(stderr, "piercing health: %.3f / %.3f / %.3f\n", ah, bh, ch);
            Access::diagnoseEnemy(game, a);
            Access::diagnoseEnemy(game, b);
            Access::diagnoseEnemy(game, c);
        }
        assert(ah == 990 && bh == 990 && ch == 1000);
        assert(game.telemetry().projectiles == 0);
        // A new projectile component starts with an empty history.
        Access::bolt(game, 0, 0);
        for (int tick = 0; tick < 60; ++tick)
            assert(Access::advanceBolts(game, 1.f / 60));
        assert(Access::health(game, a) == 980 && Access::health(game, b) == 990);
    }
    {
        auto combat = config;
        combat.enemyHealth = 1000;
        survivors::GameRun game(resources, content, rules, combat, 42, nullptr);
        assert(game.valid());
        using Access = survivors::GameRunTestAccess;
        const auto a = Access::enemy(game, {2, 0}), b = Access::enemy(game, {2, 2}),
                   c = Access::enemy(game, {4, 2});
        Access::bolt(game, 0, 1);
        for (int tick = 0; tick < 120; ++tick)
            assert(Access::advanceBolts(game, 1.f / 60));
        assert(Access::health(game, a) == 990 && Access::health(game, b) == 990
            && Access::health(game, c) == 1000);
        assert(game.telemetry().projectiles == 0);
    }
    {
        auto combat = config;
        combat.enemyHealth = 1000;
        survivors::GameRun game(resources, content, rules, combat, 42, nullptr);
        assert(game.valid());
        using Access = survivors::GameRunTestAccess;
        const auto a = Access::enemy(game, {2, 0}), b = Access::enemy(game, {3, 0}),
                   c = Access::enemy(game, {3, 2});
        Access::bolt(game, 1, 1);
        for (int tick = 0; tick < 120; ++tick)
            assert(Access::advanceBolts(game, 1.f / 60));
        assert(Access::health(game, a) == 990 && Access::health(game, b) == 990
            && Access::health(game, c) == 990);
        assert(game.telemetry().projectiles == 0);
    }
    {
        auto combat = config;
        combat.enemyHealth = 1000;
        survivors::GameRun game(resources, content, rules, combat, 42, nullptr);
        assert(game.valid());
        using Access = survivors::GameRunTestAccess;
        const auto only = Access::enemy(game, {2, 0});
        Access::bolt(game, 0, 2);
        for (int tick = 0; tick < 60; ++tick)
            assert(Access::advanceBolts(game, 1.f / 60));
        assert(Access::health(game, only) == 990 && game.telemetry().projectiles == 0);
    }
    {
        auto combat = config;
        combat.enemyHealth = 1000;
        survivors::GameRun game(resources, content, rules, combat, 42, nullptr);
        assert(game.valid());
        using Access = survivors::GameRunTestAccess;
        const auto a = Access::enemy(game, {2, 0});
        const auto projectile = Access::bolt(game, 3, 0);
        for (int tick = 0; tick < 15; ++tick)
            assert(Access::advanceBolts(game, 1.f / 60));
        assert(Access::hits(game, projectile) == 1 && Access::health(game, a) == 990);
        Access::removeEnemy(game, a);
        const auto replacement = Access::enemy(game, {3, 0});
        assert(replacement.ticket != a.ticket);
        for (int tick = 0; tick < 15; ++tick)
            assert(Access::advanceBolts(game, 1.f / 60));
        assert(Access::hits(game, projectile) == 2 && Access::health(game, replacement) == 990);
    }
    {
        auto combat = config;
        combat.enemyHealth = 1000;
        survivors::GameRun game(resources, content, rules, combat, 42, nullptr);
        assert(game.valid());
        using Access = survivors::GameRunTestAccess;
        const auto distant = Access::enemy(game, {4.4f, 0});
        assert(Access::fire(game, 2) && Access::health(game, distant) == 1000);
        choose(game, 2, 2);
        assert(game.weapons().pulseRadiusPercent == 130);
        assert(Access::fire(game, 2) && Access::health(game, distant) < 1000);
        // The HUD observes fixed-step pulse progress on the next frame update.
        assert(game.update({}, 1. / 60) && game.update({}, 1. / 60) && game.extract({}, 1));
        bool pulse = false;
        for (const auto& item : game.items())
            if (item.sprite.id() == content.sprite("pulse").id()) {
                pulse = true;
                assert(item.world.scale.x > config.pulseRadius * 2 * (1.f / 60 / .35f));
            }
        assert(pulse);
    }

    std::uint64_t replayChecksum = 0;
    for (int replay = 0; replay < 2; ++replay) {
        survivors::GameRun game(resources, content, rules, config, 42, nullptr);
        assert(game.valid());
        scene2d::SpriteDrawPlan plan(config.maxDrawItems);
        for (int tick = 0; tick < 180; ++tick) {
            assert(game.update({}, 1. / 60, true));
            assert(game.extract({}, game.interpolation()) && plan.build(resources, game.items()));
        }
        if (!replay)
            replayChecksum = game.checksum();
        else
            assert(replayChecksum == game.checksum());
    }
    if (argc > 1 && std::strcmp(argv[1], "--full") == 0) {
        survivors::GameRun game(resources, content, rules, config, 42, nullptr);
        assert(game.valid());
        for (int i = 0; i < 24000; ++i) {
            if (!game.update({}, 1.0 / 60, true)) {
                std::fprintf(stderr, "tick %d: %s\n", i, game.diagnostic().c_str());
                return 1;
            }
            const auto t = game.telemetry();
            if (i % 3600 == 0)
                std::printf("t=%.2f hp=%.1f enemies=%u kills=%u upgrades=%u\n",
                    t.seconds,
                    t.health,
                    t.enemies,
                    t.kills,
                    t.upgrades);
            if (t.state == survivors::RunState::Victory) {
                assert(game.record().wins == 1 && game.recordRevision() == 1);
                std::printf("PASS full run: %.2fs kills=%u upgrades=%u boss=%d\n",
                    t.seconds,
                    t.kills,
                    t.upgrades,
                    t.bossDefeated);
                return 0;
            }
            if (t.state == survivors::RunState::Defeat) {
                std::fprintf(
                    stderr, "DEFEAT at %.2f hp=%.1f kills=%u\n", t.seconds, t.health, t.kills);
                return 1;
            }
        }
        return 1;
    }
    std::puts(
        "PASS: real physics multishot/piercing/ricochet/generation history, blade/radius progression and caps, pause, seed replay and disposal");
}
