#include "network/http/fetch_byte_transfer.h"
#include "tests/fixtures/http_authenticated_transfer_fixture.h"

#include <cstdio>
#include <emscripten/emscripten.h>

EM_JS_DEPS(transferOrigin, "$stringToUTF8");
EM_JS(void, transferOrigin, (char* destination, int capacity), {
    // clang-format off
    stringToUTF8(window.arpTransferOrigin, destination, capacity);
    // clang-format on
});

EM_JS(int, activeFetches, (), {
    // clang-format off
    return Module.arpCoreFileTransfers ? Module.arpCoreFileTransfers.size : 0;
    // clang-format on
});

EM_JS(void, reportTransferSuccess, (), {
    // clang-format off
    window.arpTransferReport({success: true, cycles: 20, coreLiveBytesDelta: 0,
        coreLiveAllocationsDelta: 0, pendingFetches: Module.arpCoreFileTransfers.size,
        userAgent: navigator.userAgent, gpuWork: false});
    // clang-format on
});

namespace {
using namespace arp;
using namespace network::http;
using namespace network::http::tests;

void completed(void* context)
{
    ++*static_cast<unsigned*>(context);
}

void wait(unsigned& calls)
{
    const auto deadline = emscripten_get_now() + 5000;
    while (!calls && emscripten_get_now() < deadline)
        emscripten_sleep(1);
    assert(calls == 1 && activeFetches() == 0);
}

void perform(ByteReadDesc desc, const ByteReadRequest& request, TransferProbe& probe)
{
    unsigned calls = 0;
    std::atomic_bool closed{false};
    FetchByteTransfer transfer;
    assert(transfer.start(desc, request, probe, closed, completed, &calls)
        == ByteReadStatus::Accepted);
    assert(calls == 0);
    wait(calls);
}

void echo(std::string_view origin)
{
    const auto request = transferRequest(origin);
    TransferProbe probe;
    perform(transferDescriptor(), request, probe);
    assert(probe.ended && !probe.failed && probe.httpStatus == 200 && probe.body == transferBody);
}
} // namespace

int main()
{
    assert(FetchByteTransfer::supported());
    char origin[512]{};
    transferOrigin(origin, sizeof(origin));
    echo(origin);
    for (const auto* route : {"/redirect", "/oversize", "/truncated"}) {
        auto request = transferRequest(origin, route);
        request.maxBodyBytes = 64;
        TransferProbe probe(64);
        perform(transferDescriptor(), request, probe);
        assert(probe.failed && !probe.ended && probe.body.empty());
    }
    {
        auto request = transferRequest(origin);
        request.bearerToken.assign(32, 'x');
        TransferProbe probe;
        perform(transferDescriptor(), request, probe);
        assert(probe.ended && probe.httpStatus == 401);
    }
    {
        auto request = transferRequest(origin, "/slow");
        auto desc = transferDescriptor();
        desc.timeoutMs = 100;
        TransferProbe probe;
        perform(desc, request, probe);
        assert(probe.failed && probe.failure == ByteReadStatus::Timeout);
    }
    {
        auto request = transferRequest(origin, "/slow");
        TransferProbe probe;
        unsigned calls = 0;
        std::atomic_bool closed{false};
        FetchByteTransfer transfer;
        assert(transfer.start(transferDescriptor(), request, probe, closed, completed, &calls)
            == ByteReadStatus::Accepted);
        emscripten_sleep(20);
        transfer.cancel();
        wait(calls);
        assert(probe.failed && probe.failure == ByteReadStatus::Cancelled && probe.body.empty());
    }
    {
        auto request = transferRequest(origin);
        request.credentialOrigin = "http://127.0.0.1:1";
        TransferProbe probe;
        unsigned calls = 0;
        std::atomic_bool closed{false};
        FetchByteTransfer transfer;
        assert(transfer.start(transferDescriptor(), request, probe, closed, completed, &calls)
            == ByteReadStatus::InvalidRequest);
        ByteReadRequest anonymous;
        anonymous.url = request.url;
        anonymous.maxBodyBytes = 1024;
        assert(transfer.start(transferDescriptor(), anonymous, probe, closed, completed, &calls)
            == ByteReadStatus::Unsupported);
        assert(calls == 0 && probe.headerCalls == 0 && activeFetches() == 0);
    }
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        echo(origin);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    // Let aborted network promises settle before reporting the registry count.
    emscripten_sleep(500);
    assert(activeFetches() == 0);
    std::puts(
        "PASS Fetch Bearer JSON POST, exact origin, CORS, errors, cancel and 20 ordinary cycles");
    reportTransferSuccess();
}
