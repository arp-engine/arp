#pragma once

#include "core/cpu_allocator.h"
#include "renderer/postprocess/fxaa_rhi_pass.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/renderer_common.h"

#include <array>
#include <memory>

namespace arp::renderer {

class FxaaRenderer final {
public:
    static constexpr std::uint32_t kFxaaDescriptorSetSlots = 2u;

    explicit FxaaRenderer(rhi::GpuDevice& device) : device_(device) {}

    RendererResult create(const FxaaRendererDesc& desc)
    {
        reset();

        const auto validated =
            detail::validateRendererCreateDesc(desc.shaderPath, desc.colorFormat, desc.extent);
        if (!validated.success) {
            return validated;
        }

        auto layoutDesc = fxaaPipelineLayoutDesc("FxaaInputSet");
        pipelineLayout_ = device_.createPipelineLayout(layoutDesc);
        if (!pipelineLayout_) {
            return RendererResult{false, "FXAA RHI pipeline layout creation failed"};
        }

        shaderPackage_ = detail::makeGraphicsShaderPackage(
            "FxaaRhi", desc.shaderPath, sizeof(FxaaPushConstants));
        const auto reflectionHydration =
            detail::validateGeneratedReflectionHydration(shaderPackage_);
        if (!reflectionHydration.success) {
            return reflectionHydration;
        }
        for (const auto& binding : layoutDesc.descriptorLayouts.front().bindings) {
            shaderPackage_.reflection.descriptorBindings.push_back(
                rhi::makeShaderReflectionBinding(kFxaaInputSetIndex, binding));
        }
        const auto shaderValidation = rhi::validateShaderPackage(shaderPackage_);
        if (!shaderValidation.success) {
            return RendererResult{false, shaderValidation.diagnostics};
        }

        auto pipelineDesc = detail::makeRenderPipelineDesc(
            "FxaaRhiPipeline", *pipelineLayout_, shaderPackage_, desc.colorFormat, desc.extent);
        useDynamicRasterViewport(pipelineDesc);
        pipeline_ = device_.createRenderPipeline(pipelineDesc);
        if (!pipeline_) {
            return RendererResult{false, "FXAA RHI render pipeline creation failed"};
        }

        descriptorArena_ = device_.createDescriptorArena({
            "FxaaDescriptorArena",
            kFxaaDescriptorSetSlots,
            kFxaaDescriptorSetSlots * 2u,
        });
        if (!descriptorArena_) {
            return RendererResult{false, "FXAA RHI descriptor arena creation failed"};
        }
        for (std::uint32_t slot = 0; slot < kFxaaDescriptorSetSlots; ++slot) {
            descriptorSets_[slot] = descriptorArena_->allocateDescriptorSet({
                "FxaaInputSet",
                layoutDesc.descriptorLayouts[0],
            });
            if (!descriptorSets_[slot]) {
                return RendererResult{false, "FXAA RHI descriptor set allocation failed"};
            }
        }
        sampler_ = device_.createSampler({
            "FxaaLinearClamp",
            rhi::SamplerFilter::Linear,
            rhi::SamplerFilter::Linear,
            rhi::SamplerAddressMode::ClampToEdge,
            rhi::SamplerAddressMode::ClampToEdge,
            rhi::SamplerAddressMode::ClampToEdge,
        });
        if (!sampler_) {
            return RendererResult{false, "FXAA RHI descriptor resource creation failed"};
        }

        return RendererResult{true, ""};
    }

    RendererResult drawFrame(const RenderFrameContext& frame,
        rhi::GpuTexture& sourceColor,
        const FxaaSettings& settings = {})
    {
        auto encoder = device_.createCommandEncoder({"FxaaRenderer"});
        if (!encoder) {
            return RendererResult{false, "FXAA RHI command encoder creation failed"};
        }
        const auto recorded = recordStandaloneFrame(*encoder, frame, sourceColor, settings);
        if (!recorded.success) {
            return recorded;
        }
        return detail::submitEncodedFrame(device_, std::move(encoder));
    }

    RendererResult recordPass(render_graph::RenderContext& context,
        const RenderFrameContext& frame,
        rhi::GpuTexture& sourceColor,
        const FxaaSettings& settings = {})
    {
        return recordGraphFrame(context.commands(), frame, sourceColor, settings);
    }

    const rhi::RenderPipeline* graphicsPipeline() const { return pipeline_.get(); }

private:
    void reset()
    {
        shaderPackage_ = {};
        pipeline_.reset();
        pipelineLayout_.reset();
        descriptorArena_.reset();
        for (auto& descriptorSet : descriptorSets_) {
            descriptorSet.reset();
        }
        sampler_.reset();
        for (auto& inputView : inputViews_) {
            inputView.reset();
        }
        for (auto& boundInputColor : boundInputColors_) {
            boundInputColor = {};
        }
    }

