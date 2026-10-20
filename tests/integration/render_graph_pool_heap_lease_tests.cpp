#include "renderer/render_graph/frame_graph_resource_pool.h"
#include "rhi/backends/null/null_backend.h"
#include "tests/fixtures/cpu_quota_factory_fixture.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace {
using namespace arp;
using Pool = render_graph::FrameGraphResourcePool;
constexpr std::uint64_t bytes = 65536u;

auto quota()
{
    return core::makeShared<core::CpuMemoryQuota>(bytes);
}

rhi::semantics::FrameGraphPoolAllocationBudget budget()
{
    rhi::semantics::FrameGraphPoolAllocationBudget b;
    b.contractVersion = 1u;
    b.prototypeOptIn = true;
    b.maxCpuMetadataBytes = bytes;
    b.heapLeases = {1u, true, 2u, 256u};
    return b;
}

void prepare(rhi::GpuDevice& device, bool heaps = true)
{
    assert(!device.prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, quota()));
    rhi::OriginalResourceRegistrationBudget b{1u, true, 0u, 4u, 0u, 19u, bytes};
    if (heaps)
        b.heaps = {1u, true, 4u};
    assert(!device.prepareOriginalResourceRegistration(b, quota()));
}

template <class Action> void rejects(Action&& action, const char* expected)
{
    bool failed = false;
    try {
        action();
    } catch (const std::exception& e) {
        failed = true;
        assert(std::string_view(e.what()) == expected);
    }
    assert(failed);
}

auto acquire(Pool& pool, std::uint64_t size = 128u)
{
    return pool.acquireMemoryHeap(size, 16u, rhi::MemoryUsage::Transient);
}

void contracts()
{
    using C = rhi::semantics::FrameGraphPoolAllocationContract;
    auto b = budget();
    assert(!C::validateIntent(b));
    b.heapLeases = {};
    assert(C::validateIntent(b));
    b = budget();
    b.heapLeases.prototypeOptIn = false;
    assert(C::validateIntent(b));
    b = budget();
    b.heapLeases.contractVersion = 2u;
    assert(C::validateIntent(b));
    b = budget();
    b.heapLeases.maxHeapLeases = 0u;
    assert(C::validateIntent(b));
    b = budget();
    b.heapLeases.maxLogicalHeapReservationBytes = 0u;
    assert(C::validateIntent(b));
    assert(C::heapLowering(rhi::Backend::Null, {}).diagnostic);
    for (const auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL,
             static_cast<rhi::Backend>(99)}) {
        const auto d = C::heapLowering(backend, budget().heapLeases);
        const bool allowed = backend == rhi::Backend::Null || backend == rhi::Backend::Vulkan;
        assert(d.allocationOwnerAvailable == allowed && bool(d.diagnostic) != allowed);
        assert(!d.nativeExecutionProven);
        assert(d.validatorOnly == (backend == rhi::Backend::Null));
    }
}

void ordinary()
{
    auto device = rhi::createNullDevice();
    prepare(*device);
    Pool pool(*device), foreign(*device), legacy(*device);
    auto metadata = quota();
    assert(!pool.prepareAllocationAccounting(budget(), metadata));
    assert(!foreign.prepareAllocationAccounting(budget(), quota()));
    rejects(
        [&] {
            acquire(legacy);
        },
        "RG-POOL-HEAP-OPT-IN");
    auto a = acquire(pool), b = acquire(pool);
    assert(a && b && a->originalPoolAllocationCharged());
    assert(!pool.validateAllocationAccounting(*a) && foreign.validateAllocationAccounting(*a));
    assert(pool.prepareAllocationAccounting(budget(), quota()));
    auto report = pool.allocationAccountingReport();
    assert(report.heapLeases == 2u && report.logicalHeapReservationBytes == 256u);
    assert(
        report.heapLeaseLowering.validatorOnly && !report.heapLeaseLowering.nativeExecutionProven);
    rejects(
        [&] {
            acquire(pool);
        },
        "RG-POOL-ALLOCATION-RESOURCE-CAPACITY");
    b.reset();
    auto constrained = budget();
    a.reset();
    constrained.heapLeases.maxHeapLeases = 1u;
    assert(!pool.prepareAllocationAccounting(constrained, quota()));
    a = acquire(pool, 64u);
    rejects(
        [&] {
            acquire(pool, 64u);
        },
        "RG-POOL-ALLOCATION-RESOURCE-CAPACITY");
    a.reset();
    constrained = budget();
    constrained.heapLeases.maxLogicalHeapReservationBytes = 128u;
    assert(!pool.prepareAllocationAccounting(constrained, quota()));
    a = acquire(pool);
    rejects(
        [&] {
            acquire(pool, 1u);
        },
        "RG-POOL-ALLOCATION-RESOURCE-CAPACITY");
    a.reset();
    assert(!pool.allocationAccountingReport().heapLeases);
    // A return with no textures must not bypass the charged-heap gate.
    render_graph::FrameGraphAliasedResourceGroup group;
    group.heaps.push_back(acquire(pool));
    rejects(
        [&] {
            legacy.releaseDeferredAliasedGroup(std::move(group), {}, {});
        },
        "RG-POOL-ALLOCATION-RETURN-UNAVAILABLE");
    assert(!pool.allocationAccountingReport().heapLeases);
    core::pmr::Vector<core::UniquePtr<rhi::GpuMemoryHeap>> heaps;
    heaps.push_back(acquire(pool));
    rejects(
        [&] {
            legacy.releaseDeferredAliasedGroup({}, std::move(heaps), {});
        },
        "RG-POOL-ALLOCATION-RETURN-UNAVAILABLE");
    assert(!pool.allocationAccountingReport().heapLeases);
}

