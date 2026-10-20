#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_backend_internal.h"
#include "rhi/frontend/heap_placement_contract.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <string_view>

using namespace arp::rhi;
namespace core = arp::core;

int main(int argc, char** argv)
{
    using C = HeapPlacementContract;
    static_assert(!core::isPowerOfTwoUnsigned(0u));
    static_assert(core::isPowerOfTwoUnsigned(1u));
    static_assert(!core::isPowerOfTwoUnsigned(3u));
    static_assert(core::isPowerOfTwoUnsigned(std::uint64_t{1} << 63u));
    const auto allocationsBefore = core::defaultCpuAllocator().stats().allocationCount;
    assert(C::lowering(Backend::Null) == C::Disposition::ValidatorOnly);
    assert(C::lowering(Backend::Vulkan) == C::Disposition::Native);
    assert(C::lowering(Backend::Metal) == C::Disposition::Native);
    assert(C::lowering(Backend::WebGPU) == C::Disposition::DiagnosticFailure);
    assert(C::lowering(Backend::WebGL) == C::Disposition::DiagnosticFailure);
    assert(C::lowering(static_cast<Backend>(99)) == C::Disposition::DiagnosticFailure);
    assert(!C::heap(1024u, 256u, MemoryUsage::Transient));
    assert(C::heap(0u, 1u, MemoryUsage::GpuOnly));
    for (auto alignment : {0u, 3u, 255u})
        assert(C::heap(1024u, alignment, MemoryUsage::GpuOnly));
    for (auto usage : {MemoryUsage::CpuToGpu, MemoryUsage::GpuToCpu, static_cast<MemoryUsage>(99)})
        assert(C::heap(1024u, 256u, usage));
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    assert(!C::placement(maximum, 1u, maximum - 1u, {1u, 1u}));
    assert(C::placement(maximum, 1u, maximum, {1u, 1u}));
    assert(C::placement(maximum, 1u, maximum - 1u, {2u, 1u}));
    assert(!C::placement(1024u, 256u, 768u, {256u, 128u}));
    assert(C::placement(1024u, 256u, 769u, {256u, 1u}));
    assert(C::placement(1024u, 256u, 1024u, {256u, 256u}));
    TextureDesc desc;
    desc.extent = {4u, 4u, 1u};
    desc.format = TextureFormat::Rgba8Unorm;
    desc.usage = TextureUsage::Sampled;
    std::uint64_t bytes = 0u;
    assert(!C::texture(desc, bytes) && bytes == 64u);
    auto invalid = desc;
    invalid.sampleCount = 3u;
    assert(C::texture(invalid, bytes) && bytes == 0u);
    invalid = desc;
    invalid.mipLevelCount = 100u;
    assert(C::texture(invalid, bytes) && bytes == 0u);
    invalid = desc;
    invalid.memory = MemoryUsage::GpuToCpu;
    assert(C::texture(invalid, bytes));
    invalid = desc;
    invalid.extent.width = invalid.extent.height = std::numeric_limits<std::uint32_t>::max();
    assert(C::texture(invalid, bytes) && bytes == 0u);
    invalid = desc;
    invalid.initialState = ResourceState::CopyDst;
    assert(C::texture(invalid, bytes));
    invalid = desc;
    invalid.cubeCompatible = true;
    assert(C::texture(invalid, bytes));
    assert(core::defaultCpuAllocator().stats().allocationCount == allocationsBefore);
    const std::string_view mode = argc > 1 ? argv[1] : "all";
    if (mode == "pure") {
        std::puts("HeapPlacementAdmission/1 pure contract: PASS");
        return 0;
    }
    auto device = createNullDevice();
    auto other = createNullDevice();
    if (mode == "all" || mode == "admission") {
        assert(!device->createMemoryHeap(1024u, 0u, MemoryUsage::Transient));
        assert(device->lastMemoryHeapCreationDiagnostic() == "RHI-HEAP-ALIGNMENT-INVALID");
        assert(!device->createMemoryHeap(1024u, 256u, MemoryUsage::CpuToGpu));
        assert(device->lastMemoryHeapCreationDiagnostic() == "RHI-HEAP-MEMORY-INTENT");
    }
    auto heap = device->createMemoryHeap(1024u, 256u, MemoryUsage::Transient);
    assert(heap && !heap->supportsAliasing());
    assert(device->lastMemoryHeapCreationDiagnostic().empty());
    if (mode == "all" || mode == "owner")
        assert(!other->createPlacedTexture(desc, *heap, 0u));
    if (mode == "all" || mode == "range") {
        assert(!device->createPlacedTexture(desc, *heap, 1u));
        assert(!device->createPlacedTexture(desc, *heap, 1024u));
    }
    auto texture = device->createPlacedTexture(desc, *heap, 256u);
    assert(texture);
    heap.reset();
    if (mode == "all" || mode == "custody")
        assert(device->memoryStats().allocatedBytes >= 1024u);
    texture.reset();
    assert(device->memoryStats().allocatedBytes == 0u);
    auto layered = desc;
    layered.mipLevelCount = 3u;
    layered.arrayLayerCount = 2u;
    layered.memory = MemoryUsage::Transient;
    const auto queried = device->queryTexturePlacementRequirements(layered);
    assert(queried.sizeBytes == 168u && queried.alignmentBytes == 256u);
    assert(device->queryTexturePlacementRequirements(invalid).sizeBytes == 0u);
    auto layeredHeap = device->createMemoryHeap(1024u, 256u, MemoryUsage::Transient);
    auto layeredTexture = device->createPlacedTexture(layered, *layeredHeap, 256u);
    assert(layeredTexture && layeredTexture->allocation().estimatedSizeBytes == 168u);
    assert(device->memoryStats().allocatedBytes == 1192u);
    layeredHeap.reset();
    assert(device->memoryStats().allocatedBytes == 1192u);
    layeredTexture.reset();
    assert(device->memoryStats().allocatedBytes == 0u);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!device->createMemoryHeap(1024u, 256u, MemoryUsage::Transient));
        assert(
            device->lastMemoryHeapCreationDiagnostic() == "RHI-NULL-HEAP-HOST-ALLOCATION-FAILED");
    }
    assert(device->memoryStats().allocatedBytes == 0u && quota->liveBytes() == 0u);
    // Force registry publication to throw after construction has charged the
    // texture. Both its charge and retained heap lease must unwind exactly.
    auto ledger = core::makeShared<null_backend::MemoryLedger>();
    auto registry =
        core::makeShared<null_backend::ResourceRegistry>(std::pmr::null_memory_resource());
    auto lease = core::makeShared<null_backend::MemoryLedgerLease>(ledger, 1024u);
    bool failed = false;
    try {
        auto unpublished = null_backend::createTexture(desc, ledger, registry, lease);
        assert(!unpublished);
    } catch (const std::bad_alloc&) {
        failed = true;
    }
    assert(failed && ledger->stats().allocatedBytes == 1024u
        && ledger->stats().liveResourceCount == 1u);
    lease.reset();
    assert(ledger->stats().allocatedBytes == 0u && ledger->stats().liveResourceCount == 0u);
    std::puts("HeapPlacementAdmission/1 pure and Null validator: PASS");
}
