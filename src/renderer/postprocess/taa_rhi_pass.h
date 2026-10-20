#pragma once

#include "renderer/postprocess/taa_draw_plan.h"
#include "renderer/raster_viewport.h"
#include "renderer/renderer_result.h"
#include "rhi/gpu_commands.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/gpu_resources.h"

namespace arp::renderer {

inline constexpr uint32_t kTaaPushConstantSetIndex = 0u;
// taa.slang declares all sampled inputs with [[vk::binding(binding, 0)]],
// therefore this must be Vulkan descriptor set 0. Binding it at set 1 passes
// the Null backend but fails native Vulkan validation at vkCmdDraw.
inline constexpr uint32_t kTaaInputSetIndex = 0u;

// TAA resolve reads 7 textures + 1 sampler. Bindings match taa.slang:
//   0 = SceneColorTexture, 1 = HistoryColorTexture, 2 = VelocityTexture,
//   3 = DepthTexture, 4 = ReactiveTexture, 5 = HistoryDepthTexture,
//   6 = HistoryMomentsTexture, 7 = LinearClampSampler.
inline rhi::PipelineLayoutDesc taaPipelineLayoutDesc(
    const std::pmr::string& inputLayoutName = "TaaInputSet")
{
    rhi::PipelineLayoutDesc desc;
    desc.descriptorLayouts.push_back({
        inputLayoutName,
        {
            {0, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment},
            {1, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment},
            {2, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment},
            // The live scene depth is D32Float. WebGPU requires depth sample
            // type/layout matching even though TAA fetches it with textureLoad.
            {3,
                rhi::DescriptorType::SampledTexture,
                rhi::ShaderStage::Fragment,
                1,
                false,
                false,
                false,
                rhi::TextureViewDimension::Texture2D,
                true /* depthTexture */},
            {4, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment},
            {5,
                rhi::DescriptorType::SampledTexture,
                rhi::ShaderStage::Fragment,
                1,
                false,
                false,
                false,
                rhi::TextureViewDimension::Texture2D,
                true /* depthTexture */},
            // Rg16Float history moments are unfilterable in WebGPU. The TAA
            // shader uses SampleLevel for this input, so the layout must carry
            // the explicit unfilterable-float contract instead of inheriting
            // the filterable default.
            {6,
                rhi::DescriptorType::SampledTexture,
                rhi::ShaderStage::Fragment,
                1,
                false,
                false,
                false,
                rhi::TextureViewDimension::Texture2D,
                false,
                rhi::TextureFormat::Unknown,
                false /* unfilterable */},
            {7, rhi::DescriptorType::Sampler, rhi::ShaderStage::Fragment},
        },
    });
    desc.descriptorSets.push_back({kTaaInputSetIndex, inputLayoutName});
    desc.pushConstantRanges.push_back({
        rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
        0,
        sizeof(TaaPushConstants),
    });
    return desc;
}

struct TaaRhiDrawDesc {
    rhi::GpuTexture& currentColor;
    rhi::GpuTexture& historyColor;
    rhi::GpuTexture& velocity;
    rhi::GpuTexture& depth;
    rhi::GpuTexture& reactive;
    rhi::GpuTexture& historyDepth;
    rhi::GpuTexture& historyMoments;
    rhi::GpuTexture& outputColor;
    rhi::GpuTexture& outputMoments;
    rhi::RenderPipeline& pipeline;
    rhi::DescriptorSet& descriptorSet;
    TaaPushConstants pushConstants;
};

inline RendererResult validateTaaDrawDesc(const TaaRhiDrawDesc& desc)
{
    const auto inputExtent = desc.currentColor.desc().extent;
    const auto outputExtent = desc.outputColor.desc().extent;
    if (inputExtent.width == 0 || inputExtent.height == 0) {
        return RendererResult{false, "TAA current color extent is invalid"};
    }
    if (inputExtent.width != outputExtent.width || inputExtent.height != outputExtent.height) {
        return RendererResult{false, "TAA input and output extents must match"};
    }
    const auto historyExtent = desc.historyColor.desc().extent;
    if (historyExtent.width != inputExtent.width || historyExtent.height != inputExtent.height) {
        return RendererResult{false, "TAA history color extent must match current color"};
    }
    const auto velocityExtent = desc.velocity.desc().extent;
    if (velocityExtent.width != inputExtent.width || velocityExtent.height != inputExtent.height) {
        return RendererResult{false, "TAA velocity extent must match current color"};
    }
    const auto depthExtent = desc.depth.desc().extent;
    if (depthExtent.width != inputExtent.width || depthExtent.height != inputExtent.height) {
        return RendererResult{false, "TAA depth extent must match current color"};
    }
    for (const auto* texture :
        {&desc.reactive, &desc.historyDepth, &desc.historyMoments, &desc.outputMoments}) {
        const auto textureExtent = texture->desc().extent;
        if (textureExtent.width != inputExtent.width
            || textureExtent.height != inputExtent.height) {
            return RendererResult{false, "TAA auxiliary texture extent must match current color"};
        }
    }
    if ((desc.currentColor.desc().usage & rhi::TextureUsage::Sampled) == rhi::TextureUsage::None) {
        return RendererResult{false, "TAA current color texture must be sampled"};
    }
    if ((desc.historyColor.desc().usage & rhi::TextureUsage::Sampled) == rhi::TextureUsage::None) {
        return RendererResult{false, "TAA history color texture must be sampled"};
    }
    if ((desc.velocity.desc().usage & rhi::TextureUsage::Sampled) == rhi::TextureUsage::None) {
        return RendererResult{false, "TAA velocity texture must be sampled"};
    }
    if ((desc.depth.desc().usage & rhi::TextureUsage::Sampled) == rhi::TextureUsage::None) {
        return RendererResult{false, "TAA depth texture must be sampled"};
    }
    if ((desc.reactive.desc().usage & rhi::TextureUsage::Sampled) == rhi::TextureUsage::None
        || (desc.historyDepth.desc().usage & rhi::TextureUsage::Sampled) == rhi::TextureUsage::None
        || (desc.historyMoments.desc().usage & rhi::TextureUsage::Sampled)
            == rhi::TextureUsage::None) {
        return RendererResult{false, "TAA auxiliary inputs must be sampled"};
    }
    return RendererResult{true, ""};
}

inline RendererResult recordTaaDrawCommands(
    rhi::CommandEncoder& encoder, const TaaRhiDrawDesc& desc)
{
    const auto validated = validateTaaDrawDesc(desc);
    if (!validated.success) {
        return validated;
    }

    rhi::RenderPassDesc passDesc;
    passDesc.label = "Taa";
    passDesc.colorTargetName = desc.outputColor.desc().label;
    passDesc.colorAttachments.push_back({
        "TaaColor",
        desc.outputColor.id(),
        desc.outputColor.desc().format,
        rhi::LoadOp::DontCare,
        rhi::StoreOp::Store,
    });
    passDesc.colorAttachments.push_back({
        "TaaMoments",
        desc.outputMoments.id(),
        desc.outputMoments.desc().format,
        rhi::LoadOp::DontCare,
        rhi::StoreOp::Store,
    });
    auto pass = encoder.beginRenderPass(passDesc);
    if (!pass) {
        return RendererResult{false, encoder.lastValidation().diagnostics};
    }
    const auto outputExtent = desc.outputColor.desc().extent;
    const auto pipeline =
        bindRasterPipeline(*pass, desc.pipeline, {outputExtent.width, outputExtent.height});
    if (!pipeline.success) {
        pass->end();
        return RendererResult{false, pipeline.diagnostics};
    }
    const auto descriptor = pass->bindDescriptorSet(kTaaInputSetIndex, desc.descriptorSet);
    if (!descriptor.success) {
        pass->end();
        return RendererResult{false, descriptor.diagnostics};
    }
    const auto push = pass->pushConstants(rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
        0,
        sizeof(TaaPushConstants),
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

inline RendererResult recordTaaPass(rhi::CommandEncoder& encoder, const TaaRhiDrawDesc& desc)
{
    const auto validated = validateTaaDrawDesc(desc);
    if (!validated.success) {
        return validated;
    }

    // Transition all sampled inputs to ShaderRead. The output stays as a
    // color attachment (the render pass declares it DontCare -> Store).
    auto transitionInput = [&](rhi::GpuTexture& tex) -> RendererResult {
        if (tex.state() != rhi::ResourceState::ShaderRead) {
            const auto t = encoder.transition(tex, tex.state(), rhi::ResourceState::ShaderRead);
            if (!t.success) {
                return RendererResult{false, t.diagnostics};
            }
        }
        return RendererResult{true, ""};
    };
    const auto transitions = {
        transitionInput(desc.currentColor),
        transitionInput(desc.historyColor),
        transitionInput(desc.velocity),
        transitionInput(desc.depth),
        transitionInput(desc.reactive),
        transitionInput(desc.historyDepth),
        transitionInput(desc.historyMoments),
    };
    for (const auto& t : transitions) {
        if (!t.success) {
            return t;
        }
    }

    return recordTaaDrawCommands(encoder, desc);
}

} // namespace arp::renderer
