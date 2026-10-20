#pragma once
#include "core/atomic_capacity.h"
#include "core/shared_owner_projection.h"
#include "core/versioned_mutation_value.h"
#include "rhi/frontend/transfer_upload_packing.h"

namespace arp::rhi_webgpu {
class WebGpuOriginalTextureUploadTransaction;
}

namespace arp::rhi {
// OriginalTextureUploadContentTransaction/1. CPU intent, not native content.
class TextureUploadContentSource final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class TextureUploadContentSource;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    struct AuthoringClaim {
        std::uint32_t contractVersion = 0u;
        GpuResourceId destination;
        std::uint64_t contentGeneration = 0u;
    };

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxRetainedSourceBytes = 0u, maxCpuMetadataBytes = 0u;
    };

    TextureUploadContentSource(ConstructionKey,
        std::shared_ptr<const TextureUploadPacking> packing,
        AuthoringClaim claim,
        std::uint64_t retained)
        : packing_(std::move(packing)), claim_(claim), retained_(retained)
    {}

    static std::shared_ptr<const TextureUploadContentSource> prepareOriginal(
        std::shared_ptr<const TextureUploadPacking> packing,
        AuthoringClaim claim,
        Budget budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = nullptr;
        if (budget.contractVersion != 1u) {
            diagnostic = "RHI-TEXTURE-CONTENT-SOURCE-VERSION";
            return {};
        }
        if (!budget.prototypeOptIn) {
            diagnostic = "RHI-TEXTURE-CONTENT-SOURCE-OPT-IN";
            return {};
        }
        if (!budget.maxRetainedSourceBytes || !budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-TEXTURE-CONTENT-SOURCE-BUDGET";
            return {};
        }
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-TEXTURE-CONTENT-SOURCE-QUOTA";
            return {};
        }
        if (!packing || !packing->hasQuotaBoundSharedOwner(packing)) {
            diagnostic = "RHI-TEXTURE-CONTENT-SOURCE-ORIGINAL-PACKING";
            return {};
        }
        if (claim.contractVersion != 1u || !claim.destination.isValid() || !claim.contentGeneration
            || claim.destination != packing->inputOwner()->scalarFields().destination()
            || claim.contentGeneration
                != packing->inputOwner()->scalarFields().resourceGeneration()) {
            diagnostic = "RHI-TEXTURE-CONTENT-SOURCE-AUTHORING-CLAIM";
            return {};
        }
        const std::uint64_t originalBytes = packing->inputOwner()->pixelBytes().size(),
                            packedBytes = packing->packedBytes().size();
        if (!core::MemoryBudgetLedger::canAdd(originalBytes, packedBytes)
            || originalBytes + packedBytes > budget.maxRetainedSourceBytes) {
            diagnostic = "RHI-TEXTURE-CONTENT-SOURCE-BYTE-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return shareOriginal(core::makeUnique<TextureUploadContentSource>(
                ConstructionKey{}, std::move(packing), claim, originalBytes + packedBytes));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-TEXTURE-CONTENT-SOURCE-ALLOCATION";
            return {};
        }
    }

    const auto& packing() const noexcept { return packing_; }

    AuthoringClaim claim() const noexcept { return claim_; }

    std::uint64_t retainedBytes() const noexcept { return retained_; }

    const char* executionDiagnostic() const noexcept
    {
        return "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN";
    }

private:
    const std::shared_ptr<const TextureUploadPacking> packing_;
    const AuthoringClaim claim_;
    const std::uint64_t retained_;
};

// Destination region mathematics shared by the content ledger and later native
// binding. Mip/source layouts remain ordered; no interval list is synthesized.
class TextureUploadContentDomainValidation final {
public:
    static bool same(const TextureUploadPacking& a, const TextureUploadPacking& b) noexcept
    {
        if (a.inputOwner()->scalarFields().destination()
                != b.inputOwner()->scalarFields().destination()
            || a.destinationLayout().format != b.destinationLayout().format
            || a.mipLayouts().size() != b.mipLayouts().size())
            return false;
        for (std::size_t i = 0u; i < a.mipLayouts().size(); ++i) {
            const auto& x = a.mipLayouts()[i];
            const auto& y = b.mipLayouts()[i];
            if (x.range != y.range || x.extent != y.extent || x.origin != y.origin)
                return false;
        }
        return true;
    }

