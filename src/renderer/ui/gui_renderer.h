#pragma once

#include "core/memory/cpu_allocator.h"
#include "renderer/gui/gui_draw_plan.h"
#include "renderer/gui/gui_rhi_draw_commands.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/renderer_common.h"
#include "renderer/ui/ui_glyph_atlas.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>

namespace arp::renderer {

namespace detail {

inline constexpr std::uint32_t kGuiRendererMaxDrawCommands = 16384u;
inline constexpr std::uint32_t kGuiRendererVertexBytesPerCommand =
    6u * static_cast<std::uint32_t>(sizeof(float) * 4u);
inline constexpr std::uint32_t kGuiQuadIndices[6]{0u, 1u, 2u, 3u, 4u, 5u};

inline rhi::DescriptorLayoutDesc guiFrameDescriptorLayout()
{
    return {
        "GuiFrameSet",
        {
            {
                0,
                rhi::DescriptorType::UniformBuffer,
                rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
            },
        },
    };
}

inline rhi::PipelineLayoutDesc guiPipelineLayoutDesc()
{
    auto atlasLayout = guiAtlasDescriptorLayout();
    rhi::PipelineLayoutDesc desc;
    desc.descriptorLayouts.push_back(atlasLayout);
    desc.descriptorSets.push_back({1, atlasLayout.name});
    desc.pushConstantRanges.push_back({
        rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
        0,
        sizeof(GuiPushConstants),
    });
    return desc;
}

inline void enableGuiAlphaBlend(rhi::RenderPipelineDesc& desc)
{
    rhi::ColorTargetState target;
    target.format = desc.colorFormat;
    target.blendEnabled = true;
    target.colorBlend = {
        rhi::BlendFactor::SrcAlpha,
        rhi::BlendFactor::OneMinusSrcAlpha,
        rhi::BlendOp::Add,
    };
    target.alphaBlend = {
        rhi::BlendFactor::One,
        rhi::BlendFactor::OneMinusSrcAlpha,
        rhi::BlendOp::Add,
    };
    target.writeMask = rhi::ColorWriteMask::All;
    desc.colorTargets.push_back(target);
}

inline RendererResult uploadGuiFallbackAtlasTexture(rhi::GpuDevice& device,
    rhi::GpuTexture& texture,
    const std::uint8_t* rgba8,
    std::uint32_t width,
    std::uint32_t height)
{
    if (!rgba8 || width == 0u || height == 0u) {
        return RendererResult{false, "GUI atlas upload requires explicit RGBA8 pixels and extent"};
    }
    const auto sourceRowBytes = width * 4u;
    const auto uploadRowBytes = ((sourceRowBytes + 255u) / 256u) * 256u;
    const auto uploadSizeBytes =
        static_cast<std::uint64_t>(uploadRowBytes) * static_cast<std::uint64_t>(height);
    if (uploadSizeBytes == 0u || uploadSizeBytes > std::numeric_limits<std::uint32_t>::max()) {
        return RendererResult{false, "GUI atlas upload size is outside the supported contract"};
    }

    arp::core::pmr::Vector<std::uint8_t> uploadBytes(
        static_cast<std::size_t>(uploadSizeBytes), 0u, &arp::core::defaultCpuMemoryResource());
    for (std::uint32_t row = 0; row < height; ++row) {
        const auto sourceOffset = static_cast<std::size_t>(row) * sourceRowBytes;
        const auto uploadOffset = static_cast<std::size_t>(row) * uploadRowBytes;
        std::copy(rgba8 + sourceOffset,
            rgba8 + sourceOffset + sourceRowBytes,
            uploadBytes.begin() + static_cast<std::ptrdiff_t>(uploadOffset));
    }

    auto encoder = device.createCommandEncoder({texture.desc().label + "Upload"});
    if (!encoder) {
        return RendererResult{false, "GUI fallback atlas upload encoder creation failed"};
    }
    rhi::TextureSubresourceRange range;
    const auto transitionedToCopy = encoder->transition(
        texture, range, rhi::ResourceState::Undefined, rhi::ResourceState::CopyDst);
    if (!transitionedToCopy.success) {
        return RendererResult{false, transitionedToCopy.diagnostics};
    }
    auto transfer = encoder->beginTransferPass({texture.desc().label + "Upload"});
    if (!transfer) {
        return RendererResult{false, encoder->lastValidation().diagnostics};
    }
    rhi::UploadTextureDesc uploadDesc;
    uploadDesc.label = texture.desc().label + "Upload";
    uploadDesc.destinationTexture = texture.id();
    uploadDesc.extent = {width, height, 1u};
    uploadDesc.format = texture.desc().format;
    uploadDesc.bytesPerRow = uploadRowBytes;
    uploadDesc.rowsPerImage = height;
    uploadDesc.subresources = range;
    uploadDesc.pixelData = uploadBytes.data();
    uploadDesc.pixelDataSize = static_cast<std::uint32_t>(uploadBytes.size());
    uploadDesc.backendOwnedStaging = true;
    const auto uploaded = transfer->uploadTexture(uploadDesc);
    if (!uploaded.success) {
        transfer->end();
        return RendererResult{false, uploaded.diagnostics};
    }
    const auto ended = transfer->end();
    if (!ended.success) {
        return RendererResult{false, ended.diagnostics};
    }
    const auto transitionedToShader = encoder->transition(
        texture, range, rhi::ResourceState::CopyDst, rhi::ResourceState::ShaderRead);
    if (!transitionedToShader.success) {
        return RendererResult{false, transitionedToShader.diagnostics};
    }
    auto commands = encoder->finish();
    if (!commands) {
        return RendererResult{false, encoder->lastValidation().diagnostics};
    }
    const auto submitted = device.queue().submit(*commands);
    return RendererResult{submitted.success, submitted.diagnostics};
}

} // namespace detail

class GuiRenderer final : public UiRenderer {
public:
    explicit GuiRenderer(rhi::GpuDevice& device) : device_(device) {}

