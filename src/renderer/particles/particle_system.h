#pragma once

#include "core/cpu_allocator.h"
#include "core/curve.h"
#include "core/math.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

namespace arp::renderer {

struct ParticleColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

enum class ParticleQualityProfile { Low, Medium, High, Desktop };

struct ParticleSystemDesc {
    uint32_t maxParticles = 1024;
    uint32_t maxEmitters = 32;
};

inline ParticleSystemDesc particleSystemDescForProfile(ParticleQualityProfile profile)
{
    switch (profile) {
    case ParticleQualityProfile::Low:
        return ParticleSystemDesc{768, 16};
    case ParticleQualityProfile::Medium:
        return ParticleSystemDesc{2048, 32};
    case ParticleQualityProfile::High:
        return ParticleSystemDesc{5000, 48};
    case ParticleQualityProfile::Desktop:
        return ParticleSystemDesc{12000, 96};
    }
    return ParticleSystemDesc{};
}

struct ParticleFrameInput {
    float deltaSeconds = 0.0f;
    bool paused = false;
    core::Vec3 hostVelocity{0.0f, 0.0f, 0.0f};
};

struct ParticleEmitterHandle {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;

    bool valid() const { return index != UINT32_MAX; }
};

// ---------------------------------------------------------------------------
// ParticleVisualFamily — renderer-level visual family contract.
//
// Family selection is deterministic and driven by resource binding,
// never by color heuristics or runtime guess.
// ---------------------------------------------------------------------------
enum class ParticleVisualFamily : uint32_t {
    Unknown = 0,
    SpriteAlpha,    // alpha-blended textured/untextured sprite
    SpriteAdditive, // additive-blended textured/untextured sprite
    LightningThin,  // thin procedural lightning (electricity)
    StormCloud,     // dark storm cloud with tribulation lightning (渡劫雷云)
    FallbackLegacy, // shared procedural fallback
    Trail,          // non-billboard trail / ribbon strip
    Beam,           // non-billboard beam / laser segment
    MeshParticle,   // instanced 3D mesh particle (non-sprite)
    Count,
};

struct ParticleMaterialBinding {
    core::pmr::String materialAssetId{&core::defaultCpuMemoryResource()};
    core::pmr::String textureAssetId{&core::defaultCpuMemoryResource()};
    core::pmr::String shaderFamilyId{&core::defaultCpuMemoryResource()};
    core::pmr::String blendMode{&core::defaultCpuMemoryResource()};
    core::pmr::String billboardMode{&core::defaultCpuMemoryResource()};
    core::pmr::String tintPolicy{&core::defaultCpuMemoryResource()};
    bool isTextured = false;
    bool isAdditiveBlend = false;
    bool isLightningThin = false;
    bool isStormCloud = false;
    bool isTrail = false;
    bool isBeam = false;
    bool isMeshParticle = false;
};

struct ParticleBurstDesc {
    float time = 0.0f;
    uint32_t minCount = 0;
    uint32_t maxCount = 0;
    uint32_t cycleCount = 1;
    float repeatInterval = 0.0f;
    float probability = 1.0f;
};

// Deterministic visual family selection from material binding.
// Returns Unknown when the binding does not map to a known family.
inline ParticleVisualFamily visualFamilyFromBinding(const ParticleMaterialBinding& binding) noexcept
{
    if (binding.shaderFamilyId == "ParticleSpriteAlpha") {
        return ParticleVisualFamily::SpriteAlpha;
    }
    if (binding.shaderFamilyId == "ParticleSpriteAdditive") {
        return ParticleVisualFamily::SpriteAdditive;
    }
    if (binding.shaderFamilyId == "ParticleLightningThin") {
        return ParticleVisualFamily::LightningThin;
    }
    if (binding.shaderFamilyId == "ParticleStormCloud") {
        return ParticleVisualFamily::StormCloud;
    }
    if (binding.shaderFamilyId == "ParticleFallbackLegacy") {
        return ParticleVisualFamily::FallbackLegacy;
    }
    if (binding.shaderFamilyId == "ParticleTrail") {
        return ParticleVisualFamily::Trail;
    }
    if (binding.shaderFamilyId == "ParticleBeam") {
        return ParticleVisualFamily::Beam;
    }
    if (binding.shaderFamilyId == "ParticleMeshParticle") {
        return ParticleVisualFamily::MeshParticle;
    }
    // Fallback: infer from blend mode when shaderFamilyId is not set
    if (binding.blendMode == "Additive") {
        return ParticleVisualFamily::SpriteAdditive;
    }
    if (binding.blendMode == "Alpha") {
        return ParticleVisualFamily::SpriteAlpha;
    }
    return ParticleVisualFamily::Unknown;
}

struct ParticleEmitterDesc {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    uint32_t maxParticles = 128;
    uint32_t burstCount = 0;
    core::pmr::Vector<ParticleBurstDesc> burstSchedule{&core::defaultCpuMemoryResource()};
    float spawnRatePerSecond = 0.0f;
    float particleLifetimeSeconds = 1.0f;
    float gravityAcceleration = 0.0f;
    bool looping = true;
    bool enabled = true;
    uint32_t seed = 1;
    core::Vec3 origin;
    core::Vec3 velocityMin{0.0f, 1.0f, 0.0f};
    core::Vec3 velocityMax{0.0f, 1.0f, 0.0f};
    float startSize = 1.0f;
    float endSize = 1.0f;
    ParticleColor startColor;
    ParticleColor endColor{1.0f, 1.0f, 1.0f, 0.0f};
    core::FloatCurve sizeOverLifetime;
    core::FloatCurve colorAlphaOverLifetime;
    core::Vec3 boundsExtent{1.0f, 1.0f, 1.0f};
    ParticleMaterialBinding materialBinding;
    ParticleVisualFamily visualFamily = ParticleVisualFamily::SpriteAlpha;
    // Shape / emission space contract
    enum class EmitterShape : uint32_t {
        Point = 0,
        Cone,
        Sphere,
        Box,
        DirectionalForward, // emitter propagates along velocityMin forward axis; no position scatter.
        RadialBurst,        // position on sphere shell; velocity outward along (position - origin).
    };
    EmitterShape shape = EmitterShape::Point;
    float coneAngleDegrees = 25.0f;
    float sphereRadius = 1.0f;
    core::Vec3 boxExtents{1.0f, 1.0f, 1.0f};
    bool worldSpaceEmission = false;
    // Rotation / Orbit contract
    float startRotation = 0.0f;
    float rotationSpeed = 0.0f;
    bool orbitEnabled = false;
    float orbitRadius = 1.0f;
    float orbitSpeed = 0.0f;
    core::Vec3 orbitCenter{0.0f, 0.0f, 0.0f};
    core::Vec3 orbitAxis{0.0f, 1.0f, 0.0f};
    // Noise / Force field contract
    float noiseFrequency = 0.0f;
    float noiseAmplitude = 0.0f;
    core::Vec3 externalForce{0.0f, 0.0f, 0.0f};
    float externalForcesMultiplier = 1.0f;
    // Inherit velocity contract
    float inheritVelocityFactor = 0.0f;
    // Mesh particle contract (valid when visualFamily is MeshParticle)
    core::pmr::String meshAssetId{&core::defaultCpuMemoryResource()};
    core::pmr::String meshMaterialId{&core::defaultCpuMemoryResource()};
    float meshScaleMin = 1.0f;
    float meshScaleMax = 1.0f;
    // Soft particle / Depth fade contract
    bool depthFadeEnabled = false;
    float depthFadeDistance = 0.5f;
    // Trail / Beam specific contract (valid when visualFamily is Trail or Beam)
    float trailWidth = 0.1f;
    uint32_t trailMaxSegments = 32;
    float beamLength = 1.0f;
    // Flipbook / SubUV animation contract
    uint32_t flipbookTilesX = 1;
    uint32_t flipbookTilesY = 1;
    uint32_t flipbookFrameCount = 1;
    float flipbookPlayRate = 1.0f;
    bool flipbookLooping = true;
    bool flipbookRandomStart = false;
    // Collision / Trigger contract
    bool collisionEnabled = false;
    float collisionPlaneY = 0.0f;
    float collisionBounceFactor = 0.3f;
    bool collisionSpawnsSubEmitter = false;
    uint32_t subEmitterMaxParticles = 32;
};

// Billboard mode enum matching push-constant semantics:
// 0 = ViewAligned, 1 = LocalAligned, 2 = WorldAligned
inline std::uint32_t billboardModeFromString(const core::pmr::String& mode)
{
    if (mode == "LocalAligned")
        return 1u;
    if (mode == "WorldAligned")
        return 2u;
    return 0u; // default ViewAligned
}

struct ParticleInstance {
    core::Vec3 position;
    core::Vec3 velocity;
    ParticleColor color;
    float size = 1.0f;
    float ageSeconds = 0.0f;
    float lifetimeSeconds = 1.0f;
    uint32_t emitterIndex = 0;
    uint32_t seed = 0;
    uint32_t trailIndex = 0;
    uint32_t flipbookFrame = 0;
    uint32_t flipbookTilesX = 1;
    uint32_t flipbookTilesY = 1;
    float rotation = 0.0f;
    std::uint32_t billboardMode = 0; // 0=ViewAligned, 1=LocalAligned, 2=WorldAligned
    bool depthFadeEnabled = false;
    float depthFadeDistance = 0.5f;
    ParticleVisualFamily visualFamily = ParticleVisualFamily::SpriteAlpha;
    bool isTextured = false;
    core::pmr::String textureAssetId{&core::defaultCpuMemoryResource()};
    core::pmr::String meshAssetId{&core::defaultCpuMemoryResource()};
    core::pmr::String meshMaterialId{&core::defaultCpuMemoryResource()};
    float trailWidth = 0.1f;
    float beamLength = 1.0f;

