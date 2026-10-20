#include "resource/particle_asset_resources.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

// UUID fixtures for testing
constexpr const char* kShaderPackageUuid = "a1b2c3d4-e5f6-7890-abcd-ef1234567890";
constexpr const char* kMaterialAssetUuid = "b2c3d4e5-f6a7-8901-bcde-f12345678901";
constexpr const char* kMaterialBindingUuid = "c3d4e5f6-a7b8-9012-cdef-123456789012";
constexpr const char* kTextureBindingUuid = "d4e5f6a7-b890-1234-def1-234567890123";

arp::resource::ParticleEffectResource validEffect()
{
    arp::resource::ParticleEffectResource effect;
    effect.name = "SparkleBurst";
    effect.contractVersion = "particle.effect/v1";
    effect.maxParticles = 64;
    effect.maxEmitters = 4;
    effect.qualityProfile = "Medium";
    effect.shaderPackageId = kShaderPackageUuid;
    effect.materialAssetId = kMaterialAssetUuid;

    arp::resource::ParticleEmitterAssetDesc emitter;
    emitter.name = "MainBurst";
    emitter.maxParticles = 32;
    emitter.burstCount = 8;
    emitter.spawnRatePerSecond = 0.0f;
    emitter.particleLifetimeSeconds = 1.5f;
    emitter.looping = false;
    emitter.enabled = true;
    emitter.seed = 42;
    emitter.origin = {0.0f, 1.0f, 0.0f};
    emitter.velocityMin = {-1.0f, 0.5f, -1.0f};
    emitter.velocityMax = {1.0f, 2.0f, 1.0f};
    emitter.startSize = 0.5f;
    emitter.endSize = 0.1f;
    emitter.startColorR = 1.0f;
    emitter.startColorG = 0.8f;
    emitter.startColorB = 0.2f;
    emitter.startColorA = 1.0f;
    emitter.endColorR = 1.0f;
    emitter.endColorG = 0.4f;
    emitter.endColorB = 0.0f;
    emitter.endColorA = 0.0f;
    emitter.materialBindingId = kMaterialBindingUuid;
    emitter.textureBindingId = kTextureBindingUuid;
    emitter.visualBinding.shaderFamilyId = "ParticleSpriteAdditive";
    emitter.visualBinding.blendMode = "Additive";
    emitter.visualBinding.billboardMode = "ViewAligned";
    emitter.visualBinding.tintPolicy = "Modulate";
    emitter.visualBinding.materialAssetId = kMaterialBindingUuid;
    emitter.visualBinding.textureAssetId = kTextureBindingUuid;
    emitter.visualBinding.isTextured = true;
    emitter.visualBinding.isAdditiveBlend = true;

    effect.emitters.push_back(emitter);
    return effect;
}

void particle_effect_valid_passes_validation()
{
    auto effect = validEffect();
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(result.success);
}

void particle_effect_rejects_missing_name()
{
    auto effect = validEffect();
    effect.name.clear();
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("missing name") != std::pmr::string::npos);
}

void particle_effect_rejects_unknown_contract_version()
{
    auto effect = validEffect();
    effect.contractVersion = "unknown/v999";
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("unsupported") != std::pmr::string::npos);
}

void particle_effect_accepts_legacy_v1_contract_version()
{
    auto effect = validEffect();
    effect.contractVersion = "v1";
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(result.success);
}

void particle_effect_rejects_zero_max_particles()
{
    auto effect = validEffect();
    effect.maxParticles = 0;
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("maxParticles must be > 0") != std::pmr::string::npos);
}

void particle_effect_rejects_emitter_capacity_exceeds_budget()
{
    auto effect = validEffect();
    effect.maxParticles = 16;
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("exceeds effect maxParticles") != std::pmr::string::npos);
}

void particle_effect_rejects_zero_emitter_lifetime()
{
    auto effect = validEffect();
    effect.emitters[0].particleLifetimeSeconds = 0.0f;
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("particleLifetimeSeconds <= 0") != std::pmr::string::npos);
}

void particle_effect_accepts_shape_contract()
{
    auto effect = validEffect();
    effect.emitters[0].emitterShape = "Cone";
    effect.emitters[0].coneAngleDegrees = 30.0f;
    effect.emitters[0].worldSpaceEmission = true;
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(result.success);
}

void particle_effect_accepts_directional_forward_shape_contract()
{
    auto effect = validEffect();
    effect.emitters[0].emitterShape = "DirectionalForward";
    effect.emitters[0].velocityMin = {0.0f, 4.0f, 0.0f};
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(result.success);
}

void particle_effect_accepts_radial_burst_shape_contract()
{
    auto effect = validEffect();
    effect.emitters[0].emitterShape = "RadialBurst";
    effect.emitters[0].sphereRadius = 1.0f;
    effect.emitters[0].velocityMin = {0.0f, 5.0f, 0.0f};
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(result.success);
}

void particle_effect_rejects_radial_burst_zero_speed_contract()
{
    auto effect = validEffect();
    effect.emitters[0].emitterShape = "RadialBurst";
    effect.emitters[0].sphereRadius = 1.0f;
    effect.emitters[0].velocityMin = {0.0f, 0.0f, 0.0f};
    effect.emitters[0].velocityMax = {0.0f, 0.0f, 0.0f};
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("RadialBurst contract") != std::pmr::string::npos);
}

