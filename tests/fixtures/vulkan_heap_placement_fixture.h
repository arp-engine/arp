#pragma once

#include "core/unsigned_range.h"
#include "rhi/backends/vulkan/vulkan_heap_placement.h"
#include "tests/fixtures/cpu_quota_factory_fixture.h"
#include "tests/fixtures/vulkan_heap_owner_fixture.h"

#ifdef ARP_TEST_POOL_HEAP_LEASE
#include "renderer/render_graph/frame_graph_resource_pool.h"
#endif

#include <array>
#include <cstring>
#include <limits>
#include <string_view>

namespace arp::tests {

// All successful native calls use the real driver. The bind interceptor also
// protects negative tests: an unproven memory/device/range never reaches it.
class VulkanHeapPlacementFixture final {
    struct Memory {
        VkDeviceMemory handle = VK_NULL_HANDLE;
        std::uint64_t size = 0;
        std::uint32_t type = 0;
        bool live = false;
    };

    struct Image {
        VkImage handle = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        bool live = false;
    };

    struct Observation {
        VkDevice device = VK_NULL_HANDLE;
        std::weak_ptr<rhi_vulkan::VulkanRhiRuntime> runtime;
        std::weak_ptr<rhi_vulkan::VulkanRhiNativeRuntimeOwner> owner;
        std::array<Memory, 32> memories{};
        std::array<Image, 64> images{};
        unsigned allocated = 0, freed = 0, created = 0, destroyed = 0, binds = 0;
        unsigned unsafeBinds = 0, allocationAttempts = 0, imageAttempts = 0;
        bool failAllocate = false, failCreate = false, failBind = false;
        bool dedicated = false, wrongType = false, lost = false;
        const char* missing = nullptr;
        std::shared_ptr<core::CpuMemoryQuota> sealAfterAllocate, sealAfterBind;
    };

    inline static Observation* observation_ = nullptr;

