#pragma once
#include "bounded_terminal_observers_fixture.h"
#include "rhi/frontend/resource_device_validation.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalDeviceNativeLossObserverContractFixture final {
    using C = rhi::OriginalDeviceNativeLossObserverContract;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static void verify()
    {
        const rhi::OriginalDeviceNativeLossBudget original{{1u, true, 1u, 1u, 65536u}, 1u};
        const C::Request request{1u, true, 3u, 4u, 65536u};
        C::Requirements required;
        assert(!C::inspect(original, request, 65536u, required));
        assert(required.nativeRegistrations == 1u && required.nativeCallbacks == 1u
            && required.observerSlots == 3u && required.bindingOwnedObjects == 4u
            && required.cpuMetadataBytes == 65536u);
        const auto negative = [&](auto edit, const char* expected) {
            auto wrong = request;
            edit(wrong);
            auto output = required;
            is(C::inspect(original, wrong, 65536u, output), expected);
            assert(!output.nativeRegistrations && !output.nativeCallbacks && !output.observerSlots
                && !output.bindingOwnedObjects && !output.cpuMetadataBytes);
        };
        negative(
            [](auto& r) {
                r.contractVersion = 0u;
            },
            "RHI-ORIGINAL-LOSS-OBSERVER-VERSION");
        negative(
            [](auto& r) {
                r.prototypeOptIn = false;
            },
            "RHI-ORIGINAL-LOSS-OBSERVER-OPT-IN");
        negative(
            [](auto& r) {
                r.maxObserverSlots = 0u;
            },
            "RHI-ORIGINAL-LOSS-OBSERVER-SLOT-CAPACITY");
        negative(
            [](auto& r) {
                r.maxObserverSlots = UINT32_MAX;
            },
            "RHI-ORIGINAL-LOSS-OBSERVER-OWNER-OVERFLOW");
        negative(
            [](auto& r) {
                --r.maxBindingOwnedObjects;
            },
            "RHI-ORIGINAL-LOSS-OBSERVER-OWNER-CAPACITY");
        negative(
            [](auto& r) {
                --r.maxCpuMetadataBytes;
            },
            "RHI-ORIGINAL-LOSS-OBSERVER-QUOTA");
        auto wrong = original;
        ++wrong.registration.maxCallbacks;
        is(C::inspect(wrong, request, 65536u, required),
            "RHI-ORIGINAL-LOSS-OBSERVER-SUBSCRIPTION-COUNT");
        assert(!required.nativeCallbacks && !required.observerSlots);
        wrong = original;
        ++wrong.registration.maxRegistrations;
        is(C::inspect(wrong, request, 65536u, required),
            "RHI-ORIGINAL-LOSS-OBSERVER-SUBSCRIPTION-COUNT");
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            const auto lowering = C::lowering(backend);
            assert(lowering.validatorOnly == (backend == rhi::Backend::Null));
            assert(lowering.bindingAvailable == (backend == rhi::Backend::WebGPU));
            assert(!lowering.observerIssuanceAvailable && !lowering.executionAvailable
                && lowering.diagnostic);
        }
        assert(C::lowering(static_cast<rhi::Backend>(255u)).diagnostic);
        BoundedTerminalObserversFixture::verify();
        std::puts(
            "BINDLESS_RHI_TYPED_LOSS_OBSERVER_CONTRACT_OK explicit1 original-handler1-callback1 bounded-slots3 owned4 output-clear four-issuance-execution-gates-closed core24-terminal-cancel-combinations stale-foreign-address-reuse-generation-exhaustion suppression-failure-retains reentrant-reject exact-less-exhausted hot0 no-GPU-completion");
    }
};
} // namespace arp::test
