#include "asset_module.h"

#include "animation/animation.h"
#include "animation/skeleton.h"
#include "assets/avatar_retargeter.h"
#include "assets/fbx_character_importer.h"
#include "assets/glb_animated_mesh_importer.h"
#include "assets/image_decoder.h"
#include "core/file_io.h"
#include "core/sha1.h"
#include "resource/internal_asset_package.h"
#include "resource/prefab_asset_resources.h"
#include "resource/resource_manager.h"
#include "scriptvm/runtime/cpp_plugin_api.h"
#include "scriptvm/runtime/value.h"
#include "unity_asset_database_bridge.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fkYAML/node.hpp>
#include <limits>
#include <stdexcept>
#include <utility>

namespace arp::scriptvm::plugins::asset {

using arp::reflection::hdict;
using arp::reflection::hlist;
using arp::scriptvm::Value;

using YamlNode = fkyaml::node;

String uuidFromDigest(const std::uint8_t* digest)
{
    constexpr char digits[] = "0123456789abcdef";
    String result(&arp::core::defaultCpuMemoryResource());
    result.reserve(36u);
    for (std::size_t index = 0; index < 16u; ++index) {
        if (index == 4u || index == 6u || index == 8u || index == 10u) {
            result.push_back('-');
        }
        result.push_back(digits[(digest[index] >> 4u) & 0x0fu]);
        result.push_back(digits[digest[index] & 0x0fu]);
    }
    return result;
}

String yamlString(const YamlNode& node, const char* field)
{
    if (!node.is_mapping()) {
        throw std::runtime_error("package manifest root must be a mapping");
    }
    const auto& map = node.get_value_ref<const YamlNode::mapping_type&>();
    const auto found = map.find(YamlNode(field));
    if (found == map.end() || !found->second.is_string()) {
        throw std::runtime_error(std::string("package manifest missing string field: ") + field);
    }
    return String(found->second.get_value<std::string>(), &arp::core::defaultCpuMemoryResource());
}

std::int64_t yamlInteger(const YamlNode& node, const char* field)
{
    if (!node.is_mapping()) {
        throw std::runtime_error("package manifest root must be a mapping");
    }
    const auto& map = node.get_value_ref<const YamlNode::mapping_type&>();
    const auto found = map.find(YamlNode(field));
    if (found == map.end() || !found->second.is_integer()) {
        throw std::runtime_error(std::string("package manifest missing integer field: ") + field);
    }
    return found->second.get_value<std::int64_t>();
}

const YamlNode* yamlValue(const YamlNode& node, const char* field)
{
    if (!node.is_mapping()) {
        return nullptr;
    }
    const auto& map = node.get_value_ref<const YamlNode::mapping_type&>();
    const auto found = map.find(YamlNode(field));
    return found == map.end() ? nullptr : &found->second;
}

void appendDependencies(const YamlNode& entryNode,
    arp::core::pmr::Vector<arp::resource::ResourceDependency>& dependencies)
{
    const auto* value = yamlValue(entryNode, "dependencies");
    if (value == nullptr || value->is_null()) {
        return;
    }
    if (!value->is_sequence()) {
        throw std::runtime_error("package dependencies must be a sequence");
    }
    for (const auto& dependencyNode : value->get_value_ref<const YamlNode::sequence_type&>()) {
        arp::resource::ResourceDependency dependency;
        dependency.id = yamlString(dependencyNode, "id");
        dependency.usage = yamlString(dependencyNode, "usage");
        dependencies.push_back(std::move(dependency));
    }
}

Value key(const char* name)
{
    return Value{String(name, &arp::core::defaultCpuMemoryResource())};
}

template <typename T> Value numberList(const T* values, std::size_t count)
{
    hlist list(hlist::allocator_type(&arp::core::defaultCpuMemoryResource()));
    list.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        list.emplace_back(arp::scriptvm::makeFloat(values[index]));
    }
    return Value{std::move(list)};
}

int ImportedModel::vertexCount() const
{
    return static_cast<int>(asset.mesh.vertices.size());
}

int ImportedModel::indexCount() const
{
    return static_cast<int>(asset.mesh.indices.size());
}

int ImportedModel::jointCount() const
{
    return static_cast<int>(asset.skeleton.joints.size());
}

String ImportedModel::jointName(int index) const
{
    if (index < 0 || index >= static_cast<int>(asset.skeleton.joints.size())) {
        throw std::out_of_range("ImportedModel joint index is out of range");
    }
    return asset.skeleton.joints[static_cast<std::size_t>(index)].name;
}

int ImportedModel::submeshCount() const
{
    return static_cast<int>(asset.mesh.submeshes.size());
}

int ImportedModel::materialCount() const
{
    return static_cast<int>(asset.materials.size());
}