    RendererResult create(const GuiRendererDesc& desc) override
    {
        reset();

        const auto validated =
            detail::validateRendererCreateDesc(desc.shaderPath, desc.colorFormat, desc.extent);
        if (!validated.success) {
            return validated;
        }
        desc_ = desc;

        const auto layoutDesc = detail::guiPipelineLayoutDesc();
        pipelineLayout_ = device_.createPipelineLayout(layoutDesc);
        if (!pipelineLayout_) {
            return RendererResult{false, "GUI RHI pipeline layout creation failed"};
        }

        shaderPackage_ =
            detail::makeGraphicsShaderPackage("GuiRhi", desc.shaderPath, sizeof(GuiPushConstants));
        const auto reflectionHydration =
            detail::validateGeneratedReflectionHydration(shaderPackage_);
        if (!reflectionHydration.success) {
            return reflectionHydration;
        }
        // GuiPushConstants is lowered by WebGPU to its reserved group 3;
        // it is not the legacy GuiFrameSet uniform declared by the old RHI
        // draw helper.  Advertising that phantom set makes WebGPU require a
        // WGSL binding that does not exist.
        shaderPackage_.reflection.descriptorBindings.push_back({
            1,
            0,
            rhi::DescriptorType::SampledTexture,
            rhi::ShaderStage::Fragment,
        });
        shaderPackage_.reflection.descriptorBindings.push_back({
            1,
            1,
            rhi::DescriptorType::Sampler,
            rhi::ShaderStage::Fragment,
        });
        const auto shaderValidation = rhi::validateShaderPackage(shaderPackage_);
        if (!shaderValidation.success) {
            return RendererResult{false, shaderValidation.diagnostics};
        }

        auto pipelineDesc = detail::makeRenderPipelineDesc(
            "GuiRhiPipeline", *pipelineLayout_, shaderPackage_, desc.colorFormat, desc.extent);
        detail::enableGuiAlphaBlend(pipelineDesc);
        pipeline_ = device_.createRenderPipeline(pipelineDesc);
        if (!pipeline_) {
            return RendererResult{false, "GUI RHI render pipeline creation failed"};
        }

        const auto maxUploadBytes = static_cast<std::uint64_t>(detail::kGuiRendererMaxDrawCommands)
            * detail::kGuiRendererVertexBytesPerCommand;
        frameConstants_ = device_.createBuffer({
            "GuiFrameConstants",
            256u,
            rhi::ResourceUsage::UniformBuffer | rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::Undefined,
        });
        vertexBuffer_ = device_.createBuffer({
            "GuiVertices",
            maxUploadBytes,
            rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::GpuOnly,
            rhi::ResourceState::Undefined,
        });
        indexBuffer_ = device_.createBuffer({
            "GuiQuadIndices",
            sizeof(detail::kGuiQuadIndices),
            rhi::ResourceUsage::IndexBuffer | rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::ShaderRead,
        });
        uploadBuffer_ = device_.createBuffer({
            "GuiUploadSource",
            maxUploadBytes,
            rhi::ResourceUsage::CopySrc,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::Undefined,
        });
        if (!frameConstants_ || !vertexBuffer_ || !indexBuffer_ || !uploadBuffer_) {
            return RendererResult{false, "GUI RHI buffer creation failed"};
        }
        const auto indexWritten = device_.writeBuffer(
            *indexBuffer_, detail::kGuiQuadIndices, sizeof(detail::kGuiQuadIndices));
        if (!indexWritten.success) {
            return RendererResult{false, indexWritten.diagnostics};
        }

        descriptorArena_ = device_.createDescriptorArena({
            "GuiDescriptorArena",
            2u,
            3u,
        });
        if (!descriptorArena_) {
            return RendererResult{false, "GUI RHI descriptor arena creation failed"};
        }
        frameDescriptorSet_ = descriptorArena_->allocateDescriptorSet({
            "GuiFrameSet",
            detail::guiFrameDescriptorLayout(),
        });
        if (!frameDescriptorSet_) {
            return RendererResult{false, "GUI RHI descriptor set allocation failed"};
        }
        const auto descriptorWritten = frameDescriptorSet_->write({
            0,
            rhi::DescriptorType::UniformBuffer,
            frameConstants_->id(),
            rhi::DescriptorResourceKind::Buffer,
        });
        if (!descriptorWritten.success) {
            return RendererResult{false, descriptorWritten.diagnostics};
        }

        if (desc.hasAtlasTexture && !desc.atlasTexture.valid()) {
            return RendererResult{false, "GUI RHI atlas texture desc is invalid"};
        }
        const auto atlasExtent =
            desc.hasAtlasTexture ? desc.atlasTexture.extent : rhi::Extent2D{1u, 1u};
        const auto atlasLabel = desc.hasAtlasTexture && !desc.atlasTexture.resourceId.empty()
            ? desc.atlasTexture.resourceId
            : std::pmr::string("GuiFallbackAtlas");
        fallbackAtlasTexture_ = device_.createTexture({
            atlasLabel,
            {atlasExtent.width, atlasExtent.height, 1u},
            rhi::TextureFormat::Rgba8Unorm,
            rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst,
            rhi::MemoryUsage::GpuOnly,
            rhi::ResourceState::Undefined,
        });
        if (!fallbackAtlasTexture_) {
            return RendererResult{false, "GUI RHI fallback atlas texture creation failed"};
        }
        const std::array<std::uint8_t, 4u> atlasPixel{255u, 255u, 255u, 255u};
        const auto uploadedAtlas = detail::uploadGuiFallbackAtlasTexture(device_,
            *fallbackAtlasTexture_,
            desc.hasAtlasTexture ? desc.atlasTexture.rgba8.data() : atlasPixel.data(),
            atlasExtent.width,
            atlasExtent.height);
        if (!uploadedAtlas.success) {
            return uploadedAtlas;
        }
        fallbackAtlasView_ = device_.createTextureView(*fallbackAtlasTexture_,
            {
                "GuiFallbackAtlasView",
                fallbackAtlasTexture_->desc().format,
                rhi::TextureUsage::Sampled,
                rhi::TextureViewDimension::Texture2D,
            });
        fallbackAtlasSampler_ = device_.createSampler({
            "GuiFallbackAtlasSampler",
            // Linear filtering is required for SDF glyphs: the fragment shader
            // samples the distance field and reconstructs edges via smoothstep,
            // which needs bilinear-interpolated distance values. SDF atlas
            // entries are allocated with entryPadding gutters (see
            // GlyphAtlasCacheDesc::entryPadding) so Linear sampling does not
            // bleed across glyph boundaries. Box commands sample a 1x1 white
            // texture, so Linear vs Nearest is visually identical for them.
            rhi::SamplerFilter::Linear,
            rhi::SamplerFilter::Linear,
            rhi::SamplerAddressMode::ClampToEdge,
            rhi::SamplerAddressMode::ClampToEdge,
            rhi::SamplerAddressMode::ClampToEdge,
        });
        if (!fallbackAtlasView_ || !fallbackAtlasSampler_) {
            return RendererResult{false, "GUI RHI fallback atlas view/sampler creation failed"};
        }
        atlasDescriptorSet_ = descriptorArena_->allocateDescriptorSet({
            "GuiAtlasSet",
            detail::guiAtlasDescriptorLayout(),
        });
        if (!atlasDescriptorSet_) {
            return RendererResult{false, "GUI RHI atlas descriptor set allocation failed"};
        }
        const auto atlasViewWritten = atlasDescriptorSet_->write({
            0,
            rhi::DescriptorType::SampledTexture,
            fallbackAtlasView_->id(),
            rhi::DescriptorResourceKind::TextureView,
        });
        if (!atlasViewWritten.success) {
            return RendererResult{false, atlasViewWritten.diagnostics};
        }
        const auto atlasSamplerWritten = atlasDescriptorSet_->write({
            1,
            rhi::DescriptorType::Sampler,
            fallbackAtlasSampler_->id(),
            rhi::DescriptorResourceKind::Sampler,
        });
        if (!atlasSamplerWritten.success) {
            return RendererResult{false, atlasSamplerWritten.diagnostics};
        }

        return RendererResult{true, ""};
    }

