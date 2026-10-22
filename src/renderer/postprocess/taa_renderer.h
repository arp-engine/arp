#pragma once

#include "core/memory/cpu_allocator.h"
#include "renderer/postprocess/taa_rhi_pass.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/renderer_common.h"

#include <array>
#include <memory>

namespace arp::renderer {

struct TaaFrameTelemetry {
    bool recorded = false;
    bool historyUsed = false;
    std::uint32_t historyReadIndex = 0u;
};

class TaaRenderer final {
public:
    static constexpr std::uint32_t kTaaDescriptorSetSlots = 2u;
    static constexpr std::uint32_t kTaaInputTextureCount = 7u;

    explicit TaaRenderer(rhi::GpuDevice& device) : device_(device) {}

    RendererResult create(const TaaRendererDesc& desc)
    {
        reset();

        const auto validated =
            detail::validateRendererCreateDesc(desc.shaderPath, desc.colorFormat, desc.extent);
        if (!validated.success) {
            return validated;
        }

        auto layoutDesc = taaPipelineLayoutDesc("TaaInputSet");
        pipelineLayout_ = device_.createPipelineLayout(layoutDesc);
        if (!pipelineLayout_) {
            return RendererResult{false, "TAA RHI pipeline layout creation failed"};
        }

        shaderPackage_ =
            detail::makeGraphicsShaderPackage("TaaRhi", desc.shaderPath, sizeof(TaaPushConstants));
        const auto reflectionHydration =
            detail::validateGeneratedReflectionHydration(shaderPackage_);
        if (!reflectionHydration.success) {
            return reflectionHydration;
        }
        // TAA reads 7 sampled textures (scene/history/velocity/depth/reactive/
        // history-depth/history-moments) + 1
        // sampler, all in set 1. Register each binding so the shader package
        // reflection matches the layout contract.
        for (const auto& binding : layoutDesc.descriptorLayouts.front().bindings) {
            shaderPackage_.reflection.descriptorBindings.push_back(
                rhi::makeShaderReflectionBinding(kTaaInputSetIndex, binding));
        }
        const auto shaderValidation = rhi::validateShaderPackage(shaderPackage_);
        if (!shaderValidation.success) {
            return RendererResult{false, shaderValidation.diagnostics};
        }

        auto pipelineDesc = detail::makeRenderPipelineDesc(
            "TaaRhiPipeline", *pipelineLayout_, shaderPackage_, desc.colorFormat, desc.extent);
        useDynamicRasterViewport(pipelineDesc);
        pipelineDesc.colorTargets = {
            {desc.colorFormat},
            {rhi::TextureFormat::Rg16Float},
        };
        pipeline_ = device_.createRenderPipeline(pipelineDesc);
        if (!pipeline_) {
            return RendererResult{false, "TAA RHI render pipeline creation failed"};
        }

        descriptorArena_ = device_.createDescriptorArena({
            "TaaDescriptorArena",
            kTaaDescriptorSetSlots,
            // Two descriptor sets, each with seven textures and one sampler.
            kTaaDescriptorSetSlots * (kTaaInputTextureCount + 1u),
        });
        if (!descriptorArena_) {
            return RendererResult{false, "TAA RHI descriptor arena creation failed"};
        }
        for (std::uint32_t slot = 0; slot < kTaaDescriptorSetSlots; ++slot) {
            descriptorSets_[slot] = descriptorArena_->allocateDescriptorSet({
                "TaaInputSet",
                layoutDesc.descriptorLayouts[0],
            });
            if (!descriptorSets_[slot]) {
                return RendererResult{false, "TAA RHI descriptor set allocation failed"};
            }
        }
        sampler_ = device_.createSampler({
            "TaaLinearClamp",
            rhi::SamplerFilter::Linear,
            rhi::SamplerFilter::Linear,
            rhi::SamplerAddressMode::ClampToEdge,
            rhi::SamplerAddressMode::ClampToEdge,
            rhi::SamplerAddressMode::ClampToEdge,
        });
        if (!sampler_) {
            return RendererResult{false, "TAA RHI sampler creation failed"};
        }

        // Persistent history color texture (ping-pong). Owned by the renderer
        // so it survives across frames; the game loop imports it into the
        // FrameGraph via importTextureRef each frame. Sampled (TAA reads it) +
        // RenderAttachment (the previous frame's resolve wrote into it).
        const auto historyCreate = createHistoryTextures(desc);
        if (!historyCreate.success) {
            return historyCreate;
        }

        extent_ = desc.extent;
        colorFormat_ = desc.colorFormat;
        historyValid_ = false;
        return RendererResult{true, ""};
    }