String ImportedModel::materialName(int index) const
{
    if (index < 0 || index >= static_cast<int>(asset.materials.size())) {
        throw std::out_of_range("ImportedModel material index is out of range");
    }
    return asset.materials[static_cast<std::size_t>(index)].name;
}

int ImportedModel::clipCount() const
{
    return static_cast<int>(asset.clips.size());
}

String ImportedModel::clipName(int index) const
{
    if (index < 0 || index >= static_cast<int>(asset.clips.size())) {
        throw std::out_of_range("ImportedModel clip index is out of range");
    }
    return asset.clips[static_cast<std::size_t>(index)].name;
}

float ImportedModel::clipDuration(int index) const
{
    if (index < 0 || index >= static_cast<int>(asset.clips.size())) {
        throw std::out_of_range("ImportedModel clip index is out of range");
    }
    return asset.clips[static_cast<std::size_t>(index)].durationSeconds;
}

int ImportedModel::clipChannelCount(int index) const
{
    if (index < 0 || index >= static_cast<int>(asset.clips.size())) {
        throw std::out_of_range("ImportedModel clip index is out of range");
    }
    return static_cast<int>(asset.clips[static_cast<std::size_t>(index)].channels.size());
}

bool ImportedModel::meshValidForIndexedDraw() const
{
    return asset.mesh.validForIndexedDraw();
}

bool ImportedModel::skeletonValid() const
{
    return asset.skeleton.valid();
}

int ImportedModel::bindPoseCount() const
{
    return static_cast<int>(asset.bindPose.size());
}

int ImportedModel::skinnedVertexCount() const
{
    return static_cast<int>(asset.skinnedVertices.size());
}

int ImportedModel::indexValue(int index) const
{
    if (index < 0 || index >= static_cast<int>(asset.mesh.indices.size())) {
        throw std::out_of_range("ImportedModel index is out of range");
    }
    return static_cast<int>(asset.mesh.indices[static_cast<std::size_t>(index)]);
}

float ImportedModel::vertexPositionComponent(int index, int component) const
{
    if (index < 0 || index >= static_cast<int>(asset.mesh.vertices.size())) {
        throw std::out_of_range("ImportedModel vertex index is out of range");
    }
    if (component < 0 || component >= 3) {
        throw std::out_of_range("ImportedModel vertex position component is out of range");
    }
    return asset.mesh.vertices[static_cast<std::size_t>(index)]
        .position[static_cast<std::size_t>(component)];
}

bool ImportedModel::clipDurationFinite(int index) const
{
    if (index < 0 || index >= static_cast<int>(asset.clips.size())) {
        throw std::out_of_range("ImportedModel clip index is out of range");
    }
    return std::isfinite(asset.clips[static_cast<std::size_t>(index)].durationSeconds);
}

ModelJointInfo ImportedModel::jointInfo(int index) const
{
    if (index < 0 || index >= static_cast<int>(asset.skeleton.joints.size())) {
        throw std::out_of_range("ImportedModel joint index is out of range");
    }
    const auto& joint = asset.skeleton.joints[static_cast<std::size_t>(index)];
    ModelJointInfo result;
    result.name = joint.name;
    result.parentIndex = joint.parentIndex;
    result.inverseBindFinite = std::all_of(
        joint.inverseBindMatrix.m.begin(), joint.inverseBindMatrix.m.end(), [](float value) {
            return std::isfinite(value);
        });
    return result;
}

ModelVertexInfo ImportedModel::vertexInfo(int index) const
{
    if (index < 0 || index >= static_cast<int>(asset.mesh.vertices.size())) {
        throw std::out_of_range("ImportedModel vertex index is out of range");
    }
    const auto& vertex = asset.mesh.vertices[static_cast<std::size_t>(index)];
    ModelVertexInfo result;
    result.positionX = vertex.position[0];
    result.positionY = vertex.position[1];
    result.positionZ = vertex.position[2];
    result.positionFinite =
        std::all_of(std::begin(vertex.position), std::end(vertex.position), [](float value) {
            return std::isfinite(value);
        });
    return result;
}

