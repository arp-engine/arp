#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "editor/project/project_manifest.h"
#include "editor/tests/fixtures/source_recovery_file_fixture.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_source_recovery_files.h"
#include "editor/workspace/script_document_session.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace editor;
using namespace core::io;
using namespace arp::editor::testfixtures::source_recovery;

void checkAt(int line, bool value, const char* diagnostic = nullptr)
{
    if (!value)
        std::fprintf(
            stderr, "Source recovery file check %d: %s\n", line, diagnostic ? diagnostic : "");
    assert(value);
}

#define check(...) checkAt(__LINE__, __VA_ARGS__)

void observations(Host& host)
{
    const auto before = host.files.telemetry().submitted;
    EditorSourceRecoveryFiles task(host.context, host.record, "workspace", limits);
    check(host.files.telemetry().submitted == before);
    const char* issue = nullptr;
    check(!task.observations(epoch, issue) && issue);
    host.finish(task);
    check(task.state() == RecoverySourceFilesState::Ready, task.diagnostic());
    const auto* files = task.observations(epoch, issue);
    check(files && files->size() == 2 && !issue);
    for (const auto& file : *files) {
        check(file.conflict == RecoverySourceFileConflict::None);
        check(file.observation.status == FileIoStatus::Success);
        check(file.binding.pathScope == FilePathScope::MountedResource);
        check(file.binding.mount == host.mount && file.binding.maxBytes <= limits.maxFileBytes);
    }
    check(!task.observations(epoch + 1, issue) && issue);
    auto record = host.record;
    record.scripts.front().savedFileBytes = "# external baseline\n";
    record.scripts.front().savedText = record.scripts.front().savedFileBytes;
    record.scripts.front().newline = "\n";
    EditorSourceRecoveryFiles changed(host.context, record, "workspace", limits);
    host.finish(changed);
    check(changed.state() == RecoverySourceFilesState::Ready, changed.diagnostic());
    files = changed.observations(epoch, issue);
    check(files && (*files)[1].conflict == RecoverySourceFileConflict::Changed);
    check((*files)[1].savedFileBytes == "# external baseline\n"
        && (*files)[1].observation.bytes.size() == host.record.scripts[0].savedFileBytes.size());
    record.scripts.front().path = "missing.arp";
    EditorSourceRecoveryFiles missing(host.context, record, "workspace", limits);
    host.finish(missing);
    check(missing.state() == RecoverySourceFilesState::Ready, missing.diagnostic());
    files = missing.observations(epoch, issue);
    check(files && (*files)[1].conflict == RecoverySourceFileConflict::Missing
        && (*files)[1].observation.status == FileIoStatus::MissingFile
        && (*files)[1].observation.bytes.empty());
    task.cancel();
    check(task.state() == RecoverySourceFilesState::Ready);
}

void failures(Host& host)
{
    const auto before = host.files.telemetry().submitted;
    EditorSourceRecoveryFiles alias(host.context, host.record, "other", limits);
    check(alias.state() == RecoverySourceFilesState::Failed);
    EditorSourceRecoveryFiles small(host.context, host.record, "workspace", {1, 8, 8});
    check(small.state() == RecoverySourceFilesState::Failed);
    EditorSourceRecoveryFiles stale(host.context, host.record, "workspace", limits);
    stale.advance(epoch + 1);
    check(stale.state() == RecoverySourceFilesState::Failed);
    EditorSourceRecoveryFiles cancelled(host.context, host.record, "workspace", limits);
    cancelled.cancel();
    cancelled.advance(epoch);
    check(cancelled.state() == RecoverySourceFilesState::Cancelled);
    check(host.files.telemetry().submitted == before);
    auto conflict = host.record;
    conflict.scripts.front().path = conflict.scene.path;
    EditorSourceRecoveryFiles duplicate(host.context, conflict, "workspace", limits);
    check(duplicate.state() == RecoverySourceFilesState::Failed);
    const char* rejected = nullptr;
    check(!duplicate.observations(epoch, rejected) && rejected);
    // A budget can admit historical baselines but leave too little space for
    // actual IO. Such a read failure is not a missing-file recovery conflict.
    const auto historical = host.record.scene.savedFileBytes.capacity()
        + host.record.scripts[0].savedFileBytes.capacity();
    EditorSourceRecoveryFiles exhausted(host.context,
        host.record,
        "workspace",
        {limits.maxFiles, limits.maxFileBytes, historical + 1});
    host.finish(exhausted);
    check(exhausted.state() == RecoverySourceFilesState::Failed);
    check(!exhausted.observations(epoch, rejected));
    EditorSourceRecoveryFiles fault(host.context, host.record, "workspace", limits);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope denied(quota);
        fault.advance(epoch);
    }
    check(fault.state() == RecoverySourceFilesState::Failed && quota->liveBytes() == 0);
    {
        EditorSourceRecoveryFiles pending(host.context, host.record, "workspace", limits);
        pending.advance(epoch);
    }
    unified_io_fixture::wait(host.queue, [&] {
        return host.queue.reserved() == 0 && host.mounts.telemetry().inFlight == 0;
    });
    EditorSourceRecoveryFiles ready(host.context, host.record, "workspace", limits);
    host.finish(ready);
    check(ready.state() == RecoverySourceFilesState::Ready);
    check(host.mounts.unmount(host.mount));
    const char* issue = nullptr;
    check(!ready.observations(epoch, issue) && issue);
}
} // namespace

int main(int argc, char** argv)
{
    const bool native = argc > 1 && std::string_view(argv[1]) == "--native";
    const unsigned loops = argc > 1 && std::string_view(argv[1]) == "--smoke" ? 20 : 1;
    {
        Host warm(native);
        observations(warm);
        failures(warm);
    }
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < loops; ++i) {
        Host host(native);
        observations(host);
        failures(host);
    }
    const auto after = core::defaultCpuAllocator().stats();
    check(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::printf(
        "Recovery source file observations %u cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
        loops,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
}
