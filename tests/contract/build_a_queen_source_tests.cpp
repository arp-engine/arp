#include "tests/fixtures/queen_content_probe.h"

#include <cassert>
#include <cstdio>

namespace {
using namespace arp;

using ContentProbe = queen_test::ContentProbe;

std::uint64_t cycle(const core::pmr::String& root)
{
    ContentProbe content;
    assert(content.load(root));
    content.verify();
    return content.staticFingerprint();
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc >= 2);
    const core::pmr::String root(argv[1]);
    const auto expected = cycle(root);
    const auto before = core::defaultCpuAllocator().stats();
    const auto count = argc > 2 ? 20 : 1;
    for (int i = 0; i < count; ++i)
        assert(cycle(root) == expected);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::printf("Queen static content fingerprint: %016llx\n", (unsigned long long)expected);
    std::printf(
        "Queen production content source %d ordinary cycles: core bytes %llu -> %llu, live %llu -> %llu\n",
        count,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
}