    RendererResult drawFrame(const RenderFrameContext& frame,
        rhi::GpuTexture& currentColor,
        rhi::GpuTexture& velocity,
        rhi::GpuTexture& depth,
        rhi::GpuTexture& reactive,
        rhi::GpuTexture& outputMoments,
        const TaaSettings& settings = {})
    {
        auto encoder = device_.createCommandEncoder({"TaaRenderer"});
        if (!encoder) {
            return RendererResult{false, "TAA RHI command encoder creation failed"};
        }
        const auto recorded = recordStandaloneFrame(
            *encoder, frame, currentColor, velocity, depth, reactive, outputMoments, settings);
        if (!recorded.success) {
            return recorded;
        }
        return detail::submitEncodedFrame(device_, std::move(encoder));
    }

    RendererResult recordPass(render_graph::RenderContext& context,
        const RenderFrameContext& frame,
        rhi::GpuTexture& currentColor,
        rhi::GpuTexture& velocity,
        rhi::GpuTexture& depth,
        rhi::GpuTexture& reactive,
        rhi::GpuTexture& outputMoments,
        const TaaSettings& settings = {})
    {
        return recordGraphFrame(context.commands(),
            frame,
            currentColor,
            velocity,
            depth,
            reactive,
            outputMoments,
            settings);
    }

    // Access the persistent history color texture. The caller imports this into
    // the FrameGraph (importTextureRef) so the resolve pass can sample it, and
    // the previous frame's resolve output is written back into it each frame
    // (ping-pong: after resolve, the current output becomes next frame's
    // history). Returns nullptr if create() has not succeeded.
    rhi::GpuTexture* historyColor() { return historyColors_[historyReadIndex_].get(); }

    std::shared_ptr<rhi::GpuTexture> historyColorRef() const
    {
        return historyColors_[historyReadIndex_];
    }

    std::shared_ptr<rhi::GpuTexture> historyWriteColorRef() const
    {
        return historyColors_[1u - historyReadIndex_];
    }

    std::shared_ptr<rhi::GpuTexture> historyDepthRef() const
    {
        return historyDepths_[historyReadIndex_];
    }

    std::shared_ptr<rhi::GpuTexture> historyWriteDepthRef() const
    {
        return historyDepths_[1u - historyReadIndex_];
    }

    std::shared_ptr<rhi::GpuTexture> historyMomentsRef() const
    {
        return historyMoments_[historyReadIndex_];
    }

    std::shared_ptr<rhi::GpuTexture> historyWriteMomentsRef() const
    {
        return historyMoments_[1u - historyReadIndex_];
    }

    // Mark the history as invalid (first frame, camera cut, resize, device
    // loss). The next resolve will bypass history blending (historyMiss path)
    // and output the current color directly.
    // Caller must admit all outstanding input leases before replacing
    // history. Pipelines/descriptors persist; resize does not compile shaders.
    RendererResult resizeHistory(rhi::Extent2D extent)
    {
        if (!pipeline_ || !extent.width || !extent.height)
            return {false, "TAA-HISTORY-RESIZE-INVALID"};
        if (extent.width == extent_.width && extent.height == extent_.height)
            return {true, {}};
        const auto result = createHistoryTextures({{}, colorFormat_, extent});
        if (!result.success)
            return result;
        extent_ = extent;
        historyValid_ = false;
        return {true, {}};
    }

    void invalidateHistory() { historyValid_ = false; }

