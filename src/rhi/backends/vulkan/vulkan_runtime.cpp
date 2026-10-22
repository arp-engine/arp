#include "rhi/backends/vulkan/vulkan_runtime.h"

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_original_device_native_loss.h"

#include <cstring>
#include <limits>
#include <mutex>
#include <string_view>
#include <utility>

namespace arp::rhi_vulkan {

namespace {

template <typename Fn> Fn functionFrom(PFN_vkVoidFunction value)
{
    Fn result = nullptr;
    static_assert(sizeof(result) == sizeof(value), "unexpected Vulkan function pointer size");
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

} // namespace

VulkanDescriptorIndexingFeatures VulkanDescriptorIndexingFeatures::capture(
    const VkPhysicalDeviceFeatures& core, const VkPhysicalDeviceVulkan12Features& indexed) noexcept
{
    VulkanDescriptorIndexingFeatures result;
    result.dynamicallyUniform = {{core.shaderSampledImageArrayDynamicIndexing == VK_TRUE,
        core.shaderStorageImageArrayDynamicIndexing == VK_TRUE,
        core.shaderUniformBufferArrayDynamicIndexing == VK_TRUE,
        core.shaderStorageBufferArrayDynamicIndexing == VK_TRUE}};
    result.nonUniform = {{indexed.shaderSampledImageArrayNonUniformIndexing == VK_TRUE,
        indexed.shaderStorageImageArrayNonUniformIndexing == VK_TRUE,
        indexed.shaderUniformBufferArrayNonUniformIndexing == VK_TRUE,
        indexed.shaderStorageBufferArrayNonUniformIndexing == VK_TRUE}};
    result.descriptorIndexing = indexed.descriptorIndexing == VK_TRUE;
    result.vertexStoresAndAtomics = core.vertexPipelineStoresAndAtomics == VK_TRUE;
    result.fragmentStoresAndAtomics = core.fragmentStoresAndAtomics == VK_TRUE;
    return result;
}

void VulkanDescriptorIndexingFeatures::applyTo(
    VkPhysicalDeviceFeatures& core, VkPhysicalDeviceVulkan12Features& indexed) const noexcept
{
    core.shaderSampledImageArrayDynamicIndexing = dynamicallyUniform[0] ? VK_TRUE : VK_FALSE;
    core.shaderStorageImageArrayDynamicIndexing = dynamicallyUniform[1] ? VK_TRUE : VK_FALSE;
    core.shaderUniformBufferArrayDynamicIndexing = dynamicallyUniform[2] ? VK_TRUE : VK_FALSE;
    core.shaderStorageBufferArrayDynamicIndexing = dynamicallyUniform[3] ? VK_TRUE : VK_FALSE;
    indexed.shaderSampledImageArrayNonUniformIndexing = nonUniform[0] ? VK_TRUE : VK_FALSE;
    indexed.shaderStorageImageArrayNonUniformIndexing = nonUniform[1] ? VK_TRUE : VK_FALSE;
    indexed.shaderUniformBufferArrayNonUniformIndexing = nonUniform[2] ? VK_TRUE : VK_FALSE;
    indexed.shaderStorageBufferArrayNonUniformIndexing = nonUniform[3] ? VK_TRUE : VK_FALSE;
    indexed.descriptorIndexing = descriptorIndexing ? VK_TRUE : VK_FALSE;
    core.vertexPipelineStoresAndAtomics = vertexStoresAndAtomics ? VK_TRUE : VK_FALSE;
    core.fragmentStoresAndAtomics = fragmentStoresAndAtomics ? VK_TRUE : VK_FALSE;
}

bool VulkanDescriptorIndexingFeatures::subsetOf(
    const VulkanDescriptorIndexingFeatures& supported) const noexcept
{
    for (std::size_t i = 0; i < dynamicallyUniform.size(); ++i)
        if ((dynamicallyUniform[i] && !supported.dynamicallyUniform[i])
            || (nonUniform[i] && !supported.nonUniform[i]))
            return false;
    return (!descriptorIndexing || supported.descriptorIndexing)
        && (!vertexStoresAndAtomics || supported.vertexStoresAndAtomics)
        && (!fragmentStoresAndAtomics || supported.fragmentStoresAndAtomics);
}

struct VulkanRhiRuntime::Impl {
    explicit Impl(VulkanRhiRuntimeDesc runtimeDesc) : desc(std::move(runtimeDesc))
    {
        if (desc.queueFamilyCount == 0) {
            desc.queueFamilyCount = 1;
        }
        instance = desc.instance;
        physicalDevice = desc.physicalDevice;
        logicalDevice = desc.logicalDevice;
        graphicsQueue = desc.graphicsQueue;
        presentQueue = desc.presentQueue;
        surface = desc.surface;

        if (!desc.enableSyntheticRuntime) {
            if (!desc.nativeOwner) {
                admissionDiagnostic = "RHI-VULKAN-RUNTIME-OWNER-MISSING";
                return;
            }
            if (!instance || !physicalDevice || !logicalDevice
                || desc.nativeOwner->nativeInstance() != instance
                || desc.nativeOwner->nativePhysicalDevice() != physicalDevice
                || desc.nativeOwner->nativeDevice() != logicalDevice) {
                admissionDiagnostic = "RHI-VULKAN-RUNTIME-OWNER-MISMATCH";
                return;
            }
            if (!desc.getInstanceProcAddr || !desc.getDeviceProcAddr) {
                admissionDiagnostic = "RHI-DEVICE-NATIVE-RUNTIME-UNAVAILABLE";
                return;
            }
            nativeAdmitted = true;
            queryNativeRuntimeDetails();
            return;
        }

        memory.memoryTypeCount = 0;
        if (desc.hasDeviceLocalMemory) {
            memory.memoryTypes[memory.memoryTypeCount++].propertyFlags =
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        }
        if (desc.hasHostVisibleMemory) {
            memory.memoryTypes[memory.memoryTypeCount++].propertyFlags =
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }

        capabilities.minImageCount = 2;
        capabilities.maxImageCount = 3;
        capabilities.currentExtent = {320, 180};
        capabilities.minImageExtent = {1, 1};
        capabilities.maxImageExtent = {8192, 8192};

        formats.push_back({
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        });
        presentModes.push_back(VK_PRESENT_MODE_FIFO_KHR);
        queryNativeRuntimeDetails();
    }

