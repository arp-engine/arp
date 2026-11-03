#pragma once

#include "2d/physics_components.h"
#include "2d/scene_component_factory.h"
#include "scriptvm/aot/aot_v5.h"

namespace arp::survivors {

// Explicit cold-start admission profile. Content semantics remain in YAML;
// these ceilings bound acquisition and never expand to accommodate input.
struct GameplayTextLimits {
    static constexpr std::size_t maxMetadataBytes = 65536;
    static constexpr std::size_t maxSourceBytes = 2 * 1024 * 1024;
    static constexpr std::size_t maxScripts = 32;
    static constexpr std::size_t maxPathBytes = 512;
    static constexpr std::size_t maxInputBytes = maxSourceBytes + 2 * maxMetadataBytes;
};

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
    bool decode(const core::pmr::String& yaml, core::pmr::String& diagnostic);
};

class Rules final {
public:
    bool load(
        const core::pmr::String& root, std::size_t maxSourceBytes, core::pmr::String& diagnostic);
    static bool sourcePaths(const core::pmr::String& manifest,
        core::pmr::Vector<core::pmr::String>& paths,
        core::pmr::String& diagnostic);
    // Pure composition; preserves BOM and CR/LF bytes and publishes only a
    // complete candidate. Source order is exactly the manifest sequence.
    bool prepare(const core::pmr::String& configYaml,
        const core::pmr::Vector<core::pmr::String>& parts,
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

} // namespace arp::survivors