namespace {

ModelSampledVertexInfo sampleVertex(const arp::assets::AnimatedMeshAsset& asset,
    int clipIndex,
    float normalizedTime,
    int vertexIndex)
{
    if (vertexIndex < 0 || vertexIndex >= static_cast<int>(asset.mesh.vertices.size())) {
        throw std::out_of_range("ImportedModel vertex index is out of range");
    }
    if (clipIndex < -1 || clipIndex >= static_cast<int>(asset.clips.size())) {
        throw std::out_of_range("ImportedModel clip index is out of range");
    }
    if (!std::isfinite(normalizedTime) || normalizedTime < 0.0f || normalizedTime > 1.0f) {
        throw std::out_of_range("ImportedModel normalized sample time is out of range");
    }

    arp::core::pmr::Vector<animation::LocalTransform> localPose{
        &arp::core::defaultCpuMemoryResource()};
    if (clipIndex < 0) {
        localPose = asset.bindPose;
    } else {
        arp::core::pmr::Vector<animation::JointTransform> sampledPose{
            &arp::core::defaultCpuMemoryResource()};
        // sampleClip applies sparse animation channels to a complete pose;
        // channels that are absent from a clip must retain their bind pose.
        sampledPose.reserve(asset.bindPose.size());
        for (const auto& bindTransform : asset.bindPose) {
            sampledPose.push_back(animation::JointTransform{
                bindTransform.translation, bindTransform.rotation, bindTransform.scale});
        }
        const auto& clip = asset.clips[static_cast<std::size_t>(clipIndex)];
        if (!animation::sampleClip(clip, clip.durationSeconds * normalizedTime, sampledPose)) {
            return ModelSampledVertexInfo{};
        }
        localPose.reserve(sampledPose.size());
        for (const auto& transform : sampledPose) {
            localPose.push_back(animation::LocalTransform{
                transform.translation, transform.rotation, transform.scale});
        }
    }
    const auto modelPose = animation::buildModelPose(asset.skeleton, localPose);
    const auto skinMatrices = animation::buildSkinMatrices(asset.skeleton, modelPose);
    if (skinMatrices.size() != asset.skeleton.jointCount()
        || static_cast<std::size_t>(vertexIndex) >= asset.skinnedVertices.size()) {
        return ModelSampledVertexInfo{};
    }
    const auto& source = asset.mesh.vertices[static_cast<std::size_t>(vertexIndex)];
    const auto& influences = asset.skinnedVertices[static_cast<std::size_t>(vertexIndex)];
    const animation::Vec3 sourcePosition{
        source.position[0], source.position[1], source.position[2]};
    animation::Vec3 blended{};
    float totalWeight = 0.0f;
    for (std::size_t slot = 0; slot < influences.joints.size(); ++slot) {
        const auto weight = influences.weights[slot];
        if (weight <= 0.0f) {
            continue;
        }
        const auto joint = static_cast<std::size_t>(influences.joints[slot]);
        if (joint >= skinMatrices.size() || !std::isfinite(weight)) {
            return ModelSampledVertexInfo{};
        }
        const auto transformed = animation::transformPoint(skinMatrices[joint], sourcePosition);
        blended.x += transformed.x * weight;
        blended.y += transformed.y * weight;
        blended.z += transformed.z * weight;
        totalWeight += weight;
    }
    ModelSampledVertexInfo result;
    result.positionX = blended.x;
    result.positionY = blended.y;
    result.positionZ = blended.z;
    result.available = totalWeight > 0.000001f && std::isfinite(blended.x)
        && std::isfinite(blended.y) && std::isfinite(blended.z);
    return result;
}

} // namespace

bool ModelSampledVertexInfo::isAvailable() const
{
    return available;
}

ModelSampledVertexInfo ImportedModel::bindPoseVertex(int index) const
{
    return sampleVertex(asset, -1, 0.0f, index);
}

ModelSampledVertexInfo ImportedModel::sampledVertex(
    int clipIndex, float normalizedTime, int vertexIndex) const
{
    return sampleVertex(asset, clipIndex, normalizedTime, vertexIndex);
}

bool ImportedModel::sampledVertexAvailable(
    int clipIndex, float normalizedTime, int vertexIndex) const
{
    return sampleVertex(asset, clipIndex, normalizedTime, vertexIndex).available;
}

float ImportedModel::sampledVertexPositionComponent(
    int clipIndex, float normalizedTime, int vertexIndex, int component) const
{
    if (component < 0 || component >= 3) {
        throw std::out_of_range("ImportedModel sampled vertex position component is out of range");
    }
    const auto sampled = sampleVertex(asset, clipIndex, normalizedTime, vertexIndex);
    if (!sampled.available) {
        throw std::runtime_error("ImportedModel sampled vertex position is unavailable");
    }
    if (component == 0)
        return sampled.positionX;
    if (component == 1)
        return sampled.positionY;
    return sampled.positionZ;
}

SkinInfluenceInfo ImportedModel::skinInfluence(int vertexIndex, int slot) const
{
    if (vertexIndex < 0 || vertexIndex >= static_cast<int>(asset.skinnedVertices.size())) {
        throw std::out_of_range("ImportedModel skinned vertex index is out of range");
    }
    if (slot < 0 || slot >= 4) {
        throw std::out_of_range("ImportedModel skin influence slot is out of range");
    }
    const auto& influence = asset.skinnedVertices[static_cast<std::size_t>(vertexIndex)];
    SkinInfluenceInfo result;
    result.jointIndex = static_cast<int>(influence.joints[static_cast<std::size_t>(slot)]);
    result.weight = influence.weights[static_cast<std::size_t>(slot)];
    return result;
}

