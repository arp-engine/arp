#pragma once
#include "2d/scene_component_factory.h"
#include "2d/scene_sprite_extractor.h"
#include "content.h"
#include "core/math/seeded_random.h"
#include "core/time/game_clock.h"
#include "records.h"
#include "scene/script_component_module.h"
#include "scriptvm/plugin/plugin_runtime_scope.h"

namespace arp::survivors {
enum class RunState { Menu, Fighting, Upgrade, Paused, Victory, Defeat };

struct RunInput {
    core::Vec2 movement;
    bool start = false, pause = false;
    int upgrade = -1;
    bool restart = false;
    int volumeDelta = 0;
};

struct RunTelemetry {
    RunState state = RunState::Menu;
    double seconds = 0;
    float health = 0;
    std::uint32_t enemies = 0, projectiles = 0, pickups = 0, kills = 0, upgrades = 0,
                  pendingUpgrades = 0;
    bool bossSpawned = false, bossDefeated = false;
    std::uint64_t rejectedSpawns = 0;
    double catchupDebtSeconds = 0;
};

struct WeaponLoadout {
    std::uint32_t boltShots = 1, boltPierces = 0, boltBounces = 0, orbitBlades = 0,
                  pulseRadiusPercent = 100;
    float pulseRadius = 0;
};

struct RunLiveCounts {
    std::size_t objects = 0, components = 0, scripts = 0;

    bool operator==(const RunLiveCounts& other) const
    {
        return objects == other.objects && components == other.components
            && scripts == other.scripts;
    }
};

// Platform host schedules Scene and native systems; attached script components own gameplay.
class GameRun final {
public:
    GameRun(resource::ResourceManager&,
        Content&,
        Rules&,
        const GameConfig&,
        std::uint64_t,
        audio::AudioSystem*);

    ~GameRun()
    {
        scene_.reset();
        clockReference_.invalidate_native_reference();
        randomReference_.invalidate_native_reference();
    }

    bool valid() const
    {
        return diagnostic_.empty() && world_ && world_->valid() && !state_.is_undefined();
    }

    bool update(const RunInput&, double, bool autopilot = false);
    bool extract(core::Vec2, float);
    bool populateStress();

    RunTelemetry telemetry() const { return telemetry_; }

    const auto& items() const { return items_; }

    const auto& diagnostic() const { return diagnostic_; }

    core::Vec2 playerPosition() const { return player_; }

    float interpolation() const { return interpolation_; }

    const physics2d::PhysicsWorld2D& physics() const { return *world_; }

    void forceDefeat();
    bool grantExperience(std::uint32_t);

    std::uint64_t checksum()
    {
        if (!call("world_checksum"))
            return 0;
        return std::uint64_t(result_.cast<int>());
    }

    bool setRecord(const Record&);

    const Record& record() const { return record_; }

    int recordRevision() const { return recordRevision_; }

    bool restartRequested() const { return restartRequested_; }

    const WeaponLoadout& weapons() const { return weapons_; }

    RunLiveCounts liveCounts() const
    {
        RunLiveCounts counts{
            scene_->objects().size(), scene_->componentIds().size(), module_.liveInstances()};
        for (const auto& object : scene_->objects())
            counts.components += object->componentIds().size();
        return counts;
    }

private:
    friend struct GameRunTestAccess;
    bool call(const char*, std::initializer_list<scriptvm::Value> args = {});
    bool refresh();
    bool hostUpdate(const RunInput&);
    core::SharedPtr<physics2d::PhysicsWorld2D> world_;
    scene2d::TileMapRuntime tiles_;
    core::SeededRandom random_;
    scene::MainSceneRegistry scenes_;
    scriptvm::ScriptPluginRuntimeScope scope_;
    scriptvm::ScriptComponentModule module_;
    core::SharedPtr<scene::SceneComponentFactory> factory_;
    scene2d::SceneSpriteExtractor extractor_;
    std::uint32_t objectBudget_ = 0, componentBudget_ = 0, drawBudget_ = 0;
    core::pmr::Vector<scene2d::SpriteDrawItem> items_;
    core::pmr::Vector<scene::ScriptPhysicsEvent> physicsEvents_;
    scriptvm::Value state_, result_;
    core::pmr::Vector<scriptvm::Value> arguments_;
    core::GameClock clock_{1.0 / 60, 4};
    float interpolation_ = 1;
    scriptvm::Value clockReference_, randomReference_;
    RunTelemetry telemetry_;
    WeaponLoadout weapons_;
    core::Vec2 player_;
    Record record_;
    int recordRevision_ = 0;
    bool restartRequested_ = false;
    core::pmr::String diagnostic_;
    // Scene destroys attached components before their services and module.
    core::UniquePtr<scene::Scene> scene_;
};
} // namespace arp::survivors
