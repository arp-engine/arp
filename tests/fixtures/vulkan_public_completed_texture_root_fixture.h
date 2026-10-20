#pragma once
#include "rhi/frontend/bindless_root_fence_execution.h"
#include "vulkan_completed_texture_root_fixture.h"

#include <thread>

namespace arp::test {
// Same genuine uploads, program, journal and numerical oracle as private8.
// Only the public preparation/record/submit interface differs.
class VulkanPublicCompletedTextureRootFixture final : public VulkanCompletedTextureRootFixture {
    static constexpr std::uint64_t cpuBytes = 65536u;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Vulkan public color8: %s\n", id);
        assert(!id);
    }

public:
    // 0 normal, 1 cancel, 2 caller drop, 3 missing queue then retry,
    // 4 unknown queue, 5 unknown fence, 6 exact-runtime idle drain.
    unsigned publicTerminal = 0u;

    void configureRequest(rhi::BindlessRequirements& request) const
    {
        request.originalExecution = {8u, true};
    }

    using VulkanCompletedTextureRootFixture::operator();

    void operator()(const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        rhi::GpuDevice* device) const
    {
        using namespace rhi;
        using namespace rhi_vulkan;
        using Hooks = RootFenceNegativeObservation;
        if (!device) {
            assert(!root && !inventory);
            return;
        }
        auto* extension =
            static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
        assert(extension);
        assert(extension->capabilities().originalNativeColorComputeExecutionContractVersion == 8u);
        {
            const auto prepared = std::get<std::shared_ptr<const BindlessPreparedComputeBinding>>(
                root->originalPrepared());
            auto encoder =
                device->createCommandEncoder({"Public color standalone refusal", 1u, 1u});
            assert(encoder);
            auto pass = encoder->beginComputePass({"Public color standalone refusal"});
            assert(pass);
            const auto denied = pass->setPipeline(*prepared->owners()->desc().pipeline);
            assert(!denied.success
                && denied.diagnostics == "RHI-BINDLESS-ORIGINAL-EXECUTION-PUBLIC-ROOT-REQUIRED");
            assert(pass->end().success);
        }
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 8u> q;
        for (auto& owner : q)
            owner = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto built = core::makeUnique<CompletedTextureRootProgramFixture::Prepared>(
            CompletedTextureRootProgramFixture::prepare(root, inventory, device, q, 1u, cpuBytes));
        OriginalBindlessRootFenceExecutionInputs inputs{
            root, inventory, built->tx, built->binding, q[5], q[6], q[7]};
        const auto entries = static_cast<std::uint32_t>(root->elements().size() + 1u);
        OriginalBindlessRootFenceExecutionBudget budget{{8u, true},
            {1u, true, entries, 512u, 1024u, cpuBytes},
            {8u,
                true,
                1u,
                0u,
                0u,
                built->tx->retainedOwnerCount() + 2u,
                4096u,
                0u,
                cpuBytes,
                cpuBytes,
                0u},
            {4u, true, 1u, 0u, 0u},
            cpuBytes,
            256u};
        budget.requireWorkloadRetention = true;
        core::pmr::String diagnostic;
        diagnostic.reserve(256u);
        auto wrong = budget;
        wrong.intent.contractVersion = 6u;
        assert(!extension->prepareOriginalRootExecution(*device, inputs, wrong, diagnostic));
        wrong = budget;
        --wrong.closure.maxResourceEntries;
        assert(!extension->prepareOriginalRootExecution(*device, inputs, wrong, diagnostic));
        wrong = budget;
        wrong.closure.maxNativeOwners = 1u;
        assert(!extension->prepareOriginalRootExecution(*device, inputs, wrong, diagnostic));
        assert(built->tx->status() == OriginalCommandStateTransaction::Status::Prepared
            && !built->result->publishedBytes());
        for (auto i : {5u, 6u, 7u}) {
            {
                core::CpuMemoryQuotaLease pressure(q[i], cpuBytes);
                assert(pressure.charged());
                assert(
                    !extension->prepareOriginalRootExecution(*device, inputs, budget, diagnostic));
                assert(!built->result->publishedBytes());
                assert(built->tx->status()
                    == (i == 6u ? OriginalCommandStateTransaction::Status::Abandoned
                                : OriginalCommandStateTransaction::Status::Prepared));
            }
            inputs.transaction.reset();
            inputs.readback.reset();
            built.reset();
            built = core::makeUnique<CompletedTextureRootProgramFixture::Prepared>(
                CompletedTextureRootProgramFixture::prepare(
                    root, inventory, device, q, 1u, cpuBytes));
            inputs.transaction = built->tx;
            inputs.readback = built->binding;
        }
        auto execution =
            extension->prepareOriginalRootExecution(*device, inputs, budget, diagnostic);
        if (!execution)
            std::fprintf(stderr, "Vulkan public color prepare: %s\n", diagnostic.c_str());
        assert(execution);
        assert(OriginalBindlessRootExecution::record(execution, {6u, true}));
        require(OriginalBindlessRootExecution::record(execution, {8u, true}));
        assert(execution->recordingProven() && !built->result->publishedBytes());
        assert(OriginalBindlessRootExecution::submit(execution, {6u, true}));
        if (publicTerminal == 1u) {
            require(execution->cancelExecution());
            execution.reset();
            assert(built->tx->status() == OriginalCommandStateTransaction::Status::Abandoned);
        } else {
            if (publicTerminal == 3u) {
                Hooks::omitQueueEntrypoint = true;
                assert(OriginalBindlessRootExecution::submit(execution, {8u, true}));
                Hooks::omitQueueEntrypoint = false;
                assert(built->tx->status() == OriginalCommandStateTransaction::Status::Pending
                    && !built->result->publishedBytes());
            }
            Hooks::withholdCompletion = true;
            Hooks::uncertainQueueResult = publicTerminal == 4u;
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept -> const char* {
                const auto submit = OriginalBindlessRootExecution::submit(execution, {8u, true});
                assert(bool(submit) == (publicTerminal == 4u));
                return nullptr;
            }));
            Hooks::uncertainQueueResult = false;
            assert(execution->pendingRecords() == 1u && !execution->pollCompletion()
                && !built->result->publishedBytes());
            std::weak_ptr<const OriginalBindlessRootExecution> weak = execution;
            if (publicTerminal == 2u) {
                execution.reset();
                assert(weak.expired());
            } else
                assert(execution->cancelExecution());
            if (publicTerminal == 6u) {
                require(
                    CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept -> const char* {
                        device->waitIdle();
                        return nullptr;
                    }));
                Hooks::withholdCompletion = false;
            } else {
                Hooks::withholdCompletion = false;
                Hooks::uncertainFenceResult = publicTerminal == 5u;
                require(
                    CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept -> const char* {
                        if (publicTerminal == 4u)
                            device->waitIdle();
                        const auto deadline =
                            std::chrono::steady_clock::now() + std::chrono::seconds(10);
                        while (root->originalSnapshot()->originalSubmissionBudgetOwner()->used()) {
                            if (execution)
                                execution->pollCompletion();
                            retireCompletedVulkanRhiSubmittedCommands();
                            if (publicTerminal == 5u
                                && built->tx->status()
                                    == OriginalCommandStateTransaction::Status::Invalidated) {
                                Hooks::uncertainFenceResult = false;
                                device->waitIdle();
                            }
                            if (std::chrono::steady_clock::now() >= deadline)
                                return "VULKAN-COLOR-COMPLETION-TIMEOUT";
                            std::this_thread::yield();
                        }
                        return nullptr;
                    }));
                Hooks::uncertainFenceResult = false;
            }
            const bool unpublished = publicTerminal == 4u || publicTerminal == 5u;
            assert(built->tx->status()
                == (unpublished ? OriginalCommandStateTransaction::Status::Invalidated
                                : OriginalCommandStateTransaction::Status::Committed));
            assert(bool(built->result->publishedBytes()) == !unpublished);
            if (!unpublished) {
                std::uint32_t bits = 0u;
                std::memcpy(&bits, built->result->publishedBytes(), 4u);
                assert(bits == std::uint32_t(fault));
                verifyColorOutputs(*root);
                for (const auto& element : root->elements()) {
                    if (element.declaration.write.binding != 4u)
                        continue;
                    const auto& source =
                        std::get<std::shared_ptr<const OriginalCommandBufferLease>>(element.owner);
                    const auto native = vulkanRhiNativePayload(*source->originalObject());
                    assert(native);
                    const auto bytes = arrayTextures ? 64u : 24u;
                    assert(native->backendInvalidateMappedRead(0u, bytes, diagnostic));
                    char name[128u];
                    std::snprintf(name,
                        sizeof(name),
                        "vulkan-public-color8-array%d-fault%d-terminal%u-result%u.bin",
                        arrayTextures,
                        fault,
                        publicTerminal,
                        element.declaration.write.arrayElement);
                    auto* file = std::fopen(name, "wb");
                    assert(file);
                    assert(std::fwrite(native->backendMappedData(), 1u, bytes, file) == bytes);
                    assert(!std::fclose(file));
                }
            }
            execution.reset();
        }
        for (auto i : {5u, 6u, 7u})
            assert(!q[i]->liveBytes());
        assert(!root->originalSnapshot()->originalSubmissionBudgetOwner()->used());
        for (const auto& input : built->completedInputs)
            require(input.first->validateOriginalContent(input.second));
        std::printf(
            "BINDLESS_VULKAN_PUBLIC_COLOR8 array=%d fault=%d terminal=%u same-journal hot-submit0 hot-completion0 retirement0\n",
            arrayTextures,
            fault,
            publicTerminal);
        if (!publicTerminal && !fault && repeatAfterCompletion) {
            inputs = {};
            built.reset();
            auto next = *this;
            next.repeatAfterCompletion = false;
            next(root, inventory, device);
        }
    }
};
} // namespace arp::test