    RendererResult onResize(rhi::Extent2D extent) override
    {
        if (extent.width == 0u || extent.height == 0u) {
            recoveryTelemetry_.reason = "onResize";
            recoveryTelemetry_.diagnostics = "GUI renderer onResize extent is invalid";
            return RendererResult{false, recoveryTelemetry_.diagnostics.c_str()};
        }
        desc_.extent = extent;
        reset();
        recoveryTelemetry_.reason = "onResize";
        recoveryTelemetry_.retainedBytes =
            desc_.hasAtlasTexture ? desc_.atlasTexture.rgba8.size() : 0u;
        recoveryTelemetry_.reconstructedResources = 1u;
        recoveryTelemetry_.diagnostics.clear();
        const auto result = create(desc_);
        recoveryTelemetry_.latencyMicroseconds = 0u; // Synchronous path
        return result;
    }

    RendererResult onDeviceLost() override
    {
        reset();
        recoveryTelemetry_.reason = "onDeviceLost";
        recoveryTelemetry_.generation = 0u;
        recoveryTelemetry_.retainedBytes = 0u;
        recoveryTelemetry_.reconstructedResources = 0u;
        recoveryTelemetry_.droppedWork = 1u;
        recoveryTelemetry_.latencyMicroseconds = 0u;
        recoveryTelemetry_.diagnostics.clear();
        return RendererResult{true, ""};
    }