    static bool overlaps(const TextureUploadPacking& a, const TextureUploadPacking& b) noexcept
    {
        if (a.inputOwner()->scalarFields().destination()
            != b.inputOwner()->scalarFields().destination())
            return false;
        // Unknown cross-format aliasing is conservatively conflicting. A ledger
        // first enforces exact destination format/layout equality.
        if (a.destinationLayout().format != b.destinationLayout().format)
            return true;
        const auto block = blockExtent(a.destinationLayout().format);
        for (const auto& x : a.mipLayouts())
            for (const auto& y : b.mipLayouts()) {
                if (x.range.aspect != y.range.aspect || x.range.baseMipLevel != y.range.baseMipLevel
                    || !core::overlapsUnsignedRanges(x.range.baseArrayLayer,
                        x.range.arrayLayerCount,
                        y.range.baseArrayLayer,
                        y.range.arrayLayerCount))
                    continue;
                if (core::overlapsUnsignedRanges(x.origin.width / block,
                        blocksForExtent(x.extent.width, block),
                        y.origin.width / block,
                        blocksForExtent(y.extent.width, block))
                    && core::overlapsUnsignedRanges(x.origin.height / block,
                        blocksForExtent(x.extent.height, block),
                        y.origin.height / block,
                        blocksForExtent(y.extent.height, block))
                    && core::overlapsUnsignedRanges(
                        x.origin.depth, x.extent.depth, y.origin.depth, y.extent.depth))
                    return true;
            }
        return false;
    }
};

class TextureUploadContentTransaction;

class TextureUploadContentLedger final : public core::QuotaBoundSharedObjectIdentity {
    friend class TextureUploadContentTransaction;

    class ConstructionKey {
        friend class TextureUploadContentLedger;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    enum class ContentState { Uninitialized, Proposed, Submitted, Completed, Invalidated };

    struct ContentValue {
        ContentState state = ContentState::Uninitialized;
        std::uint64_t contentGeneration = 0u;

        bool operator==(const ContentValue& other) const noexcept
        {
            return state == other.state && contentGeneration == other.contentGeneration;
        }
    };

    using Control = core::VersionedMutationValue<ContentValue>;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxDomains = 0u, maxTransactions = 0u;
        std::uint64_t maxRetainedDomainBytes = 0u, maxPendingSourceBytes = 0u,
                      maxCpuMetadataBytes = 0u;
    };

private:
    class Domain final : public core::QuotaBoundSharedObjectIdentity {
        class Key {
            friend class TextureUploadContentLedger;
            Key() = default;

        public:
            Key(const Key&) = default;
        };
        friend class TextureUploadContentLedger;
        friend class TextureUploadContentTransaction;

    public:
        Domain(Key, std::shared_ptr<const TextureUploadContentSource> source)
            : source_(std::move(source)), control_(ContentValue{})
        {}

    private:
        const std::shared_ptr<const TextureUploadContentSource> source_;
        Control control_;
    };

public:
    class Expectation final {
        friend class TextureUploadContentLedger;
        friend class TextureUploadContentTransaction;

        Expectation(std::shared_ptr<const TextureUploadContentLedger> ledger,
            std::shared_ptr<Domain> domain,
            Control::Observation value)
            : ledger_(std::move(ledger)), domain_(std::move(domain)), value_(value)
        {}

    public:
        Expectation(const Expectation&) = default;

        ContentState state() const noexcept { return value_.value.state; }

        std::uint64_t contentGeneration() const noexcept { return value_.value.contentGeneration; }

        std::uint64_t internalRevision() const noexcept { return value_.revision; }

    private:
        const std::shared_ptr<const TextureUploadContentLedger> ledger_;
        const std::shared_ptr<Domain> domain_;
        const Control::Observation value_;
    };

