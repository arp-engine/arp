#include "rhi/backends/metal/metal_native_secondary.h"

namespace arp::rhi_metal {

std::shared_ptr<MetalRhiNativeSecondaryCommandPayload> createMetalRhiNativeSecondaryCommandPayload(
    const std::pmr::string&, rhi::PrimitiveTopology, std::uint32_t, bool, std::uint32_t)
{
    return {};
}

bool recordMetalRhiNativeSecondaryDraw(
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>&,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-SECONDARY-ICB-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool recordMetalRhiNativeSecondaryDrawIndexed(
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>&,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::int32_t,
    std::uint32_t,
    const std::shared_ptr<MetalRhiNativeBufferPayload>&,
    rhi::IndexFormat,
    std::uint64_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-SECONDARY-ICB-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool recordMetalRhiNativeSecondaryTessellationFactorBufferBind(
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>&,
    const std::shared_ptr<MetalRhiNativeBufferPayload>&,
    std::uint64_t,
    std::uint32_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-SECONDARY-TESSELLATION-FACTOR-BUFFER-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool executeMetalRhiNativeSecondary(const std::shared_ptr<void>&,
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>&,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-SECONDARY-ICB-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool executeMetalRhiNativeSecondaryCommand(const std::shared_ptr<void>&,
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>&,
    std::uint32_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-SECONDARY-ICB-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool metalRhiNativeSecondaryCommandBuffersAvailable()
{
    return false;
}

} // namespace arp::rhi_metal
