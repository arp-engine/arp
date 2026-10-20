#pragma once
#include "core/unsigned_range.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_graphics_triangle_range.h"
#include "rhi/backends/webgpu/webgpu_original_transfer_state_validation.h"

namespace arp::rhi_webgpu {
// Join one selected *proven* shader footprint to original inputs/resources/state.
// This grants no full-stream legality or native recording authority.
class WebGpuOriginalScalarAccessValidation final {
public:
    static const char* compute(const std::shared_ptr<const WebGpuOriginalResourceStates>& states,
        const std::shared_ptr<const WebGpuOrdinaryScalarComputeRange>& range,
        std::size_t index) noexcept
    {
        if (const auto* id = validateOwners(states, range))
            return id;
        const auto& records = states->inputs()->scalars()->arguments()->records();
        if (index >= records.size())
            return "RHI-WEBGPU-ORIGINAL-ACCESS-COMMAND";
        const auto* dispatch = records[index].scalar.get<rhi::CommandScalarArguments::Dispatch>();
        if (records[index].scalar.kind() != rhi::CommandUsageKind::Dispatch || !dispatch)
            return "RHI-WEBGPU-ORIGINAL-ACCESS-COMMAND";
        if (const auto* id = range->validateDispatch(dispatch->x, dispatch->y, dispatch->z))
            return id;
        return join(*states,
            range->originalGroups(),
            index,
            range->physicalGroup(),
            range->physicalBinding(),
            range->facts().byteOffset,
            range->facts().byteCount,
            rhi::PipelineStage::ComputeShader,
            rhi::ResourceAccess::ShaderWrite);
    }

    static const char* graphics(const std::shared_ptr<const WebGpuOriginalResourceStates>& states,
        const std::shared_ptr<const WebGpuOrdinaryGraphicsTriangleRange>& range,
        std::size_t index) noexcept
    {
        if (const auto* id = validateOwners(states, range))
            return id;
        const auto& records = states->inputs()->scalars()->arguments()->records();
        if (index >= records.size())
            return "RHI-WEBGPU-ORIGINAL-ACCESS-COMMAND";
        const auto* draw = records[index].scalar.get<rhi::CommandScalarArguments::Draw>();
        if (records[index].scalar.kind() != rhi::CommandUsageKind::Draw || !draw)
            return "RHI-WEBGPU-ORIGINAL-ACCESS-COMMAND";
        if (const auto* id = range->validateDraw(
                draw->vertices, draw->instances, draw->firstVertex, draw->firstInstance))
            return id;
        const auto& facts = range->facts();
        return join(*states,
            range->originalGroups(),
            index,
            facts.target.group,
            facts.target.binding,
            facts.fragment.byteOffset,
            facts.fragment.byteCount,
            rhi::PipelineStage::FragmentShader,
            rhi::ResourceAccess::ShaderRead);
    }

private:
    template <class Range>
    static const char* validateOwners(
        const std::shared_ptr<const WebGpuOriginalResourceStates>& states,
        const std::shared_ptr<const Range>& range) noexcept
    {
        if (!states || !states->hasQuotaBoundSharedOwner(states))
            return "RHI-WEBGPU-ORIGINAL-ACCESS-STATE-OWNER";
        if (!range || !range->hasQuotaBoundSharedOwner(range))
            return "RHI-WEBGPU-ORIGINAL-ACCESS-RANGE-OWNER";
        if (const auto* id = states->validate())
            return id;
        return range->validate();
    }

    static const char* join(const WebGpuOriginalResourceStates& states,
        const std::shared_ptr<const WebGpuOrdinaryPhysicalGroups>& groups,
        std::size_t index,
        std::uint32_t group,
        std::uint32_t binding,
        std::uint64_t offset,
        std::uint64_t bytes,
        rhi::PipelineStage stage,
        rhi::ResourceAccess access) noexcept
    {
        using K = rhi::CommandUsageKind;
        WebGpuOrdinaryPhysicalGroups::OriginalBufferBinding original;
        if (const auto* id = WebGpuOrdinaryPhysicalGroups::originalBufferBinding(
                groups, group, binding, original))
            return id;
        const auto& inputs = *states.inputs();
        const auto& graph = *inputs.scalars()->usage()->projection();
        const auto& commands = inputs.scalars()->usage()->commands();
        const auto& records = inputs.scalars()->arguments()->records();
        const std::shared_ptr<const WebGpuOrdinaryPipelineObservation>* pipeline = nullptr;
        bool bound = false;
        for (std::size_t i = 0u; i < index; ++i) {
            const auto& command = commands[i];
            switch (command.kind) {
            case K::BeginComputePass:
            case K::BeginRenderPass:
            case K::BeginTransferPass:
            case K::EndComputePass:
            case K::EndRenderPass:
            case K::EndTransferPass:
                pipeline = nullptr;
                bound = false;
                break;
            case K::BindComputePipeline:
            case K::BindGraphicsPipeline: {
                const auto& ref = graph.references()[command.firstReference];
                pipeline = &graph.pipelines()[ref.sourceIndex];
                bound = false;
                break;
            }
            case K::BindComputeDescriptor:
            case K::BindRenderDescriptor: {
                const auto& ref = graph.references()[command.firstReference];
                const auto* set =
                    records[i].scalar.get<rhi::CommandScalarArguments::DescriptorBinding>();
                if (set && set->set == original.mapping.logicalSet)
                    bound = core::SharedOwnerProjection::sameOwner(
                        graph.physicalGroups()[ref.sourceIndex], groups);
                break;
            }
            default:
                break;
            }
        }
        if (!pipeline || !core::SharedOwnerProjection::sameOwner(*pipeline, groups->pipeline()))
            return "RHI-WEBGPU-ORIGINAL-ACCESS-PIPELINE";
        if (!bound)
            return "RHI-WEBGPU-ORIGINAL-ACCESS-DESCRIPTOR";
        const auto& backing = original.resource->backing();
        const auto* state = states.find(backing.id);
        if (!state || state->source.domain != WebGpuOriginalResourceStates::Domain::Buffer
            || state->source.control
                != rhi::common::RecordedWholeResourceStateView{backing.buffer->recordedState()}
            || !core::SharedOwnerProjection::sameOwner(state->source.owner, backing.buffer))
            return "RHI-WEBGPU-ORIGINAL-ACCESS-RESOURCE";
        if (!bytes
            || !core::containsUnsignedRange(
                original.offsetBytes, original.sizeBytes, offset, bytes))
            return "RHI-WEBGPU-ORIGINAL-ACCESS-RANGE";
        rhi::ResourceState current;
        if (const auto* id = WebGpuOriginalTransferStateValidation::stateBefore(
                states, backing.id, index, current))
            return id;
        return rhi::semantics::validateResourceAccessScope({current, stage, access});
    }
};
} // namespace arp::rhi_webgpu
