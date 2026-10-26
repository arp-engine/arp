#pragma once

#include "rhi/frontend/resource_access_scope.h"

namespace arp::rhi::semantics {

// Numeric/intent inspection only. These caller-authored values are never an
// issued submission receipt, a resource observation or native execution proof.
struct OriginalSubmissionReadBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxPendingCommands = 0u, maxBufferReadPins = 0u, maxPredecessorLinks = 0u,
                  maxCpuMetadataBytes = 0u;
};

struct OriginalSubmissionReadUsage {
    std::uint64_t pendingCommands = 0u, bufferReadPins = 0u, predecessorLinks = 0u,
                  cpuMetadataBytes = 0u;
};

class OriginalSubmissionReadContract final {
public:
    static constexpr const char* canonicalStateLowering(
        Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return "RHI-SUBMISSION-READ-VERSION";
        switch (backend) {
        case Backend::Null:
            return nullptr;
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return "RHI-SUBMISSION-READ-CANONICAL-UNAVAILABLE";
        case Backend::WebGL:
            return "RHI-SUBMISSION-READ-UNSUPPORTED";
        }
        return "RHI-SUBMISSION-READ-BACKEND";
    }

    // Same finite custody profile, now reserved before Program/completion
    // construction. No canonical access projection or native authority.
    static constexpr const char* coldReservationLowering(
        Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return "RHI-SUBMISSION-READ-VERSION";
        switch (backend) {
        case Backend::Null:
            return nullptr;
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return "RHI-SUBMISSION-READ-COLD-RESERVATION-UNAVAILABLE";
        case Backend::WebGL:
            return "RHI-SUBMISSION-READ-UNSUPPORTED";
        }
        return "RHI-SUBMISSION-READ-BACKEND";
    }

    // Finite original-control custody prerequisite only; no Program admission,
    // native receipt, canonical pending observation or GPU execution authority.
    static constexpr const char* pinCustodyLowering(Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return "RHI-SUBMISSION-READ-VERSION";
        switch (backend) {
        case Backend::Null:
            return nullptr;
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return "RHI-SUBMISSION-READ-PIN-CUSTODY-UNAVAILABLE";
        case Backend::WebGL:
            return "RHI-SUBMISSION-READ-UNSUPPORTED";
        }
        return "RHI-SUBMISSION-READ-BACKEND";
    }

    struct Decision {
        bool validatorOnly = false, intentInspectionAvailable = false;
        bool nativeReceiptAvailable = false, pendingObservationAvailable = false,
             executionAvailable = false, provesGpuCompletion = false;
        const char* diagnostic = "RHI-SUBMISSION-READ-BACKEND";
    };

    static constexpr const char* validateBudget(const OriginalSubmissionReadBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-SUBMISSION-READ-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-SUBMISSION-READ-OPT-IN";
        if (!budget.maxPendingCommands || !budget.maxBufferReadPins || !budget.maxPredecessorLinks
            || !budget.maxCpuMetadataBytes)
            return "RHI-SUBMISSION-READ-CAPACITY";
        return nullptr;
    }

    static constexpr const char* validateUsage(const OriginalSubmissionReadBudget& budget,
        const OriginalSubmissionReadUsage& usage) noexcept
    {
        if (const auto* id = validateBudget(budget))
            return id;
        if ((!usage.pendingCommands && (usage.bufferReadPins || usage.predecessorLinks))
            || usage.pendingCommands > usage.predecessorLinks
            || usage.predecessorLinks > usage.bufferReadPins
            || (usage.pendingCommands && !usage.cpuMetadataBytes))
            return "RHI-SUBMISSION-READ-USAGE";
        if (usage.pendingCommands > budget.maxPendingCommands)
            return "RHI-SUBMISSION-READ-COMMAND-CAPACITY";
        if (usage.bufferReadPins > budget.maxBufferReadPins)
            return "RHI-SUBMISSION-READ-PIN-CAPACITY";
        if (usage.predecessorLinks > budget.maxPredecessorLinks)
            return "RHI-SUBMISSION-READ-LINK-CAPACITY";
        if (usage.cpuMetadataBytes > budget.maxCpuMetadataBytes)
            return "RHI-SUBMISSION-READ-METADATA-CAPACITY";
        return nullptr;
    }

    static const char* inspectBufferRead(const BufferDesc& buffer,
        std::uint64_t offsetBytes,
        std::uint64_t sizeBytes,
        const ResourceAccessScope& scope) noexcept
    {
        if (!buffer.sizeBytes || offsetBytes || sizeBytes != buffer.sizeBytes)
            return "RHI-SUBMISSION-READ-WHOLE-BUFFER";
        if (buffer.memory != MemoryUsage::GpuOnly && buffer.memory != MemoryUsage::CpuToGpu)
            return "RHI-SUBMISSION-READ-MEMORY";
        if ((buffer.usage & (ResourceUsage::UniformBuffer | ResourceUsage::StorageBuffer))
            == ResourceUsage::None)
            return "RHI-SUBMISSION-READ-BUFFER-USAGE";
        if (const auto* id = validateResourceAccessScope(scope))
            return id;
        if (scope.state != ResourceState::ShaderRead || scope.access != ResourceAccess::ShaderRead)
            return "RHI-SUBMISSION-READ-SCOPE";
        return nullptr;
    }

    static constexpr Decision lowering(Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return {false, false, false, false, false, false, "RHI-SUBMISSION-READ-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, false, false, false, "RHI-SUBMISSION-READ-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {false,
                true,
                false,
                false,
                false,
                false,
                "RHI-SUBMISSION-READ-NATIVE-RECEIPT-UNAVAILABLE"};
        case Backend::WebGL:
            return {false, false, false, false, false, false, "RHI-SUBMISSION-READ-UNSUPPORTED"};
        }
        return {};
    }
};

// Mapping implementation only; each issuer must authenticate actual native
// runtime/queue binding. This is neither a capability derived from the backend
// name nor the full OriginalSubmissionRead receipt/resource-state authority.
class OriginalQueueAcceptanceContract final {
public:
    struct Decision {
        bool validatorOnly = false, backendPrivateMappingImplemented = false;
        bool publicReceiptAvailable = false, pendingResourceAdmissionAvailable = false,
             provesGpuCompletion = false;
        const char* diagnostic = "RHI-QUEUE-ACCEPTANCE-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return {false, false, false, false, false, "RHI-QUEUE-ACCEPTANCE-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, false, false, false, false, "RHI-QUEUE-ACCEPTANCE-VALIDATOR-ONLY"};
        case Backend::Metal:
            return {false, true, false, false, false, nullptr};
        case Backend::Vulkan:
        case Backend::WebGPU:
            return {false,
                false,
                false,
                false,
                false,
                "RHI-QUEUE-ACCEPTANCE-NATIVE-OBSERVATION-UNAVAILABLE"};
        case Backend::WebGL:
            return {false, false, false, false, false, "RHI-QUEUE-ACCEPTANCE-UNSUPPORTED"};
        }
        return {};
    }
};

} // namespace arp::rhi::semantics
