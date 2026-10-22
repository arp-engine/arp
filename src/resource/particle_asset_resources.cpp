#include "resource/particle_asset_resources.h"

#include "core/diagnostics/diagnostic_builder.h"
#include "core/text/utf8.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fkYAML/node.hpp>
#include <set>
#include <sstream>
#include <string_view>

namespace arp::reflection::generated {
void register_core_math_reflection();
void register_core_curve_reflection();
void register_particle_asset_resources_reflection();
} // namespace arp::reflection::generated

namespace arp::resource {

namespace {

struct ParticleRegistration {
    ParticleRegistration()
    {
        arp::reflection::generated::register_core_math_reflection();
        arp::reflection::generated::register_core_curve_reflection();
        arp::reflection::generated::register_particle_asset_resources_reflection();

        // Custom property: FloatCurve::keys → hlist (reflection serializer handles hlist,
        // not core::pmr::Vector<CurveKey> directly)
        {
            auto* cls = arp::reflection::db::instance().find_class("FloatCurve");
            if (cls != nullptr) {
                cls->def_property("keys",
                    arp::reflection::type_of<arp::reflection::hlist>(),
                    arp::reflection::object(arp::reflection::function(
                        [](arp::core::FloatCurve* value) -> arp::reflection::hlist {
                            arp::reflection::hlist result(&arp::core::defaultCpuMemoryResource());
                            for (const auto& key : value->keys) {
                                result.push_back(arp::reflection::object(key));
                            }
                            return result;
                        })),
                    arp::reflection::object(arp::reflection::function(
                        [](arp::core::FloatCurve* value, const arp::reflection::hlist& list) {
                            value->keys.clear();
                            for (const auto& item : list) {
                                value->keys.push_back(item.cast<const arp::core::CurveKey&>());
                            }
                        })),
                    arp::reflection::NameView(),
                    0);
            }
        }

        // Custom property: ParticleVisualBinding::shaderFamilyId (already handled natively)
        // The ASTRUCT/APROP macros generate native getter/setter entries, so no
        // manual def_property needed for ParticleVisualBinding.

        // Custom property: ParticleEffectResource::emitters → hlist
        {
            auto* cls = arp::reflection::db::instance().find_class("ParticleEffectResource");
            if (cls != nullptr) {
                cls->def_property("emitters",
                    arp::reflection::type_of<arp::reflection::hlist>(),
                    arp::reflection::object(arp::reflection::function(
                        [](arp::resource::ParticleEffectResource* value) -> arp::reflection::hlist {
                            arp::reflection::hlist result(&arp::core::defaultCpuMemoryResource());
                            for (const auto& emitter : value->emitters) {
                                result.push_back(arp::reflection::object(emitter));
                            }
                            return result;
                        })),
                    arp::reflection::object(
                        arp::reflection::function([](arp::resource::ParticleEffectResource* value,
                                                      const arp::reflection::hlist& list) {
                            value->emitters.clear();
                            for (const auto& item : list) {
                                value->emitters.push_back(
                                    item.cast<const arp::resource::ParticleEmitterAssetDesc&>());
                            }
                        })),
                    arp::reflection::NameView(),
                    0);
            }
        }

        // The allocator-backed burst schedule is reflected as a typed list.
        {
            auto* cls = arp::reflection::db::instance().find_class("ParticleEmitterAssetDesc");
            if (cls != nullptr) {
                cls->def_property("burstSchedule",
                    arp::reflection::type_of<arp::reflection::hlist>(),
                    arp::reflection::object(
                        arp::reflection::function([](arp::resource::ParticleEmitterAssetDesc* value)
                                                      -> arp::reflection::hlist {
                            arp::reflection::hlist result(&arp::core::defaultCpuMemoryResource());
                            for (const auto& burst : value->burstSchedule) {
                                result.push_back(arp::reflection::object(burst));
                            }
                            return result;
                        })),
                    arp::reflection::object(
                        arp::reflection::function([](arp::resource::ParticleEmitterAssetDesc* value,
                                                      const arp::reflection::hlist& list) {
                            value->burstSchedule.clear();
                            for (const auto& item : list) {
                                value->burstSchedule.push_back(
                                    item.cast<const arp::resource::ParticleBurstAssetDesc&>());
                            }
                        })),
                    arp::reflection::NameView(),
                    0);
            }
        }
    }
} g_registration;

using YamlNode = fkyaml::node;

arp::core::pmr::String yamlString(const YamlNode& node)
{
    if (!node.is_string()) {
        return {};
    }
    const auto& value = node.get_value_ref<const YamlNode::string_type&>();
    return arp::core::pmr::String(
        value.data(), value.size(), &arp::core::defaultCpuMemoryResource());
}

bool yamlFloat(const YamlNode& node, float& out)
{
    if (node.is_float_number()) {
        out = static_cast<float>(node.get_value<double>());
        return true;
    }
    if (node.is_integer()) {
        out = static_cast<float>(node.get_value<int>());
        return true;
    }
    return false;
}

bool yamlUint(const YamlNode& node, uint32_t& out)
{
    if (node.is_integer()) {
        out = static_cast<uint32_t>(node.get_value<int>());
        return true;
    }
    return false;
}

bool yamlBool(const YamlNode& node, bool& out)
{
    if (node.is_boolean()) {
        out = node.get_value<bool>();
        return true;
    }
    return false;
}

const YamlNode* findMappingValue(const YamlNode& mapping, const char* key)
{
    if (!mapping.is_mapping()) {
        return nullptr;
    }
    const auto& values = mapping.get_value_ref<const YamlNode::mapping_type&>();
    const auto found = values.find(YamlNode(key));
    return found == values.end() ? nullptr : &found->second;
}

bool parseVec3(const YamlNode& node, core::Vec3& out)
{
    if (!node.is_sequence() || node.get_value_ref<const YamlNode::sequence_type&>().size() < 3) {
        return false;
    }
    const auto& seq = node.get_value_ref<const YamlNode::sequence_type&>();
    return yamlFloat(seq[0], out.x) && yamlFloat(seq[1], out.y) && yamlFloat(seq[2], out.z);
}

bool parseCurve(const YamlNode& node, core::FloatCurve& out)
{
    if (!node.is_sequence()) {
        return false;
    }
    const auto& seq = node.get_value_ref<const YamlNode::sequence_type&>();
    for (const auto& item : seq) {
        if (!item.is_mapping()) {
            return false;
        }
        const auto* timeNode = findMappingValue(item, "time");
        const auto* valueNode = findMappingValue(item, "value");
        if (!timeNode || !valueNode) {
            return false;
        }
        core::CurveKey key;
        if (!yamlFloat(*timeNode, key.time) || !yamlFloat(*valueNode, key.value)) {
            return false;
        }
        out.keys.push_back(key);
    }
    return out.valid();
}

arp::core::pmr::String makeString(std::string_view text)
{
    return arp::core::pmr::String(text.data(), text.size(), &arp::core::defaultCpuMemoryResource());
}

arp::core::pmr::String makeDiagnostic(const char* text)
{
    return arp::core::pmr::String(text, &arp::core::defaultCpuMemoryResource());
}

arp::core::pmr::String makeDiagnostic(const char* prefix, const arp::core::pmr::String& value)
{
    arp::core::pmr::String result(prefix, &arp::core::defaultCpuMemoryResource());
    result += value;
    return result;
}

bool isAllowedValue(
    std::string_view value, std::initializer_list<std::string_view> allowed) noexcept
{
    for (const auto candidate : allowed) {
        if (candidate == value) {
            return true;
        }
    }
    return false;
}

bool hasVisualBindingContent(const ParticleVisualBinding& binding) noexcept
{
    return !binding.shaderFamilyId.empty() || !binding.blendMode.empty()
        || !binding.billboardMode.empty() || !binding.tintPolicy.empty()
        || !binding.materialAssetId.empty() || !binding.textureAssetId.empty()
        || !binding.textureSourcePath.empty() || binding.isTextured || binding.isAdditiveBlend
        || binding.isLightningThin || binding.isTrail || binding.isBeam || binding.isMeshParticle
        || binding.isFlipbook;
}

void appendVisualBindingValidationError(const ParticleEmitterAssetDesc& emitter,
    const char* fieldName,
    const char* detail,
    ParticleEffectValidationResult& result)
{
    result.diagnostics = "emitter '";
    result.diagnostics += emitter.name;
    result.diagnostics += "' has invalid visualBinding.";
    result.diagnostics += fieldName;
    result.diagnostics += ": ";
    result.diagnostics += detail;
}

} // namespace

core::pmr::Vector<ResourceDependency> ParticleEffectResource::dependencies() const
{
    core::pmr::Vector<ResourceDependency> result{&core::defaultCpuMemoryResource()};
    if (!shaderPackageId.empty()) {
        result.push_back({shaderPackageId, makeString(kUsageShaderPackage)});
    }
    if (!materialAssetId.empty()) {
        result.push_back({materialAssetId, makeString(kUsageMaterial)});
    }
    if (!prefabBindingId.empty()) {
        result.push_back({prefabBindingId, makeString(kUsagePrefabBinding)});
    }
    for (const auto& emitter : emitters) {
        // Legacy material/texture binding IDs (deprecated)
        if (!emitter.materialBindingId.empty()) {
            result.push_back({emitter.materialBindingId, makeString(kUsageMaterial)});
        }
        if (!emitter.textureBindingId.empty()) {
            result.push_back({emitter.textureBindingId, makeString(kUsageTexture)});
        }
        // Visual binding material/texture IDs (preferred path)
        if (!emitter.visualBinding.materialAssetId.empty()) {
            result.push_back({emitter.visualBinding.materialAssetId, makeString(kUsageMaterial)});
        }
        if (!emitter.visualBinding.textureAssetId.empty()) {
            result.push_back({emitter.visualBinding.textureAssetId, makeString(kUsageTexture)});
        }
    }
    return result;
}

AssetResourcePackageBuildResult ParticleEffectResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    AssetResourcePackageBuildResult result;
    result.success = false;

