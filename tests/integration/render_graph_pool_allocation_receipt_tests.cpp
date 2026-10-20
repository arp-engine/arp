#include "renderer/render_graph/frame_graph_resource_pool.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/backends/null/null_backend.h"
#include "tests/fixtures/cpu_quota_factory_fixture.h"

#ifdef ARP_TEST_VULKAN
#include "tests/fixtures/vulkan_headless_runtime.h"
#include "tests/fixtures/vulkan_heap_owner_fixture.h"
#endif

#include <cassert>
#include <cstdio>
#include <string_view>
#include <thread>

namespace {
using namespace arp;
using Pool = render_graph::FrameGraphResourcePool;
static_assert(!std::is_copy_constructible_v<render_graph::FrameGraph>);
static_assert(!std::is_move_constructible_v<render_graph::FrameGraph>);
constexpr std::uint64_t cpuBytes = 65536u;
constexpr rhi::semantics::FrameGraphPoolAllocationBudget budget{
    1u, true, 2u, 2u, 32u, 128u, cpuBytes};

auto quota()
{
    return core::makeShared<core::CpuMemoryQuota>(cpuBytes);
}

void prepare(rhi::GpuDevice& device)
{
    assert(!device.prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, quota()));
    assert(!device.prepareOriginalResourceRegistration(
        {1u, true, 4u, 4u, 0u, 19u, cpuBytes}, quota()));
}

auto partitionBudget()
{
    auto b = budget;
    b.partitions.contractVersion = 1u;
    b.partitions.prototypeOptIn = true;
    b.partitions.maxResourceOwners = 4u;
    b.partitions.capacity.logicalTextureBytes = b.maxLogicalTextureBytes;
    b.partitions.capacity.logicalBufferBytes = b.maxLogicalBufferBytes;
    b.partitions.capacity.textureCount = b.maxTextures;
    b.partitions.capacity.bufferCount = b.maxBuffers;
    b.partitions.capacity.cpuMetadataBytes = cpuBytes;
    return b;
}

rhi::BufferDesc bufferDesc()
{
    return {"pool buffer",
        64u,
        rhi::ResourceUsage::CopySrc,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::Undefined};
}

rhi::TextureDesc textureDesc()
{
    rhi::TextureDesc desc;
    desc.extent = {2u, 2u, 1u};
    desc.format = rhi::TextureFormat::Rgba8Unorm;
    desc.usage = rhi::TextureUsage::Sampled;
    return desc;
}

template <class Operation> void rejects(Operation&& operation, const char* expected)
{
    bool failed = false;
    try {
        operation();
    } catch (const std::exception& e) {
        failed = true;
        assert(std::string_view(e.what()) == expected);
    }
    assert(failed);
}

