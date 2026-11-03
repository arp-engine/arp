#pragma once

#include "assets/mesh/glb_animated_node_graph.h"

namespace arp::assets::detail {

// A source-index animation plan. Values remain borrowed through the caller's
// accessor table; this object owns only clip/track descriptors. The later importer
// must explicitly remap nodes (including non-joint ancestors) before publication.
class GlbAnimationTrackTable final : private GlbGeometrySchema {
public:
    enum class Target : std::uint8_t { Translation, Rotation, Scale };

    struct Track final {
        std::uint32_t node = 0, input = 0, output = 0;
        Target target = Target::Translation;
    };

    struct Clip final {
        std::uint32_t firstTrack = 0, trackCount = 0;
        float durationSeconds = 0;
    };

    struct Limits final {
        std::size_t maxClips = 0, maxSamplers = 0, maxTracks = 0;
        std::size_t maxTimeKeys = 0, maxOutputKeys = 0, maxValueScalars = 0;
        std::size_t maxWorkingBytes = 0;
    };

    const core::pmr::Vector<Clip>& clips() const noexcept { return clips_; }

    const core::pmr::Vector<Track>& tracks() const noexcept { return tracks_; }

    std::size_t ownedBytes() const noexcept
    {
        return clips_.capacity() * sizeof(Clip) + tracks_.capacity() * sizeof(Track);
    }