void particle_effect_rejects_unknown_shape_contract()
{
    auto effect = validEffect();
    effect.emitters[0].emitterShape = "WeirdShape";
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("emitterShape") != std::pmr::string::npos);
}

void particle_effect_rejects_duplicate_emitter_name()
{
    auto effect = validEffect();
    effect.emitters.push_back(effect.emitters[0]);
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("duplicate emitter name") != std::pmr::string::npos);
}

void particle_effect_rejects_unsorted_curve()
{
    auto effect = validEffect();
    effect.emitters[0].sizeOverLifetime.keys.push_back({1.0f, 2.0f});
    effect.emitters[0].sizeOverLifetime.keys.push_back({0.0f, 1.0f});
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("invalid sizeOverLifetime curve") != std::pmr::string::npos);
}

void particle_effect_dependencies_include_bindings()
{
    auto effect = validEffect();
    const auto deps = effect.dependencies();
    bool foundShader = false;
    bool foundMaterial = false;
    bool foundTexture = false;
    for (const auto& dep : deps) {
        if (dep.id == kShaderPackageUuid) {
            foundShader = true;
        }
        if (dep.id == kMaterialAssetUuid) {
            foundMaterial = true;
        }
        if (dep.id == kTextureBindingUuid) {
            foundTexture = true;
        }
    }
    assert(foundShader);
    assert(foundMaterial);
    assert(foundTexture);
}

void particle_effect_builds_package_entry()
{
    auto effect = validEffect();
    arp::resource::AssetResourcePackageBuildDesc buildDesc;
    buildDesc.resourceId = "e5f6a7b8-9012-3456-def1-234567890abc";
    buildDesc.alias = "SparkleBurst";
    buildDesc.status = "Ready";
    buildDesc.envelopePath = "resources/particle-effect.arpres.yaml";

    const auto result = effect.buildPackageEntry(buildDesc);
    assert(result.success);
    assert(result.entry.resourceType == arp::resource::ParticleEffectResource::resourceTypeName());
    assert(result.entry.alias == "SparkleBurst");
    assert(!result.entry.envelopeYaml.empty());
    assert(result.entry.envelopeYaml.find("ParticleEffectResource") != std::pmr::string::npos);
    assert(result.entry.envelopeYaml.find("SparkleBurst") != std::pmr::string::npos);
    assert(result.entry.dependencies.size() >= 3);
}

void particle_effect_package_entry_rejects_invalid()
{
    auto effect = validEffect();
    effect.maxParticles = 0;
    arp::resource::AssetResourcePackageBuildDesc buildDesc;
    const auto result = effect.buildPackageEntry(buildDesc);
    assert(!result.success);
    assert(result.diagnostics.find("maxParticles must be > 0") != std::pmr::string::npos);
}

void particle_effect_rejects_invalid_shader_package_id()
{
    auto effect = validEffect();
    effect.shaderPackageId = "not-a-uuid";
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(
        result.diagnostics.find("shaderPackageId is not a valid UUID") != std::pmr::string::npos);
}

void particle_effect_rejects_invalid_material_asset_id()
{
    auto effect = validEffect();
    effect.shaderPackageId = "";
    effect.materialAssetId = "not-a-uuid-either";
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(
        result.diagnostics.find("materialAssetId is not a valid UUID") != std::pmr::string::npos);
}

void particle_effect_rejects_invalid_prefab_binding_id()
{
    auto effect = validEffect();
    effect.prefabBindingId = "bad-id";
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(
        result.diagnostics.find("prefabBindingId is not a valid UUID") != std::pmr::string::npos);
}

void particle_effect_rejects_invalid_emitter_material_binding()
{
    auto effect = validEffect();
    effect.emitters[0].materialBindingId = "invalid-uuid";
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("invalid materialBindingId") != std::pmr::string::npos);
}

void particle_effect_rejects_invalid_emitter_texture_binding()
{
    auto effect = validEffect();
    effect.emitters[0].materialBindingId.clear();
    effect.emitters[0].textureBindingId = "bad-format";
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("invalid textureBindingId") != std::pmr::string::npos);
}

void particle_effect_rejects_unknown_visual_shader_family()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.shaderFamilyId = "UnknownFamily";
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("visualBinding.shaderFamilyId") != std::pmr::string::npos);
}

void particle_effect_rejects_incomplete_visual_binding()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.blendMode.clear();
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("visualBinding.blendMode") != std::pmr::string::npos);
}

void particle_effect_rejects_textured_visual_binding_without_texture()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.textureAssetId.clear();
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("visualBinding.textureAssetId") != std::pmr::string::npos);
}

void particle_effect_rejects_additive_flag_mismatch()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.blendMode = "Alpha";
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("visualBinding.blendMode") != std::pmr::string::npos);
}

void particle_effect_rejects_material_binding_budget_exceeded()
{
    auto effect = validEffect();
    effect.maxMaterialBindings = 0;
    // Already has shaderPackageId + materialAssetId = 2 bindings, but budget is 0
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("exceeds maxMaterialBindings") != std::pmr::string::npos);
}

