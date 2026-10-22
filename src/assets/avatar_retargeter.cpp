#include "assets/avatar_retargeter.h"

#include "core/memory/cpu_allocator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>

namespace arp::assets {

namespace {

constexpr uint32_t kInvalidJoint = std::numeric_limits<uint32_t>::max();

bool startsWith(const std::pmr::string& value, const char* prefix)
{
    const std::pmr::string prefixText(prefix);
    return value.size() >= prefixText.size()
        && std::equal(prefixText.begin(), prefixText.end(), value.begin());
}

bool isPrimaryAvatarJoint(const std::pmr::string& jointName)
{
    if (jointName.find("Finger") != std::pmr::string::npos) {
        return false;
    }
    return jointName == "Bip001" || startsWith(jointName, "Bip001 ");
}

animation::Quat normalizeQuat(const animation::Quat value)
{
    const auto length =
        std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w);
    if (length <= 0.000001f) {
        return animation::Quat::identity();
    }
    return animation::Quat{
        value.x / length,
        value.y / length,
        value.z / length,
        value.w / length,
    };
}

animation::Quat conjugate(const animation::Quat value)
{
    return animation::Quat{-value.x, -value.y, -value.z, value.w};
}

animation::Quat multiply(const animation::Quat a, const animation::Quat b)
{
    return normalizeQuat(animation::Quat{
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    });
}

animation::Quat nlerp(const animation::Quat a, animation::Quat b, const float amount)
{
    const auto dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f) {
        b.x = -b.x;
        b.y = -b.y;
        b.z = -b.z;
        b.w = -b.w;
    }
    return normalizeQuat(animation::Quat{
        a.x + (b.x - a.x) * amount,
        a.y + (b.y - a.y) * amount,
        a.z + (b.z - a.z) * amount,
        a.w + (b.w - a.w) * amount,
    });
}

animation::JointTransform jointTransformFromLocal(const animation::LocalTransform& value)
{
    animation::JointTransform result;
    result.translation = value.translation;
    result.rotation = value.rotation;
    result.scale = value.scale;
    return result;
}

std::pmr::vector<animation::JointTransform> jointPoseFromBindPose(
    const std::pmr::vector<animation::LocalTransform>& bindPose)
{
    std::pmr::vector<animation::JointTransform> pose;
    pose.reserve(bindPose.size());
    for (const auto& transform : bindPose) {
        pose.push_back(jointTransformFromLocal(transform));
    }
    return pose;
}

std::pmr::vector<animation::Quat> buildGlobalRotations(
    const animation::Skeleton& skeleton, const std::pmr::vector<animation::JointTransform>& pose)
{
    std::pmr::vector<animation::Quat> rotations;
    if (!skeleton.valid() || pose.size() != skeleton.jointCount()) {
        return rotations;
    }

    rotations.resize(skeleton.jointCount());
    for (std::size_t index = 0; index < skeleton.jointCount(); ++index) {
        const auto local = normalizeQuat(pose[index].rotation);
        const auto parent = skeleton.joints[index].parentIndex;
        rotations[index] =
            parent < 0 ? local : multiply(rotations[static_cast<std::size_t>(parent)], local);
    }
    return rotations;
}

std::pmr::unordered_map<std::pmr::string, uint32_t> buildJointNameMap(
    const animation::Skeleton& skeleton)
{
    std::pmr::unordered_map<std::pmr::string, uint32_t> result;
    result.reserve(skeleton.jointCount());
    for (uint32_t index = 0; index < skeleton.jointCount(); ++index) {
        result[skeleton.joints[index].name] = index;
    }
    return result;
}

std::pmr::string channelJointName(
    const animation::AnimationChannel& channel, const animation::Skeleton& sourceSkeleton)
{
    if (!channel.jointName.empty()) {
        return channel.jointName;
    }
    if (channel.jointIndex < sourceSkeleton.jointCount()) {
        return sourceSkeleton.joints[channel.jointIndex].name;
    }
    return {};
}

std::pmr::string stripAssimpFbxHelperSuffix(const std::pmr::string& name)
{
    const auto suffix = name.find("_$AssimpFbx$");
    return suffix == std::pmr::string::npos ? name : name.substr(0, suffix);
}

std::pmr::string assimpFbxHelperRole(const std::pmr::string& name)
{
    const std::pmr::string marker("_$AssimpFbx$_");
    const auto suffix = name.find(marker);
    return suffix == std::pmr::string::npos ? std::pmr::string{}
                                            : name.substr(suffix + marker.size());
}

bool assimpFbxHelperRoleMatchesTargetPath(
    const std::pmr::string& role, const std::pmr::string& targetPath)
{
    if (role.empty()) {
        return true;
    }
    if (role == "Translation") {
        return targetPath == "translation";
    }
    if (role == "Rotation" || role == "PreRotation") {
        return targetPath == "rotation";
    }
    return false;
}

std::pmr::string unityChanSsuPrototypeHumanoidBoneAlias(const std::pmr::string& sourceName)
{
    const auto base = stripAssimpFbxHelperSuffix(sourceName);
    const std::pmr::string prefix("Character1_");
    if (!startsWith(base, prefix.c_str())) {
        return {};
    }
    const auto bone = base.substr(prefix.size());

    struct Alias {
        const char* source;
        const char* target;
    };

    constexpr Alias aliases[] = {
        {"Hips", "CH_Hips"},
        {"Spine", "CH_Spine"},
        {"Spine1", "CH_Chest"},
        {"Spine2", "CH_UpperChest"},
        {"Neck", "CH_Neck"},
        {"Head", "CH_Head"},
        {"LeftShoulder", "CH_Shou_L"},
        {"RightShoulder", "CH_Shou_R"},
        {"LeftArm", "CH_Uppe_L"},
        {"RightArm", "CH_Uppe_R"},
        {"LeftForeArm", "CH_For_L"},
        {"RightForeArm", "CH_For_R"},
        {"LeftHand", "CH_Hand_L"},
        {"RightHand", "CH_Hand_R"},
        {"LeftUpLeg", "CH_Thigh_L"},
        {"RightUpLeg", "CH_Thigh_R"},
        {"LeftLeg", "CH_Shin_L"},
        {"RightLeg", "CH_Shin_R"},
        {"LeftFoot", "CH_Foot_L"},
        {"RightFoot", "CH_Foot_R"},
        {"LeftToeBase", "CH_Toe_L"},
        {"RightToeBase", "CH_Toe_R"},
    };
    for (const auto& alias : aliases) {
        if (bone == alias.source) {
            return std::pmr::string(alias.target);
        }
    }
    return {};
}

