#pragma once

#include "tests/fixtures/vulkan_heap_owner_fixture.h"

#include <cstring>

namespace arp::tests {

// Real device and allocator; only native error returns are injected. No
// successful payload, allocation or completion is synthesized by this fixture.
class VulkanNativeFactoryAdmissionFixture final {
    enum class Failure { None, Buffer, Image, Sampler, DescriptorPool };
    inline static Failure failure_ = Failure::None;
    inline static unsigned failures_ = 0u;

    static VKAPI_ATTR VkResult VKAPI_CALL buffer(VkDevice device,
        const VkBufferCreateInfo* info,
        const VkAllocationCallbacks* callbacks,
        VkBuffer* output)
    {
        if (failure_ == Failure::Buffer) {
            ++failures_;
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        return vkCreateBuffer(device, info, callbacks, output);
    }

    static VKAPI_ATTR VkResult VKAPI_CALL image(VkDevice device,
        const VkImageCreateInfo* info,
        const VkAllocationCallbacks* callbacks,
        VkImage* output)
    {
        if (failure_ == Failure::Image) {
            ++failures_;
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        return vkCreateImage(device, info, callbacks, output);
    }

    static VKAPI_ATTR VkResult VKAPI_CALL sampler(VkDevice device,
        const VkSamplerCreateInfo* info,
        const VkAllocationCallbacks* callbacks,
        VkSampler* output)
    {
        if (failure_ == Failure::Sampler) {
            ++failures_;
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        return vkCreateSampler(device, info, callbacks, output);
    }

    static VKAPI_ATTR VkResult VKAPI_CALL pool(VkDevice device,
        const VkDescriptorPoolCreateInfo* info,
        const VkAllocationCallbacks* callbacks,
        VkDescriptorPool* output)
    {
        if (failure_ == Failure::DescriptorPool) {
            ++failures_;
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        return vkCreateDescriptorPool(device, info, callbacks, output);
    }

    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name)
    {
        if (std::strcmp(name, "vkCreateBuffer") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(buffer);
        if (std::strcmp(name, "vkCreateImage") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(image);
        if (std::strcmp(name, "vkCreateSampler") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(sampler);
        if (std::strcmp(name, "vkCreateDescriptorPool") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(pool);
        return vkGetDeviceProcAddr(device, name);
    }

public:
    static int run()
    {
        VulkanHeadlessRuntime native(false, resolve);
        if (!native.runtime())
            return 77;
        auto device = VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
        assert(device);
        rhi::BufferDesc bufferDesc{"NativeFactoryBuffer",
            256u,
            rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::GpuToCpu,
            rhi::ResourceState::Undefined};
        rhi::TextureDesc imageDesc;
        imageDesc.label = "NativeFactoryImage";
        imageDesc.extent = {4u, 4u, 1u};
        imageDesc.format = rhi::TextureFormat::Rgba8Unorm;
        imageDesc.usage = rhi::TextureUsage::Sampled;
        rhi::DescriptorLayoutDesc layout;
        layout.name = "NativeFactoryLayout";
        layout.bindings.push_back(
            {0u, rhi::DescriptorType::UniformBuffer, rhi::ShaderStage::Vertex, 1u});
        auto arena = device->createDescriptorArena({"NativeFactoryArena", 1u, 1u});
        assert(arena);
        for (const auto failure :
            {Failure::Buffer, Failure::Image, Failure::Sampler, Failure::DescriptorPool}) {
            failure_ = failure;
            const auto memory = device->memoryStats();
            const auto before = failures_;
            switch (failure) {
            case Failure::Buffer:
                assert(!device->createBuffer(bufferDesc));
                break;
            case Failure::Image:
                assert(!device->createTexture(imageDesc));
                break;
            case Failure::Sampler:
                assert(!device->createSampler({}));
                break;
            case Failure::DescriptorPool:
                assert(!arena->allocateDescriptorSet({"RejectedNativeSet", layout}));
                break;
            case Failure::None:
                assert(false);
                break;
            }
            assert(failures_ > before);
            assert(device->memoryStats().allocatedBytes == memory.allocatedBytes);
            assert(device->memoryStats().liveResourceCount == memory.liveResourceCount);
        }
        failure_ = Failure::None;
        auto buffer = device->createBuffer(bufferDesc);
        auto texture = device->createTexture(imageDesc);
        auto sampler = device->createSampler({});
        auto set = arena->allocateDescriptorSet({"NativeRetrySet", layout});
        assert(buffer && texture && sampler && set);
        assert(rhi_vulkan::vulkanRhiNativePayload(*buffer)->backendVulkanBufferHandle());
        assert(rhi_vulkan::vulkanRhiNativePayload(*texture)->backendVulkanImageHandle());
        assert(rhi_vulkan::vulkanRhiNativePayload(*sampler)->backendVulkanSamplerHandle());
        std::puts("NativeFactoryAdmission/1: real native error returns reject; native retry PASS");
        return 0;
    }
};

} // namespace arp::tests
