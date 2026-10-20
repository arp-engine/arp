#include "core/file_io.h"
#include "rhi/backends/vulkan/vulkan_allocator.h"
#include "rhi/backends/vulkan/vulkan_timeline_semaphore.h"
#include "tests/fixtures/vulkan_query_only_runtime_owner.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp::rhi_vulkan;

unsigned instanceResolutions = 0u;
unsigned deviceResolutions = 0u;

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL instanceProcedure(VkInstance, const char*)
{
    ++instanceResolutions;
    return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL deviceProcedure(VkDevice, const char*)
{
    ++deviceResolutions;
    return nullptr;
}

VulkanRhiRuntimeDesc queryOnlyDescriptor()
{
    VulkanRhiRuntimeDesc desc;
    desc.enableSyntheticRuntime = false;
    desc.instance = syntheticHandle<VkInstance>(1u);
    desc.physicalDevice = syntheticHandle<VkPhysicalDevice>(2u);
    desc.logicalDevice = syntheticHandle<VkDevice>(3u);
    desc.graphicsQueue = syntheticHandle<VkQueue>(4u);
    desc.surface = syntheticHandle<VkSurfaceKHR>(5u);
    desc.swapchainMaintenance1Enabled = true;
    desc.getInstanceProcAddr = &instanceProcedure;
    desc.getDeviceProcAddr = &deviceProcedure;
    return desc;
}

void assertRejected(VulkanRhiRuntimeDesc desc, const char* diagnostic)
{
    instanceResolutions = deviceResolutions = 0u;
    {
        VulkanRhiRuntime runtime(desc);
        assert(!runtime.valid());
        assert(std::strcmp(runtime.admissionDiagnostic(), diagnostic) == 0);
        const auto report = inspectVulkanRhiRuntime(runtime);
        assert(!report.success && !report.runtimeOwned && !report.nativeDeviceAvailable
            && !report.nativeSurfaceAvailable);
        assert(report.diagnostics == diagnostic);
        assert(!runtime.instance() && !runtime.physicalDevice() && !runtime.logicalDevice());
        assert(!runtime.graphicsQueue() && !runtime.presentQueue() && !runtime.surface());
        assert(!runtime.getInstanceProcAddr() && !runtime.getDeviceProcAddr());
        assert(!runtime.getDeviceProcAddr("vkDestroyDevice"));
        assert(!runtime.getDeviceProcAddr(nullptr));
        assert(!runtime.refreshSurfaceDetails());
        assert(!runtime.presentFenceEnabled());
        assert(
            !runtime.coreQueueCapabilities().graphics && !runtime.coreQueueCapabilities().compute);
        assert(!runtime.descriptorIndexing().queried);
        assert(!runtime.timelineSemaphoreEnabled() && !runtime.indirectFirstInstanceEnabled());
        assert(runtime.memoryProperties().memoryTypeCount == 0u);
        assert(runtime.surfaceFormats().empty() && runtime.presentModes().empty());
        assert(runtime.surfaceCapabilities().minImageCount == 0u);
        assert(!createVulkanRhiRuntime(desc));
    }
    // Includes rejected runtime/factory destructors, not just constructor gates.
    assert(instanceResolutions == 0u && deviceResolutions == 0u);
}

void missingAndMismatchedOwners()
{
    assertRejected(queryOnlyDescriptor(), "RHI-VULKAN-RUNTIME-OWNER-MISSING");
    for (unsigned field = 0u; field < 3u; ++field) {
        for (const bool nullHandle : {false, true}) {
            auto desc = queryOnlyDescriptor();
            desc.nativeOwner = arp::core::makeShared<arp::tests::VulkanQueryOnlyRuntimeOwner>(desc);
            if (field == 0u)
                desc.instance = nullHandle ? VK_NULL_HANDLE : syntheticHandle<VkInstance>(6u);
            if (field == 1u)
                desc.physicalDevice =
                    nullHandle ? VK_NULL_HANDLE : syntheticHandle<VkPhysicalDevice>(6u);
            if (field == 2u)
                desc.logicalDevice = nullHandle ? VK_NULL_HANDLE : syntheticHandle<VkDevice>(6u);
            assertRejected(desc, "RHI-VULKAN-RUNTIME-OWNER-MISMATCH");
        }
    }
    for (const bool missingInstanceResolver : {false, true}) {
        auto desc = queryOnlyDescriptor();
        desc.nativeOwner = arp::core::makeShared<arp::tests::VulkanQueryOnlyRuntimeOwner>(desc);
        if (missingInstanceResolver)
            desc.getInstanceProcAddr = nullptr;
        else
            desc.getDeviceProcAddr = nullptr;
        assertRejected(desc, "RHI-DEVICE-NATIVE-RUNTIME-UNAVAILABLE");
    }
}

void queryFixtureRetention()
{
    std::weak_ptr<VulkanRhiNativeRuntimeOwner> owner;
    std::shared_ptr<VulkanRhiRuntime> retained;
    {
        auto desc = queryOnlyDescriptor();
        desc.surface = VK_NULL_HANDLE;
        desc.nativeOwner = arp::core::makeShared<arp::tests::VulkanQueryOnlyRuntimeOwner>(desc);
        owner = desc.nativeOwner;
        VulkanRhiRuntime original(desc);
        assert(original.valid() && !original.syntheticRuntime());
        assert(original.admissionDiagnostic() == nullptr);
        retained = arp::core::makeShared<VulkanRhiRuntime>(original);
        desc.nativeOwner.reset();
        assert(!owner.expired());
    }
    assert(!owner.expired());
    assert(retained->getInstanceProcAddr() == &instanceProcedure);
    assert(retained->getDeviceProcAddr() == &deviceProcedure);
    const auto calls = deviceResolutions;
    assert(!retained->getDeviceProcAddr(nullptr) && deviceResolutions == calls);
    assert(!retained->getDeviceProcAddr("query-only-unavailable"));
    assert(deviceResolutions == calls + 1u);
    retained.reset();
    assert(owner.expired());
}

void syntheticDoesNotDispatch()
{
    auto desc = queryOnlyDescriptor();
    desc.enableSyntheticRuntime = true;
    instanceResolutions = deviceResolutions = 0u;
    {
        VulkanRhiRuntime runtime(desc);
        assert(runtime.valid() && runtime.syntheticRuntime());
        assert(!runtime.getInstanceProcAddr() && !runtime.getDeviceProcAddr());
        assert(!runtime.getDeviceProcAddr("vkDestroyFence"));
        assert(!runtime.presentFenceEnabled());
        assert(runtime.refreshSurfaceDetails());
        const auto report = inspectVulkanRhiRuntime(runtime);
        assert(!report.nativeDeviceAvailable && !report.nativeSurfaceAvailable);
    }
    assert(instanceResolutions == 0u && deviceResolutions == 0u);
}

void allocatorFacadeSharesOriginalLedger()
{
    VulkanRhiAllocatorDesc desc;
    desc.label = "shared allocator facade";
    desc.memoryBudgetBytes = 1024u;
    auto original = arp::core::makeUnique<VulkanRhiAllocator>(desc);
    auto first = original->allocate({"before copy", 256u, arp::rhi::MemoryUsage::Transient});
    assert(first.success);
    auto survivor = arp::core::makeUnique<VulkanRhiAllocator>(*original);
    assert(survivor->memoryStats().allocatedBytes == 256u);
    auto second = survivor->allocate({"shared budget", 512u, arp::rhi::MemoryUsage::Transient});
    assert(second.success && original->memoryStats().allocatedBytes == 768u);
    original.reset();
    assert(!survivor->allocate({"must reject", 257u, arp::rhi::MemoryUsage::Transient}).success);
    first.releaseToken.reset();
    assert(survivor->memoryStats().allocatedBytes == 512u);
    auto third = survivor->allocate({"after creator", 512u, arp::rhi::MemoryUsage::Transient});
    assert(third.success && survivor->memoryStats().allocatedBytes == 1024u);
    second.releaseToken.reset();
    third.releaseToken.reset();
    assert(survivor->memoryStats().allocatedBytes == 0u
        && survivor->memoryStats().liveResourceCount == 0u
        && survivor->memoryStats().budgetRejectionCount == 1u);
}

void timelineAdmissionRefusesUnsupportedRuntime()
{
    arp::core::pmr::String diagnostic;
    assert(!VulkanRhiTimelineSemaphorePayload::create({}, 0u, diagnostic));
    assert(diagnostic == "RHI-VULKAN-TIMELINE-NATIVE-RUNTIME-UNAVAILABLE");
    auto desc = queryOnlyDescriptor();
    desc.enableSyntheticRuntime = true;
    auto runtime = createVulkanRhiRuntime(desc);
    instanceResolutions = deviceResolutions = 0u;
    assert(!VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, diagnostic));
    assert(diagnostic == "RHI-VULKAN-TIMELINE-NATIVE-RUNTIME-UNAVAILABLE");
    assert(instanceResolutions == 0u && deviceResolutions == 0u);
    desc.enableSyntheticRuntime = false;
    desc.surface = VK_NULL_HANDLE;
    desc.nativeOwner = arp::core::makeShared<arp::tests::VulkanQueryOnlyRuntimeOwner>(desc);
    runtime = createVulkanRhiRuntime(desc);
    assert(runtime && !runtime->timelineSemaphoreEnabled());
    instanceResolutions = deviceResolutions = 0u;
    assert(!VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, diagnostic));
    assert(diagnostic == "RHI-VULKAN-TIMELINE-NATIVE-RUNTIME-UNAVAILABLE");
    VulkanRhiTimelineSemaphorePayload empty(runtime);
    std::uint64_t unchanged = 17u;
    assert(!empty.nativeHandle() && !empty.belongsTo(*runtime));
    assert(empty.hostSignal(1u) == VK_ERROR_INITIALIZATION_FAILED);
    assert(empty.hostWait(1u, 0u) == VK_ERROR_INITIALIZATION_FAILED);
    assert(empty.queryCounter(unchanged) == VK_ERROR_INITIALIZATION_FAILED && unchanged == 17u);
    assert(instanceResolutions == 0u && deviceResolutions == 0u);
}
} // namespace

