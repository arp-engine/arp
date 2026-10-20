#pragma once

#include "renderer/postprocess/ssao_draw_plan.h"
#include "renderer/raster_viewport.h"
#include "renderer/renderer_result.h"
#include "rhi/gpu_commands.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/gpu_resources.h"

namespace arp::renderer {

inline constexpr uint32_t kSsaoPushConstantSetIndex = 0u;
inline constexpr uint32_t kSsaoInputSetIndex = 1u;

inline rhi::PipelineLayoutDesc ssaoPipelineLayoutDesc(
    const std::pmr::string& inputLayoutName = "SsaoInputSet")
{
    rhi::PipelineLayoutDesc desc;
    desc.descriptorLayouts.push_back({
        inputLayoutName,
        {
            // SceneDepthTexture is a D32Float depth view. WebGPU requires the
            // layout sample type and WGSL resource type to use the depth form.
            {0,
                rhi::DescriptorType::SampledTexture,
                rhi::ShaderStage::Fragment,
                1,
                false,
                false,
                false,
                rhi::TextureViewDimension::Texture2D,
                true /*depthTexture*/},
        },
    });
    desc.descriptorSets.push_back({kSsaoInputSetIndex, inputLayoutName});
    desc.pushConstantRanges.push_back({
        rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
        0,
        sizeof(SsaoPushConstants),
    });
    return desc;
}

struct SsaoRhiDrawDesc {
    rhi::GpuTexture& inputDepth;
    rhi::GpuTexture& outputVisibility;
    rhi::RenderPipeline& pipeline;
    rhi::DescriptorSet& descriptorSet;
    SsaoPushConstants pushConstants;
    bool dynamicViewportScissor = false;
};

inline RendererResult validateSsaoDrawDesc(const SsaoRhiDrawDesc& desc)
{
    const auto inputExtent = desc.inputDepth.desc().extent;
    const auto outputExtent = desc.outputVisibility.desc().extent;
    if (inputExtent.width == 0 || inputExtent.height == 0) {
        return RendererResult{false, "SSAO input extent is invalid"};
    }
    if (inputExtent.width != outputExtent.width || inputExtent.height != outputExtent.height) {
        return RendererResult{false, "SSAO input and output extents must match"};
    }
    if ((desc.inputDepth.desc().usage & rhi::TextureUsage::Sampled) == rhi::TextureUsage::None) {
        return RendererResult{false, "SSAO input depth texture must be sampled"};
    }
    if (desc.inputDepth.desc().format == rhi::TextureFormat::Unknown) {
        return RendererResult{false, "SSAO input depth format is unknown"};
    }
    if (desc.outputVisibility.desc().format != rhi::TextureFormat::R8Unorm) {
        return RendererResult{false, "SSAO output visibility must use R8Unorm"};
    }
    if ((desc.outputVisibility.desc().usage & rhi::TextureUsage::RenderAttachment)
        == rhi::TextureUsage::None) {
        return RendererResult{false, "SSAO output visibility must be a color attachment"};
    }

    return RendererResult{true, ""};
}

inline RendererResult recordSsaoDrawCommands(
    rhi::CommandEncoder& encoder, const SsaoRhiDrawDesc& desc)
{
    const auto validated = validateSsaoDrawDesc(desc);
    if (!validated.success) {
        return validated;
    }

    rhi::RenderPassDesc passDesc;
    passDesc.label = "Ssao";
    passDesc.colorTargetName = desc.outputVisibility.desc().label;
    passDesc.colorAttachments.push_back({
        "SsaoVisibility",
        desc.outputVisibility.id(),
        desc.outputVisibility.desc().format,
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
              {desc.outputVisibility.desc().extent.width,
                  desc.outputVisibility.desc().extent.height})
        : pass->setPipeline(desc.pipeline);
    if (!pipeline.success) {
        pass->end();
        return RendererResult{false, pipeline.diagnostics};
    }
    const auto descriptor = pass->bindDescriptorSet(kSsaoInputSetIndex, desc.descriptorSet);
    if (!descriptor.success) {
        pass->end();
        return RendererResult{false, descriptor.diagnostics};
    }
    const auto push = pass->pushConstants(rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
        0,
        sizeof(SsaoPushConstants),
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

inline RendererResult recordSsaoPass(rhi::CommandEncoder& encoder, const SsaoRhiDrawDesc& desc)
{
    const auto validated = validateSsaoDrawDesc(desc);
    if (!validated.success) {
        return validated;
    }

    if (desc.inputDepth.state() != rhi::ResourceState::ShaderRead) {
        const auto transition = encoder.transition(
            desc.inputDepth, desc.inputDepth.state(), rhi::ResourceState::ShaderRead);
        if (!transition.success) {
            return RendererResult{false, transition.diagnostics};
        }
    }

    return recordSsaoDrawCommands(encoder, desc);
}

} // namespace arp::renderer
