#pragma once

#include "core/cpu_allocator.h"
#include "rhi/gpu_common.h"

#include <chrono>
#include <cstdint>

namespace arp::rhi::semantics {

inline constexpr std::uint32_t kShutdownDrainContractVersion = 1u;
inline constexpr const char* kShutdownDrainInvalidDeadlineDiagnostic =
    "ARP-RHI-SHUTDOWN-DRAIN-DEADLINE-INVALID";
inline constexpr const char* kShutdownDrainDeadlineExpiredDiagnostic =
    "ARP-RHI-SHUTDOWN-DRAIN-DEADLINE-EXPIRED";
inline constexpr const char* kShutdownDrainUnavailableDiagnostic =
    "ARP-RHI-SHUTDOWN-DRAIN-UNAVAILABLE";

/// The backend conclusion for a shutdown drain.  A successful result is only
/// possible when the exact submitted-work completion semantics were observed.
enum class ShutdownDrainOutcome : std::uint8_t {
    Native,
    Equivalent,
    DiagnosticFailure,
};

enum class ShutdownDrainState : std::uint8_t {
    Completed,
    DeadlineExpired,
    DiagnosticFailure,
};

/// A single absolute monotonic deadline shared by all shutdown stages.  It is
/// intentionally not a retry count or a backend-specific timeout policy.
struct ShutdownDrainRequest {
    std::uint32_t contractVersion = kShutdownDrainContractVersion;
    std::chrono::steady_clock::time_point deadline{};

    bool hasSupportedVersion() const noexcept
    {
        return contractVersion == kShutdownDrainContractVersion;
    }

    bool hasDeadline() const noexcept
    {
        return deadline != std::chrono::steady_clock::time_point{};
    }

    bool validAt(const std::chrono::steady_clock::time_point now) const noexcept
    {
        return hasSupportedVersion() && hasDeadline() && deadline > now;
    }
};

/// Backend-neutral shutdown/drain telemetry.  Native queue handles, fences,
/// and completion callbacks remain entirely backend-private.
struct ShutdownDrainReport {
    std::uint32_t contractVersion = kShutdownDrainContractVersion;
    rhi::Backend backend = rhi::Backend::Null;
    ShutdownDrainOutcome outcome = ShutdownDrainOutcome::DiagnosticFailure;
    ShutdownDrainState state = ShutdownDrainState::DiagnosticFailure;
    std::uint64_t elapsedNanoseconds = 0u;
    std::uint64_t remainingNanoseconds = 0u;
    std::pmr::string diagnosticId;

    bool completed() const noexcept
    {
        return state == ShutdownDrainState::Completed
            && (outcome == ShutdownDrainOutcome::Native
                || outcome == ShutdownDrainOutcome::Equivalent);
    }
};

inline std::uint64_t shutdownDrainNanoseconds(
    const std::chrono::steady_clock::duration duration) noexcept
{
    if (duration <= std::chrono::steady_clock::duration::zero())
        return 0u;
    const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
    return static_cast<std::uint64_t>(nanoseconds.count());
}

inline void finalizeShutdownDrainTiming(ShutdownDrainReport& report,
    const std::chrono::steady_clock::time_point started,
    const ShutdownDrainRequest& request,
    const std::chrono::steady_clock::time_point finished) noexcept
{
    report.elapsedNanoseconds = shutdownDrainNanoseconds(finished - started);
    report.remainingNanoseconds =
        request.deadline > finished ? shutdownDrainNanoseconds(request.deadline - finished) : 0u;
}

inline ShutdownDrainReport invalidShutdownDrainReport(const rhi::Backend backend,
    const std::chrono::steady_clock::time_point started,
    const ShutdownDrainRequest& request,
    const std::chrono::steady_clock::time_point finished)
{
    ShutdownDrainReport report;
    report.backend = backend;
    report.diagnosticId = kShutdownDrainInvalidDeadlineDiagnostic;
    finalizeShutdownDrainTiming(report, started, request, finished);
    return report;
}

inline ShutdownDrainReport rejectedShutdownDrainReport(const rhi::Backend backend,
    const std::chrono::steady_clock::time_point started,
    const ShutdownDrainRequest& request,
    const std::chrono::steady_clock::time_point finished)
{
    if (!request.hasSupportedVersion() || !request.hasDeadline()) {
        return invalidShutdownDrainReport(backend, started, request, finished);
    }
    ShutdownDrainReport report;
    report.backend = backend;
    // Refusing an unsupported drain before its deadline is not a timeout.
    // This is also the base GpuDevice path; CPU issue/cleanup cannot promote
    // the unavailable implementation to a successful GPU completion drain.
    if (finished >= request.deadline) {
        report.state = ShutdownDrainState::DeadlineExpired;
        report.diagnosticId = kShutdownDrainDeadlineExpiredDiagnostic;
    } else {
        report.state = ShutdownDrainState::DiagnosticFailure;
        report.diagnosticId = kShutdownDrainUnavailableDiagnostic;
    }
    finalizeShutdownDrainTiming(report, started, request, finished);
    return report;
}

} // namespace arp::rhi::semantics
