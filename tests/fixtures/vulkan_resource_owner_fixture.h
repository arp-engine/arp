#pragma once

#include "rhi/backends/vulkan/vulkan_native_resources.h"
#include "tests/fixtures/vulkan_headless_runtime.h"
#include "tests/fixtures/vulkan_heap_owner_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace arp::tests {

// Real native dispatch with a rescue reference for safe pre-fix diagnosis.
// No GPU work is submitted and no completion claim is made.
class VulkanResourceOwnerFixture final {
    enum class Dispatch : unsigned { Buffer, Image, Pool, Layout, Sampler, Memory, Count };

    struct Observation {
        std::weak_ptr<rhi_vulkan::VulkanRhiRuntime> runtime;
        std::weak_ptr<rhi_vulkan::VulkanRhiNativeRuntimeOwner> owner;
        VkDevice device = VK_NULL_HANDLE;
        std::array<unsigned, static_cast<unsigned>(Dispatch::Count)> calls{};
        unsigned createdBuffers = 0u;
        bool allOwned = true;
    };

    inline static Observation* observation_ = nullptr;

    static VKAPI_ATTR VkResult VKAPI_CALL createBuffer(VkDevice device,
        const VkBufferCreateInfo* info,
        const VkAllocationCallbacks* callbacks,
        VkBuffer* buffer)
    {
        const auto result = vkCreateBuffer(device, info, callbacks, buffer);
        if (result == VK_SUCCESS && observation_)
            ++observation_->createdBuffers;
        return result;
    }