void lifetime()
{
    auto device = rhi::createNullDevice();
    prepare(*device);
    auto pool = core::makeUnique<Pool>(*device);
    auto metadata = quota();
    assert(!pool->prepareAllocationAccounting(budget(), metadata));
    auto heap = acquire(*pool);
    const auto ticket = heap->originalResourceRegistration();
    rhi::TextureDesc desc;
    desc.extent = {4u, 4u, 1u};
    desc.format = rhi::TextureFormat::Rgba8Unorm;
    desc.usage = rhi::TextureUsage::Sampled;
    auto texture = device->createPlacedTexture(desc, *heap, 0u);
    assert(texture && !texture->originalPoolAllocationCharged());
    heap.reset();
    assert(ticket.diagnostic() && pool->allocationAccountingReport().heapLeases == 1u);
    pool.reset();
    device.reset();
    assert(metadata->liveBytes());
    const auto* id =
        test::CpuQuotaFactoryFixture::verifyHotNoAllocation<1>({metadata}, [&]() noexcept {
            texture.reset();
            return static_cast<const char*>(nullptr);
        });
    assert(!id && !metadata->liveBytes());
}

void negative()
{
    auto device = rhi::createNullDevice();
    prepare(*device, false);
    Pool missing(*device);
    assert(std::string_view(missing.prepareAllocationAccounting(budget(), quota()))
        == "RG-POOL-HEAP-REGISTRATION-REQUIRED");
    auto other = rhi::createNullDevice();
    prepare(*other);
    Pool a(*other), b(*other);
    auto metadata = quota();
    assert(!a.prepareAllocationAccounting(budget(), metadata));
    assert(!b.prepareAllocationAccounting(budget(), quota()));
    auto first = acquire(a), second = acquire(b);
    assert(a.validateAllocationAccounting(*second));
    const char* diagnostic = nullptr;
    const auto* receipt = other->originalPoolAllocationReceipt(*first, diagnostic);
    assert(receipt && !diagnostic && !receipt->nativeExecutionProven());
    std::shared_ptr<const rhi::OriginalPoolAllocationReceipt> alias(
        std::shared_ptr<void>{}, receipt);
    assert(std::string_view(other->bindOriginalPoolAllocationReceipt(*first, alias))
        == "RG-POOL-ALLOCATION-ALREADY-BOUND");
    assert(std::string_view(other->bindOriginalPoolAllocationReceipt(*second, alias))
        == "RG-POOL-ALLOCATION-RECEIPT-RESOURCE");
    assert(device->bindOriginalPoolAllocationReceipt(*first, alias));
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation<1>({metadata}, [&]() noexcept {
        for (unsigned i = 0u; i < 10000u; ++i)
            if (const auto* id = a.validateAllocationAccounting(*first))
                return id;
        first.reset();
        return static_cast<const char*>(nullptr);
    }));
    // Alias is a non-owning test borrow; never dereference it after release.
    alias.reset();
    other->retireOriginalDeviceLifetime();
    rejects(
        [&] {
            acquire(b);
        },
        "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
}

void quotaBoundaries()
{
    auto device = rhi::createNullDevice();
    prepare(*device);
    Pool pool(*device);
    auto metadata = quota();
    assert(!pool.prepareAllocationAccounting(budget(), metadata));
    const auto baseline = metadata->liveBytes();
    const auto attempt = [&]() -> core::UniquePtr<rhi::GpuMemoryHeap> {
        try {
            return acquire(pool);
        } catch (const std::bad_alloc&) {
            return {};
        }
    };
    test::CpuQuotaFactoryFixture pressure(metadata);
    const auto exact = pressure.measureSingleUse([] {}, attempt);
    for (std::uint64_t available = 0u; available <= exact; ++available) {
        {
            core::CpuMemoryQuotaLease full(metadata, bytes - baseline - available);
            assert(full.charged());
            auto heap = attempt();
            assert(bool(heap) == (available == exact));
            assert(pool.allocationAccountingReport().heapLeases == (heap ? 1u : 0u));
        }
        assert(metadata->liveBytes() == baseline);
        assert(
            !device->memoryStats().allocatedBytes && !pool.allocationAccountingReport().heapLeases);
    }
    auto replacement = quota();
    {
        core::CpuMemoryQuotaLease full(replacement, bytes);
        assert(full.charged());
        assert(pool.prepareAllocationAccounting(budget(), replacement));
    }
    assert(pool.allocationAccountingReport().allocatedCpuMetadataBytes == baseline);
    std::printf(
        "pool heap lease: exact receipt CPU bytes=%llu; all publication boundaries rolled back\n",
        static_cast<unsigned long long>(exact));
}
} // namespace

int main(int argc, char** argv)
{
    const std::string_view mode = argc > 1 ? argv[1] : "contract";
    if (mode == "contract")
        contracts();
    else if (mode == "null")
        ordinary();
    else if (mode == "lifetime")
        lifetime();
    else if (mode == "negative")
        negative();
    else if (mode == "quota")
        quotaBoundaries();
    else
        return 2;
    return 0;
}
