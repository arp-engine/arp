#include "editor/project/project_file_save_task.h"
#include "editor/tests/fixtures/source_recovery_file_fixture.h"
#include "editor/workspace/editor_project_io_task.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace editor;
using namespace editor::testfixtures::source_recovery;
using namespace core::io;
using Session = ScriptDocumentSession;

void checkAt(int line, bool value, const char* diagnostic = nullptr)
{
    if (!value)
        std::fprintf(
            stderr, "Source recovery document check %d: %s\n", line, diagnostic ? diagnostic : "");
    assert(value);
}

#define check(...) checkAt(__LINE__, __VA_ARGS__)

EditorRecoverySnapshot threeTabs(const Host& host)
{
    auto record = host.record;
    auto changed = record.scripts.front();
    changed.path = "changed.arp";
    changed.savedFileBytes = changed.savedText = "# previous baseline\n";
    changed.text = "# recovered changed draft\n";
    changed.newline = "\n";
    auto missing = record.scripts.front();
    missing.path = "gone.arp";
    missing.text = "# recovered missing draft\n";
    record.scripts.push_back(std::move(changed));
    record.scripts.push_back(std::move(missing));
    record.activeScript = 2;
    return record;
}

Session::Document retired(const Host& host)
{
    Session::Document document;
    document.path = host.workspace.root / "retired.arp";
    document.savedText = document.savedFileBytes = "# retired\n";
    document.text = "# unsaved retired\n";
    return document;
}

void waitTask(Host& host, EditorProjectIoTask& task)
{
    unified_io_fixture::wait(host.queue, [&] {
        task.advance();
        return task.ready() || task.status() != ProjectIoStatus::Pending;
    });
}

void restored(Host& host)
{
    Session live(100);
    check(live.setActiveIndex(live.append(retired(host))));
    const auto previousId = live.activeId();
    auto record = threeTabs(host);
    const auto submitted = host.files.telemetry().submitted;
    auto task =
        core::makeUnique<EditorSourceRecoveryDocuments>(host.context, record, "workspace", limits);
    check(host.files.telemetry().submitted == submitted);
    host.finish(*task);
    check(task->state() == RecoverySourceDocumentsState::Ready, task->diagnostic());
    const char* issue = nullptr;
    const auto* docs = task->documents(epoch, issue);
    check(docs && docs->size() == 3, issue);
    for (const auto& document : *docs)
        check(document.id == 0 && document.undo.empty() && document.redo.empty()
            && document.sourceBound() && document.validateSource(host.files, epoch, issue));
    check((*docs)[0].sourceSnapshot && !(*docs)[0].sourceConflict());
    check((*docs)[1].sourceSnapshot && (*docs)[1].sourceConflict());
    check(!(*docs)[2].sourceSnapshot && (*docs)[2].missingSource && (*docs)[2].sourceConflict());
    check((*docs)[2].text == record.scripts[2].text
        && (*docs)[2].savedFileBytes == record.scripts[2].savedFileBytes);
    core::pmr::String diagnostic;
    check(!live.prepareRecoveredDocuments(*docs, 0, diagnostic));
    auto prepared = task->prepareForSession(live, epoch, diagnostic);
    check(bool(prepared), diagnostic.c_str());
    check(live.commitRecoveredDocuments(*prepared)
            == Session::RecoveryCommitResult::SourceValidationRequired
        && live.activeId() == previousId);
    auto wrong = host.context;
    wrong.epoch += 1;
    check(live.commitRecoveredSourceDocuments(*prepared, wrong, diagnostic)
        == Session::RecoveryCommitResult::Stale);
    check(live.activeId() == previousId);
    task.reset();
    check(live.commitRecoveredSourceDocuments(*prepared, host.context, diagnostic)
            == Session::RecoveryCommitResult::Committed,
        diagnostic.c_str());
    check(live.size() == 3 && live.activeIndex() == 2 && live.activeId() == previousId + 3
        && live.documents()[0].id == previousId + 1 && !live.find(previousId));
    check(live.commitRecoveredSourceDocuments(*prepared, host.context, diagnostic)
        == Session::RecoveryCommitResult::Consumed);
    check(live.saveActiveFile(host.workspace.root).code == Session::FileResultCode::InvalidPath);
    check(live.reloadActiveFile(host.workspace.root, true).code
        == Session::FileResultCode::InvalidPath);
    check(!live.prepareSourceSave(live.activeId(), host.files, epoch, issue)
        && std::string_view(issue) == "EDITOR-SCRIPT-SAVE-SOURCE-MISSING");
    check(live.setActiveIndex(1));
    check(!live.prepareSourceSave(live.activeId(), host.files, epoch, issue)
        && std::string_view(issue) == "EDITOR-SCRIPT-SAVE-BASELINE-CHANGED");
    check(live.setActiveIndex(0));
    auto save = live.prepareSourceSave(live.activeId(), host.files, epoch, issue);
    check(bool(save), issue);
    ProjectFileSaveTask write(host.files, save->takeRequest());
    write.advance();
    unified_io_fixture::wait(host.queue, [&] {
        return write.terminal();
    });
    check(write.result().status == FileIoStatus::Success, write.result().diagnosticId);
    const auto committed = live.commitSourceSave(*save, host.files, epoch, write.result());
    check(committed, save->diagnostic());
    check(!live.active()->dirty() && !live.active()->sourceConflict());

    EditorProjectIoBudget budget;
    budget.inputs = limits;
    check(live.setActiveIndex(1));
    const auto changedId = live.activeId();
    auto reload = live.prepareReload(changedId, true, {}, issue);
    check(bool(reload), issue);
    EditorProjectIoTask changed(host.files,
        EditorProjectIoTask::ScriptInput{host.context.snapshot, "changed.arp", std::move(reload)},
        budget);
    waitTask(host, changed);
    check(changed.ready(), std::string(changed.diagnostic()).c_str());
    check(live.commitSourceReload(*changed.scriptReload(), changed.script()));
    changed.published();
    check(live.activeId() == changedId && live.active()->text == "# current external\n"
        && !live.active()->dirty() && !live.active()->sourceConflict());
    check(live.setActiveIndex(2));
    const auto missingId = live.activeId();
    const auto missingText = live.active()->text;
    reload = live.prepareReload(missingId, true, {}, issue);
    EditorProjectIoTask stillMissing(host.files,
        EditorProjectIoTask::ScriptInput{host.context.snapshot, "gone.arp", std::move(reload)},
        budget);
    waitTask(host, stillMissing);
    check(!stillMissing.ready() && stillMissing.status() == ProjectIoStatus::Failed);
    check(live.activeId() == missingId && live.active()->text == missingText
        && live.active()->missingSource);
    FileSaveRequest create;
    create.pathScope = FilePathScope::MountedResource;
    create.mount = host.mount;
    create.path = "folder/gone.arp";
    create.precondition = FileSavePrecondition::MustBeMissing;
    create.maxWriteBytes = 1024;
    const std::string_view recreated = "# recreated file\n";
    create.bytes.assign(recreated.begin(), recreated.end());
    ProjectFileSaveTask creation(host.files, std::move(create));
    creation.advance();
    unified_io_fixture::wait(host.queue, [&] {
        return creation.terminal();
    });
    check(creation.result().commitState == FileCommitState::Committed);
    reload = live.prepareReload(missingId, true, {}, issue);
    EditorProjectIoTask recreatedTask(host.files,
        EditorProjectIoTask::ScriptInput{host.context.snapshot, "gone.arp", std::move(reload)},
        budget);
    waitTask(host, recreatedTask);
    check(recreatedTask.ready(), std::string(recreatedTask.diagnostic()).c_str());
    const auto originalBinding = live.active()->missingSource;
    live.active()->missingSource = core::makeShared<Session::MissingSourceBinding>();
    check(!live.commitSourceReload(*recreatedTask.scriptReload(), recreatedTask.script()));
    live.active()->missingSource = originalBinding;
    check(live.commitSourceReload(*recreatedTask.scriptReload(), recreatedTask.script()));
    recreatedTask.published();
    check(live.activeId() == missingId && !live.active()->missingSource
        && live.active()->sourceSnapshot && live.active()->text == recreated
        && !live.active()->sourceConflict());
}