    RendererResult recoverFromDeviceLost(const GuiRendererDesc& desc) override
    {
        reset();
        recoveryTelemetry_.reason = "recoverFromDeviceLost";
        recoveryTelemetry_.retainedBytes =
            desc.hasAtlasTexture ? desc.atlasTexture.rgba8.size() : 0u;
        recoveryTelemetry_.reconstructedResources = 1u;
        recoveryTelemetry_.droppedWork = 0u;
        recoveryTelemetry_.diagnostics.clear();
        const auto result = create(desc);
        recoveryTelemetry_.latencyMicroseconds = 0u; // Synchronous path
        return result;
    }

    void beginFrameTelemetry() override
    {
        // Keep the telemetry string on its owning PMR resource across frames;
        // aggregate assignment would make the allocator choice implicit.
        passTelemetry_.requested = false;
        passTelemetry_.recorded = false;
        passTelemetry_.atlasReady = false;
        passTelemetry_.commandCount = 0u;
        passTelemetry_.diagnostics.clear();
        passTelemetry_.atlasReady = fallbackAtlasTexture_ != nullptr
            && fallbackAtlasView_ != nullptr && fallbackAtlasSampler_ != nullptr;
    }

    UiPassTelemetry passTelemetry() const override { return passTelemetry_; }