void particle_effect_rejects_texture_binding_budget_exceeded()
{
    auto effect = validEffect();
    effect.maxTextureBindings = 0;
    // Has one texture binding from emitter
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("exceeds maxTextureBindings") != std::pmr::string::npos);
}

void particle_effect_resource_type_name_is_stable()
{
    assert(std::string_view(arp::resource::ParticleEffectResource::resourceTypeName())
        == "ParticleEffectResource");
}

// --- Trail/Beam visual shader family contract tests ---

void particle_effect_accepts_trail_visual_shader_family()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.shaderFamilyId = "ParticleTrail";
    effect.emitters[0].visualBinding.blendMode = "Alpha";
    effect.emitters[0].visualBinding.isAdditiveBlend = false;
    effect.emitters[0].visualBinding.isTrail = true;
    effect.emitters[0].visualBinding.tintPolicy = "Modulate";
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(result.success);
}

void particle_effect_accepts_beam_visual_shader_family()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.shaderFamilyId = "ParticleBeam";
    effect.emitters[0].visualBinding.blendMode = "Alpha";
    effect.emitters[0].visualBinding.isAdditiveBlend = false;
    effect.emitters[0].visualBinding.isBeam = true;
    effect.emitters[0].visualBinding.tintPolicy = "Modulate";
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(result.success);
}

void particle_effect_accepts_lightning_thin_visual_shader_family()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.shaderFamilyId = "ParticleLightningThin";
    effect.emitters[0].visualBinding.blendMode = "Additive";
    effect.emitters[0].visualBinding.isAdditiveBlend = true;
    effect.emitters[0].visualBinding.isLightningThin = true;
    effect.emitters[0].visualBinding.tintPolicy = "Modulate";
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(result.success);
}

void particle_effect_rejects_trail_family_without_is_trail()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.shaderFamilyId = "ParticleTrail";
    effect.emitters[0].visualBinding.blendMode = "Alpha";
    effect.emitters[0].visualBinding.isAdditiveBlend = false;
    effect.emitters[0].visualBinding.tintPolicy = "Modulate";
    // isTrail is false (default)
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("isTrail") != std::pmr::string::npos);
}

void particle_effect_rejects_beam_family_without_is_beam()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.shaderFamilyId = "ParticleBeam";
    effect.emitters[0].visualBinding.blendMode = "Alpha";
    effect.emitters[0].visualBinding.isAdditiveBlend = false;
    effect.emitters[0].visualBinding.tintPolicy = "Modulate";
    // isBeam is false (default)
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("isBeam") != std::pmr::string::npos);
}

void particle_effect_accepts_storm_cloud_visual_shader_family()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.shaderFamilyId = "ParticleStormCloud";
    effect.emitters[0].visualBinding.blendMode = "Alpha";
    effect.emitters[0].visualBinding.isAdditiveBlend = false;
    effect.emitters[0].visualBinding.isStormCloud = true;
    effect.emitters[0].visualBinding.tintPolicy = "Modulate";
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(result.success);
}

void particle_effect_rejects_storm_cloud_family_without_is_storm_cloud()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.shaderFamilyId = "ParticleStormCloud";
    effect.emitters[0].visualBinding.blendMode = "Alpha";
    effect.emitters[0].visualBinding.isAdditiveBlend = false;
    // isStormCloud is false (default)
    effect.emitters[0].visualBinding.tintPolicy = "Modulate";
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("isStormCloud") != std::pmr::string::npos);
}

void particle_effect_rejects_is_storm_cloud_with_wrong_family()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.isStormCloud = true;
    // shaderFamilyId is "ParticleSpriteAdditive" (not "ParticleStormCloud")
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("shaderFamilyId") != std::pmr::string::npos);
    assert(result.diagnostics.find("isStormCloud") != std::pmr::string::npos);
}

void particle_effect_rejects_lightning_thin_family_without_additive_blend()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.shaderFamilyId = "ParticleLightningThin";
    effect.emitters[0].visualBinding.blendMode = "Alpha";
    effect.emitters[0].visualBinding.isAdditiveBlend = false;
    effect.emitters[0].visualBinding.isLightningThin = true;
    effect.emitters[0].visualBinding.tintPolicy = "Modulate";
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("blendMode") != std::pmr::string::npos);
    assert(result.diagnostics.find("ParticleLightningThin") != std::pmr::string::npos);
}

void particle_effect_rejects_is_trail_with_wrong_family()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.isTrail = true;
    // shaderFamilyId is "ParticleSpriteAdditive" (not "ParticleTrail")
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("shaderFamilyId") != std::pmr::string::npos);
    assert(result.diagnostics.find("isTrail") != std::pmr::string::npos);
}

void particle_effect_rejects_is_beam_with_wrong_family()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.isBeam = true;
    // shaderFamilyId is "ParticleSpriteAdditive" (not "ParticleBeam")
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("shaderFamilyId") != std::pmr::string::npos);
    assert(result.diagnostics.find("isBeam") != std::pmr::string::npos);
}

