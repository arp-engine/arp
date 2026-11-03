#pragma once

#include "assets/mesh/glb_animated_node_graph.h"

#include <cassert>
#include <cstring>

namespace arp::tests::animated_glb_source_fixture {
using namespace assets::detail;

using Graph = GlbAnimatedNodeGraph;
constexpr Graph::Limits limits{8, 8, 4, 16, 4, 8, 8, 4096};

inline JsonValue parse(const char* text)
{
    JsonValue result;
    assert(parseJson(core::pmr::String(text), result, {16, 512, false}));
    return result;
}

inline JsonValue& member(JsonValue& object, const char* key)
{
    const auto* value = object.find(key);
    assert(value);
    return *const_cast<JsonValue*>(value);
}

class Fixture {
public:
    JsonValue root;
    core::pmr::String binary;
    GlbAccessorTable accessors;

    Fixture() : binary(64, '\0')
    {
        // clang-format off
        root = parse(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":64}],
"bufferViews":[{"buffer":0,"byteLength":64}],
"accessors":[{"bufferView":0,"componentType":5126,"type":"MAT4","count":1}],
"nodes":[{"name":"a","mesh":0,"skin":0,"children":[1]},
{"rotation":[0,0,0,1]}, {"mesh":0,"skin":1,"children":[3]}, {}, {"mesh":0}],
"scenes":[{"nodes":[0,2,4]},{"nodes":[2]}],
"skins":[{"joints":[1],"skeleton":0,"inverseBindMatrices":0},
{"joints":[3],"skeleton":2}],"meshes":[{}]})");
        // clang-format on
        for (std::size_t diagonal : {0u, 20u, 40u, 60u})
            writeFloat(binary, diagonal, 1);
        const char* diagnostic = nullptr;
        assert(accessors.prepare(root, binary, {64, 1, 1, 256, 1, 16}, diagnostic));
    }

    static void writeFloat(core::pmr::String& bytes, std::size_t offset, float value)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        assert(core::UnsignedElementSpan::writeLittleEndian(bits,
            core::UnsignedElementSpan::Width::Uint32,
            reinterpret_cast<std::uint8_t*>(bytes.data()) + offset,
            bytes.size() - offset));
    }

    JsonValue& node(std::size_t index) { return member(root, "nodes").arrayValue[index]; }

    JsonValue& skin(std::size_t index) { return member(root, "skins").arrayValue[index]; }

    static void set(JsonValue& object, const char* key, JsonValue value)
    {
        if (object.find(key))
            member(object, key) = std::move(value);
        else
            object.objectValue.emplace_back(key, std::move(value));
    }
};

class TrackFixture : public Fixture {
public:
    Graph graph;

    TrackFixture()
    {
        binary.resize(112);
        member(member(root, "buffers").arrayValue[0], "byteLength") = parse("112");
        // clang-format off
        member(root, "bufferViews").arrayValue.push_back(parse(R"({"buffer":0,"byteOffset":64,"byteLength":8})"));
        member(root, "bufferViews").arrayValue.push_back(parse(R"({"buffer":0,"byteOffset":80,"byteLength":32})"));
        member(root, "accessors").arrayValue.push_back(parse(R"({"bufferView":1,"componentType":5126,"type":"SCALAR","count":2})"));
        member(root, "accessors").arrayValue.push_back(parse(R"({"bufferView":2,"componentType":5126,"type":"VEC4","count":2})"));
        set(root, "animations", parse(R"([{"samplers":[{"input":1,"output":2}],"channels":[{"sampler":0,"target":{"node":1,"path":"rotation"}}]}])"));
        // clang-format on
        real(4, 1);
        real(28, 1);
        real(36, 1);
        rebuild();
        const char* diagnostic = nullptr;
        assert(graph.prepare(root, accessors, 0, limits, diagnostic));
    }

    void real(std::size_t offset, float value) { writeFloat(binary, 64 + offset, value); }

    void rebuild()
    {
        const char* diagnostic = nullptr;
        assert(accessors.prepare(
            root, binary, {112, 3, 3, 6 * sizeof(GlbAccessorSpan), 8, 32}, diagnostic));
    }

    JsonValue& animation() { return member(root, "animations").arrayValue[0]; }

    JsonValue& sampler() { return member(animation(), "samplers").arrayValue[0]; }

    JsonValue& channel() { return member(animation(), "channels").arrayValue[0]; }

    JsonValue& target() { return member(channel(), "target"); }
};

} // namespace arp::tests::animated_glb_source_fixture