void partitions(rhi::GpuDevice& device)
{
    prepare(device);
    Pool pool(device);
    auto metadata = quota();
    auto b = partitionBudget();
    assert(!pool.prepareAllocationAccounting(b, metadata));
    auto baseline = pool.allocationAccountingReport();
    assert(!baseline.partitionDiagnostic && baseline.partitionContractVersion == 1u);
    assert(baseline.partitions.reserved.cpuMetadataBytes == metadata->liveBytes());
    auto buffer = pool.acquireBuffer(bufferDesc());
    auto texture = pool.acquireTexture(textureDesc());
    auto live = pool.allocationAccountingReport();
    assert(!live.partitionDiagnostic && live.partitions.inUse.bufferCount == 1u);
    assert(live.partitions.inUse.textureCount == 1u);
    assert(live.partitions.inUse.logicalBufferBytes == 64u);
    assert(live.partitions.inUse.logicalTextureBytes == 16u);
    assert(live.partitions.inUse.cpuMetadataBytes + live.partitions.reserved.cpuMetadataBytes
        == metadata->liveBytes());
    // Caller quota pressure is not pool-owned metadata.
    {
        core::CpuMemoryQuotaLease pressure(metadata, 1u);
        assert(pressure.charged());
        assert(pool.allocationAccountingReport().allocatedCpuMetadataBytes + 1u
            == metadata->liveBytes());
    }
    const char* wrong = nullptr;
    std::thread finalOwner([&] {
        wrong = pool.allocationAccountingReport().partitionDiagnostic;
        rejects(
            [&] {
                pool.acquireBuffer(bufferDesc());
            },
            "CORE-PARTITION-THREAD");
        buffer.reset();
    });
    finalOwner.join();
    assert(wrong);
    texture.reset();
    auto empty = pool.allocationAccountingReport();
    assert(!empty.partitionDiagnostic && !empty.partitions.inUse.bufferCount
        && !empty.partitions.inUse.textureCount);
    assert(empty.partitions.reserved.cpuMetadataBytes
        == baseline.partitions.reserved.cpuMetadataBytes);
    assert(metadata->liveBytes() == baseline.allocatedCpuMetadataBytes);
    b.partitions.capacity.logicalBufferBytes = 63u;
    assert(!pool.prepareAllocationAccounting(b, quota()));
    rejects(
        [&] {
            pool.acquireBuffer(bufferDesc());
        },
        "CORE-PARTITION-CAPACITY");
    assert(!pool.allocationAccountingReport().buffers);
    b = partitionBudget();
    b.partitions.maxResourceOwners = 1u;
    assert(!pool.prepareAllocationAccounting(b, quota()));
    buffer = pool.acquireBuffer(bufferDesc());
    rejects(
        [&] {
            pool.acquireTexture(textureDesc());
        },
        "CORE-POOL-CAPACITY");
    buffer.reset();
    assert(!pool.allocationAccountingReport().partitionDiagnostic);
    b = partitionBudget();
    assert(!pool.prepareAllocationAccounting(b, quota()));
    const auto fixedBytes = pool.allocationAccountingReport().allocatedCpuMetadataBytes;
    b.partitions.capacity.cpuMetadataBytes = fixedBytes;
    assert(!pool.prepareAllocationAccounting(b, quota()));
    const auto beforeFailure = pool.bufferMissCount();
    rejects(
        [&] {
            pool.acquireBuffer(bufferDesc());
        },
        "CORE-PARTITION-CAPACITY");
    auto rolledBack = pool.allocationAccountingReport();
    assert(!rolledBack.partitionDiagnostic && !rolledBack.buffers);
    assert(rolledBack.allocatedCpuMetadataBytes == fixedBytes);
    assert(pool.bufferMissCount() == beforeFailure);
}

void graphEntry(rhi::GpuDevice& device)
{
    prepare(device);
    for (const bool partitions : {false, true}) {
        auto pool = core::makeShared<Pool>(device);
        auto metadata = quota();
        assert(
            !pool->prepareAllocationAccounting(partitions ? partitionBudget() : budget, metadata));
        const auto bytesBefore = metadata->liveBytes();
        const auto deviceBefore = device.memoryStats().allocatedBytes;
        rejects(
            [&] {
                render_graph::FrameGraph graph(device, pool);
            },
            "RG-POOL-ALLOCATION-GRAPH-UNAVAILABLE");
        assert(metadata->liveBytes() == bytesBefore
            && device.memoryStats().allocatedBytes == deviceBefore);
        assert(!pool->allocationAccountingReport().buffers
            && !pool->allocationAccountingReport().textures);
    }
}

