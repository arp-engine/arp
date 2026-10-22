#pragma once
#include "core/io/file_io.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "shader/shader_artifact_files.h"

#include <array>
#include <cassert>

namespace arp::test {
// Explicit GeometryFamily/1 declarations, reused by CPU admission and platform
// execution fixtures. Rule identities come from the actual bounded graphs;
// member paths belong to the authored Frame400/Palette32864 ABI. This is not
// an execution proof or a production rule-inference mechanism.
class BindlessGeometryPackageFixture final {
    using Graph = shader::BufferDeclarationTypeLayouts;
    using Lowering = shader::SpirvWgslBufferMemoryLowering;
    using Request = shader::TargetBufferMemoryLoadRequest;
    using Native = shader::NativeBufferMemoryLoadRequest;
    using Path = shader::BufferDeclarationTypePath;
    static constexpr Graph::Budget graphBudget{65536u, 65536u, 256u};
    static constexpr std::array<core::StringView, 15u> pbrEntries{"meshColorVertex",
        "meshDepthVertex",
        "meshShadowVertex",
        "skinColorVertex",
        "skinDepthVertex",
        "skinShadowVertex",
        "grassColorVertex",
        "grassDepthVertex",
        "grassShadowVertex",
        "canopyColorVertex",
        "canopyDepthVertex",
        "canopyShadowVertex",
        "familyColorMain",
        "familyDepthMain",
        "familyShadowMain"};
    static constexpr std::array<core::StringView, 6u> terrainEntries{"terrainColorVertex",
        "terrainDepthVertex",
        "terrainShadowVertex",
        "terrainColorMain",
        "terrainDepthMain",
        "terrainShadowMain"};

    struct Entry {
        std::array<core::pmr::Vector<Lowering::MatrixColumnValue>, 4u> matrices;
        core::pmr::Vector<Request::BindingPlan> bindings;
        Request target;
        std::array<core::pmr::Vector<Native::MatrixTargetPath>, 4u> nativePaths;
        std::array<std::array<std::array<Path::Step, 4u>, 4u>, 4u> pathSteps;
        std::array<core::pmr::Vector<Native::Lowering::ArrayWrapper>, 4u> wrappers;
        core::pmr::Vector<Native::Binding> nativeBindings;
        Native native;
    };

    std::array<Entry, 15u> entries_;
    std::array<Entry, 12u> pushEntries_;
    std::array<rhi::BindlessPackagePushLoadRequest::Entry, 12u> pushes_;
    std::array<rhi::BindlessPackageMemoryLoadRequest::Entry, 15u> memories_;

    static std::size_t member(const Graph& graph, std::size_t type, std::size_t index)
    {
        assert(type < graph.nodes().size());
        const auto& node = graph.nodes()[type];
        assert(node.kind == Graph::Kind::Struct && index < node.memberCount);
        return graph.members()[node.memberBegin + index].type;
    }

    static std::size_t root(const Graph& graph, std::uint32_t binding)
    {
        auto result = Graph::absent;
        for (const auto& value : graph.roots())
            if (value.storage != Graph::Storage::PushConstant && value.set == 0u
                && value.binding == binding) {
                assert(result == Graph::absent);
                result = value.type;
            }
        assert(result != Graph::absent);
        return result;
    }

    static void matrix(const Graph& source,
        std::size_t from,
        const Graph& target,
        std::size_t to,
        core::pmr::Vector<Lowering::MatrixColumnValue>& rules)
    {
        const auto& value = source.nodes()[from];
        assert(value.kind == Graph::Kind::Matrix && value.rows == 4u && value.columns == 4u
            && value.strideBytes == 16u
            && value.matrixEncoding == Graph::MatrixEncoding::SpirvColMajorDecoration);
        const auto array = member(target, to, 0u);
        const auto& columns = target.nodes()[array];
        assert(columns.kind == Graph::Kind::FixedArray && columns.count == 4u
            && columns.strideBytes == 16u
            && target.nodes()[columns.child].kind == Graph::Kind::Vector
            && target.nodes()[columns.child].count == 4u);
        for (const auto& rule : rules)
            if (rule.sourceType == from && rule.targetType == to)
                return;
        rules.push_back({from, to, Lowering::MatrixTargetForm::ColumnArrayStruct});
    }

