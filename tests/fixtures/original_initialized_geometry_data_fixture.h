#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_bindless_geometry_fixture.h"
#include "original_initialized_shader_data_fixture.h"

namespace arp::test {
// The same frozen vertex/index bytes and quotas feed every genuine backend
// actor. Backend initialization and output readback remain backend-owned.
template <class Actor, class Base> struct OriginalInitializedGeometryDataFixture : Base {
    using Content = Actor;

    struct Initialized {
        std::array<std::shared_ptr<const Content>, 2u> actors;
        std::array<typename Content::Quotas, 2u> quotas;
    };

    std::shared_ptr<Initialized> initialized = core::makeShared<Initialized>();
    static constexpr std::uint32_t geometryBufferCount = 2u;
    bool instanceRate = false;

    void appendGeometrySources(rhi::GpuDevice& device,
        core::pmr::Vector<rhi::OriginalCommandResourceSources::Source>& sources,
        const rhi::OriginalCommandResourceLeaseBudget& leaseBudget,
        const std::shared_ptr<core::CpuMemoryQuota>& leaseQuota) const
    {
        using namespace rhi;
        using C = OriginalHostGeometryContentContract;
        constexpr std::uint64_t cpu = 65536u;
        OriginalInitializedShaderDataFixture<Content>::verify(device, this->nativeStagingCeiling);
        for (std::size_t i = 0u; i < 2u; ++i) {
            auto& q = initialized->quotas[i];
            q = {core::makeShared<core::CpuMemoryQuota>(cpu),
                core::makeShared<core::CpuMemoryQuota>(cpu),
                core::makeShared<core::CpuMemoryQuota>(cpu),
                leaseQuota};
            const BufferDesc desc{i ? "root-sealed-indices" : "root-sealed-vertices",
                i ? 8u : 64u,
                i ? ResourceUsage::IndexBuffer : ResourceUsage::VertexBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead};
            C::Budget budget{1u,
                true,
                1u,
                C::retainedOwnerBound,
                this->nativeStagingCeiling,
                cpu,
                cpu,
                cpu,
                leaseBudget.maxCpuMetadataBytes,
                C::Staging::VertexContentIntent{1u,
                    true,
                    16u,
                    instanceRate ? VertexInputRate::Instance : VertexInputRate::Vertex,
                    4u}};
            if (i)
                budget.definition = C::Staging::IndexContentIntent{1u,
                    true,
                    IndexFormat::Uint16,
                    core::UnsignedElementSpan::ByteOrder::LittleEndian,
                    3u};
            // Sentinels catch ignored offsets. firstInstance=1 reads the
            // second vertex on the instance-rate path.
            std::array<float, 16u> vertices{99.f,
                99.f,
                99.f,
                99.f,
                -1.f,
                -1.f,
                0.f,
                1.f,
                3.f,
                -1.f,
                0.f,
                1.f,
                -1.f,
                3.f,
                0.f,
                1.f};
            std::array<std::uint8_t, 8u> indices{0xffu, 0xffu, 0u, 0u, 2u, 0u, 1u, 0u};
            const void* bytes = i ? static_cast<const void*>(indices.data()) : vertices.data();
            const char* id = nullptr;
            auto* extension = static_cast<BindlessCrossExtension*>(
                device.queryInterface(kExtensionBindlessCross));
            assert(extension);
            const auto allocation = extension->queryOriginalInitializedBufferAllocation(
                device, desc, budget.contractVersion, id);
            assert(!id && allocation >= desc.sizeBytes && allocation <= this->nativeStagingCeiling);
            const auto prepare = [&](const auto& owners) {
                return std::dynamic_pointer_cast<const Content>(
                    extension->prepareOriginalInitializedBuffer(
                        device, {desc, bytes, desc.sizeBytes, budget, owners}, id));
            };
            const auto leaseBaseline = leaseQuota->liveBytes();
            auto alias = q;
            alias.payload = q.metadata;
            assert(!prepare(alias) && id);
            {
                core::CpuMemoryQuotaLease pressure(q.metadata, cpu);
                assert(pressure.charged() && !prepare(q));
            }
            assert(!q.metadata->liveBytes() && !q.payload->liveBytes() && !q.resource->liveBytes()
                && leaseQuota->liveBytes() == leaseBaseline);
            auto actor = prepare(q);
            if (!actor)
                std::fprintf(stderr, "Initialized geometry: %s\n", id ? id : "missing");
            assert(actor && !id && actor->initializedBytes() == desc.sizeBytes);
            auto forged = std::shared_ptr<const Content>(q.metadata, actor.get());
            assert(Content::validateOriginal(forged));
            forged.reset();
            assert(!device
                    .writeBuffer(*const_cast<GpuBuffer*>(actor->originalLease()->originalObject()),
                        bytes,
                        desc.sizeBytes)
                    .success);
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{q.metadata, q.payload, q.resource, q.lease}, [&]() noexcept {
                    return actor->validate();
                }));
            sources.push_back(actor->originalLease());
            initialized->actors[i] = std::move(actor);
            vertices.fill(-99.f);
            indices.fill(0xffu);
            assert(!initialized->actors[i]->validate());
        }
    }

    void configureGeometryPipeline(rhi::RenderPipelineDesc& desc) const
    {
        OriginalBindlessGeometryDataFixture data;
        data.instanceRate = instanceRate;
        data.configureGeometryPipeline(desc);
    }
};
} // namespace arp::test
