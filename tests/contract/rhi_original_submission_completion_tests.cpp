#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_backend_internal.h"
#include "rhi/frontend/original_submission_completion.h"
#include "rhi/frontend/original_submitted_buffer_read_pins.h"
#include "rhi/frontend/resource_state_control.h"
#include "tests/fixtures/original_submitted_buffer_read_pins_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef NDEBUG
#error Original submission completion tests require assertions
#endif

namespace {
using namespace arp;
using namespace rhi;
using Domain = OriginalSubmissionCompletionDomain;
using Budget = semantics::OriginalSubmissionCompletionBudget;
using Contract = semantics::OriginalSubmissionCompletionContract;
using Outcome = Domain::Outcome;

class DeliveryGate final {
public:
    void wait()
    {
        std::unique_lock lock(mutex_);
        assert(changed_.wait_for(lock, std::chrono::seconds(5), [this] {
            return open_;
        }));
    }

    void open()
    {
        {
            std::scoped_lock lock(mutex_);
            open_ = true;
        }
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool open_ = false;
};

void expect(const char* actual, const char* expected)
{
    assert(actual && std::strcmp(actual, expected) == 0);
}

// Genuine original scalar and sole journal. Different submissions own distinct
// controls: this fixture grants no cross-command pending-resource access.
struct Participant final {
    common::RecordedResourceStateControl state{ResourceState::ShaderWrite};
    core::MutationJournal journal;
    const std::thread::id owner = std::this_thread::get_id();
    unsigned resolves = 0u, cancels = 0u;
    bool returnSuccess = true;
    Domain* reentry = nullptr; // non-owning, used only during this callback
    OriginalDeviceLifetime::Ticket retiredTicket;
    std::atomic<unsigned>* terminalCounter = nullptr;

    Participant()
    {
        assert(!state.writeBuffer(ResourceState::ShaderRead, &journal));
        assert(state.pending());
    }

    static bool resolve(void* context, bool success) noexcept
    {
        auto& self = *static_cast<Participant*>(context);
        assert(self.state.pending());
        if (self.owner != std::this_thread::get_id())
            assert(!success && self.retiredTicket.terminallyRevoked());
        assert(!self.resolves && !self.cancels);
        ++self.resolves;
        if (self.terminalCounter)
            self.terminalCounter->fetch_add(1u, std::memory_order_release);
        if (self.reentry) {
            expect(self.reentry->pollValidator(), "RHI-SUBMISSION-COMPLETION-RESOLVING");
            expect(self.reentry->retireValidator(), "RHI-SUBMISSION-COMPLETION-RESOLVING");
        }
        if (success && self.returnSuccess)
            self.journal.commit();
        else
            assert(self.journal.invalidate());
        return self.returnSuccess;
    }

    static void cancel(void* context) noexcept
    {
        auto& self = *static_cast<Participant*>(context);
        assert(self.owner == std::this_thread::get_id() && self.state.pending());
        assert(!self.resolves && !self.cancels);
        ++self.cancels;
        self.journal.rollback();
    }
};

struct Fixture final {
    core::UniquePtr<GpuDevice> device = createNullDevice();
    Budget budget{1u, true, 4u, 4u, 1024u * 1024u};
    std::shared_ptr<core::CpuMemoryQuota> quota =
        core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
    std::shared_ptr<Domain> domain;

    Fixture()
    {
        assert(
            !device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, budget.maxCpuMetadataBytes},
                core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes)));
        const char* diagnostic = nullptr;
        domain = Domain::prepareValidator(*device, budget, quota, diagnostic);
        assert(domain && !diagnostic && !Domain::validateOriginal(domain));
    }

    Domain::Ticket prepare(const std::shared_ptr<Participant>& p,
        const Domain::Ticket* predecessors = nullptr,
        std::size_t count = 0u)
    {
        const char* diagnostic = nullptr;
        auto ticket = domain->prepareValidatorCommand(
            p, Participant::resolve, Participant::cancel, predecessors, count, diagnostic);
        assert(!diagnostic && ticket.outcome() == Outcome::Pending);
        assert(!ticket.provesGpuCompletion());
        return ticket;
    }

    void refuse(const std::shared_ptr<void>& p,
        const Domain::Ticket* predecessors,
        std::size_t count,
        const char* expected)
    {
        const char* diagnostic = nullptr;
        const auto ticket = domain->prepareValidatorCommand(
            p, Participant::resolve, Participant::cancel, predecessors, count, diagnostic);
        expect(diagnostic, expected);
        assert(ticket.outcome() == Outcome::Cancelled && !ticket.provesGpuCompletion());
    }
};

