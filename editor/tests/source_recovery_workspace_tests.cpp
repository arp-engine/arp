#include "editor/tests/fixtures/source_recovery_capture_fixture.h"
#include "editor/workspace/editor_source_recovery_workspace.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace editor;
using namespace editor::testfixtures::source_recovery;
using Workspace = EditorSourceRecoveryWorkspace;

void checkAt(int line, bool value, std::string_view diagnostic = {})
{
    if (!value)
        std::fprintf(stderr,
            "Source recovery workspace check %d: %.*s\n",
            line,
            int(diagnostic.size()),
            diagnostic.data());
    assert(value);
}

#define check(...) checkAt(__LINE__, __VA_ARGS__)

constexpr ProjectDirectoryBudget listing{16, 4, 1024, 8192};

EditorRecoverySnapshot record(CaptureFixture& fixture)
{
    auto capture = fixture.capture(1);
    const char* issue = nullptr;
    auto result = *capture->snapshot(epoch, issue);
    return result;
}

void finish(CaptureFixture& fixture, Workspace& task)
{
    unified_io_fixture::wait(fixture.host.queue, [&] {
        task.advance(epoch);
        return task.state() != SourceRecoveryWorkspaceState::Pending
            && task.state() != SourceRecoveryWorkspaceState::Revalidating;
    });
}

void complete(CaptureFixture& fixture)
{
    auto submitted = record(fixture);
    const auto reads = fixture.host.files.telemetry().submitted;
    auto task = core::makeUnique<Workspace>(fixture.host.files,
        submitted,
        "workspace",
        ProjectSourceLocation{fixture.host.mount, "folder", epoch},
        limits,
        listing,
        fixture.budget());
    check(fixture.host.files.telemetry().submitted == reads);
    // All task inputs are owned; later caller edits cannot alter the packet.
    submitted.scene.yaml = "invalid replacement input";
    submitted.scripts.clear();
    finish(fixture, *task);
    const char* issue = nullptr;
    auto view = task->inspect(epoch, issue);
    check(bool(view), task->diagnostic());
    check(view->recovery.document->scene().name() == "Captured unsaved scene"
        && view->savedState.projectDraft()->name == "Captured project draft"
        && view->savedState.projectDirty() && view->savedState.scenePath() == "scene.yaml"
        && view->savedState.sceneTarget() == std::filesystem::path("folder/scene.yaml")
        && view->savedState.sceneSavedHash() == fixture.host.record.scene.savedContentHash
        && view->savedState.sceneAuthoringVersion() == EditorSceneAuthoringCodec::contractVersion
        && view->savedState.workspace() == "Script" && view->savedState.activeScriptIndex() == 0);
    check(view->browser.root() == std::filesystem::path("folder")
        && view->browser.entries().size() == 4 && view->files.size() == 2
        && view->browser.sourceSnapshot()->project() == view->recovery.sources.snapshot);
    check(view->scripts.size() == 1 && view->scripts.front().id == 0
        && view->scripts.front().text == "# unsaved\n" && view->scripts.front().undo.empty());
    check(!task->inspect(epoch + 1, issue));
    check(task->revalidate(epoch));
    check(!task->inspect(epoch, issue));
    finish(fixture, *task);
    auto revalidated = task->inspect(epoch, issue);
    check(bool(revalidated), task->diagnostic());
    ScriptDocumentSession live(100);
    core::pmr::String diagnostic;
    auto prepared = task->prepareForSession(live, epoch, diagnostic);
    check(prepared && live.size() == 0, diagnostic);
    auto sources = view->recovery.sources;
    task.reset();
    check(live.commitRecoveredSourceDocuments(*prepared, sources, diagnostic)
            == ScriptDocumentSession::RecoveryCommitResult::Committed,
        diagnostic);
    check(live.activeId() == 100 && live.active()->text == "# unsaved\n");
}

