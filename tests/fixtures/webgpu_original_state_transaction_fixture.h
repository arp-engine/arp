#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_state_transaction.h"
#include "rhi/backends/webgpu/webgpu_original_transfer_dependency_validation.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class WebGpuOriginalStateTransactionFixture final {
    using Transaction = rhi_webgpu::WebGpuOriginalStateTransaction;
    using States = rhi_webgpu::WebGpuOriginalResourceStates;

public:
    static void verifyContract()
    {
        rhi_webgpu::WebGpuOriginalStateTransactionBudget budget;
        assert(std::string_view(Transaction::validateBudget(budget))
            == "RHI-WEBGPU-STATE-TRANSACTION-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(Transaction::validateBudget(budget))
            == "RHI-WEBGPU-STATE-TRANSACTION-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(Transaction::validateBudget(budget))
            == "RHI-WEBGPU-STATE-TRANSACTION-BUDGET");
        budget.maxCpuMetadataBytes = 16384u;
        assert(!Transaction::validateBudget(budget));
        const char* diagnostic = nullptr;
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        assert(!Transaction::prepareOriginal({}, budget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATE-OWNER"
            && !quota->liveBytes());
        assert(std::string_view(Transaction::apply({})) == "RHI-WEBGPU-STATE-TRANSACTION-OWNER");
        assert(std::string_view(Transaction::commit({})) == "RHI-WEBGPU-STATE-TRANSACTION-OWNER");
        rhi::common::RecordedResourceStateControl control(rhi::ResourceState::CopySrc);
        core::MutationJournal owned, foreign;
        const auto initial = control.observe();
        assert(initial && !control.observePending(owned));
        assert(!control.write(rhi::ResourceState::CopyDst, &owned));
        const auto pending = control.observePending(owned);
        assert(pending && !control.observe() && !control.matches(*initial));
        assert(
            control.matchesPending(*pending, owned) && !control.matchesPending(*pending, foreign));
        control.invalidate();
        owned.rollback();
        assert(control.state() == rhi::ResourceState::CopySrc && !control.active()
            && !control.observePending(owned));
        assert(std::string_view(control.write(rhi::ResourceState::CopyDst, nullptr))
            == "RHI-RESOURCE-STATE-REVISION-EXHAUSTED");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    template <class Snapshot> static void verify(Snapshot&& snapshot)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        const char* diagnostic = nullptr;
        auto quota = core::makeShared<core::CpuMemoryQuota>(16384u);
        auto initial = snapshot();
        assert(initial);
        const auto& root = *initial->inputs()->scalars()->usage()->projection()->transfer();
        std::uint32_t transitions = 0u;
        for (const auto& d : initial->inputs()->descriptors()) {
            const auto* dependency = std::get_if<ResourceDependencyArguments>(&d.arguments);
            if (dependency
                && (dependency->kind() == CommandUsageKind::BufferBarrier
                    || dependency->kind() == CommandUsageKind::TextureBarrier))
                ++transitions;
        }
        assert(transitions && initial->states().size() == 5u);
        WebGpuOriginalStateTransactionBudget budget{
            1u, true, 5u, transitions, 0u, quota->limitBytes()};
        const auto prepare = [&] {
            return Transaction::prepareOriginal(snapshot(), budget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(prepare);
        pressure.verify(peak, prepare);
        --budget.maxStateControls;
        assert(!prepare()
            && std::string_view(diagnostic) == "RHI-WEBGPU-STATE-TRANSACTION-CONTROL-CAPACITY");
        ++budget.maxStateControls;
        --budget.maxTransitions;
        assert(!prepare()
            && std::string_view(diagnostic) == "RHI-WEBGPU-STATE-TRANSACTION-TRANSITION-CAPACITY");
        ++budget.maxTransitions;
        --budget.maxCpuMetadataBytes;
        assert(!prepare() && std::string_view(diagnostic) == "RHI-WEBGPU-STATE-TRANSACTION-QUOTA");
        ++budget.maxCpuMetadataBytes;
        {
            core::CpuMemoryQuotaLease exhaust(quota, quota->limitBytes());
            assert(exhaust.charged());
            assert(!prepare()
                && std::string_view(diagnostic) == "RHI-WEBGPU-STATE-TRANSACTION-ALLOCATION");
        }
        budget.maxStateControls = UINT32_MAX;
        assert(!prepare()
            && std::string_view(diagnostic) == "RHI-WEBGPU-STATE-TRANSACTION-ALLOCATION");
        budget.maxStateControls = 5u;
        auto forgedState = std::shared_ptr<const States>(quota, initial.get());
        assert(!Transaction::prepareOriginal(forgedState, budget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATE-OWNER");
        forgedState.reset();
        auto transaction = prepare();
        assert(transaction && !transaction->validate());
        auto forged = std::shared_ptr<Transaction>(quota, transaction.get());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(
            std::string_view(Transaction::apply(forged)) == "RHI-WEBGPU-STATE-TRANSACTION-OWNER");
        assert(
            std::string_view(Transaction::commit(forged)) == "RHI-WEBGPU-STATE-TRANSACTION-OWNER");
        assert(std::string_view(Transaction::commit(transaction))
            == "RHI-WEBGPU-STATE-TRANSACTION-STATUS");
        const auto original = transaction->states();
        assert(!Transaction::apply(transaction)
            && transaction->status() == Transaction::Status::Pending && !transaction->validate());
        assert(std::string_view(original->validate()) == "RHI-WEBGPU-ORIGINAL-STATES-PENDING"
            && !original->validateSourceOwners());
        assertInitialMapping(original);
        assert(std::string_view(transaction->executionDiagnostic())
            == webGpuGenericRecordingUsageDiagnostic);
        assert(std::string_view(Transaction::apply(transaction))
            == "RHI-WEBGPU-STATE-TRANSACTION-STATUS");
        core::MutationJournal foreign;
        for (const auto& state : original->states())
            if (state.source.control.pending()) {
                const auto* resource = root.find(state.source.resource);
                assert(resource);
                assert(std::string_view(write(*resource, state.source.control.state(), &foreign))
                        == "RHI-RESOURCE-STATE-RECORDING-CONFLICT"
                    && foreign.empty());
                assert(std::string_view(write(*resource, state.source.control.state()))
                    == "RHI-RESOURCE-STATE-RECORDING-CONFLICT");
            }
        // A declared unused control is still part of the exact graph.
        const auto* unused = untouched(*original);
        assert(unused);
        const auto* resource = root.find(unused->source.resource);
        assert(resource);
        assert(!write(*resource, unused->observation.value, &foreign));
        assert(std::string_view(Transaction::commit(transaction))
            == "RHI-WEBGPU-ORIGINAL-STATES-PENDING");
        foreign.rollback();
        assertInitialMapping(original);
        assert(std::string_view(Transaction::commit(transaction))
            == "RHI-WEBGPU-ORIGINAL-STATES-STALE");
        transaction->abandon();
        assert(
            transaction->journalEmpty() && transaction->status() == Transaction::Status::Abandoned);
        for (const auto& state : original->states())
            assert(state.source.control.state() == state.observation.value
                && !state.source.control.pending());
        assert(std::string_view(original->validate()) == "RHI-WEBGPU-ORIGINAL-STATES-STALE");
        assertInitialMapping(original);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        forged.reset();
        transaction.reset();
        assert(!quota->liveBytes());
        verifyCommitAndDrop(snapshot, budget, quota, root);
        assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_ORIGINAL_STATE_TRANSACTION_OK %u %llu controls5 original pending commit drop foreign untouched rollback allocation0\n",
            transitions,
            static_cast<unsigned long long>(peak));
        std::puts(
            "BINDLESS_RHI_ORIGINAL_INITIAL_MAPPING_OK immutable pending committed rollback untouched current-gate allocation0");
    }

    static void retainInputsForLoss(const std::shared_ptr<const States>& states,
        const std::shared_ptr<core::CpuMemoryQuota>& inputQuota)
    {
        assert(!lossInputs_ && !loss_);
        lossInputs_ = states->inputs();
        lossInputQuota_ = inputQuota;
    }

    static void activateLossProbe()
    {
        using namespace rhi_webgpu;
        const char* diagnostic = nullptr;
        assert(lossInputs_ && !loss_);
        lossStateQuota_ = core::makeShared<core::CpuMemoryQuota>(16384u);
        lossQuota_ = core::makeShared<core::CpuMemoryQuota>(16384u);
        auto states = States::prepareOriginal(lossInputs_,
            {1u, true, 5u, 5u, lossStateQuota_->limitBytes()},
            lossStateQuota_,
            diagnostic);
        assert(states);
        loss_ = Transaction::prepareOriginal(
            states, {1u, true, 5u, 2u, 0u, lossQuota_->limitBytes()}, lossQuota_, diagnostic);
        assert(loss_);
        assert(!Transaction::apply(loss_) && loss_->status() == Transaction::Status::Pending
            && !loss_->validate());
        lossInputs_.reset();
        std::puts(
            "BINDLESS_RHI_ORIGINAL_STATE_TRANSACTION_LOSS_PENDING_OK original-own-journal controls5");
    }

    static void verifyActualDeviceLoss()
    {
        using namespace rhi_webgpu;
        assert(loss_ && webGpuNativeDeviceLost());
        assert(std::string_view(loss_->validate()) == "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE-LOST");
        assert(std::string_view(
                   rhi_webgpu::WebGpuOriginalTransferDependencyValidation::validateInitialMapping(
                       loss_->states()))
            == "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE-LOST");
        const auto original = loss_->states();
        loss_->abandon();
        assert(loss_->journalEmpty() && loss_->status() == Transaction::Status::Abandoned);
        for (const auto& state : original->states())
            assert(!state.source.control.pending()
                && state.source.control.state() == state.observation.value);
        loss_.reset();
        assert(!lossQuota_->liveBytes());
        // Original is the last local metadata observer; final source/state
        // quota retirement is checked by the caller after this scope ends.
        std::puts(
            "BINDLESS_RHI_ORIGINAL_STATE_TRANSACTION_LOSS_OK own-rollback source-reject allocation0");
    }

    static void verifyLossQuotasRetired()
    {
        assert(!loss_ && !lossInputQuota_->liveBytes() && !lossStateQuota_->liveBytes()
            && !lossQuota_->liveBytes());
        lossInputQuota_.reset();
        lossStateQuota_.reset();
        lossQuota_.reset();
    }

private:
    static void assertInitialMapping(const std::shared_ptr<const States>& states)
    {
        using namespace rhi_webgpu;
        assert(
            !WebGpuOriginalTransferStateValidation::validateInitialSequence(states, 0u).diagnostic);
        assert(!WebGpuOriginalTransferDependencyValidation::validateInitialMapping(states));
        assert(WebGpuOriginalTransferDependencyValidation::validate(states));
    }

    static const char* write(const rhi_webgpu::WebGpuTransferResourceObservation& resource,
        rhi::ResourceState state,
        core::MutationJournal* journal = nullptr)
    {
        return resource.buffer ? resource.buffer->recordState(state, journal)
                               : resource.texture->recordState(state, journal);
    }

    static const States::State* untouched(const States& states)
    {
        for (const auto& state : states.states())
            if (!state.source.control.pending())
                return &state;
        return nullptr;
    }

    template <class Snapshot>
    static void verifyCommitAndDrop(Snapshot&& snapshot,
        const rhi_webgpu::WebGpuOriginalStateTransactionBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const rhi_webgpu::WebGpuOrdinaryTransferRetention& root)
    {
        const char* diagnostic = nullptr;
        auto transaction = Transaction::prepareOriginal(snapshot(), budget, quota, diagnostic);
        assert(transaction);
        auto original = transaction->states();
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Transaction::apply(transaction) && !Transaction::commit(transaction));
        assert(transaction->status() == Transaction::Status::Committed
            && transaction->journalEmpty() && !transaction->validate());
        assert(original->validate());
        assertInitialMapping(original);
        assert(std::string_view(Transaction::commit(transaction))
            == "RHI-WEBGPU-STATE-TRANSACTION-STATUS");
        transaction->abandon();
        assert(transaction->status() == Transaction::Status::Committed && !transaction->validate());
        const auto* resource = root.find(original->states()[0].source.resource);
        assert(resource);
        assert(!write(*resource, original->states()[0].source.control.state()));
        assert(transaction->validate());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        transaction.reset();
        assert(!quota->liveBytes());
        for (const auto& state : original->states())
            assert(!write(*root.find(state.source.resource), state.observation.value));
        original.reset();
        transaction = Transaction::prepareOriginal(snapshot(), budget, quota, diagnostic);
        assert(transaction);
        original = transaction->states();
        assert(!Transaction::apply(transaction));
        transaction.reset();
        assert(!quota->liveBytes());
        for (const auto& state : original->states())
            assert(!state.source.control.pending()
                && state.source.control.state() == state.observation.value);
        assert(original->validate()); // Rollback restores values, never the old revision.
        assertInitialMapping(original);
    }

    inline static std::shared_ptr<Transaction> loss_;
    inline static std::shared_ptr<const rhi_webgpu::WebGpuOriginalCommandInputs> lossInputs_;
    inline static std::shared_ptr<core::CpuMemoryQuota> lossQuota_, lossStateQuota_,
        lossInputQuota_;
#endif
};
} // namespace arp::test
