#include "core/io/file_io.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "editor/project/project_directory_session.h"
#include "editor/project/project_file_save_task.h"
#include "editor/project/project_source_session.h"
#include "editor/project/project_source_verification.h"
#include "editor/workspace/script_document_session.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstring>

namespace {
using namespace arp;
using namespace core;
using namespace core::io;
using namespace editor;
using namespace unified_io_fixture;

pmr::Vector<std::uint8_t> bytes(std::string_view source)
{
    return {source.begin(), source.end()};
}

void scopeReplacementCycle()
{
    // The manifest is deliberately not the first acquired file. Its neighbours
    // exercise preserved byte/version identities, including an empty payload.
    auto memory = makeShared<MemoryFileBackend>(MemoryFileBackendDesc{4, 4096, 2, 4096, 128, true});
    assert(memory->registerDirectory("project") == FileIoStatus::Success);
    const auto script = bytes("script");
    const auto manifest = bytes("original");
    assert(memory->registerFile("project/script.arp", script.data(), script.size())
        == FileIoStatus::Success);
    assert(memory->registerFile("project/project.arp.yaml", manifest.data(), manifest.size())
        == FileIoStatus::Success);
    assert(memory->registerFile("project/empty.bin", nullptr, 0) == FileIoStatus::Success);
    MountedFileBackend mounts({1, 2, 8192, 32, 128});
    const auto mount = mounts.mount("scope", memory);
    runtime::CompletionQueue queue({8});
    FileService files(queue, mounts);
    constexpr std::uint64_t epoch = 3;
    ProjectSourceSession acquire(files, {mount, "project", epoch}, {3, 64, 32});
    for (const auto* name : {"script.arp", "project.arp.yaml", "empty.bin"}) {
        assert(acquire.read(name, 16) == FileIoStatus::Accepted);
        queue.dispatch(8);
    }
    auto before = acquire.finish(epoch);
    assert(before && before->size() == 3 && before->totalBytes() == 14);
    ProjectDirectorySession listing(files, before, "", {8, 8, 1024, 2048});
    assert(listing.next(epoch) == FileIoStatus::Accepted);
    queue.dispatch(8);
    auto directory = listing.finish(epoch);
    assert(directory && directory->entries().size() == 3);
    const char* issue = nullptr;
    auto prepared = before->prepareScopeFileSave(
        files, epoch, "project.arp.yaml", bytes("new settings"), 64, issue);
    assert(prepared && !issue && queue.pending() == 0 && memory->telemetry().inFlight == 0);
    auto directoryRebind = directory->prepareProjectRebind(*prepared, issue);
    assert(directoryRebind && !issue && prepared->preservesScopeOf(*before));
    assert(!directoryRebind->finish(*prepared, before));
    assert(before->find("project.arp.yaml")->receipt.bytes == manifest);
    ProjectFileSaveTask operation(files, prepared->takeRequest());
    assert(!directory->prepareProjectRebind(*prepared, issue));
    assert(std::string_view(issue) == "PROJECT-DIRECTORY-REBIND-BASELINE");
    assert(!operation.submitted());
    operation.advance();
    queue.dispatch(8);
    assert(operation.terminal() && operation.result().commitState == FileCommitState::Committed);
    SharedPtr<const ProjectSourceSnapshot> after;
    SharedPtr<const ProjectDirectorySnapshot> rebound;
    const auto quota = makeShared<CpuMemoryQuota>(1);
    {
        CpuMemoryQuotaScope denyAllocations(quota);
        after = prepared->finish(files, epoch, operation.result());
        rebound = directoryRebind->finish(*prepared, after);
    }
    assert(after && !prepared->diagnostic() && quota->liveBytes() == 0);
    assert(rebound && rebound->project() == after && rebound->validate(files, epoch, issue));
    assert(directory->project() == before && rebound->directory() == directory->directory());
    assert(rebound->version() == directory->version());
    assert(rebound->entries().size() == directory->entries().size());
    for (std::size_t i = 0; i < directory->entries().size(); ++i) {
        const auto& oldEntry = directory->entries()[i];
        const auto& newEntry = rebound->entries()[i];
        assert(newEntry.name == oldEntry.name && newEntry.metadata.kind == oldEntry.metadata.kind);
        assert(newEntry.metadata.version == oldEntry.metadata.version);
        assert(newEntry.metadata.sizeBytes == oldEntry.metadata.sizeBytes);
    }
    assert(!directoryRebind->finish(*prepared, after));
    assert(after->size() == 3 && after->totalBytes() == 18 && after->identities().size() == 3);
    for (std::size_t i = 0; i < before->size(); ++i) {
        const auto& oldEntry = *before->entryAt(i);
        const auto& newEntry = *after->entryAt(i);
        assert(oldEntry.relative == newEntry.relative);
        assert(oldEntry.binding.path == newEntry.binding.path);
        assert(oldEntry.binding.mount == newEntry.binding.mount);
        assert(before->identities()[i].path == after->identities()[i].path);
        if (i == 1) {
            assert(newEntry.binding.maxBytes == 64);
            assert(newEntry.receipt.bytes == bytes("new settings"));
            assert(newEntry.receipt.version == operation.result().committedVersion);
            assert(newEntry.binding.expectedVersion == operation.result().committedVersion);
            assert(newEntry.binding.maxVersionBytes == 12);
            assert(before->identities()[i].fingerprint != after->identities()[i].fingerprint);
        } else {
            assert(oldEntry.binding.maxBytes == newEntry.binding.maxBytes);
            assert(oldEntry.receipt.bytes == newEntry.receipt.bytes);
            assert(oldEntry.receipt.version == newEntry.receipt.version);
            assert(oldEntry.binding.expectedVersion == newEntry.binding.expectedVersion);
            assert(oldEntry.binding.maxVersionBytes == newEntry.binding.maxVersionBytes);
            assert(before->identities()[i].fingerprint == after->identities()[i].fingerprint);
        }
    }
    assert(!prepared->finish(files, epoch, operation.result()));
    assert(before->find("project.arp.yaml")->receipt.bytes == manifest);
    ProjectSourceVerification verify({after, &files, epoch}, {3, 64, 32});
    while (verify.state() == ProjectSourceVerificationState::Pending) {
        verify.advance();
        queue.dispatch(8);
    }
    assert(verify.state() == ProjectSourceVerificationState::Verified);

    // Existing script save semantics still intentionally produce one-file scopes.
    auto single = after->prepareFileSave(files, epoch, "script.arp", bytes("script"), 64, issue);
    assert(single);
    assert(!rebound->prepareProjectRebind(*single, issue));
    ProjectFileSaveTask singleOperation(files, single->takeRequest());
    singleOperation.advance();
    queue.dispatch(8);
    auto one = single->finish(files, epoch, singleOperation.result());
    assert(one && one->size() == 1 && !one->find("project.arp.yaml"));

    // Full-scope budgets are the original acquisition budgets, not the
    // looser global defaults and not merely the replacement's request budget.
    assert(!after->prepareScopeFileSave(
        files, epoch, "project.arp.yaml", bytes("123456789012345678901234567"), 64, issue));
    assert(std::string_view(issue) == "PROJECT-SOURCE-SAVE-BUDGET");
    assert(
        !after->prepareScopeFileSave(files, epoch, "project.arp.yaml", bytes("small"), 65, issue));
    assert(std::string_view(issue) == "PROJECT-SOURCE-SAVE-BUDGET");
    assert(!after->prepareScopeFileSave(files, epoch, "absent", bytes("small"), 64, issue));
    assert(std::string_view(issue) == "PROJECT-SOURCE-SAVE-NO-BASELINE");
    auto payload = bytes("candidate");
    {
        CpuMemoryQuotaScope denyAllocations(quota);
        assert(!after->prepareScopeFileSave(
            files, epoch, "project.arp.yaml", std::move(payload), 64, issue));
    }
    assert(issue && quota->liveBytes() == 0 && queue.pending() == 0);
    auto pending = after->prepareScopeFileSave(
        files, epoch, "project.arp.yaml", bytes("new settings"), 64, issue);
    assert(pending);
    assert(!directory->prepareProjectRebind(*pending, issue));
    {
        CpuMemoryQuotaScope denyAllocations(quota);
        assert(!rebound->prepareProjectRebind(*pending, issue));
    }
    assert(std::string_view(issue) == "PROJECT-DIRECTORY-REBIND-ALLOCATION");
    auto exactRecipient = rebound->prepareProjectRebind(*pending, issue);
    auto sibling = after->prepareScopeFileSave(
        files, epoch, "project.arp.yaml", bytes("new settings"), 64, issue);
    assert(exactRecipient && sibling);
    ProjectFileSaveTask siblingWrite(files, sibling->takeRequest());
    siblingWrite.advance();
    queue.dispatch(8);
    auto siblingScope = sibling->finish(files, epoch, siblingWrite.result());
    assert(siblingScope && sibling->preservesScopeOf(*after));
    // Even identical baseline/payload bytes cannot substitute another save's
    // sealed candidate for this recipient's exact prepared transaction.
    assert(!exactRecipient->finish(*sibling, siblingScope));
    ProjectFileSaveTask exactWrite(files, pending->takeRequest());
    exactWrite.advance();
    queue.dispatch(8);
    auto exactScope = pending->finish(files, epoch, exactWrite.result());
    assert(exactScope && exactRecipient->finish(*pending, exactScope));

    // Real same-byte commits supply receipts; only their domain-facing copies
    // are mutated here to test rejection. This is not an Unknown backend claim.
    for (int invalid = 0; invalid != 4; ++invalid) {
        auto rejected = after->prepareScopeFileSave(
            files, epoch, "project.arp.yaml", bytes("new settings"), 64, issue);
        assert(rejected);
        ProjectFileSaveTask write(files, rejected->takeRequest());
        write.advance();
        queue.dispatch(8);
        auto receipt = write.result();
        assert(receipt.commitState == FileCommitState::Committed);
        if (invalid == 0)
            receipt.commitState = FileCommitState::OutcomeUnknown;
        if (invalid == 1)
            receipt.committedVersion.digest[0] ^= 1;
        if (invalid == 2)
            receipt.deliveryStatus = FileIoStatus::Cancelled;
        assert(!rejected->finish(files, invalid == 3 ? epoch + 1 : epoch, receipt));
        assert(rejected->diagnostic());
        assert(after->find("project.arp.yaml")->receipt.bytes == bytes("new settings"));
    }

    // A second writer changes the actual manifest, never this immutable scope.
    auto stale = after->prepareScopeFileSave(
        files, epoch, "project.arp.yaml", bytes("stale save"), 64, issue);
    auto external =
        after->prepareScopeFileSave(files, epoch, "project.arp.yaml", bytes("external"), 64, issue);
    assert(stale && external);
    ProjectFileSaveTask other(files, external->takeRequest());
    other.advance();
    queue.dispatch(8);
    assert(other.result().commitState == FileCommitState::Committed);
    ProjectFileSaveTask conflict(files, stale->takeRequest());
    conflict.advance();
    queue.dispatch(8);
    assert(conflict.result().status == FileIoStatus::VersionMismatch);
    assert(!stale->finish(files, epoch, conflict.result()));
    auto committed = external->finish(files, epoch, other.result());
    assert(committed && committed->find("project.arp.yaml")->receipt.bytes == bytes("external"));
    auto cancelledDependency = committed->prepareScopeFileSave(
        files, epoch, "project.arp.yaml", bytes("external"), 64, issue);
    auto staleMount = committed->prepareScopeFileSave(
        files, epoch, "project.arp.yaml", bytes("external"), 64, issue);
    auto foreignService = committed->prepareScopeFileSave(
        files, epoch, "project.arp.yaml", bytes("external"), 64, issue);
    assert(cancelledDependency && staleMount && foreignService);
    ProjectFileSaveTask write(files, cancelledDependency->takeRequest());
    write.advance();
    queue.dispatch(8);
    assert(write.result().commitState == FileCommitState::Committed);
    ProjectFileSaveTask foreignWrite(files, foreignService->takeRequest());
    foreignWrite.advance();
    queue.dispatch(8);
    FileService anotherService(queue, mounts);
    assert(!foreignService->finish(anotherService, epoch, foreignWrite.result()));
    assert(std::string_view(foreignService->diagnostic()) == "PROJECT-SOURCE-SAVE-STALE-OWNER");
    ProjectFileSaveTask finalWrite(files, staleMount->takeRequest());
    finalWrite.advance();
    queue.dispatch(8);
    assert(finalWrite.result().commitState == FileCommitState::Committed);
    // The replaced file has a fresh token; retained dependencies still belong
    // to their original acquisition and must not survive its explicit cancel.
    acquire.cancel();
    assert(!cancelledDependency->finish(files, epoch, write.result()));
    assert(std::string_view(cancelledDependency->diagnostic()) == "PROJECT-SOURCE-CANCELLED");
    assert(queue.pending() == 0 && queue.reserved() == 0);
    mounts.shutdown();
    assert(!staleMount->finish(files, epoch, finalWrite.result()));
    assert(std::string_view(staleMount->diagnostic()) == "PROJECT-SOURCE-SAVE-STALE-OWNER");
    assert(memory->telemetry().inFlight == 0 && memory->telemetry().queuedBytes == 0);
}

void readonlySourceContract()
{
    auto memory = makeShared<MemoryFileBackend>(MemoryFileBackendDesc{1, 1024, 1, 1024, 128});
    const auto source = bytes("initial\n");
    assert(
        memory->registerFile("script.arp", source.data(), source.size()) == FileIoStatus::Success);
    MountedFileBackend mounts({1, 1, 4096, 32, 128});
    const auto mount = mounts.mount("readonly", memory);
    runtime::CompletionQueue queue({4});
    FileService files(queue, mounts);
    ProjectSourceSession acquire(files, {mount, "", 1}, {1, 1024, 1024});
    assert(acquire.read("script.arp", 1024) == FileIoStatus::Accepted);
    queue.dispatch(4);
    auto snapshot = acquire.finish(1);
    assert(snapshot);
    const char* diagnostic = nullptr;
    assert(
        !snapshot->prepareFileSave(files, 1, "script.arp", bytes("changed\n"), 1024, diagnostic));
    assert(std::string_view(diagnostic) == "PROJECT-SOURCE-SAVE-READ-ONLY");
    assert(!snapshot->prepareScopeFileSave(
        files, 1, "script.arp", bytes("changed\n"), 1024, diagnostic));
    assert(std::string_view(diagnostic) == "PROJECT-SOURCE-SAVE-READ-ONLY");
    ScriptDocumentSession session;
    UniquePtr<ScriptDocumentSession::Document> document;
    assert(ScriptDocumentSession::prepareSourceFile(
        snapshot, "script.arp", files, 1, document, diagnostic));
    const auto index = session.append(std::move(*document));
    assert(session.setActiveIndex(index));
    assert(session.replaceActiveRange(0, session.active()->text.size(), "changed\n", 1024)
        == ScriptDocumentSession::EditResult::Applied);
    assert(!session.prepareSourceSave(session.activeId(), files, 1, diagnostic));
    assert(std::string_view(diagnostic) == "PROJECT-SOURCE-SAVE-READ-ONLY");
    assert(session.active()->savedText == "initial\n" && session.active()->dirty());
    assert(snapshot->find("script.arp")->receipt.bytes == source);
    assert(queue.pending() == 0 && queue.reserved() == 0);
    mounts.shutdown();
}

#if defined(_WIN32)
void scriptCycle(Workspace& workspace)
{
    constexpr std::uint64_t epoch = 9;
    const std::string_view original = "\xef\xbb\xbf"
                                      "hello\r\n";
    workspace.write("nested/script.arp", original);
    auto desc = workspace.descriptor();
    // Two admitted script payload+baseline reservations with native scratch.
    desc.maxBytesInFlight = 4 * ScriptDocumentSession::maxScriptBytes;
    desc.allowObservedBaselineReplace = true;
    auto native = makeShared<NativeDirectoryFileBackend>(desc);
    MountedFileBackend mounts({1, 4, 2 * desc.maxBytesInFlight, 32, desc.maxPathBytes});
    const auto mount = mounts.mount("workspace", native);
    runtime::CompletionQueue queue({8});
    FileService files(queue, mounts);
    ProjectSourceSession acquire(files, {mount, "nested", epoch}, {1, 1024, 1024});
    assert(acquire.read("script.arp", 1024) == FileIoStatus::Accepted);
    wait(queue, [&] {
        return acquire.state() != ProjectSourceState::Pending;
    });
    auto snapshot = acquire.finish(epoch);
    assert(snapshot);
    ScriptDocumentSession session;
    UniquePtr<ScriptDocumentSession::Document> document;
    const char* diagnostic = nullptr;
    assert(ScriptDocumentSession::prepareSourceFile(
        snapshot, "script.arp", files, epoch, document, diagnostic));
    assert(document->text == "hello\n" && document->newline == "\r\n");
    const auto index = session.append(std::move(*document));
    assert(session.setActiveIndex(index));
    const auto id = session.activeId();
    assert(session.replaceActiveRange(0, session.active()->text.size(), "first\n", 1024)
        == ScriptDocumentSession::EditResult::Applied);
    auto save = session.prepareSourceSave(id, files, epoch, diagnostic);
    assert(save && !diagnostic);
    Probe<FileSaveResult> probe;
    auto boundedSave = save->takeRequest();
    assert(boundedSave.maxWriteBytes == 1024 && boundedSave.maxBaselineBytes == 1024);
    assert(files.save(std::move(boundedSave), {Probe<FileSaveResult>::complete, &probe})
        == FileIoStatus::Accepted);
    assert(session.replaceActiveRange(0, session.active()->text.size(), "second\n", 1024)
        == ScriptDocumentSession::EditResult::Applied);
    wait(queue, [&] {
        return probe.calls == 1;
    });
    auto quota = makeShared<CpuMemoryQuota>(1);
    {
        CpuMemoryQuotaScope failure(quota);
        assert(session.commitSourceSave(*save, files, epoch, probe.result));
    }
    assert(session.active()->text == "second\n" && session.active()->savedText == "first\n");
    assert(session.active()->dirty() && session.active()->undo.size() == 2);
    assert(session.active()->savedFileBytes
        == "\xef\xbb\xbf"
           "first\r\n");
    assert(!session.commitSourceSave(*save, files, epoch, probe.result));
    auto disk = readBinaryFile(workspace.root / "nested/script.arp", 1024);
    assert(disk.success
        && disk.bytes
            == bytes("\xef\xbb\xbf"
                     "first\r\n"));
    save = session.prepareSourceSave(id, files, epoch, diagnostic);
    assert(save);
    assert(files.save(save->takeRequest(), {Probe<FileSaveResult>::complete, &probe})
        == FileIoStatus::Accepted);
    wait(queue, [&] {
        return probe.calls == 2;
    });
    assert(session.commitSourceSave(*save, files, epoch, probe.result));
    assert(!session.active()->dirty() && session.active()->undo.size() == 2);
    assert(session.active()->sourceSnapshot->find("script.arp")->receipt.bytes
        == bytes("\xef\xbb\xbf"
                 "second\r\n"));

    // Identical per-session document IDs and source pointers do not authorize
    // a different session to consume another owner's save publication token.
    ScriptDocumentSession foreign;
    const auto foreignIndex = foreign.append(*session.active());
    assert(foreign.setActiveIndex(foreignIndex));
    assert(foreign.activeId() == id);
    auto foreignToken = session.prepareSourceSave(id, files, epoch, diagnostic);
    assert(foreignToken);
    auto foreignRequest = foreignToken->takeRequest();
    Probe<FileSaveResult> foreignProbe;
    assert(files.save(std::move(foreignRequest), {Probe<FileSaveResult>::complete, &foreignProbe})
        == FileIoStatus::Accepted);
    wait(queue, [&] {
        return foreignProbe.calls == 1;
    });
    assert(foreignProbe.result.commitState == FileCommitState::Committed);
    assert(!foreign.commitSourceSave(*foreignToken, files, epoch, foreignProbe.result));
    assert(foreign.active()->savedText == "second\n");
    assert(session.commitSourceSave(*foreignToken, files, epoch, foreignProbe.result));
    const auto savedSource = session.active()->sourceSnapshot;
    assert(session.replaceActiveRange(0, session.active()->text.size(), "third\n", 1024)
        == ScriptDocumentSession::EditResult::Applied);
    save = session.prepareSourceSave(id, files, epoch, diagnostic);
    assert(save);
    workspace.write("nested/script.arp", "external\r\n");
    assert(files.save(save->takeRequest(), {Probe<FileSaveResult>::complete, &probe})
        == FileIoStatus::Accepted);
    wait(queue, [&] {
        return probe.calls == 3;
    });
    assert(probe.result.status == FileIoStatus::VersionMismatch);
    assert(!session.commitSourceSave(*save, files, epoch, probe.result));
    assert(session.active()->sourceSnapshot == savedSource);
    assert(session.active()->dirty() && session.active()->savedText == "second\n");
    disk = readBinaryFile(workspace.root / "nested/script.arp", 1024);
    assert(disk.success && disk.bytes == bytes("external\r\n"));
    workspace.write("nested/script.arp",
        "\xef\xbb\xbf"
        "second\r\n");
    {
        CpuMemoryQuotaScope failure(quota);
        assert(!session.prepareSourceSave(id, files, epoch, diagnostic));
    }
    assert(session.active()->sourceSnapshot == savedSource && session.active()->dirty());
    assert(quota->liveBytes() == 0);

    save = session.prepareSourceSave(id, files, epoch, diagnostic);
    assert(save);
    assert(files.save(save->takeRequest(), {Probe<FileSaveResult>::complete, &probe})
        == FileIoStatus::Accepted);
    wait(queue, [&] {
        return probe.calls == 4;
    });
    assert(probe.result.commitState == FileCommitState::Committed);
    assert(session.erase(index));
    // Reopening the same logical path creates a new document identity. The
    // former save token cannot update its baseline, even after a real commit.
    assert(ScriptDocumentSession::prepareSourceFile(
        snapshot, "script.arp", files, epoch, document, diagnostic));
    const auto replacementIndex = session.append(std::move(*document));
    assert(session.setActiveIndex(replacementIndex) && session.activeId() != id);
    assert(!session.commitSourceSave(*save, files, epoch, probe.result));
    assert(session.active()->savedFileBytes == original);
    disk = readBinaryFile(workspace.root / "nested/script.arp", 1024);
    assert(disk.success
        && disk.bytes
            == bytes("\xef\xbb\xbf"
                     "third\r\n"));
    mounts.shutdown();
    queue.dispatch(8);
    assert(native->telemetry().inFlight == 0 && native->telemetry().reservedBytes == 0);
    assert(native->telemetry().openNativeObjects == 0);
}

void cycle(Workspace& workspace)
{
    constexpr std::uint64_t epoch = 7;
    constexpr std::size_t maxBytes = 1024;
    const std::string_view original = "\xef\xbb\xbf"
                                      "hello\r\n";
    const std::string_view replacement = "\xef\xbb\xbf"
                                         "changed\r\n";
    workspace.write("nested/source.arp", original);
    workspace.write("nested/dependency.bin", "unchanged dependency");
    auto desc = workspace.descriptor();
    desc.allowObservedBaselineReplace = true;
    auto native = makeShared<NativeDirectoryFileBackend>(desc);
    MountedFileBackend mounts({1, 4, desc.maxBytesInFlight, 32, desc.maxPathBytes});
    const auto mount = mounts.mount("workspace", native);
    runtime::CompletionQueue queue({8});
    FileService files(queue, mounts);
    ProjectSourceLocation location{mount, "nested", epoch};
    ProjectInputBudget budget{2, maxBytes, 2 * maxBytes};
    ProjectSourceSession acquire(files, location, budget);
    assert(acquire.read("dependency.bin", maxBytes) == FileIoStatus::Accepted);
    wait(queue, [&] {
        return acquire.state() != ProjectSourceState::Pending;
    });
    assert(acquire.read("source.arp", maxBytes) == FileIoStatus::Accepted);
    wait(queue, [&] {
        return acquire.state() != ProjectSourceState::Pending;
    });
    auto before = acquire.finish(epoch);
    assert(before && before->find("source.arp"));
    const char* diagnostic = nullptr;
    auto prepared = before->prepareScopeFileSave(
        files, epoch, "source.arp", bytes(replacement), maxBytes, diagnostic);
    assert(prepared && !diagnostic);
    // A prepared receipt must not become public before the real disk commit.
    assert(before->find("source.arp")->receipt.bytes == bytes(original));
    auto disk = readBinaryFile(workspace.root / "nested/source.arp", maxBytes);
    assert(disk.success && disk.bytes == bytes(original));
    Probe<FileSaveResult> probe;
    assert(files.save(prepared->takeRequest(), {Probe<FileSaveResult>::complete, &probe})
        == FileIoStatus::Accepted);
    wait(queue, [&] {
        return probe.calls == 1;
    });
    assert(probe.result.commitState == FileCommitState::Committed);
    SharedPtr<const ProjectSourceSnapshot> after;
    auto quota = makeShared<CpuMemoryQuota>(1);
    {
        CpuMemoryQuotaScope failure(quota);
        after = prepared->finish(files, epoch, probe.result);
    }
    assert(after && !prepared->diagnostic());
    assert(after->validate(files, epoch, diagnostic));
    assert(after->size() == 2 && after->find("source.arp")->receipt.bytes == bytes(replacement));
    assert(after->find("dependency.bin")->receipt.bytes == bytes("unchanged dependency"));
    assert(after->find("dependency.bin")->receipt.version
        == before->find("dependency.bin")->receipt.version);
    assert(after->identities()[0].fingerprint == before->identities()[0].fingerprint);
    assert(after->find("source.arp")->receipt.version == probe.result.committedVersion);
    assert(after->location().mount == before->location().mount);
    assert(after->location().root == before->location().root && after->location().epoch == epoch);
    assert(!prepared->finish(files, epoch, probe.result));
    assert(prepared->takeRequest().contractVersion == 0);
    assert(before->find("source.arp")->receipt.bytes == bytes(original));

    auto conflict = after->prepareScopeFileSave(
        files, epoch, "source.arp", bytes("candidate"), maxBytes, diagnostic);
    assert(conflict);
    workspace.write("nested/source.arp", "external-writer");
    assert(files.save(conflict->takeRequest(), {Probe<FileSaveResult>::complete, &probe})
        == FileIoStatus::Accepted);
    wait(queue, [&] {
        return probe.calls == 2;
    });
    assert(probe.result.status == FileIoStatus::VersionMismatch);
    assert(!conflict->finish(files, epoch, probe.result));
    disk = readBinaryFile(workspace.root / "nested/source.arp", maxBytes);
    assert(disk.success && disk.bytes == bytes("external-writer"));
    assert(after->find("source.arp")->receipt.bytes == bytes(replacement));
    workspace.write("nested/source.arp", replacement);

    auto payload = bytes("oom-candidate");
    {
        CpuMemoryQuotaScope failure(quota);
        assert(!after->prepareScopeFileSave(
            files, epoch, "source.arp", std::move(payload), maxBytes, diagnostic));
    }
    assert(diagnostic && quota->liveBytes() == 0);
    disk = readBinaryFile(workspace.root / "nested/source.arp", maxBytes);
    assert(disk.success && disk.bytes == bytes(replacement));

    // Changing the project owner after a real commit refuses publication while
    // preserving the receipt needed for explicit reconciliation.
    auto stale = after->prepareScopeFileSave(
        files, epoch, "source.arp", bytes("committed"), maxBytes, diagnostic);
    assert(stale);
    assert(files.save(stale->takeRequest(), {Probe<FileSaveResult>::complete, &probe})
        == FileIoStatus::Accepted);
    wait(queue, [&] {
        return probe.calls == 3;
    });
    assert(probe.result.commitState == FileCommitState::Committed);
    assert(!stale->finish(files, epoch + 1, probe.result));
    assert(std::string_view(stale->diagnostic()) == "PROJECT-SOURCE-SAVE-STALE-OWNER");
    disk = readBinaryFile(workspace.root / "nested/source.arp", maxBytes);
    assert(disk.success && disk.bytes == bytes("committed"));
    mounts.shutdown();
    queue.dispatch(8);
    assert(native->telemetry().inFlight == 0 && native->telemetry().reservedBytes == 0);
    assert(native->telemetry().openNativeObjects == 0);
    assert(mounts.telemetry().inFlight == 0 && mounts.telemetry().reservedReadBytes == 0);
}
#endif
} // namespace