    float normalizedAge() const
    {
        return lifetimeSeconds > 0.0f ? std::min(std::max(ageSeconds / lifetimeSeconds, 0.0f), 1.0f)
                                      : 1.0f;
    }
};

struct ParticleBounds {
    core::Vec3 center;
    core::Vec3 extents;

    bool valid() const { return extents.x >= 0.0f && extents.y >= 0.0f && extents.z >= 0.0f; }
};

struct ParticleEmitterResult {
    bool success = false;
    ParticleEmitterHandle handle;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};

    bool valid() const { return success && handle.valid(); }
};

struct ParticleSystemTelemetry {
    uint32_t capacity = 0;
    uint32_t emitterCount = 0;
    uint32_t aliveParticles = 0;
    uint32_t spawnedThisFrame = 0;
    uint32_t retiredThisFrame = 0;
    uint32_t droppedSpawns = 0;
    uint32_t simulatedEmitters = 0;
    uint32_t boundsUpdates = 0;
    uint32_t budgetDenials = 0;
    uint32_t collisionEvents = 0;
    uint32_t subEmitterSpawns = 0;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

class ParticleSystem {
public:
    explicit ParticleSystem(ParticleSystemDesc desc) : desc_(desc)
    {
        particles_.reserve(desc_.maxParticles);
        emitters_.reserve(desc_.maxEmitters);
        telemetry_.capacity = desc_.maxParticles;
    }

