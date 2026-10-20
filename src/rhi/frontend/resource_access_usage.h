#pragma once
#include "rhi/frontend/resource_access_scope.h"

namespace arp::rhi::semantics {
// Bind an already validated L2 access scope to explicit resource usage flags.
// A state enum alone does not prove that the declared access is permitted.
class ResourceAccessUsageValidation final {
public:
    static ResourceAccessScope conservativeBufferReadScope(const BufferDesc& desc) noexcept
    {
        using A = ResourceAccess;
        using P = PipelineStage;
        using U = ResourceUsage;
        ResourceAccessScope result{ResourceState::ShaderRead, P::None, A::None};
        const auto include = [&](U usage, P stages, A access) {
            if ((desc.usage & usage) != U::None) {
                result.stages = result.stages | stages;
                result.access = result.access | access;
            }
        };
        include(U::VertexBuffer, P::VertexInput, A::VertexAttributeRead);
        include(U::IndexBuffer, P::VertexInput, A::IndexRead);
        include(U::IndirectBuffer, P::DrawIndirect, A::IndirectCommandRead);
        include(U::UniformBuffer | U::StorageBuffer,
            P::VertexShader | P::FragmentShader | P::ComputeShader,
            A::ShaderRead);
        return result;
    }

    // Only the new geometry scopes are admitted here. Ordinary barrier policy
    // remains with its existing validators; exact states are supplied by the
    // original transition or recorded memory-barrier observation.
    template <class Desc, class Barrier>
    static const char* validateGeometryBarrier(const Desc& desc,
        const Barrier& barrier,
        ResourceState sourceState,
        ResourceState destinationState) noexcept
    {
        const auto access = barrier.sourceAccess | barrier.destinationAccess;
        const auto stages = barrier.sourceStages | barrier.destinationStages;
        if ((access & (ResourceAccess::VertexAttributeRead | ResourceAccess::IndexRead))
                == ResourceAccess::None
            && (stages & PipelineStage::VertexInput) == PipelineStage::None)
            return nullptr;
        if (const auto* id = validate(desc, access))
            return id;
        if (const auto* id = validateResourceAccessScope(
                {sourceState, barrier.sourceStages, barrier.sourceAccess}))
            return id;
        return validateResourceAccessScope(
            {destinationState, barrier.destinationStages, barrier.destinationAccess});
    }

    static const char* validate(const BufferDesc& desc, ResourceAccess access) noexcept
    {
        using A = ResourceAccess;
        using U = ResourceUsage;
        if (((access & A::TransferRead) != A::None && (desc.usage & U::CopySrc) == U::None)
            || ((access & A::TransferWrite) != A::None && (desc.usage & U::CopyDst) == U::None)
            || ((access & A::ShaderRead) != A::None
                && (desc.usage & (U::StorageBuffer | U::UniformBuffer)) == U::None)
            || ((access & A::ShaderWrite) != A::None && (desc.usage & U::StorageBuffer) == U::None)
            || ((access & A::IndirectCommandRead) != A::None
                && (desc.usage & U::IndirectBuffer) == U::None)
            || ((access & A::VertexAttributeRead) != A::None
                && (desc.usage & U::VertexBuffer) == U::None)
            || ((access & A::IndexRead) != A::None && (desc.usage & U::IndexBuffer) == U::None)
            || (access
                   & (A::ColorAttachmentRead | A::ColorAttachmentWrite | A::DepthStencilRead
                       | A::DepthStencilWrite | A::PresentRead))
                != A::None)
            return "RHI-ORIGINAL-ACCESS-BUFFER-USAGE";
        return nullptr;
    }

    static const char* validate(const TextureDesc& desc, ResourceAccess access) noexcept
    {
        using A = ResourceAccess;
        using U = TextureUsage;
        if (((access & A::TransferRead) != A::None && (desc.usage & U::CopySrc) == U::None)
            || ((access & A::TransferWrite) != A::None && (desc.usage & U::CopyDst) == U::None)
            || ((access & A::ShaderRead) != A::None
                && (desc.usage & (U::Sampled | U::Storage)) == U::None)
            || ((access & A::ShaderWrite) != A::None && (desc.usage & U::Storage) == U::None)
            || ((access
                    & (A::ColorAttachmentRead | A::ColorAttachmentWrite | A::DepthStencilRead
                        | A::DepthStencilWrite))
                    != A::None
                && (desc.usage & U::RenderAttachment) == U::None)
            || (access & (A::IndirectCommandRead | A::VertexAttributeRead | A::IndexRead))
                != A::None
            || (access & A::PresentRead) != A::None)
            return "RHI-ORIGINAL-ACCESS-TEXTURE-USAGE";
        // Present needs an original swapchain-image closure, absent from this
        // Source profile. RenderAttachment is not proof of presentation ownership.
        return nullptr;
    }
};
} // namespace arp::rhi::semantics