    static void nativeMatrix(Entry& entry,
        std::uint32_t binding,
        std::size_t source,
        std::initializer_list<Path::Step> steps)
    {
        auto& paths = entry.nativePaths[binding];
        for (const auto& path : paths)
            if (path.sourceType == source)
                return;
        assert(paths.size() < 4u && steps.size() <= 4u);
        auto& storage = entry.pathSteps[binding][paths.size()];
        std::copy(steps.begin(), steps.end(), storage.begin());
        paths.push_back({source, {storage.data(), steps.size()}});
    }

    static void prepareNative(Entry& entry,
        const Graph& source,
        const core::pmr::Vector<shader::SpirvBufferDeclarationLayout>& facts)
    {
        for (const auto& buffer : facts) {
            if (buffer.set != 0u)
                continue;
            assert(buffer.binding < 4u);
            const auto type = root(source, buffer.binding);
            if (buffer.binding == 0u)
                for (std::size_t m = 0u; m < 4u; ++m)
                    nativeMatrix(entry, 0u, member(source, type, m), {{Path::Kind::Member, m}});
            if (buffer.binding == 2u)
                for (std::size_t temporal = 0u; temporal < 2u; ++temporal) {
                    const auto wrapper = member(source, member(source, type, temporal), 0u);
                    const auto array = member(source, wrapper, 0u);
                    assert(source.nodes()[array].kind == Graph::Kind::FixedArray
                        && source.nodes()[array].count == 256u);
                    // Both std140 SPIR-V and the pinned MSL std::array
                    // declaration retain one authored array wrapper. The real
                    // driver reports current.skinMatrices.__elems[element].
                    // Compare both wrappers; do not erase only the source one.
                    nativeMatrix(entry,
                        2u,
                        source.nodes()[array].child,
                        {{Path::Kind::Member, temporal},
                            {Path::Kind::Member, 0u},
                            {Path::Kind::Member, 0u},
                            {Path::Kind::ArrayElement, 0u}});
                }
            const auto& wrappers = entry.wrappers[buffer.binding];
            const auto& paths = entry.nativePaths[buffer.binding];
            entry.nativeBindings.push_back({0u,
                buffer.binding,
                graphBudget,
                {2u,
                    65536u,
                    wrappers.size(),
                    paths.size(),
                    wrappers.data(),
                    wrappers.size(),
                    nullptr,
                    0u,
                    buffer.hasRuntimeArray ? Native::Lowering::RootForm::RuntimeElement
                                           : Native::Lowering::RootForm::Block},
                paths.data(),
                paths.size(),
                paths.size() * 4u});
        }
        entry.native = {3u, graphBudget, entry.nativeBindings.data(), entry.nativeBindings.size()};
    }

    static void prepareTarget(Entry& entry,
        const Graph& source,
        const Graph& target,
        const core::pmr::Vector<shader::SpirvBufferDeclarationLayout>& sourceFacts,
        bool includePush)
    {
        for (const auto& facts : sourceFacts) {
            if (facts.set != 0u)
                continue;
            assert(facts.binding < 4u);
            auto& rules = entry.matrices[facts.binding];
            const auto from = root(source, facts.binding);
            const auto to = root(target, facts.binding);
            if (facts.binding == 0u)
                for (std::size_t m = 0u; m < 4u; ++m)
                    matrix(source, member(source, from, m), target, member(target, to, m), rules);
            if (facts.binding == 2u)
                for (std::size_t temporal = 0u; temporal < 2u; ++temporal) {
                    const auto a =
                        member(source, member(source, member(source, from, temporal), 0u), 0u);
                    const auto b =
                        member(target, member(target, member(target, to, temporal), 0u), 0u);
                    assert(source.nodes()[a].kind == Graph::Kind::FixedArray
                        && source.nodes()[a].count == 256u
                        && target.nodes()[b].kind == Graph::Kind::FixedArray
                        && target.nodes()[b].count == 256u);
                    matrix(source, source.nodes()[a].child, target, target.nodes()[b].child, rules);
                }
            entry.bindings.push_back({0u,
                facts.binding,
                facts.storageClass == 2u ? Lowering::RootForm::BlockStruct
                                         : Lowering::RootForm::StorageRuntimeArray,
                {nullptr, 0u, rules.data(), rules.size(), 2u}});
        }
        entry.target = {2u, graphBudget, graphBudget, 65536u, 0u};
        entry.target.bindings = entry.bindings.data();
        entry.target.bindingCount = entry.bindings.size();
        entry.target.push = includePush;
        for (const auto& rules : entry.matrices)
            entry.target.maxMatrixRules += rules.size();
    }

public:
    rhi::BindlessPackagePushLoadRequest push;
    rhi::BindlessPackageMemoryLoadRequest memory;

