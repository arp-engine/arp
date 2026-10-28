#pragma once

#include "rhi/frontend/command_render_pass_storage.h"
#include "rhi/frontend/compute_dispatch_recording.h"
#include "rhi/semantics/opaque_depth_graphics.h"
#include "rhi/semantics/viewport_scissor_validation.h"

namespace arp::rhi {

// Preparation/5 is an explicit private Prototype: vertex-ID Graphics, one
// set0, one color target, dynamic viewport/scissor and read-only shader inputs.
// This borrowed projection adds no inventory, journal or execution authority.
class OriginalReadOnlyGraphicsPassProjection final {
public:
    using Set = OriginalCommandDescriptorSetSource;

    static const Set* descriptor(
        const OriginalCommandStreamProgram& program, std::size_t command) noexcept
    {
        const auto* input = program.originalInputs()->descriptorAt(command);
        const auto* binding =
            input ? std::get_if<OriginalCommandDescriptorBinding>(input) : nullptr;
        if (!binding)
            return nullptr;
        const auto& sources = program.originalInputs()
                                  ->originalScalars()
                                  ->originalUsage()
                                  ->originalSources()
                                  ->sources();
        if (binding->sourceIndex >= sources.size())
            return nullptr;
        const auto* set = std::get_if<std::shared_ptr<const Set>>(&sources[binding->sourceIndex]);
        return set ? set->get() : nullptr;
    }

    static const char* inspect(const OriginalCommandStreamProgram& program,
        std::uint64_t maxVisits,
        std::uint64_t& visits) noexcept
    {
        return inspectProfile<false>(program, maxVisits, visits, false, nullptr);
    }

    static const char* inspectMixed(const OriginalCommandStreamProgram& program,
        std::uint64_t maxVisits,
        std::uint64_t& visits,
        bool opaqueDepthGraphics = false,
        const OriginalComputeDispatchRecordingInput* dispatchAdmission = nullptr,
        std::uint32_t sampledColorVersion = 0u) noexcept
    {
        return inspectProfile<true>(program,
            maxVisits,
            visits,
            opaqueDepthGraphics,
            dispatchAdmission,
            sampledColorVersion);
    }

    static const char* inspectOpaqueDepth(const OriginalCommandStreamProgram& program,
        std::uint64_t maxVisits,
        std::uint64_t& visits) noexcept
    {
        return inspectProfile<true, true>(program, maxVisits, visits, true, nullptr);
    }

    static const char* inspectTimedMixed(const OriginalCommandStreamProgram& program,
        std::uint64_t maxVisits,
        std::uint64_t& visits,
        bool opaqueDepthGraphics = false) noexcept
    {
        return inspectProfile<true, false, true>(
            program, maxVisits, visits, opaqueDepthGraphics, nullptr);
    }

