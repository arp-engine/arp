#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/frame_graph_resource_pool.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/backends/null/null_backend.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace {

class ControlledFence final : public arp::rhi::GpuFence {
public:
    void signal() override { signaled_.store(true, std::memory_order_release); }

    bool isSignaled() const override
    {
        pollCount_.fetch_add(1u, std::memory_order_relaxed);
        return signaled_.load(std::memory_order_acquire);
    }

    std::uint64_t pollCount() const noexcept { return pollCount_.load(std::memory_order_relaxed); }

    void wait() override {}

    void reset() override { signaled_.store(false, std::memory_order_release); }

private:
    std::atomic<bool> signaled_{false};
    mutable std::atomic<std::uint64_t> pollCount_{0u};
};

void assertSamePoolReport(const arp::render_graph::FrameGraphResourcePoolReport& a,
    const arp::render_graph::FrameGraphResourcePoolReport& b)
{
    assert(std::tie(a.compiledTransientPeakBytes,
               a.compiledTransientResourceCount,
               a.logicalFrameSlots,
               a.byteBudget,
               a.resourceBudget,
               a.cachedBytes,
               a.pendingBytes,
               a.cachedResourceCount,
               a.pendingResourceCount,
               a.hitCount,
               a.missCount,
               a.evictionCount,
               a.reuseFailureCount,
               a.lastDiagnostic,
               a.accountingContractVersion,
               a.accountingDomain)
        == std::tie(b.compiledTransientPeakBytes,
            b.compiledTransientResourceCount,
            b.logicalFrameSlots,
            b.byteBudget,
            b.resourceBudget,
            b.cachedBytes,
            b.pendingBytes,
            b.cachedResourceCount,
            b.pendingResourceCount,
            b.hitCount,
            b.missCount,
            b.evictionCount,
            b.reuseFailureCount,
            b.lastDiagnostic,
            b.accountingContractVersion,
            b.accountingDomain));
    const auto& x = a.nativePhysicalObservation;
    const auto& y = b.nativePhysicalObservation;
    assert(std::tie(x.state,
               x.unit,
               x.capacity,
               x.current,
               x.peak,
               x.rejectionCount,
               x.liveCount,
               x.retiredCount,
               x.chargeRelation,
               x.diagnosticId)
        == std::tie(y.state,
            y.unit,
            y.capacity,
            y.current,
            y.peak,
            y.rejectionCount,
            y.liveCount,
            y.retiredCount,
            y.chargeRelation,
            y.diagnosticId));
}

void assertSameMemoryStats(const arp::rhi::MemoryStats& a, const arp::rhi::MemoryStats& b)
{
    assert(std::tie(a.allocatedBytes,
               a.budgetCapacityBytes,
               a.peakAllocatedBytes,
               a.budgetRejectionCount,
               a.liveResourceCount,
               a.backendFallbacksHiddenFromUpperLayer)
        == std::tie(b.allocatedBytes,
            b.budgetCapacityBytes,
            b.peakAllocatedBytes,
            b.budgetRejectionCount,
            b.liveResourceCount,
            b.backendFallbacksHiddenFromUpperLayer));
}