void failures(Host& host)
{
    EditorSourceRecoveryDocuments cancelled(host.context, host.record, "workspace", limits);
    cancelled.advance(epoch);
    cancelled.cancel();
    check(cancelled.state() == RecoverySourceDocumentsState::Cancelled);
    const char* issue = nullptr;
    check(!cancelled.documents(epoch, issue));
    unified_io_fixture::wait(host.queue, [&] {
        return host.mounts.telemetry().inFlight == 0;
    });
    Session live;
    check(live.setActiveIndex(live.append(retired(host))));
    const auto oldId = live.activeId();
    EditorSourceRecoveryDocuments task(host.context, threeTabs(host), "workspace", limits);
    host.finish(task);
    check(task.state() == RecoverySourceDocumentsState::Ready, task.diagnostic());
    const auto* docs = task.documents(epoch, issue);
    check(docs);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    core::pmr::String diagnostic;
    diagnostic.reserve(256);
    {
        core::CpuMemoryQuotaScope denied(quota);
        check(!live.prepareRecoveredSourceDocuments(*docs, 0, host.context, diagnostic));
    }
    check(quota->liveBytes() == 0 && live.activeId() == oldId && live.size() == 1);
    const auto next = live.append(retired(host));
    check(live.documents()[next].id == oldId + 1);
    check(live.erase(next));
    auto stale = task.prepareForSession(live, epoch, diagnostic);
    check(bool(stale), diagnostic.c_str());
    check(live.replaceActiveRange(0, 0, "# edit\n", Session::maxScriptBytes)
        == Session::EditResult::Applied);
    check(live.commitRecoveredSourceDocuments(*stale, host.context, diagnostic)
        == Session::RecoveryCommitResult::Stale);
    auto fresh = task.prepareForSession(live, epoch, diagnostic);
    check(bool(fresh));
    auto invalid = *docs;
    invalid[2].sourceSnapshot = invalid[0].sourceSnapshot;
    check(!live.prepareRecoveredSourceDocuments(invalid, 0, host.context, diagnostic));
    check(live.activeId() == oldId);
    invalid = *docs;
    invalid[0].path = "folder/foreign.arp";
    check(!live.prepareRecoveredSourceDocuments(invalid, 0, host.context, diagnostic));
    invalid = *docs;
    invalid[0].newline = "\n";
    check(!live.prepareRecoveredSourceDocuments(invalid, 0, host.context, diagnostic));
    check(host.mounts.unmount(host.mount));
    check(live.commitRecoveredSourceDocuments(*fresh, host.context, diagnostic)
        == Session::RecoveryCommitResult::Stale);
    check(live.activeId() == oldId && !task.documents(epoch, issue));
}
} // namespace

int main(int argc, char** argv)
{
    const bool native = argc > 1 && std::string_view(argv[1]) == "--native";
    const unsigned loops = argc > 1 && std::string_view(argv[1]) == "--smoke" ? 20 : 1;
    const auto run = [&] {
        {
            Host host(native, true);
            restored(host);
        }
        {
            Host host(native, true);
            failures(host);
        }
    };
    run();
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < loops; ++i)
        run();
    const auto after = core::defaultCpuAllocator().stats();
    check(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::printf(
        "Recovered source documents %u cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
        loops,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
}
