#include "renderer/gi/ddgi_renderer.h"
#include "rhi/backends/null/null_backend.h"
#include "tests/fixtures/flat_terrain_surface.h"

#include <cassert>
#include <cstdio>
#include <string_view>

void recover(unsigned domain, unsigned failure, unsigned slot)
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    {
        renderer::DdgiRenderer ddgi(*device);
        assert(
            ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
                    {core::makeShared<tests::FlatTerrainSurface>(), 2u, 2u},
                    2u,
                    2u,
                    2u,
                    4u,
                    4u)
                .success);
        auto depth = device->createTexture({"Depth",
            {4u, 4u, 1u},
            rhi::TextureFormat::D32Float,
            rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderAttachment,
            rhi::MemoryUsage::GpuOnly,
            rhi::ResourceState::ShaderRead});
        auto normal = device->createTexture({"Normal",
            {4u, 4u, 1u},
            rhi::TextureFormat::Rgba16Float,
            rhi::TextureUsage::Sampled,
            rhi::MemoryUsage::GpuOnly,
            rhi::ResourceState::ShaderRead});
        auto& originalParent = domain == 0u ? depth : normal;
        const auto format = originalParent->desc().format;
        const auto aspect = domain == 0u ? rhi::TextureAspect::Depth : rhi::TextureAspect::Color;
        auto original = device->createTextureView(*originalParent,
            {"OriginalBinding",
                format,
                rhi::TextureUsage::Sampled,
                rhi::TextureViewDimension::Texture2D,
                0u,
                1u,
                0u,
                1u,
                aspect});
        auto replacementParent = device->createTexture(originalParent->desc());
        assert(replacementParent);
        auto replacement = device->createTextureView(*replacementParent,
            {"PartialBinding",
                format,
                rhi::TextureUsage::Sampled,
                rhi::TextureViewDimension::Texture2D,
                0u,
                1u,
                0u,
                1u,
                aspect});
        rhi::SamplerDesc samplerDesc{"CsmTestSampler",
            rhi::SamplerFilter::Nearest,
            rhi::SamplerFilter::Nearest,
            rhi::SamplerAddressMode::ClampToEdge,
            rhi::SamplerAddressMode::ClampToEdge,
            rhi::SamplerAddressMode::ClampToEdge};
        samplerDesc.mipFilter = rhi::SamplerMipFilter::Nearest;
        auto sampler = device->createSampler(samplerDesc);
        auto expiredSampler = device->createSampler(samplerDesc);
        assert(depth && normal && original && replacement && sampler && expiredSampler);
        const auto bind = [&](const rhi::GpuTextureView& view, const rhi::GpuSampler& sample) {
            if (domain == 0u)
                ddgi.setCsmShadowMap(view, sample);
            else
                ddgi.setMaterialAlbedo(domain - 1u, view, sample);
        };
        const auto record = [&] {
            auto encoder = device->createCommandEncoder({"CsmCacheRecovery"});
            render_graph::RenderContext context(*device, *encoder);
            renderer::DdgiPushConstants pc{};
            ddgi.recordResolvePass(context, *depth, *normal, pc, slot);
            auto commands = encoder->finish();
            assert(commands && commands->commandCount() > 0u);
        };
        bind(*original, *sampler);
        record();
        const auto submitted = device->queue().submitCount();
        bind(*replacement, *expiredSampler);
        if (failure == 0u)
            expiredSampler.reset();
        else if (failure == 1u)
            replacement.reset();
        else
            replacementParent.reset();
        bool rejected = false;
        try {
            record();
        } catch (const core::DiagnosticError& error) {
            rejected = std::string_view(error.what())
                           .find(failure == 2u ? "RHI-DESCRIPTOR-PARENT-NOT-LIVE"
                                               : "RHI-DESCRIPTOR-RESOURCE-NOT-LIVE")
                != std::string_view::npos;
            std::fprintf(stderr, "DDGI descriptor partial write diagnostic=%s\n", error.what());
        }
        assert(rejected && device->queue().submitCount() == submitted);
        // In the sampler case a texture binding changed before the sampler failed.
        // Restoring the exact original identity must not hit a stale valid cache.
        replacement.reset();
        bind(*original, *sampler);
        record();
        assert(device->queue().submitCount() == submitted);
        std::fprintf(stderr,
            "DDGI descriptor domain=%u failure=%u slot=%u original-signature recovery=1 no-submit=1\n",
            domain,
            failure,
            slot);
    }
    device->waitIdle();
    assert(device->memoryStats().allocatedBytes == 0u);
    std::fprintf(stderr, "DDGI descriptor Null retired=1\n");
}

int main()
{
    for (unsigned domain = 0; domain < 5u; ++domain)
        for (unsigned failure = 0; failure < 3u; ++failure)
            for (unsigned slot = 0; slot < 2u; ++slot)
                recover(domain, failure, slot);
}
