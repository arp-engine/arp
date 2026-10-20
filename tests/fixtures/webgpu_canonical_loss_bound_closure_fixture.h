#pragma once
#include "webgpu_canonical_native_closure_fixture.h"

namespace arp::test {
class WebGpuCanonicalLossBoundClosureFixture final {
    using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    template <class Factory> static void verify(Factory&& create)
    {
        OriginalCommandAccessFixture::withOriginalPlan(create, [&](auto& context, auto& root) {
            const char* diagnostic = nullptr;
            rhi::OriginalDeviceNativeLossObservation unissued;
            const auto budget = WebGpuCanonicalNativeClosureFixture::resourceBudget(context, 43u);
            assert(!Closure::prepareOriginalLossBound(
                root, *context.device, budget, unissued, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-UNISSUED");
            assert(!context.device->observeOriginalDeviceNativeLoss(diagnostic));
            is(diagnostic, "RHI-ORIGINAL-DEVICE-LOSS-UNPREPARED");
            auto subscription = core::makeShared<core::CpuMemoryQuota>(65536u);
            const auto lowering =
                rhi::OriginalDeviceNativeLossContract::lowering(context.device->backend());
            is(context.device->prepareOriginalDeviceNativeLoss(
                   {{1u, true, 1u, 1u, 65536u}, 1u}, subscription),
                lowering.diagnostic ? lowering.diagnostic
                                    : "RHI-ORIGINAL-DEVICE-LOSS-REGISTRY-ALREADY-PREPARED");
            assert(root->journalEmpty() && !root->validate());
            std::puts(
                "BINDLESS_RHI_CANONICAL_LOSS_BOUND_CLOSURE_CONTRACT_OK unissued-unprepared-refuse registry-no-retrofit same-root-no-mutation native-execution-closed");
        });
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        OriginalCommandAccessFixture::withNativeLossOriginalPlan(
            create, [&](auto& context, auto& root) {
                const char* diagnostic = nullptr;
                const auto& observation = *context.nativeLossObservation;
                auto budget = WebGpuCanonicalNativeClosureFixture::resourceBudget(context, 43u);
                const auto make = [&] {
                    return Closure::prepareOriginalLossBound(
                        root, *context.device, budget, observation, diagnostic);
                };
                CpuQuotaFactoryFixture pressure(context.quotas[5]);
                const auto peak = pressure.measure(make);
                pressure.verify(peak, make);
                auto invalid = budget;
                --invalid.maxRetainedOwners;
                assert(!Closure::prepareOriginalLossBound(
                    root, *context.device, invalid, observation, diagnostic));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-CLOSURE-OWNER-CAPACITY");
                auto foreign = create();
                assert(foreign);
                auto domain = core::makeShared<core::CpuMemoryQuota>(65536u),
                     subscription = core::makeShared<core::CpuMemoryQuota>(65536u);
                assert(!foreign->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, 65536u}, domain));
                assert(!foreign->prepareOriginalDeviceNativeLoss(
                    {{1u, true, 1u, 1u, 65536u}, 1u}, subscription));
                auto other = foreign->observeOriginalDeviceNativeLoss(diagnostic);
                assert(other);
                assert(!Closure::prepareOriginalLossBound(
                    root, *context.device, budget, *other, diagnostic));
                is(diagnostic, "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-FOREIGN");
                other.reset();
                foreign.reset();
                assert(!domain->liveBytes() && !subscription->liveBytes());
                auto closure = make();
                assert(closure && closure->retainedOwnerCount() == 43u
                    && !closure->compareOriginalRoot(root));
                assert(closure->originalNativeLossObservation()
                    && !closure->originalNativeLossObservation()->compareOriginal(observation));
                auto unbound = Closure::prepareOriginal(root, *context.device, budget, diagnostic);
                assert(unbound && unbound->retainedOwnerCount() == 42u);
                is(unbound->nativeLossDiagnostic(), "RHI-WEBGPU-CANONICAL-CLOSURE-LOSS-UNBOUND");
                unbound.reset();
                context.nativeLossQuota->setLimitBytes(65535u);
                is(closure->validate(), "RHI-DEVICE-LOSS-REGISTRATION-QUOTA");
                assert(!make());
                is(diagnostic, "RHI-DEVICE-LOSS-REGISTRATION-QUOTA");
                context.nativeLossQuota->setLimitBytes(65536u);
                const std::array quotas{context.quotas[0],
                    context.quotas[1],
                    context.quotas[2],
                    context.quotas[3],
                    context.quotas[4],
                    context.quotas[5],
                    context.nativeLossQuota};
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    if (const auto* id = closure->validate())
                        return id;
                    if (const auto* id = closure->nativeLossDiagnostic())
                        return id;
                    if (const auto* id = context.device->validateOriginalDeviceNativeLoss(
                            *closure->originalNativeLossObservation()))
                        return id;
                    return closure->nativeExecutionProven() || !closure->executionDiagnostic()
                        ? "TEST-LOSS-BOUND-EXECUTION"
                        : nullptr;
                }));
                const auto retainedBytes = closure->retainedResourceBytes();
                std::weak_ptr<rhi::OriginalCommandStateTransaction> retained = root;
                root.reset();
                context.nativeLossObservation.reset();
                assert(!retained.expired() && !closure->validate());
                context.device.reset();
                is(closure->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
                is(closure->nativeLossDiagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
                assert(context.nativeLossQuota->liveBytes());
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    closure.reset();
                    assert(retained.expired());
                    retained.reset();
                    return static_cast<const char*>(nullptr);
                }));
                assert(!context.nativeLossQuota->liveBytes() && !context.quotas[5]->liveBytes());
                std::printf(
                    "BINDLESS_RHI_CANONICAL_LOSS_BOUND_CLOSURE_OK resources5 owners43 bytes%llu cold%llu genuine-loss-before-registry exact-subscription-domain-dedup original-root native-parent-allocation-dedup exact-less-exhausted unbound-foreign-quota-reject caller-drop shutdown-retained hot-allocation0 native-execution-closed\n",
                    static_cast<unsigned long long>(retainedBytes),
                    static_cast<unsigned long long>(peak));
            });
#endif
    }
};
} // namespace arp::test