    RendererResult drawFrame(
        const RenderFrameContext& frame, const gui::DebugDrawData& DebugDrawData) override
    {
        auto encoder = device_.createCommandEncoder({"GuiRenderer"});
        if (!encoder) {
            return RendererResult{false, "GUI RHI command encoder creation failed"};
        }
        const auto recorded = recordFrame(*encoder, frame, DebugDrawData);
        if (!recorded.success) {
            return recorded;
        }
        return detail::submitEncodedFrame(device_, std::move(encoder));
    }

    RendererResult recordPass(render_graph::RenderContext& context,
        const RenderFrameContext& frame,
        const gui::DebugDrawData& DebugDrawData) override
    {
        return recordFrame(context.commands(), frame, DebugDrawData);
    }

    RendererResult recordPass(render_graph::RenderContext& context,
        const RenderFrameContext& frame,
        const arp::core::pmr::Vector<ui::UiDrawCommand>& commands) override
    {
        return recordFrame(context.commands(), frame, commands);
    }

    RendererResult recordUiCommands(rhi::CommandEncoder& encoder,
        const RenderFrameContext& frame,
        const arp::core::pmr::Vector<ui::UiDrawCommand>& commands)
    {
        return recordFrame(encoder, frame, commands);
    }

    RendererResult recordPassWithExternalTextures(render_graph::RenderContext& context,
        const RenderFrameContext& frame,
        const arp::core::pmr::Vector<ui::UiDrawCommand>& commands,
        const arp::core::pmr::Vector<GuiExternalTextureSetBinding>& bindings)
    {
        return recordFrame(context.commands(), frame, commands, &bindings);
    }

