#include "core/io/file_service.h"
#include "core/io/http_file_backend.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace arp::core;
using namespace arp::core::io;

namespace {

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

HttpFileBackendDesc descriptor(const char* url)
{
    HttpFileBackendDesc desc;
    desc.baseUrl = url;
    desc.maxInFlight = 4;
    desc.maxBytesInFlight = 256 * 1024;
    desc.maxPathBytes = 1024;
    desc.maxHeaderBytes = 4096;
    desc.ioChunkBytes = 2;
    desc.timeoutMs = 300;
    desc.cancellationPollMs = 2;
    desc.maxDnsAddresses = 8;
    return desc;
}

FileReadRequest request(const char* name)
{
    FileReadRequest read;
    read.path = name;
    read.pathScope = FilePathScope::MountedResource;
    read.contractVersion = 1;
    read.maxBytes = 64;
    return read;
}

void pump(runtime::CompletionQueue& queue, const Probe& probe, unsigned calls)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (probe.calls < calls && std::chrono::steady_clock::now() < deadline) {
        queue.dispatch(8);
        std::this_thread::yield();
    }
    assert(probe.calls == calls);
}

void cycle(const char* url)
{
    runtime::CompletionQueue queue({8});
    auto backend = makeUnique<HttpFileBackend>(descriptor(url));
    FileService service(queue, *backend);
    Probe probe;
    assert(service.read(request("whole"), {Probe::complete, &probe}) == FileIoStatus::Accepted);
    pump(queue, probe, 1);
    assert(probe.result.status == FileIoStatus::Success && probe.result.bytes.size() == 6);
    assert(service.read(request("slow-body"), {Probe::complete, &probe}) == FileIoStatus::Accepted);
    backend->shutdown();
    assert(backend->telemetry().activeNativeTransfers == 0);
    backend.reset();
    pump(queue, probe, 2);
    assert(probe.result.status == FileIoStatus::ServiceClosed);
    assert(queue.reserved() == 0 && queue.pending() == 0);
}

} // namespace