    bool prepare(const Json& root,
        const GlbAccessorTable& accessors,
        const GlbAnimatedNodeGraph& graph,
        Limits limits,
        const char*& diagnostic)
    {
        diagnostic = nullptr;
        try {
            for (auto limit : {limits.maxClips,
                     limits.maxSamplers,
                     limits.maxTracks,
                     limits.maxTimeKeys,
                     limits.maxOutputKeys,
                     limits.maxValueScalars})
                require(limit && limit < UINT32_MAX, "ASSET-GLB-ANIMATION-LIMITS");
            require(limits.maxWorkingBytes && limits.maxWorkingBytes != SIZE_MAX,
                "ASSET-GLB-ANIMATION-LIMITS");
            const auto& animations = array(root, "animations");
            const auto& nodes = array(root, "nodes");
            require(!animations.empty() && nodes.size() == graph.nodes().size(),
                "ASSET-GLB-ANIMATION-SCHEMA");
            require(animations.size() <= limits.maxClips, "ASSET-GLB-ANIMATION-BUDGET");
            auto samplerBudget = limits.maxSamplers;
            std::size_t totalTracks = 0;
            for (const auto& animation : animations) {
                charge(samplerBudget, array(animation, "samplers").size(), 1);
                require(totalTracks <= limits.maxTracks
                        && array(animation, "channels").size() <= limits.maxTracks - totalTracks,
                    "ASSET-GLB-ANIMATION-BUDGET");
                totalTracks += array(animation, "channels").size();
            }
            auto remaining = limits.maxWorkingBytes;
            charge(remaining, ownedBytes(), 1);
            charge(remaining, animations.size(), sizeof(Clip));
            charge(remaining, totalTracks, sizeof(Track) + sizeof(std::uint64_t));
            GlbAnimationTrackTable candidate;
            candidate.clips_.reserve(animations.size());
            candidate.tracks_.reserve(totalTracks);
            core::pmr::Vector<std::uint64_t> targets{&core::defaultCpuMemoryResource()};
            targets.reserve(totalTracks);
            remaining = limits.maxWorkingBytes;
            charge(remaining, ownedBytes(), 1);
            charge(remaining, candidate.ownedBytes(), 1);
            charge(remaining, targets.capacity(), sizeof(std::uint64_t));
            auto timeBudget = limits.maxTimeKeys;
            auto outputBudget = limits.maxOutputKeys;
            auto scalarBudget = limits.maxValueScalars;
            for (const auto& animation : animations) {
                keys(animation, {"name", "samplers", "channels", "extras"});
                const auto& samplers = array(animation, "samplers");
                const auto& channels = array(animation, "channels");
                require(!samplers.empty() && !channels.empty(), "ASSET-GLB-ANIMATION-SCHEMA");
                for (const auto& sampler : samplers) {
                    keys(sampler, {"input", "output", "interpolation", "extras"});
                    if (sampler.find("interpolation"))
                        require(text(sampler, "interpolation", "LINEAR"),
                            "ASSET-GLB-ANIMATION-INTERPOLATION-UNSUPPORTED");
                    const auto* input = accessors.find(number(sampler, "input"));
                    const auto* output = accessors.find(number(sampler, "output"));
                    require(input && output && input->componentType() == 5126
                            && input->components() == 1 && !input->normalized()
                            && output->componentType() == 5126 && !output->normalized()
                            && (output->components() == 3 || output->components() == 4)
                            && output->count() == input->count() && input->count(),
                        "ASSET-GLB-ANIMATION-ACCESSOR");
                    charge(timeBudget, input->count(), 1);
                    float previous = -1;
                    for (std::uint32_t i = 0; i < input->count(); ++i) {
                        float time;
                        require(input->readFloat(i, 0, time) && time >= 0 && time > previous,
                            "ASSET-GLB-ANIMATION-TIME");
                        previous = time;
                    }
                }
                Clip clip{static_cast<std::uint32_t>(candidate.tracks_.size()),
                    static_cast<std::uint32_t>(channels.size()),
                    0};
                targets.clear();
                for (const auto& channel : channels) {
                    keys(channel, {"sampler", "target", "extras"});
                    const auto samplerIndex = number(channel, "sampler");
                    require(samplerIndex < samplers.size(), "ASSET-GLB-ANIMATION-SAMPLER");
                    const auto& target = field(channel, "target");
                    keys(target, {"node", "path", "extras"});
                    Track track;
                    track.node = number(target, "node");
                    require(track.node < nodes.size(), "ASSET-GLB-ANIMATION-NODE");
                    require(!nodes[track.node].find("matrix"), "ASSET-GLB-ANIMATION-MATRIX-TARGET");
                    if (text(target, "path", "translation"))
                        track.target = Target::Translation;
                    else if (text(target, "path", "rotation"))
                        track.target = Target::Rotation;
                    else {
                        require(text(target, "path", "scale"),
                            "ASSET-GLB-ANIMATION-TARGET-UNSUPPORTED");
                        track.target = Target::Scale;
                    }
                    targets.push_back(
                        (std::uint64_t(track.node) << 8) | static_cast<unsigned>(track.target));
                    const auto& sampler = samplers[samplerIndex];
                    track.input = number(sampler, "input");
                    track.output = number(sampler, "output");
                    const auto& input = *accessors.find(track.input);
                    const auto& output = *accessors.find(track.output);
                    const auto components = track.target == Target::Rotation ? 4u : 3u;
                    require(output.components() == components, "ASSET-GLB-ANIMATION-ACCESSOR");
                    charge(outputBudget, output.count(), 1);
                    charge(scalarBudget, output.count(), components);
                    for (std::uint32_t i = 0; i < output.count(); ++i) {
                        float values[4]{};
                        for (unsigned c = 0; c < components; ++c)
                            require(output.readFloat(i, c, values[c]), "ASSET-GLB-ANIMATION-VALUE");
                        if (track.target == Target::Rotation) {
                            core::Quat normalized;
                            require(core::tryNormalize(
                                        {values[0], values[1], values[2], values[3]}, normalized),
                                "ASSET-GLB-ANIMATION-VALUE");
                        }
                    }
                    float lastTime;
                    require(input.readFloat(input.count() - 1, 0, lastTime),
                        "ASSET-GLB-ANIMATION-TIME");
                    clip.durationSeconds = std::max(clip.durationSeconds, lastTime);
                    candidate.tracks_.push_back(track);
                }
                std::sort(targets.begin(), targets.end());
                require(std::adjacent_find(targets.begin(), targets.end()) == targets.end(),
                    "ASSET-GLB-ANIMATION-DUPLICATE-TARGET");
                candidate.clips_.push_back(clip);
            }
            clips_.swap(candidate.clips_);
            tracks_.swap(candidate.tracks_);
            return true;
        } catch (const Failure& failure) {
            diagnostic = failure.diagnostic;
        } catch (const std::bad_alloc&) {
            diagnostic = "ASSET-GLB-ANIMATION-ALLOCATION";
        }
        return false;
    }

private:
    static void charge(std::size_t& remaining, std::size_t count, std::size_t width)
    {
        chargeBudget(remaining, count, width, "ASSET-GLB-ANIMATION-BUDGET");
    }

    core::pmr::Vector<Clip> clips_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Track> tracks_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::assets::detail
