#pragma once

#include "core/hash/stable_hash.h"
#include "core/math/curve.h"
#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/reflection_macros.h"
#include "resource/internal_asset_package.h"
#include "resource/prefab_asset_resources.h"
#include "resource/resource_contract.h"
#include "resource/resource_manager.h"

#include <cstdint>

namespace arp::resource {

// Contract descriptor for particle effect resources.
inline constexpr ResourceContractDescriptor kParticleEffectContract{
    "ParticleEffectResource",
    "particle.effect/v1",
};

inline constexpr std::string_view kUsageShaderPackage = "shader-package";
inline constexpr std::string_view kUsageMaterial = "material";
inline constexpr std::string_view kUsageTexture = "texture";
inline constexpr std::string_view kUsagePrefabBinding = "prefab-binding";

// ---------------------------------------------------------------------------
// ParticleVisualBinding — typed descriptor for how a particle emitter should
// be rendered: which shader family, blend mode, billboard alignment, tint
// policy, and which material/texture resources to bind.
//
// All string fields are expected to contain deterministic identifiers:
// shader family UUIDs or well-known enum names. The validator rejects
// unknown/empty values for required fields.
// ---------------------------------------------------------------------------
ASTRUCT(Construct = Default)

struct ParticleVisualBinding {
    // Shader family identifier. Well-known values:
    //   "ParticleSpriteAlpha"    — alpha-blended textured/untextured sprite
    //   "ParticleSpriteAdditive" — additive-blended textured/untextured sprite
    //   "ParticleLightningThin"  — thin procedural lightning (electricity)
    //   "ParticleStormCloud"     — dark storm cloud with tribulation lightning (渡劫雷云)
    //   "ParticleFallbackLegacy" — shared procedural fallback (current default)
    //   "ParticleTrail"          — non-billboard trail / ribbon strip
    //   "ParticleBeam"           — non-billboard beam / laser segment
    APROP(Serialize)
    core::pmr::String shaderFamilyId{&core::defaultCpuMemoryResource()};

    // Blend mode. Well-known values: "Alpha", "Additive", "Multiply", "Custom".
    APROP(Serialize)
    core::pmr::String blendMode{&core::defaultCpuMemoryResource()};

    // Billboard alignment. Well-known values:
    //   "ViewAligned" — always face camera (default)
    //   "LocalAligned" — align to emitter local space
    //   "WorldAligned" — fixed world orientation
    APROP(Serialize)
    core::pmr::String billboardMode{&core::defaultCpuMemoryResource()};

    // Tint modulation policy. Well-known values:
    //   "Modulate" — multiply vertex color × texture (default)
    //   "Override" — replace texture color entirely
    APROP(Serialize)
    core::pmr::String tintPolicy{&core::defaultCpuMemoryResource()};

    // Material resource UUID (if the particle uses a dedicated material).
    APROP(Serialize)
    core::pmr::String materialAssetId{&core::defaultCpuMemoryResource()};

    // Texture resource UUID (main particle texture).
    APROP(Serialize)
    core::pmr::String textureAssetId{&core::defaultCpuMemoryResource()};