    RendererResult updateAtlasTexture(const GuiAtlasTextureDesc& atlas,
        const arp::core::pmr::Vector<arp::renderer::ui::GlyphAtlasUploadSchedule>& uploads) override
    {
        if (!fallbackAtlasTexture_) {
            return RendererResult{false, "GUI RHI renderer atlas texture is not available"};
        }
        if (!atlas.valid()) {
            return RendererResult{false, "GUI RHI atlas update requires a valid atlas descriptor"};
        }
        const auto textureExtent = fallbackAtlasTexture_->desc().extent;
        if (atlas.extent.width != textureExtent.width
            || atlas.extent.height != textureExtent.height) {
            return RendererResult{
                false,
                "GUI RHI atlas update extent does not match the live atlas texture",
            };
        }
        if (uploads.empty()) {
            return RendererResult{true, ""};
        }

        // Validate every source rectangle before changing the tracked RHI
        // state. A rejected upload must leave the atlas in ShaderRead so the
        // next frame can still sample it or submit a valid update.
        for (const auto& upload : uploads) {
            const auto x = static_cast<std::uint32_t>(upload.atlasRect.x);
            const auto y = static_cast<std::uint32_t>(upload.atlasRect.y);
            const auto width = static_cast<std::uint32_t>(upload.atlasRect.width);
            const auto height = static_cast<std::uint32_t>(upload.atlasRect.height);
            if (width == 0u || height == 0u) {
                continue;
            }
            if (x >= atlas.extent.width || y >= atlas.extent.height
                || width > atlas.extent.width - x || height > atlas.extent.height - y) {
                return RendererResult{
                    false,
                    "GUI atlas dynamic upload rectangle is outside the atlas extent",
                };
            }
        }

        auto encoder = device_.createCommandEncoder({"GuiAtlasDynamicUpload"});
        if (!encoder) {
            return RendererResult{false, "GUI atlas dynamic upload encoder creation failed"};
        }
        rhi::TextureSubresourceRange range;
        // The atlas texture is in ShaderRead after create()'s initial upload
        // (uploadGuiFallbackAtlasTexture transitions Undefined -> CopyDst ->
        // ShaderRead and submits). Dynamic updates must transition out of
        // ShaderRead (not Undefined) to CopyDst, upload, then transition back
        // to ShaderRead so the next draw can sample it. Transitioning from
        // Undefined here would fail validation ("texture transition state
        // mismatch") because the tracked state is ShaderRead.
        const auto transitionedToCopy = encoder->transition(*fallbackAtlasTexture_,
            range,
            rhi::ResourceState::ShaderRead,
            rhi::ResourceState::CopyDst);
        if (!transitionedToCopy.success) {
            return RendererResult{false, transitionedToCopy.diagnostics};
        }
        auto transfer = encoder->beginTransferPass({"GuiAtlasDynamicUpload"});
        if (!transfer) {
            return RendererResult{false, encoder->lastValidation().diagnostics};
        }

        for (const auto& upload : uploads) {
            const auto x = static_cast<std::uint32_t>(upload.atlasRect.x);
            const auto y = static_cast<std::uint32_t>(upload.atlasRect.y);
            const auto width = static_cast<std::uint32_t>(upload.atlasRect.width);
            const auto height = static_cast<std::uint32_t>(upload.atlasRect.height);
            if (width == 0u || height == 0u) {
                continue;
            }
            const auto sourceRowBytes = width * 4u;
            const auto uploadRowBytes = ((sourceRowBytes + 255u) / 256u) * 256u;
            const auto uploadSizeBytes = static_cast<std::size_t>(uploadRowBytes) * height;

            arp::core::pmr::Vector<std::uint8_t> uploadBytes(
                uploadSizeBytes, 0u, &arp::core::defaultCpuMemoryResource());
            for (std::uint32_t row = 0; row < height; ++row) {
                const auto sourceY = y + row;
                const auto sourceOffset =
                    (static_cast<std::size_t>(sourceY) * atlas.extent.width + x) * 4u;
                const auto uploadOffset = static_cast<std::size_t>(row) * uploadRowBytes;
                const auto availableBytes =
                    atlas.rgba8.size() > sourceOffset ? atlas.rgba8.size() - sourceOffset : 0u;
                const auto copyBytes = std::min<std::size_t>(sourceRowBytes, availableBytes);
                if (copyBytes != sourceRowBytes) {
                    transfer->end();
                    return RendererResult{
                        false,
                        "GUI atlas CPU payload is smaller than its declared extent",
                    };
                }
                std::copy(atlas.rgba8.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
                    atlas.rgba8.begin() + static_cast<std::ptrdiff_t>(sourceOffset + copyBytes),
                    uploadBytes.begin() + static_cast<std::ptrdiff_t>(uploadOffset));
            }

            rhi::UploadTextureDesc uploadDesc;
            uploadDesc.label = fallbackAtlasTexture_->desc().label + "DynamicUpload";
            uploadDesc.destinationTexture = fallbackAtlasTexture_->id();
            uploadDesc.extent = rhi::Extent3D{width, height, 1u};
            // Destination origin must match the glyph's atlas slot (the same
            // (x,y) we read pixels from). Without this, the native
            // buffer-to-image copy defaults to (0,0,0) and every dynamic glyph
            // overwrites the texture's top-left corner.
            uploadDesc.destinationOffset = rhi::Extent3D{x, y, 0u};
            uploadDesc.format = fallbackAtlasTexture_->desc().format;
            uploadDesc.bytesPerRow = uploadRowBytes;
            uploadDesc.rowsPerImage = height;
            uploadDesc.subresources = range;
            uploadDesc.pixelData = uploadBytes.data();
            uploadDesc.pixelDataSize = static_cast<std::uint32_t>(uploadBytes.size());
            uploadDesc.backendOwnedStaging = true;
            const auto uploaded = transfer->uploadTexture(uploadDesc);
            if (!uploaded.success) {
                transfer->end();
                return RendererResult{false, uploaded.diagnostics};
            }
        }

        const auto ended = transfer->end();
        if (!ended.success) {
            return RendererResult{false, ended.diagnostics};
        }
        const auto transitionedToShader = encoder->transition(*fallbackAtlasTexture_,
            range,
            rhi::ResourceState::CopyDst,
            rhi::ResourceState::ShaderRead);
        if (!transitionedToShader.success) {
            return RendererResult{false, transitionedToShader.diagnostics};
        }
        auto commands = encoder->finish();
        if (!commands) {
            return RendererResult{false, encoder->lastValidation().diagnostics};
        }
        const auto submitted = device_.queue().submit(*commands);
        return RendererResult{submitted.success, submitted.diagnostics};
    }

