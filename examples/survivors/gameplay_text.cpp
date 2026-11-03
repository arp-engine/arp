#include "gameplay_text.h"

#include "core/encoding/yaml_contract.h"
#include "core/io/file_io.h"

namespace arp::survivors {
using Y = core::YamlContract;

bool GameConfig::load(const core::pmr::String& root, core::pmr::String& diagnostic)
{
    auto source = core::readTextFile(std::filesystem::path((root + "/game.yaml").c_str()), 65536);
    if (!source.success) {
        diagnostic = source.diagnostics;
        return false;
    }
    return decode(source.text, diagnostic);
}

bool GameConfig::decode(const core::pmr::String& yaml, core::pmr::String& diagnostic)
{
    try {
        auto n = Y::Node::deserialize(yaml);
        if (Y::text(n, "type") != "arp.survivors-config" || Y::uint32(n, "contractVersion") != 3)
            Y::fail();
        duration = Y::uint32(n, "durationSeconds");
        bossSecond = Y::uint32(n, "bossSecond");
        maxEnemies = Y::uint32(n, "maxEnemies");
        maxProjectiles = Y::uint32(n, "maxProjectiles");
        maxPickups = Y::uint32(n, "maxPickups");
        maxParticles = Y::uint32(n, "maxParticles");
        maxDrawItems = Y::uint32(n, "maxDrawItems");
        maxResidentChunks = Y::uint32(n, "maxResidentChunks");
        maxTileCells = Y::uint32(n, "maxTileCells");
        maxDamageNumbers = Y::uint32(n, "maxDamageNumbers");
        maxSourceBytes = Y::uint32(n, "maxSourceBytes");
        maxTextureBytes = Y::uint32(n, "maxTextureBytes");
        spawnPerSecond = Y::uint32(n, "spawnPerSecond");
        spawnRampSeconds = Y::uint32(n, "spawnRampSeconds");
        xpPerUpgrade = Y::uint32(n, "xpPerUpgrade");
        stressEnemies = Y::uint32(n, "stressEnemies");
        stressProjectiles = Y::uint32(n, "stressProjectiles");
        stressPickups = Y::uint32(n, "stressPickups");
        warmup = Y::uint32(n, "stressWarmupSeconds");
        sample = Y::uint32(n, "stressSampleSeconds");
        maxFrameSamples = Y::uint32(n, "maxFrameSamples");
        health = float(Y::number(n, "playerHealth"));
        speed = float(Y::number(n, "playerSpeed"));
        enemyHealth = float(Y::number(n, "enemyHealth"));
        enemySpeed = float(Y::number(n, "enemySpeed"));
        enemyDamage = float(Y::number(n, "enemyDamage"));
        bossHealth = float(Y::number(n, "bossHealth"));
        bossSpeed = float(Y::number(n, "bossSpeed"));
        spawnRadius = float(Y::number(n, "spawnRadius"));
        pickupRadius = float(Y::number(n, "pickupRadius"));
        ppu = float(Y::number(n, "pixelsPerUnit"));
        projectileSpeed = float(Y::number(n, "projectileSpeed"));
        projectileLife = float(Y::number(n, "projectileLifeSeconds"));
        orbitRadius = float(Y::number(n, "orbitRadius"));
        pulseRadius = float(Y::number(n, "pulseRadius"));
        maxBoltShots = Y::uint32(n, "maxBoltShots");
        boltPierceEveryLevels = Y::uint32(n, "boltPierceEveryLevels");
        maxBoltPierces = Y::uint32(n, "maxBoltPierces");
        boltBounceEveryLevels = Y::uint32(n, "boltBounceEveryLevels");
        maxBoltBounces = Y::uint32(n, "maxBoltBounces");
        baseOrbitBlades = Y::uint32(n, "baseOrbitBlades");
        maxOrbitBlades = Y::uint32(n, "maxOrbitBlades");
        pulseRadiusPercentPerLevel = Y::uint32(n, "pulseRadiusPercentPerLevel");
        maxPulseRadiusPercent = Y::uint32(n, "maxPulseRadiusPercent");
        projectileSpreadDegrees = float(Y::number(n, "projectileSpreadDegrees"));
        projectileHomingDelay = float(Y::number(n, "projectileHomingDelaySeconds"));
        physics = {{0, 0},
            Y::uint32(n, "maxBodies"),
            Y::uint32(n, "maxColliders"),
            Y::uint32(n, "maxJoints"),
            Y::uint32(n, "maxNativeShapes"),
            Y::uint32(n, "maxEvents"),
            Y::uint32(n, "maxQueryResults"),
            Y::uint32(n, "maxChainPoints"),
            Y::uint32(n, "physicsSubsteps"),
            1.f / 60.f};
        componentBudget = {Y::uint32(n, "animationEventsPerComponent"),
            Y::uint32(n, "maxSceneEvents"),
            Y::uint32(n, "glyphsPerComponent"),
            maxParticles};
        if (!duration || bossSecond >= duration || !maxEnemies || !maxProjectiles || !maxPickups
            || !maxParticles || !maxDrawItems || !spawnRampSeconds || !xpPerUpgrade
            || !maxFrameSamples || !maxDamageNumbers || health <= 0 || speed <= 0
            || enemyHealth <= 0 || enemySpeed <= 0 || enemyDamage <= 0 || bossHealth <= 0
            || spawnRadius <= 0 || pickupRadius <= 0 || ppu <= 0 || projectileSpeed <= 0
            || projectileLife <= 0 || orbitRadius <= 0 || pulseRadius <= 0
            || stressEnemies >= maxEnemies || stressProjectiles > maxProjectiles
            || stressPickups > maxPickups)
            Y::fail();
        // Explicit representability and pool budgets; no inferred resource expansion.
        if (!maxBoltShots || maxBoltShots > maxProjectiles || !boltPierceEveryLevels
            || !boltBounceEveryLevels || !baseOrbitBlades || baseOrbitBlades > maxOrbitBlades
            || maxOrbitBlades > maxDrawItems || maxOrbitBlades > INT32_MAX - 2
            || maxBoltShots > INT32_MAX || maxBoltPierces > maxEnemies
            || maxBoltBounces > maxEnemies || boltPierceEveryLevels > INT32_MAX
            || boltBounceEveryLevels > INT32_MAX || !pulseRadiusPercentPerLevel
            || maxPulseRadiusPercent < 100 || maxPulseRadiusPercent > INT32_MAX
            || pulseRadiusPercentPerLevel > INT32_MAX || maxBoltPierces > INT32_MAX
            || maxBoltBounces > INT32_MAX || !std::isfinite(projectileSpreadDegrees)
            || projectileSpreadDegrees < 0
            || double(projectileSpreadDegrees) * (maxBoltShots - 1) > 180
            || !std::isfinite(projectileHomingDelay) || projectileHomingDelay < 0
            || projectileHomingDelay >= projectileLife
            || std::uint64_t(maxBoltPierces) + maxBoltBounces + 1
                > SIZE_MAX / sizeof(core::PoolHandle) / maxProjectiles)
            Y::fail();
        maxSceneObjects = Y::uint32(n, "maxSceneObjects");
        maxSceneComponents = Y::uint32(n, "maxSceneComponents");
        maxScriptInstances = Y::uint32(n, "maxScriptInstances");
        if (!maxSceneObjects || !maxSceneComponents || !maxScriptInstances)
            Y::fail();
        const auto& b = componentBudget;
        if (!b.animationEvents || !b.timerEvents || !b.glyphs || !b.particles)
            Y::fail();
        executionBudget = {Y::uint64(n["scriptInstructionLimit"]),
            Y::uint32(n, "scriptRecursionLimit"),
            Y::uint32(n, "scriptFrameValues"),
            Y::uint32(n, "scriptContainerValues"),
            Y::uint32(n, "scriptCallArguments"),
            Y::uint32(n, "scriptContinuationFrames"),
            Y::uint64(n["scriptMemoryBytes"])};
        if (!executionBudget.instructionLimit || executionBudget.instructionLimit > INT64_MAX
            || !executionBudget.recursionLimit || !executionBudget.maxFrameValues
            || !executionBudget.maxContainerValues || !executionBudget.maxCallArguments
            || !executionBudget.maxContinuationFrames || !executionBudget.maxMemoryBytes)
            Y::fail();
        return true;
    } catch (...) {
        diagnostic = "ARP-SURVIVORS-CONFIG-INVALID";
        return false;
    }
}

bool Rules::sourcePaths(const core::pmr::String& manifest,
    core::pmr::Vector<core::pmr::String>& paths,
    core::pmr::String& diagnostic)
{
    paths.clear();
    try {
        auto node = Y::Node::deserialize(manifest);
        if (Y::text(node, "type") != "arp.survivors-script-package"
            || Y::uint32(node, "contractVersion") != 1)
            Y::fail();
        const auto& sources = Y::sequence(node, "sources");
        if (sources.empty())
            Y::fail();
        for (const auto& entry : sources) {
            auto path = Y::text(entry);
            const std::filesystem::path relative(path.c_str());
            if (path.empty() || path.find('\0') != core::pmr::String::npos
                || relative.has_root_path() || relative.extension() != ".arp")
                Y::fail();
            for (const auto& component : relative)
                if (component == "." || component == "..")
                    Y::fail();
            paths.push_back(std::move(path));
        }
        return true;
    } catch (...) {
        paths.clear();
        diagnostic = "ARP-SURVIVORS-SCRIPT-PACKAGE-INVALID";
        return false;
    }
}

bool Rules::prepare(const core::pmr::String& configYaml,
    const core::pmr::Vector<core::pmr::String>& parts,
    std::size_t maxSourceBytes,
    core::pmr::String& diagnostic)
{
    core::pmr::String candidate;
    for (const auto& part : parts) {
        const std::size_t separator = candidate.empty() ? 0u : 1u;
        if (candidate.size() > maxSourceBytes || separator > maxSourceBytes - candidate.size()
            || part.size() > maxSourceBytes - candidate.size() - separator) {
            diagnostic = "ARP-SURVIVORS-SCRIPT-SOURCE-BUDGET";
            return false;
        }
        if (separator)
            candidate.push_back('\n');
        candidate += part;
    }
    // Allocate both owners before replacing any previously valid rules.
    core::pmr::String config(configYaml);
    source_ = std::move(candidate);
    configYaml_ = std::move(config);
    return true;
}

bool Rules::load(
    const core::pmr::String& root, std::size_t maxSourceBytes, core::pmr::String& diagnostic)
{
    auto manifest = core::readTextFile(
        std::filesystem::path((root + "/script-package.yaml").c_str()), maxSourceBytes);
    if (!manifest.success) {
        diagnostic = manifest.diagnostics;
        return false;
    }
    core::pmr::Vector<core::pmr::String> paths, parts;
    if (!sourcePaths(manifest.text, paths, diagnostic))
        return false;
    std::size_t bytes = 0;
    bool empty = true;
    for (const auto& path : paths) {
        const std::size_t separator = empty ? 0u : 1u;
        if (bytes > maxSourceBytes || separator > maxSourceBytes - bytes) {
            diagnostic = "ARP-SURVIVORS-SCRIPT-SOURCE-BUDGET";
            return false;
        }
        auto part = core::readTextFile(
            std::filesystem::path(root.c_str()) / path.c_str(), maxSourceBytes - bytes - separator);
        if (!part.success) {
            diagnostic = part.diagnostics;
            return false;
        }
        bytes += separator + part.text.size();
        empty = bytes == 0;
        parts.push_back(std::move(part.text));
    }
    auto config = core::readTextFile(std::filesystem::path((root + "/game.yaml").c_str()), 65536);
    if (!config.success) {
        diagnostic = config.diagnostics;
        return false;
    }
    return prepare(config.text, parts, maxSourceBytes, diagnostic);
}
} // namespace arp::survivors