    const auto validated = validateParticleEffectResource(*this);
    if (!validated.success) {
        result.diagnostics = validated.diagnostics;
        return result;
    }

    result.entry.resourceId = desc.resourceId.empty() ? name : desc.resourceId;
    result.entry.resourceType = resourceTypeName();
    result.entry.alias = desc.alias;
    result.entry.status = desc.status;
    result.entry.envelopePath = desc.envelopePath;
    result.entry.contentHash = 0;
    result.entry.dependencies = dependencies();

    // Use reflection-based envelope building: serialize this resource through
    // the reflection system so the YAML format is compatible with what
    // InternalAssetPackageReader::load() expects from deserialize_from_yaml().
    ResourceEnvelopeBuildDesc envelopeDesc;
    envelopeDesc.resourceId = result.entry.resourceId;
    envelopeDesc.resourceType = result.entry.resourceType;
    envelopeDesc.alias = result.entry.alias;
    envelopeDesc.displayName = desc.displayName;
    envelopeDesc.status = result.entry.status;
    envelopeDesc.dependencies = result.entry.dependencies;

    const auto envelope = buildResourceEnvelope(envelopeDesc, arp::reflection::object(*this));
    if (!envelope.success) {
        result.diagnostics = envelope.diagnostics;
        return result;
    }