    ~Impl()
    {
        if (!nativeAdmitted)
            return;
        const auto destroyFence =
            functionFrom<PFN_vkDestroyFence>(desc.getDeviceProcAddr && logicalDevice
                    ? desc.getDeviceProcAddr(logicalDevice, "vkDestroyFence")
                    : nullptr);
        if (destroyFence) {
            for (auto& entry : presentFences)
                if (entry.fence)
                    destroyFence(logicalDevice, entry.fence, nullptr);
        }
    }

    struct PresentFenceEntry {
        VkFence fence = VK_NULL_HANDLE;
        std::uint64_t token = 0;
        bool abandoned = false;
    };

    void retireCompletedPresentFences()
    {
        const auto getStatus = functionFrom<PFN_vkGetFenceStatus>(
            desc.getDeviceProcAddr(logicalDevice, "vkGetFenceStatus"));
        const auto destroyFence = functionFrom<PFN_vkDestroyFence>(
            desc.getDeviceProcAddr(logicalDevice, "vkDestroyFence"));
        if (!getStatus || !destroyFence)
            return;
        for (auto& entry : presentFences) {
            if (entry.fence && getStatus(logicalDevice, entry.fence) == VK_SUCCESS) {
                destroyFence(logicalDevice, entry.fence, nullptr);
                entry.fence = VK_NULL_HANDLE;
                if (entry.abandoned)
                    entry.token = 0;
            }
        }
    }

    void queryNativeRuntimeDetails()
    {
        // Descriptor memory flags belong only to the explicit synthetic fixture.
        // A missing native query source must not leave those flags observable.
        if (!desc.enableSyntheticRuntime)
            memory = {};
        if (!nativeAdmitted || instance == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE
            || desc.getInstanceProcAddr == nullptr) {
            return;
        }

        queryCoreQueueCapabilities();

        auto* getMemoryProperties = functionFrom<PFN_vkGetPhysicalDeviceMemoryProperties>(
            desc.getInstanceProcAddr(instance, "vkGetPhysicalDeviceMemoryProperties"));
        if (getMemoryProperties) {
            getMemoryProperties(physicalDevice, &memory);
            if (memory.memoryTypeCount == 0u || memory.memoryTypeCount > VK_MAX_MEMORY_TYPES) {
                memory = {};
                rhi::RhiErrorHandler::get().report(
                    rhi::RhiSeverity::Error, "RHI-VULKAN-MEMORY-PROPERTIES-INVALID");
            }
        } else {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, "RHI-VULKAN-MEMORY-QUERY-UNAVAILABLE");
        }