    ParticleEmitterResult addEmitter(const ParticleEmitterDesc& desc)
    {
        const auto speedMagnitudeSq = desc.velocityMin.x * desc.velocityMin.x
            + desc.velocityMin.y * desc.velocityMin.y + desc.velocityMin.z * desc.velocityMin.z;
        if (emitters_.size() >= desc_.maxEmitters) {
            return ParticleEmitterResult{
                false,
                {},
                "emitter budget exhausted: cannot exceed maxEmitters",
            };
        }
        if (desc.maxParticles == 0) {
            return ParticleEmitterResult{
                false,
                {},
                "invalid emitter contract: maxParticles must be > 0",
            };
        }
        if (desc.particleLifetimeSeconds <= 0.0f) {
            return ParticleEmitterResult{
                false,
                {},
                "invalid emitter contract: particleLifetimeSeconds must be > 0",
            };
        }
        if (desc.shape == ParticleEmitterDesc::EmitterShape::RadialBurst
            && speedMagnitudeSq < 0.0001f * 0.0001f) {
            return ParticleEmitterResult{
                false,
                {},
                "invalid emitter contract: RadialBurst requires |velocityMin| > 0 to define deterministic outward speed",
            };
        }
        if (reservedParticleCapacity() + desc.maxParticles > desc_.maxParticles) {
            return ParticleEmitterResult{
                false,
                {},
                "emitter particle capacity exceeds system budget",
            };
        }

        EmitterState state;
        state.desc = desc;
        state.generation = 1;
        state.randomState = desc.seed ? desc.seed : 1;
        state.currentOrigin = desc.origin;
        state.burstCyclesEmitted.assign(desc.burstSchedule.size(), 0u);
        emitters_.push_back(state);
        telemetry_.emitterCount = static_cast<uint32_t>(emitters_.size());
        return ParticleEmitterResult{
            true,
            ParticleEmitterHandle{
                static_cast<uint32_t>(emitters_.size() - 1),
                state.generation,
            },
            {},
        };
    }

    void enableEmitter(ParticleEmitterHandle handle)
    {
        auto* emitter = emitterFor(handle);
        if (emitter) {
            emitter->desc.enabled = true;
        }
    }

    void disableEmitter(ParticleEmitterHandle handle)
    {
        auto* emitter = emitterFor(handle);
        if (emitter) {
            emitter->desc.enabled = false;
        }
    }

    void triggerBurst(ParticleEmitterHandle handle, uint32_t count)
    {
        auto* emitter = emitterFor(handle);
        if (!emitter) {
            return;
        }
        emitter->pendingBurst += count;
    }

    /// Move a world-space emitter's origin (for emitters that track a moving
    /// source like a sword tip). No-op for local-space emitters (those get
    /// displaced by hostVelocity in update() already).
    void setEmitterOrigin(ParticleEmitterHandle handle, core::Vec3 origin)
    {
        auto* emitter = emitterFor(handle);
        if (emitter) {
            emitter->currentOrigin = origin;
        }
    }

    /// Update a DirectionalForward emitter's velocity range (direction +
    /// magnitude). Useful when the "forward" axis changes per frame (e.g.
    /// rotating the player).
    void setEmitterVelocity(
        ParticleEmitterHandle handle, core::Vec3 velocityMin, core::Vec3 velocityMax)
    {
        auto* emitter = emitterFor(handle);
        if (emitter) {
            emitter->desc.velocityMin = velocityMin;
            emitter->desc.velocityMax = velocityMax;
        }
    }

