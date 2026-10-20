#pragma once
#include "metal_original_bindless_root_fixture.h"
#include "rhi/backends/metal/metal_native_sync.h"
#include "rhi/backends/metal/metal_original_bindless_command.h"

#include <chrono>
#include <cstring>
#include <thread>

namespace arp::test {
struct MetalOriginalBindlessExecutionFixture {
    static constexpr bool enabled = true;
    bool submit = true, dropCommand = false;
    std::uint32_t expectedFault = 0u;
    bool retireDevice = false;
    std::array<std::uint32_t, 2> expectedCounters{14u, 16u};
    bool publicExecution = false;

    void configureRequest(rhi::BindlessRequirements& request) const
    {
        if (publicExecution)
            request.originalExecution = {7u, true};
    }

    bool originalDeviceLifetimeRetired() const noexcept { return retireDevice; }

    mutable std::array<std::shared_ptr<rhi_metal::MetalRhiNativeBufferPayload>, 2> counters;

    template <class Table> void configureBuffer(rhi::BufferDesc& desc, const Table& table) const
    {
        desc.memory = rhi::MemoryUsage::CpuToGpu;
        desc.sizeBytes = table.bufferLayout.minBindingSizeBytes;
    }

    template <class Table>
    void initializeBuffer(rhi::GpuBuffer& buffer, const Table& table, std::uint32_t index) const
    {
        const auto native = rhi_metal::metalRhiNativePayloadFor(buffer);
        assert(native);
        core::pmr::Vector<std::uint8_t> bytes(
            buffer.desc().sizeBytes, 0u, &core::defaultCpuMemoryResource());
        if (table.tableId == 1u) {
            const float value = float(index + 1u);
            std::memcpy(bytes.data(), &value, sizeof(value));
        } else if (table.tableId == 2u) {
            const std::uint32_t value = 10u + index;
            std::memcpy(bytes.data(), &value, sizeof(value));
        } else
            counters[index] = native;
        core::pmr::String diagnostic;
        assert(rhi_metal::writeMetalRhiNativeBuffer(
            native, bytes.data(), bytes.size(), 0u, buffer.desc().sizeBytes, diagnostic));
    }

    template <class Result, class Request>
    void verifyRetiredResult(const Result& result, const Request& request) const
    {
        assert(core::SharedOwnerProjection::sameOwner(result->originalRequest(), request));
        assert(result->nativeExecutionProven() == (submit && !retireDevice));
        if (submit && !retireDevice) {
            std::uint32_t bits = 0u;
            std::memcpy(&bits, result->publishedBytes(), sizeof(bits));
            assert(bits == expectedFault);
        } else
            assert(!rhi::OriginalCommandBufferReadbackResultSnapshot::validateOriginalCold(
                result, request));
    }