    // Mark the history as valid after a successful resolve. The game loop
    // calls this once the resolve pass has written a fresh history color so
    // subsequent frames can accumulate. Idempotent.
    void markHistoryValid() { historyValid_ = true; }

    void advanceHistory()
    {
        historyReadIndex_ = 1u - historyReadIndex_;
        historyValid_ = true;
    }

    const rhi::RenderPipeline* graphicsPipeline() const { return pipeline_.get(); }

    TaaFrameTelemetry lastFrameTelemetry() const { return lastFrameTelemetry_; }

    rhi::GpuResourceId historyColorId() const
    {
        return historyColors_[historyReadIndex_] ? historyColors_[historyReadIndex_]->id()
                                                 : rhi::GpuResourceId{};
    }

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
        for (auto& slot : inputSlots_) {
            for (auto& view : slot.textureViews) {
                view.reset();
            }
            for (auto& id : slot.boundTextureIds) {
                id = {};
            }
        }
        for (auto& history : historyColors_)
            history.reset();
        for (auto& history : historyDepths_)
            history.reset();
        for (auto& history : historyMoments_)
            history.reset();
        historyValid_ = false;
        historyReadIndex_ = 0u;
        lastFrameTelemetry_ = {};
    }

    RendererResult createHistoryTextures(const TaaRendererDesc& desc)
    {
        for (std::uint32_t i = 0; i < 2u; ++i) {
            auto history = device_.createTexture({
                i == 0u ? "TaaHistoryColorA" : "TaaHistoryColorB",
                {desc.extent.width, desc.extent.height, 1u},
                desc.colorFormat,
                rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled
                    | rhi::TextureUsage::CopyDst,
                rhi::MemoryUsage::GpuOnly,
                rhi::ResourceState::ShaderRead,
                1u,
            });
            if (!history)
                return RendererResult{false, "TAA history color texture creation failed"};
            historyColors_[i] = std::shared_ptr<rhi::GpuTexture>(std::move(history));
            auto historyDepth = device_.createTexture({
                i == 0u ? "TaaHistoryDepthA" : "TaaHistoryDepthB",
                {desc.extent.width, desc.extent.height, 1u},
                rhi::TextureFormat::D32Float,
                rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled
                    | rhi::TextureUsage::CopyDst,
                rhi::MemoryUsage::GpuOnly,
                rhi::ResourceState::ShaderRead,
                1u,
            });
            auto historyMoments = device_.createTexture({
                i == 0u ? "TaaHistoryMomentsA" : "TaaHistoryMomentsB",
                {desc.extent.width, desc.extent.height, 1u},
                rhi::TextureFormat::Rg16Float,
                rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled
                    | rhi::TextureUsage::CopyDst,
                rhi::MemoryUsage::GpuOnly,
                rhi::ResourceState::ShaderRead,
                1u,
            });
            if (!historyDepth || !historyMoments) {
                return RendererResult{false, "TAA history depth/moments texture creation failed"};
            }
            historyDepths_[i] = std::shared_ptr<rhi::GpuTexture>(std::move(historyDepth));
            historyMoments_[i] = std::shared_ptr<rhi::GpuTexture>(std::move(historyMoments));
        }
        return RendererResult{true, ""};
    }