    void update(ParticleFrameInput input)
    {
        telemetry_.spawnedThisFrame = 0;
        telemetry_.retiredThisFrame = 0;
        telemetry_.droppedSpawns = 0;
        telemetry_.simulatedEmitters = 0;
        telemetry_.boundsUpdates = 0;
        telemetry_.budgetDenials = 0;
        telemetry_.diagnostics.clear();

        if (input.paused) {
            telemetry_.aliveParticles = static_cast<uint32_t>(particles_.size());
            return;
        }

        hostVelocity_ = input.hostVelocity;
        const auto dt = std::max(input.deltaSeconds, 0.0f);
        const core::Vec3 hostDisplacement{
            hostVelocity_.x * dt,
            hostVelocity_.y * dt,
            hostVelocity_.z * dt,
        };
        retireAndSimulate(dt);
        for (auto& emitter : emitters_) {
            if (!emitter.desc.enabled) {
                continue;
            }
            ++telemetry_.simulatedEmitters;
            const auto previousEmissionTime = emitter.emissionTimeSeconds;
            emitter.emissionTimeSeconds += dt;
            if (!emitter.desc.worldSpaceEmission) {
                emitter.currentOrigin.x += hostDisplacement.x;
                emitter.currentOrigin.y += hostDisplacement.y;
                emitter.currentOrigin.z += hostDisplacement.z;
            }

            const auto burst = emitter.pendingBurst;
            emitter.pendingBurst = 0;
            spawnFromEmitter(emitter, burst);

            if (emitter.desc.burstSchedule.empty() && emitter.desc.burstCount > 0
                && !emitter.initialBurstEmitted) {
                emitter.initialBurstEmitted = true;
                spawnFromEmitter(emitter, emitter.desc.burstCount);
            }
            spawnScheduledBursts(emitter, previousEmissionTime);
            if (emitter.desc.looping && emitter.desc.spawnRatePerSecond > 0.0f) {
                emitter.spawnAccumulator += emitter.desc.spawnRatePerSecond * dt;
                const auto continuousCount = static_cast<uint32_t>(emitter.spawnAccumulator);
                emitter.spawnAccumulator -= static_cast<float>(continuousCount);
                spawnFromEmitter(emitter, continuousCount);
            }
        }
        telemetry_.aliveParticles = static_cast<uint32_t>(particles_.size());
    }

    const core::pmr::Vector<ParticleInstance>& particles() const { return particles_; }

    ParticleSystemTelemetry telemetry() const
    {
        auto result = telemetry_;
        result.emitterCount = static_cast<uint32_t>(emitters_.size());
        result.aliveParticles = static_cast<uint32_t>(particles_.size());
        return result;
    }

    ParticleSystemTelemetry stats() const { return telemetry(); }

    void spawnSubEmitter(uint32_t parentEmitterIndex, core::Vec3 position, uint32_t maxParticles)
    {
        if (parentEmitterIndex >= emitters_.size())
            return;
        const auto& parentDesc = emitters_[parentEmitterIndex].desc;
        if (!parentDesc.collisionSpawnsSubEmitter)
            return;
        if (particles_.size() + maxParticles > desc_.maxParticles) {
            ++telemetry_.budgetDenials;
            return;
        }

        ParticleEmitterDesc subDesc;
        subDesc.name = parentDesc.name;
        subDesc.name += "_sub";
        subDesc.maxParticles = maxParticles;
        subDesc.burstCount = std::min(maxParticles, 4u);
        subDesc.spawnRatePerSecond = 0.0f;
        subDesc.particleLifetimeSeconds = parentDesc.particleLifetimeSeconds * 0.5f;
        subDesc.looping = false;
        subDesc.seed = parentDesc.seed + 999u;
        subDesc.origin = position;
        subDesc.velocityMin = {-0.3f, 1.5f, -0.3f};
        subDesc.velocityMax = {0.3f, 3.0f, 0.3f};
        subDesc.startSize = parentDesc.startSize * 0.5f;
        subDesc.endSize = parentDesc.endSize;
        subDesc.startColor = parentDesc.startColor;
        subDesc.endColor = parentDesc.endColor;
        subDesc.endColor.a = 0.0f;
        subDesc.visualFamily = parentDesc.visualFamily;
        subDesc.materialBinding = parentDesc.materialBinding;

        const auto result = addEmitter(subDesc);
        if (result.success) {
            ++telemetry_.subEmitterSpawns;
        }
    }

    ParticleBounds bounds() const
    {
        if (particles_.empty()) {
            return ParticleBounds{
                core::Vec3{0.0f, 0.0f, 0.0f},
                core::Vec3{0.0f, 0.0f, 0.0f},
            };
        }
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float minZ = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        float maxZ = std::numeric_limits<float>::lowest();
        for (const auto& p : particles_) {
            minX = std::min(minX, p.position.x);
            minY = std::min(minY, p.position.y);
            minZ = std::min(minZ, p.position.z);
            maxX = std::max(maxX, p.position.x);
            maxY = std::max(maxY, p.position.y);
            maxZ = std::max(maxZ, p.position.z);
        }
        const auto center = core::Vec3{
            (minX + maxX) * 0.5f,
            (minY + maxY) * 0.5f,
            (minZ + maxZ) * 0.5f,
        };
        const auto extents = core::Vec3{
            (maxX - minX) * 0.5f,
            (maxY - minY) * 0.5f,
            (maxZ - minZ) * 0.5f,
        };
        return ParticleBounds{center, extents};
    }

private:
    struct EmitterState {
        ParticleEmitterDesc desc;
        uint32_t generation = 0;
        uint32_t randomState = 1;
        uint32_t pendingBurst = 0;
        uint32_t spawnCounter = 0;
        float spawnAccumulator = 0.0f;
        float emissionTimeSeconds = 0.0f;
        bool initialBurstEmitted = false;
        core::Vec3 currentOrigin;
        core::pmr::Vector<uint32_t> burstCyclesEmitted{&core::defaultCpuMemoryResource()};
    };

