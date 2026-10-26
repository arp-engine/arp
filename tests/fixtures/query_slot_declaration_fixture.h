#pragma once
#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/query_slot_declaration_control.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class QuerySlotDeclarationFixture final {
public:
    static void verify()
    {
        using Control = rhi::common::QuerySlotDeclarationControl;
        using Phase = Control::Phase;
        using Event = Control::Event;
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto l = Control::lowering(backend);
            assert(l.declarationImplemented && l.validatorOnly == (backend == rhi::Backend::Null)
                && l.diagnostic);
        }
        assert(!Control::lowering(static_cast<rhi::Backend>(99u)).declarationImplemented);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto old = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        Control control;
        auto initial = control.observe();
        assert(initial);
        for (const auto event :
            {Event::Submit, Event::Complete, Event::DeclareSnapshot, Event::RetireFailure}) {
            assert(control.declare(event) && control.matches(*initial));
        }
        assert(!control.declare(Event::Record) && !control.matches(*initial));
        assert(control.declare(Event::Record) && control.declare(Event::Reset));
        assert(!control.declare(Event::Submit) && control.declare(Event::Reset));
        assert(!control.declare(Event::Complete) && control.declare(Event::Reset));
        assert(
            !control.declare(Event::DeclareSnapshot) && control.phase() == Phase::SnapshotDeclared);
        assert(!control.nativeAvailabilityProven() && !control.nativeExecutionProven());
        assert(!control.declare(Event::Reset) && control.phase() == Phase::NotRecorded
            && !control.matches(*initial));
        for (const auto failPhase : {Phase::Recorded, Phase::Submitted, Phase::Completed}) {
            Control failed(failPhase);
            assert(!failed.declare(Event::Fail) && failed.phase() == Phase::Quarantined
                && failed.declare(Event::Reset));
            assert(failed.declare(Event::Fail) && failed.declare(Event::DeclareSnapshot));
            assert(!failed.declare(Event::RetireFailure) && failed.phase() == Phase::FailedIdle);
            assert(!failed.declare(Event::Reset));
        }
        for (const auto failPhase : {Phase::NotRecorded, Phase::SnapshotDeclared}) {
            Control failed(failPhase);
            assert(!failed.declare(Event::Fail) && failed.phase() == Phase::FailedIdle
                && !failed.declare(Event::Reset));
        }
        assert(!Control::after(static_cast<Phase>(99u), Event::Reset));
        assert(!Control::after(Phase::NotRecorded, static_cast<Event>(99u)));
        Control unknown(static_cast<Phase>(99u));
        assert(!unknown.observe() && unknown.declare(Event::Record));
        {
            core::MutationJournal journal, foreign;
            const auto before = control.observe();
            assert(before);
            assert(!control.declare(Event::Record, &journal));
            const auto pending = control.observePending(journal);
            assert(pending && !control.observe() && !control.matches(*before));
            assert(control.matchesPending(*pending, journal)
                && !control.matchesPending(*pending, foreign));
            const auto revision = control.revision();
            assert(control.declare(Event::Submit, &foreign) && foreign.empty()
                && control.revision() == revision);
            assert(control.declare(Event::Submit) && control.declare(Event::Reset, &journal));
            assert(!control.declare(Event::Submit, &journal)
                && !control.matchesPending(*pending, journal));
            const auto rewritten = control.observePending(journal);
            assert(rewritten);
            journal.rollback();
            assert(control.phase() == Phase::NotRecorded && !control.matches(*before)
                && !control.matchesPending(*rewritten, journal));
            assert(!control.declare(Event::Record, &journal));
            journal.commit();
            assert(control.phase() == Phase::Recorded && control.observe()
                && !control.observePending(journal));
        }
        // Cancel transitions must advance identity even when the value returns.
        {
            Control cancel;
            const auto before = cancel.observe();
            assert(before && !cancel.preflight(Event::Record));
            assert(cancel.matches(*before));
            assert(!cancel.declare(Event::Record));
            const auto recorded = cancel.observe();
            assert(recorded && !cancel.declare(Event::Submit));
            assert(!cancel.declare(Event::CancelSubmit) && cancel.phase() == Phase::Recorded
                && !cancel.matches(*recorded));
            assert(!cancel.declare(Event::CancelRecord) && cancel.phase() == Phase::NotRecorded
                && !cancel.matches(*before));
            assert(cancel.declare(Event::CancelRecord) && cancel.declare(Event::CancelSubmit));
            core::MutationJournal own, other;
            assert(!cancel.declare(Event::Record, &own));
            const auto first = cancel.observePending(own);
            assert(first && !cancel.preflight(Event::CancelRecord, &own));
            assert(cancel.matchesPending(*first, own));
            assert(cancel.preflight(Event::CancelRecord, &other) && other.empty());
            assert(!cancel.declare(Event::CancelRecord, &own)
                && !cancel.declare(Event::Record, &own) && !cancel.matchesPending(*first, own));
            own.rollback();
            assert(cancel.phase() == Phase::NotRecorded);
        }
        // A dry preflight cannot consume revisions, enlist, or invalidate.
        {
            for (const bool failed : {false, true}) {
                Control lifecycle(Phase::NotRecorded, UINT64_MAX - 4u);
                assert(!lifecycle.preflight(Event::Record, nullptr, 4u)
                    && !lifecycle.declare(Event::Record)
                    && !lifecycle.preflight(Event::Submit, nullptr, 3u)
                    && !lifecycle.declare(Event::Submit));
                assert(!lifecycle.declare(failed ? Event::Fail : Event::Complete));
                assert(!lifecycle.declare(failed ? Event::RetireFailure : Event::DeclareSnapshot));
                assert(lifecycle.active() && lifecycle.preflight(Event::Reset));
            }
            Control lifecycleShort(Phase::NotRecorded, UINT64_MAX - 3u);
            const auto lifecycleBefore = lifecycleShort.observe();
            assert(lifecycleBefore && lifecycleShort.preflight(Event::Record, nullptr, 4u)
                && lifecycleShort.matches(*lifecycleBefore));
            Control exact(Phase::Submitted, UINT64_MAX - 2u);
            const auto before = exact.observe();
            assert(
                before && !exact.preflight(Event::Complete, nullptr, 2u) && exact.matches(*before));
            assert(!exact.declare(Event::Complete) && !exact.declare(Event::DeclareSnapshot));
            const auto last = exact.observe();
            assert(last && exact.preflight(Event::Reset) && exact.matches(*last));
            Control shortByOne(Phase::Submitted, UINT64_MAX - 1u);
            const auto shortBefore = shortByOne.observe();
            assert(shortBefore && shortByOne.preflight(Event::Complete, nullptr, 2u)
                && shortByOne.matches(*shortBefore));
            Control journalShort(Phase::NotRecorded, UINT64_MAX - 1u);
            core::MutationJournal journal;
            const auto initial = journalShort.observe();
            assert(initial && journalShort.preflight(Event::Record, &journal) && journal.empty()
                && journalShort.matches(*initial));
            assert(journalShort.preflight(Event::Record, nullptr, 0u));
        }
        Control terminal;
        core::MutationJournal journal;
        assert(!terminal.declare(Event::Record, &journal));
        const auto pending = terminal.observePending(journal);
        assert(pending);
        terminal.invalidate();
        journal.rollback();
        assert(!terminal.active() && !terminal.observe()
            && !terminal.matchesPending(*pending, journal) && terminal.declare(Event::Reset));
        Control last(Phase::NotRecorded, UINT64_MAX);
        core::MutationJournal exhausted;
        assert(std::string_view(last.declare(Event::Record, &exhausted))
                == "RHI-QUERY-SLOT-REVISION-EXHAUSTED"
            && exhausted.empty() && !last.active());
        Control rollbackLast(Phase::NotRecorded, UINT64_MAX - 1u);
        {
            core::MutationJournal rollback;
            assert(!rollbackLast.declare(Event::Record, &rollback));
            rollback.rollback();
        }
        assert(!rollbackLast.active());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(old);
        std::puts(
            "BINDLESS_RHI_QUERY_SLOT_DECLARATION_OK phases7 events9 quarantine reset journal-ABA exhaustion backends4 native-proof-closed allocation0");
    }
};
} // namespace arp::test
