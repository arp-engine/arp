#include "core/cpu_allocator.h"

#include <cassert>
#include <cstddef>
#include <limits>

int main()
{
    using namespace arp::core;

    assert(isValidCpuAllocationAlignment(1u));
    assert(isValidCpuAllocationAlignment(64u));
    assert(!isValidCpuAllocationAlignment(0u));
    assert(!isValidCpuAllocationAlignment(3u));

    const auto alreadyAligned = alignCpuAllocationOffsetUp(256u, 256u);
    assert(alreadyAligned.success);
    assert(alreadyAligned.alignedValue == 256u);
    assert(alreadyAligned.diagnostics == nullptr);

    const auto rounded = alignCpuAllocationOffsetUp(257u, 256u);
    assert(rounded.success);
    assert(rounded.alignedValue == 512u);

    const auto zeroAlignment = alignCpuAllocationOffsetUp(8u, 0u);
    assert(!zeroAlignment.success);
    assert(zeroAlignment.diagnostics != nullptr);

    const auto nonPowerOfTwo = alignCpuAllocationOffsetUp(8u, 3u);
    assert(!nonPowerOfTwo.success);
    assert(nonPowerOfTwo.diagnostics != nullptr);

    const auto overflow =
        alignCpuAllocationOffsetUp(std::numeric_limits<std::size_t>::max() - 7u, 16u);
    assert(!overflow.success);
    assert(overflow.diagnostics != nullptr);

    return 0;
}