// --- Flipbook / SubUV contract tests ---

void particle_effect_accepts_flipbook_visual_binding()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.isFlipbook = true;
    effect.emitters[0].visualBinding.isTextured = true;
    effect.emitters[0].visualBinding.textureAssetId = kTextureBindingUuid;
    effect.emitters[0].flipbookTilesX = 4;
    effect.emitters[0].flipbookTilesY = 4;
    effect.emitters[0].flipbookFrameCount = 16;
    effect.emitters[0].flipbookPlayRate = 1.0f;
    effect.emitters[0].flipbookLooping = true;
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(result.success);
}

void particle_effect_rejects_flipbook_without_texture()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.isFlipbook = true;
    effect.emitters[0].visualBinding.isTextured = false;
    effect.emitters[0].visualBinding.textureAssetId.clear();
    effect.emitters[0].flipbookTilesX = 4;
    effect.emitters[0].flipbookTilesY = 4;
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("isTextured") != std::pmr::string::npos);
}

void particle_effect_rejects_flipbook_zero_tiles()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.isFlipbook = true;
    effect.emitters[0].visualBinding.isTextured = true;
    effect.emitters[0].visualBinding.textureAssetId = kTextureBindingUuid;
    effect.emitters[0].flipbookTilesX = 0;
    effect.emitters[0].flipbookTilesY = 4;
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("flipbookTiles") != std::pmr::string::npos);
}

void particle_effect_accepts_native_texture_source_metadata()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.textureSourcePath = "textures/pika_flipbook_explosion.png";
    effect.emitters[0].visualBinding.textureSrgb = true;
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(result.success);
}

void particle_effect_rejects_texture_source_path_without_texture_id()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.textureAssetId.clear();
    effect.emitters[0].visualBinding.textureSourcePath = "textures/pika_flipbook_explosion.png";
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("textureAssetId") != std::pmr::string::npos);
}

void particle_effect_rejects_texture_source_path_without_textured_flag()
{
    auto effect = validEffect();
    effect.emitters[0].visualBinding.isTextured = false;
    effect.emitters[0].visualBinding.textureSourcePath = "textures/pika_flipbook_explosion.png";
    const auto result = arp::resource::validateParticleEffectResource(effect);
    assert(!result.success);
    assert(result.diagnostics.find("isTextured") != std::pmr::string::npos);
}

void particle_effect_append_content_fingerprint_is_stable()
{
    auto effectA = validEffect();
    auto effectB = validEffect();
    arp::core::StableHash64 hashA;
    arp::core::StableHash64 hashB;
    effectA.appendContentFingerprint(hashA);
    effectB.appendContentFingerprint(hashB);
    // Same content should produce same hash
    assert(hashA.value() == hashB.value());
}

void particle_effect_append_content_fingerprint_changes_with_name()
{
    auto effectA = validEffect();
    auto effectB = validEffect();
    effectB.name = "DifferentName";
    arp::core::StableHash64 hashA;
    arp::core::StableHash64 hashB;
    effectA.appendContentFingerprint(hashA);
    effectB.appendContentFingerprint(hashB);
    // Different content should produce different hash
    assert(hashA.value() != hashB.value());
}

