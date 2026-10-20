#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/bindless_original_workload_resources.h"
#include "rhi/frontend/bindless_workload_device_preflight.h"

#include <cassert>
#include <cstdio>
#include <limits>

namespace arp::test {
class OriginalWorkloadResourceInspectionFixture final {
public:
    static std::shared_ptr<rhi::BindlessWorkloadCapacityPool> prepareRetention(
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        const rhi::GpuDevice& device)
    {
        using Retention = rhi::OriginalBindlessWorkloadRetention;
        using Pool = rhi::BindlessWorkloadCapacityPool;
        Pool::Amounts amounts;
        const auto* id = Retention::inspect(inventory, device, amounts, true);
        if (id)
            std::fprintf(stderr, "Original workload retention: %s\n", id);
        assert(!id && amounts[0] == 1u && amounts[1] && amounts[2] && amounts[3] && amounts[4]
            && amounts[5] && amounts[6]);
        const auto baseline = inventory->originalMetadataQuota()->liveBytes();
        assert(Retention::validate(inventory));
        for (std::size_t domain = 0u; domain < amounts.size(); ++domain) {
            auto limits = amounts;
            --limits[domain];
            auto refused = core::makeShared<Pool>(Pool::Budget{1u, true, limits});
            assert(Retention::prepare(inventory, device, refused));
            assert(!inventory->originalWorkloadReservation() && !inventory->inventorySealed());
            for (std::size_t i = 0u; i < amounts.size(); ++i)
                assert(!refused->observation(static_cast<Pool::Domain>(i)).used());
            assert(inventory->originalMetadataQuota()->liveBytes() == baseline);
        }
        auto pool = core::makeShared<Pool>(Pool::Budget{1u, true, amounts});
        // Allocation failure cannot publish or retain even a first counter.
        const auto held = inventory->originalMetadataQuota()->limitBytes()
            - inventory->originalMetadataQuota()->liveBytes();
        assert(inventory->originalMetadataQuota()->tryReserve(held));
        assert(Retention::prepare(inventory, device, pool));
        inventory->originalMetadataQuota()->release(held);
        assert(!inventory->originalWorkloadReservation() && !inventory->inventorySealed());
        assert(!Retention::prepare(inventory, device, pool));
        assert(inventory->inventorySealed() && !Retention::validate(inventory));
        const auto reservation = inventory->originalWorkloadReservation();
        assert(reservation && reservation->amounts() == amounts);
        assert(!Retention::prepare(inventory, device, pool));
        auto foreign = core::makeShared<Pool>(Pool::Budget{1u, true, amounts});
        assert(Retention::prepare(inventory, device, foreign));
        assert(inventory->originalWorkloadReservation() == reservation);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{inventory->originalMetadataQuota()}, [&]() noexcept -> const char* {
                Pool::Amounts observed;
                if (const auto* diagnostic = Retention::inspect(inventory, device, observed))
                    return diagnostic;
                assert(observed == amounts);
                return Retention::validate(inventory);
            }));
        std::puts(
            "BINDLESS_WORKLOAD_RETENTION_OK original-inventory independent7 exact one-less allocation-failure hot0 no-execution");
        return pool;
    }

    static rhi::OriginalBindlessWorkloadResourceInspectionBudget budgetFor(
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory)
    {
        rhi::OriginalBindlessWorkloadResourceInspectionBudget budget{1u,
            true,
            inventory->sources().size(),
            0u,
            inventory->sources().size()
                * rhi::OriginalBindlessWorkloadResourceInspection::maxRetainedResourcesPerSource};
        for (const auto& source : inventory->sources())
            budget.maxFrozenDescriptorElements += std::visit(
                [](const auto& value) -> std::size_t {
                    using T =
                        std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                    if constexpr (T::domain == rhi::CommandReferenceDomain::BindlessRoot)
                        return value->elements().size();
                    else if constexpr (T::domain == rhi::CommandReferenceDomain::Descriptor
                        || T::domain == rhi::CommandReferenceDomain::DescriptorTable)
                        return value->frozenElements().size();
                    return 0u;
                },
                source);
        return budget;
    }

    static void inspectRetired(
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        const rhi::GpuDevice& device)
    {
        rhi::OriginalBindlessWorkloadResourceReport report;
        report.demand.maxBufferBytes = 123u;
        report.sourceVisits = 123u;
        const auto* expected =
            rhi::OriginalCommandResourceSources::validateRegisteredLifetime(inventory, device);
        assert(expected);
        const auto* id = rhi::OriginalBindlessWorkloadResourceInspection::inspect(
            inventory, device, budgetFor(inventory), report);
        assert(id && core::StringView(id) == expected && !report.demand.maxBufferBytes
            && !report.sourceVisits);
        report.demand.maxBufferBytes = 123u;
        id = rhi::OriginalBindlessWorkloadDevicePreflight::inspectAdmitted(
            inventory, device, report);
        assert(id && core::StringView(id) == expected && !report.demand.maxBufferBytes
            && !report.sourceVisits);
    }

    static void inspectUnsealed(
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        const rhi::GpuDevice& device)
    {
        rhi::OriginalBindlessWorkloadResourceReport report;
        report.demand.maxBufferBytes = 123u;
        const auto* id = rhi::OriginalBindlessWorkloadResourceInspection::inspect(
            inventory, device, {1u, true, inventory->sources().size(), 0u, 0u}, report);
        assert(id && core::StringView(id) == "RHI-ORIGINAL-WORKLOAD-RESOURCES-UNSEALED"
            && !report.demand.maxBufferBytes && !report.sourceVisits);
        id = rhi::OriginalBindlessWorkloadDevicePreflight::inspectAdmitted(
            inventory, device, report);
        assert(id && core::StringView(id) == "RHI-ORIGINAL-WORKLOAD-RESOURCES-UNSEALED"
            && !report.demand.maxBufferBytes && !report.sourceVisits);
    }

    static void inspect(const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        rhi::GpuDevice& device)
    {
        using Inspection = rhi::OriginalBindlessWorkloadResourceInspection;
        using Report = rhi::OriginalBindlessWorkloadResourceReport;
        auto budget = budgetFor(inventory);
        Report report;
        const auto* id = Inspection::inspect(inventory, device, budget, report);
        if (id)
            std::fprintf(stderr, "Original workload resource inspection: %s\n", id);
        assert(!id && report.sourceVisits == inventory->sources().size()
            && report.frozenDescriptorElements == budget.maxFrozenDescriptorElements
            && report.bufferVisits >= 1u && report.demand.maxBufferBytes
            && report.demand.maxBufferBytes >= report.demand.maxUniformBindingBytes
            && report.demand.maxBufferBytes >= report.demand.maxStorageBindingBytes
            && report.validatorOnly == (device.backend() == rhi::Backend::Null));
        budget.maxRetainedResourceVisits = report.bufferVisits + report.textureVisits;
        const auto expected = report;
        const auto fakeOwner =
            core::makeShared<std::shared_ptr<const rhi::OriginalCommandResourceSources>>(inventory);
        const std::shared_ptr<const rhi::OriginalCommandResourceSources> forged(
            fakeOwner, inventory.get());
        // A separately configured original device is still foreign; equal
        // descriptors and compatible validator capabilities cannot substitute.
        auto foreign = rhi::createNullDevice();
        const auto cpuBytes = inventory->originalMetadataQuota()->limitBytes();
        assert(!foreign->prepareOriginalDeviceLifetime(
            {1u, true, 1u, 2u, cpuBytes}, core::makeShared<core::CpuMemoryQuota>(cpuBytes)));
        rhi::OriginalResourceRegistrationBudget foreignRegistration{1u,
            true,
            1u,
            0u,
            0u,
            rhi::OriginalResourceRegistrationContract::fixedOwnerCount
                + rhi::OriginalResourceRegistrationContract::perResourceOwnerCount,
            cpuBytes};
        assert(!foreign->prepareOriginalResourceRegistration(
            foreignRegistration, core::makeShared<core::CpuMemoryQuota>(cpuBytes)));
        const auto empty = [](const Report& value) {
            const auto& d = value.demand;
            return !value.sourceVisits && !value.bufferVisits && !value.textureVisits
                && !value.samplerSources && !value.frozenDescriptorElements && !value.validatorOnly
                && !d.maxBufferBytes && !d.maxUniformBindingBytes && !d.maxStorageBindingBytes
                && !d.uniformOffsetDivisor && !d.storageOffsetDivisor && !d.texture2DDimension
                && !d.texture3DDimension && !d.textureArrayLayers;
        };
        rhi::OriginalBindlessWorkloadDeviceReport checked;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{inventory->originalMetadataQuota()}, [&]() noexcept -> const char* {
                const auto reject = [&](const char* expectedId, const auto& owner) {
                    report = expected;
                    const auto* diagnostic = Inspection::inspect(owner, device, budget, report);
                    assert(
                        diagnostic && core::StringView(diagnostic) == expectedId && empty(report));
                };
                for (const auto version : {0u, 2u, UINT32_MAX}) {
                    budget.contractVersion = version;
                    reject("RHI-ORIGINAL-WORKLOAD-RESOURCES-VERSION", inventory);
                }
                budget.contractVersion = 1u;
                budget.prototypeOptIn = false;
                reject("RHI-ORIGINAL-WORKLOAD-RESOURCES-OPT-IN", inventory);
                budget.prototypeOptIn = true;
                --budget.maxSourceVisits;
                reject("RHI-ORIGINAL-WORKLOAD-RESOURCES-SOURCE-CAPACITY", inventory);
                ++budget.maxSourceVisits;
                assert(budget.maxFrozenDescriptorElements);
                --budget.maxFrozenDescriptorElements;
                reject("RHI-ORIGINAL-WORKLOAD-RESOURCES-DESCRIPTOR-CAPACITY", inventory);
                ++budget.maxFrozenDescriptorElements;
                --budget.maxRetainedResourceVisits;
                reject("RHI-ORIGINAL-WORKLOAD-RESOURCES-RESOURCE-CAPACITY", inventory);
                ++budget.maxRetainedResourceVisits;
                reject("RHI-ORIGINAL-SOURCES-OWNER", forged);
                reject("RHI-ORIGINAL-SOURCES-OWNER",
                    std::shared_ptr<const rhi::OriginalCommandResourceSources>{});
                report = expected;
                assert(Inspection::inspect(inventory, *foreign, budget, report) && empty(report));
                if (const auto* diagnostic = Inspection::inspect(inventory, device, budget, report))
                    return diagnostic;
                assert(report.demand.maxBufferBytes == expected.demand.maxBufferBytes
                    && report.demand.maxUniformBindingBytes
                        == expected.demand.maxUniformBindingBytes
                    && report.demand.maxStorageBindingBytes
                        == expected.demand.maxStorageBindingBytes
                    && report.demand.uniformOffsetDivisor == expected.demand.uniformOffsetDivisor
                    && report.demand.storageOffsetDivisor == expected.demand.storageOffsetDivisor
                    && report.demand.texture2DDimension == expected.demand.texture2DDimension
                    && report.demand.texture3DDimension == expected.demand.texture3DDimension
                    && report.demand.textureArrayLayers == expected.demand.textureArrayLayers);
                using Preflight = rhi::OriginalBindlessWorkloadDevicePreflight;
                Report admitted;
                assert(!Preflight::inspectAdmitted(inventory, device, admitted));
                assert(admitted.sourceVisits == expected.sourceVisits
                    && admitted.frozenDescriptorElements == expected.frozenDescriptorElements
                    && admitted.demand.maxBufferBytes == expected.demand.maxBufferBytes);
                for (const auto& owner :
                    {forged, std::shared_ptr<const rhi::OriginalCommandResourceSources>{}}) {
                    admitted = expected;
                    assert(Preflight::inspectAdmitted(owner, device, admitted) && empty(admitted));
                }
                admitted = expected;
                assert(
                    Preflight::inspectAdmitted(inventory, *foreign, admitted) && empty(admitted));
                checked.inventory = expected;
                checked.deviceLimits.contractVersion = 123u;
                checked.globalAlignmentValidated = true;
                const auto* deviceId = Preflight::validate(inventory, device, checked);
                if (device.backend() == rhi::Backend::Null) {
                    assert(deviceId
                        && core::StringView(deviceId)
                            == "RHI-BINDLESS-WORKLOAD-DEVICE-VALIDATOR-ONLY"
                        && empty(checked.inventory) && !checked.deviceLimits.contractVersion
                        && !checked.globalAlignmentValidated);
                } else {
                    if (deviceId)
                        std::fprintf(stderr, "Original device resource preflight: %s\n", deviceId);
                    assert(!deviceId && checked.deviceLimits.contractVersion == 1u
                        && checked.inventory.demand.maxBufferBytes == expected.demand.maxBufferBytes
                        && checked.globalAlignmentValidated
                            == (device.backend() != rhi::Backend::Metal));
                }
                return nullptr;
            }));
        if (device.backend() != rhi::Backend::Null) {
            const auto& limits = checked.deviceLimits;
            std::printf(
                "BINDLESS_ORIGINAL_DEVICE_PREFLIGHT_OK maxBuffer%llu uniform%llu storage%llu dim2D%u dim3D%u layers%u globalAlignment%u hot0 no-execution\n",
                static_cast<unsigned long long>(limits.maxBufferBytes),
                static_cast<unsigned long long>(limits.maxUniformBindingBytes),
                static_cast<unsigned long long>(limits.maxStorageBindingBytes),
                limits.texture2DDimension,
                limits.texture3DDimension,
                limits.textureArrayLayers,
                checked.globalAlignmentValidated ? 1u : 0u);
        }
        std::printf(
            "BINDLESS_ORIGINAL_WORKLOAD_RESOURCES_OK sources%llu buffers%llu textures%llu samplers%llu descriptors%llu maxBuffer%llu uniform%llu storage%llu dim2D%u dim3D%u layers%u hot0 no-execution\n",
            static_cast<unsigned long long>(report.sourceVisits),
            static_cast<unsigned long long>(report.bufferVisits),
            static_cast<unsigned long long>(report.textureVisits),
            static_cast<unsigned long long>(report.samplerSources),
            static_cast<unsigned long long>(report.frozenDescriptorElements),
            static_cast<unsigned long long>(report.demand.maxBufferBytes),
            static_cast<unsigned long long>(report.demand.maxUniformBindingBytes),
            static_cast<unsigned long long>(report.demand.maxStorageBindingBytes),
            report.demand.texture2DDimension,
            report.demand.texture3DDimension,
            report.demand.textureArrayLayers);
    }
};
} // namespace arp::test