ModelAnimationChannelInfo ImportedModel::clipChannelInfo(int clipIndex, int channelIndex) const
{
    if (clipIndex < 0 || clipIndex >= static_cast<int>(asset.clips.size())) {
        throw std::out_of_range("ImportedModel clip index is out of range");
    }
    const auto& clip = asset.clips[static_cast<std::size_t>(clipIndex)];
    if (channelIndex < 0 || channelIndex >= static_cast<int>(clip.channels.size())) {
        throw std::out_of_range("ImportedModel animation channel index is out of range");
    }
    const auto& channel = clip.channels[static_cast<std::size_t>(channelIndex)];
    ModelAnimationChannelInfo result;
    result.jointIndex = static_cast<int>(channel.jointIndex);
    result.targetPath = channel.targetPath;
    result.keyframeCount = static_cast<int>(channel.keyframes.size());
    return result;
}

ModelAnimationKeyframeInfo ImportedModel::clipKeyframeInfo(
    int clipIndex, int channelIndex, int keyframeIndex) const
{
    if (clipIndex < 0 || clipIndex >= static_cast<int>(asset.clips.size())) {
        throw std::out_of_range("ImportedModel clip index is out of range");
    }
    const auto& clip = asset.clips[static_cast<std::size_t>(clipIndex)];
    if (channelIndex < 0 || channelIndex >= static_cast<int>(clip.channels.size())) {
        throw std::out_of_range("ImportedModel animation channel index is out of range");
    }
    const auto& channel = clip.channels[static_cast<std::size_t>(channelIndex)];
    if (keyframeIndex < 0 || keyframeIndex >= static_cast<int>(channel.keyframes.size())) {
        throw std::out_of_range("ImportedModel keyframe index is out of range");
    }
    const auto& keyframe = channel.keyframes[static_cast<std::size_t>(keyframeIndex)];
    const auto& transform = keyframe.transform;
    ModelAnimationKeyframeInfo result;
    result.timeSeconds = keyframe.timeSeconds;
    result.translationX = transform.translation.x;
    result.translationY = transform.translation.y;
    result.translationZ = transform.translation.z;
    result.rotationX = transform.rotation.x;
    result.rotationY = transform.rotation.y;
    result.rotationZ = transform.rotation.z;
    result.rotationW = transform.rotation.w;
    result.scaleX = transform.scale.x;
    result.scaleY = transform.scale.y;
    result.scaleZ = transform.scale.z;
    result.transformFinite = std::isfinite(result.timeSeconds) && std::isfinite(result.translationX)
        && std::isfinite(result.translationY) && std::isfinite(result.translationZ)
        && std::isfinite(result.rotationX) && std::isfinite(result.rotationY)
        && std::isfinite(result.rotationZ) && std::isfinite(result.rotationW)
        && std::isfinite(result.scaleX) && std::isfinite(result.scaleY)
        && std::isfinite(result.scaleZ);
    return result;
}

float ImportedModel::clipKeyframeComponent(
    int clipIndex, int channelIndex, int keyframeIndex, int component) const
{
    if (component < 0 || component >= 10) {
        throw std::out_of_range("ImportedModel keyframe component is out of range");
    }
    const auto keyframe = clipKeyframeInfo(clipIndex, channelIndex, keyframeIndex);
    if (component == 0)
        return keyframe.translationX;
    if (component == 1)
        return keyframe.translationY;
    if (component == 2)
        return keyframe.translationZ;
    if (component == 3)
        return keyframe.rotationX;
    if (component == 4)
        return keyframe.rotationY;
    if (component == 5)
        return keyframe.rotationZ;
    if (component == 6)
        return keyframe.rotationW;
    if (component == 7)
        return keyframe.scaleX;
    if (component == 8)
        return keyframe.scaleY;
    return keyframe.scaleZ;
}

bool ImportedModel::clipKeyframeTransformFinite(
    int clipIndex, int channelIndex, int keyframeIndex) const
{
    return clipKeyframeInfo(clipIndex, channelIndex, keyframeIndex).transformFinite;
}

void AvatarBinding::setHumanoid(bool value)
{
    humanoid = value;
}

bool AvatarBinding::addHumanBone(const String& boneName, const String& humanName)
{
    if (boneName.empty() || humanName.empty()) {
        return false;
    }
    humanBones_.push_back(AvatarHumanBoneBinding{{}, boneName, humanName});
    return true;
}

bool AvatarBinding::addBoneMapping(
    const String& sourceBoneName, const String& targetBoneName, const String& humanName)
{
    if (sourceBoneName.empty() || targetBoneName.empty() || humanName.empty()) {
        return false;
    }
    humanBones_.push_back(AvatarHumanBoneBinding{sourceBoneName, targetBoneName, humanName});
    return true;
}

