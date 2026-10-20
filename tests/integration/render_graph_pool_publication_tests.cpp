#include "renderer/render_graph/frame_graph_resource_pool.h"
#include "rhi/backends/null/null_backend.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <new>
#include <string_view>

namespace {

struct AliasedFixture {
    arp::render_graph::FrameGraphAliasedResourceGroup group;
    arp::render_graph::FrameGraphAliasedResourceLayout layout;
};

AliasedFixture makeAliasedFixture(arp::rhi::GpuDevice& device,
    const arp::rhi::TextureDesc& desc,
    std::pmr::memory_resource* resource,
    unsigned count)
{
    using namespace arp;
    AliasedFixture result{{core::pmr::Vector<core::UniquePtr<rhi::GpuMemoryHeap>>(resource),
                              core::pmr::Vector<core::UniquePtr<rhi::GpuTexture>>(resource)},
        {}};
    result.group.heaps.reserve(1u);
    result.group.textures.reserve(count);
    const auto requirements = device.queryTexturePlacementRequirements(desc);
    auto heap = device.createMemoryHeap(
        requirements.sizeBytes, requirements.alignmentBytes, rhi::MemoryUsage::Transient);
    assert(heap);
    result.layout.heapSizes.push_back(requirements.sizeBytes);
    result.layout.heapAlignments.push_back(requirements.alignmentBytes);
    for (unsigned i = 0u; i < count; ++i) {
        auto placed = device.createPlacedTexture(desc, *heap, 0u);
        assert(placed);
        result.layout.textureDescs.push_back(desc);
        result.layout.textureHeapIndices.push_back(0u);
        result.layout.textureOffsets.push_back(0u);
        result.group.textures.push_back(std::move(placed));
    }
    result.group.heaps.push_back(std::move(heap));
    return result;
}

void aliasEvictionPreservesAllocatorIdentity()
{
    using namespace arp;
    constexpr auto heapBytes = sizeof(core::UniquePtr<rhi::GpuMemoryHeap>);
    constexpr auto textureBytes = sizeof(core::UniquePtr<rhi::GpuTexture>);
    alignas(std::max_align_t) std::array<std::byte, heapBytes + textureBytes> firstStorage{};
    alignas(std::max_align_t) std::array<std::byte, heapBytes + 2u * textureBytes> secondStorage{};
    std::pmr::monotonic_buffer_resource firstResource(
        firstStorage.data(), firstStorage.size(), std::pmr::null_memory_resource());
    std::pmr::monotonic_buffer_resource secondResource(
        secondStorage.data(), secondStorage.size(), std::pmr::null_memory_resource());
    auto device = rhi::createNullDevice();
    render_graph::FrameGraphResourcePool pool(*device);
    rhi::TextureDesc desc;
    desc.extent = {8u, 4u, 1u};
    desc.format = rhi::TextureFormat::Rgba8Unorm;
    desc.usage = rhi::TextureUsage::Sampled;
    const auto bytes = rhi::ResourceCacheAccounting::requireLogicalBytes(device->backend(), desc);
    pool.configureForCompiledGraph(3u * bytes, 3u);
    auto first = makeAliasedFixture(*device, desc, &firstResource, 1u);
    auto second = makeAliasedFixture(*device, desc, &secondResource, 2u);
    const auto secondData = second.group.textures.data();
    auto fence = core::shareUnique(device->createFence());
    assert(fence);
    fence->signal();
    pool.releaseDeferredAliasedGroup(std::move(first.group), std::move(first.layout), fence);
    pool.releaseDeferredAliasedGroup(std::move(second.group), second.layout, fence);
    pool.recycleCompleted();
    assert(pool.report().cachedResourceCount == 3u);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(quota);
        pool.configureForCompiledGraph(2u * bytes, 2u);
    }
    assert(quota->peakBytes() == 0u);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    const auto report = pool.report();
    assert(report.pendingResourceCount == 0u && report.pendingBytes == 0u);
    assert(report.cachedResourceCount == 2u && report.cachedBytes == 2u * bytes);
    assert(report.evictionCount == 1u);
    render_graph::FrameGraphAliasedResourceGroup retained{
        core::pmr::Vector<core::UniquePtr<rhi::GpuMemoryHeap>>(&secondResource),
        core::pmr::Vector<core::UniquePtr<rhi::GpuTexture>>(&secondResource)};
    assert(pool.acquireAliasedGroup(second.layout, retained));
    assert(retained.textures.size() == 2u && retained.textures.data() == secondData);
    assert(retained.textures.get_allocator().resource() == &secondResource);
    retained.textures.clear();
    retained.heaps.clear();
    pool.trimCachedResources();
    assert(device->memoryStats().allocatedBytes == 0u);
    assert(device->memoryStats().liveResourceCount == 0u);
    assert(device->queue().submitCount() == 0u);
}

