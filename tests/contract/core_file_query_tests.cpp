#include "core/io/file_query_contract.h"
#include "core/io/file_service.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>

namespace {
using namespace arp::core;
using namespace arp::core::io;
using unified_io_fixture::query;
using QueryProbe = unified_io_fixture::Probe<FileQueryResult>;
using ReadProbe = unified_io_fixture::Probe<FileReadResult>;

void contract()
{
    auto valid = query(FileQueryOperation::Enumerate);
    assert(FileQueryContract::validate(valid).status == FileIoStatus::Success);
    auto invalid = valid;
    invalid.path = "../outside";
    assert(FileQueryContract::validate(invalid).status == FileIoStatus::InvalidRequest);
    invalid = valid;
    invalid.maxResultBytes = 0;
    assert(FileQueryContract::validate(invalid).status == FileIoStatus::InvalidRequest);
    invalid = valid;
    invalid.cursor.offset = 1;
    assert(FileQueryContract::validate(invalid).status == FileIoStatus::InvalidRequest);
    invalid = valid;
    invalid.operation = static_cast<FileQueryOperation>(255);
    assert(FileQueryContract::validate(invalid).status == FileIoStatus::InvalidRequest);
    invalid = valid;
    invalid.maxScanNameBytes = UINT64_MAX;
    assert(FileQueryContract::validate(invalid).status == FileIoStatus::BudgetExceeded);
    invalid = valid;
    invalid.cursor.snapshot.digest[0] = 1;
    assert(FileQueryContract::validate(invalid).status == FileIoStatus::InvalidRequest);
    auto metadata = query(FileQueryOperation::Stat, "asset");
    metadata.versionPolicy = FileVersionPolicy::ContentSha256;
    metadata.maxVersionBytes = UINT64_MAX;
    assert(FileQueryContract::validate(metadata).status == FileIoStatus::InvalidRequest);
    metadata = query(FileQueryOperation::Stat, "asset");
    assert(FileQueryContract::page(metadata, {}, 1).status == FileIoStatus::InvalidRequest);
    runtime::CompletionQueue queue({2});
    NullFileBackend null;
    FileService service(queue, null);
    QueryProbe probe;
    assert(service.query(valid, {QueryProbe::complete, &probe}) == FileIoStatus::Unsupported);
    assert(queue.pending() == 0 && probe.calls == 0);
}

void memoryQueries()
{
    auto backend = makeShared<MemoryFileBackend>(MemoryFileBackendDesc{8, 128, 4, 128 * 1024, 128});
    assert(backend->registerDirectory("assets") == FileIoStatus::Success);
    const std::uint8_t bytes[]{'a', 'b', 'c', 'd', 'e', 'f'};
    assert(backend->registerFile("assets/a", bytes, sizeof(bytes)) == FileIoStatus::Success);
    assert(backend->registerFile("assets/b", bytes, sizeof(bytes)) == FileIoStatus::Success);
    assert(backend->telemetry().files == 2 && backend->telemetry().directories == 1);
    auto router = makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 4, 128 * 1024, 16, 128});
    const auto mount = router->mount("workspace", backend);
    runtime::CompletionQueue queue({4});
    FileService service(queue, *router);
    auto stat = query(FileQueryOperation::Stat, "assets/a");
    stat.mount = mount;
    stat.versionPolicy = FileVersionPolicy::ContentSha256;
    stat.maxVersionBytes = sizeof(bytes);
    QueryProbe probe;
    assert(service.query(stat, {QueryProbe::complete, &probe}) == FileIoStatus::Accepted);
    assert(service.telemetry().submitted == 1 && service.telemetry().maxCompletionQueueDepth == 1);
    queue.dispatch(1);
    assert(probe.result.status == FileIoStatus::Success
        && probe.result.metadata.sizeBytes == sizeof(bytes));
    assert(probe.result.metadata.version.kind == FileVersionKind::ContentSha256);
    FileReadRequest read;
    read.pathScope = FilePathScope::MountedResource;
    read.path = "assets/a";
    read.mount = mount;
    read.contractVersion = 1;
    read.maxBytes = 3;
    read.range = FileReadRange::exactRange(1, 3);
    read.expectedVersion = probe.result.metadata.version;
    read.maxVersionBytes = sizeof(bytes);
    ReadProbe readProbe;
    assert(service.read(read, {ReadProbe::complete, &readProbe}) == FileIoStatus::Accepted);
    queue.dispatch(1);
    assert(readProbe.result.status == FileIoStatus::Success && readProbe.result.bytes[0] == 'b');
    read.expectedVersion.digest[0] ^= 1;
    assert(service.read(read, {ReadProbe::complete, &readProbe}) == FileIoStatus::Accepted);
    queue.dispatch(1);
    assert(
        readProbe.result.status == FileIoStatus::VersionMismatch && readProbe.result.bytes.empty());
    auto list = query(FileQueryOperation::Enumerate, "assets");
    list.mount = mount;
    assert(service.query(list, {QueryProbe::complete, &probe}) == FileIoStatus::Accepted);
    queue.dispatch(1);
    assert(probe.result.status == FileIoStatus::Success && probe.result.entries[0].name == "a");
    assert(probe.result.totalEntries == 2 && probe.result.next.offset == 1);
    list.cursor = probe.result.next;
    assert(backend->registerFile("assets/c", bytes, sizeof(bytes)) == FileIoStatus::Success);
    assert(service.query(list, {QueryProbe::complete, &probe}) == FileIoStatus::Accepted);
    queue.dispatch(1);
    assert(probe.result.status == FileIoStatus::VersionMismatch && probe.result.entries.empty());
    list.cursor = {};
    list.maxScanEntries = 1;
    assert(service.query(list, {QueryProbe::complete, &probe}) == FileIoStatus::Accepted);
    queue.dispatch(1);
    assert(probe.result.status == FileIoStatus::BudgetExceeded && probe.result.entries.empty());
    list.maxScanEntries = 32;
    list.maxResultBytes = 1;
    assert(service.query(list, {QueryProbe::complete, &probe}) == FileIoStatus::Accepted);
    queue.dispatch(1);
    assert(probe.result.status == FileIoStatus::BudgetExceeded && probe.result.next.offset == 0);
    stat.maxResultBytes = 8192;
    assert(service.query(stat, {QueryProbe::complete, &probe}) == FileIoStatus::Accepted);
    assert(router->unmount(mount));
    queue.dispatch(1);
    assert(probe.result.status == FileIoStatus::StaleLocation);
    assert(probe.result.metadata.version.kind == FileVersionKind::None);
    assert(router->telemetry().inFlight == 0 && router->telemetry().reservedReadBytes == 0);
    assert(backend->telemetry().inFlight == 0 && backend->telemetry().queuedBytes == 0);
}

} // namespace

int main()
{
    contract();
    memoryQueries();
    const auto before = arp::core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        memoryQueries();
    const auto after = arp::core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
}
