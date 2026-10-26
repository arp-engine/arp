#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/workspace/script_document_session.h"

#include <limits>
#include <memory_resource>
#include <type_traits>

namespace {
using namespace arp;
using namespace arp::editor;
using Session = ScriptDocumentSession;
using Documents = core::pmr::Vector<Session::Document>;

Session::Document restored(std::string_view name = "rules.arp")
{
    Session::Document document;
    document.path = std::filesystem::absolute(std::filesystem::u8path(name)).lexically_normal();
    document.savedFileBytes = "# saved\r\n# second\n";
    document.savedText = Session::normalizeNewlines(document.savedFileBytes);
    document.text = "# unsaved\n";
    document.newline = "\r\n";
    document.cursor = 4;
    document.anchor = 1;
    document.scrollLine = 7;
    return document;
}

void replacement()
{
    static_assert(!std::is_copy_constructible_v<Session> && !std::is_move_constructible_v<Session>);
    Session live;
    const auto oldIndex = live.append(restored("old.arp"));
    assert(live.setActiveIndex(oldIndex));
    const auto oldId = live.activeId();
    assert(live.replaceActiveRange(0, 0, "# change\n", Session::maxScriptBytes)
        == Session::EditResult::Applied);
    assert(live.undoActive() && live.active()->redo.size() == 1);
    const auto oldText = live.active()->text;
    Documents prepared;
    prepared.push_back(restored());
    prepared.push_back(restored("other.arp"));
    const auto baseline = prepared[1];
    core::pmr::String diagnostic = "previous feedback";
    assert(live.replaceRecoveredDocuments(prepared, 1, diagnostic) && diagnostic.empty());
    assert(live.size() == 2 && live.activeIndex() == 1 && live.activeId() > oldId
        && live.documents()[0].id > oldId && live.documents()[0].id < live.activeId()
        && !live.find(oldId) && !live.find(0));
    assert(prepared.size() == 1 && prepared[0].id == oldId && prepared[0].text == oldText
        && prepared[0].redo.size() == 1);
    assert(live.active()->text == baseline.text && live.active()->savedText == baseline.savedText
        && live.active()->savedFileBytes == baseline.savedFileBytes
        && live.active()->newline == baseline.newline && live.activeCursor() == baseline.cursor
        && live.activeAnchor() == baseline.anchor && live.activeScrollLine() == baseline.scrollLine
        && live.active()->dirty() && !live.undoActive() && !live.redoActive());
    const auto latest = live.activeId();
    assert(live.erase(1) && live.erase(0));
    assert(live.setActiveIndex(live.append(restored("reopened.arp"))));
    assert(live.activeId() > latest && !live.find(oldId) && !live.find(latest));
    std::puts(
        "PASS restored documents receive fresh live IDs; retired history and exact baselines retained; old targets rejected");
}

void rejectedInputs()
{
    Session live;
    assert(live.setActiveIndex(live.append(restored("live.arp"))));
    const auto id = live.activeId();
    assert(live.replaceActiveRange(0, 0, "x", Session::maxScriptBytes)
        == Session::EditResult::Applied);
    const auto text = live.active()->text;
    const auto reject = [&](Documents& prepared, std::size_t active, const char* expected) {
        core::pmr::String diagnostic;
        const auto count = prepared.size();
        const auto inputId = count ? prepared[0].id : 0;
        assert(!live.replaceRecoveredDocuments(prepared, active, diagnostic)
            && diagnostic == expected && live.activeId() == id && live.active()->text == text
            && live.active()->undo.size() == 1 && live.size() == 1 && prepared.size() == count
            && (!count || prepared[0].id == inputId));
    };
    Documents prepared;
    prepared.push_back(restored());
    reject(prepared, 1, "EDITOR-SCRIPT-RECOVERY-ACTIVE-INDEX");
    prepared[0].id = id;
    reject(prepared, 0, "EDITOR-SCRIPT-RECOVERY-DOCUMENT");
    prepared[0].id = 0;
    prepared[0].undo.emplace_back();
    reject(prepared, 0, "EDITOR-SCRIPT-RECOVERY-DOCUMENT");
    prepared[0].undo.clear();
    prepared[0].redo.emplace_back();
    reject(prepared, 0, "EDITOR-SCRIPT-RECOVERY-DOCUMENT");
    prepared[0] = restored();
    prepared[0].cursor = prepared[0].text.size() + 1;
    reject(prepared, 0, "EDITOR-SCRIPT-RECOVERY-DOCUMENT");
    prepared[0] = restored();
    prepared[0].savedText = "wrong disk baseline";
    reject(prepared, 0, "EDITOR-SCRIPT-RECOVERY-DOCUMENT");
    prepared[0] = restored();
    prepared[0].text = "\r";
    reject(prepared, 0, "EDITOR-SCRIPT-RECOVERY-DOCUMENT");
    prepared[0] = restored();
    prepared[0].newline = "\r";
    reject(prepared, 0, "EDITOR-SCRIPT-RECOVERY-DOCUMENT");
    prepared[0] = restored();
    prepared[0].path = "relative.arp";
    reject(prepared, 0, "EDITOR-SCRIPT-RECOVERY-DOCUMENT");
    prepared[0] = restored();
    prepared[0].text.assign(Session::maxScriptBytes + 1, 'x');
    reject(prepared, 0, "EDITOR-SCRIPT-RECOVERY-DOCUMENT");
    prepared[0] = restored();
    prepared.push_back(restored());
    reject(prepared, 0, "EDITOR-SCRIPT-RECOVERY-DUPLICATE-PATH");
    // Validation is all-or-nothing even when the later document is invalid.
    prepared[1] = restored("other.arp");
    prepared[1].anchor = prepared[1].text.size() + 1;
    reject(prepared, 0, "EDITOR-SCRIPT-RECOVERY-DOCUMENT");
    prepared[1] = restored("other.arp");
    std::byte storage[4096];
    std::pmr::monotonic_buffer_resource foreign(
        storage, sizeof(storage), std::pmr::null_memory_resource());
    Documents foreignVector(&foreign);
    reject(foreignVector, Session::noActiveDocument, "EDITOR-SCRIPT-RECOVERY-ALLOCATOR-OR-ALIAS");
    // Construct a new element: PMR move assignment retains its old allocator.
    prepared.pop_back();
    prepared.push_back(
        Session::Document{0, restored("other.arp").path, core::pmr::String("foreign", &foreign)});
    reject(prepared, 0, "EDITOR-SCRIPT-RECOVERY-ALLOCATOR-OR-ALIAS");
    prepared.pop_back();
    prepared.push_back(restored("other.arp"));
    core::pmr::String diagnostic;
    assert(live.replaceRecoveredDocuments(prepared, 0, diagnostic)
        && live.documents()[0].id == id + 1 && live.documents()[1].id == id + 2);
    std::puts(
        "PASS invalid recovery input preserves live documents, history, prepared identities and next ID");
}

void exhaustionAndFiles()
{
    constexpr auto last = std::numeric_limits<std::uint64_t>::max();
    Session live(last - 1);
    assert(live.setActiveIndex(live.append(restored("old.arp"))));
    Documents prepared;
    prepared.push_back(restored());
    prepared.push_back(restored("other.arp"));
    core::pmr::String diagnostic;
    assert(!live.replaceRecoveredDocuments(prepared, 0, diagnostic)
        && diagnostic == "EDITOR-SCRIPT-IDENTITY-EXHAUSTED" && live.activeId() == last - 1
        && prepared[0].id == 0 && prepared[1].id == 0);
    prepared.pop_back();
    assert(live.replaceRecoveredDocuments(prepared, 0, diagnostic) && live.activeId() == last);
    assert(live.append(restored("later.arp")) == Session::noActiveDocument && live.size() == 1);
    tests::RecoveryFixture fixture;
    const auto path = fixture.root / "disk.arp";
    tests::RecoveryFixture::write(path, "# original\r\n");
    const auto bytes = tests::RecoveryFixture::read(path);
    assert(live.openFile(fixture.root, path).code == Session::FileResultCode::IdentityExhausted
        && live.activeId() == last && tests::RecoveryFixture::read(path) == bytes);
    Documents empty;
    assert(live.replaceRecoveredDocuments(empty, Session::noActiveDocument, diagnostic)
        && live.size() == 0 && live.activeId() == 0 && empty[0].id == last);
    assert(live.append(restored()) == Session::noActiveDocument);
    Session none(0);
    assert(none.append(restored()) == Session::noActiveDocument);
    Documents inactive;
    inactive.push_back(restored());
    Session available;
    assert(available.replaceRecoveredDocuments(inactive, Session::noActiveDocument, diagnostic)
        && available.size() == 1 && available.activeId() == 0);
    std::puts(
        "PASS batch identity exhaustion is atomic, UINT64_MAX used once, empty replacement does not reset, files kept");
}

void smoke()
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            Session live;
            live.append(restored("old.arp"));
            Documents prepared;
            prepared.push_back(restored());
            prepared.push_back(restored("other.arp"));
            core::pmr::String diagnostic;
            assert(live.replaceRecoveredDocuments(prepared, 1, diagnostic));
            assert(live.replaceActiveRange(0, 0, "x", Session::maxScriptBytes)
                == Session::EditResult::Applied);
            assert(live.undoActive() && live.redoActive());
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary script replacement/edit/history destruction cycles: core live bytes/count delta=0");
}