    result.entry.contentHash = envelope.contentHash;
    result.entry.envelopeYaml = envelope.envelopeYaml;
    result.success = true;
    return result;
}

ResourceLoadResult ParticleEffectResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    // Build package entry for validation and envelope generation
    const auto entry = buildPackageEntry(desc.package);
    if (!entry.success) {
        return ResourceLoadResult{
            false,
            ResourceState::Failed,
            desc.package.resourceId,
            makeString(resourceTypeName()),
            entry.diagnostics,
        };
    }

    // Create a copy of this resource, configure metadata, and load through manager
    auto typed = core::makeUnique<ParticleEffectResource>(*this);
    typed->configureMetadata(
        ResourceReference{
            entry.entry.resourceId,
            entry.entry.resourceType,
            entry.entry.alias,
            entry.entry.status,
            desc.package.displayName.empty() ? desc.package.alias : desc.package.displayName,
        },
        entry.entry.dependencies);

    return manager.loadResource(std::move(typed));
}

void ParticleEffectResource::appendContentFingerprint(core::StableHash64& hasher) const noexcept
{
    hasher.appendString(name);
    hasher.appendString(contractVersion);
    hasher.appendString(sourceHash);
    hasher.appendString(prefabBindingId);
    hasher.appendString(sidecarBlobPath);
    hasher.appendU64(sidecarBlobHash);
    hasher.appendU64(maxParticles);
    hasher.appendU64(maxEmitters);
    hasher.appendU64(maxMaterialBindings);
    hasher.appendU64(maxTextureBindings);
    hasher.appendString(qualityProfile);
    hasher.appendString(shaderPackageId);

    for (const auto& emitter : emitters) {
        hasher.appendString(emitter.name);
        hasher.appendU64(emitter.maxParticles);
        hasher.appendU64(emitter.burstCount);
        hasher.appendU64(emitter.burstSchedule.size());
        for (const auto& burst : emitter.burstSchedule) {
            hasher.appendFloat(burst.time);
            hasher.appendU64(burst.minCount);
            hasher.appendU64(burst.maxCount);
            hasher.appendU64(burst.cycleCount);
            hasher.appendFloat(burst.repeatInterval);
            hasher.appendFloat(burst.probability);
        }
        hasher.appendFloat(emitter.spawnRatePerSecond);
        hasher.appendFloat(emitter.particleLifetimeSeconds);
        hasher.appendFloat(emitter.gravityAcceleration);
        hasher.appendU64(emitter.looping ? 1u : 0u);
        hasher.appendU64(emitter.seed);
        hasher.appendFloat(emitter.origin.x);
        hasher.appendFloat(emitter.origin.y);
        hasher.appendFloat(emitter.origin.z);
        hasher.appendFloat(emitter.velocityMin.x);
        hasher.appendFloat(emitter.velocityMin.y);
        hasher.appendFloat(emitter.velocityMin.z);
        hasher.appendFloat(emitter.velocityMax.x);
        hasher.appendFloat(emitter.velocityMax.y);
        hasher.appendFloat(emitter.velocityMax.z);
        hasher.appendString(emitter.emitterShape);
        hasher.appendFloat(emitter.coneAngleDegrees);
        hasher.appendFloat(emitter.sphereRadius);
        hasher.appendFloat(emitter.boxExtents.x);
        hasher.appendFloat(emitter.boxExtents.y);
        hasher.appendFloat(emitter.boxExtents.z);
        hasher.appendU64(emitter.worldSpaceEmission ? 1u : 0u);
        hasher.appendFloat(emitter.startSize);
        hasher.appendFloat(emitter.endSize);
        hasher.appendFloat(emitter.startColorR);
        hasher.appendFloat(emitter.startColorG);
        hasher.appendFloat(emitter.startColorB);
        hasher.appendFloat(emitter.startColorA);
        hasher.appendFloat(emitter.endColorR);
        hasher.appendFloat(emitter.endColorG);
        hasher.appendFloat(emitter.endColorB);
        hasher.appendFloat(emitter.endColorA);
        hasher.appendString(emitter.materialBindingId);
        hasher.appendString(emitter.textureBindingId);
        // Visual binding (preferred path)
        hasher.appendString(emitter.visualBinding.shaderFamilyId);
        hasher.appendString(emitter.visualBinding.blendMode);
        hasher.appendString(emitter.visualBinding.billboardMode);
        hasher.appendString(emitter.visualBinding.tintPolicy);
        hasher.appendString(emitter.visualBinding.materialAssetId);
        hasher.appendString(emitter.visualBinding.textureAssetId);
        hasher.appendString(emitter.visualBinding.textureSourcePath);
        hasher.appendU64(emitter.visualBinding.textureSrgb ? 1u : 0u);
        hasher.appendU64(emitter.visualBinding.isTextured ? 1u : 0u);
        hasher.appendU64(emitter.visualBinding.isAdditiveBlend ? 1u : 0u);
        hasher.appendU64(emitter.visualBinding.isLightningThin ? 1u : 0u);
        hasher.appendU64(emitter.visualBinding.isStormCloud ? 1u : 0u);
        hasher.appendU64(emitter.visualBinding.isTrail ? 1u : 0u);
        hasher.appendU64(emitter.visualBinding.isBeam ? 1u : 0u);
        hasher.appendU64(emitter.visualBinding.isFlipbook ? 1u : 0u);
        hasher.appendU64(emitter.visualBinding.flipbookTilesX);
        hasher.appendU64(emitter.visualBinding.flipbookTilesY);
        hasher.appendU64(emitter.visualBinding.flipbookFrameCount);
        hasher.appendFloat(emitter.visualBinding.flipbookPlayRate);
        hasher.appendU64(emitter.visualBinding.flipbookLooping ? 1u : 0u);
        hasher.appendU64(emitter.visualBinding.flipbookRandomStart ? 1u : 0u);
        hasher.appendU64(emitter.visualBinding.isMeshParticle ? 1u : 0u);
        hasher.appendString(emitter.visualBinding.meshAssetId);
        hasher.appendString(emitter.visualBinding.meshMaterialId);
        hasher.appendU64(emitter.depthFadeEnabled ? 1u : 0u);
        hasher.appendFloat(emitter.depthFadeDistance);
        hasher.appendU64(emitter.collisionEnabled ? 1u : 0u);
        hasher.appendFloat(emitter.collisionPlaneY);
        hasher.appendFloat(emitter.collisionBounceFactor);
        hasher.appendU64(emitter.collisionSpawnsSubEmitter ? 1u : 0u);
        hasher.appendU64(emitter.subEmitterMaxParticles);
        hasher.appendU64(emitter.collisionUsesWorldPlane ? 1u : 0u);
        hasher.appendU64(emitter.triggerEnabled ? 1u : 0u);
        hasher.appendU64(static_cast<std::uint64_t>(emitter.triggerColliderType));
        hasher.appendFloat(emitter.triggerPlaneY);
        hasher.appendU64(emitter.triggerSpawnsSubEmitter ? 1u : 0u);
        hasher.appendString(emitter.triggerSubEmitterName);
        hasher.appendFloat(emitter.rotationSpeed);
        hasher.appendU64(emitter.orbitEnabled ? 1u : 0u);
        hasher.appendFloat(emitter.orbitRadius);
        hasher.appendFloat(emitter.orbitSpeed);
        hasher.appendFloat(emitter.orbitCenter.x);
        hasher.appendFloat(emitter.orbitCenter.y);
        hasher.appendFloat(emitter.orbitCenter.z);
        hasher.appendFloat(emitter.orbitAxis.x);
        hasher.appendFloat(emitter.orbitAxis.y);
        hasher.appendFloat(emitter.orbitAxis.z);
        hasher.appendFloat(emitter.noiseFrequency);
        hasher.appendFloat(emitter.noiseAmplitude);
        hasher.appendFloat(emitter.externalForce.x);
        hasher.appendFloat(emitter.externalForce.y);
        hasher.appendFloat(emitter.externalForce.z);
        hasher.appendFloat(emitter.inheritVelocityFactor);
        hasher.appendFloat(emitter.externalForcesMultiplier);
        hasher.appendFloat(emitter.boundsExtent.x);
        hasher.appendFloat(emitter.boundsExtent.y);
        hasher.appendFloat(emitter.boundsExtent.z);
        for (const auto& key : emitter.sizeOverLifetime.keys) {
            hasher.appendFloat(key.time);
            hasher.appendFloat(key.value);
        }
        for (const auto& key : emitter.colorAlphaOverLifetime.keys) {
            hasher.appendFloat(key.time);
            hasher.appendFloat(key.value);
        }
    }
}