void particle_effect_package_reader_load_roundtrip()
{
    using namespace arp::resource;
    using namespace arp::core;

    // 1. Create a particle effect with emitter that has curves (hlist) + material/texture bindings
    ParticleEffectResource effect;
    effect.name = "ReaderTest";
    effect.contractVersion = kParticleEffectContract.contractVersion;
    effect.maxParticles = 128;
    effect.maxEmitters = 2;
    effect.maxMaterialBindings = 4;
    effect.maxTextureBindings = 2;
    effect.qualityProfile = "Medium";
    // Clear external dependencies so the package validator doesn't reject them
    // (external deps would be provided by the ResourceManager at load time)
    effect.shaderPackageId.clear();
    effect.materialAssetId.clear();

    ParticleEmitterAssetDesc emitter;
    emitter.name = "MainEmitter";
    emitter.maxParticles = 64;
    emitter.burstCount = 10;
    emitter.spawnRatePerSecond = 5.0f;
    emitter.particleLifetimeSeconds = 2.0f;
    emitter.looping = true;
    emitter.enabled = true;
    emitter.seed = 123;
    emitter.origin = {1.0f, 2.0f, 3.0f};
    emitter.velocityMin = {-2.0f, 0.0f, -2.0f};
    emitter.velocityMax = {2.0f, 4.0f, 2.0f};
    emitter.startSize = 0.8f;
    emitter.endSize = 0.2f;
    emitter.startColorR = 1.0f;
    emitter.startColorG = 0.5f;
    emitter.startColorB = 0.0f;
    emitter.startColorA = 1.0f;
    emitter.endColorR = 1.0f;
    emitter.endColorG = 0.0f;
    emitter.endColorB = 0.0f;
    emitter.endColorA = 0.0f;
    // Curves (hlist serialization path)
    emitter.sizeOverLifetime.keys.push_back({0.0f, 0.5f});
    emitter.sizeOverLifetime.keys.push_back({0.5f, 1.0f});
    emitter.sizeOverLifetime.keys.push_back({1.0f, 0.2f});
    emitter.colorAlphaOverLifetime.keys.push_back({0.0f, 1.0f});
    emitter.colorAlphaOverLifetime.keys.push_back({0.5f, 0.5f});
    emitter.colorAlphaOverLifetime.keys.push_back({1.0f, 0.0f});
    emitter.boundsExtent = {2.0f, 2.0f, 2.0f};
    // Keep materialBindingId/textureBindingId empty for standalone package round-trip
    // (external deps are tested separately via manager.declareResource)
    emitter.materialBindingId.clear();
    emitter.textureBindingId.clear();
    effect.emitters.push_back(emitter);

    // 2. Build package entry (serialize via buildResourceEnvelope)
    constexpr const char* kReaderTestUuid = "f1a2b3c4-d5e6-7890-abcd-ef1234567890";
    AssetResourcePackageBuildDesc buildDesc;
    buildDesc.resourceId = kReaderTestUuid;
    buildDesc.alias = "reader-test";
    buildDesc.displayName = "ReaderTestEffect";
    buildDesc.status = "Contract Complete";
    buildDesc.envelopePath = "resources/particle-effect/ReaderTest.arpres.yaml";

    const auto entryResult = effect.buildPackageEntry(buildDesc);
    assert(entryResult.success && "buildPackageEntry should succeed");
    assert(!entryResult.entry.envelopeYaml.empty());
    assert(entryResult.entry.envelopeYaml.find("ParticleEffectResource") != pmr::String::npos);
    assert(entryResult.entry.envelopeYaml.find("MainEmitter") != pmr::String::npos);
    // Verify hlist (curve) data is in the YAML
    assert(entryResult.entry.envelopeYaml.find("sizeOverLifetime") != pmr::String::npos
        && "envelope YAML should contain sizeOverLifetime curve data");

    // 3. Write package to temp directory
    InternalAssetPackageDesc packageDesc;
    packageDesc.name = "ReaderTestPackage";
    packageDesc.status = "Contract Complete";
    packageDesc.entries.push_back(entryResult.entry);

    const auto tmpDir = std::filesystem::temp_directory_path() / "arp_particle_reader_test";
    std::filesystem::create_directories(tmpDir);
    InternalAssetPackageWriter writer;
    const auto writeResult = writer.write(packageDesc, tmpDir);
    assert(writeResult.success && "package write should succeed");

    // 4. Read and load through InternalAssetPackageReader::load()
    ResourceManager manager;
    // Declare dependencies first so the envelope load doesn't fail on unresolved deps
    manager.declareResource(kShaderPackageUuid, "ShaderPackageResource");
    manager.declareResource(kMaterialAssetUuid, "MaterialResource");
    manager.declareResource(kMaterialBindingUuid, "MaterialResource");
    manager.declareResource(kTextureBindingUuid, "TextureResource");

    InternalAssetPackageReader reader;
    const auto loadResult = reader.load(tmpDir, manager);
    assert(loadResult.success && "InternalAssetPackageReader::load should succeed");
    assert(loadResult.state == ResourceState::Ready);

    // 5. Find the loaded resource and verify values
    auto handle = manager.find<ParticleEffectResource>(kReaderTestUuid);
    assert(handle.ready() && "loaded particle effect should be findable");
    assert(handle.get() != nullptr);
    assert(pmr::String(handle.get()->name.c_str(), &defaultCpuMemoryResource()) == "ReaderTest");
    assert(handle.get()->maxParticles == 128);
    assert(handle.get()->emitters.size() == 1);
    assert(handle.get()->emitters[0].name == "MainEmitter");
    assert(handle.get()->emitters[0].maxParticles == 64);
    assert(handle.get()->emitters[0].spawnRatePerSecond == 5.0f);
    // Verify curve keys survived round-trip
    assert(handle.get()->emitters[0].sizeOverLifetime.keys.size() == 3);
    assert(handle.get()->emitters[0].sizeOverLifetime.keys[0].time == 0.0f);
    assert(handle.get()->emitters[0].sizeOverLifetime.keys[1].time == 0.5f);
    assert(handle.get()->emitters[0].sizeOverLifetime.keys[2].time == 1.0f);
    assert(handle.get()->emitters[0].sizeOverLifetime.keys[2].value == 0.2f);
    // Verify alpha curve
    assert(handle.get()->emitters[0].colorAlphaOverLifetime.keys.size() == 3);
    assert(handle.get()->emitters[0].colorAlphaOverLifetime.keys[1].value == 0.5f);
    // Verify bindings (cleared for standalone package round-trip)
    assert(handle.get()->emitters[0].materialBindingId.empty());
    assert(handle.get()->emitters[0].textureBindingId.empty());
    assert(handle.get()->shaderPackageId.empty());
    assert(handle.get()->materialAssetId.empty());
    // Verify quality profile
    assert(handle.get()->qualityProfile == "Medium");

    // 6. Verify alias resolution
    auto aliasHandle = manager.findByAlias<ParticleEffectResource>("reader-test");
    assert(aliasHandle.ready() && "loaded particle effect should be findable by alias");

    // Cleanup
    std::filesystem::remove_all(tmpDir);
}

