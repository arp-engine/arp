#pragma once
#include "rhi/frontend/resource_state_sequence.h"

namespace arp::rhi::semantics {
// TransferDependencyLowering/1: a versioned normative mapping decision only.
// A decision cannot issue native commands, capabilities or recorded proof.
class TransferDependencyLowering final {
public:
    enum class Operation { DiagnosticFailure, ImplicitOrderedUsage };

    struct Decision {
        CapabilityMode outcome = CapabilityMode::DiagnosticFailure;
        Operation operation = Operation::DiagnosticFailure;
        const char* diagnostic = "RHI-TRANSFER-DEPENDENCY-UNDECIDED";
    };

    static Decision transition(Backend backend,
        std::uint32_t version,
        bool optIn,
        const ResourceBarrierDesc& desc,
        ResourceState current,
        bool texture,
        const TextureSubresourceRange& wholeRange = {}) noexcept
    {
        if (const auto* id = gate(backend, version, optIn))
            return failure(id);
        if (const auto* id = validateTransitionIntent(desc, current, texture, wholeRange))
            return failure(id);
        return equivalent();
    }

    // Reusable L2 descriptor validation, with no backend choice or execution
    // authority. Native lowering consumes it after its own backend gate.
    static const char* validateTransitionIntent(const ResourceBarrierDesc& desc,
        ResourceState current,
        bool texture,
        const TextureSubresourceRange& wholeRange = {}) noexcept
    {
        if (const auto* id = RecordedResourceStateSequence::validateBarrier(desc, current))
            return id;
        if (texture ? !wholeRange.mipLevelCount || !wholeRange.arrayLayerCount
                    || wholeRange.baseMipLevel || wholeRange.baseArrayLayer
                    || (wholeRange.aspect != TextureAspect::Color
                        && wholeRange.aspect != TextureAspect::Depth
                        && wholeRange.aspect != TextureAspect::Stencil)
                    || desc.textureSubresources != wholeRange
                    : desc.textureSubresources != TextureSubresourceRange{})
            return "RHI-TRANSFER-DEPENDENCY-RANGE-UNPROVEN";
        if (!scope(desc.after, desc.destinationStages, desc.destinationAccess))
            return "RHI-TRANSFER-DEPENDENCY-SCOPE-UNPROVEN";
        if (desc.before != ResourceState::Undefined
            && !scope(desc.before, desc.sourceStages, desc.sourceAccess))
            return "RHI-TRANSFER-DEPENDENCY-SCOPE-UNPROVEN";
        return nullptr;
    }

    static Decision memory(Backend backend,
        std::uint32_t version,
        bool optIn,
        const ResourceMemoryBarrierDesc& desc,
        ResourceState current) noexcept
    {
        if (const auto* id = gate(backend, version, optIn))
            return failure(id);
        if (const auto* id = RecordedResourceStateSequence::validateMemoryBarrier(desc, current))
            return failure(id);
        if (!scope(current, desc.sourceStages, desc.sourceAccess)
            || !scope(current, desc.destinationStages, desc.destinationAccess))
            return failure("RHI-TRANSFER-DEPENDENCY-SCOPE-UNPROVEN");
        return equivalent();
    }

private:
    static bool scope(ResourceState state, PipelineStage stage, ResourceAccess access) noexcept
    {
        return stage == PipelineStage::Transfer
            && ((state == ResourceState::CopySrc && access == ResourceAccess::TransferRead)
                || (state == ResourceState::CopyDst && access == ResourceAccess::TransferWrite));
    }

    static const char* gate(Backend backend, std::uint32_t version, bool optIn) noexcept
    {
        if (version != 1u)
            return "RHI-TRANSFER-DEPENDENCY-VERSION";
        if (!optIn)
            return "RHI-TRANSFER-DEPENDENCY-OPT-IN";
        switch (backend) {
        case Backend::Null:
            return "RHI-TRANSFER-DEPENDENCY-NULL-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::Metal:
            return "RHI-TRANSFER-DEPENDENCY-BACKEND-PAYLOAD-UNPROVEN";
        case Backend::WebGPU:
            return nullptr; // Explicit normative mapping in contract version1.
        case Backend::WebGL:
            break;
        }
        return "RHI-TRANSFER-DEPENDENCY-BACKEND-INVALID";
    }

    static Decision failure(const char* id) noexcept
    {
        return {CapabilityMode::DiagnosticFailure, Operation::DiagnosticFailure, id};
    }

    static Decision equivalent() noexcept
    {
        return {CapabilityMode::Equivalent, Operation::ImplicitOrderedUsage, nullptr};
    }
};
} // namespace arp::rhi::semantics
