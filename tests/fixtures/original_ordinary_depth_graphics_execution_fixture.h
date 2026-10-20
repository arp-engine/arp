#pragma once
#include "ordinary_depth_comparison_oracle_fixture.h"
#include "original_ordinary_depth_root_fixture.h"
#include "rhi/frontend/bindless_graphics_execution.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace arp::test {
// Executes the existing original stream/Root/transaction and real fence. No
// separate executor, texture stamp, journal or host completion is introduced.
template <class Backend> class OriginalOrdinaryDepthGraphicsExecutionFixture final {
    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "ordinary Graphics depth execution: %s\n", id);
        assert(!id);
    }

public:
    static void execute(const OriginalOrdinaryDepthRootFixture::Prepared& prepared,
        rhi::GpuDevice& device,
        const rhi::OriginalBindlessRootNativeClosureBudget& closureBudget,
        bool fault)
    {
        using namespace rhi;
        using Public = OriginalBindlessGraphicsExecution;
        using Tx = OriginalCommandStateTransaction;
        const auto content = OriginalOrdinaryDepthRootFixture::combinedBudget(prepared);
        // Cancel a genuinely recorded transaction, then rebuild the same graph
        // and execute it. Both paths must release their original reservation.
        for (const bool submit : {false, true}) {
            if (!submit && Backend::captureSubmittedOnly)
                continue;
            OriginalBindlessGraphicsStreamFixture::execute(
                prepared.root,
                prepared.inventory,
                device,
                [&](const auto& root,
                    const auto& inventory,
                    const auto& binding,
                    const auto& tx,
                    auto& gpu) {
                    const auto bytes = tx->originalMetadataQuota()->limitBytes();
                    std::array<std::shared_ptr<core::CpuMemoryQuota>, 4u> q;
                    for (auto& owner : q)
                        owner = core::makeShared<core::CpuMemoryQuota>(bytes);
                    auto* extension = static_cast<BindlessCrossExtension*>(
                        gpu.queryInterface(kExtensionBindlessCross));
                    assert(extension
                        && extension->capabilities().originalGraphicsDepthExecutionContractVersion
                            == 2u);
                    const char* id = nullptr;
                    using Closure = typename Backend::Closure;
                    using Pass = typename Backend::Pass;
                    auto closure =
                        Closure::prepareOriginal(root, inventory, gpu, closureBudget, q[0], id);
                    require(id);
                    assert(closure);
                    OriginalBindlessGraphicsPassBudget passBudget{1u,
                        true,
                        1u,
                        Backend::passNativeObjects,
                        static_cast<std::uint32_t>(closure->retainedOwnerCount()
                            + tx->retainedOwnerCount()
                            + binding->originalProgram()->retainedOwnerCount()),
                        bytes};
                    auto physical =
                        core::makeShared<core::AtomicCapacityBudget>(passBudget.maxNativeObjects);
                    auto pass = Pass::prepareOriginal(closure,
                        tx,
                        binding->originalProgram(),
                        gpu,
                        passBudget,
                        physical,
                        q[1],
                        id);
                    require(id);
                    assert(pass);
                    passBudget.maxRetainedOwners = pass->retainedOwnerCount();
                    OriginalCommandNativeLifecycleBudget life{
                        19u, true, 1u, 0u, 0u, tx->retainedOwnerCount(), 1u, 0u, bytes, bytes, 0u};
                    life.maxRecordingOwners +=
                        OriginalCommandRecordingSeal::additionalOwnerCountFor(life);
                    life.maxSubmissionOwners = closure->retainedOwnerCount()
                        + tx->retainedOwnerCount()
                        + OriginalCommandRecordingSeal::additionalOwnerCountFor(life)
                        + OriginalCommandLifecycleReservation::additionalOwnerCount
                        + binding->originalProgram()->retainedOwnerCount()
                        + OriginalCommandBufferReadbackBinding::retainedOwnerCount
                        + Backend::submissionOwnerOverhead + pass->retainedOwnerCount();
                    pass.reset();
                    closure.reset();
                    assert(!physical->used() && !q[0]->liveBytes() && !q[1]->liveBytes());
                    OriginalBindlessGraphicsExecutionInputs inputs{root,
                        inventory,
                        tx,
                        binding,
                        nullptr,
                        0u,
                        q[0],
                        q[1],
                        q[2],
                        q[3],
                        physical};
                    OriginalBindlessGraphicsExecutionBudget budget;
                    budget.intent = {2u, true};
                    budget.closure = closureBudget;
                    budget.pass = passBudget;
                    budget.lifecycle = life;
                    budget.capacity = {10u, true, 1u, 0u, 0u};
                    budget.maxCpuViewBytes = bytes;
                    budget.maxDiagnosticBytes = 512u;
                    core::pmr::String diagnostic;
                    diagnostic.reserve(budget.maxDiagnosticBytes);
                    auto less = budget;
                    --less.lifecycle.maxSubmissionOwners;
                    assert(!extension->prepareOriginalGraphicsExecution(
                        gpu, inputs, less, diagnostic));
                    assert(diagnostic == Backend::ownerCapacityDiagnostic
                        && tx->status() == Tx::Status::Prepared && tx->journalEmpty()
                        && !binding->originalResult()->publishedBytes() && !physical->used());
                    for (const auto& owner : q)
                        assert(!owner->liveBytes());
                    auto issuer = extension->prepareOriginalGraphicsExecution(
                        gpu, inputs, budget, diagnostic);
                    if (!issuer)
                        std::fprintf(stderr, "ordinary Graphics prepare: %s\n", diagnostic.c_str());
                    assert(issuer && !issuer->recordingProven() && !issuer->pollCompletion());
                    assert(Public::record(issuer, {1u, true}));
                    require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        std::array{q[2], q[3], tx->originalMetadataQuota()}, [&]() noexcept {
                            return Public::record(issuer, {2u, true});
                        }));
                    assert(issuer->recordingProven() && tx->status() == Tx::Status::Pending
                        && !binding->originalResult()->publishedBytes());
                    assert(tx->commitOriginalPendingOwnership());
                    if (!submit) {
                        require(issuer->cancel());
                        assert(tx->status() == Tx::Status::Abandoned
                            && !binding->originalResult()->publishedBytes());
                    } else {
                        require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                            std::array{q[2], q[3], tx->originalMetadataQuota()}, [&]() noexcept {
                                return Public::submit(issuer, {2u, true});
                            }));
                        assert(issuer->cancel());
                        assert(Public::submit(issuer, {1u, true}));
                        const auto deadline =
                            std::chrono::steady_clock::now() + std::chrono::seconds(5);
                        while (!issuer->pollCompletion()
                            && std::chrono::steady_clock::now() < deadline)
                            std::this_thread::yield();
                        assert(issuer->submissionCompleted() && !issuer->completionDiagnostic()
                            && tx->status() == Tx::Status::Committed && tx->journalEmpty());
                        std::uint32_t faultBits = ~0u;
                        assert(binding->originalResult()->publishedBytes());
                        std::memcpy(&faultBits,
                            binding->originalResult()->publishedBytes(),
                            sizeof(faultBits));
                        assert(faultBits == (fault ? 1u : 0u));
                        std::size_t results = 0u;
                        for (const auto& element : root->elements()) {
                            if (element.declaration.write.type != DescriptorType::StorageBuffer
                                || element.declaration.write.resource
                                    == root->originalSnapshot()->faultLatchBuffer()->id())
                                continue;
                            const auto& lease =
                                std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                                    element.owner);
                            assert(
                                lease->originalObject()->desc().sizeBytes == 40u * sizeof(float));
                            std::array<float, 40u> values{};
                            Backend::readValues(*lease->originalObject(), values, diagnostic);
                            OrdinaryDepthComparisonOracleFixture::verifyValues(values);
                            ++results;
                        }
                        assert(results == 1u);
                        Backend::verifyOutput(*inventory, fault);
                    }
                    issuer.reset();
                    assert(!physical->used());
                    for (const auto& owner : q)
                        assert(!owner->liveBytes());
                },
                &content);
            assert(!prepared.inventory->originalStateTransactionCount());
        }
    }
};
} // namespace arp::test
