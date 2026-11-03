#include "core/runtime/cancellation_token.h"
#include "tests/fixtures/http_authenticated_transfer_fixture.h"

#include <atomic>
#include <cstdio>

using namespace arp;
using namespace network::http;
using namespace network::http::tests;

namespace {
void echo(std::string_view origin)
{
    const auto request = transferRequest(origin);
    TransferProbe probe;
    std::atomic_bool closed{false};
    std::atomic_uint64_t active{0};
    ByteReadTransport::read(transferDescriptor(), request, probe, closed, active);
    assert(probe.ended && !probe.failed && probe.httpStatus == 200 && probe.body == transferBody
        && active == 0);
}
} // namespace

int main(int argc, char** argv)
{
#if !defined(__EMSCRIPTEN__)
    assert(argc == 2);
    const std::string_view origin(argv[1]);
    const auto perform = [&](ByteReadDesc desc, ByteReadRequest request, TransferProbe& probe) {
        std::atomic_bool closed{false};
        std::atomic_uint64_t active{0};
        ByteReadTransport::read(desc, request, probe, closed, active);
        assert(active == 0);
    };
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
        assert(probe.httpStatus == 401 && probe.ended && !probe.failed);
    }
    {
        auto request = transferRequest(origin);
        core::runtime::CancellationSource cancellation;
        cancellation.cancel();
        request.cancellation = cancellation.token();
        TransferProbe probe;
        perform(transferDescriptor(), request, probe);
        assert(probe.failed && probe.failure == ByteReadStatus::Cancelled && !probe.headerCalls);
    }
    {
        auto request = transferRequest(origin, "/slow");
        auto desc = transferDescriptor();
        desc.timeoutMs = 100;
        TransferProbe probe;
        perform(desc, request, probe);
        assert(probe.failed && probe.failure == ByteReadStatus::Timeout && probe.body.empty());
    }
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        echo(origin);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::printf("Authenticated HTTP 20 cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
#else
    (void)argc;
    (void)argv;
    assert(!ByteReadTransport::supported());
#endif
}
