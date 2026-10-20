#pragma once
#include "original_command_native_lifecycle_fixture.h"
#include "rhi/frontend/command_recording.h"

namespace arp::test {
class OriginalCommandRecordingSealFixture final {
    using Seal = rhi::OriginalCommandRecordingSeal;
    using Root = rhi::OriginalCommandStateTransaction;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    template <class Factory> static void verify(Factory&& create)
    {
        OriginalCommandAccessFixture::withConfiguredOriginalPlan(create,
            OriginalCommandNativeLifecycleFixture::configureSourceBudget,
            [&](auto& context, auto& root) {
                const auto graph = root->originalGraph();
                const auto& source =
                    graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
                rhi::OriginalCommandNativeLifecycleBudget budget{
                    1u, true, 1u, 1u, 1u, 33u, 1u, 1u, 65536u, 65536u, 65536u};
                auto submission =
                    core::makeShared<core::CpuMemoryQuota>(budget.maxSubmissionCpuBytes);
                auto callback = core::makeShared<core::CpuMemoryQuota>(budget.maxCallbackCpuBytes);
                const char* diagnostic = nullptr;
                const auto rebuild = [&](bool materialize = true) {
                    root.reset();
                    assert(!source->originalStateTransactionCount());
                    root = Root::prepareOriginalOwnership(graph,
                        *context.device,
                        {1u, true, 1u, 5u, 8u, 6u, 30u, 65536u},
                        context.quotas[5],
                        diagnostic);
                    assert(root && !root->hasOriginalRecordingSeal()
                        && source->originalStateTransactionCount() == 1u);
                    if (materialize)
                        assert(
                            !root->materializeOriginalPrototypePlan({1u, true, 10u, 0u, 8u, 10u}));
                };
                const auto make = [&] {
                    return Seal::prepareOriginal(
                        root, *context.device, budget, submission, callback, diagnostic);
                };
                rebuild(false);
                assert(!make());
                is(diagnostic, "RHI-ORIGINAL-NATIVE-LIFECYCLE-PLAN-STATUS");
                assert(!root->hasOriginalRecordingSeal() && root->journalEmpty());
                assert(!root->materializeOriginalPrototypePlan({1u, true, 10u, 0u, 8u, 10u}));
                CpuQuotaFactoryFixture pressure(context.quotas[5]);
                const auto peak = pressure.measure(make);
                assert(root->status() == Root::Status::Abandoned && root->hasOriginalRecordingSeal()
                    && root->journalEmpty());
                pressure.verifySingleUse(peak, rebuild, make);
                assert(root->status() == Root::Status::Prepared && !root->hasOriginalRecordingSeal()
                    && !root->validate());
                auto invalid = budget;
                --invalid.maxRecordingOwners;
                assert(!Seal::prepareOriginal(
                    root, *context.device, invalid, submission, callback, diagnostic));
                is(diagnostic, "RHI-ORIGINAL-RECORDING-SEAL-OWNER-CAPACITY");
                auto forgedRoot = std::shared_ptr<Root>(callback, root.get());
                assert(!Seal::prepareOriginal(
                    forgedRoot, *context.device, budget, submission, callback, diagnostic));
                is(diagnostic, "RHI-ORIGINAL-TRANSACTION-OWNER");
                forgedRoot.reset();
                auto peer = create();
                assert(peer);
                auto peerQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
                assert(!peer->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, 65536u}, peerQuota));
                assert(
                    !Seal::prepareOriginal(root, *peer, budget, submission, callback, diagnostic));
                is(diagnostic, "RHI-ORIGINAL-DEVICE-LIFETIME-FOREIGN-DEVICE");
                peer.reset();
                assert(!Seal::prepareOriginal(
                    root, *context.device, budget, submission, submission, diagnostic));
                is(diagnostic, "RHI-ORIGINAL-NATIVE-LIFECYCLE-QUOTA-DOMAIN");
                auto seal = make();
                assert(seal && !Seal::validateOriginal(seal) && !seal->compareOriginalRoot(root)
                    && seal->retainedOwnerCount() == 33u && root->hasOriginalRecordingSeal());
                assert(!make());
                is(diagnostic, "RHI-ORIGINAL-RECORDING-SEAL-DUPLICATE");
                auto forgedSeal = std::shared_ptr<const Seal>(callback, seal.get());
                is(Seal::validateOriginal(forgedSeal), "RHI-ORIGINAL-RECORDING-SEAL-OWNER");
                forgedSeal.reset();
                forgedRoot = std::shared_ptr<Root>(callback, root.get());
                is(seal->compareOriginalRoot(forgedRoot), "RHI-ORIGINAL-RECORDING-SEAL-ROOT");
                forgedRoot.reset();
                const std::array quotas{context.quotas[0],
                    context.quotas[1],
                    context.quotas[2],
                    context.quotas[3],
                    context.quotas[4],
                    context.quotas[5],
                    submission,
                    callback};
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    is(root->applyOriginalPrototypePlan(),
                        "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
                    is(root->beginOriginalPendingOwnership(),
                        "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
                    is(root->commitOriginalPendingOwnership(),
                        "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
                    is(root->abandonOriginalPendingOwnership(),
                        "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
                    is(root->materializeOriginalPrototypePlan({1u, true, 10u, 0u, 8u, 10u}),
                        "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
                    for (const auto backend : {rhi::Backend::Null,
                             rhi::Backend::Vulkan,
                             rhi::Backend::Metal,
                             rhi::Backend::WebGPU})
                        if (seal->nativeExecutionProven() || !seal->executionDiagnostic(backend))
                            return "TEST-SEAL-EXECUTION";
                    if (!root->journalEmpty() || root->status() != Root::Status::Prepared
                        || source->originalStateTransactionCount() != 1u)
                        return "TEST-SEAL-MUTATION";
                    return seal->validate();
                }));
                callback->setLimitBytes(budget.maxCallbackCpuBytes - 1u);
                is(seal->validate(), "RHI-ORIGINAL-NATIVE-LIFECYCLE-QUOTA");
                callback->setLimitBytes(budget.maxCallbackCpuBytes);
                std::weak_ptr<Root> retained = root;
                root.reset();
                assert(!retained.expired() && !seal->validate());
                context.device.reset();
                is(seal->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    seal.reset();
                    if (!retained.expired() || source->originalStateTransactionCount())
                        return "TEST-SEAL-RELEASE";
                    // The test observer retains its own quota-charged shared control
                    // block after object destruction; release it before the byte check.
                    retained.reset();
                    return static_cast<const char*>(nullptr);
                }));
                assert(!context.quotas[5]->liveBytes() && !submission->liveBytes()
                    && !callback->liveBytes());
                std::printf(
                    "BINDLESS_RHI_CANONICAL_RECORDING_SEAL_OK owner33 cold%llu exact-original-root exclusive-one-shot CPU-mutation-refused exact-less-exhausted duplicate-forged-foreign-quota-reject caller-drop shutdown source-count-released hot-allocation0 no-native-event-authority\n",
                    static_cast<unsigned long long>(peak));
            });
        OriginalCommandAccessFixture::withConfiguredOriginalPlan(create,
            OriginalCommandNativeLifecycleFixture::configureSourceBudget,
            [&](auto& context, auto& root) {
                rhi::OriginalCommandNativeLifecycleBudget budget{
                    1u, true, 1u, 1u, 1u, 33u, 1u, 1u, 65536u, 65536u, 65536u};
                auto submission =
                    core::makeShared<core::CpuMemoryQuota>(budget.maxSubmissionCpuBytes);
                auto callback = core::makeShared<core::CpuMemoryQuota>(budget.maxCallbackCpuBytes);
                const char* diagnostic = nullptr;
                assert(!root->applyOriginalPrototypePlan());
                assert(!Seal::prepareOriginal(
                    root, *context.device, budget, submission, callback, diagnostic));
                is(diagnostic, "RHI-ORIGINAL-NATIVE-LIFECYCLE-PLAN-STATUS");
                assert(!root->hasOriginalRecordingSeal() && !root->journalEmpty()
                    && !root->validate());
                assert(!root->commitOriginalPendingOwnership());
                assert(!Seal::prepareOriginal(
                    root, *context.device, budget, submission, callback, diagnostic));
                is(diagnostic, "RHI-ORIGINAL-NATIVE-LIFECYCLE-PLAN-STATUS");
            });
    }
};
} // namespace arp::test