void mixed_publication_preserves_failed_suffix()
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    render_graph::FrameGraphResourcePool pool(*device);
    rhi::TextureDesc firstDesc;
    firstDesc.extent = {8u, 4u, 1u};
    firstDesc.format = rhi::TextureFormat::Rgba8Unorm;
    firstDesc.usage = rhi::TextureUsage::Sampled;
    auto secondDesc = firstDesc;
    secondDesc.extent.width *= 2u;
    auto thirdDesc = firstDesc;
    thirdDesc.extent.width *= 4u;
    const auto firstBytes =
        rhi::ResourceCacheAccounting::requireLogicalBytes(device->backend(), firstDesc);
    const auto secondBytes =
        rhi::ResourceCacheAccounting::requireLogicalBytes(device->backend(), secondDesc);
    const auto thirdBytes =
        rhi::ResourceCacheAccounting::requireLogicalBytes(device->backend(), thirdDesc);
    const auto total = 2u * firstBytes + secondBytes + thirdBytes;
    pool.configureForCompiledGraph(total, 4u);
    pool.releaseTexture(device->createTexture(firstDesc));
    pool.releaseTexture(device->createTexture(firstDesc));
    auto first = pool.acquireTexture(firstDesc);
    auto second = pool.acquireTexture(secondDesc);
    auto third = pool.acquireTexture(thirdDesc);
    auto fence = core::shareUnique(device->createFence());
    assert(first && second && third && fence);
    pool.releaseDeferred(std::move(first), fence);
    pool.releaseDeferred(std::move(second), fence);
    pool.releaseDeferred(std::move(third), fence);
    fence->signal();
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    bool rejected = false;
    try {
        core::CpuMemoryQuotaScope scope(quota);
        pool.recycleCompleted();
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    assert(rejected);
    const auto partial = pool.report();
    assert(partial.cachedBytes == 2u * firstBytes && partial.cachedResourceCount == 2u);
    assert(partial.pendingBytes == secondBytes + thirdBytes && partial.pendingResourceCount == 2u);
    assert(device->memoryStats().allocatedBytes == total);
    pool.recycleCompleted();
    const auto retried = pool.report();
    assert(retried.pendingBytes == 0u && retried.pendingResourceCount == 0u);
    assert(retried.cachedBytes == total && retried.cachedResourceCount == 4u);
    pool.trimCachedResources();
    assert(device->memoryStats().allocatedBytes == 0u);
    assert(device->memoryStats().liveResourceCount == 0u);
    assert(device->queue().submitCount() == 0u);
}

} // namespace

