#pragma once
#include "cpu_quota_factory_fixture.h"
#include "device_idle_failure_fixture.h"
#include "original_bindless_depth_stream_fixture.h"
#include "rhi/backends/vulkan/vulkan_native_resources.h"
#include "rhi/frontend/bindless_root_fence_execution.h"
#include "rhi/gpu_extensions.h"
#include "vulkan_original_root_terminal_fixture.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace arp::test {
class VulkanOriginalDepthExecutionFixture final {
public:
    static void execute(const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        const rhi::OriginalBindlessRootNativeClosureBudget& closureBudget,
        rhi::GpuDevice& device,
        bool fault,
        unsigned terminal = 0u)
    {
        using namespace rhi;
        using Execution = OriginalBindlessRootExecution;
        using Tx = OriginalCommandStateTransaction;
        const auto bytes = inventory->originalMetadataQuota()->limitBytes();
        auto* extension =
            static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
        assert(extension);
        const auto check = [](const char* id) {
            if (id)
                std::fprintf(stderr, "Native depth execution6: %s\n", id);
            assert(!id);
        };
        for (unsigned attempt = 0u; attempt < 2u; ++attempt) {
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u> q;
            for (auto& quota : q)
                quota = core::makeShared<core::CpuMemoryQuota>(bytes);
            OriginalBindlessDepthStreamFixture::execute(
                root,
                inventory,
                device,
                [&](const auto&,
                    const auto&,
                    const auto& binding,
                    const auto& transaction,
                    GpuDevice&) {
                    OriginalBindlessRootFenceExecutionInputs inputs{
                        root, inventory, transaction, binding, q[0], q[1], q[2]};
                    OriginalBindlessRootFenceExecutionBudget budget;
                    budget.intent = {6u, true};
                    budget.requireWorkloadRetention = true;
                    budget.closure = closureBudget;
                    budget.closure.maxCpuMetadataBytes = bytes;
                    budget.lifecycle = {14u,
                        true,
                        1u,
                        0u,
                        0u,
                        transaction->retainedOwnerCount() + 2u,
                        512u,
                        0u,
                        bytes,
                        bytes,
                        0u};
                    budget.capacity = {7u, true, 1u, 0u, 0u};
                    budget.maxCpuViewBytes = bytes;
                    budget.maxDiagnosticBytes = 512u;
                    core::pmr::String diagnostic;
                    const auto refuse = [&](const auto& denied) {
                        assert(!extension->prepareOriginalRootExecution(
                                   device, inputs, denied, diagnostic)
                            && !diagnostic.empty());
                        assert(transaction->status() == Tx::Status::Prepared
                            && transaction->journalEmpty()
                            && !binding->originalResult()->publishedBytes());
                        for (const auto& quota : q)
                            assert(!quota->liveBytes());
                    };
                    auto wrong = budget;
                    wrong.lifecycle.contractVersion = 13u;
                    refuse(wrong);
                    wrong = budget;
                    wrong.capacity.contractVersion = 6u;
                    refuse(wrong);
                    wrong = budget;
                    wrong.lifecycle.maxCallbackTickets = 1u;
                    refuse(wrong);
                    wrong = budget;
                    --wrong.closure.maxNativeOwners;
                    refuse(wrong);
                    {
                        core::CpuMemoryQuotaLease pressure(q[2], bytes);
                        assert(pressure.charged());
                        assert(!extension->prepareOriginalRootExecution(
                            device, inputs, budget, diagnostic));
                        assert(transaction->status() == Tx::Status::Prepared
                            && transaction->journalEmpty());
                    }
                    auto issuer =
                        extension->prepareOriginalRootExecution(device, inputs, budget, diagnostic);
                    if (!issuer)
                        std::fprintf(
                            stderr, "Native depth public prepare6: %s\n", diagnostic.c_str());
                    assert(issuer);
                    assert(!issuer->recordingProven() && !issuer->submissionCompleted()
                        && !issuer->pendingRecords());
                    assert(Execution::record(issuer, {5u, true}));
                    check(Execution::record(issuer, {6u, true}));
                    assert(issuer->recordingProven() && transaction->status() == Tx::Status::Pending
                        && !binding->originalResult()->publishedBytes());
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        std::array{q[0], q[1], q[2], transaction->originalMetadataQuota()},
                        [&]() noexcept {
                            return transaction->validate();
                        }));
                    if (!attempt) {
                        check(issuer->cancelExecution());
                        assert(transaction->status() == Tx::Status::Abandoned
                            && !binding->originalResult()->publishedBytes());
                    } else if (terminal) {
                        using Hooks = RootFenceNegativeObservation;
                        using namespace rhi_vulkan;
                        assert(terminal <= 4u && !fault);
                        const bool invalidated = terminal != 1u;
                        const auto& result = binding->originalResult();
                        const auto retained = [&] {
                            assert(!result->publishedBytes() && !result->nativeExecutionProven()
                                && q[0]->liveBytes() && q[1]->liveBytes()
                                && root->originalSnapshot()->originalSubmissionBudgetOwner()->used()
                                    == 1u);
                        };
                        Hooks::omitQueueEntrypoint = true;
                        assert(Execution::submit(issuer, {6u, true}));
                        Hooks::omitQueueEntrypoint = false;
                        assert(transaction->status() == Tx::Status::Pending);
                        retained();
                        Hooks::withholdCompletion = true;
                        Hooks::uncertainQueueResult = terminal == 2u || terminal == 3u;
                        Hooks::rejectQueueInvocation = terminal == 3u;
                        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                            std::array{q[1], q[2], transaction->originalMetadataQuota()},
                            [&]() noexcept -> const char* {
                                const auto* id = Execution::submit(issuer, {6u, true});
                                assert(bool(id) == (terminal == 2u || terminal == 3u));
                                return nullptr;
                            }));
                        Hooks::uncertainQueueResult = Hooks::rejectQueueInvocation = false;
                        assert(!issuer->pollCompletion());
                        retained();
                        if (terminal == 4u) {
                            Hooks::withholdCompletion = false;
                            Hooks::uncertainFenceResult = true;
                            const auto deadline =
                                std::chrono::steady_clock::now() + std::chrono::seconds(5);
                            while (transaction->status() != Tx::Status::Invalidated
                                && std::chrono::steady_clock::now() < deadline)
                                assert(!issuer->pollCompletion());
                            Hooks::uncertainFenceResult = false;
                            Hooks::withholdCompletion = true;
                        }
                        assert(transaction->status()
                            == (invalidated ? Tx::Status::Invalidated : Tx::Status::Pending));
                        std::weak_ptr<const Execution> weak = issuer;
                        issuer.reset();
                        assert(weak.expired());
                        weak.reset();
                        assert(!q[2]->liveBytes());
                        retained();
                        assert(!drainVulkanRhiSubmittedCommandsUntil(device,
                            std::chrono::steady_clock::now() + std::chrono::milliseconds(1),
                            diagnostic));
                        drainVulkanRhiSubmittedCommandsForShutdown(device);
                        retireCompletedVulkanRhiSubmittedCommands(device);
                        retained();
                        tests::VulkanHeadlessRuntime foreign(true, Hooks::resolve);
                        assert(foreign.runtime()
                            && !waitIdleAndRetireVulkanRhiSubmittedCommands(foreign.runtime()));
                        retained();
                        Hooks::omitIdleEntrypoint = true;
                        tests::DeviceIdleFailureFixture::verify(
                            device, "RHI-VULKAN-IDLE-COMPLETION-ENTRYPOINT");
                        Hooks::omitIdleEntrypoint = false;
                        retained();
                        Hooks::uncertainIdleResult = true;
                        tests::DeviceIdleFailureFixture::verify(
                            device, "RHI-VULKAN-IDLE-COMPLETION-FAILED");
                        Hooks::uncertainIdleResult = false;
                        retained();
                        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                            std::array{q[0], q[1], transaction->originalMetadataQuota()},
                            [&]() noexcept -> const char* {
                                device.waitIdle();
                                return nullptr;
                            }));
                        Hooks::withholdCompletion = false;
                        assert(!q[0]->liveBytes() && !q[1]->liveBytes()
                            && !root->originalSnapshot()->originalSubmissionBudgetOwner()->used());
                        assert(transaction->status()
                                == (invalidated ? Tx::Status::Invalidated : Tx::Status::Committed)
                            && bool(result->publishedBytes()) == !invalidated
                            && result->nativeExecutionProven() == !invalidated);
                        if (!invalidated) {
                            std::uint32_t bits = ~0u;
                            std::memcpy(&bits, result->publishedBytes(), 4u);
                            assert(!bits);
                        }
                        retireCompletedVulkanRhiSubmittedCommands(device);
                    } else {
                        assert(Execution::submit(issuer, {5u, true}));
                        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                            std::array{q[1], q[2], transaction->originalMetadataQuota()},
                            [&]() noexcept {
                                return Execution::submit(issuer, {6u, true});
                            }));
                        assert(issuer->cancelExecution());
                        const auto deadline =
                            std::chrono::steady_clock::now() + std::chrono::seconds(5);
                        while (!issuer->pollCompletion()
                            && std::chrono::steady_clock::now() < deadline)
                            std::this_thread::yield();
                        check(issuer->executionDiagnostic());
                        assert(issuer->submissionCompleted() && !issuer->pendingRecords());
                        assert(transaction->status() == Tx::Status::Committed
                            && binding->originalResult()->publishedBytes());
                        std::uint32_t bits = ~0u;
                        std::memcpy(&bits, binding->originalResult()->publishedBytes(), 4u);
                        assert(bits == (fault ? 1u : 0u));
                        if (!fault) {
                            std::size_t results = 0u;
                            for (const auto& element : root->elements()) {
                                const auto& write = element.declaration.write;
                                if (write.type != DescriptorType::StorageBuffer
                                    || write.resource
                                        == root->originalSnapshot()->faultLatchBuffer()->id())
                                    continue;
                                const auto& lease =
                                    std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                                        inventory->sources()[element.declaration.sourceIndex]);
                                const auto native =
                                    rhi_vulkan::vulkanRhiNativePayload(*lease->originalObject());
                                assert(native);
                                assert(native->backendInvalidateMappedRead(0u, 16u, diagnostic));
                                std::array<std::uint32_t, 4u> words{};
                                std::memcpy(words.data(), native->backendMappedData(), 16u);
                                float loaded = 0.0f, filtered = 0.0f;
                                std::memcpy(&loaded, &words[0], 4u);
                                std::memcpy(&filtered, &words[1], 4u);
                                assert(std::abs(loaded - .01125f) < 1e-7f
                                    && std::abs(filtered - .01375f) < 1e-7f && words[2] == 2u
                                    && words[3] == 2u);
                                ++results;
                            }
                            assert(results == 2u);
                        }
                    }
                },
                12u,
                1u);
            for (const auto& quota : q)
                assert(!quota->liveBytes());
        }
        if (terminal)
            std::printf(
                "BINDLESS_VULKAN_PUBLIC_DEPTH_EXECUTION6_TERMINAL %u caller-drop original-pool missing-queue withheld-fence shutdown foreign-idle missing-idle failed-idle genuine-idle retirement unpublished-on-uncertainty hot0\n",
                terminal);
        else
            std::puts(fault
                    ? "BINDLESS_VULKAN_PUBLIC_DEPTH_EXECUTION6 fault1 native public-root original-journal real-fence cancellation retirement"
                    : "BINDLESS_VULKAN_PUBLIC_DEPTH_EXECUTION6 clean native public-root nonuniform2 load sampleLevel query original-journal real-fence cancellation numerical retirement");
    }
};
} // namespace arp::test
