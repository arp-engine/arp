#pragma once

#include "core/memory/cpu_allocator.h"
#include "renderer/postprocess/ssao_rhi_pass.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/renderer_common.h"

#include <array>
#include <memory>

namespace arp::renderer {

class SsaoRenderer final {
public:
    static constexpr std::uint32_t kSsaoDescriptorSetSlots = 2u;

    explicit SsaoRenderer(rhi::GpuDevice& device) : device_(device) {}

    RendererResult create(const SsaoRendererDesc& desc)
    {
        reset();

        const auto validated =
            detail::validateRendererCreateDesc(desc.shaderPath, desc.colorFormat, desc.extent);
        if (!validated.success) {
            return validated;
        }

        auto layoutDesc = ssaoPipelineLayoutDesc("SsaoInputSet");
        pipelineLayout_ = device_.createPipelineLayout(layoutDesc);
        if (!pipelineLayout_) {
            return RendererResult{false, "SSAO RHI pipeline layout creation failed"};
        }

        shaderPackage_ = detail::makeGraphicsShaderPackage(
            "SsaoRhi", desc.shaderPath, sizeof(SsaoPushConstants));
        const auto reflectionHydration =
            detail::validateGeneratedReflectionHydration(shaderPackage_);
        if (!reflectionHydration.success) {
            return reflectionHydration;
        }
        for (const auto& binding : layoutDesc.descriptorLayouts.front().bindings) {
            shaderPackage_.reflection.descriptorBindings.push_back(
                rhi::makeShaderReflectionBinding(kSsaoInputSetIndex, binding));
        }
        const auto shaderValidation = rhi::validateShaderPackage(shaderPackage_);
        if (!shaderValidation.success) {
            return RendererResult{false, shaderValidation.diagnostics};
        }

        auto pipelineDesc = detail::makeRenderPipelineDesc(
            "SsaoRhiPipeline", *pipelineLayout_, shaderPackage_, desc.colorFormat, desc.extent);
        useDynamicRasterViewport(pipelineDesc);
        pipeline_ = device_.createRenderPipeline(pipelineDesc);
        if (!pipeline_) {
            const auto validation = device_.validateRenderPipeline(pipelineDesc);
            return RendererResult{
                false,
                validation.diagnostics.empty() ? "SSAO RHI render pipeline creation failed"
                                               : validation.diagnostics,
            };
        }

        descriptorArena_ = device_.createDescriptorArena({
            "SsaoDescriptorArena",
            kSsaoDescriptorSetSlots,
            kSsaoDescriptorSetSlots,
        });
        if (!descriptorArena_) {
            return RendererResult{false, "SSAO RHI descriptor arena creation failed"};
        }
        for (std::uint32_t slot = 0; slot < kSsaoDescriptorSetSlots; ++slot) {
            descriptorSets_[slot] = descriptorArena_->allocateDescriptorSet({
                "SsaoInputSet",
                layoutDesc.descriptorLayouts[0],
            });
            if (!descriptorSets_[slot]) {
                return RendererResult{false, "SSAO RHI descriptor set allocation failed"};
            }
        }
        return RendererResult{true, ""};
    }

    RendererResult drawFrame(const RenderFrameContext& frame,
        rhi::GpuTexture& sourceDepth,
        const SsaoSettings& settings = {})
    {
        auto encoder = device_.createCommandEncoder({"SsaoRenderer"});
        if (!encoder) {
            return RendererResult{false, "SSAO RHI command encoder creation failed"};
        }
        const auto recorded = recordStandaloneFrame(*encoder, frame, sourceDepth, settings);
        if (!recorded.success) {
            return recorded;
        }
        return detail::submitEncodedFrame(device_, std::move(encoder));
    }

    RendererResult recordPass(render_graph::RenderContext& context,
        const RenderFrameContext& frame,
        rhi::GpuTexture& sourceDepth,
        const SsaoSettings& settings = {})
    {
        return recordGraphFrame(context.commands(), frame, sourceDepth, settings);
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
        for (auto& inputView : depthInputViews_) {
            inputView.reset();
        }
        for (auto& boundInputDepth : boundInputDepths_) {
            boundInputDepth = {};
        }
    }