std::pmr::string unityHumanoidNameFromSourceJoint(const std::pmr::string& sourceName)
{
    const auto base = stripAssimpFbxHelperSuffix(sourceName);
    const std::pmr::string prefix("Character1_");

    std::pmr::string bone = base;
    if (startsWith(base, prefix.c_str())) {
        bone = base.substr(prefix.size());
    }

    struct Mapping {
        const char* source;
        const char* human;
    };

    constexpr Mapping mappings[] = {
        {"Hips", "Hips"},
        {"Spine", "Spine"},
        {"Spine1", "Chest"},
        {"Spine2", "UpperChest"},
        {"Neck", "Neck"},
        {"Head", "Head"},
        {"LeftShoulder", "LeftShoulder"},
        {"RightShoulder", "RightShoulder"},
        {"LeftArm", "LeftUpperArm"},
        {"RightArm", "RightUpperArm"},
        {"LeftForeArm", "LeftLowerArm"},
        {"RightForeArm", "RightLowerArm"},
        {"LeftHand", "LeftHand"},
        {"RightHand", "RightHand"},
        {"LeftUpLeg", "LeftUpperLeg"},
        {"RightUpLeg", "RightUpperLeg"},
        {"LeftLeg", "LeftLowerLeg"},
        {"RightLeg", "RightLowerLeg"},
        {"LeftFoot", "LeftFoot"},
        {"RightFoot", "RightFoot"},
        {"LeftToeBase", "LeftToes"},
        {"RightToeBase", "RightToes"},
        {"Hips", "Hips"},
        {"Abdomen", "Spine"},
        {"Torso", "Chest"},
        {"Neck", "Neck"},
        {"Head", "Head"},
        {"Shoulder.L", "LeftShoulder"},
        {"UpperArm.L", "LeftUpperArm"},
        {"LowerArm.L", "LeftLowerArm"},
        {"Fist.L", "LeftHand"},
        {"Shoulder.R", "RightShoulder"},
        {"UpperArm.R", "RightUpperArm"},
        {"LowerArm.R", "RightLowerArm"},
        {"Fist.R", "RightHand"},
        {"UpperLeg.L", "LeftUpperLeg"},
        {"LowerLeg.L", "LeftLowerLeg"},
        {"Foot.L", "LeftFoot"},
        {"UpperLeg.R", "RightUpperLeg"},
        {"LowerLeg.R", "RightLowerLeg"},
        {"Foot.R", "RightFoot"},
        {"hips", "Hips"},
        {"spine", "Spine"},
        {"chest", "Chest"},
        {"head", "Head"},
        {"upperarm.l", "LeftUpperArm"},
        {"lowerarm.l", "LeftLowerArm"},
        {"hand.l", "LeftHand"},
        {"upperarm.r", "RightUpperArm"},
        {"lowerarm.r", "RightLowerArm"},
        {"hand.r", "RightHand"},
        {"upperleg.l", "LeftUpperLeg"},
        {"lowerleg.l", "LeftLowerLeg"},
        {"foot.l", "LeftFoot"},
        {"toes.l", "LeftToes"},
        {"upperleg.r", "RightUpperLeg"},
        {"lowerleg.r", "RightLowerLeg"},
        {"foot.r", "RightFoot"},
        {"toes.r", "RightToes"},
    };
    for (const auto& mapping : mappings) {
        if (bone == mapping.source) {
            return std::pmr::string(mapping.human);
        }
    }
    return {};
}

std::pmr::unordered_map<std::pmr::string, std::pmr::string> buildTargetBoneByHumanName(
    const HumanDescription& humanDescription)
{
    std::pmr::unordered_map<std::pmr::string, std::pmr::string> result;
    result.reserve(humanDescription.humanBones.size());
    for (const auto& bone : humanDescription.humanBones) {
        if (!bone.humanName.empty() && !bone.boneName.empty()) {
            result[bone.humanName] = bone.boneName;
        }
    }
    return result;
}

bool targetHumanoidSkeletonReferenceContainsBone(
    const HumanDescription& humanDescription, const std::pmr::string& boneName)
{
    for (const auto& Node : humanDescription.skeletonNodes) {
        if (Node.name == boneName) {
            return true;
        }
    }
    return false;
}

