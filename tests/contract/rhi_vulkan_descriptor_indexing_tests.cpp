#include "tests/fixtures/vulkan_headless_runtime.h"
#include "tests/fixtures/vulkan_query_only_runtime_owner.h"

#include <cassert>
#include <cstring>
#include <string_view>

using namespace arp::rhi_vulkan;

namespace {
bool featureQueryAvailable = true;
bool propertiesQueryAvailable = true;
std::uint32_t apiVersion = VK_API_VERSION_1_2;
VulkanDescriptorIndexingFeatures physicalFeatures;
unsigned featureQueries = 0;

VKAPI_ATTR void VKAPI_CALL properties(VkPhysicalDevice, VkPhysicalDeviceProperties* output)
{
    *output = {};
    output->apiVersion = apiVersion;
    output->limits.maxPerStageDescriptorSamplers = 53;
    output->limits.maxDescriptorSetStorageBuffers = 127;
    output->limits.minStorageBufferOffsetAlignment = 256;
}

VKAPI_ATTR void VKAPI_CALL features(VkPhysicalDevice, VkPhysicalDeviceFeatures2* output)
{
    ++featureQueries;
    auto* indexed = static_cast<VkPhysicalDeviceVulkan12Features*>(output->pNext);
    assert(indexed && indexed->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    physicalFeatures.applyTo(output->features, *indexed);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL instanceProc(VkInstance, const char* name)
{
    if (propertiesQueryAvailable && std::strcmp(name, "vkGetPhysicalDeviceProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(properties);
    if (featureQueryAvailable && std::strcmp(name, "vkGetPhysicalDeviceFeatures2") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(features);
    return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL deviceProc(VkDevice, const char*)
{
    return nullptr;
}

void contract()
{
    // Basis vectors catch swapped class mappings, not merely capture/apply symmetry.
    VkPhysicalDeviceFeatures core{};
    VkPhysicalDeviceVulkan12Features native12{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    core.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    core.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
    native12.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
    auto selected = VulkanDescriptorIndexingFeatures::capture(core, native12);
    assert(selected.dynamicallyUniform[0] && !selected.dynamicallyUniform[1]
        && !selected.dynamicallyUniform[2] && selected.dynamicallyUniform[3]);
    assert(!selected.nonUniform[0] && !selected.nonUniform[1] && selected.nonUniform[2]
        && !selected.nonUniform[3]);
    for (std::size_t i = 0; i < 4; ++i) {
        VulkanDescriptorIndexingFeatures bit;
        bit.nonUniform[i] = true;
        bit.dynamicallyUniform[i] = true;
        core = {};
        native12 = {};
        native12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        core.samplerAnisotropy = VK_TRUE;
        native12.timelineSemaphore = VK_TRUE;
        bit.applyTo(core, native12);
        assert(core.samplerAnisotropy && native12.timelineSemaphore);
        assert(!native12.runtimeDescriptorArray && !native12.descriptorBindingPartiallyBound
            && !native12.descriptorBindingVariableDescriptorCount
            && !native12.descriptorBindingSampledImageUpdateAfterBind
            && !native12.descriptorBindingStorageImageUpdateAfterBind
            && !native12.descriptorBindingUniformBufferUpdateAfterBind
            && !native12.descriptorBindingStorageBufferUpdateAfterBind);
        auto copied = VulkanDescriptorIndexingFeatures::capture(core, native12);
        assert(copied.subsetOf(bit) && bit.subsetOf(copied));
        assert(!bit.subsetOf({}));
    }
    physicalFeatures.dynamicallyUniform.fill(true);
    physicalFeatures.nonUniform.fill(true);
    physicalFeatures.descriptorIndexing = true;
    physicalFeatures.vertexStoresAndAtomics = true;
    physicalFeatures.fragmentStoresAndAtomics = true;
    VulkanRhiRuntimeDesc desc;
    desc.enableSyntheticRuntime = false;
    desc.instance = reinterpret_cast<VkInstance>(1);
    desc.physicalDevice = reinterpret_cast<VkPhysicalDevice>(2);
    desc.logicalDevice = reinterpret_cast<VkDevice>(3);
    desc.getInstanceProcAddr = instanceProc;
    desc.getDeviceProcAddr = deviceProc;
    desc.nativeOwner = arp::core::makeShared<arp::tests::VulkanQueryOnlyRuntimeOwner>(desc);
    auto runtime = createVulkanRhiRuntime(desc);
    assert(runtime && runtime->descriptorIndexing().queried
        && !runtime->descriptorIndexing().enablementVerified());
    assert(std::string_view(runtime->descriptorIndexing().diagnosticId)
        == "RHI-VULKAN-INDEXING-ENABLEMENT-MISSING");
    desc.descriptorIndexingEnablementReported = true;
    runtime = createVulkanRhiRuntime(desc);
    assert(runtime->descriptorIndexing().enablementVerified());
    assert(!runtime->descriptorIndexing().enabled.descriptorIndexing);
    desc.descriptorIndexingEnabled = physicalFeatures;
    runtime = createVulkanRhiRuntime(desc);
    const auto& queried = runtime->descriptorIndexing();
    assert(queried.enablementVerified() && queried.enabled.nonUniform[3]);
    assert(queried.limits.maxPerStageDescriptorSamplers == 53
        && queried.limits.maxDescriptorSetStorageBuffers == 127
        && queried.limits.minStorageBufferOffsetAlignment == 256);
    physicalFeatures.nonUniform[2] = false;
    runtime = createVulkanRhiRuntime(desc);
    assert(!runtime->descriptorIndexing().enablementVerified());
    assert(std::string_view(runtime->descriptorIndexing().diagnosticId)
        == "RHI-VULKAN-INDEXING-ENABLEMENT-UNSUPPORTED");
    for (unsigned missing = 0; missing < 3; ++missing) {
        featureQueryAvailable = missing != 0;
        propertiesQueryAvailable = missing != 1;
        apiVersion = missing == 2 ? VK_API_VERSION_1_1 : VK_API_VERSION_1_2;
        const auto before = featureQueries;
        runtime = createVulkanRhiRuntime(desc);
        assert(!runtime->descriptorIndexing().queried
            && !runtime->descriptorIndexing().enablementVerified());
        assert(!runtime->descriptorIndexing().limits.maxDescriptorSetStorageBuffers
            && before == featureQueries);
    }
    featureQueryAvailable = propertiesQueryAvailable = true;
    apiVersion = VK_API_VERSION_1_2;
    desc.enableSyntheticRuntime = true;
    const auto before = featureQueries;
    runtime = createVulkanRhiRuntime(desc);
    assert(!runtime->descriptorIndexing().queried
        && !runtime->descriptorIndexing().enablementVerified() && before == featureQueries);
}

int native()
{
    for (const bool enable : {false, true}) {
        arp::tests::VulkanHeadlessRuntime fixture(enable);
        if (!fixture.runtime())
            return 77;
        const auto& runtime = *fixture.runtime();
        const auto& indexed = runtime.descriptorIndexing();
        assert(indexed.enablementVerified());
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(runtime.physicalDevice(), &properties);
        assert(indexed.limits.maxPerStageDescriptorSampledImages
            == properties.limits.maxPerStageDescriptorSampledImages);
        assert(indexed.limits.maxDescriptorSetStorageImages
            == properties.limits.maxDescriptorSetStorageImages);
        VkPhysicalDeviceVulkan12Features supported12{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        supported.pNext = &supported12;
        vkGetPhysicalDeviceFeatures2(runtime.physicalDevice(), &supported);
        const auto observed =
            VulkanDescriptorIndexingFeatures::capture(supported.features, supported12);
        assert(observed.subsetOf(indexed.supported) && indexed.supported.subsetOf(observed));
        if (enable)
            assert(indexed.enabled.subsetOf(observed) && observed.subsetOf(indexed.enabled));
        else
            assert(indexed.enabled.subsetOf({}));
        std::printf(
            "Queried/enabled Vulkan indexing: requested=%u nonUniform=%u,%u,%u,%u samplersPerStage=%u\n",
            unsigned(enable),
            unsigned(indexed.enabled.nonUniform[0]),
            unsigned(indexed.enabled.nonUniform[1]),
            unsigned(indexed.enabled.nonUniform[2]),
            unsigned(indexed.enabled.nonUniform[3]),
            indexed.limits.maxPerStageDescriptorSamplers);
    }
    std::puts(
        "PASS: physical-query/device-enablement evidence only; no bindless execution authorization");
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc > 1 && std::string_view(argv[1]) == "--native")
        return native();
    contract();
}