    RendererResult ensureInputDescriptor(rhi::GpuTexture& currentColor,
        rhi::GpuTexture& historyColor,
        rhi::GpuTexture& velocity,
        rhi::GpuTexture& depth,
        rhi::GpuTexture& reactive,
        rhi::GpuTexture& historyDepth,
        rhi::GpuTexture& historyMoments,
        std::uint32_t slot)
    {
        if (slot >= kTaaDescriptorSetSlots || !descriptorSets_[slot] || !sampler_) {
            return RendererResult{false, "TAA RHI descriptor set is not ready"};
        }

        rhi::GpuTexture* const textures[kTaaInputTextureCount] = {
            &currentColor,
            &historyColor,
            &velocity,
            &depth,
            &reactive,
            &historyDepth,
            &historyMoments,
        };
        auto& inputSlot = inputSlots_[slot];
        bool anyChanged = false;
        for (std::uint32_t i = 0; i < kTaaInputTextureCount; ++i) {
            if (inputSlot.boundTextureIds[i] != textures[i]->id()) {
                anyChanged = true;
                rhi::TextureViewDesc viewDesc;
                viewDesc.label = "TaaInputView";
                viewDesc.format = textures[i]->desc().format;
                viewDesc.usage = rhi::TextureUsage::Sampled;
                // D32 textures require an explicit depth-only view on the
                // Vulkan backend; the default color aspect is invalid.
                if (rhi::isDepthStencilFormat(viewDesc.format)) {
                    viewDesc.aspect = rhi::TextureAspect::Depth;
                }
                inputSlot.textureViews[i] = device_.createTextureView(*textures[i], viewDesc);
                if (!inputSlot.textureViews[i]) {
                    return RendererResult{false, "TAA RHI input texture view creation failed"};
                }
                inputSlot.boundTextureIds[i] = textures[i]->id();
            }
        }
        if (!anyChanged) {
            return RendererResult{true, ""};
        }

        for (std::uint32_t i = 0; i < kTaaInputTextureCount; ++i) {
            const auto textureWritten = descriptorSets_[slot]->write({
                i,
                rhi::DescriptorType::SampledTexture,
                inputSlot.textureViews[i]->id(),
                rhi::DescriptorResourceKind::TextureView,
            });
            if (!textureWritten.success) {
                return RendererResult{false, textureWritten.diagnostics};
            }
        }
        const auto samplerWritten = descriptorSets_[slot]->write({
            kTaaInputTextureCount,
            rhi::DescriptorType::Sampler,
            sampler_->id(),
            rhi::DescriptorResourceKind::Sampler,
        });
        if (!samplerWritten.success) {
            return RendererResult{false, samplerWritten.diagnostics};
        }
        return RendererResult{true, ""};
    }

    RendererResult prepareFrame(const RenderFrameContext& frame,
        rhi::GpuTexture& currentColor,
        rhi::GpuTexture& velocity,
        rhi::GpuTexture& depth,
        rhi::GpuTexture& reactive,
        std::uint32_t& slot)
    {
        if (!pipeline_ || !historyColors_[historyReadIndex_]) {
            return RendererResult{false, "TAA RHI renderer is not ready"};
        }

        const auto frameValid = detail::validateFrameTarget(frame);
        if (!frameValid.success) {
            return frameValid;
        }
        slot = frame.frame.frameIndex % kTaaDescriptorSetSlots;
        const auto descriptorReady = ensureInputDescriptor(currentColor,
            *historyColors_[historyReadIndex_],
            velocity,
            depth,
            reactive,
            *historyDepths_[historyReadIndex_],
            *historyMoments_[historyReadIndex_],
            slot);
        if (!descriptorReady.success) {
            return descriptorReady;
        }
        return RendererResult{true, ""};
    }

    RendererResult recordGraphFrame(rhi::CommandEncoder& encoder,
        const RenderFrameContext& frame,
        rhi::GpuTexture& currentColor,
        rhi::GpuTexture& velocity,
        rhi::GpuTexture& depth,
        rhi::GpuTexture& reactive,
        rhi::GpuTexture& outputMoments,
        const TaaSettings& settings)
    {
        std::uint32_t slot = 0;
        const auto prepared = prepareFrame(frame, currentColor, velocity, depth, reactive, slot);
        if (!prepared.success) {
            return prepared;
        }

        const bool historyUsed = historyValid_ && !settings.resetHistory;
        const auto recorded = recordTaaDrawCommands(encoder,
            TaaRhiDrawDesc{
                currentColor,
                *historyColors_[historyReadIndex_],
                velocity,
                depth,
                reactive,
                *historyDepths_[historyReadIndex_],
                *historyMoments_[historyReadIndex_],
                *frame.colorTexture,
                outputMoments,
                *pipeline_,
                *descriptorSets_[slot],
                buildTaaPushConstants(
                    detail::extent2D(currentColor.desc().extent), effectiveSettings(settings)),
            });
        if (recorded.success) {
            lastFrameTelemetry_ = {true, historyUsed, historyReadIndex_};
        }
        return recorded;
    }