bool AvatarBinding::addSkeletonNode(const String& name, const String& parentName)
{
    if (name.empty()) {
        return false;
    }
    skeletonNodes_.push_back(AvatarSkeletonNodeBinding{name, parentName});
    return true;
}

int AvatarBinding::humanBoneCount() const
{
    return static_cast<int>(humanBones_.size());
}

int AvatarBinding::skeletonNodeCount() const
{
    return static_cast<int>(skeletonNodes_.size());
}

const arp::core::pmr::Vector<AvatarHumanBoneBinding>& AvatarBinding::humanBones() const
{
    return humanBones_;
}

const arp::core::pmr::Vector<AvatarSkeletonNodeBinding>& AvatarBinding::skeletonNodes() const
{
    return skeletonNodes_;
}

Value ImportedModel::toRecord() const
{
    hdict record{
        arp::core::pmr::Map<Value, Value>::allocator_type(&arp::core::defaultCpuMemoryResource())};
    record[key("success")] = Value{success};
    record[key("source")] = Value{source};
    record[key("diagnostics")] = Value{diagnostics};

    hlist vertices(hlist::allocator_type(&arp::core::defaultCpuMemoryResource()));
    vertices.reserve(asset.mesh.vertices.size());
    for (const auto& vertex : asset.mesh.vertices) {
        hdict value{arp::core::pmr::Map<Value, Value>::allocator_type(
            &arp::core::defaultCpuMemoryResource())};
        value[key("position")] = numberList(vertex.position, 3u);
        value[key("normal")] = numberList(vertex.normal, 3u);
        value[key("uv")] = numberList(vertex.uv, 2u);
        value[key("tangent")] = numberList(vertex.tangent, 4u);
        vertices.emplace_back(std::move(value));
    }
    record[key("vertices")] = Value{std::move(vertices)};

    hlist indices(hlist::allocator_type(&arp::core::defaultCpuMemoryResource()));
    indices.reserve(asset.mesh.indices.size());
    for (const auto index : asset.mesh.indices) {
        indices.emplace_back(arp::scriptvm::makeInt64(index));
    }
    record[key("indices")] = Value{std::move(indices)};

    hlist submeshes(hlist::allocator_type(&arp::core::defaultCpuMemoryResource()));
    submeshes.reserve(asset.mesh.submeshes.size());
    for (const auto& submesh : asset.mesh.submeshes) {
        hdict value{arp::core::pmr::Map<Value, Value>::allocator_type(
            &arp::core::defaultCpuMemoryResource())};
        value[key("firstIndex")] = arp::scriptvm::makeInt64(submesh.firstIndex);
        value[key("indexCount")] = arp::scriptvm::makeInt64(submesh.indexCount);
        value[key("materialSlot")] = arp::scriptvm::makeInt64(submesh.materialSlot);
        submeshes.emplace_back(std::move(value));
    }
    record[key("submeshes")] = Value{std::move(submeshes)};

    hlist joints(hlist::allocator_type(&arp::core::defaultCpuMemoryResource()));
    joints.reserve(asset.skeleton.joints.size());
    for (const auto& joint : asset.skeleton.joints) {
        hdict value{arp::core::pmr::Map<Value, Value>::allocator_type(
            &arp::core::defaultCpuMemoryResource())};
        value[key("name")] = Value{joint.name};
        value[key("parentIndex")] = arp::scriptvm::makeInt(joint.parentIndex);
        value[key("inverseBindMatrix")] = numberList(joint.inverseBindMatrix.m.data(), 16u);
        joints.emplace_back(std::move(value));
    }
    record[key("joints")] = Value{std::move(joints)};

    hlist clips(hlist::allocator_type(&arp::core::defaultCpuMemoryResource()));
    clips.reserve(asset.clips.size());
    for (const auto& clip : asset.clips) {
        hdict value{arp::core::pmr::Map<Value, Value>::allocator_type(
            &arp::core::defaultCpuMemoryResource())};
        value[key("name")] = Value{clip.name};
        value[key("durationSeconds")] = arp::scriptvm::makeFloat(clip.durationSeconds);
        value[key("channelCount")] = arp::scriptvm::makeInt64(clip.channels.size());
        clips.emplace_back(std::move(value));
    }
    record[key("clips")] = Value{std::move(clips)};
    return Value{std::move(record)};
}

ImportedModel importGlb(const String& path)
{
    const auto imported =
        arp::assets::importAnimatedMeshFromGlbFile(std::filesystem::path(path.c_str()));
    ImportedModel result;
    result.success = imported.success;
    result.source = path;
    result.diagnostics = imported.diagnostics;
    result.asset = std::move(imported.asset);
    return result;
}

