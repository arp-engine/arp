#pragma once

#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/frontend/bindless_snapshot.h"

namespace arp::rhi_vulkan {

arp::core::UniquePtr<rhi::RenderPassEncoder> createVulkanRhiRenderPassEncoder(
    std::uint32_t& commandCount,
    bool& active,
    std::uint64_t recordingIdentity,
    bool bindlessExecutable,
    bool indirectFirstInstanceEnabled,
    rhi::BindlessIndirectDeclarations& indirectAccesses,
    const rhi::RenderPassDesc& desc,
    std::shared_ptr<VulkanRhiNativeCommandPayload> nativePayload,
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry,
    const rhi::semantics::NativeBackendCapabilityContract* capabilityContract,
    rhi::CommandValidationResult& validation);

} // namespace arp::rhi_vulkan
