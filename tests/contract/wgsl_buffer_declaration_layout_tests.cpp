#include "core/io/file_io.h"
#include "shader/wgsl_buffer_declaration_layout.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>

#ifdef NDEBUG
#error WGSL declaration tests require assertions
#endif

namespace {
using namespace arp;
using Facts = shader::WgslBufferDeclarationLayout;
using Verifier = shader::WgslBufferDeclarationLayoutVerifier;

class LayoutFixture final {
public:
    Facts valid(std::string_view source,
        std::uint64_t size,
        std::uint64_t alignment,
        bool runtime = false,
        std::uint64_t offset = 0u,
        std::uint64_t stride = 0u)
    {
        const auto* id = Verifier::verify(source, 1u, output_);
        if (id)
            std::fprintf(stderr, "Unexpected: %s\n", id);
        assert(!id && output_.size() == 1u);
        const auto facts = output_.front();
        assert(facts.minimumBindingBytes == size && facts.alignmentBytes == alignment
            && facts.hasRuntimeArray == runtime && facts.runtimeArrayOffsetBytes == offset
            && facts.runtimeArrayStrideBytes == stride);
        return facts;
    }

    void reject(
        std::string_view source, const char* expected = nullptr, std::uint32_t maxBindings = 1u)
    {
        output_.push_back({});
        const auto* id = Verifier::verify(source, maxBindings, output_);
        if (!id || (expected && std::strcmp(id, expected)))
            std::fprintf(stderr,
                "Expected %s; got %s; source: %.*s\n",
                expected ? expected : "rejection",
                id ? id : "success",
                static_cast<int>(source.size()),
                source.data());
        assert(id && (!expected || !std::strcmp(id, expected)) && output_.empty());
        ++rejections_;
    }