ImportedModel importFbx(const String& path)
{
    arp::assets::FbxCharacterImporter importer;
    const auto imported = importer.importFile(std::filesystem::path(path.c_str()));
    ImportedModel result;
    result.success = imported.success;
    result.source = path;
    result.diagnostics = imported.diagnostics;
    result.asset = std::move(imported.asset);
    return result;
}

RuntimePayloadResult writePrefabRuntimePayload(const String& packageRoot,
    const String& blobPath,
    const String& displayName,
    const ImportedModel& imported,
    const String& defaultStateName)
{
    RuntimePayloadResult result;
    if (!imported.success) {
        result.diagnostics =
            String("runtime payload source import failed", &arp::core::defaultCpuMemoryResource());
        return result;
    }
    if (imported.asset.clips.empty()) {
        result.diagnostics = String("runtime payload source has no animation clips",
            &arp::core::defaultCpuMemoryResource());
        return result;
    }

    String resolvedDefaultState = defaultStateName;
    if (resolvedDefaultState.empty()) {
        resolvedDefaultState = imported.asset.clips.front().name;
    }
    bool defaultStateFound = false;
    arp::core::pmr::Vector<arp::resource::PrefabRuntimeStateBinding> bindings{
        &arp::core::defaultCpuMemoryResource()};
    for (const auto& clip : imported.asset.clips) {
        if (clip.name == resolvedDefaultState) {
            defaultStateFound = true;
        }
        arp::resource::PrefabRuntimeStateBinding binding;
        binding.stateName = clip.name;
        binding.clipName = clip.name;
        binding.speed = 1.0f;
        bindings.push_back(std::move(binding));
    }
    if (!defaultStateFound) {
        result.diagnostics = String("runtime payload default state does not match an imported clip",
            &arp::core::defaultCpuMemoryResource());
        return result;
    }

    arp::resource::PrefabRuntimePayloadWriteDesc desc;
    animation::AnimatorControllerAsset controller;
    controller.name = displayName;
    controller.defaultStateName = resolvedDefaultState;
    for (const auto& binding : bindings) {
        const auto clip = std::find_if(imported.asset.clips.begin(), imported.asset.clips.end(),
            [&](const animation::AnimationClip& value) { return value.name == binding.clipName; });
        if (clip == imported.asset.clips.end()) {
            result.diagnostics = String("runtime payload state references an unknown clip",
                &arp::core::defaultCpuMemoryResource());
            return result;
        }
        animation::AnimatorState state;
        state.name = binding.stateName;
        state.clipName = binding.clipName;
        state.clipDurationSeconds = clip->durationSeconds;
        state.speed = binding.speed;
        controller.states.push_back(std::move(state));
    }
    desc.packageRoot = std::filesystem::path(packageRoot.c_str());
    desc.blobPath = blobPath;
    desc.displayName = displayName;
    desc.asset = &imported.asset;
    desc.controllerAsset = &controller;
    desc.defaultStateName = resolvedDefaultState;
    desc.stateBindings = std::move(bindings);
    const auto written = arp::resource::writePrefabRuntimePayload(desc);
    result.success = written.success;
    result.blobPath = written.resource.blobPath;
    result.blobHash = written.resource.blobHash;
    result.blobByteSize = written.resource.blobByteSize;
    result.stateBindingCount = written.resource.stateBindingCount;
    result.materialCount = written.resource.materialCount;
    result.textureCount = written.resource.textureCount;
    result.imageCount = written.resource.imageCount;
    result.defaultStateName = written.resource.defaultStateName;
    result.defaultClipName = written.resource.defaultClipName;
    result.diagnostics = written.diagnostics;
    return result;
}

