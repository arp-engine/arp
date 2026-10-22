#pragma once

// Shared imported asset data types consumed by the engine runtime (src/resource/).
//
// These types are extracted from the unity_* importer modules and represent
// the data contract between Unity import tools (tools/unity_import/) and the
// engine asset pipeline.
//
// DO NOT add parse/import function declarations here — those belong in
// tools/unity_import/.

#include "animation/animation.h"
#include "animation/animator_controller.h"
#include "animation/skeleton.h"
#include "assets/asset_pipeline.h"
#include "assets/glb_animated_mesh_importer.h"
#include "core/api.h"
#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace arp::assets {

// =========================================================================
// Shared diagnostic types
// =========================================================================

struct SourceLocation {
    std::filesystem::path path;
    std::uint32_t line = 0;
    std::uint32_t column = 0;

    [[nodiscard]] bool valid() const { return !path.empty() || line != 0 || column != 0; }
};

struct ImportReportRecord {
    std::pmr::string severity;
    std::pmr::string contract;
    std::pmr::string message;
    SourceLocation location;
};

struct ImportReport {
    std::pmr::vector<ImportReportRecord> records;

    void add(const std::pmr::string& severity,
        const std::pmr::string& contract,
        const std::pmr::string& message,
        const SourceLocation& location);
    [[nodiscard]] bool hasErrors() const;
    [[nodiscard]] std::pmr::string toText() const;
};

struct ImportReportWriteResult {
    bool success = false;
    std::pmr::string diagnostics;
};

// =========================================================================
// Avatar / Humanoid types (extracted from unity_model_importer_meta.h)
// =========================================================================

enum class AnimationType {
    Unknown = 0,
    Legacy = 1,
    Generic = 2,
    Humanoid = 3,
};

enum class AvatarSetup {
    NoAvatar = 0,
    CreateFromThisModel = 1,
    CopyFromOtherAvatar = 2,
    Unknown = 3,
};

enum class AvatarKind {
    Missing,
    Generic,
    Humanoid,
    Unsupported,
};

struct ClipAnimationSettings {
    std::pmr::string name;
    std::pmr::string takeName;
    bool loopTime = false;
    bool keepOriginalOrientation = false;
    bool keepOriginalPositionY = false;
    bool keepOriginalPositionXZ = false;
};

struct HumanBoneMapping {
    std::pmr::string sourceBoneName;
    std::pmr::string boneName;
    std::pmr::string humanName;
    SourceLocation location;
};

struct HumanSkeletonNode {
    std::pmr::string name;
    std::pmr::string parentName;
    std::pmr::string position;
    std::pmr::string rotation;
    std::pmr::string scale;
    SourceLocation location;
};

struct HumanDescription {
    bool declared = false;
    bool humanSectionDeclared = false;
    bool skeletonSectionDeclared = false;
    std::pmr::vector<HumanBoneMapping> humanBones;
    std::pmr::vector<HumanSkeletonNode> skeletonNodes;
};

struct ARP_API AvatarAsset {
    AvatarKind kind = AvatarKind::Missing;
    AnimationType animationType = AnimationType::Unknown;
    AvatarSetup setup = AvatarSetup::NoAvatar;
    std::filesystem::path sourceMetaPath;
    HumanDescription humanDescription;

    bool valid() const;
};

struct ModelImporterAsset {
    std::filesystem::path sourceMetaPath;
    AnimationType animationType = AnimationType::Unknown;
    AvatarSetup avatarSetup = AvatarSetup::NoAvatar;
    bool importAnimation = false;
    std::pmr::vector<ClipAnimationSettings> clipAnimations;
    HumanDescription humanDescription;
    AvatarAsset avatar;
};

struct ModelImporterParseResult {
    bool success = false;
    std::pmr::string diagnostics;
    ModelImporterAsset model;
};

// =========================================================================
// Character import types (extracted from unity_character_importer.h)
// =========================================================================

