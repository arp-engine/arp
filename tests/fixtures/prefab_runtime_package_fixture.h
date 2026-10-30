#pragma once

#include "resource/prefab_asset_resources.h"

#include <cassert>

namespace arp::tests::prefab_runtime_package_fixture {

constexpr const char* kPrefabId = "0b5f7a2d-78a7-47d2-91f5-4ab0f8b827e1";
constexpr const char* kBasicShaderId = "f0817aa6-f639-46c9-b9de-0ea317a2b94d";
constexpr const char* kRuntimePayloadId = "c8591a23-0f99-4070-a4d0-d79ab87379eb";
constexpr const char* kAnimatorControllerId = "3c71905f-6c79-42d9-a8e1-69549913fc23";

inline arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

inline arp::resource::ResourceDependency dependency(const char* id, const char* usage)
{
    arp::resource::ResourceDependency result;
    result.id = text(id);
    result.usage = text(usage);
    return result;
}

template <typename T>
arp::resource::InternalAssetPackageEntry makeEntry(const char* id,
    const char* type,
    const char* alias,
    const char* path,
    const arp::core::pmr::Vector<arp::resource::ResourceDependency>& dependencies,
    const T& payload)
{
    arp::resource::ResourceEnvelopeBuildDesc envelopeDesc;
    envelopeDesc.resourceId = text(id);
    envelopeDesc.resourceType = text(type);
    envelopeDesc.alias = text(alias);
    envelopeDesc.displayName = text(alias);
    envelopeDesc.status = text("Prototype Complete");
    envelopeDesc.dependencies = dependencies;

    const auto envelope =
        arp::resource::buildResourceEnvelope(envelopeDesc, arp::reflection::object(payload));
    assert(envelope.success);

    arp::resource::InternalAssetPackageEntry entry;
    entry.resourceId = text(id);
    entry.resourceType = text(type);
    entry.alias = text(alias);
    entry.envelopePath = text(path);
    entry.status = text("Prototype Complete");
    entry.contentHash = envelope.contentHash;
    entry.dependencies = dependencies;
    entry.envelopeYaml = envelope.envelopeYaml;
    return entry;
}

inline arp::assets::AnimatedMeshAsset makeRuntimeAsset()
{
    arp::assets::AnimatedMeshAsset asset;
    asset.mesh = arp::assets::MeshAsset::debugTriangle();
    asset.skinnedVertices.resize(asset.mesh.vertices.size());
    for (auto& vertex : asset.skinnedVertices) {
        vertex.joints = {0, 0, 0, 0};
        vertex.weights = {1.0f, 0.0f, 0.0f, 0.0f};
    }
    animation::Joint joint;
    joint.name = "root";
    joint.parentIndex = -1;
    asset.skeleton.joints.push_back(joint);
    asset.bindPose.push_back(animation::LocalTransform{});

    animation::AnimationClip clip;
    clip.name = "idle";
    clip.durationSeconds = 1.0f;
    animation::AnimationChannel channel;
    channel.jointIndex = 0;
    channel.jointName = "root";
    channel.targetPath = "translation";
    channel.keyframes.push_back(animation::JointKeyframe{});
    animation::JointKeyframe endFrame;
    endFrame.timeSeconds = 1.0f;
    channel.keyframes.push_back(endFrame);
    clip.channels.push_back(channel);
    asset.clips.push_back(clip);
    auto runClip = clip;
    runClip.name = "run";
    asset.clips.push_back(runClip);

    arp::assets::ImportedMaterialAsset material;
    material.name = "material";
    material.baseColorTextureCoordinateSet = 2u;
    material.normalTextureCoordinateSet = 1u;
    material.hasCompleteMaterialImportMetadata = true;
    material.hasUnsupportedMaterialExtensions = true;
    asset.materials.push_back(material);
    return asset;
}

inline animation::AnimatorControllerAsset makeController()
{
    animation::AnimatorControllerAsset controller;
    controller.name = "runtime-controller";
    controller.defaultStateName = "idle";
    animation::AnimatorParameterDefinition moving;
    moving.name = "moving";
    moving.type = animation::AnimatorParameterType::Bool;
    moving.defaultBool = false;
    controller.parameters.push_back(moving);
    animation::AnimatorState idle;
    idle.name = "idle";
    idle.clipName = "idle";
    idle.clipDurationSeconds = 1.0f;
    controller.states.push_back(idle);
    animation::AnimatorState run;
    run.name = "run";
    run.clipName = "run";
    run.clipDurationSeconds = 1.0f;
    run.speed = 1.5f;
    controller.states.push_back(run);
    animation::AnimatorTransition startRunning;
    startRunning.sourceState = "idle";
    startRunning.destinationState = "run";
    startRunning.durationSeconds = 0.2f;
    animation::AnimatorCondition startCondition;
    startCondition.parameter = "moving";
    startCondition.mode = animation::AnimatorConditionMode::If;
    startRunning.conditions.push_back(startCondition);
    controller.transitions.push_back(startRunning);
    return controller;
}

inline resource::InternalAssetPackageDesc makePackage(
    const resource::PrefabRuntimePayloadResource& payload)
{
    arp::core::pmr::Vector<arp::resource::ResourceDependency> prefabDependencies{
        &arp::core::defaultCpuMemoryResource()};
    prefabDependencies.push_back(dependency(kRuntimePayloadId, "runtime-payload"));
    prefabDependencies.push_back(dependency(kAnimatorControllerId, "animator-controller"));
    arp::resource::PrefabResource prefab;
    prefab.name = text("runtime");
    prefab.runtimePayloadId = text(kRuntimePayloadId);

    arp::core::pmr::Vector<arp::resource::ResourceDependency> noDependencies{
        &arp::core::defaultCpuMemoryResource()};
    arp::resource::AnimatorControllerResource animatorResource;
    animatorResource.name = text("runtime-controller");
    animatorResource.defaultStateName = text("idle");
    animatorResource.stateCount = 2;
    animatorResource.parameterCount = 1;
    arp::resource::InternalAssetPackageDesc package;
    package.name = text("runtime");
    package.status = text("Prototype Complete");
    package.entries.push_back(makeEntry(kRuntimePayloadId,
        "PrefabRuntimePayloadResource",
        "prefab/runtime/runtime-payload",
        "resources/runtime-payload.arpres.yaml",
        noDependencies,
        payload));
    package.entries.push_back(makeEntry(kAnimatorControllerId,
        "AnimatorControllerResource",
        "prefab/runtime/animator",
        "resources/animator.arpres.yaml",
        noDependencies,
        animatorResource));
    package.entries.push_back(makeEntry(kPrefabId,
        "PrefabResource",
        "prefab/runtime",
        "resources/prefab.arpres.yaml",
        prefabDependencies,
        prefab));

    return package;
}

inline resource::PrefabRuntimePayloadWriteDesc describe(const std::filesystem::path& root,
    const assets::AnimatedMeshAsset& asset,
    const animation::AnimatorControllerAsset& controller)
{
    resource::PrefabRuntimePayloadWriteDesc desc;
    desc.packageRoot = root;
    desc.blobPath = "blobs/prefab-runtime.arpa.bin";
    desc.displayName = "runtime";
    desc.asset = &asset;
    desc.controllerAsset = &controller;
    desc.defaultStateName = "idle";
    desc.stateBindings.push_back({"idle", "idle"});
    resource::PrefabRuntimeStateBinding run;
    run.stateName = "run";
    run.clipName = "run";
    run.speed = 1.5f;
    desc.stateBindings.push_back(run);
    return desc;
}

inline resource::InternalAssetPackageDesc write(const std::filesystem::path& root)
{
    const auto asset = makeRuntimeAsset();
    const auto controller = makeController();
    const auto payload = resource::writePrefabRuntimePayload(describe(root, asset, controller));
    assert(payload.success);
    auto package = makePackage(payload.resource);
    assert(resource::InternalAssetPackageWriter().write(package, root).success);
    return package;
}

} // namespace arp::tests::prefab_runtime_package_fixture