    template <class Root, class Inventory, class Binding, class Transaction>
    void operator()(const Root& root,
        const Inventory& inventory,
        const Binding& binding,
        const Transaction& tx,
        rhi::GpuDevice& device) const
    {
        using namespace rhi;
        using namespace rhi_metal;
        constexpr std::uint64_t cpuBytes = 65536u;
        auto closureQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto submissionQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* id = nullptr;
        auto entries = static_cast<std::uint32_t>(root->elements().size() + 1u);
        for (const auto& domain : root->ordinaryDomains())
            entries += domain.source->frozenElements().size();
        const OriginalBindlessRootNativeClosureBudget closureBudget{
            1u, true, entries, 512u, 1024u, cpuBytes};
        auto closure = MetalRhiBindlessRootNativeClosure::prepareOriginal(
            root, inventory, device, closureBudget, closureQuota, id);
        assert(closure && !id);
        const OriginalCommandNativeLifecycleBudget budget{
            6u, true, 1u, 0u, 0u, tx->retainedOwnerCount() + 2u, 4096u, 0u, cpuBytes, cpuBytes, 0u};
        auto seal = OriginalCommandRecordingSeal::prepareOriginal(
            tx, device, budget, submissionQuota, {}, id);
        assert(seal && !id);
        auto reservation =
            OriginalCommandLifecycleReservation::prepareOriginal(seal, {2u, true, 1u, 0u, 0u}, id);
        assert(reservation && !id);
        core::pmr::String diagnostic;
        diagnostic.reserve(256u);
        auto forged = std::shared_ptr<const OriginalCommandLifecycleReservation>(
            submissionQuota, reservation.get());
        assert(!MetalOriginalBindlessCommand::prepareOriginal(
            closure, forged, binding, device, diagnostic));
        forged.reset();
        {
            core::CpuMemoryQuotaLease pressure(
                submissionQuota, cpuBytes - submissionQuota->liveBytes());
            assert(pressure.charged());
            assert(!MetalOriginalBindlessCommand::prepareOriginal(
                closure, reservation, binding, device, diagnostic));
            assert(tx->status() == OriginalCommandStateTransaction::Status::Prepared
                && !root->originalSnapshot()->originalSubmissionBudgetOwner()->used());
        }
        auto command = MetalOriginalBindlessCommand::prepareOriginal(
            closure, reservation, binding, device, diagnostic);
        if (!command)
            std::fprintf(stderr, "Metal original command: %s\n", diagnostic.c_str());
        assert(command && tx->status() == OriginalCommandStateTransaction::Status::Pending);
        assert(!command->recordComputePassBegin(diagnostic)
            && core::StringView(diagnostic) == "RHI-METAL-ROOT-COMMAND-RECORDING-CLOSED");
        assert(!command->finish(diagnostic)
            && core::StringView(diagnostic) == "RHI-METAL-ROOT-COMMAND-RECORDING-CLOSED");
        assert(!binding->originalResult()->publishedBytes() && tx->applyOriginalPrototypePlan());
        assert(!MetalOriginalBindlessCommand::prepareOriginal(
            closure, reservation, binding, device, diagnostic));
        std::weak_ptr<const OriginalCommandRecordingSeal> weakSeal = seal;
        if (submit) {
            auto gate = createMetalRhiNativeSharedEventPayload("OriginalRootDelayedCompletion");
            assert(gate && gate->nativeRuntimeBacked);
            auto blocker = createMetalRhiNativeCommandPayload(
                "OriginalRootQueueGate", metalRhiIssuingResourceRegistry(device), &diagnostic);
            assert(blocker && blocker->finish(diagnostic));
            auto blocked = blocker->preparedCompletionState();
            MetalRhiNativeQueueStats queue;
            assert(blocker->submit(queue, blocked, gate, 1u, {}, 0u, diagnostic));
            const auto done = command->preparedCompletionState();
            const std::array quotas{submissionQuota,
                closureQuota,
                tx->originalMetadataQuota(),
                binding->originalMetadataQuota(),
                binding->originalResult()->originalMetadataQuota()};
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    return command->submit(queue, done, {}, 0u, {}, 0u, diagnostic)
                        ? nullptr
                        : diagnostic.c_str();
                }));
            assert(!done->isComplete() && !binding->originalResult()->publishedBytes()
                && tx->status() == OriginalCommandStateTransaction::Status::Pending);
            if (dropCommand)
                command.reset();
            closure.reset();
            reservation.reset();
            seal.reset();
            assert(
                !weakSeal.expired() && submissionQuota->liveBytes() && closureQuota->liveBytes());
            if (retireDevice) {
                device.retireOriginalDeviceLifetime();
                assert(!done->isComplete() && !binding->originalResult()->publishedBytes()
                    && !weakSeal.expired() && closureQuota->liveBytes());
            }
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    signalMetalRhiNativeSharedEvent(gate, 1u);
                    done->wait();
                    blocked->wait();
                    return done->outcome()
                            == (retireDevice ? MetalRhiCompletionState::Outcome::Failed
                                             : MetalRhiCompletionState::Outcome::Completed)
                        ? nullptr
                        : "Metal original completion failed";
                }));
            assert(tx->status()
                == (retireDevice ? OriginalCommandStateTransaction::Status::Invalidated
                                 : OriginalCommandStateTransaction::Status::Committed));
            assert(bool(binding->originalResult()->publishedBytes()) == !retireDevice);
            assert(weakSeal.expired() && !closureQuota->liveBytes());
            for (std::uint32_t i = 0u; i < counters.size(); ++i) {
                if (!counters[i])
                    continue;
                std::uint32_t value = 0u;
                assert(readMetalRhiNativeBuffer(
                    counters[i], &value, sizeof(value), 0u, sizeof(value), diagnostic));
                assert(value == expectedCounters[i]);
            }
            if (command)
                assert(!command->submit(queue, done, {}, 0u, {}, 0u, diagnostic));
            command.reset();
            blocker.reset();
        } else {
            command.reset();
            assert(tx->status() == OriginalCommandStateTransaction::Status::Abandoned
                && !binding->originalResult()->publishedBytes());
        }
        closure.reset();
        reservation.reset();
        seal.reset();
        for (auto& counter : counters)
            counter.reset();
        assert(!root->originalSnapshot()->originalSubmissionBudgetOwner()->used()
            && weakSeal.expired() && !submissionQuota->liveBytes() && !closureQuota->liveBytes());
        std::printf(
            "BINDLESS_METAL_ORIGINAL_EXECUTION6 submit=%d drop=%d fault=%u retire-device=%d same-root journal real-completion hot-submit0 hot-completion0 retirement0\n",
            submit,
            dropCommand,
            expectedFault,
            retireDevice);
    }
};

