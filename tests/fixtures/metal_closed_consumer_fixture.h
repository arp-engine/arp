#pragma once
#include "metal_ordinary_graphics_fixture.h"
#include "rhi/backends/metal/metal_zero_base_indirect_consumer_pipeline.h"

namespace arp::test {
// Reusable compiler-owned closed consumer package and complete render descriptor.
class MetalClosedConsumerFixture final {
public:
    MetalClosedConsumerFixture(
        OrdinaryGraphicsFixture& fixture, const char* publication, const char* nativePublication)
        : fixture_(fixture)
    {
        core::CpuMemoryQuotaScope scope(fixture.quota);
        const rhi::BindlessPackageLoadBudget budget{
            fixture.cpuBytes, fixture.cpuBytes * 4u, 64u, 8u, 32u, 8u};
        package = rhi::BindlessShaderPackage::load(publication, budget, fixture.diagnostic);
        nativePackage =
            rhi::BindlessShaderPackage::load(nativePublication, budget, fixture.diagnostic);
        assert(package && nativePackage && package->quota() == fixture.quota);
        state.label = "ClosedConsumerQuery";
        state.colorFormat = rhi::TextureFormat::Rgba8Unorm;
        state.dynamicViewportScissor = true;
        rhi::VertexBufferLayoutDesc stream;
        stream.slot = 0u;
        stream.strideBytes = 2u * sizeof(float);
        stream.inputRate = rhi::VertexInputRate::Vertex;
        stream.attributes = {{0u, 0u, rhi::VertexFormat::Float32x2}};
        state.vertexBuffers.push_back(std::move(stream));
    }

    auto prepare(const std::shared_ptr<const rhi_metal::MetalRhiZeroBaseIndirectSource>& source)
    {
        return rhi_metal::MetalRhiZeroBaseIndirectConsumerPipeline::prepare(source,
            package,
            state,
            rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal),
            fixture_.budget(),
            {1u, true, 128u},
            fixture_.diagnostic);
    }

    std::shared_ptr<const rhi::BindlessShaderPackage> package, nativePackage;
    rhi::RenderPipelineDesc state;

private:
    OrdinaryGraphicsFixture& fixture_;
};
} // namespace arp::test