void graphActive(rhi::GpuDevice& device)
{
    prepare(device);
    for (const bool partitions : {false, true}) {
        auto pool = core::makeShared<Pool>(device);
        auto metadata = quota();
        const auto request = partitions ? partitionBudget() : budget;
        const auto refuses = [&] {
            const auto* id = pool->prepareAllocationAccounting(request, metadata);
            assert(id && std::string_view(id) == "RG-POOL-ALLOCATION-GRAPH-ACTIVE");
            assert(!metadata->liveBytes() && !pool->allocationAccountingReport().contractVersion);
        };
        {
            render_graph::FrameGraph first(device, pool);
            {
                render_graph::FrameGraph second(device, pool);
                refuses();
                first.reset();
                second.reset();
                refuses();
            }
            // Destroying one graph is insufficient; reset is not destruction.
            refuses();
        }
        assert(!pool->prepareAllocationAccounting(request, metadata));
        assert(pool->allocationAccountingReport().contractVersion == 1u);
    }
}

void contracts()
{
    using Contract = rhi::semantics::FrameGraphPoolAllocationContract;
    assert(!Contract::validateIntent(budget));
    assert(Contract::lowering(rhi::Backend::Null).validatorOnly);
    assert(!Contract::lowering(rhi::Backend::Vulkan).validatorOnly);
    auto b = budget;
    b.prototypeOptIn = false;
    assert(Contract::validateIntent(b));
    b = budget;
    b.contractVersion = 2u;
    assert(Contract::validateIntent(b));
    b = budget;
    b.maxTextures = 0u;
    assert(Contract::validateIntent(b));
    b = budget;
    b.maxLogicalBufferBytes = 0u;
    assert(Contract::validateIntent(b));
    b = {};
    b.contractVersion = 1u;
    b.prototypeOptIn = true;
    b.maxCpuMetadataBytes = cpuBytes;
    assert(Contract::validateIntent(b));
    b = budget;
    b.maxCpuMetadataBytes = 0u;
    assert(Contract::validateIntent(b));
    for (const auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL}) {
        const auto decision = Contract::lowering(backend);
        assert(!decision.nativeExecutionProven);
        assert(decision.mode
            == (backend == rhi::Backend::Null || backend == rhi::Backend::Vulkan
                    ? rhi::CapabilityMode::Equivalent
                    : rhi::CapabilityMode::DiagnosticFailure));
        assert(decision.allocationOwnerAvailable == !bool(decision.diagnostic));
        assert(bool(Contract::lowering(backend).diagnostic)
            == (backend != rhi::Backend::Null && backend != rhi::Backend::Vulkan));
    }
    assert(Contract::lowering(static_cast<rhi::Backend>(99)).diagnostic);
    b = partitionBudget();
    assert(!Contract::validateIntent(b));
    b.partitions.prototypeOptIn = false;
    assert(std::string_view(Contract::validateIntent(b)) == "RG-POOL-PARTITION-OPT-IN");
    b = partitionBudget();
    b.partitions.contractVersion = 2u;
    assert(std::string_view(Contract::validateIntent(b)) == "RG-POOL-PARTITION-VERSION");
    b = partitionBudget();
    b.partitions.maxResourceOwners = 0u;
    assert(std::string_view(Contract::validateIntent(b)) == "RG-POOL-PARTITION-OWNERS");
    b = partitionBudget();
    b.partitions.maxResourceOwners = UINT32_MAX;
    assert(Contract::validateIntent(b));
    b = partitionBudget();
    b.partitions.capacity.cpuMetadataBytes = cpuBytes + 1u;
    assert(std::string_view(Contract::validateIntent(b)) == "RG-POOL-PARTITION-CPU-BUDGET");
    b = partitionBudget();
    b.partitions.capacity.cpuMetadataBytes = 0u;
    assert(Contract::validateIntent(b));
    static_assert(!std::is_copy_constructible_v<rhi::OriginalPoolAllocationReceipt>);
    static_assert(
        !std::is_default_constructible_v<rhi::OriginalPoolAllocationReceipt::ConstructionKey>);
}