std::pmr::vector<uint32_t> buildHumanoidSourceToTargetJointMap(
    const animation::AnimationClip& sourceClip,
    const animation::Skeleton& targetSkeleton,
    const animation::Skeleton& sourceSkeleton,
    const AvatarAsset& avatar)
{
    std::pmr::vector<uint32_t> sourceToTarget(sourceSkeleton.jointCount(), kInvalidJoint);
    if (avatar.kind != AvatarKind::Humanoid || avatar.humanDescription.humanBones.empty()
        || avatar.humanDescription.skeletonNodes.empty()) {
        return sourceToTarget;
    }

    const auto targetByName = buildJointNameMap(targetSkeleton);
    const auto targetBoneByHumanName = buildTargetBoneByHumanName(avatar.humanDescription);
    for (const auto& channel : sourceClip.channels) {
        if (channel.jointIndex >= sourceToTarget.size()
            || !assimpFbxHelperRoleMatchesTargetPath(
                assimpFbxHelperRole(channelJointName(channel, sourceSkeleton)),
                channel.targetPath)) {
            continue;
        }
        std::pmr::string humanName(&core::defaultCpuMemoryResource());
        const auto sourceName = channelJointName(channel, sourceSkeleton);
        for (const auto& mapping : avatar.humanDescription.humanBones) {
            if (!mapping.sourceBoneName.empty() && mapping.sourceBoneName == sourceName) {
                humanName = mapping.humanName;
                break;
            }
        }
        if (humanName.empty()) {
            humanName = unityHumanoidNameFromSourceJoint(sourceName);
        }
        if (humanName.empty()) {
            continue;
        }
        const auto targetBone = targetBoneByHumanName.find(humanName);
        if (targetBone == targetBoneByHumanName.end()) {
            continue;
        }
        if (!targetHumanoidSkeletonReferenceContainsBone(
                avatar.humanDescription, targetBone->second)) {
            continue;
        }
        const auto targetJoint = targetByName.find(targetBone->second);
        if (targetJoint != targetByName.end()) {
            sourceToTarget[channel.jointIndex] = targetJoint->second;
        }
    }
    return sourceToTarget;
}

uint32_t findTargetJointForImportedChannel(const animation::AnimationChannel& channel,
    const std::pmr::unordered_map<std::pmr::string, uint32_t>& targetByName,
    const animation::Skeleton& sourceSkeleton)
{
    const auto name = channelJointName(channel, sourceSkeleton);
    if (!assimpFbxHelperRoleMatchesTargetPath(assimpFbxHelperRole(name), channel.targetPath)) {
        return kInvalidJoint;
    }
    const auto direct = targetByName.find(name);
    if (direct != targetByName.end()) {
        return direct->second;
    }

    const auto alias = unityChanSsuPrototypeHumanoidBoneAlias(name);
    if (alias.empty()) {
        return kInvalidJoint;
    }
    if (channel.targetPath == "scale") {
        return kInvalidJoint;
    }
    if (channel.targetPath == "translation" && alias != "CH_Hips") {
        return kInvalidJoint;
    }
    if (channel.targetPath == "translation") {
        const auto translationNode = alias + std::pmr::string("_$AssimpFbx$_Translation");
        const auto translated = targetByName.find(translationNode);
        if (translated != targetByName.end()) {
            return translated->second;
        }
    }
    const auto aliased = targetByName.find(alias);
    return aliased == targetByName.end() ? kInvalidJoint : aliased->second;
}

bool canDirectlyRemapClipToTargetSkeleton(const animation::AnimationClip& clip,
    const animation::Skeleton& targetSkeleton,
    const animation::Skeleton& sourceSkeleton)
{
    if (!targetSkeleton.valid() || clip.channels.empty()) {
        return false;
    }

    const auto targetByName = buildJointNameMap(targetSkeleton);
    for (const auto& channel : clip.channels) {
        if (findTargetJointForImportedChannel(channel, targetByName, sourceSkeleton)
            == kInvalidJoint) {
            return false;
        }
    }
    return true;
}

animation::AnimationClip directlyRemapClipToTargetSkeleton(animation::AnimationClip clip,
    const animation::Skeleton& targetSkeleton,
    const animation::Skeleton& sourceSkeleton)
{
    const auto targetByName = buildJointNameMap(targetSkeleton);
    std::pmr::vector<animation::AnimationChannel> remapped;
    remapped.reserve(clip.channels.size());
    for (auto& channel : clip.channels) {
        const auto targetIndex =
            findTargetJointForImportedChannel(channel, targetByName, sourceSkeleton);
        if (targetIndex == kInvalidJoint) {
            continue;
        }
        channel.jointIndex = targetIndex;
        channel.jointName = targetSkeleton.joints[targetIndex].name;
        remapped.push_back(std::move(channel));
    }
    clip.channels = std::move(remapped);
    return clip;
}

std::pmr::vector<uint32_t> buildTargetToSourceJointMap(
    const animation::Skeleton& targetSkeleton, const animation::Skeleton& sourceSkeleton)
{
    const auto sourceByName = buildJointNameMap(sourceSkeleton);
    std::pmr::vector<uint32_t> targetToSource(targetSkeleton.jointCount(), kInvalidJoint);
    for (uint32_t targetIndex = 0; targetIndex < targetSkeleton.jointCount(); ++targetIndex) {
        const auto& jointName = targetSkeleton.joints[targetIndex].name;
        if (!isPrimaryAvatarJoint(jointName)) {
            continue;
        }
        const auto sourceFound = sourceByName.find(jointName);
        if (sourceFound != sourceByName.end()) {
            targetToSource[targetIndex] = sourceFound->second;
        }
    }
    return targetToSource;
}

std::pmr::vector<uint32_t> buildExactTargetToSourceJointMap(
    const animation::Skeleton& targetSkeleton, const animation::Skeleton& sourceSkeleton)
{
    const auto sourceByName = buildJointNameMap(sourceSkeleton);
    std::pmr::vector<uint32_t> targetToSource(targetSkeleton.jointCount(), kInvalidJoint);
    for (uint32_t targetIndex = 0; targetIndex < targetSkeleton.jointCount(); ++targetIndex) {
        const auto sourceFound = sourceByName.find(targetSkeleton.joints[targetIndex].name);
        if (sourceFound != sourceByName.end()) {
            targetToSource[targetIndex] = sourceFound->second;
        }
    }
    return targetToSource;
}

float quatDot(animation::Quat a, animation::Quat b)
{
    a = normalizeQuat(a);
    b = normalizeQuat(b);
    return std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
}

bool nearlyEqual(const float left, const float right, const float epsilon)
{
    return std::fabs(left - right) <= epsilon;
}

