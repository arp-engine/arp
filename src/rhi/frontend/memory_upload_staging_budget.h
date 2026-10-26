#pragma once

#include "rhi/gpu_common.h"

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
inline constexpr const char* kUploadStagingBudgetDiagnosticReduction =
    "ARP-RHI-STAGING-BUDGET-REDUCTION";

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

// Admission for a workload that only reduces an already authored ceiling.
// Validate both complete descriptions first; never normalize or clamp an
// invalid request. Alignment is an exact representation constraint here.
// This numeric relation grants no backend recording or execution authority.
inline constexpr UploadStagingBudgetValidation validateUploadStagingBudgetReduction(
    const UploadStagingBudgetV1& budget, const UploadStagingBudgetV1& ceiling)
{
    if (const auto result = validateUploadStagingBudget(budget); !result.success)
        return result;
    if (const auto result = validateUploadStagingBudget(ceiling); !result.success)
        return result;
    if (budget.maxRequestBytes > ceiling.maxRequestBytes
        || budget.pageSizeBytes > ceiling.pageSizeBytes
        || budget.maxPageCount > ceiling.maxPageCount
        || budget.totalCapacityBytes > ceiling.totalCapacityBytes
        || budget.alignmentBytes != ceiling.alignmentBytes)
        return {false,
            "upload staging policy exceeds its declared ceiling or changes alignment",
            kUploadStagingBudgetDiagnosticReduction};
    return {true, nullptr, nullptr};
}

// Cold metadata/page preparation only; this grants no original upload
// initialization, command recording or execution authority.
struct UploadStagingColdStorageBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    UploadStagingBudgetV1 capacity;
    std::uint32_t maxLiveSlicesPerPage = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

class UploadStagingColdStorageContract final {
public:
    static constexpr const char* validate(const UploadStagingColdStorageBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-STAGING-COLD-STORAGE-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-STAGING-COLD-STORAGE-OPT-IN";
        if (const auto validation = validateUploadStagingBudget(budget.capacity);
            !validation.success)
            return validation.diagnosticId;
        if (!budget.maxLiveSlicesPerPage
            || budget.maxLiveSlicesPerPage
                > std::numeric_limits<std::uint32_t>::max() / budget.capacity.maxPageCount)
            return "RHI-STAGING-COLD-STORAGE-SLICE-CAPACITY";
        if (!budget.maxCpuMetadataBytes)
            return "RHI-STAGING-COLD-STORAGE-CPU-CAPACITY";
        return nullptr;
    }

    enum class Disposition { ValidatorOnly, Prototype, DiagnosticFailure };

    struct Decision {
        Disposition disposition = Disposition::DiagnosticFailure;
        bool coldPreparationImplemented = false;
        bool originalExecutionAvailable = false;
        const char* diagnostic = "RHI-STAGING-COLD-STORAGE-BACKEND";
    };

    static constexpr Decision lowering(
        Backend backend, const UploadStagingColdStorageBudget& budget) noexcept
    {
        if (const auto* id = validate(budget))
            return {Disposition::DiagnosticFailure, false, false, id};
        switch (backend) {
        case Backend::Null:
            return {Disposition::ValidatorOnly,
                false,
                false,
                "RHI-STAGING-COLD-STORAGE-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {Disposition::Prototype, true, false, nullptr};
        case Backend::Metal:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-STAGING-COLD-STORAGE-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-STAGING-COLD-STORAGE-WEBGPU-PAYLOAD-MISSING"};
        case Backend::WebGL:
            return {Disposition::DiagnosticFailure,
                false,
                false,
                "RHI-STAGING-COLD-STORAGE-WEBGL-UNSUPPORTED"};
        }
        return {};
    }
};

} // namespace arp::rhi::semantics
