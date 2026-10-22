#include "core/memory/cpu_allocator.h"

#include <cassert>
#include <cstdint>
#include <memory_resource>
#include <vector>

int main()
{
    using namespace arp::core;

    auto& allocator = defaultCpuAllocator();
    assert(allocator.backendName() == "mimalloc");

    const auto before = allocator.stats();
    CpuAllocationDesc desc;
    desc.sizeBytes = 128u;
    desc.alignment = 64u;
    desc.intent = CpuAllocationIntent::Telemetry;
    desc.label = "MimallocSmokeAlignedBlock";

    const auto result = allocator.allocate(desc);
    assert(result.success());
    assert(
        (reinterpret_cast<std::uintptr_t>(result.ptr) % static_cast<std::uintptr_t>(desc.alignment))
        == 0u);

    auto* bytes = static_cast<std::uint8_t*>(result.ptr);
    bytes[0] = 0x7fu;
    bytes[127] = 0x42u;
    allocator.deallocate(result.ptr, desc);

    const auto after = allocator.stats();
    assert(after.liveBytes == before.liveBytes);
    assert(after.byIntent(CpuAllocationIntent::Telemetry).liveBytes == 0u);

    std::pmr::vector<std::uint32_t> values{&defaultCpuMemoryResource()};
    values.resize(8u, 11u);
    assert(values[7] == 11u);

    return 0;
}
