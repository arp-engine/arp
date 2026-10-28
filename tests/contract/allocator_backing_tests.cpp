#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"
#include "rhi/frontend/detail/allocator_backing_ledger.h"
#include "rhi/frontend/memory_allocator_backing.h"

#include <array>
#include <cassert>
#include <cstring>
#include <thread>

using namespace arp::rhi;
using arp::rhi::detail::AllocatorBackingLedger;

namespace arp::tests {
class AllocatorBackingLedgerFixture final {
public:
    static void contendedEvent(AllocatorBackingLedger& ledger)
    {
        assert(!ledger.guard_.test_and_set(std::memory_order_acquire));
        // A blocking regression deadlocks and fails this test's timeout.
        std::thread writer([&] {
            ledger.allocated(64u);
        });
        writer.join();
        ledger.guard_.clear(std::memory_order_release);
    }

    static void busyObservation(AllocatorBackingLedger& ledger)
    {
        assert(!ledger.guard_.test_and_set(std::memory_order_acquire));
        const auto result = ledger.observe(0u, 0u, 0u, 0u);
        assert(result.state == AllocatorBackingState::DiagnosticFailure);
        assert(!std::strcmp(result.diagnostic, "RHI-ALLOCATOR-BACKING-BUSY"));
        assert(!result.backingBytes && !result.allocationEvents);
        ledger.guard_.clear(std::memory_order_release);
    }
};
} // namespace arp::tests

int main(int argc, char** argv)
{
    const char* expected[] = {"RHI-ALLOCATOR-BACKING-NULL-VALIDATOR",
        "RHI-ALLOCATOR-BACKING-VULKAN-UNINSTRUMENTED",
        "RHI-ALLOCATOR-BACKING-METAL-UNINSTRUMENTED",
        "RHI-ALLOCATOR-BACKING-WEBGPU-UNAVAILABLE",
        "RHI-ALLOCATOR-BACKING-WEBGL-UNAVAILABLE"};
    std::size_t backendIndex = 0u;
    for (const auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        const auto unavailable = AllocatorBackingContract::unavailable(backend);
        assert(unavailable.state == AllocatorBackingState::Unsupported && unavailable.diagnostic);
        assert(!std::strcmp(unavailable.diagnostic, expected[backendIndex++]));
        assert(!unavailable.backingBytes && !unavailable.peakBackingBytes
            && !unavailable.occupiedBytes && !unavailable.allocationEvents);
        for (const auto version : {0u, 2u}) {
            const auto invalid = AllocatorBackingContract::unavailable(backend, version);
            assert(invalid.state == AllocatorBackingState::DiagnosticFailure);
            assert(!std::strcmp(invalid.diagnostic, "RHI-ALLOCATOR-BACKING-VERSION"));
        }
    }
    const auto unknown = AllocatorBackingContract::unavailable(static_cast<Backend>(255));
    assert(unknown.state == AllocatorBackingState::DiagnosticFailure);
    assert(!std::strcmp(unknown.diagnostic, "RHI-ALLOCATOR-BACKING-BACKEND"));
    AllocatorBackingLedger ledger;
    assert(ledger.observe(0u, 0u, 0u, 0u).state == AllocatorBackingState::Observed);
    ledger.allocated(1024u);
    ledger.allocated(2048u);
    auto report = ledger.observe(3072u, 2u, 1536u, 3u);
    assert(report.state == AllocatorBackingState::Observed && !report.diagnostic);
    assert(report.backingBytes == 3072u && report.peakBackingBytes == 3072u
        && report.occupiedBytes == 1536u && report.unusedBytes == 1536u);
    assert(report.blockCount == 2u && report.peakBlockCount == 2u && report.allocationCount == 3u);
    assert(ledger.observe(3071u, 2u, 1536u, 3u).state == AllocatorBackingState::DiagnosticFailure);
    assert(ledger.observe(3072u, 1u, 1536u, 3u).state == AllocatorBackingState::DiagnosticFailure);
    assert(ledger.observe(3072u, 2u, 3073u, 3u).state == AllocatorBackingState::DiagnosticFailure);
    assert(ledger.observe(3072u, 2u, 1u, 0u).state == AllocatorBackingState::DiagnosticFailure);
    ledger.released(1024u);
    report = ledger.observe(2048u, 1u, 0u, 0u);
    assert(report.state == AllocatorBackingState::Observed && report.unusedBytes == 2048u);
    ledger.released(2048u);
    report = ledger.observe(0u, 0u, 0u, 0u);
    assert(report.peakBackingBytes == 3072u && report.peakBlockCount == 2u
        && report.allocationEvents == 2u && report.releaseEvents == 2u);
    ledger.released(1u);
    assert(ledger.observe(0u, 0u, 0u, 0u).state == AllocatorBackingState::DiagnosticFailure);
    AllocatorBackingLedger overflow;
    overflow.allocated(std::numeric_limits<std::uint64_t>::max());
    overflow.allocated(1u);
    assert(overflow.observe(std::numeric_limits<std::uint64_t>::max(), 1u, 0u, 0u).state
        == AllocatorBackingState::DiagnosticFailure);
    AllocatorBackingLedger zero;
    zero.allocated(0u);
    assert(zero.observe(0u, 0u, 0u, 0u).state == AllocatorBackingState::DiagnosticFailure);
    AllocatorBackingLedger concurrent;
    std::array<std::thread, 4u> workers;
    for (auto& worker : workers)
        worker = std::thread([&] {
            for (unsigned i = 0u; i < 1000u; ++i) {
                concurrent.allocated(64u);
                concurrent.released(64u);
            }
        });
    for (auto& worker : workers)
        worker.join();
    report = concurrent.observe(0u, 0u, 0u, 0u);
    if (report.state == AllocatorBackingState::Observed) {
        assert(report.allocationEvents == 4000u && report.releaseEvents == 4000u
            && report.peakBackingBytes <= 256u);
    } else {
        assert(report.state == AllocatorBackingState::DiagnosticFailure);
        assert(!std::strcmp(report.diagnostic, "RHI-ALLOCATOR-BACKING-EVENT-CONTENTION"));
        assert(!report.backingBytes && !report.allocationEvents);
    }
    AllocatorBackingLedger contended;
    arp::tests::AllocatorBackingLedgerFixture::contendedEvent(contended);
    report = contended.observe(0u, 0u, 0u, 0u);
    assert(report.state == AllocatorBackingState::DiagnosticFailure);
    assert(!std::strcmp(report.diagnostic, "RHI-ALLOCATOR-BACKING-EVENT-CONTENTION"));
    contended.allocated(64u);
    assert(contended.observe(64u, 1u, 64u, 1u).state == AllocatorBackingState::DiagnosticFailure);
    AllocatorBackingLedger busy;
    arp::tests::AllocatorBackingLedgerFixture::busyObservation(busy);
    assert(busy.observe(0u, 0u, 0u, 0u).state == AllocatorBackingState::Observed);
    auto device = createNullDevice();
    assert(device->observeAllocatorBacking().state == AllocatorBackingState::Unsupported);
    assert(device->observeAllocatorBacking(0u).state == AllocatorBackingState::DiagnosticFailure);
    if (argc == 2 && !std::strcmp(argv[1], "--null-hidden-frame")) {
        assert(NullHiddenFrameSmokeRunner(*device).run({}).success);
        assert(device->observeAllocatorBacking().state == AllocatorBackingState::Unsupported);
    }
}