void ordinary(rhi::GpuDevice& device)
{
    prepare(device);
    Pool pool(device), foreign(device);
    auto metadata = quota();
    assert(!pool.prepareAllocationAccounting(budget, metadata));
    assert(!foreign.prepareAllocationAccounting(budget, quota()));
    const auto baseline = metadata->liveBytes();
    auto a = pool.acquireBuffer(bufferDesc());
    auto t = pool.acquireTexture(textureDesc());
    assert(!pool.validateAllocationAccounting(*a) && !pool.validateAllocationAccounting(*t));
    assert(foreign.validateAllocationAccounting(*a));
    assert(pool.prepareAllocationAccounting(budget, quota()));
    auto report = pool.allocationAccountingReport();
    assert(report.contractVersion == 1u && report.lowering.allocationOwnerAvailable
        && !report.lowering.nativeExecutionProven);
    assert(report.buffers == 1u && report.logicalBufferBytes == 64u);
    assert(report.textures == 1u && report.logicalTextureBytes == 16u);
    assert(metadata->liveBytes() > baseline);
    auto b = pool.acquireBuffer(bufferDesc());
    const auto misses = pool.bufferMissCount();
    rejects(
        [&] {
            pool.acquireBuffer(bufferDesc());
        },
        "RG-POOL-ALLOCATION-RESOURCE-CAPACITY");
    assert(pool.bufferMissCount() == misses);
    b.reset();
    assert(pool.allocationAccountingReport().buffers == 1u);
    rejects(
        [&] {
            pool.releaseBuffer(std::move(a));
        },
        "RG-POOL-ALLOCATION-RETURN-UNAVAILABLE");
    assert(!a && pool.bufferPoolSize() == 0u);
    t.reset();
    assert(
        !pool.allocationAccountingReport().buffers && !pool.allocationAccountingReport().textures);
    assert(metadata->liveBytes() == baseline);
    auto unbound = device.createBuffer(bufferDesc());
    assert(unbound && pool.validateAllocationAccounting(*unbound));
    auto charged = pool.acquireBuffer(bufferDesc());
    Pool legacy(device);
    rejects(
        [&] {
            legacy.releaseBuffer(std::move(charged));
        },
        "RG-POOL-ALLOCATION-RETURN-UNAVAILABLE");
    assert(!pool.prepareAllocationAccounting(budget, quota()));
    auto live = pool.acquireBuffer(bufferDesc());
    device.retireOriginalDeviceLifetime();
    assert(pool.validateAllocationAccounting(*live));
    rejects(
        [&] {
            pool.acquireBuffer(bufferDesc());
        },
        "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
    live.reset();
}

void negative()
{
    auto device = rhi::createNullDevice();
    Pool pool(*device);
    assert(pool.prepareAllocationAccounting(budget, quota()));
    auto domain = quota(), registration = quota(), metadata = quota();
    assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, domain));
    assert(pool.prepareAllocationAccounting(budget, metadata));
    assert(!device->prepareOriginalResourceRegistration(
        {1u, true, 4u, 4u, 0u, 19u, cpuBytes}, registration));
    assert(pool.prepareAllocationAccounting(budget, domain));
    assert(pool.prepareAllocationAccounting(budget, registration));
    assert(!pool.prepareAllocationAccounting(budget, metadata));

    Pool prior(*device);
    auto old = prior.acquireBuffer(bufferDesc());
    assert(prior.prepareAllocationAccounting(budget, quota()));
    old.reset();
    assert(prior.prepareAllocationAccounting(budget, quota()));

    auto foreignDevice = rhi::createNullDevice();
    prepare(*foreignDevice);
    auto foreignResource = foreignDevice->createBuffer(bufferDesc());
    assert(pool.validateAllocationAccounting(*foreignResource));
    auto first = pool.acquireBuffer(bufferDesc());
    auto unbound = device->createBuffer(bufferDesc());
    assert(pool.validateAllocationAccounting(*unbound));
    const char* diagnostic = nullptr;
    const auto* observed = device->originalPoolAllocationReceipt(*first, diagnostic);
    assert(observed && !diagnostic && !observed->nativeExecutionProven());
    // An invented alias owner cannot bind a receipt to a different factory
    // resource or replace the original allocation's already occupied slot.
    std::shared_ptr<const rhi::OriginalPoolAllocationReceipt> invented(quota(), observed);
    assert(std::string_view(device->bindOriginalPoolAllocationReceipt(*first, invented))
        == "RG-POOL-ALLOCATION-ALREADY-BOUND");
    assert(std::string_view(device->bindOriginalPoolAllocationReceipt(*unbound, invented))
        == "RG-POOL-ALLOCATION-RECEIPT-RESOURCE");
    assert(device->bindOriginalPoolAllocationReceipt(*unbound, {}));
    assert(foreignDevice->bindOriginalPoolAllocationReceipt(*first, invented));
    invented.reset();
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        std::array{metadata}, [&]() noexcept -> const char* {
            for (unsigned i = 0u; i < 10000u; ++i)
                if (const auto* id = pool.validateAllocationAccounting(*first))
                    return id;
            first.reset();
            return nullptr;
        }));
    assert(pool.allocationAccountingReport().buffers == 0u);

    // Independently exercise the byte gate and count gate before factory work.
    auto constrained = budget;
    constrained.maxLogicalBufferBytes = 64u;
    assert(!pool.prepareAllocationAccounting(constrained, quota()));
    first = pool.acquireBuffer(bufferDesc());
    const auto live = device->memoryStats().allocatedBytes;
    const auto misses = pool.bufferMissCount();
    rejects(
        [&] {
            pool.acquireBuffer(bufferDesc());
        },
        "RG-POOL-ALLOCATION-RESOURCE-CAPACITY");
    assert(device->memoryStats().allocatedBytes == live && pool.bufferMissCount() == misses);
    first.reset();
    constrained = budget;
    constrained.maxBuffers = 1u;
    assert(!pool.prepareAllocationAccounting(constrained, quota()));
    first = pool.acquireBuffer(bufferDesc());
    rejects(
        [&] {
            pool.acquireBuffer(bufferDesc());
        },
        "RG-POOL-ALLOCATION-RESOURCE-CAPACITY");
    first.reset();

    // Every legacy return family rejects before accepting a public fence,
    // and foreign opt-out pools cannot bypass the charged-resource gate.
    rejects(
        [&] {
            pool.releaseDeferredBuffer(pool.acquireBuffer(bufferDesc()), {});
        },
        "RG-POOL-ALLOCATION-RETURN-UNAVAILABLE");
    rejects(
        [&] {
            prior.releaseDeferredBuffer(pool.acquireBuffer(bufferDesc()), {});
        },
        "RG-POOL-ALLOCATION-RETURN-UNAVAILABLE");
    rejects(
        [&] {
            pool.releaseTexture(pool.acquireTexture(textureDesc()));
        },
        "RG-POOL-ALLOCATION-RETURN-UNAVAILABLE");
    rejects(
        [&] {
            pool.releaseDeferred(
                pool.acquireTexture(textureDesc()), std::shared_ptr<rhi::GpuFence>{});
        },
        "RG-POOL-ALLOCATION-RETURN-UNAVAILABLE");
    rejects(
        [&] {
            prior.releaseDeferred(
                pool.acquireTexture(textureDesc()), core::UniquePtr<rhi::GpuFence>{});
        },
        "RG-POOL-ALLOCATION-RETURN-UNAVAILABLE");
    rejects(
        [&] {
            render_graph::FrameGraphAliasedResourceGroup group;
            group.textures.push_back(pool.acquireTexture(textureDesc()));
            prior.releaseDeferredAliasedGroup(std::move(group), {}, {});
        },
        "RG-POOL-ALLOCATION-RETURN-UNAVAILABLE");
    rejects(
        [&] {
            core::pmr::Vector<core::UniquePtr<rhi::GpuTexture>> textures;
            textures.push_back(pool.acquireTexture(textureDesc()));
            prior.releaseDeferredAliasedGroup(std::move(textures), {}, {});
        },
        "RG-POOL-ALLOCATION-RETURN-UNAVAILABLE");
    render_graph::FrameGraphAliasedResourceGroup group;
    rejects(
        [&] {
            pool.acquireAliasedGroup({}, group);
        },
        "RG-POOL-ALLOCATION-RETURN-UNAVAILABLE");
    rejects(
        [&] {
            pool.configureForCompiledGraph(64u, 1u);
        },
        "RG-POOL-ALLOCATION-RETURN-UNAVAILABLE");
    assert(
        !pool.allocationAccountingReport().textures && !pool.allocationAccountingReport().buffers);
    assert(pool.poolSize() == 0u && pool.bufferPoolSize() == 0u);
    {
        core::pmr::Vector<core::UniquePtr<rhi::GpuBuffer>> blockers;
        for (unsigned i = 0u; i < 3u; ++i) {
            blockers.push_back(device->createBuffer(bufferDesc()));
            assert(blockers.back());
        }
        const auto bytes = device->memoryStats().allocatedBytes;
        const auto before = pool.bufferMissCount();
        rejects(
            [&] {
                pool.acquireBuffer(bufferDesc());
            },
            "RG-POOL-ALLOCATION-FACTORY-FAILED");
        assert(device->lastOriginalResourceRegistrationDiagnostic()
            == "RHI-ORIGINAL-REGISTRATION-KIND-CAPACITY");
        assert(device->memoryStats().allocatedBytes == bytes && pool.bufferMissCount() == before);
        assert(!pool.allocationAccountingReport().buffers);
    }
}

