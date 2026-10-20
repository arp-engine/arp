#pragma once
#include "original_target_memory_fixture.h"

namespace arp::test {
// Authored shadow-depth ABI. Graph indices are read from the actual sealed
// declaration, then passed explicitly; this is never a runtime policy selector.
class OriginalTargetPushMatrixMemoryFixture final {
    using Artifact = shader::SingleEntryShaderArtifact;
    using Graph = shader::BufferDeclarationTypeLayouts;
    using Memory = shader::TargetBufferMemoryDeclarations;
    using Lowering = shader::SpirvWgslBufferMemoryLowering;

public:
    static void verifyProductionBinary(
        const std::filesystem::path& binaryPath, const std::filesystem::path& reflectionPath)
    {
        shader::ShaderArtifactInput input(1024u * 1024u, 2u * 1024u * 1024u);
        const auto binary = input.read(binaryPath);
        const auto yaml = input.read(reflectionPath);
        auto reflected = shader::ShaderReflectionCache::loadFromYaml(
            core::pmr::String(shader::shaderArtifactText(yaml), &core::defaultCpuMemoryResource()));
        assert(reflected.success && reflected.module.reflection.pushConstantSizeBytes == 224u);
        Graph source;
        core::pmr::Vector<shader::SpirvBufferDeclarationLayout> buffers{
            &core::defaultCpuMemoryResource()};
        shader::SpirvPushConstantDeclarationLayout push;
        assert(!shader::SpirvBufferDeclarationLayoutVerifier::verifyWithPushConstants(binary.data(),
            binary.size(),
            16u,
            TargetMemoryRequestFixture::graphBudget,
            buffers,
            source,
            push));
        const auto& block = source.nodes()[source.roots()[push.rootIndex].type];
        shader::PushBinaryMemoryPlan::Member rule{
            0u, source.members()[block.memberBegin].type, true, true};
        shader::PushBinaryMemoryPlan plan{1u, &rule, 1u, 1u};
        const auto* id = Memory::pushBinaryAbi(source, push, reflected.module.reflection, &plan);
        if (id)
            std::fprintf(stderr, "WOCC push: %s\n", id);
        assert(!id);
        assert(Memory::pushBinaryAbi(source, push, reflected.module.reflection));
        std::puts(
            "WOCC_SHADOW_PUSH_BINARY_MEMORY1_OK actual224 explicit-wrapper matrix-stride declaration-only");
    }

    static void prepare(const core::ByteBuffer& binary,
        core::StringView wgsl,
        Graph& source,
        Graph& target,
        shader::SpirvPushConstantDeclarationLayout& push,
        shader::PushBinaryMemoryPlan::Member& rule,
        Lowering::MatrixColumnValue& matrix)
    {
        core::pmr::Vector<shader::SpirvBufferDeclarationLayout> buffers{
            &core::defaultCpuMemoryResource()};
        core::pmr::Vector<shader::WgslBufferDeclarationLayout> targets{
            &core::defaultCpuMemoryResource()};
        assert(!shader::SpirvBufferDeclarationLayoutVerifier::verifyWithPushConstants(binary.data(),
            binary.size(),
            16u,
            TargetMemoryRequestFixture::graphBudget,
            buffers,
            source,
            push));
        assert(push.present && buffers.empty() && push.occupiedExtentBytes == 224u);
        assert(!shader::WgslBufferDeclarationLayoutVerifier::verifyWithTypes(
            wgsl, 16u, TargetMemoryRequestFixture::graphBudget, targets, target));
        assert(target.roots().size() == 1u);
        const auto sourceType =
            source.members()[source.nodes()[source.roots()[push.rootIndex].type].memberBegin].type;
        const auto targetType =
            target.members()[target.nodes()[target.roots()[0].type].memberBegin].type;
        assert(source.nodes()[sourceType].kind == Graph::Kind::Struct
            && target.nodes()[targetType].kind == Graph::Kind::Struct);
        const auto sourceArray = source.members()[source.nodes()[sourceType].memberBegin].type;
        const auto targetArray = target.members()[target.nodes()[targetType].memberBegin].type;
        assert(source.nodes()[sourceArray].count == 3u && target.nodes()[targetArray].count == 3u);
        matrix = {source.nodes()[sourceArray].child,
            target.nodes()[targetArray].child,
            Lowering::MatrixTargetForm::ColumnArrayStruct};
        rule = {0u, sourceType, true, true};
    }