void changedFile(CaptureFixture& fixture)
{
    auto submitted = record(fixture);
    submitted.scripts.front().savedText = submitted.scripts.front().savedFileBytes = "# history\n";
    submitted.scripts.front().newline = "\n";
    Workspace task(fixture.host.files,
        submitted,
        "workspace",
        {fixture.host.mount, "folder", epoch},
        limits,
        listing,
        fixture.budget());
    finish(fixture, task);
    const char* issue = nullptr;
    auto view = task.inspect(epoch, issue);
    check(
        view && view->files[1].conflict == RecoverySourceFileConflict::Changed, task.diagnostic());
    fixture.replaceFile("rules.arp", "# another external change\n");
    check(task.revalidate(epoch));
    finish(fixture, task);
    check(task.state() == SourceRecoveryWorkspaceState::Failed
        && task.diagnostic() == "EDITOR-RECOVERY-SOURCE-WORKSPACE-FILE-CHANGED");
    check(!task.inspect(epoch, issue));
}

void changedDirectory(CaptureFixture& fixture)
{
    Workspace task(fixture.host.files,
        record(fixture),
        "workspace",
        {fixture.host.mount, "folder", epoch},
        limits,
        listing,
        fixture.budget());
    finish(fixture, task);
    check(task.state() == SourceRecoveryWorkspaceState::Ready, task.diagnostic());
    fixture.replaceFile("new.arp", "# new directory member\n");
    check(task.revalidate(epoch));
    finish(fixture, task);
    check(task.state() == SourceRecoveryWorkspaceState::Failed
        && task.diagnostic() == "EDITOR-RECOVERY-SOURCE-WORKSPACE-DIRECTORY-CHANGED");
}

void missingFile(CaptureFixture& fixture)
{
    auto submitted = record(fixture);
    submitted.scripts[0].path = "missing.arp";
    Workspace task(fixture.host.files,
        submitted,
        "workspace",
        {fixture.host.mount, "folder", epoch},
        limits,
        listing,
        fixture.budget());
    finish(fixture, task);
    const char* issue = nullptr;
    auto view = task.inspect(epoch, issue);
    check(view && view->files[1].conflict == RecoverySourceFileConflict::Missing
            && view->scripts[0].missingSource && !view->scripts[0].sourceSnapshot,
        task.diagnostic());
    check(task.revalidate(epoch));
    finish(fixture, task);
    check(task.state() == SourceRecoveryWorkspaceState::Ready, task.diagnostic());
    fixture.replaceFile("missing.arp", "# recreated outside editor\n");
    check(task.revalidate(epoch));
    finish(fixture, task);
    check(task.state() == SourceRecoveryWorkspaceState::Failed
        && task.diagnostic() == "EDITOR-RECOVERY-SOURCE-WORKSPACE-FILE-CHANGED");
}

void changedDependency(CaptureFixture& fixture)
{
    Workspace task(fixture.host.files,
        record(fixture),
        "workspace",
        {fixture.host.mount, "folder", epoch},
        limits,
        listing,
        fixture.budget());
    finish(fixture, task);
    check(task.state() == SourceRecoveryWorkspaceState::Ready, task.diagnostic());
    fixture.replaceFile(
        "project.arp.yaml", fixture.host.record.source->manifestFileBytes + "# change\n");
    check(task.revalidate(epoch));
    finish(fixture, task);
    check(task.state() == SourceRecoveryWorkspaceState::Failed);
}

void changedCandidate(CaptureFixture& fixture)
{
    Workspace task(fixture.host.files,
        record(fixture),
        "workspace",
        {fixture.host.mount, "folder", epoch},
        limits,
        listing,
        fixture.budget());
    finish(fixture, task);
    const char* issue = nullptr;
    auto view = task.inspect(epoch, issue);
    check(bool(view), task.diagnostic());
    view->recovery.document->scene().setName("Mutation through retained document alias");
    const auto reads = fixture.host.files.telemetry().submitted;
    check(!task.inspect(epoch, issue));
    check(!task.revalidate(epoch) && task.state() == SourceRecoveryWorkspaceState::Failed
        && fixture.host.files.telemetry().submitted == reads);
}

