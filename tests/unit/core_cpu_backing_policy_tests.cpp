#include "core/memory/cpu_allocator.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mimalloc.h>

namespace {
using namespace arp::core;

std::uint64_t live(const CpuMemoryStats& value)
{
    return value.allocationCount - value.deallocationCount;
}

void cycle()
{
    auto quota = makeShared<CpuMemoryQuota>(64u * 1024u * 1024u);
    {
        CpuMemoryQuotaScope scope(quota);
        // Different size classes and simultaneous ownership exercise backing
        // reuse rather than merely allocating the same single small block.
        std::array<pmr::Vector<std::uint8_t>, 4> buffers;
        constexpr std::array<std::size_t, 4> sizes{
            256u * 1024u, 1024u * 1024u, 8u * 1024u * 1024u, 16u * 1024u * 1024u};
        for (std::size_t i = 0; i < buffers.size(); ++i) {
            buffers[i].resize(sizes[i]);
            buffers[i].front() = static_cast<std::uint8_t>(17 + i);
            buffers[i].back() = static_cast<std::uint8_t>(31 + i);
        }
        for (std::size_t i = 0; i < buffers.size(); ++i)
            assert(buffers[i].front() == 17 + i && buffers[i].back() == 31 + i);
        const CpuAllocationDesc aligned{
            257, 256, CpuAllocationIntent::General, "backing-alignment"};
        const auto allocation = defaultCpuAllocator().allocate(aligned);
        assert(allocation.success() && reinterpret_cast<std::uintptr_t>(allocation.ptr) % 256 == 0);
        defaultCpuAllocator().deallocate(allocation.ptr, aligned);
        assert(quota->peakBytes() <= quota->limitBytes());
    }
    assert(quota->liveBytes() == 0);
    auto bounded = makeShared<CpuMemoryQuota>(1);
    {
        CpuMemoryQuotaScope scope(bounded);
        const auto refused = defaultCpuAllocator().allocate(
            {2, alignof(std::max_align_t), CpuAllocationIntent::General, "backing-rejection"});
        assert(!refused.success() && bounded->liveBytes() == 0);
    }
}
} // namespace

int main(int argc, char** argv)
{
    // The header's PMR bootstrap already constructed the actual raw backend.
    // Inspect before this test's first allocation; no test-side option setters.
    const auto reserve = mi_option_get(mi_option_arena_reserve);
#if defined(__EMSCRIPTEN__)
    assert(reserve == 0);
#else
    if (const auto* expected = std::getenv("ARP_TEST_ARENA_RESERVE_KIB")) {
        char* end = nullptr;
        const auto value = std::strtol(expected, &end, 10);
        std::fprintf(stderr, "Native arena reserve: expected=%ld actual=%ld\n", value, reserve);
        assert(end && !*end && value > 0 && reserve == value);
    }
#endif
    assert(defaultCpuAllocator().backendName() == "mimalloc");
    cycle();
    const auto before = defaultCpuAllocator().stats();
    const unsigned cycles = argc > 1 && std::strcmp(argv[1], "--smoke") == 0 ? 20 : 1;
    for (unsigned i = 0; i < cycles; ++i) {
        cycle();
        const auto after = defaultCpuAllocator().stats();
        assert(after.liveBytes == before.liveBytes && live(after) == live(before));
        assert(mi_option_get(mi_option_arena_reserve) == reserve);
    }
    std::printf("PASS CPU backing arenaReserveKiB=%ld cycles=%u core=%llu/%llu\n",
        reserve,
        cycles,
        (unsigned long long)before.liveBytes,
        (unsigned long long)live(before));
}
