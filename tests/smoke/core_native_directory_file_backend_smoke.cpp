#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstdio>

namespace {
using namespace arp::core;
using namespace arp::core::io;
using namespace unified_io_fixture;

#if defined(_WIN32)
template <class Predicate> void pump(runtime::AsyncRuntime& runtime, Predicate done)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!done() && std::chrono::steady_clock::now() < deadline) {
        runtime.dispatch();
        std::this_thread::yield();
    }
    assert(done());
}

void cycle(const Workspace& workspace)
{
    auto local = makeShared<NativeDirectoryFileBackend>(workspace.descriptor());
    auto mounts =
        makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 4, 256 * 1024, 16, 1024});
    auto mount = mounts->mount("workspace", local);
    assert(mount);
    runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = 8;
    desc.completionBudget = 8;
    desc.enableWork = false;
    desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = mounts;
    runtime::AsyncRuntime runtime(desc);
    assert(runtime.valid());
    Probe<FileQueryResult> probe;
    const auto perform = [&](FileQueryRequest request) {
        request.mount = mount;
        const auto count = probe.calls;
        assert(runtime.fileService().query(request, {Probe<FileQueryResult>::complete, &probe})
            == FileIoStatus::Accepted);
        pump(runtime, [&] {
            return probe.calls == count + 1;
        });
        assert(probe.result.status == FileIoStatus::Success);
    };
    auto stat = query(FileQueryOperation::Stat, "a.bin");
    stat.versionPolicy = FileVersionPolicy::ContentSha256;
    stat.maxVersionBytes = 6;
    perform(stat);
    FileReadRequest read;
    read.pathScope = FilePathScope::MountedResource;
    read.path = "a.bin";
    read.mount = mount;
    read.contractVersion = 1;
    read.maxBytes = 3;
    read.range = FileReadRange::exactRange(1, 3);
    read.expectedVersion = probe.result.metadata.version;
    read.maxVersionBytes = 6;
    Probe<FileReadResult> bytes;
    assert(runtime.fileService().read(read, {Probe<FileReadResult>::complete, &bytes})
        == FileIoStatus::Accepted);
    pump(runtime, [&] {
        return bytes.calls == 1;
    });
    assert(bytes.result.status == FileIoStatus::Success && bytes.result.bytes.size() == 3
        && bytes.result.bytes[0] == 'b' && bytes.result.bytes[2] == 'd');
    auto list = query(FileQueryOperation::Enumerate);
    for (const auto* name : {"a.bin", "b.bin", "nested"}) {
        perform(list);
        assert(probe.result.entries.size() == 1 && probe.result.entries[0].name == name);
        list.cursor = probe.result.next;
    }
    assert(list.cursor.snapshot.kind == FileVersionKind::None);
    stat.mount = mount;
    auto count = probe.calls;
    assert(runtime.fileService().query(stat, {Probe<FileQueryResult>::complete, &probe})
        == FileIoStatus::Accepted);
    assert(mounts->unmount(mount));
    mount = mounts->mount("workspace", local);
    assert(mount && !(mount == stat.mount));
    pump(runtime, [&] {
        return probe.calls == count + 1;
    });
    assert(probe.result.status == FileIoStatus::StaleLocation && probe.result.entries.empty());
    stat.mount = mount;
    count = probe.calls;
    {
        runtime::CancellationSource cancel;
        stat.cancellation = cancel.token();
        assert(runtime.fileService().query(stat, {Probe<FileQueryResult>::complete, &probe})
            == FileIoStatus::Accepted);
        cancel.cancel();
    }
    pump(runtime, [&] {
        return probe.calls == count + 1;
    });
    assert(probe.result.status == FileIoStatus::Cancelled);
    stat.cancellation = {};
    count = probe.calls;
    assert(runtime.fileService().query(stat, {Probe<FileQueryResult>::complete, &probe})
        == FileIoStatus::Accepted);
    runtime.shutdown();
    assert(probe.calls == count + 1 && probe.result.status == FileIoStatus::ServiceClosed);
    assert(local->telemetry().inFlight == 0 && local->telemetry().reservedBytes == 0);
    assert(local->telemetry().openNativeObjects == 0);
    assert(mounts->telemetry().inFlight == 0 && mounts->telemetry().reservedReadBytes == 0);
    assert(runtime.completionQueue().pending() == 0 && runtime.completionQueue().reserved() == 0);
}
#endif

} // namespace

int main()
{
#if defined(_WIN32)
    Workspace workspace;
    // Fixed warmup includes Windows process wait-object first-use caches.
    for (unsigned i = 0; i < 20; ++i)
        cycle(workspace);
    const auto before = defaultCpuAllocator().stats();
    DWORD beforeHandles = 0;
    assert(GetProcessHandleCount(GetCurrentProcess(), &beforeHandles));
    for (unsigned i = 0; i < 20; ++i)
        cycle(workspace);
    const auto after = defaultCpuAllocator().stats();
    DWORD afterHandles = 0;
    assert(GetProcessHandleCount(GetCurrentProcess(), &afterHandles));
    std::fprintf(stderr, "native handle baseline=%lu after=%lu\n", beforeHandles, afterHandles);
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    // Process-wide BCrypt wait caches are outside the IO handle owner. Their
    // creation stack is recorded in i1-native/handle-trace.log; report the raw
    // process observation without treating it as an IO ownership counter.
    std::printf("type: NativeDirectoryIoSmoke\ncontractVersion: 1\ncycles: 20\nwarmupCycles: 20\n"
                "liveBytesDelta: 0\nliveAllocationsDelta: 0\nopenIoObjectsAtShutdown: 0\n"
                "processHandlesDelta: %lld\n",
        static_cast<long long>(afterHandles) - static_cast<long long>(beforeHandles));
#else
    std::puts("type: NativeDirectoryIoSmoke\ncontractVersion: 1\nstatus: UnsupportedHost");
#endif
}