void setterOverflowPreservesConfiguration(bool byteOverflow)
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    render_graph::FrameGraphResourcePool pool(*device);
    rhi::TextureDesc desc;
    desc.extent = {8u, 4u, 1u};
    desc.format = rhi::TextureFormat::Rgba8Unorm;
    desc.usage = rhi::TextureUsage::Sampled;
    const auto bytes = rhi::ResourceCacheAccounting::requireLogicalBytes(device->backend(), desc);
    const auto peak = byteOverflow ? UINT64_MAX / 2u + 1u : bytes;
    const auto count = byteOverflow ? 1u : UINT32_MAX / 2u + 1u;
    pool.configureForCompiledGraph(peak, count);
    auto cached = device->createTexture(desc);
    auto pending = device->createTexture(desc);
    assert(cached && pending);
    const auto cachedId = cached->id();
    pool.releaseTexture(std::move(cached));
    auto fence = core::makeShared<ControlledFence>();
    pool.releaseDeferred(std::move(pending), fence);
    const auto before = pool.report();
    const auto memoryBefore = device->memoryStats();
    const auto polls = fence->pollCount();
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    bool rejected = false;
    {
        core::CpuMemoryQuotaScope scope(quota);
        try {
            pool.setLogicalFrameSlots(2u);
        } catch (const std::overflow_error& error) {
            rejected = std::string_view(error.what()) == "RG-POOL-BUDGET-OVERFLOW";
        }
    }
    assert(rejected);
    const auto after = pool.report();
    std::printf("configuration-%s: rejected=%u slots=%u->%u budget=%llu->%llu "
                "pending=%u cached=%u\n",
        byteOverflow ? "bytes" : "count",
        unsigned(rejected),
        before.logicalFrameSlots,
        after.logicalFrameSlots,
        static_cast<unsigned long long>(before.byteBudget),
        static_cast<unsigned long long>(after.byteBudget),
        after.pendingResourceCount,
        after.cachedResourceCount);
    std::fflush(stdout);
    assertSamePoolReport(before, after);
    assertSameMemoryStats(memoryBefore, device->memoryStats());
    assert(fence->pollCount() == polls && quota->peakBytes() == 0u);
    pool.configureForCompiledGraph(2u * bytes, 2u);
    pool.setLogicalFrameSlots(2u);
    assert(pool.report().logicalFrameSlots == 2u);
    auto reused = pool.acquireTexture(desc);
    assert(reused && reused->id() == cachedId);
    reused.reset();
    fence->signal();
    pool.recycleCompleted();
    pool.trimCachedResources();
    assert(device->memoryStats().allocatedBytes == 0u);
    assert(device->memoryStats().liveResourceCount == 0u);
    assert(device->queue().submitCount() == 0u);
}

void configurationBoundariesAndRetry()
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    for (const auto slots : std::array<std::uint32_t, 4u>{1u, 2u, 3u, UINT32_MAX}) {
        render_graph::FrameGraphResourcePool pool(*device, slots);
        const auto maxBytes = UINT64_MAX / slots;
        const auto maxCount = UINT32_MAX / slots;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaScope scope(quota);
            pool.configureForCompiledGraph(maxBytes, maxCount);
            pool.configureForCompiledGraph(maxBytes, maxCount);
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        const auto exact = pool.report();
        assert(exact.byteBudget == maxBytes * slots && exact.resourceBudget == maxCount * slots);
        for (unsigned mode = 0u; mode < 3u; ++mode) {
            if (slots == 1u && mode != 0u)
                continue;
            bool rejected = false;
            try {
                if (mode == 0u)
                    pool.setLogicalFrameSlots(0u);
                else if (mode == 1u)
                    pool.configureForCompiledGraph(maxBytes + 1u, 1u);
                else
                    pool.configureForCompiledGraph(1u, maxCount + 1u);
            } catch (const std::invalid_argument& error) {
                rejected = mode == 0u
                    && std::string_view(error.what()) == "RG-POOL-LOGICAL-FRAME-SLOTS-INVALID";
            } catch (const std::overflow_error& error) {
                rejected =
                    mode != 0u && std::string_view(error.what()) == "RG-POOL-BUDGET-OVERFLOW";
            }
            assert(rejected);
            assertSamePoolReport(exact, pool.report());
        }
        {
            core::CpuMemoryQuotaScope scope(quota);
            pool.configureForCompiledGraph(0u, 0u);
            pool.setLogicalFrameSlots(slots == 3u ? 2u : 3u);
        }
        const auto empty = pool.report();
        assert(empty.logicalFrameSlots == (slots == 3u ? 2u : 3u));
        assert(empty.byteBudget == 0u && empty.resourceBudget == 0u);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
    assert(quota->peakBytes() == 0u);
    assert(device->queue().submitCount() == 0u);
}

