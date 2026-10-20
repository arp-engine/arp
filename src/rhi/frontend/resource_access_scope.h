#pragma once

#include "rhi/gpu_commands.h"

namespace arp::rhi::semantics {

struct ResourceAccessScope {
    ResourceState state = ResourceState::Undefined;
    PipelineStage stages = PipelineStage::None;
    ResourceAccess access = ResourceAccess::None;
};

inline bool resourceAccessWrites(ResourceAccess access) noexcept
{
    return (access
               & (ResourceAccess::TransferWrite | ResourceAccess::ShaderWrite
                   | ResourceAccess::ColorAttachmentWrite | ResourceAccess::DepthStencilWrite))
        != ResourceAccess::None;
}

inline bool resourceAccessReads(ResourceAccess access) noexcept
{
    return (access
               & (ResourceAccess::TransferRead | ResourceAccess::ShaderRead
                   | ResourceAccess::ColorAttachmentRead | ResourceAccess::DepthStencilRead
                   | ResourceAccess::PresentRead | ResourceAccess::IndirectCommandRead
                   | ResourceAccess::VertexAttributeRead | ResourceAccess::IndexRead))
        != ResourceAccess::None;
}

// Whole-resource state and exact access are independent: a read may use the
// general ShaderWrite layout, and an indirect buffer read uses ShaderRead state.
inline const char* validateResourceAccessScope(const ResourceAccessScope& scope) noexcept
{
    using S = PipelineStage;
    using A = ResourceAccess;
    const auto shaderStages = S::VertexShader | S::FragmentShader | S::ComputeShader;
    A permitted = A::None;
    switch (scope.state) {
    case ResourceState::CopySrc:
        permitted = A::TransferRead;
        break;
    case ResourceState::CopyDst:
        permitted = A::TransferWrite;
        break;
    case ResourceState::ShaderRead:
        permitted = A::ShaderRead | A::IndirectCommandRead | A::VertexAttributeRead | A::IndexRead;
        break;
    case ResourceState::ShaderWrite:
        permitted = A::ShaderRead | A::ShaderWrite;
        break;
    case ResourceState::ColorAttachment:
        permitted = A::ColorAttachmentRead | A::ColorAttachmentWrite;
        break;
    case ResourceState::DepthAttachment:
        permitted = A::DepthStencilRead | A::DepthStencilWrite;
        break;
    case ResourceState::Present:
        permitted = A::PresentRead;
        break;
    default:
        return "RHI-ACCESS-STATE-INVALID";
    }
    if (scope.access == A::None || (scope.access & permitted) != scope.access)
        return "RHI-ACCESS-STATE-MISMATCH";
    S allowed = S::None;
    const auto require = [&](A bits, S stages) {
        if ((scope.access & bits) == A::None)
            return true;
        allowed = allowed | stages;
        return (scope.stages & stages) != S::None;
    };
    if (!require(A::TransferRead | A::TransferWrite, S::Transfer)
        || !require(A::ShaderRead | A::ShaderWrite, shaderStages)
        || !require(A::IndirectCommandRead, S::DrawIndirect)
        || !require(A::VertexAttributeRead | A::IndexRead, S::VertexInput)
        || !require(A::ColorAttachmentRead | A::ColorAttachmentWrite, S::ColorAttachmentOutput)
        || !require(A::DepthStencilRead | A::DepthStencilWrite, S::DepthStencil)
        || !require(A::PresentRead, S::Present) || scope.stages == S::None
        || (scope.stages & allowed) != scope.stages)
        return "RHI-ACCESS-STAGE-MISMATCH";
    return nullptr;
}

} // namespace arp::rhi::semantics
