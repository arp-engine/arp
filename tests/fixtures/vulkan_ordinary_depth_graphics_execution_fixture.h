#pragma once
#include "original_ordinary_depth_graphics_execution_fixture.h"
#include "rhi/backends/vulkan/vulkan_original_bindless_graphics_pass.h"

namespace arp::test {
struct VulkanOrdinaryDepthGraphicsBackendFixture {
    static constexpr bool captureSubmittedOnly = false;
    using Closure = rhi_vulkan::VulkanBindlessRootNativeClosure;
    using Pass = rhi_vulkan::VulkanOriginalBindlessGraphicsPass;
    static constexpr std::uint32_t passNativeObjects = 3u;
    static constexpr std::uint32_t submissionOwnerOverhead = 2u;
    static constexpr const char* ownerCapacityDiagnostic = "RHI-VULKAN-ROOT-FENCE-OWNER-CAPACITY";

    static void readValues(
        const rhi::GpuBuffer& buffer, std::array<float, 40u>& values, core::pmr::String& diagnostic)
    {
        const auto native = rhi_vulkan::vulkanRhiNativePayload(buffer);
        assert(native && native->backendInvalidateMappedRead(0u, sizeof(values), diagnostic));
        std::memcpy(values.data(), native->backendMappedData(), sizeof(values));
    }

    static void verifyOutput(const rhi::OriginalCommandResourceSources&, bool)
    {
        std::puts(
            "BINDLESS_VULKAN_PUBLIC_GRAPHICS_DEPTH2_OK numeric40 less-equal-greater samplers2 color fault real-fence same-journal hot0");
    }
};

using VulkanOrdinaryDepthGraphicsExecutionFixture =
    OriginalOrdinaryDepthGraphicsExecutionFixture<VulkanOrdinaryDepthGraphicsBackendFixture>;
} // namespace arp::test