bool nearlyEqual(const animation::Vec3 left, const animation::Vec3 right, const float epsilon)
{
    return nearlyEqual(left.x, right.x, epsilon) && nearlyEqual(left.y, right.y, epsilon)
        && nearlyEqual(left.z, right.z, epsilon);
}

bool localTransformsNearlyEqual(
    const animation::LocalTransform& left, const animation::LocalTransform& right)
{
    constexpr float translationEpsilon = 0.0001f;
    constexpr float scaleEpsilon = 0.0001f;
    constexpr float rotationDotEpsilon = 0.000001f;
    return nearlyEqual(left.translation, right.translation, translationEpsilon)
        && nearlyEqual(left.scale, right.scale, scaleEpsilon)
        && quatDot(left.rotation, right.rotation) >= 1.0f - rotationDotEpsilon;
}

bool bindPosesCompatibleForDirectRemap(const animation::Skeleton& targetSkeleton,
    const std::pmr::vector<animation::LocalTransform>& targetBindPose,
    const animation::Skeleton& sourceSkeleton,
    const std::pmr::vector<animation::LocalTransform>& sourceBindPose)
{
    if (!targetSkeleton.valid() || !sourceSkeleton.valid()
        || targetBindPose.size() != targetSkeleton.jointCount()
        || sourceBindPose.size() != sourceSkeleton.jointCount()) {
        return false;
    }

    const auto targetToSource = buildExactTargetToSourceJointMap(targetSkeleton, sourceSkeleton);
    for (uint32_t targetIndex = 0; targetIndex < targetToSource.size(); ++targetIndex) {
        const auto sourceIndex = targetToSource[targetIndex];
        if (sourceIndex == kInvalidJoint) {
            continue;
        }
        if (!localTransformsNearlyEqual(targetBindPose[targetIndex], sourceBindPose[sourceIndex])) {
            return false;
        }
    }
    return true;
}

bool transformChannelPath(const std::pmr::string& targetPath)
{
    return targetPath == "translation" || targetPath == "rotation" || targetPath == "scale";
}

animation::Vec3 rotateVector(const animation::Quat rawRotation, const animation::Vec3 value)
{
    const auto q = normalizeQuat(rawRotation);
    const auto uv = animation::Vec3{
        q.y * value.z - q.z * value.y,
        q.z * value.x - q.x * value.z,
        q.x * value.y - q.y * value.x,
    };
    const auto uuv = animation::Vec3{
        q.y * uv.z - q.z * uv.y,
        q.z * uv.x - q.x * uv.z,
        q.x * uv.y - q.y * uv.x,
    };
    return animation::Vec3{
        value.x + (uv.x * q.w + uuv.x) * 2.0f,
        value.y + (uv.y * q.w + uuv.y) * 2.0f,
        value.z + (uv.z * q.w + uuv.z) * 2.0f,
    };
}

std::pmr::vector<float> collectSampleTimes(
    const animation::AnimationClip& clip, const std::pmr::vector<uint32_t>& sourceToTarget)
{
    std::pmr::vector<float> times;
    times.push_back(0.0f);
    times.push_back(clip.durationSeconds);
    for (const auto& channel : clip.channels) {
        if (channel.jointIndex >= sourceToTarget.size()
            || sourceToTarget[channel.jointIndex] == kInvalidJoint) {
            continue;
        }
        for (const auto& keyframe : channel.keyframes) {
            times.push_back(keyframe.timeSeconds);
        }
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(),
                    times.end(),
                    [](const float left, const float right) {
                        return std::fabs(left - right) <= 0.0001f;
                    }),
        times.end());
    return times;
}

std::pmr::vector<uint32_t> invertJointMap(
    const std::pmr::vector<uint32_t>& targetToSource, const std::size_t sourceCount)
{
    std::pmr::vector<uint32_t> sourceToTarget(sourceCount, kInvalidJoint);
    for (uint32_t targetIndex = 0; targetIndex < targetToSource.size(); ++targetIndex) {
        const auto sourceIndex = targetToSource[targetIndex];
        if (sourceIndex < sourceToTarget.size()) {
            sourceToTarget[sourceIndex] = targetIndex;
        }
    }
    return sourceToTarget;
}

animation::AnimationChannel makeChannel(
    const uint32_t jointIndex, const std::pmr::string& jointName, const char* targetPath)
{
    animation::AnimationChannel channel;
    channel.jointIndex = jointIndex;
    channel.jointName = jointName;
    channel.targetPath = targetPath;
    return channel;
}

animation::Vec3 retargetLocalTranslationDelta(animation::Vec3 sourceDelta,
    const animation::Skeleton& targetSkeleton,
    uint32_t targetIndex,
    const animation::Skeleton& sourceSkeleton,
    uint32_t sourceIndex,
    const std::pmr::vector<animation::Quat>& targetReferenceGlobal,
    const std::pmr::vector<animation::Quat>& sourceReferenceGlobal);

