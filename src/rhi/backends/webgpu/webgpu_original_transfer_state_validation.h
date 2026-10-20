#pragma once
#include "rhi/backends/webgpu/webgpu_original_resource_states.h"
#include "rhi/frontend/resource_memory_barrier.h"
#include "rhi/frontend/resource_state_sequence.h"

namespace arp::rhi_webgpu {
// Original dependency/copy state validation only. No native barrier lowering,
// resource mutation, recorder friendship or command/submission authority.
class WebGpuOriginalTransferStateValidation final {
    using Sequence = rhi::semantics::RecordedResourceStateSequence;
    using K = rhi::CommandUsageKind;
    using Inputs = WebGpuOriginalCommandInputs;

public:
    struct Result {
        const char* diagnostic = nullptr;
        std::size_t commandIndex = 0u;
    };

    static Result validate(const std::shared_ptr<const WebGpuOriginalResourceStates>& states,
        std::uint32_t maxDebugDepth) noexcept
    {
        if (!states || !states->hasQuotaBoundSharedOwner(states))
            return {"RHI-WEBGPU-ORIGINAL-STATE-OWNER", 0u};
        if (const auto* id = states->validate())
            return {id, 0u};
        return validateInitialSequence(states, maxDebugDepth);
    }

    // Immutable initial intent only; current pending/committed evidence is the
    // independently owned transaction's responsibility, never inferred here.
    static Result validateInitialSequence(
        const std::shared_ptr<const WebGpuOriginalResourceStates>& states,
        std::uint32_t maxDebugDepth) noexcept
    {
        if (!states || !states->hasQuotaBoundSharedOwner(states))
            return {"RHI-WEBGPU-ORIGINAL-STATE-OWNER", 0u};
        if (const auto* id = states->validateSourceOwners())
            return {id, 0u};
        const auto& inputs = *states->inputs();
        const auto& usage = *inputs.scalars()->usage();
        const auto& commands = usage.commands();
        rhi::semantics::CommandRecordingOrder order(maxDebugDepth);
        for (std::size_t i = 0u; i < commands.size(); ++i) {
            const auto kind = commands[i].kind;
            if (const auto* id = order.append(kind))
                return {id, i};
            const char* diagnostic = nullptr;
            switch (kind) {
            case K::BeginTransferPass:
            case K::EndTransferPass:
            case K::Finish:
            case K::PushDebugGroup:
            case K::PopDebugGroup:
            case K::InsertDebugMarker:
                break;
            case K::BufferBarrier:
            case K::TextureBarrier:
            case K::BufferMemoryBarrier:
            case K::TextureMemoryBarrier:
                diagnostic = validateDependency(*states, i, kind);
                break;
            case K::BufferCopy:
            case K::BufferToTextureCopy:
            case K::TextureToBufferCopy:
            case K::TextureToTextureCopy:
            case K::BufferUpload:
                diagnostic = validateCopy(*states, i);
                break;
            case K::TextureReadback:
                diagnostic = validateReadback(*states, i);
                break;
            case K::AliasBarrier:
                diagnostic = "RHI-WEBGPU-ORIGINAL-STATE-ALIAS-UNPROVEN";
                break;
            default:
                diagnostic = "RHI-WEBGPU-ORIGINAL-STATE-COMMAND-UNPROVEN";
                break;
            }
            if (diagnostic)
                return {diagnostic, i};
        }
        return order.complete() ? Result{}
                                : Result{"RHI-COMMAND-ORDER-INCOMPLETE", commands.size()};
    }

private:
    static const Inputs::OriginalDescriptor* descriptor(
        const Inputs& inputs, std::size_t i) noexcept
    {
        for (const auto& d : inputs.descriptors())
            if (d.commandIndex == i)
                return &d;
        return nullptr;
    }

public:
    // Ordered metadata query only; full stream/lowering validation is separate.
    static const char* stateBefore(const WebGpuOriginalResourceStates& states,
        rhi::GpuResourceId id,
        std::size_t index,
        rhi::ResourceState& output) noexcept
    {
        const auto& inputs = *states.inputs();
        return Sequence::resolve(
            id,
            index,
            inputs.scalars()->usage()->commands().size(),
            [&](rhi::GpuResourceId resource) -> std::optional<rhi::ResourceState> {
                const auto* state = states.find(resource);
                if (!state)
                    return {};
                return state->observation.value;
            },
            [&](std::size_t i) -> std::optional<Sequence::Transition> {
                const auto* d = descriptor(inputs, i);
                if (!d)
                    return {};
                const auto* dependency =
                    std::get_if<rhi::ResourceDependencyArguments>(&d->arguments);
                if (!dependency)
                    return {};
                rhi::ResourceBarrierDesc barrier;
                if (!dependency->write(dependency->kind(), barrier))
                    return {};
                return Sequence::Transition{barrier.resource, barrier.before, barrier.after};
            },
            output);
    }

private:
    static const char* validateDependency(
        const WebGpuOriginalResourceStates& states, std::size_t i, K kind) noexcept
    {
        const auto* d = descriptor(*states.inputs(), i);
        const auto* dependency =
            d ? std::get_if<rhi::ResourceDependencyArguments>(&d->arguments) : nullptr;
        if (!dependency)
            return "RHI-WEBGPU-ORIGINAL-STATE-DESCRIPTOR";
        rhi::ResourceBarrierDesc barrier;
        rhi::ResourceMemoryBarrierDesc memory;
        const bool transition = kind == K::BufferBarrier || kind == K::TextureBarrier;
        if (transition ? !dependency->write(kind, barrier) : !dependency->write(kind, memory))
            return "RHI-WEBGPU-ORIGINAL-STATE-DESCRIPTOR";
        const auto resource = transition ? barrier.resource : memory.resource;
        const auto* state = states.find(resource);
        const bool texture = kind == K::TextureBarrier || kind == K::TextureMemoryBarrier;
        if (!state
            || (state->source.domain == WebGpuOriginalResourceStates::Domain::UniformWholeTexture)
                != texture)
            return "RHI-WEBGPU-ORIGINAL-STATE-DOMAIN";
        rhi::ResourceState before;
        if (const auto* id = stateBefore(states, resource, i, before))
            return id;
        if (transition) {
            if (texture) {
                const auto& graph = *states.inputs()->scalars()->usage()->projection();
                const auto& transfer = graph.transfer();
                const auto* source = transfer ? transfer->find(resource) : nullptr;
                const auto& command = states.inputs()->scalars()->usage()->commands()[i];
                const auto& reference = graph.references()[command.firstReference];
                if (reference.source == WebGpuCommandReferenceSource::OriginalTextureReadbackState
                    && reference.sourceIndex < graph.originalReadbacks().size())
                    source = &graph.originalReadbacks()[reference.sourceIndex]
                                  ->destination()
                                  ->source()
                                  ->resource();
                if (!source || !source->texture)
                    return "RHI-WEBGPU-ORIGINAL-STATE-TEXTURE-SOURCE";
                if (barrier.textureSubresources
                    != rhi::defaultTextureSubresourceRange(source->textureDesc))
                    return "RHI-WEBGPU-BARRIER-SUBRESOURCE-STATE-UNPROVEN";
            } else if (barrier.textureSubresources != rhi::TextureSubresourceRange{})
                return "RHI-WEBGPU-ORIGINAL-STATE-BUFFER-RANGE";
            if (!texture
                && (barrier.after == rhi::ResourceState::ColorAttachment
                    || barrier.after == rhi::ResourceState::DepthAttachment
                    || barrier.after == rhi::ResourceState::Present))
                return "RHI-WEBGPU-ORIGINAL-STATE-DOMAIN";
            return Sequence::validateBarrier(barrier, before);
        }
        return Sequence::validateMemoryBarrier(memory, before);
    }

