#pragma once

#include "renderer/postprocess/fxaa_draw_plan.h"
#include "renderer/raster_viewport.h"
#include "renderer/renderer_result.h"
#include "rhi/gpu_commands.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/gpu_resources.h"

namespace arp::renderer {

inline constexpr uint32_t kFxaaPushConstantSetIndex = 0u;
inline constexpr uint32_t kFxaaInputSetIndex = 1u;

inline rhi::PipelineLayoutDesc fxaaPipelineLayoutDesc(
    const std::pmr::string& inputLayoutName = "FxaaInputSet")
{
    rhi::PipelineLayoutDesc desc;
    desc.descriptorLayouts.push_back({
        inputLayoutName,
        {
            {0, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment},
            {1, rhi::DescriptorType::Sampler, rhi::ShaderStage::Fragment},
        },
    });
    desc.descriptorSets.push_back({kFxaaInputSetIndex, inputLayoutName});
    desc.pushConstantRanges.push_back({
        rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
        0,
        sizeof(FxaaPushConstants),
    });
    return desc;
}

struct FxaaRhiDrawDesc {
    rhi::GpuTexture& inputColor;
    rhi::GpuTexture& outputColor;
    rhi::RenderPipeline& pipeline;
    rhi::DescriptorSet& descriptorSet;
    FxaaPushConstants pushConstants;
    bool dynamicViewportScissor = false;
};

inline RendererResult validateFxaaDrawDesc(const FxaaRhiDrawDesc& desc)
{
    const auto inputExtent = desc.inputColor.desc().extent;
    const auto outputExtent = desc.outputColor.desc().extent;
    if (inputExtent.width == 0 || inputExtent.height == 0) {
        return RendererResult{false, "FXAA input extent is invalid"};
    }
    // The fullscreen triangle samples normalized UVs, so an output target may
    // be a different resolution (for example, half-resolution DDGI resolve
    // upscaled into the swapchain). The push constants retain the input
    // texel size used by the FXAA neighborhood taps.
    if (outputExtent.width == 0 || outputExtent.height == 0) {
        return RendererResult{false, "FXAA output extent is invalid"};
    }
    if ((desc.inputColor.desc().usage & rhi::TextureUsage::Sampled) == rhi::TextureUsage::None) {
        return RendererResult{false, "FXAA input texture must be sampled"};
    }

    return RendererResult{true, ""};
}

inline RendererResult recordFxaaDrawCommands(
    rhi::CommandEncoder& encoder, const FxaaRhiDrawDesc& desc)
{
    const auto validated = validateFxaaDrawDesc(desc);
    if (!validated.success) {
        return validated;
    }

    rhi::RenderPassDesc passDesc;
    passDesc.label = "Fxaa";
    passDesc.colorTargetName = desc.outputColor.desc().label;
    passDesc.colorAttachments.push_back({
        "FxaaColor",
        desc.outputColor.id(),
        desc.outputColor.desc().format,
        rhi::LoadOp::DontCare,
        rhi::StoreOp::Store,
    });
    auto pass = encoder.beginRenderPass(passDesc);
    if (!pass) {
        return RendererResult{false, encoder.lastValidation().diagnostics};
    }
    const auto pipeline = desc.dynamicViewportScissor
        ? bindRasterPipeline(*pass,
              desc.pipeline,
              {desc.outputColor.desc().extent.width, desc.outputColor.desc().extent.height})
        : pass->setPipeline(desc.pipeline);
    if (!pipeline.success) {
        pass->end();
        return RendererResult{false, pipeline.diagnostics};
    }
    const auto descriptor = pass->bindDescriptorSet(kFxaaInputSetIndex, desc.descriptorSet);
    if (!descriptor.success) {
        pass->end();
        return RendererResult{false, descriptor.diagnostics};
    }
    const auto push = pass->pushConstants(rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
        0,
        sizeof(FxaaPushConstants),
        &desc.pushConstants);
    if (!push.success) {
        pass->end();
        return RendererResult{false, push.diagnostics};
    }
    const auto draw = pass->draw(3, 1, 0, 0);
    if (!draw.success) {
        pass->end();
        return RendererResult{false, draw.diagnostics};
    }
    const auto end = pass->end();
    return RendererResult{end.success, end.diagnostics};
}

inline RendererResult recordFxaaPass(rhi::CommandEncoder& encoder, const FxaaRhiDrawDesc& desc)
{
    const auto validated = validateFxaaDrawDesc(desc);
    if (!validated.success) {
        return validated;
    }

    if (desc.inputColor.state() != rhi::ResourceState::ShaderRead) {
        const auto transition = encoder.transition(
            desc.inputColor, desc.inputColor.state(), rhi::ResourceState::ShaderRead);
        if (!transition.success) {
            return RendererResult{false, transition.diagnostics};
        }
    }

    return recordFxaaDrawCommands(encoder, desc);
}

} // namespace arp::renderer