void contract()
{
    Budget valid{1u, true, 4u, 4u, 1024u * 1024u};
    assert(!Contract::inspect(valid));
    for (auto version : {0u, 2u, UINT32_MAX}) {
        auto invalid = valid;
        invalid.contractVersion = version;
        invalid.prototypeOptIn = false;
        expect(Contract::inspect(invalid), "RHI-SUBMISSION-COMPLETION-VERSION");
    }
    auto invalid = valid;
    invalid.prototypeOptIn = false;
    expect(Contract::inspect(invalid), "RHI-SUBMISSION-COMPLETION-OPT-IN");
    for (auto field :
        {&Budget::maxSubmissions, &Budget::maxPredecessorLinks, &Budget::maxCpuMetadataBytes}) {
        invalid = valid;
        invalid.*field = 0u;
        expect(Contract::inspect(invalid), "RHI-SUBMISSION-COMPLETION-CAPACITY");
    }
    for (auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255u)})
        for (auto version : {0u, 1u, 2u, UINT32_MAX}) {
            const auto result = Contract::lowering(backend, version);
            const bool validator = backend == Backend::Null && version == 1u;
            assert(
                result.validatorOnly == validator && result.validatorStorageAvailable == validator);
            assert(!result.nativeResultHandoffAvailable && !result.pendingResourceAdmissionAvailable
                && !result.provesGpuCompletion);
            assert(bool(result.diagnostic) != validator);
            const auto* join = Contract::ordinaryOwnerJoinLowering(backend, version);
            assert(bool(join) != (backend == Backend::Metal && version == 1u));
            if (version != 1u)
                expect(join, "RHI-SUBMISSION-COMPLETION-VERSION");
            const auto* pin =
                semantics::OriginalSubmissionReadContract::pinCustodyLowering(backend, version);
            assert(bool(pin) != (backend == Backend::Null && version == 1u));
            const auto* cold = semantics::OriginalSubmissionReadContract::coldReservationLowering(
                backend, version);
            assert(bool(cold) != validator);
            const auto* canonical =
                semantics::OriginalSubmissionReadContract::canonicalStateLowering(backend, version);
            assert(bool(canonical) != validator);
            if (version != 1u)
                expect(canonical, "RHI-SUBMISSION-READ-VERSION");
            else if (backend == Backend::Vulkan || backend == Backend::Metal
                || backend == Backend::WebGPU)
                expect(canonical, "RHI-SUBMISSION-READ-CANONICAL-UNAVAILABLE");
            if (version != 1u)
                expect(cold, "RHI-SUBMISSION-READ-VERSION");
            else if (backend == Backend::Vulkan || backend == Backend::Metal
                || backend == Backend::WebGPU)
                expect(cold, "RHI-SUBMISSION-READ-COLD-RESERVATION-UNAVAILABLE");
        }
}

void dependencyOrder(bool childFirst, bool parentSuccess, bool childSuccess)
{
    Fixture f;
    auto parent = core::makeShared<Participant>(), child = core::makeShared<Participant>();
    auto a = f.prepare(parent);
    expect(Domain::deliverValidatorResult(a, true), "RHI-SUBMISSION-COMPLETION-RESULT-PHASE");
    assert(!f.domain->acceptValidator(a));
    auto b = f.prepare(child, &a, 1u);
    assert(!f.domain->acceptValidator(b));
    parent->reentry = f.domain.get();
    child->reentry = f.domain.get();
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    const auto deliver = [](const Domain::Ticket& ticket, bool success) {
        std::thread worker([ticket, success] {
            assert(!Domain::deliverValidatorResult(ticket, success));
        });
        worker.join();
    };
    if (childFirst) {
        deliver(b, childSuccess);
        assert(child->state.pending() && !child->resolves);
        assert(!f.domain->pollValidator());
        if (childSuccess) {
            assert(b.outcome() == Outcome::Pending && child->state.pending());
        } else {
            assert(b.outcome() == Outcome::Failed && !child->state.active());
        }
        deliver(a, parentSuccess);
    } else {
        deliver(a, parentSuccess);
        assert(!f.domain->pollValidator());
        assert(a.outcome() == (parentSuccess ? Outcome::Succeeded : Outcome::Failed));
        assert(child->state.pending() && b.outcome() == Outcome::Pending);
        deliver(b, childSuccess);
    }
    assert(!f.domain->pollValidator());
    assert(a.outcome() == (parentSuccess ? Outcome::Succeeded : Outcome::Failed));
    assert(b.outcome() == (parentSuccess && childSuccess ? Outcome::Succeeded : Outcome::Failed));
    assert(parent->resolves == 1u && child->resolves == 1u);
    assert(!parent->state.pending() && !child->state.pending());
    assert(parent->state.active() == parentSuccess);
    assert(child->state.active() == (parentSuccess && childSuccess));
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    assert(!f.domain->pollValidator());
    assert(parent->resolves == 1u && child->resolves == 1u);
}

