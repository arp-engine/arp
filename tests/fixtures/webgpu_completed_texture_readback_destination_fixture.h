#pragma once
#include "webgpu_completed_texture_readback_source_fixture.h"
#include "webgpu_error_scope_handoff_fixture.h"
#include "webgpu_original_readback_state_transaction_fixture.h"
#include "webgpu_original_texture_readback_declaration_fixture.h"
#include "webgpu_original_texture_readback_inputs_fixture.h"
#include "webgpu_original_texture_readback_pair_fixture.h"
#include "webgpu_original_texture_readback_projection_fixture.h"
#include "webgpu_original_texture_readback_recording_fixture.h"
#include "webgpu_original_texture_readback_source_fixture.h"
#include "webgpu_original_texture_readback_state_intent_fixture.h"

namespace arp::test {
class WebGpuCompletedTextureReadbackDestinationFixture final {
public:
    using SourceFixture = WebGpuCompletedTextureReadbackSourceFixture;
    using View = SourceFixture::View;
    using Destination = rhi_webgpu::WebGpuOriginalTextureReadbackDestination;
    using Budget = rhi_webgpu::WebGpuCompletedTextureReadbackDestinationBudget;
    using Promises = WebGpuErrorScopeHandoffFixture<2u>;
    static constexpr std::uint64_t cpuBytes = 65536u, bufferBytes = 1536u;

    static Budget budget()
    {
        return {1u,
            true,
            1u,
            1696u,
            {1u,
                true,
                5u,
                Destination::callbackCount,
                2896u,
                bufferBytes,
                cpuBytes,
                cpuBytes,
                cpuBytes}};
    }

    class Context final {
    public:
        SourceFixture::Context authoring;
        std::shared_ptr<const SourceFixture::Source> source;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> quotas;

        explicit Context(const std::shared_ptr<const View>& view) : authoring(view)
        {
            for (auto& q : quotas)
                q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            const char* diagnostic = nullptr;
            source = authoring.prepare(view, SourceFixture::budget(), diagnostic);
            assert(source);
        }

        auto prepare(const Budget& b, const char*& diagnostic)
        {
            return Destination::prepareCompletedOriginal(
                source, b, quotas[0], quotas[1], quotas[2], diagnostic);
        }

        void coldEmpty() const
        {
            for (const auto& q : quotas)
                assert(!q->liveBytes());
        }

        void retire()
        {
            source.reset();
            authoring.retire();
            coldEmpty();
        }
    };

