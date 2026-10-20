#include "rhi/backends/vulkan/vulkan_device.h"
#include "tests/fixtures/vulkan_core_capability_query_fixture.h"

#include <cassert>
#include <cstring>
#include <limits>

namespace {

using arp::rhi_vulkan::VulkanRhiRuntime;
using arp::tests::VulkanCoreCapabilityQueryFixture;

void assertClosed(const VulkanRhiRuntime& runtime, const char* diagnostic)
{
    assert(!runtime.coreQueueCapabilities().graphics);
    assert(!runtime.coreQueueCapabilities().compute);
    assert(std::strcmp(runtime.coreQueueCapabilityDiagnostic(), diagnostic) == 0);
}

void queueFlagsAndCachedReads()
{
    for (const VkQueueFlags flags : {0u,
             VkQueueFlags(VK_QUEUE_TRANSFER_BIT),
             VkQueueFlags(VK_QUEUE_GRAPHICS_BIT),
             VkQueueFlags(VK_QUEUE_COMPUTE_BIT),
             VkQueueFlags(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)}) {
        for (std::uint32_t index = 0u; index < 2u; ++index) {
            VulkanCoreCapabilityQueryFixture query;
            query.families[index].queueFlags = flags;
            auto desc = query.descriptor();
            desc.graphicsQueueFamilyIndex = index;
            VulkanRhiRuntime runtime(desc);
            assert(runtime.valid()); // Fixture validity is not native execution.
            const auto reads = query.familyQueries;
            const auto queueReads = query.queueQueries;
            for (unsigned repeat = 0u; repeat < 100u; ++repeat) {
                const auto caps = runtime.coreQueueCapabilities();
                assert(caps.graphics == ((flags & VK_QUEUE_GRAPHICS_BIT) != 0u));
                assert(caps.compute == ((flags & VK_QUEUE_COMPUTE_BIT) != 0u));
            }
            assert(query.familyQueries == reads && reads == 2u);
            assert(query.queueQueries == queueReads && queueReads == 1u);
            if ((flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == 0u)
                assertClosed(runtime, "RHI-VULKAN-CORE-QUEUE-UNSUPPORTED");
            else
                assert(runtime.coreQueueCapabilityDiagnostic() == nullptr);
        }
    }
}

void missingAndMismatchedSources()
{
    for (unsigned failure = 0u; failure < 10u; ++failure) {
        VulkanCoreCapabilityQueryFixture query;
        auto desc = query.descriptor();
        const char* diagnostic = "RHI-VULKAN-CORE-QUEUE-IDENTITY-MISMATCH";
        switch (failure) {
        case 0u:
            query.familyQueryAvailable = false;
            diagnostic = "RHI-VULKAN-CORE-QUEUE-QUERY-UNAVAILABLE";
            break;
        case 1u:
            query.queueQueryAvailable = false;
            diagnostic = "RHI-VULKAN-CORE-QUEUE-QUERY-UNAVAILABLE";
            break;
        case 2u:
            desc.graphicsQueue = VK_NULL_HANDLE;
            diagnostic = "RHI-VULKAN-CORE-QUEUE-QUERY-UNAVAILABLE";
            break;
        case 3u:
            query.reportedCount = 3u;
            break;
        case 4u:
            desc.graphicsQueueFamilyIndex = 2u;
            break;
        case 5u:
            desc.graphicsQueueFamilyIndex = std::numeric_limits<std::uint32_t>::max();
            break;
        case 6u:
            query.reportedCount = 0u;
            break;
        case 7u:
            query.secondCount = 1u;
            break;
        case 8u:
            query.families[0].queueCount = 0u;
            break;
        case 9u:
            query.queriedQueue = arp::rhi_vulkan::syntheticHandle<VkQueue>(5u);
            break;
        }
        VulkanRhiRuntime runtime(desc);
        assertClosed(runtime, diagnostic);
    }
    for (std::uint32_t declared = 0u; declared < 4u; ++declared) {
        VulkanCoreCapabilityQueryFixture query;
        auto desc = query.descriptor();
        desc.queueFamilyCount = declared;
        VulkanRhiRuntime runtime(desc);
        assert(runtime.coreQueueCapabilityDiagnostic() == nullptr);
        assert(runtime.coreQueueCapabilities().graphics && runtime.coreQueueCapabilities().compute);
    }
    VulkanCoreCapabilityQueryFixture query;
    auto desc = query.descriptor();
    desc.enableSyntheticRuntime = true;
    VulkanRhiRuntime synthetic(desc);
    assertClosed(synthetic, "RHI-DEVICE-NATIVE-RUNTIME-UNAVAILABLE");
    assert(query.familyQueries == 0u && query.queueQueries == 0u);
}

void memoryFactsDoNotInheritDescriptorDefaults()
{
    for (unsigned failure = 0u; failure < 6u; ++failure) {
        VulkanCoreCapabilityQueryFixture query;
        if (failure == 0u)
            query.memoryQueryAvailable = false;
        if (failure == 1u)
            query.memoryTypeCount = 0u;
        if (failure == 2u)
            query.memoryTypeCount = VK_MAX_MEMORY_TYPES + 1u;
        auto desc = query.descriptor();
        if (failure == 3u)
            desc.getInstanceProcAddr = nullptr;
        if (failure == 4u)
            desc.instance = VK_NULL_HANDLE;
        if (failure == 5u)
            desc.physicalDevice = VK_NULL_HANDLE;
        desc.hasDeviceLocalMemory = true;
        desc.hasHostVisibleMemory = true;
        VulkanRhiRuntime runtime(desc);
        assert(runtime.memoryProperties().memoryTypeCount == 0u);
    }
    VulkanCoreCapabilityQueryFixture query;
    VulkanRhiRuntime runtime(query.descriptor());
    assert(runtime.memoryProperties().memoryTypeCount == 2u);
}

void unavailableDeviceCannotPublishCallerClaims()
{
    arp::rhi_vulkan::VulkanRhiDeviceDesc desc;
    desc.memoryBudgetBytes = 64u * 1024u;
    desc.uploadStagingPageSizeBytes = 16u * 1024u;
    desc.uploadStagingMaxPageCount = 1u;
    desc.capabilityContract =
        arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Vulkan);
    desc.capabilityContract.queriedFromNativeDevice = true;
    desc.capabilityContract.queriedExtendedFeatures = true;
    auto device = arp::rhi_vulkan::createVulkanRhiDevice(desc);
    // The legacy diagnostic-only object is not proof of a connected device.
    assert(device);
    const auto caps = device->capabilities();
    assert(!caps.core.graphics && !caps.core.compute);
    assert(!caps.explicitFeatures.explicitBarriers);
    assert(!caps.memory.deviceLocal && !caps.memory.hostVisible);
    assert(caps.swapchain.requestedImageCount == 0u && caps.swapchain.maxFramesInFlight == 0u);
    assert(!caps.features.nativeSync && !caps.features.computeDispatch);
    assert(!caps.pushConstantLoweringHasExecutablePayload);
    for (const auto& status : caps.executionStatuses)
        assert(!status.executable);
    const auto stats = arp::rhi_vulkan::inspectVulkanRhiNativeResourceStats(*device);
    assert(!stats.runtimeConnected);
    assert(stats.nativeBufferCreateCount == 0u && stats.nativeQueueSubmitCount == 0u);
}

} // namespace

int main()
{
    queueFlagsAndCachedReads();
    missingAndMismatchedSources();
    memoryFactsDoNotInheritDescriptorDefaults();
    unavailableDeviceCannotPublishCallerClaims();
}