void preparedReplacement()
{
    using Result = Session::RecoveryCommitResult;
    static_assert(noexcept(std::declval<Session&>().commitRecoveredDocuments(
        std::declval<Session::PreparedRecovery&>())));
    Session live;
    assert(live.setActiveIndex(live.append(restored("old.arp"))));
    assert(live.replaceActiveRange(0, 0, "# old history\n", Session::maxScriptBytes)
        == Session::EditResult::Applied);
    const auto oldId = live.activeId();
    const auto oldText = live.active()->text;
    const auto epoch = live.stateTicket();
    Documents input{restored(), restored("other.arp")};
    auto* inputAlias = &input[0];
    core::pmr::String diagnostic;
    auto prepared = live.prepareRecoveredDocuments(input, 1, diagnostic);
    assert(prepared && input.size() == 2 && input[0].id == 0 && prepared->documents().size() == 2
        && prepared->documents()[0].id == 0 && !prepared->committed() && live.activeId() == oldId
        && live.active()->text == oldText && epoch.isCurrent());
    const auto preparedText = prepared->documents()[0].text;
    inputAlias->text = "# changed through preexisting alias\n";
    inputAlias->id = 777;
    assert(prepared->documents()[0].text == preparedText && prepared->documents()[0].id == 0);
    const auto before = core::defaultCpuAllocator().stats();
    assert(live.recoveryCommitReadiness(*prepared) == Result::Ready);
    assert(live.commitRecoveredDocuments(*prepared) == Result::Committed);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.allocationCount == after.allocationCount
        && before.deallocationCount == after.deallocationCount);
    assert(prepared->committed() && prepared->documents().size() == 1
        && prepared->documents()[0].id == oldId && prepared->documents()[0].text == oldText
        && prepared->documents()[0].undo.size() == 1 && live.activeIndex() == 1
        && live.documents()[0].id == oldId + 1 && live.activeId() == oldId + 2
        && live.documents()[0].text == preparedText && input[0].id == 777 && !epoch.isCurrent()
        && !live.find(oldId));
    assert(live.commitRecoveredDocuments(*prepared) == Result::Consumed
        && live.recoveryCommitReadiness(*prepared) == Result::Consumed);
    std::puts(
        "PASS prepared replacement snapshots provisional inputs, allocation-free commit assigns original-sequence IDs once and retains retired history");
}