void particle_effect_package_reader_load_fails_for_nonexistent_directory()
{
    using namespace arp::resource;

    const auto tmpDir =
        std::filesystem::temp_directory_path() / "arp_particle_nonexistent_dir_should_not_exist";
    // Ensure directory does not exist
    std::filesystem::remove_all(tmpDir);

    ResourceManager manager;
    InternalAssetPackageReader reader;
    const auto loadResult = reader.load(tmpDir, manager);
    assert(!loadResult.success && "load should fail when package directory does not exist");
    assert(loadResult.state == ResourceState::Failed);
    assert(!loadResult.diagnostics.empty() && "diagnostics should explain the failure");
}

void particle_effect_package_reader_rejects_payload_hash_mismatch()
{
    using namespace arp::resource;
    using namespace arp::core;

    ParticleEffectResource effect;
    effect.name = "HashMismatchTest";
    effect.contractVersion = kParticleEffectContract.contractVersion;
    effect.maxParticles = 64;
    effect.maxEmitters = 1;
    effect.shaderPackageId.clear();
    effect.materialAssetId.clear();

    ParticleEmitterAssetDesc emitter;
    emitter.name = "TestEmitter";
    emitter.maxParticles = 32;
    emitter.burstCount = 5;
    emitter.spawnRatePerSecond = 10.0f;
    emitter.particleLifetimeSeconds = 1.0f;
    emitter.looping = false;
    emitter.enabled = true;
    emitter.materialBindingId.clear();
    emitter.textureBindingId.clear();
    effect.emitters.push_back(emitter);

    constexpr const char* kHashMismatchUuid = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeee0001";
    AssetResourcePackageBuildDesc buildDesc;
    buildDesc.resourceId = kHashMismatchUuid;
    buildDesc.alias = "hash-mismatch";
    buildDesc.status = "Contract Complete";
    buildDesc.envelopePath = "resources/particle-effect/HashMismatch.arpres.yaml";

    const auto entryResult = effect.buildPackageEntry(buildDesc);
    assert(entryResult.success);

    InternalAssetPackageDesc pkgDesc;
    pkgDesc.name = "HashMismatchPackage";
    pkgDesc.status = "Contract Complete";
    pkgDesc.entries.push_back(entryResult.entry);

    const auto tmpDir = std::filesystem::temp_directory_path() / "arp_particle_hash_mismatch";
    InternalAssetPackageWriter writer;
    const auto writeResult = writer.write(pkgDesc, tmpDir);
    assert(writeResult.success);

    // Find the envelope file and corrupt a payload field
    const auto envelopePath = tmpDir / "resources/particle-effect/HashMismatch.arpres.yaml";
    assert(std::filesystem::exists(envelopePath));

    std::string content;
    {
        auto file = std::fopen(envelopePath.string().c_str(), "r");
        assert(file != nullptr);
        std::fseek(file, 0, SEEK_END);
        const auto size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        content.resize(static_cast<std::size_t>(size));
        std::fread(content.data(), 1, content.size(), file);
        std::fclose(file);
    }

    // Corrupt the maxParticles field in the payload
    const std::string target = "maxParticles: 64";
    const auto pos = content.find(target);
    assert(pos != std::string::npos && "envelope YAML must contain maxParticles: 64");
    content.replace(pos, target.size(), "maxParticles: 99");

    {
        auto file = std::fopen(envelopePath.string().c_str(), "w");
        assert(file != nullptr);
        std::fwrite(content.data(), 1, content.size(), file);
        std::fclose(file);
    }

    // Load should fail due to contentHash mismatch
    ResourceManager manager;
    InternalAssetPackageReader reader;
    const auto loadResult = reader.load(tmpDir, manager);
    assert(!loadResult.success
        && "load should fail when envelope payload hash does not match contentHash");
    assert(loadResult.state == ResourceState::Failed);
    assert(loadResult.diagnostics.find("contentHash") != arp::core::pmr::String::npos
        || loadResult.diagnostics.find("hash") != arp::core::pmr::String::npos);

    std::filesystem::remove_all(tmpDir);
}