void facadeLifetime()
{
    auto device = rhi::createNullDevice();
    prepare(*device);
    auto lifetime = device->originalDeviceLifetimeTicket();
    auto pool = core::makeUnique<Pool>(*device);
    auto metadata = quota();
    assert(!pool->prepareAllocationAccounting(budget, metadata));
    auto resource = pool->acquireBuffer(bufferDesc());
    device.reset();
    const auto* diagnostic = lifetime.diagnostic();
    assert(diagnostic && pool->validateAllocationAccounting(*resource));
    rejects(
        [&] {
            pool->acquireBuffer(bufferDesc());
        },
        diagnostic);
    assert(pool->prepareAllocationAccounting(budget, quota()));
    pool.reset();
    assert(metadata->liveBytes() != 0u);
    resource.reset();
    assert(metadata->liveBytes() == 0u);
}

void quotas(bool partitionOwners = false)
{
    const auto originalBudget = partitionOwners ? partitionBudget() : budget;
    auto device = rhi::createNullDevice();
    assert(device);
    prepare(*device);
    Pool pool(*device);
    auto metadata = quota();
    test::CpuQuotaFactoryFixture pressure(metadata);
    // Cold replacement is transactional at every quota boundary.
    const auto factory = [&]() {
        return pool.prepareAllocationAccounting(originalBudget, metadata) == nullptr;
    };
    assert(factory());
    const auto baseline = metadata->liveBytes();
    // Failed replacement preserves the old capacity and accounting state.
    auto changed = originalBudget;
    changed.maxBuffers = 3u;
    changed.maxLogicalBufferBytes = 192u;
    {
        core::CpuMemoryQuotaLease full(metadata, cpuBytes - baseline);
        assert(full.charged());
        assert(pool.prepareAllocationAccounting(changed, metadata));
    }
    {
        auto first = pool.acquireBuffer(bufferDesc());
        auto second = pool.acquireBuffer(bufferDesc());
        rejects(
            [&] {
                pool.acquireBuffer(bufferDesc());
            },
            "RG-POOL-ALLOCATION-RESOURCE-CAPACITY");
    }
    metadata->setLimitBytes(baseline);
    assert(pool.prepareAllocationAccounting(originalBudget, metadata));
    metadata->setLimitBytes(cpuBytes);
    const auto acquire = [&]() -> core::UniquePtr<rhi::GpuBuffer> {
        try {
            return pool.acquireBuffer(bufferDesc());
        } catch (const std::bad_alloc&) {
            return nullptr;
        }
    };
    const auto peak = pressure.measureSingleUse([] {}, acquire);
    pressure.verify(peak, acquire);
    for (std::uint64_t available = 0u; available <= peak; ++available) {
        core::CpuMemoryQuotaLease reserved(metadata, cpuBytes - baseline - available);
        assert(reserved.charged());
        auto result = acquire();
        assert(bool(result) == (available == peak));
    }
    assert(pool.allocationAccountingReport().buffers == 0u);
    assert(metadata->liveBytes() == baseline);
    if (partitionOwners) {
        // Every byte of the entire replacement storage, including the fixed
        // metadata slot, is required. Failed replacements preserve the source.
        auto coldQuota = quota();
        test::CpuQuotaFactoryFixture cold(coldQuota);
        const auto replacement = [&] {
            Pool candidate(*device);
            return candidate.prepareAllocationAccounting(originalBudget, coldQuota) == nullptr;
        };
        const auto exact = cold.measureSingleUse([] {}, replacement);
        for (std::uint64_t available = 0u; available <= exact; ++available) {
            core::CpuMemoryQuotaLease pressure(coldQuota, cpuBytes - available);
            assert(pressure.charged());
            assert(replacement() == (available == exact));
        }
        assert(!coldQuota->liveBytes());
        assert(!pool.allocationAccountingReport().partitionDiagnostic);
        std::printf("partition pool: cold=%llu receipt=%llu; every byte rolled back\n",
            static_cast<unsigned long long>(exact),
            static_cast<unsigned long long>(peak));
    }
}

