#include "rhi/backends/null/null_backend.h"
#include "tests/fixtures/cpu_quota_factory_fixture.h"

#ifdef ARP_TEST_VULKAN
#include "tests/fixtures/vulkan_heap_owner_fixture.h"
#endif

#include <cassert>
#include <cstdio>
#include <string_view>

namespace {
using namespace arp;
constexpr std::uint64_t cpuBytes = 65536u;

auto quota()
{
    return core::makeShared<core::CpuMemoryQuota>(cpuBytes);
}

rhi::OriginalResourceRegistrationBudget budget()
{
    rhi::OriginalResourceRegistrationBudget b;
    b.contractVersion = 1u;
    b.prototypeOptIn = true;
    b.maxTextures = 2u;
    b.maxRetainedOwners = 11u;
    b.maxCpuMetadataBytes = cpuBytes;
    b.heaps = {1u, true, 1u};
    return b;
}

void prepare(rhi::GpuDevice& device, const std::shared_ptr<core::CpuMemoryQuota>& metadata)
{
    assert(!device.prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, quota()));
    assert(!device.prepareOriginalResourceRegistration(budget(), metadata));
}

rhi::TextureDesc textureDesc()
{
    rhi::TextureDesc desc;
    desc.extent = {4u, 4u, 1u};
    desc.format = rhi::TextureFormat::Rgba8Unorm;
    desc.usage = rhi::TextureUsage::Sampled;
    return desc;
}

class HeapFacade final : public rhi::GpuMemoryHeap {
public:
    explicit HeapFacade(const rhi::GpuMemoryHeap& source) : source_(source) {}

    rhi::GpuResourceId id() const override { return source_.id(); }

    std::uint64_t sizeBytes() const override { return source_.sizeBytes(); }

    std::uint64_t alignmentBytes() const override { return source_.alignmentBytes(); }

    bool supportsAliasing() const override { return source_.supportsAliasing(); }

private:
    const rhi::GpuMemoryHeap& source_;
};

void contracts()
{
    using Contract = rhi::OriginalResourceRegistrationContract;
    using Kind = rhi::OriginalRegisteredResourceKind;
    static_assert(static_cast<unsigned>(Kind::Sampler) == 10u);
    static_assert(static_cast<unsigned>(Kind::MemoryHeap) == 11u);
    auto b = budget();
    assert(!Contract::validateIntent(b));
    b.heaps.prototypeOptIn = false;
    assert(Contract::validateIntent(b));
    b = budget();
    b.heaps.contractVersion = 2u;
    assert(Contract::validateIntent(b));
    b = budget();
    b.heaps.maxHeaps = 0u;
    assert(Contract::validateIntent(b));
    b = budget();
    b.maxTextures = 0u;
    assert(!Contract::validateIntent(b));
    b.contractVersion = 7u;
    assert(std::string_view(Contract::validateIntent(b))
        == "RHI-ORIGINAL-HEAP-NATIVE-LOSS-JOIN-UNAVAILABLE");
    assert(Contract::heapLowering(rhi::Backend::Null, {}).diagnostic);
    for (const auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL,
             static_cast<rhi::Backend>(99)}) {
        const auto plan = Contract::heapLowering(backend, budget().heaps);
        const bool available = backend == rhi::Backend::Null || backend == rhi::Backend::Vulkan;
        assert(plan.typedLifetimeIssuerAvailable == available);
        assert(bool(plan.diagnostic) != available);
        assert(!plan.executionAvailable && !plan.originalStateIssuerAvailable);
    }
}

void identity(rhi::GpuDevice& device, rhi::GpuDevice& other)
{
    auto metadata = quota();
    prepare(device, metadata);
    prepare(other, quota());
    const auto req = device.queryTexturePlacementRequirements(textureDesc());
    assert(req.sizeBytes && req.alignmentBytes);
    auto heap =
        device.createMemoryHeap(req.sizeBytes, req.alignmentBytes, rhi::MemoryUsage::Transient);
    assert(heap && !device.validateOriginalResourceRegistration(*heap));
    const auto ticket = heap->originalResourceRegistration();
    const auto originalGeneration = ticket.generation();
    assert(ticket.matchesIdentity(
        heap.get(), heap->id().value, rhi::OriginalRegisteredResourceKind::MemoryHeap));
    assert(!ticket.nativeExecutionProven());
    HeapFacade sameId(*heap);
    assert(device.validateOriginalResourceRegistration(sameId));
    assert(!device.createPlacedTexture(textureDesc(), sameId, 0u));
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation<1>({metadata}, [&]() noexcept {
        for (unsigned i = 0u; i < 10000u; ++i)
            if (const auto* id = device.originalMemoryHeapPlacementDiagnostic(*heap))
                return id;
        return static_cast<const char*>(nullptr);
    }));
    assert(other.validateOriginalResourceRegistration(*heap));
    assert(!other.createPlacedTexture(textureDesc(), *heap, 0u));
    auto texture = device.createPlacedTexture(textureDesc(), *heap, 0u);
    assert(texture && !device.validateOriginalResourceRegistration(*texture));
    const auto before = device.memoryStats();
    assert(
        !device.createMemoryHeap(req.sizeBytes, req.alignmentBytes, rhi::MemoryUsage::Transient));
    assert(device.lastOriginalResourceRegistrationDiagnostic()
        == "RHI-ORIGINAL-REGISTRATION-KIND-CAPACITY");
    assert(device.memoryStats().allocatedBytes == before.allocatedBytes);
    heap.reset();
    assert(ticket.diagnostic());
    auto next =
        device.createMemoryHeap(req.sizeBytes, req.alignmentBytes, rhi::MemoryUsage::Transient);
    assert(next && next->originalResourceRegistration().generation() > originalGeneration);
    device.retireOriginalDeviceLifetime();
    assert(device.validateOriginalResourceRegistration(*next));
    assert(!device.createPlacedTexture(textureDesc(), *next, 0u));
    next.reset();
    texture.reset();
}