    uint32_t reservedParticleCapacity() const
    {
        uint32_t reserved = 0;
        for (const auto& emitter : emitters_) {
            reserved += emitter.desc.maxParticles;
        }
        return reserved;
    }

    EmitterState* emitterFor(ParticleEmitterHandle handle)
    {
        if (handle.index >= emitters_.size()) {
            return nullptr;
        }
        auto& emitter = emitters_[handle.index];
        return emitter.generation == handle.generation ? &emitter : nullptr;
    }

    static float lerp(float from, float to, float t) { return from + (to - from) * t; }

    static ParticleColor lerpColor(ParticleColor from, ParticleColor to, float t)
    {
        return ParticleColor{
            lerp(from.r, to.r, t),
            lerp(from.g, to.g, t),
            lerp(from.b, to.b, t),
            lerp(from.a, to.a, t),
        };
    }

    static uint32_t nextRandom(uint32_t& state)
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    static float random01(uint32_t& state)
    {
        return static_cast<float>(nextRandom(state) & 0x00FFFFFFu)
            / static_cast<float>(0x01000000u);
    }

    static float randomRange(uint32_t& state, float minValue, float maxValue)
    {
        return minValue + (maxValue - minValue) * random01(state);
    }

    uint32_t aliveCountForEmitter(uint32_t emitterIndex) const
    {
        uint32_t count = 0;
        for (const auto& particle : particles_) {
            if (particle.emitterIndex == emitterIndex) {
                ++count;
            }
        }
        return count;
    }

    float sampleSizeOverLifetime(const EmitterState& emitter, float normalizedAge) const
    {
        if (emitter.desc.sizeOverLifetime.valid()) {
            return emitter.desc.sizeOverLifetime.sample(normalizedAge);
        }
        return lerp(emitter.desc.startSize, emitter.desc.endSize, normalizedAge);
    }

    float sampleAlphaOverLifetime(const EmitterState& emitter, float normalizedAge) const
    {
        if (emitter.desc.colorAlphaOverLifetime.valid()) {
            return emitter.desc.colorAlphaOverLifetime.sample(normalizedAge);
        }
        return lerp(emitter.desc.startColor.a, emitter.desc.endColor.a, normalizedAge);
    }