void preparedLifetimeAndFailures()
{
    using Result = Session::RecoveryCommitResult;
    core::pmr::String diagnostic;
    Session live;
    assert(live.setActiveIndex(live.append(restored("old.arp"))));
    const auto id = live.activeId();
    Documents invalid{restored()};
    invalid[0].savedText = "incorrect baseline";
    assert(!live.prepareRecoveredDocuments(invalid, 0, diagnostic) && invalid.size() == 1
        && invalid[0].id == 0 && live.activeId() == id);
    Session::PreparedRecovery empty;
    assert(live.commitRecoveredDocuments(empty) == Result::Incomplete);
    Documents input{restored()};
    auto abandoned = live.prepareRecoveredDocuments(input, 0, diagnostic);
    assert(abandoned && input.size() == 1 && input[0].id == 0);
    abandoned.reset();
    auto stale = live.prepareRecoveredDocuments(input, 0, diagnostic);
    assert(stale);
    assert(live.replaceActiveRange(0, 0, "x", Session::maxScriptBytes)
        == Session::EditResult::Applied);
    assert(live.undoActive());
    const auto before = core::defaultCpuAllocator().stats();
    assert(live.commitRecoveredDocuments(*stale) == Result::Stale);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.allocationCount == after.allocationCount
        && before.deallocationCount == after.deallocationCount && stale->documents()[0].id == 0
        && live.activeId() == id);
    auto prepared = live.prepareRecoveredDocuments(input, 0, diagnostic);
    assert(prepared);
    Session another;
    assert(another.commitRecoveredDocuments(*prepared) == Result::Stale);
    assert(
        live.commitRecoveredDocuments(*prepared) == Result::Committed && live.activeId() == id + 1);
    core::UniquePtr<Session::PreparedRecovery> expired;
    {
        Session owner;
        Documents documents{restored()};
        expired = owner.prepareRecoveredDocuments(documents, 0, diagnostic);
        assert(expired);
    }
    assert(another.commitRecoveredDocuments(*expired) == Result::Stale
        && expired->documents()[0].id == 0);
    constexpr auto last = std::numeric_limits<std::uint64_t>::max();
    Session bounded(last);
    Documents tooMany{restored(), restored("other.arp")};
    assert(!bounded.prepareRecoveredDocuments(tooMany, 0, diagnostic)
        && diagnostic == "EDITOR-SCRIPT-IDENTITY-EXHAUSTED" && tooMany[0].id == 0);
    tooMany.pop_back();
    auto final = bounded.prepareRecoveredDocuments(tooMany, 0, diagnostic);
    assert(final && bounded.commitRecoveredDocuments(*final) == Result::Committed
        && bounded.activeId() == last);
    Documents noDocuments;
    Session noContent;
    const auto noOpEpoch = noContent.stateTicket();
    auto noOp =
        noContent.prepareRecoveredDocuments(noDocuments, Session::noActiveDocument, diagnostic);
    assert(noOp && noContent.commitRecoveredDocuments(*noOp) == Result::Committed
        && noOpEpoch.isCurrent());
    assert(noContent.commitRecoveredDocuments(*noOp) == Result::Consumed);
    std::puts(
        "PASS abandoned/stale/cross-owner/destroyed/consumed tokens preserve live state and IDs, exhaustion fails atomically and empty replacement stays no-op");
}