    TextureUploadContentLedger(ConstructionKey,
        GpuResourceId destination,
        TextureUploadPacking::Destination layout,
        Budget budget)
        : destination_(destination), layout_(layout), budget_(budget), domains_(budget.maxDomains),
          transactionCount_(core::makeShared<core::AtomicCapacityBudget>(budget.maxTransactions)),
          pendingSourceBytes_(
              core::makeShared<core::AtomicCapacityBudget>(budget.maxPendingSourceBytes))
    {}

    static std::shared_ptr<TextureUploadContentLedger> prepareOriginal(GpuResourceId destination,
        const TextureDesc& layout,
        Budget budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = nullptr;
        if (budget.contractVersion != 1u) {
            diagnostic = "RHI-TEXTURE-CONTENT-LEDGER-VERSION";
            return {};
        }
        if (!budget.prototypeOptIn) {
            diagnostic = "RHI-TEXTURE-CONTENT-LEDGER-OPT-IN";
            return {};
        }
        if (!budget.maxDomains || !budget.maxTransactions || !budget.maxRetainedDomainBytes
            || !budget.maxPendingSourceBytes || !budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-TEXTURE-CONTENT-LEDGER-BUDGET";
            return {};
        }
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-TEXTURE-CONTENT-LEDGER-QUOTA";
            return {};
        }
        // Geometry/content validity is established from each original packing
        // at domain admission; this standalone ledger is CPU comparison intent.
        if (!destination.isValid()) {
            diagnostic = "RHI-TEXTURE-CONTENT-LEDGER-DESTINATION";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return shareOriginal(core::makeUnique<TextureUploadContentLedger>(ConstructionKey{},
                destination,
                TextureUploadPacking::Destination::capture(layout),
                budget));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-TEXTURE-CONTENT-LEDGER-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-TEXTURE-CONTENT-LEDGER-ALLOCATION";
            return {};
        }
    }

    static std::optional<Expectation> prepareDomainOriginal(
        const std::shared_ptr<TextureUploadContentLedger>& self,
        const std::shared_ptr<const TextureUploadContentSource>& source,
        const char*& diagnostic)
    {
        diagnostic = originalDiagnostic(self, source);
        if (diagnostic)
            return {};
        if (const auto domain = self->find(*source->packing()))
            return self->observe(self, domain, diagnostic);
        for (const auto& domain : self->domains_.values())
            if (TextureUploadContentDomainValidation::overlaps(
                    *domain->source_->packing(), *source->packing())) {
                diagnostic = "RHI-TEXTURE-CONTENT-DOMAIN-OVERLAP";
                return {};
            }
        if (!self->domains_.canAppend()) {
            diagnostic = "RHI-TEXTURE-CONTENT-DOMAIN-CAPACITY";
            return {};
        }
        if (source->retainedBytes()
            > self->budget_.maxRetainedDomainBytes - self->retainedDomainBytes_) {
            diagnostic = "RHI-TEXTURE-CONTENT-DOMAIN-BYTE-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(self->originalMetadataQuota());
            auto domain = Domain::shareOriginal(core::makeUnique<Domain>(Domain::Key{}, source));
            const auto observation = domain->control_.observe();
            if (!observation) {
                diagnostic = "RHI-TEXTURE-CONTENT-DOMAIN-INVALIDATED";
                return {};
            }
            self->domains_.append(domain);
            self->retainedDomainBytes_ += source->retainedBytes();
            return Expectation(self, std::move(domain), *observation);
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-TEXTURE-CONTENT-DOMAIN-ALLOCATION";
            return {};
        }
    }

    static const char* originalDiagnostic(const std::shared_ptr<TextureUploadContentLedger>& self,
        const std::shared_ptr<const TextureUploadContentSource>& source) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-TEXTURE-CONTENT-LEDGER-ORIGINAL-OWNER";
        if (!self->active_)
            return "RHI-TEXTURE-CONTENT-LEDGER-INVALIDATED";
        if (!source || !source->hasQuotaBoundSharedOwner(source))
            return "RHI-TEXTURE-CONTENT-SOURCE-ORIGINAL-OWNER";
        TextureDesc desc;
        self->layout_.write(desc);
        if (source->claim().destination != self->destination_
            || !source->packing()->destinationLayout().matches(desc))
            return "RHI-TEXTURE-CONTENT-DESTINATION-MISMATCH";
        return nullptr;
    }

    std::optional<ContentState> state(const TextureUploadPacking& domain) const noexcept
    {
        if (!active_)
            return ContentState::Invalidated;
        const auto entry = find(domain);
        if (!entry)
            return {};
        return !entry->control_.active() ? ContentState::Invalidated
                                         : entry->control_.value().state;
    }

    // Explicit CPU intent invalidation only, never native write/transition proof.
    void invalidate() noexcept
    {
        active_ = false;
        for (const auto& domain : domains_.values())
            domain->control_.invalidate();
    }

    std::size_t domainCount() const noexcept { return domains_.values().size(); }

    std::uint64_t retainedDomainBytes() const noexcept { return retainedDomainBytes_; }

    std::uint64_t liveTransactions() const noexcept { return transactionCount_->used(); }

    std::uint64_t livePendingSourceBytes() const noexcept { return pendingSourceBytes_->used(); }

    const char* executionDiagnostic() const noexcept
    {
        return "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN";
    }

