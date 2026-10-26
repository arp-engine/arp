#include "editor/tests/fixtures/recovery_fixture.h"

#include <array>
#include <new>

namespace {
using namespace arp;
using Session = editor::ScriptDocumentSession;

// Compare semantic state, including history payloads, rather than capacities.
class Snapshot final {
public:
    explicit Snapshot(const Session& session)
        : document_(*session.active()), ticket_(session.stateTicket()), id_(session.activeId())
    {}

    bool unchanged(const Session& session) const
    {
        const auto& now = *session.active();
        return session.activeId() == id_ && ticket_.isCurrent() && now.path == document_.path
            && now.text == document_.text && now.savedText == document_.savedText
            && now.savedFileBytes == document_.savedFileBytes && now.newline == document_.newline
            && now.cursor == document_.cursor && now.anchor == document_.anchor
            && now.scrollLine == document_.scrollLine && history(now.undo, document_.undo)
            && history(now.redo, document_.redo);
    }

private:
    static bool history(
        const core::pmr::Vector<Session::Edit>& a, const core::pmr::Vector<Session::Edit>& b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i].offset != b[i].offset || a[i].removed != b[i].removed
                || a[i].inserted != b[i].inserted || a[i].cursorBefore != b[i].cursorBefore
                || a[i].anchorBefore != b[i].anchorBefore || a[i].cursorAfter != b[i].cursorAfter
                || a[i].anchorAfter != b[i].anchorAfter)
                return false;
        }
        return true;
    }

    Session::Document document_;
    core::LifetimeEpoch::Ticket ticket_;
    std::uint64_t id_;
};

void initialize(Session& session)
{
    Session::Document document;
    document.text = "initial text";
    document.savedText = document.text;
    document.savedFileBytes = document.text;
    document.cursor = 7;
    document.anchor = 2;
    document.scrollLine = 3;
    document.text.reserve(8192);
    assert(session.setActiveIndex(session.append(std::move(document))));
}

enum class Operation { Replace, Undo, Redo };

bool failure(Operation operation, std::size_t budget, bool growing = false)
{
    Session session;
    initialize(session);
    const core::pmr::String large(4096, 'x');
    if (operation != Operation::Replace) {
        if (growing && operation == Operation::Undo) {
            assert(session.replaceActiveRange(
                       0, session.active()->text.size(), large, Session::maxScriptBytes)
                == Session::EditResult::Applied);
        }
        assert(session.replaceActiveRange(
                   0, session.active()->text.size(), "short", Session::maxScriptBytes)
            == Session::EditResult::Applied);
        if (operation == Operation::Redo) {
            if (growing) {
                assert(session.replaceActiveRange(
                           0, session.active()->text.size(), large, Session::maxScriptBytes)
                    == Session::EditResult::Applied);
            }
            assert(session.undoActive());
            // Empty the destination so the next transfer must allocate.
            session.active()->undo.clear();
            session.active()->undo.shrink_to_fit();
        }
        session.active()->text.shrink_to_fit();
    }
    const Snapshot before(session);
    auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
    bool threw = false;
    {
        core::CpuMemoryQuotaScope scope(quota);
        try {
            if (operation == Operation::Replace)
                session.replaceActiveRange(
                    0, 1, growing ? std::string_view(large) : "z", Session::maxScriptBytes);
            else if (operation == Operation::Undo)
                session.undoActive();
            else
                session.redoActive();
        } catch (const std::bad_alloc&) {
            threw = true;
        }
    }
    if (!threw)
        return true;
    if (!before.unchanged(session))
        return false;
    // Retry the very same operation without rejection, then exercise history.
    if (operation == Operation::Replace) {
        assert(session.replaceActiveRange(
                   0, 1, growing ? std::string_view(large) : "z", Session::maxScriptBytes)
            == Session::EditResult::Applied);
        assert(session.undoActive() && session.redoActive());
    } else if (operation == Operation::Undo) {
        assert(session.undoActive() && session.redoActive());
    } else {
        assert(session.redoActive() && session.undoActive());
    }
    return true;
}

