#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/transfer_upload_content_transaction.h"
#include "texture_upload_packing_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <type_traits>

namespace arp::test {
class TextureUploadContentTransactionFixture final {
    template <class T>
    static auto exposesNativeTransitions(int) -> decltype(std::declval<T&>().markNativeSubmitted(),
        std::declval<T&>().markNativeCompleted(),
        std::true_type{});
    template <class T> static auto exposesNativeTransitions(...) -> std::false_type;
    using Source = rhi::TextureUploadContentSource;
    using Ledger = rhi::TextureUploadContentLedger;
    using Transaction = rhi::TextureUploadContentTransaction;
    using Domain = rhi::TextureUploadContentDomainValidation;
    using Quotas = std::array<std::shared_ptr<core::CpuMemoryQuota>, 8>;
    static constexpr std::uint64_t cpuBytes = 65536u;
    static constexpr std::uint64_t retainedBytes = 1696u;

    static Quotas quotas()
    {
        Quotas result;
        for (auto& q : result)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        return result;
    }

    static auto packing(const rhi::UploadTextureDesc& d,
        const rhi::TextureDesc& t,
        const Quotas& q,
        const char*& diagnostic)
    {
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 5> packingQuotas;
        for (std::size_t i = 0u; i < packingQuotas.size(); ++i)
            packingQuotas[i] = q[i];
        return TextureUploadPackingFixture::prepareContentPacking(d, t, packingQuotas, diagnostic);
    }

    static auto source(const std::shared_ptr<const rhi::TextureUploadPacking>& p,
        const Quotas& q,
        Source::AuthoringClaim claim,
        const char*& diagnostic)
    {
        return Source::prepareOriginal(p, claim, {1u, true, 4096u, cpuBytes}, q[5], diagnostic);
    }

    static auto ledger(
        const rhi::TextureDesc& d, const Quotas& q, Ledger::Budget budget, const char*& diagnostic)
    {
        return Ledger::prepareOriginal({41u}, d, budget, q[6], diagnostic);
    }