animation::AnimationClip retargetAliasedClipToTargetLocalSpace(
    const animation::AnimationClip& sourceClip,
    const animation::Skeleton& targetSkeleton,
    const std::pmr::vector<animation::LocalTransform>& targetBindPose,
    const animation::Skeleton& sourceSkeleton,
    const std::pmr::vector<animation::LocalTransform>& sourceBindPose)
{
    if (!targetSkeleton.valid() || !sourceSkeleton.valid()
        || targetBindPose.size() != targetSkeleton.jointCount()
        || sourceBindPose.size() != sourceSkeleton.jointCount()) {
        return {};
    }

    const auto targetByName = buildJointNameMap(targetSkeleton);
    auto sourceReferencePose = jointPoseFromBindPose(sourceBindPose);
    animation::sampleClip(sourceClip, 0.0f, sourceReferencePose);
    const auto sourceReferenceGlobal = buildGlobalRotations(sourceSkeleton, sourceReferencePose);
    const auto targetReferencePose = jointPoseFromBindPose(targetBindPose);
    const auto targetReferenceGlobal = buildGlobalRotations(targetSkeleton, targetReferencePose);
    if (sourceReferenceGlobal.size() != sourceSkeleton.jointCount()
        || targetReferenceGlobal.size() != targetSkeleton.jointCount()) {
        return {};
    }

    animation::AnimationClip result;
    result.name = sourceClip.name;
    result.durationSeconds = sourceClip.durationSeconds;
    result.events = sourceClip.events;
    result.channels.reserve(sourceClip.channels.size());

    for (const auto& channel : sourceClip.channels) {
        if (!transformChannelPath(channel.targetPath) || channel.targetPath == "scale"
            || channel.keyframes.empty() || channel.jointIndex >= sourceReferencePose.size()) {
            continue;
        }

        const auto targetIndex =
            findTargetJointForImportedChannel(channel, targetByName, sourceSkeleton);
        if (targetIndex == kInvalidJoint || targetIndex >= targetBindPose.size()) {
            continue;
        }

        auto retargeted = makeChannel(
            targetIndex, targetSkeleton.joints[targetIndex].name, channel.targetPath.c_str());
        retargeted.keyframes.reserve(channel.keyframes.size());
        const auto& sourceReference = sourceReferencePose[channel.jointIndex];
        const auto& targetReference = targetBindPose[targetIndex];
        for (const auto& keyframe : channel.keyframes) {
            animation::JointKeyframe outKey;
            outKey.timeSeconds = keyframe.timeSeconds;
            if (channel.targetPath == "rotation") {
                const auto sourceDelta =
                    multiply(conjugate(normalizeQuat(sourceReference.rotation)),
                        normalizeQuat(keyframe.transform.rotation));
                outKey.transform.rotation =
                    multiply(normalizeQuat(targetReference.rotation), sourceDelta);
            } else {
                const animation::Vec3 sourceDelta{
                    keyframe.transform.translation.x - sourceReference.translation.x,
                    keyframe.transform.translation.y - sourceReference.translation.y,
                    keyframe.transform.translation.z - sourceReference.translation.z,
                };
                const auto localDelta = retargetLocalTranslationDelta(sourceDelta,
                    targetSkeleton,
                    targetIndex,
                    sourceSkeleton,
                    channel.jointIndex,
                    targetReferenceGlobal,
                    sourceReferenceGlobal);
                outKey.transform.translation = {
                    targetReference.translation.x + localDelta.x,
                    targetReference.translation.y + localDelta.y,
                    targetReference.translation.z + localDelta.z,
                };
            }
            retargeted.keyframes.push_back(outKey);
        }
        if (!retargeted.keyframes.empty()) {
            result.channels.push_back(std::move(retargeted));
        }
    }
    return result;
}

animation::Vec3 retargetLocalTranslationDelta(const animation::Vec3 sourceDelta,
    const animation::Skeleton& targetSkeleton,
    const uint32_t targetIndex,
    const animation::Skeleton& sourceSkeleton,
    const uint32_t sourceIndex,
    const std::pmr::vector<animation::Quat>& targetReferenceGlobal,
    const std::pmr::vector<animation::Quat>& sourceReferenceGlobal)
{
    const auto sourceParent = sourceSkeleton.joints[sourceIndex].parentIndex;
    const auto targetParent = targetSkeleton.joints[targetIndex].parentIndex;
    const auto sourceParentRotation = sourceParent < 0
        ? animation::Quat::identity()
        : sourceReferenceGlobal[static_cast<std::size_t>(sourceParent)];
    const auto targetParentRotation = targetParent < 0
        ? animation::Quat::identity()
        : targetReferenceGlobal[static_cast<std::size_t>(targetParent)];
    return rotateVector(
        conjugate(targetParentRotation), rotateVector(sourceParentRotation, sourceDelta));
}