    const rhi::RenderPipeline* graphicsPipeline() const override { return pipeline_.get(); }

    UiResourceRecoveryTelemetry recoveryTelemetry() const override { return recoveryTelemetry_; }

private:
    void reset()
    {
        shaderPackage_ = {};
        pipeline_.reset();
        pipelineLayout_.reset();
        descriptorArena_.reset();
        frameDescriptorSet_.reset();
        atlasDescriptorSet_.reset();
        frameConstants_.reset();
        vertexBuffer_.reset();
        indexBuffer_.reset();
        uploadBuffer_.reset();
        fallbackAtlasTexture_.reset();
        fallbackAtlasView_.reset();
        fallbackAtlasSampler_.reset();
    }

    RendererResult recordFrame(rhi::CommandEncoder& encoder,
        const RenderFrameContext& frame,
        const gui::DebugDrawData& DebugDrawData)
    {
        passTelemetry_.requested = true;
        passTelemetry_.atlasReady = fallbackAtlasTexture_ != nullptr
            && fallbackAtlasView_ != nullptr && fallbackAtlasSampler_ != nullptr;
        if (!pipeline_ || !frameDescriptorSet_ || !atlasDescriptorSet_ || !vertexBuffer_
            || !indexBuffer_ || !uploadBuffer_) {
            passTelemetry_.diagnostics = "GUI RHI renderer is not ready";
            return RendererResult{false, passTelemetry_.diagnostics};
        }

        const auto frameValid = detail::validateFrameTarget(frame);
        if (!frameValid.success) {
            passTelemetry_.diagnostics = frameValid.diagnostics;
            return RendererResult{false, frameValid.diagnostics};
        }

        const auto drawPlan =
            buildGuiDrawPlan(DebugDrawData, frame.colorTarget.extent, device_.backend());
        if (!drawPlan.success) {
            passTelemetry_.diagnostics = drawPlan.diagnostics;
            return RendererResult{false, drawPlan.diagnostics};
        }
        if (drawPlan.commands.size() > detail::kGuiRendererMaxDrawCommands) {
            passTelemetry_.diagnostics = "GUI RHI draw command count exceeds renderer budget";
            return RendererResult{false, passTelemetry_.diagnostics};
        }
        passTelemetry_.commandCount += static_cast<std::uint32_t>(drawPlan.commands.size());

        const auto recorded = recordGuiRhiDrawPlan(encoder,
            GuiRhiDrawDesc{
                *frame.colorTexture,
                *pipeline_,
                drawPlan,
                {
                    frame.frame.frameIndex,
                    "GuiPipelineLayout",
                    sizeof(GuiPushConstants),
                    2u,
                    2u,
                    0u,
                    1u,
                    1u,
                    "GuiAtlasSet",
                    "GuiAtlasSet",
                },
                vertexBuffer_.get(),
                indexBuffer_.get(),
                uploadBuffer_.get(),
                frameDescriptorSet_.get(),
                atlasDescriptorSet_.get(),
                frame.frame.frameSerial,
            });
        if (!recorded.success) {
            passTelemetry_.diagnostics = recorded.diagnostics;
            return RendererResult{false, recorded.diagnostics};
        }
        passTelemetry_.recorded = true;
        return RendererResult{true, ""};
    }