struct MetalComposedBindlessExecutionFixture : MetalOriginalBindlessExecutionFixture {
    bool indexedFault = false;
    mutable std::shared_ptr<rhi_metal::MetalRhiNativeBufferPayload> ordinaryOutput;

    void configureRequest(rhi::BindlessRequirements& request) const
    {
        MetalOriginalBindlessExecutionFixture::configureRequest(request);
        if (indexedFault)
            request.tables.front().resourceCount = 1u;
    }

    void configureDispatch(rhi::CommandScalarArguments::Dispatch& dispatch) const
    {
        dispatch.x = 2u;
    }

    template <class Table>
    void initializeBuffer(rhi::GpuBuffer& buffer, const Table&, std::uint32_t index) const
    {
        auto native = rhi_metal::metalRhiNativePayloadFor(buffer);
        assert(native);
        const std::uint32_t value = 777u;
        core::pmr::String diagnostic;
        assert(rhi_metal::writeMetalRhiNativeBuffer(
            native, &value, sizeof(value), 0u, buffer.desc().sizeBytes, diagnostic));
        counters[index] = native;
    }

    void configureOrdinaryBuffer(
        rhi::BufferDesc& desc, std::uint32_t, const rhi::DescriptorBindingDesc&) const
    {
        desc.memory = rhi::MemoryUsage::CpuToGpu;
    }

    void initializeOrdinaryBuffer(
        rhi::GpuBuffer& buffer, std::uint32_t set, const rhi::DescriptorBindingDesc& binding) const
    {
        std::array<std::uint32_t, 4> words{};
        if (set == 0u && binding.binding == 3u) {
            const float value = 17.0f;
            std::memcpy(words.data(), &value, sizeof(value));
        } else if (set == 2u && binding.binding == 3u)
            words[0] = 32u;
        else {
            assert(set == 2u && binding.binding == 4u);
            ordinaryOutput = rhi_metal::metalRhiNativePayloadFor(buffer);
        }
        core::pmr::String diagnostic;
        assert(rhi_metal::writeMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(buffer),
            words.data(),
            sizeof(words),
            0u,
            buffer.desc().sizeBytes,
            diagnostic));
    }

    template <class Root, class Inventory, class Binding, class Transaction>
    void operator()(const Root& root,
        const Inventory& inventory,
        const Binding& binding,
        const Transaction& tx,
        rhi::GpuDevice& device) const
    {
        using namespace rhi;
        assert(root->sourceContractVersion() == 2u && root->ordinaryDomains().size() == 2u
            && ordinaryOutput);
        {
            const auto& prepared = std::get<std::shared_ptr<const BindlessPreparedComputeBinding>>(
                root->originalPrepared());
            auto encoder = device.createCommandEncoder({"Root2 standalone refusal", 1u, 1u});
            assert(encoder);
            auto pass = encoder->beginComputePass({"Root2 standalone refusal"});
            assert(pass);
            assert(pass->setPipeline(*prepared->owners()->desc().pipeline).success);
            auto* indexed =
                static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
            assert(indexed);
            const auto refused =
                indexed->bindPreparedComputeBinding(prepared, BindlessAccessArray(nullptr, 0u));
            assert(!refused.success
                && refused.diagnostics == "RHI-BINDLESS-PIPELINE-DOMAINS-ORIGINAL-ROOT-REQUIRED");
            assert(!pass->dispatch(1u, 1u, 1u).success);
            assert(pass->end().success);
        }
        MetalOriginalBindlessExecutionFixture::operator()(root, inventory, binding, tx, device);
        if (submit) {
            std::array<std::uint32_t, 2> words{};
            core::pmr::String diagnostic;
            assert(rhi_metal::readMetalRhiNativeBuffer(
                ordinaryOutput, words.data(), sizeof(words), 0u, sizeof(words), diagnostic));
            assert(words[0] == 49u && words[1] == 49u + std::uint32_t(indexedFault));
        }
        ordinaryOutput.reset();
        std::printf(
            "BINDLESS_METAL_COMPOSED_ROOT2 submit=%d drop=%d fault=%u retire-device=%d sets0/1/2 exact-output same-journal retirement0\n",
            submit,
            dropCommand,
            expectedFault,
            retireDevice);
    }
};