int main()
{
    // Default production construction must not silently select the synthetic
    // validator runtime. This rejects before resolving any native entry point.
    assertRejected(VulkanRhiRuntimeDesc{}, "RHI-VULKAN-RUNTIME-OWNER-MISSING");
    VulkanRhiRuntimeDesc explicitValidator;
    explicitValidator.enableSyntheticRuntime = true;
    const auto validator = createVulkanRhiRuntime(explicitValidator);
    assert(validator && validator->syntheticRuntime());
    const auto header =
        arp::core::readTextFile(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_runtime.h");
    const auto platform =
        arp::core::readTextFile(ARP_SOURCE_DIR "/src/platform/render_system_vulkan.cpp");
    assert(header.success && platform.success);
    const bool ownerContract =
        header.text.find("VulkanRhiNativeRuntimeOwner") != arp::core::pmr::String::npos
        && header.text.find("nativeOwner") != arp::core::pmr::String::npos;
    const bool bootstrapRetains = platform.text.find("runtimeDesc.nativeOwner = nativeRuntime")
        != arp::core::pmr::String::npos;
    std::printf(
        "native-runtime-owner: contract=%d bootstrap=%d\n", ownerContract, bootstrapRetains);
    std::fflush(stdout);
    assert(ownerContract && bootstrapRetains);
    missingAndMismatchedOwners();
    queryFixtureRetention();
    syntheticDoesNotDispatch();
    timelineAdmissionRefusesUnsupportedRuntime();
    allocatorFacadeSharesOriginalLedger();
    std::puts("NativeRuntimeOwner/1 CPU admission/retention: PASS (query fixture only)");
}