// Validator-only safe red. No GPU work is issued. A quota failure may retain
// or destroy a cold resource, but the report must match its actual ownership.
int main(int argc, char** argv)
{
    using namespace arp;
    assert(argc == 2);
    const std::string_view mode(argv[1]);
    if (mode == "alias-eviction") {
        aliasEvictionPreservesAllocatorIdentity();
        return 0;
    }
    if (mode == "mixed") {
        mixed_publication_preserves_failed_suffix();
        return 0;
    }
    const bool alias = mode == "alias" || mode == "alias-recycle";
    const bool isBuffer = mode == "buffer" || mode == "buffer-recycle";
    const bool recycle = mode == "recycle" || mode == "buffer-recycle" || mode == "alias-recycle";
    assert(mode == "texture" || mode == "buffer" || recycle || alias);
    auto device = rhi::createNullDevice();
    render_graph::FrameGraphResourcePool pool(*device);
    pool.configureForCompiledGraph(128u, 1u);
    auto fence = core::shareUnique(device->createFence());
    assert(fence && !fence->isSignaled());
    rhi::TextureDesc desc;
    desc.extent = {8u, 4u, 1u};
    desc.format = rhi::TextureFormat::Rgba8Unorm;
    desc.usage = rhi::TextureUsage::Sampled;
    core::UniquePtr<rhi::GpuTexture> texture;
    core::UniquePtr<rhi::GpuBuffer> buffer;
    render_graph::FrameGraphAliasedResourceGroup group;
    render_graph::FrameGraphAliasedResourceLayout layout;
    if (alias) {
        auto fixture = makeAliasedFixture(*device, desc, &core::defaultCpuMemoryResource(), 1u);
        group = std::move(fixture.group);
        layout = std::move(fixture.layout);
    } else if (isBuffer) {
        rhi::BufferDesc bufferDesc;
        bufferDesc.sizeBytes = 128u;
        bufferDesc.usage = rhi::ResourceUsage::StorageBuffer;
        buffer = device->createBuffer(bufferDesc);
        assert(buffer);
    } else {
        texture = device->createTexture(desc);
        assert(texture);
    }
    if (recycle) {
        if (alias)
            pool.releaseDeferredAliasedGroup(std::move(group), std::move(layout), fence);
        else if (isBuffer)
            pool.releaseDeferredBuffer(std::move(buffer), fence);
        else
            pool.releaseDeferred(std::move(texture), fence);
        assert(pool.report().pendingBytes == 128u);
        fence->signal();
    }
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    bool rejected = false;
    try {
        core::CpuMemoryQuotaScope scope(quota);
        if (recycle)
            pool.recycleCompleted();
        else if (alias)
            pool.releaseDeferredAliasedGroup(std::move(group), std::move(layout), fence);
        else if (mode == "texture")
            pool.releaseDeferred(std::move(texture), fence);
        else if (mode == "buffer")
            pool.releaseDeferredBuffer(std::move(buffer), fence);
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    const auto report = pool.report();
    const auto actual = device->memoryStats();
    assert(device->queue().submitCount() == 0u);
    std::printf("mode=%s quotaRejected=%u pendingBytes=%llu pendingCount=%u cachedBytes=%llu "
                "cachedCount=%u actualBytes=%llu actualCount=%u\n",
        argv[1],
        unsigned(rejected),
        static_cast<unsigned long long>(report.pendingBytes),
        report.pendingResourceCount,
        static_cast<unsigned long long>(report.cachedBytes),
        report.cachedResourceCount,
        static_cast<unsigned long long>(actual.allocatedBytes),
        actual.liveResourceCount);
    std::fflush(stdout);
    assert(rejected);
    if (!alias) {
        assert(report.pendingBytes + report.cachedBytes == actual.allocatedBytes);
        assert(
            report.pendingResourceCount + report.cachedResourceCount == actual.liveResourceCount);
    }
    assert(report.cachedBytes == 0u && report.cachedResourceCount == 0u);
    if (recycle) {
        assert(report.pendingBytes == 128u && report.pendingResourceCount == 1u);
        assert(actual.allocatedBytes && actual.liveResourceCount);
        pool.recycleCompleted();
        const auto retried = pool.report();
        assert(retried.pendingBytes == 0u && retried.pendingResourceCount == 0u);
        assert(retried.cachedBytes == 128u && retried.cachedResourceCount == 1u);
        pool.trimCachedResources();
    } else {
        assert(report.pendingBytes == 0u && report.pendingResourceCount == 0u);
    }
    assert(device->memoryStats().allocatedBytes == 0u);
    assert(device->memoryStats().liveResourceCount == 0u);
}