    void spawnFromEmitter(EmitterState& emitter, uint32_t requestedCount)
    {
        const auto emitterIndex = static_cast<uint32_t>(&emitter - emitters_.data());
        uint32_t emitterAlive = aliveCountForEmitter(emitterIndex);
        uint32_t spawned = 0;
        for (uint32_t index = 0; index < requestedCount && particles_.size() < desc_.maxParticles
            && emitterAlive < emitter.desc.maxParticles;
            ++index) {
            const auto seed = nextRandom(emitter.randomState);

            // Shape-based position sampling
            core::Vec3 spawnPos = emitter.currentOrigin;
            switch (emitter.desc.shape) {
            case ParticleEmitterDesc::EmitterShape::Cone: {
                float angle = emitter.desc.coneAngleDegrees * 0.0174533f;
                float r = std::tan(angle) * random01(emitter.randomState);
                float theta = random01(emitter.randomState) * 6.283185f;
                spawnPos.x += r * std::cos(theta);
                spawnPos.z += r * std::sin(theta);
                break;
            }
            case ParticleEmitterDesc::EmitterShape::Sphere: {
                float r = emitter.desc.sphereRadius * std::cbrt(random01(emitter.randomState));
                float phi = random01(emitter.randomState) * 6.283185f;
                float cosTheta = randomRange(emitter.randomState, -1.0f, 1.0f);
                float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
                spawnPos.x += r * sinTheta * std::cos(phi);
                spawnPos.y += r * cosTheta;
                spawnPos.z += r * sinTheta * std::sin(phi);
                break;
            }
            case ParticleEmitterDesc::EmitterShape::Box: {
                spawnPos.x += randomRange(
                    emitter.randomState, -emitter.desc.boxExtents.x, emitter.desc.boxExtents.x);
                spawnPos.y += randomRange(
                    emitter.randomState, -emitter.desc.boxExtents.y, emitter.desc.boxExtents.y);
                spawnPos.z += randomRange(
                    emitter.randomState, -emitter.desc.boxExtents.z, emitter.desc.boxExtents.z);
                break;
            }
            case ParticleEmitterDesc::EmitterShape::DirectionalForward:
                // Position stays at the emitter origin; velocity is fixed along velocityMin
                // (treated as the emitter’s forward axis). No positional scatter — this
                // is the curated “枪口定向火焰” / muzzle-flame pattern.
                break;
            case ParticleEmitterDesc::EmitterShape::RadialBurst: {
                // Position on a sphere shell of radius sphereRadius; velocity is picked
                // outward along (position - origin). This is the curated “环形扩散” / radial
                // explosion burst pattern and does not use velocityMin/Max for an explicit
                // vector override — magnitude is taken from |velocityMin| as start speed.
                float r = emitter.desc.sphereRadius;
                float phi = random01(emitter.randomState) * 6.283185f;
                float cosTheta = randomRange(emitter.randomState, -1.0f, 1.0f);
                float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
                spawnPos.x += r * sinTheta * std::cos(phi);
                spawnPos.y += r * cosTheta;
                spawnPos.z += r * sinTheta * std::sin(phi);
                break;
            }
            case ParticleEmitterDesc::EmitterShape::Point:
            default:
                break;
            }

            // Directional velocity for cone: radial burst based on cone angle
            core::Vec3 spawnVelocity = core::Vec3{
                randomRange(
                    emitter.randomState, emitter.desc.velocityMin.x, emitter.desc.velocityMax.x),
                randomRange(
                    emitter.randomState, emitter.desc.velocityMin.y, emitter.desc.velocityMax.y),
                randomRange(
                    emitter.randomState, emitter.desc.velocityMin.z, emitter.desc.velocityMax.z),
            };
            if (emitter.desc.shape == ParticleEmitterDesc::EmitterShape::Cone) {
                float angle = emitter.desc.coneAngleDegrees * 0.0174533f;
                float r = std::tan(angle) * random01(emitter.randomState);
                float theta = random01(emitter.randomState) * 6.283185f;
                float speed = std::sqrt(spawnVelocity.x * spawnVelocity.x
                    + spawnVelocity.y * spawnVelocity.y + spawnVelocity.z * spawnVelocity.z);
                spawnVelocity.x = r * std::cos(theta);
                spawnVelocity.y = 1.0f;
                spawnVelocity.z = r * std::sin(theta);
                float len = std::sqrt(spawnVelocity.x * spawnVelocity.x
                    + spawnVelocity.y * spawnVelocity.y + spawnVelocity.z * spawnVelocity.z);
                if (len > 0.001f) {
                    float scale = speed / len;
                    spawnVelocity.x *= scale;
                    spawnVelocity.y *= scale;
                    spawnVelocity.z *= scale;
                }
            }

            if (emitter.desc.shape == ParticleEmitterDesc::EmitterShape::DirectionalForward) {
                // Curated directional-forward pattern: stable forward direction = velocityMin
                // normalized, magnitude scaled by uniform random between velocityMin/Max.
                // Curated subset uses velocityMin as the forward axis. Any angular variation
                // must come from a separate Shape (e.g. Cone) — DirectionalForward does not
                // apply cone spread.
                float fwdLen = std::sqrt(emitter.desc.velocityMin.x * emitter.desc.velocityMin.x
                    + emitter.desc.velocityMin.y * emitter.desc.velocityMin.y
                    + emitter.desc.velocityMin.z * emitter.desc.velocityMin.z);
                float scalarSpeed = 1.0f;
                const auto speedMinSq = emitter.desc.velocityMin.x * emitter.desc.velocityMin.x
                    + emitter.desc.velocityMin.y * emitter.desc.velocityMin.y
                    + emitter.desc.velocityMin.z * emitter.desc.velocityMin.z;
                const auto speedMaxSq = emitter.desc.velocityMax.x * emitter.desc.velocityMax.x
                    + emitter.desc.velocityMax.y * emitter.desc.velocityMax.y
                    + emitter.desc.velocityMax.z * emitter.desc.velocityMax.z;
                if (speedMaxSq > 0.0f) {
                    const auto sMin = std::sqrt(speedMinSq);
                    const auto sMax = std::sqrt(speedMaxSq);
                    scalarSpeed = randomRange(emitter.randomState, sMin, sMax);
                    if (sMax < 0.0001f)
                        scalarSpeed = 0.0f;
                }
                if (fwdLen > 0.0001f) {
                    const auto inv = scalarSpeed / fwdLen;
                    spawnVelocity.x = emitter.desc.velocityMin.x * inv;
                    spawnVelocity.y = emitter.desc.velocityMin.y * inv;
                    spawnVelocity.z = emitter.desc.velocityMin.z * inv;
                } else {
                    // Forward axis is zero — there is no deterministic forward direction.
                    // The runtime refuses to silently default to (0,1,0) per AGENTS.md;
                    // it emits zero velocity, which is observable and debuggable.
                    spawnVelocity = core::Vec3{0.0f, 0.0f, 0.0f};
                }
            }

            if (emitter.desc.shape == ParticleEmitterDesc::EmitterShape::RadialBurst) {
                // Velocity outward along normalised (position - emitter origin). Magnitude
                // is taken from |velocityMin| (the importer-lowered startSpeed); position is
                // already laid on the sphere shell so |position - origin| = sphereRadius.
                core::Vec3 outward = {spawnPos.x - emitter.currentOrigin.x,
                    spawnPos.y - emitter.currentOrigin.y,
                    spawnPos.z - emitter.currentOrigin.z};
                float outLen = std::sqrt(
                    outward.x * outward.x + outward.y * outward.y + outward.z * outward.z);
                if (outLen > 0.0001f) {
                    float scalarSpeed =
                        std::sqrt(emitter.desc.velocityMin.x * emitter.desc.velocityMin.x
                            + emitter.desc.velocityMin.y * emitter.desc.velocityMin.y
                            + emitter.desc.velocityMin.z * emitter.desc.velocityMin.z);
                    const auto inv = scalarSpeed / outLen;
                    spawnVelocity.x = outward.x * inv;
                    spawnVelocity.y = outward.y * inv;
                    spawnVelocity.z = outward.z * inv;
                }
            }

            ParticleInstance particle;
            particle.position = spawnPos;
            particle.velocity = spawnVelocity;
            particle.color = emitter.desc.startColor;
            particle.size = emitter.desc.startSize;
            particle.lifetimeSeconds = emitter.desc.particleLifetimeSeconds;
            particle.emitterIndex = emitterIndex;
            particle.seed = seed;
            particle.trailIndex = emitter.spawnCounter++;
            particle.flipbookFrame = emitter.desc.flipbookRandomStart
                ? static_cast<uint32_t>(random01(emitter.randomState)
                      * static_cast<float>(emitter.desc.flipbookFrameCount))
                : 0u;
            particle.flipbookTilesX = emitter.desc.flipbookTilesX;
            particle.flipbookTilesY = emitter.desc.flipbookTilesY;
            particle.rotation = emitter.desc.startRotation
                + emitter.desc.rotationSpeed * random01(emitter.randomState);
            particle.visualFamily = emitter.desc.visualFamily;
            particle.isTextured = emitter.desc.materialBinding.isTextured;
            particle.textureAssetId = emitter.desc.materialBinding.textureAssetId;
            particle.billboardMode =
                billboardModeFromString(emitter.desc.materialBinding.billboardMode);
            particle.depthFadeEnabled = emitter.desc.depthFadeEnabled;
            particle.depthFadeDistance = emitter.desc.depthFadeDistance;
            particle.meshAssetId = emitter.desc.meshAssetId;
            particle.meshMaterialId = emitter.desc.meshMaterialId;
            particle.trailWidth = emitter.desc.trailWidth;
            particle.beamLength = emitter.desc.beamLength;
            particles_.push_back(particle);
            ++emitterAlive;
            ++spawned;
            ++telemetry_.spawnedThisFrame;
        }
        const uint32_t dropped = requestedCount - spawned;
        telemetry_.droppedSpawns += dropped;
        if (dropped > 0) {
            telemetry_.budgetDenials += 1;
        }
    }

