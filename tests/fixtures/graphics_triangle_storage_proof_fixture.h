#pragma once
#include "core/memory/cpu_allocator.h"
#include "shader/graphics_triangle_storage_cross_target_proof.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class GraphicsTriangleStorageProofFixture final {
public:
    static void verify(
        const core::ByteBuffer& vertex, const core::ByteBuffer& fragment, core::StringView source)
    {
        using Proof = shader::GraphicsTriangleStorageCrossTargetProof;
        using Target = shader::GraphicsTriangleStorageWgslProof;
        Proof::Facts proof;
        const auto cross = [&](core::StringView input) {
            return Proof::verify(vertex.data(),
                vertex.size(),
                fragment.data(),
                fragment.size(),
                input,
                "vertexMain",
                "fragmentMain",
                proof);
        };
        const auto* id = cross(source);
        if (id)
            std::printf("GRAPHICS_PROOF_FAILURE %s\n", id);
        assert(!id && proof.vertex.positions == shader::TrianglePositionBits::fullscreen());
        assert(proof.fragment.byteCount == 4u && proof.target.byteCount == 4u
            && proof.vertex.requiresDrawParameters);
        const auto rejected = [&](core::StringView input) {
            Target::Facts facts{
                99u, 99u, 99u, 99u, 99u, 99u, shader::TrianglePositionBits::fullscreen()};
            assert(Target::verify(input, "vertexMain", "fragmentMain", facts));
            assert(!facts.contractVersion && !facts.group && !facts.binding && !facts.comparison
                && !facts.byteOffset && !facts.byteCount);
            for (const auto& p : facts.positions)
                for (const auto bit : p)
                    assert(!bit);
            assert(cross(input) && !proof.vertex.contractVersion && !proof.fragment.contractVersion
                && !proof.target.contractVersion);
        };
        const auto replaced = [&](core::StringView before, core::StringView after) {
            core::pmr::String copy(source, &core::defaultCpuMemoryResource());
            const auto at = copy.find(before);
            assert(at != copy.npos);
            copy.replace(at, before.size(), after);
            return copy;
        };
        rejected({});
        rejected(source.substr(0, source.size() - 4u));
        for (const auto pair :
            {std::pair<core::StringView, core::StringView>{"storage, read", "storage, read_write"},
                {"array<u32>", "array<f32>"},
                {"@builtin(position)", "@location(0)"},
                {"@location(0)", "@location(1)"},
                {"vertex_index", "instance_index"},
                {"vertex_0 == u32(0)", "vertex_0 == u32(1)"},
                {"vertex_0 == u32(1)", "vertex_0 != u32(1)"},
                {"p_0 = vec2", "vertex_0 = vec2"},
                {"vec4<f32>(p_0,", "vec4<f32>(vertex_0,"},
                {"return _S1;", "return _S2;"},
                {"return _S2;", "return _S1;"},
                {"Input_0[i32(0)]", "Input_0[i32(1)]"},
                {"Input_0[i32(0)]", "Input_0[i32(position_0)]"},
                {"== u32(305419896)", "!= u32(305419896)"},
                {"position_0 :", "Input_0 :"},
                {"var _S2 :", "var Input_0 :"},
                {"1.0f", "1.0e0"},
                {"1.0f", "1.0F"},
                {"1.0f", "1.f"},
                {"1.0f", "-0.0f"},
                {"305419896", "4294967296"},
                {"305419896", "305419896u"},
                {"Input_0[i32(0)]", "Other_0[i32(0)]"},
                {"select(vec4<f32>(1.0f, 0.0f, 0.0f, 1.0f)",
                    "select(vec4<f32>(0.0f, 1.0f, 0.0f, 1.0f)"}})
            rejected(replaced(pair.first, pair.second));
        auto extra = core::pmr::String(source, &core::defaultCpuMemoryResource());
        extra += "fn helper() {}";
        rejected(extra);
        extra = source;
        extra += "/* unclosed";
        rejected(extra);
        extra = source;
        extra.push_back('\0');
        rejected(extra);
        extra = source;
        extra += "//end\n/* outer /*inner*/ */";
        assert(!cross(extra));
        auto comments = replaced("storage, read", "/*a*/ storage, //b\n read");
        assert(!cross(comments));
        for (const auto pair :
            {std::pair<core::StringView, core::StringView>{"305419896", "305419897"},
                {"vec2<f32>(3.0f, -1.0f)", "vec2<f32>(1.0f, -1.0f)"}}) {
            auto mismatch = replaced(pair.first, pair.second);
            Target::Facts target;
            assert(!Target::verify(mismatch, "vertexMain", "fragmentMain", target));
            assert(std::string_view(cross(mismatch)) == "SHADER-GRAPHICS-TRIANGLE-TARGET-MISMATCH");
        }
        auto renamed = core::pmr::String(source, &core::defaultCpuMemoryResource());
        for (auto at = renamed.find("Input_0"); at != renamed.npos;
            at = renamed.find("Input_0", at + 1u))
            renamed.replace(at, 7u, "Buffer9");
        assert(!cross(renamed));
        for (const auto pair : {std::pair<core::StringView, core::StringView>{"_S2", "Input_0"},
                 {"_S1", "vertexOutput_0"},
                 {"p_0", "vertex_0"}}) {
            auto shadow = core::pmr::String(source, &core::defaultCpuMemoryResource());
            for (auto at = shadow.find(pair.first); at != shadow.npos;
                at = shadow.find(pair.first, at + pair.second.size()))
                shadow.replace(at, pair.first.size(), pair.second);
            rejected(shadow);
        }
        assert(!Proof::validateDraw(3u, 1u, 0u, 0u));
        for (const auto invalid : {0u, 2u, UINT32_MAX})
            assert(Proof::validateDraw(invalid, 1u, 0u, 0u));
        for (const auto invalid : {0u, 2u, UINT32_MAX})
            assert(Proof::validateDraw(3u, invalid, 0u, 0u));
        assert(Proof::validateDraw(3u, 1u, 1u, 0u) && Proof::validateDraw(3u, 1u, 0u, 1u)
            && Proof::validateDraw(3u, 1u, 0u, 0u, true));
        auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        core::CpuMemoryQuotaScope scope(quota);
        core::CpuMemoryQuotaLease pressure(quota, 1u);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (unsigned i = 0; i < 20u; ++i) {
            assert(!cross(source));
            rejected({});
        }
        std::pmr::set_default_resource(previous);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
};
} // namespace arp::test