ImportedAnimationClip retargetFbxClipInternal(const String& sourcePath,
    const String& clipName,
    const String& targetPath,
    const arp::assets::AvatarAsset& avatar)
{
    ImportedAnimationClip result;
    result.source = sourcePath;
    if (clipName.empty()) {
        result.diagnostics = "retarget_fbx_clip requires a clip name";
        return result;
    }

    arp::assets::FbxCharacterImporter importer;
    const auto source = importer.importFile(std::filesystem::path(sourcePath.c_str()),
        arp::assets::FbxCharacterImportOptions{false, true, false});
    if (!source.success) {
        result.diagnostics = source.diagnostics;
        return result;
    }
    const auto target = importer.importFile(std::filesystem::path(targetPath.c_str()),
        arp::assets::FbxCharacterImportOptions{true, false, false});
    if (!target.success) {
        result.diagnostics = target.diagnostics;
        return result;
    }
    const animation::AnimationClip* sourceClip = nullptr;
    for (const auto& candidate : source.asset.clips) {
        if (candidate.name == clipName) {
            if (sourceClip != nullptr) {
                result.diagnostics = String("source FBX contains duplicate clip name: ",
                    &arp::core::defaultCpuMemoryResource());
                result.diagnostics += clipName;
                return result;
            }
            sourceClip = &candidate;
        }
    }
    if (sourceClip == nullptr) {
        result.diagnostics =
            String("source FBX clip was not found: ", &arp::core::defaultCpuMemoryResource());
        result.diagnostics += clipName;
        return result;
    }
    if (!source.asset.skeleton.valid()
        || source.asset.bindPose.size() != source.asset.skeleton.jointCount()
        || !target.asset.skeleton.valid()
        || target.asset.bindPose.size() != target.asset.skeleton.jointCount()) {
        result.diagnostics = "FBX retarget requires valid source and target skeleton bind poses";
        return result;
    }
    const auto retargeted = arp::assets::retargetClipToSkeleton(*sourceClip,
        target.asset.skeleton,
        target.asset.bindPose,
        source.asset.skeleton,
        source.asset.bindPose,
        avatar);
    if (retargeted.channels.empty()) {
        result.diagnostics = String(
            "FBX clip retarget produced no channels: ", &arp::core::defaultCpuMemoryResource());
        result.diagnostics += clipName;
        return result;
    }
    result.success = true;
    result.name = retargeted.name;
    result.durationSeconds = retargeted.durationSeconds;
    result.channelCount = static_cast<int>(retargeted.channels.size());
    return result;
}

ImportedAnimationClip retargetFbxClip(
    const String& sourcePath, const String& clipName, const String& targetPath)
{
    return retargetFbxClipInternal(sourcePath, clipName, targetPath, arp::assets::AvatarAsset{});
}

ImportedAnimationClip retargetFbxClipWithAvatar(const String& sourcePath,
    const String& clipName,
    const String& targetPath,
    const AvatarBinding& binding)
{
    if (!binding.humanoid) {
        return retargetFbxClip(sourcePath, clipName, targetPath);
    }
    if (binding.humanBones().empty() || binding.skeletonNodes().empty()) {
        ImportedAnimationClip result;
        result.source = sourcePath;
        result.diagnostics = "humanoid AvatarBinding requires human bones and skeleton nodes";
        return result;
    }

    arp::assets::AvatarAsset avatar;
    avatar.kind = arp::assets::AvatarKind::Humanoid;
    avatar.animationType = arp::assets::AnimationType::Humanoid;
    avatar.setup = arp::assets::AvatarSetup::CreateFromThisModel;
    avatar.humanDescription.declared = true;
    avatar.humanDescription.humanSectionDeclared = true;
    avatar.humanDescription.skeletonSectionDeclared = true;
    for (const auto& bone : binding.humanBones()) {
        arp::assets::HumanBoneMapping mapping;
        mapping.sourceBoneName = bone.sourceBoneName;
        mapping.boneName = bone.boneName;
        mapping.humanName = bone.humanName;
        avatar.humanDescription.humanBones.push_back(std::move(mapping));
    }
    for (const auto& node : binding.skeletonNodes()) {
        arp::assets::HumanSkeletonNode skeletonNode;
        skeletonNode.name = node.name;
        skeletonNode.parentName = node.parentName;
        avatar.humanDescription.skeletonNodes.push_back(std::move(skeletonNode));
    }
    return retargetFbxClipInternal(sourcePath, clipName, targetPath, avatar);
}

DecodedImageResult decodeImage(const String& path, const String& mime, bool srgb)
{
    const auto bytes = arp::core::readBinaryFile(std::filesystem::path(path.c_str()));
    if (!bytes.success) {
        DecodedImageResult result;
        result.diagnostics = bytes.diagnostics;
        return result;
    }
    const auto decoded = arp::assets::decodeImageToRgba8(bytes.bytes, mime, srgb);
    DecodedImageResult result;
    result.success = decoded.success;
    result.diagnostics = decoded.diagnostics;
    result.width = decoded.image.width;
    result.height = decoded.image.height;
    result.srgb = decoded.image.srgb;
    result.alpha = decoded.image.alpha;
    result.rgba8.reserve(decoded.image.rgba8.size());
    for (const auto byte : decoded.image.rgba8) {
        result.rgba8.emplace_back(arp::scriptvm::makeInt(static_cast<int>(byte)));
    }
    return result;
}

UnityAssetDatabaseResult scanUnityAssets(const String& root)
{
    UnityAssetDatabaseResult result;
    const auto database = arp::assets::scanUnityAssetDatabase(std::filesystem::path(root.c_str()));
    result.success = database.success;
    result.assetCount = database.assetCount;
    result.errorCount = database.errorCount;
    result.warningCount = database.warningCount;
    result.diagnostics = database.diagnostics;
    return result;
}

