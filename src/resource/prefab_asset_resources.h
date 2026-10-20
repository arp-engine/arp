#pragma once

#include "animation/animator_controller.h"
#include "assets/glb_animated_mesh_importer.h"
#include "assets/imported_asset_types.h"
#include "core/cpu_allocator.h"
#include "reflection/reflection_macros.h"
#include "resource/internal_asset_package.h"
#include "resource/resource_manager.h"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace arp::resource {

inline constexpr const char* kPbrMetallicRoughnessContract = "arp/material/pbr-metallic-roughness";
inline constexpr std::uint32_t kPbrMetallicRoughnessContractVersion = 1u;

enum class MaterialContractDiagnostic : std::uint8_t {
    None,
    UnsupportedVersion,
    UnsupportedSurfaceModel,
    InvalidParameter,
    ShaderInterfaceMismatch,
};

struct ShaderPackageResource;

struct MaterialContractValidation {
    MaterialContractDiagnostic diagnostic = MaterialContractDiagnostic::None;

    bool valid() const noexcept { return diagnostic == MaterialContractDiagnostic::None; }
};

struct PrefabAssetShaderPaths {
    arp::core::pmr::String basicMaterialPreview{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String stylizedScene{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String guiOverlay{&arp::core::defaultCpuMemoryResource()};
};

struct PrefabAssetGraphIds {
    arp::core::pmr::String rootId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String prefabId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String modelId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String skinnedMeshId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String skeletonId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String avatarId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String animationSetId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String animatorControllerId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String materialSetId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String runtimePayloadId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String sequenceId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String sequenceBindingSetId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String previewStudioId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String basicShaderId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String stylizedShaderId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String guiShaderId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String startupSplashShaderId{&arp::core::defaultCpuMemoryResource()};
};

struct PrefabAssetSourceDesc {
    PrefabAssetGraphIds ids;
    arp::core::pmr::String alias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String displayName{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    std::filesystem::path runtimePayloadPackageRoot;
    PrefabAssetShaderPaths shaderPaths;
    arp::core::pmr::String startupSplashShaderPath{&arp::core::defaultCpuMemoryResource()};
};

struct PrefabAssetGraphBuildDesc {
    PrefabAssetGraphIds ids;
    arp::core::pmr::String alias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String displayName{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String sourceKind{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String packageSeed{&arp::core::defaultCpuMemoryResource()};
    std::filesystem::path runtimePayloadPackageRoot;
    const assets::CharacterImportResult* importedCharacter = nullptr;
    PrefabAssetShaderPaths shaderPaths;
    arp::core::pmr::String startupSplashShaderPath{&arp::core::defaultCpuMemoryResource()};
};

struct PrefabAssetGraphBuildResult {
    bool success = false;
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
    InternalAssetPackageDesc package;
    PrefabAssetGraphIds ids;
};

struct AssetResourcePackageBuildDesc {
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String alias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String displayName{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String status{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String envelopePath{&arp::core::defaultCpuMemoryResource()};
};

struct AssetResourcePackageBuildResult {
    bool success = false;
    InternalAssetPackageEntry entry;
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

struct AssetResourceRuntimeLoadDesc {
    AssetResourcePackageBuildDesc package;
    bool preserveRuntimeObject = false;
};

ASTRUCT(Construct = Default)

struct ExternalAssetSourceResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept
    {
        return "ExternalAssetSourceResource";
    }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/source.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourceGuid{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String fileId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourceAssetType{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String importerKind{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    int contentHash = 0;

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

ASTRUCT(Construct = Default)

struct PrefabResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept { return "PrefabResource"; }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/prefab.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourceResourceId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String modelId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String skinnedMeshId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String skeletonId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String avatarId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String animationSetId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String animatorControllerId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String materialSetId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String runtimePayloadId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sequenceId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sequenceBindingSetId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String previewStudioId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::reflection::hlist shaderPackageIds = arp::reflection::makeEmptyHList();
    APROP(Serialize)
    int objectCount = 0;
    APROP(Serialize)
    int componentCount = 0;
    APROP(Serialize)
    int physicsColliderCount = 0;
    APROP(Serialize)
    int lightHelperCount = 0;

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

ASTRUCT(Construct = Default)

struct PrefabRuntimePayloadResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept
    {
        return "PrefabRuntimePayloadResource";
    }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/runtime-payload.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String blobPath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    int blobHash = 0;
    APROP(Serialize)
    int blobByteSize = 0;
    APROP(Serialize)
    arp::core::pmr::String defaultStateName{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String defaultClipName{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    int stateBindingCount = 0;
    APROP(Serialize)
    int materialCount = 0;
    APROP(Serialize)
    int textureCount = 0;
    APROP(Serialize)
    int imageCount = 0;

    core::SharedPtr<const arp::assets::AnimatedMeshAsset> runtimeAsset;
    core::SharedPtr<const animation::AnimatorControllerAsset> controllerAsset;

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

struct PrefabRuntimeStateBinding {
    arp::core::pmr::String stateName{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String clipName{&arp::core::defaultCpuMemoryResource()};
    float speed = 1.0f;
};

struct PrefabRuntimePayloadWriteDesc {
    std::filesystem::path packageRoot;
    arp::core::pmr::String blobPath{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String displayName{&arp::core::defaultCpuMemoryResource()};
    const arp::assets::AnimatedMeshAsset* asset = nullptr;
    const animation::AnimatorControllerAsset* controllerAsset = nullptr;
    arp::core::pmr::String defaultStateName{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<PrefabRuntimeStateBinding> stateBindings{
        &arp::core::defaultCpuMemoryResource()};
};

struct PrefabRuntimePayloadWriteResult {
    bool success = false;
    PrefabRuntimePayloadResource resource;
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

struct PrefabRuntimePayloadLoadResult {
    bool success = false;
    arp::assets::AnimatedMeshAsset asset;
    animation::AnimatorControllerAsset controllerAsset;
    bool hasCompleteControllerData = false;
    arp::core::pmr::String defaultStateName{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<PrefabRuntimeStateBinding> stateBindings{
        &arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

ARP_API PrefabRuntimePayloadWriteResult writePrefabRuntimePayload(
    const PrefabRuntimePayloadWriteDesc& desc);

ARP_API PrefabRuntimePayloadLoadResult loadPrefabRuntimePayloadFromPackage(
    const std::filesystem::path& packageRoot, const arp::core::pmr::String& prefabAlias);

ARP_API PrefabRuntimePayloadLoadResult loadPrefabRuntimePayloadByResourceIdFromPackage(
    const std::filesystem::path& packageRoot, const arp::core::pmr::String& prefabResourceId);

ASTRUCT(Construct = Default)

struct ModelResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept { return "ModelResource"; }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/model.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourceResourceId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String skinnedMeshId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String skeletonId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String avatarId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String animationSetId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String materialSetId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    int materialRemapCount = 0;
    APROP(Serialize)
    int importedAnimationCount = 0;
    APROP(Serialize)
    bool humanoid = false;

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

ASTRUCT(Construct = Default)

struct SkinnedMeshResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept { return "SkinnedMeshResource"; }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/skinned-mesh.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    int vertexCount = 0;
    APROP(Serialize)
    int indexCount = 0;
    APROP(Serialize)
    int submeshCount = 0;
    APROP(Serialize)
    int materialSlotCount = 0;

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

ASTRUCT(Construct = Default)

struct SkeletonResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept { return "SkeletonResource"; }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/skeleton.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    int jointCount = 0;
    APROP(Serialize)
    int bindPoseCount = 0;

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

ASTRUCT(Construct = Default)

struct AvatarResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept { return "AvatarResource"; }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/avatar.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    int humanBoneCount = 0;
    APROP(Serialize)
    bool humanoid = false;

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

ASTRUCT(Construct = Default)

struct AnimationClipResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept { return "AnimationClipResource"; }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/animation-clip.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    float durationSeconds = 0.0f;
    APROP(Serialize)
    int channelCount = 0;
    APROP(Serialize)
    bool recordedCurve = false;

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

ASTRUCT(Construct = Default)

struct AnimationSetResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept { return "AnimationSetResource"; }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/animation-set.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    int clipCount = 0;
    APROP(Serialize)
    arp::reflection::hlist clipIds = arp::reflection::makeEmptyHList();

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

ASTRUCT(Construct = Default)

struct MaterialResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept { return "MaterialResource"; }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/material.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourceResourceId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String shaderPackageId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    int textureBindingCount = 0;
    APROP(Serialize)
    int propertyCount = 0;
    APROP(Serialize)
    std::uint32_t materialContractVersion = 0u;
    APROP(Serialize)
    arp::core::pmr::String surfaceModel{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    float baseColorR = 1.0f;
    APROP(Serialize)
    float baseColorG = 1.0f;
    APROP(Serialize)
    float baseColorB = 1.0f;
    APROP(Serialize)
    float baseColorA = 1.0f;
    APROP(Serialize)
    float emissiveR = 0.0f;
    APROP(Serialize)
    float emissiveG = 0.0f;
    APROP(Serialize)
    float emissiveB = 0.0f;
    APROP(Serialize)
    float metallicFactor = 1.0f;
    APROP(Serialize)
    float roughnessFactor = 1.0f;
    APROP(Serialize)
    arp::core::pmr::String alphaMode{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    float alphaCutoff = 0.5f;
    APROP(Serialize)
    bool doubleSided = false;
    APROP(Serialize)
    arp::core::pmr::String baseColorTextureId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String normalTextureId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String metallicRoughnessTextureId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String occlusionTextureId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String emissiveTextureId{&arp::core::defaultCpuMemoryResource()};

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

ARP_API MaterialContractValidation validateMaterialContract(
    const MaterialResource& material, const ShaderPackageResource& shader);

ASTRUCT(Construct = Default)

struct MaterialSetResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept { return "MaterialSetResource"; }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/material-set.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    int materialCount = 0;
    APROP(Serialize)
    arp::reflection::hlist materialIds = arp::reflection::makeEmptyHList();

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

ASTRUCT(Construct = Default)

struct TextureResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept { return "TextureResource"; }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/texture-resource.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourceResourceId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String usage{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    bool srgb = false;
    APROP(Serialize)
    bool alpha = false;

    std::uint32_t decodedWidth = 0u;
    std::uint32_t decodedHeight = 0u;
    bool decodedHasAlpha = false;
    bool decodedSrgb = false;
    arp::core::pmr::Vector<std::uint8_t> decodedRgba8{&arp::core::defaultCpuMemoryResource()};
    // Source generation used to produce this decoded payload; zero is unknown.
    std::uint64_t sourceGeneration = 0u;

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;

    std::size_t processedByteCount() const noexcept override { return decodedRgba8.size(); }

    void appendContentFingerprint(core::StableHash64& hasher) const noexcept override
    {
        hasher.appendString({sourcePath.data(), sourcePath.size()});
        hasher.appendString({sourceResourceId.data(), sourceResourceId.size()});
        hasher.appendU64(sourceGeneration);
        hasher.appendString({usage.data(), usage.size()});
        hasher.appendU64(srgb ? 1u : 0u);
        hasher.appendU64(alpha ? 1u : 0u);
        hasher.appendU64(decodedWidth);
        hasher.appendU64(decodedHeight);
        hasher.appendU64(decodedHasAlpha ? 1u : 0u);
        hasher.appendU64(decodedSrgb ? 1u : 0u);
        hasher.appendU64(decodedRgba8.size());
        hasher.appendBytes(decodedRgba8.data(), decodedRgba8.size());
    }
};

ASTRUCT(Construct = Default)

struct AuthoringAssetResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept { return "AuthoringAssetResource"; }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/authoring.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourceResourceId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String authoringType{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    int contentHash = 0;

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

ASTRUCT(Construct = Default)

struct ShaderGraphSourceResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept { return "ShaderGraphSourceResource"; }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/shader-graph-source.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourceResourceId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    int dependencyCount = 0;

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

ASTRUCT(Construct = Default)

struct SequenceResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept { return "SequenceResource"; }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/sequence.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourceResourceId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    float durationSeconds = 0.0f;
    APROP(Serialize)
    int trackCount = 0;
    APROP(Serialize)
    int clipCount = 0;
    APROP(Serialize)
    int recordedCurveCount = 0;
    APROP(Serialize)
    int emptyBindingCount = 0;
    APROP(Serialize)
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

ASTRUCT(Construct = Default)

struct SequenceBindingSetResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept
    {
        return "SequenceBindingSetResource";
    }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/sequence-bindings.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sequenceId{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    int bindingCount = 0;
    APROP(Serialize)
    int emptyBindingCount = 0;
    APROP(Serialize)
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

ASTRUCT(Construct = Default)

struct PhysicsColliderResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept { return "PhysicsColliderResource"; }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/physics-collider.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    int colliderCount = 0;

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

ASTRUCT(Construct = Default)

struct AnimatorControllerResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept
    {
        return "AnimatorControllerResource";
    }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/animator.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String sourcePath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String defaultStateName{&arp::core::defaultCpuMemoryResource()};
    core::SharedPtr<const animation::AnimatorControllerAsset> controllerAsset;
    APROP(Serialize)
    int stateCount = 0;
    APROP(Serialize)
    int parameterCount = 0;

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

ASTRUCT(Construct = Default)

struct ShaderPackageResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept { return "ShaderPackageResource"; }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/shader.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String slangPath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String spirvPath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String mslPath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String reflectionPath{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    arp::core::pmr::String materialInterfaceContract{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    std::uint32_t materialInterfaceVersion = 0u;

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

ASTRUCT(Construct = Default)

struct PreviewStudioResource : public ResourceBase {
    static constexpr const char* resourceTypeName() noexcept { return "PreviewStudioResource"; }

    static constexpr const char* defaultEnvelopePath() noexcept
    {
        return "resources/preview-studio.arpres.yaml";
    }

    APROP(Serialize)
    arp::core::pmr::String lightingPreset{&arp::core::defaultCpuMemoryResource()};
    APROP(Serialize)
    float verticalFovDegrees = 42.0f;
    APROP(Serialize)
    float cameraDistance = 4.0f;
    APROP(Serialize)
    float exposure = 1.08f;

    arp::core::pmr::Vector<ResourceDependency> dependencies() const;
    AssetResourcePackageBuildResult buildPackageEntry(
        const AssetResourcePackageBuildDesc& desc) const;
    ResourceLoadResult loadInto(
        ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const;
};

class ARP_API PrefabAssetGraphBuilder final {
public:
    PrefabAssetGraphBuildResult buildPackage(const PrefabAssetGraphBuildDesc& desc) const;
};

ARP_API void registerPrefabAssetReflection();

class PrefabResourceFactory final {
public:
    using Creator = core::UniquePtr<ResourceBase> (*)(const arp::reflection::object& payload);

    bool registerType(arp::reflection::NameView resourceType, Creator creator);
    core::UniquePtr<ResourceBase> create(
        arp::reflection::NameView resourceType, const arp::reflection::object& payload) const;
    bool supports(arp::reflection::NameView resourceType) const;

private:
    core::pmr::UnorderedMap<core::pmr::String, Creator> creators_{
        &core::defaultCpuMemoryResource()};
};

PrefabResourceFactory& prefabResourceFactory();

core::UniquePtr<ResourceBase> createPrefabResourceFromEnvelope(
    arp::reflection::NameView envelopeYaml, core::pmr::String& diagnostics);

namespace compatibility {

ARP_API ResourceLoadResult loadFromEnvelope(ResourceManager& manager,
    arp::reflection::NameView envelopeYaml,
    ResourceLoadPolicy policy = {});

} // namespace compatibility

} // namespace arp::resource