    static void verifyContract()
    {
        Budget b;
        assert(std::string_view(Destination::validateBudget(b))
            == "RHI-READBACK-COMPLETED-DESTINATION-VERSION");
        b.contractVersion = 1u;
        assert(std::string_view(Destination::validateBudget(b))
            == "RHI-READBACK-COMPLETED-DESTINATION-OPT-IN");
        b.prototypeOptIn = true;
        assert(std::string_view(Destination::validateBudget(b))
            == "RHI-READBACK-COMPLETED-DESTINATION-BUDGET");
        b = budget();
        assert(!Destination::validateBudget(b));
        auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* diagnostic = nullptr;
        assert(!Destination::prepareCompletedOriginal({}, b, quota, quota, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-READBACK-DESTINATION-SOURCE-OWNER"
            && !quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_COMPLETED_READBACK_DESTINATION_CONTRACT_OK separate-profile complete-native-content-budgets creation-only");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    template <class Inspect>
    static void withInitialReadbackStates(const std::shared_ptr<const View>& view, Inspect inspect)
    {
        Context c(view);
        const char* diagnostic = nullptr;
        auto destination = c.prepare(budget(), diagnostic);
        assert(destination);
        assert(!Destination::initialize(destination));
        waitReady(destination);
        auto pairQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto pair = WebGpuOriginalTextureReadbackPairFixture::Pair::prepareOriginal(
            destination, WebGpuOriginalTextureReadbackPairFixture::budget(), pairQuota, diagnostic);
        assert(pair);
        WebGpuOriginalTextureReadbackStateIntentFixture::Quotas quotas;
        for (auto& q : quotas)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto states = WebGpuOriginalTextureReadbackStateIntentFixture::prepare(pair,
            WebGpuOriginalTextureReadbackStateIntentFixture::barrier(pair),
            quotas,
            diagnostic);
        assert(states);
        inspect(states);
        states.reset();
        pair.reset();
        assert(!pairQuota->liveBytes());
        for (const auto& q : quotas)
            assert(!q->liveBytes());
        destination.reset();
        c.retire();
    }

    static void verifyCurrent(const std::shared_ptr<const View>& view)
    {
        withInitialReadbackStates(
            view, WebGpuOriginalReadbackStateTransactionFixture::verifyColdAndPending);
        withInitialReadbackStates(
            view, WebGpuOriginalReadbackStateTransactionFixture::verifyCommit);
    }

    static void verifyActual(const std::shared_ptr<const View>& view)
    {
        Context c(view);
        auto b = budget();
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return c.prepare(b, diagnostic);
        };
        std::array<std::uint64_t, 3> peaks{};
        for (std::size_t i = 0u; i < peaks.size(); ++i) {
            CpuQuotaFactoryFixture pressure(c.quotas[i]);
            peaks[i] = pressure.measure(factory);
            pressure.verify(peaks[i], factory);
            c.coldEmpty();
            {
                core::CpuMemoryQuotaLease full(c.quotas[i], cpuBytes);
                assert(full.charged() && !factory());
            }
            c.coldEmpty();
        }
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !c.source->validate());
            c.coldEmpty();
        };
        --b.destination.maxSourceOwners;
        reject("RHI-READBACK-DESTINATION-CAPACITY");
        b = budget();
        --b.destination.maxRetainedSourceBytes;
        reject("RHI-READBACK-DESTINATION-CAPACITY");
        b = budget();
        --b.maxRetainedContentBytes;
        reject("RHI-READBACK-COMPLETED-DESTINATION-CONTENT-CAPACITY");
        b = budget();
        b.maxCompletedGraphs = 0u;
        reject("RHI-READBACK-COMPLETED-DESTINATION-BUDGET");
        b = budget();
        --b.destination.maxGpuBufferBytes;
        reject("RHI-READBACK-DESTINATION-CAPACITY");
        b = budget();
        --b.destination.maxCpuResourceBytes;
        reject("RHI-READBACK-DESTINATION-QUOTA");
        b = budget();
        auto source = c.source;
        c.source = std::shared_ptr<const SourceFixture::Source>(c.quotas[0], source.get());
        assert(
            !factory() && std::string_view(diagnostic) == "RHI-READBACK-DESTINATION-SOURCE-OWNER");
        c.source = source;
        source.reset();
        assert(!Destination::prepareOriginal(
                   c.source, b.destination, c.quotas[0], c.quotas[1], c.quotas[2], diagnostic)
            && std::string_view(diagnostic) == "RHI-READBACK-DESTINATION-SOURCE-PROFILE");
        {
            WebGpuOriginalTextureReadbackSourceFixture::Context ordinary;
            ordinary.makeTexture(true);
            auto ordinarySource = ordinary.prepare({1u, true, 3u, 1u, 16u, cpuBytes}, diagnostic);
            assert(ordinarySource);
            assert(!Destination::prepareCompletedOriginal(
                       ordinarySource, b, c.quotas[0], c.quotas[1], c.quotas[2], diagnostic)
                && std::string_view(diagnostic) == "RHI-READBACK-DESTINATION-SOURCE-PROFILE");
            ordinarySource.reset();
            ordinary.retire();
        }
        auto object = factory();
        assert(object && !object->resultPublished()
            && object->memoryStats().allocatedBytes == bufferBytes);
        WebGpuOriginalTextureReadbackPairFixture::verifyNotReady(
            object, "RHI-READBACK-DESTINATION-UNINITIALIZED");
        Promises::holdScopes();
        assert(!Destination::initialize(object));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        assert(object->status() == Destination::Status::Pending && !object->resultPublished());
        WebGpuOriginalTextureReadbackPairFixture::verifyNotReady(
            object, "RHI-READBACK-DESTINATION-PENDING");
        Promises::releaseScopes();
        waitReady(object);
        auto pair = WebGpuOriginalTextureReadbackPairFixture::verifyActual(object);
        auto graph = WebGpuOriginalTextureReadbackProjectionFixture::verifyActual(pair);
        WebGpuOriginalTextureReadbackInputsFixture::Quotas inputQuotas;
        for (auto& q : inputQuotas)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto states = WebGpuOriginalTextureReadbackInputsFixture::prepare(graph, inputQuotas, true);
        auto rejectedQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        assert(!WebGpuOriginalReadbackStateTransactionFixture::Transaction::prepareReadbackOriginal(
                   states,
                   WebGpuOriginalReadbackStateTransactionFixture::budget(),
                   rejectedQuota,
                   diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-READBACK-TRANSACTION-STREAM"
            && !rejectedQuota->liveBytes());
        WebGpuOriginalTextureReadbackStateIntentFixture::Quotas intentQuotas;
        for (auto& q : intentQuotas)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto intentStates =
            WebGpuOriginalTextureReadbackStateIntentFixture::verifyActual(pair, intentQuotas);
        WebGpuOriginalTextureReadbackDeclarationFixture::verifyMap(pair);
        assert(!view->completedRegistry()->find(object->resourceId())
            && object->source()->retainedNativeAllocationBytes() == 2896u
            && object->source()->retainedContentBytes() == 1696u);
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!object->validate()
            && std::string_view(object->executionDiagnostic())
                == "RHI-READBACK-DESTINATION-EXECUTION-UNPROVEN");
        auto& native = *object->nativeResource();
        const auto bytes = native.sizeBytes;
        ++native.sizeBytes;
        assert(object->validate());
        native.sizeBytes = bytes;
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        object.reset();
        // Creation callbacks must revalidate the exact completed source state.
        object = factory();
        assert(object);
        Promises::holdScopes();
        assert(!Destination::initialize(object));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        WebGpuOriginalTextureReadbackDeclarationFixture::verifyPendingAndRollback(pair);
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return object->creationScopesObserved();
        }));
        assert(object->status() == Destination::Status::Rejected && !object->resultPublished());
        assert(std::string_view(pair->validate()) == "RHI-READBACK-SOURCE-STATE-STALE"
            && std::string_view(graph->validate()) == "RHI-READBACK-SOURCE-STATE-STALE");
        assert(std::string_view(states->validate()) == "RHI-READBACK-SOURCE-STATE-STALE");
        states.reset();
        for (const auto& q : inputQuotas)
            assert(!q->liveBytes());
        assert(std::string_view(intentStates->validate()) == "RHI-READBACK-SOURCE-STATE-STALE");
        intentStates.reset();
        for (const auto& q : intentQuotas)
            assert(!q->liveBytes());
        WebGpuOriginalTextureReadbackDeclarationFixture::verifyInvalidation(pair);
        graph.reset();
        pair.reset();
        object.reset();
        c.retire();
        std::printf(
            "BINDLESS_RHI_COMPLETED_READBACK_DESTINATION_NATIVE_OK %llu %llu %llu graph1 owners5 native2896 content1696 gpu1536 result1536 scopes3 profiles-exclusive unpublished\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]),
            static_cast<unsigned long long>(peaks[2]));
    }

    static void waitReady(const std::shared_ptr<Destination>& object)
    {
        assert(WebGpuAsyncWaitFixture::until([&] {
            return object->status() != Destination::Status::Pending;
        }));
        assert(object->status() == Destination::Status::CreationReady
            && object->creationScopesObserved() && !object->validate());
    }

    static void prepareActualLoss(const std::shared_ptr<const View>& view)
    {
        for (std::size_t i = 0u; i < lossContexts_.size(); ++i) {
            lossContexts_[i] = core::makeUnique<Context>(view);
            const char* diagnostic = nullptr;
            lossObjects_[i] = lossContexts_[i]->prepare(budget(), diagnostic);
            assert(lossObjects_[i]);
        }
        assert(!Destination::initialize(lossObjects_[0]));
        waitReady(lossObjects_[0]);
        lossPairQuota_ = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* diagnostic = nullptr;
        lossPair_ = WebGpuOriginalTextureReadbackPairFixture::Pair::prepareOriginal(lossObjects_[0],
            WebGpuOriginalTextureReadbackPairFixture::budget(),
            lossPairQuota_,
            diagnostic);
        assert(lossPair_);
        lossGraphQuota_ = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        lossGraph_ = WebGpuOriginalTextureReadbackProjectionFixture::prepare(lossPair_,
            WebGpuOriginalTextureReadbackProjectionFixture::budget(),
            lossGraphQuota_,
            diagnostic);
        assert(lossGraph_);
        for (auto& q : lossInputQuotas_)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        lossStates_ = WebGpuOriginalTextureReadbackInputsFixture::prepare(
            lossGraph_, lossInputQuotas_, false);
        for (auto& q : lossIntentQuotas_)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        lossIntentStates_ = WebGpuOriginalTextureReadbackStateIntentFixture::prepare(lossPair_,
            WebGpuOriginalTextureReadbackStateIntentFixture::barrier(lossPair_),
            lossIntentQuotas_,
            diagnostic);
        assert(lossIntentStates_
            && !rhi_webgpu::WebGpuOriginalTransferStateValidation::validate(lossIntentStates_, 0u)
                .diagnostic);
        Promises::holdScopes();
        assert(!Destination::initialize(lossObjects_[1]));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        assert(lossObjects_[1]->status() == Destination::Status::Pending
            && lossObjects_[2]->status() == Destination::Status::Prepared);
        for (auto& c : lossContexts_)
            c->source.reset();
    }

    static void verifyActualDeviceLoss()
    {
        WebGpuOriginalTextureReadbackRecordingFixture::verifyActualLoss();
        using Pair = WebGpuOriginalTextureReadbackPairFixture::Pair;
        Pair::Mip output, sentinel;
        sentinel.bufferOffsetBytes = UINT64_MAX;
        output = sentinel;
        WebGpuOriginalTextureReadbackDeclarationFixture::verifyLoss(lossPair_);
        using Copies = WebGpuOriginalTextureReadbackCopiesFixture::Copies;
        assert(std::string_view(lossCopies_->validate()) == "RHI-READBACK-DESTINATION-DEVICE-LOST");
        assert(std::string_view(Copies::validateCurrent(lossCopies_,
                   0u,
                   lossCopies_->decisions()[0],
                   lossPair_->destination()->nativeResource()->ownerDevice))
            == "RHI-READBACK-DESTINATION-DEVICE-LOST");
        assert(WebGpuOriginalReadbackStateTransactionFixture::Transaction::mapReadbackMipCurrent(
                   lossCopies_->transaction(),
                   lossPair_->layout()->inputOwner(),
                   0u,
                   lossPair_->layout()->mipCopies()[0],
                   output)
            && output.matches(sentinel));
        lossCopies_.reset();
        assert(!lossCopyQuota_->liveBytes());
        lossCopyQuota_.reset();
        for (auto& transaction : lossCurrentTransactions_) {
            assert(std::string_view(transaction->validate())
                == "RHI-READBACK-DESTINATION-DEVICE-LOST");
            transaction->invalidate();
            assert(transaction->status()
                    == WebGpuOriginalReadbackStateTransactionFixture::Transaction::Status::
                        Invalidated
                && transaction->journalEmpty());
            transaction.reset();
        }
        for (const auto& original : lossIntentStates_->states())
            assert(!original.source.control.active() && !original.source.control.pending());
        for (const auto& q : lossCurrentQuotas_)
            assert(!q->liveBytes());
        lossCurrentQuotas_ = {};
        assert(std::string_view(lossIntentStates_->validate())
            == "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE-LOST");
        lossIntentStates_.reset();
        for (const auto& q : lossIntentQuotas_)
            assert(!q->liveBytes());
        lossIntentQuotas_ = {};
        assert(std::string_view(lossPair_->validate()) == "RHI-READBACK-DESTINATION-DEVICE-LOST");
        assert(std::string_view(Pair::mapMipOriginal(
                   lossPair_, lossPair_->layout()->inputOwner(), 0u, {}, nullptr, output))
                == "RHI-READBACK-DESTINATION-DEVICE-LOST"
            && output.matches(sentinel));
        assert(std::string_view(lossGraph_->validate())
            == "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE-LOST");
        assert(std::string_view(lossStates_->validate())
            == "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE-LOST");
        lossStates_.reset();
        for (const auto& q : lossInputQuotas_)
            assert(!q->liveBytes());
        lossInputQuotas_ = {};
        lossGraph_.reset();
        assert(!lossGraphQuota_->liveBytes());
        lossGraphQuota_.reset();
        lossPair_.reset();
        assert(!lossPairQuota_->liveBytes());
        lossPairQuota_.reset();
        for (const auto& object : lossObjects_) {
            assert(
                object->status() == Destination::Status::DeviceLost && !object->resultPublished());
            assert(std::string_view(object->validate()) == "RHI-READBACK-DESTINATION-DEVICE-LOST");
        }
        std::weak_ptr<Destination> pending = lossObjects_[1];
        for (auto& object : lossObjects_)
            object.reset();
        assert(!pending.expired());
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return pending.expired();
        }));
        pending.reset();
        for (auto& c : lossContexts_) {
            c->retire();
            c.reset();
        }
        std::puts(
            "BINDLESS_RHI_COMPLETED_READBACK_DESTINATION_LOSS_OK prepared-pending-ready full-caller-drop scopes-retire exact-native-cpu unpublished");
        std::puts(
            "BINDLESS_RHI_READBACK_PAIR_LOSS_OK actual-lost output-preserved original-graph exact-retire no-emit-proof");
        std::puts(
            "BINDLESS_RHI_READBACK_PROJECTION_LOSS_OK actual-device-loss original-pair graph-retire recording-closed");
        std::puts(
            "BINDLESS_RHI_READBACK_INPUTS_LOSS_OK actual-device-loss full-input state3 exact-graph-retire recording-closed");
        std::puts(
            "BINDLESS_RHI_READBACK_STATE_INTENT_LOSS_OK actual-device-loss original-state3 exact-retire no-proof");
        std::puts(
            "BINDLESS_RHI_READBACK_CURRENT_LOSS_OK actual-prepared-pending-device-loss controls3 terminal-retire no-proof");
        std::puts(
            "BINDLESS_RHI_READBACK_COPIES_LOSS_OK actual-device-loss current-output-preserved original-graph exact-retire");
    }

    static void activateCurrentLoss(
        const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& owner)
    {
        WebGpuOriginalTextureReadbackRecordingFixture::prepareActualLoss(lossIntentStates_, owner);
        for (std::size_t i = 0u; i < lossCurrentQuotas_.size(); ++i) {
            lossCurrentQuotas_[i] = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            lossCurrentTransactions_[i] = WebGpuOriginalReadbackStateTransactionFixture::prepare(
                lossIntentStates_, lossCurrentQuotas_[i]);
        }
        lossCopyQuota_ = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* diagnostic = nullptr;
        lossCopies_ = WebGpuOriginalTextureReadbackCopiesFixture::Copies::prepareOriginal(
            lossCurrentTransactions_[1],
            WebGpuOriginalTextureReadbackCopiesFixture::budget(),
            lossCopyQuota_,
            diagnostic);
        assert(lossCopies_);
        assert(!WebGpuOriginalReadbackStateTransactionFixture::Transaction::apply(
            lossCurrentTransactions_[1]));
    }

