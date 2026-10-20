#pragma once
#include "core/file_io.h"
#include "cpu_quota_factory_fixture.h"
#include "shader/runtime_array_length_operations.h"
#include "shader/shader_artifact_files.h"
#include "spirv_binary_mutation_fixture.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace arp::test {
class RuntimeArrayLengthOperationsFixture final {
    using Verifier = shader::RuntimeArrayLengthOperations;

public:
    static void real(
        const char* binaryPath, const char* targetPath, bool readWrite = false, bool bytes = false)
    {
        auto binary = core::readBinaryFile(binaryPath, std::filesystem::file_size(binaryPath));
        assert(binary.success);
        auto target = core::readBinaryFile(targetPath, std::filesystem::file_size(targetPath));
        assert(target.success);
        auto code = core::pmr::String(
            shader::shaderArtifactText(target.bytes), &core::defaultCpuMemoryResource());
        shader::ShaderReflection reflection;
        for (std::uint32_t slot = 0u; slot < 2u; ++slot) {
            shader::ShaderBinding b;
            b.set = 0u;
            b.binding = slot;
            b.arrayCount = 1u;
            b.type = slot || readWrite ? shader::BindingType::StorageBuffer
                                       : shader::BindingType::ReadOnlyStorageBuffer;
            reflection.bindings.push_back(b);
            shader::ShaderWebGpuBindingSpaceBinding physical;
            physical.active = true;
            physical.set = 0u;
            physical.binding = slot;
            physical.group = 0u;
            physical.webGpuBinding = slot;
            physical.capacity = 1u;
            physical.arrayElement = 0u;
            physical.physicalType = b.type;
            reflection.webGpuBindingSpace.push_back(physical);
        }
        shader::TargetBufferMemoryLoadRequest::BindingPlan plans[]{
            {0u,
                0u,
                shader::SpirvWgslBufferMemoryLowering::RootForm::StorageRuntimeArray,
                {nullptr, 0u}},
            {0u,
                1u,
                shader::SpirvWgslBufferMemoryLowering::RootForm::StorageRuntimeArray,
                {nullptr, 0u}}};
        shader::TargetBufferMemoryLoadRequest request{
            1u, {128u, 128u, 3u}, {128u, 128u, 3u}, 128u, 0u, plans, 2u};
        core::pmr::Vector<Verifier::Facts> facts{&core::defaultCpuMemoryResource()};
        const auto verify = [&](const core::ByteBuffer& data,
                                core::StringView wgsl,
                                Verifier::Budget budget = Verifier::Budget{4u, 256u}) {
            return Verifier::verify(data, wgsl, reflection, 2u, request, budget, facts);
        };
        const auto success = [&] {
            const auto* id = verify(binary.bytes, code);
            if (id)
                std::fprintf(stderr, "Runtime length: %s\n", id);
            assert(!id);
            assert(facts.size() == 1u && facts[0].set == 0u && facts[0].binding == 0u
                && facts[0].group == 0u && facts[0].targetBinding == 0u && facts[0].sourceResultId
                && !facts[0].sourceMember && !facts[0].runtimeOffsetBytes
                && facts[0].runtimeStrideBytes == (bytes ? 4u : 16u)
                && core::StringView(code).substr(facts[0].targetByteOffset, 11u) == "arrayLength");
        };
        const auto reject = [&](const core::ByteBuffer& data,
                                core::StringView wgsl,
                                const char* expected,
                                Verifier::Budget budget = Verifier::Budget{4u, 256u}) {
            facts.push_back({});
            const auto* id = verify(data, wgsl, budget);
            if (!id || std::strcmp(id, expected))
                std::fprintf(
                    stderr, "Runtime length expected %s got %s\n", expected, id ? id : "success");
            assert(id && !std::strcmp(id, expected) && facts.empty());
        };
        success();
        const auto replace = [&](core::StringView from, core::StringView to) {
            auto text = core::pmr::String(code, &core::defaultCpuMemoryResource());
            const auto at = text.find(from);
            assert(at != text.npos);
            text.replace(at, from.size(), to);
            return text;
        };
        reject(binary.bytes,
            replace("arrayLength(&Input_0)", "arrayLength(&Output_0)"),
            "SHADER-RUNTIME-LENGTH-TARGET-COVERAGE");
        reject(binary.bytes,
            replace("arrayLength(&Input_0)", "arrayLength(Input_0)"),
            "SHADER-RUNTIME-LENGTH-TARGET-OPERATION");
        reject(binary.bytes,
            replace("arrayLength(&Input_0)", "arrayLength(&Input_0.member)"),
            "SHADER-RUNTIME-LENGTH-TARGET-OPERATION");
        reject(binary.bytes,
            replace("arrayLength(&Input_0)", "u32(2)"),
            "SHADER-RUNTIME-LENGTH-TARGET-COVERAGE");
        reject(binary.bytes,
            replace("arrayLength(&Input_0)", "arrayLength(&Input_0)+arrayLength(&Input_0)"),
            "SHADER-RUNTIME-LENGTH-TARGET-COVERAGE");
        reject(binary.bytes,
            replace("fn computeMain()", "fn computeMain(Input_0:u32)"),
            "SHADER-RUNTIME-LENGTH-SHADOW");
        reject(binary.bytes,
            replace("fn computeMain()", "fn arrayLength()"),
            "SHADER-RUNTIME-LENGTH-SHADOW");
        reject(binary.bytes, replace("{", "{ let Input_0=u32(0);"), "SHADER-RUNTIME-LENGTH-SHADOW");
        reject(binary.bytes,
            replace("{", "{ var<function> Input_0:u32;"),
            "SHADER-RUNTIME-LENGTH-SHADOW");
        reject(binary.bytes, code, "SHADER-RUNTIME-LENGTH-BUDGET", {0u, 256u});
        reject(binary.bytes, code, "SHADER-RUNTIME-LENGTH-TOKEN-BUDGET", {4u, 1u});
        reject(binary.bytes,
            replace("arrayLength(&Input_0)", "arrayLength(&Input_0)+arrayLength(&Input_0)"),
            "SHADER-RUNTIME-LENGTH-OPERATION-BUDGET",
            {1u, 256u});
        SpirvBinaryMutationFixture mutations(binary.bytes);
        const auto operation = mutations.locate(68u);
        mutations.word(operation + 4u, 1u);
        reject(mutations.bytes(), code, "SHADER-RUNTIME-LENGTH-FINAL-MEMBER");
        mutations.reset();
        const auto outputVariable = mutations.locate(59u, -1, 0u, 1u);
        mutations.word(operation + 3u, mutations.operand(outputVariable, 1u));
        reject(mutations.bytes(), code, "SHADER-RUNTIME-LENGTH-TARGET-COVERAGE");
        mutations.reset();
        mutations.word(operation + 3u, mutations.operand(operation, 1u));
        reject(mutations.bytes(), code, "SHADER-RUNTIME-LENGTH-DIRECT-ROOT");
        mutations.reset();
        mutations.patch(21u, 2u, 1u, 1, 32u);
        reject(mutations.bytes(), code, "SHADER-BUFFER-LOWERING-MEMORY-MISMATCH");
        mutations.reset();
        const auto signedType = mutations.locate(21u, 2, 1u);
        mutations.word(operation + 1u, mutations.operand(signedType, 0u));
        reject(mutations.bytes(), code, "SHADER-RUNTIME-LENGTH-RESULT-TYPE");
        mutations.reset();
        const auto sourceVariable = mutations.locate(59u, 1, mutations.operand(operation, 2u));
        const auto sourcePointer = mutations.locate(32u, 0, mutations.operand(sourceVariable, 0u));
        const auto sourceBlockId = mutations.operand(sourcePointer, 2u);
        const auto sourceBlock = mutations.locate(30u, 0, sourceBlockId);
        const auto runtimeArrayType = mutations.operand(sourceBlock, 1u);
        const auto uintType = mutations.operand(operation, 0u);
        mutations.word(operation + 4u, 1u);
        mutations.replaceInstruction(sourceBlock, 30u, {sourceBlockId, uintType, runtimeArrayType});
        mutations.insertInstruction(mutations.locate(19u), 72u, {sourceBlockId, 1u, 35u, 16u});
        const auto rootDeclaration =
            core::StringView(readWrite ? "var<storage, read_write> Input_0 : array<vec3<f32>>;"
                                       : "var<storage, read> Input_0 : array<vec3<f32>>;");
        const auto bytesRoot =
            core::StringView(readWrite ? "var<storage, read_write> Input_0 : array<u32>;"
                                       : "var<storage, read> Input_0 : array<u32>;");
        auto prefix = replace(bytes ? bytesRoot : rootDeclaration,
            readWrite ? "var<storage, read_write> Input_0 : Prefixed;"
                      : "var<storage, read> Input_0 : Prefixed;");
        prefix.insert(0u,
            bytes
                ? "alias Tail=array<u32>; struct Prefixed { header:u32, @align(16) tail:Tail };\n"
                : "alias Tail=array<vec3<f32>>; struct Prefixed { header:u32, @align(16) tail:Tail };\n");
        constexpr core::StringView directCall = "arrayLength(&Input_0)";
        const auto pointer = prefix.find(directCall);
        assert(pointer != prefix.npos);
        prefix.replace(pointer, directCall.size(), "arrayLength(&Input_0.tail)");
        plans[0].form = shader::SpirvWgslBufferMemoryLowering::RootForm::BlockStruct;
        assert(!verify(mutations.bytes(), prefix) && facts.size() == 1u
            && facts[0].runtimeOffsetBytes == 16u && facts[0].sourceMember == 1u);
        auto wrongMember = core::pmr::String(prefix, &core::defaultCpuMemoryResource());
        wrongMember.replace(wrongMember.find("Input_0.tail"), 12u, "Input_0.header");
        reject(mutations.bytes(), wrongMember, "SHADER-RUNTIME-LENGTH-TARGET-MEMBER");
        plans[0].form = shader::SpirvWgslBufferMemoryLowering::RootForm::StorageRuntimeArray;
        mutations.reset();
        auto comments = replace("arrayLength(&Input_0)",
            "/* arrayLength(&Output_0) */ arrayLength( /* root */ &Input_0)");
        assert(!verify(binary.bytes, comments) && facts.size() == 1u);
        core::pmr::Vector<shader::WgslBufferRuntimeReference> refs{
            &core::defaultCpuMemoryResource()};
        assert(!shader::WgslBufferDeclarationLayoutVerifier::verifyWithRuntimeReferences(
            "alias T=array<vec3f>; struct S { prefix:u32, @align(16) tail:T }; alias B=S; @group(0) @binding(2) var<storage> Original:B;",
            1u,
            refs));
        assert(refs.size() == 1u && refs[0].variable == "Original"
            && refs[0].runtimeMember == "tail" && refs[0].declaration.runtimeArrayOffsetBytes == 16u
            && refs[0].declaration.runtimeArrayStrideBytes == 16u);
        assert(shader::WgslBufferDeclarationLayoutVerifier::verifyWithRuntimeReferences(
                   "@group(0) @binding(2) var<storage> Missing:Unknown;", 1u, refs)
            && refs.empty());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        success();
        std::pmr::set_default_resource(previous);
        facts.clear();
        facts.shrink_to_fit();
        auto quota = core::makeShared<core::CpuMemoryQuota>(64u * 1024u);
        const auto factory = [&] {
            core::CpuMemoryQuotaScope scope(quota);
            core::pmr::Vector<Verifier::Facts> output{&core::defaultCpuMemoryResource()};
            const auto* id =
                Verifier::verify(binary.bytes, code, reflection, 2u, request, {4u, 256u}, output);
            return !id && output.size() == 1u;
        };
        CpuQuotaFactoryFixture quotaFixture(quota);
        const auto peak = quotaFixture.measure(factory);
        quotaFixture.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            assert(!factory());
        }
        assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_RUNTIME_LENGTH_OPERATIONS_OK real-slang direct-roots full-calls wrong-root-member-shadow-budget-cleared hostile-pmr quota-cleanup no-native-bound-authority");
    }
};
} // namespace arp::test
