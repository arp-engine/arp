#pragma once
#include "rhi/frontend/bindless_compute_binding.h"
#include "rhi/frontend/bindless_graphics_binding.h"

namespace arp::rhi {
class OriginalCommandBindlessRootSource;
}

namespace arp::rhi_webgpu {
struct WebGpuNativeDescriptorGroupPayload;
class WebGpuNativeIssuedPipelineContract;
// Selected target bindings are independent of the complete retained Source.
// Produced16 uses EntryAccess2/3; scene pairs use EntryAccess4. Every selected
// stage must match immutable package bytes independently of the native facts.
const char* inspectWebGpuOriginalBindlessBindingActivity(const WebGpuNativeIssuedPipelineContract&,
    const rhi::BindlessAuthorization&,
    std::uint32_t set,
    std::uint32_t binding,
    bool& active) noexcept;
// Exact original Root2/Publication6 custody. Raw descriptor export stays closed.
std::shared_ptr<WebGpuNativeDescriptorGroupPayload> prepareWebGpuOriginalOrdinaryColorDescriptors(
    const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>&,
    std::uint32_t logicalSet,
    std::uint32_t closureVersion);
// Explicit ordinary Graphics correspondence; Closure19 additionally retains
// canonical completed depth Source1/Input1 beside whole-upload color.
std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
prepareWebGpuOriginalOrdinaryGraphicsDescriptors(
    const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>&,
    std::uint32_t logicalSet,
    std::uint32_t closureVersion);
// Validate actual selected physical bindings, independently of shader access
// projection. Reused before native group creation and at Root revalidation.
const char* validateWebGpuOriginalBindlessPhysicalAliases(
    const WebGpuNativeDescriptorGroupPayload&, const WebGpuNativeDescriptorGroupPayload&) noexcept;
// Shared cold emission from exact issued mappings and immutable L2 writes.
// Callers retain the original descriptor Source and validate its live closure.
rhi::ValidationResult prepareWebGpuOriginalBindlessPhysicalGroup(
    const WebGpuNativeIssuedPipelineContract&,
    const rhi::DescriptorSet&,
    const std::shared_ptr<WebGpuNativeDescriptorGroupPayload>&,
    const rhi::BindlessAuthorization&,
    std::uint32_t,
    std::shared_ptr<WebGpuNativeDescriptorGroupPayload>&,
    std::uint32_t&,
    std::uint32_t ordinaryColorClosureVersion = 0u);
// Same PreparedComputeBinding/1 factory identity; native execution remains a
// separate original stream/journal/completion issuer.
std::shared_ptr<const rhi::BindlessPreparedComputeBinding> prepareWebGpuOriginalBindlessCompute(
    const rhi::BindlessCrossExtension&,
    std::shared_ptr<const rhi::BindlessComputeBindingOwners>,
    core::pmr::String&);
std::shared_ptr<WebGpuNativeDescriptorGroupPayload> webGpuOriginalBindlessPhysicalGroup(
    const std::shared_ptr<const rhi::BindlessPreparedComputeBinding>&) noexcept;
std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding> prepareWebGpuOriginalBindlessGraphics(
    const rhi::BindlessCrossExtension&,
    std::shared_ptr<const rhi::BindlessGraphicsBindingOwners>,
    core::pmr::String&);
std::shared_ptr<WebGpuNativeDescriptorGroupPayload> webGpuOriginalBindlessPhysicalGroup(
    const std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding>&) noexcept;
} // namespace arp::rhi_webgpu
