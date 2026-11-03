#pragma once

#include "animation/skeleton.h"
#include "assets/mesh/glb_animation_track_table.h"
#include "core/math/matrix_validation.h"
#include "core/text/number_format.h"

#include <string_view>

namespace arp::assets::detail {

// Right-handed source decode into the existing animation ABI. Canonical nodes
// retain all selected ancestors. Each (skin, local joint) adds an identity-local
// child with that skin's IBM, so distinct skins never overwrite one another.
// Geometry later maps JOINTS_0 through paletteJoint(), then converts the complete
// asset once at the existing RH -> LH boundary.
class GlbSkeletalSource final : private GlbGeometrySchema {
public:
    static constexpr auto absent = GlbAnimatedNodeGraph::absent;

    struct Limits final {
        std::uint32_t maxJoints = 0, maxClips = 0, maxChannels = 0, maxKeyframes = 0;
        std::size_t maxNameBytes = 0, maxDecodedBytes = 0;
        // Explicit absolute residual for matrix-authored node -> runtime TRS.
        float matrixElementError = -1;
    };

    struct Palette final {
        std::uint32_t first = 0, count = 0;
    };

    const animation::Skeleton& skeleton() const noexcept { return skeleton_; }

    const core::pmr::Vector<animation::LocalTransform>& bindPose() const noexcept
    {
        return bindPose_;
    }

    const core::pmr::Vector<animation::AnimationClip>& clips() const noexcept { return clips_; }

    std::size_t decodedBytes() const noexcept { return decodedBytes_; }

    void moveRuntimeData(animation::Skeleton& skeleton,
        core::pmr::Vector<animation::LocalTransform>& bindPose,
        core::pmr::Vector<animation::AnimationClip>& clips) noexcept
    {
        skeleton.joints.swap(skeleton_.joints);
        bindPose.swap(bindPose_);
        clips.swap(clips_);
    }

    std::uint32_t nodeJoint(std::size_t node) const noexcept
    {
        return node < nodeJoints_.size() ? nodeJoints_[node] : absent;
    }

    std::uint32_t paletteJoint(std::size_t skin, std::size_t slot) const noexcept
    {
        if (skin >= palettes_.size() || slot >= palettes_[skin].count)
            return absent;
        return paletteJoints_[palettes_[skin].first + slot];
    }