struct AnimatorStateAsset {
    std::pmr::string name;
    std::pmr::string motionGuid;
    std::filesystem::path sourceAnimationPath;
    std::pmr::string clipName;
    SourceLocation sourceLocation;
    SourceLocation motionLocation;
    float speed = 1.0f;
    int motionType = 0;
    bool skeletalClipRequired = false;
};

struct AnimatorStateMachineAsset {
    std::pmr::string name;
    std::pmr::string defaultStateName;
    std::pmr::vector<AnimatorStateAsset> states;
    animation::AnimatorControllerAsset animator;
};

struct AnimatorControllerParseResult {
    bool success = false;
    std::pmr::string diagnostics;
    AnimatorStateMachineAsset stateMachine;
};

struct MaterialTextureSlotAsset {
    std::pmr::string propertyName;
    std::pmr::string textureGuid;
    SourceLocation location;
    int baseColorPriority = 1000;
};

struct MaterialImporterAsset {
    std::filesystem::path sourcePath;
    std::pmr::string name;
    std::pmr::string materialGuid;
    std::pmr::vector<MaterialTextureSlotAsset> textureSlots;
    std::pmr::string baseColorTextureGuid;
    std::pmr::string baseColorTextureProperty;
    SourceLocation baseColorTextureLocation;
    bool hasCullMode = false;
    int cullMode = 2;
    bool hasSurface = false;
    int surface = 0;
    bool hasAlphaClip = false;
    int alphaClip = 0;
    bool hasAlphaCutoff = false;
    float alphaCutoff = 0.5f;
};

struct MaterialImporterParseResult {
    bool success = false;
    std::pmr::string diagnostics;
    MaterialImporterAsset material;
};

struct MaterialTextureReferenceValidationResult {
    bool success = false;
    std::pmr::string resolvedBaseColorTexturePath;
    ImportReport report;
};

struct FbxExternalMaterialRemap {
    std::pmr::string sourceMaterialName;
    std::pmr::string materialGuid;
    SourceLocation sourceLocation;
    SourceLocation materialLocation;
};

struct FbxExternalMaterialRemapParseResult {
    bool success = false;
    std::pmr::string diagnostics;
    std::pmr::vector<FbxExternalMaterialRemap> remaps;
};

struct CharacterImportResult {
    bool success = false;
    std::pmr::string diagnostics;
    AvatarAsset avatar;
    AnimatedMeshAsset character;
    AnimatorStateMachineAsset stateMachine;
    std::pmr::vector<std::filesystem::path> materials;
    std::pmr::vector<std::filesystem::path> textures;
    ImportReport report;
};

struct CharacterImportOptions {
    std::pmr::vector<std::pmr::string> forceDoubleSidedMaterials;
    std::filesystem::path reportOutputPath;
};

// =========================================================================
// Particle import types (extracted from unity_particle_importer.h)
// =========================================================================

enum class ParticleModuleKind {
    Unknown = 0,
    Main,
    Emission,
    Shape,
    SizeOverLifetime,
    ColorOverLifetime,
    VelocityOverLifetime,
    Renderer,
    ForceOverLifetime,
    TextureSheetAnimation,
    SubUVParticles,
    Lights,
    Noise,
    Collision,
    RotationOverLifetime,
    ExternalForces,
    InheritVelocity,
    Trigger,
    CustomData,
};

enum class ShaderClassification {
    Unknown = 0,
    AlphaBlended,
    Additive,
    Multiply,
    LightningThin,
    UnlitTransparent,
    UnlitAdditive,
    Custom,
};

enum class ShaderClassificationSource {
    Unknown = 0,
    GuidMatch,
    NameFallback,
    KeywordFallback,
};

struct ParticleBurstImporterAsset {
    float time = 0.0f;
    int minCount = 0;
    int maxCount = 0;
};

struct ParticleModuleImporterAsset {
    ParticleModuleKind kind = ParticleModuleKind::Unknown;
    bool enabled = false;
};

