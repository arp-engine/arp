#include "core/io/file_io.h"
#include "core/io/file_query_contract.h"
#include "core/io/file_service.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>

using namespace arp::core;
using namespace arp::core::io;
using namespace unified_io_fixture;

int main()
{
#if defined(_WIN32)
    Workspace workspace;
    auto backend = makeShared<NativeDirectoryFileBackend>(workspace.descriptor());
    assert(backend->capability().supportsMetadata && backend->capability().supportsContentVersion);
    runtime::CompletionQueue queue({8});
    FileService service(queue, *backend);
    Probe<FileQueryResult> probe;
    const auto performQuery = [&](const FileQueryRequest& request) {
        const auto calls = probe.calls;
        assert(service.query(request, {Probe<FileQueryResult>::complete, &probe})
            == FileIoStatus::Accepted);
        wait(queue, [&] {
            return probe.calls != calls;
        });
        if (probe.result.status != FileIoStatus::Success)
            std::fprintf(stderr,
                "native query status=%u diagnostic=%s\n",
                unsigned(probe.result.status),
                probe.result.diagnosticId ? probe.result.diagnosticId : "none");
        assert(probe.thread == std::this_thread::get_id());
    };
    auto stat = query(FileQueryOperation::Stat, "a.bin");
    stat.versionPolicy = FileVersionPolicy::ContentSha256;
    stat.maxVersionBytes = 6;
    performQuery(stat);
    assert(probe.result.status == FileIoStatus::Success);
    assert(probe.result.metadata.sizeBytes == 6);
    const auto version = probe.result.metadata.version;
    FileReadRequest read;
    read.pathScope = FilePathScope::MountedResource;
    read.path = "a.bin";
    read.contractVersion = 1;
    read.maxBytes = 3;
    read.range = FileReadRange::exactRange(1, 3);
    read.expectedVersion = version;
    read.maxVersionBytes = 6;
    Probe<FileReadResult> readProbe;
    const auto performRead = [&] {
        const auto calls = readProbe.calls;
        assert(service.read(read, {Probe<FileReadResult>::complete, &readProbe})
            == FileIoStatus::Accepted);
        wait(queue, [&] {
            return readProbe.calls != calls;
        });
    };
    performRead();
    assert(readProbe.result.status == FileIoStatus::Success);
    assert(readProbe.result.bytes.size() == 3 && readProbe.result.bytes[0] == 'b'
        && readProbe.result.bytes[2] == 'd' && readProbe.result.version == version);
    const auto originalTime = std::filesystem::last_write_time(workspace.root / "a.bin");
    workspace.write("a.bin", "ghijkl");
    std::filesystem::last_write_time(workspace.root / "a.bin", originalTime);
    performRead();
    assert(
        readProbe.result.status == FileIoStatus::VersionMismatch && readProbe.result.bytes.empty());
    auto list = query(FileQueryOperation::Enumerate);
    performQuery(list);
    assert(probe.result.status == FileIoStatus::Success && probe.result.totalEntries == 3);
    assert(probe.result.entries[0].name == "a.bin" && probe.result.next.offset == 1);
    list.cursor = probe.result.next;
    workspace.write("c.bin", "c");
    performQuery(list);
    assert(probe.result.status == FileIoStatus::VersionMismatch && probe.result.entries.empty());
    list.cursor = {};
    const char* expected[]{"a.bin", "b.bin", "c.bin", "nested"};
    for (const auto* name : expected) {
        performQuery(list);
        assert(probe.result.status == FileIoStatus::Success && probe.result.entries.size() == 1);
        assert(probe.result.entries[0].name == name);
        list.cursor = probe.result.next;
    }
    assert(list.cursor.snapshot.kind == FileVersionKind::None);
    auto unicode = query(FileQueryOperation::Stat, u8"nested/路径.bin");
    performQuery(unicode);
    assert(probe.result.status == FileIoStatus::Success && probe.result.metadata.sizeBytes == 7);
    read.expectedVersion = probe.result.metadata.version;
    assert(read.expectedVersion.kind == FileVersionKind::MetadataObservation);
    assert(service.read(read, {Probe<FileReadResult>::complete, &readProbe})
        == FileIoStatus::Unsupported);
    read.path = u8"nested/路径.bin";
    read.range = FileReadRange::wholeFile();
    read.expectedVersion = {};
    read.maxVersionBytes = 0;
    read.maxBytes = 7;
    performRead();
    const auto legacyBytes =
        readBinaryFile(workspace.root / std::filesystem::u8path(u8"nested/路径.bin"), 7);
    assert(legacyBytes.success && readProbe.result.status == FileIoStatus::Success);
    assert(readProbe.result.bytes == legacyBytes.bytes);
    auto tinyMetadata = unicode;
    tinyMetadata.maxResultBytes = 1;
    performQuery(tinyMetadata);
    assert(probe.result.status == FileIoStatus::BudgetExceeded);
    auto boundedVersion = stat;
    boundedVersion.maxVersionBytes = 5;
    performQuery(boundedVersion);
    assert(probe.result.status == FileIoStatus::BudgetExceeded);
    auto missing = query(FileQueryOperation::Stat, "missing");
    performQuery(missing);
    assert(probe.result.status == FileIoStatus::MissingFile);
    runtime::CompletionQueue foreign({1});
    Probe<FileQueryResult> foreignProbe;
    assert(backend->queryAsync(stat, {Probe<FileQueryResult>::complete, &foreignProbe}, foreign)
        == FileIoStatus::InvalidRequest);
    assert(foreignProbe.calls == 0 && foreign.pending() == 0 && foreign.reserved() == 0);
    auto quota = makeShared<CpuMemoryQuota>(1);
    {
        CpuMemoryQuotaScope failure(quota);
        assert(backend->queryAsync(stat, {Probe<FileQueryResult>::complete, &foreignProbe}, queue)
            == FileIoStatus::BudgetExceeded);
    }
    assert(backend->telemetry().inFlight == 0 && quota->liveBytes() == 0);
    workspace.write("empty.bin", "");
    auto empty = query(FileQueryOperation::Stat, "empty.bin");
    empty.versionPolicy = FileVersionPolicy::ContentSha256;
    performQuery(empty);
    assert(probe.result.status == FileIoStatus::Success && probe.result.metadata.sizeBytes == 0);
    read.path = "empty.bin";
    read.range = FileReadRange::wholeFile();
    read.maxBytes = 1;
    read.expectedVersion = probe.result.metadata.version;
    performRead();
    assert(readProbe.result.status == FileIoStatus::Success && readProbe.result.bytes.empty());
    list = query(FileQueryOperation::Enumerate);
    list.maxScanEntries = 1;
    performQuery(list);
    assert(probe.result.status == FileIoStatus::BudgetExceeded && probe.result.entries.empty());
    assert(workspace.createJunction());
    read.path = "link/secret.bin";
    read.expectedVersion = {};
    read.maxVersionBytes = 0;
    performRead();
    assert(readProbe.result.status == FileIoStatus::PermissionDenied
        && readProbe.result.bytes.empty());
    auto link = query(FileQueryOperation::Stat, "link");
    performQuery(link);
    assert(probe.result.status == FileIoStatus::PermissionDenied);
    auto all = query(FileQueryOperation::Enumerate);
    all.pageSize = all.maxScanEntries;
    performQuery(all);
    assert(probe.result.status == FileIoStatus::Success);
    bool listedLink = false;
    for (const auto& entry : probe.result.entries)
        if (entry.name == "link")
            listedLink = entry.metadata.kind == FileEntryKind::Link;
    assert(listedLink);
    backend->shutdown();
    assert(backend->telemetry().inFlight == 0 && backend->telemetry().reservedBytes == 0);
    assert(backend->telemetry().openNativeObjects == 0);
    assert(queue.pending() == 0 && queue.reserved() == 0);
#else
    NativeDirectoryFileBackendDesc desc;
    desc.rootPath = "/fixture";
    desc.maxInFlight = 1;
    desc.maxBytesInFlight = 65536;
    desc.maxPathBytes = 128;
    desc.ioChunkBytes = 1024;
    desc.directoryBufferBytes = 4096;
    desc.maxScanEntries = 8;
    desc.maxScanNameBytes = 1024;
    NativeDirectoryFileBackend backend(desc);
    assert(!backend.capability().supportsMetadata);
    runtime::CompletionQueue queue({1});
    Probe<FileQueryResult> probe;
    assert(backend.queryAsync(
               query(FileQueryOperation::Stat), {Probe<FileQueryResult>::complete, &probe}, queue)
        == FileIoStatus::Unsupported);
    assert(probe.calls == 0 && queue.pending() == 0);
#endif
}