    // Counts/decoded bytes describe the output's logical storage, including
    // strings and index maps. The importer owns the core allocation quota for
    // JSON/BIN + old/candidate + allocator capacity/temporary overhead; this
    // stage does not replace that quota with a nested, uncharged allocator.
    bool prepare(const Json& root,
        const GlbAnimatedNodeGraph& graph,
        const GlbAccessorTable& accessors,
        const GlbAnimationTrackTable& tracks,
        Limits limits,
        const char*& diagnostic)
    {
        diagnostic = nullptr;
        try {
            require(limits.maxJoints && limits.maxJoints <= std::uint32_t(UINT16_MAX) + 1
                    && limits.maxClips && limits.maxClips != UINT32_MAX && limits.maxChannels
                    && limits.maxChannels != UINT32_MAX && limits.maxKeyframes
                    && limits.maxKeyframes != UINT32_MAX && limits.maxNameBytes
                    && limits.maxNameBytes != SIZE_MAX && limits.maxDecodedBytes
                    && limits.maxDecodedBytes != SIZE_MAX
                    && std::isfinite(limits.matrixElementError) && limits.matrixElementError >= 0,
                "ASSET-GLB-SKELETON-LIMITS");
            const auto quota = core::currentCpuMemoryQuota();
            require(quota && quota->limitBytes() && quota->limitBytes() != UINT64_MAX,
                "ASSET-GLB-SKELETON-QUOTA-REQUIRED");
            const auto& nodes = array(root, "nodes");
            const auto& animations = array(root, "animations");
            require(nodes.size() == graph.nodes().size()
                    && animations.size() == tracks.clips().size()
                    && graph.order().size() == nodes.size(),
                "ASSET-GLB-SKELETON-INPUT");
            require(tracks.clips().size() <= limits.maxClips
                    && tracks.tracks().size() <= limits.maxChannels,
                "ASSET-GLB-SKELETON-BUDGET");
            GlbSkeletalSource candidate;
            auto bytes = limits.maxDecodedBytes;
            auto names = limits.maxNameBytes;
            charge(bytes, nodes.size(), sizeof(std::uint32_t));
            charge(bytes, graph.skins().size(), sizeof(Palette));
            charge(bytes, graph.joints().size(), sizeof(std::uint32_t));
            candidate.nodeJoints_.assign(nodes.size(), absent);
            candidate.palettes_.resize(graph.skins().size());
            candidate.paletteJoints_.assign(graph.joints().size(), absent);
            core::pmr::Vector<bool> usedSkins(
                graph.skins().size(), false, &core::defaultCpuMemoryResource());
            auto jointBudget = std::size_t(limits.maxJoints);
            for (const auto& node : graph.nodes()) {
                if (!node.selected)
                    continue;
                require(jointBudget, "ASSET-GLB-SKELETON-JOINT-BUDGET");
                --jointBudget;
                if (node.skin != absent)
                    usedSkins[node.skin] = true;
            }
            for (std::size_t i = 0; i < usedSkins.size(); ++i)
                if (usedSkins[i]) {
                    require(graph.skins()[i].jointCount <= jointBudget,
                        "ASSET-GLB-SKELETON-JOINT-BUDGET");
                    jointBudget -= graph.skins()[i].jointCount;
                }
            const auto joints = limits.maxJoints - jointBudget;
            require(joints, "ASSET-GLB-SKELETON-JOINT-BUDGET");
            charge(bytes, joints, sizeof(animation::Joint) + sizeof(animation::LocalTransform));
            candidate.skeleton_.joints.reserve(joints);
            candidate.bindPose_.reserve(joints);
            for (const auto index : graph.order()) {
                if (!graph.nodes()[index].selected)
                    continue;
                const auto& node = nodes[index];
                animation::Joint joint;
                assignName(joint.name, node.find("name"), "@gltf/node/", index, names, bytes);
                const auto parent = graph.nodes()[index].parent;
                if (parent != absent) {
                    require(candidate.nodeJoint(parent) != absent, "ASSET-GLB-SKELETON-PARENT");
                    joint.parentIndex = static_cast<int>(candidate.nodeJoint(parent));
                }
                candidate.nodeJoints_[index] =
                    static_cast<std::uint32_t>(candidate.skeleton_.joints.size());
                candidate.skeleton_.joints.push_back(std::move(joint));
                candidate.bindPose_.push_back(decodeNode(node, limits.matrixElementError));
            }
            for (std::uint32_t skinIndex = 0; skinIndex < usedSkins.size(); ++skinIndex) {
                if (!usedSkins[skinIndex])
                    continue;
                const auto& skin = graph.skins()[skinIndex];
                candidate.palettes_[skinIndex] = {skin.firstJoint, skin.jointCount};
                for (std::uint32_t slot = 0; slot < skin.jointCount; ++slot) {
                    const auto node = graph.joints()[skin.firstJoint + slot];
                    require(candidate.nodeJoint(node) != absent, "ASSET-GLB-SKELETON-PALETTE-NODE");
                    animation::Joint leaf;
                    leaf.parentIndex = static_cast<int>(candidate.nodeJoint(node));
                    core::pmr::String label("@gltf/skin/", &core::defaultCpuMemoryResource());
                    core::appendUnsigned(label, skinIndex);
                    label += "/joint/";
                    core::appendUnsigned(label, slot);
                    assignText(leaf.name, label, names, bytes);
                    if (skin.inverseBindMatrices != absent) {
                        const auto* matrices = accessors.find(skin.inverseBindMatrices);
                        require(matrices && slot < matrices->count(),
                            "ASSET-GLB-SKELETON-INVERSE-BIND");
                        for (unsigned c = 0; c < 16; ++c)
                            require(matrices->readFloat(slot, c, leaf.inverseBindMatrix.m[c]),
                                "ASSET-GLB-SKELETON-INVERSE-BIND");
                        const auto& m = leaf.inverseBindMatrix.m;
                        require(m[3] == 0 && m[7] == 0 && m[11] == 0 && m[15] == 1,
                            "ASSET-GLB-SKELETON-INVERSE-BIND");
                    }
                    candidate.paletteJoints_[skin.firstJoint + slot] =
                        static_cast<std::uint32_t>(candidate.skeleton_.joints.size());
                    candidate.skeleton_.joints.push_back(std::move(leaf));
                    candidate.bindPose_.push_back({});
                }
            }
            charge(bytes, tracks.clips().size(), sizeof(animation::AnimationClip));
            charge(bytes, tracks.tracks().size(), sizeof(animation::AnimationChannel));
            candidate.clips_.reserve(tracks.clips().size());
            auto keyBudget = std::size_t(limits.maxKeyframes);
            for (std::uint32_t clipIndex = 0; clipIndex < tracks.clips().size(); ++clipIndex) {
                const auto& source = tracks.clips()[clipIndex];
                animation::AnimationClip clip;
                assignName(clip.name,
                    animations[clipIndex].find("name"),
                    "@gltf/animation/",
                    clipIndex,
                    names,
                    bytes);
                for (const auto& previous : candidate.clips_)
                    require(previous.name != clip.name, "ASSET-GLB-SKELETON-CLIP-NAME-DUPLICATE");
                clip.durationSeconds = source.durationSeconds;
                clip.channels.reserve(source.trackCount);
                for (std::uint32_t offset = 0; offset < source.trackCount; ++offset) {
                    require(std::size_t(source.firstTrack) + offset < tracks.tracks().size(),
                        "ASSET-GLB-SKELETON-INPUT");
                    const auto& track = tracks.tracks()[source.firstTrack + offset];
                    require(candidate.nodeJoint(track.node) != absent,
                        "ASSET-GLB-SKELETON-OUTSIDE-SCENE-TRACK");
                    const auto* input = accessors.find(track.input);
                    const auto* output = accessors.find(track.output);
                    require(input && output && input->count() == output->count(),
                        "ASSET-GLB-SKELETON-INPUT");
                    charge(keyBudget, input->count(), 1);
                    charge(bytes, input->count(), sizeof(animation::JointKeyframe));
                    animation::AnimationChannel channel;
                    channel.interpolation = animation::ChannelInterpolation::GltfLinear;
                    channel.jointIndex = candidate.nodeJoint(track.node);
                    assignText(channel.jointName,
                        candidate.skeleton_.joints[channel.jointIndex].name,
                        names,
                        bytes);
                    using Target = GlbAnimationTrackTable::Target;
                    require(track.target == Target::Translation || track.target == Target::Rotation
                            || track.target == Target::Scale,
                        "ASSET-GLB-SKELETON-TRACK-TARGET");
                    const auto path = track.target == Target::Translation ? "translation"
                        : track.target == Target::Rotation                ? "rotation"
                                                                          : "scale";
                    assignText(channel.targetPath, path, names, bytes);
                    channel.keyframes.reserve(input->count());
                    for (std::uint32_t key = 0; key < input->count(); ++key) {
                        animation::JointKeyframe value;
                        require(
                            input->readFloat(key, 0, value.timeSeconds), "ASSET-GLB-SKELETON-KEY");
                        float components[4]{};
                        const auto count = track.target == Target::Rotation ? 4u : 3u;
                        for (unsigned c = 0; c < count; ++c)
                            require(
                                output->readFloat(key, c, components[c]), "ASSET-GLB-SKELETON-KEY");
                        if (track.target == Target::Rotation) {
                            core::Quat q;
                            require(
                                core::tryNormalize(
                                    {components[0], components[1], components[2], components[3]},
                                    q),
                                "ASSET-GLB-SKELETON-KEY");
                            value.transform.rotation = {q.x, q.y, q.z, q.w};
                        } else if (track.target == Target::Scale)
                            value.transform.scale = {components[0], components[1], components[2]};
                        else
                            value.transform.translation = {
                                components[0], components[1], components[2]};
                        channel.keyframes.push_back(value);
                    }
                    clip.channels.push_back(std::move(channel));
                }
                require(
                    animation::validateClipSampling(clip) == nullptr, "ASSET-GLB-SKELETON-CLIP");
                candidate.clips_.push_back(std::move(clip));
            }
            require(candidate.skeleton_.valid(), "ASSET-GLB-SKELETON-HIERARCHY");
            candidate.decodedBytes_ = limits.maxDecodedBytes - bytes;
            skeleton_.joints.swap(candidate.skeleton_.joints);
            bindPose_.swap(candidate.bindPose_);
            clips_.swap(candidate.clips_);
            nodeJoints_.swap(candidate.nodeJoints_);
            paletteJoints_.swap(candidate.paletteJoints_);
            palettes_.swap(candidate.palettes_);
            std::swap(decodedBytes_, candidate.decodedBytes_);
            return true;
        } catch (const Failure& failure) {
            diagnostic = failure.diagnostic;
        } catch (const std::bad_alloc&) {
            diagnostic = "ASSET-GLB-SKELETON-ALLOCATION";
        }
        return false;
    }

private:
    static void charge(std::size_t& remaining, std::size_t count, std::size_t width)
    {
        chargeBudget(remaining, count, width, "ASSET-GLB-SKELETON-BUDGET");
    }