void particle_effect_resource_reload_commits_content_change()
{
    using namespace arp::resource;
    using namespace arp::core;

    constexpr const char* kReloadUuid = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeee0002";

    // 1. Create and load initial particle effect
    ParticleEffectResource effect;
    effect.name = "ReloadTest";
    effect.contractVersion = kParticleEffectContract.contractVersion;
    effect.maxParticles = 64;
    effect.maxEmitters = 2;
    effect.maxMaterialBindings = 4;
    effect.maxTextureBindings = 2;
    effect.qualityProfile = "Medium";
    effect.shaderPackageId.clear();
    effect.materialAssetId.clear();

    ParticleEmitterAssetDesc emitter;
    emitter.name = "Emitter1";
    emitter.maxParticles = 32;
    emitter.burstCount = 5;
    emitter.spawnRatePerSecond = 10.0f;
    emitter.particleLifetimeSeconds = 2.0f;
    emitter.looping = true;
    emitter.enabled = true;
    emitter.materialBindingId.clear();
    emitter.textureBindingId.clear();
    effect.emitters.push_back(emitter);

    AssetResourceRuntimeLoadDesc loadDesc;
    loadDesc.package.resourceId = kReloadUuid;
    loadDesc.package.alias = "reload-test";
    loadDesc.package.status = "Contract Complete";
    loadDesc.package.envelopePath = "resources/particle-effect/ReloadTest.arpres.yaml";

    ResourceManager manager;
    const auto initialResult = effect.loadInto(manager, loadDesc);
    assert(initialResult.success);

    auto handle = manager.find<ParticleEffectResource>(kReloadUuid);
    assert(handle.ready());
    assert(handle.generation() == 1u);
    assert(handle.get()->maxParticles == 64);
    assert(handle.get()->emitters.size() == 1u);
    assert(handle.get()->emitters[0].spawnRatePerSecond == 10.0f);

    // 2. Create modified particle effect with same UUID but changed values
    auto updated = arp::core::makeUnique<ParticleEffectResource>();
    updated->name = "ReloadTest";
    updated->contractVersion = kParticleEffectContract.contractVersion;
    updated->maxParticles = 128; // Changed from 64
    updated->maxEmitters = 3;    // Changed from 2
    updated->maxMaterialBindings = 4;
    updated->maxTextureBindings = 2;
    updated->qualityProfile = "High"; // Changed from Medium
    updated->shaderPackageId.clear();
    updated->materialAssetId.clear();

    ParticleEmitterAssetDesc updatedEmitter;
    updatedEmitter.name = "Emitter1";
    updatedEmitter.maxParticles = 64; // Changed from 32
    updatedEmitter.burstCount = 5;
    updatedEmitter.spawnRatePerSecond = 20.0f; // Changed from 10
    updatedEmitter.particleLifetimeSeconds = 2.0f;
    updatedEmitter.looping = true;
    updatedEmitter.enabled = true;
    updatedEmitter.materialBindingId.clear();
    updatedEmitter.textureBindingId.clear();
    updated->emitters.push_back(updatedEmitter);

    // Configure metadata to match original resource identity
    {
        AssetResourcePackageBuildDesc buildDesc;
        buildDesc.resourceId = kReloadUuid;
        buildDesc.alias = "reload-test";
        buildDesc.status = "Contract Complete";
        buildDesc.envelopePath = "resources/particle-effect/ReloadTest.arpres.yaml";
        const auto entry = updated->buildPackageEntry(buildDesc);
        assert(entry.success);
        updated->configureMetadata(
            ResourceReference{
                entry.entry.resourceId,
                entry.entry.resourceType,
                entry.entry.alias,
                entry.entry.status,
                arp::core::pmr::String("ReloadTest", &arp::core::defaultCpuMemoryResource()),
            },
            entry.entry.dependencies);
    }

    // Verify content hash differs from original
    const auto oldHandle = manager.find<ParticleEffectResource>(kReloadUuid);
    const std::uint32_t oldGeneration = oldHandle.generation();

    ResourceLoadContext context;
    const auto reloadResult = manager.reloadResource(std::move(updated), context);
    assert(reloadResult.success && "reload should succeed for modified content");
    assert(reloadResult.telemetry.committed && "reload should commit when content changed");
    assert(!reloadResult.unchangedContent && "content should be detected as changed");

    // 3. Verify new values are present and generation incremented
    handle = manager.find<ParticleEffectResource>(kReloadUuid);
    assert(handle.ready());
    assert(handle.generation() == oldGeneration + 1u);
    assert(handle.get()->maxParticles == 128);
    assert(handle.get()->maxEmitters == 3u);
    assert(handle.get()->qualityProfile == "High");
    assert(handle.get()->emitters.size() == 1u);
    assert(handle.get()->emitters[0].maxParticles == 64);
    assert(handle.get()->emitters[0].spawnRatePerSecond == 20.0f);
}

