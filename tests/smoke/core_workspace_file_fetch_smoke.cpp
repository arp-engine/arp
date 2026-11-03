#include "tests/fixtures/workspace_backend_fixture.h"

EM_JS_DEPS(workspaceOrigin, "$stringToUTF8");
EM_JS(void, workspaceOrigin, (char* output, int size), {
    // clang-format off
    stringToUTF8(window.arpWorkspaceOrigin, output, size);
    // clang-format on
});
EM_JS(int, remainingWorkspaceFetches, (), {
    // clang-format off
    return Module.arpCoreFileTransfers ? Module.arpCoreFileTransfers.size : 0;
    // clang-format on
});
EM_JS(void, reportWorkspaceSuccess, (), {
    // clang-format off
    window.arpWorkspaceReport({success: true, cycles: 20, warmupCycles: 1,
        coreLiveBytesDelta: 0, coreLiveAllocationsDelta: 0,
        pendingFetches: Module.arpCoreFileTransfers.size,
        userAgent: navigator.userAgent, gpuWork: false});
    // clang-format on
});

int main()
{
    char origin[512]{};
    workspaceOrigin(origin, sizeof(origin));
    workspace_backend_fixture::exercise(origin, "cycle-0", [] {}, true);
    const auto before = arp::core::defaultCpuAllocator().stats();
    for (unsigned i = 1; i <= 20; ++i) {
        const auto folder = arp::core::pmr::String("cycle-") + arp::core::formatUnsigned(i);
        workspace_backend_fixture::exercise(origin, folder, [] {}, true);
    }
    const auto after = arp::core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    assert(remainingWorkspaceFetches() == 0);
    std::puts(
        "PASS actual browser FileService workspace: read/query/save/receipt/release, dropped response and ordinary20");
    reportWorkspaceSuccess();
}
