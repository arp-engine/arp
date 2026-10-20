#pragma once
#include "rhi/frontend/resource_access_scope.h"
#include "rhi/frontend/resource_memory_barrier.h"
#include "rhi/frontend/resource_state_validation.h"

#include <optional>

namespace arp::rhi::semantics {
// Value-only dependency validation. The caller supplies each command's union
// of real accesses to one original resource, in order. This owns no resource
// state and cannot create a native barrier or completion proof.
class RecordedResourceAccessDependencies final {
public:
    const char* consume(const ResourceAccessScope& scope) noexcept
    {
        if (const auto* id = validateResourceAccessScope(scope))
            return id;
        if (pendingAccess_ != ResourceAccess::None
            && (resourceAccessWrites(pendingAccess_) || resourceAccessWrites(scope.access)))
            return "RHI-RECORDED-RESOURCE-DEPENDENCY-MISSING";
        if (destinationDeclared_
            && !covers(destinationStages_, destinationAccess_, scope.stages, scope.access))
            return "RHI-RECORDED-RESOURCE-DEPENDENCY-DESTINATION";
        pendingStages_ = pendingStages_ | scope.stages;
        pendingAccess_ = pendingAccess_ | scope.access;
        return nullptr;
    }

    const char* depend(
        const ResourceAccessScope& source, const ResourceAccessScope& destination) noexcept
    {
        if (const auto* id = validateResourceAccessScope(source))
            return id;
        if (const auto* id = validateResourceAccessScope(destination))
            return id;
        if (!covers(source.stages, source.access, pendingStages_, pendingAccess_))
            return "RHI-RECORDED-RESOURCE-DEPENDENCY-SOURCE";
        if (destinationDeclared_ && pendingAccess_ == ResourceAccess::None
            && !covers(source.stages, source.access, destinationStages_, destinationAccess_))
            return "RHI-RECORDED-RESOURCE-DEPENDENCY-CHAIN";
        destinationDeclared_ = true;
        destinationStages_ = destination.stages;
        destinationAccess_ = destination.access;
        pendingStages_ = PipelineStage::None;
        pendingAccess_ = ResourceAccess::None;
        return nullptr;
    }

private:
    static bool covers(PipelineStage stages,
        ResourceAccess access,
        PipelineStage requiredStages,
        ResourceAccess requiredAccess) noexcept
    {
        return (stages & requiredStages) == requiredStages
            && (access & requiredAccess) == requiredAccess;
    }

    PipelineStage pendingStages_ = PipelineStage::None, destinationStages_ = PipelineStage::None;
    ResourceAccess pendingAccess_ = ResourceAccess::None, destinationAccess_ = ResourceAccess::None;
    bool destinationDeclared_ = false;
};

// Resolve a committed original resource through bounded original transition
// positions. The caller supplies original typed records, never inferred barriers.
class RecordedResourceStateSequence final {
public:
    struct Transition {
        GpuResourceId resource;
        ResourceState before, after;
    };

    template <class InitialState, class TransitionAt>
    static const char* resolve(GpuResourceId resource,
        std::size_t beforeCommand,
        std::size_t commandCount,
        InitialState&& initialState,
        TransitionAt&& transitionAt,
        ResourceState& output) noexcept
    {
        if (!resource.isValid() || beforeCommand > commandCount)
            return "RHI-STATE-SEQUENCE-INPUT";
        const auto initial = initialState(resource);
        if (!initial)
            return "RHI-STATE-SEQUENCE-RESOURCE";
        auto state = *initial;
        if (!isKnownResourceState(state))
            return "RHI-STATE-SEQUENCE-STATE-INVALID";
        for (std::size_t i = 0u; i < beforeCommand; ++i) {
            const auto transition = transitionAt(i);
            if (!transition || transition->resource != resource)
                continue;
            if (!isKnownResourceState(transition->before)
                || !isKnownResourceState(transition->after)
                || transition->after == ResourceState::Undefined)
                return "RHI-STATE-SEQUENCE-STATE-INVALID";
            if (transition->before != state)
                return "RHI-STATE-SEQUENCE-BEFORE-MISMATCH";
            state = transition->after;
        }
        output = state;
        return nullptr;
    }

    static const char* validateMemoryBarrier(
        const ResourceMemoryBarrierDesc& desc, ResourceState current) noexcept
    {
        if (const auto* id = validateResourceMemoryBarrierDesc(desc, desc.resource))
            return id;
        if (const auto* id =
                validateResourceAccessScope({current, desc.sourceStages, desc.sourceAccess}))
            return id;
        return validateResourceAccessScope(
            {current, desc.destinationStages, desc.destinationAccess});
    }