int main(int argc, char** argv)
{
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    assert(argc == 2);
    runtime::CompletionQueue queue({4});
    HttpFileBackend backend(descriptor(argv[1]));
    assert(!backend.diagnostic());
    FileService service(queue, backend);
    Probe probe;
    const auto perform = [&](FileReadRequest read) {
        const auto next = probe.calls + 1;
        assert(service.read(read, {Probe::complete, &probe}) == FileIoStatus::Accepted);
        pump(queue, probe, next);
        if (probe.result.status != FileIoStatus::Success)
            std::fprintf(stderr,
                "%s: %u %s\n",
                read.path.c_str(),
                static_cast<unsigned>(probe.result.status),
                probe.result.diagnosticId ? probe.result.diagnosticId : "no-diagnostic");
        assert(backend.telemetry().inFlight == 0 && backend.telemetry().reservedBytes == 0);
        assert(backend.telemetry().activeNativeTransfers == 0);
    };
    perform(request("whole"));
    assert(probe.result.status == FileIoStatus::Success && probe.result.bytes.size() == 6);
    const auto version = probe.result.version;
    auto dnsDesc = descriptor(argv[1]);
    const auto original = dnsDesc.baseUrl.find("127.0.0.1");
    assert(original != dnsDesc.baseUrl.npos);
    dnsDesc.baseUrl.replace(original, sizeof("127.0.0.1") - 1, "localhost");
    {
        HttpFileBackend dns(dnsDesc);
        Probe named;
        assert(dns.readAsync(request("host"), {Probe::complete, &named}, queue)
            == FileIoStatus::Accepted);
        pump(queue, named, 1);
        assert(named.result.status == FileIoStatus::Success && named.result.bytes.size() == 6);
        assert(named.result.bytes.front() == 'd');
        dns.shutdown();
        dnsDesc.maxDnsAddresses = 0;
        HttpFileBackend disabled(dnsDesc);
        assert(disabled.readAsync(request("whole"), {Probe::complete, &named}, queue)
            == FileIoStatus::Accepted);
        pump(queue, named, 2);
        assert(named.result.status == FileIoStatus::Unsupported && named.result.bytes.empty());
    }
    for (const auto* name : {"whole", u8"路径/a #%.bin", "unknown-length", "binary"}) {
        perform(request(name));
        assert(probe.result.status == FileIoStatus::Success && probe.result.bytes.size() == 6);
    }
    assert(
        probe.result.bytes[0] == 0 && probe.result.bytes[1] == 255 && probe.result.bytes[5] == 128);
    auto read = request("whole");
    read.range = FileReadRange::exactRange(2, 3);
    perform(read);
    assert(probe.result.status == FileIoStatus::Success && probe.result.bytes[0] == 'c');
    assert(probe.result.version.kind == FileVersionKind::None);
    for (const auto* name : {"ignore-range", "bad-range"}) {
        read.path = name;
        perform(read);
        assert(
            probe.result.status == FileIoStatus::IntegrityMismatch && probe.result.bytes.empty());
    }
    read.path = "whole";
    read.expectedVersion = version;
    read.maxVersionBytes = 6;
    perform(read);
    assert(probe.result.status == FileIoStatus::Success && probe.result.bytes.size() == 3);
    read.path = "changed";
    perform(read);
    assert(probe.result.status == FileIoStatus::VersionMismatch && probe.result.bytes.empty());
    for (const auto* name :
        {"redirect", "encoded", "truncated", "duplicate-length", "header-budget"}) {
        perform(request(name));
        assert(probe.result.status != FileIoStatus::Success && probe.result.bytes.empty());
    }
    perform(request("missing"));
    assert(probe.result.status == FileIoStatus::MissingFile);
    perform(request("forbidden"));
    assert(probe.result.status == FileIoStatus::PermissionDenied);
    read = request("unknown-length");
    read.maxBytes = 2;
    perform(read);
    assert(probe.result.status == FileIoStatus::BudgetExceeded);
    perform(request("slow-headers"));
    assert(probe.result.status == FileIoStatus::Timeout);
    perform(request("slow-body"));
    assert(probe.result.status == FileIoStatus::Timeout);
    runtime::CancellationSource cancel;
    read = request("slow-body");
    read.cancellation = cancel.token();
    auto count = probe.calls;
    assert(service.read(read, {Probe::complete, &probe}) == FileIoStatus::Accepted);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto cancelledAt = std::chrono::steady_clock::now();
    cancel.cancel();
    pump(queue, probe, count + 1);
    assert(probe.result.status == FileIoStatus::Cancelled);
    assert(std::chrono::steady_clock::now() - cancelledAt < std::chrono::milliseconds(250));

    for (unsigned i = 0; i < queue.capacity(); ++i)
        assert(service.read(request("whole"), {Probe::complete, &probe}) == FileIoStatus::Accepted);
    assert(service.read(request("whole"), {Probe::complete, &probe}) == FileIoStatus::QueueFull);
    backend.shutdown();
    queue.dispatch(4);
    assert(probe.calls == count + 5);
    assert(backend.telemetry().inFlight == 0 && backend.telemetry().reservedBytes == 0);
    assert(backend.telemetry().activeNativeTransfers == 0);
    assert(queue.pending() == 0 && queue.reserved() == 0);

    for (unsigned i = 0; i < 20; ++i)
        cycle(argv[1]);
    const auto before = defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle(argv[1]);
    const auto after = defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    std::puts("type: HttpFileIoSmoke\ncontractVersion: 1\ncycles: 20\ncoreLiveBytesDelta: 0\n"
              "coreLiveAllocationsDelta: 0\nactiveNativeTransfersAtShutdown: 0\nstatus: PASS");
#else
    (void)argc;
    (void)argv;
    runtime::CompletionQueue queue({1});
    HttpFileBackend backend(descriptor("http://127.0.0.1/fixture/"));
    Probe probe;
    assert(backend.readAsync(request("whole"), {Probe::complete, &probe}, queue)
        == FileIoStatus::Unsupported);
    assert(!probe.calls && queue.reserved() == 0);
    std::puts("HTTP native driver Unsupported host execution gate PASS");
#endif
}
