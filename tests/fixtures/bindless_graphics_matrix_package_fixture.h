#pragma once
#include "core/file_io.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "shader/shader_artifact_files.h"

#include <array>
#include <cassert>

namespace arp::test {
// Explicit per-entry proof for this authored fixture, using the actual source
// and target graph identities. No production rule inference or fallback.
class BindlessGraphicsMatrixPackageFixture final {
    using Graph = shader::BufferDeclarationTypeLayouts;
    using Lowering = shader::SpirvWgslBufferMemoryLowering;
    using Request = shader::TargetBufferMemoryLoadRequest;
    static constexpr Graph::Budget graphBudget{512u, 512u, 16u};
    std::array<Lowering::MatrixColumnValue, 3u> rules_;
    std::array<std::array<Request::BindingPlan, 2u>, 3u> bindings_;
    std::array<rhi::BindlessPackageMemoryLoadRequest::Entry, 3u> entries_;

public:
    rhi::BindlessPackageMemoryLoadRequest request;

    explicit BindlessGraphicsMatrixPackageFixture(const std::filesystem::path& directory,
        const char* family = "GraphicsMatrixOrdinary",
        std::array<const char*, 3u> declarations = {
            "    float4x4 current;", "    float4x4 previous;", "    float4x4 palette[3];"})
    {
        constexpr std::array<const char*, 3u> names{"vertexMain", "cleanFragment", "faultFragment"};
        for (std::size_t stage = 0u; stage < names.size(); ++stage) {
            const auto stem = directory / (std::string(family) + ".emulated." + names[stage]);
            const auto binary = core::readBinaryFile(stem.string() + ".spv", 1024u * 1024u);
            const auto wgsl = core::readBinaryFile(stem.string() + ".wgsl", 1024u * 1024u);
            const auto slang = core::readBinaryFile(stem.string() + ".slang", 1024u * 1024u);
            assert(binary.success && wgsl.success && slang.success);
            const auto text = shader::shaderArtifactText(slang.bytes);
            for (const auto* declaration : declarations)
                assert(declaration && text.find(declaration) != text.npos);
            assert(text.find("row_major float4x4") == text.npos
                && text.find("column_major float4x4") == text.npos);
            Graph source, target;
            core::pmr::Vector<shader::SpirvBufferDeclarationLayout> sourceFacts;
            core::pmr::Vector<shader::WgslBufferDeclarationLayout> targetFacts;
            assert(!shader::SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(
                binary.bytes.data(), binary.bytes.size(), 16u, graphBudget, sourceFacts, source));
            assert(!shader::WgslBufferDeclarationLayoutVerifier::verifyWithTypes(
                shader::shaderArtifactText(wgsl.bytes), 16u, graphBudget, targetFacts, target));
            auto& rule = rules_[stage];
            rule.targetForm = Lowering::MatrixTargetForm::ColumnArrayStruct;
            for (std::size_t i = 0u; i < source.nodes().size(); ++i)
                if (source.nodes()[i].kind == Graph::Kind::Matrix) {
                    assert(rule.sourceType == Graph::absent);
                    const auto& matrix = source.nodes()[i];
                    assert(matrix.matrixEncoding == Graph::MatrixEncoding::SpirvColMajorDecoration
                        && matrix.rows == 4u && matrix.columns == 4u && matrix.strideBytes == 16u);
                    rule.sourceType = i;
                }
            for (std::size_t i = 0u; i < target.nodes().size(); ++i) {
                const auto& n = target.nodes()[i];
                if (n.kind != Graph::Kind::Struct || n.memberCount != 1u)
                    continue;
                const auto& a = target.nodes()[target.members()[n.memberBegin].type];
                if (a.kind != Graph::Kind::FixedArray || a.count != 4u
                    || target.nodes()[a.child].kind != Graph::Kind::Vector)
                    continue;
                assert(rule.targetType == Graph::absent);
                rule.targetType = i;
            }
            assert(rule.sourceType != Graph::absent && rule.targetType != Graph::absent);
            bindings_[stage] = {
                {{0u, 0u, Lowering::RootForm::BlockStruct, {nullptr, 0u, &rule, 1u, 2u}},
                    {0u, 1u, Lowering::RootForm::BlockStruct, {nullptr, 0u}}}};
            Request buffers{2u, graphBudget, graphBudget, 1024u, 0u};
            buffers.bindings = bindings_[stage].data();
            buffers.bindingCount = bindings_[stage].size();
            buffers.maxMatrixRules = 1u;
            entries_[stage] = {names[stage], buffers};
        }
        request = {1u, entries_.data(), entries_.size()};
    }

    BindlessGraphicsMatrixPackageFixture(const BindlessGraphicsMatrixPackageFixture&) = delete;
    BindlessGraphicsMatrixPackageFixture& operator=(
        const BindlessGraphicsMatrixPackageFixture&) = delete;
};
} // namespace arp::test
