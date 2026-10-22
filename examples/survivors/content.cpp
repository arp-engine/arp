#include "content.h"

#include "core/hash/sha256.h"
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
    try {
        auto n = Y::Node::deserialize(source.text);
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

bool Rules::load(
    const core::pmr::String& root, std::size_t maxSourceBytes, core::pmr::String& diagnostic)
{
    auto manifest = core::readTextFile(
        std::filesystem::path((root + "/script-package.yaml").c_str()), maxSourceBytes);
    if (!manifest.success) {
        diagnostic = manifest.diagnostics;
        return false;
    }
    try {
        auto node = Y::Node::deserialize(manifest.text);
        if (Y::text(node, "type") != "arp.survivors-script-package"
            || Y::uint32(node, "contractVersion") != 1)
            Y::fail();

        const auto& sources = Y::sequence(node, "sources");
        if (sources.empty())
            Y::fail();

        source_.clear();
        std::size_t sourceBytes = 0;
        for (const auto& entry : sources) {
            const auto sourcePath = Y::text(entry);
            const std::filesystem::path relativePath(sourcePath.c_str());
            if (sourcePath.empty() || relativePath.has_root_path()
                || relativePath.extension() != ".arp")
                Y::fail();
            for (const auto& component : relativePath) {
                if (component == "." || component == "..")
                    Y::fail();
            }

            const std::size_t separatorBytes = source_.empty() ? 0u : 1u;
            if (sourceBytes > maxSourceBytes || separatorBytes > maxSourceBytes - sourceBytes) {
                diagnostic = "ARP-SURVIVORS-SCRIPT-SOURCE-BUDGET";
                return false;
            }
            const std::size_t remainingBytes = maxSourceBytes - sourceBytes - separatorBytes;
            auto part = core::readTextFile(
                std::filesystem::path(root.c_str()) / relativePath, remainingBytes);
            if (!part.success) {
                diagnostic = part.diagnostics;
                return false;
            }
            if (part.text.size() > remainingBytes) {
                diagnostic = "ARP-SURVIVORS-SCRIPT-SOURCE-BUDGET";
                return false;
            }
            if (separatorBytes)
                source_.push_back('\n');
            source_ += part.text;
            sourceBytes += separatorBytes + part.text.size();
        }
    } catch (...) {
        source_.clear();
        diagnostic = "ARP-SURVIVORS-SCRIPT-PACKAGE-INVALID";
        return false;
    }

    auto config = core::readTextFile(std::filesystem::path((root + "/game.yaml").c_str()), 65536);
    if (!config.success) {
        diagnostic = config.diagnostics;
        return false;
    }
    configYaml_ = std::move(config.text);
    return true;
}

resource::ResourceHandle<scene2d::SpriteResource> Content::sprite(std::string_view name) const
{
    for (const auto& s : sprites_)
        if (std::string_view(s.name) == name)
            return s.handle;
    return {};
}

resource::ResourceHandle<scene2d::SpriteAnimationResource> Content::animation(
    std::string_view name) const
{
    for (const auto& a : animations_)
        if (std::string_view(a.name) == name)
            return a.handle;
    return {};
}

scriptvm::Value Content::spriteIds() const
{
    reflection::hdict values;
    for (const auto& s : sprites_)
        values.emplace(scriptvm::makeString(s.name), scriptvm::makeString(s.handle.id()));
    return scriptvm::Value{std::move(values)};
}

scriptvm::Value Content::animationIds() const
{
    reflection::hdict values;
    for (const auto& a : animations_)
        values.emplace(scriptvm::makeString(a.name), scriptvm::makeString(a.handle.id()));
    return scriptvm::Value{std::move(values)};
}

scriptvm::Value Content::soundIds() const
{
    reflection::hlist values;
    for (const auto& sound : sounds)
        values.push_back(scriptvm::makeString(sound.id()));
    return scriptvm::Value{std::move(values)};
}

core::SharedPtr<const core::pmr::Vector<renderer::ui::SpriteGlyph>> Content::font() const
{
    auto values = core::makeShared<core::pmr::Vector<renderer::ui::SpriteGlyph>>();
    for (unsigned code = 32; code < 128; ++code) {
        char name[32];
        std::snprintf(name, sizeof(name), "glyph_%u", code);
        auto s = sprite(name);
        if (s.ready()) {
            const auto* slice = atlas.get()->slice(s.get()->sliceName());
            values->push_back({code, s, float(slice->width) / slice->pixelsPerUnit});
        }
    }
    return values;
}

bool Content::load(
    const core::pmr::String& root, const GameConfig& config, core::pmr::String& diagnostic)
{
    auto source = core::readTextFile(
        std::filesystem::path((root + "/content.yaml").c_str()), config.maxSourceBytes);
    if (!source.success) {
        diagnostic = source.diagnostics;
        return false;
    }
    try {
        auto n = Y::Node::deserialize(source.text);
        if (Y::text(n, "type") != "arp.survivors-content" || Y::uint32(n, "contractVersion") != 1)
            Y::fail();
        const auto atlasId = Y::text(n, "atlasId");
        auto result = resources_.load<scene2d::SpriteAtlasResource>({},
            atlasId,
            root + "/" + Y::text(n, "atlasMetadata"),
            root + "/" + Y::text(n, "atlasImage"),
            config.maxTextureBytes);
        if (!result.success) {
            diagnostic = result.diagnostics;
            return false;
        }
        atlas = resources_.find<scene2d::SpriteAtlasResource>(atlasId);
        for (const auto& node : Y::sequence(n, "sprites")) {
            auto name = Y::text(node, "name"), id = Y::text(node, "resourceId");
            result = resources_.load<scene2d::SpriteResource>({}, id, atlasId, name);
            if (!result.success) {
                diagnostic = result.diagnostics;
                return false;
            }
            sprites_.push_back({name, resources_.find<scene2d::SpriteResource>(id)});
        }
        for (const auto& node : Y::sequence(n, "animations")) {
            auto id = Y::text(node, "resourceId");
            core::pmr::Vector<scene2d::SpriteAnimationFrame> frames;
            for (const auto& f : Y::sequence(node, "frames"))
                frames.push_back(
                    {Y::text(f, "spriteId"), Y::number(f, "seconds"), Y::uint32(f, "eventId")});
            result = resources_.load<scene2d::SpriteAnimationResource>(
                {}, id, frames, Y::boolean(node, "loop"));
            if (!result.success) {
                diagnostic = result.diagnostics;
                return false;
            }
            animations_.push_back(
                {Y::text(node, "name"), resources_.find<scene2d::SpriteAnimationResource>(id)});
        }
        const auto set = Y::text(n, "tileSetId"), mapId = Y::text(n, "tileMapId");
        result = resources_.load<scene2d::TileSetResource>(
            {}, set, root + "/dungeon.tileset.yaml", config.maxSourceBytes);
        if (!result.success) {
            diagnostic = result.diagnostics;
            return false;
        }
        result = resources_.load<scene2d::TileMapResource>(
            {}, mapId, set, root + "/dungeon.tilemap.yaml", config.maxSourceBytes);
        if (!result.success) {
            diagnostic = result.diagnostics;
            return false;
        }
        map = resources_.find<scene2d::TileMapResource>(mapId);
        const core::pmr::String prefabId = "20000000-0000-4000-8000-000000005000";
        result = resources_.load<scene::ScenePrefabResource>({},
            prefabId,
            root + "/orbit.prefab.yaml",
            config.maxSourceBytes,
            config.maxOrbitBlades + 1);
        if (!result.success) {
            diagnostic = result.diagnostics;
            return false;
        }
        orbitPrefab = resources_.find<scene::ScenePrefabResource>(prefabId);
        int index = 0;
        for (const char* sound : {"hit", "upgrade", "menu", "music"}) {
            const auto path = root + "/" + sound + ".wav";
            auto decoded = audio::importAudioFile(path.c_str(), config.maxSourceBytes);
            if (!decoded.success()) {
                diagnostic = decoded.diagnostics;
                return false;
            }
            char id[64];
            std::snprintf(id, sizeof(id), "20000000-0000-4000-8000-%012d", 4000 + index);
            result = resources_.load<resource::AudioClipResource>(
                {}, core::pmr::String(id), std::move(decoded.samples), decoded.channels);
            if (!result.success) {
                diagnostic = result.diagnostics;
                return false;
            }
            sounds[index++] = resources_.find<resource::AudioClipResource>(id);
        }
        return true;
    } catch (...) {
        diagnostic = "ARP-SURVIVORS-CONTENT-INVALID";
        return false;
    }
}
} // namespace arp::survivors