bool saveFailure(const editor::tests::RecoveryFixture& files, std::size_t budget)
{
    const auto path = files.root / "transaction.arp";
    editor::tests::RecoveryFixture::write(path, "old\r\n");
    Session session;
    const auto opened = session.openFile(files.root, path);
    assert(opened && session.setActiveIndex(opened.documentIndex));
    const core::pmr::String text(8192, 's');
    assert(
        session.replaceActiveRange(0, session.active()->text.size(), text, Session::maxScriptBytes)
        == Session::EditResult::Applied);
    const Snapshot before(session);
    auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
    bool threw = false, saved = false;
    {
        core::CpuMemoryQuotaScope scope(quota);
        try {
            saved = session.saveActiveFile(files.root).code == Session::FileResultCode::Saved;
        } catch (const std::bad_alloc&) {
            threw = true;
        }
    }
    const auto disk = core::readTextFile(path);
    assert(disk.success);
    if (threw || !saved) {
        if (!before.unchanged(session) || disk.text != "old\r\n")
            return false;
        assert(session.saveActiveFile(files.root).code == Session::FileResultCode::Saved);
    }
    assert(!session.active()->dirty() && session.active()->savedFileBytes == text);
    assert(core::readTextFile(path).text == text);
    // Successful save retains history and defines the new save point.
    assert(session.undoActive() && session.active()->dirty());
    assert(session.redoActive() && !session.active()->dirty());
    return true;
}

void normalContract()
{
    Session session;
    initialize(session);
    const auto epoch = session.stateTicket();
    assert(session.replaceActiveRange(0, 0, "", Session::maxScriptBytes)
        == Session::EditResult::Unchanged);
    assert(session.replaceActiveRange(0, 0, "xx", 1) == Session::EditResult::Rejected);
    assert(epoch.isCurrent());
    const auto original = session.active()->text;
    const auto alias = std::string_view(session.active()->text).substr(2, 8);
    assert(session.replaceActiveRange(0, 1, alias, Session::maxScriptBytes)
        == Session::EditResult::Applied);
    assert(session.active()->text == original.substr(2, 8) + original.substr(1));
    assert(!epoch.isCurrent() && session.undoActive() && session.active()->text == original);
    assert(session.active()->cursor == 7 && session.active()->anchor == 2);
    assert(session.redoActive() && session.active()->cursor == 8);
    assert(session.undoActive());
    assert(session.replaceActiveRange(0, 1, "branch", Session::maxScriptBytes)
        == Session::EditResult::Applied);
    assert(!session.redoActive());
}
} // namespace

int main(int argc, char** argv)
{
    editor::tests::RecoveryFixture files;
    const bool probe = argc == 2 && std::string_view(argv[1]) == "--probe";
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    if (probe) {
        bool passed = true;
        for (const auto operation : {Operation::Replace, Operation::Undo, Operation::Redo}) {
            const auto ok = failure(operation, 1);
            std::printf("%s operation=%u allocation rejection preserves complete state\n",
                ok ? "PASS" : "FAIL",
                static_cast<unsigned>(operation));
            passed &= ok;
        }
        const auto ok = saveFailure(files, 20000);
        std::printf(
            "%s save allocation rejection preserves disk and baseline\n", ok ? "PASS" : "FAIL");
        return passed && ok ? 0 : 2;
    }
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= (smoke ? 20u : 0u); ++cycle) {
        normalContract();
        for (const auto operation : {Operation::Replace, Operation::Undo, Operation::Redo}) {
            for (const auto budget : std::array<std::size_t, 6>{1, 128, 4096, 4097, 8192, 16384}) {
                assert(failure(operation, budget));
                assert(failure(operation, budget, true));
            }
        }
        // A bounded fault matrix around the 8 KiB text and two encoded buffers.
        for (const auto budget :
            std::array<std::size_t, 8>{1, 128, 8192, 16384, 18000, 20000, 24576, 32768})
            assert(saveFailure(files, budget));
        const auto now = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = now;
        assert(now.liveBytes == baseline.liveBytes);
        assert(now.allocationCount - now.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS edit/undo/redo/save bounded allocation failures, retry, alias, branch and save point");
    if (smoke)
        std::puts(
            "PASS 20 warmed ordinary document lifecycle cycles: core live bytes/count delta=0");
}