    // Optional feature flags — generic extension point for boolean visual
    // features that affect shader variant selection.
    APROP(Serialize)
    core::pmr::String textureSourcePath{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    bool textureSrgb = true;
    APROP(Serialize)
    bool isTextured = false;
    APROP(Serialize)
    bool isAdditiveBlend = false;
    APROP(Serialize)
    bool isLightningThin = false;
    APROP(Serialize)
    bool isStormCloud = false;
    APROP(Serialize)
    bool isTrail = false;
    APROP(Serialize)
    bool isBeam = false;
    APROP(Serialize)
    bool isMeshParticle = false;
    // Mesh particle binding (valid when isMeshParticle is true)
    APROP(Serialize)
    core::pmr::String meshAssetId{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    core::pmr::String meshMaterialId{&core::defaultCpuMemoryResource()};
    // Flipbook / SubUV animation (valid when the particle uses a sprite sheet)
    APROP(Serialize)
    bool isFlipbook = false;
    APROP(Serialize)
    uint32_t flipbookTilesX = 1;
    APROP(Serialize)
    uint32_t flipbookTilesY = 1;
    APROP(Serialize)
    uint32_t flipbookFrameCount = 1;
    APROP(Serialize)
    float flipbookPlayRate = 1.0f;
    APROP(Serialize)
    bool flipbookLooping = true;
    APROP(Serialize)
    bool flipbookRandomStart = false;
};

ASTRUCT(Construct = Default)

struct ParticleBurstAssetDesc {
    APROP(Serialize)
    float time = 0.0f;
    APROP(Serialize)
    uint32_t minCount = 0;
    APROP(Serialize)
    uint32_t maxCount = 0;
    APROP(Serialize)
    uint32_t cycleCount = 1;
    APROP(Serialize)
    float repeatInterval = 0.0f;
    APROP(Serialize)
    float probability = 1.0f;
};

ASTRUCT(Construct = Default)

struct ParticleEmitterAssetDesc {
    APROP(Serialize)
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    uint32_t maxParticles = 128;
    APROP(Serialize)
    uint32_t burstCount = 0;
    APROP(Serialize)
    core::pmr::Vector<ParticleBurstAssetDesc> burstSchedule{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    float spawnRatePerSecond = 0.0f;
    APROP(Serialize)
    float particleLifetimeSeconds = 1.0f;
    APROP(Serialize)
    float gravityAcceleration = 0.0f;
    APROP(Serialize)
    bool looping = true;
    APROP(Serialize)
    bool enabled = true;
    APROP(Serialize)
    uint32_t seed = 1;
    APROP(Serialize)
    core::Vec3 origin;
    APROP(Serialize)
    core::Vec3 velocityMin{0.0f, 1.0f, 0.0f};
    APROP(Serialize)
    core::Vec3 velocityMax{0.0f, 1.0f, 0.0f};
    // Internal emitter shape contract (deterministic lowering target from Unity ShapeModule)
    APROP(Serialize)
    core::pmr::String emitterShape{"Point", &core::defaultCpuMemoryResource()};
    APROP(Serialize)
    float coneAngleDegrees = 25.0f;
    APROP(Serialize)
    float sphereRadius = 1.0f;
    APROP(Serialize)
    core::Vec3 boxExtents{1.0f, 1.0f, 1.0f};
    APROP(Serialize)
    bool worldSpaceEmission = false;
    APROP(Serialize)
    float startSize = 1.0f;
    APROP(Serialize)
    float endSize = 1.0f;
    APROP(Serialize)
    float startColorR = 1.0f;
    APROP(Serialize)
    float startColorG = 1.0f;
    APROP(Serialize)
    float startColorB = 1.0f;
    APROP(Serialize)
    float startColorA = 1.0f;
    APROP(Serialize)
    float endColorR = 1.0f;
    APROP(Serialize)
    float endColorG = 1.0f;
    APROP(Serialize)
    float endColorB = 1.0f;
    APROP(Serialize)
    float endColorA = 0.0f;
    APROP(Serialize)
    core::FloatCurve sizeOverLifetime;
    APROP(Serialize)
    core::FloatCurve colorAlphaOverLifetime;
    APROP(Serialize)
    core::Vec3 boundsExtent{1.0f, 1.0f, 1.0f};
    // DEPRECATED: Prefer visualBinding. Kept for backward compat during migration.
    APROP(Serialize)
    core::pmr::String materialBindingId{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    core::pmr::String textureBindingId{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    ParticleVisualBinding visualBinding;
    // Trail / Beam emitter contract (valid when visualFamily is Trail or Beam)
    APROP(Serialize)
    float trailWidth = 0.1f;
    APROP(Serialize)
    uint32_t trailMaxSegments = 32;
    APROP(Serialize)
    float beamLength = 1.0f;
    // Mesh particle contract (valid when visualBinding.isMeshParticle is true)
    APROP(Serialize)
    core::pmr::String meshAssetId{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    core::pmr::String meshMaterialId{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    float meshScaleMin = 1.0f;
    APROP(Serialize)
    float meshScaleMax = 1.0f;
    // Soft particle / Depth fade contract (valid when depthFadeEnabled)
    APROP(Serialize)
    bool depthFadeEnabled = false;
    APROP(Serialize)
    float depthFadeDistance = 0.5f;
    // Collision / Trigger / Sub-emitter contract
    APROP(Serialize)
    bool collisionEnabled = false;
    APROP(Serialize)
    float collisionPlaneY = 0.0f;
    APROP(Serialize)
    float collisionBounceFactor = 0.3f;
    APROP(Serialize)
    bool collisionSpawnsSubEmitter = false;
    APROP(Serialize)
    uint32_t subEmitterMaxParticles = 32;
    APROP(Serialize)
    bool collisionUsesWorldPlane = true;
    // Collision / Trigger / Sub-emitter contract
    APROP(Serialize)
    bool collisionSpawnsOnTriggerEnabled = false; // TriggerModule plane-only curated subset
    // TriggerModule curated subset — Plane collider type 0 only.
    // Scene-collider linking and non-plane colliders are deterministic
    // diagnostic failures in the importer.
    APROP(Serialize)
    bool triggerEnabled = false;
    APROP(Serialize)
    int triggerColliderType = 0; // 0=Plane (only supported)
    APROP(Serialize)
    float triggerPlaneY = 0.0f;
    APROP(Serialize)
    bool triggerSpawnsSubEmitter = false;
    APROP(Serialize)
    core::pmr::String triggerSubEmitterName{&core::defaultCpuMemoryResource()};
    // Rotation / Orbit contract
    APROP(Serialize)
    float rotationSpeed = 0.0f;
    APROP(Serialize)
    bool orbitEnabled = false;
    APROP(Serialize)
    float orbitRadius = 1.0f;
    APROP(Serialize)
    float orbitSpeed = 0.0f;
    APROP(Serialize)
    core::Vec3 orbitCenter{0.0f, 0.0f, 0.0f};
    APROP(Serialize)
    core::Vec3 orbitAxis{0.0f, 1.0f, 0.0f};
    // Noise / Force field contract
    APROP(Serialize)
    float noiseFrequency = 0.0f;
    APROP(Serialize)
    float noiseAmplitude = 0.0f;
    APROP(Serialize)
    core::Vec3 externalForce{0.0f, 0.0f, 0.0f};
    // Inherit velocity contract
    APROP(Serialize)
    float inheritVelocityFactor = 0.0f;
    // ExternalForcesModule subset (multiplier scales externalForce)
    APROP(Serialize)
    float externalForcesMultiplier = 1.0f;
    // Flipbook / SubUV animation contract (valid when visualBinding.isFlipbook is true)
    APROP(Serialize)
    uint32_t flipbookTilesX = 1;
    APROP(Serialize)
    uint32_t flipbookTilesY = 1;
    APROP(Serialize)
    uint32_t flipbookFrameCount = 1;
    APROP(Serialize)
    float flipbookPlayRate = 1.0f;
    APROP(Serialize)
    bool flipbookLooping = true;
    APROP(Serialize)
    bool flipbookRandomStart = false;
};

ASTRUCT(Construct = Default)

struct ParticleEffectResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept
    {
        return kParticleEffectContract.type.data();
    }

    static constexpr const char* defaultContractVersion() noexcept
    {
        return kParticleEffectContract.contractVersion.data();
    }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/particle-effect.arpres.yaml";
    }

    APROP(Serialize)
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    core::pmr::String sourcePath{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    core::pmr::String contractVersion{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    core::pmr::String sourceHash{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    core::pmr::String prefabBindingId{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    core::pmr::String sidecarBlobPath{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    std::uint32_t sidecarBlobHash = 0u;
    core::pmr::Vector<ParticleEmitterAssetDesc> emitters{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    core::pmr::String shaderPackageId{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    core::pmr::String materialAssetId{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    uint32_t maxParticles = 1024;
    APROP(Serialize)
    uint32_t maxEmitters = 32;
    APROP(Serialize)
    uint32_t maxMaterialBindings = 8;
    APROP(Serialize)
    uint32_t maxTextureBindings = 8;
    APROP(Serialize)
    core::pmr::String qualityProfile{&core::defaultCpuMemoryResource()};

    core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
    void appendContentFingerprint(core::StableHash64& hasher) const noexcept override;
};

struct ParticleEffectValidationResult {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

ParticleEffectValidationResult validateParticleEffectResource(const ParticleEffectResource& effect);

} // namespace arp::resource