    template <Dispatch Kind, class Handle, auto Function>
    static VKAPI_ATTR void VKAPI_CALL destroy(
        VkDevice device, Handle handle, const VkAllocationCallbacks* callbacks)
    {
        if (observation_) {
            ++observation_->calls[static_cast<unsigned>(Kind)];
            observation_->allOwned &= device == observation_->device
                && !observation_->runtime.expired() && !observation_->owner.expired();
        }
        Function(device, handle, callbacks);
    }

    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name)
    {
        if (std::strcmp(name, "vkCreateBuffer") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(createBuffer);
        if (std::strcmp(name, "vkDestroyBuffer") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(
                destroy<Dispatch::Buffer, VkBuffer, vkDestroyBuffer>);
        if (std::strcmp(name, "vkDestroyImage") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(
                destroy<Dispatch::Image, VkImage, vkDestroyImage>);
        if (std::strcmp(name, "vkDestroyDescriptorPool") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(
                destroy<Dispatch::Pool, VkDescriptorPool, vkDestroyDescriptorPool>);
        if (std::strcmp(name, "vkDestroyDescriptorSetLayout") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(
                destroy<Dispatch::Layout, VkDescriptorSetLayout, vkDestroyDescriptorSetLayout>);
        if (std::strcmp(name, "vkDestroySampler") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(
                destroy<Dispatch::Sampler, VkSampler, vkDestroySampler>);
        if (std::strcmp(name, "vkFreeMemory") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(
                destroy<Dispatch::Memory, VkDeviceMemory, vkFreeMemory>);
        return vkGetDeviceProcAddr(device, name);
    }

public:
    enum class Resource { Buffer, Image, Descriptor, Sampler };

    static int runAllocationRollback()
    {
        using namespace rhi_vulkan;
        auto native = core::makeUnique<VulkanHeadlessRuntime>(false, resolve);
        if (!native->runtime() || !native->runtime()->valid())
            return 77;
        Observation observation;
        observation.runtime = native->runtime();
        observation.owner = native->nativeOwner();
        observation.device = native->device();
        observation_ = &observation;
        {
            VulkanRhiAllocator allocator({"quota", 64u * 1024u * 1024u}, native->runtime());
            auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
            std::uint64_t tokenBytes = 0u;
            {
                core::CpuMemoryQuotaScope scope(quota);
                const auto allocation =
                    allocator.allocate({"quota", 256u, rhi::MemoryUsage::CpuToGpu});
                assert(allocation.success && allocation.releaseToken);
                tokenBytes = quota->liveBytes();
            }
            assert(tokenBytes && quota->liveBytes() == 0u);
            // Permit the real charge token and fail precisely at the next CPU
            // payload allocation, after the driver has created the VkBuffer.
            quota->setLimitBytes(tokenBytes);
            bool rejected = false;
            try {
                core::CpuMemoryQuotaScope scope(quota);
                const auto allocation = allocator.createBuffer({"quota",
                                                                   256u,
                                                                   rhi::ResourceUsage::CopySrc,
                                                                   rhi::MemoryUsage::CpuToGpu,
                                                                   rhi::ResourceState::Undefined},
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
                (void)allocation;
            } catch (const std::bad_alloc&) {
                rejected = true;
            }
            const auto destroyed = observation.calls[static_cast<unsigned>(Dispatch::Buffer)];
            std::printf(
                "allocation rollback: rejected=%d buffers=%u destroyed=%u CPU=%llu charge=%llu\n",
                rejected,
                observation.createdBuffers,
                destroyed,
                static_cast<unsigned long long>(quota->liveBytes()),
                static_cast<unsigned long long>(allocator.memoryStats().allocatedBytes));
            std::fflush(stdout);
            assert(rejected && observation.createdBuffers == 1u && destroyed == 1u);
            assert(quota->liveBytes() == 0u && allocator.memoryStats().allocatedBytes == 0u);
            assert(allocator.memoryStats().liveResourceCount == 0u);
        }
        assert(observation.allOwned
            && observation.calls[static_cast<unsigned>(Dispatch::Memory)] > 0u);
        observation_ = nullptr;
        return 0;
    }

    static int runRegistered(Resource kind)
    {
        using namespace rhi_vulkan;
        assert(kind == Resource::Buffer || kind == Resource::Image);
        auto native = core::makeUnique<VulkanHeadlessRuntime>(false, resolve);
        if (!native->runtime() || !native->runtime()->valid())
            return 77;
        Observation observation;
        observation.runtime = native->runtime();
        observation.owner = native->nativeOwner();
        observation.device = native->device();
        observation_ = &observation;
        auto device = VulkanHeapOwnerFixture::makeDevice(native->runtime(), false);
        auto foreign = VulkanHeapOwnerFixture::makeDevice(native->runtime(), false);
        assert(device && foreign);
        const auto prepare = [](rhi::GpuDevice& source) {
            constexpr std::uint64_t bytes = 65536u;
            auto domain = core::makeShared<core::CpuMemoryQuota>(bytes);
            auto registry = core::makeShared<core::CpuMemoryQuota>(bytes);
            assert(!source.prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            assert(!source.prepareOriginalResourceRegistration(
                {1u, true, 1u, 1u, 0u, 7u, bytes}, registry));
        };
        prepare(*device);
        prepare(*foreign);
        const auto baseline = device->memoryStats();
        for (unsigned phase = 0u; phase < 2u; ++phase) {
            rhi::OriginalResourceRegistrationTicket ticket;
            std::shared_ptr<VulkanRhiNativeResourcePayload> payload;
            const auto capture = [&](auto resource) {
                assert(resource && !device->validateOriginalResourceRegistration(*resource));
                ticket = resource->originalResourceRegistration();
                const auto* foreignDiagnostic =
                    foreign->validateOriginalResourceRegistration(*resource);
                assert(foreignDiagnostic
                    && std::strcmp(foreignDiagnostic, "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE")
                        == 0);
                payload = vulkanRhiNativePayload(*resource);
                assert(payload && payload->allocationReleaseToken);
                assert(payload->backendVulkanDeviceHandle() == native->device());
            };
            if (kind == Resource::Buffer) {
                capture(device->createBuffer({"registered",
                    256u,
                    rhi::ResourceUsage::CopySrc,
                    rhi::MemoryUsage::CpuToGpu,
                    rhi::ResourceState::Undefined}));
            } else {
                rhi::TextureDesc desc;
                desc.extent = {4u, 4u, 1u};
                desc.format = rhi::TextureFormat::Rgba8Unorm;
                desc.usage = rhi::TextureUsage::Sampled;
                capture(device->createTexture(desc));
            }
            // capture's unique wrapper has died; a weak source receipt cannot
            // keep it alive or authorize retirement of the still-live payload.
            assert(ticket.diagnostic()
                && std::strcmp(ticket.diagnostic(), "RHI-ORIGINAL-REGISTRATION-EXPIRED") == 0);
            const auto retained = device->memoryStats();
            assert(retained.allocatedBytes > baseline.allocatedBytes);
            assert(retained.liveResourceCount == baseline.liveResourceCount + 1u);
            std::weak_ptr<void> charge = payload->allocationReleaseToken;
            auto last = payload;
            payload.reset();
            assert(!charge.expired());
            const auto dispatch = kind == Resource::Buffer ? Dispatch::Buffer : Dispatch::Image;
            assert(observation.calls[static_cast<unsigned>(dispatch)] == phase);
            VkMemoryRequirements actual{};
            if (kind == Resource::Buffer)
                vkGetBufferMemoryRequirements(
                    observation.device, last->backendVulkanBufferHandle(), &actual);
            else
                vkGetImageMemoryRequirements(
                    observation.device, last->backendVulkanImageHandle(), &actual);
            assert(actual.size && retained.allocatedBytes - baseline.allocatedBytes == actual.size);
            if (phase == 0u) {
                last.reset();
                assert(charge.expired());
                assert(device->memoryStats().allocatedBytes == baseline.allocatedBytes);
                assert(device->memoryStats().liveResourceCount == baseline.liveResourceCount);
                assert(observation.calls[static_cast<unsigned>(dispatch)] == 1u);
            } else {
                assert(inspectVulkanRhiNativeResourceStats(*device).nativeQueueSubmitCount == 0u);
                device->retireOriginalDeviceLifetime();
                foreign.reset();
                device.reset();
                native.reset();
                assert(!charge.expired() && !observation.runtime.expired()
                    && !observation.owner.expired());
                last.reset();
                assert(charge.expired() && observation.runtime.expired()
                    && observation.owner.expired());
                assert(observation.calls[static_cast<unsigned>(dispatch)] == 2u);
            }
        }
        assert(observation.allOwned);
        observation_ = nullptr;
        std::printf(
            "registered allocation owner: kind=%u; weak source expired; exact charge retained to native destruction\n",
            static_cast<unsigned>(kind));
        return 0;
    }

    static int run(Resource kind)
    {
        using namespace rhi_vulkan;
        auto native = core::makeUnique<VulkanHeadlessRuntime>(false, resolve);
        if (!native->runtime() || !native->runtime()->valid())
            return 77;
        auto rescue = native->runtime();
        Observation observation;
        observation.runtime = rescue;
        observation.owner = native->nativeOwner();
        observation.device = native->device();
        observation_ = &observation;
        std::shared_ptr<VulkanRhiNativeResourcePayload> payload;
        {
            VulkanRhiAllocator allocator({"resource owner", 64u * 1024u * 1024u}, rescue);
            VulkanRhiNativeResourceAllocator resources(rescue, &allocator);
            VulkanRhiNativeResourceResult result;
            switch (kind) {
            case Resource::Buffer:
                result = resources.createBuffer({"owner buffer",
                    256u,
                    rhi::ResourceUsage::CopyDst,
                    rhi::MemoryUsage::GpuToCpu,
                    rhi::ResourceState::Undefined});
                break;
            case Resource::Image: {
                rhi::TextureDesc desc;
                desc.label = "owner image";
                desc.extent = {4u, 4u, 1u};
                desc.format = rhi::TextureFormat::Rgba8Unorm;
                desc.usage = rhi::TextureUsage::Sampled;
                result = resources.createTexture(desc);
                break;
            }
            case Resource::Descriptor: {
                rhi::DescriptorLayoutDesc layout;
                layout.name = "owner descriptor";
                layout.bindings.push_back(
                    {0u, rhi::DescriptorType::UniformBuffer, rhi::ShaderStage::Vertex, 1u});
                result = resources.createDescriptorSet(layout);
                break;
            }
            case Resource::Sampler:
                result = resources.createSampler(rhi::SamplerDesc{});
                break;
            }
            if (!result.success)
                std::fprintf(
                    stderr, "resource-owner creation failed: %s\n", result.diagnostics.c_str());
            assert(result.success && result.payload);
            payload = std::move(result.payload);
        }
        native.reset();
        // The rescue is the only test-owned runtime reference. Do not lock a
        // weak pointer here: that would mask the missing production owner.
        const bool retained = rescue.use_count() > 1;
        if (retained)
            rescue.reset();
        const bool aliveWithoutCreator =
            retained && !observation.runtime.expired() && !observation.owner.expired();
        bool nativeUse = payload->backendVulkanDeviceHandle() == observation.device;
        VkMemoryRequirements requirements{};
        switch (kind) {
        case Resource::Buffer:
            vkGetBufferMemoryRequirements(
                observation.device, payload->backendVulkanBufferHandle(), &requirements);
            nativeUse &= requirements.size >= 256u;
            break;
        case Resource::Image:
            vkGetImageMemoryRequirements(
                observation.device, payload->backendVulkanImageHandle(), &requirements);
            nativeUse &= requirements.size != 0u;
            break;
        case Resource::Descriptor:
            nativeUse &= payload->backendVulkanDescriptorSetHandle() != VK_NULL_HANDLE
                && payload->backendVulkanDescriptorSetLayoutHandle() != VK_NULL_HANDLE;
            break;
        case Resource::Sampler:
            nativeUse &= payload->backendVulkanSamplerHandle() != VK_NULL_HANDLE;
            break;
        }
        observation.calls.fill(0u);
        payload.reset();
        // On the red path the borrowed wrapper and actual device stay valid
        // through every native destructor; only now release the rescue.
        rescue.reset();
        const bool released = observation.runtime.expired() && observation.owner.expired();
        const auto calls = [&](Dispatch dispatch) {
            return observation.calls[static_cast<unsigned>(dispatch)];
        };
        bool destroyed = false;
        switch (kind) {
        case Resource::Buffer:
            destroyed = calls(Dispatch::Buffer) == 1u && calls(Dispatch::Memory) > 0u;
            break;
        case Resource::Image:
            destroyed = calls(Dispatch::Image) == 1u && calls(Dispatch::Memory) > 0u;
            break;
        case Resource::Descriptor:
            destroyed = calls(Dispatch::Pool) == 1u && calls(Dispatch::Layout) == 1u;
            break;
        case Resource::Sampler:
            destroyed = calls(Dispatch::Sampler) == 1u;
            break;
        }
        observation_ = nullptr;
        std::printf(
            "resource-owner: kind=%u retained=%d independent=%d native-use=%d owned-destroy=%d destroyed=%d memory-frees=%u released=%d\n",
            static_cast<unsigned>(kind),
            retained,
            aliveWithoutCreator,
            nativeUse,
            observation.allOwned,
            destroyed,
            calls(Dispatch::Memory),
            released);
        std::fflush(stdout);
        assert(retained && aliveWithoutCreator && nativeUse);
        assert(observation.allOwned && destroyed && released);
        return 0;
    }
};

} // namespace arp::tests
