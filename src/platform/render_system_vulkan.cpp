#define ARP_VULKAN_VALIDATION_ENABLED 1
#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"
#include "platform/frame_input_admission.h"
#include "platform/render_system.h"
#include "platform/vulkan_runtime_bootstrap.h"
#include "platform/vulkan_validation_policy.h"
#include "renderer/frame/render_frame_director.h"
#include "renderer/mesh/mesh_gpu_buffer.h"
#include "renderer/particles/particle_renderer.h"
#include "renderer/pipeline/custom_render_frame_workload.h"
#include "renderer/pipeline/render_pipeline.h"
#include "renderer/rhi_production_frame.h"
#include "renderer/ui/gui_renderer.h"
#include "rhi/backends/vulkan/vulkan_device.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/gpu_device.h"
#include "rhi/gpu_swapchain.h"
#include "rhi/semantics/vulkan_device_capability_query.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace arp::platform {

namespace {

renderer::RendererResult rendererResult(bool success, std::pmr::string diagnostics)
{
    return renderer::RendererResult{success, std::move(diagnostics)};
}

RenderSystemResult renderSystemResult(bool success, std::pmr::string diagnostics)
{
    return RenderSystemResult{success, std::move(diagnostics)};
}

class AcquiredFrameDiscardGuard {
public:
    AcquiredFrameDiscardGuard(rhi::Swapchain& swapchain,
        rhi::AcquiredFrame& currentFrame,
        const rhi::AcquiredFrame& frame)
        : swapchain_(swapchain), currentFrame_(currentFrame), frame_(frame)
    {}

    ~AcquiredFrameDiscardGuard() noexcept
    {
        if (committed_) {
            return;
        }
        const auto discarded = swapchain_.discardFrame(frame_);
        currentFrame_ = {};
        if (!discarded.success) {
            std::fprintf(stderr,
                "[render-system] failed to discard Vulkan frame after render failure: %s\n",
                discarded.diagnostics.c_str());
        }
    }

    void commit() noexcept { committed_ = true; }

private:
    rhi::Swapchain& swapchain_;
    rhi::AcquiredFrame& currentFrame_;
    const rhi::AcquiredFrame& frame_;
    bool committed_ = false;
};

template <typename Fn, typename Source> Fn functionPointerFrom(Source value)
{
    Fn result = nullptr;
    static_assert(sizeof(result) == sizeof(value), "unexpected Vulkan function pointer size");
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

std::pmr::string vkFailure(const char* operation, VkResult result)
{
    std::ostringstream message;
    message << operation << " failed: " << static_cast<int>(result);
    return arp::core::toPmrString(message.str());
}

#if ARP_VULKAN_VALIDATION_ENABLED
static VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*userData*/)
{
    const char* label = "Vulkan";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        label = "Vulkan ERROR";
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        label = "Vulkan WARNING";
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        label = "Vulkan INFO";
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
        label = "Vulkan VERBOSE";
    }
    const char* message = data->pMessage ? data->pMessage : "(no message)";
    const char* messageId = data->pMessageIdName ? data->pMessageIdName : "";

    std::fprintf(stderr, "[%s] %s  (id: %s)\n", label, message, messageId);

    // Print queue label hierarchy if present
    if (data->queueLabelCount > 0) {
        std::fprintf(stderr, "  Queue labels:\n");
        for (std::uint32_t i = 0; i < data->queueLabelCount; ++i) {
            std::fprintf(stderr,
                "    [%u] %s\n",
                i,
                data->pQueueLabels[i].pLabelName ? data->pQueueLabels[i].pLabelName : "");
        }
    }
    // Print command buffer label hierarchy if present
    if (data->cmdBufLabelCount > 0) {
        std::fprintf(stderr, "  Command labels:\n");
        for (std::uint32_t i = 0; i < data->cmdBufLabelCount; ++i) {
            std::fprintf(stderr,
                "    [%u] %s\n",
                i,
                data->pCmdBufLabels[i].pLabelName ? data->pCmdBufLabels[i].pLabelName : "");
        }
    }
    // Print related objects
    if (data->objectCount > 0) {
        std::fprintf(stderr, "  Objects (%u):\n", data->objectCount);
        for (std::uint32_t i = 0; i < data->objectCount; ++i) {
            std::fprintf(stderr,
                "    [%u] type=%d handle=0x%llx name=%s\n",
                i,
                static_cast<int>(data->pObjects[i].objectType),
                static_cast<unsigned long long>(data->pObjects[i].objectHandle),
                data->pObjects[i].pObjectName ? data->pObjects[i].pObjectName : "");
        }
    }

    return VK_FALSE; // don't abort
}
#endif // ARP_VULKAN_VALIDATION_ENABLED

void captureFrameGraphTrace(
    const render_graph::FrameGraph& graph, RenderSystemSceneRuntimeCustomGraphFrameReport& report)
{
    report.frameGraphExecutionPassCount = static_cast<std::uint32_t>(graph.executionTrace().size());
    report.frameGraphAttachmentTraceCount =
        static_cast<std::uint32_t>(graph.attachmentTrace().size());
    report.frameGraphBarrierTraceCount = static_cast<std::uint32_t>(graph.barrierTrace().size());

    std::size_t mainSceneIndex = static_cast<std::size_t>(-1);
    std::size_t particlesIndex = static_cast<std::size_t>(-1);
    std::size_t guiIndex = static_cast<std::size_t>(-1);
    for (std::size_t index = 0; index < graph.executionTrace().size(); ++index) {
        const auto& passName = graph.executionTrace()[index];
        if (!report.frameGraphExecutionTrace.empty()) {
            report.frameGraphExecutionTrace += " -> ";
        }
        report.frameGraphExecutionTrace += passName;
        if (passName == "MainScene") {
            report.frameGraphHasMainScene = true;
            mainSceneIndex = index;
        } else if (passName == "Particles") {
            report.frameGraphHasParticles = true;
            particlesIndex = index;
        } else if (passName == "Gui") {
            report.frameGraphHasGui = true;
            guiIndex = index;
        }
    }
    report.frameGraphParticlesAfterMainScene = mainSceneIndex != static_cast<std::size_t>(-1)
        && particlesIndex != static_cast<std::size_t>(-1) && particlesIndex > mainSceneIndex;
    report.frameGraphGuiAfterParticles = guiIndex != static_cast<std::size_t>(-1)
        && particlesIndex != static_cast<std::size_t>(-1) && guiIndex > particlesIndex;

    for (const auto& attachment : graph.attachmentTrace()) {
        if (attachment.passName != "Particles") {
            continue;
        }
        if (!report.frameGraphParticleAttachmentSummary.empty()) {
            report.frameGraphParticleAttachmentSummary += " | ";
        }
        report.frameGraphParticleAttachmentSummary += attachment.resourceName;
        report.frameGraphParticleAttachmentSummary += " load=";
        switch (attachment.loadOp) {
        case rhi::LoadOp::Load:
            report.frameGraphParticleAttachmentSummary += "Load";
            break;
        case rhi::LoadOp::Clear:
            report.frameGraphParticleAttachmentSummary += "Clear";
            break;
        case rhi::LoadOp::DontCare:
            report.frameGraphParticleAttachmentSummary += "DontCare";
            break;
        }
        report.frameGraphParticleAttachmentSummary += " imported=";
        report.frameGraphParticleAttachmentSummary += attachment.imported ? "true" : "false";
    }
}

struct VulkanQueueFamilySelection {
    bool success = false;
    std::pmr::string diagnostics;
    std::uint32_t graphics = 0;
    std::uint32_t present = 0;
    std::uint32_t familyCount = 0;
};

struct VulkanMemorySupport {
    bool deviceLocal = false;
    bool hostVisible = false;
    /// Total size of all DEVICE_LOCAL memory heaps (the GPU's VRAM), in bytes.
    /// Used to scale the logical allocator budget so high-resolution render
    /// targets do not exhaust a fixed budget tuned for smaller surfaces.
    std::uint64_t deviceLocalHeapBytes = 0;
};

bool deviceSupportsExtension(VkInstance instance,
    VkPhysicalDevice physicalDevice,
    PFN_vkGetInstanceProcAddr getInstanceProcAddr,
    const char* extensionName)
{
    auto* enumerateExtensions = functionPointerFrom<PFN_vkEnumerateDeviceExtensionProperties>(
        getInstanceProcAddr(instance, "vkEnumerateDeviceExtensionProperties"));
    if (!enumerateExtensions) {
        return false;
    }
    std::uint32_t extensionCount = 0;
    if (enumerateExtensions(physicalDevice, nullptr, &extensionCount, nullptr) != VK_SUCCESS
        || extensionCount == 0) {
        return false;
    }
    std::pmr::vector<VkExtensionProperties> extensions(extensionCount);
    if (enumerateExtensions(physicalDevice, nullptr, &extensionCount, extensions.data())
        != VK_SUCCESS) {
        return false;
    }
    for (const auto& extension : extensions) {
        if (std::strcmp(extension.extensionName, extensionName) == 0) {
            return true;
        }
    }
    return false;
}

bool instanceSupportsExtension(
    PFN_vkGetInstanceProcAddr getInstanceProcAddr, const char* extensionName)
{
    auto* enumerateExtensions = functionPointerFrom<PFN_vkEnumerateInstanceExtensionProperties>(
        getInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties"));
    if (!enumerateExtensions)
        return false;
    std::uint32_t extensionCount = 0;
    if (enumerateExtensions(nullptr, &extensionCount, nullptr) != VK_SUCCESS || extensionCount == 0)
        return false;
    std::pmr::vector<VkExtensionProperties> extensions(extensionCount);
    if (enumerateExtensions(nullptr, &extensionCount, extensions.data()) != VK_SUCCESS)
        return false;
    for (const auto& extension : extensions)
        if (std::strcmp(extension.extensionName, extensionName) == 0)
            return true;
    return false;
}

VulkanQueueFamilySelection selectQueueFamilies(VkInstance instance,
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    PFN_vkGetInstanceProcAddr getInstanceProcAddr)
{
    VulkanQueueFamilySelection selection;
    auto* getQueueFamilyProperties =
        functionPointerFrom<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            getInstanceProcAddr(instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    auto* getSurfaceSupport = functionPointerFrom<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
        getInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceSupportKHR"));
    if (!getQueueFamilyProperties || !getSurfaceSupport) {
        selection.diagnostics = "Vulkan surface runtime missing queue-family entrypoints";
        return selection;
    }

    std::uint32_t familyCount = 0;
    getQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    if (familyCount == 0) {
        selection.diagnostics = "Vulkan physical device has no queue families";
        return selection;
    }
    std::pmr::vector<VkQueueFamilyProperties> families(familyCount);
    getQueueFamilyProperties(physicalDevice, &familyCount, families.data());
    selection.familyCount = familyCount;

    std::uint32_t firstGraphics = familyCount;
    std::uint32_t firstPresent = familyCount;
    for (std::uint32_t index = 0; index < familyCount; ++index) {
        VkBool32 presentSupported = VK_FALSE;
        getSurfaceSupport(physicalDevice, index, surface, &presentSupported);
        const bool graphicsSupported = (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        if (graphicsSupported && presentSupported == VK_TRUE) {
            selection.success = true;
            selection.graphics = index;
            selection.present = index;
            return selection;
        }
        if (graphicsSupported && firstGraphics == familyCount) {
            firstGraphics = index;
        }
        if (presentSupported == VK_TRUE && firstPresent == familyCount) {
            firstPresent = index;
        }
    }

    if (firstGraphics < familyCount && firstPresent < familyCount) {
        selection.success = true;
        selection.graphics = firstGraphics;
        selection.present = firstPresent;
        return selection;
    }

    selection.diagnostics =
        "Vulkan physical device lacks graphics and present queue support for the window surface";
    return selection;
}

VulkanMemorySupport queryMemorySupport(VkInstance instance,
    VkPhysicalDevice physicalDevice,
    PFN_vkGetInstanceProcAddr getInstanceProcAddr)
{
    VulkanMemorySupport support;
    auto* getMemoryProperties = functionPointerFrom<PFN_vkGetPhysicalDeviceMemoryProperties>(
        getInstanceProcAddr(instance, "vkGetPhysicalDeviceMemoryProperties"));
    if (!getMemoryProperties) {
        return support;
    }
    VkPhysicalDeviceMemoryProperties memory{};
    getMemoryProperties(physicalDevice, &memory);
    // Sum the size of every heap that backs at least one DEVICE_LOCAL memory
    // type. A heap may be shared by several memory types (e.g. device-local
    // and device-local-host-visible on unified-memory GPUs); each heap is
    // counted once via a visited-bitmap to avoid double-counting.
    bool heapVisited[VK_MAX_MEMORY_HEAPS] = {};
    for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
        const auto flags = memory.memoryTypes[index].propertyFlags;
        support.deviceLocal =
            support.deviceLocal || ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0);
        support.hostVisible =
            support.hostVisible || ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0);
        if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
            const auto heapIndex = memory.memoryTypes[index].heapIndex;
            if (heapIndex < VK_MAX_MEMORY_HEAPS && !heapVisited[heapIndex]) {
                heapVisited[heapIndex] = true;
                support.deviceLocalHeapBytes += memory.memoryHeaps[heapIndex].size;
            }
        }
    }
    return support;
}

class VulkanSurfaceRuntimeOwner final : public rhi_vulkan::VulkanRhiNativeRuntimeOwner {
public:
    VkInstance nativeInstance() const noexcept override { return instance_; }

    VkPhysicalDevice nativePhysicalDevice() const noexcept override { return physicalDevice_; }

    VkDevice nativeDevice() const noexcept override { return logicalDevice_; }

    void waitIdle() const
    {
        if (logicalDevice_ && deviceWaitIdle_) {
            deviceWaitIdle_(logicalDevice_);
        }
    }

    ~VulkanSurfaceRuntimeOwner()
    {
        if (logicalDevice_) {
            waitIdle();
            if (destroyDevice_) {
                destroyDevice_(logicalDevice_, nullptr);
            }
        }
#if ARP_VULKAN_VALIDATION_ENABLED
        if (debugMessenger_ && instance_ && getInstanceProcAddr_) {
            auto* destroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                getInstanceProcAddr_(instance_, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroyDebugMessenger) {
                destroyDebugMessenger(instance_, debugMessenger_, nullptr);
            }
        }
#endif
        surface_.reset();
        if (instance_ && destroyInstance_) {
            destroyInstance_(instance_, nullptr);
        }
    }