    static VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice device,
        const VkMemoryAllocateInfo* info,
        const VkAllocationCallbacks* callbacks,
        VkDeviceMemory* memory)
    {
        auto& o = *observation_;
        assert(device == o.device);
        ++o.allocationAttempts;
        if (o.failAllocate) {
            *memory = rhi_vulkan::syntheticHandle<VkDeviceMemory>(0x123u);
            return o.lost ? VK_ERROR_DEVICE_LOST : VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        const auto result = vkAllocateMemory(device, info, callbacks, memory);
        if (result == VK_SUCCESS) {
            assert(o.allocated < o.memories.size());
            o.memories[o.allocated++] = {
                *memory, info->allocationSize, info->memoryTypeIndex, true};
            if (o.sealAfterAllocate)
                o.sealAfterAllocate->setLimitBytes(o.sealAfterAllocate->liveBytes());
        }
        return result;
    }

    static VKAPI_ATTR void VKAPI_CALL free(
        VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* callbacks)
    {
        auto& o = *observation_;
        assert(device == o.device && !o.runtime.expired() && !o.owner.expired());
        Memory* record = nullptr;
        for (auto& entry : o.memories)
            if (entry.live && entry.handle == memory)
                record = &entry;
        assert(record); // Never pass garbage failure outputs to the driver.
        for (const auto& image : o.images)
            assert(!image.live || image.memory != memory);
        record->live = false;
        ++o.freed;
        vkFreeMemory(device, memory, callbacks);
    }

    static VKAPI_ATTR VkResult VKAPI_CALL create(VkDevice device,
        const VkImageCreateInfo* info,
        const VkAllocationCallbacks* callbacks,
        VkImage* image)
    {
        auto& o = *observation_;
        assert(device == o.device && (info->flags & VK_IMAGE_CREATE_ALIAS_BIT));
        ++o.imageAttempts;
        if (o.failCreate) {
            *image = rhi_vulkan::syntheticHandle<VkImage>(0x123u);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        const auto result = vkCreateImage(device, info, callbacks, image);
        if (result == VK_SUCCESS) {
            assert(o.created < o.images.size());
            o.images[o.created++] = {*image, VK_NULL_HANDLE, true};
        }
        return result;
    }

    static VKAPI_ATTR void VKAPI_CALL destroy(
        VkDevice device, VkImage image, const VkAllocationCallbacks* callbacks)
    {
        auto& o = *observation_;
        assert(device == o.device && !o.runtime.expired() && !o.owner.expired());
        Image* record = nullptr;
        for (auto& entry : o.images)
            if (entry.live && entry.handle == image)
                record = &entry;
        assert(record);
        record->live = false;
        ++o.destroyed;
        vkDestroyImage(device, image, callbacks);
    }

    static VKAPI_ATTR void VKAPI_CALL requirements(
        VkDevice device, const VkImageMemoryRequirementsInfo2* info, VkMemoryRequirements2* output)
    {
        vkGetImageMemoryRequirements2(device, info, output);
        auto& o = *observation_;
        assert(device == o.device);
        auto* dedicated = static_cast<VkMemoryDedicatedRequirements*>(output->pNext);
        assert(dedicated && dedicated->sType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS);
        if (o.dedicated)
            dedicated->requiresDedicatedAllocation = VK_TRUE;
        if (o.wrongType)
            output->memoryRequirements.memoryTypeBits = o.memories[0].type == 0u ? 2u : 1u;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL bind(
        VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset)
    {
        auto& o = *observation_;
        ++o.binds;
        Image* imageRecord = nullptr;
        Memory* memoryRecord = nullptr;
        for (auto& entry : o.images)
            if (entry.live && entry.handle == image)
                imageRecord = &entry;
        for (auto& entry : o.memories)
            if (entry.live && entry.handle == memory)
                memoryRecord = &entry;
        VkMemoryRequirements actual{};
        if (device == o.device && imageRecord)
            vkGetImageMemoryRequirements(device, image, &actual);
        if (device != o.device || !imageRecord || !memoryRecord || !actual.alignment
            || offset % actual.alignment || offset > memoryRecord->size
            || actual.size > memoryRecord->size - offset
            || !(actual.memoryTypeBits & (std::uint32_t{1} << memoryRecord->type))) {
            ++o.unsafeBinds;
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        if (o.failBind)
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        const auto result = vkBindImageMemory(device, image, memory, offset);
        if (result == VK_SUCCESS) {
            imageRecord->memory = memory;
            if (o.sealAfterBind)
                o.sealAfterBind->setLimitBytes(o.sealAfterBind->liveBytes());
        }
        return result;
    }

    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name)
    {
        if (observation_ && observation_->missing && std::strcmp(name, observation_->missing) == 0)
            return nullptr;
        if (std::strcmp(name, "vkAllocateMemory") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(allocate);
        if (std::strcmp(name, "vkFreeMemory") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(free);
        if (std::strcmp(name, "vkCreateImage") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(create);
        if (std::strcmp(name, "vkDestroyImage") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(destroy);
        if (std::strcmp(name, "vkGetImageMemoryRequirements2") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(requirements);
        if (std::strcmp(name, "vkBindImageMemory") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(bind);
        return vkGetDeviceProcAddr(device, name);
    }

#ifdef ARP_TEST_POOL_HEAP_LEASE
    static void poolLeaseCase(core::UniquePtr<rhi::GpuDevice>& device,
        core::UniquePtr<VulkanHeadlessRuntime>& native,
        std::shared_ptr<rhi_vulkan::VulkanRhiRuntime>& runtime,
        Observation& o,
        const rhi::TextureDesc& desc,
        const rhi::TexturePlacementRequirements& req,
        std::uint64_t stride,
        bool failure,
        bool partitions)
    {
        using Pool = render_graph::FrameGraphResourcePool;
        constexpr std::uint64_t cpuBytes = 65536u;
        auto domain = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto registrations = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto metadata = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, domain));
        rhi::OriginalResourceRegistrationBudget registration{1u, true, 0u, 4u, 0u, 19u, cpuBytes};
        registration.heaps = {1u, true, 4u};
        assert(!device->prepareOriginalResourceRegistration(registration, registrations));
        rhi::semantics::FrameGraphPoolAllocationBudget b;
        b.contractVersion = 1u;
        b.prototypeOptIn = true;
        b.maxCpuMetadataBytes = cpuBytes;
        b.heapLeases = {1u, true, 1u, stride * 2u};
        if (partitions) {
            b.partitions = {1u, true, 1u, {}};
            b.partitions.capacity.aliasHeapCount = 1u;
            b.partitions.capacity.cpuMetadataBytes = cpuBytes;
        }
        auto pool = core::makeUnique<Pool>(*device);
        assert(!pool->prepareAllocationAccounting(b, metadata));
        const auto baseline = metadata->liveBytes();
        const auto checkPartitions = [&](std::uint64_t heapOwners,
                                         std::uint64_t externalBytes = 0u) {
            if (!partitions)
                return;
            const auto report = pool->allocationAccountingReport();
            assert(!report.partitionDiagnostic && report.partitionContractVersion == 1u);
            assert(report.partitions.inUse.aliasHeapCount == heapOwners);
            assert(!report.partitions.reserved.aliasHeapCount
                && !report.partitions.pending.aliasHeapCount
                && !report.partitions.cached.aliasHeapCount);
            assert(!report.partitions.inUse.logicalAliasBytes);
            assert(report.allocatedCpuMetadataBytes + externalBytes == metadata->liveBytes());
        };
        checkPartitions(0u);
        const auto acquire = [&] {
            return pool->acquireMemoryHeap(
                stride * 2u, req.alignmentBytes, rhi::MemoryUsage::Transient);
        };
        const auto rejectCapacity = [&] {
            bool rejected = false;
            try {
                acquire();
            } catch (const core::DiagnosticError& e) {
                rejected = std::string_view(e.what()) == "RG-POOL-ALLOCATION-RESOURCE-CAPACITY";
            }
            assert(rejected);
        };
        if (failure) {
            const auto attempt = [&]() -> core::UniquePtr<rhi::GpuMemoryHeap> {
                try {
                    return acquire();
                } catch (const std::bad_alloc&) {
                    return {};
                } catch (const core::DiagnosticError& e) {
                    assert(std::string_view(e.what()) == "RG-POOL-HEAP-FACTORY-FAILED");
                    return {};
                }
            };
            test::CpuQuotaFactoryFixture pressure(metadata);
            const auto exact = pressure.measureSingleUse([] {}, attempt);
            for (std::uint64_t available = 0u; available <= exact; ++available) {
                {
                    core::CpuMemoryQuotaLease full(metadata, cpuBytes - baseline - available);
                    assert(full.charged());
                    auto heap = attempt();
                    assert(bool(heap) == (available == exact));
                    assert(pool->allocationAccountingReport().heapLeases == (heap ? 1u : 0u));
                    checkPartitions(heap ? 1u : 0u, cpuBytes - baseline - available);
                }
                assert(metadata->liveBytes() == baseline
                    && !pool->allocationAccountingReport().heapLeases);
                assert(o.allocated == 1 && !o.freed
                    && device->memoryStats().allocatedBytes == stride * 2u);
            }
            auto factoryQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            const auto factoryAttempt = [&]() -> core::UniquePtr<rhi::GpuMemoryHeap> {
                core::CpuMemoryQuotaScope scope(factoryQuota);
                return attempt();
            };
            test::CpuQuotaFactoryFixture factoryPressure(factoryQuota);
            const auto factoryExact = factoryPressure.measureSingleUse([] {}, factoryAttempt);
            factoryPressure.verify(factoryExact, factoryAttempt);
            assert(!factoryQuota->liveBytes() && metadata->liveBytes() == baseline);
            assert(o.allocationAttempts == 1 && !pool->allocationAccountingReport().heapLeases);
            std::printf(
                "pool heap native quotas: receipt=%llu factory=%llu; physical cache retained and charged\n",
                static_cast<unsigned long long>(exact),
                static_cast<unsigned long long>(factoryExact));
            return;
        }
        auto foreignMetadata = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto foreign = core::makeUnique<Pool>(*device);
        assert(!foreign->prepareAllocationAccounting(b, foreignMetadata));
        auto heap = acquire();
        const auto ticket = heap->originalResourceRegistration();
        const auto generation = ticket.generation();
        assert(heap->originalPoolAllocationCharged() && !pool->validateAllocationAccounting(*heap));
        assert(foreign->validateAllocationAccounting(*heap));
        const auto attempts = o.allocationAttempts;
        rejectCapacity();
        assert(o.allocationAttempts == attempts);
        auto first = device->createPlacedTexture(desc, *heap, 0u);
        auto second = device->createPlacedTexture(desc, *heap, stride);
        assert(first && second && !first->originalPoolAllocationCharged());
        auto firstPayload = rhi_vulkan::vulkanRhiNativePayload(*first);
        auto secondPayload = rhi_vulkan::vulkanRhiNativePayload(*second);
        first.reset();
        second.reset();
        heap.reset();
        assert(ticket.diagnostic() && pool->allocationAccountingReport().heapLeases == 1u);
        checkPartitions(1u);
        assert(
            pool->prepareAllocationAccounting(b, core::makeShared<core::CpuMemoryQuota>(cpuBytes)));
        rejectCapacity();
        firstPayload.reset();
        assert(pool->allocationAccountingReport().heapLeases == 1u);
        secondPayload.reset();
        assert(!pool->allocationAccountingReport().heapLeases && !o.freed && o.allocated == 1);
        checkPartitions(0u);
        heap = pool->acquireMemoryHeap(stride, req.alignmentBytes, rhi::MemoryUsage::Transient);
        assert(heap && heap->originalResourceRegistration().generation() > generation);
        assert(o.allocationAttempts == attempts
            && pool->allocationAccountingReport().logicalHeapReservationBytes == stride);
        assert(device->memoryStats().allocatedBytes == stride * 2u);
        auto foreignHeap =
            foreign->acquireMemoryHeap(stride, req.alignmentBytes, rhi::MemoryUsage::Transient);
        assert(foreignHeap && o.allocated == 2);
        assert(pool->validateAllocationAccounting(*foreignHeap));
        foreignHeap.reset();
        assert(!foreign->allocationAccountingReport().heapLeases
            && pool->allocationAccountingReport().heapLeases == 1u);
        auto texture = device->createPlacedTexture(desc, *heap, 0u);
        assert(texture);
        auto payload = rhi_vulkan::vulkanRhiNativePayload(*texture);
        texture.reset();
        {
            Pool legacy(*device);
            render_graph::FrameGraphAliasedResourceGroup group;
            group.heaps.push_back(std::move(heap));
            bool rejected = false;
            try {
                legacy.releaseDeferredAliasedGroup(std::move(group), {}, {});
            } catch (const core::DiagnosticError& e) {
                rejected = std::string_view(e.what()) == "RG-POOL-ALLOCATION-RETURN-UNAVAILABLE";
            }
            assert(rejected && pool->allocationAccountingReport().heapLeases == 1u);
        }
        foreign.reset();
        pool.reset();
        device.reset();
        native.reset();
        runtime.reset();
        assert(metadata->liveBytes() && !o.runtime.expired() && !o.owner.expired());
        VkMemoryRequirements actual{};
        vkGetImageMemoryRequirements(o.device, payload->backendVulkanImageHandle(), &actual);
        assert(actual.size == req.sizeBytes && actual.alignment == req.alignmentBytes);
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation<1>({metadata}, [&]() noexcept {
            payload.reset();
            return static_cast<const char*>(nullptr);
        }));
        assert(!metadata->liveBytes() && o.runtime.expired() && o.owner.expired());
    }
#endif

public:
    static int run(std::string_view mode)
    {
        Observation o;
        observation_ = &o;
        auto native = core::makeUnique<VulkanHeadlessRuntime>(false, resolve);
        if (!native->runtime() || !native->runtime()->valid()) {
            observation_ = nullptr;
            return 77;
        }
        auto runtime = native->runtime();
        o.device = native->device();
        o.runtime = runtime;
        o.owner = native->nativeOwner();
        auto device = VulkanHeapOwnerFixture::makeDevice(runtime);
        assert(device && device->capabilities().features.transientAliasing);
        rhi::TextureDesc desc;
        desc.extent = {4u, 4u, 1u};
        desc.format = rhi::TextureFormat::Rgba8Unorm;
        desc.usage = rhi::TextureUsage::Sampled;
        const auto req = device->queryTexturePlacementRequirements(desc);
        assert(req.sizeBytes && req.alignmentBytes && o.created == o.destroyed);
        std::uint64_t stride = 0u;
        assert(core::roundUpUnsigned(req.sizeBytes, req.alignmentBytes, stride));
        core::UniquePtr<rhi::GpuMemoryHeap> heap;
#ifdef ARP_TEST_POOL_HEAP_LEASE
        if (mode == "native-pool-lease" || mode == "native-pool-lease-failure"
            || mode == "native-pool-partitions" || mode == "native-pool-partitions-failure") {
            poolLeaseCase(device,
                native,
                runtime,
                o,
                desc,
                req,
                stride,
                mode == "native-pool-lease-failure" || mode == "native-pool-partitions-failure",
                mode == "native-pool-partitions" || mode == "native-pool-partitions-failure");
        } else
#endif
        {
            std::shared_ptr<core::CpuMemoryQuota> originalMetadata;
            if (mode == "native-provenance" || mode == "native-provenance-publication") {
                auto domain = core::makeShared<core::CpuMemoryQuota>(65536u);
                originalMetadata = core::makeShared<core::CpuMemoryQuota>(65536u);
                assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, 65536u}, domain));
                rhi::OriginalResourceRegistrationBudget b{1u, true, 0u, 2u, 0u, 11u, 65536u};
                b.heaps = {1u, true, 1u};
                assert(!device->prepareOriginalResourceRegistration(b, originalMetadata));
            }
            const auto originalBaseline = originalMetadata ? originalMetadata->liveBytes() : 0u;
            if (mode == "native-provenance-publication")
                o.sealAfterAllocate = originalMetadata;
            heap = device->createMemoryHeap(
                stride * 2u, req.alignmentBytes, rhi::MemoryUsage::Transient);
            if (mode == "native-provenance-publication") {
                assert(!heap && o.allocated == 1 && o.freed == 0);
                assert(device->lastMemoryHeapCreationDiagnostic()
                    == "RHI-ORIGINAL-REGISTRATION-QUOTA");
                assert(originalMetadata->liveBytes() == originalBaseline);
                // Backend cache accounting remains charged for its real allocation.
                // Rejected logical publication is not physical deallocation.
                assert(device->memoryStats().allocatedBytes == stride * 2u);
                o.sealAfterAllocate.reset();
                originalMetadata->setLimitBytes(65536u);
                heap = device->createMemoryHeap(
                    stride * 2u, req.alignmentBytes, rhi::MemoryUsage::Transient);
                assert(heap && o.allocationAttempts == 1);
            }
            assert(heap && o.allocated == 1 && device->memoryStats().allocatedBytes == stride * 2u);
            if (mode == "native-provenance-publication") {
                assert(!device->validateOriginalResourceRegistration(*heap));
                auto texture = device->createPlacedTexture(desc, *heap, 0u);
                assert(texture && o.binds == 1 && !o.unsafeBinds);
                texture.reset();
                heap.reset();
                assert(originalMetadata->liveBytes() == originalBaseline);
            } else if (mode == "native-provenance") {
                const auto ticket = heap->originalResourceRegistration();
                assert(!device->validateOriginalResourceRegistration(*heap));
                const auto attempts = o.allocationAttempts;
                const auto images = o.imageAttempts;
                auto foreign = VulkanHeapOwnerFixture::makeDevice(runtime);
                assert(!foreign->createPlacedTexture(desc, *heap, 0u));
                assert(!device->createMemoryHeap(
                    stride, req.alignmentBytes, rhi::MemoryUsage::Transient));
                assert(o.allocationAttempts == attempts && o.imageAttempts == images);
                auto texture = device->createPlacedTexture(desc, *heap, 0u);
                assert(texture && o.binds == 1 && o.unsafeBinds == 0);
                auto payload = rhi_vulkan::vulkanRhiNativePayload(*texture);
                texture.reset();
                heap.reset();
                assert(ticket.diagnostic() && o.freed == 0);
                // The cached physical allocation cannot be reused while the image
                // still owns it. Provenance loss does not certify completion.
                auto next = device->createMemoryHeap(
                    stride * 2u, req.alignmentBytes, rhi::MemoryUsage::Transient);
                assert(next && o.allocated == 2);
                const auto generation = next->originalResourceRegistration().generation();
                assert(generation && !device->validateOriginalResourceRegistration(*next));
                payload.reset();
                next.reset();
                auto reused = device->createMemoryHeap(
                    stride * 2u, req.alignmentBytes, rhi::MemoryUsage::Transient);
                assert(reused && o.allocated == 2);
                assert(reused->originalResourceRegistration().generation() > generation);
                reused.reset();
                const auto baseline = originalMetadata->liveBytes();
                {
                    core::CpuMemoryQuotaLease full(originalMetadata, 65536u - baseline);
                    assert(full.charged());
                    assert(!device->createMemoryHeap(
                        stride, req.alignmentBytes, rhi::MemoryUsage::Transient));
                    assert(o.allocationAttempts == attempts + 1u);
                }
                assert(originalMetadata->liveBytes() == baseline);
                foreign.reset();
            } else if (mode == "native-positive") {
                auto first = device->createPlacedTexture(desc, *heap, 0u);
                auto second = device->createPlacedTexture(desc, *heap, stride);
                assert(first && second && o.binds == 2 && o.unsafeBinds == 0);
                first.reset();
                second.reset();
                heap.reset();
                auto reused = device->createMemoryHeap(
                    stride * 2u, req.alignmentBytes, rhi::MemoryUsage::GpuOnly);
                assert(reused && o.allocated == 1);
                auto texture = device->createPlacedTexture(desc, *reused, stride);
                assert(texture);
                auto payload = rhi_vulkan::vulkanRhiNativePayload(*texture);
                texture.reset();
                reused.reset();
                device.reset();
                native.reset();
                runtime.reset();
                assert(!o.runtime.expired() && !o.owner.expired() && o.freed == 0);
                VkMemoryRequirements actual{};
                vkGetImageMemoryRequirements(
                    o.device, payload->backendVulkanImageHandle(), &actual);
                assert(actual.size == req.sizeBytes && actual.alignment == req.alignmentBytes);
                payload.reset();
                assert(o.runtime.expired() && o.owner.expired());
            } else if (mode == "native-negative") {
                const auto beforeCreates = o.imageAttempts, beforeBinds = o.binds;
                for (const auto offset : {std::uint64_t{1},
                         heap->sizeBytes(),
                         std::numeric_limits<std::uint64_t>::max()})
                    assert(!device->createPlacedTexture(desc, *heap, offset));
                VulkanHeadlessRuntime other;
                assert(other.runtime() && other.runtime()->valid());
                auto otherDevice = VulkanHeapOwnerFixture::makeDevice(other.runtime());
                auto foreign = otherDevice->createMemoryHeap(
                    stride * 2u, req.alignmentBytes, rhi::MemoryUsage::Transient);
                assert(foreign && !device->createPlacedTexture(desc, *foreign, 0u));
                assert(o.imageAttempts == beforeCreates && o.binds == beforeBinds);
                auto tooLarge = desc;
                tooLarge.extent = {64u, 64u, 1u};
                assert(!device->createPlacedTexture(tooLarge, *heap, 0u));
                o.dedicated = true;
                assert(!device->createPlacedTexture(desc, *heap, 0u));
                assert(!device->queryTexturePlacementRequirements(desc).sizeBytes);
                o.dedicated = false;
                o.wrongType = true;
                assert(!device->createPlacedTexture(desc, *heap, 0u));
                assert(!device->queryTexturePlacementRequirements(desc).sizeBytes);
                o.wrongType = false;
                assert(o.binds == beforeBinds && o.created == o.destroyed);
            } else if (mode == "native-publication-rollback") {
                auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
                const auto baseline = device->memoryStats();
                const auto factory = [&]() -> core::UniquePtr<rhi::GpuTexture> {
                    core::CpuMemoryQuotaScope scope(quota);
                    return device->createPlacedTexture(desc, *heap, 0u);
                };
                test::CpuQuotaFactoryFixture pressure(quota);
                const auto exact = pressure.measureSingleUse([] {}, factory);
                pressure.verify(exact, factory);
                assert(quota->liveBytes() == 0u && o.created == o.destroyed);
                assert(device->memoryStats().allocatedBytes == baseline.allocatedBytes);
                assert(device->memoryStats().liveResourceCount == baseline.liveResourceCount);
                std::printf(
                    "placed publication rollback: exact CPU=%llu retained=%llu images=%u/%u\n",
                    static_cast<unsigned long long>(exact),
                    static_cast<unsigned long long>(quota->liveBytes()),
                    o.created,
                    o.destroyed);
            } else if (mode == "native-faults") {
                o.failAllocate = true;
                assert(!device->createMemoryHeap(
                    stride * 3u, req.alignmentBytes, rhi::MemoryUsage::Transient));
                assert(device->lastMemoryHeapCreationDiagnostic()
                    == "RHI-VULKAN-HEAP-NATIVE-ALLOCATION-FAILED");
                assert(o.allocated == 1 && o.freed == 0
                    && device->memoryStats().allocatedBytes == stride * 2u);
                o.failAllocate = false;
                o.failCreate = true;
                assert(!device->queryTexturePlacementRequirements(desc).sizeBytes);
                assert(!device->createPlacedTexture(desc, *heap, 0u));
                assert(o.created == o.destroyed);
                o.failCreate = false;
                o.failBind = true;
                assert(!device->createPlacedTexture(desc, *heap, 0u));
                assert(o.created == o.destroyed);
                o.failBind = false;
                auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
                const auto imagesBefore = o.imageAttempts, allocationsBefore = o.allocationAttempts;
                {
                    core::CpuMemoryQuotaScope scope(quota);
                    assert(!device->createMemoryHeap(
                        stride * 3u, req.alignmentBytes, rhi::MemoryUsage::Transient));
                    assert(device->lastMemoryHeapCreationDiagnostic()
                        == "RHI-VULKAN-HEAP-HOST-ALLOCATION-FAILED");
                    assert(!device->queryTexturePlacementRequirements(desc).sizeBytes);
                    assert(!device->createPlacedTexture(desc, *heap, 0u));
                }
                assert(quota->liveBytes() == 0u && o.imageAttempts == imagesBefore
                    && o.allocationAttempts == allocationsBefore);
                quota->setLimitBytes(0u);
                o.sealAfterAllocate = quota;
                {
                    core::CpuMemoryQuotaScope scope(quota);
                    auto committed = device->createMemoryHeap(
                        stride * 3u, req.alignmentBytes, rhi::MemoryUsage::Transient);
                    assert(committed); // No throwing host allocation after native allocation.
                }
                o.sealAfterAllocate.reset();
                quota->setLimitBytes(0u);
                o.sealAfterBind = quota;
                {
                    core::CpuMemoryQuotaScope scope(quota);
                    assert(!device->createPlacedTexture(
                        desc, *heap, 0u)); // Publication host allocation rejects.
                }
                o.sealAfterBind.reset();
                quota->setLimitBytes(0u);
                assert(o.created == o.destroyed && o.unsafeBinds == 0);
                auto retry = device->createPlacedTexture(desc, *heap, 0u);
                assert(retry);
            } else {
                assert(mode == "native-entrypoints");
                rhi_vulkan::VulkanRhiAllocator allocator(
                    {"entrypoint fixture", 64u * 1024u * 1024u, true, false}, runtime);
                // Measure actual cold owner storage, then hold that exact quota
                // while changing only the logical/native allocation-count bound.
                // A driver maximum is not a request to reserve every host slot.
                auto setupQuota = core::makeShared<core::CpuMemoryQuota>();
                {
                    core::CpuMemoryQuotaScope scope(setupQuota);
                    rhi_vulkan::VulkanRhiHeapPlacement small(runtime, allocator, true, 1u);
                    assert(!small.admissionDiagnostic());
                }
                assert(setupQuota->liveBytes() == 0u && setupQuota->peakBytes() > 0u);
                setupQuota->setLimitBytes(setupQuota->peakBytes());
                {
                    core::CpuMemoryQuotaScope scope(setupQuota);
                    rhi_vulkan::VulkanRhiHeapPlacement large(
                        runtime, allocator, true, runtime->nativeLimits().maxMemoryAllocationCount);
                    std::printf("heap cache cold admission: %s\n",
                        large.admissionDiagnostic() ? large.admissionDiagnostic() : "accepted");
                    std::fflush(stdout);
                    assert(!large.admissionDiagnostic());
                }
                assert(setupQuota->liveBytes() == 0u);
                for (const auto* name : {"vkAllocateMemory",
                         "vkFreeMemory",
                         "vkCreateImage",
                         "vkDestroyImage",
                         "vkGetImageMemoryRequirements2",
                         "vkBindImageMemory"}) {
                    o.missing = name;
                    rhi_vulkan::VulkanRhiHeapPlacement unavailable(
                        runtime, allocator, true, 64u * 1024u * 1024u);
                    assert(std::string_view(unavailable.admissionDiagnostic())
                        == "RHI-VULKAN-HEAP-ENTRYPOINT-UNAVAILABLE");
                    const char* diagnostic = nullptr;
                    assert(!unavailable.createHeap(
                        stride, req.alignmentBytes, rhi::MemoryUsage::Transient, diagnostic));
                    assert(
                        std::string_view(diagnostic) == "RHI-VULKAN-HEAP-ENTRYPOINT-UNAVAILABLE");
                    assert(!unavailable.query(desc, diagnostic).sizeBytes);
                    assert(!unavailable.createPlaced(desc, *heap, 0u).payload);
                }
                o.missing = nullptr;
                o.failAllocate = o.lost = true;
                assert(!device->createMemoryHeap(
                    stride * 3u, req.alignmentBytes, rhi::MemoryUsage::Transient));
                assert(device->lastMemoryHeapCreationDiagnostic() == "RHI-VULKAN-HEAP-DEVICE-LOST");
                assert(runtime->coreQueueCapabilityDiagnostic()
                    && !runtime->nativeLimits().maxMemoryAllocationCount);
                const auto attempts = o.allocationAttempts;
                assert(!device->createMemoryHeap(
                    stride, req.alignmentBytes, rhi::MemoryUsage::Transient));
                assert(!device->queryTexturePlacementRequirements(desc).sizeBytes);
                assert(!device->createPlacedTexture(desc, *heap, 0u));
                assert(o.allocationAttempts == attempts);
            }
        }
        heap.reset();
        device.reset();
        native.reset();
        runtime.reset();
        assert(o.unsafeBinds == 0 && o.created == o.destroyed && o.allocated == o.freed);
        std::printf(
            "heap-placement %.*s: alloc/free=%u/%u image/destroy=%u/%u binds=%u unsafe=%u\n",
            static_cast<int>(mode.size()),
            mode.data(),
            o.allocated,
            o.freed,
            o.created,
            o.destroyed,
            o.binds,
            o.unsafeBinds);
        observation_ = nullptr;
        return 0;
    }
};

} // namespace arp::tests