#ifdef ARP_TEST_VULKAN
void independentNativeRoles(const std::shared_ptr<rhi_vulkan::VulkanRhiRuntime>& runtime)
{
    rhi_vulkan::VulkanRhiAllocator allocator({"independent roles", 64u * 1024u * 1024u}, runtime);
    {
        auto metadata = allocator.allocate({"metadata only", 64u, rhi::MemoryUsage::GpuOnly});
        assert(metadata.success && metadata.originalLifetime.expired());
        metadata.releaseToken.reset();
        assert(!allocator.memoryStats().allocatedBytes);
    }
    for (unsigned role = 0u; role < 3u; ++role) {
        auto allocation = allocator.createBuffer(bufferDesc(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        assert(allocation.success && allocation.payload && allocation.writeFlusher
            && allocation.readback);
        const auto bytes = allocator.memoryStats().allocatedBytes;
        std::shared_ptr<void> retained = role == 0u ? allocation.payload
            : role == 1u                            ? std::shared_ptr<void>(allocation.writeFlusher)
                                                    : std::shared_ptr<void>(allocation.readback);
        allocation = {};
        assert(allocator.memoryStats().allocatedBytes == bytes);
        assert(allocator.memoryStats().liveResourceCount == 1u);
        retained.reset();
        assert(
            !allocator.memoryStats().allocatedBytes && !allocator.memoryStats().liveResourceCount);
    }
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent = {2u, 2u, 1u};
    info.mipLevels = 1u;
    info.arrayLayers = 1u;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    auto allocation = allocator.createImage(textureDesc(), info);
    assert(allocation.success && allocation.payload);
    const auto bytes = allocator.memoryStats().allocatedBytes;
    auto retained = allocation.payload;
    allocation = {};
    assert(allocator.memoryStats().allocatedBytes == bytes
        && allocator.memoryStats().liveResourceCount == 1u);
    retained.reset();
    assert(!allocator.memoryStats().allocatedBytes && !allocator.memoryStats().liveResourceCount);
}

int native(bool partitionOwners = false)
{
    tests::VulkanHeadlessRuntime native;
    if (!native.runtime() || !native.runtime()->valid())
        return 77;
    independentNativeRoles(native.runtime());
    {
        auto source = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
        ordinary(*source);
    }
    auto device = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
    if (partitionOwners) {
        auto candidate = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
        partitions(*candidate);
    }
    prepare(*device);
    auto metadata = quota();
    auto pool = core::makeUnique<Pool>(*device);
    assert(
        !pool->prepareAllocationAccounting(partitionOwners ? partitionBudget() : budget, metadata));
    for (bool texture : {false, true}) {
        std::shared_ptr<rhi_vulkan::VulkanRhiNativeResourcePayload> payload;
        rhi::OriginalResourceRegistrationTicket ticket;
        if (texture) {
            auto resource = pool->acquireTexture(textureDesc());
            payload = rhi_vulkan::vulkanRhiNativePayload(*resource);
            ticket = resource->originalResourceRegistration();
        } else {
            auto resource = pool->acquireBuffer(bufferDesc());
            payload = rhi_vulkan::vulkanRhiNativePayload(*resource);
            ticket = resource->originalResourceRegistration();
        }
        assert(payload && ticket.diagnostic());
        const auto report = pool->allocationAccountingReport();
        assert(texture ? report.textures == 1u : report.buffers == 1u);
        if (partitionOwners) {
            assert(!report.partitionDiagnostic);
            assert(texture ? report.partitions.inUse.textureCount == 1u
                           : report.partitions.inUse.bufferCount == 1u);
        }
        assert(pool->prepareAllocationAccounting(budget, quota()));
        payload.reset();
        assert(!pool->allocationAccountingReport().buffers
            && !pool->allocationAccountingReport().textures);
    }
    auto resource = pool->acquireBuffer(bufferDesc());
    auto payload = rhi_vulkan::vulkanRhiNativePayload(*resource);
    resource.reset();
    pool.reset();
    device.reset();
    assert(metadata->liveBytes() > 0u);
    if (partitionOwners) {
        std::thread finalOwner([&] {
            payload.reset();
        });
        finalOwner.join();
    } else {
        payload.reset();
    }
    assert(metadata->liveBytes() == 0u);
    std::puts("original pool/native allocation receipt survives wrapper, pool and facade: PASS");
    return 0;
}
#endif
} // namespace

int main(int argc, char** argv)
{
    const std::string_view mode = argc > 1 ? argv[1] : "null";
    if (mode == "contract") {
        contracts();
    } else if (mode == "partitions") {
        auto device = arp::rhi::createNullDevice();
        partitions(*device);
    } else if (mode == "quota") {
        quotas();
    } else if (mode == "partitions-quota") {
        quotas(true);
    } else if (mode == "graph-entry" || mode == "graph-active") {
        auto device = arp::rhi::createNullDevice();
        if (mode == "graph-entry")
            graphEntry(*device);
        else
            graphActive(*device);
    } else if (mode == "negative") {
        negative();
    } else if (mode == "lifetime") {
        facadeLifetime();
#ifdef ARP_TEST_VULKAN
    } else if (mode == "native") {
        return native();
    } else if (mode == "partitions-native") {
        return native(true);
    } else if (mode == "graph-native") {
        arp::tests::VulkanHeadlessRuntime runtime;
        if (!runtime.runtime() || !runtime.runtime()->valid())
            return 77;
        auto device = arp::tests::VulkanHeapOwnerFixture::makeDevice(runtime.runtime(), false);
        graphEntry(*device);
        device = arp::tests::VulkanHeapOwnerFixture::makeDevice(runtime.runtime(), false);
        graphActive(*device);
#endif
    } else {
        auto device = arp::rhi::createNullDevice();
        assert(device);
        ordinary(*device);
    }
    return 0;
}
