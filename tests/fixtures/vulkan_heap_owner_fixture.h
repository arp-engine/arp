#pragma once

#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_device.h"
#include "rhi/semantics/vulkan_device_capability_query.h"
#include "tests/fixtures/vulkan_headless_runtime.h"

#include <cassert>
#include <cstdio>

namespace arp::tests {

class VulkanHeapOwnerFixture final {
public:
    static core::UniquePtr<rhi::GpuDevice> makeDevice(
        const std::shared_ptr<rhi_vulkan::VulkanRhiRuntime>& runtime,
        bool enableAliasing = true,
        std::uint64_t budget = 64u * 1024u * 1024u)
    {
        rhi_vulkan::VulkanRhiDeviceDesc desc;
        desc.label = "heap owner";
        desc.runtime = runtime;
        desc.memoryBudgetBytes = budget;
        desc.enableTransientAliasing = enableAliasing;
        rhi::semantics::vulkan::VulkanDeviceCapabilitySnapshot snapshot;
        snapshot.queriedFromNativeDevice = runtime->valid();
        snapshot.queriedExtendedFeatures = runtime->coreQueueCapabilityDiagnostic() == nullptr;
        snapshot.supportsTimelineSynchronization = runtime->timelineSemaphoreEnabled();
        for (std::uint32_t i = 0u; i < runtime->memoryProperties().memoryTypeCount; ++i) {
            const auto flags = runtime->memoryProperties().memoryTypes[i].propertyFlags;
            snapshot.hasDeviceLocalMemory |= (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0u;
            snapshot.hasHostVisibleMemory |= (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u;
        }
        desc.capabilityContract =
            rhi::semantics::vulkan::buildVulkanNativeCapabilityContract(snapshot);
        return rhi_vulkan::createVulkanRhiDevice(desc);
    }

public:
    enum class Mode { Lease, Placed };

    static int run(Mode mode)
    {
        using namespace rhi_vulkan;
        auto native = core::makeUnique<VulkanHeadlessRuntime>();
        if (!native->runtime() || !native->runtime()->valid())
            return 77;
        auto rescue = native->runtime();
        const std::weak_ptr<VulkanRhiRuntime> observedRuntime = rescue;
        const std::weak_ptr<VulkanRhiNativeRuntimeOwner> observedOwner = native->nativeOwner();
        const auto deviceHandle = native->device();
        auto device = makeDevice(rescue);
        assert(device && device->capabilities().features.transientAliasing);
        rhi::TextureDesc desc;
        desc.label = "independent placed owner";
        desc.extent = {4u, 4u, 1u};
        desc.format = rhi::TextureFormat::Rgba8Unorm;
        desc.usage = rhi::TextureUsage::Sampled;
        const auto requirements = device->queryTexturePlacementRequirements(desc);
        assert(requirements.sizeBytes && requirements.alignmentBytes);
        auto heap = device->createMemoryHeap(
            requirements.sizeBytes, requirements.alignmentBytes, rhi::MemoryUsage::Transient);
        assert(heap && heap->supportsAliasing());
        std::shared_ptr<VulkanRhiNativeResourcePayload> payload;
        if (mode == Mode::Placed) {
            auto texture = device->createPlacedTexture(desc, *heap, 0u);
            assert(texture);
            payload = vulkanRhiNativePayload(*texture);
            assert(payload && payload->backendVulkanImageHandle());
            texture.reset();
            heap.reset();
        }
        device.reset();
        native.reset();
        // Keep a real runtime for safe red cleanup. Never invoke the old
        // borrowed pool allocator after creator destruction on the red path.
        const bool retained = rescue.use_count() > 1;
        bool acquiredAfterCreator = false;
        if (mode == Mode::Lease && retained) {
            auto nextDevice = makeDevice(rescue);
            auto texture = nextDevice->createPlacedTexture(desc, *heap, 0u);
            acquiredAfterCreator = bool(texture);
            if (texture)
                payload = vulkanRhiNativePayload(*texture);
            texture.reset();
            nextDevice.reset();
            heap.reset();
        }
        if (retained)
            rescue.reset();
        const bool independent = retained && !observedRuntime.expired() && !observedOwner.expired();
        bool nativeUse = false;
        bool deviceIdentity = false;
        if (payload) {
            VkMemoryRequirements actual{};
            vkGetImageMemoryRequirements(
                deviceHandle, payload->backendVulkanImageHandle(), &actual);
            nativeUse = actual.size == requirements.sizeBytes
                && actual.alignment == requirements.alignmentBytes;
            deviceIdentity = payload->backendVulkanDeviceHandle() == deviceHandle;
        }
        payload.reset();
        heap.reset();
        rescue.reset();
        const bool released = observedRuntime.expired() && observedOwner.expired();
        std::printf(
            "heap-owner: mode=%u retained=%d independent=%d acquired-after-creator=%d native-use=%d device-identity=%d released=%d\n",
            static_cast<unsigned>(mode),
            retained,
            independent,
            acquiredAfterCreator,
            nativeUse,
            deviceIdentity,
            released);
        std::fflush(stdout);
        assert(retained && independent && released);
        assert(nativeUse && deviceIdentity);
        assert(mode != Mode::Lease || acquiredAfterCreator);
        return 0;
    }
};

} // namespace arp::tests
