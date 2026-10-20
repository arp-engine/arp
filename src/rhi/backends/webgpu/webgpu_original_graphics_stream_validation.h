#pragma once
#include "rhi/backends/webgpu/webgpu_original_scalar_access_validation.h"
#include "rhi/frontend/pipeline_single_color_validation.h"

namespace arp::rhi_webgpu {
// OriginalGraphicsStreamValidation/1: exact full-profile legality only.
// No allocation, state mutation, native payload or recorded-proof authority.
class WebGpuOriginalGraphicsStreamValidation final {
public:
    static const char* validate(const std::shared_ptr<const WebGpuOriginalResourceStates>& states,
        const std::shared_ptr<const WebGpuOrdinaryGraphicsTriangleRange>& range) noexcept
    {
        if (const auto* id = WebGpuOriginalScalarAccessValidation::graphics(states, range, 5u))
            return id;
        using K = rhi::CommandUsageKind;
        using Profile = rhi::semantics::SingleColor2DRendering;
        const auto& inputs = *states->inputs();
        const auto& usage = *inputs.scalars()->usage();
        const auto& graph = *usage.projection();
        const auto& groups = *range->originalGroups();
        const std::array<K, 8> language{K::BeginRenderPass,
            K::BindGraphicsPipeline,
            K::SetViewport,
            K::SetScissor,
            K::BindRenderDescriptor,
            K::Draw,
            K::EndRenderPass,
            K::Finish};
        if (usage.commands().size() != language.size())
            return "RHI-WEBGPU-ORIGINAL-GRAPHICS-LANGUAGE";
        for (std::size_t i = 0u; i < language.size(); ++i)
            if (usage.commands()[i].kind != language[i])
                return "RHI-WEBGPU-ORIGINAL-GRAPHICS-LANGUAGE";
        if (graph.transfer() || !graph.resources() || graph.resources()->resources().size() != 1u
            || graph.pipelines().size() != 1u || graph.physicalGroups().size() != 1u
            || graph.references().size() != 3u || groups.observedSets().size() != 1u
            || groups.observedSets()[0].observation->resources().resources().size() != 1u
            || groups.groupCount() != 1u || states->sourceReferences() != 2u
            || states->states().size() != 2u
            || !core::SharedOwnerProjection::sameOwner(graph.pipelines()[0], groups.pipeline())
            || !core::SharedOwnerProjection::sameOwner(
                graph.physicalGroups()[0], range->originalGroups())
            || !inputs.passes() || inputs.passes()->passes().size() != 1u
            || inputs.passes()->passes()[0].commandIndex != 0u)
            return "RHI-WEBGPU-ORIGINAL-GRAPHICS-SOURCE-COVERAGE";
        const auto& pass = inputs.passes()->passes()[0].arguments->desc();
        if (const auto* id = Profile::validatePass(pass))
            return id;
        const auto& target = graph.resources()->resources()[0];
        const auto& backing = target.backing();
        if (target.id() != pass.colorAttachments[0].target || !target.view() || !backing.texture)
            return "RHI-WEBGPU-ORIGINAL-GRAPHICS-TARGET";
        rhi::Extent3D extent;
        if (const auto* id = Profile::resolveTarget(
                backing.textureDesc, target.viewDesc(), pass.colorAttachments[0].format, extent))
            return id;
        const auto& pipeline = groups.pipeline()->contract();
        if (target.nativeDevice() != pipeline.nativeDevice())
            return "RHI-WEBGPU-ORDINARY-RECORDING-DEVICE-MISMATCH";
        if (const auto* id = Profile::validatePipeline(
                pipeline.graphics(), pass.colorAttachments[0].format, extent))
            return id;
        const auto& records = inputs.scalars()->arguments()->records();
        const auto* viewport = records[2u].scalar.get<rhi::ViewportState>();
        const auto* scissor = records[3u].scalar.get<rhi::ScissorState>();
        if (!viewport
            || !rhi::semantics::viewportStatesEqual(*viewport, pipeline.graphics().viewport))
            return "RHI-WEBGPU-ORIGINAL-GRAPHICS-VIEWPORT";
        if (!scissor || !rhi::semantics::scissorStatesEqual(*scissor, pipeline.graphics().scissor))
            return "RHI-WEBGPU-ORIGINAL-GRAPHICS-SCISSOR";
        const auto* state = states->find(backing.id);
        if (!state
            || state->source.domain != WebGpuOriginalResourceStates::Domain::UniformWholeTexture
            || state->source.control != backing.texture->recordedState()
            || !core::SharedOwnerProjection::sameOwner(state->source.owner, backing.texture))
            return "RHI-WEBGPU-ORIGINAL-GRAPHICS-TARGET-STATE";
        rhi::ResourceState current;
        if (const auto* id = WebGpuOriginalTransferStateValidation::stateBefore(
                *states, backing.id, 0u, current))
            return id;
        return rhi::semantics::validateResourceAccessScope({current,
            rhi::PipelineStage::ColorAttachmentOutput,
            rhi::ResourceAccess::ColorAttachmentWrite});
    }
};
} // namespace arp::rhi_webgpu
