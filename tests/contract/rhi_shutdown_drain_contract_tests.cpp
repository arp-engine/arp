#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/memory_shutdown_drain.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>

#ifdef NDEBUG
#undef assert
#define assert(expression)                                                                         \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            std::fprintf(stderr,                                                                   \
                "ShutdownDrain contract failed: %s (%s:%d)\\n",                                    \
                #expression,                                                                       \
                __FILE__,                                                                          \
                __LINE__);                                                                         \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)
#endif

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi::semantics;

    const auto now = std::chrono::steady_clock::now();
    // An unavailable implementation must not claim its future deadline has
    // expired. Issuing/cancelling CPU work is never a completed GPU drain.
    for (auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        ShutdownDrainRequest future;
        future.deadline = now + std::chrono::seconds(1);
        const auto unavailable = rejectedShutdownDrainReport(backend, now, future, now);
        assert(!unavailable.completed() && unavailable.backend == backend);
        assert(unavailable.outcome == ShutdownDrainOutcome::DiagnosticFailure);
        assert(unavailable.state == ShutdownDrainState::DiagnosticFailure);
        assert(unavailable.diagnosticId == kShutdownDrainUnavailableDiagnostic);
        assert(unavailable.elapsedNanoseconds == 0u
            && unavailable.remainingNanoseconds == 1'000'000'000u);
        const auto timedOut = rejectedShutdownDrainReport(backend, now, future, future.deadline);
        assert(!timedOut.completed() && timedOut.state == ShutdownDrainState::DeadlineExpired);
        assert(timedOut.diagnosticId == kShutdownDrainDeadlineExpiredDiagnostic
            && !timedOut.remainingNanoseconds);
    }
    ShutdownDrainRequest invalid;
    const auto invalidReport = invalidShutdownDrainReport(Backend::Null, now, invalid, now);
    assert(invalidReport.backend == Backend::Null);
    assert(!invalidReport.completed());
    assert(invalidReport.outcome == ShutdownDrainOutcome::DiagnosticFailure);
    assert(invalidReport.state == ShutdownDrainState::DiagnosticFailure);
    assert(invalidReport.diagnosticId == kShutdownDrainInvalidDeadlineDiagnostic);

    auto device = createNullDevice();
    assert(device);
    ShutdownDrainRequest request;
    request.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    const auto report = device->drainForShutdown(request);
    assert(report.backend == Backend::Null);
    assert(report.outcome == ShutdownDrainOutcome::Equivalent);
    assert(report.state == ShutdownDrainState::Completed);
    assert(report.completed());
    assert(report.elapsedNanoseconds <= 1'000'000'000ull);
    assert(report.remainingNanoseconds > 0u);

    request.deadline = std::chrono::steady_clock::now() - std::chrono::nanoseconds(1);
    const auto expired = device->drainForShutdown(request);
    assert(!expired.completed());
    assert(expired.state == ShutdownDrainState::DeadlineExpired);
    assert(expired.diagnosticId == kShutdownDrainDeadlineExpiredDiagnostic);
    return 0;
}
