#pragma once

#include "cpu_quota_factory_fixture.h"
#include "original_device_native_loss_contract_fixture.h"
#include "original_state_domain_fixture.h"
#include "rhi/frontend/command_model.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/command_lowering.h"

#include <cstdio>
#include <string_view>
#include <type_traits>

namespace arp::test {
class OriginalDeviceLifetimeFixture final {
public:
    template <class Factory> static void verify(Factory&& createActualDevice)
    {
        OriginalDeviceNativeLossContractFixture::verify();
        OriginalStateDomainFixture::verify(createActualDevice);
        using Contract = rhi::OriginalDeviceLifetimeContract;
        using Ticket = rhi::OriginalDeviceLifetime::Ticket;
        static_assert(!std::is_copy_constructible_v<rhi::GpuDevice>);
        static_assert(!std::is_move_constructible_v<rhi::GpuDevice>);
        static_assert(!std::is_default_constructible_v<rhi::OriginalDeviceLifetime>);
        const rhi::OriginalDeviceLifetimeBudget budget{1u, true, 1u, 2u, 65536u};
        const auto is = [](const char* actual, const char* expected) {
            assert(actual && std::string_view(actual) == expected);
        };
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = Contract::lowering(backend);
            assert(decision.logicalLifetimeAvailable
                && decision.mode == rhi::CapabilityMode::Equivalent);
            assert(!decision.resourceIssuerAvailable && !decision.nativeLossObserved
                && !decision.executionAvailable);
            assert(!arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend)
                    .originalIssuerAvailable);
        }
        assert(!Contract::lowering(static_cast<rhi::Backend>(UINT32_MAX)).logicalLifetimeAvailable);
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        const auto cold = [&]() {
            // Actual device factory runs outside the domain's quota scope.
            auto device = createActualDevice();
            assert(device);
            const auto* id = device->prepareOriginalDeviceLifetime(budget, quota);
            if (id) {
                is(id, "RHI-ORIGINAL-DEVICE-LIFETIME-ALLOCATION");
                device.reset();
            }
            return device;
        };
        CpuQuotaFactoryFixture factory(quota);
        const auto peak = factory.measure(cold);
        assert(!quota->liveBytes());
        factory.verify(peak, cold);
        assert(!quota->liveBytes());
        Ticket expired;
        {
            auto device = createActualDevice();
            auto foreign = createActualDevice();
            assert(device && foreign);
            is(device->originalDeviceLifetimeTicket().diagnostic(),
                "RHI-ORIGINAL-DEVICE-LIFETIME-EXPIRED");
            const auto invalid = [&](auto change, const char* expected) {
                auto changed = budget;
                change(changed);
                is(device->prepareOriginalDeviceLifetime(changed, quota), expected);
                assert(!quota->liveBytes());
            };
            invalid(
                [](auto& b) {
                    b.contractVersion = 0u;
                },
                "RHI-ORIGINAL-DEVICE-LIFETIME-VERSION");
            invalid(
                [](auto& b) {
                    b.prototypeOptIn = false;
                },
                "RHI-ORIGINAL-DEVICE-LIFETIME-OPT-IN");
            invalid(
                [](auto& b) {
                    b.maxDeviceDomains = 0u;
                },
                "RHI-ORIGINAL-DEVICE-LIFETIME-DOMAIN-BUDGET");
            invalid(
                [](auto& b) {
                    b.maxDeviceDomains = 2u;
                },
                "RHI-ORIGINAL-DEVICE-LIFETIME-DOMAIN-BUDGET");
            invalid(
                [](auto& b) {
                    b.maxRetainedOwners = 1u;
                },
                "RHI-ORIGINAL-DEVICE-LIFETIME-OWNER-BUDGET");
            invalid(
                [](auto& b) {
                    b.maxCpuMetadataBytes = 0u;
                },
                "RHI-ORIGINAL-DEVICE-LIFETIME-CPU-BUDGET");
            is(device->prepareOriginalDeviceLifetime(budget, {}),
                "RHI-ORIGINAL-DEVICE-LIFETIME-QUOTA");
            auto wrong = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes - 1u);
            is(device->prepareOriginalDeviceLifetime(budget, wrong),
                "RHI-ORIGINAL-DEVICE-LIFETIME-QUOTA");
            // Exhaustion fails without attaching a domain or consuming device ownership.
            {
                core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
                assert(pressure.charged());
                is(device->prepareOriginalDeviceLifetime(budget, quota),
                    "RHI-ORIGINAL-DEVICE-LIFETIME-ALLOCATION");
                is(device->originalDeviceLifetimeTicket().diagnostic(),
                    "RHI-ORIGINAL-DEVICE-LIFETIME-EXPIRED");
            }
            assert(!device->prepareOriginalDeviceLifetime(budget, quota));
            OriginalDeviceNativeLossContractFixture::verifyDeviceGate(*device);
            auto ticket = device->originalDeviceLifetimeTicket();
            auto copy = ticket;
            assert(!ticket.diagnostic() && !device->validateOriginalDeviceLifetime(copy)
                && !ticket.nativeExecutionProven());
            quota->setLimitBytes(budget.maxCpuMetadataBytes + 1u);
            is(ticket.diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-QUOTA");
            is(device->validateOriginalDeviceLifetime(copy), "RHI-ORIGINAL-DEVICE-LIFETIME-QUOTA");
            quota->setLimitBytes(budget.maxCpuMetadataBytes);
            assert(!ticket.diagnostic());
            is(foreign->validateOriginalDeviceLifetime(ticket),
                "RHI-ORIGINAL-DEVICE-LIFETIME-FOREIGN-DEVICE");
            auto foreignQuota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
            assert(!foreign->prepareOriginalDeviceLifetime(budget, foreignQuota));
            is(foreign->validateOriginalDeviceLifetime(copy),
                "RHI-ORIGINAL-DEVICE-LIFETIME-FOREIGN-DEVICE");
            is(device->prepareOriginalDeviceLifetime(budget, foreignQuota),
                "RHI-ORIGINAL-DEVICE-LIFETIME-ALREADY-PREPARED");
            // No allocation, lock wait or IO to issue/copy/validate/retire tickets.
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{quota, foreignQuota}, [&]() noexcept {
                    auto hot = device->originalDeviceLifetimeTicket();
                    auto alias = hot;
                    if (hot.diagnostic() || device->validateOriginalDeviceLifetime(alias))
                        return "TEST-DEVICE-TICKET";
                    device->retireOriginalDeviceLifetime();
                    device->retireOriginalDeviceLifetime();
                    return std::string_view(copy.diagnostic())
                                == "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED"
                            && std::string_view(device->validateOriginalDeviceLifetime(hot))
                                == "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED"
                        ? nullptr
                        : "TEST-DEVICE-RETIREMENT";
                }));
            is(device->prepareOriginalDeviceLifetime(budget, quota),
                "RHI-ORIGINAL-DEVICE-LIFETIME-ALREADY-PREPARED");
            is(device->originalDeviceLifetimeTicket().diagnostic(),
                "RHI-ORIGINAL-DEVICE-LIFETIME-EXPIRED");
            expired = foreign->originalDeviceLifetimeTicket();
            foreign.reset();
            is(expired.diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-EXPIRED");
            // Weak tickets retain only allocator-accounted control blocks, never
            // the device or domain. Drop them before asserting quota retirement.
            assert(foreignQuota->liveBytes());
            expired = {};
            assert(!foreignQuota->liveBytes());
        }
        assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_ORIGINAL_DEVICE_LIFETIME_OK actual-device sealed-weak-epoch exact-foreign-retired-destroyed cold-exact-less-exhausted hot-allocation0 resource-state-native-issuers-closed\n");
    }
};
} // namespace arp::test
