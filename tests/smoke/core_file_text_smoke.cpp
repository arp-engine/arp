#include "core/io/file_service.h"
#include "core/io/memory_file_backend.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstdio>

using namespace arp::core;
using namespace arp::core::io;

int main()
{
#if defined(_WIN32)
    unified_io_fixture::Workspace workspace;
    const auto raw = std::string_view("\xEF\xBB\xBFhello\r\n");
    workspace.write("text.yaml", raw);
    NativeDirectoryFileBackend local(workspace.descriptor());
    MemoryFileBackend memory({1, 128, 2, 128, 128});
    assert(memory.registerFile(
               "text.yaml", reinterpret_cast<const std::uint8_t*>(raw.data()), raw.size())
        == FileIoStatus::Success);
    runtime::CompletionQueue queue({4});
    FileService disk(queue, local), registered(queue, memory);
    FileTextReadRequest request;
    request.file.pathScope = FilePathScope::MountedResource;
    request.file.path = "text.yaml";
    request.file.contractVersion = 1;
    request.file.maxBytes = 64;
    request.file.maxVersionBytes = raw.size();
    Sha256 hash;
    hash.update(raw);
    request.file.expectedVersion =
        FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256);
    request.text.maxOutputBytes = 7;
    request.text.bom = TextBomPolicy::Strip;
    const auto read = [&](FileService& service) {
        unified_io_fixture::Probe<FileTextReadResult> probe;
        assert(service.readText(request, {decltype(probe)::complete, &probe})
            == FileIoStatus::Accepted);
        unified_io_fixture::wait(queue, [&] {
            return probe.calls == 1;
        });
        assert(probe.result.status() == FileIoStatus::Success);
        assert(probe.result.source.version == request.file.expectedVersion);
        assert(probe.result.source.bytes.size() == raw.size());
        assert(probe.result.decoded.text == "hello\r\n" && probe.result.decoded.hadBom);
    };
    for (unsigned i = 0; i < 20; ++i) {
        read(disk);
        read(registered);
    }
    const auto before = defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i) {
        read(disk);
        read(registered);
    }
    const auto after = defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    local.shutdown();
    memory.shutdown();
    assert(local.telemetry().openNativeObjects == 0);
    assert(queue.reserved() == 0 && queue.pending() == 0);
    std::puts("FileService text native/memory bytes, original hash and 20 ordinary cycles PASS");
#else
    std::puts("Native text smoke unavailable on this host; use browser Fetch smoke");
#endif
}