    static std::shared_ptr<VulkanSurfaceRuntimeOwner> create(WindowSystem& platform,
        const PlatformWindow& window,
        const std::pmr::string& applicationName,
        std::pmr::string& diagnostics)
    {
        auto owner = core::makeShared<VulkanSurfaceRuntimeOwner>();
        if (!owner->initialize(platform, window, applicationName, diagnostics)) {
            return nullptr;
        }
        return owner;
    }

    rhi_vulkan::VulkanRhiRuntimeDesc runtimeDesc(const std::pmr::string& label) const
    {
        rhi_vulkan::VulkanRhiRuntimeDesc desc;
        desc.label = label;
        desc.enableSyntheticRuntime = false;
        desc.nativeHandlesPrivate = true;
        desc.queueFamilyCount = queueFamilyCount_;
        desc.hasDeviceLocalMemory = memorySupport_.deviceLocal;
        desc.hasHostVisibleMemory = memorySupport_.hostVisible;
        desc.instance = instance_;
        desc.physicalDevice = physicalDevice_;
        desc.logicalDevice = logicalDevice_;
        desc.graphicsQueue = graphicsQueue_;
        desc.presentQueue = presentQueue_;
        desc.surface = surface_ ? surface_->handle() : VK_NULL_HANDLE;
        desc.getInstanceProcAddr = getInstanceProcAddr_;
        desc.getDeviceProcAddr = getDeviceProcAddr_;
        desc.graphicsQueueFamilyIndex = graphicsQueueFamilyIndex_;
        desc.presentQueueFamilyIndex = presentQueueFamilyIndex_;
        desc.descriptorIndexingEnablementReported = descriptorIndexingEnablementReported_;
        desc.descriptorIndexingEnabled = descriptorIndexingEnabled_;
        desc.indirectFirstInstanceEnablementReported = true;
        desc.indirectFirstInstanceEnabled = indirectFirstInstanceEnabled_;
        desc.timelineSemaphoreEnablementReported = true;
        desc.timelineSemaphoreEnabled = timelineSemaphoreEnabled_;
        desc.swapchainMaintenance1Enabled = swapchainMaintenance1Enabled_;
        return desc;
    }

    std::uint32_t queueFamilyCount() const { return queueFamilyCount_; }

    const VulkanMemorySupport& memorySupport() const { return memorySupport_; }

    std::uint32_t maxPushConstantSizeBytes() const { return maxPushConstantSizeBytes_; }

    bool depthBiasClampSupported() const { return depthBiasClampSupported_; }

    bool rg16FloatStorageTextureSupported() const { return rg16FloatStorageTextureSupported_; }

    bool rgba16FloatStorageTextureSupported() const { return rgba16FloatStorageTextureSupported_; }

    bool drawIndirectCountSupported() const { return drawIndirectCountSupported_; }

    bool timestampQueriesSupported() const { return timestampQueriesSupported_; }