animation::AnimationClip retargetHumanoidAvatarClip(const animation::AnimationClip& sourceClip,
    const animation::Skeleton& targetSkeleton,
    const std::pmr::vector<animation::LocalTransform>& targetBindPose,
    const animation::Skeleton& sourceSkeleton,
    const std::pmr::vector<animation::LocalTransform>& sourceBindPose,
    const AvatarAsset& avatar)
{
    if (!targetSkeleton.valid() || !sourceSkeleton.valid()
        || targetBindPose.size() != targetSkeleton.jointCount()
        || sourceBindPose.size() != sourceSkeleton.jointCount()
        || avatar.kind != AvatarKind::Humanoid || avatar.humanDescription.humanBones.empty()
        || avatar.humanDescription.skeletonNodes.empty()) {
        return {};
    }

    const auto sourceToTarget =
        buildHumanoidSourceToTargetJointMap(sourceClip, targetSkeleton, sourceSkeleton, avatar);
    const auto sampleTimes = collectSampleTimes(sourceClip, sourceToTarget);
    if (sampleTimes.empty()) {
        return {};
    }

    const auto sourceReferencePose = jointPoseFromBindPose(sourceBindPose);
    const auto sourceReferenceGlobal = buildGlobalRotations(sourceSkeleton, sourceReferencePose);
    const auto targetReferencePose = jointPoseFromBindPose(targetBindPose);
    const auto targetReferenceGlobal = buildGlobalRotations(targetSkeleton, targetReferencePose);
    if (sourceReferenceGlobal.size() != sourceSkeleton.jointCount()
        || targetReferenceGlobal.size() != targetSkeleton.jointCount()) {
        return {};
    }

    std::pmr::vector<bool> animatedTargets(targetSkeleton.jointCount(), false);
    std::pmr::vector<bool> targetIsHips(targetSkeleton.jointCount(), false);
    std::pmr::vector<uint32_t> targetToSource(targetSkeleton.jointCount(), kInvalidJoint);
    const auto targetByName = buildJointNameMap(targetSkeleton);
    const auto targetBoneByHumanName = buildTargetBoneByHumanName(avatar.humanDescription);
    const auto hipsBone = targetBoneByHumanName.find("Hips");
    if (hipsBone != targetBoneByHumanName.end()) {
        const auto hipsJoint = targetByName.find(hipsBone->second);
        if (hipsJoint != targetByName.end()) {
            targetIsHips[hipsJoint->second] = true;
        }
    }
    for (uint32_t sourceIndex = 0; sourceIndex < sourceToTarget.size(); ++sourceIndex) {
        const auto targetIndex = sourceToTarget[sourceIndex];
        if (targetIndex != kInvalidJoint && targetIndex < targetToSource.size()) {
            animatedTargets[targetIndex] = true;
            targetToSource[targetIndex] = sourceIndex;
        }
    }
    if (std::find(animatedTargets.begin(), animatedTargets.end(), true) == animatedTargets.end()) {
        return {};
    }

    animation::AnimationClip result;
    result.name = sourceClip.name;
    result.durationSeconds = sourceClip.durationSeconds;
    result.events = sourceClip.events;

    std::pmr::vector<animation::AnimationChannel> rotationChannels(targetSkeleton.jointCount());
    std::pmr::vector<animation::AnimationChannel> translationChannels(targetSkeleton.jointCount());
    for (uint32_t targetIndex = 0; targetIndex < targetSkeleton.jointCount(); ++targetIndex) {
        const auto& jointName = targetSkeleton.joints[targetIndex].name;
        rotationChannels[targetIndex] = makeChannel(targetIndex, jointName, "rotation");
        translationChannels[targetIndex] = makeChannel(targetIndex, jointName, "translation");
    }

    for (const auto time : sampleTimes) {
        auto sourcePose = jointPoseFromBindPose(sourceBindPose);
        if (!animation::sampleClip(sourceClip, time, sourcePose)) {
            continue;
        }
        const auto sourceGlobal = buildGlobalRotations(sourceSkeleton, sourcePose);
        if (sourceGlobal.size() != sourceSkeleton.jointCount()) {
            continue;
        }

        std::pmr::vector<animation::JointTransform> targetLocal = targetReferencePose;
        std::pmr::vector<animation::Quat> targetGlobal(
            targetSkeleton.jointCount(), animation::Quat::identity());
        for (uint32_t targetIndex = 0; targetIndex < targetSkeleton.jointCount(); ++targetIndex) {
            const auto sourceIndex = targetToSource[targetIndex];
            const auto parentIndex = targetSkeleton.joints[targetIndex].parentIndex;
            const auto parentGlobal = parentIndex < 0
                ? animation::Quat::identity()
                : targetGlobal[static_cast<std::size_t>(parentIndex)];

            animation::Quat desiredGlobal =
                multiply(parentGlobal, normalizeQuat(targetBindPose[targetIndex].rotation));
            if (sourceIndex != kInvalidJoint) {
                const auto sourceGlobalDelta = multiply(normalizeQuat(sourceGlobal[sourceIndex]),
                    conjugate(normalizeQuat(sourceReferenceGlobal[sourceIndex])));
                desiredGlobal =
                    multiply(sourceGlobalDelta, normalizeQuat(targetReferenceGlobal[targetIndex]));
            }
            targetGlobal[targetIndex] = desiredGlobal;
            targetLocal[targetIndex].rotation = multiply(conjugate(parentGlobal), desiredGlobal);
        }

        for (uint32_t targetIndex = 0; targetIndex < targetSkeleton.jointCount(); ++targetIndex) {
            if (!animatedTargets[targetIndex]) {
                continue;
            }
            animation::JointKeyframe rotationKey;
            rotationKey.timeSeconds = time;
            rotationKey.transform.rotation = normalizeQuat(targetLocal[targetIndex].rotation);
            rotationChannels[targetIndex].keyframes.push_back(rotationKey);

            const auto sourceIndex = targetToSource[targetIndex];
            if (sourceIndex != kInvalidJoint && targetIsHips[targetIndex]) {
                const auto sourceTranslationDelta = animation::Vec3{
                    sourcePose[sourceIndex].translation.x
                        - sourceReferencePose[sourceIndex].translation.x,
                    sourcePose[sourceIndex].translation.y
                        - sourceReferencePose[sourceIndex].translation.y,
                    sourcePose[sourceIndex].translation.z
                        - sourceReferencePose[sourceIndex].translation.z,
                };
                const auto localDelta = retargetLocalTranslationDelta(sourceTranslationDelta,
                    targetSkeleton,
                    targetIndex,
                    sourceSkeleton,
                    sourceIndex,
                    targetReferenceGlobal,
                    sourceReferenceGlobal);
                animation::JointKeyframe translationKey;
                translationKey.timeSeconds = time;
                translationKey.transform.translation = {
                    targetBindPose[targetIndex].translation.x + localDelta.x,
                    targetBindPose[targetIndex].translation.y + localDelta.y,
                    targetBindPose[targetIndex].translation.z + localDelta.z,
                };
                translationChannels[targetIndex].keyframes.push_back(translationKey);
            }
        }
    }

    for (uint32_t targetIndex = 0; targetIndex < targetSkeleton.jointCount(); ++targetIndex) {
        if (!translationChannels[targetIndex].keyframes.empty()) {
            result.channels.push_back(std::move(translationChannels[targetIndex]));
        }
        if (!rotationChannels[targetIndex].keyframes.empty()) {
            result.channels.push_back(std::move(rotationChannels[targetIndex]));
        }
    }
    return result;
}