void configurationShrinkPreservesPending()
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    render_graph::FrameGraphResourcePool pool(*device, 2u);
    rhi::TextureDesc desc;
    desc.extent = {8u, 4u, 1u};
    desc.format = rhi::TextureFormat::Rgba8Unorm;
    desc.usage = rhi::TextureUsage::Sampled;
    const auto bytes = rhi::ResourceCacheAccounting::requireLogicalBytes(device->backend(), desc);
    pool.configureForCompiledGraph(bytes, 1u);
    auto first = device->createTexture(desc);
    auto second = device->createTexture(desc);
    auto pending = device->createTexture(desc);
    rhi::BufferDesc bufferDesc;
    bufferDesc.sizeBytes = bytes;
    bufferDesc.usage = rhi::ResourceUsage::StorageBuffer;
    auto pendingBuffer = device->createBuffer(bufferDesc);
    assert(first && second && pending && pendingBuffer);
    const auto retainedId = second->id();
    pool.releaseTexture(std::move(first));
    pool.releaseTexture(std::move(second));
    auto fence = core::makeShared<ControlledFence>();
    pool.releaseDeferred(std::move(pending), fence);
    pool.releaseDeferredBuffer(std::move(pendingBuffer), fence);
    const auto before = pool.report();
    const auto memoryBefore = device->memoryStats();
    const auto polls = fence->pollCount();
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(quota);
        pool.setLogicalFrameSlots(1u);
    }
    const auto shrunk = pool.report();
    assert(shrunk.logicalFrameSlots == 1u && shrunk.byteBudget == bytes);
    assert(shrunk.resourceBudget == 1u && shrunk.cachedResourceCount == 1u);
    assert(shrunk.cachedBytes == bytes && shrunk.evictionCount == before.evictionCount + 1u);
    assert(shrunk.pendingResourceCount == 2u && shrunk.pendingBytes == 2u * bytes);
    assert(device->memoryStats().allocatedBytes == memoryBefore.allocatedBytes - bytes);
    assert(device->memoryStats().liveResourceCount == memoryBefore.liveResourceCount - 1u);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    assert(fence->pollCount() == polls && quota->peakBytes() == 0u);
    auto retained = pool.acquireTexture(desc);
    assert(retained && retained->id() == retainedId);
    pool.releaseTexture(std::move(retained));
    const auto zeroPolls = fence->pollCount();
    const auto zeroAllocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(quota);
        pool.configureForCompiledGraph(0u, 0u);
    }
    const auto empty = pool.report();
    assert(empty.cachedBytes == 0u && empty.cachedResourceCount == 0u);
    assert(empty.pendingResourceCount == 2u && empty.pendingBytes == 2u * bytes);
    assert(fence->pollCount() == zeroPolls && quota->peakBytes() == 0u);
    assert(core::defaultCpuAllocator().stats().allocationCount == zeroAllocations);
    fence->signal();
    pool.recycleCompleted();
    assert(pool.report().pendingResourceCount == 0u && pool.report().pendingBytes == 0u);
    pool.trimCachedResources();
    assert(device->memoryStats().allocatedBytes == 0u);
    assert(device->memoryStats().liveResourceCount == 0u);
    assert(device->queue().submitCount() == 0u);
}

arp::rhi::TextureDesc transientDesc(const char* label, std::uint32_t width = 32)
{
    return {
        label,
        {width, 32, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    };
}

struct DestructionOrderState {
    bool textureDestroyed = false;
    bool heapDestroyed = false;
    bool heapObservedTextureDestruction = false;
};

class TrackedTexture final : public arp::rhi::GpuTexture {
public:
    explicit TrackedTexture(std::shared_ptr<DestructionOrderState> state)
        : state_(std::move(state)), desc_(transientDesc("TrackedAliasTexture"))
    {
        allocation_.estimatedSizeBytes = 4096;
    }

    ~TrackedTexture() override { state_->textureDestroyed = true; }

    arp::rhi::GpuResourceId id() const override { return {9001}; }

    const arp::rhi::TextureDesc& desc() const override { return desc_; }

    const arp::rhi::ResourceAllocation& allocation() const override { return allocation_; }

    arp::rhi::ResourceState state() const override { return arp::rhi::ResourceState::Undefined; }

    arp::rhi::ResourceState subresourceState(
        const arp::rhi::TextureSubresourceRange&) const override
    {
        return state();
    }

private:
    std::shared_ptr<DestructionOrderState> state_;
    arp::rhi::TextureDesc desc_;
    arp::rhi::ResourceAllocation allocation_;
};

class TrackedHeap final : public arp::rhi::GpuMemoryHeap {
public:
    explicit TrackedHeap(std::shared_ptr<DestructionOrderState> state) : state_(std::move(state)) {}

    ~TrackedHeap() override
    {
        state_->heapObservedTextureDestruction = state_->textureDestroyed;
        state_->heapDestroyed = true;
    }

    arp::rhi::GpuResourceId id() const override { return {9002}; }

    std::uint64_t sizeBytes() const override { return 4096; }

    std::uint64_t alignmentBytes() const override { return 256; }

    bool supportsAliasing() const override { return true; }

private:
    std::shared_ptr<DestructionOrderState> state_;
};

void transitionTexture(arp::rhi::GpuDevice& device,
    arp::rhi::GpuTexture& texture,
    arp::rhi::ResourceState before,
    arp::rhi::ResourceState after)
{
    auto commands = device.createCommandEncoder({"ResourceStateSetup"});
    assert(commands);
    const auto result = commands->transition(texture, before, after);
    assert(result.success);
    auto recorded = commands->finish();
    assert(recorded && device.queue().submit(*recorded).hasAcceptedWork());
}

void completion_gates_pool_reuse_and_normalizes_state()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraphResourcePool pool(*device, 1);
    const auto desc = transientDesc("CompletionGated");
    auto texture = device->createTexture(desc);
    assert(texture);
    const auto id = texture->id();
    const auto bytes = texture->allocation().estimatedSizeBytes;
    pool.configureForCompiledGraph(bytes, 1);
    transitionTexture(*device,
        *texture,
        arp::rhi::ResourceState::Undefined,
        arp::rhi::ResourceState::ColorAttachment);

    auto fence = arp::core::makeShared<ControlledFence>();
    pool.releaseDeferred(std::move(texture), fence);
    pool.recycleCompleted();
    assert(pool.report().pendingResourceCount == 1);
    assert(pool.report().cachedResourceCount == 0);

    auto premature = pool.acquireTexture(desc);
    assert(premature);
    assert(premature->id() != id);
    pool.releaseTexture(std::move(premature));

    fence->signal();
    pool.recycleCompleted();
    auto reused = pool.acquireTexture(desc);
    assert(reused);
    assert(reused->id() == id);
    assert(reused->state() == arp::rhi::ResourceState::Undefined);
}