    void run()
    {
        valid("@group(0) @binding(0) var<uniform> b:u32;", 4u, 4u);
        valid("@binding(3) @group(2) var<storage,read> b:vec3<f32>;", 12u, 16u);
        valid("struct S { p:vec3f, v:u32, }; @group(0) @binding(0) var<uniform> b:S;", 16u, 16u);
        valid("struct S { p:vec3f, v:vec2f, }; @group(0) @binding(0) var<storage> b:S;", 32u, 16u);
        valid(
            "struct S { @align(32) @size(64) p:u32, q:u32 }; @group(0) @binding(0) var<storage> b:S;",
            96u,
            32u);
        valid("alias N=E; alias E=u32; @group(0) @binding(0) var<uniform> b:N;", 4u, 4u);
        valid("@group(0) @binding(0) var<storage> b:array<array<u32,3>,2>;", 24u, 4u);
        valid("@group(0) @binding(0) var<storage> b:array<vec3f,3>;", 48u, 16u);
        valid("@group(i32(0)) @binding(u32(0)) var<storage> b:array<vec3f,i32(3)>;", 48u, 16u);
        valid("struct S { @size(16) x:f32, }; @group(0) @binding(0) var<uniform> b:array<S,4>;",
            64u,
            4u);
        valid(
            "struct S { x:u32, }; struct B { a:S, @align(16) b:u32, }; @group(0) @binding(0) var<uniform> b:B;",
            32u,
            16u);
        const auto scalar =
            valid("@group(0) @binding(0) var<storage> b:array<u32>;", 4u, 4u, true, 0u, 4u);
        const auto padded =
            valid("struct S { v:vec4f, r:array<u32>, }; @group(0) @binding(0) var<storage> b:S;",
                32u,
                16u,
                true,
                16u,
                4u);
        valid(
            "struct E { a:vec3f, b:vec3f, }; struct S { n:u32, r:array<E>, }; @group(0) @binding(0) var<storage> b:S;",
            48u,
            16u,
            true,
            16u,
            32u);
        valid("@group(0) @binding(0) var<storage,read_write> b:array<atomic<u32>>;",
            4u,
            4u,
            true,
            0u,
            4u);
        valid("enable f16; @group(0) @binding(0) var<storage> b:vec3h;", 6u, 8u);
        // Matrix size cases are parser fixtures, not CPU/shader numerical proof.
        for (std::uint32_t columns = 2u; columns <= 4u; ++columns)
            for (std::uint32_t rows = 2u; rows <= 4u; ++rows) {
                char text[160];
                std::snprintf(text,
                    sizeof(text),
                    "@group(0) @binding(0) var<storage> b:mat%ux%u<f32>;",
                    columns,
                    rows);
                valid(text, columns * (rows == 2u ? 8u : 16u), rows == 2u ? 8u : 16u);
            }
        valid(
            "/* comment /*nested*/ */ @group(0) @binding(0) var<uniform> b:u32;\n"
            "struct Output { @builtin(position) p:vec4f, }; var<private> x:f32=1e-3f; const N=0x10u;"
            "@compute @workgroup_size(1) fn main() { if(true) { let f=1.3e+2f; } /* } */ }",
            4u,
            4u);
        for (std::uint64_t bytes = 4u; bytes < 128u; ++bytes) {
            std::uint64_t count = 99u;
            assert(!scalar.runtimeArrayLength(bytes, count) && count == bytes / 4u);
            if (bytes >= 32u)
                assert(!padded.runtimeArrayLength(bytes, count) && count == (bytes - 16u) / 4u);
            else
                assert(padded.runtimeArrayLength(bytes, count) && count == 0u);
        }
        auto forged = scalar;
        forged.runtimeArrayStrideBytes = 0u;
        std::uint64_t count = 99u;
        assert(forged.runtimeArrayLength(100u, count) && !count);
        for (const auto bad :
            {"@group(0) @binding(0) var<storage> b:bool;",
                "@group(0) @binding(0) var<storage> b:Missing;",
                "@group(0) @binding(0) var<storage> b:array<u32,0>;",
                "@group(0) @binding(0) var<storage> b:array<u32,2u>;",
                "@group(0) @binding(0) var<storage> b:array<u32,2+2>;",
                "const N=2; @group(0) @binding(0) var<storage> b:array<u32,N>;",
                "@group(0) @binding(0) var<storage> b:array<array<u32>,2>;",
                "@group(0) @binding(0) var<uniform> b:array<u32>;",
                "@group(0) @binding(0) var<storage> b:vec2<S>; struct S { x:u32, };",
                "@group(0) @binding(0) var<storage> b:mat2x2<u32>;",
                "@group(0) @binding(0) var<storage> b:atomic<f32>;",
                "@group(0) @binding(0) var<storage> b:atomic<u32>;",
                "@group(0) @binding(0) var<uniform> b:array<u32,3>;",
                "@group(0) @binding(0) var<uniform> b:vec3h;",
                "enable uniform_buffer_standard_layout;",
                "enable f16; enable f16;",
                "@binding(0) var<storage> b:u32;",
                "@group(4294967296) @binding(0) var<storage> b:u32;",
                "@group(0) @binding(0) @binding(0) var<storage> b:u32;",
                "@group(0) @binding(0) var<uniform,read> b:u32;",
                "@group(0) @binding(0) var<storage,write> b:u32;",
                "@group(0) @binding(0) var<storage> b:binding_array<u32,3>;",
                "@group(0) @binding(0) var<storage> b:buffer;",
                "struct S { @size(3) a:u32, }; @group(0) @binding(0) var<storage> b:S;",
                "struct S { @align(3) a:u32, }; @group(0) @binding(0) var<storage> b:S;",
                "struct S { @align(2) a:u32, }; @group(0) @binding(0) var<storage> b:S;",
                "struct S { @size(0) a:u32, }; @group(0) @binding(0) var<storage> b:S;",
                "struct S { @location(0) a:u32, }; @group(0) @binding(0) var<storage> b:S;",
                "struct S { a:u32, a:u32, }; @group(0) @binding(0) var<storage> b:S;",
                "struct S { a:S, }; @group(0) @binding(0) var<storage> b:S;",
                "alias A=B; alias B=A; @group(0) @binding(0) var<storage> b:A;",
                "struct S { a:array<u32>, b:u32 }; @group(0) @binding(0) var<storage> b:S;",
                "struct S { a:array<u32>, }; struct T { a:S }; @group(0) @binding(0) var<storage> b:T;",
                "struct S { @size(16) a:array<u32>, }; @group(0) @binding(0) var<storage> b:S;",
                "struct S { a:u32, }; struct T { a:S, b:u32 }; @group(0) @binding(0) var<uniform> b:T;",
                "struct E { @size(16) x:u32 }; struct S { a:array<E,2> }; struct T { p:u32, a:S }; @group(0) @binding(0) var<uniform> b:T;",
                "struct S {}; @group(0) @binding(0) var<storage> b:S;",
                "/* unclosed",
                "fn main() { /* } */",
                "@group(0) @binding(0) var<storage> b:array<u32,18446744073709551615>;",
                "@group(0) @binding(0) var<storage> b:array<array<array<u32,4294967295>,4294967295>,4294967295>;",
                "@group(0) @binding(0) var<storage> b:u32; @group(0) @binding(0) var<storage> c:u32;",
                "alias A=u32; alias A=f32;",
                "struct u32 { a:u32 };",
                "fn main() { \"string\"; }"})
            reject(bad);
        reject("@group(0) @binding(0) var<storage> b:array<u32,i32(2147483648)>;");
        reject("@group(0) @binding(0) var<storage> b:array<u32,u32(4294967296)>;");
        reject(
            "@group(0) @binding(0) var<storage> b:u32;", "SHADER-WGSL-LAYOUT-BINDING-BUDGET", 0u);
        core::pmr::String deep{&core::defaultCpuMemoryResource()};
        deep = "@group(0) @binding(0) var<storage> b:";
        for (unsigned i = 0u; i < 1500u; ++i)
            deep += "array<";
        deep += "u32";
        for (unsigned i = 0u; i < 1500u; ++i)
            deep += ",1>";
        deep += ";";
        valid(deep, 4u, 4u);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        valid("@group(0) @binding(0) var<uniform> b:u32;", 4u, 4u);
        std::pmr::set_default_resource(previous);
        auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease exhausted(quota, 1u);
            reject("@group(0) @binding(0) var<storage> b:u32;", "SHADER-WGSL-LAYOUT-ALLOCATION");
        }
        assert(quota->liveBytes() == 0u);
        std::printf(
            "WgslBufferDeclarationMinimum/1: scalar/vector/matrix/nested/alias/fixed/runtime/atomic/f16/uniform constraints, %u negatives, deep1500, core PMR and rejection cleanup PASS; no native authority\n",
            rejections_);
    }