    static void retired(const Quotas& q)
    {
        for (const auto& quota : q)
            assert(!quota->liveBytes());
    }

public:
    static void verify()
    {
        static_assert(!decltype(exposesNativeTransitions<Transaction>(0))::value);
        static_assert(!std::is_default_constructible_v<Ledger::Expectation>);
        std::array<std::uint8_t, 160> pixels{};
        for (std::size_t i = 0u; i < pixels.size(); ++i)
            pixels[i] = static_cast<std::uint8_t>(i + 1u);
        auto d = TextureUploadPackingFixture::contentInput(pixels.data());
        auto t = TextureUploadPackingFixture::contentDestination();
        auto q = quotas();
        const char* diagnostic = nullptr;
        auto p = packing(d, t, q, diagnostic);
        assert(p);
        Source::AuthoringClaim claim{1u, {41u}, 59u};
        Source::Budget sourceBudget{1u, true, retainedBytes, cpuBytes};
        const auto sourceFactory = [&] {
            return Source::prepareOriginal(p, claim, sourceBudget, q[5], diagnostic);
        };
        CpuQuotaFactoryFixture sourceAdmission(q[5]);
        const auto sourcePeak = sourceAdmission.measure(sourceFactory);
        sourceAdmission.verify(sourcePeak, sourceFactory);
        {
            core::CpuMemoryQuotaLease exhausted(q[5], cpuBytes);
            assert(exhausted.charged() && !sourceFactory());
        }
        assert(!q[5]->liveBytes());
        const auto rejectSource = [&](const char* id) {
            assert(!sourceFactory() && diagnostic && std::string_view(diagnostic) == id
                && !q[5]->liveBytes());
        };
        --sourceBudget.maxRetainedSourceBytes;
        rejectSource("RHI-TEXTURE-CONTENT-SOURCE-BYTE-CAPACITY");
        ++sourceBudget.maxRetainedSourceBytes;
        sourceBudget.contractVersion = 0u;
        rejectSource("RHI-TEXTURE-CONTENT-SOURCE-VERSION");
        sourceBudget.contractVersion = 1u;
        sourceBudget.prototypeOptIn = false;
        rejectSource("RHI-TEXTURE-CONTENT-SOURCE-OPT-IN");
        sourceBudget.prototypeOptIn = true;
        sourceBudget.maxCpuMetadataBytes = 0u;
        rejectSource("RHI-TEXTURE-CONTENT-SOURCE-BUDGET");
        sourceBudget.maxCpuMetadataBytes = cpuBytes;
        --sourceBudget.maxCpuMetadataBytes;
        rejectSource("RHI-TEXTURE-CONTENT-SOURCE-QUOTA");
        ++sourceBudget.maxCpuMetadataBytes;
        claim.contractVersion = 0u;
        rejectSource("RHI-TEXTURE-CONTENT-SOURCE-AUTHORING-CLAIM");
        claim.contractVersion = 1u;
        claim.destination = {42u};
        rejectSource("RHI-TEXTURE-CONTENT-SOURCE-AUTHORING-CLAIM");
        claim.destination = {41u};
        for (const auto invalid :
            {std::uint64_t{0u}, std::uint64_t{60u}, std::uint64_t{UINT64_MAX}}) {
            claim.contentGeneration = invalid;
            rejectSource("RHI-TEXTURE-CONTENT-SOURCE-AUTHORING-CLAIM");
        }
        claim.contentGeneration = 59u;
        auto originalPacking = p;
        p = std::shared_ptr<const rhi::TextureUploadPacking>(q[3], p.get());
        rejectSource("RHI-TEXTURE-CONTENT-SOURCE-ORIGINAL-PACKING");
        p = originalPacking;
        originalPacking.reset();
        auto s = sourceFactory();
        assert(s && s->hasQuotaBoundSharedOwner(s) && s->retainedBytes() == retainedBytes);
        assert(!Source::prepareOriginal(p, claim, sourceBudget, {}, diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-SOURCE-QUOTA");
        assert(!Source::prepareOriginal({}, claim, sourceBudget, q[5], diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-SOURCE-ORIGINAL-PACKING");
        Ledger::Budget ledgerBudget{1u, true, 2u, 2u, 2880u, 2u * retainedBytes, cpuBytes};
        const auto ledgerFactory = [&] {
            auto l = ledger(t, q, ledgerBudget, diagnostic);
            if (!l)
                return l;
            if (!Ledger::prepareDomainOriginal(l, s, diagnostic))
                return std::shared_ptr<Ledger>{};
            return l;
        };
        CpuQuotaFactoryFixture ledgerAdmission(q[6]);
        const auto ledgerPeak = ledgerAdmission.measure(ledgerFactory);
        ledgerAdmission.verify(ledgerPeak, ledgerFactory);
        {
            core::CpuMemoryQuotaLease exhausted(q[6], cpuBytes);
            assert(exhausted.charged() && !ledgerFactory());
        }
        assert(!q[6]->liveBytes());
        const auto rejectLedger = [&](const char* id) {
            assert(!ledgerFactory() && diagnostic && std::string_view(diagnostic) == id
                && !q[6]->liveBytes());
        };
        ledgerBudget.contractVersion = 0u;
        rejectLedger("RHI-TEXTURE-CONTENT-LEDGER-VERSION");
        ledgerBudget.contractVersion = 1u;
        ledgerBudget.prototypeOptIn = false;
        rejectLedger("RHI-TEXTURE-CONTENT-LEDGER-OPT-IN");
        ledgerBudget.prototypeOptIn = true;
        ledgerBudget.maxDomains = 0u;
        rejectLedger("RHI-TEXTURE-CONTENT-LEDGER-BUDGET");
        ledgerBudget.maxDomains = 2u;
        ledgerBudget.maxTransactions = 0u;
        rejectLedger("RHI-TEXTURE-CONTENT-LEDGER-BUDGET");
        ledgerBudget.maxTransactions = 2u;
        --ledgerBudget.maxCpuMetadataBytes;
        rejectLedger("RHI-TEXTURE-CONTENT-LEDGER-QUOTA");
        ++ledgerBudget.maxCpuMetadataBytes;
        ledgerBudget.maxDomains = UINT32_MAX;
        rejectLedger("RHI-TEXTURE-CONTENT-LEDGER-ALLOCATION");
        ledgerBudget.maxDomains = 2u;
        ledgerBudget.maxRetainedDomainBytes = retainedBytes - 1u;
        rejectLedger("RHI-TEXTURE-CONTENT-DOMAIN-BYTE-CAPACITY");
        ledgerBudget.maxRetainedDomainBytes = 2880u;
        auto l = ledgerFactory();
        assert(l && l->domainCount() == 1u && l->retainedDomainBytes() == retainedBytes);
        assert(!Ledger::prepareOriginal({41u}, t, ledgerBudget, {}, diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-LEDGER-QUOTA");
        assert(!Ledger::prepareDomainOriginal({}, s, diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-LEDGER-ORIGINAL-OWNER");
        assert(!Ledger::prepareDomainOriginal(l, {}, diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-SOURCE-ORIGINAL-OWNER");
        auto expected = Ledger::prepareDomainOriginal(l, s, diagnostic);
        assert(expected && expected->state() == Ledger::ContentState::Uninitialized
            && !expected->contentGeneration());
        Transaction::Budget transactionBudget{1u, true, cpuBytes};
        assert(!Transaction::prepareOriginal(l, s, *expected, transactionBudget, {}, diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-TRANSACTION-QUOTA");
        const auto transactionFactory = [&] {
            return Transaction::prepareOriginal(
                l, s, *expected, transactionBudget, q[7], diagnostic);
        };
        CpuQuotaFactoryFixture transactionAdmission(q[7]);
        const auto transactionPeak = transactionAdmission.measure(transactionFactory);
        transactionAdmission.verify(transactionPeak, transactionFactory);
        {
            core::CpuMemoryQuotaLease exhausted(q[7], cpuBytes);
            assert(exhausted.charged() && !transactionFactory());
        }
        assert(!q[7]->liveBytes() && !l->liveTransactions() && !l->livePendingSourceBytes());
        const auto rejectTransaction = [&](const char* id) {
            assert(!transactionFactory() && diagnostic && std::string_view(diagnostic) == id);
        };
        transactionBudget.contractVersion = 0u;
        rejectTransaction("RHI-TEXTURE-CONTENT-TRANSACTION-VERSION");
        transactionBudget.contractVersion = 1u;
        transactionBudget.prototypeOptIn = false;
        rejectTransaction("RHI-TEXTURE-CONTENT-TRANSACTION-OPT-IN");
        transactionBudget.prototypeOptIn = true;
        --transactionBudget.maxCpuMetadataBytes;
        rejectTransaction("RHI-TEXTURE-CONTENT-TRANSACTION-QUOTA");
        ++transactionBudget.maxCpuMetadataBytes;
        auto forgedLedger = std::shared_ptr<Ledger>(q[6], l.get());
        assert(!Ledger::prepareDomainOriginal(forgedLedger, s, diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-LEDGER-ORIGINAL-OWNER");
        auto forgedSource = std::shared_ptr<const Source>(q[5], s.get());
        assert(!Ledger::prepareDomainOriginal(l, forgedSource, diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-SOURCE-ORIGINAL-OWNER");
        forgedLedger.reset();
        forgedSource.reset();
        auto foreign = ledgerFactory();
        auto foreignExpected = Ledger::prepareDomainOriginal(foreign, s, diagnostic);
        assert(foreignExpected);
        assert(!Transaction::prepareOriginal(
                   l, s, *foreignExpected, transactionBudget, q[7], diagnostic)
            && std::string_view(diagnostic)
                == "RHI-TEXTURE-CONTENT-TRANSACTION-FOREIGN-EXPECTATION");
        foreignExpected.reset();
        foreign.reset();
        auto first = transactionFactory(), second = transactionFactory();
        assert(
            first && second && l->liveTransactions() == 2u && l->livePendingSourceBytes() == 3392u);
        assert(first->matchesOriginalAdmission(l, s, *expected));
        {
            auto equalLedger = ledgerFactory();
            auto equalExpectation = Ledger::prepareDomainOriginal(equalLedger, s, diagnostic);
            assert(equalExpectation);
            assert(!first->matchesOriginalAdmission(equalLedger, s, *equalExpectation));
            auto forgedSource = std::shared_ptr<const Source>(q[5], s.get());
            assert(!first->matchesOriginalAdmission(l, forgedSource, *expected));
        }
        rejectTransaction("RHI-TEXTURE-CONTENT-TRANSACTION-COUNT-CAPACITY");
        assert(l->liveTransactions() == 2u && l->livePendingSourceBytes() == 3392u);
        auto forgedTransaction = std::shared_ptr<Transaction>(q[7], first.get());
        assert(std::string_view(Transaction::apply(forgedTransaction))
            == "RHI-TEXTURE-CONTENT-TRANSACTION-ORIGINAL-OWNER");
        forgedTransaction.reset();
        {
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Transaction::apply(first) && !first->journalEmpty()
                && first->status() == Transaction::Status::Pending && !first->validate());
            assert(first->matchesOriginalAdmission(l, s, *expected));
            assert(l->state(*p) == Ledger::ContentState::Proposed
                && std::string_view(Transaction::apply(second))
                    == "RHI-TEXTURE-CONTENT-DOMAIN-PENDING");
            assert(!Ledger::prepareDomainOriginal(l, s, diagnostic)
                && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-DOMAIN-PENDING");
            assert(std::string_view(first->executionDiagnostic())
                == "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN");
            first->abandon();
            assert(first->journalEmpty() && l->state(*p) == Ledger::ContentState::Uninitialized);
            assert(std::string_view(Transaction::apply(second))
                == "RHI-TEXTURE-CONTENT-TRANSACTION-STALE");
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            std::pmr::set_default_resource(previous);
        }
        first.reset();
        second.reset();
        assert(!l->liveTransactions() && !l->livePendingSourceBytes());
        assert(!transactionFactory()
            && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-TRANSACTION-STALE");
        expected.reset();
        verifyDomains(d, t, q, l, s);
        auto fresh = Ledger::prepareDomainOriginal(l, s, diagnostic);
        assert(fresh && fresh->internalRevision() > 1u);
        auto pending =
            Transaction::prepareOriginal(l, s, *fresh, transactionBudget, q[7], diagnostic);
        assert(pending && !Transaction::apply(pending));
        auto observed = l;
        pending.reset();
        assert(l->state(*p) == Ledger::ContentState::Uninitialized && !l->liveTransactions());
        observed.reset();
        fresh.reset();
        auto last = Ledger::prepareDomainOriginal(l, s, diagnostic);
        auto terminal =
            Transaction::prepareOriginal(l, s, *last, transactionBudget, q[7], diagnostic);
        assert(terminal && !Transaction::apply(terminal));
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        terminal->invalidate();
        terminal->abandon();
        assert(terminal->journalEmpty() && l->state(*p) == Ledger::ContentState::Invalidated
            && terminal->status() == Transaction::Status::Invalidated);
        assert(!Ledger::prepareDomainOriginal(l, s, diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-DOMAIN-INVALIDATED");
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        terminal.reset();
        last.reset();
        assert(std::string_view(l->executionDiagnostic())
            == "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN");
        l.reset();
        s.reset();
        p.reset();
        retired(q);
        verifyOpaqueVersionsAndByteCapacity(d, t, q);
        std::printf(
            "BINDLESS_RHI_TEXTURE_CONTENT_TRANSACTION_OK %llu %llu %llu domains2 mips2 layers2 opaque-version overlap stale rollback terminal gates-closed allocation0\n",
            static_cast<unsigned long long>(sourcePeak),
            static_cast<unsigned long long>(ledgerPeak),
            static_cast<unsigned long long>(transactionPeak));
    }

    static void prepareActualLoss()
    {
        lossQuotas_ = quotas();
        std::array<std::uint8_t, 160> bytes{};
        const char* diagnostic = nullptr;
        const auto d = TextureUploadPackingFixture::contentInput(bytes.data());
        const auto t = TextureUploadPackingFixture::contentDestination();
        const auto p = packing(d, t, lossQuotas_, diagnostic);
        const auto s = source(p, lossQuotas_, {1u, {41u}, 59u}, diagnostic);
        const auto l = ledger(t,
            lossQuotas_,
            {1u, true, 1u, 2u, retainedBytes, 2u * retainedBytes, cpuBytes},
            diagnostic);
        const auto expected = Ledger::prepareDomainOriginal(l, s, diagnostic);
        assert(expected);
        for (auto& tx : lossTransactions_) {
            tx = Transaction::prepareOriginal(
                l, s, *expected, {1u, true, cpuBytes}, lossQuotas_[7], diagnostic);
            assert(tx);
        }
        assert(!Transaction::apply(lossTransactions_[1]) && l->liveTransactions() == 2u
            && l->livePendingSourceBytes() == 3392u);
    }

    static void verifyActualDeviceLoss()
    {
        assert(
            rhi_webgpu::webGpuNativeDeviceLost() && lossTransactions_[0] && lossTransactions_[1]);
        auto l = lossTransactions_[1]->ledger();
        auto p = lossTransactions_[1]->source()->packing();
        assert(l->state(*p) == Ledger::ContentState::Proposed && !lossTransactions_[1]->validate());
        assert(std::string_view(lossTransactions_[1]->executionDiagnostic())
            == "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN");
        assert(lossTransactions_[1]->source()->claim().contentGeneration == 59u
            && p->inputOwner()->pixelBytes().size() == 160u);
        l->invalidate();
        for (auto& tx : lossTransactions_) {
            tx->abandon();
            assert(tx->journalEmpty());
            tx.reset();
        }
        assert(l->state(*p) == Ledger::ContentState::Invalidated && !l->liveTransactions()
            && !l->livePendingSourceBytes());
        l.reset();
        p.reset();
        retired(lossQuotas_);
        for (auto& q : lossQuotas_)
            q.reset();
        std::puts(
            "BINDLESS_RHI_TEXTURE_CONTENT_TRANSACTION_LOSS_OK prepared-pending cpu-only terminal rollback exact-retire");
    }

private:
    static void verifyDomains(const rhi::UploadTextureDesc& input,
        const rhi::TextureDesc& t,
        const Quotas& q,
        const std::shared_ptr<Ledger>& l,
        const std::shared_ptr<const Source>& original)
    {
        const char* diagnostic = nullptr;
        auto d = input;
        d.subresources.mipLevelCount = 1u;
        d.mipLayouts.resize(1u);
        auto p = packing(d, t, q, diagnostic);
        auto partial = source(p, q, {1u, {41u}, 59u}, diagnostic);
        assert(partial);
        assert(
            !Domain::same(*original->packing(), *p) && Domain::overlaps(*original->packing(), *p));
        assert(!Ledger::prepareDomainOriginal(l, partial, diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-DOMAIN-OVERLAP");
        partial.reset();
        p.reset();
        d = input;
        d.extent = {1u, 1u, 1u};
        d.mipLayouts[0].extent = d.extent;
        d.destinationOffset = {0u, 0u, 0u};
        p = packing(d, t, q, diagnostic);
        auto adjacent = source(p, q, {1u, {41u}, 59u}, diagnostic);
        assert(adjacent && adjacent->retainedBytes() == 1184u);
        assert(!l->state(*p) && !Domain::overlaps(*original->packing(), *p));
        auto domain = Ledger::prepareDomainOriginal(l, adjacent, diagnostic);
        assert(domain && l->domainCount() == 2u && l->retainedDomainBytes() == 2880u);
        domain.reset();
        d = input;
        d.arrayLayer = 0u;
        d.subresources.baseArrayLayer = 0u;
        d.subresources.arrayLayerCount = 1u;
        auto otherPacking = packing(d, t, q, diagnostic);
        auto other = source(otherPacking, q, {1u, {41u}, 59u}, diagnostic);
        assert(other && !Domain::overlaps(*original->packing(), *otherPacking));
        assert(!Ledger::prepareDomainOriginal(l, other, diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-DOMAIN-CAPACITY");
        auto limited = ledger(t, q, {1u, true, 2u, 1u, 2879u, retainedBytes, cpuBytes}, diagnostic);
        assert(limited);
        assert(Ledger::prepareDomainOriginal(limited, original, diagnostic));
        assert(!Ledger::prepareDomainOriginal(limited, adjacent, diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-DOMAIN-BYTE-CAPACITY"
            && limited->domainCount() == 1u);
        auto mismatch = t;
        ++mismatch.extent.width;
        auto wrong = ledger(
            mismatch, q, {1u, true, 1u, 1u, retainedBytes, retainedBytes, cpuBytes}, diagnostic);
        assert(!Ledger::prepareDomainOriginal(wrong, original, diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-DESTINATION-MISMATCH");
        verifyVolumeAndBlocks(input, t, q);
    }

    static void verifyVolumeAndBlocks(
        const rhi::UploadTextureDesc& input, rhi::TextureDesc t, const Quotas& q)
    {
        const char* diagnostic = nullptr;
        auto d = input;
        d.mipLayouts.clear();
        d.mipLevel = 0u;
        d.arrayLayer = 0u;
        d.subresources = {0u, 1u, 0u, 1u, rhi::TextureAspect::Color};
        d.extent = {2u, 2u, 1u};
        d.destinationOffset = {0u, 0u, 0u};
        d.bytesPerRow = 8u;
        d.rowsPerImage = 2u;
        t.extent = {4u, 4u, 4u};
        t.mipLevelCount = 1u;
        t.arrayLayerCount = 1u;
        const auto first = packing(d, t, q, diagnostic);
        assert(first);
        d.destinationOffset.depth = 1u;
        const auto adjacent = packing(d, t, q, diagnostic);
        assert(adjacent && !Domain::overlaps(*first, *adjacent));
        d.extent.depth = 2u;
        d.destinationOffset.depth = 0u;
        const auto overlapping = packing(d, t, q, diagnostic);
        assert(overlapping && Domain::overlaps(*overlapping, *adjacent));
        t.extent = {10u, 6u, 1u};
        t.format = d.format = rhi::TextureFormat::Bc1Unorm;
        d.extent = {4u, 6u, 1u};
        d.destinationOffset = {0u, 0u, 0u};
        d.bytesPerRow = 8u;
        d.rowsPerImage = 2u;
        const auto blockFirst = packing(d, t, q, diagnostic);
        assert(blockFirst);
        d.extent.width = 6u;
        d.destinationOffset.width = 4u;
        d.bytesPerRow = 16u;
        const auto blockAdjacent = packing(d, t, q, diagnostic);
        assert(blockAdjacent && !Domain::overlaps(*blockFirst, *blockAdjacent));
        d.extent.width = 10u;
        d.destinationOffset.width = 0u;
        d.bytesPerRow = 24u;
        const auto blockAll = packing(d, t, q, diagnostic);
        assert(blockAll && Domain::overlaps(*blockAll, *blockAdjacent));
    }

    static void verifyOpaqueVersionsAndByteCapacity(
        rhi::UploadTextureDesc d, const rhi::TextureDesc& t, const Quotas& q)
    {
        const char* diagnostic = nullptr;
        d.resourceGeneration = UINT64_MAX;
        auto p = packing(d, t, q, diagnostic);
        auto s = source(p, q, {1u, {41u}, UINT64_MAX}, diagnostic);
        assert(s);
        auto l =
            ledger(t, q, {1u, true, 1u, 2u, retainedBytes, retainedBytes, cpuBytes}, diagnostic);
        auto expected = Ledger::prepareDomainOriginal(l, s, diagnostic);
        assert(expected && expected->internalRevision() == 1u);
        auto tx =
            Transaction::prepareOriginal(l, s, *expected, {1u, true, cpuBytes}, q[7], diagnostic);
        assert(tx && l->liveTransactions() == 1u);
        assert(
            !Transaction::prepareOriginal(l, s, *expected, {1u, true, cpuBytes}, q[7], diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-TRANSACTION-BYTE-CAPACITY");
        assert(l->liveTransactions() == 1u && l->livePendingSourceBytes() == retainedBytes);
        auto limited = ledger(
            t, q, {1u, true, 1u, 1u, retainedBytes, retainedBytes - 1u, cpuBytes}, diagnostic);
        auto limitedExpected = Ledger::prepareDomainOriginal(limited, s, diagnostic);
        assert(limitedExpected);
        assert(!Transaction::prepareOriginal(
                   limited, s, *limitedExpected, {1u, true, cpuBytes}, q[7], diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-TRANSACTION-BYTE-CAPACITY");
        assert(!limited->liveTransactions() && !limited->livePendingSourceBytes());
        limitedExpected.reset();
        limited.reset();
        assert(!Transaction::apply(tx) && tx->source()->claim().contentGeneration == UINT64_MAX);
        assert(std::string_view(tx->executionDiagnostic())
            == "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN");
        std::weak_ptr<Ledger> observed = l;
        p.reset();
        s.reset();
        l.reset();
        expected.reset();
        assert(!observed.expired()
            && tx->source()->packing()->inputOwner()->pixelBytes().size() == 160u);
        tx.reset();
        assert(observed.expired());
        observed.reset();
        retired(q);
    }

    inline static Quotas lossQuotas_;
    inline static std::array<std::shared_ptr<Transaction>, 2> lossTransactions_;
};
} // namespace arp::test