private:
    std::shared_ptr<Domain> find(const TextureUploadPacking& source) const noexcept
    {
        for (const auto& domain : domains_.values())
            if (TextureUploadContentDomainValidation::same(*domain->source_->packing(), source))
                return domain;
        return {};
    }

    static std::optional<Expectation> observe(
        std::shared_ptr<const TextureUploadContentLedger> self,
        std::shared_ptr<Domain> domain,
        const char*& diagnostic) noexcept
    {
        if (!domain->control_.active()) {
            diagnostic = "RHI-TEXTURE-CONTENT-DOMAIN-INVALIDATED";
            return {};
        }
        const auto value = domain->control_.observe();
        if (!value) {
            diagnostic = "RHI-TEXTURE-CONTENT-DOMAIN-PENDING";
            return {};
        }
        return Expectation(std::move(self), std::move(domain), *value);
    }

    const GpuResourceId destination_;
    const TextureUploadPacking::Destination layout_;
    const Budget budget_;
    core::BoundedSequence<std::shared_ptr<Domain>> domains_;
    const std::shared_ptr<core::AtomicCapacityBudget> transactionCount_, pendingSourceBytes_;
    std::uint64_t retainedDomainBytes_ = 0u;
    bool active_ = true;
};

class TextureUploadContentTransaction final : public core::QuotaBoundSharedObjectIdentity {
    friend class rhi_webgpu::WebGpuOriginalTextureUploadTransaction;
    using Ledger = TextureUploadContentLedger;

    class ConstructionKey {
        friend class TextureUploadContentTransaction;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxCpuMetadataBytes = 0u;
    };
    enum class Status { Prepared, Pending, Submitted, Completed, Abandoned, Invalidated };

    TextureUploadContentTransaction(ConstructionKey,
        std::shared_ptr<Ledger> ledger,
        std::shared_ptr<const TextureUploadContentSource> source,
        Ledger::Expectation expected)
        : ledger_(std::move(ledger)), source_(std::move(source)), expected_(std::move(expected)),
          countLease_(ledger_->transactionCount_, 1u),
          byteLease_(
              ledger_->pendingSourceBytes_, countLease_.charged() ? source_->retainedBytes() : 0u)
    {}

    ~TextureUploadContentTransaction() { abandon(); }