    static const char* validateBarrier(
        const ResourceBarrierDesc& desc, ResourceState current) noexcept
    {
        if (!desc.resource.isValid())
            return "RHI-STATE-SEQUENCE-RESOURCE";
        if (!isKnownResourceState(current) || !isKnownResourceState(desc.before)
            || !isKnownResourceState(desc.after) || desc.after == ResourceState::Undefined)
            return "RHI-STATE-SEQUENCE-STATE-INVALID";
        if (desc.before != current)
            return "RHI-STATE-SEQUENCE-BEFORE-MISMATCH";
        if (desc.before == ResourceState::Undefined) {
            if (desc.sourceStages != PipelineStage::TopOfPipe
                || desc.sourceAccess != ResourceAccess::None)
                return "RHI-STATE-SEQUENCE-DISCARD-SCOPE";
        } else if (const auto* id = validateResourceAccessScope(
                       {desc.before, desc.sourceStages, desc.sourceAccess}))
            return id;
        return validateResourceAccessScope(
            {desc.after, desc.destinationStages, desc.destinationAccess});
    }
};
} // namespace arp::rhi::semantics

namespace arp::rhi {
// Exact L2 dependency of the independently opted-in ordinary output readback.
class OriginalGeneralOutputReadbackDependencyContract final {
public:
    static constexpr bool contentReaderProfileMatches(
        std::uint32_t outputVersion, std::uint32_t contentVersion) noexcept
    {
        return outputVersion == 3u
            ? contentVersion == 3u
            : (outputVersion == 1u || outputVersion == 2u) && contentVersion == 0u;
    }

    // One authored transition performs one VersionedMutationValue/Array write.
    // These scalar facts never attest journal ownership or upstream completion.
    static const char* validatePendingStates(ResourceState captured,
        ResourceState initial,
        ResourceState final,
        ResourceState pending,
        std::uint64_t capturedRevision,
        std::uint64_t pendingRevision,
        std::uint32_t outputVersion = 1u) noexcept
    {
        if (outputVersion < 1u || outputVersion > 3u)
            return "RHI-GENERAL-OUTPUT-READBACK-DEPENDENCY-VERSION";
        const auto source =
            outputVersion == 3u ? ResourceState::ColorAttachment : ResourceState::ShaderWrite;
        if (captured != source || initial != captured || final != ResourceState::CopySrc
            || pending != final)
            return "RHI-GENERAL-OUTPUT-PENDING-STATES";
        return capturedRevision && capturedRevision != UINT64_MAX
                && pendingRevision == capturedRevision + 1u
            ? nullptr
            : "RHI-GENERAL-OUTPUT-PENDING-REVISION";
    }

    static const char* validate(const ResourceBarrierDesc& desc,
        ResourceState current,
        const TextureSubresourceRange& wholeRange,
        std::uint32_t outputVersion = 1u) noexcept
    {
        if (outputVersion < 1u || outputVersion > 3u)
            return "RHI-GENERAL-OUTPUT-READBACK-DEPENDENCY-VERSION";
        if (const auto* id =
                semantics::RecordedResourceStateSequence::validateBarrier(desc, current))
            return id;
        if (wholeRange != TextureSubresourceRange{} || desc.textureSubresources != wholeRange)
            return "RHI-GENERAL-OUTPUT-READBACK-DEPENDENCY-RANGE";
        const bool graphics = outputVersion == 3u;
        const auto source = graphics ? ResourceState::ColorAttachment : ResourceState::ShaderWrite;
        const auto stages =
            graphics ? PipelineStage::ColorAttachmentOutput : PipelineStage::ComputeShader;
        const auto access = graphics
            ? ResourceAccess::ColorAttachmentRead | ResourceAccess::ColorAttachmentWrite
            : ResourceAccess::ShaderWrite;
        return desc.before == source && desc.after == ResourceState::CopySrc
                && desc.sourceStages == stages && desc.sourceAccess == access
                && desc.destinationStages == PipelineStage::Transfer
                && desc.destinationAccess == ResourceAccess::TransferRead
            ? nullptr
            : "RHI-GENERAL-OUTPUT-READBACK-DEPENDENCY-SCOPE";
    }
};
} // namespace arp::rhi
