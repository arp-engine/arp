#include "network/resolver.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace arp::network;

namespace {

void released()
{
    const auto live = Resolver::telemetry();
    assert(live.activeLookups == 0 && live.openNativeEvents == 0 && live.ownedNativeResults == 0);
}

void cycle()
{
    const std::atomic_bool closed{false};
    const auto result =
        Resolver::resolveOnWorker({256, 8, 1000, 2}, "localhost", 54321, {}, closed);
    assert(result.status == ResolverStatus::Success && !result.addresses.empty());
    assert(result.addresses.front() == endpoint("127.0.0.1", 54321));
    for (const auto& address : result.addresses)
        assert(address.port() == 54321 && !address.is_ipv6());
    released();
}

} // namespace

int main()
{
    const std::atomic_bool closed{false};
    ResolverDesc desc{256, 8, 1000, 2};
    assert(Resolver::resolveOnWorker(desc, "bad\r\nname", 54321, {}, closed).status
        == ResolverStatus::InvalidRequest);
    desc.maxAddresses = 0;
    assert(Resolver::resolveOnWorker(desc, "localhost", 54321, {}, closed).status
        == ResolverStatus::InvalidRequest);
    desc.maxAddresses = 8;
    arp::core::runtime::CancellationSource cancelled;
    cancelled.cancel();
    assert(Resolver::resolveOnWorker(desc, "localhost", 54321, cancelled.token(), closed).status
        == ResolverStatus::Cancelled);
    released();
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    assert(Resolver::supported());
    for (unsigned i = 0; i < 20; ++i)
        cycle();
    const auto before = arp::core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle();
    const auto after = arp::core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    // RFC2606 .invalid cannot resolve to a public host. No HTTP connection is
    // attempted. Record whether the native query was still pending at cancel.
    ResolverResult result;
    std::atomic_bool finished{false};
    arp::core::runtime::CancellationSource source;
    std::thread worker([&] {
        result = Resolver::resolveOnWorker(
            desc, "arp-unified-io-cancel.invalid", 54321, source.token(), closed);
        finished = true;
    });
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!Resolver::telemetry().activeLookups && !finished.load()
        && std::chrono::steady_clock::now() < until)
        std::this_thread::yield();
    source.cancel();
    worker.join();
    assert(result.status != ResolverStatus::Success && result.addresses.empty());
    released();
    std::printf("type: NativeDnsResolverSmoke\ncycles: 20\ncoreLiveBytesDelta: 0\n"
                "coreLiveAllocationsDelta: 0\nopenNativeEvents: 0\nownedNativeResults: 0\n"
                "nativeCancelRequestsObserved: %llu\n",
        static_cast<unsigned long long>(Resolver::telemetry().cancellations));
#else
    assert(!Resolver::supported());
    assert(Resolver::resolveOnWorker(desc, "localhost", 54321, {}, closed).status
        == ResolverStatus::Unsupported);
    std::puts("Resolver unsupported-platform execution gate PASS");
#endif
}
