#pragma once
#include "core/io/file_io.h"
#include "shader/spirv_buffer_declaration_layout.h"
#include "shader/wgsl_buffer_declaration_layout.h"
#include "spirv_binary_mutation_fixture.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace arp::test {
class BufferDeclarationTypeLayoutFixture final {
public:
    using Graph = shader::BufferDeclarationTypeLayouts;
    // Authored fixture scope; these are test limits, never device capabilities.
    static constexpr Graph::Budget fixtureBudget{128u, 128u, 8u};

    static Graph wgsl(std::string_view source, Graph::Budget budget = fixtureBudget)
    {
        Graph types;
        core::pmr::Vector<shader::WgslBufferDeclarationLayout> facts{
            &core::defaultCpuMemoryResource()};
        const auto* id = shader::WgslBufferDeclarationLayoutVerifier::verifyWithTypes(
            source, 8u, budget, facts, types);
        if (id)
            std::fprintf(stderr, "Unexpected type graph rejection: %s\n", id);
        assert(!id && facts.size() == types.roots().size());
        return types;
    }

    static const Graph::Root& root(const Graph& types, std::uint32_t binding = 0u)
    {
        for (const auto& root : types.roots())
            if (root.set == 0u && root.binding == binding)
                return root;
        assert(false);
        return types.roots().front();
    }

    static void compare(
        const Graph& lhs, const Graph& rhs, bool expected, std::size_t maxPairs = 128u)
    {
        bool equal = !expected;
        assert(!Graph::compare(lhs, root(lhs).type, rhs, root(rhs).type, maxPairs, equal)
            && equal == expected);
    }

    static void reject(std::string_view source, Graph::Budget budget, const char* expected)
    {
        auto graph = wgsl("@group(0) @binding(0) var<storage> v:u32;");
        core::pmr::Vector<shader::WgslBufferDeclarationLayout> facts{
            &core::defaultCpuMemoryResource()};
        facts.push_back({});
        const auto* id = shader::WgslBufferDeclarationLayoutVerifier::verifyWithTypes(
            source, 8u, budget, facts, graph);
        assert(id && std::strcmp(id, expected) == 0 && facts.empty() && graph.nodes().empty()
            && graph.members().empty() && graph.roots().empty());
        assert(!graph.budget().maxNodes && !graph.budget().maxMembers && !graph.budget().maxRoots);
    }

