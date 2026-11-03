#pragma once

#include "core/hash/stable_hash.h"
#include "examples/build_a_queen/scene_content.h"

#include <cassert>

namespace arp::queen_test {

class ContentProbe final : public build_a_queen::SceneContent {
public:
    bool load(const core::pmr::String& root)
    {
        return loadCharacters(root) && loadAnimatedCharacters(root);
    }

    std::uint64_t staticFingerprint(bool includeSourceFiles = true) const
    {
        core::StableHash64 hash;
        const auto vector = [&](const core::Vec3& value) {
            hash.appendFloat(value.x);
            hash.appendFloat(value.y);
            hash.appendFloat(value.z);
        };
        hash.appendU64(characters_.size());
        for (const auto& character : characters_) {
            hash.appendString(character.id);
            if (includeSourceFiles)
                hash.appendString(character.sourceFile);
            vector(character.boundsMin);
            vector(character.boundsMax);
            hash.appendFloat(character.scale);
            hash.appendFloat(character.yawCos);
            hash.appendFloat(character.yawSin);
            hash.appendU64(character.materialBindings.size());
            for (const auto& binding : character.materialBindings) {
                hash.appendString(binding.name);
                hash.appendByte(static_cast<std::uint8_t>(binding.role));
            }
            hashVertices(hash, character.vertices);
            for (const auto& part : character.parts) {
                hash.appendByte(part.valid ? 1 : 0);
                hashVertices(hash, part.vertices);
            }
        }
        return hash.value();
    }

    std::uint64_t frameFingerprint(float seconds)
    {
        build_a_queen::Game game;
        assert(game.start(1));
        PreparedFrame frame;
        assert(prepareFrame(game.snapshot(),
            game.levelDesc(),
            game.pickups(),
            game.resolvedPickups(),
            game.pickupCount(),
            seconds,
            frame));
        core::StableHash64 hash;
        hashVertices(hash, frame.world);
        hashVertices(hash, frame.preview);
        return hash.value();
    }

    void verify() const
    {
        assert(characters_.size() == 2 && animatedCharacters_.size() == 2);
        for (std::size_t i = 0; i < 2; ++i) {
            const auto& character = animatedCharacters_[i];
            assert(
                character.asset.skeleton.jointCount() == 129 && character.asset.clips.size() == 24);
            assert(character.vertexCount == (i == 0 ? 19272u : 18324u));
            assert(character.vertices.front().joints[0] >= 67
                && character.vertices.front().joints[0] < 129);
            for (const auto& clip : character.asset.clips)
                for (const auto& channel : clip.channels)
                    assert(channel.interpolation == animation::ChannelInterpolation::GltfLinear);
            animation::SkinnedMeshPlayer player(character.asset);
            assert(player.valid());
            assert(player.update("Run", .25f) && player.update("Wave", .25f));
        }
    }

private:
    static void hashVertices(core::StableHash64& hash, const core::pmr::Vector<Vertex>& values)
    {
        hash.appendU64(values.size());
        for (const auto& vertex : values) {
            for (const auto value : vertex.position)
                hash.appendFloat(value);
            for (const auto value : vertex.normal)
                hash.appendFloat(value);
            for (const auto value : vertex.color)
                hash.appendFloat(value);
            hash.appendByte(vertex.materialRole);
            for (const auto value : vertex.joints)
                hash.appendFloat(value);
            for (const auto value : vertex.weights)
                hash.appendFloat(value);
        }
    }
};

} // namespace arp::queen_test
