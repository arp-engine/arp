#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"

#include <cassert>
#include <cstdio>

namespace {

void cycle()
{
    using namespace arp::core;
    using namespace arp::core::io;
    auto memory = makeShared<MemoryFileBackend>(MemoryFileBackendDesc{1, 8, 1, 8, 64});
    const std::uint8_t bytes[]{'u', 't', 'f', '8', '\r', '\n', 0, 255};
    assert(memory->registerFile("fixture.bin", bytes, sizeof(bytes)) == FileIoStatus::Success);
    auto mounts = makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 1, 8, 16, 64});
    const auto workspace = mounts->mount("workspace", memory);
    assert(workspace);
    runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = 1;
    desc.completionBudget = 1;
    desc.enableWork = false;
    desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = mounts;
    runtime::AsyncRuntime runtime(desc);
    assert(runtime.valid());
    FileReadRequest request;
    request.pathScope = FilePathScope::MountedResource;
    request.path = "fixture.bin";
    request.mount = workspace;
    request.contractVersion = 1;
    request.maxBytes = sizeof(bytes);

    struct Probe {
        unsigned callbacks = 0;
        FileIoStatus expected = FileIoStatus::Success;
    } probe;

    const FileReadCompletion completion{
        [](void* user, FileReadResult result) {
            auto& probe = *static_cast<Probe*>(user);
            assert(result.status == probe.expected);
            if (result.status == FileIoStatus::Success)
                assert(result.bytes.size() == 8 && result.bytes[7] == 255);
            else
                assert(result.bytes.empty());
            ++probe.callbacks;
        },
        &probe};
    assert(runtime.fileService().read(request, completion) == FileIoStatus::Accepted);
    assert(probe.callbacks == 0);
    assert(runtime.dispatch().dispatched == 1 && probe.callbacks == 1);
    probe.expected = FileIoStatus::StaleLocation;
    assert(runtime.fileService().read(request, completion) == FileIoStatus::Accepted);
    assert(mounts->unmount(workspace));
    request.mount = mounts->mount("workspace", memory);
    assert(request.mount && !(request.mount == workspace));
    assert(runtime.dispatch().dispatched == 1 && probe.callbacks == 2);
    probe.expected = FileIoStatus::Cancelled;
    {
        runtime::CancellationSource source;
        request.cancellation = source.token();
        assert(runtime.fileService().read(request, completion) == FileIoStatus::Accepted);
        source.cancel();
    }
    assert(runtime.dispatch().dispatched == 1 && probe.callbacks == 3);
    request.cancellation = {};
    probe.expected = FileIoStatus::ServiceClosed;
    assert(runtime.fileService().read(request, completion) == FileIoStatus::Accepted);
    runtime.shutdown();
    assert(probe.callbacks == 4);
    assert(mounts->telemetry().inFlight == 0 && memory->telemetry().inFlight == 0);
    assert(runtime.completionQueue().pending() == 0 && runtime.completionQueue().reserved() == 0);
}

} // namespace

int main()
{
    cycle();
    const auto before = arp::core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle();
    const auto after = arp::core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    std::puts("type: UnifiedIoMemoryMountSmoke\ncycles: 20\nrequestsPerCycle: 4\n"
              "liveBytesDelta: 0\nliveAllocationsDelta: 0");
}