    // Preparation/7 admits one explicit D32 opaque raster attachment. A Draw
    // never proves full-cell initialization: the content timeline still must
    // establish Clear/Load and Store independently of primitive coverage.
    static const char* inspectDepth(
        const RenderPipelineDesc& d, const RenderPassDesc& pass, bool opaqueDepth) noexcept
    {
        const auto format = effectiveDepthStencilFormat(d);
        if (format == TextureFormat::Unknown && !pass.hasDepthStencilAttachment)
            return nullptr;
        if (!opaqueDepth || !pass.hasDepthStencilAttachment || format != TextureFormat::D32Float
            || pass.depthStencilAttachment.format != format || !d.depthStencil.depthTestEnabled
            || !d.depthStencil.depthWriteEnabled || d.depthStencil.depthCompare != CompareOp::Less
            || d.depthStencil.stencilEnabled)
            return "RHI-ORDINARY-GRAPHICS-DEPTH-PROFILE";
        return nullptr;
    }

private:
    template <bool Mixed, bool OpaqueDepth = false, bool PassTiming = false>
    static const char* inspectProfile(const OriginalCommandStreamProgram& program,
        std::uint64_t maxVisits,
        std::uint64_t& visits,
        bool opaqueDepthGraphics,
        const OriginalComputeDispatchRecordingInput* dispatchAdmission,
        std::uint32_t sampledColorVersion = 0u) noexcept
    {
        using K = CommandUsageKind;
        if (program.originalBudget().contractVersion != 5u
            || program.originalInputs()->originalBudget().contractVersion
                != (PassTiming  ? 11u
                        : Mixed ? 10u
                                : 6u)
            || (PassTiming && !program.originalInputs()->originalBudget().passTiming))
            return "RHI-ORDINARY-GRAPHICS-PROGRAM-PROFILE";
        const auto& scalars = *program.originalInputs()->originalScalars();
        const auto& sources = scalars.originalUsage()->originalSources()->sources();
        const RenderPassDesc* pass = nullptr;
        const OriginalCommandGraphicsPipelineSource* pipeline = nullptr;
        const Set* set = nullptr;
        bool viewport = false, scissor = false;
        bool compute = false, transfer = false;
        const OriginalCommandComputePipelineSource* computePipeline = nullptr;
        const std::shared_ptr<const OriginalCommandComputePipelineSource>* computePipelineOwner =
            nullptr;
        const Set* computeSet = nullptr;
        for (const auto& op : program.operations()) {
            if (visits == maxVisits)
                return "RHI-ORDINARY-ATTACHMENT-RECORDING-COMMAND-CAPACITY";
            ++visits;
            const auto& scalar = scalars.originalArguments()->records()[op.commandIndex].scalar;
            const auto& record = scalars.originalArguments()->records()[op.commandIndex];
            if (record.offsetCount || record.byteCount)
                return "RHI-ORDINARY-GRAPHICS-VARIABLE-ARGUMENTS";
            switch (op.kind) {
            case K::Timestamp:
                if (!PassTiming || (!pass && !compute) || transfer)
                    return "RHI-MIXED-PASS-TIMING-COMMAND-PROFILE";
                break;
            case K::BeginComputePass:
                if (!Mixed || pass || compute || transfer)
                    return "RHI-ORDINARY-MIXED-PASS-ORDER";
                compute = true;
                computePipeline = nullptr;
                computePipelineOwner = nullptr;
                computeSet = nullptr;
                break;
            case K::EndComputePass:
                if (!Mixed || !compute)
                    return "RHI-ORDINARY-MIXED-PASS-ORDER";
                compute = false;
                computePipeline = nullptr;
                computePipelineOwner = nullptr;
                computeSet = nullptr;
                break;
            case K::BindComputePipeline: {
                if (!Mixed || !compute)
                    return "RHI-ORDINARY-MIXED-COMPUTE-PROFILE";
                const auto& binding = std::get<OriginalCommandPipelineBinding>(
                    *program.originalInputs()->descriptorAt(op.commandIndex));
                const auto* owner =
                    std::get_if<std::shared_ptr<const OriginalCommandComputePipelineSource>>(
                        &sources[binding.sourceIndex]);
                if (!owner || OriginalCommandComputePipelineSource::validateOriginal(*owner)
                    || !(*owner)->originalObject()->originalComputeCreation())
                    return "RHI-ORDINARY-MIXED-COMPUTE-SOURCE";
                computePipeline = owner->get();
                computePipelineOwner = owner;
                computeSet = nullptr;
                break;
            }
            case K::BindComputeDescriptor:
                computeSet = descriptor(program, op.commandIndex);
                if (!Mixed || !compute || !computePipeline || !computeSet
                    || computeSet->sourceContractVersion() != 7u
                    || computeSet->originalObject()->originalGeneralContentContractVersion() != 4u
                    || computeSet->frozenLogicalSet() != 0u)
                    return "RHI-ORDINARY-MIXED-COMPUTE-DESCRIPTOR";
                break;
            case K::Dispatch: {
                const auto* dispatch = scalar.get<CommandScalarArguments::Dispatch>();
                if (!Mixed || !compute || !computePipeline || !computeSet || !dispatch
                    || !dispatch->x || !dispatch->y || !dispatch->z)
                    return "RHI-ORDINARY-MIXED-DISPATCH-STATE";
                if (dispatchAdmission) {
                    if (!computePipelineOwner)
                        return "RHI-ORDINARY-MIXED-COMPUTE-SOURCE";
                    if (const auto* id =
                            OriginalComputeDispatchRecordingAdmission::inspect(*dispatchAdmission,
                                *computePipelineOwner,
                                {dispatch->x, dispatch->y, dispatch->z}))
                        return id;
                }
                break;
            }
            case K::BeginTransferPass:
                if (!Mixed || pass || compute || transfer)
                    return "RHI-ORDINARY-MIXED-PASS-ORDER";
                transfer = true;
                break;
            case K::TextureToBufferCopy:
                if (!Mixed || !transfer)
                    return "RHI-ORDINARY-MIXED-COPY-PROFILE";
                break;
            case K::EndTransferPass:
                if (!Mixed || !transfer)
                    return "RHI-ORDINARY-MIXED-PASS-ORDER";
                transfer = false;
                break;
            case K::BufferBarrier:
                if (!Mixed || pass || compute || transfer)
                    return "RHI-ORDINARY-MIXED-BARRIER-PROFILE";
                break;
            case K::TextureBarrier:
            case K::Finish:
                if (pass || compute || transfer)
                    return "RHI-ORDINARY-GRAPHICS-PASS-ORDER";
                break;
            case K::BeginRenderPass:
                if (compute || transfer)
                    return "RHI-ORDINARY-MIXED-PASS-ORDER";
                pass = &std::get<std::shared_ptr<const RenderPassArguments>>(
                    *program.originalInputs()->descriptorAt(op.commandIndex))
                            ->desc();
                pipeline = nullptr;
                set = nullptr;
                viewport = scissor = false;
                break;
            case K::EndRenderPass:
                pass = nullptr;
                pipeline = nullptr;
                set = nullptr;
                break;
            case K::BindGraphicsPipeline: {
                const auto& binding = std::get<OriginalCommandPipelineBinding>(
                    *program.originalInputs()->descriptorAt(op.commandIndex));
                const auto* owner =
                    std::get_if<std::shared_ptr<const OriginalCommandGraphicsPipelineSource>>(
                        &sources[binding.sourceIndex]);
                if (!pass || !owner
                    || OriginalCommandGraphicsPipelineSource::validateOriginal(*owner))
                    return "RHI-ORDINARY-GRAPHICS-PIPELINE-SOURCE";
                pipeline = owner->get();
                const auto& object = *pipeline->originalObject();
                const auto& creation = object.originalGraphicsCreation();
                if (!creation)
                    return "RHI-ORDINARY-GRAPHICS-PIPELINE-CREATION";
                const auto& d = creation->definition();
                if (!d.vertexBuffers.empty() || !d.dynamicViewportScissor
                    || !pipeline->originalLayout()->desc().pushConstantRanges.empty()
                    || pipeline->originalLayout()->desc().descriptorSets.size() != 1u
                    || pipeline->originalLayout()->desc().descriptorSets[0].set != 0u
                    || effectiveColorTargetCount(d) != 1u || pass->colorAttachments.size() != 1u
                    || effectiveColorTargetAt(d, 0u).format != pass->colorAttachments[0].format
                    || d.multisample.sampleCount != 1u)
                    return "RHI-ORDINARY-GRAPHICS-PIPELINE-PASS-PROFILE";
                if constexpr (OpaqueDepth) {
                    if (const auto* id = inspectDepth(d, *pass, true))
                        return id;
                } else if (const auto* id =
                               semantics::OpaqueDepthGraphicsContract::validateAttachments(
                                   d, *pass, opaqueDepthGraphics))
                    return id;
                set = nullptr;
                viewport = scissor = false;
                break;
            }
            case K::BindRenderDescriptor:
                set = descriptor(program, op.commandIndex);
                if (!pipeline || !set || set->sourceContractVersion() != 5u
                    || set->originalObject()->originalGeneralContentContractVersion() != 3u
                    || set->frozenLogicalSet() != 0u)
                    return "RHI-ORDINARY-GRAPHICS-DESCRIPTOR-PROFILE";
                if (set->originalObject()->originalSampledColorContractVersion()
                    && (sampledColorVersion != 1u
                        || set->originalObject()->originalSampledColorContractVersion()
                            != sampledColorVersion))
                    return "RHI-ORDINARY-SAMPLED-COLOR-RECORDING-PROFILE";
                for (const auto& element : set->frozenElements()) {
                    if (visits == maxVisits)
                        return "RHI-ORDINARY-ATTACHMENT-RECORDING-COMMAND-CAPACITY";
                    ++visits;
                    if (element.declaration.write.type != DescriptorType::UniformBuffer
                        && element.declaration.write.type != DescriptorType::ReadOnlyStorageBuffer
                        && element.declaration.write.type != DescriptorType::SampledTexture
                        && element.declaration.write.type != DescriptorType::Sampler)
                        return "RHI-ORDINARY-GRAPHICS-WRITABLE-INPUT";
                }
                break;
            case K::SetViewport:
                if (!pipeline
                    || !semantics::validateViewportCommand(*scalar.get<ViewportState>()).success)
                    return "RHI-ORDINARY-GRAPHICS-VIEWPORT";
                viewport = true;
                break;
            case K::SetScissor:
                if (!pipeline
                    || !semantics::validateScissorCommand(*scalar.get<ScissorState>()).success)
                    return "RHI-ORDINARY-GRAPHICS-SCISSOR";
                scissor = true;
                break;
            case K::Draw: {
                const auto* draw = scalar.get<CommandScalarArguments::Draw>();
                if (!pass || !pipeline || !set || !viewport || !scissor || !draw || !draw->vertices
                    || !draw->instances)
                    return "RHI-ORDINARY-GRAPHICS-DRAW-STATE";
                break;
            }
            default:
                return "RHI-ORDINARY-GRAPHICS-COMMAND-PROFILE";
            }
        }
        return nullptr;
    }
};

} // namespace arp::rhi