    static std::shared_ptr<TextureUploadContentTransaction> prepareOriginal(
        std::shared_ptr<Ledger> ledger,
        std::shared_ptr<const TextureUploadContentSource> source,
        const Ledger::Expectation& expected,
        Budget budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = nullptr;
        if (budget.contractVersion != 1u) {
            diagnostic = "RHI-TEXTURE-CONTENT-TRANSACTION-VERSION";
            return {};
        }
        if (!budget.prototypeOptIn) {
            diagnostic = "RHI-TEXTURE-CONTENT-TRANSACTION-OPT-IN";
            return {};
        }
        if (!budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-TEXTURE-CONTENT-TRANSACTION-BUDGET";
            return {};
        }
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-TEXTURE-CONTENT-TRANSACTION-QUOTA";
            return {};
        }
        diagnostic = validateExpected(ledger, source, expected);
        if (diagnostic)
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<TextureUploadContentTransaction>(
                ConstructionKey{}, std::move(ledger), std::move(source), expected);
            if (!result->countLease_.charged()) {
                diagnostic = "RHI-TEXTURE-CONTENT-TRANSACTION-COUNT-CAPACITY";
                return {};
            }
            if (!result->byteLease_.charged()) {
                diagnostic = "RHI-TEXTURE-CONTENT-TRANSACTION-BYTE-CAPACITY";
                return {};
            }
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-TEXTURE-CONTENT-TRANSACTION-ALLOCATION";
            return {};
        }
    }

    const char* validate() const noexcept
    {
        if (status_ == Status::Abandoned)
            return "RHI-TEXTURE-CONTENT-TRANSACTION-ABANDONED";
        if (status_ == Status::Invalidated)
            return "RHI-TEXTURE-CONTENT-TRANSACTION-INVALIDATED";
        if (const auto* id = Ledger::originalDiagnostic(ledger_, source_))
            return id;
        if (status_ == Status::Prepared)
            return validateExpected(ledger_, source_, expected_);
        const auto& control = expected_.domain_->control_;
        if (!control.active())
            return "RHI-TEXTURE-CONTENT-DOMAIN-INVALIDATED";
        if (status_ == Status::Submitted || status_ == Status::Completed) {
            const auto state = status_ == Status::Submitted ? Ledger::ContentState::Submitted
                                                            : Ledger::ContentState::Completed;
            if (!committed_ || !journal_.empty() || !control.matches(*committed_)
                || committed_->value.state != state
                || committed_->value.contentGeneration != source_->claim().contentGeneration)
                return "RHI-TEXTURE-CONTENT-TRANSACTION-COMMITTED-MISMATCH";
            return nullptr;
        }
        if (!pending_ || !control.matchesPending(*pending_, journal_)
            || pending_->value().state != Ledger::ContentState::Proposed
            || pending_->value().contentGeneration != source_->claim().contentGeneration)
            return "RHI-TEXTURE-CONTENT-TRANSACTION-PENDING-MISMATCH";
        return nullptr;
    }

    static const char* apply(const std::shared_ptr<TextureUploadContentTransaction>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-TEXTURE-CONTENT-TRANSACTION-ORIGINAL-OWNER";
        if (self->status_ != Status::Prepared)
            return "RHI-TEXTURE-CONTENT-TRANSACTION-STATUS";
        if (const auto* id = self->validate())
            return id;
        auto& control = self->expected_.domain_->control_;
        const auto result = control.tryWrite(
            {Ledger::ContentState::Proposed, self->source_->claim().contentGeneration},
            &self->journal_);
        if (result != Ledger::Control::WriteResult::Written)
            return "RHI-TEXTURE-CONTENT-TRANSACTION-WRITE-REJECTED";
        self->pending_ = control.observePending(self->journal_);
        self->status_ = Status::Pending;
        if (const auto* id = self->validate()) {
            self->invalidate();
            return id;
        }
        return nullptr;
    }

    void abandon() noexcept
    {
        if (status_ == Status::Abandoned || status_ == Status::Invalidated
            || status_ == Status::Completed)
            return;
        if (status_ == Status::Submitted) {
            invalidate();
            return;
        }
        journal_.rollback();
        status_ = Status::Abandoned;
    }

    void invalidate() noexcept
    {
        if (status_ == Status::Abandoned || status_ == Status::Invalidated)
            return;
        expected_.domain_->control_.invalidate();
        journal_.rollback();
        status_ = Status::Invalidated;
    }

    Status status() const noexcept { return status_; }

    const auto& source() const noexcept { return source_; }

    const auto& ledger() const noexcept { return ledger_; }

    const auto& expected() const noexcept { return expected_; }

    // Identity comparison only: the caller must also validate the exact current
    // journal observation. Equal domains/generation are not original admission.
    bool matchesOriginalAdmission(const std::shared_ptr<Ledger>& ledger,
        const std::shared_ptr<const TextureUploadContentSource>& source,
        const Ledger::Expectation& expected) const noexcept
    {
        return ledger_.get() == ledger.get() && source_.get() == source.get()
            && core::SharedOwnerProjection::sameOwner(ledger_, ledger)
            && core::SharedOwnerProjection::sameOwner(source_, source)
            && expected_.ledger_.get() == expected.ledger_.get()
            && core::SharedOwnerProjection::sameOwner(expected_.ledger_, expected.ledger_)
            && expected_.domain_.get() == expected.domain_.get()
            && core::SharedOwnerProjection::sameOwner(expected_.domain_, expected.domain_)
            && expected_.value_.revision == expected.value_.revision
            && expected_.value_.value == expected.value_.value;
    }

    bool journalEmpty() const noexcept { return journal_.empty(); }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN";
    }

