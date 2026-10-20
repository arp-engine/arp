#pragma once

#include <cstdint>
#include <limits>

namespace arp::rhi::semantics {

inline constexpr std::uint32_t kUploadStagingBudgetVersion = 1u;

inline constexpr const char* kUploadStagingBudgetDiagnosticVersion =
    "ARP-RHI-STAGING-BUDGET-VERSION";
inline constexpr const char* kUploadStagingBudgetDiagnosticRequest =
    "ARP-RHI-STAGING-BUDGET-REQUEST";
inline constexpr const char* kUploadStagingBudgetDiagnosticPage = "ARP-RHI-STAGING-BUDGET-PAGE";
inline constexpr const char* kUploadStagingBudgetDiagnosticPageCount =
    "ARP-RHI-STAGING-BUDGET-PAGE-COUNT";
inline constexpr const char* kUploadStagingBudgetDiagnosticAlignment =
    "ARP-RHI-STAGING-BUDGET-ALIGNMENT";
inline constexpr const char* kUploadStagingBudgetDiagnosticCapacity =
    "ARP-RHI-STAGING-BUDGET-CAPACITY";

/// Versioned L2 declaration for backend-owned upload staging.
///
/// A request is atomic at this boundary: it must fit in one page unless a
/// future version explicitly adds chunk semantics. Backends may choose their
/// native allocation strategy, but they must implement this same bounded
/// request and total-capacity contract.
struct UploadStagingBudgetV1 {
    std::uint32_t version = kUploadStagingBudgetVersion;
    std::uint64_t maxRequestBytes = 0;
    std::uint64_t pageSizeBytes = 0;
    std::uint32_t maxPageCount = 0;
    std::uint64_t totalCapacityBytes = 0;
    std::uint64_t alignmentBytes = 1;
};

struct UploadStagingBudgetValidation {
    bool success = false;
    const char* diagnostics = nullptr;
    const char* diagnosticId = nullptr;
};

inline constexpr bool isUploadStagingPowerOfTwo(const std::uint64_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

inline constexpr UploadStagingBudgetValidation validateUploadStagingBudget(
    const UploadStagingBudgetV1& budget)
{
    if (budget.version != kUploadStagingBudgetVersion) {
        return {
            false,
            "upload staging budget version is unsupported",
            kUploadStagingBudgetDiagnosticVersion,
        };
    }
    if (budget.maxRequestBytes == 0u) {
        return {
            false,
            "upload staging budget max request must be greater than zero",
            kUploadStagingBudgetDiagnosticRequest,
        };
    }
    if (budget.pageSizeBytes < budget.maxRequestBytes) {
        return {
            false,
            "upload staging budget page is smaller than its max request",
            kUploadStagingBudgetDiagnosticPage,
        };
    }
    if (budget.maxPageCount == 0u) {
        return {
            false,
            "upload staging budget page count must be greater than zero",
            kUploadStagingBudgetDiagnosticPageCount,
        };
    }
    if (!isUploadStagingPowerOfTwo(budget.alignmentBytes)) {
        return {
            false,
            "upload staging budget alignment must be a power of two",
            kUploadStagingBudgetDiagnosticAlignment,
        };
    }
    if (budget.pageSizeBytes > std::numeric_limits<std::uint64_t>::max()
            / static_cast<std::uint64_t>(budget.maxPageCount)) {
        return {
            false,
            "upload staging budget capacity overflows",
            kUploadStagingBudgetDiagnosticCapacity,
        };
    }
    const auto derivedCapacity =
        budget.pageSizeBytes * static_cast<std::uint64_t>(budget.maxPageCount);
    if (budget.totalCapacityBytes != derivedCapacity) {
        return {
            false,
            "upload staging budget total capacity does not match page capacity",
            kUploadStagingBudgetDiagnosticCapacity,
        };
    }
    return {true, nullptr, nullptr};
}

} // namespace arp::rhi::semantics
