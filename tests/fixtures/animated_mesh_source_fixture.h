#pragma once

#include "assets/animated_mesh_source_import.h"
#include "tests/fixtures/static_glb_geometry.h"

namespace arp::tests {

class AnimatedMeshSourceFixture final {
public:
    static core::pmr::String document()
    {
        // clang-format off
        return R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":308}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},
{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},
{"buffer":0,"byteOffset":96,"byteLength":48},{"buffer":0,"byteOffset":144,"byteLength":6},
{"buffer":0,"byteOffset":152,"byteLength":12},{"buffer":0,"byteOffset":164,"byteLength":48},
{"buffer":0,"byteOffset":212,"byteLength":8},{"buffer":0,"byteOffset":220,"byteLength":24},
{"buffer":0,"byteOffset":244,"byteLength":64}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
{"bufferView":3,"componentType":5126,"count":3,"type":"VEC4"},
{"bufferView":4,"componentType":5123,"count":3,"type":"SCALAR"},
{"bufferView":5,"componentType":5121,"count":3,"type":"VEC4"},
{"bufferView":6,"componentType":5126,"count":3,"type":"VEC4"},
{"bufferView":7,"componentType":5126,"count":2,"type":"SCALAR"},
{"bufferView":8,"componentType":5126,"count":2,"type":"VEC3"},
{"bufferView":9,"componentType":5126,"count":1,"type":"MAT4"}],
"materials":[{"name":"surface","pbrMetallicRoughness":{"baseColorFactor":[0.2,0.4,0.6,1]}}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"TANGENT":3,"JOINTS_0":5,"WEIGHTS_0":6},"indices":4,"material":0}]},
{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"TANGENT":3},"indices":4,"material":0}]}],
"nodes":[{"name":"root","children":[2]},{"mesh":0,"skin":0},
{"name":"bone","translation":[2,3,4]},{"mesh":0,"skin":1},
{"mesh":1,"translation":[5,6,7]}],"scenes":[{"nodes":[0,1,3,4]}],
"skins":[{"joints":[2],"skeleton":0,"inverseBindMatrices":9},{"joints":[2],"skeleton":0}],
"animations":[{"name":"move","samplers":[{"input":7,"output":8}],"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]}]})";
        // clang-format on
    }

    static void word(core::ByteBuffer& bytes, std::size_t offset, std::uint32_t value)
    {
        assert(core::UnsignedElementSpan::writeLittleEndian(value,
            core::UnsignedElementSpan::Width::Uint32,
            bytes.data() + offset,
            bytes.size() - offset));
    }

    static void real(core::ByteBuffer& bytes, std::size_t offset, float value)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        word(bytes, offset, bits);
    }

    static core::ByteBuffer binary()
    {
        const auto base = StaticGlbGeometryFixture::bytes();
        // Reuse the explicitly authored 152-byte static triangle payload.
        core::ByteBuffer data(base.end() - 152, base.end());
        data.resize(308);
        for (unsigned vertex = 0; vertex < 3; ++vertex)
            real(data, 164 + vertex * 16, 1);
        real(data, 216, 1);
        constexpr float translations[]{10, 20, 30, 14, 28, 42};
        for (unsigned i = 0; i < 6; ++i)
            real(data, 220 + i * 4, translations[i]);
        constexpr float matrix[]{2, 2, 3, 0, 4, 5, 6, 0, 7, 8, 10, 0, 10, 20, 30, 1};
        for (unsigned i = 0; i < 16; ++i)
            real(data, 244 + i * 4, matrix[i]);
        return data;
    }

    static core::ByteBuffer bytes(
        const core::pmr::String& json = document(), const core::ByteBuffer& payload = binary())
    {
        // The existing fixture writes the canonical GLB header and JSON padding.
        // Replace only its declared BIN payload, using explicit source lengths.
        auto data = StaticGlbGeometryFixture::bytes(std::string(json.data(), json.size()));
        const auto start = data.size() - 152;
        data.resize(start + payload.size());
        std::copy(payload.begin(), payload.end(), data.begin() + start);
        word(data, start - 8, static_cast<std::uint32_t>(payload.size()));
        word(data, 8, static_cast<std::uint32_t>(data.size()));
        return data;
    }

    static core::pmr::String replace(
        core::pmr::String text, std::string_view from, std::string_view to)
    {
        const auto at = text.find(from);
        assert(at != core::pmr::String::npos);
        text.replace(at, from.size(), to);
        return text;
    }

    static assets::AnimatedMeshSourceImportLimits limits()
    {
        assets::AnimatedMeshSourceImportLimits result;
        result.maxSourceBytes = 65536;
        result.maxImportBytes = 2 * 1024 * 1024;
        result.maxDecodedBytes = 256 * 1024;
        result.maxJsonDepth = 32;
        result.maxJsonValues = 4096;
        result.selectedScene = 0;
        result.layout = {16, 16, 256, 2048, 4096};
        result.scene = {16, 16, 4, 16, 4, 16, 8, 4096};
        result.animation = {8, 16, 16, 64, 64, 256, 4096};
        result.skeleton = {32, 64, 8192, .00001f};
        result.geometry = {64,
            64,
            8,
            assets::AnimatedMeshColorPolicy::RejectColor0,
            assets::AnimatedMeshTangentPolicy::AuthoredOrMaterialUnused,
            .0001f,
            .0001f,
            .0001f};
        result.materials = {8, 8, 8, 16384, 8192};
        return result;
    }
};
} // namespace arp::tests
