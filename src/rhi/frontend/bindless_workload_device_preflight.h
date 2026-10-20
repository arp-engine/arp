#pragma once
#include "rhi/frontend/bindless_original_workload_resources.h"
#include "rhi/frontend/bindless_workload_retention.h"
#include "rhi/gpu_extensions.h"

namespace arp::rhi {
struct OriginalBindlessWorkloadDeviceReport {
    OriginalBindlessWorkloadResourceReport inventory;
    semantics::BindlessWorkloadResourceLimits deviceLimits;
    bool globalAlignmentValidated = false;
};

class OriginalBindlessWorkloadDevicePreflight final {
public:
    // Reuses ceilings already owned by canonical Source admission. This only
    // bounds a read-only scan; no new allocation/reservation is authorized.
    static const char* inspectAdmitted(
        const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
        const GpuDevice& device,
        OriginalBindlessWorkloadResourceReport& output) noexcept
    {
        output = {};
        if (const auto* id = OriginalCommandResourceSources::validateOriginal(inventory))
            return id;
        OriginalBindlessWorkloadResourceInspectionBudget budget{1u, true};
        for (const auto limit : inventory->originalBudget().maxSourceOwners) {
            if (std::uint64_t(limit) > UINT64_MAX - budget.maxSourceVisits)
                return "RHI-ORIGINAL-WORKLOAD-DEVICE-VISIT-OVERFLOW";
            budget.maxSourceVisits += limit;
        }
        if (inventory->sources().size() > budget.maxSourceVisits)
            return "RHI-ORIGINAL-WORKLOAD-RESOURCES-SOURCE-CAPACITY";
        if (!core::canMultiplyUnsigned(budget.maxSourceVisits,
                std::uint64_t(
                    OriginalBindlessWorkloadResourceInspection::maxRetainedResourcesPerSource)))
            return "RHI-ORIGINAL-WORKLOAD-DEVICE-VISIT-OVERFLOW";
        budget.maxRetainedResourceVisits = budget.maxSourceVisits
            * OriginalBindlessWorkloadResourceInspection::maxRetainedResourcesPerSource;
        for (const auto& source : inventory->sources()) {
            const auto limit = std::visit(
                [](const auto& value) -> std::uint64_t {
                    using T =
                        std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                    if constexpr (T::domain == CommandReferenceDomain::BindlessRoot
                        || T::domain == CommandReferenceDomain::Descriptor
                        || T::domain == CommandReferenceDomain::DescriptorTable)
                        return value->frozenElementLimit();
                    return 0u;
                },
                source);
            if (limit > UINT64_MAX - budget.maxFrozenDescriptorElements)
                return "RHI-ORIGINAL-WORKLOAD-DEVICE-VISIT-OVERFLOW";
            budget.maxFrozenDescriptorElements += limit;
        }
        return OriginalBindlessWorkloadResourceInspection::inspect(
            inventory, device, budget, output);
    }

    static const char* validate(
        const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
        GpuDevice& device,
        OriginalBindlessWorkloadDeviceReport& output,
        bool requireWorkloadRetention = false) noexcept
    {
        output = {};
        OriginalBindlessWorkloadDeviceReport result;
        if (const auto* id = inspectAdmitted(inventory, device, result.inventory))
            return id;
        if (const auto* id =
                OriginalBindlessWorkloadRetention::validate(inventory, requireWorkloadRetention))
            return id;
        const auto decision =
            semantics::BindlessWorkloadResourceContract::devicePreflightLowering(device.backend());
        if (!decision.available)
            return decision.diagnostic;
        const auto* issuer = static_cast<const BindlessCrossExtension*>(
            device.queryInterface(kExtensionBindlessCross));
        if (!issuer)
            return "RHI-ORIGINAL-WORKLOAD-DEVICE-ISSUER";
        const auto& caps = issuer->capabilities();
        if (!caps.deviceIdentity || !caps.limitsQueried)
            return "RHI-BINDLESS-WORKLOAD-LIMITS-UNAVAILABLE";
        result.deviceLimits = caps.workloadResourceLimits;
        const auto* id = decision.globalAlignmentRequired
            ? semantics::BindlessWorkloadResourceContract::validateLimits(
                  result.inventory.demand, result.deviceLimits)
            : semantics::BindlessWorkloadResourceContract::validateSizeLimits(
                  result.inventory.demand, result.deviceLimits);
        if (id)
            return id;
        result.globalAlignmentValidated = decision.globalAlignmentRequired;
        output = result;
        return nullptr;
    }
};
} // namespace arp::rhi
