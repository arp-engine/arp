#pragma once
#include "rhi/frontend/bindless_original_workload_resources.h"

namespace arp::rhi {
// Cold reservation on the original sealed inventory. No copied resource table,
// replacement state machine or completion notification is created.
class OriginalBindlessWorkloadRetention final {
public:
    using Amounts = semantics::BindlessWorkloadCapacityAmounts;
    using Domain = semantics::BindlessWorkloadCapacityDomain;

    static const char* inspect(
        const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
        const GpuDevice& device,
        Amounts& output,
        bool coldAssembly = false) noexcept
    {
        output = {};
        if (const auto* id =
                OriginalCommandResourceSources::validateRegisteredLifetime(inventory, device))
            return id;
        if (!coldAssembly && !inventory->inventorySealed())
            return "RHI-BINDLESS-WORKLOAD-RETENTION-UNSEALED";
        Amounts result{};
        const auto add = [&](Domain domain, std::uint64_t value) -> const char* {
            auto& sum = result[static_cast<std::size_t>(domain)];
            if (value > UINT64_MAX - sum)
                return "RHI-BINDLESS-WORKLOAD-RETENTION-OVERFLOW";
            sum += value;
            return nullptr;
        };
        result[static_cast<std::size_t>(Domain::Workloads)] = 1u;
        // Canonical source validation already enforces exactly one original
        // resource lease per resource, and includes every nested dependency.
        for (std::size_t i = 0u; i < inventory->sources().size(); ++i) {
            const auto* id = std::visit(
                [&](const auto& value) -> const char* {
                    using T =
                        std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                    if constexpr (std::is_same_v<T, OriginalCommandBufferLease>
                        || std::is_same_v<T, OriginalCommandTextureLease>
                        || std::is_same_v<T, OriginalCommandReadbackTextureSource>) {
                        const auto* resource = value->originalObject();
                        const auto bytes = resource->allocation().estimatedSizeBytes;
                        if (!bytes)
                            return "RHI-BINDLESS-WORKLOAD-RETENTION-ALLOCATION-UNKNOWN";
                        if constexpr (std::is_same_v<T, OriginalCommandBufferLease>)
                            if (bytes < resource->desc().sizeBytes)
                                return "RHI-BINDLESS-WORKLOAD-RETENTION-ALLOCATION-SIZE";
                        if (const auto* error = add(Domain::Resources, 1u))
                            return error;
                        return add(Domain::ResourceBytes, bytes);
                    } else if constexpr (T::domain == CommandReferenceDomain::BindlessRoot) {
                        const auto& snapshot = value->originalSnapshot();
                        for (std::size_t j = 0u; j < i; ++j)
                            if (const auto* prior = std::get_if<
                                    std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                                    &inventory->sources()[j]))
                                if (core::SharedOwnerProjection::sameOwner(
                                        (*prior)->originalSnapshot(), snapshot))
                                    return nullptr;
                        if (!snapshot->originalCountLease() || !snapshot->originalByteLease()
                            || snapshot->originalCountLease()->units() != 1u
                            || snapshot->originalByteLease()->units()
                                != snapshot->plan().snapshotBytes)
                            return "RHI-BINDLESS-WORKLOAD-RETENTION-SNAPSHOT-CHARGE";
                        if (const auto* error = add(Domain::Snapshots, 1u))
                            return error;
                        if (const auto* error =
                                add(Domain::SnapshotBytes, snapshot->originalByteLease()->units()))
                            return error;
                        for (const auto* buffer :
                            {snapshot->faultLatchBuffer(), snapshot->faultReadbackBuffer()}) {
                            if (!buffer)
                                return "RHI-BINDLESS-WORKLOAD-RETENTION-FAULT-RESOURCE";
                            const auto bytes = buffer->allocation().estimatedSizeBytes;
                            if (bytes < buffer->desc().sizeBytes)
                                return "RHI-BINDLESS-WORKLOAD-RETENTION-FAULT-ALLOCATION";
                            if (const auto* error = add(Domain::FaultBytes, bytes))
                                return error;
                        }
                    }
                    return nullptr;
                },
                inventory->sources()[i]);
            if (id)
                return id;
        }
        if (!result[static_cast<std::size_t>(Domain::Snapshots)])
            return "RHI-BINDLESS-WORKLOAD-RETENTION-NO-ROOT";
        std::uint32_t cpuOwners = 0u, owners = 0u, registrations = 0u;
        // Include the reservation and its pool in the same canonical owner
        // count before they are attached. No owner can escape the existing cap.
        const auto additional = inventory->originalWorkloadReservation()
            ? 0u
            : OriginalBindlessWorkloadReservation::retainedOwnerCount;
        if (const auto* id = inventory->cpuOwnerCount(
                2u + additional, inventory->originalBudget().maxGraphOwners, cpuOwners))
            return id;
        if (const auto* id = OriginalCommandResourceSources::registeredOwnerUnion(
                inventory, cpuOwners, UINT32_MAX, UINT32_MAX, owners, registrations))
            return id;
        result[static_cast<std::size_t>(Domain::RetainedOwners)] = owners;
        output = result;
        return nullptr;
    }