private:
    static inline std::array<core::UniquePtr<Context>, 3> lossContexts_;
    static inline std::array<std::shared_ptr<Destination>, 3> lossObjects_;
    static inline std::shared_ptr<const WebGpuOriginalTextureReadbackPairFixture::Pair> lossPair_;
    static inline std::shared_ptr<core::CpuMemoryQuota> lossPairQuota_;
    static inline std::shared_ptr<const WebGpuOriginalTextureReadbackProjectionFixture::Graph>
        lossGraph_;
    static inline std::shared_ptr<core::CpuMemoryQuota> lossGraphQuota_;
    static inline WebGpuOriginalTextureReadbackInputsFixture::Quotas lossInputQuotas_;
    static inline std::shared_ptr<const WebGpuOriginalTextureReadbackInputsFixture::States>
        lossStates_;
    static inline WebGpuOriginalTextureReadbackStateIntentFixture::Quotas lossIntentQuotas_;
    static inline std::shared_ptr<const WebGpuOriginalTextureReadbackStateIntentFixture::States>
        lossIntentStates_;
    static inline std::array<std::shared_ptr<core::CpuMemoryQuota>, 2> lossCurrentQuotas_;
    static inline std::
        array<std::shared_ptr<WebGpuOriginalReadbackStateTransactionFixture::Transaction>, 2>
            lossCurrentTransactions_;
    static inline std::shared_ptr<core::CpuMemoryQuota> lossCopyQuota_;
    static inline std::shared_ptr<const WebGpuOriginalTextureReadbackCopiesFixture::Copies>
        lossCopies_;
#endif
};
} // namespace arp::test