void refusalsAndCancellation()
{
    Fixture f, other;
    auto parent = core::makeShared<Participant>(), child = core::makeShared<Participant>();
    auto a = f.prepare(parent);
    f.refuse(child, &a, 1u, "RHI-SUBMISSION-COMPLETION-PREDECESSOR");
    assert(!f.domain->acceptValidator(a));
    expect(f.domain->acceptValidator(a), "RHI-SUBMISSION-COMPLETION-COMMAND");
    other.refuse(child, &a, 1u, "RHI-SUBMISSION-COMPLETION-PREDECESSOR");
    const std::array duplicate{a, a};
    f.refuse(child, duplicate.data(), duplicate.size(), "RHI-SUBMISSION-COMPLETION-DUPLICATE-LINK");
    f.refuse({}, nullptr, 0u, "RHI-SUBMISSION-COMPLETION-PARTICIPANT");
    f.refuse(child, nullptr, 1u, "RHI-SUBMISSION-COMPLETION-PARTICIPANT");
    f.refuse(f.domain, nullptr, 0u, "RHI-SUBMISSION-COMPLETION-PARTICIPANT");
    f.refuse(f.quota, nullptr, 0u, "RHI-SUBMISSION-COMPLETION-PARTICIPANT");
    f.refuse(parent, nullptr, 0u, "RHI-SUBMISSION-COMPLETION-DUPLICATE-PARTICIPANT");
    auto b = f.prepare(child, &a, 1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    assert(!f.domain->cancelValidator(b));
    assert(b.outcome() == Outcome::Cancelled && child->cancels == 1u);
    assert(child->state.active() && !child->state.pending()
        && child->state.state() == ResourceState::ShaderWrite);
    assert(parent->state.pending() && !parent->resolves && a.outcome() == Outcome::Pending);
    expect(f.domain->cancelValidator(a), "RHI-SUBMISSION-COMPLETION-CANCEL-PHASE");
    expect(f.domain->cancelValidator(b), "RHI-SUBMISSION-COMPLETION-CANCEL-PHASE");
    expect(Domain::deliverValidatorResult(b, true), "RHI-SUBMISSION-COMPLETION-RESULT-PHASE");
    assert(!Domain::deliverValidatorResult(a, true));
    expect(Domain::deliverValidatorResult(a, false), "RHI-SUBMISSION-COMPLETION-DUPLICATE-RESULT");
    assert(!f.domain->pollValidator() && !parent->state.pending());
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    auto fakeOwner = core::makeShared<int>(1);
    std::shared_ptr<Domain> fake(fakeOwner, f.domain.get());
    expect(Domain::validateOriginal(fake), "RHI-SUBMISSION-COMPLETION-OWNER");
    expect(Domain::validateOriginal({}), "RHI-SUBMISSION-COMPLETION-OWNER");
    expect(other.domain->acceptValidator(a), "RHI-SUBMISSION-COMPLETION-COMMAND");
}

void capacitiesAndQuota()
{
    Fixture f;
    std::array<std::shared_ptr<Participant>, 4u> owners;
    std::array<Domain::Ticket, 4u> tickets;
    for (std::size_t i = 0u; i < owners.size(); ++i) {
        owners[i] = core::makeShared<Participant>();
        const std::size_t links = i ? (i == 3u ? 2u : 1u) : 0u;
        tickets[i] = f.prepare(owners[i], tickets.data(), links);
        assert(!f.domain->acceptValidator(tickets[i]));
    }
    auto extra = core::makeShared<Participant>();
    f.refuse(extra, nullptr, 0u, "RHI-SUBMISSION-COMPLETION-SLOT-CAPACITY");
    assert(!Domain::deliverValidatorResult(tickets[3u], true));
    assert(!f.domain->pollValidator());
    assert(!Domain::deliverValidatorResult(tickets[0u], true));
    assert(!f.domain->pollValidator());
    // One slot is free, but all four unresolved links remain charged.
    f.refuse(extra, tickets.data(), 1u, "RHI-SUBMISSION-COMPLETION-LINK-CAPACITY");
    assert(!Domain::deliverValidatorResult(tickets[1u], true));
    assert(!Domain::deliverValidatorResult(tickets[2u], true));
    assert(!f.domain->pollValidator());
    for (const auto& ticket : tickets)
        assert(ticket.outcome() == Outcome::Succeeded);
    // The metadata quota is exhausted independently of released slot/link capacity.
    {
        core::CpuMemoryQuotaLease full(
            f.quota, static_cast<std::size_t>(f.budget.maxCpuMetadataBytes - f.quota->liveBytes()));
        assert(full.charged() && f.quota->liveBytes() == f.budget.maxCpuMetadataBytes);
        f.refuse(extra, nullptr, 0u, "RHI-SUBMISSION-COMPLETION-ALLOCATION");
    }
    auto reused = f.prepare(extra, tickets.data(), 2u);
    assert(!f.domain->cancelValidator(reused));

    const char* diagnostic = nullptr;
    auto invalid = f.budget;
    invalid.maxSubmissions = UINT64_MAX;
    assert(!Domain::prepareValidator(*f.device, invalid, f.quota, diagnostic));
    expect(diagnostic, "RHI-SUBMISSION-COMPLETION-STORAGE-SIZE");
    invalid = f.budget;
    --invalid.maxCpuMetadataBytes;
    assert(!Domain::prepareValidator(*f.device, invalid, f.quota, diagnostic));
    expect(diagnostic, "RHI-SUBMISSION-COMPLETION-QUOTA");
    assert(!Domain::prepareValidator(*f.device, f.budget, {}, diagnostic));
    expect(diagnostic, "RHI-SUBMISSION-COMPLETION-QUOTA");

    // Measure the actual layout, then verify exact-byte and one-short construction.
    auto probeQuota = core::makeShared<core::CpuMemoryQuota>(f.budget.maxCpuMetadataBytes);
    auto probe = Domain::prepareValidator(*f.device, f.budget, probeQuota, diagnostic);
    assert(probe && !diagnostic);
    const auto required = probeQuota->peakBytes();
    probe.reset();
    assert(!probeQuota->liveBytes());
    for (const auto bytes : {required - 1u, required}) {
        auto budget = f.budget;
        budget.maxCpuMetadataBytes = bytes;
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto domain = Domain::prepareValidator(*f.device, budget, quota, diagnostic);
        if (bytes == required)
            assert(domain && !diagnostic);
        else {
            assert(!domain && !quota->liveBytes());
            expect(diagnostic, "RHI-SUBMISSION-COMPLETION-ALLOCATION");
        }
    }
}

void threadAndRetirement()
{
    Fixture f;
    auto accepted = core::makeShared<Participant>(), pending = core::makeShared<Participant>();
    auto a = f.prepare(accepted), b = f.prepare(pending);
    assert(!f.domain->acceptValidator(a));
    std::thread foreign([&] {
        const char* diagnostic = nullptr;
        (void)f.domain->prepareValidatorCommand(
            pending, Participant::resolve, Participant::cancel, nullptr, 0u, diagnostic);
        expect(diagnostic, "RHI-SUBMISSION-COMPLETION-THREAD");
        expect(Domain::validateOriginal(f.domain), "RHI-SUBMISSION-COMPLETION-THREAD");
        expect(f.domain->pollValidator(), "RHI-SUBMISSION-COMPLETION-THREAD");
        expect(f.domain->acceptValidator(b), "RHI-SUBMISSION-COMPLETION-THREAD");
        expect(f.domain->cancelValidator(b), "RHI-SUBMISSION-COMPLETION-THREAD");
        expect(f.domain->retireValidator(), "RHI-SUBMISSION-COMPLETION-THREAD");
        assert(!Domain::deliverValidatorResult(a, true));
        assert(a.outcome() == Outcome::Pending);
    });
    foreign.join();
    assert(accepted->state.pending() && !accepted->resolves && !pending->cancels);
    f.quota->setLimitBytes(f.budget.maxCpuMetadataBytes - 1u);
    expect(f.domain->pollValidator(), "RHI-SUBMISSION-COMPLETION-QUOTA");
    assert(!f.domain->retireValidator());
    assert(a.outcome() == Outcome::Failed && !accepted->state.active());
    assert(b.outcome() == Outcome::Cancelled && pending->state.active());
    assert(accepted->resolves == 1u && pending->cancels == 1u);
    expect(f.domain->pollValidator(), "RHI-SUBMISSION-COMPLETION-RETIRED");

    Fixture retiredDevice;
    auto p = core::makeShared<Participant>();
    auto ticket = retiredDevice.prepare(p);
    assert(!retiredDevice.domain->acceptValidator(ticket));
    retiredDevice.device->retireOriginalDeviceLifetime();
    assert(retiredDevice.domain->diagnostic());
    retiredDevice.refuse(
        core::makeShared<Participant>(), nullptr, 0u, retiredDevice.domain->diagnostic());
    assert(!retiredDevice.domain->retireValidator());
    assert(ticket.outcome() == Outcome::Failed && !p->state.active());
}

void custodyAndJournal()
{
    Fixture f;
    auto p = core::makeShared<Participant>();
    const std::weak_ptr<Participant> weak = p;
    auto a = f.prepare(p);
    assert(!f.domain->acceptValidator(a));
    core::MutationJournal foreign;
    assert(!p->state.canPrepare(foreign));
    expect(p->state.writeBuffer(ResourceState::CopySrc, &foreign),
        "RHI-RESOURCE-STATE-RECORDING-CONFLICT");
    p.reset();
    assert(!weak.expired());
    assert(!Domain::deliverValidatorResult(a, true));
    assert(!weak.expired() && a.outcome() == Outcome::Pending);
    assert(!f.domain->pollValidator());
    assert(weak.expired() && a.outcome() == Outcome::Succeeded);
    // Tickets retain metadata but do not retain the domain or original participant.
    p = core::makeShared<Participant>();
    auto b = f.prepare(p);
    std::weak_ptr<Domain> weakDomain = f.domain;
    f.domain.reset();
    assert(weakDomain.expired() && b.outcome() == Outcome::Cancelled);
    assert(p->cancels == 1u && !p->state.pending());
    a = {};
    b = {};
    weakDomain.reset();
    assert(!f.quota->liveBytes());
}

void resolverFailure()
{
    Fixture f;
    auto p = core::makeShared<Participant>();
    p->returnSuccess = false;
    auto a = f.prepare(p);
    assert(!f.domain->acceptValidator(a));
    assert(!Domain::deliverValidatorResult(a, true));
    assert(!f.domain->pollValidator());
    assert(a.outcome() == Outcome::Failed && !p->state.active() && p->resolves == 1u);
}

void reusedSlotDependencyScan()
{
    Fixture f;
    auto spare = core::makeShared<Participant>(), parent = core::makeShared<Participant>(),
         child = core::makeShared<Participant>(), grandchild = core::makeShared<Participant>();
    auto first = f.prepare(spare), a = f.prepare(parent);
    assert(!f.domain->cancelValidator(first));
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    assert(!f.domain->acceptValidator(a));
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    auto b = f.prepare(child, &a, 1u); // reuses slot0, ahead of its predecessor
    assert(!f.domain->acceptValidator(b));
    auto c = f.prepare(grandchild, &b, 1u);
    assert(!f.domain->acceptValidator(c));
    assert(!Domain::deliverValidatorResult(c, true));
    assert(!Domain::deliverValidatorResult(b, true));
    assert(!f.domain->pollValidator());
    assert(a.outcome() == Outcome::Pending && b.outcome() == Outcome::Pending
        && c.outcome() == Outcome::Pending);
    assert(!Domain::deliverValidatorResult(a, true));
    assert(!f.domain->pollValidator()); // bounded rescan must reach b and c
    assert(a.outcome() == Outcome::Succeeded && b.outcome() == Outcome::Succeeded
        && c.outcome() == Outcome::Succeeded);
}

void factoryLifetimeRefusals()
{
    const Budget budget{1u, true, 4u, 4u, 1024u * 1024u};
    auto device = createNullDevice();
    auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
    const char* diagnostic = nullptr;
    assert(!Domain::prepareValidator(*device, budget, quota, diagnostic));
    assert(diagnostic); // no original lifetime prepared
    assert(!device->prepareOriginalDeviceLifetime(
        {1u, true, 1u, 2u, budget.maxCpuMetadataBytes}, quota));
    assert(!Domain::prepareValidator(*device, budget, quota, diagnostic));
    expect(diagnostic, "RHI-SUBMISSION-COMPLETION-QUOTA"); // independent ownership
    auto own = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
    auto domain = Domain::prepareValidator(*device, budget, own, diagnostic);
    assert(domain && !diagnostic);
    device.reset();
    expect(domain->pollValidator(), "RHI-ORIGINAL-DEVICE-LIFETIME-EXPIRED");
    assert(!domain->retireValidator());
}

void irreversibleRetirementProof()
{
    Fixture f;
    const auto ticket = f.device->originalDeviceLifetimeTicket();
    assert(!OriginalDeviceLifetime::Ticket{}.terminallyRevoked());
    assert(!ticket.terminallyRevoked());
    expect(
        f.domain->retireValidatorAfterResults(), "RHI-SUBMISSION-COMPLETION-RETIRE-DEVICE-ACTIVE");
    f.quota->setLimitBytes(f.budget.maxCpuMetadataBytes - 1u);
    assert(!ticket.terminallyRevoked());
    expect(
        f.domain->retireValidatorAfterResults(), "RHI-SUBMISSION-COMPLETION-RETIRE-DEVICE-ACTIVE");
    f.device->retireOriginalDeviceLifetime();
    assert(ticket.terminallyRevoked());
    assert(!f.domain->retireValidatorAfterResults());
    f.device.reset();
    assert(ticket.terminallyRevoked());
    assert(!OriginalDeviceLifetime::Ticket{}.terminallyRevoked());
}

void deferredRetirementCustody()
{
    Fixture f;
    auto parent = core::makeShared<Participant>(), child = core::makeShared<Participant>(),
         unaccepted = core::makeShared<Participant>();
    auto a = f.prepare(parent);
    assert(!f.domain->acceptValidator(a));
    auto b = f.prepare(child, &a, 1u);
    assert(!f.domain->acceptValidator(b));
    auto c = f.prepare(unaccepted);
    std::atomic<unsigned> terminalResolutions{0u};
    for (auto& p : {parent, child}) {
        p->retiredTicket = f.device->originalDeviceLifetimeTicket();
        p->terminalCounter = &terminalResolutions;
    }
    const std::weak_ptr<Participant> weakParent = parent, weakChild = child;
    std::weak_ptr<Domain> weakDomain = f.domain;
    DeliveryGate parentGo, childGo;
    std::thread first([domain = f.domain, a, &parentGo]() mutable {
        parentGo.wait();
        assert(!Domain::deliverValidatorResult(a, true));
        domain.reset();
    });
    std::thread last([domain = f.domain, b, &childGo]() mutable {
        childGo.wait();
        assert(!Domain::deliverValidatorResult(b, true));
        domain.reset();
    });
    f.device->retireOriginalDeviceLifetime();
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    assert(!f.domain->retireValidatorAfterResults());
    assert(c.outcome() == Outcome::Cancelled && unaccepted->cancels == 1u);
    expect(f.domain->drainRetiredValidatorUntil({}),
        semantics::kShutdownDrainInvalidDeadlineDiagnostic);
    expect(f.domain->drainRetiredValidatorUntil(
               {2u, std::chrono::steady_clock::now() + std::chrono::seconds(1)}),
        semantics::kShutdownDrainInvalidDeadlineDiagnostic);
    expect(f.domain->drainRetiredValidatorUntil(
               {1u, std::chrono::steady_clock::now() - std::chrono::milliseconds(1)}),
        semantics::kShutdownDrainDeadlineExpiredDiagnostic);
    expect(f.domain->drainRetiredValidatorUntil(
               {1u, std::chrono::steady_clock::now() + std::chrono::milliseconds(2)}),
        semantics::kShutdownDrainDeadlineExpiredDiagnostic);
    assert(a.outcome() == Outcome::Pending && b.outcome() == Outcome::Pending);
    assert(parent->state.pending() && child->state.pending());
    assert(!terminalResolutions.load() && !parent->cancels && !child->cancels);
    parent.reset();
    child.reset();
    f.domain.reset();
    f.device.reset();
    parentGo.open();
    first.join();
    // The final result producer retains the entire retired domain. Even the
    // first result's participant survives until all affected results arrive.
    assert(!weakDomain.expired() && !weakParent.expired() && !weakChild.expired());
    assert(!terminalResolutions.load() && a.outcome() == Outcome::Pending);
    childGo.open();
    last.join();
    assert(weakDomain.expired() && weakParent.expired() && weakChild.expired());
    assert(terminalResolutions.load() == 2u);
    assert(a.outcome() == Outcome::Failed && b.outcome() == Outcome::Failed);
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    a = b = c = {};
    weakDomain.reset();
    assert(!f.quota->liveBytes());
}

void ownerDeadlineDrain()
{
    Fixture f;
    auto p = core::makeShared<Participant>();
    auto a = f.prepare(p);
    assert(!f.domain->acceptValidator(a));
    expect(f.domain->drainRetiredValidatorUntil(
               {1u, std::chrono::steady_clock::now() + std::chrono::seconds(1)}),
        "RHI-SUBMISSION-COMPLETION-RETIRE-PHASE");
    f.device->retireOriginalDeviceLifetime();
    assert(!f.domain->retireValidatorAfterResults());
    DeliveryGate go;
    std::thread worker([domain = f.domain, a, &go] {
        go.wait();
        expect(domain->retireValidatorAfterResults(), "RHI-SUBMISSION-COMPLETION-THREAD");
        expect(domain->drainRetiredValidatorUntil({}), "RHI-SUBMISSION-COMPLETION-THREAD");
        assert(!Domain::deliverValidatorResult(a, true));
    });
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    go.open();
    assert(!f.domain->drainRetiredValidatorUntil(
        {1u, std::chrono::steady_clock::now() + std::chrono::seconds(5)}));
    worker.join();
    assert(a.outcome() == Outcome::Failed && p->resolves == 1u && !p->cancels);
    assert(!p->state.active() && !p->state.pending());
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    assert(!f.domain->retireValidatorAfterResults());
    assert(p->resolves == 1u);
}

Participant* forbiddenDestructionParticipant = nullptr;

[[noreturn]] void rejectedDestruction() noexcept
{
    const auto* p = forbiddenDestructionParticipant;
    if (!p || !p->state.pending() || p->resolves || p->cancels)
        std::_Exit(88);
    std::fputs("destruction refused before journal mutation\n", stdout);
    std::fflush(stdout);
    std::_Exit(87);
}

void forbiddenDestruction(bool retired)
{
    Fixture f;
    auto p = core::makeShared<Participant>();
    auto a = f.prepare(p);
    assert(!f.domain->acceptValidator(a));
    forbiddenDestructionParticipant = p.get();
    std::set_terminate(rejectedDestruction);
    if (retired) {
        f.device->retireOriginalDeviceLifetime();
        assert(!f.domain->retireValidatorAfterResults());
        // No retained result producer: destroying pending custody must refuse.
        f.domain.reset();
    } else {
        std::thread foreign([domain = std::move(f.domain)]() mutable {
            domain.reset();
        });
        foreign.join();
    }
    // A silent rollback or foreign-thread journal resolution must never pass.
    std::_Exit(89);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--forbid-unresolved-retirement") == 0)
        forbiddenDestruction(true);
    if (argc == 2 && std::strcmp(argv[1], "--forbid-active-foreign-destruction") == 0)
        forbiddenDestruction(false);
    if (argc == 2 && std::strcmp(argv[1], "--null-smoke") == 0) {
        test::OriginalSubmittedBufferReadPinsFixture::verify(true);
        dependencyOrder(true, true, true);
        dependencyOrder(true, false, true);
        custodyAndJournal();
        deferredRetirementCustody();
        ownerDeadlineDrain();
        std::puts(
            "Null validator owner/journal dependency smoke passed; no native completion proof");
        return 0;
    }
    assert(argc == 1);
    contract();
    test::OriginalSubmittedBufferReadPinsFixture::verify();
    for (bool childFirst : {false, true})
        for (bool parentSuccess : {false, true})
            for (bool childSuccess : {false, true})
                dependencyOrder(childFirst, parentSuccess, childSuccess);
    refusalsAndCancellation();
    capacitiesAndQuota();
    threadAndRetirement();
    custodyAndJournal();
    resolverFailure();
    reusedSlotDependencyScan();
    factoryLifetimeRefusals();
    irreversibleRetirementProof();
    deferredRetirementCustody();
    ownerDeadlineDrain();
    std::puts(
        "OriginalSubmissionCompletion1 bounded Null validator contracts passed; native gates closed");
}
