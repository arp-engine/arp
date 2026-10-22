#pragma once
#include "core/io/file_io.h"
#include "rhi/frontend/bindless_shader_package.h"

#include <array>
#include <cassert>

namespace arp::test {
// Explicit proof rules shared by the package contract and real backend fixture.
class BindlessGraphicsPushPackageFixture final {
public:
    using Graph = shader::BufferDeclarationTypeLayouts;
    using MemoryRequest = shader::TargetBufferMemoryLoadRequest;
    using Lowering = shader::SpirvWgslBufferMemoryLowering;
    using Request = rhi::BindlessPackagePushLoadRequest;
    std::array<Graph, 2u> graphs;
    std::array<shader::PushBinaryMemoryPlan::Member, 2u> rules;
    std::array<shader::PushBinaryMemoryPlan, 2u> plans;
    std::array<Request::Entry, 2u> entries;
    std::array<Graph, 2u> targets;
    std::array<MemoryRequest, 2u> targetRequests;
    std::array<core::pmr::Vector<MemoryRequest::BindingPlan>, 2u> targetBindings;
    std::array<Lowering::MatrixColumnValue, 2u> matrices;
    static constexpr std::array<const char*, 2u> names{"vertexMain", "fragmentMain"};
    static constexpr Graph::Budget graphBudget{512u, 512u, 16u};
    Request request;

    BindlessGraphicsPushPackageFixture(const std::filesystem::path& publication, const char* mode)
    {
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto filename =
                core::pmr::String("GraphicsMatrixPush.") + mode + "." + names[i] + ".spv";
            auto binary =
                core::readBinaryFile(publication.parent_path() / filename.c_str(), 1024u * 1024u);
            assert(binary.success);
            core::pmr::Vector<shader::SpirvBufferDeclarationLayout> buffers;
            shader::SpirvPushConstantDeclarationLayout push;
            assert(!shader::SpirvBufferDeclarationLayoutVerifier::verifyWithPushConstants(
                binary.bytes.data(),
                binary.bytes.size(),
                16u,
                graphBudget,
                buffers,
                graphs[i],
                push));
            assert(push.present && push.occupiedExtentBytes == 224u);
            const auto& block = graphs[i].nodes()[graphs[i].roots()[push.rootIndex].type];
            rules[i] = {0u, graphs[i].members()[block.memberBegin].type, true, true};
            plans[i] = {1u, &rules[i], 1u, 1u};
            entries[i] = {names[i], graphBudget, &plans[i]};
            if (core::StringView(mode) == "emulated") {
                const auto targetFile =
                    core::pmr::String("GraphicsMatrixPush.emulated.") + names[i] + ".wgsl";
                auto wgsl = core::readBinaryFile(
                    publication.parent_path() / targetFile.c_str(), 1024u * 1024u);
                assert(wgsl.success);
                core::pmr::Vector<shader::WgslBufferDeclarationLayout> declarations;
                assert(!shader::WgslBufferDeclarationLayoutVerifier::verifyWithTypes(
                    {reinterpret_cast<const char*>(wgsl.bytes.data()), wgsl.bytes.size()},
                    16u,
                    graphBudget,
                    declarations,
                    targets[i]));
                std::size_t targetRoot = Graph::absent;
                for (const auto& root : targets[i].roots())
                    if (root.set == 3u && root.binding == 0u)
                        targetRoot = root.type;
                assert(targetRoot != Graph::absent);
                const auto targetWrapper =
                    targets[i].members()[targets[i].nodes()[targetRoot].memberBegin].type;
                const auto sourceArray =
                    graphs[i].members()[graphs[i].nodes()[rules[i].sourceType].memberBegin].type;
                const auto targetArray =
                    targets[i].members()[targets[i].nodes()[targetWrapper].memberBegin].type;
                matrices[i] = {graphs[i].nodes()[sourceArray].child,
                    targets[i].nodes()[targetArray].child,
                    Lowering::MatrixTargetForm::ColumnArrayStruct};
                for (const auto& facts : buffers)
                    targetBindings[i].push_back({facts.set,
                        facts.binding,
                        Lowering::RootForm::StorageRuntimeArray,
                        {nullptr, 0u}});
                auto& request = targetRequests[i];
                request = {2u, graphBudget, graphBudget, 1024u, 0u};
                request.bindings = targetBindings[i].data();
                request.bindingCount = targetBindings[i].size();
                request.push = true;
                request.pushBinary = &plans[i];
                request.pushArrays = {nullptr, 0u, &matrices[i], 1u, 2u};
                request.maxMatrixRules = 1u;
            }
        }
        request = {1u, 224u, entries.data(), entries.size()};
    }

    BindlessGraphicsPushPackageFixture(const BindlessGraphicsPushPackageFixture&) = delete;
    BindlessGraphicsPushPackageFixture& operator=(
        const BindlessGraphicsPushPackageFixture&) = delete;
};
} // namespace arp::test