void pool_reuse_normalization_is_idempotent_for_undefined_resources()
{
    auto device = arp::rhi::createNullDevice();
    auto texture = device->createTexture(transientDesc("AlreadyUndefined"));
    auto buffer = device->createBuffer({
        "AlreadyUndefinedBuffer",
        256,
        arp::rhi::ResourceUsage::StorageBuffer,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    assert(texture && buffer);
    const auto texturePrepared = device->prepareTextureForPoolReuse(*texture);
    const auto bufferPrepared = device->prepareBufferForPoolReuse(*buffer);
    assert(texturePrepared.success);
    assert(bufferPrepared.success);
    assert(texture->state() == arp::rhi::ResourceState::Undefined);
    assert(buffer->state() == arp::rhi::ResourceState::Undefined);
}

void barrier_failure_prevents_submit()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    const auto texture = graph.createTexture(transientDesc("BarrierConflict"));
    transitionTexture(*device,
        graph.texture(texture),
        arp::rhi::ResourceState::Undefined,
        arp::rhi::ResourceState::ShaderRead);
    graph.addPass("WriteAfterInjectedConflict")
        .write(texture, arp::rhi::ResourceState::ColorAttachment)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto submitCountBefore = device->queue().submitCount();
    bool rejected = false;
    try {
        graph.execute();
    } catch (const std::runtime_error& error) {
        rejected =
            std::string(error.what()).find("RG-BARRIER-TRANSITION-FAILED") != std::string::npos;
    }
    assert(rejected);
    assert(device->queue().submitCount() == submitCountBefore);
}

void completed_cache_obeys_exact_budget_and_lru()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraphResourcePool pool(*device, 1);
    const auto firstDesc = transientDesc("LruFirst", 32);
    const auto secondDesc = transientDesc("LruSecond", 32);
    auto first = device->createTexture(firstDesc);
    auto second = device->createTexture(secondDesc);
    assert(first && second);
    const auto bytes = first->allocation().estimatedSizeBytes;
    pool.configureForCompiledGraph(bytes, 1);
    pool.releaseTexture(std::move(first));
    pool.releaseTexture(std::move(second));

    const auto report = pool.report();
    assert(report.cachedBytes <= report.byteBudget);
    assert(report.cachedResourceCount <= report.resourceBudget);
    assert(report.cachedResourceCount == 1);
    assert(report.evictionCount == 1);
}

