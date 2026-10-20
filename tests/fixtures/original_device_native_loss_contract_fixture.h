#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/gpu_device.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalDeviceNativeLossContractFixture final {
public:
    static void verifyDeviceGate(rhi::GpuDevice& device)
    {
        constexpr std::uint64_t bytes = 65536u;
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const auto resultDecision =
            rhi::OriginalDeviceNativeResultLossContract::lowering(device.backend());
        if (!resultDecision.bindingAvailable) {
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            const auto* id =
                device.prepareOriginalDeviceNativeResultLoss({1u, true, 1u, 2u, bytes}, quota);
            assert(id && std::string_view(id) == resultDecision.diagnostic && !quota->liveBytes());
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        }
        const auto decision = rhi::OriginalDeviceNativeLossContract::lowering(device.backend());
        const char* expected = decision.diagnostic;
#if !defined(__EMSCRIPTEN__)
        // This host build has no actual browser-native source. The WebGPU
        // lowering intent cannot turn metadata-only devices into subscriptions.
        if (decision.nativePrototype)
            expected = "RHI-ORIGINAL-DEVICE-LOSS-NATIVE-SOURCE-UNAVAILABLE";
#endif
        if (expected) {
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            const auto* diagnostic =
                device.prepareOriginalDeviceNativeLoss({{1u, true, 1u, 1u, bytes}, 1u}, quota);
            assert(diagnostic && std::string_view(diagnostic) == expected && !quota->liveBytes());
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        }
    }

    static void verify()
    {
        using ResultContract = rhi::OriginalDeviceNativeResultLossContract;
        const rhi::OriginalDeviceNativeResultLossBudget resultBudget{1u, true, 1u, 2u, 65536u};
        assert(!ResultContract::validateIntent(resultBudget, 65536u));
        for (const auto version : {0u, 2u}) {
            auto invalid = resultBudget;
            invalid.contractVersion = version;
            assert(ResultContract::validateIntent(invalid, 65536u));
        }
        auto invalidResult = resultBudget;
        invalidResult.prototypeOptIn = false;
        assert(ResultContract::validateIntent(invalidResult, 65536u));
        for (const auto subscriptions : {0u, 2u}) {
            invalidResult = resultBudget;
            invalidResult.maxSourceSubscriptions = subscriptions;
            assert(ResultContract::validateIntent(invalidResult, 65536u));
        }
        invalidResult = resultBudget;
        --invalidResult.maxRetainedOwners;
        assert(ResultContract::validateIntent(invalidResult, 65536u));
        assert(ResultContract::validateIntent(resultBudget, 65535u));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto result = ResultContract::lowering(backend);
            assert(result.bindingAvailable
                == (backend == rhi::Backend::Vulkan || backend == rhi::Backend::Metal));
            assert(result.validatorOnly == (backend == rhi::Backend::Null));
            assert(
                !result.executionAvailable && bool(result.diagnostic) == !result.bindingAvailable);
        }
        assert(ResultContract::lowering(static_cast<rhi::Backend>(UINT32_MAX)).diagnostic);
        using Contract = rhi::OriginalDeviceNativeLossContract;
        static_assert(
            !std::is_default_constructible_v<rhi::OriginalDeviceLifetime::NativeLossReceiver>);
        static_assert(!std::is_copy_constructible_v<rhi::OriginalDeviceNativeLossBinding>);
        const rhi::OriginalDeviceNativeLossBudget budget{{1u, true, 1u, 1u, 65536u}, 1u};
        assert(!Contract::validateIntent(budget, 65536u));
        const auto reject = [&](auto change, const char* expected) {
            auto b = budget;
            change(b);
            const auto* id = Contract::validateIntent(b, 65536u);
            assert(id && std::string_view(id) == expected);
        };
        reject(
            [](auto& b) {
                b.registration.contractVersion = 0u;
            },
            "RHI-DEVICE-LOSS-REGISTRATION-VERSION");
        reject(
            [](auto& b) {
                b.registration.prototypeOptIn = false;
            },
            "RHI-DEVICE-LOSS-REGISTRATION-OPT-IN");
        reject(
            [](auto& b) {
                b.registration.maxRegistrations = 0u;
            },
            "RHI-DEVICE-LOSS-REGISTRATION-COUNT");
        reject(
            [](auto& b) {
                b.registration.maxCallbacks = 0u;
            },
            "RHI-DEVICE-LOSS-REGISTRATION-COUNT");
        reject(
            [](auto& b) {
                b.registration.maxCpuMetadataBytes = 0u;
            },
            "RHI-DEVICE-LOSS-REGISTRATION-QUOTA");
        reject(
            [](auto& b) {
                --b.registration.maxCpuMetadataBytes;
            },
            "RHI-DEVICE-LOSS-REGISTRATION-QUOTA");
        reject(
            [](auto& b) {
                b.maxRetainedOwners = 0u;
            },
            "RHI-ORIGINAL-DEVICE-LOSS-OWNER-CAPACITY");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = Contract::lowering(backend);
            assert(!decision.executionAvailable);
            assert(decision.nativePrototype == (backend == rhi::Backend::WebGPU));
            assert(decision.validatorOnly == (backend == rhi::Backend::Null));
            assert(bool(decision.diagnostic) == !decision.nativePrototype);
            assert(!rhi::OriginalDeviceLifetimeContract::lowering(backend).nativeLossObserved);
        }
        assert(Contract::lowering(static_cast<rhi::Backend>(UINT32_MAX)).diagnostic);
        std::puts(
            "BINDLESS_RHI_ORIGINAL_DEVICE_LOSS_CONTRACT_OK sealed-receiver domain-subscription-registry-order independent-count-owner-CPU four-explicit-lowerings full-gates-closed");
    }
};
} // namespace arp::test
