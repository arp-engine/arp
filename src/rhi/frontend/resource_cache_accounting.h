#pragma once

#include "core/diagnostic_error.h"
#include "rhi/frontend/memory_physical_budget_ledger.h"
#include "rhi/frontend/transfer_upload_destination_validation.h"

namespace arp::rhi {

enum class ResourceCacheAccountingDomain : std::uint8_t {
    Unspecified,
    LogicalDescriptor,
    NativePhysical,
};

struct ResourceCacheAccountingRequest {
    std::uint32_t contractVersion = 1u;
    ResourceCacheAccountingDomain domain = ResourceCacheAccountingDomain::Unspecified;
};

struct ResourceCacheAccountingResult {
    std::uint32_t contractVersion = 1u;
    ResourceCacheAccountingDomain domain = ResourceCacheAccountingDomain::Unspecified;
    std::uint64_t logicalBytes = 0u;
    semantics::PhysicalBudgetObservation nativePhysicalObservation;
    const char* diagnostic = nullptr;

    bool success() const noexcept
    {
        return contractVersion == 1u && domain == ResourceCacheAccountingDomain::LogicalDescriptor
            && logicalBytes != 0u && diagnostic == nullptr;
    }
};

// ResourceCacheAccounting/1. Metadata arithmetic only. It neither validates
// native resource creation nor grants execution, alias or completion authority.
class ResourceCacheAccounting final {
public:
    static constexpr std::uint32_t contractVersion = 1u;

    template <class Desc>
    static std::uint64_t requireLogicalBytes(Backend backend, const Desc& desc)
    {
        const auto result = query(
            backend, desc, {contractVersion, ResourceCacheAccountingDomain::LogicalDescriptor});
        if (!result.success())
            throw core::DiagnosticError(result.diagnostic);
        return result.logicalBytes;
    }

    static const char* accumulate(std::uint64_t& total, std::uint64_t logicalBytes) noexcept
    {
        if (!core::MemoryBudgetLedger::canAdd(total, logicalBytes))
            return "RHI-CACHE-ACCOUNTING-BYTE-OVERFLOW";
        total += logicalBytes;
        return nullptr;
    }

    static constexpr const char* lowering(
        Backend backend, const ResourceCacheAccountingRequest& request) noexcept
    {
        if (request.contractVersion != contractVersion)
            return "RHI-CACHE-ACCOUNTING-VERSION";
        switch (backend) {
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
        case Backend::WebGL:
            break;
        default:
            return "RHI-CACHE-ACCOUNTING-BACKEND";
        }
        if (request.domain == ResourceCacheAccountingDomain::NativePhysical)
            return "RHI-CACHE-ACCOUNTING-NATIVE-PHYSICAL-UNAVAILABLE";
        if (request.domain != ResourceCacheAccountingDomain::LogicalDescriptor)
            return "RHI-CACHE-ACCOUNTING-DOMAIN";
        return nullptr;
    }

    static ResourceCacheAccountingResult query(Backend backend,
        const TextureDesc& desc,
        const ResourceCacheAccountingRequest& request) noexcept
    {
        ResourceCacheAccountingResult result;
        result.diagnostic = lowering(backend, request);
        if (result.diagnostic)
            return result;
        if (!core::isPowerOfTwoUnsigned(desc.sampleCount) || desc.sampleCount > 64u) {
            result.diagnostic = "RHI-CACHE-ACCOUNTING-SAMPLES";
            return result;
        }
        std::uint64_t bytes = 0u;
        result.diagnostic = semantics::TextureUploadDestinationValidation::footprint(desc, bytes);
        if (result.diagnostic)
            return result;
        if (!core::canMultiplyUnsigned(bytes, std::uint64_t{desc.sampleCount})) {
            result.diagnostic = "RHI-CACHE-ACCOUNTING-BYTE-OVERFLOW";
            return result;
        }
        result.logicalBytes = bytes * desc.sampleCount;
        result.domain = request.domain;
        return result;
    }

    static ResourceCacheAccountingResult query(Backend backend,
        const BufferDesc& desc,
        const ResourceCacheAccountingRequest& request) noexcept
    {
        ResourceCacheAccountingResult result;
        result.diagnostic = lowering(backend, request);
        if (!result.diagnostic && !desc.sizeBytes)
            result.diagnostic = "RHI-CACHE-ACCOUNTING-BUFFER-SIZE";
        if (result.diagnostic)
            return result;
        result.domain = request.domain;
        result.logicalBytes = desc.sizeBytes;
        return result;
    }
};

} // namespace arp::rhi