        auto* getPhysicalDeviceProperties = functionFrom<PFN_vkGetPhysicalDeviceProperties>(
            desc.getInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties"));
        if (getPhysicalDeviceProperties) {
            getPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
        }

        indexing = {};
        indirectFirstInstanceEnabled = false;
        auto* getFeatures = functionFrom<PFN_vkGetPhysicalDeviceFeatures2>(
            desc.getInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2"));
        if (getPhysicalDeviceProperties && getFeatures
            && physicalDeviceProperties.apiVersion >= VK_API_VERSION_1_2) {
            VkPhysicalDeviceVulkan12Features features12{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
            VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features.pNext = &features12;
            getFeatures(physicalDevice, &features);
            timelineSemaphoreSupported = features12.timelineSemaphore == VK_TRUE;
            indirectFirstInstanceEnabled = desc.indirectFirstInstanceEnablementReported
                && desc.indirectFirstInstanceEnabled
                && features.features.drawIndirectFirstInstance == VK_TRUE;
            indexing.queried = true;
            indexing.limits = physicalDeviceProperties.limits;
            // maxStorageBufferRange limits a binding slice, not VkBuffer size.
            // Query maintenance4 only with explicit physical support.
            bool maintenance4 = physicalDeviceProperties.apiVersion >= VK_API_VERSION_1_3;
            auto* enumerateExtensions = functionFrom<PFN_vkEnumerateDeviceExtensionProperties>(
                desc.getInstanceProcAddr(instance, "vkEnumerateDeviceExtensionProperties"));
            if (!maintenance4 && enumerateExtensions) {
                std::uint32_t count = 0u;
                if (enumerateExtensions(physicalDevice, nullptr, &count, nullptr) == VK_SUCCESS) {
                    core::pmr::Vector<VkExtensionProperties> extensions{
                        &core::defaultCpuMemoryResource()};
                    extensions.resize(count);
                    if (enumerateExtensions(physicalDevice, nullptr, &count, extensions.data())
                        == VK_SUCCESS)
                        for (std::uint32_t i = 0u; i < count; ++i)
                            if (std::string_view(extensions[i].extensionName)
                                == VK_KHR_MAINTENANCE_4_EXTENSION_NAME)
                                maintenance4 = true;
                }
            }
            auto* getProperties2 = functionFrom<PFN_vkGetPhysicalDeviceProperties2>(
                desc.getInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2"));
            if (!getProperties2)
                getProperties2 = functionFrom<PFN_vkGetPhysicalDeviceProperties2>(
                    desc.getInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2KHR"));
            if (maintenance4 && getProperties2) {
                VkPhysicalDeviceMaintenance4Properties resourceLimits{
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES};
                VkPhysicalDeviceProperties2 properties{
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
                properties.pNext = &resourceLimits;
                getProperties2(physicalDevice, &properties);
                indexing.maintenance4Queried = true;
                indexing.maxBufferSize = resourceLimits.maxBufferSize;
            }
            indexing.supported =
                VulkanDescriptorIndexingFeatures::capture(features.features, features12);
            indexing.enablementReported = desc.descriptorIndexingEnablementReported;
            if (indexing.enablementReported) {
                indexing.enabled = desc.descriptorIndexingEnabled;
                indexing.enabledFeaturesSupported = indexing.enabled.subsetOf(indexing.supported);
            }
            indexing.diagnosticId = !indexing.enablementReported
                ? "RHI-VULKAN-INDEXING-ENABLEMENT-MISSING"
                : !indexing.enabledFeaturesSupported ? "RHI-VULKAN-INDEXING-ENABLEMENT-UNSUPPORTED"
                                                     : "RHI-VULKAN-INDEXING-ENABLEMENT-VERIFIED";
        }

        if (surface == VK_NULL_HANDLE) {
            return;
        }

        formats.clear();
        presentModes.clear();
        capabilities = {};

        auto* getSurfaceCapabilities = functionFrom<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
            desc.getInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
        auto* getSurfaceFormats = functionFrom<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(
            desc.getInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR"));
        auto* getPresentModes = functionFrom<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(
            desc.getInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfacePresentModesKHR"));
        if (!getSurfaceCapabilities || !getSurfaceFormats || !getPresentModes) {
            return;
        }

        if (getSurfaceCapabilities(physicalDevice, surface, &capabilities) != VK_SUCCESS) {
            capabilities = {};
            return;
        }

        std::uint32_t formatCount = 0;
        if (getSurfaceFormats(physicalDevice, surface, &formatCount, nullptr) == VK_SUCCESS
            && formatCount > 0) {
            formats.resize(formatCount);
            if (getSurfaceFormats(physicalDevice, surface, &formatCount, formats.data())
                != VK_SUCCESS) {
                formats.clear();
            } else {
                formats.resize(formatCount);
            }
        }

        std::uint32_t presentModeCount = 0;
        if (getPresentModes(physicalDevice, surface, &presentModeCount, nullptr) == VK_SUCCESS
            && presentModeCount > 0) {
            presentModes.resize(presentModeCount);
            if (getPresentModes(physicalDevice, surface, &presentModeCount, presentModes.data())
                != VK_SUCCESS) {
                presentModes.clear();
            } else {
                presentModes.resize(presentModeCount);
            }
        }
    }

    void queryCoreQueueCapabilities()
    {
        coreQueue = {};
        coreQueueDiagnostic = "RHI-VULKAN-CORE-QUEUE-QUERY-UNAVAILABLE";
        if (logicalDevice == VK_NULL_HANDLE || graphicsQueue == VK_NULL_HANDLE)
            return;
        const auto query = functionFrom<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            desc.getInstanceProcAddr(instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
        if (!query)
            return;
        std::uint32_t count = 0u;
        query(physicalDevice, &count, nullptr);
        if (count == 0u || desc.graphicsQueueFamilyIndex >= count) {
            coreQueueDiagnostic = "RHI-VULKAN-CORE-QUEUE-IDENTITY-MISMATCH";
            return;
        }
        // The actual native query count bounds this cold temporary allocation.
        // desc.queueFamilyCount is legacy selected-queue metadata, not the
        // physical device's family count or authority for these native facts.
        core::pmr::Vector<VkQueueFamilyProperties> families{&core::defaultCpuMemoryResource()};
        families.resize(count);
        query(physicalDevice, &count, families.data());
        if (count != families.size() || families[desc.graphicsQueueFamilyIndex].queueCount == 0u) {
            coreQueueDiagnostic = "RHI-VULKAN-CORE-QUEUE-IDENTITY-MISMATCH";
            return;
        }
        const auto flags = families[desc.graphicsQueueFamilyIndex].queueFlags;
        const auto getQueue = functionFrom<PFN_vkGetDeviceQueue>(desc.getDeviceProcAddr
                ? desc.getDeviceProcAddr(logicalDevice, "vkGetDeviceQueue")
                : nullptr);
        if (!getQueue)
            return;
        VkQueue queriedQueue = VK_NULL_HANDLE;
        getQueue(logicalDevice, desc.graphicsQueueFamilyIndex, 0u, &queriedQueue);
        if (queriedQueue == VK_NULL_HANDLE || queriedQueue != graphicsQueue) {
            coreQueueDiagnostic = "RHI-VULKAN-CORE-QUEUE-IDENTITY-MISMATCH";
            return;
        }
        coreQueue.graphics = (flags & VK_QUEUE_GRAPHICS_BIT) != 0u;
        coreQueue.compute = (flags & VK_QUEUE_COMPUTE_BIT) != 0u;
        coreQueueDiagnostic =
            coreQueue.graphics || coreQueue.compute ? nullptr : "RHI-VULKAN-CORE-QUEUE-UNSUPPORTED";
    }

    VulkanRhiRuntimeDesc desc;
    bool nativeAdmitted = false;
    const char* admissionDiagnostic = nullptr;
    rhi::CoreCapabilities coreQueue;
    const char* coreQueueDiagnostic = "RHI-VULKAN-CORE-QUEUE-QUERY-UNAVAILABLE";
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice logicalDevice = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory{};
    VkPhysicalDeviceProperties physicalDeviceProperties{};
    VulkanDescriptorIndexingInspection indexing;
    bool indirectFirstInstanceEnabled = false;
    bool timelineSemaphoreSupported = false;
    VkSurfaceCapabilitiesKHR capabilities{};
    std::pmr::vector<VkSurfaceFormatKHR> formats;
    std::pmr::vector<VkPresentModeKHR> presentModes;
    mutable std::mutex presentFenceMutex;
    std::array<PresentFenceEntry, kVulkanMaxPendingPresentFences> presentFences{};
    std::uint64_t nextPresentToken = 1u;
    // NativeResultLoss/1 authors one synchronous source subscription. Weak
    // custody avoids a runtime/binding cycle and never publishes raw userdata.
    std::weak_ptr<VulkanOriginalDeviceNativeResultLoss> originalResultLoss;
    bool originalDeviceLost = false;
};

VulkanRhiRuntime::VulkanRhiRuntime(const VulkanRhiRuntimeDesc& desc)
    : impl_(arp::core::makeShared<Impl>(desc))
{}

const char* VulkanRhiRuntime::attachOriginalResultLoss(
    const std::shared_ptr<VulkanOriginalDeviceNativeResultLoss>& binding) noexcept
{
    if (!impl_ || !valid() || syntheticRuntime() || !logicalDevice())
        return "RHI-ORIGINAL-RESULT-LOSS-NATIVE-SOURCE-UNAVAILABLE";
    if (impl_->originalDeviceLost)
        return "RHI-VULKAN-RESULT-LOSS-RUNTIME-LOST";
    if (!binding || !binding->hasQuotaBoundSharedOwner(binding) || binding->runtime_.get() != this)
        return "RHI-VULKAN-RESULT-LOSS-BINDING-OWNER";
    if (!impl_->originalResultLoss.expired())
        return "RHI-VULKAN-RESULT-LOSS-SOURCE-CAPACITY";
    impl_->originalResultLoss = binding;
    return nullptr;
}

const char* VulkanRhiRuntime::validateOriginalResultLoss(
    const VulkanOriginalDeviceNativeResultLoss* binding) const noexcept
{
    if (!impl_ || !valid() || syntheticRuntime() || !logicalDevice())
        return "RHI-ORIGINAL-RESULT-LOSS-NATIVE-SOURCE-UNAVAILABLE";
    if (impl_->originalDeviceLost)
        return "RHI-VULKAN-RESULT-LOSS-RUNTIME-LOST";
    const auto retained = impl_->originalResultLoss.lock();
    return retained && retained.get() == binding && retained->hasQuotaBoundSharedOwner(retained)
        ? nullptr
        : "RHI-VULKAN-RESULT-LOSS-SOURCE-IDENTITY";
}

const char* VulkanRhiRuntime::detachOriginalResultLoss(
    const VulkanOriginalDeviceNativeResultLoss* binding) noexcept
{
    if (!impl_)
        return "RHI-VULKAN-RESULT-LOSS-RUNTIME-MISSING";
    const auto retained = impl_->originalResultLoss.lock();
    // A binding's last-owner destructor may already see an expired weak slot.
    if (!retained) {
        impl_->originalResultLoss.reset();
        return nullptr;
    }
    if (retained.get() != binding)
        return "RHI-VULKAN-RESULT-LOSS-SOURCE-IDENTITY";
    impl_->originalResultLoss.reset();
    return nullptr;
}

void VulkanRhiRuntime::captureOriginalDeviceLoss(VkResult result) const noexcept
{
    if (result != VK_ERROR_DEVICE_LOST || !impl_ || syntheticRuntime() || impl_->originalDeviceLost)
        return;
    impl_->originalDeviceLost = true;
    if (const auto binding = impl_->originalResultLoss.lock())
        binding->deliverNativeLoss();
}

bool VulkanRhiRuntime::valid() const
{
    if (!impl_) {
        return false;
    }
    if (impl_->desc.enableSyntheticRuntime) {
        return true;
    }
    return impl_->nativeAdmitted;
}

const char* VulkanRhiRuntime::admissionDiagnostic() const noexcept
{
    return impl_ ? impl_->admissionDiagnostic : "RHI-DEVICE-NATIVE-RUNTIME-UNAVAILABLE";
}

bool VulkanRhiRuntime::syntheticRuntime() const
{
    return valid() && impl_->desc.enableSyntheticRuntime;
}

bool VulkanRhiRuntime::nativeHandlesPrivate() const
{
    return !impl_ || impl_->desc.nativeHandlesPrivate;
}

rhi::CoreCapabilities VulkanRhiRuntime::coreQueueCapabilities() const noexcept
{
    return valid() && !syntheticRuntime() && !impl_->originalDeviceLost ? impl_->coreQueue
                                                                        : rhi::CoreCapabilities{};
}

const char* VulkanRhiRuntime::coreQueueCapabilityDiagnostic() const noexcept
{
    if (!valid() || syntheticRuntime() || impl_->originalDeviceLost)
        return "RHI-DEVICE-NATIVE-RUNTIME-UNAVAILABLE";
    return impl_->coreQueueDiagnostic;
}

const std::pmr::string& VulkanRhiRuntime::label() const
{
    static const std::pmr::string empty;
    return impl_ ? impl_->desc.label : empty;
}

VkInstance VulkanRhiRuntime::instance() const
{
    return valid() ? impl_->instance : VK_NULL_HANDLE;
}

PFN_vkGetInstanceProcAddr VulkanRhiRuntime::getInstanceProcAddr() const
{
    return impl_ && impl_->nativeAdmitted ? impl_->desc.getInstanceProcAddr : nullptr;
}

PFN_vkGetDeviceProcAddr VulkanRhiRuntime::getDeviceProcAddr() const
{
    return impl_ && impl_->nativeAdmitted ? impl_->desc.getDeviceProcAddr : nullptr;
}

PFN_vkVoidFunction VulkanRhiRuntime::getDeviceProcAddr(const char* name) const
{
    return impl_ && impl_->nativeAdmitted && name
        ? impl_->desc.getDeviceProcAddr(impl_->logicalDevice, name)
        : nullptr;
}

VkPhysicalDevice VulkanRhiRuntime::physicalDevice() const
{
    return valid() ? impl_->physicalDevice : VK_NULL_HANDLE;
}

VkDevice VulkanRhiRuntime::logicalDevice() const
{
    return valid() ? impl_->logicalDevice : VK_NULL_HANDLE;
}

VkQueue VulkanRhiRuntime::graphicsQueue() const
{
    return valid() ? impl_->graphicsQueue : VK_NULL_HANDLE;
}

VkQueue VulkanRhiRuntime::presentQueue() const
{
    return valid() ? impl_->presentQueue : VK_NULL_HANDLE;
}

VkSurfaceKHR VulkanRhiRuntime::surface() const
{
    return valid() ? impl_->surface : VK_NULL_HANDLE;
}

bool VulkanRhiRuntime::presentFenceEnabled() const noexcept
{
    return impl_ && impl_->nativeAdmitted && impl_->desc.swapchainMaintenance1Enabled
        && impl_->logicalDevice;
}

bool VulkanRhiRuntime::reservePresentFence(
    VkFence& fence, std::uint64_t& token, std::pmr::string& diagnostics)
{
    fence = VK_NULL_HANDLE;
    token = 0;
    if (!presentFenceEnabled()) {
        diagnostics = "RHI-VULKAN-PRESENT-FENCE-UNAVAILABLE";
        return false;
    }
    std::lock_guard lock(impl_->presentFenceMutex);
    impl_->retireCompletedPresentFences();
    auto* slot = static_cast<Impl::PresentFenceEntry*>(nullptr);
    for (auto& entry : impl_->presentFences)
        if (!entry.token) {
            slot = &entry;
            break;
        }
    if (!slot) {
        diagnostics = "RHI-VULKAN-PRESENT-FENCE-CAPACITY";
        return false;
    }
    if (impl_->nextPresentToken == std::numeric_limits<std::uint64_t>::max()) {
        diagnostics = "RHI-VULKAN-PRESENT-FENCE-TOKEN-EXHAUSTED";
        return false;
    }
    const auto createFence = functionFrom<PFN_vkCreateFence>(getDeviceProcAddr("vkCreateFence"));
    if (!createFence) {
        diagnostics = "RHI-VULKAN-PRESENT-FENCE-CREATE-UNAVAILABLE";
        return false;
    }
    VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (createFence(impl_->logicalDevice, &info, nullptr, &fence) != VK_SUCCESS || !fence) {
        diagnostics = "RHI-VULKAN-PRESENT-FENCE-CREATE-FAILED";
        return false;
    }
    token = impl_->nextPresentToken++;
    *slot = {fence, token, false};
    diagnostics.clear();
    return true;
}

bool VulkanRhiRuntime::presentFenceCompleted(std::uint64_t token) const
{
    if (!presentFenceEnabled() || !token)
        return false;
    std::lock_guard lock(impl_->presentFenceMutex);
    impl_->retireCompletedPresentFences();
    for (auto& entry : impl_->presentFences)
        if (entry.token == token) {
            if (entry.fence)
                return false;
            entry.token = 0;
            return true;
        }
    return false;
}

bool VulkanRhiRuntime::waitPresentFenceUntil(std::uint64_t token,
    std::chrono::steady_clock::time_point deadline,
    std::pmr::string& diagnostics)
{
    if (!presentFenceEnabled() || !token) {
        diagnostics = "RHI-VULKAN-PRESENT-FENCE-UNAVAILABLE";
        return false;
    }
    std::lock_guard lock(impl_->presentFenceMutex);
    impl_->retireCompletedPresentFences();
    for (auto& entry : impl_->presentFences) {
        if (entry.token != token)
            continue;
        if (!entry.fence) {
            entry.token = 0;
            diagnostics.clear();
            return true;
        }
        const auto now = std::chrono::steady_clock::now();
        if (deadline <= now) {
            diagnostics = "RHI-VULKAN-COMPLETION-DEADLINE-EXPIRED";
            return false;
        }
        const auto waitForFences =
            functionFrom<PFN_vkWaitForFences>(getDeviceProcAddr("vkWaitForFences"));
        if (!waitForFences) {
            diagnostics = "RHI-VULKAN-PRESENT-FENCE-WAIT-UNAVAILABLE";
            return false;
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count();
        const auto waited = waitForFences(
            impl_->logicalDevice, 1u, &entry.fence, VK_TRUE, static_cast<std::uint64_t>(remaining));
        if (waited != VK_SUCCESS) {
            diagnostics = waited == VK_TIMEOUT   ? "RHI-VULKAN-COMPLETION-DEADLINE-EXPIRED"
                : waited == VK_ERROR_DEVICE_LOST ? "RHI-VULKAN-COMPLETION-DEVICE-LOST"
                                                 : "RHI-VULKAN-PRESENT-FENCE-WAIT-FAILED";
            return false;
        }
        impl_->retireCompletedPresentFences();
        entry.token = 0;
        diagnostics.clear();
        return true;
    }
    diagnostics = "RHI-VULKAN-PRESENT-FENCE-TOKEN-INVALID";
    return false;
}

void VulkanRhiRuntime::abandonPresentFence(std::uint64_t token) const
{
    if (!presentFenceEnabled() || !token)
        return;
    std::lock_guard lock(impl_->presentFenceMutex);
    for (auto& entry : impl_->presentFences)
        if (entry.token == token) {
            entry.abandoned = true;
            if (!entry.fence)
                entry.token = 0;
            break;
        }
    impl_->retireCompletedPresentFences();
}

bool VulkanRhiRuntime::drainPresentFencesUntil(
    std::chrono::steady_clock::time_point deadline, std::pmr::string& diagnostics)
{
    if (!presentFenceEnabled()) {
        diagnostics = "RHI-VULKAN-PRESENT-FENCE-UNAVAILABLE";
        return false;
    }
    if (deadline == std::chrono::steady_clock::time_point{}
        || deadline <= std::chrono::steady_clock::now()) {
        diagnostics = "RHI-VULKAN-COMPLETION-DEADLINE-EXPIRED";
        return false;
    }
    std::lock_guard lock(impl_->presentFenceMutex);
    impl_->retireCompletedPresentFences();
    const auto waitForFences =
        functionFrom<PFN_vkWaitForFences>(getDeviceProcAddr("vkWaitForFences"));
    if (!waitForFences) {
        diagnostics = "RHI-VULKAN-PRESENT-FENCE-WAIT-UNAVAILABLE";
        return false;
    }
    for (auto& entry : impl_->presentFences) {
        if (!entry.fence)
            continue;
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            diagnostics = "RHI-VULKAN-COMPLETION-DEADLINE-EXPIRED";
            return false;
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count();
        const auto waited = waitForFences(
            impl_->logicalDevice, 1u, &entry.fence, VK_TRUE, static_cast<std::uint64_t>(remaining));
        if (waited != VK_SUCCESS) {
            diagnostics = waited == VK_TIMEOUT   ? "RHI-VULKAN-COMPLETION-DEADLINE-EXPIRED"
                : waited == VK_ERROR_DEVICE_LOST ? "RHI-VULKAN-COMPLETION-DEVICE-LOST"
                                                 : "RHI-VULKAN-PRESENT-FENCE-WAIT-FAILED";
            impl_->retireCompletedPresentFences();
            return false;
        }
    }
    impl_->retireCompletedPresentFences();
    diagnostics.clear();
    return true;
}

std::uint32_t VulkanRhiRuntime::graphicsQueueFamilyIndex() const
{
    return impl_ ? impl_->desc.graphicsQueueFamilyIndex : 0;
}

std::uint32_t VulkanRhiRuntime::presentQueueFamilyIndex() const
{
    return impl_ ? impl_->desc.presentQueueFamilyIndex : 0;
}

std::uint32_t VulkanRhiRuntime::queueFamilyCount() const
{
    return impl_ ? impl_->desc.queueFamilyCount : 0;
}

bool VulkanRhiRuntime::refreshSurfaceDetails()
{
    if (!impl_ || !valid()) {
        return false;
    }
    impl_->queryNativeRuntimeDetails();
    if (impl_->desc.enableSyntheticRuntime || impl_->surface == VK_NULL_HANDLE) {
        return true;
    }
    return !impl_->formats.empty() && !impl_->presentModes.empty();
}

const VkPhysicalDeviceMemoryProperties& VulkanRhiRuntime::memoryProperties() const
{
    static const VkPhysicalDeviceMemoryProperties empty{};
    return impl_ ? impl_->memory : empty;
}

const VkPhysicalDeviceLimits& VulkanRhiRuntime::nativeLimits() const noexcept
{
    static const VkPhysicalDeviceLimits empty{};
    return impl_ && !coreQueueCapabilityDiagnostic() ? impl_->physicalDeviceProperties.limits
                                                     : empty;
}

float VulkanRhiRuntime::maxSamplerAnisotropy() const
{
    if (!impl_) {
        return 1.0f;
    }
    const float limit = impl_->physicalDeviceProperties.limits.maxSamplerAnisotropy;
    // A zero limit indicates the properties were never queried (synthetic
    // runtime). Clamp to the Vulkan minimum of 1.0 so callers that disable
    // anisotropy (maxAnisotropy <= 1.0) are unaffected.
    return limit > 0.0f ? limit : 1.0f;
}

const VkSurfaceCapabilitiesKHR& VulkanRhiRuntime::surfaceCapabilities() const
{
    static const VkSurfaceCapabilitiesKHR empty{};
    return impl_ ? impl_->capabilities : empty;
}

const VulkanDescriptorIndexingInspection& VulkanRhiRuntime::descriptorIndexing() const
{
    static const VulkanDescriptorIndexingInspection unavailable;
    return valid() && !syntheticRuntime() ? impl_->indexing : unavailable;
}

bool VulkanRhiRuntime::indirectFirstInstanceEnabled() const noexcept
{
    return valid() && !syntheticRuntime() && impl_->indirectFirstInstanceEnabled;
}

bool VulkanRhiRuntime::timelineSemaphoreEnabled() const noexcept
{
    return valid() && !syntheticRuntime() && impl_->timelineSemaphoreSupported
        && impl_->desc.timelineSemaphoreEnablementReported && impl_->desc.timelineSemaphoreEnabled;
}

const std::pmr::vector<VkSurfaceFormatKHR>& VulkanRhiRuntime::surfaceFormats() const
{
    static const std::pmr::vector<VkSurfaceFormatKHR> empty;
    return impl_ ? impl_->formats : empty;
}

const std::pmr::vector<VkPresentModeKHR>& VulkanRhiRuntime::presentModes() const
{
    static const std::pmr::vector<VkPresentModeKHR> empty;
    return impl_ ? impl_->presentModes : empty;
}

std::shared_ptr<VulkanRhiRuntime> createVulkanRhiRuntime(const VulkanRhiRuntimeDesc& desc)
{
    auto runtime = arp::core::makeShared<VulkanRhiRuntime>(desc);
    if (!runtime->valid()) {
        rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error, runtime->admissionDiagnostic());
        return nullptr;
    }
    return runtime;
}

VulkanRhiRuntimeInspection inspectVulkanRhiRuntime(const VulkanRhiRuntime& runtime)
{
    VulkanRhiRuntimeInspection inspection;
    inspection.success = runtime.valid() && runtime.nativeHandlesPrivate();
    inspection.runtimeOwned = runtime.valid();
    inspection.nativeHandlesPrivate = runtime.nativeHandlesPrivate();
    inspection.nativeDeviceAvailable =
        runtime.valid() && !runtime.syntheticRuntime() && runtime.logicalDevice() != VK_NULL_HANDLE;
    inspection.nativeSurfaceAvailable =
        inspection.nativeDeviceAvailable && runtime.surface() != VK_NULL_HANDLE;
    inspection.syntheticRuntime = runtime.syntheticRuntime();
    inspection.queueFamilyCount = runtime.queueFamilyCount();
    if (!runtime.valid()) {
        inspection.diagnostics = runtime.admissionDiagnostic();
    } else if (!runtime.nativeHandlesPrivate()) {
        inspection.diagnostics = "Vulkan RHI runtime native handles must stay private";
    }
    return inspection;
}

} // namespace arp::rhi_vulkan