private:
    // Only the private original backend transaction can reach these mutations;
    // its own submission/completion entries are restricted to OrdinarySubmission.
    const char* markNativeSubmitted() noexcept
    {
        if (status_ != Status::Pending)
            return "RHI-TEXTURE-CONTENT-TRANSACTION-SUBMIT-STATUS";
        if (const auto* id = validate())
            return id;
        journal_.commit();
        return writeNativeCommitted(Ledger::ContentState::Submitted, Status::Submitted);
    }

    const char* markNativeCompleted() noexcept
    {
        if (status_ != Status::Submitted)
            return "RHI-TEXTURE-CONTENT-TRANSACTION-COMPLETE-STATUS";
        if (const auto* id = validate())
            return id;
        return writeNativeCommitted(Ledger::ContentState::Completed, Status::Completed);
    }

    const char* writeNativeCommitted(Ledger::ContentState value, Status status) noexcept
    {
        auto& control = expected_.domain_->control_;
        if (control.tryWrite({value, source_->claim().contentGeneration})
            != Ledger::Control::WriteResult::Written) {
            invalidate();
            return "RHI-TEXTURE-CONTENT-TRANSACTION-COMMIT-WRITE";
        }
        committed_ = control.observe();
        status_ = status;
        if (const auto* id = validate()) {
            invalidate();
            return id;
        }
        return nullptr;
    }

    static const char* validateExpected(const std::shared_ptr<Ledger>& ledger,
        const std::shared_ptr<const TextureUploadContentSource>& source,
        const Ledger::Expectation& expected) noexcept
    {
        if (const auto* id = Ledger::originalDiagnostic(ledger, source))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(ledger, expected.ledger_)
            || !expected.domain_->hasQuotaBoundSharedOwner(expected.domain_)
            || ledger->find(*source->packing()).get() != expected.domain_.get())
            return "RHI-TEXTURE-CONTENT-TRANSACTION-FOREIGN-EXPECTATION";
        const auto& control = expected.domain_->control_;
        if (!control.active())
            return "RHI-TEXTURE-CONTENT-DOMAIN-INVALIDATED";
        if (control.pending())
            return "RHI-TEXTURE-CONTENT-DOMAIN-PENDING";
        if (!control.matches(expected.value_))
            return "RHI-TEXTURE-CONTENT-TRANSACTION-STALE";
        if (expected.state() != Ledger::ContentState::Uninitialized)
            return "RHI-TEXTURE-CONTENT-TRANSACTION-NATIVE-BINDING-UNPROVEN";
        return nullptr;
    }

    // Stable original owners outlive all pending observations and the journal.
    const std::shared_ptr<Ledger> ledger_;
    const std::shared_ptr<const TextureUploadContentSource> source_;
    const Ledger::Expectation expected_;
    core::AtomicCapacityLease countLease_, byteLease_;
    core::MutationJournal journal_;
    std::optional<Ledger::Control::PendingObservation> pending_;
    std::optional<Ledger::Control::Observation> committed_;
    Status status_ = Status::Prepared;
};
} // namespace arp::rhi