    BindlessGeometryPackageFixture(
        const std::filesystem::path& publication, bool terrain, core::StringView mode)
    {
        assert(mode == "native" || mode == "emulated");
        const auto count = terrain ? terrainEntries.size() : pbrEntries.size();
        const auto vertices = terrain ? 3u : 12u;
        std::size_t memoryCount = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            const auto name = terrain ? terrainEntries[i] : pbrEntries[i];
            const auto stem =
                core::pmr::String(terrain ? "TerrainGeometryFamily." : "PbrGeometryFamily.")
                + core::pmr::String(mode) + "." + core::pmr::String(name);
            if (i < vertices)
                pushes_[i] = {name, graphBudget, nullptr, nullptr};
            auto binary = core::readBinaryFile(
                publication.parent_path() / (stem + ".spv").c_str(), 4u * 1024u * 1024u);
            assert(binary.success);
            Graph source, target;
            core::pmr::Vector<shader::SpirvBufferDeclarationLayout> sourceFacts;
            core::pmr::Vector<shader::WgslBufferDeclarationLayout> targetFacts;
            assert(!shader::SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(
                binary.bytes.data(), binary.bytes.size(), 256u, graphBudget, sourceFacts, source));
            auto& entry = entries_[i];
            if (mode == "native") {
                prepareNative(entry, source, sourceFacts);
                if (!entry.nativeBindings.empty())
                    memories_[memoryCount++] = {name, {}, &entry.native};
                continue;
            }
            auto wgsl = core::readBinaryFile(
                publication.parent_path() / (stem + ".wgsl").c_str(), 4u * 1024u * 1024u);
            assert(wgsl.success);
            assert(!shader::WgslBufferDeclarationLayoutVerifier::verifyWithTypes(
                shader::shaderArtifactText(wgsl.bytes), 256u, graphBudget, targetFacts, target));
            prepareTarget(entry, source, target, sourceFacts, false);
            if (!entry.bindings.empty())
                memories_[memoryCount++] = {name, entry.target, nullptr};
            if (i < vertices) {
                // Push loading includes the push root in its source graph.
                // Freeze its own relation rather than borrowing node numbers
                // from the independent ordinary-buffer graph.
                Graph pushSource;
                core::pmr::Vector<shader::SpirvBufferDeclarationLayout> pushFacts;
                shader::SpirvPushConstantDeclarationLayout pushLayout;
                assert(!shader::SpirvBufferDeclarationLayoutVerifier::verifyWithPushConstants(
                    binary.bytes.data(),
                    binary.bytes.size(),
                    256u,
                    graphBudget,
                    pushFacts,
                    pushSource,
                    pushLayout));
                assert(pushLayout.present);
                prepareTarget(pushEntries_[i], pushSource, target, pushFacts, true);
                pushes_[i].target = &pushEntries_[i].target;
            }
        }
        push = {1u, 16u, pushes_.data(), vertices};
        memory = {mode == "emulated" ? 1u : 2u, memories_.data(), memoryCount};
    }

    BindlessGeometryPackageFixture(const BindlessGeometryPackageFixture&) = delete;
    BindlessGeometryPackageFixture& operator=(const BindlessGeometryPackageFixture&) = delete;
};
} // namespace arp::test