    void opaque()
    {
        core::pmr::Vector<Facts> buffers;
        core::pmr::Vector<shader::WgslOpaqueDeclaration> opaque;
        const std::string_view source =
            "@group(0) @binding(3) var<uniform> globals:vec4f;"
            "@binding(1) @group(2) var environment:texture_cube<f32>;"
            "@group(2) @binding(4) var shadow:texture_depth_2d;"
            "@group(2) @binding(5) var comparison:sampler_comparison;"
            "@group(2) @binding(3) var filtering:sampler;"
            "@group(1) @binding(0) var weights:texture_2d_array<f32>;"
            "@group(3) @binding(0) var output:texture_storage_2d<rgba16float,write>;"
            "@group(3) @binding(1) var volume:texture_3d<u32>;"
            "fn main() { /* Fake @group(9) @binding(9) var x:sampler; */ }";
        assert(!Verifier::verifyWithOpaqueDeclarations(source, 8u, buffers, opaque));
        assert(buffers.size() == 1u && opaque.size() == 7u);
        assert(opaque[0].group == 2u && opaque[0].binding == 1u && opaque[0].type == "texture_cube"
            && opaque[0].scalar == "f32");
        assert(opaque[1].type == "texture_depth_2d" && opaque[1].scalar.empty());
        assert(opaque[2].type == "sampler_comparison" && opaque[3].type == "sampler");
        assert(opaque[4].type == "texture_2d_array" && opaque[5].format == "rgba16float"
            && opaque[5].access == "write" && opaque[6].scalar == "u32");
        const auto reject = [&](std::string_view text,
                                const char* expected,
                                std::uint32_t budget = 8u) {
            buffers.push_back({});
            opaque.push_back({});
            const auto* id = Verifier::verifyWithOpaqueDeclarations(text, budget, buffers, opaque);
            if (!id || std::strcmp(id, expected))
                std::fprintf(stderr, "Opaque expected %s, got %s\n", expected, id ? id : "success");
            assert(id && !std::strcmp(id, expected) && buffers.empty() && opaque.empty());
        };
        reject(source, "SHADER-WGSL-LAYOUT-BINDING-BUDGET", 7u);
        reject("@group(0) @binding(0) var a:sampler; @group(0) @binding(0) var b:texture_2d<f32>;",
            "SHADER-WGSL-LAYOUT-DUPLICATE-BINDING");
        reject("@group(0) @binding(0) var a:sampler; @group(0) @binding(0) var<storage> b:u32;",
            "SHADER-WGSL-LAYOUT-DUPLICATE-BINDING");
        reject("@group(0) @binding(0) var<storage> a:u32; @group(0) @binding(0) var b:sampler;",
            "SHADER-WGSL-LAYOUT-DUPLICATE-BINDING");
        reject("@group(0) var a:sampler;", "SHADER-WGSL-LAYOUT-BINDING-ATTRIBUTE");
        reject("var<uniform> a:vec4f;", "SHADER-WGSL-LAYOUT-BINDING-ATTRIBUTE");
        reject("@group(0) @binding(0) var<private> a:sampler;", "SHADER-WGSL-OPAQUE-ADDRESS-SPACE");
        reject("@group(0) @binding(0) var a:binding_array<texture_2d<f32>,2>;",
            "SHADER-WGSL-OPAQUE-TYPE-UNSUPPORTED");
        reject("alias Image=texture_2d<f32>; @group(0) @binding(0) var a:Image;",
            "SHADER-WGSL-LAYOUT-TYPE-UNSUPPORTED");
        reject("@group(0) @binding(0) var a:texture_cube<bool>;", "SHADER-WGSL-OPAQUE-SCALAR");
        reject("@group(0) @binding(0) var a:texture_storage_2d<r32float,unknown>;",
            "SHADER-WGSL-OPAQUE-ACCESS");
        reject(
            "@group(0) @binding(0) var a:sampler_comparison<f32>;", "SHADER-WGSL-LAYOUT-GRAMMAR");
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(!Verifier::verifyWithOpaqueDeclarations(source, 8u, buffers, opaque));
        std::pmr::set_default_resource(previous);
        auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease exhausted(quota, 1u);
            reject(source, "SHADER-WGSL-LAYOUT-ALLOCATION");
        }
        assert(!quota->liveBytes());
        std::printf(
            "WgslOpaqueDeclarations: exact cube/depth/comparison/array/storage/scalar facts, shared binding budgets, cross-kind duplicates and failure rollback PASS; no execution authority\n");
    }

    void opaqueFile(const char* path)
    {
        const auto result = core::readBinaryFile(path, std::filesystem::file_size(path));
        assert(result.success);
        const std::string_view text(
            reinterpret_cast<const char*>(result.bytes.data()), result.bytes.size());
        core::pmr::Vector<Facts> buffers;
        core::pmr::Vector<shader::WgslOpaqueDeclaration> opaque;
        const auto* id = Verifier::verifyWithOpaqueDeclarations(text, 256u, buffers, opaque);
        if (id) {
            std::fprintf(stderr, "Opaque file: %s\n", id);
            std::exit(1);
        }
        std::printf(
            "type: WgslDescriptorDeclarations\ncontractVersion: 1\nstatus: DeclarationOnly\nbuffers:%s\n",
            buffers.empty() ? " []" : "");
        for (const auto& binding : buffers)
            std::printf("  - group: %u\n    binding: %u\n    addressSpace: %u\n",
                binding.group,
                binding.binding,
                static_cast<unsigned>(binding.addressSpace));
        std::printf("opaque:%s\n", opaque.empty() ? " []" : "");
        for (const auto& binding : opaque)
            std::printf(
                "  - group: %u\n    binding: %u\n    type: %.*s\n    scalar: '%.*s'\n    format: '%.*s'\n    access: '%.*s'\n",
                binding.group,
                binding.binding,
                int(binding.type.size()),
                binding.type.data(),
                int(binding.scalar.size()),
                binding.scalar.empty() ? "" : binding.scalar.data(),
                int(binding.format.size()),
                binding.format.empty() ? "" : binding.format.data(),
                int(binding.access.size()),
                binding.access.empty() ? "" : binding.access.data());
    }

    void file(const char* path)
    {
        const auto result = core::readBinaryFile(path, std::filesystem::file_size(path));
        assert(result.success);
        const std::string_view text(
            reinterpret_cast<const char*>(result.bytes.data()), result.bytes.size());
        const auto* id = Verifier::verify(text, static_cast<std::uint32_t>(text.size()), output_);
        if (id) {
            std::fprintf(stderr, "%s: %s\n", path, id);
            std::exit(1);
        }
        std::printf(
            "type: WgslBufferDeclarationMinimums\ncontractVersion: 1\nstatus: DeclarationOnly\nbindings:%s\n",
            output_.empty() ? " []" : "");
        for (const auto& f : output_)
            std::printf(
                "  - group: %u\n    binding: %u\n    addressSpace: %u\n    alignmentBytes: %llu\n    minimumBindingBytes: %llu\n    hasRuntimeArray: %s\n    runtimeArrayOffsetBytes: %llu\n    runtimeArrayStrideBytes: %llu\n",
                f.group,
                f.binding,
                static_cast<unsigned>(f.addressSpace),
                static_cast<unsigned long long>(f.alignmentBytes),
                static_cast<unsigned long long>(f.minimumBindingBytes),
                f.hasRuntimeArray ? "true" : "false",
                static_cast<unsigned long long>(f.runtimeArrayOffsetBytes),
                static_cast<unsigned long long>(f.runtimeArrayStrideBytes));
    }

private:
    core::pmr::Vector<Facts> output_{&core::defaultCpuMemoryResource()};
    unsigned rejections_ = 0u;
};
} // namespace

int main(int argc, char** argv)
{
    LayoutFixture fixture;
    if (argc == 3 && std::strcmp(argv[1], "--opaque-file") == 0)
        fixture.opaqueFile(argv[2]);
    else if (argc == 2)
        fixture.file(argv[1]);
    else {
        assert(argc == 1);
        fixture.run();
        fixture.opaque();
    }
}