    static void verifyModule(const std::filesystem::path& path)
    {
        using Module = shader::GraphicsShaderModuleArtifact;
        auto budget = OriginalTargetMemoryFixture::moduleBudget();
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        auto bare = Module::load(path, budget, quota, diagnostic);
        assert(bare && bare->module().reflection.pushConstantSizeBytes == 224u);
        std::array<Graph, 3u> sources, targets;
        std::array<shader::SpirvPushConstantDeclarationLayout, 3u> pushes;
        std::array<shader::PushBinaryMemoryPlan::Member, 3u> rules;
        std::array<shader::PushBinaryMemoryPlan, 3u> plans;
        std::array<Lowering::MatrixColumnValue, 3u> matrices;
        const auto prep = [&](std::size_t i, const auto& owner) {
            prepare(owner->bytes(shader::ShaderArtifactRole::Spirv),
                shader::shaderArtifactText(owner->bytes(shader::ShaderArtifactRole::Wgsl)),
                sources[i],
                targets[i],
                pushes[i],
                rules[i],
                matrices[i]);
            plans[i] = {1u, &rules[i], 1u, 1u};
        };
        prep(0u, bare->vertex());
        prep(1u, bare->fragment());
        prep(2u, bare);
        std::array<shader::TargetBufferMemoryLoadRequest, 2u> children;
        for (std::size_t i = 0; i < children.size(); ++i) {
            children[i] = {2u,
                TargetMemoryRequestFixture::graphBudget,
                TargetMemoryRequestFixture::graphBudget,
                512u,
                0u};
            children[i].push = true;
            children[i].pushArrays = {nullptr, 0u, &matrices[i], 1u, 2u};
            children[i].maxMatrixRules = 1u;
            children[i].pushBinary = &plans[i];
        }
        shader::GraphicsTargetMemoryLoadRequest request{2u,
            children[0],
            children[1],
            TargetMemoryRequestFixture::graphBudget,
            TargetMemoryRequestFixture::graphBudget,
            512u,
            &plans[2]};
        bare.reset();
        assert(!quota->liveBytes());
        budget.targetMemory = &request;
        auto value = Module::load(path, budget, quota, diagnostic);
        if (!value)
            std::fprintf(stderr, "Matrix module: %s\n", diagnostic.c_str());
        assert(value && !value->nativeExecutionProven());
        shader::TargetPushMemoryLayout facts;
        assert(!Module::originalTargetPushMemory(value, facts) && facts.sourceOccupiedBytes == 224u
            && facts.targetDeclarationBytes == 224u);
        const auto baseline = quota->liveBytes();
        const auto reject = [&](const char* expected) {
            auto failed = Module::load(path, budget, quota, diagnostic);
            if (failed || diagnostic.find(expected) == diagnostic.npos)
                std::fprintf(stderr, "Module expected %s, got %s\n", expected, diagnostic.c_str());
            assert(!failed && diagnostic.find(expected) != diagnostic.npos
                && quota->liveBytes() == baseline);
        };
        request.contractVersion = 1u;
        reject("TARGET-MEMORY-BUDGET");
        request.contractVersion = 2u;
        request.combinedPushBinary = nullptr;
        reject("PUSH-BINARY-ARRAY");
        request.combinedPushBinary = &plans[2];
        plans[2].maxMembers = 0u;
        reject("PUSH-BINARY-PLAN-BUDGET");
        plans[2].maxMembers = 1u;
        request.fragment.pushBinary = nullptr;
        reject("PUSH-BINARY-ARRAY");
        request.fragment.pushBinary = &plans[1];
        request.fragment.pushArrays.matrixCount = 0u;
        reject("MATRIX-PROOF");
        request.fragment.pushArrays.matrixCount = 1u;
        rules[2].sourceType = Graph::absent;
        reject("PUSH-BINARY-PLAN-FRAMING");
        // Retained original facts cannot observe later mutations of any plan.
        assert(!Module::originalTargetPushMemory(value, facts)
            && facts.targetDeclarationBytes == 224u);
        value.reset();
        assert(!quota->liveBytes());
        std::puts(
            "ORIGINAL_GRAPHICS_PUSH_MATRIX_MEMORY2_OK independent-stages combined224 frozen-proof retirement0 no-execution-grant");
    }