struct MetalPublicBindlessExecutionFixture final : MetalComposedBindlessExecutionFixture {
    bool composed = true;

    void configureRequest(rhi::BindlessRequirements& request) const
    {
        if (composed)
            MetalComposedBindlessExecutionFixture::configureRequest(request);
        else
            MetalOriginalBindlessExecutionFixture::configureRequest(request);
    }

    void configureDispatch(rhi::CommandScalarArguments::Dispatch& dispatch) const
    {
        dispatch.x = composed ? 2u : 1u;
    }

    template <class Table>
    void initializeBuffer(rhi::GpuBuffer& buffer, const Table& table, std::uint32_t index) const
    {
        if (composed)
            MetalComposedBindlessExecutionFixture::initializeBuffer(buffer, table, index);
        else
            MetalOriginalBindlessExecutionFixture::initializeBuffer(buffer, table, index);
    }

    template <class Root, class Inventory, class Binding, class Transaction>
    void operator()(const Root& root,
        const Inventory& inventory,
        const Binding& binding,
        const Transaction& tx,
        rhi::GpuDevice& device) const
    {
        using namespace rhi;
        using namespace rhi_metal;
        using Execution = OriginalBindlessRootExecution;
        constexpr std::uint64_t bytes = 65536u;
        const std::array q{core::makeShared<core::CpuMemoryQuota>(bytes),
            core::makeShared<core::CpuMemoryQuota>(bytes),
            core::makeShared<core::CpuMemoryQuota>(bytes)};
        auto* extension =
            static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
        assert(extension);
        assert(root->originalSnapshot()->plan().originalExecutionContractVersion == 7u);
        auto entries = static_cast<std::uint32_t>(root->elements().size() + 1u);
        for (const auto& domain : root->ordinaryDomains())
            entries += domain.source->frozenElements().size();
        OriginalBindlessRootFenceExecutionInputs inputs{
            root, inventory, tx, binding, q[0], q[1], q[2]};
        OriginalBindlessRootFenceExecutionBudget budget{{7u, true},
            {1u, true, entries, 512u, 1024u, bytes},
            {6u, true, 1u, 0u, 0u, tx->retainedOwnerCount() + 2u, 4096u, 0u, bytes, bytes, 0u},
            {2u, true, 1u, 0u, 0u},
            bytes,
            256u};
        budget.requireWorkloadRetention = true;
        core::pmr::String diagnostic;
        diagnostic.reserve(256u);
        const auto refused = [&](const auto& b) {
            assert(!extension->prepareOriginalRootExecution(device, inputs, b, diagnostic));
            assert(!diagnostic.empty()
                && tx->status() == OriginalCommandStateTransaction::Status::Prepared
                && !binding->originalResult()->publishedBytes());
            for (const auto& quota : q)
                assert(!quota->liveBytes());
        };
        auto wrong = budget;
        wrong.intent.contractVersion = 6u;
        refused(wrong);
        wrong = budget;
        wrong.lifecycle.contractVersion = 14u;
        refused(wrong);
        wrong = budget;
        --wrong.closure.maxResourceEntries;
        refused(wrong);
        {
            core::CpuMemoryQuotaLease pressure(q[2], bytes);
            assert(pressure.charged());
            assert(!extension->prepareOriginalRootExecution(device, inputs, budget, diagnostic));
            assert(tx->status() == OriginalCommandStateTransaction::Status::Prepared
                && !q[0]->liveBytes() && !q[1]->liveBytes());
        }
        auto issuer = extension->prepareOriginalRootExecution(device, inputs, budget, diagnostic);
        if (!issuer)
            std::fprintf(stderr, "Metal public prepare7: %s\n", diagnostic.c_str());
        assert(issuer && !issuer->recordingProven() && !issuer->submissionCompleted()
            && !issuer->pendingRecords());
        auto alias = std::shared_ptr<const Execution>(q[2], issuer.get());
        assert(Execution::record(alias, {7u, true}));
        alias.reset();
        assert(Execution::record(issuer, {6u, true}) && Execution::submit(issuer, {7u, true}));
        const auto* error = Execution::record(issuer, {7u, true});
        if (error)
            std::fprintf(stderr, "Metal public record7: %s\n", error);
        assert(!error && issuer->recordingProven()
            && tx->status() == OriginalCommandStateTransaction::Status::Pending);
        assert(
            Execution::record(issuer, {7u, true}) && !binding->originalResult()->publishedBytes());
        if (!submit) {
            assert(!issuer->cancelExecution());
            issuer.reset();
            assert(tx->status() == OriginalCommandStateTransaction::Status::Abandoned);
        } else {
            auto gate = createMetalRhiNativeSharedEventPayload("public-original-gate");
            auto blocker = createMetalRhiNativeCommandPayload("public-original-blocker", {});
            assert(gate && blocker);
            auto blocked = blocker->preparedCompletionState();
            MetalRhiNativeQueueStats queue;
            assert(blocker->finish(diagnostic)
                && blocker->submit(queue, blocked, gate, 1u, {}, 0u, diagnostic));
            const std::array quotas{q[0],
                q[1],
                q[2],
                tx->originalMetadataQuota(),
                binding->originalMetadataQuota(),
                binding->originalResult()->originalMetadataQuota()};
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                return Execution::submit(issuer, {7u, true});
            }));
            assert(issuer->pendingRecords() == 1u && issuer->cancelExecution()
                && !issuer->submissionCompleted());
            if (dropCommand)
                issuer.reset();
            assert(q[0]->liveBytes() && q[1]->liveBytes()
                && !binding->originalResult()->publishedBytes());
            if (retireDevice)
                device.retireOriginalDeviceLifetime();
            const auto closurePressureBytes = q[0]->limitBytes() - q[0]->liveBytes();
            const auto* completionError = CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    signalMetalRhiNativeSharedEvent(gate, 1u);
                    const auto deadline =
                        std::chrono::steady_clock::now() + std::chrono::seconds(10);
                    while (q[0]->liveBytes() != closurePressureBytes) {
                        if (std::chrono::steady_clock::now() > deadline)
                            return "Metal public completion timeout";
                        std::this_thread::yield();
                    }
                    return nullptr;
                });
            if (completionError)
                std::fprintf(stderr,
                    "public completion: %s quotas=%llu/%llu/%llu blocked=%d pending=%u tx=%u\n",
                    completionError,
                    static_cast<unsigned long long>(q[0]->liveBytes()),
                    static_cast<unsigned long long>(q[1]->liveBytes()),
                    static_cast<unsigned long long>(q[2]->liveBytes()),
                    int(blocked->outcome()),
                    issuer ? issuer->pendingRecords() : 0u,
                    unsigned(tx->status()));
            assert(!completionError);
            assert(tx->status()
                == (retireDevice ? OriginalCommandStateTransaction::Status::Invalidated
                                 : OriginalCommandStateTransaction::Status::Committed));
            assert(bool(binding->originalResult()->publishedBytes()) == !retireDevice);
            if (issuer) {
                // Custody drains just before the native completed state becomes visible.
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (issuer->pendingRecords()) {
                    assert(std::chrono::steady_clock::now() < deadline);
                    std::this_thread::yield();
                }
                assert(issuer->submissionCompleted() == !retireDevice);
                assert(bool(issuer->executionDiagnostic()) == retireDevice);
            }
            for (std::size_t i = 0u; i < counters.size(); ++i) {
                if (!counters[i])
                    continue;
                std::uint32_t value = 0u;
                assert(readMetalRhiNativeBuffer(
                    counters[i], &value, sizeof(value), 0u, sizeof(value), diagnostic));
                assert(value == expectedCounters[i]);
            }
            if (ordinaryOutput) {
                std::array<std::uint32_t, 2> words{};
                assert(readMetalRhiNativeBuffer(
                    ordinaryOutput, words.data(), sizeof(words), 0u, sizeof(words), diagnostic));
                assert(words[0] == 49u && words[1] == 49u + std::uint32_t(indexedFault));
            }
            issuer.reset();
            blocked->wait();
            blocker.reset();
        }
        for (const auto& quota : q)
            assert(!quota->liveBytes());
        assert(!root->originalSnapshot()->originalSubmissionBudgetOwner()->used());
        for (auto& counter : counters)
            counter.reset();
        ordinaryOutput.reset();
        std::printf(
            "BINDLESS_METAL_PUBLIC_EXECUTION7 root=%u submit=%d drop=%d fault=%u retire-device=%d same-journal hot-submit0 hot-completion0 retirement0\n",
            root->sourceContractVersion(),
            submit,
            dropCommand,
            expectedFault,
            retireDevice);
    }
};
} // namespace arp::test