    RendererResult ensureInputDescriptor(rhi::GpuTexture& sourceDepth, std::uint32_t slot)
    {
        if (slot >= kSsaoDescriptorSetSlots || !descriptorSets_[slot]) {
            return RendererResult{false, "SSAO RHI descriptor set is not ready"};
        }
        if (boundInputDepths_[slot] == sourceDepth.id()) {
            return RendererResult{true, ""};
        }

        auto nextDepthInputView = device_.createTextureView(sourceDepth,
            {
                "SsaoSceneDepthInput",
                sourceDepth.desc().format,
                rhi::TextureUsage::Sampled,
                rhi::TextureViewDimension::Texture2D,
                0u,
                1u,
                0u,
                1u,
                rhi::TextureAspect::Depth,
            });
        if (!nextDepthInputView) {
            return RendererResult{false, "SSAO RHI depth input texture view creation failed"};
        }
        const auto depthWritten = descriptorSets_[slot]->write({
            0,
            rhi::DescriptorType::SampledTexture,
            nextDepthInputView->id(),
            rhi::DescriptorResourceKind::TextureView,
        });
        if (!depthWritten.success) {
            return RendererResult{false, depthWritten.diagnostics};
        }
        depthInputViews_[slot] = std::move(nextDepthInputView);
        boundInputDepths_[slot] = sourceDepth.id();
        return RendererResult{true, ""};
    }

    RendererResult prepareFrame(
        const RenderFrameContext& frame, rhi::GpuTexture& sourceDepth, std::uint32_t& slot)
    {
        if (!pipeline_) {
            return RendererResult{false, "SSAO RHI renderer is not ready"};
        }

        const auto frameValid = detail::validateFrameTarget(frame);
        if (!frameValid.success) {
            return frameValid;
        }
        slot = frame.frame.frameIndex % kSsaoDescriptorSetSlots;
        const auto descriptorReady = ensureInputDescriptor(sourceDepth, slot);
        if (!descriptorReady.success) {
            return descriptorReady;
        }
        return RendererResult{true, ""};
    }

    RendererResult recordGraphFrame(rhi::CommandEncoder& encoder,
        const RenderFrameContext& frame,
        rhi::GpuTexture& sourceDepth,
        const SsaoSettings& settings)
    {
        std::uint32_t slot = 0;
        const auto prepared = prepareFrame(frame, sourceDepth, slot);
        if (!prepared.success) {
            return prepared;
        }

        return recordSsaoDrawCommands(encoder,
            SsaoRhiDrawDesc{
                sourceDepth,
                *frame.colorTexture,
                *pipeline_,
                *descriptorSets_[slot],
                buildSsaoPushConstants(detail::extent2D(sourceDepth.desc().extent), settings),
                true,
            });
    }

    RendererResult recordStandaloneFrame(rhi::CommandEncoder& encoder,
        const RenderFrameContext& frame,
        rhi::GpuTexture& sourceDepth,
        const SsaoSettings& settings)
    {
        std::uint32_t slot = 0;
        const auto prepared = prepareFrame(frame, sourceDepth, slot);
        if (!prepared.success) {
            return prepared;
        }

        return recordSsaoPass(encoder,
            SsaoRhiDrawDesc{
                sourceDepth,
                *frame.colorTexture,
                *pipeline_,
                *descriptorSets_[slot],
                buildSsaoPushConstants(detail::extent2D(sourceDepth.desc().extent), settings),
                true,
            });
    }

    rhi::GpuDevice& device_;
    rhi::ShaderPackageDesc shaderPackage_;
    arp::core::UniquePtr<rhi::PipelineLayout> pipelineLayout_;
    arp::core::UniquePtr<rhi::RenderPipeline> pipeline_;
    arp::core::UniquePtr<rhi::DescriptorArena> descriptorArena_;
    std::array<arp::core::UniquePtr<rhi::DescriptorSet>, kSsaoDescriptorSetSlots> descriptorSets_;
    std::array<arp::core::UniquePtr<rhi::GpuTextureView>, kSsaoDescriptorSetSlots> depthInputViews_;
    std::array<rhi::GpuResourceId, kSsaoDescriptorSetSlots> boundInputDepths_;
};

} // namespace arp::renderer