    RendererResult recordFrame(rhi::CommandEncoder& encoder,
        const RenderFrameContext& frame,
        const arp::core::pmr::Vector<ui::UiDrawCommand>& commands,
        const arp::core::pmr::Vector<GuiExternalTextureSetBinding>* externalTextureSets = nullptr)
    {
        passTelemetry_.requested = true;
        passTelemetry_.atlasReady = fallbackAtlasTexture_ != nullptr
            && fallbackAtlasView_ != nullptr && fallbackAtlasSampler_ != nullptr;
        if (!pipeline_ || !frameDescriptorSet_ || !atlasDescriptorSet_ || !vertexBuffer_
            || !indexBuffer_ || !uploadBuffer_) {
            passTelemetry_.diagnostics = "UI RHI renderer is not ready";
            return RendererResult{false, passTelemetry_.diagnostics};
        }

        const auto frameValid = detail::validateFrameTarget(frame);
        if (!frameValid.success) {
            passTelemetry_.diagnostics = frameValid.diagnostics;
            return RendererResult{false, frameValid.diagnostics};
        }

        const auto drawPlan =
            buildGuiDrawPlan(commands, frame.colorTarget.extent, device_.backend());
        if (!drawPlan.success) {
            passTelemetry_.diagnostics = drawPlan.diagnostics;
            return RendererResult{false, drawPlan.diagnostics};
        }
        if (drawPlan.commands.size() > detail::kGuiRendererMaxDrawCommands) {
            passTelemetry_.diagnostics = "UI RHI draw command count exceeds renderer budget";
            return RendererResult{false, passTelemetry_.diagnostics};
        }
        passTelemetry_.commandCount += static_cast<std::uint32_t>(drawPlan.commands.size());

        GuiRhiDrawDesc drawDesc{
            *frame.colorTexture,
            *pipeline_,
            drawPlan,
            {
                frame.frame.frameIndex,
                "UiPipelineLayout",
                sizeof(GuiPushConstants),
                2u,
                2u,
                0u,
                1u,
                2u,
                "UiFrameSet",
                "UiAtlasSet",
            },
            vertexBuffer_.get(),
            indexBuffer_.get(),
            uploadBuffer_.get(),
            frameDescriptorSet_.get(),
            atlasDescriptorSet_.get(),
            frame.frame.frameSerial,
        };
        if (externalTextureSets != nullptr) {
            for (const auto& binding : *externalTextureSets)
                drawDesc.externalTextureSets.push_back(binding);
        }
        const auto recorded = recordGuiRhiDrawPlan(encoder, drawDesc);
        if (!recorded.success) {
            passTelemetry_.diagnostics = recorded.diagnostics;
            return RendererResult{false, recorded.diagnostics};
        }
        passTelemetry_.recorded = true;
        return RendererResult{true, ""};
    }

    rhi::GpuDevice& device_;
    GuiRendererDesc desc_;
    rhi::ShaderPackageDesc shaderPackage_;
    arp::core::UniquePtr<rhi::PipelineLayout> pipelineLayout_;
    arp::core::UniquePtr<rhi::RenderPipeline> pipeline_;
    arp::core::UniquePtr<rhi::DescriptorArena> descriptorArena_;
    arp::core::UniquePtr<rhi::DescriptorSet> frameDescriptorSet_;
    arp::core::UniquePtr<rhi::DescriptorSet> atlasDescriptorSet_;
    arp::core::UniquePtr<rhi::GpuBuffer> frameConstants_;
    arp::core::UniquePtr<rhi::GpuBuffer> vertexBuffer_;
    arp::core::UniquePtr<rhi::GpuBuffer> indexBuffer_;
    arp::core::UniquePtr<rhi::GpuBuffer> uploadBuffer_;
    arp::core::UniquePtr<rhi::GpuTexture> fallbackAtlasTexture_;
    arp::core::UniquePtr<rhi::GpuTextureView> fallbackAtlasView_;
    arp::core::UniquePtr<rhi::GpuSampler> fallbackAtlasSampler_;
    mutable UiResourceRecoveryTelemetry recoveryTelemetry_;
    mutable UiPassTelemetry passTelemetry_;
};

} // namespace arp::renderer