void particle_effect_resource_reload_rollback_on_invalid()
{
    using namespace arp::resource;
    using namespace arp::core;

    constexpr const char* kRollbackUuid = "bbbbcccc-dddd-eeee-ffff-000000000001";

    // 1. Load initial particle effect with valid content
    ParticleEffectResource effect;
    effect.name = "RollbackTest";
    effect.contractVersion = kParticleEffectContract.contractVersion;
    effect.maxParticles = 64;
    effect.maxEmitters = 2;
    effect.qualityProfile = "Medium";
    effect.shaderPackageId.clear();
    effect.materialAssetId.clear();

    AssetResourceRuntimeLoadDesc loadDesc;
    loadDesc.package.resourceId = kRollbackUuid;
    loadDesc.package.alias = "rollback-test";
    loadDesc.package.status = "Contract Complete";
    loadDesc.package.envelopePath = "resources/particle-effect/RollbackTest.arpres.yaml";

    ResourceManager manager;
    const auto initialResult = effect.loadInto(manager, loadDesc);
    assert(initialResult.success);

    auto handle = manager.find<ParticleEffectResource>(kRollbackUuid);
    assert(handle.ready());
    assert(handle.generation() == 1u);
    assert(handle.get()->maxParticles == 64);

    // 2. Attempt reload with invalid status string.
    //    An invalid status passes the promotion check (skipped when new status is unparsable)
    //    but causes admitResourceMetadata to fail AFTER reloadInProgress is set,
    //    triggering the rollback path.
    auto invalidReload = arp::core::makeUnique<ParticleEffectResource>();
    invalidReload->name = "RollbackTest";
    invalidReload->contractVersion = kParticleEffectContract.contractVersion;
    invalidReload->maxParticles = 128;
    invalidReload->qualityProfile = "Medium";
    invalidReload->shaderPackageId.clear();
    invalidReload->materialAssetId.clear();

    {
        AssetResourcePackageBuildDesc buildDesc;
        buildDesc.resourceId = kRollbackUuid;
        buildDesc.alias = "rollback-test";
        buildDesc.status = "InvalidUnknownStatus"; // Not a valid ResourceStatus!
        buildDesc.envelopePath = "resources/particle-effect/RollbackTest.arpres.yaml";
        const auto entry = invalidReload->buildPackageEntry(buildDesc);
        assert(entry.success);
        invalidReload->configureMetadata(
            ResourceReference{
                entry.entry.resourceId,
                entry.entry.resourceType,
                entry.entry.alias,
                entry.entry.status,
                arp::core::pmr::String("RollbackTest", &arp::core::defaultCpuMemoryResource()),
            },
            entry.entry.dependencies);
    }

    ResourceLoadContext context;
    const auto rollbackResult = manager.reloadResource(std::move(invalidReload), context);
    assert(!rollbackResult.success && "reload should fail for status demotion");
    assert(rollbackResult.telemetry.rolledBack && "reload should roll back on failure");

    // 3. Verify the original resource is unchanged
    handle = manager.find<ParticleEffectResource>(kRollbackUuid);
    assert(handle.ready());
    assert(handle.generation() == 1u && "generation must not change after failed reload");
    assert(handle.get()->maxParticles == 64 && "original value must survive failed reload");
}

} // namespace

int main()
{
    particle_effect_valid_passes_validation();
    particle_effect_rejects_missing_name();
    particle_effect_rejects_unknown_contract_version();
    particle_effect_accepts_legacy_v1_contract_version();
    particle_effect_rejects_zero_max_particles();
    particle_effect_rejects_emitter_capacity_exceeds_budget();
    particle_effect_rejects_zero_emitter_lifetime();
    particle_effect_accepts_shape_contract();
    particle_effect_accepts_directional_forward_shape_contract();
    particle_effect_accepts_radial_burst_shape_contract();
    particle_effect_rejects_radial_burst_zero_speed_contract();
    particle_effect_rejects_unknown_shape_contract();
    particle_effect_rejects_duplicate_emitter_name();
    particle_effect_rejects_unsorted_curve();
    particle_effect_dependencies_include_bindings();
    particle_effect_builds_package_entry();
    particle_effect_package_entry_rejects_invalid();
    particle_effect_resource_type_name_is_stable();
    particle_effect_rejects_invalid_shader_package_id();
    particle_effect_rejects_invalid_material_asset_id();
    particle_effect_rejects_invalid_prefab_binding_id();
    particle_effect_rejects_invalid_emitter_material_binding();
    particle_effect_rejects_invalid_emitter_texture_binding();
    particle_effect_rejects_unknown_visual_shader_family();
    particle_effect_rejects_incomplete_visual_binding();
    particle_effect_rejects_textured_visual_binding_without_texture();
    particle_effect_rejects_additive_flag_mismatch();
    particle_effect_rejects_material_binding_budget_exceeded();
    particle_effect_rejects_texture_binding_budget_exceeded();
    particle_effect_accepts_trail_visual_shader_family();
    particle_effect_accepts_beam_visual_shader_family();
    particle_effect_accepts_lightning_thin_visual_shader_family();
    particle_effect_accepts_storm_cloud_visual_shader_family();
    particle_effect_rejects_storm_cloud_family_without_is_storm_cloud();
    particle_effect_rejects_is_storm_cloud_with_wrong_family();
    particle_effect_rejects_trail_family_without_is_trail();
    particle_effect_rejects_beam_family_without_is_beam();
    particle_effect_rejects_lightning_thin_family_without_additive_blend();
    particle_effect_rejects_is_trail_with_wrong_family();
    particle_effect_rejects_is_beam_with_wrong_family();
    particle_effect_accepts_flipbook_visual_binding();
    particle_effect_rejects_flipbook_without_texture();
    particle_effect_rejects_flipbook_zero_tiles();
    particle_effect_accepts_native_texture_source_metadata();
    particle_effect_rejects_texture_source_path_without_texture_id();
    particle_effect_rejects_texture_source_path_without_textured_flag();
    particle_effect_append_content_fingerprint_is_stable();
    particle_effect_append_content_fingerprint_changes_with_name();
    particle_effect_package_reader_load_roundtrip();
    particle_effect_package_reader_load_fails_for_nonexistent_directory();
    particle_effect_package_reader_rejects_payload_hash_mismatch();
    particle_effect_resource_reload_commits_content_change();
    particle_effect_resource_reload_rollback_on_invalid();
    return 0;
}