void failures(CaptureFixture& fixture)
{
    const auto submitted = record(fixture);
    Workspace wrong(fixture.host.files,
        submitted,
        "other",
        {fixture.host.mount, "folder", epoch},
        limits,
        listing,
        fixture.budget());
    finish(fixture, wrong);
    check(wrong.state() == SourceRecoveryWorkspaceState::Failed);
    Workspace capacity(fixture.host.files,
        submitted,
        "workspace",
        {fixture.host.mount, "folder", epoch},
        limits,
        {1, 1, 1024, 8192},
        fixture.budget());
    finish(fixture, capacity);
    check(capacity.state() == SourceRecoveryWorkspaceState::Failed);
    Workspace stale(fixture.host.files,
        submitted,
        "workspace",
        {fixture.host.mount, "folder", epoch},
        limits,
        listing,
        fixture.budget());
    stale.advance(epoch + 1);
    check(stale.state() == SourceRecoveryWorkspaceState::Failed);
    {
        Workspace retired(fixture.host.files,
            submitted,
            "workspace",
            {fixture.host.mount, "folder", epoch},
            limits,
            listing,
            fixture.budget());
        retired.advance(epoch);
        retired.cancel();
        check(retired.state() == SourceRecoveryWorkspaceState::Cancelled);
    }
    unified_io_fixture::wait(fixture.host.queue, [&] {
        return fixture.host.queue.reserved() == 0 && fixture.host.mounts.telemetry().inFlight == 0;
    });
    Workspace allocation(fixture.host.files,
        submitted,
        "workspace",
        {fixture.host.mount, "folder", epoch},
        limits,
        listing,
        fixture.budget());
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope denied(quota);
        allocation.advance(epoch);
    }
    check(allocation.state() == SourceRecoveryWorkspaceState::Failed && quota->liveBytes() == 0);
    Workspace ready(fixture.host.files,
        submitted,
        "workspace",
        {fixture.host.mount, "folder", epoch},
        limits,
        listing,
        fixture.budget());
    finish(fixture, ready);
    check(ready.state() == SourceRecoveryWorkspaceState::Ready, ready.diagnostic());
    check(fixture.host.mounts.unmount(fixture.host.mount));
    const char* issue = nullptr;
    check(!ready.inspect(epoch, issue) && !ready.revalidate(epoch));
}

void consumed(CaptureFixture& fixture)
{
    EditorSourceRecoveryPreparation task(fixture.host.files,
        record(fixture),
        "workspace",
        {fixture.host.mount, "folder", epoch},
        limits,
        fixture.budget());
    unified_io_fixture::wait(fixture.host.queue, [&] {
        task.advance(epoch);
        return task.state() != SourceRecoveryState::Pending;
    });
    const char* issue = nullptr;
    check(!task.takeCandidate(epoch + 1, issue) && task.state() == SourceRecoveryState::Ready);
    auto candidate = task.takeCandidate(epoch, issue);
    check(candidate && task.state() == SourceRecoveryState::Consumed
        && !task.candidate(epoch, issue) && !task.takeCandidate(epoch, issue));
}
} // namespace

int main(int argc, char** argv)
{
    bool native = false, smoke = false;
    for (int i = 1; i < argc; ++i) {
        native |= std::string_view(argv[i]) == "--native";
        smoke |= std::string_view(argv[i]) == "--smoke";
    }
    const auto run = [&] {
        {
            CaptureFixture fixture(native);
            complete(fixture);
        }
        {
            CaptureFixture fixture(native);
            changedFile(fixture);
        }
        {
            CaptureFixture fixture(native);
            changedDirectory(fixture);
        }
        {
            CaptureFixture fixture(native);
            missingFile(fixture);
        }
        {
            CaptureFixture fixture(native);
            changedDependency(fixture);
        }
        {
            CaptureFixture fixture(native);
            changedCandidate(fixture);
        }
        {
            CaptureFixture fixture(native);
            failures(fixture);
        }
        {
            CaptureFixture fixture(native);
            consumed(fixture);
        }
    };
    run();
    const auto before = core::defaultCpuAllocator().stats();
    const unsigned loops = smoke ? 20 : 1;
    for (unsigned i = 0; i < loops; ++i)
        run();
    const auto after = core::defaultCpuAllocator().stats();
    check(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::printf("Source recovery workspace %u cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
        loops,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
}