    void spawnScheduledBursts(EmitterState& emitter, float previousEmissionTime)
    {
        for (std::size_t burstIndex = 0; burstIndex < emitter.desc.burstSchedule.size();
            ++burstIndex) {
            const auto& burst = emitter.desc.burstSchedule[burstIndex];
            auto& cycle = emitter.burstCyclesEmitted[burstIndex];
            while (cycle < burst.cycleCount) {
                const auto eventTime =
                    burst.time + static_cast<float>(cycle) * burst.repeatInterval;
                if (eventTime > emitter.emissionTimeSeconds) {
                    break;
                }
                if (eventTime >= previousEmissionTime
                    && random01(emitter.randomState) <= burst.probability) {
                    const auto count = burst.minCount == burst.maxCount
                        ? burst.minCount
                        : static_cast<uint32_t>(randomRange(emitter.randomState,
                              static_cast<float>(burst.minCount),
                              static_cast<float>(burst.maxCount) + 1.0f));
                    spawnFromEmitter(emitter, count);
                }
                ++cycle;
            }
        }
    }

    void retireAndSimulate(float deltaSeconds)
    {
        size_t writeIndex = 0;
        for (size_t readIndex = 0; readIndex < particles_.size(); ++readIndex) {
            auto particle = particles_[readIndex];
            particle.ageSeconds += deltaSeconds;
            if (particle.ageSeconds >= particle.lifetimeSeconds) {
                ++telemetry_.retiredThisFrame;
                continue;
            }
            const auto& emitter = emitters_[particle.emitterIndex];
            if (!emitter.desc.worldSpaceEmission) {
                particle.position.x += hostVelocity_.x * deltaSeconds;
                particle.position.y += hostVelocity_.y * deltaSeconds;
                particle.position.z += hostVelocity_.z * deltaSeconds;
            }
            // Inherit host velocity
            if (emitter.desc.inheritVelocityFactor != 0.0f) {
                particle.velocity.x += emitter.desc.inheritVelocityFactor
                    * (hostVelocity_.x - particle.velocity.x) * deltaSeconds;
                particle.velocity.y += emitter.desc.inheritVelocityFactor
                    * (hostVelocity_.y - particle.velocity.y) * deltaSeconds;
                particle.velocity.z += emitter.desc.inheritVelocityFactor
                    * (hostVelocity_.z - particle.velocity.z) * deltaSeconds;
            }
            particle.velocity.y += emitter.desc.gravityAcceleration * deltaSeconds;
            particle.position.x += particle.velocity.x * deltaSeconds;
            particle.position.y += particle.velocity.y * deltaSeconds;
            particle.position.z += particle.velocity.z * deltaSeconds;

            // Collision detection: plane at Y = collisionPlaneY
            if (emitter.desc.collisionEnabled
                && particle.position.y <= emitter.desc.collisionPlaneY) {
                particle.position.y = emitter.desc.collisionPlaneY;
                particle.velocity.y = -particle.velocity.y * emitter.desc.collisionBounceFactor;
                ++telemetry_.collisionEvents;

                // Sub-emitter burst on collision
                if (emitter.desc.collisionSpawnsSubEmitter
                    && particle.ageSeconds < particle.lifetimeSeconds * 0.5f) {
                    spawnSubEmitter(particle.emitterIndex,
                        particle.position,
                        emitter.desc.subEmitterMaxParticles);
                }
            }

            // Flipbook frame advancement
            if (emitter.desc.flipbookFrameCount > 1) {
                float frameFloat = static_cast<float>(particle.flipbookFrame)
                    + emitter.desc.flipbookPlayRate * deltaSeconds
                        * static_cast<float>(emitter.desc.flipbookFrameCount);
                const auto totalFrames = static_cast<float>(emitter.desc.flipbookFrameCount);
                if (emitter.desc.flipbookLooping) {
                    while (frameFloat >= totalFrames)
                        frameFloat -= totalFrames;
                } else {
                    frameFloat = std::min(frameFloat, totalFrames - 1.0f);
                }
                particle.flipbookFrame = static_cast<uint32_t>(frameFloat);
            }

            // Rotation advancement
            particle.rotation += emitter.desc.rotationSpeed * deltaSeconds;

            // Orbit motion
            if (emitter.desc.orbitEnabled && emitter.desc.orbitSpeed != 0.0f) {
                float orbitAngle = particle.normalizedAge() * emitter.desc.orbitSpeed * 6.283185f;
                float cosA = std::cos(orbitAngle);
                float sinA = std::sin(orbitAngle);
                float dx = particle.position.x - emitter.desc.orbitCenter.x;
                float dz = particle.position.z - emitter.desc.orbitCenter.z;
                particle.position.x = emitter.desc.orbitCenter.x + (dx * cosA - dz * sinA);
                particle.position.z = emitter.desc.orbitCenter.z + (dx * sinA + dz * cosA);
            }

            // Noise perturbation (deterministic: uses particle seed + age)
            if (emitter.desc.noiseFrequency > 0.0f && emitter.desc.noiseAmplitude > 0.0f) {
                float phase = particle.normalizedAge() * emitter.desc.noiseFrequency * 6.283185f
                    + static_cast<float>(particle.seed) * 0.127f;
                float noiseX = std::sin(phase * 1.3f) * std::cos(phase * 0.7f);
                float noiseY = std::cos(phase * 0.9f) * std::sin(phase * 1.1f);
                float noiseZ = std::sin(phase * 0.8f) * std::cos(phase * 1.4f);
                particle.position.x += noiseX * emitter.desc.noiseAmplitude * deltaSeconds;
                particle.position.y += noiseY * emitter.desc.noiseAmplitude * deltaSeconds;
                particle.position.z += noiseZ * emitter.desc.noiseAmplitude * deltaSeconds;
            }

            // External force (wind / constant force), scaled by ExternalForcesModule multiplier
            if (emitter.desc.externalForce.x != 0.0f || emitter.desc.externalForce.y != 0.0f
                || emitter.desc.externalForce.z != 0.0f) {
                const auto m = emitter.desc.externalForcesMultiplier;
                particle.velocity.x += emitter.desc.externalForce.x * m * deltaSeconds;
                particle.velocity.y += emitter.desc.externalForce.y * m * deltaSeconds;
                particle.velocity.z += emitter.desc.externalForce.z * m * deltaSeconds;
            }

            const auto t = particle.normalizedAge();
            particle.size = sampleSizeOverLifetime(emitter, t);
            const float alpha = sampleAlphaOverLifetime(emitter, t);
            particle.color = lerpColor(emitter.desc.startColor, emitter.desc.endColor, t);
            particle.color.a = alpha;
            particles_[writeIndex++] = particle;
        }
        particles_.resize(writeIndex);
    }

    ParticleSystemDesc desc_;
    core::pmr::Vector<EmitterState> emitters_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ParticleInstance> particles_{&core::defaultCpuMemoryResource()};
    core::Vec3 hostVelocity_{0.0f, 0.0f, 0.0f};
    ParticleSystemTelemetry telemetry_;
};

} // namespace arp::renderer
