#pragma once
#include "metal_ordinary_graphics_write_fixture.h"
#include "rhi/backends/metal/metal_ordinary_graphics_read_consumer.h"

namespace arp::test {
class OrdinaryGraphicsReadConsumerFixture : public OrdinaryGraphicsWriteFixture {
public:
    OrdinaryGraphicsReadConsumerFixture(
        const char* vs, const char* fs, const char* readVs, const char* readFs, bool uniform = true)
        : OrdinaryGraphicsWriteFixture(vs, fs, uniform ? Consumer::UniformShader : Consumer::Shader)
    {
        auto& g = pass.graphics;
        buffer->sealHostWrites();
        write = prepare();
        assert(write);
        producerPass = pass.prepare();
        core::CpuMemoryQuotaScope scope(g.quota);
        shader::SingleEntryShaderLoadBudget budget{g.cpuBytes, g.cpuBytes * 4u, g.cpuBytes, 16u};
        auto vertex =
            shader::SingleEntryShaderArtifact::load(readVs, budget, g.quota, g.diagnostic);
        auto fragment =
            shader::SingleEntryShaderArtifact::load(readFs, budget, g.quota, g.diagnostic);
        assert(vertex && fragment);
        readLayout.name = "ExactWordConsumer";
        readLayout.bindings = {{3u,
            uniform ? rhi::DescriptorType::UniformBuffer
                    : rhi::DescriptorType::ReadOnlyStorageBuffer,
            rhi::ShaderStage::Fragment}};
        rhi::PipelineLayoutDesc authored;
        authored.descriptorLayouts = {readLayout};
        authored.descriptorSets = {{0u, readLayout.name}};
        auto layout = g.device->createPipelineLayout(authored);
        assert(layout);
        rhi::RenderPipelineDesc state;
        state.label = "ExactWordReader";
        state.layout = layout.get();
        state.colorFormat = rhi::TextureFormat::Rgba8Unorm;
        state.dynamicViewportScissor = true;
        auto caps = rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal);
        caps.queriedFromNativeDevice = true;
        caps.queriedExtendedFeatures = true;
        provider = rhi_metal::MetalRhiOrdinaryGraphicsProvider::create(
            vertex, fragment, std::move(state), caps, g.budget(), g.quota, g.diagnostic);
        if (!provider)
            std::fprintf(stderr, "Read provider: %s\n", g.diagnostic.c_str());
        assert(provider);
        set = g.arena->allocateDescriptorSet({"Reader", readLayout});
        assert(set);
        assert(
            set->write({3u,
                           readLayout.bindings.front().type,
                           buffer->id(),
                           rhi::DescriptorResourceKind::Buffer})
                .success);
        readBinding = rhi_metal::MetalRhiOrdinaryPreparedGraphicsBinding::prepare(
            provider, *set, 0u, g.registry, g.budget(), g.quota, g.diagnostic);
        assert(readBinding);
        readPass = rhi_metal::MetalRhiOrdinaryPreparedRenderPass::prepare(
            pass.desc, provider, g.registry, g.budget(), g.quota, g.diagnostic);
        assert(readPass);
    }

    auto prepareConsumer()
    {
        return rhi_metal::MetalRhiOrdinaryPreparedGraphicsReadConsumer::prepare(
            write, readBinding, readPass, pass.graphics.diagnostic);
    }

    void dropReaderInputs()
    {
        set.reset();
        provider.reset();
        readBinding.reset();
        readPass.reset();
        write.reset();
        producerPass.reset();
        readLayout = {};
        dropInputs();
    }

    rhi::DescriptorLayoutDesc readLayout;
    std::shared_ptr<const rhi_metal::MetalRhiOrdinaryPreparedGraphicsWrite> write;
    std::shared_ptr<const rhi_metal::MetalRhiOrdinaryPreparedRenderPass> producerPass, readPass;
    std::shared_ptr<const rhi_metal::MetalRhiOrdinaryGraphicsProvider> provider;
    std::shared_ptr<const rhi_metal::MetalRhiOrdinaryPreparedGraphicsBinding> readBinding;
    core::UniquePtr<rhi::DescriptorSet> set;
};
} // namespace arp::test
