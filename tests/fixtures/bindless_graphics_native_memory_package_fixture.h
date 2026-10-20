#pragma once
#include "core/file_io.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "shader/spirv_buffer_declaration_layout.h"

#include <array>
#include <cassert>

namespace arp::test {
class BindlessGraphicsNativeMemoryPackageFixture final {
public:
    using Request = shader::NativeBufferMemoryLoadRequest;
    using Lowering = shader::SpirvMslBufferMemoryLowering;
    using Graph = shader::BufferDeclarationTypeLayouts;
    static constexpr Graph::Budget graphBudget{512u, 512u, 16u};
    std::array<Lowering::MatrixColumns, 3u> matrices;
    std::array<std::array<Request::Binding, 2u>, 3u> bindings;
    std::array<Request, 3u> native;
    std::array<rhi::BindlessPackageMemoryLoadRequest::Entry, 3u> entries;
    rhi::BindlessPackageMemoryLoadRequest request;

    explicit BindlessGraphicsNativeMemoryPackageFixture(
        const std::filesystem::path& directory, const char* family = "GraphicsMatrixOrdinary")
    {
        constexpr std::array<const char*, 3u> names{"vertexMain", "cleanFragment", "faultFragment"};
        for (std::size_t i = 0u; i < names.size(); ++i) {
            const auto path = directory / (std::string(family) + ".native." + names[i] + ".spv");
            const auto file = core::readBinaryFile(path, 1024u * 1024u);
            assert(file.success);
            Graph source;
            core::pmr::Vector<shader::SpirvBufferDeclarationLayout> facts;
            assert(!shader::SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(
                file.bytes.data(), file.bytes.size(), 16u, graphBudget, facts, source));
            for (std::size_t n = 0u; n < source.nodes().size(); ++n)
                if (source.nodes()[n].kind == Graph::Kind::Matrix) {
                    assert(matrices[i].sourceType == Graph::absent);
                    matrices[i].sourceType = n;
                }
            assert(matrices[i].sourceType != Graph::absent);
            // Authored fixture target graph identity, independently checked by
            // the registered real Metal member-memory test. No device limit.
            matrices[i].targetType = 2u;
            bindings[i] = {
                {{0u, 0u, graphBudget, {1u, 1024u, 0u, 1u, nullptr, 0u, &matrices[i], 1u}},
                    {0u, 1u, graphBudget, {1u, 1024u, 0u, 0u, nullptr, 0u, nullptr, 0u}}}};
            native[i] = {1u, graphBudget, bindings[i].data(), bindings[i].size()};
            entries[i] = {names[i], {}, &native[i]};
        }
        request = {2u, entries.data(), entries.size()};
    }

    BindlessGraphicsNativeMemoryPackageFixture(
        const BindlessGraphicsNativeMemoryPackageFixture&) = delete;
    BindlessGraphicsNativeMemoryPackageFixture& operator=(
        const BindlessGraphicsNativeMemoryPackageFixture&) = delete;
};

// Explicit source forms for the existing GraphicsDomains publication. Target
// graphs/stride are deliberately absent: only the actual driver can supply them.
class BindlessGraphicsRuntimeNativeMemoryPackageFixture final {
public:
    using Request = shader::NativeBufferMemoryLoadRequest;
    using Lowering = shader::SpirvMslBufferMemoryLowering;
    using Graph = shader::BufferDeclarationTypeLayouts;
    static constexpr Graph::Budget graphBudget{64u, 64u, 16u};
    std::array<std::array<Request::Binding, 3u>, 3u> bindings;
    std::array<Request, 3u> native;
    std::array<rhi::BindlessPackageMemoryLoadRequest::Entry, 3u> entries;
    rhi::BindlessPackageMemoryLoadRequest request;

    explicit BindlessGraphicsRuntimeNativeMemoryPackageFixture(
        const std::filesystem::path& directory)
    {
        constexpr std::array<const char*, 3u> names{"vertexMain", "cleanFragment", "faultFragment"};
        for (std::size_t i = 0u; i < names.size(); ++i) {
            const auto file = core::readBinaryFile(
                directory / (std::string("GraphicsDomains.native.") + names[i] + ".spv"),
                1024u * 1024u);
            assert(file.success);
            Graph source;
            core::pmr::Vector<shader::SpirvBufferDeclarationLayout> facts;
            assert(!shader::SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(
                file.bytes.data(), file.bytes.size(), 16u, graphBudget, facts, source));
            std::size_t count = 0u;
            for (const auto& root : source.roots()) {
                if (root.set == 1u) {
                    assert(root.binding == 0u || root.binding == 1u || root.binding == 3u);
                    continue; // Authored bindless tables/metadata, not ordinary domains.
                }
                assert(count < bindings[i].size());
                const bool frame = root.set == 0u && root.binding == 3u;
                assert(frame || (root.set == 2u && (root.binding == 3u || root.binding == 4u)));
                bindings[i][count++] = {root.set,
                    root.binding,
                    graphBudget,
                    {2u,
                        128u,
                        0u,
                        0u,
                        nullptr,
                        0u,
                        nullptr,
                        0u,
                        frame ? Lowering::RootForm::Value : Lowering::RootForm::RuntimeElement}};
            }
            assert(count == (i ? 3u : 2u));
            native[i] = {2u, graphBudget, bindings[i].data(), count};
            entries[i] = {names[i], {}, &native[i]};
        }
        request = {2u, entries.data(), entries.size()};
    }

    BindlessGraphicsRuntimeNativeMemoryPackageFixture(
        const BindlessGraphicsRuntimeNativeMemoryPackageFixture&) = delete;
    BindlessGraphicsRuntimeNativeMemoryPackageFixture& operator=(
        const BindlessGraphicsRuntimeNativeMemoryPackageFixture&) = delete;
};
} // namespace arp::test