animation::AnimationClip retargetMatchingNamedClipToTargetLocalSpace(
    const animation::AnimationClip& sourceClip,
    const animation::Skeleton& targetSkeleton,
    const std::pmr::vector<animation::LocalTransform>& targetBindPose,
    const animation::Skeleton& sourceSkeleton,
    const std::pmr::vector<animation::LocalTransform>& sourceBindPose)
{
    if (!targetSkeleton.valid() || !sourceSkeleton.valid()
        || targetBindPose.size() != targetSkeleton.jointCount()
        || sourceBindPose.size() != sourceSkeleton.jointCount()) {
        return directlyRemapClipToTargetSkeleton(sourceClip, targetSkeleton, sourceSkeleton);
    }

    const auto targetToSource = buildExactTargetToSourceJointMap(targetSkeleton, sourceSkeleton);
    const auto sourceToTarget = invertJointMap(targetToSource, sourceSkeleton.jointCount());
    const auto sampleTimes = collectSampleTimes(sourceClip, sourceToTarget);
    auto sourceReferencePose = jointPoseFromBindPose(sourceBindPose);
    animation::sampleClip(sourceClip, 0.0f, sourceReferencePose);
    const auto sourceReferenceGlobal = buildGlobalRotations(sourceSkeleton, sourceReferencePose);
    const auto targetReferencePose = jointPoseFromBindPose(targetBindPose);
    const auto targetReferenceGlobal = buildGlobalRotations(targetSkeleton, targetReferencePose);
    if (sourceReferenceGlobal.size() != sourceSkeleton.jointCount()
        || targetReferenceGlobal.size() != targetSkeleton.jointCount()) {
        return directlyRemapClipToTargetSkeleton(sourceClip, targetSkeleton, sourceSkeleton);
    }

    std::pmr::vector<bool> animatedTargets(targetSkeleton.jointCount(), false);
    for (const auto& channel : sourceClip.channels) {
        if (channel.jointIndex < sourceToTarget.size()
            && sourceToTarget[channel.jointIndex] != kInvalidJoint) {
            animatedTargets[sourceToTarget[channel.jointIndex]] = true;
        }
    }

    animation::AnimationClip result;
    result.name = sourceClip.name;
    result.durationSeconds = sourceClip.durationSeconds;
    result.events = sourceClip.events;

    std::pmr::vector<animation::AnimationChannel> rotationChannels(targetSkeleton.jointCount());
    std::pmr::vector<animation::AnimationChannel> translationChannels(targetSkeleton.jointCount());
    std::pmr::vector<animation::AnimationChannel> scaleChannels(targetSkeleton.jointCount());
    for (uint32_t targetIndex = 0; targetIndex < targetSkeleton.jointCount(); ++targetIndex) {
        const auto& jointName = targetSkeleton.joints[targetIndex].name;
        rotationChannels[targetIndex] = makeChannel(targetIndex, jointName, "rotation");
        translationChannels[targetIndex] = makeChannel(targetIndex, jointName, "translation");
        scaleChannels[targetIndex] = makeChannel(targetIndex, jointName, "scale");
    }

    for (const auto time : sampleTimes) {
        auto sourcePose = jointPoseFromBindPose(sourceBindPose);
        if (!animation::sampleClip(sourceClip, time, sourcePose)) {
            continue;
        }
        std::pmr::vector<animation::JointTransform> targetLocal =
            jointPoseFromBindPose(targetBindPose);
        for (uint32_t targetIndex = 0; targetIndex < targetSkeleton.jointCount(); ++targetIndex) {
            const auto sourceIndex = targetToSource[targetIndex];
            if (sourceIndex != kInvalidJoint) {
                const auto& sourceRest = sourceReferencePose[sourceIndex];
                const auto& sourceNow = sourcePose[sourceIndex];
                const auto& targetRest = targetBindPose[targetIndex];
                const auto sourceRotationDelta =
                    multiply(conjugate(normalizeQuat(sourceRest.rotation)),
                        normalizeQuat(sourceNow.rotation));
                targetLocal[targetIndex].rotation =
                    multiply(normalizeQuat(targetRest.rotation), sourceRotationDelta);

                const auto sourceTranslationDelta = animation::Vec3{
                    sourceNow.translation.x - sourceRest.translation.x,
                    sourceNow.translation.y - sourceRest.translation.y,
                    sourceNow.translation.z - sourceRest.translation.z,
                };
                targetLocal[targetIndex].translation = animation::Vec3{
                    targetRest.translation.x + sourceTranslationDelta.x,
                    targetRest.translation.y + sourceTranslationDelta.y,
                    targetRest.translation.z + sourceTranslationDelta.z,
                };

                const auto safeDivide = [](const float numerator, const float denominator) {
                    return std::fabs(denominator) <= 0.000001f ? 1.0f : numerator / denominator;
                };
                targetLocal[targetIndex].scale = animation::Vec3{
                    targetRest.scale.x * safeDivide(sourceNow.scale.x, sourceRest.scale.x),
                    targetRest.scale.y * safeDivide(sourceNow.scale.y, sourceRest.scale.y),
                    targetRest.scale.z * safeDivide(sourceNow.scale.z, sourceRest.scale.z),
                };
            }
        }

        for (uint32_t targetIndex = 0; targetIndex < targetSkeleton.jointCount(); ++targetIndex) {
            if (!animatedTargets[targetIndex]) {
                continue;
            }
            animation::JointKeyframe translationKey;
            translationKey.timeSeconds = time;
            translationKey.transform.translation = targetLocal[targetIndex].translation;
            translationChannels[targetIndex].keyframes.push_back(translationKey);

            animation::JointKeyframe rotationKey;
            rotationKey.timeSeconds = time;
            rotationKey.transform.rotation = normalizeQuat(targetLocal[targetIndex].rotation);
            rotationChannels[targetIndex].keyframes.push_back(rotationKey);

            animation::JointKeyframe scaleKey;
            scaleKey.timeSeconds = time;
            scaleKey.transform.scale = targetLocal[targetIndex].scale;
            scaleChannels[targetIndex].keyframes.push_back(scaleKey);
        }
    }

    for (uint32_t targetIndex = 0; targetIndex < targetSkeleton.jointCount(); ++targetIndex) {
        if (!translationChannels[targetIndex].keyframes.empty()) {
            result.channels.push_back(std::move(translationChannels[targetIndex]));
        }
        if (!rotationChannels[targetIndex].keyframes.empty()) {
            result.channels.push_back(std::move(rotationChannels[targetIndex]));
        }
        if (!scaleChannels[targetIndex].keyframes.empty()) {
            result.channels.push_back(std::move(scaleChannels[targetIndex]));
        }
    }
    return result;
}

