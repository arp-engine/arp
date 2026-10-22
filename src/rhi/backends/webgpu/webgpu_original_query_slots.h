#pragma once
#include "core/container/bounded_sequence.h"
#include "rhi/backends/webgpu/webgpu_original_query_binding.h"
#include "rhi/frontend/query_slot_declaration_control.h"

namespace arp::rhi_webgpu {
struct WebGpuOriginalQuerySlotsBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxNativeOwners = 0u, maxQuerySlots = 0u, maxScopeIds = 0u;
    std::uint64_t maxLabelBytes = 0u, maxCpuGraphBytes = 0u, maxCpuSlotBytes = 0u;
};

// Original cold slot declarations. CPU revision currentness is never GPU proof.
class WebGpuOriginalQuerySlots final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuOriginalQueryRecordReservation;

    class ConstructionKey {
        friend class WebGpuOriginalQuerySlots;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Control = rhi::common::QuerySlotDeclarationControl;
    using Ticket = core::runtime::CompletionSignal::Ticket;

public:
    class Observation final {
        friend class WebGpuOriginalQuerySlots;

        Observation(std::shared_ptr<const WebGpuOriginalQuerySlots> graph,
            WebGpuOriginalQueryBinding binding,
            std::uint32_t index,
            Control::Observation slot,
            Ticket ticket)
            : graph_(std::move(graph)), binding_(std::move(binding)), index_(index), slot_(slot),
              ticket_(ticket)
        {}

    public:
        std::uint32_t index() const noexcept { return index_; }

        Control::Phase phase() const noexcept { return slot_.value; }

        std::uint64_t revision() const noexcept { return slot_.revision; }

        const auto& originalBinding() const noexcept { return binding_; }

        const auto& originalGraph() const noexcept { return graph_; }

    private:
        const std::shared_ptr<const WebGpuOriginalQuerySlots> graph_;
        const WebGpuOriginalQueryBinding binding_;
        const std::uint32_t index_;
        const Control::Observation slot_;
        const Ticket ticket_;
    };

    struct Lowering {
        bool validatorOnly = false, coldGraphImplemented = false;
        const char* diagnostic = "RHI-QUERY-SLOTS-BACKEND";
    };

    static constexpr Lowering lowering(rhi::Backend backend) noexcept
    {
        switch (backend) {
        case rhi::Backend::Null:
            return {true, false, "RHI-QUERY-SLOTS-VALIDATOR-ONLY"};
        case rhi::Backend::Vulkan:
        case rhi::Backend::Metal:
            return {false, false, "RHI-QUERY-SLOTS-NATIVE-UNIMPLEMENTED"};
        case rhi::Backend::WebGPU:
            return {false, true, "RHI-QUERY-SLOTS-EXECUTION-UNPROVEN"};
        case rhi::Backend::WebGL:
            break;
        }
        return {};
    }

    WebGpuOriginalQuerySlots(ConstructionKey, std::shared_ptr<const WebGpuOriginalQueryOwner> owner)
        : owner_(std::move(owner))
    {}

    ~WebGpuOriginalQuerySlots()
    {
        if (owner_->slotReservation_.release(ticket_))
            owner_->slotReservation_.reset();
    }

    static const char* validateBudget(const WebGpuOriginalQuerySlotsBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-QUERY-SLOTS-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-QUERY-SLOTS-OPT-IN";
        if (!b.maxNativeOwners || !b.maxQuerySlots || !b.maxCpuGraphBytes || !b.maxCpuSlotBytes)
            return "RHI-QUERY-SLOTS-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<WebGpuOriginalQuerySlots> prepareOriginal(
        std::shared_ptr<const WebGpuOriginalQueryOwner> owner,
        const WebGpuOriginalQuerySlotsBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> graphQuota,
        std::shared_ptr<core::CpuMemoryQuota> slotQuota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!graphQuota || graphQuota->limitBytes() != b.maxCpuGraphBytes || !slotQuota
            || slotQuota->limitBytes() != b.maxCpuSlotBytes) {
            diagnostic = "RHI-QUERY-SLOTS-QUOTA";
            return {};
        }
        diagnostic = validateOwner(owner);
        if (diagnostic)
            return {};
        const auto& input = owner->originalInput();
        if (input->scalarFields().count() > b.maxQuerySlots
            || input->scopeIds().size() > b.maxScopeIds
            || input->label().size() > b.maxLabelBytes) {
            diagnostic = "RHI-QUERY-SLOTS-CAPACITY";
            return {};
        }
        if (!owner->slotReservation_.canReserve()) {
            diagnostic = "RHI-QUERY-SLOTS-OWNER-BUSY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope metadata(graphQuota);
            auto object = core::makeUnique<WebGpuOriginalQuerySlots>(ConstructionKey{}, owner);
            {
                core::CpuMemoryQuotaScope slots(slotQuota);
                object->slots_ =
                    core::BoundedSequence<core::UniquePtr<Control>>(input->scalarFields().count());
                for (std::uint32_t i = 0u; i < input->scalarFields().count(); ++i)
                    if (!object->slots_.append(core::makeUnique<Control>())) {
                        diagnostic = "RHI-QUERY-SLOTS-CAPACITY";
                        return {};
                    }
            }
            auto shared = shareOriginal(std::move(object));
            if (!shared) {
                diagnostic = "RHI-QUERY-SLOTS-GRAPH-OWNER";
                return {};
            }
            shared->ticket_ = owner->slotReservation_.reserve();
            if (!shared->ticket_) {
                diagnostic = "RHI-QUERY-SLOTS-OWNER-BUSY";
                return {};
            }
            return shared;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-QUERY-SLOTS-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-QUERY-SLOTS-ALLOCATION";
            return {};
        }
    }

    std::size_t slotCount() const noexcept { return slots_.values().size(); }

    const auto& originalOwner() const noexcept { return owner_; }

    bool recordingSupported() const noexcept { return false; }

    bool resultAvailable() const noexcept { return false; }

    static std::optional<Observation> observeOriginal(
        const std::shared_ptr<const WebGpuOriginalQuerySlots>& self,
        const WebGpuOriginalQueryBinding& binding,
        const char*& diagnostic) noexcept
    {
        diagnostic = validateGraph(self);
        if (diagnostic)
            return {};
        diagnostic = validateBinding(*self, binding);
        if (diagnostic)
            return {};
        const auto index =
            binding.originalCommand().scalar().get<rhi::CommandScalarArguments::Query>()->index;
        if (index >= self->slotCount()) {
            diagnostic = "RHI-QUERY-SLOTS-INDEX";
            return {};
        }
        const auto slot = self->slots_.values()[index]->observe();
        if (!slot) {
            diagnostic = "RHI-QUERY-SLOTS-CURRENT-UNAVAILABLE";
            return {};
        }
        return Observation(self, binding, index, *slot, self->ticket_);
    }

    static const char* validateCurrentOriginal(
        const std::shared_ptr<const WebGpuOriginalQuerySlots>& self,
        const Observation& observation) noexcept
    {
        if (const auto* id = validateGraph(self))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(self, observation.graph_)
            || observation.ticket_.generation != self->ticket_.generation)
            return "RHI-QUERY-SLOTS-OBSERVATION-OWNER";
        if (const auto* id = validateBinding(*self, observation.binding_))
            return id;
        if (observation.index_ >= self->slotCount()
            || observation.index_
                != observation.binding_.originalCommand()
                    .scalar()
                    .get<rhi::CommandScalarArguments::Query>()
                    ->index)
            return "RHI-QUERY-SLOTS-INDEX";
        if (!self->slots_.values()[observation.index_]->matches(observation.slot_))
            return "RHI-QUERY-SLOTS-STALE-REVISION";
        return nullptr;
    }

    static const char* executionDiagnostic(
        const std::shared_ptr<const WebGpuOriginalQuerySlots>& self,
        const Observation& observation) noexcept
    {
        if (const auto* id = validateCurrentOriginal(self, observation))
            return id;
        return observation.binding_.executionDiagnostic();
    }

    static const char* resetOriginal(const std::shared_ptr<WebGpuOriginalQuerySlots>& self) noexcept
    {
        if (const auto* id = validateGraph(self))
            return id;
        for (const auto& slot : self->slots_.values()) {
            if (!slot->active() || slot->pending())
                return "RHI-QUERY-SLOTS-RESET-CURRENT";
            if (!Control::resettable(slot->phase()))
                return "RHI-QUERY-SLOTS-RESET-BUSY";
        }
        core::MutationJournal journal;
        for (const auto& slot : self->slots_.values())
            if (const auto* id = slot->declare(Control::Event::Reset, &journal)) {
                journal.rollback();
                self->invalidateAll(id);
                return id;
            }
        journal.commit();
        return nullptr;
    }

    static const char* invalidateOriginal(
        const std::shared_ptr<WebGpuOriginalQuerySlots>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-QUERY-SLOTS-GRAPH-OWNER";
        self->invalidateAll("RHI-QUERY-SLOTS-INVALIDATED");
        return nullptr;
    }

