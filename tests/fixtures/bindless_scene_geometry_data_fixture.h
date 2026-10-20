#pragma once
#include "bindless_scene_buffer_data_fixture.h"
#include "original_bindless_root_source_fixture.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace arp::test {
// FamilyInput has a declared location schema. Pack only attributes retained by
// the actual immutable entry, checking each format instead of guessing a stride.
// Native callers retain the public initialization actors, Null checks bytes only.
struct BindlessSceneGeometryDataFixture : OriginalBindlessRootFixtureExecution,
                                          BindlessSceneBufferDataFixture {
    static constexpr bool graphicsPush = true, tableBufferData = true;
    static constexpr std::uint32_t geometryBufferCount = 3u;
    static constexpr std::uint64_t originalRootCpuBytes = 4u * 1024u * 1024u;
    const rhi::BindlessPackageModule* vertex = nullptr;

    struct Geometry {
        std::array<std::shared_ptr<const rhi::OriginalInitializedGeometrySource>, 2u> actors;
        std::array<std::shared_ptr<const rhi::OriginalCommandBufferLease>, 2u> leases;
        std::shared_ptr<const rhi::OriginalCommandBufferLease> resultReadback;
    };

    std::shared_ptr<Geometry> geometry = core::makeShared<Geometry>();

    void configureAdmission(rhi::OriginalCommandAdmissionBudget& budget) const
    {
        budget.maxPushBytes = sizeof(rhi::BindlessPbrGeometryDraw16);
    }

    rhi::VertexBufferLayoutDesc vertexLayout() const
    {
        assert(vertex);
        rhi::VertexBufferLayoutDesc stream;
        constexpr std::array<std::uint32_t, 7u> widths{3u, 3u, 2u, 4u, 3u, 4u, 4u};
        constexpr std::array<shader::VertexAttributeFormat, 7u> formats{
            shader::VertexAttributeFormat::Float3,
            shader::VertexAttributeFormat::Float3,
            shader::VertexAttributeFormat::Float2,
            shader::VertexAttributeFormat::Float4,
            shader::VertexAttributeFormat::Float3,
            shader::VertexAttributeFormat::Float4,
            shader::VertexAttributeFormat::Float4};
        std::array<bool, 7u> seen{};
        for (const auto& attribute : vertex->reflection.reflection.vertexAttributes) {
            assert(attribute.location < widths.size() && !seen[attribute.location]);
            seen[attribute.location] = true;
            assert(attribute.format == formats[attribute.location]);
            const auto format = widths[attribute.location] == 2u ? rhi::VertexFormat::Float32x2
                : widths[attribute.location] == 3u               ? rhi::VertexFormat::Float32x3
                                                                 : rhi::VertexFormat::Float32x4;
            stream.attributes.push_back({attribute.location, stream.strideBytes, format});
            stream.strideBytes += widths[attribute.location] * sizeof(float);
        }
        assert(seen[0u] && stream.strideBytes);
        return stream;
    }

    core::pmr::Vector<std::uint8_t> vertexBytes() const
    {
        const auto layout = vertexLayout();
        core::pmr::Vector<std::uint8_t> result(layout.strideBytes * 3u, 0u);
        // Oversized triangle covers the fixed 2x2 target. z=0.5 is an exact,
        // valid depth for every family; current/previous transforms are identity.
        constexpr std::array<std::array<float, 3u>, 3u> positions{
            {{-1.f, -1.f, 0.5f}, {3.f, -1.f, 0.5f}, {-1.f, 3.f, 0.5f}}};
        constexpr std::array<std::array<float, 2u>, 3u> uvs{{{0.f, 0.f}, {2.f, 0.f}, {0.f, 2.f}}};
        for (std::size_t i = 0u; i < positions.size(); ++i) {
            for (const auto& attribute : layout.attributes) {
                std::array<float, 4u> value{};
                std::size_t count = 4u;
                switch (attribute.shaderLocation) {
                case 0u:
                    std::copy(positions[i].begin(), positions[i].end(), value.begin());
                    count = 3u;
                    break;
                case 1u:
                    value = {0.f, 0.f, 1.f, 0.f};
                    count = 3u;
                    break;
                case 2u:
                    value = {uvs[i][0u], uvs[i][1u], 0.f, 0.f};
                    count = 2u;
                    break;
                case 3u:
                    value = {1.f, 0.f, 0.f, 1.f};
                    break;
                case 4u:
                    count = 3u;
                    break; // zero wind weights
                case 5u:
                    break; // all joints select the initialized identity
                case 6u:
                    value[0u] = 1.f;
                    break;
                default:
                    assert(false);
                }
                std::memcpy(result.data() + i * layout.strideBytes + attribute.offsetBytes,
                    value.data(),
                    count * sizeof(float));
            }
        }
        return result;
    }

    static core::pmr::Vector<std::uint8_t> indexBytes() { return {0u, 0u, 2u, 0u, 1u, 0u}; }

    void configureGeometryPipeline(rhi::RenderPipelineDesc& state) const
    {
        state.vertexBuffers = {vertexLayout()};
    }

    void appendGeometrySources(rhi::GpuDevice& device,
        core::pmr::Vector<rhi::OriginalCommandResourceSources::Source>& sources,
        const rhi::OriginalCommandResourceLeaseBudget& leaseBudget,
        const std::shared_ptr<core::CpuMemoryQuota>& leaseQuota) const
    {
        using namespace rhi;
        using C = OriginalHostGeometryContentContract;
        for (std::size_t i = 0u; i < 2u; ++i) {
            const auto contents = i ? indexBytes() : vertexBytes();
            const BufferDesc desc{i ? "scene-indices" : "scene-vertices",
                contents.size(),
                i ? ResourceUsage::IndexBuffer : ResourceUsage::VertexBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead};
            C::Definition definition = C::Staging::VertexContentIntent{
                1u, true, vertexLayout().strideBytes, VertexInputRate::Vertex, 3u};
            if (i)
                definition = C::Staging::IndexContentIntent{1u,
                    true,
                    IndexFormat::Uint16,
                    core::UnsignedElementSpan::ByteOrder::LittleEndian,
                    3u};
            if (initializeNative) {
                geometry->actors[i] =
                    initializeContent(device, desc, contents, definition, leaseBudget, leaseQuota);
                geometry->leases[i] = geometry->actors[i]->originalLease();
            } else {
                assert(device.backend() == Backend::Null);
                auto buffer = device.createBuffer(desc);
                assert(buffer
                    && device.writeBuffer(*buffer, contents.data(), contents.size()).success);
                const char* id = nullptr;
                geometry->leases[i] = OriginalCommandBufferLease::prepareOriginal(
                    buffer, leaseBudget, leaseQuota, id);
                assert(geometry->leases[i] && !id);
            }
            sources.push_back(geometry->leases[i]);
        }
        const BufferDesc desc{"scene-results-readback",
            resultBytes,
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu,
            ResourceState::CopyDst};
        auto result = device.createBuffer(desc);
        assert(result);
        const char* id = nullptr;
        geometry->resultReadback =
            OriginalCommandBufferLease::prepareOriginal(result, leaseBudget, leaseQuota, id);
        assert(geometry->resultReadback && !id);
        sources.push_back(geometry->resultReadback);
    }
};
} // namespace arp::test
