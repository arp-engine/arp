#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"

#include <cassert>

namespace {

using namespace arp::core;
using namespace arp::core::io;

struct Probe {
    unsigned calls = 0;
    FileReadResult result;

    static void complete(void* context, FileReadResult result)
    {
        auto& probe = *static_cast<Probe*>(context);
        ++probe.calls;
        probe.result = std::move(result);
    }

    FileReadCompletion callback() { return {complete, this}; }
};

SharedPtr<MemoryFileBackend> memory()
{
    auto backend = makeShared<MemoryFileBackend>(MemoryFileBackendDesc{4, 64, 2, 8, 128});
    assert(backend->registerDirectory("assets") == FileIoStatus::Success);
    const std::uint8_t bytes[]{0, 1, 2, 255};
    assert(
        backend->registerFile("assets/bytes.bin", bytes, sizeof(bytes)) == FileIoStatus::Success);
    assert(backend->registerFile("empty.txt", nullptr, 0) == FileIoStatus::Success);
    return backend;
}

FileReadRequest request(PoolHandle mount = {})
{
    FileReadRequest result;
    result.pathScope = FilePathScope::MountedResource;
    result.path = "assets/bytes.bin";
    result.contractVersion = 1;
    result.maxBytes = 4;
    result.mount = mount;
    return result;
}

void memoryContract()
{
    runtime::CompletionQueue queue({4});
    auto backend = memory();
    auto read = request();
    Probe first, second, rejected;
    assert(backend->readAsync(read, first.callback(), queue) == FileIoStatus::Accepted);
    assert(backend->readAsync(read, second.callback(), queue) == FileIoStatus::Accepted);
    assert(backend->readAsync(read, rejected.callback(), queue) == FileIoStatus::QueueFull);
    assert(backend->telemetry().inFlight == 2 && backend->telemetry().queuedBytes == 8);
    assert(first.calls == 0 && second.calls == 0 && rejected.calls == 0);
    queue.dispatch(4);
    assert(first.calls == 1 && second.calls == 1);
    assert(first.result.status == FileIoStatus::Success && first.result.bytes[3] == 255);
    assert(backend->telemetry().inFlight == 0 && backend->telemetry().queuedBytes == 0);
    read.range = FileReadRange::exactRange(2, 2);
    assert(backend->readAsync(read, first.callback(), queue) == FileIoStatus::Accepted);
    queue.dispatch(1);
    assert(first.result.bytes.size() == 2 && first.result.bytes.front() == 2);
    read.range = FileReadRange::exactRange(3, 2);
    assert(backend->readAsync(read, first.callback(), queue) == FileIoStatus::Accepted);
    queue.dispatch(1);
    assert(first.result.status == FileIoStatus::PartialReadDenied);
    read.partialPolicy = FilePartialReadPolicy::AllowPartial;
    assert(backend->readAsync(read, first.callback(), queue) == FileIoStatus::Accepted);
    queue.dispatch(1);
    assert(first.result.status == FileIoStatus::Success && first.result.bytes.size() == 1);
    read = request();
    read.path = "missing.bin";
    assert(backend->readAsync(read, first.callback(), queue) == FileIoStatus::Accepted);
    queue.dispatch(1);
    assert(first.result.status == FileIoStatus::MissingFile);
    read.path = "../bytes.bin";
    assert(backend->readAsync(read, rejected.callback(), queue) == FileIoStatus::InvalidRequest);
    read.path = "empty.txt";
    assert(backend->readAsync(read, first.callback(), queue) == FileIoStatus::Accepted);
    queue.dispatch(1);
    assert(first.result.status == FileIoStatus::Success && first.result.bytes.empty());
    read = request();
    {
        runtime::CancellationSource cancel;
        read.cancellation = cancel.token();
        assert(backend->readAsync(read, first.callback(), queue) == FileIoStatus::Accepted);
        cancel.cancel();
    }
    queue.dispatch(1);
    assert(first.result.status == FileIoStatus::Cancelled && first.result.bytes.empty());
    assert(first.result.diagnosticId);
    read = request();
    assert(backend->readAsync(read, first.callback(), queue) == FileIoStatus::Accepted);
    backend.reset();
    queue.dispatch(1);
    assert(first.result.status == FileIoStatus::ServiceClosed && first.result.bytes.empty());
}

void mountContract()
{
    runtime::CompletionQueue queue({4});
    auto backend = memory();
    auto router = makeShared<MountedFileBackend>(MountedFileBackendDesc{2, 2, 8, 16, 128});
    auto handle = router->mount("workspace", backend);
    assert(handle && router->findMount("workspace") == handle);
    assert(!router->mount("workspace", backend));
    assert(!router->mount("nested", router));
    Probe old, current, rejected;
    const auto oldRequest = request(handle);
    assert(router->readAsync(oldRequest, old.callback(), queue) == FileIoStatus::Accepted);
    assert(router->unmount(handle));
    assert(
        router->readAsync(oldRequest, rejected.callback(), queue) == FileIoStatus::StaleLocation);
    handle = router->mount("workspace", backend);
    assert(handle && !(handle == oldRequest.mount));
    assert(router->readAsync(request(handle), current.callback(), queue) == FileIoStatus::Accepted);
    queue.dispatch(4);
    assert(old.calls == 1 && old.result.status == FileIoStatus::StaleLocation);
    assert(old.result.bytes.empty());
    assert(old.result.diagnosticId);
    assert(current.calls == 1 && current.result.status == FileIoStatus::Success);
    assert(router->telemetry().inFlight == 0 && router->telemetry().reservedReadBytes == 0);
    auto oversized = request(handle);
    oversized.maxBytes = 9;
    assert(
        router->readAsync(oversized, rejected.callback(), queue) == FileIoStatus::BudgetExceeded);
    assert(router->telemetry().inFlight == 0 && rejected.calls == 0);
    assert(router->readAsync(request(handle), current.callback(), queue) == FileIoStatus::Accepted);
    router->shutdown();
    assert(router->readAsync(request(handle), rejected.callback(), queue)
        == FileIoStatus::ServiceClosed);
    queue.dispatch(1);
    assert(current.result.status == FileIoStatus::ServiceClosed);
    assert(router->telemetry().inFlight == 0);
}

void injectionContract()
{
    auto backend = memory();
    runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = 4;
    desc.completionBudget = 2;
    desc.enableWork = false;
    desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = backend;
    runtime::AsyncRuntime runtime(desc);
    assert(runtime.valid() && runtime.workService().workerCount() == 0);
    assert(runtime.telemetry().file.kind == FileBackendKind::MemoryFileBackend);
    Probe result;
    assert(runtime.fileService().read(request(), result.callback()) == FileIoStatus::Accepted);
    runtime.dispatch();
    assert(result.calls == 1 && result.result.status == FileIoStatus::Success);
    assert(runtime.fileService().read(request(), result.callback()) == FileIoStatus::Accepted);
    runtime.shutdown();
    assert(result.calls == 2 && result.result.status == FileIoStatus::ServiceClosed);
    assert(runtime.completionQueue().pending() == 0 && runtime.completionQueue().reserved() == 0);
}

void allocationFailureContract()
{
    runtime::CompletionQueue queue({2});
    auto backend = memory();
    auto router = makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 1, 8, 16, 128});
    const auto handle = router->mount("workspace", backend);
    Probe result;
    auto read = request(handle);
    auto hostRead = read;
    hostRead.pathScope = FilePathScope::HostFilesystem;
    auto quota = makeShared<CpuMemoryQuota>(1);
    NullFileBackend null;
    FileService nullService(queue, null);
    runtime::WorkService work({&queue, 1, 1});
    ThreadPoolFallbackFileBackend local(work);
    FileService localService(queue, local);
    {
        CpuMemoryQuotaScope failure(quota);
        assert(router->readAsync(read, result.callback(), queue) == FileIoStatus::BudgetExceeded);
        assert(backend->readAsync(read, result.callback(), queue) == FileIoStatus::BudgetExceeded);
        assert(nullService.read(read, result.callback()) == FileIoStatus::BudgetExceeded);
        assert(localService.read(hostRead, result.callback()) == FileIoStatus::BudgetExceeded);
    }
    assert(queue.pending() == 0 && queue.reserved() == 0 && result.calls == 0);
    assert(router->telemetry().inFlight == 0 && router->telemetry().reservedReadBytes == 0);
    assert(backend->telemetry().inFlight == 0 && quota->liveBytes() == 0);
    assert(router->readAsync(read, result.callback(), queue) == FileIoStatus::Accepted);
    {
        CpuMemoryQuotaScope failure(quota);
        const auto allocations = defaultCpuAllocator().stats().allocationCount;
        assert(queue.dispatch(1).dispatched == 1);
        assert(defaultCpuAllocator().stats().allocationCount == allocations);
    }
    assert(result.calls == 1 && result.result.status == FileIoStatus::Success);
    work.shutdown();
}

