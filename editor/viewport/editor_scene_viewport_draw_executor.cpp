#include "editor/viewport/editor_scene_viewport_draw_executor.h"

#include <limits>

namespace arp::editor {
namespace {

EditorSceneViewportDrawExecutionResult fail(const char* message)
{
    EditorSceneViewportDrawExecutionResult result;
    result.diagnostics = message;
    return result;
}

bool validViewport(const EditorSceneViewportRect& viewport, const rhi::GpuTexture& color)
{
    const auto extent = color.desc().extent;
    return viewport.width > 0u && viewport.height > 0u && viewport.x <= extent.width
        && viewport.y <= extent.height && viewport.width <= extent.width - viewport.x
        && viewport.height <= extent.height - viewport.y
        && viewport.width <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
        && viewport.height <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
}

} // namespace

EditorSceneViewportDrawExecutionResult recordEditorSceneViewportPass(rhi::GpuDevice& device,
    rhi::RenderPipeline& pipeline,
    rhi::GpuTexture& colorTarget,
    rhi::GpuTexture& depthTarget,
    const EditorSceneViewportRect& viewport,
    const core::pmr::Vector<EditorSceneViewportDrawItemSubmission>& items,
    const EditorSceneViewportDrawBudget budget,
    const bool preserveAttachments)
{
    if (device.backend() == rhi::Backend::Null)
        return fail("ARP-EDITOR-VIEWPORT-NULL-RECORDING-ONLY");
    if (items.empty())
        return fail("ARP-EDITOR-VIEWPORT-DRAW-LIST-EMPTY");
    if (budget.maxDrawCalls == 0u)
        return fail("ARP-EDITOR-VIEWPORT-DRAW-BUDGET-INVALID");
    if (!validViewport(viewport, colorTarget))
        return fail("ARP-EDITOR-VIEWPORT-RECT-OUT-OF-BOUNDS");
    const auto colorExtent = colorTarget.desc().extent;
    const auto depthExtent = depthTarget.desc().extent;
    if (depthExtent.width != colorExtent.width || depthExtent.height != colorExtent.height
        || depthExtent.depth != colorExtent.depth
        || colorTarget.desc().format != rhi::TextureFormat::Rgba16Float
        || depthTarget.desc().format != rhi::TextureFormat::D32Float
        || colorTarget.desc().sampleCount != 1u || depthTarget.desc().sampleCount != 1u) {
        return fail("ARP-EDITOR-VIEWPORT-ATTACHMENT-CONTRACT-MISMATCH");
    }
    const auto colorRequiredUsage =
        rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled;
    const auto depthRequiredUsage = rhi::TextureUsage::RenderAttachment;
    if ((colorTarget.desc().usage & colorRequiredUsage) != colorRequiredUsage
        || (depthTarget.desc().usage & depthRequiredUsage) != depthRequiredUsage) {
        return fail("ARP-EDITOR-VIEWPORT-ATTACHMENT-USAGE-MISMATCH");
    }
    std::uint64_t plannedDrawCount = 0u;
    for (const auto& item : items) {
        if (item.mesh == nullptr || !item.mesh->validForIndexedDraw()
            || item.materialSet == nullptr) {
            return fail("ARP-EDITOR-VIEWPORT-DRAW-ITEM-INCOMPLETE");
        }
        if (item.mesh->submeshCount() > budget.maxDrawCalls - plannedDrawCount)
            return fail("ARP-EDITOR-VIEWPORT-DRAW-BUDGET-EXCEEDED");
        plannedDrawCount += item.mesh->submeshCount();
    }

    auto encoder = device.createCommandEncoder({"EditorSceneViewport"});
    if (!encoder)
        return fail("ARP-EDITOR-VIEWPORT-ENCODER-CREATE-FAILED");
    if (colorTarget.state() != rhi::ResourceState::ColorAttachment) {
        const auto transitioned = encoder->transition(
            colorTarget, colorTarget.state(), rhi::ResourceState::ColorAttachment);
        if (!transitioned.success)
            return fail("ARP-EDITOR-VIEWPORT-COLOR-TRANSITION-FAILED");
    }
    if (depthTarget.state() != rhi::ResourceState::DepthAttachment) {
        const auto transitioned = encoder->transition(
            depthTarget, depthTarget.state(), rhi::ResourceState::DepthAttachment);
        if (!transitioned.success)
            return fail("ARP-EDITOR-VIEWPORT-DEPTH-TRANSITION-FAILED");
    }

    rhi::RenderPassDesc passDesc;
    passDesc.label = "EditorSceneViewport";
    passDesc.colorTargetName = colorTarget.desc().label;
    passDesc.colorAttachments.push_back({"EditorSceneViewportColor",
        colorTarget.id(),
        colorTarget.desc().format,
        preserveAttachments ? rhi::LoadOp::Load : rhi::LoadOp::Clear,
        rhi::StoreOp::Store});
    passDesc.colorAttachments.back().clearColor = {0.075f, 0.09f, 0.12f, 1.0f};
    passDesc.hasDepthStencilAttachment = true;
    passDesc.depthStencilAttachment = {"EditorSceneViewportDepth",
        depthTarget.id(),
        depthTarget.desc().format,
        preserveAttachments ? rhi::LoadOp::Load : rhi::LoadOp::Clear,
        rhi::StoreOp::Store,
        rhi::LoadOp::DontCare,
        rhi::StoreOp::DontCare,
        1.0f,
        0u,
        false,
        false};
    auto pass = encoder->beginRenderPass(passDesc);
    if (!pass)
        return fail("ARP-EDITOR-VIEWPORT-RENDER-PASS-BEGIN-FAILED");

    const auto pipelineResult = pass->setPipeline(pipeline);
    if (!pipelineResult.success) {
        pass->end();
        return fail("ARP-EDITOR-VIEWPORT-PIPELINE-BIND-FAILED");
    }
    const rhi::ViewportState rasterViewport{static_cast<float>(viewport.x),
        static_cast<float>(viewport.y),
        static_cast<float>(viewport.width),
        static_cast<float>(viewport.height),
        0.0f,
        1.0f};
    const auto viewportResult = pass->setViewport(rasterViewport);
    if (!viewportResult.success) {
        pass->end();
        return fail("ARP-EDITOR-VIEWPORT-VIEWPORT-SET-FAILED");
    }
    const rhi::ScissorState scissor{static_cast<std::int32_t>(viewport.x),
        static_cast<std::int32_t>(viewport.y),
        viewport.width,
        viewport.height};
    const auto scissorResult = pass->setScissor(scissor);
    if (!scissorResult.success) {
        pass->end();
        return fail("ARP-EDITOR-VIEWPORT-SCISSOR-SET-FAILED");
    }

    EditorSceneViewportDrawExecutionResult result;
    for (const auto& item : items) {
        const auto vertexResult = pass->bindVertexBuffer(0u, item.mesh->vertexBuffer(), 0u);
        if (!vertexResult.success) {
            pass->end();
            return fail("ARP-EDITOR-VIEWPORT-VERTEX-BIND-FAILED");
        }
        const auto indexResult =
            pass->bindIndexBuffer(item.mesh->indexBuffer(), rhi::IndexFormat::Uint32, 0u);
        if (!indexResult.success) {
            pass->end();
            return fail("ARP-EDITOR-VIEWPORT-INDEX-BIND-FAILED");
        }
        const auto descriptorResult =
            pass->bindDescriptorSet(item.materialSetIndex, *item.materialSet);
        if (!descriptorResult.success) {
            pass->end();
            return fail("ARP-EDITOR-VIEWPORT-MATERIAL-DESCRIPTOR-BIND-FAILED");
        }

        for (std::size_t submeshIndex = 0u; submeshIndex < item.mesh->submeshCount();
            ++submeshIndex) {
            const auto& submesh = item.mesh->submesh(submeshIndex);
            if (submesh.indexCount == 0u || submesh.firstIndex > item.mesh->indexCount()
                || submesh.indexCount > item.mesh->indexCount() - submesh.firstIndex) {
                pass->end();
                return fail("ARP-EDITOR-VIEWPORT-SUBMESH-RANGE-INVALID");
            }

            struct PushConstants {
                core::Mat4 viewProjection;
                core::Mat4 model;
                core::Mat4 normalTransform;
                std::array<float, 4> cameraPosition;
                std::array<float, 4> baseColorFactor;
                std::array<float, 4> emissiveMetallic;
                std::array<float, 4> roughnessOcclusionAlphaCutoffMode;
            };

            static_assert(sizeof(PushConstants) == 256u);
            const PushConstants push{item.viewProjection,
                item.model,
                item.normalTransform,
                item.cameraPosition,
                item.baseColorFactor,
                item.emissiveMetallic,
                item.roughnessOcclusionAlphaCutoffMode};
            const auto pushResult =
                pass->pushConstants(rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
                    0u,
                    sizeof(PushConstants),
                    &push);
            if (!pushResult.success) {
                pass->end();
                return fail("ARP-EDITOR-VIEWPORT-PUSH-CONSTANTS-FAILED");
            }
            const auto drawResult =
                pass->drawIndexed(submesh.indexCount, 1u, submesh.firstIndex, 0, 0u);
            if (!drawResult.success) {
                pass->end();
                return fail("ARP-EDITOR-VIEWPORT-DRAW-FAILED");
            }
            ++result.drawCount;
        }
    }
    const auto endResult = pass->end();
    if (!endResult.success)
        return fail("ARP-EDITOR-VIEWPORT-RENDER-PASS-END-FAILED");
    if (colorTarget.state() != rhi::ResourceState::ShaderRead) {
        const auto transitioned =
            encoder->transition(colorTarget, colorTarget.state(), rhi::ResourceState::ShaderRead);
        if (!transitioned.success)
            return fail("ARP-EDITOR-VIEWPORT-COLOR-SAMPLE-TRANSITION-FAILED");
    }
    result.commandBuffer = encoder->finish();
    if (!result.commandBuffer)
        return fail("ARP-EDITOR-VIEWPORT-COMMAND-FINISH-FAILED");
    return result;
}

} // namespace arp::editor