    bool timelineSemaphoreEnabled() const { return timelineSemaphoreEnabled_; }

private:
    bool initialize(WindowSystem& platform,
        const PlatformWindow& window,
        const std::pmr::string& applicationName,
        std::pmr::string& diagnostics)
    {
        getInstanceProcAddr_ =
            functionPointerFrom<PFN_vkGetInstanceProcAddr>(platform.vulkanGetInstanceProcAddr());
        if (!getInstanceProcAddr_) {
            diagnostics = "WindowSystem Vulkan loader did not provide vkGetInstanceProcAddr";
            return false;
        }

        auto* createInstance = functionPointerFrom<PFN_vkCreateInstance>(
            getInstanceProcAddr_(nullptr, "vkCreateInstance"));
        if (!createInstance) {
            diagnostics = "Vulkan loader is missing instance creation entrypoints";
            return false;
        }

        auto requiredExtensions = platform.requiredVulkanInstanceExtensions();
        if (requiredExtensions.empty()) {
            diagnostics = "WindowSystem did not report required Vulkan instance extensions";
            return false;
        }
        std::pmr::vector<const char*> extensionNames;
        extensionNames.reserve(requiredExtensions.size());
        for (const auto& extension : requiredExtensions) {
            extensionNames.push_back(extension.c_str());
        }
        const bool surfaceCapabilities2Available = instanceSupportsExtension(
            getInstanceProcAddr_, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
        surfaceMaintenance1Enabled_ = surfaceCapabilities2Available
            && instanceSupportsExtension(
                getInstanceProcAddr_, VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        if (surfaceMaintenance1Enabled_) {
            const auto addIfMissing = [&](const char* name) {
                const auto present = std::any_of(requiredExtensions.begin(),
                    requiredExtensions.end(),
                    [name](const auto& extension) {
                        return extension == name;
                    });
                if (!present)
                    extensionNames.push_back(name);
            };
            addIfMissing(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
            addIfMissing(VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        }

#if ARP_VULKAN_VALIDATION_ENABLED
        // Query available instance layers and enable validation if present.
        const auto validationPolicy = VulkanValidationPolicy::resolve(
#ifdef NDEBUG
            false,
#else
            true,
#endif
            std::getenv("ARP_VULKAN_VALIDATION"));
        if (!validationPolicy.valid) {
            diagnostics = "ARP-VULKAN-VALIDATION-POLICY-INVALID: expected 0 or 1";
            return false;
        }
        core::pmr::Vector<const char*> enabledLayers{&core::defaultCpuMemoryResource()};
        bool validationAvailable = false;
        if (validationPolicy.enabled) {
            auto* enumerateLayers = functionPointerFrom<PFN_vkEnumerateInstanceLayerProperties>(
                getInstanceProcAddr_(nullptr, "vkEnumerateInstanceLayerProperties"));
            if (enumerateLayers) {
                std::uint32_t layerCount = 0;
                enumerateLayers(&layerCount, nullptr);
                if (layerCount > 0) {
                    core::pmr::Vector<VkLayerProperties> layers(
                        layerCount, &core::defaultCpuMemoryResource());
                    enumerateLayers(&layerCount, layers.data());
                    for (const auto& layer : layers) {
                        if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
                            validationAvailable = true;
                            break;
                        }
                    }
                }
            }
        }
        if (validationPolicy.explicitlyRequired && !validationAvailable) {
            diagnostics = "ARP-VULKAN-VALIDATION-LAYER-UNAVAILABLE";
            return false;
        }
        if (validationAvailable) {
            enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
            extensionNames.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        std::fprintf(stderr,
            "[vulkan-validation] requested=%u enabled=%u explicit=%u\n",
            unsigned(validationPolicy.enabled),
            unsigned(validationAvailable),
            unsigned(validationPolicy.explicitlyRequired));
#endif

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName =
            applicationName.empty() ? "AnimeRenderPipeline" : applicationName.c_str();
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "AnimeRenderPipeline";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo = &appInfo;
        instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensionNames.size());
        instanceInfo.ppEnabledExtensionNames = extensionNames.data();
#if ARP_VULKAN_VALIDATION_ENABLED
        instanceInfo.enabledLayerCount = static_cast<std::uint32_t>(enabledLayers.size());
        instanceInfo.ppEnabledLayerNames = enabledLayers.data();
#endif

        const auto instanceCreated = createInstance(&instanceInfo, nullptr, &instance_);
        if (instanceCreated != VK_SUCCESS) {
            diagnostics = vkFailure("vkCreateInstance", instanceCreated);
            return false;
        }

#if ARP_VULKAN_VALIDATION_ENABLED
        if (validationAvailable) {
            auto* createDebugMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                getInstanceProcAddr_(instance_, "vkCreateDebugUtilsMessengerEXT"));
            if (createDebugMessenger) {
                VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
                debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
                debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                    | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                    | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                    | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                debugInfo.pfnUserCallback = vulkanDebugCallback;
                createDebugMessenger(instance_, &debugInfo, nullptr, &debugMessenger_);
            }
        }
#endif
        destroyInstance_ = functionPointerFrom<PFN_vkDestroyInstance>(
            getInstanceProcAddr_(instance_, "vkDestroyInstance"));
        if (!destroyInstance_) {
            diagnostics = "Vulkan instance missing vkDestroyInstance";
            return false;
        }

        surface_ = std::make_unique<PlatformVulkanSurface>(
            platform.createVulkanSurface(window, instance_));
        if (!surface_ || !surface_->valid()) {
            diagnostics = "WindowSystem failed to create a Vulkan surface for the window";
            return false;
        }

        auto* enumeratePhysicalDevices = functionPointerFrom<PFN_vkEnumeratePhysicalDevices>(
            getInstanceProcAddr_(instance_, "vkEnumeratePhysicalDevices"));
        if (!enumeratePhysicalDevices) {
            diagnostics = "Vulkan instance missing vkEnumeratePhysicalDevices";
            return false;
        }

        std::uint32_t physicalDeviceCount = 0;
        if (enumeratePhysicalDevices(instance_, &physicalDeviceCount, nullptr) != VK_SUCCESS
            || physicalDeviceCount == 0) {
            diagnostics = "Vulkan surface runtime found no physical devices";
            return false;
        }
        std::pmr::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
        if (enumeratePhysicalDevices(instance_, &physicalDeviceCount, physicalDevices.data())
            != VK_SUCCESS) {
            diagnostics = "Vulkan surface runtime failed to enumerate physical devices";
            return false;
        }

        for (const auto candidate : physicalDevices) {
            if (!deviceSupportsExtension(
                    instance_, candidate, getInstanceProcAddr_, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
                continue;
            }
            const auto queues =
                selectQueueFamilies(instance_, candidate, surface_->handle(), getInstanceProcAddr_);
            if (!queues.success) {
                diagnostics = queues.diagnostics;
                continue;
            }
            if (createLogicalDevice(candidate, queues, diagnostics)) {
                physicalDevice_ = candidate;
                graphicsQueueFamilyIndex_ = queues.graphics;
                presentQueueFamilyIndex_ = queues.present;
                queueFamilyCount_ = queues.familyCount;
                memorySupport_ = queryMemorySupport(instance_, candidate, getInstanceProcAddr_);
                queryDeviceLimits(candidate);
                return true;
            }
        }

        if (diagnostics.empty()) {
            diagnostics =
                "Vulkan surface runtime found no physical device with VK_KHR_swapchain support";
        }
        return false;
    }

    bool createLogicalDevice(VkPhysicalDevice physicalDevice,
        const VulkanQueueFamilySelection& queues,
        std::pmr::string& diagnostics)
    {
        auto* createDevice = functionPointerFrom<PFN_vkCreateDevice>(
            getInstanceProcAddr_(instance_, "vkCreateDevice"));
        if (!createDevice) {
            diagnostics = "Vulkan instance missing vkCreateDevice";
            return false;
        }

        const float queuePriority = 1.0f;
        std::array<std::uint32_t, 2> uniqueFamilies{
            queues.graphics,
            queues.present,
        };
        std::pmr::vector<VkDeviceQueueCreateInfo> queueInfos;
        for (std::uint32_t index = 0; index < uniqueFamilies.size(); ++index) {
            if (index == 1 && uniqueFamilies[index] == uniqueFamilies[0]) {
                continue;
            }
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = uniqueFamilies[index];
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &queuePriority;
            queueInfos.push_back(queueInfo);
        }

        const bool maintenanceExtensionAvailable = surfaceMaintenance1Enabled_
            && deviceSupportsExtension(instance_,
                physicalDevice,
                getInstanceProcAddr_,
                VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
        const char* deviceExtensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
        };
        auto* getPhysicalDeviceFeatures2 = functionPointerFrom<PFN_vkGetPhysicalDeviceFeatures2>(
            getInstanceProcAddr_(instance_, "vkGetPhysicalDeviceFeatures2"));
        if (!getPhysicalDeviceFeatures2) {
            diagnostics = "Vulkan 1.2 runtime missing vkGetPhysicalDeviceFeatures2";
            return false;
        }
        VkPhysicalDeviceVulkan11Features supportedVulkan11{};
        supportedVulkan11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        VkPhysicalDeviceFeatures2 supportedFeatures{};
        supportedFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        supportedFeatures.pNext = &supportedVulkan11;
        getPhysicalDeviceFeatures2(physicalDevice, &supportedFeatures);
        if (supportedVulkan11.shaderDrawParameters != VK_TRUE) {
            diagnostics = "Vulkan device lacks required shaderDrawParameters support";
            return false;
        }

        VkPhysicalDeviceVulkan11Features enabledVulkan11{};
        enabledVulkan11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        enabledVulkan11.shaderDrawParameters = VK_TRUE;
        VkPhysicalDeviceFeatures2 enabledFeatures{};
        enabledFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        enabledFeatures.pNext = &enabledVulkan11;
        enabledFeatures.features.shaderStorageImageReadWithoutFormat =
            supportedFeatures.features.shaderStorageImageReadWithoutFormat;
        enabledFeatures.features.shaderStorageImageWriteWithoutFormat =
            supportedFeatures.features.shaderStorageImageWriteWithoutFormat;
        // samplerAnisotropy is a Vulkan 1.1 mandatory feature, but mirror the
        // supported value so devices that genuinely lack it still create the
        // device without validation errors. Required for anisotropic texture
        // filtering in the PBR / terrain material sampler.
        enabledFeatures.features.samplerAnisotropy = supportedFeatures.features.samplerAnisotropy;
        // SPIR-V modules declare the Int16 capability when their sources use
        // short types; mirror the supported core feature so module creation
        // matches the enabled feature set (Float16 lives on the 1.2 chain).
        enabledFeatures.features.shaderInt16 = supportedFeatures.features.shaderInt16;
        enabledFeatures.features.drawIndirectFirstInstance =
            supportedFeatures.features.drawIndirectFirstInstance;
        // depthBiasClamp is an optional Vulkan physical device feature
        // (VUID-00754): a pipeline may set a non-zero VkPipelineRasterization
        // StateCreateInfo::depthBiasClamp only if the device enabled it. The
        // shadow / depth-bias pipelines rely on it. Mirror the supported
        // value, record the query result so the RHI capability contract can
        // fail closed on devices that lack it, and let pipeline state
        // lowering surface the diagnostic instead of letting the native path
        // silently emit an invalid pipeline.
        depthBiasClampSupported_ = supportedFeatures.features.depthBiasClamp == VK_TRUE;
        enabledFeatures.features.depthBiasClamp = supportedFeatures.features.depthBiasClamp;
        // Per-format storage-texture probe for Rg16Float. VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT is a per-format
        // property in Vulkan core - it is not a device-level feature and may
        // vary across implementations, so probe it explicitly via
        // vkGetPhysicalDeviceFormatProperties rather than inferring from the
        // coarse StorageTextureBinding capability (AGENTS.md:31). Virtually
        // all desktop Vulkan GPUs support R16G16_SFLOAT storage, but
        // mobile/embedded may not. DDGI textures use VK_IMAGE_TILING_OPTIMAL,
        // so optimalTilingFeatures is the authoritative field.
        VkFormatProperties rg16FloatFormatProperties{};
        vkGetPhysicalDeviceFormatProperties(
            physicalDevice, VK_FORMAT_R16G16_SFLOAT, &rg16FloatFormatProperties);
        rg16FloatStorageTextureSupported_ =
            (rg16FloatFormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
            != 0;
        VkFormatProperties rgba16FloatFormatProperties{};
        vkGetPhysicalDeviceFormatProperties(
            physicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT, &rgba16FloatFormatProperties);
        rgba16FloatStorageTextureSupported_ = (rgba16FloatFormatProperties.optimalTilingFeatures
                                                  & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
            != 0;
        // Probe drawIndirectCount from the Vulkan 1.2 feature chain. This is
        // core in Vulkan 1.2 (promoted from VK_KHR_draw_indirect_count) and
        // required by P5 drawIndexedIndirectCount (GPU-provided draw count).
        // When unsupported, the WoCC layer falls back to drawIndexedIndirect
        // with a CPU-known max draw count.
        VkPhysicalDeviceVulkan12Features supportedVulkan12{};
        supportedVulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR supportedMaintenance{};
        supportedMaintenance.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR;
        supportedVulkan12.pNext = maintenanceExtensionAvailable ? &supportedMaintenance : nullptr;
        supportedVulkan11.pNext = &supportedVulkan12;
        getPhysicalDeviceFeatures2(physicalDevice, &supportedFeatures);
        const bool maintenanceEnabled =
            maintenanceExtensionAvailable && supportedMaintenance.swapchainMaintenance1 == VK_TRUE;
        drawIndirectCountSupported_ = supportedVulkan12.drawIndirectCount == VK_TRUE;
        // FrameGraph pass timestamps may begin inside a render pass, where a
        // command-buffer query reset is invalid. Enable the Vulkan 1.2 host
        // reset feature when present so each per-frame timing pool can be
        // reset before command recording starts.
        VkPhysicalDeviceVulkan12Features enabledVulkan12{};
        enabledVulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR enabledMaintenance{};
        enabledMaintenance.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR;
        enabledMaintenance.swapchainMaintenance1 = maintenanceEnabled ? VK_TRUE : VK_FALSE;
        enabledVulkan12.pNext = maintenanceEnabled ? &enabledMaintenance : nullptr;
        enabledVulkan12.hostQueryReset = supportedVulkan12.hostQueryReset;
        // SPIR-V modules declare the Float16 capability whenever their shader
        // sources use half types; Vulkan requires the matching device feature
        // to be enabled at creation even if a module never executes such an
        // op. Mirror the supported value like the optional core features above
        // so the validated module set matches the enabled feature set; devices
        // that genuinely lack support keep it disabled and module creation
        // fails closed there. (Int16 is a core VkPhysicalDeviceFeatures member
        // and is mirrored with the other core features above.)
        enabledVulkan12.shaderFloat16 = supportedVulkan12.shaderFloat16;
        // drawIndirectCountSupported_ is probed above and published through the
        // capability snapshot; advertising the command while the device feature
        // bit stayed disabled would lower P5 draw commands into invalid usage.
        enabledVulkan12.drawIndirectCount = supportedVulkan12.drawIndirectCount;
        enabledVulkan12.timelineSemaphore = supportedVulkan12.timelineSemaphore;
        const auto indexedFeatures = rhi_vulkan::VulkanDescriptorIndexingFeatures::capture(
            supportedFeatures.features, supportedVulkan12);
        indexedFeatures.applyTo(enabledFeatures.features, enabledVulkan12);
        enabledVulkan11.pNext = &enabledVulkan12;
        // Restore the pNext chain for device creation (supportedVulkan11.pNext
        // was temporarily repointed to supportedVulkan12 for the probe).
        supportedVulkan11.pNext = nullptr;
        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
        deviceInfo.pQueueCreateInfos = queueInfos.data();
        deviceInfo.enabledExtensionCount = maintenanceEnabled ? 2u : 1u;
        deviceInfo.ppEnabledExtensionNames = deviceExtensions;
        deviceInfo.pNext = &enabledFeatures;
        deviceInfo.pEnabledFeatures = nullptr;

        VkDevice device = VK_NULL_HANDLE;
        const auto created = createDevice(physicalDevice, &deviceInfo, nullptr, &device);
        if (created != VK_SUCCESS) {
            diagnostics = vkFailure("vkCreateDevice", created);
            return false;
        }

        getDeviceProcAddr_ = functionPointerFrom<PFN_vkGetDeviceProcAddr>(
            getInstanceProcAddr_(instance_, "vkGetDeviceProcAddr"));
        if (!getDeviceProcAddr_) {
            diagnostics = "Vulkan instance missing vkGetDeviceProcAddr";
            return false;
        }

        auto* getDeviceQueue = functionPointerFrom<PFN_vkGetDeviceQueue>(
            getDeviceProcAddr_(device, "vkGetDeviceQueue"));
        destroyDevice_ =
            functionPointerFrom<PFN_vkDestroyDevice>(getDeviceProcAddr_(device, "vkDestroyDevice"));
        deviceWaitIdle_ = functionPointerFrom<PFN_vkDeviceWaitIdle>(
            getDeviceProcAddr_(device, "vkDeviceWaitIdle"));
        if (!getDeviceQueue || !destroyDevice_) {
            diagnostics = "Vulkan logical device missing queue/destruction entrypoints";
            if (destroyDevice_) {
                destroyDevice_(device, nullptr);
            }
            return false;
        }

        logicalDevice_ = device;
        descriptorIndexingEnabled_ = rhi_vulkan::VulkanDescriptorIndexingFeatures::capture(
            enabledFeatures.features, enabledVulkan12);
        indirectFirstInstanceEnabled_ =
            enabledFeatures.features.drawIndirectFirstInstance == VK_TRUE;
        descriptorIndexingEnablementReported_ = true;
        timelineSemaphoreEnabled_ = enabledVulkan12.timelineSemaphore == VK_TRUE;
        swapchainMaintenance1Enabled_ = maintenanceEnabled;
        getDeviceQueue(logicalDevice_, queues.graphics, 0, &graphicsQueue_);
        getDeviceQueue(logicalDevice_, queues.present, 0, &presentQueue_);
        if (!graphicsQueue_ || !presentQueue_) {
            diagnostics = "Vulkan logical device failed to expose graphics/present queues";
            return false;
        }
        return true;
    }

    void queryDeviceLimits(VkPhysicalDevice physicalDevice)
    {
        auto* getProperties = functionPointerFrom<PFN_vkGetPhysicalDeviceProperties>(
            getInstanceProcAddr_(instance_, "vkGetPhysicalDeviceProperties"));
        if (!getProperties) {
            return;
        }
        VkPhysicalDeviceProperties properties{};
        getProperties(physicalDevice, &properties);
        maxPushConstantSizeBytes_ = properties.limits.maxPushConstantsSize;
        timestampQueriesSupported_ = properties.limits.timestampPeriod > 0.0f;
    }

    PFN_vkGetInstanceProcAddr getInstanceProcAddr_ = nullptr;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr_ = nullptr;
    PFN_vkDestroyInstance destroyInstance_ = nullptr;
    PFN_vkDestroyDevice destroyDevice_ = nullptr;
    PFN_vkDeviceWaitIdle deviceWaitIdle_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice logicalDevice_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
#if ARP_VULKAN_VALIDATION_ENABLED
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
#endif
    std::unique_ptr<PlatformVulkanSurface> surface_;
    VulkanMemorySupport memorySupport_;
    std::uint32_t queueFamilyCount_ = 0;
    std::uint32_t graphicsQueueFamilyIndex_ = 0;
    std::uint32_t presentQueueFamilyIndex_ = 0;
    std::uint32_t maxPushConstantSizeBytes_ = 256;
    bool depthBiasClampSupported_ = false;
    bool rg16FloatStorageTextureSupported_ = false;
    bool rgba16FloatStorageTextureSupported_ = false;
    bool drawIndirectCountSupported_ = false;
    bool timestampQueriesSupported_ = false;
    bool descriptorIndexingEnablementReported_ = false;
    rhi_vulkan::VulkanDescriptorIndexingFeatures descriptorIndexingEnabled_;
    bool indirectFirstInstanceEnabled_ = false;
    bool timelineSemaphoreEnabled_ = false;
    bool swapchainMaintenance1Enabled_ = false;
    bool surfaceMaintenance1Enabled_ = false;
};

rhi::semantics::vulkan::VulkanDeviceCapabilitySnapshot makeVulkanCapabilitySnapshot(
    const VulkanSurfaceRuntimeOwner* nativeRuntime)
{
    rhi::semantics::vulkan::VulkanDeviceCapabilitySnapshot snapshot;
    snapshot.queriedFromNativeDevice = nativeRuntime != nullptr;
    snapshot.queriedExtendedFeatures = nativeRuntime != nullptr;
    snapshot.maxPushConstantSizeBytes =
        nativeRuntime ? nativeRuntime->maxPushConstantSizeBytes() : 0;
    // DescriptorTable bind currently rejects with
    // RHI-VULKAN-DESCRIPTOR-TABLE-NATIVE-LOWERING-UNAVAILABLE. A device name,
    // API version or successful creation cannot authorize that missing payload.
    // BindlessCross/1 will promote only queried AND enabled indexing features
    // together with its executable typed-table lowering.
    snapshot.supportsDescriptorIndexing = false;
    snapshot.supportsTimelineSynchronization =
        nativeRuntime && nativeRuntime->timelineSemaphoreEnabled();
    snapshot.supportsSecondaryCommandBuffers = nativeRuntime != nullptr;
    snapshot.queueFamilyCount = nativeRuntime ? nativeRuntime->queueFamilyCount() : 0;
    snapshot.hasDeviceLocalMemory =
        nativeRuntime ? nativeRuntime->memorySupport().deviceLocal : false;
    snapshot.hasHostVisibleMemory =
        nativeRuntime ? nativeRuntime->memorySupport().hostVisible : false;
    // Propagate the queried VkPhysicalDeviceFeatures::depthBiasClamp result
    // (VUID-00754) so pipeline state lowering fails closed without evidence.
    snapshot.supportsDepthBiasClamp =
        nativeRuntime ? nativeRuntime->depthBiasClampSupported() : false;
    // Per-format storage-texture support requires a native device query.
    snapshot.supportsRg16FloatStorageTexture =
        nativeRuntime ? nativeRuntime->rg16FloatStorageTextureSupported() : false;
    snapshot.supportsRgba16FloatStorageTexture =
        nativeRuntime ? nativeRuntime->rgba16FloatStorageTextureSupported() : false;
    // drawIndirectCount probe (Vulkan 1.2 core / VK_KHR_draw_indirect_count).
    snapshot.supportsDrawIndirectCount =
        nativeRuntime ? nativeRuntime->drawIndirectCountSupported() : false;
    snapshot.supportsTimestampQueries =
        nativeRuntime ? nativeRuntime->timestampQueriesSupported() : false;
    return snapshot;
}

arp::core::UniquePtr<rhi::GpuDevice> createRhiVulkanDevice(const std::pmr::string& applicationName,
    const std::shared_ptr<VulkanSurfaceRuntimeOwner>& nativeRuntime,
    std::pmr::string& diagnostics)
{
    if (!nativeRuntime) {
        diagnostics = "RHI-VULKAN-RUNTIME-OWNER-MISSING";
        return nullptr;
    }
    const auto snapshot = makeVulkanCapabilitySnapshot(nativeRuntime.get());

    rhi_vulkan::VulkanRhiRuntimeDesc runtimeDesc;
    runtimeDesc.label = applicationName.empty() ? "RenderSystemRhiVulkanRuntime"
                                                : applicationName + "RhiVulkanRuntime";
    runtimeDesc = nativeRuntime->runtimeDesc(runtimeDesc.label);
    runtimeDesc.nativeOwner = nativeRuntime;
    runtimeDesc.enableSyntheticRuntime = false;

    auto runtime = rhi_vulkan::createVulkanRhiRuntime(runtimeDesc);
    if (!runtime) {
        diagnostics = "Vulkan RHI runtime creation failed";
        return nullptr;
    }

    rhi_vulkan::VulkanRhiDeviceDesc deviceDesc;
    deviceDesc.label = applicationName.empty() ? "RenderSystemRhiVulkanDevice"
                                               : applicationName + "RhiVulkanDevice";
    deviceDesc.capabilityContract =
        rhi::semantics::vulkan::buildVulkanNativeCapabilityContract(snapshot);
    // Scale the logical allocator budget against the GPU's actual device-local
    // VRAM instead of a fixed 768 MB. The fixed budget was tuned for 1024x576
    // render targets and is exhausted at 2560x1440 (borderless fullscreen) by
    // the HDR scene color + depth + bloom/SSAO/post transient targets plus the
    // persistent 2K terrain surface sets, causing mid-frame image allocation
    // failure ("frame graph resource pool acquire failed for texture").
    //
    // We cap at 75% of the device-local heap to leave headroom for the OS /
    // desktop compositor / other applications, and floor at the original 768 MB
    // so low-VRAM devices (or synthetic runtimes with no queryable heap) keep
    // the proven baseline. This is a logical allocator budget, not an eager
    // reservation.
    constexpr std::uint64_t kBaselineBudgetBytes = 768u * 1024u * 1024u;
    const std::uint64_t deviceLocalBytes =
        nativeRuntime ? nativeRuntime->memorySupport().deviceLocalHeapBytes : 0u;
    constexpr std::uint64_t kBudgetFractionNumerator = 3u;
    constexpr std::uint64_t kBudgetFractionDenominator = 4u;
    std::uint64_t scaledBudget = kBaselineBudgetBytes;
    if (deviceLocalBytes > 0u) {
        scaledBudget = (deviceLocalBytes / kBudgetFractionDenominator) * kBudgetFractionNumerator;
        if (scaledBudget < kBaselineBudgetBytes) {
            scaledBudget = kBaselineBudgetBytes;
        }
    }
    deviceDesc.memoryBudgetBytes = scaledBudget;
    deviceDesc.runtime = std::move(runtime);
    deviceDesc.enableVmaAllocator = true;
    deviceDesc.requireNativeResourceAllocation = true;
    deviceDesc.allowNativeHandleExposureToUpperLayers = false;
    deviceDesc.allowBlockingFrameWaits = false;
    deviceDesc.allowPerDrawPipelineCreation = false;
    deviceDesc.allowPerFrameShaderCompilation = false;

    const auto report = rhi_vulkan::describeVulkanRhiDevice(deviceDesc);
    if (!report.success) {
        diagnostics = "Vulkan RHI device contract failed: " + report.diagnostics;
        return nullptr;
    }

    auto device = rhi_vulkan::createVulkanRhiDevice(deviceDesc);
    if (!device) {
        diagnostics = "Vulkan RHI device creation failed";
        return nullptr;
    }
    return device;
}

bool sameFrame(const rhi::AcquiredFrame& lhs, const rhi::AcquiredFrame& rhs)
{
    return lhs.success && rhs.success && lhs.frameSerial == rhs.frameSerial
        && lhs.frameIndex == rhs.frameIndex;
}

rhi::RenderTargetDesc makeBackbufferTarget(
    rhi::GpuTexture& texture, rhi::Extent2D extent, rhi::TextureFormat format)
{
    return {
        "RenderSystemBackbuffer",
        texture.id(),
        extent,
        format,
        rhi::ResourceState::ColorAttachment,
    };
}

renderer::RenderFrameContext makeFrameContext(const rhi::AcquiredFrame& frame,
    rhi::GpuTexture& backbuffer,
    rhi::Extent2D extent,
    rhi::TextureFormat format)
{
    return {
        frame,
        makeBackbufferTarget(backbuffer, extent, format),
        nullptr,
        &backbuffer,
        nullptr,
    };
}

RenderSystemPreparedRendererFramePrepareReport preparedRendererFrameReportFrom(
    const renderer::RendererRhiProductionFramePrepareReport& source, std::uint32_t prepareCount)
{
    RenderSystemPreparedRendererFramePrepareReport report;
    report.success = source.success;
    report.diagnostics = source.diagnostics;
    report.usedRhiGpuDevice = source.usedRhiGpuDevice;
    report.rendererFramePlanAccepted = source.rendererFramePlanAccepted;
    report.rhiResourceCreated = source.rhiResourceCreated;
    report.rhiPipelineCreated = source.rhiPipelineCreated;
    report.rhiDescriptorCreated = source.rhiDescriptorCreated;
    report.rhiDescriptorWritten = source.rhiDescriptorWritten;
    report.shaderPackageValidated = source.shaderPackageValidated;
    report.fullSurfaceViewportConfigured = source.fullSurfaceViewportConfigured;
    report.fullSurfaceScissorConfigured = source.fullSurfaceScissorConfigured;
    report.backendFallbacksHiddenFromUpperLayer = source.backendFallbacksHiddenFromUpperLayer;
    report.nativeHandleExposedToUpperLayer = source.nativeHandleExposedToUpperLayer;
    report.prepareCount = prepareCount;
    return report;
}

RenderSystemSceneRuntimeCustomGraphPrepareReport sceneRuntimePrepareReportFrom(
    const RenderSystemPreparedRendererFramePrepareReport& source)
{
    RenderSystemSceneRuntimeCustomGraphPrepareReport report;
    report.success = source.success;
    report.diagnostics = source.diagnostics;
    report.usedRhiGpuDevice = source.usedRhiGpuDevice;
    report.rendererFramePlanAccepted = source.rendererFramePlanAccepted;
    report.rhiResourceCreated = source.rhiResourceCreated;
    report.rhiPipelineCreated = source.rhiPipelineCreated;
    report.rhiDescriptorCreated = source.rhiDescriptorCreated;
    report.rhiDescriptorWritten = source.rhiDescriptorWritten;
    report.shaderPackageValidated = source.shaderPackageValidated;
    report.fullSurfaceViewportConfigured = source.fullSurfaceViewportConfigured;
    report.fullSurfaceScissorConfigured = source.fullSurfaceScissorConfigured;
    report.backendFallbacksHiddenFromUpperLayer = source.backendFallbacksHiddenFromUpperLayer;
    report.nativeHandleExposedToUpperLayer = source.nativeHandleExposedToUpperLayer;
    report.prepareCount = source.prepareCount;
    return report;
}

std::uint32_t preparedSceneRuntimeDrawCommandCount(
    const renderer::RendererRhiFramePlanDesc& framePlan)
{
    std::uint32_t count = 0;
    for (const auto& drawPlan : framePlan.drawPlans) {
        if (drawPlan.workKind == renderer::RendererRhiWorkKind::Scene) {
            count += drawPlan.drawCommandCount;
        }
    }
    return count;
}

bool preparedRendererFrameMatchesDecision(
    const renderer::RenderFramePreparedSubmitRequirement& requirement,
    const renderer::RendererRhiFramePlanDesc& preparedFramePlan)
{
    return requirement.required
        && requirement.expectedDrawItemCount
        == preparedSceneRuntimeDrawCommandCount(preparedFramePlan)
        && preparedSceneRuntimeDrawCommandCount(requirement.framePlan)
        == preparedSceneRuntimeDrawCommandCount(preparedFramePlan);
}

void applySceneRuntimeSubmitReport(RenderSystemSceneRuntimeCustomGraphFrameReport& target,
    const renderer::RendererRhiProductionFrameSubmitReport& submitted,
    const RenderSystemSceneRuntimeCustomGraphPrepareReport& prepared,
    bool surfaceBackedVulkan)
{
    target.success = submitted.success;
    target.diagnostics = submitted.diagnostics;
    target.renderGraphFrameUsed = true;
    target.sceneRuntimePathSubmitted = target.sceneRuntimePathSubmitted || submitted.success;
    target.surfaceBackedVulkan = surfaceBackedVulkan;
    target.usedRhiGpuDevice = target.usedRhiGpuDevice || submitted.usedRhiGpuDevice;
    target.preparedBeforeSubmit = target.preparedBeforeSubmit || prepared.success;
    target.resourcesPreparedBeforeSubmit =
        target.resourcesPreparedBeforeSubmit || submitted.resourcesPreparedBeforeSubmit;
    target.pipelinePreparedBeforeSubmit =
        target.pipelinePreparedBeforeSubmit || submitted.pipelinePreparedBeforeSubmit;
    target.descriptorsPreparedBeforeSubmit =
        target.descriptorsPreparedBeforeSubmit || submitted.descriptorsPreparedBeforeSubmit;
    target.rhiCommandEncoded = target.rhiCommandEncoded || submitted.rhiCommandEncoded;
    target.rhiQueueSubmitted = target.rhiQueueSubmitted || submitted.rhiQueueSubmitted;
    target.swapchainRenderTargetUsed = true;
    target.proceduralSceneRuntime3DObjectDrawn =
        target.proceduralSceneRuntime3DObjectDrawn || submitted.proceduralSceneRuntime3DObjectDrawn;
    target.proceduralSceneRuntime2DSpriteDrawn =
        target.proceduralSceneRuntime2DSpriteDrawn || submitted.proceduralSceneRuntime2DSpriteDrawn;
    target.proceduralSceneRuntimeUiDrawn =
        target.proceduralSceneRuntimeUiDrawn || submitted.proceduralSceneRuntimeUiDrawn;
    target.resourceAllocatedDuringSubmit =
        target.resourceAllocatedDuringSubmit || submitted.resourceAllocatedDuringSubmit;
    target.pipelineCreatedDuringSubmit =
        target.pipelineCreatedDuringSubmit || submitted.pipelineCreatedDuringSubmit;
    target.backendFallbacksHiddenFromUpperLayer = target.backendFallbacksHiddenFromUpperLayer
        && submitted.backendFallbacksHiddenFromUpperLayer
        && prepared.backendFallbacksHiddenFromUpperLayer;
    target.nativeHandleExposedToUpperLayer = target.nativeHandleExposedToUpperLayer
        || submitted.nativeHandleExposedToUpperLayer || prepared.nativeHandleExposedToUpperLayer;
    target.prepareCount = prepared.prepareCount;
    if (submitted.success) {
        ++target.framesSubmitted;
    }
    target.renderPassCount += submitted.renderPassCount;
    target.drawCommandCount += submitted.drawCommandCount;
}

void applySceneRuntimeVulkanStats(
    RenderSystemSceneRuntimeCustomGraphFrameReport& target, const rhi::GpuDevice& device)
{
    const auto stats = rhi_vulkan::inspectVulkanRhiNativeResourceStats(device);
    target.vulkanNativeStatsCaptured = stats.runtimeConnected;
    target.vulkanNativeRenderPassBeginCount = stats.nativeRenderPassBeginRecordCount;
    target.vulkanNativeIndexedDrawCount = stats.nativeIndexedDrawRecordCount;
    target.vulkanNativeQueueSubmitCount = stats.nativeQueueSubmitCount;
    target.hiddenWindowRenderOutputHashCaptured = stats.hiddenWindowRenderOutputHashCaptured;
    target.deterministicRenderOutputHash = stats.deterministicRenderOutputHash;
    target.nativeHandleExposedToUpperLayer =
        target.nativeHandleExposedToUpperLayer || !stats.nativeHandlesHiddenFromPublicApi;
}

void throwIfFailed(const renderer::RendererResult& result, const char* prefix)
{
    if (result.success) {
        return;
    }
    std::pmr::string diagnostic(prefix);
    diagnostic += result.diagnostics;
    throw std::runtime_error(arp::core::toStdString(diagnostic));
}

} // namespace

rhi_vulkan::VulkanRhiRuntimeDesc VulkanSurfaceRuntimeBootstrap::create(WindowSystem& platform,
    const PlatformWindow& window,
    const core::pmr::String& label,
    core::pmr::String& diagnostics)
{
    auto owner = VulkanSurfaceRuntimeOwner::create(platform, window, label, diagnostics);
    if (!owner)
        return {};
    auto desc = owner->runtimeDesc(label);
    desc.nativeOwner = std::move(owner);
    return desc;
}

struct RenderSystem::Impl {
    struct ParticleRendererConfig {
        core::pmr::String shaderPath{&core::defaultCpuMemoryResource()};
        core::pmr::String trailBeamShaderPath{&core::defaultCpuMemoryResource()};
        core::pmr::String meshParticleShaderPath{&core::defaultCpuMemoryResource()};
        std::uint32_t maxParticles = 0u;

        bool valid() const noexcept { return !shaderPath.empty() && maxParticles > 0u; }
    };

    std::shared_ptr<VulkanSurfaceRuntimeOwner> nativeVulkanRuntime;
    arp::core::UniquePtr<rhi::GpuDevice> device;
    // RenderSystem owns the transient pool so short-lived per-frame graphs
    // reuse L2 resources without extending device lifetime globally.
    std::shared_ptr<render_graph::FrameGraphResourcePool> frameGraphResourcePool;
    arp::core::UniquePtr<rhi::Swapchain> swapchain;
    renderer::RenderFrameDirector frameDirector;
    std::unique_ptr<renderer::RenderPipeline> pipeline;
    arp::core::UniquePtr<renderer::RenderPipeline> corePipeline;
    renderer::RenderPipeline* activePipeline = nullptr;
    arp::core::UniquePtr<renderer::RendererRhiProductionFramePath> preparedRendererFramePath;
    renderer::RendererRhiFramePlanDesc preparedRendererFramePlan;
    renderer::RendererRhiDrawPlanBridgeDesc preparedRendererFramePlanBridge{2u, 8u, 4u, 2u};
    std::uint32_t preparedRendererFrameMaxDrawCommandsPerPass = 1024u;
    std::pmr::string preparedRendererFrameShaderArtifactRoot;
    RenderSystemPreparedRendererFramePrepareReport preparedRendererFramePrepareReport;
    std::uint32_t preparedRendererFramePrepareCount = 0;
    RenderSystemSceneRuntimeCustomGraphPrepareReport sceneRuntimeCustomGraphPrepareReport;
    RenderSystemSceneRuntimeCustomGraphFrameReport lastSceneRuntimeCustomGraphFrameReport;
    std::uint32_t sceneRuntimeCustomGraphPrepareCount = 0;
    std::uint64_t sceneRuntimeCustomGraphPendingPresentSerial = 0;
    bool sceneRuntimeCustomGraphPendingPresent = false;
    std::shared_ptr<const rhi::GpuFence> currentFramePresentationCompletion;
    core::UniquePtr<renderer::GuiRenderer> guiRenderer;
    core::UniquePtr<renderer::ParticleRenderer> particleRenderer;
    ParticleRendererConfig particleRendererConfig;
    rhi::AcquiredFrame currentFrame;
    FrameInputAdmission inputAdmission;
    rhi::Extent2D extent{1280u, 720u};
    rhi::TextureFormat colorFormat = rhi::TextureFormat::Bgra8Unorm;
    rhi::SwapchainDesc swapchainDesc;
    rhi::Backend backend = rhi::Backend::Null;
    bool surfaceBackedPresentation = false;
    RenderSystemRhiFrameLoopDryRunDesc rhiDryRunDesc;
    RenderSystemRhiFrameLoopDryRunReport lastRhiDryRunReport;
    RenderSystemRhiFrameLoopDryRunSmokeReport lastRhiDryRunSmokeReport;
    RenderSystemRhiStartupProbeReport lastRhiStartupProbeReport;
    RenderSystemRhiStartupGateReport lastRhiStartupGateReport;
    RenderSystemRhiStartupProbeScheduleReport lastRhiStartupProbeScheduleReport;
    RenderSystemRhiStartupProbeBatchReport lastRhiStartupProbeBatchReport;
    RenderSystemRhiStartupProbeRunReport lastRhiStartupProbeRunReport;
    RenderSystemRhiStartupTelemetryReport lastRhiStartupTelemetryReport;
    RenderSystemRhiStartupReadinessReport lastRhiStartupReadinessReport;
    RenderSystemRhiFormalRefactorStartGateReport lastRhiFormalRefactorStartGateReport;
    std::pmr::string diagnostics;
    bool valid = false;
};

RenderSystem::RenderSystem() = default;

RenderSystem::RenderSystem(core::UniquePtr<Impl> impl) : impl_(std::move(impl))
{}

RenderSystem::RenderSystem(RenderSystem&&) noexcept = default;

RenderSystem& RenderSystem::operator=(RenderSystem&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    // OUT_OF_DATE recovery replaces a live RenderSystem through this operator.
    // A default unique_ptr move assignment would delete the prior Impl without
    // invoking ~RenderSystem(), bypassing its device-idle lifetime barrier.
    if (impl_ && impl_->nativeVulkanRuntime) {
        impl_->nativeVulkanRuntime->waitIdle();
    }
    impl_ = std::move(other.impl_);
    return *this;
}

RenderSystem::~RenderSystem()
{
    // Backend lifecycle contract: all renderer-owned descriptor resources,
    // images, samplers and swapchain objects must outlive submitted work.
    // Impl member destruction releases them before the device wrapper, so the
    // native device must be idle at this boundary rather than relying on a
    // later device destructor.
    waitGpuIdle();
}

void RenderSystem::waitGpuIdle() const
{
    if (impl_ && impl_->nativeVulkanRuntime) {
        impl_->nativeVulkanRuntime->waitIdle();
    }
}

rhi::semantics::ShutdownDrainReport RenderSystem::drainGpuForShutdown(
    const rhi::semantics::ShutdownDrainRequest& request) const
{
    if (impl_ && impl_->device)
        return impl_->device->drainForShutdown(request);
    const auto now = std::chrono::steady_clock::now();
    rhi::semantics::ShutdownDrainReport report;
    report.backend = impl_ ? impl_->backend : rhi::Backend::Null;
    report.diagnosticId = "ARP-PLATFORM-SHUTDOWN-DRAIN-RENDER-SYSTEM-INVALID";
    rhi::semantics::finalizeShutdownDrainTiming(report, now, request, now);
    return report;
}

bool RenderSystem::valid() const
{
    return impl_ && impl_->valid;
}

const std::pmr::string& RenderSystem::diagnostics() const
{
    static const std::pmr::string empty;
    return impl_ ? impl_->diagnostics : empty;
}

RuntimeCapabilities RenderSystem::capabilities() const
{
    RuntimeCapabilities capabilities;
    capabilities.backend = backend();
    capabilities.surfaceBacked = valid() && impl_ && impl_->surfaceBackedPresentation;
    capabilities.canRender = valid();
    if (impl_) {
        capabilities.rhiFrameLoopDryRun = summarizeRenderSystemRhiFrameLoopDryRunCapability(
            impl_->rhiDryRunDesc, impl_->lastRhiDryRunReport);
        capabilities.rhiFrameLoopDryRunSmoke =
            summarizeRenderSystemRhiFrameLoopDryRunSmokeCapability(impl_->lastRhiDryRunSmokeReport);
        capabilities.rhiStartupProbe =
            summarizeRenderSystemRhiStartupProbeCapability(impl_->lastRhiStartupProbeReport);
        capabilities.rhiStartupProbeSchedule = impl_->lastRhiStartupProbeScheduleReport;
        capabilities.rhiStartupProbeBatch = impl_->lastRhiStartupProbeBatchReport;
        capabilities.rhiStartupProbeRun = impl_->lastRhiStartupProbeRunReport;
        capabilities.rhiStartupTelemetry = impl_->lastRhiStartupTelemetryReport;
        capabilities.rhiStartupReadiness = impl_->lastRhiStartupReadinessReport;
        capabilities.rhiFormalRefactorStartGate = impl_->lastRhiFormalRefactorStartGateReport;
    }
    capabilities.rhiStartupGate = evaluateRenderSystemRhiStartupGate(capabilities.rhiStartupProbe);
    capabilities.diagnostics = diagnostics();
    return capabilities;
}

rhi::Backend RenderSystem::backend() const
{
    return valid() ? impl_->backend : rhi::Backend::Null;
}

rhi::GpuDevice* RenderSystem::gpuDevice() noexcept
{
    return impl_ ? impl_->device.get() : nullptr;
}

const rhi::GpuDevice* RenderSystem::gpuDevice() const noexcept
{
    return impl_ ? impl_->device.get() : nullptr;
}

rhi::Swapchain* RenderSystem::swapchain() noexcept
{
    return impl_ ? impl_->swapchain.get() : nullptr;
}

rhi::Extent2D RenderSystem::extent() const
{
    return valid() ? impl_->extent : rhi::Extent2D{};
}

rhi::TextureFormat RenderSystem::colorFormat() const
{
    return valid() ? impl_->colorFormat : rhi::TextureFormat::Unknown;
}

rhi::MemoryStats RenderSystem::memoryStats() const
{
    return valid() && impl_->device ? impl_->device->memoryStats() : rhi::MemoryStats{};
}

rhi::GpuTimingSample RenderSystem::latestGpuTiming() const
{
    return valid() && impl_->device ? impl_->device->latestGpuTiming() : rhi::GpuTimingSample{};
}

rhi::semantics::UploadStagingReadiness RenderSystem::uploadStagingReadiness() const
{
    return valid() && impl_->device ? impl_->device->uploadStagingReadiness()
                                    : rhi::semantics::uploadStagingReadinessUnavailable();
}

RenderSystemResult RenderSystem::resize(rhi::Extent2D extent)
{
    if (!valid() || !impl_->swapchain) {
        return renderSystemResult(false, "render system is not valid");
    }
    if (impl_->currentFrame.success) {
        return renderSystemResult(false, "cannot resize RHI swapchain while a frame is active");
    }
    const auto cancelled = impl_->inputAdmission.cancel();
    if (!cancelled.success)
        return renderSystemResult(false, cancelled.diagnostics);
    const auto resized = impl_->swapchain->resize(extent);
    if (!resized.success) {
        return renderSystemResult(false, "Vulkan RHI resize failed: " + resized.diagnostics);
    }
    impl_->extent = resized.extent;
    if (impl_->activePipeline) {
        const auto pipelineResized = impl_->activePipeline->resize(resized.extent);
        if (!pipelineResized.success) {
            return renderSystemResult(
                false, "Vulkan render pipeline resize failed: " + pipelineResized.diagnostics);
        }
    }
    if (impl_->guiRenderer) {
        const auto guiResized = impl_->guiRenderer->onResize(resized.extent);
        if (!guiResized.success) {
            return renderSystemResult(
                false, "GUI renderer resize failed: " + guiResized.diagnostics);
        }
    }
    if (impl_->particleRenderer && impl_->particleRendererConfig.valid()) {
        auto particleRenderer = core::makeUnique<renderer::ParticleRenderer>(*impl_->device);
        const auto particleResized = particleRenderer->create(renderer::ParticleRendererDesc{
            impl_->particleRendererConfig.shaderPath,
            impl_->colorFormat,
            impl_->extent,
            impl_->particleRendererConfig.maxParticles,
            impl_->particleRendererConfig.trailBeamShaderPath,
            impl_->particleRendererConfig.meshParticleShaderPath,
        });
        if (!particleResized.success) {
            return renderSystemResult(
                false, "particle renderer resize failed: " + particleResized.diagnostics);
        }
        impl_->particleRenderer = std::move(particleRenderer);
    }
    impl_->preparedRendererFramePath.reset();
    impl_->preparedRendererFramePrepareReport = {};
    impl_->preparedRendererFramePlan = {};
    impl_->sceneRuntimeCustomGraphPrepareReport = {};
    impl_->lastSceneRuntimeCustomGraphFrameReport = {};
    impl_->sceneRuntimeCustomGraphPendingPresent = false;
    return renderSystemResult(true, "");
}

rhi::SwapchainPresentationStatus RenderSystem::presentationStatus() const
{
    return valid() && impl_->swapchain ? impl_->swapchain->presentationStatus()
                                       : rhi::SwapchainPresentationStatus{};
}

RenderSystemResult RenderSystem::setPresentationMode(const rhi::PresentationMode presentationMode)
{
    if (!valid() || !impl_->swapchain) {
        return renderSystemResult(false, "render system is not valid");
    }
    if (impl_->currentFrame.success) {
        return renderSystemResult(false, "cannot change presentation mode while a frame is active");
    }
    if (impl_->swapchainDesc.presentationMode == presentationMode) {
        return renderSystemResult(true, "");
    }
    const auto cancelled = impl_->inputAdmission.cancel();
    if (!cancelled.success)
        return renderSystemResult(false, cancelled.diagnostics);

    auto requested = impl_->swapchainDesc;
    requested.presentationMode = presentationMode;
    const auto result = impl_->swapchain->reconfigure({std::move(requested)});
    if (!result.success) {
        return renderSystemResult(
            false, "RHI swapchain presentation reconfigure failed: " + result.diagnostics);
    }
    impl_->swapchainDesc.presentationMode = presentationMode;
    impl_->extent = result.extent;
    return renderSystemResult(true, "");
}

RenderSystemResult RenderSystem::uploadMesh(
    const renderer::MeshAsset& mesh, renderer::MeshGpuBuffer& output)
{
    if (!valid() || !impl_->device) {
        return renderSystemResult(false, "render system is not valid");
    }
    auto uploaded = renderer::uploadOrUpdateMeshToRhi(*impl_->device, mesh, output);
    if (!uploaded.success) {
        return renderSystemResult(false, std::move(uploaded.diagnostics));
    }
    return renderSystemResult(true, "");
}

RenderSystemResult RenderSystem::setRenderPipeline(
    std::unique_ptr<renderer::RenderPipeline>&& pipeline)
{
    if (!valid() || !impl_->device) {
        return renderSystemResult(false, "render system is not valid");
    }
    if (!pipeline) {
        return renderSystemResult(false, "render pipeline is null");
    }

    const auto cancelled = impl_->inputAdmission.cancel();
    if (!cancelled.success)
        return renderSystemResult(false, cancelled.diagnostics);

    const auto created = pipeline->create(renderer::RenderPipelineCreateContext{
        *impl_->device,
        impl_->colorFormat,
        rhi::TextureFormat::D32Float,
        impl_->extent,
    });
    if (!created.success) {
        return renderSystemResult(false, created.diagnostics);
    }
    impl_->pipeline = std::move(pipeline);
    impl_->corePipeline.reset();
    impl_->activePipeline = impl_->pipeline.get();
    impl_->preparedRendererFramePath.reset();
    impl_->preparedRendererFramePrepareReport = {};
    impl_->preparedRendererFramePlan = {};
    impl_->sceneRuntimeCustomGraphPrepareReport = {};
    impl_->lastSceneRuntimeCustomGraphFrameReport = {};
    impl_->sceneRuntimeCustomGraphPendingPresent = false;
    return renderSystemResult(true, "");
}

RenderSystemResult RenderSystem::setRenderPipeline(
    arp::core::UniquePtr<renderer::RenderPipeline>&& pipeline)
{
    if (!valid() || !impl_->device) {
        return renderSystemResult(false, "render system is not valid");
    }
    if (!pipeline) {
        return renderSystemResult(false, "render pipeline is null");
    }

    const auto cancelled = impl_->inputAdmission.cancel();
    if (!cancelled.success)
        return renderSystemResult(false, cancelled.diagnostics);

    const auto created = pipeline->create(renderer::RenderPipelineCreateContext{
        *impl_->device,
        impl_->colorFormat,
        rhi::TextureFormat::D32Float,
        impl_->extent,
    });
    if (!created.success) {
        return renderSystemResult(false, created.diagnostics);
    }
    impl_->corePipeline = std::move(pipeline);
    impl_->pipeline.reset();
    impl_->activePipeline = impl_->corePipeline.get();
    impl_->preparedRendererFramePath.reset();
    impl_->preparedRendererFramePrepareReport = {};
    impl_->preparedRendererFramePlan = {};
    impl_->sceneRuntimeCustomGraphPrepareReport = {};
    impl_->lastSceneRuntimeCustomGraphFrameReport = {};
    impl_->sceneRuntimeCustomGraphPendingPresent = false;
    return renderSystemResult(true, "");
}

RenderSystemResult RenderSystem::createParticleRenderer(const std::pmr::string& shaderPath,
    std::uint32_t maxParticles,
    const std::pmr::string& trailBeamShaderPath,
    const std::pmr::string& meshParticleShaderPath)
{
    if (!valid() || !impl_->device) {
        return renderSystemResult(false, "render system is not valid");
    }
    auto particleRenderer = core::makeUnique<renderer::ParticleRenderer>(*impl_->device);
    const auto created = particleRenderer->create(renderer::ParticleRendererDesc{
        shaderPath,
        impl_->colorFormat,
        impl_->extent,
        maxParticles,
        trailBeamShaderPath,
        meshParticleShaderPath,
    });
    if (!created.success) {
        return renderSystemResult(false, created.diagnostics);
    }
    impl_->particleRendererConfig.shaderPath = shaderPath;
    impl_->particleRendererConfig.trailBeamShaderPath = trailBeamShaderPath;
    impl_->particleRendererConfig.meshParticleShaderPath = meshParticleShaderPath;
    impl_->particleRendererConfig.maxParticles = maxParticles;
    impl_->particleRenderer = std::move(particleRenderer);
    return renderSystemResult(true, "");
}

RenderSystemResult RenderSystem::createGuiRenderer(const std::pmr::string& shaderPath)
{
    return createGuiRenderer(renderer::GuiRendererDesc{
        shaderPath,
        impl_ ? impl_->colorFormat : rhi::TextureFormat::Unknown,
        impl_ ? impl_->extent : rhi::Extent2D{},
    });
}

RenderSystemResult RenderSystem::createGuiRenderer(const renderer::GuiRendererDesc& desc)
{
    if (!valid() || !impl_->device) {
        return renderSystemResult(false, "render system is not valid");
    }
    auto guiRenderer = core::makeUnique<renderer::GuiRenderer>(*impl_->device);
    auto rendererDesc = desc;
    rendererDesc.colorFormat = impl_->colorFormat;
    rendererDesc.extent = impl_->extent;
    const auto created = guiRenderer->create(rendererDesc);
    if (!created.success) {
        return renderSystemResult(false, created.diagnostics);
    }
    impl_->guiRenderer = std::move(guiRenderer);
    return renderSystemResult(true, "");
}

RenderSystemResult RenderSystem::updateGuiAtlasTexture(const renderer::GuiAtlasTextureDesc& atlas,
    const arp::core::pmr::Vector<arp::renderer::ui::GlyphAtlasUploadSchedule>& uploads)
{
    if (!valid() || !impl_->guiRenderer) {
        return renderSystemResult(false, "render system has no GUI renderer");
    }
    const auto updated = impl_->guiRenderer->updateAtlasTexture(atlas, uploads);
    return renderSystemResult(updated.success, updated.diagnostics.c_str());
}

renderer::UiPassTelemetry RenderSystem::guiPassTelemetry() const
{
    return impl_ && impl_->guiRenderer ? impl_->guiRenderer->passTelemetry()
                                       : renderer::UiPassTelemetry{};
}

rhi::AcquiredFrame RenderSystem::acquireFrame()
{
    if (!valid() || !impl_->swapchain) {
        return rhi::makeAcquireFailureResult("render system is not valid", 0u, 0u);
    }
    if (impl_->currentFrame.success)
        return rhi::makeAcquireFailureResult("PLATFORM-FRAME-ALREADY-ACQUIRED",
            impl_->currentFrame.frameSerial,
            impl_->currentFrame.frameIndex);
    auto frame = impl_->inputAdmission.acquire(*impl_->swapchain, [this](std::uint32_t slot) {
        return !impl_->activePipeline || impl_->activePipeline->frameInputsReady(slot);
    });
    if (frame.success) {
        impl_->currentFrame = frame;
        impl_->currentFramePresentationCompletion.reset();
    }
    return frame;
}

renderer::RendererResult RenderSystem::renderGraphFrame(
    const rhi::AcquiredFrame& frame, const renderer::CustomRenderFrameWorkload& workload)
{
    if (!valid()) {
        return rendererResult(false, "render system is not valid");
    }
    if (!sameFrame(frame, impl_->currentFrame)) {
        return rendererResult(false, "frame is not drawable");
    }
    impl_->currentFramePresentationCompletion.reset();
    AcquiredFrameDiscardGuard frameDiscardGuard(*impl_->swapchain, impl_->currentFrame, frame);
    if (frame.frameSerial != workload.frameSerial()) {
        return rendererResult(false, "frame serial does not match CustomRenderFrameWorkload");
    }

    const auto validation = workload.validate();
    if (!validation.success) {
        return rendererResult(
            false, "invalid CustomRenderFrameWorkload: " + validation.diagnostics);
    }

    if (impl_->activePipeline == nullptr) {
        return rendererResult(false, "render pipeline is not ready");
    }

    if (impl_->guiRenderer) {
        impl_->guiRenderer->beginFrameTelemetry();
    }

    auto acquiredImage = impl_->swapchain->acquiredImage(frame);
    if (!acquiredImage.success || !acquiredImage.texture) {
        return rendererResult(
            false, "Vulkan RHI acquired swapchain image failed: " + acquiredImage.diagnostics);
    }
    const rhi::RhiFrameContractValidator frameContract({impl_->swapchain->maxFramesInFlight()});
    const auto imageContract = frameContract.validateSwapchainImage(acquiredImage.ref);
    if (!imageContract.success) {
        return rendererResult(false,
            "Vulkan RHI acquired swapchain image contract failed: " + imageContract.diagnostics);
    }

    render_graph::FrameGraph graph(*impl_->device, impl_->frameGraphResourcePool);
    const auto backbuffer =
        graph.importSwapchainTexture(std::move(acquiredImage.texture), acquiredImage.ref);
    rhi::GpuTexture* backbufferTexture = nullptr;

    renderer::RenderPipelineFrameFeatures frameFeatures;
    renderer::RenderFrameRequest pipelineRequest;
    pipelineRequest.view.extent = impl_->extent;
    pipelineRequest.view.colorFormat = impl_->colorFormat;
    pipelineRequest.view.frameIndex = frame.frameIndex;
    pipelineRequest.pipelinePayload =
        workload.scene().enabled() ? workload.scene().payload().object : nullptr;
    pipelineRequest.pipelinePayloadContractName = workload.scenePayloadContractName();
    pipelineRequest.pipelinePayloadContractVersion = workload.scenePayloadContractVersion();

    if (workload.particles().enabled()) {
        const auto* particlePayload =
            renderer::customParticleGraphPayloadFrom(workload.particles());
        if (!particlePayload) {
            return rendererResult(false, "Custom graph particle payload contract is invalid");
        }
        if (!impl_->particleRenderer) {
            return rendererResult(false, "particle renderer is not ready");
        }
        frameFeatures.particlesWithColor =
            [&](render_graph::RenderContext& graphContext,
                const renderer::RenderGraphColorTargetContext& particleTarget) {
                if (!particleTarget.valid()) {
                    throw std::runtime_error("Custom graph particle color target is invalid");
                }
                const renderer::RenderFrameContext frameContext{
                    frame,
                    particleTarget.colorTarget,
                    nullptr,
                    particleTarget.colorTexture,
                    nullptr,
                };
                renderer::ParticleRenderer::ParticleTextureBinding textureBinding;
                const renderer::ParticleRenderer::ParticleTextureBinding* textureBindingPtr =
                    nullptr;
                if (particlePayload->textureBinding.valid()) {
                    textureBinding.texture = particlePayload->textureBinding.texture;
                    textureBinding.textureResource =
                        particlePayload->textureBinding.textureResource;
                    textureBinding.resourceId = particlePayload->textureBinding.resourceId;
                    textureBinding.explicitUpload = particlePayload->textureBinding.explicitUpload;
                    textureBindingPtr = &textureBinding;
                }
                throwIfFailed(impl_->particleRenderer->recordPass(graphContext,
                                  frameContext,
                                  particlePayload->particleInstances(),
                                  *particlePayload->scene,
                                  true,
                                  textureBindingPtr),
                    "Custom graph particle pass failed: ");
            };
    }

    if (workload.gui().enabled()) {
        const auto* guiPayload = renderer::customGuiGraphPayloadFrom(workload.gui());
        const auto* uiPayload = renderer::customUiGraphPayloadFrom(workload.gui());
        if (!guiPayload && !uiPayload) {
            return rendererResult(false, "Custom graph GUI payload contract is invalid");
        }
        if (!impl_->guiRenderer) {
            return rendererResult(false, "UI renderer is not ready");
        }
        // The frame graph owns this callback after composition. Capture the
        // validated payload pointers by value rather than references to these
        // stack-local pointer variables.
        frameFeatures.gui = [&, guiPayload, uiPayload](render_graph::RenderContext& graphContext) {
            const auto frameContext =
                makeFrameContext(frame, *backbufferTexture, impl_->extent, impl_->colorFormat);
            if (uiPayload != nullptr && uiPayload->loweredPlan != nullptr) {
                throwIfFailed(impl_->guiRenderer->recordPass(
                                  graphContext, frameContext, uiPayload->loweredPlan->commands()),
                    "Custom graph UI pass failed: ");
            } else if (guiPayload != nullptr) {
                throwIfFailed(impl_->guiRenderer->recordPass(
                                  graphContext, frameContext, *guiPayload->DebugDrawData),
                    "Custom graph GUI pass failed: ");
            }
            if (uiPayload != nullptr && uiPayload->debugDrawData != nullptr) {
                throwIfFailed(impl_->guiRenderer->recordPass(
                                  graphContext, frameContext, *uiPayload->debugDrawData),
                    "Custom graph debug overlay pass failed: ");
            }
        };
    }

    if (workload.globalIllumination().enabled()) {
        frameFeatures.globalIllumination =
            renderer::globalIlluminationDescForProfile(renderer::QualityProfile::Medium);
    } else {
        frameFeatures.globalIllumination =
            renderer::globalIlluminationDescForProfile(renderer::QualityProfile::Low);
    }

    try {
        backbufferTexture = &graph.texture(backbuffer);
        renderer::RenderFramePreparedRendererFrameConfig preparedRendererFrame;
        preparedRendererFrame.preparedFramePlan = impl_->preparedRendererFramePrepareReport.success
            ? &impl_->preparedRendererFramePlan
            : nullptr;
        preparedRendererFrame.framePlanBridge = impl_->preparedRendererFramePlanBridge;
        preparedRendererFrame.maxDrawCommandsPerPass =
            impl_->preparedRendererFrameMaxDrawCommandsPerPass;
        preparedRendererFrame.shaderArtifactRootPolicy =
            impl_->preparedRendererFrameShaderArtifactRoot.empty()
            ? "DefaultRendererRhiShaderArtifactRoot"
            : impl_->preparedRendererFrameShaderArtifactRoot;
        preparedRendererFrame.nativeHandleExposedToUpperLayer =
            impl_->preparedRendererFramePrepareReport.nativeHandleExposedToUpperLayer;
        renderer::RenderFrameDirectorRequest directorRequest{
            workload,
            graph,
            backbuffer,
            renderer::RenderPipelineGraphCompositionContext{
                frame,
                pipelineRequest,
                workload.scene().enabled(),
            },
            impl_->activePipeline,
            std::move(frameFeatures),
            preparedRendererFrame,
        };
        const auto pipelineComposition = impl_->frameDirector.compose(directorRequest);
        if (!pipelineComposition.success) {
            std::pmr::string diagnostics = "Custom graph pipeline composition failed: ";
            diagnostics += pipelineComposition.diagnostics.empty()
                ? "RenderFrameDirector failed without diagnostics"
                : pipelineComposition.diagnostics.front().message;
            return rendererResult(false, std::move(diagnostics));
        }
        if (pipelineComposition.decision.mode
            == renderer::RenderFrameExecutionMode::PreparedSubmit) {
            if (pipelineComposition.decision.preparedSubmitRequirements.empty()) {
                return rendererResult(false,
                    "Prepared renderer-frame Vulkan path requires an explicit prepared-submit requirement");
            }
            const auto& preparedSubmit =
                pipelineComposition.decision.preparedSubmitRequirements.front();
            if (!impl_->preparedRendererFramePath
                || !impl_->preparedRendererFramePrepareReport.success) {
                return rendererResult(false,
                    "Prepared renderer-frame Vulkan path requires explicit prepareRendererFrame before hot submit");
            }
            if (!preparedRendererFrameMatchesDecision(
                    preparedSubmit, impl_->preparedRendererFramePlan)) {
                return rendererResult(false,
                    "Prepared renderer-frame Vulkan path rejected a payload whose draw contract differs from the prepared frame plan");
            }

            auto submitted =
                impl_->preparedRendererFramePath->submit(*impl_->device, *backbufferTexture);
            applySceneRuntimeSubmitReport(impl_->lastSceneRuntimeCustomGraphFrameReport,
                submitted,
                sceneRuntimePrepareReportFrom(impl_->preparedRendererFramePrepareReport),
                impl_->surfaceBackedPresentation && impl_->backend == rhi::Backend::Vulkan);
            if (!submitted.success) {
                return rendererResult(false,
                    "Prepared renderer-frame Vulkan submit failed: " + submitted.diagnostics);
            }
            impl_->sceneRuntimeCustomGraphPendingPresent = true;
            impl_->sceneRuntimeCustomGraphPendingPresentSerial = frame.frameSerial;
            frameDiscardGuard.commit();
            return rendererResult(true, "");
        }

        class FramePresentationCompletionObserver final
            : public render_graph::PassSubmissionObserver {
        public:
            void onSubmitted(render_graph::PassSubmissionToken,
                const std::shared_ptr<const rhi::GpuFence>& completion) noexcept override
            {
                completion_ = completion;
            }

            std::shared_ptr<const rhi::GpuFence> completion_;
        } framePresentationCompletionObserver;

        graph.addPass("PlatformFramePresentationCompletion")
            .externalSideEffects()
            .onSubmitted(framePresentationCompletionObserver)
            .execute([](render_graph::RenderContext&) {});
        graph.execute();
        if (!framePresentationCompletionObserver.completion_) {
            return rendererResult(false, "PLATFORM-FRAME-PRESENT-COMPLETION-MISSING");
        }
        impl_->currentFramePresentationCompletion =
            std::move(framePresentationCompletionObserver.completion_);
        frameDiscardGuard.commit();
        if (std::getenv("WOCC_WAIT_IDLE_AFTER_GRAPH") != nullptr && impl_->nativeVulkanRuntime) {
            impl_->nativeVulkanRuntime->waitIdle();
        }
        captureFrameGraphTrace(graph, impl_->lastSceneRuntimeCustomGraphFrameReport);
        impl_->lastSceneRuntimeCustomGraphFrameReport.renderGraphFrameUsed = true;
        impl_->lastSceneRuntimeCustomGraphFrameReport.legacyPipelineFallbackUsed =
            pipelineComposition.decision.mode
            == renderer::RenderFrameExecutionMode::LegacyPipelineFallback;
        impl_->lastSceneRuntimeCustomGraphFrameReport.sceneRuntimePathSubmitted =
            !impl_->lastSceneRuntimeCustomGraphFrameReport.legacyPipelineFallbackUsed;
        impl_->lastSceneRuntimeCustomGraphFrameReport.surfaceBackedVulkan =
            impl_->surfaceBackedPresentation && impl_->backend == rhi::Backend::Vulkan;
        impl_->lastSceneRuntimeCustomGraphFrameReport.usedRhiGpuDevice = impl_->device != nullptr;
        impl_->lastSceneRuntimeCustomGraphFrameReport.swapchainRenderTargetUsed = true;
        impl_->lastSceneRuntimeCustomGraphFrameReport.backendFallbacksHiddenFromUpperLayer =
            impl_->lastSceneRuntimeCustomGraphFrameReport.backendFallbacksHiddenFromUpperLayer
            && impl_->device != nullptr
            && pipelineComposition.decision.mode
                != renderer::RenderFrameExecutionMode::LegacyPipelineFallback;
        impl_->sceneRuntimeCustomGraphPendingPresent = true;
        impl_->sceneRuntimeCustomGraphPendingPresentSerial = frame.frameSerial;
    } catch (const std::exception& error) {
        std::pmr::string diagnostics = "Custom graph RHI composition/execution failed: ";
        diagnostics += error.what();
        return rendererResult(false, std::move(diagnostics));
    }

    return rendererResult(true, "");
}

rhi::PresentResult RenderSystem::presentFrame(const rhi::AcquiredFrame& frame)
{
    if (!valid() || !impl_->swapchain) {
        return rhi::makePresentFailureResult(
            "present failure: render system is not valid", frame.frameSerial, frame.frameIndex);
    }
    if (!sameFrame(frame, impl_->currentFrame)) {
        return rhi::makePresentFailureResult(
            "present failure: frame is not presentable", frame.frameSerial, frame.frameIndex);
    }
    auto presented = impl_->swapchain->present(frame, {impl_->currentFramePresentationCompletion});
    if (!presented.success) {
        if (!(presented.recoverable && presented.swapchainRebuildRequired)) {
            const auto discarded = impl_->swapchain->discardFrame(frame);
            if (!discarded.success && !discarded.diagnostics.empty()) {
                presented.diagnostics += "; frame discard after present failure failed: ";
                presented.diagnostics += discarded.diagnostics;
            }
        }
        impl_->currentFrame = {};
        impl_->currentFramePresentationCompletion.reset();
        presented.diagnostics = "present failure: " + presented.diagnostics;
        return presented;
    }
    if (impl_->sceneRuntimeCustomGraphPendingPresent
        && impl_->sceneRuntimeCustomGraphPendingPresentSerial == frame.frameSerial
        && impl_->device) {
        applySceneRuntimeVulkanStats(impl_->lastSceneRuntimeCustomGraphFrameReport, *impl_->device);
        impl_->sceneRuntimeCustomGraphPendingPresent = false;
        impl_->sceneRuntimeCustomGraphPendingPresentSerial = 0;
    }
    impl_->currentFrame = {};
    impl_->currentFramePresentationCompletion.reset();
    return presented;
}

RenderSystemPreparedRendererFramePrepareReport RenderSystem::prepareRendererFrame(
    const RenderSystemPreparedRendererFramePrepareDesc& desc)
{
    RenderSystemPreparedRendererFramePrepareReport report;
    if (!valid() || !impl_->device || !impl_->swapchain) {
        report.diagnostics =
            "Prepared renderer-frame Vulkan prepare requires a valid render system";
        return report;
    }
    if (impl_->backend != rhi::Backend::Vulkan || !impl_->surfaceBackedPresentation) {
        report.diagnostics =
            "Prepared renderer-frame Vulkan prepare requires a surface-backed Vulkan backend";
        return report;
    }
    if (desc.framePlan.drawPlans.empty()) {
        report.diagnostics =
            "Prepared renderer-frame Vulkan prepare requires an explicit renderer frame plan";
        return report;
    }

    renderer::RendererRhiProductionFramePathDesc pathDesc;
    pathDesc.label = desc.label.empty() ? "PreparedRendererFrame" : desc.label;
    pathDesc.colorExtent = {impl_->extent.width, impl_->extent.height, 1u};
    pathDesc.colorFormat = impl_->colorFormat;
    pathDesc.framePlanBridge = desc.framePlanBridge;
    pathDesc.maxDrawCommandsPerPass = desc.maxDrawCommandsPerPass;
    pathDesc.useExternalColorTarget = true;
    pathDesc.shaderArtifactRoot = desc.shaderArtifactRoot;

    auto path = arp::core::makeUnique<renderer::RendererRhiProductionFramePath>(pathDesc);
    const auto prepared = path->prepare(desc.framePlan, *impl_->device);
    const auto prepareCount = impl_->preparedRendererFramePrepareCount + 1u;
    report = preparedRendererFrameReportFrom(prepared, prepareCount);
    if (!report.success) {
        return report;
    }

    impl_->preparedRendererFramePath = std::move(path);
    impl_->preparedRendererFramePlan = desc.framePlan;
    impl_->preparedRendererFramePlanBridge = desc.framePlanBridge;
    impl_->preparedRendererFrameMaxDrawCommandsPerPass = desc.maxDrawCommandsPerPass;
    impl_->preparedRendererFrameShaderArtifactRoot = desc.shaderArtifactRoot;
    impl_->preparedRendererFramePrepareCount = prepareCount;
    impl_->preparedRendererFramePrepareReport = report;
    return report;
}

RenderSystemSceneRuntimeCustomGraphPrepareReport RenderSystem::prepareSceneRuntimeCustomGraphFrame(
    const RenderSystemSceneRuntimeCustomGraphPrepareDesc& desc)
{
    RenderSystemSceneRuntimeCustomGraphPrepareReport report;
    if (!valid() || !impl_->device || !impl_->swapchain) {
        report.diagnostics =
            "SceneRuntime custom graph Vulkan prepare requires a valid render system";
        return report;
    }
    if (impl_->backend != rhi::Backend::Vulkan || !impl_->surfaceBackedPresentation) {
        report.diagnostics =
            "SceneRuntime custom graph Vulkan prepare requires a surface-backed Vulkan backend";
        return report;
    }
    if (desc.framePlan.drawPlans.empty()) {
        report.diagnostics =
            "SceneRuntime custom graph Vulkan prepare requires an explicit renderer frame plan";
        return report;
    }

    RenderSystemPreparedRendererFramePrepareDesc preparedDesc;
    preparedDesc.label = desc.label.empty() ? "SceneRuntimeCustomGraph" : desc.label;
    preparedDesc.framePlan = desc.framePlan;
    preparedDesc.framePlanBridge = desc.framePlanBridge;
    preparedDesc.maxDrawCommandsPerPass = desc.maxDrawCommandsPerPass;
    preparedDesc.shaderArtifactRoot = desc.shaderArtifactRoot;

    const auto prepared = prepareRendererFrame(preparedDesc);
    report = sceneRuntimePrepareReportFrom(prepared);
    if (!report.success) {
        return report;
    }

    impl_->sceneRuntimeCustomGraphPrepareCount = prepared.prepareCount;
    impl_->sceneRuntimeCustomGraphPrepareReport = report;
    impl_->lastSceneRuntimeCustomGraphFrameReport = {};
    impl_->lastSceneRuntimeCustomGraphFrameReport.prepareCount = prepared.prepareCount;
    impl_->lastSceneRuntimeCustomGraphFrameReport.surfaceBackedVulkan = true;
    impl_->lastSceneRuntimeCustomGraphFrameReport.backendFallbacksHiddenFromUpperLayer =
        report.backendFallbacksHiddenFromUpperLayer;
    impl_->lastSceneRuntimeCustomGraphFrameReport.nativeHandleExposedToUpperLayer =
        report.nativeHandleExposedToUpperLayer;
    impl_->sceneRuntimeCustomGraphPendingPresent = false;
    impl_->sceneRuntimeCustomGraphPendingPresentSerial = 0;
    return report;
}

const RenderSystemSceneRuntimeCustomGraphFrameReport&
RenderSystem::lastSceneRuntimeCustomGraphFrameReport() const
{
    static const RenderSystemSceneRuntimeCustomGraphFrameReport emptyReport;
    return impl_ ? impl_->lastSceneRuntimeCustomGraphFrameReport : emptyReport;
}

RenderSystemRhiProductionFrameSubmitReport RenderSystem::submitRhiProductionFrame(
    const RenderSystemRhiProductionFrameSubmitDesc& desc)
{
    RenderSystemRhiProductionFrameSubmitReport report;
    if (!valid() || !impl_->device || !impl_->swapchain) {
        arp::core::appendDiagnostic(report.diagnostics,
            "Vulkan RenderSystem RHI production frame submit requires a valid render system");
        return report;
    }
    if (impl_->backend != rhi::Backend::Vulkan) {
        arp::core::appendDiagnostic(report.diagnostics,
            "RenderSystem RHI production frame submit requires the Vulkan backend");
        return report;
    }

    renderer::RendererRhiProductionFramePath path({
        desc.label.empty() ? "RenderSystemRhiProductionFrame" : desc.label,
        {impl_->extent.width, impl_->extent.height, 1u},
        impl_->colorFormat,
        desc.framePlanBridge,
        desc.maxDrawCommandsPerPass,
        desc.renderToSwapchain,
        desc.shaderArtifactRoot,
    });

    const auto prepared = path.prepare(desc.framePlan, *impl_->device);
    arp::core::appendDiagnostic(report.diagnostics, prepared.diagnostics);
    report.usedRhiGpuDevice = report.usedRhiGpuDevice || prepared.usedRhiGpuDevice;
    report.rendererFramePlanAccepted = prepared.rendererFramePlanAccepted;
    report.rhiResourceCreated = prepared.rhiResourceCreated;
    report.rhiPipelineCreated = prepared.rhiPipelineCreated;
    report.rhiDescriptorCreated = prepared.rhiDescriptorCreated;
    report.shaderPackageValidated = prepared.shaderPackageValidated;
    report.fullSurfaceViewportConfigured = prepared.fullSurfaceViewportConfigured;
    report.fullSurfaceScissorConfigured = prepared.fullSurfaceScissorConfigured;
    report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
        && prepared.backendFallbacksHiddenFromUpperLayer;
    report.nativeHandleExposedToUpperLayer =
        report.nativeHandleExposedToUpperLayer || prepared.nativeHandleExposedToUpperLayer;
    if (!prepared.success) {
        return report;
    }
    report.productionFramePreparedBeforeSubmit = true;

    const auto frameCount = std::max(1u, desc.frameCount);
    for (std::uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        rhi::AcquiredFrame acquired;
        rhi::AcquiredSwapchainImage acquiredImage;
        if (desc.renderToSwapchain) {
            acquired = acquireFrame();
            arp::core::appendDiagnostic(report.diagnostics, acquired.diagnostics);
            report.acquireFrame = report.acquireFrame || acquired.success;
            if (!acquired.success) {
                return report;
            }
            acquiredImage = impl_->swapchain->acquiredImage(acquired);
            arp::core::appendDiagnostic(report.diagnostics, acquiredImage.diagnostics);
            if (!acquiredImage.success || !acquiredImage.texture) {
                return report;
            }
            report.swapchainRenderTargetUsed = true;
        }

        const auto submitted = desc.renderToSwapchain
            ? path.submit(*impl_->device, *acquiredImage.texture)
            : path.submit(*impl_->device);
        arp::core::appendDiagnostic(report.diagnostics, submitted.diagnostics);
        report.usedRhiGpuDevice = report.usedRhiGpuDevice || submitted.usedRhiGpuDevice;
        report.rhiCommandEncoded = report.rhiCommandEncoded || submitted.rhiCommandEncoded;
        report.rhiQueueSubmitted = report.rhiQueueSubmitted || submitted.rhiQueueSubmitted;
        report.proceduralSceneRuntime3DObjectDrawn = report.proceduralSceneRuntime3DObjectDrawn
            || submitted.proceduralSceneRuntime3DObjectDrawn;
        report.proceduralSceneRuntime2DSpriteDrawn = report.proceduralSceneRuntime2DSpriteDrawn
            || submitted.proceduralSceneRuntime2DSpriteDrawn;
        report.proceduralSceneRuntimeUiDrawn =
            report.proceduralSceneRuntimeUiDrawn || submitted.proceduralSceneRuntimeUiDrawn;
        report.resourceAllocatedDuringSubmit =
            report.resourceAllocatedDuringSubmit || submitted.resourceAllocatedDuringSubmit;
        report.pipelineCreatedDuringSubmit =
            report.pipelineCreatedDuringSubmit || submitted.pipelineCreatedDuringSubmit;
        report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
            && submitted.backendFallbacksHiddenFromUpperLayer;
        report.nativeHandleExposedToUpperLayer =
            report.nativeHandleExposedToUpperLayer || submitted.nativeHandleExposedToUpperLayer;
        report.renderPassCount += submitted.renderPassCount;
        report.drawCommandCount += submitted.drawCommandCount;
        if (!submitted.success) {
            return report;
        }
        if (desc.expectedDrawCommandCount != 0
            && submitted.drawCommandCount != desc.expectedDrawCommandCount) {
            arp::core::appendDiagnostic(report.diagnostics,
                "Vulkan RenderSystem production submit draw count did not match the scene-runtime frame");
            return report;
        }

        if (!desc.renderToSwapchain) {
            acquired = acquireFrame();
            arp::core::appendDiagnostic(report.diagnostics, acquired.diagnostics);
            report.acquireFrame = report.acquireFrame || acquired.success;
            if (!acquired.success) {
                return report;
            }
        }
        const auto presented = presentFrame(acquired);
        arp::core::appendDiagnostic(report.diagnostics, presented.diagnostics);
        report.presentFrame = report.presentFrame || presented.success;
        if (!presented.success) {
            return report;
        }
        ++report.framesSubmitted;
    }

    const auto stats = rhi_vulkan::inspectVulkanRhiNativeResourceStats(*impl_->device);
    report.vulkanNativeStatsCaptured = stats.runtimeConnected;
    report.vulkanNativeRenderPassBeginCount = stats.nativeRenderPassBeginRecordCount;
    report.vulkanNativeIndexedDrawCount = stats.nativeIndexedDrawRecordCount;
    report.vulkanNativeQueueSubmitCount = stats.nativeQueueSubmitCount;
    report.hiddenWindowRenderOutputHashCaptured = stats.hiddenWindowRenderOutputHashCaptured;
    report.deterministicRenderOutputHash = stats.deterministicRenderOutputHash;
    report.nativeHandleExposedToUpperLayer =
        report.nativeHandleExposedToUpperLayer || !stats.nativeHandlesHiddenFromPublicApi;

    report.success = report.productionRendererPath && report.usedRhiGpuDevice
        && report.productionFramePreparedBeforeSubmit && report.rendererFramePlanAccepted
        && report.rhiResourceCreated && report.rhiPipelineCreated && report.rhiDescriptorCreated
        && report.shaderPackageValidated && report.fullSurfaceViewportConfigured
        && report.fullSurfaceScissorConfigured && report.rhiCommandEncoded
        && report.rhiQueueSubmitted && report.acquireFrame && report.presentFrame
        && report.framesSubmitted == frameCount && report.vulkanNativeStatsCaptured
        && report.hiddenWindowRenderOutputHashCaptured && report.deterministicRenderOutputHash != 0
        && !report.resourceAllocatedDuringSubmit && !report.pipelineCreatedDuringSubmit
        && !report.nativeHandleExposedToUpperLayer && report.backendFallbacksHiddenFromUpperLayer;
    if (!report.success && report.diagnostics.empty()) {
        arp::core::appendDiagnostic(report.diagnostics,
            !report.hiddenWindowRenderOutputHashCaptured
                ? rhi::semantics::RenderOutputObservationContract::unavailableDiagnostic
                : "Vulkan RenderSystem RHI production frame submit failed validation");
    }
    return report;
}

RenderSystemResult RenderSystem::configureRhiFrameLoopDryRun(
    const RenderSystemRhiFrameLoopDryRunDesc& desc)
{
    if (!impl_) {
        impl_ = core::makeUnique<Impl>();
    }
    impl_->rhiDryRunDesc = desc;
    impl_->lastRhiDryRunReport = {};
    impl_->lastRhiDryRunSmokeReport = {};
    impl_->lastRhiStartupProbeReport = {};
    impl_->lastRhiStartupGateReport = {};
    impl_->lastRhiStartupProbeScheduleReport = {};
    impl_->lastRhiStartupProbeBatchReport = {};
    impl_->lastRhiStartupProbeRunReport = {};
    impl_->lastRhiStartupTelemetryReport = {};
    impl_->lastRhiStartupReadinessReport = {};
    impl_->lastRhiFormalRefactorStartGateReport = {};
    return renderSystemResult(true, "");
}

RenderSystemRhiFrameLoopDryRunReport RenderSystem::evaluateRhiFrameLoopDryRun(
    const RenderSystemRhiFrameLoopDryRunInput& input)
{
    if (!impl_) {
        impl_ = core::makeUnique<Impl>();
    }
    impl_->lastRhiDryRunReport =
        evaluateRenderSystemRhiFrameLoopDryRun(impl_->rhiDryRunDesc, input);
    return impl_->lastRhiDryRunReport;
}

RenderSystemRhiFrameLoopDryRunReport RenderSystem::evaluateRhiFrameLoopDryRunForCurrentState(
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    platform::RenderSystemFrameLoopState state;
    state.renderPipelineReady = valid();
    state.frameInProgress = impl_ && impl_->currentFrame.success;
    state.nextFrameSerial =
        impl_ && impl_->currentFrame.success ? impl_->currentFrame.frameSerial + 1u : 1u;
    return evaluateRhiFrameLoopDryRun({rendererFramePlan, lifecycleState, state});
}

RenderSystemRhiFrameLoopDryRunSmokeReport
RenderSystem::evaluateRhiFrameLoopDryRunSmokeForCurrentState(
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    const auto dryRun =
        evaluateRhiFrameLoopDryRunForCurrentState(rendererFramePlan, lifecycleState);
    impl_->lastRhiDryRunSmokeReport =
        evaluateRenderSystemRhiFrameLoopDryRunSmoke(smokeDesc, dryRun);
    return impl_->lastRhiDryRunSmokeReport;
}

RenderSystemRhiStartupProbeReport RenderSystem::evaluateRhiStartupProbeForCurrentState(
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    const auto smoke = evaluateRhiFrameLoopDryRunSmokeForCurrentState(
        smokeDesc, rendererFramePlan, lifecycleState);
    impl_->lastRhiStartupProbeReport = evaluateRenderSystemRhiStartupProbe(
        summarizeRenderSystemRhiFrameLoopDryRunSmokeCapability(smoke));
    return impl_->lastRhiStartupProbeReport;
}

RenderSystemRhiStartupGateReport RenderSystem::evaluateRhiStartupGateForCurrentState(
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    const auto startupProbe =
        evaluateRhiStartupProbeForCurrentState(smokeDesc, rendererFramePlan, lifecycleState);
    impl_->lastRhiStartupGateReport = evaluateRenderSystemRhiStartupGate(
        summarizeRenderSystemRhiStartupProbeCapability(startupProbe));
    return impl_->lastRhiStartupGateReport;
}

RenderSystemRhiStartupProbeScheduleReport RenderSystem::scheduleRhiStartupProbeForCurrentState(
    const RenderSystemRhiStartupProbeScheduleDesc& scheduleDesc,
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    const auto startupGate =
        evaluateRhiStartupGateForCurrentState(smokeDesc, rendererFramePlan, lifecycleState);
    impl_->lastRhiStartupProbeScheduleReport =
        scheduleRenderSystemRhiStartupProbe(scheduleDesc, startupGate);
    return impl_->lastRhiStartupProbeScheduleReport;
}

RenderSystemRhiStartupProbeBatchReport RenderSystem::planRhiStartupProbeBatchForCurrentState(
    const RenderSystemRhiStartupProbeScheduleDesc& scheduleDesc,
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    const auto schedule = scheduleRhiStartupProbeForCurrentState(
        scheduleDesc, smokeDesc, rendererFramePlan, lifecycleState);
    impl_->lastRhiStartupProbeBatchReport = planRenderSystemRhiStartupProbeBatch(schedule);
    return impl_->lastRhiStartupProbeBatchReport;
}

RenderSystemRhiStartupProbeRunReport RenderSystem::runRhiStartupProbeForCurrentState(
    const RenderSystemRhiStartupProbeRunnerDesc& runnerDesc,
    const RenderSystemRhiStartupProbeScheduleDesc& scheduleDesc,
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    const auto batch = planRhiStartupProbeBatchForCurrentState(
        scheduleDesc, smokeDesc, rendererFramePlan, lifecycleState);
    impl_->lastRhiStartupProbeRunReport = runRenderSystemRhiStartupProbeBatch(runnerDesc, batch);
    return impl_->lastRhiStartupProbeRunReport;
}

RenderSystemRhiStartupTelemetryReport RenderSystem::evaluateRhiStartupTelemetryForCurrentState(
    const RenderSystemRhiStartupProbeRunnerDesc& runnerDesc,
    const RenderSystemRhiStartupProbeScheduleDesc& scheduleDesc,
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    const auto run = runRhiStartupProbeForCurrentState(
        runnerDesc, scheduleDesc, smokeDesc, rendererFramePlan, lifecycleState);
    impl_->lastRhiStartupTelemetryReport = collectRenderSystemRhiStartupTelemetry(run);
    return impl_->lastRhiStartupTelemetryReport;
}

RenderSystemRhiStartupReadinessReport RenderSystem::evaluateRhiStartupReadinessForCurrentState(
    const RenderSystemRhiStartupProbeRunnerDesc& runnerDesc,
    const RenderSystemRhiStartupProbeScheduleDesc& scheduleDesc,
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    const auto telemetry = evaluateRhiStartupTelemetryForCurrentState(
        runnerDesc, scheduleDesc, smokeDesc, rendererFramePlan, lifecycleState);
    impl_->lastRhiStartupReadinessReport = evaluateRenderSystemRhiStartupReadiness(telemetry);
    return impl_->lastRhiStartupReadinessReport;
}

RenderSystemRhiFormalRefactorStartGateReport
RenderSystem::evaluateRhiFormalRefactorStartGateForCurrentState(
    const RenderSystemRhiStartupProbeRunnerDesc& runnerDesc,
    const RenderSystemRhiStartupProbeScheduleDesc& scheduleDesc,
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    const auto readiness = evaluateRhiStartupReadinessForCurrentState(
        runnerDesc, scheduleDesc, smokeDesc, rendererFramePlan, lifecycleState);
    impl_->lastRhiFormalRefactorStartGateReport =
        evaluateRenderSystemRhiFormalRefactorStartGate(readiness);
    return impl_->lastRhiFormalRefactorStartGateReport;
}

const RenderSystemRhiFrameLoopDryRunReport& RenderSystem::lastRhiFrameLoopDryRunReport() const
{
    static const RenderSystemRhiFrameLoopDryRunReport emptyReport;
    return impl_ ? impl_->lastRhiDryRunReport : emptyReport;
}

const RenderSystemRhiFrameLoopDryRunSmokeReport&
RenderSystem::lastRhiFrameLoopDryRunSmokeReport() const
{
    static const RenderSystemRhiFrameLoopDryRunSmokeReport emptyReport;
    return impl_ ? impl_->lastRhiDryRunSmokeReport : emptyReport;
}

const RenderSystemRhiStartupProbeReport& RenderSystem::lastRhiStartupProbeReport() const
{
    static const RenderSystemRhiStartupProbeReport emptyReport;
    return impl_ ? impl_->lastRhiStartupProbeReport : emptyReport;
}

const RenderSystemRhiStartupGateReport& RenderSystem::lastRhiStartupGateReport() const
{
    static const RenderSystemRhiStartupGateReport emptyReport;
    return impl_ ? impl_->lastRhiStartupGateReport : emptyReport;
}

const RenderSystemRhiStartupProbeScheduleReport&
RenderSystem::lastRhiStartupProbeScheduleReport() const
{
    static const RenderSystemRhiStartupProbeScheduleReport emptyReport;
    return impl_ ? impl_->lastRhiStartupProbeScheduleReport : emptyReport;
}

const RenderSystemRhiStartupProbeBatchReport& RenderSystem::lastRhiStartupProbeBatchReport() const
{
    static const RenderSystemRhiStartupProbeBatchReport emptyReport;
    return impl_ ? impl_->lastRhiStartupProbeBatchReport : emptyReport;
}

const RenderSystemRhiStartupProbeRunReport& RenderSystem::lastRhiStartupProbeRunReport() const
{
    static const RenderSystemRhiStartupProbeRunReport emptyReport;
    return impl_ ? impl_->lastRhiStartupProbeRunReport : emptyReport;
}

const RenderSystemRhiStartupTelemetryReport& RenderSystem::lastRhiStartupTelemetryReport() const
{
    static const RenderSystemRhiStartupTelemetryReport emptyReport;
    return impl_ ? impl_->lastRhiStartupTelemetryReport : emptyReport;
}

const RenderSystemRhiStartupReadinessReport& RenderSystem::lastRhiStartupReadinessReport() const
{
    static const RenderSystemRhiStartupReadinessReport emptyReport;
    return impl_ ? impl_->lastRhiStartupReadinessReport : emptyReport;
}

const RenderSystemRhiFormalRefactorStartGateReport&
RenderSystem::lastRhiFormalRefactorStartGateReport() const
{
    static const RenderSystemRhiFormalRefactorStartGateReport emptyReport;
    return impl_ ? impl_->lastRhiFormalRefactorStartGateReport : emptyReport;
}

RenderSystem createDefaultRenderSystem(WindowSystem& platform,
    const PlatformWindow& window,
    const std::pmr::string& applicationName,
    const rhi::PresentationMode presentationMode,
    const rhi::semantics::UploadStagingBudgetV1* uploadStagingBudgetOverride,
    const rhi::semantics::DirectObjectBudget* directObjectBudgetOverride)
{
    (void)uploadStagingBudgetOverride;
    auto impl = core::makeUnique<RenderSystem::Impl>();
    impl->backend = rhi::Backend::Vulkan;
    if (directObjectBudgetOverride != nullptr) {
        impl->diagnostics = "RHI-DIRECT-OBJECT-BUDGET-VULKAN-UNSUPPORTED";
        return RenderSystem(std::move(impl));
    }
    impl->extent = {
        static_cast<std::uint32_t>(window.width()),
        static_cast<std::uint32_t>(window.height()),
    };
    if (impl->extent.width == 0u || impl->extent.height == 0u) {
        impl->diagnostics = "Vulkan RHI RenderSystem requires a non-zero surface extent";
        return RenderSystem(std::move(impl));
    }

    impl->nativeVulkanRuntime =
        VulkanSurfaceRuntimeOwner::create(platform, window, applicationName, impl->diagnostics);
    if (!impl->nativeVulkanRuntime) {
        impl->diagnostics = impl->diagnostics.empty()
            ? (window.hidden()
                      ? "Vulkan RHI hidden RenderSystem requires native surface-backed validation"
                      : "Vulkan RHI visible RenderSystem requires native surface-backed presentation")
            : impl->diagnostics;
        return RenderSystem(std::move(impl));
    }

    impl->device =
        createRhiVulkanDevice(applicationName, impl->nativeVulkanRuntime, impl->diagnostics);
    if (!impl->device) {
        return RenderSystem(std::move(impl));
    }
    impl->frameGraphResourcePool =
        arp::core::makeShared<render_graph::FrameGraphResourcePool>(*impl->device);
    impl->swapchainDesc = {
        applicationName.empty() ? "RenderSystemRhiVulkanSurface" : applicationName,
        impl->extent,
        impl->colorFormat,
        2u,
        presentationMode,
    };
    impl->swapchain = impl->device->createSwapchain(impl->swapchainDesc);
    if (!impl->swapchain) {
        impl->diagnostics = "Vulkan RHI swapchain creation failed";
        return RenderSystem(std::move(impl));
    }

    if (impl->nativeVulkanRuntime) {
        impl->surfaceBackedPresentation = true;
    }
    impl->valid = true;
    return RenderSystem(std::move(impl));
}

RenderSystem createRenderSystem(const RenderSystemDesc& desc)
{
    const auto route =
        RenderSystemBackendSelection::forBuild(desc.backendPreference, rhi::Backend::Vulkan);
    if (!route.routeToFactory) {
        auto impl = core::makeUnique<RenderSystem::Impl>();
        impl->backend = route.requestedBackend;
        impl->diagnostics = route.diagnostic;
        return RenderSystem(std::move(impl));
    }
    if (desc.orderedGraphics) {
        auto impl = core::makeUnique<RenderSystem::Impl>();
        impl->diagnostics = "PLATFORM-ORDERED-PROFILE-BACKEND-UNAVAILABLE";
        return RenderSystem(std::move(impl));
    }
    if (!desc.surface.windowSystem || !desc.surface.window) {
        return RenderSystem();
    }
    return createDefaultRenderSystem(*desc.surface.windowSystem,
        *desc.surface.window,
        desc.applicationName,
        desc.presentationMode);
}

} // namespace arp::platform
