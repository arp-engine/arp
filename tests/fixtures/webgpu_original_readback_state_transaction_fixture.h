#pragma once
#include "rhi/backends/webgpu/webgpu_original_state_transaction.h"
#include "webgpu_map_handoff_fixture.h"
#include "webgpu_original_texture_readback_copies_fixture.h"
#include "webgpu_original_texture_readback_state_intent_fixture.h"

namespace arp::test {
class WebGpuOriginalReadbackStateTransactionFixture final {
public:
    using Transaction = rhi_webgpu::WebGpuOriginalStateTransaction;
    using Budget = rhi_webgpu::WebGpuOriginalReadbackStateTransactionBudget;
    using States = rhi_webgpu::WebGpuOriginalResourceStates;
    static constexpr auto cpuBytes = WebGpuOriginalTextureReadbackStateIntentFixture::cpuBytes;

    static Budget budget()
    {
        return {1u, true, 1u, 2u, 1u, 1536u, {1u, true, 3u, 1u, 0u, cpuBytes}};
    }

    static void verifyContract()
    {
        assert(std::string_view(Transaction::validateCommittedReadbackDeclarationOriginal({}))
            == "RHI-WEBGPU-STATE-TRANSACTION-OWNER");
        std::puts(
            "BINDLESS_RHI_READBACK_COMMITTED_DECLARATION_CONTRACT_OK cpu-declaration-only default-unmapped native-proof-required");
        Budget b;
        assert(std::string_view(Transaction::validateBudget(b))
            == "RHI-WEBGPU-READBACK-TRANSACTION-VERSION");
        b.contractVersion = 1u;
        assert(std::string_view(Transaction::validateBudget(b))
            == "RHI-WEBGPU-READBACK-TRANSACTION-OPT-IN");
        b.prototypeOptIn = true;
        assert(std::string_view(Transaction::validateBudget(b))
            == "RHI-WEBGPU-READBACK-TRANSACTION-BUDGET");
        b = budget();
        assert(!Transaction::validateBudget(b));
        auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* diagnostic = nullptr;
        assert(!Transaction::prepareReadbackOriginal({}, b, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATE-OWNER"
            && !quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_READBACK_CURRENT_CONTRACT_OK reused-original-transaction exact-profile budgets-closed no-native-proof");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static auto prepare(
        const std::shared_ptr<const States>& states, std::shared_ptr<core::CpuMemoryQuota> quota)
    {
        const char* diagnostic = nullptr;
        auto result =
            Transaction::prepareReadbackOriginal(states, budget(), std::move(quota), diagnostic);
        assert(result);
        return result;
    }

    static const auto& pair(const std::shared_ptr<const States>& states)
    {
        return states->inputs()->scalars()->usage()->projection()->originalReadbacks()[0];
    }

    static void verifyColdAndPending(const std::shared_ptr<const States>& states)
    {
        const char* diagnostic = nullptr;
        auto b = budget();
        auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const auto factory = [&] {
            return Transaction::prepareReadbackOriginal(states, b, quota, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !quota->liveBytes()
                && !states->validate());
        };
        --b.maxMipMappings;
        reject("RHI-WEBGPU-READBACK-TRANSACTION-CAPACITY");
        b = budget();
        --b.maxUnpublishedResultBytes;
        reject("RHI-WEBGPU-READBACK-TRANSACTION-CAPACITY");
        b = budget();
        --b.maxOriginalPairs;
        reject("RHI-WEBGPU-READBACK-TRANSACTION-BUDGET");
        b = budget();
        --b.maxPrivateAccessClaims;
        reject("RHI-WEBGPU-READBACK-TRANSACTION-BUDGET");
        b = budget();
        --b.states.maxStateControls;
        reject("RHI-WEBGPU-STATE-TRANSACTION-CONTROL-CAPACITY");
        b = budget();
        --b.states.maxTransitions;
        reject("RHI-WEBGPU-STATE-TRANSACTION-TRANSITION-CAPACITY");
        b = budget();
        --b.states.maxCpuMetadataBytes;
        reject("RHI-WEBGPU-STATE-TRANSACTION-QUOTA");
        b = budget();
        {
            core::CpuMemoryQuotaLease full(quota, cpuBytes);
            assert(full.charged() && !factory()
                && std::string_view(diagnostic) == "RHI-WEBGPU-STATE-TRANSACTION-ALLOCATION");
        }
        b.states.maxStateControls = UINT32_MAX;
        reject("RHI-WEBGPU-STATE-TRANSACTION-ALLOCATION");
        b = budget();
        auto forgedState = std::shared_ptr<const States>(quota, states.get());
        assert(!Transaction::prepareReadbackOriginal(forgedState, b, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATE-OWNER");
        forgedState.reset();
        assert(!Transaction::prepareOriginal(states, b.states, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-STATE-TRANSACTION-SOURCE-COVERAGE");
        auto transaction = factory();
        assert(transaction && !transaction->validate());
        assert(
            std::string_view(Transaction::validateCommittedReadbackDeclarationOriginal(transaction))
            == "RHI-WEBGPU-READBACK-DECLARATION-COMMITTED-STATUS");
        auto forged = std::shared_ptr<Transaction>(quota, transaction.get());
        auto copyQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto copies =
            WebGpuOriginalTextureReadbackCopiesFixture::verifyCold(transaction, copyQuota);
        const auto& destination = *pair(states)->destination();
        const auto* staging = destination.source()->retainedNativeResourceAt(0u);
        auto& target = *destination.source()->resource().texture;
        auto& buffer = *destination.nativeResource();
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(
            std::string_view(Transaction::apply(forged)) == "RHI-WEBGPU-STATE-TRANSACTION-OWNER");
        assert(std::string_view(Transaction::commit(transaction))
            == "RHI-WEBGPU-STATE-TRANSACTION-STATUS");
        assert(!Transaction::apply(transaction) && !transaction->validate()
            && transaction->status() == Transaction::Status::Pending);
        assert(
            std::string_view(Transaction::validateCommittedReadbackDeclarationOriginal(transaction))
            == "RHI-WEBGPU-READBACK-DECLARATION-COMMITTED-STATUS");
        WebGpuOriginalTextureReadbackCopiesFixture::verifyCurrent(copies);
        assert(target.recordedState().pending()
            && target.recordedState().state() == rhi::ResourceState::CopySrc
            && buffer.recordedState().pending()
            && buffer.recordedState().state() == rhi::ResourceState::CopyDst
            && !staging->buffer->recordedState().pending());
        assert(std::string_view(states->validate()) == "RHI-READBACK-SOURCE-STATE-PENDING"
            && !pair(states)->validateReadyDeclaration());
        core::MutationJournal foreign;
        assert(std::string_view(target.recordState(rhi::ResourceState::CopySrc, &foreign))
            == "RHI-RESOURCE-STATE-RECORDING-CONFLICT");
        assert(std::string_view(buffer.recordState(rhi::ResourceState::CopyDst, &foreign))
                == "RHI-RESOURCE-STATE-RECORDING-CONFLICT"
            && foreign.empty());
        const auto originalBytes = buffer.sizeBytes;
        ++buffer.sizeBytes;
        assert(transaction->validate());
        buffer.sizeBytes = originalBytes;
        assert(!transaction->validate());
        assert(std::string_view(transaction->executionDiagnostic())
                == rhi_webgpu::webGpuGenericRecordingUsageDiagnostic
            && !destination.resultPublished());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        WebGpuMapHandoffFixture::verifyMapStateSeparation(
            buffer.buffer, destination.unpublishedResultCapacityBytes(), [&] {
                assert(std::string_view(transaction->validate())
                        == "RHI-READBACK-DESTINATION-NATIVE-LAYOUT"
                    && !destination.resultPublished());
                assert(std::string_view(copies->validate())
                    == "RHI-READBACK-DESTINATION-NATIVE-LAYOUT");
            });
        assert(!transaction->validate());
        const auto secondPrevious =
            std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto secondAllocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!staging->buffer->recordState(staging->buffer->recordedState().state(), &foreign));
        assert(std::string_view(Transaction::commit(transaction))
            == "RHI-WEBGPU-ORIGINAL-STATES-PENDING");
        foreign.rollback();
        assert(std::string_view(Transaction::commit(transaction))
            == "RHI-WEBGPU-ORIGINAL-STATES-STALE");
        transaction->abandon();
        assert(transaction->journalEmpty()
            && transaction->status() == Transaction::Status::Abandoned
            && !target.recordedState().pending() && !buffer.recordedState().pending());
        assert(std::string_view(states->validate()) == "RHI-READBACK-SOURCE-STATE-STALE"
            && !pair(states)->validateReadyDeclaration());
        assert(core::defaultCpuAllocator().stats().allocationCount == secondAllocations);
        std::pmr::set_default_resource(secondPrevious);
        assert(std::string_view(copies->validate()) == "RHI-WEBGPU-STATE-TRANSACTION-ABANDONED");
        copies.reset();
        assert(!copyQuota->liveBytes());
        forged.reset();
        transaction.reset();
        assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_READBACK_CURRENT_NATIVE_OK %llu controls3 transitions1 access-claim1 owned-pending foreign-staging rollback mapped-gate allocation0 no-proof\n",
            static_cast<unsigned long long>(peak));
    }

    static void verifyCommit(const std::shared_ptr<const States>& states)
    {
        auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto transaction = prepare(states, quota);
        auto copyQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* diagnostic = nullptr;
        auto copies =
            WebGpuOriginalTextureReadbackCopiesFixture::Copies::prepareOriginal(transaction,
                WebGpuOriginalTextureReadbackCopiesFixture::budget(),
                copyQuota,
                diagnostic);
        assert(copies);
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Transaction::apply(transaction) && !Transaction::commit(transaction)
            && !transaction->validate() && transaction->journalEmpty());
        const auto& decision = copies->decisions()[0];
        assert(!WebGpuOriginalTextureReadbackCopiesFixture::Copies::validateCurrent(
            copies, 0u, decision, pair(states)->destination()->nativeResource()->ownerDevice));
        assert(copies->nativeStatus()
                == WebGpuOriginalTextureReadbackCopiesFixture::Copies::NativeStatus::Unrecorded
            && WebGpuOriginalTextureReadbackCopiesFixture::Copies::validateNativeCompletionOriginal(
                copies));
        assert(transaction->status() == Transaction::Status::Committed
            && !pair(states)->destination()->resultPublished());
        assert(!Transaction::validateCommittedReadbackDeclarationOriginal(transaction));
        auto forged = std::shared_ptr<const Transaction>(quota, transaction.get());
        assert(std::string_view(Transaction::validateCommittedReadbackDeclarationOriginal(forged))
            == "RHI-WEBGPU-STATE-TRANSACTION-OWNER");
        forged.reset();
        assert(WebGpuOriginalTextureReadbackCopiesFixture::Copies::
                validateNativeCompletedDeclarationOriginal(copies));
        assert(std::string_view(transaction->executionDiagnostic())
            == rhi_webgpu::webGpuGenericRecordingUsageDiagnostic);
        assert(std::string_view(Transaction::commit(transaction))
            == "RHI-WEBGPU-STATE-TRANSACTION-STATUS");
        std::pmr::set_default_resource(previous);
        auto& buffer = *pair(states)->destination()->nativeResource();
        WebGpuMapHandoffFixture::verifyMapStateSeparation(
            buffer.buffer, pair(states)->destination()->unpublishedResultCapacityBytes(), [&] {
                const auto count = core::defaultCpuAllocator().stats().allocationCount;
                assert(!Transaction::validateCommittedReadbackDeclarationOriginal(transaction));
                assert(transaction->validate() && copies->validate()
                    && WebGpuOriginalTextureReadbackCopiesFixture::Copies::
                        validateNativeCompletedDeclarationOriginal(copies));
                assert(core::defaultCpuAllocator().stats().allocationCount == count);
            });
        std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(!buffer.recordState(rhi::ResourceState::CopyDst));
        assert(std::string_view(transaction->validate())
            == "RHI-WEBGPU-STATE-TRANSACTION-COMMITTED-MISMATCH");
        assert(
            std::string_view(Transaction::validateCommittedReadbackDeclarationOriginal(transaction))
            == "RHI-WEBGPU-STATE-TRANSACTION-COMMITTED-MISMATCH");
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        assert(std::string_view(copies->validate())
            == "RHI-WEBGPU-STATE-TRANSACTION-COMMITTED-MISMATCH");
        copies.reset();
        assert(!copyQuota->liveBytes());
        transaction.reset();
        assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_READBACK_COMMITTED_DECLARATION_CPU_OK Prepared-Pending-forged-stale-reject actual-Pending-Mapped-declaration strict-access no-native-proof allocation0");
        std::puts(
            "BINDLESS_RHI_READBACK_CURRENT_COMMIT_OK cpu-metadata-only committed-revision-mismatch allocation0 unpublished");
    }

    static void verifyInvalidation(const std::shared_ptr<const States>& states)
    {
        auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto transaction = prepare(states, quota);
        assert(!Transaction::apply(transaction));
        transaction->invalidate();
        assert(transaction->status() == Transaction::Status::Invalidated
            && transaction->journalEmpty());
        for (const auto& original : states->states())
            assert(!original.source.control.active() && !original.source.control.pending());
        assert(
            std::string_view(transaction->validate()) == "RHI-WEBGPU-STATE-TRANSACTION-INVALIDATED"
            && pair(states)->validateReadyDeclaration());
        transaction.reset();
        assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_READBACK_CURRENT_INVALIDATION_OK terminal-controls3 rollback-no-resurrection exact-quota");
    }
#endif
};
} // namespace arp::test