int main(int argc, char** argv)
{
    readonlySourceContract();
    scopeReplacementCycle();
    const auto scopeBefore = defaultCpuAllocator().stats();
    const auto scopeCycles = argc > 1 && std::string_view(argv[1]) == "--smoke" ? 20 : 1;
    for (int i = 0; i < scopeCycles; ++i)
        scopeReplacementCycle();
    const auto scopeAfter = defaultCpuAllocator().stats();
    assert(scopeBefore.liveBytes == scopeAfter.liveBytes);
    assert(scopeBefore.allocationCount - scopeBefore.deallocationCount
        == scopeAfter.allocationCount - scopeAfter.deallocationCount);
    std::printf("full scope save %d cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
        scopeCycles,
        static_cast<unsigned long long>(scopeBefore.liveBytes),
        static_cast<unsigned long long>(scopeAfter.liveBytes),
        static_cast<unsigned long long>(
            scopeBefore.allocationCount - scopeBefore.deallocationCount),
        static_cast<unsigned long long>(scopeAfter.allocationCount - scopeAfter.deallocationCount));
#if defined(_WIN32)
    Workspace workspace;
    cycle(workspace);
    scriptCycle(workspace);
    const auto before = defaultCpuAllocator().stats();
    const auto cycles = argc > 1 && std::string_view(argv[1]) == "--smoke" ? 20 : 1;
    for (int i = 0; i < cycles; ++i) {
        cycle(workspace);
        scriptCycle(workspace);
    }
    const auto after = defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    std::printf("source save %d cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
        cycles,
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(before.allocationCount - before.deallocationCount),
        static_cast<unsigned long long>(after.allocationCount - after.deallocationCount));
#else
    (void)argc;
    (void)argv;
    std::puts("Project source save native integration: UnsupportedHost");
#endif
}
