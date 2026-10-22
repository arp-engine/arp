#pragma once

#include "rhi/frontend/resource_state_sequence.h"

namespace arp::rhi::semantics {

class PrototypeCommandBatchPassContract final {
public:
    static const char* validate(const RenderPassDesc& desc) noexcept
    {
        return !desc.subpasses.empty() || desc.layeredRendering
            ? "RHI-PROTOTYPE-BATCH-RENDER-PASS-DOMAIN"
            : nullptr;
    }
};

// PrototypeCommandBatch/2 orders same-queue render/compute usage scopes. The
// backend closes passes before dependencies; WebGPU's ordered command buffer
// and automatic usage tracking provide visibility. Transfer, geometry fetch,
// indirect and queue ownership domains have no approved lowering here.
class PrototypeCommandBatchBarrierLowering final {
public:
    static const char* transition(const ResourceBarrierDesc& desc, bool texture) noexcept
    {
        if (desc.before == desc.after)
            return "RHI-PROTOTYPE-BATCH-BARRIER-NO-OP";
        if (const auto* id = RecordedResourceStateSequence::validateBarrier(desc, desc.before))
            return id;
        if (!state(desc.before, texture, true) || !state(desc.after, texture, false))
            return "RHI-PROTOTYPE-BATCH-BARRIER-DOMAIN";
        return scope(desc.sourceStages, desc.sourceAccess)
                && scope(desc.destinationStages, desc.destinationAccess)
            ? nullptr
            : "RHI-PROTOTYPE-BATCH-BARRIER-DOMAIN";
    }

    static const char* memory(
        const ResourceMemoryBarrierDesc& desc, ResourceState current, bool texture = false) noexcept
    {
        if (const auto* id = RecordedResourceStateSequence::validateMemoryBarrier(desc, current))
            return id;
        if (!scope(desc.sourceStages, desc.sourceAccess)
            || !scope(desc.destinationStages, desc.destinationAccess)
            || !state(current, texture, false))
            return "RHI-PROTOTYPE-BATCH-BARRIER-DOMAIN";
        return nullptr;
    }

private:
    static bool state(ResourceState value, bool texture, bool source) noexcept
    {
        return (source && value == ResourceState::Undefined) || value == ResourceState::ShaderRead
            || value == ResourceState::ShaderWrite
            || (texture
                && (value == ResourceState::ColorAttachment
                    || value == ResourceState::DepthAttachment || value == ResourceState::Present));
    }

    static bool scope(PipelineStage stages, ResourceAccess access) noexcept
    {
        const auto permittedStages = PipelineStage::TopOfPipe | PipelineStage::VertexShader
            | PipelineStage::FragmentShader | PipelineStage::ComputeShader
            | PipelineStage::ColorAttachmentOutput | PipelineStage::DepthStencil
            | PipelineStage::Present;
        const auto permittedAccess = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite
            | ResourceAccess::ColorAttachmentRead | ResourceAccess::ColorAttachmentWrite
            | ResourceAccess::DepthStencilRead | ResourceAccess::DepthStencilWrite
            | ResourceAccess::PresentRead;
        return (stages & permittedStages) == stages && (access & permittedAccess) == access;
    }
};
} // namespace arp::rhi::semantics
