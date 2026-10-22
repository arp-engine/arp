#pragma once

#include "core/encoding/unsigned_element_span.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace arp::tests {
class StaticGlbGeometryFixture final {
public:
    static std::string document()
    {
        // clang-format off
        return R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":150}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},
{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":48},
{"buffer":0,"byteOffset":144,"byteLength":6}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
{"bufferView":3,"componentType":5126,"count":3,"type":"VEC4"},
{"bufferView":4,"componentType":5123,"count":3,"type":"SCALAR"}],
"materials":[{"name":"external-slot-zero"}],"meshes":[{"primitives":[
{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"TANGENT":3},"indices":4,"material":0}]}],
"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
        // clang-format on
    }

    static void write(const std::filesystem::path& path, std::string json = document())
    {
        while (json.size() % 4u)
            json += ' ';
        std::ofstream out(path, std::ios::binary);
        const auto word = [&](std::uint32_t value) {
            std::uint8_t bytes[4];
            assert(core::UnsignedElementSpan::writeLittleEndian(
                value, core::UnsignedElementSpan::Width::Uint32, bytes, sizeof(bytes)));
            out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
        };
        word(0x46546c67u);
        word(2u);
        word(static_cast<std::uint32_t>(28u + json.size() + 152u));
        word(static_cast<std::uint32_t>(json.size()));
        word(0x4e4f534au);
        out.write(json.data(), json.size());
        word(152u);
        word(0x004e4942u);
        for (float value : {1.25f,
                 0.f,
                 0.f,
                 2.5f,
                 0.f,
                 0.f,
                 1.25f,
                 1.f,
                 -0.75f,
                 0.f,
                 0.f,
                 1.f,
                 0.f,
                 0.f,
                 1.f,
                 0.f,
                 0.f,
                 1.f,
                 0.f,
                 0.f,
                 1.f,
                 0.f,
                 0.f,
                 1.f,
                 1.f,
                 0.f,
                 0.f,
                 1.f,
                 1.f,
                 0.f,
                 0.f,
                 1.f,
                 1.f,
                 0.f,
                 0.f,
                 1.f}) {
            std::uint32_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            word(bits);
        }
        const char indices[] = {0, 0, 1, 0, 2, 0, 0, 0};
        out.write(indices, sizeof(indices));
        assert(out.good());
    }
};
} // namespace arp::tests
