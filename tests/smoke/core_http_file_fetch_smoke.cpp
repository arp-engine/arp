#include "core/io/file_service.h"
#include "core/io/http_file_backend.h"
#include "core/io/mounted_file_backend.h"

#include <cassert>
#include <cstdio>
#include <emscripten/emscripten.h>

EM_JS_DEPS(fixtureBaseUrl, "$stringToUTF8");

EM_JS(void, fixtureBaseUrl, (char* destination, int capacity), {
    // clang-format off
    stringToUTF8(location.origin + '/fixture/', destination, capacity);
    // clang-format on
});

EM_JS(int, pendingFetches, (), {
    // clang-format off
    return Module.arpCoreFileTransfers ? Module.arpCoreFileTransfers.size : 0;
    // clang-format on
});

EM_JS(int, lateSettled, (), {
    // clang-format off
    return window.arpHttpProbe.lateSettled;
    // clang-format on
});

EM_JS(void, reportSuccess, (), {
    // clang-format off
    window.arpHttpReport({success: true, contractVersion: 1,
        cycles: 20, warmupCycles: 20, coreLiveBytesDelta: 0,
        coreLiveAllocationsDelta: 0, pendingFetches: Module.arpCoreFileTransfers.size,
        lateSettled: window.arpHttpProbe.lateSettled,
        lateAborted: window.arpHttpProbe.lateAborted,
        lateBodyCancelled: window.arpHttpProbe.lateBodyCancelled,
        userAgent: navigator.userAgent, gpuWork: false});
    // clang-format on
});

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
};

struct TextProbe {
    unsigned calls = 0;
    FileTextReadResult result;

    static void complete(void* context, FileTextReadResult result)
    {
        auto& probe = *static_cast<TextProbe*>(context);
        ++probe.calls;
        probe.result = std::move(result);
    }
};

HttpFileBackendDesc descriptor()
{
    char url[256]{};
    fixtureBaseUrl(url, sizeof(url));
    HttpFileBackendDesc desc;
    desc.baseUrl = url;
    desc.maxInFlight = 4;
    desc.maxBytesInFlight = 256 * 1024;
    desc.maxPathBytes = 1024;
    desc.maxHeaderBytes = 4096;
    desc.ioChunkBytes = 2;
    desc.timeoutMs = 600;
    desc.cancellationPollMs = 2;
    return desc;
}

FileReadRequest request(const char* path)
{
    FileReadRequest read;
    read.pathScope = FilePathScope::MountedResource;
    read.path = path;
    read.maxBytes = 64;
    read.contractVersion = 1;
    return read;
}

template <class ResultProbe>
void pump(runtime::CompletionQueue& queue, const ResultProbe& probe, unsigned calls)
{
    const auto deadline = emscripten_get_now() + 5000;
    while (probe.calls < calls && emscripten_get_now() < deadline) {
        queue.dispatch(8);
        if (probe.calls < calls)
            emscripten_sleep(1);
    }
    assert(probe.calls == calls);
}

void perform(FileService& service,
    runtime::CompletionQueue& queue,
    Probe& probe,
    FileReadRequest read,
    FileIoStatus expected)
{
    const auto calls = probe.calls;
    assert(service.read(read, {Probe::complete, &probe}) == FileIoStatus::Accepted);
    assert(probe.calls == calls); // A request never completes inline.
    pump(queue, probe, calls + 1);
    if (probe.result.status != expected)
        std::fprintf(stderr,
            "%s expected %u got %u %s\n",
            read.path.c_str(),
            static_cast<unsigned>(expected),
            static_cast<unsigned>(probe.result.status),
            probe.result.diagnosticId ? probe.result.diagnosticId : "no diagnostic");
    assert(probe.result.status == expected);
    assert(probe.result.backendKind == FileBackendKind::FetchFileBackend);
    if (expected != FileIoStatus::Success)
        assert(probe.result.bytes.empty() && probe.result.version.kind == FileVersionKind::None);
}