void nullQuota()
{
    auto device = rhi::createNullDevice();
    auto metadata = quota();
    prepare(*device, metadata);
    const auto baseline = metadata->liveBytes();
    const auto req = device->queryTexturePlacementRequirements(textureDesc());
    const auto factory = [&]() {
        return device->createMemoryHeap(
            req.sizeBytes, req.alignmentBytes, rhi::MemoryUsage::Transient);
    };
    test::CpuQuotaFactoryFixture pressure(metadata);
    const auto exact = pressure.measureSingleUse([] {}, factory);
    assert(metadata->liveBytes() == baseline);
    for (std::uint64_t available = 0u; available <= exact; ++available) {
        {
            core::CpuMemoryQuotaLease occupied(metadata, cpuBytes - baseline - available);
            assert(occupied.charged());
            auto heap = factory();
            assert(bool(heap) == (available == exact));
            assert(device->memoryStats().allocatedBytes == (heap ? req.sizeBytes : 0u));
        }
        assert(metadata->liveBytes() == baseline);
        assert(!device->memoryStats().allocatedBytes);
    }
    // A failed backend factory must return its canonical reservation too.
    assert(!device->createMemoryHeap(0u, req.alignmentBytes, rhi::MemoryUsage::Transient));
    assert(metadata->liveBytes() == baseline);
    assert(factory());
    auto resourceQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    {
        core::CpuMemoryQuotaLease full(resourceQuota, cpuBytes);
        assert(full.charged());
        core::CpuMemoryQuotaScope scope(resourceQuota);
        assert(!factory());
    }
    assert(metadata->liveBytes() == baseline && !device->memoryStats().allocatedBytes);
    std::printf("heap registration: exact record bytes=%llu; all quota boundaries rolled back\n",
        static_cast<unsigned long long>(exact));
}

void legacy()
{
    auto device = rhi::createNullDevice();
    assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, quota()));
    auto b = budget();
    b.heaps = {};
    assert(!device->prepareOriginalResourceRegistration(b, quota()));
    const auto req = device->queryTexturePlacementRequirements(textureDesc());
    auto heap =
        device->createMemoryHeap(req.sizeBytes, req.alignmentBytes, rhi::MemoryUsage::Transient);
    assert(heap && heap->originalResourceRegistration().diagnostic());
    assert(device->createPlacedTexture(textureDesc(), *heap, 0u));
    {
        auto owner = rhi::createNullDevice();
        prepare(*owner, quota());
        auto registered =
            owner->createMemoryHeap(req.sizeBytes, req.alignmentBytes, rhi::MemoryUsage::Transient);
        assert(registered);
        const auto ticket = registered->originalResourceRegistration();
        owner.reset();
        assert(ticket.diagnostic());
        assert(!device->createPlacedTexture(textureDesc(), *registered, 0u));
    }
    {
        auto owner = rhi::createNullDevice();
        auto domain = quota();
        assert(!owner->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, domain));
        auto b = budget();
        b.heaps.maxHeaps = 2u;
        b.maxRetainedOwners = 5u;
        assert(!owner->prepareOriginalResourceRegistration(b, quota()));
        auto first =
            owner->createMemoryHeap(req.sizeBytes, req.alignmentBytes, rhi::MemoryUsage::Transient);
        assert(first);
        assert(!owner->createMemoryHeap(
            req.sizeBytes, req.alignmentBytes, rhi::MemoryUsage::Transient));
        assert(owner->lastOriginalResourceRegistrationDiagnostic()
            == "RHI-ORIGINAL-REGISTRATION-OWNER-CAPACITY");
    }
}
} // namespace

int main(int argc, char** argv)
{
    const std::string_view mode = argc > 1 ? argv[1] : "contract";
    if (mode == "contract")
        contracts();
    else if (mode == "null") {
        auto device = rhi::createNullDevice(), other = rhi::createNullDevice();
        identity(*device, *other);
        legacy();
    } else if (mode == "quota")
        nullQuota();
#ifdef ARP_TEST_VULKAN
    else if (mode == "native") {
        tests::VulkanHeadlessRuntime native;
        if (!native.runtime() || !native.runtime()->valid())
            return 77;
        auto device = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime());
        auto other = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime());
        identity(*device, *other);
    }
#endif
    else
        return 2;
    return 0;
}
