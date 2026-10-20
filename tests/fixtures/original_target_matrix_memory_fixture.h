#pragma once
#include "original_target_memory_fixture.h"

namespace arp::test {
// Explicit policy for buffer_matrix_probe only. Never a production rule selector.
class OriginalTargetMatrixMemoryFixture final {
    using Artifact = shader::SingleEntryShaderArtifact;
    using Request = shader::TargetBufferMemoryLoadRequest;
    using Graph = shader::BufferDeclarationTypeLayouts;
    using Lowering = shader::SpirvWgslBufferMemoryLowering;

public:
    static void verify(const std::filesystem::path& path)
    {
        auto budget = OriginalTargetMemoryFixture::entryBudget();
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        auto bare = Artifact::load(path, budget, quota, diagnostic);
        assert(bare);
        assert(bare->entry() == "computeMain");
        const auto sourceText =
            shader::shaderArtifactText(bare->bytes(shader::ShaderArtifactRole::Source));
        assert(sourceText.find("row_major float4x4") == sourceText.npos
            && sourceText.find("    float4x4 current;") != sourceText.npos
            && sourceText.find("    float4x4 previous;") != sourceText.npos
            && sourceText.find("    float4x4 palette[3];") != sourceText.npos
            && !bare->module().reflection.pushConstantSizeBytes);
        Graph source, target;
        core::pmr::Vector<shader::SpirvBufferDeclarationLayout> binaryFacts;
        core::pmr::Vector<shader::WgslBufferDeclarationLayout> targetFacts;
        const auto& binary = bare->bytes(shader::ShaderArtifactRole::Spirv);
        assert(!shader::SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(binary.data(),
            binary.size(),
            2u,
            TargetMemoryRequestFixture::graphBudget,
            binaryFacts,
            source));
        assert(!shader::WgslBufferDeclarationLayoutVerifier::verifyWithTypes(
            shader::shaderArtifactText(bare->bytes(shader::ShaderArtifactRole::Wgsl)),
            2u,
            TargetMemoryRequestFixture::graphBudget,
            targetFacts,
            target));
        // The fixture declares one shared f32 4x4 matrix type. Its WGSL physical
        // representation is exactly one struct wrapping four vec4 columns.
        Lowering::MatrixColumnValue rule;
        rule.targetForm = Lowering::MatrixTargetForm::ColumnArrayStruct;
        for (std::size_t i = 0u; i < source.nodes().size(); ++i)
            if (source.nodes()[i].kind == Graph::Kind::Matrix) {
                const auto& matrix = source.nodes()[i];
                assert(
                    matrix.rows == 4u && matrix.columns == 4u && rule.sourceType == Graph::absent);
                rule.sourceType = i;
            }
        for (std::size_t i = 0u; i < target.nodes().size(); ++i) {
            const auto& node = target.nodes()[i];
            if (node.kind != Graph::Kind::Struct || node.memberCount != 1u)
                continue;
            const auto& array = target.nodes()[target.members()[node.memberBegin].type];
            if (array.kind != Graph::Kind::FixedArray || array.count != 4u)
                continue;
            const auto& vector = target.nodes()[array.child];
            if (vector.kind != Graph::Kind::Vector || vector.count != 4u)
                continue;
            assert(rule.targetType == Graph::absent);
            rule.targetType = i;
        }
        assert(rule.sourceType != Graph::absent && rule.targetType != Graph::absent);
        std::array<Request::BindingPlan, 2u> plans{
            {{0u, 0u, Lowering::RootForm::BlockStruct, {nullptr, 0u, &rule, 1u, 2u}},
                {0u, 1u, Lowering::RootForm::StorageRuntimeArray, {nullptr, 0u}}}};
        Request request{2u,
            TargetMemoryRequestFixture::graphBudget,
            TargetMemoryRequestFixture::graphBudget,
            512u,
            0u};
        request.bindings = plans.data();
        request.bindingCount = plans.size();
        request.maxMatrixRules = 1u;
        bare.reset();
        assert(!quota->liveBytes());
        budget.targetMemory = &request;
        const auto factory = [&] {
            return Artifact::load(path, budget, quota, diagnostic);
        };
        auto value = factory();
        if (!value)
            std::fprintf(stderr, "Matrix artifact request2: %s\n", diagnostic.c_str());
        assert(value);
        OriginalTargetMemoryFixture::hot(value);
        shader::TargetBufferMemoryLayout facts;
        assert(!Artifact::originalTargetBufferMemory(value, 0u, 0u, facts)
            && facts.source.staticOccupiedExtentBytes == 320u
            && facts.target.minimumBindingBytes == 320u);
        assert(!value->nativeExecutionProven());
        const auto before = quota->liveBytes();
        request.contractVersion = 1u;
        request.maxMatrixRules = 0u;
        plans[0].arrays = {nullptr, 0u};
        auto legacy = factory();
        assert(legacy);
        facts.target.minimumBindingBytes = UINT64_MAX;
        const auto* legacyId = Artifact::originalTargetBufferMemory(legacy, 0u, 0u, facts);
        assert(legacyId && !std::strcmp(legacyId, "SHADER-BUFFER-LOWERING-MATRIX-PROOF")
            && !facts.target.minimumBindingBytes && !legacy->nativeExecutionProven());
        legacy.reset();
        assert(quota->liveBytes() == before);
        request.contractVersion = 2u;
        request.maxMatrixRules = 1u;
        plans[0].arrays = {nullptr, 0u, &rule, 1u, 2u};
        const auto rejects = [&](const char* expected) {
            auto failed = factory();
            if (failed || diagnostic.find(expected) == diagnostic.npos)
                std::fprintf(
                    stderr, "Matrix request expected %s, got %s\n", expected, diagnostic.c_str());
            assert(!failed && diagnostic.find(expected) != diagnostic.npos
                && quota->liveBytes() == before);
        };
        request.maxMatrixRules = 0u;
        rejects("MATRIX-RULE-BUDGET");
        request.maxMatrixRules = 1u;
        request.contractVersion = 1u;
        rejects("MATRIX-REQUEST-VERSION");
        request.contractVersion = 2u;
        plans[0].arrays.contractVersion = 1u;
        rejects("MATRIX-REQUEST-VERSION");
        plans[0].arrays.contractVersion = 2u;
        plans[0].arrays.matrixCount = 0u;
        rejects("MATRIX-PROOF");
        plans[0].arrays.matrixCount = 1u;
        plans[0].arrays.matrixColumnValues = nullptr;
        rejects("MATRIX-RULE-BUDGET");
        plans[0].arrays.matrixColumnValues = &rule;
        const auto validRule = rule;
        rule.sourceType = Graph::absent;
        rejects("MATRIX-PLAN-FRAMING");
        rule = validRule;
        std::array duplicates{rule, rule};
        plans[0].arrays.matrixColumnValues = duplicates.data();
        plans[0].arrays.matrixCount = 2u;
        request.maxMatrixRules = 2u;
        rejects("PLAN-DUPLICATE");
        plans[0].arrays = {nullptr, 0u, &rule, 1u, 2u};
        request.maxMatrixRules = 1u;
        plans[1].arrays = {nullptr, 0u, &rule, 1u, 2u};
        rejects("MATRIX-RULE-BUDGET");
        request.maxMatrixRules = 2u;
        rejects("PLAN-UNUSED");
        plans[1].arrays = {nullptr, 0u};
        request.maxMatrixRules = 1u;
        request.pushArrays = {nullptr, 0u, &rule, 1u, 2u};
        request.maxMatrixRules = 2u;
        rejects("PLAN-COVERAGE");
        request.pushArrays = {nullptr, 0u};
        request.maxMatrixRules = 1u;
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - before);
            assert(!factory() && quota->liveBytes() == quota->limitBytes());
        }
        assert(quota->liveBytes() == before);
        // Caller mutations cannot change the original's retained verified facts.
        rule.sourceType = Graph::absent;
        request.bindings = nullptr;
        request.bindingCount = SIZE_MAX;
        OriginalTargetMemoryFixture::hot(value);
        value.reset();
        assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_ORIGINAL_MATRIX_MEMORY2_OK explicit-budget consumed-rules frozen-owner hot0 no-execution-grant");
    }
};
} // namespace arp::test