void byteAndFailureContracts()
{
    runtime::CompletionQueue queue({4});
    HttpFileBackend backend(descriptor());
    assert(!backend.diagnostic() && backend.capability().kind == FileBackendKind::FetchFileBackend);
    FileService service(queue, backend);
    Probe probe;
    perform(service, queue, probe, request("whole"), FileIoStatus::Success);
    assert(probe.result.bytes.size() == 6);
    const auto version = probe.result.version;
    Sha256 hash;
    hash.update("abcdef");
    assert(version == FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256));
    for (const auto* name : {u8"路径/a #%.bin", "unknown-length", "binary"}) {
        perform(service, queue, probe, request(name), FileIoStatus::Success);
        assert(probe.result.bytes.size() == 6);
    }
    assert(
        probe.result.bytes[0] == 0 && probe.result.bytes[1] == 255 && probe.result.bytes[5] == 128);
    perform(service, queue, probe, request("empty"), FileIoStatus::Success);
    assert(probe.result.bytes.empty());
    FileTextReadRequest text;
    text.file = request("text");
    text.text.maxOutputBytes = 7;
    text.text.bom = TextBomPolicy::Strip;
    TextProbe decoded;
    assert(service.readText(text, {TextProbe::complete, &decoded}) == FileIoStatus::Accepted);
    pump(queue, decoded, 1);
    assert(decoded.result.status() == FileIoStatus::Success
        && decoded.result.decoded.text == "hello\r\n");
    assert(decoded.result.decoded.hadBom && decoded.result.source.bytes.size() == 10);
    Sha256 rawTextHash;
    rawTextHash.update("\xEF\xBB\xBFhello\r\n");
    assert(decoded.result.source.version
        == FileVersionEncoding::finish(rawTextHash, FileVersionKind::ContentSha256));
    auto read = request("whole");
    read.range = FileReadRange::exactRange(2, 3);
    perform(service, queue, probe, read, FileIoStatus::Success);
    assert(probe.result.bytes.size() == 3 && probe.result.bytes.front() == 'c');
    assert(probe.result.version.kind == FileVersionKind::None);
    for (const auto* name : {"ignore-range", "bad-range"}) {
        read.path = name;
        perform(service, queue, probe, read, FileIoStatus::IntegrityMismatch);
    }
    read.path = "whole";
    read.expectedVersion = version;
    read.maxVersionBytes = 6;
    perform(service, queue, probe, read, FileIoStatus::Success);
    assert(probe.result.bytes.size() == 3 && probe.result.version == version);
    read.path = "changed";
    perform(service, queue, probe, read, FileIoStatus::VersionMismatch);
    read = request("whole");
    read.range = FileReadRange::exactRange(4, 4);
    perform(service, queue, probe, read, FileIoStatus::PartialReadDenied);
    read.partialPolicy = FilePartialReadPolicy::AllowPartial;
    perform(service, queue, probe, read, FileIoStatus::Success);
    assert(probe.result.bytes.size() == 2);
    perform(service, queue, probe, request("missing"), FileIoStatus::MissingFile);
    perform(service, queue, probe, request("forbidden"), FileIoStatus::PermissionDenied);
    perform(service, queue, probe, request("encoded"), FileIoStatus::Unsupported);
    perform(service, queue, probe, request("header-budget"), FileIoStatus::BudgetExceeded);
    for (const auto* name : {"redirect", "truncated", "duplicate-length"}) {
        const auto calls = probe.calls;
        assert(service.read(request(name), {Probe::complete, &probe}) == FileIoStatus::Accepted);
        pump(queue, probe, calls + 1);
        assert(probe.result.status != FileIoStatus::Success && probe.result.bytes.empty());
    }
    read = request("unknown-length");
    read.maxBytes = 2;
    perform(service, queue, probe, read, FileIoStatus::BudgetExceeded);
    for (const auto* name : {"slow-headers", "slow-body"})
        perform(service, queue, probe, request(name), FileIoStatus::Timeout);
    runtime::CancellationSource cancelled;
    read = request("slow-body");
    read.cancellation = cancelled.token();
    auto calls = probe.calls;
    assert(service.read(read, {Probe::complete, &probe}) == FileIoStatus::Accepted);
    emscripten_sleep(20);
    cancelled.cancel();
    pump(queue, probe, calls + 1);
    assert(probe.result.status == FileIoStatus::Cancelled && probe.result.bytes.empty());
    calls = probe.calls;
    for (unsigned i = 0; i < 4; ++i)
        assert(service.read(request("slow-body"), {Probe::complete, &probe})
            == FileIoStatus::Accepted);
    assert(service.read(request("whole"), {Probe::complete, &probe}) == FileIoStatus::QueueFull);
    backend.shutdown();
    queue.dispatch(4);
    assert(probe.calls == calls + 4);
    assert(backend.telemetry().inFlight == 0 && backend.telemetry().reservedBytes == 0);
    assert(queue.pending() == 0 && queue.reserved() == 0 && pendingFetches() == 0);

    auto foreign = descriptor();
    const auto host = foreign.baseUrl.find("127.0.0.1");
    assert(host != foreign.baseUrl.npos);
    foreign.baseUrl.replace(host, sizeof("127.0.0.1") - 1, "localhost");
    HttpFileBackend crossOrigin(std::move(foreign));
    calls = probe.calls;
    assert(crossOrigin.readAsync(request("whole"), {Probe::complete, &probe}, queue)
        == FileIoStatus::Unsupported);
    assert(probe.calls == calls && queue.reserved() == 0 && pendingFetches() == 0);
}