ParticleEffectValidationResult validateParticleEffectResource(const ParticleEffectResource& effect)
{
    ParticleEffectValidationResult result;
    result.success = false;

    if (effect.name.empty()) {
        result.diagnostics = "particle effect missing name";
        return result;
    }

    if (!effect.contractVersion.empty()
        && effect.contractVersion != ParticleEffectResource::defaultContractVersion()
        && effect.contractVersion != "v1") {
        result.diagnostics = "unsupported particle effect contract version: ";
        result.diagnostics += effect.contractVersion;
        return result;
    }

    if (effect.maxParticles == 0) {
        result.diagnostics = "particle effect maxParticles must be > 0";
        return result;
    }

    if (effect.maxEmitters == 0) {
        result.diagnostics = "particle effect maxEmitters must be > 0";
        return result;
    }

    if (effect.emitters.size() > effect.maxEmitters) {
        result.diagnostics = "emitter count (";
        result.diagnostics += std::to_string(effect.emitters.size());
        result.diagnostics += ") exceeds maxEmitters (";
        result.diagnostics += std::to_string(effect.maxEmitters);
        result.diagnostics += ")";
        return result;
    }

    // Validate bind budget
    uint32_t materialBindCount = 0;
    uint32_t textureBindCount = 0;
    if (!effect.shaderPackageId.empty()) {
        if (isResourceUuid(effect.shaderPackageId)) {
            ++materialBindCount;
        } else {
            result.diagnostics = "shaderPackageId is not a valid UUID: ";
            result.diagnostics += effect.shaderPackageId;
            return result;
        }
    }
    if (!effect.materialAssetId.empty()) {
        if (isResourceUuid(effect.materialAssetId)) {
            ++materialBindCount;
        } else {
            result.diagnostics = "materialAssetId is not a valid UUID: ";
            result.diagnostics += effect.materialAssetId;
            return result;
        }
    }
    if (!effect.prefabBindingId.empty()) {
        if (!isResourceUuid(effect.prefabBindingId)) {
            result.diagnostics = "prefabBindingId is not a valid UUID: ";
            result.diagnostics += effect.prefabBindingId;
            return result;
        }
    }

    uint32_t totalEmitterCapacity = 0;
    std::set<std::pmr::string, std::less<>> emitterNames;
    for (const auto& emitter : effect.emitters) {
        if (emitter.maxParticles == 0) {
            result.diagnostics = makeDiagnostic("emitter '", emitter.name);
            result.diagnostics += "' has maxParticles == 0";
            return result;
        }
        for (const auto& burst : emitter.burstSchedule) {
            if (!std::isfinite(burst.time) || burst.time < 0.0f) {
                result.diagnostics = makeDiagnostic("emitter '", emitter.name);
                result.diagnostics += "' has burst time < 0 or non-finite";
                return result;
            }
            if (burst.minCount > burst.maxCount || burst.cycleCount == 0
                || !std::isfinite(burst.repeatInterval) || burst.repeatInterval < 0.0f
                || !std::isfinite(burst.probability) || burst.probability < 0.0f
                || burst.probability > 1.0f) {
                result.diagnostics = makeDiagnostic("emitter '", emitter.name);
                result.diagnostics += "' has invalid burst schedule entry";
                return result;
            }
        }
        if (emitter.particleLifetimeSeconds <= 0.0f) {
            result.diagnostics = makeDiagnostic("emitter '", emitter.name);
            result.diagnostics += "' has particleLifetimeSeconds <= 0";
            return result;
        }
        if (!emitter.sizeOverLifetime.keys.empty() && !emitter.sizeOverLifetime.valid()) {
            result.diagnostics = makeDiagnostic("emitter '", emitter.name);
            result.diagnostics += "' has invalid sizeOverLifetime curve (unsorted keys)";
            return result;
        }
        if (!emitter.colorAlphaOverLifetime.keys.empty()
            && !emitter.colorAlphaOverLifetime.valid()) {
            result.diagnostics = makeDiagnostic("emitter '", emitter.name);
            result.diagnostics += "' has invalid colorAlphaOverLifetime curve (unsorted keys)";
            return result;
        }
        if (!isAllowedValue(emitter.emitterShape,
                {"Point", "Cone", "Sphere", "Box", "DirectionalForward", "RadialBurst"})) {
            result.diagnostics = makeDiagnostic("emitter '", emitter.name);
            result.diagnostics += "' has unsupported emitterShape: ";
            result.diagnostics += emitter.emitterShape;
            return result;
        }
        if (emitter.emitterShape == "Cone" && emitter.coneAngleDegrees < 0.0f) {
            result.diagnostics = makeDiagnostic("emitter '", emitter.name);
            result.diagnostics += "' has coneAngleDegrees < 0";
            return result;
        }
        if (emitter.emitterShape == "Sphere" && emitter.sphereRadius < 0.0f) {
            result.diagnostics = makeDiagnostic("emitter '", emitter.name);
            result.diagnostics += "' has sphereRadius < 0";
            return result;
        }
        if (emitter.emitterShape == "Box"
            && (emitter.boxExtents.x < 0.0f || emitter.boxExtents.y < 0.0f
                || emitter.boxExtents.z < 0.0f)) {
            result.diagnostics = makeDiagnostic("emitter '", emitter.name);
            result.diagnostics += "' has boxExtents with negative component";
            return result;
        }
        if (emitter.emitterShape == "RadialBurst") {
            const auto speedMagnitudeSq = emitter.velocityMin.x * emitter.velocityMin.x
                + emitter.velocityMin.y * emitter.velocityMin.y
                + emitter.velocityMin.z * emitter.velocityMin.z;
            if (speedMagnitudeSq < 0.0001f * 0.0001f) {
                result.diagnostics = makeDiagnostic("emitter '", emitter.name);
                result.diagnostics +=
                    "' has invalid RadialBurst contract: |velocityMin| must be > 0";
                return result;
            }
        }

        // Validate material binding UUID if non-empty
        if (!emitter.materialBindingId.empty()) {
            if (!isResourceUuid(emitter.materialBindingId)) {
                result.diagnostics = "emitter '";
                result.diagnostics += emitter.name;
                result.diagnostics += "' has invalid materialBindingId (not UUID): ";
                result.diagnostics += emitter.materialBindingId;
                return result;
            }
            ++materialBindCount;
        }
        // Validate texture binding UUID if non-empty
        if (!emitter.textureBindingId.empty()) {
            if (!isResourceUuid(emitter.textureBindingId)) {
                result.diagnostics = "emitter '";
                result.diagnostics += emitter.name;
                result.diagnostics += "' has invalid textureBindingId (not UUID): ";
                result.diagnostics += emitter.textureBindingId;
                return result;
            }
            ++textureBindCount;
        }

        // Validate visual binding (preferred path)
        {
            const auto& vb = emitter.visualBinding;
            if (!vb.materialAssetId.empty() && !isResourceUuid(vb.materialAssetId)) {
                result.diagnostics = "emitter '";
                result.diagnostics += emitter.name;
                result.diagnostics += "' has invalid visualBinding.materialAssetId (not UUID): ";
                result.diagnostics += vb.materialAssetId;
                return result;
            }
            if (!vb.textureAssetId.empty() && !isResourceUuid(vb.textureAssetId)) {
                result.diagnostics = "emitter '";
                result.diagnostics += emitter.name;
                result.diagnostics += "' has invalid visualBinding.textureAssetId (not UUID): ";
                result.diagnostics += vb.textureAssetId;
                return result;
            }
            if (!vb.materialAssetId.empty())
                ++materialBindCount;
            if (!vb.textureAssetId.empty())
                ++textureBindCount;

            if (hasVisualBindingContent(vb)) {
                if (vb.shaderFamilyId.empty()) {
                    appendVisualBindingValidationError(emitter,
                        "shaderFamilyId",
                        "required when visualBinding is present",
                        result);
                    return result;
                }
                if (!isAllowedValue(vb.shaderFamilyId,
                        {
                            "ParticleSpriteAlpha",
                            "ParticleSpriteAdditive",
                            "ParticleLightningThin",
                            "ParticleStormCloud",
                            "ParticleFallbackLegacy",
                            "ParticleTrail",
                            "ParticleBeam",
                            "ParticleMeshParticle",
                        })) {
                    appendVisualBindingValidationError(
                        emitter, "shaderFamilyId", "unknown shader family", result);
                    return result;
                }

                if (vb.blendMode.empty()) {
                    appendVisualBindingValidationError(
                        emitter, "blendMode", "required when visualBinding is present", result);
                    return result;
                }
                if (!isAllowedValue(vb.blendMode, {"Alpha", "Additive", "Multiply", "Custom"})) {
                    appendVisualBindingValidationError(
                        emitter, "blendMode", "unknown blend mode", result);
                    return result;
                }

                if (vb.billboardMode.empty()) {
                    appendVisualBindingValidationError(
                        emitter, "billboardMode", "required when visualBinding is present", result);
                    return result;
                }
                if (!isAllowedValue(
                        vb.billboardMode, {"ViewAligned", "LocalAligned", "WorldAligned"})) {
                    appendVisualBindingValidationError(
                        emitter, "billboardMode", "unknown billboard mode", result);
                    return result;
                }

                if (vb.tintPolicy.empty()) {
                    appendVisualBindingValidationError(
                        emitter, "tintPolicy", "required when visualBinding is present", result);
                    return result;
                }
                if (!isAllowedValue(vb.tintPolicy, {"Modulate", "Override"})) {
                    appendVisualBindingValidationError(
                        emitter, "tintPolicy", "unknown tint policy", result);
                    return result;
                }

                if (vb.isTextured && vb.textureAssetId.empty()) {
                    appendVisualBindingValidationError(
                        emitter, "textureAssetId", "required when isTextured is true", result);
                    return result;
                }
                if (!vb.textureSourcePath.empty() && vb.textureAssetId.empty()) {
                    appendVisualBindingValidationError(emitter,
                        "textureAssetId",
                        "required when textureSourcePath is present",
                        result);
                    return result;
                }
                if (!vb.textureSourcePath.empty() && !vb.isTextured) {
                    appendVisualBindingValidationError(emitter,
                        "isTextured",
                        "must be true when textureSourcePath is present",
                        result);
                    return result;
                }
                if (vb.isAdditiveBlend && vb.blendMode != "Additive") {
                    appendVisualBindingValidationError(emitter,
                        "blendMode",
                        "must be Additive when isAdditiveBlend is true",
                        result);
                    return result;
                }
                if (vb.shaderFamilyId == "ParticleSpriteAdditive" && vb.blendMode != "Additive") {
                    appendVisualBindingValidationError(emitter,
                        "blendMode",
                        "must be Additive for ParticleSpriteAdditive",
                        result);
                    return result;
                }
                if (vb.shaderFamilyId == "ParticleLightningThin" && !vb.isLightningThin) {
                    appendVisualBindingValidationError(emitter,
                        "isLightningThin",
                        "must be true for ParticleLightningThin",
                        result);
                    return result;
                }
                if (vb.shaderFamilyId == "ParticleLightningThin" && vb.blendMode != "Additive") {
                    appendVisualBindingValidationError(
                        emitter, "blendMode", "must be Additive for ParticleLightningThin", result);
                    return result;
                }
                if (vb.shaderFamilyId == "ParticleStormCloud" && !vb.isStormCloud) {
                    appendVisualBindingValidationError(
                        emitter, "isStormCloud", "must be true for ParticleStormCloud", result);
                    return result;
                }
                if (vb.isStormCloud && vb.shaderFamilyId != "ParticleStormCloud") {
                    appendVisualBindingValidationError(emitter,
                        "shaderFamilyId",
                        "must be ParticleStormCloud when isStormCloud is true",
                        result);
                    return result;
                }
                if (vb.shaderFamilyId == "ParticleTrail" && !vb.isTrail) {
                    appendVisualBindingValidationError(
                        emitter, "isTrail", "must be true for ParticleTrail", result);
                    return result;
                }
                if (vb.shaderFamilyId == "ParticleBeam" && !vb.isBeam) {
                    appendVisualBindingValidationError(
                        emitter, "isBeam", "must be true for ParticleBeam", result);
                    return result;
                }
                if (vb.isTrail && vb.shaderFamilyId != "ParticleTrail") {
                    appendVisualBindingValidationError(emitter,
                        "shaderFamilyId",
                        "must be ParticleTrail when isTrail is true",
                        result);
                    return result;
                }
                if (vb.isBeam && vb.shaderFamilyId != "ParticleBeam") {
                    appendVisualBindingValidationError(emitter,
                        "shaderFamilyId",
                        "must be ParticleBeam when isBeam is true",
                        result);
                    return result;
                }
                // Flipbook invariants
                if (vb.isFlipbook) {
                    if (!vb.isTextured) {
                        appendVisualBindingValidationError(emitter,
                            "isTextured",
                            "must be true when isFlipbook is true (flipbook requires a texture)",
                            result);
                        return result;
                    }
                    if (emitter.flipbookTilesX == 0 || emitter.flipbookTilesY == 0) {
                        appendVisualBindingValidationError(emitter,
                            "flipbookTiles",
                            "flipbookTilesX and flipbookTilesY must be > 0 when isFlipbook is true",
                            result);
                        return result;
                    }
                }
                // Mesh particle invariants
                if (vb.isMeshParticle) {
                    if (vb.shaderFamilyId != "ParticleMeshParticle") {
                        appendVisualBindingValidationError(emitter,
                            "shaderFamilyId",
                            "must be ParticleMeshParticle when isMeshParticle is true",
                            result);
                        return result;
                    }
                    if (emitter.meshAssetId.empty()) {
                        appendVisualBindingValidationError(
                            emitter, "meshAssetId", "required when isMeshParticle is true", result);
                        return result;
                    }
                }
                // Collision invariants
                if (emitter.collisionEnabled) {
                    if (emitter.collisionBounceFactor < 0.0f
                        || emitter.collisionBounceFactor > 1.0f) {
                        result.diagnostics = "emitter '";
                        result.diagnostics += emitter.name;
                        result.diagnostics += "' has invalid collisionBounceFactor (must be 0-1)";
                        return result;
                    }
                    if (emitter.collisionSpawnsSubEmitter && emitter.subEmitterMaxParticles == 0) {
                        result.diagnostics = "emitter '";
                        result.diagnostics += emitter.name;
                        result.diagnostics +=
                            "' has subEmitterMaxParticles == 0 when collisionSpawnsSubEmitter is true";
                        return result;
                    }
                }
                // Trigger invariants (curated subset: plane-only collider)
                if (emitter.triggerEnabled) {
                    if (emitter.triggerColliderType != 0) {
                        result.diagnostics = "emitter '";
                        result.diagnostics += emitter.name;
                        result.diagnostics +=
                            "' has unsupported triggerColliderType (only Plane 0 is supported)";
                        return result;
                    }
                    if (emitter.triggerSpawnsSubEmitter && emitter.triggerSubEmitterName.empty()) {
                        result.diagnostics = "emitter '";
                        result.diagnostics += emitter.name;
                        result.diagnostics +=
                            "' has triggerSpawnsSubEmitter=true but triggerSubEmitterName is empty";
                        return result;
                    }
                }
            }
        }

        totalEmitterCapacity += emitter.maxParticles;
        if (!emitter.name.empty()) {
            if (emitterNames.count(emitter.name.c_str())) {
                result.diagnostics = "duplicate emitter name: ";
                result.diagnostics += emitter.name;
                return result;
            }
            emitterNames.insert(emitter.name.c_str());
        }
    }

    if (totalEmitterCapacity > effect.maxParticles) {
        result.diagnostics = "total emitter capacity (";
        result.diagnostics += std::to_string(totalEmitterCapacity);
        result.diagnostics += ") exceeds effect maxParticles (";
        result.diagnostics += std::to_string(effect.maxParticles);
        result.diagnostics += ")";
        return result;
    }

    if (materialBindCount > effect.maxMaterialBindings) {
        result.diagnostics = "material binding count (";
        result.diagnostics += std::to_string(materialBindCount);
        result.diagnostics += ") exceeds maxMaterialBindings (";
        result.diagnostics += std::to_string(effect.maxMaterialBindings);
        result.diagnostics += ")";
        return result;
    }

    if (textureBindCount > effect.maxTextureBindings) {
        result.diagnostics = "texture binding count (";
        result.diagnostics += std::to_string(textureBindCount);
        result.diagnostics += ") exceeds maxTextureBindings (";
        result.diagnostics += std::to_string(effect.maxTextureBindings);
        result.diagnostics += ")";
        return result;
    }

    result.success = true;
    return result;
}

} // namespace arp::resource