void aliased_group_waits_for_completion_and_destroys_texture_before_heap()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraphResourcePool pool(*device, 1);
    pool.configureForCompiledGraph(4096, 1);
    auto state = arp::core::makeShared<DestructionOrderState>();
    auto fence = arp::core::makeShared<ControlledFence>();
    arp::core::pmr::Vector<arp::core::UniquePtr<arp::rhi::GpuTexture>> textures(
        &arp::core::defaultCpuMemoryResource());
    arp::core::pmr::Vector<arp::core::UniquePtr<arp::rhi::GpuMemoryHeap>> heaps(
        &arp::core::defaultCpuMemoryResource());
    textures.push_back(arp::core::makeUnique<TrackedTexture>(state));
    heaps.push_back(arp::core::makeUnique<TrackedHeap>(state));
    pool.releaseDeferredAliasedGroup(std::move(textures), std::move(heaps), fence);

    pool.recycleCompleted();
    assert(!state->textureDestroyed);
    assert(!state->heapDestroyed);
    assert(pool.report().pendingBytes == 4096);
    assert(pool.report().pendingResourceCount == 1);

    fence->signal();
    pool.recycleCompleted();
    assert(state->textureDestroyed);
    assert(state->heapDestroyed);
    assert(state->heapObservedTextureDestruction);
    assert(pool.report().pendingBytes == 0);
    assert(pool.report().pendingResourceCount == 0);
}

void completed_aliased_group_is_reused_only_for_the_same_layout()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraphResourcePool pool(*device, 1);
    pool.configureForCompiledGraph(4096, 1);
    auto fence = arp::core::makeShared<ControlledFence>();
    arp::render_graph::FrameGraphAliasedResourceLayout layout;
    layout.heapSizes.push_back(4096);
    layout.heapAlignments.push_back(256);
    layout.textureDescs.push_back(transientDesc("ReusableAliasTexture"));
    layout.textureHeapIndices.push_back(0);
    layout.textureOffsets.push_back(0);
    arp::render_graph::FrameGraphAliasedResourceGroup group;
    auto texture = device->createTexture(layout.textureDescs.front());
    auto heap = device->createMemoryHeap(4096, 256, arp::rhi::MemoryUsage::Transient);
    assert(texture && heap);
    const auto textureId = texture->id();
    const auto heapId = heap->id();
    group.textures.push_back(std::move(texture));
    group.heaps.push_back(std::move(heap));

    pool.releaseDeferredAliasedGroup(std::move(group), layout, fence);
    fence->signal();
    pool.recycleCompleted();
    assert(pool.report().cachedBytes == 4096);

    arp::render_graph::FrameGraphAliasedResourceGroup reused;
    assert(pool.acquireAliasedGroup(layout, reused));
    assert(reused.textures.size() == 1);
    assert(reused.heaps.size() == 1);
    assert(reused.textures.front()->id() == textureId);
    assert(reused.heaps.front()->id() == heapId);

    auto incompatible = layout;
    incompatible.heapSizes[0] = 8192;
    arp::render_graph::FrameGraphAliasedResourceGroup rejected;
    assert(!pool.acquireAliasedGroup(incompatible, rejected));
}

void ordinary_import_cannot_claim_present_state()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    auto texture = device->createTexture(transientDesc("NotSwapchainOwned"));
    assert(texture);
    const auto imported = graph.importTexture(std::move(texture));

    bool rejected = false;
    try {
        graph.setExternalFinalState(imported, arp::rhi::ResourceState::Present);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()) == "RG-EXTERNAL-PRESENT-NON-SWAPCHAIN";
    }
    assert(rejected);
}

void acquired_swapchain_import_reaches_present_state()
{
    auto device = arp::rhi::createNullDevice();
    auto swapchain = device->createSwapchain({
        "ResourceManagementContractSwapchain",
        {32, 32},
        arp::rhi::TextureFormat::Rgba8Unorm,
        2,
    });
    assert(swapchain);
    const auto frame = swapchain->acquireNextFrame();
    assert(frame.success);
    auto image = swapchain->acquiredImage(frame);
    assert(image.success && image.texture);
    assert(image.texture->state() == arp::rhi::ResourceState::Undefined);

    arp::render_graph::FrameGraph graph(*device);
    const auto backbuffer = graph.importSwapchainTexture(std::move(image.texture), image.ref);
    graph.addPass("WriteAcquiredSwapchainImage")
        .write(backbuffer, arp::rhi::ResourceState::ColorAttachment)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto compiled = graph.compile();
    assert(compiled.externalResources().size() == 1);
    assert(compiled.externalResources().front().presentable);
    assert(compiled.externalResources().front().requiredFinalState
        == arp::rhi::ResourceState::Present);
    graph.execute();
    assert(graph.texture(backbuffer).state() == arp::rhi::ResourceState::Present);
    const auto presented = swapchain->present(frame);
    assert(presented.success);
}