    static void assignText(
        core::pmr::String& output, std::string_view text, std::size_t& names, std::size_t& bytes)
    {
        require(
            !text.empty() && text.find('\0') == std::string_view::npos, "ASSET-GLB-SKELETON-NAME");
        charge(names, text.size(), 1);
        charge(bytes, text.size(), 1);
        output.assign(text);
    }

    static void assignName(core::pmr::String& output,
        const Json* name,
        const char* prefix,
        std::uint32_t index,
        std::size_t& names,
        std::size_t& bytes)
    {
        if (name) {
            require(name->type == Json::Type::String, "ASSET-GLB-SKELETON-NAME");
            assignText(output, name->stringValue, names, bytes);
        } else {
            core::pmr::String label(prefix, &core::defaultCpuMemoryResource());
            core::appendUnsigned(label, index);
            assignText(output, label, names, bytes);
        }
    }

    static animation::LocalTransform decodeNode(const Json& node, float elementError)
    {
        validateNodeTransform(node, true, "ASSET-GLB-SKELETON-NODE");
        animation::LocalTransform result;
        if (node.find("matrix")) {
            // glTF component order -> the one core row-major storage convention.
            const auto& values = array(node, "matrix");
            core::Mat4 matrix;
            for (unsigned r = 0; r < 4; ++r)
                for (unsigned c = 0; c < 4; ++c)
                    matrix.m[r * 4 + c] = static_cast<float>(values[c * 4 + r].numberValue);
            core::AffineTrs decomposed;
            require(core::tryDecomposeAffineTrs(matrix, elementError, decomposed),
                "ASSET-GLB-SKELETON-MATRIX-TRS-UNREPRESENTABLE");
            result = {
                {decomposed.translation.x, decomposed.translation.y, decomposed.translation.z},
                {decomposed.rotation.x,
                    decomposed.rotation.y,
                    decomposed.rotation.z,
                    decomposed.rotation.w},
                {decomposed.scale.x, decomposed.scale.y, decomposed.scale.z}};
        } else {
            for (const auto* path : {"translation", "scale"}) {
                if (!node.find(path))
                    continue;
                const auto& values = array(node, path);
                const animation::Vec3 vector{static_cast<float>(values[0].numberValue),
                    static_cast<float>(values[1].numberValue),
                    static_cast<float>(values[2].numberValue)};
                if (std::string_view(path) == "translation")
                    result.translation = vector;
                else
                    result.scale = vector;
            }
            if (node.find("rotation")) {
                const auto& values = array(node, "rotation");
                core::Quat q;
                require(core::tryNormalize({static_cast<float>(values[0].numberValue),
                                               static_cast<float>(values[1].numberValue),
                                               static_cast<float>(values[2].numberValue),
                                               static_cast<float>(values[3].numberValue)},
                            q),
                    "ASSET-GLB-SKELETON-NODE");
                result.rotation = {q.x, q.y, q.z, q.w};
            }
        }
        return result;
    }

    animation::Skeleton skeleton_;
    core::pmr::Vector<animation::LocalTransform> bindPose_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<animation::AnimationClip> clips_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint32_t> nodeJoints_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint32_t> paletteJoints_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Palette> palettes_{&core::defaultCpuMemoryResource()};
    std::size_t decodedBytes_ = 0;
};
} // namespace arp::assets::detail