private:
    static const char* validateOwner(
        const std::shared_ptr<const WebGpuOriginalQueryOwner>& owner) noexcept
    {
        if (!owner || !owner->hasQuotaBoundSharedOwner(owner))
            return "RHI-QUERY-SLOTS-NATIVE-OWNER";
        if (!owner->decision().creationEligible()
            || !owner->decision().matchesOriginal(owner->originalInput()))
            return "RHI-QUERY-SLOTS-INPUT";
        switch (owner->status()) {
        case WebGpuOriginalQueryOwner::Status::Prepared:
        case WebGpuOriginalQueryOwner::Status::Pending:
            return nullptr;
        case WebGpuOriginalQueryOwner::Status::CreationReady:
        case WebGpuOriginalQueryOwner::Status::Rejected:
        case WebGpuOriginalQueryOwner::Status::DeviceLost:
            return owner->validate();
        }
        return "RHI-QUERY-SLOTS-NATIVE-STATUS";
    }

    static const char* validateGraph(
        const std::shared_ptr<const WebGpuOriginalQuerySlots>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-QUERY-SLOTS-GRAPH-OWNER";
        if (const auto* id = validateOwner(self->owner_))
            return id;
        if (self->failure_)
            return self->failure_;
        if (!self->owner_->slotReservation_.matches(self->ticket_)
            || self->slotCount() != self->owner_->originalInput()->scalarFields().count())
            return "RHI-QUERY-SLOTS-ORIGINAL-TICKET";
        return nullptr;
    }

    static const char* validateBinding(
        const WebGpuOriginalQuerySlots& self, const WebGpuOriginalQueryBinding& binding) noexcept
    {
        if (!binding.matchesOriginal(binding.originalCommand(), self.owner_)
            || !self.owner_->decision().matchesOriginal(binding.originalCommand().originalInput()))
            return "RHI-QUERY-SLOTS-ORIGINAL-BINDING";
        return binding.originalCommand().validateDeclaration();
    }

    void invalidateAll(const char* id) noexcept
    {
        if (!failure_)
            failure_ = id;
        for (const auto& slot : slots_.values())
            slot->invalidate();
    }

    const std::shared_ptr<const WebGpuOriginalQueryOwner> owner_;
    core::BoundedSequence<core::UniquePtr<Control>> slots_;
    Ticket ticket_;
    const char* failure_ = nullptr;
};
} // namespace arp::rhi_webgpu