    static void verify(const std::filesystem::path& path)
    {
        auto budget = OriginalTargetMemoryFixture::entryBudget();
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        auto bare = Artifact::load(path, budget, quota, diagnostic);
        if (!bare)
            std::fprintf(stderr, "%s\n", diagnostic.c_str());
        assert(bare && bare->module().reflection.pushConstantSizeBytes == 224u
            && bare->module().reflection.bindings.empty());
        Graph source, target;
        shader::SpirvPushConstantDeclarationLayout push;
        shader::PushBinaryMemoryPlan::Member rule;
        Lowering::MatrixColumnValue matrix;
        prepare(bare->bytes(shader::ShaderArtifactRole::Spirv),
            shader::shaderArtifactText(bare->bytes(shader::ShaderArtifactRole::Wgsl)),
            source,
            target,
            push,
            rule,
            matrix);
        const auto sourceType = rule.sourceType;
        const auto sourceArray = source.members()[source.nodes()[sourceType].memberBegin].type;
        shader::PushBinaryMemoryPlan plan{1u, &rule, 1u, 1u};
        auto reflection = bare->module().reflection;
        const auto check = [&](const char* expected) {
            const auto* id = Memory::pushBinaryAbi(source, push, reflection, &plan);
            if (!id || std::strcmp(id, expected))
                std::fprintf(
                    stderr, "Push plan expected %s, got %s\n", expected, id ? id : "success");
            assert(id && !std::strcmp(id, expected));
        };
        assert(Memory::pushBinaryAbi(source, push, reflection));
        assert(!Memory::pushBinaryAbi(source, push, reflection, &plan));
        plan.contractVersion = 2u;
        check("SHADER-ENTRY-TARGET-PUSH-BINARY-PLAN-VERSION");
        plan.contractVersion = 1u;
        plan.maxMembers = 0u;
        check("SHADER-ENTRY-TARGET-PUSH-BINARY-PLAN-BUDGET");
        plan.maxMembers = 1u;
        rule.sourceType = Graph::absent;
        check("SHADER-ENTRY-TARGET-PUSH-BINARY-PLAN-FRAMING");
        rule.sourceType = sourceArray;
        check("SHADER-ENTRY-TARGET-PUSH-BINARY-PLAN-TYPE");
        rule.sourceType = sourceType;
        rule.slangRowMajorFloat4x4 = false;
        check("SHADER-ENTRY-TARGET-PUSH-BINARY-MATRIX");
        rule.slangRowMajorFloat4x4 = true;
        reflection.pushConstantMembers[0].rowMajor = false;
        check("SHADER-ENTRY-TARGET-PUSH-BINARY-MATRIX-RELATION");
        reflection = bare->module().reflection;
        reflection.pushConstantMembers[0].decorations = 4u;
        check("SHADER-ENTRY-TARGET-PUSH-BINARY-MATRIX-RELATION");
        reflection = bare->module().reflection;
        reflection.pushConstantMembers[0].arrayStride = 16u;
        check("SHADER-ENTRY-TARGET-PUSH-BINARY-ARRAY");
        reflection = bare->module().reflection;
        reflection.pushConstantMembers[0].size = 128u;
        check("SHADER-ENTRY-TARGET-PUSH-BINARY-MEMBER-SIZE");
        reflection = bare->module().reflection;
        std::array duplicate{rule, rule};
        plan.members = duplicate.data();
        plan.count = plan.maxMembers = 2u;
        check("SHADER-ENTRY-TARGET-PUSH-BINARY-PLAN-DUPLICATE");
        plan = {1u, &rule, 1u, 1u};
        shader::TargetBufferMemoryLoadRequest request{2u,
            TargetMemoryRequestFixture::graphBudget,
            TargetMemoryRequestFixture::graphBudget,
            512u,
            0u};
        request.push = true;
        request.pushArrays = {nullptr, 0u, &matrix, 1u, 2u};
        request.maxMatrixRules = 1u;
        request.pushBinary = &plan;
        bare.reset();
        assert(!quota->liveBytes());
        budget.targetMemory = &request;
        auto value = Artifact::load(path, budget, quota, diagnostic);
        if (!value)
            std::fprintf(stderr, "Push matrix artifact: %s\n", diagnostic.c_str());
        assert(value && !value->nativeExecutionProven());
        shader::TargetPushMemoryLayout facts;
        assert(!Artifact::originalTargetPushMemory(value, facts)
            && facts.sourceOccupiedBytes == 224u && facts.targetDeclarationBytes == 224u);
        OriginalTargetMemoryFixture::hot(value);
        const auto live = quota->liveBytes();
        request.pushBinary = nullptr;
        auto failed = Artifact::load(path, budget, quota, diagnostic);
        assert(!failed && diagnostic.find("PUSH-BINARY-ARRAY") != diagnostic.npos
            && quota->liveBytes() == live);
        request.pushBinary = &plan;
        request.contractVersion = 1u;
        request.maxMatrixRules = 0u;
        request.pushArrays = {nullptr, 0u};
        failed = Artifact::load(path, budget, quota, diagnostic);
        assert(!failed && diagnostic.find("PUSH-BINARY-REQUEST-VERSION") != diagnostic.npos
            && quota->liveBytes() == live);
        rule.sourceType = Graph::absent;
        plan.members = nullptr;
        assert(
            !Artifact::originalTargetPushMemory(value, facts) && facts.sourceOccupiedBytes == 224u);
        value.reset();
        assert(!quota->liveBytes());
        std::puts(
            "ORIGINAL_PUSH_MATRIX_MEMORY1_OK 224-byte frozen-proof hot0 retirement0 no-execution-grant");
    }
};
} // namespace arp::test
