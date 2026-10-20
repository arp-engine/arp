#pragma once

#include "assets/glb_animated_mesh_importer.h"
#include "core/cpu_allocator.h"
#include "reflection/reflection.h"
#include "reflection/reflection_macros.h"
#include "scriptvm/runtime/value.h"

#include <cstddef>
#include <cstdint>

namespace arp::scriptvm::plugins::asset {

using arp::core::pmr::String;
using arp::reflection::hlist;

struct AvatarHumanBoneBinding final {
    String sourceBoneName{&arp::core::defaultCpuMemoryResource()};
    String boneName{&arp::core::defaultCpuMemoryResource()};
    String humanName{&arp::core::defaultCpuMemoryResource()};
};

struct AvatarSkeletonNodeBinding final {
    String name{&arp::core::defaultCpuMemoryResource()};
    String parentName{&arp::core::defaultCpuMemoryResource()};
};

ASTRUCT(Name = "ModelJointInfo", Construct = Default)

struct ModelJointInfo final {
    APROP()
    String name{&arp::core::defaultCpuMemoryResource()};
    APROP()
    int parentIndex = -1;
    APROP()
    bool inverseBindFinite = false;
};

ASTRUCT(Name = "ModelVertexInfo", Construct = Default)

struct ModelVertexInfo final {
    APROP()
    float positionX = 0.0f;
    APROP()
    float positionY = 0.0f;
    APROP()
    float positionZ = 0.0f;
    APROP()
    bool positionFinite = false;
};

ASTRUCT(Name = "ModelSampledVertexInfo", Construct = Default)

struct ModelSampledVertexInfo final {
    APROP()
    float positionX = 0.0f;
    APROP()
    float positionY = 0.0f;
    APROP()
    float positionZ = 0.0f;
    APROP()
    bool available = false;
    AFUNC(Name = "is_available")
    bool isAvailable() const;
};

ASTRUCT(Name = "SkinInfluenceInfo", Construct = Default)

struct SkinInfluenceInfo final {
    APROP()
    int jointIndex = 0;
    APROP()
    float weight = 0.0f;
};

ASTRUCT(Name = "ModelAnimationChannelInfo", Construct = Default)

struct ModelAnimationChannelInfo final {
    APROP()
    int jointIndex = 0;
    APROP()
    String targetPath{&arp::core::defaultCpuMemoryResource()};
    APROP()
    int keyframeCount = 0;
};

ASTRUCT(Name = "ModelAnimationKeyframeInfo", Construct = Default)

struct ModelAnimationKeyframeInfo final {
    APROP()
    float timeSeconds = 0.0f;
    APROP()
    float translationX = 0.0f;
    APROP()
    float translationY = 0.0f;
    APROP()
    float translationZ = 0.0f;
    APROP()
    float rotationX = 0.0f;
    APROP()
    float rotationY = 0.0f;
    APROP()
    float rotationZ = 0.0f;
    APROP()
    float rotationW = 1.0f;
    APROP()
    float scaleX = 1.0f;
    APROP()
    float scaleY = 1.0f;
    APROP()
    float scaleZ = 1.0f;
    APROP()
    bool transformFinite = false;
};

ASTRUCT(Name = "AvatarBinding", Construct = Default)

struct AvatarBinding final {
    APROP()
    bool humanoid = false;

    AFUNC(Name = "set_humanoid")
    void setHumanoid(bool value);

    AFUNC(Name = "add_human_bone")
    bool addHumanBone(const String& boneName, const String& humanName);

    AFUNC(Name = "add_bone_mapping")
    bool addBoneMapping(
        const String& sourceBoneName, const String& targetBoneName, const String& humanName);

    AFUNC(Name = "add_skeleton_node")
    bool addSkeletonNode(const String& name, const String& parentName);

    AFUNC(Name = "human_bone_count")
    int humanBoneCount() const;

    AFUNC(Name = "skeleton_node_count")
    int skeletonNodeCount() const;

