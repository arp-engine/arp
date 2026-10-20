#pragma once
#include "texture_readback_cold_budget_fixture.h"
#include "webgpu_async_wait_fixture.h"

namespace arp::test {
class WebGpuOriginalDeviceLossRegistrationFixture final {
public:
    using Owner = rhi_webgpu::WebGpuOrdinaryCompletionOwner;
    using Plan = rhi::TextureReadbackColdBudgetPlan;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void rememberNative(const std::shared_ptr<Owner>& owner, unsigned index)
    {
        assert(owner && owner->originalLoss_ && owner->originalLoss_->nativeState && index < 6u);
        EM_ASM(
            {
                // clang-format off
                    Module.arpOriginalLossStates = Module.arpOriginalLossStates || new Array(6);
                    if (Module.arpOriginalLossStates[$1])
                        throw new Error('Duplicate loss evidence slot');
                    Module.arpOriginalLossStates[$1] = Emval.toValue($0);
                // clang-format on
            },
            owner->originalLoss_->nativeState,
            index);
    }

    static bool armed(const std::shared_ptr<Owner>& owner) noexcept
    {
        return owner && owner->originalLoss_
            && owner->originalLoss_->gate.phase()
            == core::runtime::ExternalCallbackGate::Phase::Armed;
    }

    static void rememberDeviceContext(const std::shared_ptr<Owner>& owner, unsigned index)
    {
        assert(owner && owner->originalLoss_ && owner->originalLoss_->nativeState && index < 3u);
        EM_ASM(
            {
                // clang-format off
                    Module.arpOwnedContextLossStates = Module.arpOwnedContextLossStates || new Array(3);
                    if (Module.arpOwnedContextLossStates[$1])
                        throw new Error("Duplicate device context evidence");
                    Module.arpOwnedContextLossStates[$1] = Emval.toValue($0);
                // clang-format on
            },
            owner->originalLoss_->nativeState,
            index);
    }

