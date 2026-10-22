#pragma once

#include "rhi/frontend/page_slice_allocator.h"

#include <cstdint>
#include <string_view>

namespace arp::rhi::semantics {

inline constexpr std::uint32_t kUploadStagingReadinessVersion = 1u;
inline constexpr const char* kUploadStagingInFlightCapacityDiagnostic =
    "ARP-RHI-STAGING-IN-FLIGHT-CAPACITY";
inline constexpr const char* kUploadStagingReadinessUnavailableDiagnostic =
    "ARP-RHI-UPLOAD-STAGING-READINESS-UNAVAILABLE";

/// A backend-neutral completion observation for completion-gated upload work.
/// `completionEpoch` is deliberately not a submission serial: it is only a
/// monotonic credential that advances after the backend has observed work
/// retire. Consumers may compare epochs, but cannot derive native queue state,
/// allocation identity, or a timing policy from it.
enum class UploadStagingReadinessStatus : std::uint8_t {
    Available,
    Equivalent,
    DiagnosticFailure,
};

struct UploadStagingReadiness {
    UploadStagingReadinessStatus status = UploadStagingReadinessStatus::DiagnosticFailure;
    std::uint64_t completionEpoch = 0u;
    const char* diagnosticId = kUploadStagingReadinessUnavailableDiagnostic;

    bool available() const noexcept
    {
        return status == UploadStagingReadinessStatus::Available
            || status == UploadStagingReadinessStatus::Equivalent;
    }

    bool advancedSince(const UploadStagingReadiness& earlier) const noexcept
    {
        return available() && earlier.available() && completionEpoch > earlier.completionEpoch;
    }
};

inline UploadStagingReadiness uploadStagingReadinessUnavailable() noexcept
{
    return {};
}

enum class UploadStagingFailureDisposition {
    DiagnosticFailure,
    RetryAfterCompletion,
};

struct UploadStagingFailureDecision {
    UploadStagingFailureDisposition disposition =
        UploadStagingFailureDisposition::DiagnosticFailure;
    const char* diagnosticId = nullptr;

    bool retryAfterCompletion() const noexcept
    {
        return disposition == UploadStagingFailureDisposition::RetryAfterCompletion;
    }
};

inline UploadStagingFailureDecision classifyUploadStagingFailure(
    std::string_view diagnosticId) noexcept
{
    if (diagnosticId == core::kFrameRetiredPageSliceDiagnosticPageCount
        || diagnosticId == kUploadStagingInFlightCapacityDiagnostic) {
        return {
            UploadStagingFailureDisposition::RetryAfterCompletion,
            diagnosticId == core::kFrameRetiredPageSliceDiagnosticPageCount
                ? core::kFrameRetiredPageSliceDiagnosticPageCount
                : kUploadStagingInFlightCapacityDiagnostic,
        };
    }
    return {UploadStagingFailureDisposition::DiagnosticFailure, nullptr};
}

} // namespace arp::rhi::semantics