    RendererResult ensureInputDescriptor(rhi::GpuTexture& sourceColor, std::uint32_t slot)
    {
        if (slot >= kFxaaDescriptorSetSlots || !descriptorSets_[slot] || !sampler_) {
            return RendererResult{false, "FXAA RHI descriptor set is not ready"};
        }
        if (boundInputColors_[slot] == sourceColor.id()) {
            return RendererResult{true, ""};
        }

        inputViews_[slot] = device_.createTextureView(sourceColor,
            {
                "FxaaSceneColorInput",
                sourceColor.desc().format,
                rhi::TextureUsage::Sampled,
            });
        if (!inputViews_[slot]) {
            return RendererResult{false, "FXAA RHI input texture view creation failed"};
        }
        const auto textureWritten = descriptorSets_[slot]->write({
            0,
            rhi::DescriptorType::SampledTexture,
            inputViews_[slot]->id(),
            rhi::DescriptorResourceKind::TextureView,
        });
        if (!textureWritten.success) {
            return RendererResult{false, textureWritten.diagnostics};
        }
        const auto samplerWritten = descriptorSets_[slot]->write({
            1,
            rhi::DescriptorType::Sampler,
            sampler_->id(),
            rhi::DescriptorResourceKind::Sampler,
        });
        if (!samplerWritten.success) {
            return RendererResult{false, samplerWritten.diagnostics};
        }
        boundInputColors_[slot] = sourceColor.id();
        return RendererResult{true, ""};
    }

    RendererResult prepareFrame(
        const RenderFrameContext& frame, rhi::GpuTexture& sourceColor, std::uint32_t& slot)
    {
        if (!pipeline_) {
            return RendererResult{false, "FXAA RHI renderer is not ready"};
        }

        const auto frameValid = detail::validateFrameTarget(frame);
        if (!frameValid.success) {
            return frameValid;
        }
        slot = frame.frame.frameIndex % kFxaaDescriptorSetSlots;
        const auto descriptorReady = ensureInputDescriptor(sourceColor, slot);
        if (!descriptorReady.success) {
            return descriptorReady;
        }
        return RendererResult{true, ""};
    }

    RendererResult recordGraphFrame(rhi::CommandEncoder& encoder,
        const RenderFrameContext& frame,
        rhi::GpuTexture& sourceColor,
        const FxaaSettings& settings)
    {
        std::uint32_t slot = 0;
        const auto prepared = prepareFrame(frame, sourceColor, slot);
        if (!prepared.success) {
            return prepared;
        }

        return recordFxaaDrawCommands(encoder,
            FxaaRhiDrawDesc{
                sourceColor,
                *frame.colorTexture,
                *pipeline_,
                *descriptorSets_[slot],
                buildFxaaPushConstants(detail::extent2D(sourceColor.desc().extent), settings),
                true,
            });
    }

    RendererResult recordStandaloneFrame(rhi::CommandEncoder& encoder,
        const RenderFrameContext& frame,
        rhi::GpuTexture& sourceColor,
        const FxaaSettings& settings)
    {
        std::uint32_t slot = 0;
        const auto prepared = prepareFrame(frame, sourceColor, slot);
        if (!prepared.success) {
            return prepared;
        }

        return recordFxaaPass(encoder,
            FxaaRhiDrawDesc{
                sourceColor,
                *frame.colorTexture,
                *pipeline_,
                *descriptorSets_[slot],
                buildFxaaPushConstants(detail::extent2D(sourceColor.desc().extent), settings),
                true,
            });
    }

    rhi::GpuDevice& device_;
    rhi::ShaderPackageDesc shaderPackage_;
    arp::core::UniquePtr<rhi::PipelineLayout> pipelineLayout_;
    arp::core::UniquePtr<rhi::RenderPipeline> pipeline_;
    arp::core::UniquePtr<rhi::DescriptorArena> descriptorArena_;
    std::array<arp::core::UniquePtr<rhi::DescriptorSet>, kFxaaDescriptorSetSlots> descriptorSets_;
    arp::core::UniquePtr<rhi::GpuSampler> sampler_;
    std::array<arp::core::UniquePtr<rhi::GpuTextureView>, kFxaaDescriptorSetSlots> inputViews_;
    std::array<rhi::GpuResourceId, kFxaaDescriptorSetSlots> boundInputColors_;
};

} // namespace arp::renderer
