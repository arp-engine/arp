#pragma once

#include "renderer/particles/particle_draw_plan.h"
#include "renderer/raster_viewport.h"
#include "renderer/renderer_result.h"
#include "rhi/gpu_commands.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/gpu_resources.h"

#include <cstdint>

namespace arp::renderer {

inline constexpr uint32_t kParticleTextureSetIndex = 1u;

struct ParticleRhiDrawDesc {
    rhi::GpuTexture& colorTarget;
    rhi::RenderPipeline& pipeline;
    rhi::GpuBuffer& vertexBuffer;
    rhi::GpuBuffer* uploadSourceBuffer = nullptr;
    const ParticleDrawPlan& drawPlan;
    bool backendOwnedStaging = true;
    std::uint64_t frameSerial = 0;
    rhi::DescriptorSet* descriptorSet = nullptr;
    rhi::DescriptorSet* depthDescriptorSet = nullptr;
    bool dynamicViewportScissor = false;
};

inline RendererResult recordParticleDrawPlan(
    rhi::CommandEncoder& encoder, const ParticleRhiDrawDesc& desc)
{
    if (!desc.drawPlan.success) {
        return RendererResult{false, desc.drawPlan.diagnostics};
    }
    if (desc.drawPlan.commands.empty()) {
        return RendererResult{false, "particle draw plan is empty"};
    }
    if (desc.drawPlan.vertices.empty()) {
        return RendererResult{false, "particle vertex data is empty"};
    }
    if (!desc.backendOwnedStaging) {
        return RendererResult{false, "particle RHI draw requires backend-owned upload staging"};
    }
    if (desc.uploadSourceBuffer) {
        auto transfer = encoder.beginTransferPass({"ParticlesUpload"});
        if (!transfer) {
            return RendererResult{false, encoder.lastValidation().diagnostics};
        }
        const auto uploaded = transfer->uploadBuffer({
            "ParticleVertexUpload",
            desc.uploadSourceBuffer->id(),
            desc.vertexBuffer.id(),
            desc.drawPlan.verticesSizeBytes(),
            rhi::StagingIntent::Upload,
            true,
            desc.frameSerial,
        });
        if (!uploaded.success) {
            transfer->end();
            return RendererResult{false, uploaded.diagnostics};
        }
        const auto transferEnd = transfer->end();
        if (!transferEnd.success) {
            return RendererResult{false, transferEnd.diagnostics};
        }
    }

    rhi::RenderPassDesc passDesc;
    passDesc.label = "Particles";
    passDesc.colorTargetName = desc.colorTarget.desc().label;
    passDesc.colorAttachments.push_back({
        "ParticlesColor",
        desc.colorTarget.id(),
        desc.colorTarget.desc().format,
        rhi::LoadOp::Load,
        rhi::StoreOp::Store,
    });
    auto pass = encoder.beginRenderPass(passDesc);
    if (!pass) {
        return RendererResult{false, encoder.lastValidation().diagnostics};
    }
    const auto pipeline = desc.dynamicViewportScissor
        ? bindRasterPipeline(*pass,
              desc.pipeline,
              {desc.colorTarget.desc().extent.width, desc.colorTarget.desc().extent.height})
        : pass->setPipeline(desc.pipeline);
    if (!pipeline.success) {
        pass->end();
        return RendererResult{false, pipeline.diagnostics};
    }
    const auto vb = pass->bindVertexBuffer(0, desc.vertexBuffer, 0);
    if (!vb.success) {
        pass->end();
        return RendererResult{false, vb.diagnostics};
    }
    if (desc.descriptorSet) {
        const auto ds = pass->bindDescriptorSet(kParticleTextureSetIndex, *desc.descriptorSet);
        if (!ds.success) {
            pass->end();
            return RendererResult{false, ds.diagnostics};
        }
    }
    if (desc.depthDescriptorSet) {
        constexpr uint32_t kDepthSetIndex = 2u;
        const auto ds = pass->bindDescriptorSet(kDepthSetIndex, *desc.depthDescriptorSet);
        if (!ds.success) {
            pass->end();
            return RendererResult{false, ds.diagnostics};
        }
    }
    const auto push = pass->pushConstants(rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
        0,
        static_cast<std::uint32_t>(desc.drawPlan.pushConstantSizeBytes()),
        &desc.drawPlan.pushConstants);
    if (!push.success) {
        pass->end();
        return RendererResult{false, push.diagnostics};
    }
    for (const auto& command : desc.drawPlan.commands) {
        const auto draw = pass->draw(
            command.vertexCount, command.instanceCount, command.firstVertex, command.firstInstance);
        if (!draw.success) {
            pass->end();
            return RendererResult{false, draw.diagnostics};
        }
    }
    const auto end = pass->end();
    return RendererResult{end.success, end.diagnostics};
}

} // namespace arp::renderer
