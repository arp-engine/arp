#pragma once
#include "rhi/gpu_common.h"
#include "rhi/gpu_resource_types.h"

namespace arp::rhi::semantics {
// Creation/current metadata permits Undefined. Access legality is a separate
// contract in resource_access_scope.h; unknown enum values never form facts.
inline bool isKnownResourceState(ResourceState state) noexcept
{
    switch (state) {
    case ResourceState::Undefined:
    case ResourceState::CopySrc:
    case ResourceState::CopyDst:
    case ResourceState::ShaderRead:
    case ResourceState::ShaderWrite:
    case ResourceState::ColorAttachment:
    case ResourceState::DepthAttachment:
    case ResourceState::Present:
        return true;
    }
    return false;
}

inline bool isBufferResourceState(ResourceState state) noexcept
{
    switch (state) {
    case ResourceState::Undefined:
    case ResourceState::CopySrc:
    case ResourceState::CopyDst:
    case ResourceState::ShaderRead:
    case ResourceState::ShaderWrite:
        return true;
    default:
        return false;
    }
}

inline bool isTextureResourceState(TextureFormat format, ResourceState state) noexcept
{
    if (!(isBlockCompressed(format) ? bytesPerBlock(format) : bytesPerPixel(format)))
        return false;
    switch (state) {
    case ResourceState::Undefined:
    case ResourceState::CopySrc:
    case ResourceState::CopyDst:
    case ResourceState::ShaderRead:
    case ResourceState::ShaderWrite:
        return true;
    case ResourceState::ColorAttachment:
    case ResourceState::Present:
        return !isDepthTextureFormat(format);
    case ResourceState::DepthAttachment:
        return isDepthTextureFormat(format);
    }
    return false;
}
} // namespace arp::rhi::semantics