struct ParticleEmitterImporterAsset {
    ParticleEmitterImporterAsset()
        : name(&core::defaultCpuMemoryResource()), bursts(&core::defaultCpuMemoryResource()),
          sizeOverLifetime(&core::defaultCpuMemoryResource()),
          colorAlphaOverLifetime(&core::defaultCpuMemoryResource()),
          materialGuid(&core::defaultCpuMemoryResource()),
          mainTextureGuid(&core::defaultCpuMemoryResource()),
          modules(&core::defaultCpuMemoryResource())
    {}

    std::pmr::string name;

    // Main module
    float duration = 0.0f;
    bool looping = false;
    bool prewarm = false;
    float startLifetime = 0.0f;
    float startSpeed = 0.0f;
    float startSize = 0.0f;
    float startColorR = 1.0f;
    float startColorG = 1.0f;
    float startColorB = 1.0f;
    float startColorA = 1.0f;
    float gravityModifier = 0.0f;
    int maxParticles = 0;

    // Emission module
    float rateOverTime = 0.0f;
    float rateOverDistance = 0.0f;
    std::pmr::vector<ParticleBurstImporterAsset> bursts;

    // Shape module
    int shapeType = 0;
    float shapeRadius = 0.0f;
    float shapeAngleDegrees = 25.0f;
    core::Vec3 shapeBoxExtents{1.0f, 1.0f, 1.0f};
    int simulationSpace = 0;
    bool velocityOverLifetimeEnabled = false;
    core::Vec3 velocityOverLifetime{0.0f, 0.0f, 0.0f};

    // Size over lifetime
    bool sizeOverLifetimeEnabled = false;
    std::pmr::vector<float> sizeOverLifetime;

    // Color over lifetime
    std::pmr::vector<float> colorAlphaOverLifetime;

    // Renderer (from ParticleSystemRenderer)
    std::pmr::string materialGuid;
    int renderAlignment = 0;

    // Texture / shader classification (from Unity Material)
    std::pmr::string mainTextureGuid;
    ShaderClassification shaderClassification = ShaderClassification::Unknown;

    // Source provenance / fallback marking
    bool isMaterialPrototypeFallback = false;
    ShaderClassificationSource shaderClassificationSource = ShaderClassificationSource::Unknown;
    std::pmr::string materialImporterVersion;

    // TextureSheetAnimationModule (flipbook / SubUV)
    uint32_t flipbookTilesX = 1;
    uint32_t flipbookTilesY = 1;
    uint32_t flipbookFrameCount = 1;
    float flipbookPlayRate = 1.0f;
    bool flipbookLooping = true;
    bool flipbookRandomStart = false;

    // CollisionModule subset
    bool collisionEnabled = false;
    int collisionType = 0;
    float collisionPlaneY = 0.0f;
    float collisionBounce = 0.0f;
    float collisionDampen = 0.0f;

    // RotationOverLifetimeModule subset
    bool rotationEnabled = false;
    float rotationSpeed = 0.0f;

    // NoiseModule + ForceOverLifetimeModule subset
    float noiseStrength = 0.0f;
    float forceX = 0.0f;
    float forceY = 0.0f;
    float forceZ = 0.0f;

    // InheritVelocityModule subset
    float inheritVelocityFactor = 0.0f;

    // ExternalForcesModule subset
    float externalForcesMultiplier = 1.0f;

    // TriggerModule curated subset
    bool triggerEnabled = false;
    int triggerColliderType = 0;
    float triggerPlaneY = 0.0f;
    bool triggerSpawnsSubEmitter = false;
    std::pmr::string triggerSubEmitterName{&core::defaultCpuMemoryResource()};

    // Module tracking
    std::pmr::vector<ParticleModuleImporterAsset> modules;
};

struct ParticleImporterAsset {
    ParticleImporterAsset() : emitters(&core::defaultCpuMemoryResource()) {}

    std::filesystem::path sourcePath;
    std::pmr::vector<ParticleEmitterImporterAsset> emitters;
};

struct ParticleImporterParseResult {
    bool success = false;
    std::pmr::string diagnostics{&core::defaultCpuMemoryResource()};
    ParticleImporterAsset particle;
};

