#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/webgpu/webgpu_original_observed_native_loss.h"
#include "rhi/gpu_device.h"
#include "webgpu_foreign_device_fixture.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
// Only inspects original device-issued bindings and native subscription states.
// Never attaches a test callback or issues typed custody/completion authority.
class OriginalObservedNativeLossBindingFixture final {
    using Binding = rhi_webgpu::WebGpuOriginalObservedNativeLoss;
    using Terminal = rhi::OriginalDeviceNativeLossTerminal;
    static constexpr std::uint64_t bytes = 65536u;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

    static rhi::OriginalDeviceNativeLossBudget original()
    {
        return {{1u, true, 1u, 1u, bytes}, 1u};
    }

    static rhi::OriginalDeviceNativeLossObserverRequest request()
    {
        return {1u, true, 3u, 4u, bytes};
    }

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static void verifyContract()
    {
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            const auto lowering = rhi::OriginalDeviceNativeLossObserverContract::lowering(backend);
            assert(lowering.bindingAvailable == (backend == rhi::Backend::WebGPU));
            assert(lowering.typedColdObserverPreparationAvailable
                == (backend == rhi::Backend::WebGPU));
            assert(!lowering.observerIssuanceAvailable && !lowering.executionAvailable
                && lowering.diagnostic);
        }
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
#if !defined(__EMSCRIPTEN__)
        auto device = rhi::createNullDevice();
        auto metadata = quota();
        is(device->prepareOriginalDeviceNativeLoss(original(), request(), metadata),
            "RHI-ORIGINAL-LOSS-OBSERVER-NULL-VALIDATOR-ONLY");
        assert(!metadata->liveBytes());
#endif
        std::puts(
            "BINDLESS_RHI_TYPED_LOSS_OBSERVER_BINDING_CONTRACT_OK explicit-profile-before-registry one-native-handler stable-v1-layout Null-Vulkan-Metal-refuse WebGPU-binding-only issuance-execution-closed");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    template <class Factory> static void prepare(Factory&& create)
    {
        auto domain = quota(), metadata = quota();
        const auto factory = [&] {
            auto device = create();
            assert(device);
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            if (const auto* id =
                    device->prepareOriginalDeviceNativeLoss(original(), request(), metadata)) {
                is(id, "RHI-ORIGINAL-LOSS-OBSERVER-ALLOCATION");
                return core::UniquePtr<rhi::GpuDevice>{};
            }
            return device;
        };
        CpuQuotaFactoryFixture pressure(metadata);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease full(metadata, bytes);
            assert(full.charged() && !factory());
        }
        assert(!domain->liveBytes() && !metadata->liveBytes());
        {
            auto device = create();
            assert(device);
            is(device->prepareOriginalDeviceNativeLoss(original(), request(), metadata),
                "RHI-ORIGINAL-DEVICE-LOSS-DOMAIN-UNPREPARED");
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            is(device->prepareOriginalDeviceNativeLoss(original(), request(), domain),
                "RHI-ORIGINAL-DEVICE-LOSS-QUOTA-DOMAIN");
            // The actual promise registers, then throws. Runtime suppresses the
            // captured userdata before the failed binding's C++ owners release.
            captureNext(0u, true);
            is(device->prepareOriginalDeviceNativeLoss(original(), request(), metadata),
                "RHI-ORIGINAL-DEVICE-LOSS-NATIVE-BINDING");
            restoreCapture();
            assert(!metadata->liveBytes());
            captureNext(1u);
            assert(!device->prepareOriginalDeviceNativeLoss(original(), request(), metadata));
            restoreCapture();
            auto observation = observe(*device);
            const auto binding = inspect(observation);
            assert(binding);
            assert(binding->observers_.capacity() == 3u && binding->observers_.accepting()
                && !binding->observers_.pending());
            assert(!observation.terminalDiagnostic());
            remember(binding, 0u);
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{domain, metadata}, [&]() noexcept -> const char* {
                    auto copy = device->observeOriginalDeviceNativeLoss(diagnostic_);
                    assert(copy && !diagnostic_);
                    assert(!device->validateOriginalDeviceNativeLoss(*copy)
                        && !copy->compareOriginal(observation));
                    is(device->prepareOriginalDeviceNativeLoss(original(), request(), metadata),
                        "RHI-ORIGINAL-DEVICE-LOSS-ALREADY-PREPARED");
                    assert(!device->detachOriginalDeviceNativeLoss());
                    assert(binding->observers_.terminal() == Terminal::Retired
                        && !binding->observers_.accepting());
                    is(observation.diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
                    return nullptr;
                }));
            device.reset();
            assert(!observation.terminalDiagnostic());
        }
        assert(!domain->liveBytes() && !metadata->liveBytes());
        {
            auto device = create();
            assert(device
                && !device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            auto registry = quota();
            assert(!device->prepareOriginalResourceRegistration(
                {1u, true, 1u, 0u, 0u, 5u, bytes}, registry));
            is(device->prepareOriginalDeviceNativeLoss(original(), request(), metadata),
                "RHI-ORIGINAL-DEVICE-LOSS-REGISTRY-ALREADY-PREPARED");
        }
        // Facade's source was captured at construction. Global substitution
        // during binding must still attach to the exact original native device.
        {
            WebGpuForeignDeviceFixture foreign;
            auto device = create();
            assert(device
                && !device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            captureNext(2u);
            {
                WebGpuForeignDeviceFixture::Scope scope(foreign);
                assert(!device->prepareOriginalDeviceNativeLoss(original(), request(), metadata));
            }
            restoreCapture();
            auto observation = observe(*device);
            auto binding = inspect(observation);
            remember(binding, 1u);
            auto peer = create();
            assert(peer);
            is(peer->validateOriginalDeviceNativeLoss(observation),
                "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-FOREIGN");
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{domain, metadata}, [&]() noexcept -> const char* {
                    device.reset();
                    assert(binding->observers_.terminal() == Terminal::Shutdown);
                    is(observation.diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
                    assert(!observation.terminalDiagnostic());
                    return nullptr;
                }));
        }
        assert(!domain->liveBytes() && !metadata->liveBytes());
        // Retired closes local receivers but keeps the real native borrow until
        // physical loss/shutdown. It never changes into NativeLost completion.
        retired_.emplace(prepareLive(create, 3u, 2u));
        retired_->device->retireOriginalDeviceLifetime();
        assert(inspect(*retired_->observation)->observers_.terminal() == Terminal::Retired);
        is(retired_->observation->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
        live_.emplace(prepareLive(create, 4u, 3u));
        dropped_.emplace(prepareLive(create, 5u, 4u));
        auto binding = inspect(*dropped_->observation);
        droppedBinding_ = binding;
        const auto state = binding->nativeState_;
        assert(state);
        // Deterministic negative: corrupt only this genuine foreign adapter's
        // phase to reject suppression. Preserve native userdata and callback.
        EM_ASM(
            {
                // clang-format off
                    const s = Emval.toValue($0);
                    if (s.phase !== 1 || !s.userdata || !s.callback)
                        throw new Error('Unarmed observed loss');
                    s.phase = 4;
                // clang-format on
            },
            state);
        dropped_->device.reset();
        is(dropped_->observation->terminalDiagnostic(), "RHI-ORIGINAL-DEVICE-LOSS-NATIVE-DETACH");
        is(dropped_->observation->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
        assert(binding->observers_.terminal() == Terminal::Shutdown && binding->nativeCallbackLease_
            && binding->nativeState_ == state);
        dropped_->observation.reset();
        binding.reset();
        assert(!droppedBinding_.expired());
        EM_ASM(
            {
                // clang-format off
                    const s = Emval.toValue($0);
                    if (s.phase !== 4 || !s.userdata || !s.callback)
                        throw new Error('Lost suppression-failure owner');
                    s.phase = 1;
                // clang-format on
            },
            state);
        std::printf(
            "BINDLESS_RHI_TYPED_LOSS_OBSERVER_BINDING_NATIVE_OK %llu exact-less-exhausted publication-failure-retry shared-handler1 slots3 issuance0 exact-device-domain foreign-reject retire-shutdown-distinct suppression-failure-retains hot0\n",
            static_cast<unsigned long long>(peak));
    }

    // Reusable hostile native suppression fixture, using only an authentic
    // original device-issued observation. No callback is delivered by this test.
    template <class Drain>
    static void verifyDeferredSuppressionFailure(
        const rhi::OriginalDeviceNativeLossObservation& observation, Drain&& drain)
    {
        const auto binding = inspect(observation);
        const auto state = binding->nativeState_;
        assert(state && binding->nativeCallbackLease_);
        EM_ASM(
            {
                // clang-format off
                    const s = Emval.toValue($0);
                    if (s.phase !== 1 || !s.userdata || !s.callback)
                        throw new Error('Unarmed deferred suppression');
                    s.phase = 4;
                // clang-format on
            },
            state);
        drain();
        is(observation.terminalDiagnostic(), "RHI-ORIGINAL-DEVICE-LOSS-NATIVE-DETACH");
        assert(binding->nativeState_ == state && binding->nativeCallbackLease_
            && !binding->observers_.pending() && !binding->observers_.delivered());
        EM_ASM(
            {
                // clang-format off
                    const s = Emval.toValue($0);
                    if (s.phase !== 4 || !s.userdata || !s.callback)
                        throw new Error('Suppression-failure lost native owner');
                    s.phase = 1;
                // clang-format on
            },
            state);
    }

    static void verifyActualLoss()
    {
        assert(live_ && retired_ && dropped_ && rhi_webgpu::webGpuNativeDeviceLost());
        assert(WebGpuAsyncWaitFixture::until([] {
            return EM_ASM_INT({
                // clang-format off
                    return Module.arpObservedBindingStates.every(s => s.observed);
                // clang-format on
            }) != 0
                && droppedBinding_.expired();
        }));
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpObservedBindingReceipts.length === 6 &&
                       Module.arpObservedBindingReceipts.every(r =>
                                                                   r.registrations === 1 && r.deliveries === 1);
            // clang-format on
        }) == 1);
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpObservedBindingStates.length === 5 &&
                       Module.arpObservedBindingStates.every((s, i) => !s.userdata && !s.callback &&
                                                                       s.deliveries === (i < 2 ? 0 : 1) &&
                                                                       s.phase === (i < 2 ? 3 : 2));
            // clang-format on
        }) == 1);
        auto live = inspect(*live_->observation), retired = inspect(*retired_->observation);
        assert(live->observers_.terminal() == Terminal::NativeLost
            && retired->observers_.terminal() == Terminal::Retired);
        assert(!live->observers_.pending() && !retired->observers_.pending());
        is(live_->observation->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
        is(retired_->observation->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
        assert(!live_->observation->terminalDiagnostic()
            && !retired_->observation->terminalDiagnostic());
        live.reset();
        retired.reset();
        droppedBinding_.reset();
        release(live_);
        release(retired_);
        release(dropped_);
        EM_ASM({
            // clang-format off
                delete Module.arpObservedBindingStates;
                delete Module.arpObservedBindingReceipts;
            // clang-format on
        });
        std::puts(
            "BINDLESS_RHI_TYPED_LOSS_OBSERVER_BINDING_LOSS_OK actual-native-device-loss one-handler-per-profile delivered3 suppressed2 cleared-userdata drop-owner-reclaimed quotas-zero no-typed-observer-issued no-completion");
    }

private:
    struct Live {
        std::shared_ptr<core::CpuMemoryQuota> domain, metadata;
        core::UniquePtr<rhi::GpuDevice> device;
        std::optional<rhi::OriginalDeviceNativeLossObservation> observation;
    };

    static inline std::optional<Live> live_, retired_, dropped_;
    static inline std::weak_ptr<const Binding> droppedBinding_;
    static inline const char* diagnostic_ = nullptr;

    static rhi::OriginalDeviceNativeLossObservation observe(rhi::GpuDevice& device)
    {
        auto observed = device.observeOriginalDeviceNativeLoss(diagnostic_);
        assert(observed && !diagnostic_);
        return *observed;
    }

    static std::shared_ptr<const Binding> inspect(
        const rhi::OriginalDeviceNativeLossObservation& observation)
    {
        auto binding = std::dynamic_pointer_cast<const Binding>(observation.binding_);
        assert(binding && binding->hasQuotaBoundSharedOwner(binding));
        return binding;
    }

    static void captureNext(unsigned index, bool fail = false)
    {
        EM_ASM(
            {
                // clang-format off
                    Module.arpObservedBindingReceipts = Module.arpObservedBindingReceipts || new Array(6);
                    if (Module.arpObservedBindingReceipts[$0])
                        throw new Error('Duplicate observed profile');
                    const r = Module.arpObservedBindingReceipts[$0] = ({registrations : 0, deliveries : 0});
                    const p = Module.preinitializedWebGPUDevice.lost;
                    const original = p.then;
                    Module.arpObservedBindingThen = original;
                    const fail = $1;
                    p.then = function(callback) {
                        ++r.registrations;
                        const result = original.call(this, info => {
                            ++r.deliveries;
                            callback(info);
                        });
                        if (fail)
                            throw new Error('Authored observed native binding failure after promise publication');
                        return result;
                    };
                // clang-format on
            },
            index,
            fail ? 1 : 0);
    }

    static void restoreCapture()
    {
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.lost.then = Module.arpObservedBindingThen;
                delete Module.arpObservedBindingThen;
            // clang-format on
        });
    }

    static void remember(const std::shared_ptr<const Binding>& binding, unsigned index)
    {
        assert(binding->nativeState_);
        EM_ASM(
            {
                // clang-format off
                    Module.arpObservedBindingStates = Module.arpObservedBindingStates || new Array(5);
                    if (Module.arpObservedBindingStates[$1])
                        throw new Error('Duplicate observed native state');
                    Module.arpObservedBindingStates[$1] = Emval.toValue($0);
                // clang-format on
            },
            binding->nativeState_,
            index);
    }

    template <class Factory>
    static Live prepareLive(Factory&& create, unsigned receipt, unsigned state)
    {
        Live value;
        value.domain = quota();
        value.metadata = quota();
        value.device = create();
        assert(value.device);
        assert(
            !value.device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, value.domain));
        captureNext(receipt);
        assert(
            !value.device->prepareOriginalDeviceNativeLoss(original(), request(), value.metadata));
        restoreCapture();
        value.observation.emplace(observe(*value.device));
        remember(inspect(*value.observation), state);
        return value;
    }

    static void release(std::optional<Live>& value)
    {
        assert(value);
        auto domain = value->domain, metadata = value->metadata;
        value.reset();
        assert(!domain->liveBytes() && !metadata->liveBytes());
    }
#endif
};
} // namespace arp::test