    // Externally serialized cold operation. Atomically attach and freeze the
    // complete typed assembly before any graph caches its owner count.
    static const char* prepare(
        const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
        const GpuDevice& device,
        const std::shared_ptr<BindlessWorkloadCapacityPool>& pool) noexcept
    {
        if (!pool || !pool->sharedOwnerMatches(pool))
            return "RHI-BINDLESS-WORKLOAD-RETENTION-POOL-OWNER";
        if (const auto* id = semantics::BindlessWorkloadCapacityContract::validate(pool->budget()))
            return id;
        const auto decision = semantics::BindlessWorkloadCapacityContract::lowering(
            device.backend(), pool->budget().contractVersion);
        if (!decision.reservationAvailable)
            return decision.diagnostic;
        Amounts amounts;
        if (const auto* id = inspect(inventory, device, amounts, true))
            return id;
        if (const auto& original = inventory->originalWorkloadReservation()) {
            if (!core::SharedOwnerProjection::sameOwner(pool, original->originalPool()))
                return "RHI-BINDLESS-WORKLOAD-RETENTION-POOL-MISMATCH";
            return validate(inventory);
        }
        if (inventory->inventorySealed() || inventory->originalStateTransactionCount()
            || inventory->originalReservedSubmissionCount()
            || inventory->originalReservedCallbackTicketCount()
            || inventory->originalReservedCallbackRegistrationCount())
            return "RHI-BINDLESS-WORKLOAD-RETENTION-ALREADY-IN-USE";
        try {
            core::CpuMemoryQuotaScope scope(inventory->originalMetadataQuota());
            auto reservation = core::QuotaBoundSharedObjectIdentity::shareOriginal(
                core::makeUnique<OriginalBindlessWorkloadReservation>(
                    OriginalBindlessWorkloadReservation::ConstructionKey{},
                    pool,
                    inventory.get(),
                    amounts));
            if (!reservation->charged())
                return "RHI-BINDLESS-WORKLOAD-RETENTION-CAPACITY";
            inventory->workloadReservation_ = std::move(reservation);
            inventory->sealed_ = true;
            return nullptr;
        } catch (const std::bad_alloc&) {
            return "RHI-BINDLESS-WORKLOAD-RETENTION-ALLOCATION";
        }
    }

    static const char* validate(
        const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
        bool required = true) noexcept
    {
        if (const auto* id = OriginalCommandResourceSources::validateOriginal(inventory))
            return id;
        const auto& reservation = inventory->originalWorkloadReservation();
        if (!reservation)
            return required ? "RHI-BINDLESS-WORKLOAD-RETENTION-REQUIRED" : nullptr;
        if (!reservation->hasQuotaBoundSharedOwner(reservation)
            || reservation->originalInventory() != inventory.get()
            || !core::SharedOwnerProjection::sameOwner(
                reservation->originalMetadataQuota(), inventory->originalMetadataQuota())
            || !reservation->charged())
            return "RHI-BINDLESS-WORKLOAD-RETENTION-OWNER";
        return nullptr;
    }
};
} // namespace arp::rhi