    static void verifyIdleCold()
    {
        const char* id = nullptr;
        auto recipe = TextureReadbackColdBudgetFixture::prepare();
        auto plan = recipe.originalPlan;
        auto quota = plan->quotas()[0];
        auto budget = recipe.completion;
        auto loss = recipe.deviceLoss;
        const auto factory = [&] {
            return rhi_webgpu::prepareWebGpuOriginalReadbackCompletionOwner(
                plan, budget, loss, quota, id);
        };
        auto baseline = quota->liveBytes();
        auto owner = factory();
        assert(owner && armed(owner)
            && core::SharedOwnerProjection::sameOwner(owner->originalReadbackPlan(), plan));
        const auto peak = quota->liveBytes() - baseline;
        rememberNative(owner, 0u);
        const auto a = core::defaultCpuAllocator().stats().allocationCount;
        assert(!factory() && std::string_view(id) == "RHI-DEVICE-LOSS-REGISTRATION-CAPACITY");
        auto forgedOwner = std::shared_ptr<Owner>(quota, owner.get());
        assert(std::string_view(Owner::detachOriginalDeviceLoss(forgedOwner))
            == "RHI-DEVICE-LOSS-REGISTRATION-OWNER");
        forgedOwner.reset();
        assert(
            !Owner::detachOriginalDeviceLoss(owner) && !armed(owner) && owner->retained.closed());
        assert(std::string_view(Owner::detachOriginalDeviceLoss(owner))
            == "RHI-DEVICE-LOSS-REGISTRATION-RETIRED");
        assert(!factory() && std::string_view(id) == "RHI-DEVICE-LOSS-REGISTRATION-REUSED-PLAN");
        assert(a == core::defaultCpuAllocator().stats().allocationCount);
        owner.reset();
        assert(quota->liveBytes() == baseline);
        recipe = TextureReadbackColdBudgetFixture::prepare();
        plan = recipe.originalPlan;
        quota = plan->quotas()[0];
        baseline = quota->liveBytes();
        for (const auto available : std::array<std::uint64_t, 3>{peak - 1u, 0u, peak}) {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - baseline - available);
            assert(pressure.charged());
            auto prepared = factory();
            assert(bool(prepared) == (available == peak));
            if (prepared) {
                const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
                assert(!Owner::detachOriginalDeviceLoss(prepared));
                prepared.reset();
                assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
            }
        }
        assert(quota->liveBytes() == baseline);
        recipe = TextureReadbackColdBudgetFixture::prepare();
        plan = recipe.originalPlan;
        quota = plan->quotas()[0];
        baseline = quota->liveBytes();
        const auto rejected = [&](const char* expected) {
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(
                !factory() && std::string_view(id) == expected && quota->liveBytes() == baseline);
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        };
        auto saved = plan;
        plan = std::shared_ptr<const Plan>(quota, plan.get());
        rejected("RHI-READBACK-COLD-BUDGET-OWNER");
        plan = saved;
        saved.reset();
        auto savedQuota = quota;
        quota = core::makeShared<core::CpuMemoryQuota>(quota->limitBytes());
        // The foreign quota has no plan charge; preserve both independent ledgers.
        assert(!factory() && std::string_view(id) == "RHI-DEVICE-LOSS-REGISTRATION-DEVICE-QUOTA"
            && !quota->liveBytes());
        quota = savedQuota;
        savedQuota.reset();
        ++budget.maxPendingRecords;
        rejected("RHI-DEVICE-LOSS-REGISTRATION-PLAN-BUDGET");
        budget = recipe.completion;
        for (auto* count : {&loss.maxRegistrations, &loss.maxCallbacks}) {
            ++*count;
            rejected("RHI-DEVICE-LOSS-REGISTRATION-PLAN-BUDGET");
            --*count;
        }
        loss.prototypeOptIn = false;
        rejected("RHI-DEVICE-LOSS-REGISTRATION-PLAN-BUDGET");
        loss = recipe.deviceLoss;
        loss.contractVersion = 0u;
        rejected("RHI-DEVICE-LOSS-REGISTRATION-PLAN-BUDGET");
        loss = recipe.deviceLoss;
        --loss.maxCpuMetadataBytes;
        rejected("RHI-DEVICE-LOSS-REGISTRATION-PLAN-BUDGET");
        loss = recipe.deviceLoss;
        EM_ASM({
            // clang-format off
                Module.arpOriginalLossBindingFailureReceipt = 0;
                Module.arpOriginalLossThen = Module.preinitializedWebGPUDevice.lost.then;
                Module.preinitializedWebGPUDevice.lost.then = function(callback) {
                    Module.arpOriginalLossThen.call(this, function(info) {
                        ++Module.arpOriginalLossBindingFailureReceipt;
                        callback(info);
                    });
                    throw new Error("Authored native loss binding failure after actual promise registration");
                };
            // clang-format on
        });
        assert(!factory() && std::string_view(id) == "RHI-DEVICE-LOSS-REGISTRATION-NATIVE-BINDING"
            && quota->liveBytes() == baseline);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.lost.then = Module.arpOriginalLossThen;
                delete Module.arpOriginalLossThen;
            // clang-format on
        });
        rejected("RHI-DEVICE-LOSS-REGISTRATION-REUSED-PLAN");
        recipe = TextureReadbackColdBudgetFixture::prepare();
        plan = recipe.originalPlan;
        quota = plan->quotas()[0];
        baseline = quota->liveBytes();
        owner = factory();
        assert(owner);
        rememberNative(owner, 2u);
        dropped_ = owner;
        owner.reset();
        assert(!dropped_.expired() && quota->liveBytes() > baseline);
        droppedQuota_ = quota;
        recipe = {};
        plan.reset();
        quota.reset();
        std::printf(
            "BINDLESS_RHI_DEVICE_LOSS_REGISTRATION_COLD_NATIVE_OK %llu exact-less-exhausted forged-foreign duplicate budget failure-unwind idle-detach allocation0\n",
            static_cast<unsigned long long>(peak));
    }

    static void verifyDelayedLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost());
        assert(WebGpuAsyncWaitFixture::until([] {
            return EM_ASM_INT({
                // clang-format off
                    return Module.arpOriginalLossStates.every(function(s) {
                        return s && s.observed;
                    });
                // clang-format on
            }) != 0;
        }));
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOriginalLossStates.every(function(s, i) {
                    return !s.userdata && !s.callback && s.deliveries === (i < 2 ? 0 : 1) &&
                           s.phase === (i < 2 ? 3 : 2);
                });
            // clang-format on
        }) != 0);
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOriginalLossBindingFailureReceipt;
            // clang-format on
        }) == 1);
        assert(dropped_.expired());
        dropped_.reset();
        assert(!droppedQuota_->liveBytes());
        droppedQuota_.reset();
        EM_ASM({
            // clang-format off
                delete Module.arpOriginalLossStates;
                delete Module.arpOriginalLossBindingFailureReceipt;
            // clang-format on
        });
        std::puts(
            "BINDLESS_RHI_DEVICE_LOSS_REGISTRATION_DELAYED_LOSS_OK detached2 deliveries0 retained4 deliveries1 cleared-userdata native-loss drop-owner Device-quota-zero");
    }

private:
    static inline std::weak_ptr<Owner> dropped_;
    static inline std::shared_ptr<core::CpuMemoryQuota> droppedQuota_;
#endif
};
} // namespace arp::test
