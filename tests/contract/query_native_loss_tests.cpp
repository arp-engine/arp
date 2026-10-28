#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"
#include "tests/fixtures/original_query_native_loss_fixture.h"

#include <cassert>
#include <cstdio>
#include <string_view>

#ifdef NDEBUG
#error Query loss tests require assertions
#endif

int main(int argc, char** argv)
{
    using namespace arp;
    test::OriginalQueryNativeLossFixture::verifyContract();
    auto device = rhi::createNullDevice();
    assert(device);
    const auto budget = test::OriginalQueryNativeLossFixture::budget();
    auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
    const rhi::OriginalDeviceNativeLossObservation unissued;
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    const auto* id =
        device->prepareOriginalTypedNativeLossRegistration({3u, true}, budget, quota, unissued);
    test::OriginalQueryNativeLossFixture::expect(id, "RHI-ORIGINAL-QUERY-LOSS-VALIDATOR-ONLY");
    assert(
        !quota->liveBytes() && allocations == core::defaultCpuAllocator().stats().allocationCount);
    if (argc == 2 && std::string_view(argv[1]) == "--null-hidden-frame") {
        rhi::NullHiddenFrameSmokeRunner runner(*device);
        const auto report = runner.run({});
        assert(report.success && report.submittedFrameCount == 2u);
        std::puts("QUERY_LOSS_NULL_SMOKE hidden-frames2 validator-only no-native-query PASS");
    }
}
