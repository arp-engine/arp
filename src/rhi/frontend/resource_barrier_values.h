#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/gpu_resource_types.h"

#include <cstdint>
#include <string>

namespace arp::rhi {

enum class PipelineStage : std::uint32_t {
    None = 0,
    TopOfPipe = 1u << 0,
    Transfer = 1u << 1,
    VertexShader = 1u << 2,
    FragmentShader = 1u << 3,
    ComputeShader = 1u << 4,
    ColorAttachmentOutput = 1u << 5,
    DepthStencil = 1u << 6,
    Present = 1u << 7,
    DrawIndirect = 1u << 8,
    // Fixed-function vertex/instance fetch and index fetch, not shader execution.
    VertexInput = 1u << 9
};

inline PipelineStage operator|(PipelineStage lhs, PipelineStage rhs)
{
    return static_cast<PipelineStage>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

inline PipelineStage operator&(PipelineStage lhs, PipelineStage rhs)
{
    return static_cast<PipelineStage>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

enum class ResourceAccess : std::uint32_t {
    None = 0,
    TransferRead = 1u << 0,
    TransferWrite = 1u << 1,
    ShaderRead = 1u << 2,
    ShaderWrite = 1u << 3,
    ColorAttachmentRead = 1u << 4,
    ColorAttachmentWrite = 1u << 5,
    DepthStencilRead = 1u << 6,
    DepthStencilWrite = 1u << 7,
    PresentRead = 1u << 8,
    IndirectCommandRead = 1u << 9,
    VertexAttributeRead = 1u << 10,
    IndexRead = 1u << 11
};

inline ResourceAccess operator|(ResourceAccess lhs, ResourceAccess rhs)
{
    return static_cast<ResourceAccess>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

inline ResourceAccess operator&(ResourceAccess lhs, ResourceAccess rhs)
{
    return static_cast<ResourceAccess>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

struct ResourceBarrierResult {
    bool success = false;
    std::pmr::string diagnostics;
    GpuResourceId resource;
    ResourceState before = ResourceState::Undefined;
    ResourceState after = ResourceState::Undefined;
    bool subresourceAware = false;
    bool stageAccessAware = false;
    PipelineStage sourceStages = PipelineStage::None;
    PipelineStage destinationStages = PipelineStage::None;
    ResourceAccess sourceAccess = ResourceAccess::None;
    ResourceAccess destinationAccess = ResourceAccess::None;
};

struct ResourceBarrierDesc {
    GpuResourceId resource;
    TextureSubresourceRange textureSubresources;
    ResourceState before = ResourceState::Undefined;
    ResourceState after = ResourceState::Undefined;
    PipelineStage sourceStages = PipelineStage::None;
    PipelineStage destinationStages = PipelineStage::None;
    ResourceAccess sourceAccess = ResourceAccess::None;
    ResourceAccess destinationAccess = ResourceAccess::None;
};

/// An access-only dependency for one resource. Unlike ResourceBarrierDesc it
/// deliberately does not carry before/after states and never changes logical
/// resource-state metadata.
struct ResourceMemoryBarrierDesc {
    GpuResourceId resource;
    PipelineStage sourceStages = PipelineStage::None;
    PipelineStage destinationStages = PipelineStage::None;
    ResourceAccess sourceAccess = ResourceAccess::None;
    ResourceAccess destinationAccess = ResourceAccess::None;
};

struct AliasingBarrierDesc {
    GpuResourceId beforeResource;
    GpuResourceId afterResource;
    std::uint32_t aliasGroup = 0;
    PipelineStage sourceStages = PipelineStage::None;
    PipelineStage destinationStages = PipelineStage::None;
    ResourceAccess sourceAccess = ResourceAccess::None;
    ResourceAccess destinationAccess = ResourceAccess::None;
    std::uint32_t sourceQueueRole = 0;
    std::uint32_t destinationQueueRole = 0;
    bool ownershipHandoff = false;
};

} // namespace arp::rhi
