#pragma once
#include "indirect_argument_fixture.h"
#include "metal_ordinary_graphics_fixture.h"
#include "rhi/backends/metal/metal_graphics_preparation.h"

#include <array>

namespace arp::test {
class OrdinaryGraphicsVertexFixture final {
public:
    struct Instance {
        std::array<float, 2> offset;
        std::array<float, 4> color;
    };

    static auto layouts()
    {
        core::pmr::Vector<VertexBufferLayoutDesc> result{&core::defaultCpuMemoryResource()};
        result.push_back({3u, 8u, VertexInputRate::Vertex, {{0u, 0u, VertexFormat::Float32x2}}});
        result.push_back({8u,
            sizeof(Instance),
            VertexInputRate::Instance,
            {{1u, 0u, VertexFormat::Float32x2},
                {2u, sizeof(float) * 2u, VertexFormat::Float32x4}}});
        return result;
    }

    static DescriptorLayoutDesc descriptorLayout()
    {
        DescriptorLayoutDesc d;
        d.name = "EmptyVertexInputs";
        return d;
    }

    OrdinaryGraphicsVertexFixture(const char* vs,
        const char* fs,
        PrimitiveTopology topology = PrimitiveTopology::TriangleList)
        : pass(vs, fs, false, descriptorLayout(), {}, layouts(), 1u, topology)
    {
        auto& f = pass.graphics;
        const std::array<std::array<float, 2>, 5> vertices{
            {{9, 9}, {9, 9}, {-1, -1}, {3, -1}, {-1, 3}}};
        const std::array<Instance, 4> instances{{{{9, 9}, {1, 1, 1, 1}},
            {{9, 9}, {1, 1, 1, 1}},
            {{0, 0}, {1, 0, 0, 1}},
            {{0, 0}, {0, 1, 0, 1}}}};
        auto second = instances;
        second[2].color = {0, 0, 1, 1};
        for (std::size_t i = 0; i < buffers.size(); ++i) {
            const auto bytes = i ? sizeof(instances) : sizeof(vertices);
            buffers[i] = core::shareUnique(f.device->createBuffer({"PreparedVertex",
                bytes,
                ResourceUsage::VertexBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead}));
            assert(buffers[i]);
            const void* data = i == 0 ? static_cast<const void*>(vertices.data())
                : i == 1              ? instances.data()
                                      : second.data();
            assert(f.device->writeBuffer(*buffers[i], data, bytes).success);
            prepared[i] =
                prepare(*buffers[i], i ? 8u : 3u, i ? sizeof(Instance) : sizeof(vertices[0]));
        }
        assert(
            f.provider->vertexInput().streams().size() == 2u && f.provider->vertexMappingMatches());
        assert(prepared[0]->physicalSlot() == 0u && prepared[1]->physicalSlot() == 1u);
    }

    std::shared_ptr<const MetalRhiOrdinaryPreparedVertexBinding> prepare(
        const GpuBuffer& buffer, std::uint32_t slot, std::uint64_t offset)
    {
        auto& f = pass.graphics;
        auto result = MetalRhiOrdinaryPreparedVertexBinding::prepare(
            f.provider, buffer, slot, offset, f.registry, f.budget(), f.quota, f.diagnostic);
        if (!result)
            std::fprintf(stderr, "%s\n", f.diagnostic.c_str());
        assert(result);
        return result;
    }

    void dropInputs()
    {
        prepared = {};
        buffers = {};
        auto& f = pass.graphics;
        f.set.reset();
        f.arena.reset();
        f.provider.reset();
    }

    auto indexPlan(IndexFormat format,
        bool positive = false,
        std::uint32_t instances = 1u,
        std::uint32_t firstInstance = 1u)
    {
        auto& f = pass.graphics;
        const std::size_t width =
            format == IndexFormat::Uint16 ? sizeof(std::uint16_t) : sizeof(std::uint32_t);
        std::array<std::uint8_t, 24> bytes{};
        for (std::size_t i = 0; i < 6u; ++i) {
            const std::uint32_t value = (i < 2u || i == 5u)
                ? (format == IndexFormat::Uint16 ? UINT16_MAX : UINT32_MAX)
                : positive ? i - 2u
                           : i;
            for (std::size_t j = 0; j < width; ++j)
                bytes[i * width + j] = static_cast<std::uint8_t>(value >> ((width - j - 1u) * 8u));
        }
        auto index = MetalRhiOrdinaryPreparedIndexBinding::prepare(f.provider,
            *f.device,
            bytes.data(),
            width * 6u,
            format,
            core::UnsignedElementSpan::ByteOrder::BigEndian,
            IndexInputConsumptionContract::PrimitiveRestart::Disabled,
            f.registry,
            f.budget(),
            f.quota,
            f.diagnostic);
        assert(index);
        auto plan = MetalRhiOrdinaryPreparedIndexedDraw::prepare(
            index, width, 1u, 3u, positive ? 1 : -1, instances, firstInstance, f.diagnostic);
        assert(plan);
        return plan;
    }

    template <std::size_t Count>
    auto indirectArguments(IndexFormat format,
        const std::array<DrawIndexedIndirectCommand, Count>& records,
        bool positive = false)
    {
        auto draw = indexPlan(format, positive);
        auto& f = pass.graphics;
        test::IndirectArgumentFixture<Count> bytes(
            records, core::UnsignedElementSpan::ByteOrder::BigEndian);
        auto result = MetalRhiOrdinaryPreparedIndirectArguments::prepare(
            draw->index(), *f.device, bytes.source(), bytes.budget(), f.budget(), f.diagnostic);
        if (!result)
            std::fprintf(stderr, "%s\n", f.diagnostic.c_str());
        assert(result);
        return result;
    }

    auto indirectCommand(
        std::uint32_t owners, rhi::IndirectRecordingBudget budget = {1u, true, 132u, 32u, 128u})
    {
        auto& f = pass.graphics;
        const char* id = nullptr;
        auto retention = common::CommandRetentionStorage::prepare(
            {1u, true, owners, 0u, 0u, 0u, f.cpuBytes}, f.quota, id);
        if (!retention) {
            f.diagnostic = id;
            return std::shared_ptr<MetalRhiNativeCommandPayload>{};
        }
        return createMetalRhiOrdinaryIndirectNativeCommandPayload(
            budget, "BoundedHostIndirect", f.registry, std::move(retention), f.diagnostic);
    }

    test::OrdinaryGraphicsPassFixture pass;
    std::array<std::shared_ptr<GpuBuffer>, 3> buffers;
    std::array<std::shared_ptr<const MetalRhiOrdinaryPreparedVertexBinding>, 3> prepared;
};
} // namespace arp::test