UnityAssetRecordResult findUnityAsset(const String& root, const String& guid)
{
    UnityAssetRecordResult result;
    const auto database = arp::assets::findUnityAsset(std::filesystem::path(root.c_str()), guid);
    result.success = database.success;
    result.guid = database.guid;
    result.metaPath = database.metaPath;
    result.assetPath = database.assetPath;
    result.diagnostics = database.diagnostics;
    return result;
}

AssetHashResult hashPayload(const String& payloadYaml)
{
    const auto result = arp::resource::computeResourcePayloadHash(payloadYaml);
    AssetHashResult output;
    output.success = result.success;
    output.hash = static_cast<std::int64_t>(result.hash);
    output.canonicalPayloadYaml = result.canonicalPayloadYaml;
    output.diagnostics = result.diagnostics;
    return output;
}

String uuidFromText(const String& text)
{
    const auto digest = arp::core::Sha1::digest(arp::core::StringView(text.data(), text.size()));
    auto mutableDigest = digest;
    mutableDigest[6] = static_cast<std::uint8_t>((mutableDigest[6] & 0x0fu) | 0x50u);
    mutableDigest[8] = static_cast<std::uint8_t>((mutableDigest[8] & 0x3fu) | 0x80u);
    return uuidFromDigest(mutableDigest.data());
}

String unityGuidToUuid(const String& guid)
{
    if (guid.size() != 32u) {
        return String(&arp::core::defaultCpuMemoryResource());
    }
    for (const char value : guid) {
        if (!std::isxdigit(static_cast<unsigned char>(value))) {
            return String(&arp::core::defaultCpuMemoryResource());
        }
    }
    String result(&arp::core::defaultCpuMemoryResource());
    result.reserve(36u);
    for (std::size_t index = 0; index < guid.size(); ++index) {
        if (index == 8u || index == 12u || index == 16u || index == 20u) {
            result.push_back('-');
        }
        result.push_back(guid[index]);
    }
    return result;
}

bool isResourceUuid(const String& value)
{
    return arp::resource::isResourceUuid(value);
}

AssetPackageWriteResult writePackage(const String& outputDirectory, const String& manifestYaml)
{
    AssetPackageWriteResult output;
    try {
        const auto root =
            YamlNode::deserialize(manifestYaml.data(), manifestYaml.data() + manifestYaml.size());
        arp::resource::InternalAssetPackageDesc package;
        package.name = yamlString(root, "name");
        package.status = yamlString(root, "status");
        const auto* entries = yamlValue(root, "entries");
        if (entries == nullptr || !entries->is_sequence()) {
            throw std::runtime_error("package manifest requires entries sequence");
        }
        const std::filesystem::path directory(outputDirectory.c_str());
        for (const auto& entryNode : entries->get_value_ref<const YamlNode::sequence_type&>()) {
            arp::resource::InternalAssetPackageEntry entry;
            entry.resourceId = yamlString(entryNode, "resourceId");
            entry.resourceType = yamlString(entryNode, "resourceType");
            entry.alias = yamlString(entryNode, "alias");
            entry.envelopePath = yamlString(entryNode, "envelopePath");
            entry.status = yamlString(entryNode, "status");
            entry.contentHash = static_cast<std::uint32_t>(yamlInteger(entryNode, "contentHash"));
            appendDependencies(entryNode, entry.dependencies);
            const auto envelope = arp::core::readTextFile(
                directory / std::filesystem::path(entry.envelopePath.c_str()));
            if (!envelope.success) {
                throw std::runtime_error(
                    std::string("missing package envelope: ") + entry.envelopePath.c_str());
            }
            entry.envelopeYaml = envelope.text;
            package.entries.push_back(std::move(entry));
        }
        const auto written = arp::resource::InternalAssetPackageWriter().write(package, directory);
        output.success = written.success;
        output.entryCount = static_cast<int>(written.package.entries.size());
        output.diagnostics = written.diagnostics;
        return output;
    } catch (const std::exception& error) {
        output.diagnostics = String(error.what(), &arp::core::defaultCpuMemoryResource());
        return output;
    }
}

} // namespace arp::scriptvm::plugins::asset

namespace arp::reflection::generated {
bool register_asset_reflection(arp::scriptvm::ModuleRegistration* module);
} // namespace arp::reflection::generated

extern "C" ARP_SCRIPT_PLUGIN_EXPORT int arp_register_asset(ArpScriptPluginHostV2* host)
{
    if (host == nullptr || host->abiVersion != ARP_SCRIPT_PLUGIN_ABI_VERSION_V2
        || host->registrar == nullptr) {
        return 0;
    }

    return arp::reflection::generated::register_asset_reflection(
               host->registrar->reflectionModule())
        ? 1
        : 0;
}