void preparedSmoke()
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            Session live;
            assert(live.setActiveIndex(live.append(restored("old.arp"))));
            Documents input{restored(), restored("other.arp")};
            core::pmr::String diagnostic;
            auto prepared = live.prepareRecoveredDocuments(input, 1, diagnostic);
            assert(prepared
                && live.commitRecoveredDocuments(*prepared)
                    == Session::RecoveryCommitResult::Committed);
            input.clear();
            input.push_back(restored());
            auto abandoned = live.prepareRecoveredDocuments(input, 0, diagnostic);
            assert(abandoned);
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary prepared/committed/abandoned token destruction cycles: core live bytes/count delta=0");
}

void preparedAllocationFailure()
{
    Session live;
    assert(live.setActiveIndex(live.append(restored("old.arp"))));
    const auto id = live.activeId();
    const auto epoch = live.stateTicket();
    Documents input{restored()};
    input[0].text.assign(8192, 'x');
    core::pmr::String diagnostic;
    diagnostic.reserve(128);
    auto quota = core::makeShared<core::CpuMemoryQuota>(4096u);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!live.prepareRecoveredDocuments(input, 0, diagnostic)
            && diagnostic == "EDITOR-SCRIPT-RECOVERY-PREPARATION-FAILED");
    }
    assert(quota->peakBytes() > 0 && quota->liveBytes() == 0 && input.size() == 1
        && input[0].id == 0 && input[0].text.size() == 8192 && live.activeId() == id
        && epoch.isCurrent());
    auto valid = live.prepareRecoveredDocuments(input, 0, diagnostic);
    assert(valid
        && live.commitRecoveredDocuments(*valid) == Session::RecoveryCommitResult::Committed
        && live.activeId() == id + 1);
    std::puts(
        "PASS bounded allocation failure cleans partial snapshots and preserves source/live state, epoch and next identity");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        smoke();
        preparedSmoke();
    } else {
        replacement();
        rejectedInputs();
        exhaustionAndFiles();
        preparedReplacement();
        preparedLifetimeAndFailures();
        preparedAllocationFailure();
    }
}