void destroyedRouterContract()
{
    runtime::CompletionQueue queue({1});
    auto backend = memory();
    auto router = makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 1, 8, 16, 128});
    const auto handle = router->mount("workspace", backend);
    Probe result;
    assert(router->readAsync(request(handle), result.callback(), queue) == FileIoStatus::Accepted);
    router.reset();
    backend.reset();
    assert(queue.dispatch(1).dispatched == 1);
    assert(result.calls == 1 && result.result.status == FileIoStatus::ServiceClosed);
    assert(queue.pending() == 0 && queue.reserved() == 0);
}

void invalidInjectionContract()
{
    auto backend = memory();
    runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = 2;
    desc.completionBudget = 1;
    desc.workerCount = 1;
    desc.workQueueCapacity = 1;
    desc.fileBackend = backend;
    Probe result;
    {
        runtime::AsyncRuntime invalid(desc);
        assert(!invalid.valid());
        assert(
            invalid.startupDiagnostics() == runtime::AsyncRuntimeDiagnostic::AmbiguousFileBackend);
        assert(invalid.fileService().read(request(), result.callback())
            == FileIoStatus::ServiceClosed);
        assert(invalid.workService().workerCount() == 0);
    }
    // A rejected runtime never adopted producer shutdown ownership.
    runtime::CompletionQueue queue({1});
    assert(backend->readAsync(request(), result.callback(), queue) == FileIoStatus::Accepted);
    queue.dispatch(1);
    assert(result.calls == 1 && result.result.status == FileIoStatus::Success);
    desc.fileBackend.reset();
    desc.enableWork = false;
    desc.enableThreadPoolFileBackend = false;
    runtime::AsyncRuntime missing(desc);
    assert(!missing.valid());
    assert(missing.startupDiagnostics() == runtime::AsyncRuntimeDiagnostic::MissingFileBackend);
    assert(missing.fileService().read(request(), result.callback()) == FileIoStatus::ServiceClosed);
}

} // namespace

int main()
{
    memoryContract();
    mountContract();
    injectionContract();
    allocationFailureContract();
    destroyedRouterContract();
    invalidInjectionContract();
}