void compiled_graph_can_execute_repeatedly()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    const auto texture = graph.createTransientTexture(transientDesc("RepeatedCompiledGraph"));
    graph.addPass("RepeatedCompiledGraphPass")
        .write(texture, arp::rhi::ResourceState::ColorAttachment)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto compiled = graph.compile();
    const auto before = device->queue().submitCount();
    graph.execute(compiled);
    graph.execute(compiled);
    assert(device->queue().submitCount() == before + 2u);
    assert(graph.texture(texture).state() == arp::rhi::ResourceState::ColorAttachment);
}

void pool_charges_logical_descriptors_and_preserves_physical_unknown()
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    render_graph::FrameGraphResourcePool pool(*device);
    auto desc = transientDesc("LogicalMips");
    desc.extent = {8u, 4u, 1u};
    desc.mipLevelCount = 2u;
    desc.arrayLayerCount = 2u;
    const auto bytes = (8u * 4u + 4u * 2u) * 4u * 2u;
    pool.configureForCompiledGraph(bytes, 1u);
    auto texture = pool.acquireTexture(desc);
    assert(texture && texture->allocation().estimatedSizeBytes != bytes);
    auto fence = core::makeShared<ControlledFence>();
    pool.releaseDeferred(std::move(texture), fence);
    assert(pool.report().pendingBytes == bytes);
    fence->signal();
    pool.recycleCompleted();
    const auto report = pool.report();
    assert(report.pendingBytes == 0u && report.cachedBytes == bytes);
    assert(report.accountingContractVersion == 1u);
    assert(report.accountingDomain == rhi::ResourceCacheAccountingDomain::LogicalDescriptor);
    assert(report.nativePhysicalObservation.state
        == rhi::semantics::PhysicalBudgetObservationState::Unknown);
    assert(rhi::semantics::validPhysicalBudgetObservation(report.nativePhysicalObservation));
    pool.trimCachedResources();

    const auto member = transientDesc("LogicalAliasMembers");
    const auto req = device->queryTexturePlacementRequirements(member);
    auto heap =
        device->createMemoryHeap(req.sizeBytes, req.alignmentBytes, rhi::MemoryUsage::Transient);
    assert(heap);
    render_graph::FrameGraphAliasedResourceGroup group;
    for (unsigned index = 0u; index < 2u; ++index) {
        auto placed = device->createPlacedTexture(member, *heap, 0u);
        assert(placed);
        group.textures.push_back(std::move(placed));
    }
    group.heaps.push_back(std::move(heap));
    const auto groupBytes = 2u * 32u * 32u * 4u;
    pool.configureForCompiledGraph(groupBytes, 2u);
    fence->reset();
    pool.releaseDeferredAliasedGroup(std::move(group.textures), std::move(group.heaps), fence);
    assert(pool.report().pendingBytes == groupBytes);
    pool.recycleCompleted();
    assert(pool.report().pendingBytes == groupBytes);
    fence->signal();
    pool.recycleCompleted();
    assert(pool.report().pendingBytes == 0u);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2) {
        const std::string_view mode(argv[1]);
        if (mode == "configuration-bytes")
            setterOverflowPreservesConfiguration(true);
        else if (mode == "configuration-count")
            setterOverflowPreservesConfiguration(false);
        else if (mode == "configuration-boundaries")
            configurationBoundariesAndRetry();
        else if (mode == "configuration-shrink")
            configurationShrinkPreservesPending();
        else
            return 2;
        return 0;
    }
    assert(argc == 1);
    pool_charges_logical_descriptors_and_preserves_physical_unknown();
    completion_gates_pool_reuse_and_normalizes_state();
    pool_reuse_normalization_is_idempotent_for_undefined_resources();
    barrier_failure_prevents_submit();
    completed_cache_obeys_exact_budget_and_lru();
    aliased_group_waits_for_completion_and_destroys_texture_before_heap();
    completed_aliased_group_is_reused_only_for_the_same_layout();
    ordinary_import_cannot_claim_present_state();
    acquired_swapchain_import_reaches_present_state();
    compiled_graph_can_execute_repeatedly();
    return 0;
}