    RendererResult recordStandaloneFrame(rhi::CommandEncoder& encoder,
        const RenderFrameContext& frame,
        rhi::GpuTexture& currentColor,
        rhi::GpuTexture& velocity,
        rhi::GpuTexture& depth,
        rhi::GpuTexture& reactive,
        rhi::GpuTexture& outputMoments,
        const TaaSettings& settings)
    {
        std::uint32_t slot = 0;
        const auto prepared = prepareFrame(frame, currentColor, velocity, depth, reactive, slot);
        if (!prepared.success) {
            return prepared;
        }

        const bool historyUsed = historyValid_ && !settings.resetHistory;
        const auto recorded = recordTaaPass(encoder,
            TaaRhiDrawDesc{
                currentColor,
                *historyColors_[historyReadIndex_],
                velocity,
                depth,
                reactive,
                *historyDepths_[historyReadIndex_],
                *historyMoments_[historyReadIndex_],
                *frame.colorTexture,
                outputMoments,
                *pipeline_,
                *descriptorSets_[slot],
                buildTaaPushConstants(
                    detail::extent2D(currentColor.desc().extent), effectiveSettings(settings)),
            });
        if (recorded.success) {
            lastFrameTelemetry_ = {true, historyUsed, historyReadIndex_};
        }
        return recorded;
    }

    // Fold the renderer's internal history-validity state into the settings:
    // if the history is invalid (first frame / cut / reset), force the
    // historyMiss flag so the resolve bypasses accumulation.
    TaaSettings effectiveSettings(const TaaSettings& settings) const
    {
        TaaSettings effective = settings;
        switch (effective.quality) {
        case TaaSettings::Quality::Low:
            // Low is intentionally cheap: no depth/moments rejection and no
            // sharpening. The shader keeps its descriptor layout invariant,
            // but takes the lightweight resolve branch.
            effective.sharpenStrength = 0.0f;
            break;
        case TaaSettings::Quality::Balanced:
            // Depth validation + YCoCg neighborhood clipping, but no
            // temporal moments clamp or sharpening.
            effective.sharpenStrength = 0.0f;
            break;
        case TaaSettings::Quality::High:
            break;
        }
        if (!historyValid_) {
            effective.resetHistory = true;
        }
        return effective;
    }

    struct InputSlot {
        std::array<arp::core::UniquePtr<rhi::GpuTextureView>, kTaaInputTextureCount> textureViews;
        std::array<rhi::GpuResourceId, kTaaInputTextureCount> boundTextureIds;
    };

    rhi::GpuDevice& device_;
    rhi::ShaderPackageDesc shaderPackage_;
    arp::core::UniquePtr<rhi::PipelineLayout> pipelineLayout_;
    arp::core::UniquePtr<rhi::RenderPipeline> pipeline_;
    arp::core::UniquePtr<rhi::DescriptorArena> descriptorArena_;
    std::array<arp::core::UniquePtr<rhi::DescriptorSet>, kTaaDescriptorSetSlots> descriptorSets_;
    arp::core::UniquePtr<rhi::GpuSampler> sampler_;
    std::array<InputSlot, kTaaDescriptorSetSlots> inputSlots_;
    // Persistent history color (ping-pong). Owned by the renderer so it
    // survives across frames; the resolve pass writes the new history here.
    std::array<std::shared_ptr<rhi::GpuTexture>, 2> historyColors_;
    std::array<std::shared_ptr<rhi::GpuTexture>, 2> historyDepths_;
    std::array<std::shared_ptr<rhi::GpuTexture>, 2> historyMoments_;
    std::uint32_t historyReadIndex_ = 0u;
    bool historyValid_ = false;
    TaaFrameTelemetry lastFrameTelemetry_;
    rhi::Extent2D extent_{};
    rhi::TextureFormat colorFormat_ = rhi::TextureFormat::Unknown;
};

} // namespace arp::renderer