struct ParticleMaterialImporterAsset {
    ParticleMaterialImporterAsset()
        : name(&core::defaultCpuMemoryResource()), shaderGuid(&core::defaultCpuMemoryResource()),
          shaderKeywords(&core::defaultCpuMemoryResource()),
          mainTexGuid(&core::defaultCpuMemoryResource()),
          sourceMaterialGuid(&core::defaultCpuMemoryResource())
    {}

    std::pmr::string name;
    std::pmr::string shaderGuid;
    int shaderFileId = 0;
    std::pmr::string shaderKeywords;
    std::pmr::string mainTexGuid;
    float mainTexScaleX = 1.0f;
    float mainTexScaleY = 1.0f;
    float mainTexOffsetX = 0.0f;
    float mainTexOffsetY = 0.0f;
    float colorR = 1.0f, colorG = 1.0f, colorB = 1.0f, colorA = 1.0f;
    float tintColorR = 1.0f, tintColorG = 1.0f, tintColorB = 1.0f, tintColorA = 1.0f;
    ShaderClassification classification = ShaderClassification::Unknown;
    std::pmr::string sourceMaterialGuid;
    std::pmr::string importerVersion;
    ShaderClassificationSource classificationSource = ShaderClassificationSource::Unknown;
    bool isPrototypeFallback = false;
};

struct ParticleMaterialParseResult {
    bool success = false;
    std::pmr::string diagnostics{&core::defaultCpuMemoryResource()};
    ParticleMaterialImporterAsset material;
};

// Part of the well-known Unity built-in shader GUID set.
// Used by the particle importer for shader classification.
// These are Prototype placeholder GUIDs tracked in tools/unity_import/.
namespace builtin_shader_guids {
inline constexpr const char* kParticleAlphaBlended = "b60b8b6c1d8d4e8b8c9d9f1c2d3e4f5a";
inline constexpr const char* kParticleAdditive = "7c7e5c0a1d8d4e8b8c9d9f1c2d3e4f5b";
inline constexpr const char* kParticleMultiply = "8d8e5c0a1d8d4e8b8c9d9f1c2d3e4f5c";
inline constexpr const char* kMobileParticleAlphaBlended = "9e9f5c0a1d8d4e8b8c9d9f1c2d3e4f5d";
inline constexpr const char* kMobileParticleAdditive = "0a0b5c0a1d8d4e8b8c9d9f1c2d3e4f5e";
inline constexpr const char* kUnlitTransparent = "1b2b5c0a1d8d4e8b8c9d9f1c2d3e4f5f";
inline constexpr const char* kUnlitAdditive = "2c3c5c0a1d8d4e8b8c9d9f1c2d3e4f60";
inline constexpr const char* kDefaultMaterial = "0000000000000000f000000000000000";
} // namespace builtin_shader_guids

// =========================================================================
// Inline method implementations
// =========================================================================

namespace {
inline std::pmr::string _toPmrString(const std::filesystem::path& path)
{
    return core::toPmrString(path.string());
}
} // namespace

inline void ImportReport::add(const std::pmr::string& severity,
    const std::pmr::string& contract,
    const std::pmr::string& message,
    const SourceLocation& location)
{
    records.push_back(ImportReportRecord{severity, contract, message, location});
}

inline bool ImportReport::hasErrors() const
{
    for (const auto& record : records) {
        if (record.severity == "error") {
            return true;
        }
    }
    return false;
}

inline std::pmr::string ImportReport::toText() const
{
    std::pmr::string text;
    for (const auto& record : records) {
        text += "[";
        text += record.severity;
        text += "] ";
        text += record.contract;
        text += " ";
        if (!record.location.path.empty()) {
            text += _toPmrString(record.location.path);
            if (record.location.line > 0) {
                text += ":";
                text += std::to_string(record.location.line);
            }
            if (record.location.column > 0) {
                text += ":";
                text += std::to_string(record.location.column);
            }
            text += " ";
        }
        text += record.message;
        text += "\n";
    }
    return text;
}

} // namespace arp::assets