animation::AnimationClip retargetGenericAvatarClip(const animation::AnimationClip& sourceClip,
    const animation::Skeleton& targetSkeleton,
    const std::pmr::vector<animation::LocalTransform>& targetBindPose,
    const animation::Skeleton& sourceSkeleton,
    const std::pmr::vector<animation::LocalTransform>& sourceBindPose)
{
    if (!targetSkeleton.valid() || !sourceSkeleton.valid()
        || targetBindPose.size() != targetSkeleton.jointCount()
        || sourceBindPose.size() != sourceSkeleton.jointCount()) {
        return sourceClip;
    }

    animation::AnimationClip result;
    result.name = sourceClip.name;
    result.durationSeconds = sourceClip.durationSeconds;
    result.events = sourceClip.events;

    const auto targetByName = buildJointNameMap(targetSkeleton);
    result.channels.reserve(sourceClip.channels.size());
    for (const auto& channel : sourceClip.channels) {
        const auto jointName = channelJointName(channel, sourceSkeleton);
        if (channel.keyframes.empty()) {
            continue;
        }
        const auto targetFound = targetByName.find(jointName);
        if (targetFound == targetByName.end()) {
            continue;
        }
        const auto targetIndex = targetFound->second;
        const auto primaryAvatarJoint = isPrimaryAvatarJoint(jointName);
        const auto transformPath = channel.targetPath == "translation"
            || channel.targetPath == "rotation" || channel.targetPath == "scale";
        if (!transformPath || (primaryAvatarJoint && channel.targetPath != "rotation")) {
            continue;
        }

        auto remapped = makeChannel(
            targetIndex, targetSkeleton.joints[targetIndex].name, channel.targetPath.c_str());
        remapped.keyframes.reserve(channel.keyframes.size());
        for (const auto& keyframe : channel.keyframes) {
            animation::JointKeyframe outKey;
            outKey.timeSeconds = keyframe.timeSeconds;
            outKey.transform = keyframe.transform;
            if (channel.targetPath == "rotation") {
                outKey.transform.rotation = normalizeQuat(keyframe.transform.rotation);
            }
            remapped.keyframes.push_back(outKey);
        }
        result.channels.push_back(std::move(remapped));
    }
    return result;
}

animation::AnimationClip retargetLegacyClip(animation::AnimationClip clip,
    const animation::Skeleton& targetSkeleton,
    const std::pmr::vector<animation::LocalTransform>& targetBindPose)
{
    const auto targetJointByName = buildJointNameMap(targetSkeleton);

    std::pmr::vector<animation::AnimationChannel> remapped;
    remapped.reserve(clip.channels.size());
    for (auto& channel : clip.channels) {
        auto jointName = channel.jointName;
        if (jointName.empty() && channel.jointIndex < targetSkeleton.joints.size()) {
            jointName = targetSkeleton.joints[channel.jointIndex].name;
        }
        const auto targetFound = targetJointByName.find(jointName);
        if (targetFound == targetJointByName.end() || !isPrimaryAvatarJoint(jointName)) {
            continue;
        }
        channel.jointIndex = targetFound->second;
        channel.jointName = jointName;
        if (channel.targetPath == "rotation") {
            for (auto& keyframe : channel.keyframes) {
                keyframe.transform.rotation = normalizeQuat(keyframe.transform.rotation);
            }
        }
        remapped.push_back(std::move(channel));
    }
    clip.channels = std::move(remapped);
    return clip;
}

} // namespace

animation::AnimationClip retargetClipToSkeleton(animation::AnimationClip clip,
    const animation::Skeleton& targetSkeleton,
    const std::pmr::vector<animation::LocalTransform>& targetBindPose,
    const animation::Skeleton& sourceSkeleton,
    const std::pmr::vector<animation::LocalTransform>& sourceBindPose,
    const AvatarAsset& avatar)
{
    if (avatar.kind == AvatarKind::Humanoid && sourceSkeleton.valid()
        && sourceBindPose.size() == sourceSkeleton.jointCount()) {
        auto retargeted = retargetHumanoidAvatarClip(
            clip, targetSkeleton, targetBindPose, sourceSkeleton, sourceBindPose, avatar);
        if (!retargeted.channels.empty()) {
            return retargeted;
        }
        return {};
    }

    if (avatar.kind == AvatarKind::Generic && sourceSkeleton.valid()
        && sourceBindPose.size() == sourceSkeleton.jointCount()) {
        auto retargeted = retargetGenericAvatarClip(
            clip, targetSkeleton, targetBindPose, sourceSkeleton, sourceBindPose);
        if (!retargeted.channels.empty()) {
            return retargeted;
        }
    }
    auto aliasedRetarget = retargetAliasedClipToTargetLocalSpace(
        clip, targetSkeleton, targetBindPose, sourceSkeleton, sourceBindPose);
    if (!aliasedRetarget.channels.empty()) {
        return aliasedRetarget;
    }
    auto namedRemap = directlyRemapClipToTargetSkeleton(clip, targetSkeleton, sourceSkeleton);
    if (!namedRemap.channels.empty()) {
        return namedRemap;
    }
    return retargetLegacyClip(std::move(clip), targetSkeleton, targetBindPose);
}

} // namespace arp::assets