    const arp::core::pmr::Vector<AvatarHumanBoneBinding>& humanBones() const;
    const arp::core::pmr::Vector<AvatarSkeletonNodeBinding>& skeletonNodes() const;

private:
    arp::core::pmr::Vector<AvatarHumanBoneBinding> humanBones_{
        &arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<AvatarSkeletonNodeBinding> skeletonNodes_{
        &arp::core::defaultCpuMemoryResource()};
};

ASTRUCT(Name = "ImportedModelResult")

struct ImportedModel final {
    APROP()
    bool success = false;
    APROP()
    String source{&arp::core::defaultCpuMemoryResource()};
    APROP()
    String diagnostics{&arp::core::defaultCpuMemoryResource()};
    arp::assets::AnimatedMeshAsset asset;

    AFUNC(Name = "vertex_count")
    int vertexCount() const;
    AFUNC(Name = "index_count")
    int indexCount() const;
    AFUNC(Name = "joint_count")
    int jointCount() const;
    AFUNC(Name = "joint_name")
    String jointName(int index) const;
    AFUNC(Name = "submesh_count")
    int submeshCount() const;
    AFUNC(Name = "material_count")
    int materialCount() const;
    AFUNC(Name = "material_name")
    String materialName(int index) const;
    AFUNC(Name = "clip_count")
    int clipCount() const;
    AFUNC(Name = "clip_name")
    String clipName(int index) const;
    AFUNC(Name = "clip_duration")
    float clipDuration(int index) const;
    AFUNC(Name = "clip_channel_count")
    int clipChannelCount(int index) const;
    AFUNC(Name = "mesh_valid_for_indexed_draw")
    bool meshValidForIndexedDraw() const;
    AFUNC(Name = "skeleton_valid")
    bool skeletonValid() const;
    AFUNC(Name = "bind_pose_count")
    int bindPoseCount() const;
    AFUNC(Name = "skinned_vertex_count")
    int skinnedVertexCount() const;
    AFUNC(Name = "index_value")
    int indexValue(int index) const;
    AFUNC(Name = "vertex_position_component")
    float vertexPositionComponent(int index, int component) const;
    AFUNC(Name = "clip_duration_finite")
    bool clipDurationFinite(int index) const;
    AFUNC(Name = "joint_info")
    ModelJointInfo jointInfo(int index) const;
    AFUNC(Name = "vertex_info")
    ModelVertexInfo vertexInfo(int index) const;
    AFUNC(Name = "bind_pose_vertex")
    ModelSampledVertexInfo bindPoseVertex(int index) const;
    AFUNC(Name = "sampled_vertex")
    ModelSampledVertexInfo sampledVertex(
        int clipIndex, float normalizedTime, int vertexIndex) const;
    AFUNC(Name = "sampled_vertex_available")
    bool sampledVertexAvailable(int clipIndex, float normalizedTime, int vertexIndex) const;
    AFUNC(Name = "sampled_vertex_position_component")
    float sampledVertexPositionComponent(
        int clipIndex, float normalizedTime, int vertexIndex, int component) const;
    AFUNC(Name = "clip_keyframe_component")
    float clipKeyframeComponent(
        int clipIndex, int channelIndex, int keyframeIndex, int component) const;
    AFUNC(Name = "clip_keyframe_transform_finite")
    bool clipKeyframeTransformFinite(int clipIndex, int channelIndex, int keyframeIndex) const;
    AFUNC(Name = "skin_influence")
    SkinInfluenceInfo skinInfluence(int vertexIndex, int slot) const;
    AFUNC(Name = "clip_channel_info")
    ModelAnimationChannelInfo clipChannelInfo(int clipIndex, int channelIndex) const;
    AFUNC(Name = "clip_keyframe_info")
    ModelAnimationKeyframeInfo clipKeyframeInfo(
        int clipIndex, int channelIndex, int keyframeIndex) const;
    AFUNC(Name = "to_record")
    Value toRecord() const;
};

ASTRUCT(Name = "RuntimePayloadResult")

struct RuntimePayloadResult final {
    APROP()
    bool success = false;
    APROP()
    String blobPath{&arp::core::defaultCpuMemoryResource()};
    APROP()
    std::int64_t blobHash = 0;
    APROP()
    int blobByteSize = 0;
    APROP()
    int stateBindingCount = 0;
    APROP()
    int materialCount = 0;
    APROP()
    int textureCount = 0;
    APROP()
    int imageCount = 0;
    APROP()
    String defaultStateName{&arp::core::defaultCpuMemoryResource()};
    APROP()
    String defaultClipName{&arp::core::defaultCpuMemoryResource()};
    APROP()
    String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

ASTRUCT(Name = "ImportedAnimationClipResult")

struct ImportedAnimationClip final {
    APROP()
    bool success = false;
    APROP()
    String source{&arp::core::defaultCpuMemoryResource()};
    APROP()
    String name{&arp::core::defaultCpuMemoryResource()};
    APROP()
    float durationSeconds = 0.0f;
    APROP()
    int channelCount = 0;
    APROP()
    String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

ASTRUCT()

struct DecodedImageResult final {
    APROP()
    bool success = false;
    APROP()
    String diagnostics{&arp::core::defaultCpuMemoryResource()};
    APROP()
    int width = 0;
    APROP()
    int height = 0;
    APROP()
    bool srgb = true;
    APROP()
    bool alpha = false;
    APROP()
    hlist rgba8 = hlist(hlist::allocator_type(&arp::core::defaultCpuMemoryResource()));
};

ASTRUCT(Name = "UnityAssetDatabaseResult")

struct UnityAssetDatabaseResult final {
    APROP()
    bool success = false;
    APROP()
    int assetCount = 0;
    APROP()
    int errorCount = 0;
    APROP()
    int warningCount = 0;
    APROP()
    String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

ASTRUCT(Name = "UnityAssetRecordResult")

struct UnityAssetRecordResult final {
    APROP()
    bool success = false;
    APROP()
    String guid{&arp::core::defaultCpuMemoryResource()};
    APROP()
    String metaPath{&arp::core::defaultCpuMemoryResource()};
    APROP()
    String assetPath{&arp::core::defaultCpuMemoryResource()};
    APROP()
    String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

ASTRUCT(Name = "AssetHashResult")

struct AssetHashResult final {
    APROP()
    bool success = false;
    APROP()
    std::int64_t hash = 0;
    APROP()
    String canonicalPayloadYaml{&arp::core::defaultCpuMemoryResource()};
    APROP()
    String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

ASTRUCT(Name = "AssetPackageWriteResult")

struct AssetPackageWriteResult final {
    APROP()
    bool success = false;
    APROP()
    int entryCount = 0;
    APROP()
    String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

AFUNC(Name = "import_glb")
ImportedModel importGlb(const String& path);

AFUNC(Name = "import_fbx")
ImportedModel importFbx(const String& path);

AFUNC(Name = "write_prefab_runtime_payload")
RuntimePayloadResult writePrefabRuntimePayload(const String& packageRoot,
    const String& blobPath,
    const String& displayName,
    const ImportedModel& imported,
    const String& defaultStateName);

AFUNC(Name = "retarget_fbx_clip")
ImportedAnimationClip retargetFbxClip(
    const String& sourcePath, const String& clipName, const String& targetPath);

AFUNC(Name = "retarget_fbx_clip_with_avatar")
ImportedAnimationClip retargetFbxClipWithAvatar(const String& sourcePath,
    const String& clipName,
    const String& targetPath,
    const AvatarBinding& avatar);

AFUNC(Name = "decode_image", DefaultArguments = true)
DecodedImageResult decodeImage(const String& path, const String& mime, bool srgb);

AFUNC(Name = "scan_unity_assets")
UnityAssetDatabaseResult scanUnityAssets(const String& root);

AFUNC(Name = "find_unity_asset")
UnityAssetRecordResult findUnityAsset(const String& root, const String& guid);

AFUNC(Name = "hash_payload")
AssetHashResult hashPayload(const String& payloadYaml);

AFUNC(Name = "uuid_from_text")
String uuidFromText(const String& text);

AFUNC(Name = "unity_guid_to_uuid")
String unityGuidToUuid(const String& guid);

AFUNC(Name = "is_resource_uuid")
bool isResourceUuid(const String& value);

AFUNC(Name = "write_package")
AssetPackageWriteResult writePackage(const String& outputDirectory, const String& manifestYaml);

} // namespace arp::scriptvm::plugins::asset