    static const char* validateReadback(
        const WebGpuOriginalResourceStates& states, std::size_t i) noexcept
    {
        const auto* d = descriptor(*states.inputs(), i);
        const auto* readback =
            d ? std::get_if<rhi::TextureReadbackOriginalInputArguments>(&d->arguments) : nullptr;
        if (!readback || !readback->originalInput())
            return "RHI-WEBGPU-ORIGINAL-READBACK-INPUT";
        rhi::ReadbackTextureDesc fields;
        readback->originalInput()->scalarFields().writeScalarFields(fields);
        rhi::ResourceState source, destination;
        if (const auto* id = stateBefore(states, fields.sourceResource, i, source))
            return id;
        if (const auto* id = stateBefore(states, fields.destinationReadbackBuffer, i, destination))
            return id;
        return source == rhi::ResourceState::CopySrc && destination == rhi::ResourceState::CopyDst
            ? nullptr
            : "RHI-WEBGPU-ORIGINAL-READBACK-STATE-MISMATCH";
    }

    static const char* validateCopy(
        const WebGpuOriginalResourceStates& states, std::size_t i) noexcept
    {
        const auto* d = descriptor(*states.inputs(), i);
        const auto* copy = d ? std::get_if<rhi::TransferCopyArguments>(&d->arguments) : nullptr;
        const auto* upload = d ? std::get_if<rhi::BufferUploadArguments>(&d->arguments) : nullptr;
        if (upload)
            copy = &upload->copyArguments();
        const auto& root = states.inputs()->scalars()->usage()->projection()->transfer();
        if (!copy || !root)
            return "RHI-WEBGPU-ORIGINAL-STATE-COPY-SOURCE";
        if (const auto* id = WebGpuTransferCopyValidation::validate(*root, *copy))
            return id;
        rhi::ResourceState source, destination;
        if (const auto* id = stateBefore(states, copy->source(), i, source))
            return id;
        if (const auto* id = stateBefore(states, copy->destination(), i, destination))
            return id;
        if (source != rhi::ResourceState::CopySrc || destination != rhi::ResourceState::CopyDst)
            return "RHI-WEBGPU-ORIGINAL-STATE-COPY-MISMATCH";
        return nullptr;
    }
};
} // namespace arp::rhi_webgpu