void routingAndLateResults()
{
    runtime::CompletionQueue queue({8});
    auto http = makeShared<HttpFileBackend>(descriptor());
    auto router =
        makeUnique<MountedFileBackend>(MountedFileBackendDesc{1, 4, 256 * 1024, 16, 1024});
    auto mount = router->mount("workspace", http);
    assert(mount);
    Probe probe;
    auto read = request("whole");
    read.mount = mount;
    assert(router->readAsync(read, {Probe::complete, &probe}, queue) == FileIoStatus::Accepted);
    assert(router->unmount(mount));
    auto replacement = router->mount("workspace", http);
    assert(replacement && !(replacement == mount));
    pump(queue, probe, 1);
    assert(probe.result.status == FileIoStatus::StaleLocation && probe.result.bytes.empty());
    router->shutdown();
    queue.dispatch(8);
    router.reset();
    http.reset();
    assert(pendingFetches() == 0 && queue.reserved() == 0);

    // Test fixture deliberately lets one real HTTP response arrive despite
    // AbortController. All ordinary requests still use actual Fetch abort.
    auto late = makeUnique<HttpFileBackend>(descriptor());
    assert(late->readAsync(request("late-result"), {Probe::complete, &probe}, queue)
        == FileIoStatus::Accepted);
    emscripten_sleep(20);
    late->shutdown();
    late.reset();
    queue.dispatch(8);
    const auto calls = probe.calls;
    assert(probe.result.status == FileIoStatus::ServiceClosed && pendingFetches() == 0);
    HttpFileBackend next(descriptor());
    FileService nextService(queue, next);
    perform(nextService, queue, probe, request("whole"), FileIoStatus::Success);
    const auto until = emscripten_get_now() + 2000;
    while (!lateSettled() && emscripten_get_now() < until)
        emscripten_sleep(10);
    assert(lateSettled() == 1);
    emscripten_sleep(20);
    queue.dispatch(8);
    assert(probe.calls == calls + 1 && pendingFetches() == 0 && queue.pending() == 0);
}

void cycle()
{
    runtime::CompletionQueue queue({4});
    auto backend = makeUnique<HttpFileBackend>(descriptor());
    Probe probe;
    assert(backend->readAsync(request("whole"), {Probe::complete, &probe}, queue)
        == FileIoStatus::Accepted);
    pump(queue, probe, 1);
    assert(probe.result.status == FileIoStatus::Success);
    assert(backend->readAsync(request("slow-body"), {Probe::complete, &probe}, queue)
        == FileIoStatus::Accepted);
    backend->shutdown();
    backend.reset();
    pump(queue, probe, 2);
    assert(probe.result.status == FileIoStatus::ServiceClosed);
    assert(queue.pending() == 0 && queue.reserved() == 0 && pendingFetches() == 0);
}

} // namespace

int main()
{
    byteAndFailureContracts();
    routingAndLateResults();
    for (unsigned i = 0; i < 20; ++i)
        cycle();
    const auto before = defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle();
    const auto after = defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    emscripten_sleep(20);
    assert(pendingFetches() == 0);
    std::puts("Real browser Fetch IO and ordinary 20 lifecycle cycles PASS");
    reportSuccess();
}
