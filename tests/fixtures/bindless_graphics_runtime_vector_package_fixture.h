#pragma once
#include "core/file_io.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "shader/spirv_buffer_declaration_layout.h"

#include <array>
#include <cassert>

namespace arp::test {
// Explicit forms from the authored ABI and actual generated SPIR-V/MSL/WGSL.
// No target graph, size or runtime capacity is inferred for the driver.
class BindlessGraphicsRuntimeVectorPackageFixture {
public:
    using Graph = shader::BufferDeclarationTypeLayouts;
    using Native = shader::NativeBufferMemoryLoadRequest;
    using Emulated = shader::TargetBufferMemoryLoadRequest;
    using Msl = shader::SpirvMslBufferMemoryLowering;
    using Wgsl = shader::SpirvWgslBufferMemoryLowering;
    static constexpr Graph::Budget graphBudget{128u, 128u, 16u};
    std::array<std::array<Native::Binding, 2u>, 3u> nativeBindings;
    std::array<Native, 3u> nativeRequests;
    std::array<std::array<Emulated::BindingPlan, 2u>, 3u> emulatedBindings;
    std::array<rhi::BindlessPackageMemoryLoadRequest::Entry, 3u> entries;
    rhi::BindlessPackageMemoryLoadRequest request;

    struct Shape {
        const char* packageName;
        std::uint64_t runtimeStride;
        std::size_t runtimeFields, probeFields;
        std::uint64_t probeBytes;
        Msl::RootForm nativeProbeForm = Msl::RootForm::Block;
        Wgsl::RootForm emulatedProbeForm = Wgsl::RootForm::BlockStruct;
    };

    explicit BindlessGraphicsRuntimeVectorPackageFixture(
        const std::filesystem::path& directory, const Shape& shape, bool native = false)
    {
        constexpr std::array<const char*, 3u> names{"vertexMain", "cleanFragment", "faultFragment"};
        for (std::size_t i = 0u; i < names.size(); ++i) {
            const auto file = core::readBinaryFile(directory
                    / (std::string(shape.packageName) + "." + (native ? "native." : "emulated.")
                        + names[i] + ".spv"),
                1024u * 1024u);
            assert(file.success);
            Graph source;
            core::pmr::Vector<shader::SpirvBufferDeclarationLayout> facts;
            assert(!shader::SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(
                file.bytes.data(), file.bytes.size(), 16u, graphBudget, facts, source));
            std::size_t ordinary = 0u;
            for (const auto& root : source.roots()) {
                if (root.set != 0u)
                    continue;
                assert(root.binding < 2u);
                const auto& block = source.nodes()[root.type];
                if (root.binding == 0u) {
                    assert(block.kind == Graph::Kind::Struct && block.memberCount == 1u);
                    const auto& member = source.members()[block.memberBegin];
                    const auto& array = source.nodes()[member.type];
                    assert(!member.offsetBytes && array.kind == Graph::Kind::RuntimeArray
                        && array.strideBytes == shape.runtimeStride);
                    const auto& element = source.nodes()[array.child];
                    assert(element.kind == Graph::Kind::Struct
                        && element.memberCount == shape.runtimeFields
                        && element.occupiedExtentBytes == shape.runtimeStride);
                    for (std::size_t field = 0u; field < shape.runtimeFields; ++field) {
                        const auto& m = source.members()[element.memberBegin + field];
                        const auto& vector = source.nodes()[m.type];
                        assert(m.offsetBytes == field * 16u && vector.kind == Graph::Kind::Vector
                            && vector.count == 4u && vector.occupiedExtentBytes == 16u);
                    }
                } else {
                    assert(block.kind == Graph::Kind::Struct
                        && block.memberCount == shape.probeFields
                        && block.occupiedExtentBytes == shape.probeBytes);
                }
                ++ordinary;
            }
            assert(ordinary == 2u);
            if (native) {
                nativeBindings[i] = {{{0u,
                                          0u,
                                          graphBudget,
                                          {2u,
                                              256u,
                                              0u,
                                              0u,
                                              nullptr,
                                              0u,
                                              nullptr,
                                              0u,
                                              Msl::RootForm::RuntimeElement}},
                    {0u,
                        1u,
                        graphBudget,
                        {2u, 256u, 0u, 0u, nullptr, 0u, nullptr, 0u, shape.nativeProbeForm}}}};
                nativeRequests[i] = {2u, graphBudget, nativeBindings[i].data(), 2u};
                entries[i] = {names[i], {}, &nativeRequests[i]};
            } else {
                emulatedBindings[i] = {
                    {{0u, 0u, Wgsl::RootForm::StorageRuntimeArray, {nullptr, 0u}},
                        {0u, 1u, shape.emulatedProbeForm, {nullptr, 0u}}}};
                Emulated buffers{1u, graphBudget, graphBudget, 256u, 0u};
                buffers.bindings = emulatedBindings[i].data();
                buffers.bindingCount = 2u;
                entries[i] = {names[i], buffers};
            }
        }
        request = {native ? 2u : 1u, entries.data(), entries.size()};
    }

    BindlessGraphicsRuntimeVectorPackageFixture(
        const BindlessGraphicsRuntimeVectorPackageFixture&) = delete;
    BindlessGraphicsRuntimeVectorPackageFixture& operator=(
        const BindlessGraphicsRuntimeVectorPackageFixture&) = delete;
};
} // namespace arp::test
