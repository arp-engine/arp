#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/gpu_device.h"

#include <string_view>
#include <type_traits>

namespace arp::test {
class OriginalDeviceNativeLossObservationFixture final {
    using Observation = rhi::OriginalDeviceNativeLossObservation;
    static constexpr std::uint64_t bytes = 65536u;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

public:
    template <class Factory> static void verifyMissing(Factory&& create)
    {
        static_assert(!std::is_constructible_v<Observation,
            std::shared_ptr<rhi::OriginalDeviceLifetime>,
            rhi::OriginalDeviceLifetime::Ticket,
            std::shared_ptr<const rhi::OriginalDeviceNativeLossBinding>>);
        static_assert(
            std::is_copy_constructible_v<Observation> && !std::is_copy_assignable_v<Observation>);
        Observation unissued;
        is(unissued.diagnostic(), "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-UNISSUED");
        assert(!unissued.nativeExecutionProven());
        auto device = create();
        assert(device);
        const char* diagnostic = nullptr;
        assert(!device->observeOriginalDeviceNativeLoss(diagnostic));
        is(diagnostic, "RHI-ORIGINAL-DEVICE-LOSS-DOMAIN-UNPREPARED");
        is(device->validateOriginalDeviceNativeLoss(unissued),
            "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-UNISSUED");
        auto domain = quota();
        assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{domain}, [&]() noexcept {
            if (device->observeOriginalDeviceNativeLoss(diagnostic))
                return "TEST-LOSS-MISSING-SUBSCRIPTION";
            is(diagnostic, "RHI-ORIGINAL-DEVICE-LOSS-UNPREPARED");
            return static_cast<const char*>(nullptr);
        }));
        device.reset();
        assert(!domain->liveBytes());
        std::puts(
            "BINDLESS_RHI_ORIGINAL_NATIVE_LOSS_OBSERVATION_CONTRACT_OK device-issued-only opaque-domain-subscription unissued-unprepared-refuse no-native-proof allocation0");
    }

    template <class Factory> static void verifyNative(Factory&& create)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        auto domain = quota(), subscription = quota(), foreignDomain = quota(),
             foreignSubscription = quota();
        const auto prepare = [&](const auto& d, const auto& s) {
            auto device = create();
            assert(device && !device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, d));
            assert(!device->prepareOriginalDeviceNativeLoss({{1u, true, 1u, 1u, bytes}, 1u}, s));
            return device;
        };
        auto device = prepare(domain, subscription),
             foreign = prepare(foreignDomain, foreignSubscription);
        const char* diagnostic = nullptr;
        auto original = device->observeOriginalDeviceNativeLoss(diagnostic),
             other = foreign->observeOriginalDeviceNativeLoss(diagnostic);
        assert(original && other && !original->diagnostic()
            && !device->validateOriginalDeviceNativeLoss(*original));
        is(original->compareOriginal(*other), "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-FOREIGN");
        is(device->validateOriginalDeviceNativeLoss(*other),
            "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-FOREIGN");
        is(foreign->validateOriginalDeviceNativeLoss(*original),
            "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-FOREIGN");
        std::optional<Observation> copy;
        copy.emplace(*original);
        assert(!copy->compareOriginal(*original));
        subscription->setLimitBytes(bytes - 1u);
        is(copy->diagnostic(), "RHI-DEVICE-LOSS-REGISTRATION-QUOTA");
        subscription->setLimitBytes(bytes);
        domain->setLimitBytes(bytes - 1u);
        is(copy->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-QUOTA");
        domain->setLimitBytes(bytes);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{domain, subscription, foreignDomain, foreignSubscription}, [&]() noexcept {
                auto hot = device->observeOriginalDeviceNativeLoss(diagnostic);
                if (!hot || diagnostic || hot->nativeExecutionProven())
                    return "TEST-LOSS-OBSERVATION";
                if (const auto* id = hot->compareOriginal(*copy))
                    return id;
                if (const auto* id = device->validateOriginalDeviceNativeLoss(*hot))
                    return id;
                return device->detachOriginalDeviceNativeLoss();
            }));
        is(original->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
        assert(!device->observeOriginalDeviceNativeLoss(diagnostic));
        is(diagnostic, "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
        device.reset();
        is(copy->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
        foreign.reset();
        is(other->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
        assert(domain->liveBytes() && subscription->liveBytes() && foreignDomain->liveBytes()
            && foreignSubscription->liveBytes());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{domain, subscription, foreignDomain, foreignSubscription}, [&]() noexcept {
                original.reset();
                copy.reset();
                other.reset();
                return static_cast<const char*>(nullptr);
            }));
        assert(!domain->liveBytes() && !subscription->liveBytes() && !foreignDomain->liveBytes()
            && !foreignSubscription->liveBytes());
        std::puts(
            "BINDLESS_RHI_ORIGINAL_NATIVE_LOSS_OBSERVATION_OK genuine-armed original-domain-and-subscription exact-copy foreign-facade-refused quota-mutation-refused retired-shutdown-distinct caller-drop terminal-custody hot-allocation0 no-native-completion");
#else
        (void)create;
#endif
    }
};
} // namespace arp::test