    static void verify()
    {
        auto scalar = wgsl("@group(0) @binding(0) var<storage> v:u32;");
        auto signedScalar = wgsl("@group(0) @binding(0) var<storage> v:i32;");
        auto floating = wgsl("@group(0) @binding(0) var<storage> v:f32;");
        auto alias = wgsl("alias A=B; alias B=u32; @group(0) @binding(0) var<storage> v:A;");
        compare(scalar, alias, true);
        compare(scalar, signedScalar, false);
        compare(scalar, floating, false);
        auto first = wgsl("struct S { a:u32, b:f32 }; @group(0) @binding(0) var<storage> v:S;");
        auto swapped = wgsl("struct S { a:f32, b:u32 }; @group(0) @binding(0) var<storage> v:S;");
        auto renamed = wgsl("struct T { x:u32, y:f32 }; @group(0) @binding(0) var<storage> v:T;");
        compare(first, swapped, false);
        compare(first, renamed, true);
        auto reserved =
            wgsl("struct S { @size(16) a:u32 }; @group(0) @binding(0) var<storage> v:S;");
        auto aligned =
            wgsl("struct S { @align(16) a:u32 }; @group(0) @binding(0) var<storage> v:S;");
        assert(reserved.nodes().back().targetSizeBytes == aligned.nodes().back().targetSizeBytes);
        compare(reserved, aligned, false);
        bool memoryEqual = false;
        assert(!Graph::compare(reserved,
                   root(reserved).type,
                   aligned,
                   root(aligned).type,
                   128u,
                   memoryEqual,
                   Graph::Comparison::MemoryRepresentation)
            && memoryEqual);
        auto padded = wgsl("struct S { @size(32) a:u32 }; @group(0) @binding(0) var<storage> v:S;");
        assert(!Graph::compare(reserved,
                   root(reserved).type,
                   padded,
                   root(padded).type,
                   128u,
                   memoryEqual,
                   Graph::Comparison::MemoryRepresentation)
            && memoryEqual);
        auto offset =
            wgsl("struct S { a:u32, @align(16) b:f32 }; @group(0) @binding(0) var<storage> v:S;");
        assert(!Graph::compare(first,
                   root(first).type,
                   offset,
                   root(offset).type,
                   128u,
                   memoryEqual,
                   Graph::Comparison::MemoryRepresentation)
            && !memoryEqual);
        assert(Graph::compare(first,
                   root(first).type,
                   first,
                   root(first).type,
                   128u,
                   memoryEqual,
                   static_cast<Graph::Comparison>(99u))
            && !memoryEqual);
        auto flat = wgsl("@group(0) @binding(0) var<storage> v:array<u32,6>;");
        auto nested = wgsl("@group(0) @binding(0) var<storage> v:array<array<u32,3>,2>;");
        assert(flat.nodes().back().targetSizeBytes == nested.nodes().back().targetSizeBytes);
        compare(flat, nested, false);
        auto vector = wgsl("@group(0) @binding(0) var<storage> v:vec3f;");
        assert(vector.nodes().back().occupiedExtentBytes == 12u
            && vector.nodes().back().targetSizeBytes == 12u
            && vector.nodes().back().alignmentBytes == 16u);
        auto matrix = wgsl("@group(0) @binding(0) var<storage> v:mat4x3f;");
        const auto& mat = matrix.nodes().back();
        assert(mat.kind == Graph::Kind::Matrix && mat.rows == 3u && mat.columns == 4u
            && mat.strideBytes == 16u && mat.occupiedExtentBytes == 60u
            && mat.targetSizeBytes == 64u
            && mat.matrixEncoding == Graph::MatrixEncoding::WgslColumnVectors);
        auto rows = wgsl("@group(0) @binding(0) var<storage> v:array<vec3f,4>;");
        assert(rows.nodes().back().occupiedExtentBytes == mat.occupiedExtentBytes);
        compare(matrix, rows, false);
        auto atomic = wgsl("@group(0) @binding(0) var<storage,read_write> v:atomic<u32>;");
        compare(scalar, atomic, false);
        auto runtime = wgsl(
            "struct S { a:u32, @align(16) b:array<vec3f> }; @group(0) @binding(0) var<storage> v:S;");
        const auto& rt = runtime.nodes().back();
        assert(rt.occupiedExtentBytes == 4u && rt.targetSizeBytes == 32u);
        const auto& last = runtime.members().at(rt.memberBegin + 1u);
        assert(last.offsetBytes == 16u && last.alignmentKnown && last.alignmentBytes == 16u
            && !last.reservedSizeKnown && !last.reservedSizeBytes);
        assert(runtime.nodes()[last.type].kind == Graph::Kind::RuntimeArray
            && runtime.nodes()[last.type].strideBytes == 16u
            && !runtime.nodes()[last.type].occupiedExtentBytes
            && runtime.nodes()[last.type].targetSizeBytes == 16u);
        auto shared =
            wgsl("alias T=u32; struct S { a:T,b:T }; @group(0) @binding(0) var<storage> v:S;");
        auto repeated = wgsl("struct S { a:u32,b:u32 }; @group(0) @binding(0) var<storage> v:S;");
        assert(shared.nodes().size() < repeated.nodes().size());
        compare(shared, repeated, true);
        for (const auto budget : {Graph::Budget{0u, 128u, 8u}, Graph::Budget{1u, 128u, 8u}})
            reject("struct S { a:u32 }; @group(0) @binding(0) var<storage> v:S;",
                budget,
                "SHADER-BUFFER-TYPE-NODE-BUDGET");
        reject("struct S { a:u32,b:u32 }; @group(0) @binding(0) var<storage> v:S;",
            {128u, 1u, 8u},
            "SHADER-BUFFER-TYPE-MEMBER-BUDGET");
        reject("@group(0) @binding(0) var<storage> v:u32;",
            {128u, 128u, 0u},
            "SHADER-BUFFER-TYPE-ROOT-BUDGET");
        reject(
            "@group(0) @binding(0) var<storage> v:u32; @group(0) @binding(1) var<storage> w:Missing;",
            fixtureBudget,
            "SHADER-WGSL-LAYOUT-TYPE-UNSUPPORTED");
        bool equal = true;
        assert(Graph::compare(first, root(first).type, renamed, root(renamed).type, 1u, equal)
            && !equal);
        assert(Graph::compare(first, Graph::absent, renamed, root(renamed).type, 128u, equal)
            && !equal);
        assert(Graph::compare(first, root(first).type, renamed, root(renamed).type, 0u, equal)
            && !equal);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto hostile = wgsl("@group(0) @binding(0) var<storage> v:u32;");
        compare(scalar, hostile, true);
        std::pmr::set_default_resource(previous);
        auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease exhausted(quota, 1u);
            assert(Graph::compare(scalar, root(scalar).type, alias, root(alias).type, 8u, equal)
                && !equal);
            Graph rejected;
            core::pmr::Vector<shader::WgslBufferDeclarationLayout> facts{
                &core::defaultCpuMemoryResource()};
            const auto* id = shader::WgslBufferDeclarationLayoutVerifier::verifyWithTypes(
                "@group(0) @binding(0) var<storage> v:u32;", 8u, fixtureBudget, facts, rejected);
            assert(id && !std::strcmp(id, "SHADER-WGSL-LAYOUT-ALLOCATION")
                && rejected.nodes().empty() && facts.empty());
        }
        assert(!quota->liveBytes());
        core::pmr::String deep{&core::defaultCpuMemoryResource()};
        deep = "@group(0) @binding(0) var<storage> v:";
        for (unsigned i = 0u; i < 1500u; ++i)
            deep += "array<";
        deep += "u32";
        for (unsigned i = 0u; i < 1500u; ++i)
            deep += ",1>";
        deep += ";";
        auto a = wgsl(deep, {1501u, 0u, 1u});
        auto b = wgsl(deep, {1501u, 0u, 1u});
        compare(a, b, true, 1501u);
        assert(Graph::compare(a, root(a).type, b, root(b).type, 1500u, equal) && !equal);
        reject(deep, {1500u, 0u, 1u}, "SHADER-BUFFER-TYPE-NODE-BUDGET");
        std::puts(
            "BINDLESS_RHI_BUFFER_TYPE_LAYOUT_OK bounded-core-pmr complete-typed-members scalar-vector-matrix-fixed-runtime alias-sharing budget-clearing deep1500 no-original-native-authority");
    }

    static Graph binary(const core::ByteBuffer& bytes, Graph::Budget budget = fixtureBudget)
    {
        Graph types;
        core::pmr::Vector<shader::SpirvBufferDeclarationLayout> facts{
            &core::defaultCpuMemoryResource()};
        const auto* id = shader::SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(
            bytes.data(), bytes.size(), 8u, budget, facts, types);
        if (id)
            std::fprintf(stderr, "Unexpected binary type graph rejection: %s\n", id);
        assert(!id && types.roots().size() == facts.size());
        for (const auto& f : facts) {
            const auto& root = BufferDeclarationTypeLayoutFixture::root(types, f.binding);
            assert(root.descriptorCount == f.descriptorCount
                && types.nodes()[root.type].occupiedExtentBytes == f.staticOccupiedExtentBytes);
            for (const auto& n : types.nodes())
                assert(!n.targetSizeKnown && !n.targetSizeBytes && !n.alignmentKnown
                    && !n.alignmentBytes);
        }
        return types;
    }

    static void real(const char* wgslPath, const char* binaryPath)
    {
        const auto source = core::readBinaryFile(wgslPath, std::filesystem::file_size(wgslPath));
        assert(source.success);
        const auto bytes = core::readBinaryFile(binaryPath, std::filesystem::file_size(binaryPath));
        assert(bytes.success);
        auto target =
            wgsl({reinterpret_cast<const char*>(source.bytes.data()), source.bytes.size()});
        auto spirv = binary(bytes.bytes);
        assert(target.roots().size() == 3u && spirv.roots().size() == 3u);
        const auto& r = root(spirv, 1u);
        const auto& block = spirv.nodes()[r.type];
        assert(block.kind == Graph::Kind::Struct && block.memberCount == 1u);
        const auto& array = spirv.nodes()[spirv.members()[block.memberBegin].type];
        assert(array.kind == Graph::Kind::RuntimeArray && array.strideBytes == 48u);
        const auto& element = spirv.nodes()[array.child];
        assert(element.kind == Graph::Kind::Struct && element.memberCount == 3u
            && element.occupiedExtentBytes == 40u);
        const auto& targetArray = target.nodes()[root(target, 1u).type];
        assert(targetArray.kind == Graph::Kind::RuntimeArray && targetArray.strideBytes == 48u);
        const auto& targetElement = target.nodes()[targetArray.child];
        assert(targetElement.memberCount == 3u && targetElement.occupiedExtentBytes == 40u
            && targetElement.targetSizeBytes == 48u);
        for (std::size_t i = 0u; i < 3u; ++i) {
            const auto& m = spirv.members()[element.memberBegin + i];
            const auto& n = target.members()[targetElement.memberBegin + i];
            assert(m.offsetBytes == n.offsetBytes && !m.alignmentKnown && !m.reservedSizeKnown
                && n.alignmentKnown && n.reservedSizeKnown);
        }
        bool equal = true;
        assert(
            !Graph::compare(spirv, array.child, target, targetArray.child, 128u, equal) && !equal);
        // Same-size signedness mutation: occupied/minimum summaries are stable,
        // but complete binary type graphs must differ.
        SpirvBinaryMutationFixture mutation(bytes.bytes);
        assert(mutation.operand(mutation.locate(21u, 2, 0u), 1u) == 32u);
        mutation.patch(21u, 2u, 1u, 2, 0u);
        auto mutated = binary(mutation.bytes());
        assert(spirv.nodes()[r.type].occupiedExtentBytes
            == mutated.nodes()[root(mutated, 1u).type].occupiedExtentBytes);
        assert(
            !Graph::compare(spirv, r.type, mutated, root(mutated, 1u).type, 128u, equal) && !equal);
        for (const auto budget : {Graph::Budget{0u, 128u, 8u},
                 Graph::Budget{128u, 0u, 8u},
                 Graph::Budget{128u, 128u, 0u}}) {
            Graph graph;
            core::pmr::Vector<shader::SpirvBufferDeclarationLayout> facts{
                &core::defaultCpuMemoryResource()};
            facts.push_back({});
            assert(shader::SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(
                bytes.bytes.data(), bytes.bytes.size(), 8u, budget, facts, graph));
            assert(facts.empty() && graph.nodes().empty() && graph.members().empty()
                && graph.roots().empty());
        }
        auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease exhausted(quota, 1u);
            Graph graph;
            core::pmr::Vector<shader::SpirvBufferDeclarationLayout> facts{
                &core::defaultCpuMemoryResource()};
            const auto* id = shader::SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(
                bytes.bytes.data(), bytes.bytes.size(), 8u, fixtureBudget, facts, graph);
            assert(id && !std::strcmp(id, "SHADER-BUFFER-TYPE-ALLOCATION") && facts.empty()
                && graph.nodes().empty() && graph.members().empty() && graph.roots().empty());
        }
        assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_BUFFER_TYPE_LAYOUT_REAL_OK actual-slang occupied40 target48 all-members scalar-sign-mutation-rejected no-padding-inference no-original-native-authority");
    }
};
} // namespace arp::test
