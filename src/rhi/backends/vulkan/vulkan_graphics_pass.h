#pragma once

#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/frontend/bindless_snapshot.h"

namespace arp::rhi {
class RenderPassArguments;
}

namespace arp::rhi_vulkan {

// Shared terminal cold failure publication used by the ordinary pass wrappers.
struct VulkanRhiColdFailure {
    void* context = nullptr;
    void (*publish)(void*, std::string_view) noexcept = nullptr;
};

// Backend-private cold storage only. This does not activate a pass, look up
// attachment IDs, retain native payloads or authorize any recording operation.
core::UniquePtr<rhi::RenderPassEncoder> createVulkanRhiColdRenderPassEncoder(
    std::uint32_t& commandCount,
    bool& active,
    std::uint64_t recordingIdentity,
    rhi::BindlessIndirectDeclarations& indirectAccesses,
    std::shared_ptr<const rhi::RenderPassArguments> original);

// Borrowed inspection valid only while the owning encoder remains alive.
// It provides no mutable wrapper or recording/activation authority.
struct VulkanRhiColdRenderPassObservation {
    const rhi::RenderPassEncoder* wrapper = nullptr;
    const rhi::RenderPassArguments* original = nullptr;
    const rhi::RenderPassDesc* descriptor = nullptr;
};

VulkanRhiColdRenderPassObservation inspectVulkanRhiColdRenderPass(
    const rhi::RenderPassEncoder&) noexcept;
VulkanRhiColdRenderPassObservation inspectVulkanRhiColdRenderPass(
    const rhi::CommandEncoder&, std::size_t literalPassIndex) noexcept;
const char* activateVulkanRhiColdRenderPass(rhi::RenderPassEncoder&,
    std::shared_ptr<VulkanRhiNativeCommandPayload>,
    VulkanRhiColdFailure) noexcept;

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
