#include "core/io/file_service.h"
#include "core/io/memory_file_backend.h"

#include <cassert>
#include <cstdio>

using namespace arp::core;
using namespace arp::core::io;

namespace {

struct Probe {
    unsigned calls = 0;
    FileTextReadResult result;

    static void complete(void* context, FileTextReadResult result)
    {
        auto& probe = *static_cast<Probe*>(context);
        ++probe.calls;
        probe.result = std::move(result);
    }
};

FileTextReadRequest request()
{
    FileTextReadRequest result;
    result.file.pathScope = FilePathScope::MountedResource;
    result.file.path = "bom.txt";
    result.file.contractVersion = 1;
    result.file.maxBytes = 64;
    result.text.maxOutputBytes = 64;
    result.text.bom = TextBomPolicy::Strip;
    return result;
}

} // namespace

int main()
{
    runtime::CompletionQueue queue({1});
    auto memory = makeUnique<MemoryFileBackend>(MemoryFileBackendDesc{3, 128, 2, 128, 64});
    const auto raw = std::string_view("\xEF\xBB\xBFhello\r\n");
    assert(memory->registerFile(
               "bom.txt", reinterpret_cast<const std::uint8_t*>(raw.data()), raw.size())
        == FileIoStatus::Success);
    const std::uint8_t invalid[]{0xFF, 0xFE};
    assert(memory->registerFile("bad.txt", invalid, sizeof(invalid)) == FileIoStatus::Success);
    const auto longText = std::string_view("a text longer than inline string storage");
    assert(memory->registerFile(
               "long.txt", reinterpret_cast<const std::uint8_t*>(longText.data()), longText.size())
        == FileIoStatus::Success);
    auto service = makeUnique<FileService>(queue, *memory);
    Probe probe;
    auto read = request();
    assert(service->readText(read, {Probe::complete, &probe}) == FileIoStatus::Accepted);
    assert(probe.calls == 0 && queue.pending() == 1);
    const auto reserved = defaultCpuAllocator().stats().liveBytes;
    assert(service->readText(read, {Probe::complete, &probe}) == FileIoStatus::QueueFull);
    assert(defaultCpuAllocator().stats().liveBytes == reserved);
    queue.dispatch(1);
    assert(probe.calls == 1 && probe.result.status() == FileIoStatus::Success);
    assert(probe.result.decoded.text == "hello\r\n" && probe.result.decoded.hadBom);
    assert(probe.result.source.bytes.size() == raw.size() && probe.result.source.bytes[0] == 0xEF);
    Sha256 hash;
    hash.update(raw);
    const auto expected = FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256);
    assert(probe.result.source.version == expected);
    read.file.path = "bad.txt";
    assert(service->readText(read, {Probe::complete, &probe}) == FileIoStatus::Accepted);
    queue.dispatch(1);
    assert(probe.result.source.status == FileIoStatus::Success);
    assert(probe.result.status() == FileIoStatus::IntegrityMismatch
        && probe.result.decoded.text.empty());
    assert(probe.result.decoded.status == TextDecodeStatus::InvalidUtf8);
    assert(probe.result.source.bytes.size() == 2); // Original identity is retained on text failure.
    read = request();
    read.file.path = "long.txt";
    assert(service->readText(read, {Probe::complete, &probe}) == FileIoStatus::Accepted);
    auto quota = makeShared<CpuMemoryQuota>(1);
    {
        CpuMemoryQuotaScope scope(quota);
        queue.dispatch(1);
    }
    assert(probe.result.source.status == FileIoStatus::Success);
    assert(probe.result.decoded.status == TextDecodeStatus::AllocationFailed);
    assert(
        probe.result.status() == FileIoStatus::BudgetExceeded && probe.result.decoded.text.empty());
    assert(quota->liveBytes() == 0 && probe.result.source.bytes.size() == longText.size());
    read = request();
    read.text.maxOutputBytes = 1;
    assert(service->readText(read, {Probe::complete, &probe}) == FileIoStatus::Accepted);
    queue.dispatch(1);
    assert(probe.result.status() == FileIoStatus::BudgetExceeded);
    assert(probe.result.source.version == expected);
    read = request();
    read.text.maxOutputBytes = UINT64_MAX;
    const auto calls = probe.calls;
    assert(service->readText(read, {Probe::complete, &probe}) == FileIoStatus::InvalidRequest);
    assert(queue.reserved() == 0 && queue.pending() == 0 && probe.calls == calls);
    read = request();
    runtime::CancellationSource cancelled;
    read.file.cancellation = cancelled.token();
    assert(service->readText(read, {Probe::complete, &probe}) == FileIoStatus::Accepted);
    cancelled.cancel();
    queue.dispatch(1);
    assert(probe.result.status() == FileIoStatus::Cancelled && probe.result.decoded.text.empty());
    read = request();
    assert(service->readText(read, {Probe::complete, &probe}) == FileIoStatus::Accepted);
    service.reset();
    memory->shutdown();
    memory.reset();
    queue.dispatch(1);
    assert(probe.result.status() == FileIoStatus::ServiceClosed);
    assert(queue.pending() == 0 && queue.reserved() == 0);
    std::puts("FileService text routing/identity/negative/budget/lifetime contracts PASS");
}
