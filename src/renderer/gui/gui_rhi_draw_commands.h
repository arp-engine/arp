#pragma once

#include "core/cpu_allocator.h"
#include "renderer/gui/gui_draw_plan.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/gpu_commands.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/gpu_resources.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

namespace arp::renderer {

namespace detail {

inline rhi::DescriptorLayoutDesc guiAtlasDescriptorLayout()
{
    return {
        "GuiAtlasSet",
        {
            {
                0,
                rhi::DescriptorType::SampledTexture,
                rhi::ShaderStage::Fragment,
                1,
                false,
                false,
                true,
                rhi::TextureViewDimension::Texture2D,
            },
            {
                1,
                rhi::DescriptorType::Sampler,
                rhi::ShaderStage::Fragment,
            },
        },
    };
}

} // namespace detail

struct GuiExternalTextureSetBinding {
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
    rhi::DescriptorSet* descriptorSet = nullptr;
};

inline constexpr std::uint32_t kGuiQuadIndexCount = 6u;

struct GuiRhiFramePlanDesc {
    std::uint32_t frameSlot = 0;
    std::pmr::string pipelineLayoutName = "GuiPipelineLayout";
    std::uint32_t pipelinePushConstantCapacityBytes = 0;
    std::uint32_t logicalFrameSlots = 2;
    std::uint32_t maxFeatureRequirements = 2;
    std::uint32_t maxBarriers = 0;
    std::uint32_t maxPasses = 1;
    std::uint32_t descriptorSetCount = 0;
    std::pmr::string descriptorLayoutName = "GuiFrameSet";
    std::pmr::string atlasDescriptorLayoutName = "GuiAtlasSet";
};

struct GuiRhiDrawDesc {
    rhi::GpuTexture& colorTarget;
    rhi::RenderPipeline& pipeline;
    const GuiDrawPlan& drawPlan;
    GuiRhiFramePlanDesc framePlan;
    rhi::GpuBuffer* vertexBuffer = nullptr;
    rhi::GpuBuffer* indexBuffer = nullptr;
    rhi::GpuBuffer* uploadSourceBuffer = nullptr;
    rhi::DescriptorSet* frameDescriptorSet = nullptr;
    rhi::DescriptorSet* atlasDescriptorSet = nullptr;
    std::uint64_t frameSerial = 0;
    arp::core::pmr::Vector<GuiExternalTextureSetBinding> externalTextureSets{
        &arp::core::defaultCpuMemoryResource()};
};

struct GuiRhiDrawRecordResult {
    bool success = false;
    std::pmr::string diagnostics;
    RendererRhiFramePlanBuildResult framePlan;
    std::uint32_t recordedCommandCount = 0;
    std::uint32_t recordedUploadCommandCount = 0;
    std::uint32_t recordedDrawCommandCount = 0;
    std::uint32_t recordedPushConstantCount = 0;
    bool recordedThroughCommandEncoder = false;
    bool descriptorSetBound = false;
    bool backendOwnedUploadStaging = true;
    bool removedModuleCompatibilityExplicit = true;
    bool usedRemovedModuleCompatibility = false;
    bool nativeHandleExposedToRenderer = false;
    std::uint32_t recordedTextureDescriptorBindCount = 0u;
};

inline bool guiAtlasLayoutCompatible(const rhi::DescriptorLayoutDesc& layout)
{
    return rhi::descriptorLayoutDefinitionMatches(detail::guiAtlasDescriptorLayout(), layout);
}

inline RendererRhiDrawPlanDesc buildGuiRhiDrawPlanDesc(
    const GuiDrawPlan& drawPlan, const GuiRhiFramePlanDesc& desc = {})
{
    RendererRhiDrawPlanDesc rhiPlan;
    rhiPlan.name = "Gui";
    rhiPlan.workKind = RendererRhiWorkKind::Gui;
    rhiPlan.frameSlot = desc.frameSlot;
    rhiPlan.drawCommandCount = drawPlan.commands.size() > std::numeric_limits<std::uint32_t>::max()
        ? std::numeric_limits<std::uint32_t>::max()
        : static_cast<std::uint32_t>(drawPlan.commands.size());
    rhiPlan.drawCommands.reserve(drawPlan.commands.size());
    for (const auto& command : drawPlan.commands) {
        rhiPlan.drawCommands.push_back({
            kGuiQuadIndexCount,
            command.instanceCount,
            0u,
            0,
            command.firstInstance,
        });
    }
    rhiPlan.pushConstantSizeBytes =
        drawPlan.pushConstantSizeBytes() > std::numeric_limits<std::uint32_t>::max()
        ? std::numeric_limits<std::uint32_t>::max()
        : static_cast<std::uint32_t>(drawPlan.pushConstantSizeBytes());
    rhiPlan.pipelinePushConstantCapacityBytes = desc.pipelinePushConstantCapacityBytes;
    rhiPlan.pipelineLayoutName = desc.pipelineLayoutName;
    rhiPlan.expectedPipelineLayoutName = desc.pipelineLayoutName;
    rhiPlan.descriptorSetCount = desc.descriptorSetCount;
    if (desc.descriptorSetCount > 0) {
        rhiPlan.descriptorLayoutNameCount = 1;
        rhiPlan.descriptorLayoutNames[0] = desc.descriptorLayoutName;
    }
    if (desc.descriptorSetCount > 1) {
        rhiPlan.descriptorLayoutNameCount = 2;
        rhiPlan.descriptorLayoutNames[1] = desc.atlasDescriptorLayoutName;
    }
    rhiPlan.hasInputContract = desc.descriptorSetCount > 0;
    rhiPlan.inputContract.kind = rhi::RendererInputKind::GuiDraw;
    rhiPlan.inputContract.label = "Gui";
    rhiPlan.inputContract.drawCommandCount = rhiPlan.drawCommandCount;
    rhiPlan.inputContract.vertexBytes =
        rhiPlan.drawCommandCount * 6u * static_cast<std::uint32_t>(sizeof(float) * 4u);
    rhiPlan.inputContract.indexBytes =
        rhiPlan.drawCommandCount * 6u * static_cast<std::uint32_t>(sizeof(std::uint32_t));
    rhiPlan.inputContract.descriptorBacked = desc.descriptorSetCount > 0;
    rhiPlan.inputContract.pipelineLayoutKnown = !desc.pipelineLayoutName.empty();
    rhiPlan.inputContract.explicitResourceStates = true;
    rhiPlan.inputContract.backendOwnedStaging = true;
    return rhiPlan;
}

inline GuiRhiDrawRecordResult failGuiRhiDraw(
    GuiRhiDrawRecordResult result, const std::pmr::string& diagnostics)
{
    result.success = false;
    result.diagnostics = diagnostics;
    return result;
}

inline GuiRhiDrawRecordResult recordGuiRhiDrawPlan(
    rhi::CommandEncoder& encoder, const GuiRhiDrawDesc& desc)
{
    GuiRhiDrawRecordResult result;
    result.nativeHandleExposedToRenderer = desc.colorTarget.allocation().nativeHandleExposed
        || desc.pipeline.nativeHandleExposedToUpperLayer();
    if (result.nativeHandleExposedToRenderer) {
        return failGuiRhiDraw(result, "GUI RHI draw path rejected native handle exposure");
    }
    if (!desc.drawPlan.success) {
        return failGuiRhiDraw(result,
            desc.drawPlan.diagnostics.empty() ? std::pmr::string("GUI draw plan is empty")
                                              : desc.drawPlan.diagnostics);
    }
    if (desc.drawPlan.commands.empty()) {
        return failGuiRhiDraw(result, "GUI draw plan is empty");
    }
    if (desc.drawPlan.commands.size() > std::numeric_limits<std::uint32_t>::max()) {
        return failGuiRhiDraw(result, "GUI RHI draw command count exceeds renderer capacity");
    }
    if (desc.drawPlan.pushConstantSizeBytes() > std::numeric_limits<std::uint32_t>::max()) {
        return failGuiRhiDraw(result, "GUI RHI push constant size exceeds renderer capacity");
    }
    if (desc.framePlan.descriptorSetCount > 0 && !desc.frameDescriptorSet) {
        return failGuiRhiDraw(result, "GUI RHI draw path requires a descriptor-backed frame set");
    }
    if (desc.framePlan.descriptorSetCount > 1 && !desc.atlasDescriptorSet) {
        return failGuiRhiDraw(result, "GUI RHI draw path requires a descriptor-backed atlas set");
    }
    if (!desc.indexBuffer) {
        return failGuiRhiDraw(result, "GUI RHI draw path requires an explicit quad index buffer");
    }
    if ((desc.vertexBuffer == nullptr) != (desc.uploadSourceBuffer == nullptr)) {
        return failGuiRhiDraw(
            result, "GUI RHI upload staging requires source and destination buffers");
    }
    for (std::size_t index = 0u; index < desc.externalTextureSets.size(); ++index) {
        const auto& binding = desc.externalTextureSets[index];
        if (binding.resourceId.empty() || binding.descriptorSet == nullptr
            || !guiAtlasLayoutCompatible(binding.descriptorSet->layout())) {
            return failGuiRhiDraw(result,
                "GUI external texture binding requires an identity and compatible atlas descriptor set");
        }
        for (std::size_t previous = 0u; previous < index; ++previous) {
            if (desc.externalTextureSets[previous].resourceId == binding.resourceId) {
                return failGuiRhiDraw(
                    result, "GUI external texture resource identity is duplicated");
            }
        }
    }
    for (const auto& command : desc.drawPlan.commands) {
        if (!command.externalTexture)
            continue;
        const auto matchCount = std::count_if(desc.externalTextureSets.begin(),
            desc.externalTextureSets.end(),
            [&](const auto& binding) {
                return binding.resourceId == command.textureResourceId;
            });
        if (matchCount != 1u) {
            return failGuiRhiDraw(
                result, "GUI external texture command has no unique descriptor binding");
        }
    }

    RendererRhiDrawPlanBridge bridge({
        desc.framePlan.logicalFrameSlots,
        desc.framePlan.maxFeatureRequirements,
        desc.framePlan.maxBarriers,
        desc.framePlan.maxPasses,
    });
    result.framePlan =
        bridge.buildFramePlan(buildGuiRhiDrawPlanDesc(desc.drawPlan, desc.framePlan));
    if (!result.framePlan.success) {
        return failGuiRhiDraw(result, result.framePlan.diagnostics);
    }

    if (desc.vertexBuffer && desc.uploadSourceBuffer) {
        auto transfer = encoder.beginTransferPass({"GuiUpload"});
        if (!transfer) {
            return failGuiRhiDraw(result, encoder.lastValidation().diagnostics);
        }
        const auto uploaded = transfer->uploadBuffer({
            "GuiVertexUpload",
            desc.uploadSourceBuffer->id(),
            desc.vertexBuffer->id(),
            desc.drawPlan.commands.size() * 6u * sizeof(float) * 4u,
            rhi::StagingIntent::Upload,
            true,
            desc.frameSerial,
        });
        if (!uploaded.success) {
            transfer->end();
            return failGuiRhiDraw(result, uploaded.diagnostics);
        }
        ++result.recordedUploadCommandCount;
        result.backendOwnedUploadStaging = true;
        const auto transferEnded = transfer->end();
        if (!transferEnded.success) {
            return failGuiRhiDraw(result, transferEnded.diagnostics);
        }
    }

    rhi::RenderPassDesc passDesc;
    passDesc.label = "Gui";
    passDesc.colorTargetName = desc.colorTarget.desc().label;
    passDesc.colorAttachments.reserve(1);
    passDesc.colorAttachments.push_back({
        "GuiColor",
        desc.colorTarget.id(),
        desc.colorTarget.desc().format,
        rhi::LoadOp::Load,
        rhi::StoreOp::Store,
    });

    auto pass = encoder.beginRenderPass(passDesc);
    if (!pass) {
        return failGuiRhiDraw(result, encoder.lastValidation().diagnostics);
    }

    const auto pipelineResult = pass->setPipeline(desc.pipeline);
    if (!pipelineResult.success) {
        pass->end();
        return failGuiRhiDraw(result, pipelineResult.diagnostics);
    }
    // GUI per-command constants travel through the push-constant contract;
    // GuiFrameSet is retained as an upload-side compatibility object only and
    // has no physical shader binding on WebGPU.
    const rhi::DescriptorSet* activeTextureSet = nullptr;
    if (desc.atlasDescriptorSet) {
        const auto descriptorResult = pass->bindDescriptorSet(1, *desc.atlasDescriptorSet);
        if (!descriptorResult.success) {
            pass->end();
            return failGuiRhiDraw(result, descriptorResult.diagnostics);
        }
        result.descriptorSetBound = true;
        ++result.recordedTextureDescriptorBindCount;
        activeTextureSet = desc.atlasDescriptorSet;
    }
    const auto indexBound = pass->bindIndexBuffer(*desc.indexBuffer, rhi::IndexFormat::Uint32, 0u);
    if (!indexBound.success) {
        pass->end();
        return failGuiRhiDraw(result, indexBound.diagnostics);
    }

    const auto pushConstantBytes =
        static_cast<std::uint32_t>(desc.drawPlan.pushConstantSizeBytes());
    for (const auto& command : desc.drawPlan.commands) {
        const rhi::DescriptorSet* textureSet = desc.atlasDescriptorSet;
        if (command.externalTexture) {
            const auto binding = std::find_if(desc.externalTextureSets.begin(),
                desc.externalTextureSets.end(),
                [&](const auto& entry) {
                    return entry.resourceId == command.textureResourceId;
                });
            textureSet = binding->descriptorSet;
        }
        if (textureSet != nullptr && command.textured && textureSet != activeTextureSet) {
            const auto descriptorResult = pass->bindDescriptorSet(1, *textureSet);
            if (!descriptorResult.success) {
                pass->end();
                return failGuiRhiDraw(result, descriptorResult.diagnostics);
            }
            ++result.recordedTextureDescriptorBindCount;
            activeTextureSet = textureSet;
        }
        const auto pushResult =
            pass->pushConstants(rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
                0,
                pushConstantBytes,
                &command.pushConstants);
        if (!pushResult.success) {
            pass->end();
            return failGuiRhiDraw(result, pushResult.diagnostics);
        }
        ++result.recordedPushConstantCount;

        const auto drawResult = pass->drawIndexed(
            kGuiQuadIndexCount, command.instanceCount, 0, 0, command.firstInstance);
        if (!drawResult.success) {
            pass->end();
            return failGuiRhiDraw(result, drawResult.diagnostics);
        }
        ++result.recordedDrawCommandCount;
    }

    const auto endResult = pass->end();
    if (!endResult.success) {
        return failGuiRhiDraw(result, endResult.diagnostics);
    }

    result.recordedThroughCommandEncoder = true;
    result.recordedCommandCount = endResult.recordedCommandCount;
    result.success = true;
    return result;
}

} // namespace arp::renderer
