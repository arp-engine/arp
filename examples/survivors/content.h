#pragma once
#include "2d/scene_component_factory.h"
#include "2d/tile_map_runtime.h"
#include "audio/audio_importer.h"
#include "core/yaml_contract.h"
#include "resource/audio_clip_resource.h"
#include "scene/scene_asset_resource.h"
#include "scriptvm/aot/aot_v5.h"
#include "scriptvm/runtime/module_registry.h"

#include <array>

namespace arp::survivors {
struct GameConfig {
    std::uint32_t duration = 0, bossSecond = 0, maxEnemies = 0, maxProjectiles = 0, maxPickups = 0,
                  maxParticles = 0, maxDrawItems = 0, maxDamageNumbers = 0;
    std::uint32_t maxResidentChunks = 0, maxTileCells = 0, spawnPerSecond = 0, spawnRampSeconds = 0,
                  xpPerUpgrade = 0;
    std::uint32_t stressEnemies = 0, stressProjectiles = 0, stressPickups = 0, warmup = 0,
                  sample = 0, maxFrameSamples = 0;
    std::size_t maxSourceBytes = 0, maxTextureBytes = 0;
    float health = 0, speed = 0, enemyHealth = 0, enemySpeed = 0, enemyDamage = 0, bossHealth = 0,
          bossSpeed = 0;
    float spawnRadius = 0, pickupRadius = 0, ppu = 0, projectileSpeed = 0, projectileLife = 0,
          orbitRadius = 0, pulseRadius = 0;
    std::uint32_t maxBoltShots = 0, boltPierceEveryLevels = 0, maxBoltPierces = 0,
                  boltBounceEveryLevels = 0, maxBoltBounces = 0;
    std::uint32_t baseOrbitBlades = 0, maxOrbitBlades = 0, pulseRadiusPercentPerLevel = 0,
                  maxPulseRadiusPercent = 0;
    float projectileSpreadDegrees = 0, projectileHomingDelay = 0;
    scriptvm::AotExecutionBudgetV5 executionBudget;
    physics2d::WorldConfig physics;
    scene2d::Scene2DComponentBudget componentBudget;
    std::uint32_t maxSceneObjects = 0, maxSceneComponents = 0, maxScriptInstances = 0;
    bool load(const core::pmr::String& root, core::pmr::String& diagnostic);
};

class Rules final {
public:
    bool load(const core::pmr::String& root,
        std::size_t maxSourceBytes,
        core::pmr::String& diagnostic);

    const auto& diagnostic() const { return diagnostic_; }

    const auto& source() const { return source_; }

    const auto& configYaml() const { return configYaml_; }

    void appendSource(const core::pmr::String& source)
    {
        source_ += "\n";
        source_ += source;
    }

    void useInterpreter(bool value) { interpreter_ = value; }

    bool interpreter() const { return interpreter_; }

private:
    core::pmr::String diagnostic_;
    core::pmr::String source_, configYaml_;
    bool interpreter_ = true;
};

struct NamedSprite {
    core::pmr::String name;
    resource::ResourceHandle<scene2d::SpriteResource> handle;
};

struct NamedAnimation {
    core::pmr::String name;
    resource::ResourceHandle<scene2d::SpriteAnimationResource> handle;
};

class Content final {
public:
    Content(resource::ResourceManager& resources) : resources_(resources) {}

    bool load(const core::pmr::String& root, const GameConfig&, core::pmr::String& diagnostic);
    resource::ResourceHandle<scene2d::SpriteResource> sprite(std::string_view) const;
    resource::ResourceHandle<scene2d::SpriteAnimationResource> animation(std::string_view) const;
    scriptvm::Value spriteIds() const;
    scriptvm::Value animationIds() const;
    scriptvm::Value soundIds() const;
    core::SharedPtr<const core::pmr::Vector<renderer::ui::SpriteGlyph>> font() const;
    resource::ResourceHandle<scene2d::SpriteAtlasResource> atlas;
    resource::ResourceHandle<scene2d::TileMapResource> map;
    resource::ResourceHandle<scene::ScenePrefabResource> orbitPrefab;
    std::array<resource::ResourceHandle<resource::AudioClipResource>, 4> sounds;

private:
    resource::ResourceManager& resources_;
    core::pmr::Vector<NamedSprite> sprites_;
    core::pmr::Vector<NamedAnimation> animations_;
};
} // namespace arp::survivors
