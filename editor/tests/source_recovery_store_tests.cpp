#include "core/hash/sha256.h"
#include "editor/tests/fixtures/source_recovery_capture_fixture.h"
#include "editor/workspace/editor_scene_script_policy.h"
#include "editor/workspace/editor_source_recovery_preparation.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace editor;
using namespace editor::testfixtures::source_recovery;
using namespace core::io;

void checkAt(int line, bool value, std::string_view diagnostic = {})
{
    if (!value)
        std::fprintf(stderr,
            "Source recovery store check %d: %.*s\n",
            line,
            int(diagnostic.size()),
            diagnostic.data());
    assert(value);
}

#define check(...) checkAt(__LINE__, __VA_ARGS__)
using Store = EditorSourceRecoveryStore;

FileReadResult read(CaptureFixture& fixture, std::string_view path)
{
    return fixture.readFile(path);
}

void external(CaptureFixture& fixture, std::string_view path, std::string_view text)
{
    fixture.replaceFile(path, text);
}

void captureContract(CaptureFixture& fixture)
{
    const auto submitted = fixture.host.files.telemetry().submitted;
    EditorSourceRecoveryCapture owned(fixture.input(1), limits, fixture.budget());
    check(fixture.host.files.telemetry().submitted == submitted);
    fixture.document.scene().setName("Newer live scene");
    fixture.draft.name = "Newer live project";
    fixture.scripts.active()->text = "# newer live text\n";
    unified_io_fixture::wait(fixture.host.queue, [&] {
        owned.advance(epoch);
        return owned.state() != SourceRecoveryCaptureState::Pending;
    });
    const char* issue = nullptr;
    const auto* snapshot = owned.snapshot(epoch, issue);
    check(snapshot && snapshot->scene.yaml.find("Captured unsaved scene") != core::pmr::String::npos
        && snapshot->project->draftYaml.find("Captured project draft") != core::pmr::String::npos
        && snapshot->scripts[0].text == "# unsaved\n");
    check(!owned.snapshot(epoch + 1, issue));
    auto invalid = fixture.input(2);
    invalid.sceneVersion = 99;
    EditorSourceRecoveryCapture version(invalid, limits, fixture.budget());
    check(version.state() == SourceRecoveryCaptureState::Failed
        && version.diagnostic() == "EDITOR-RECOVERY-SOURCE-CAPTURE-SCENE-VERSION");
    auto budget = fixture.budget();
    budget.maxScripts = 0;
    EditorSourceRecoveryCapture capacity(fixture.input(2), limits, budget);
    check(capacity.state() == SourceRecoveryCaptureState::Failed);
    EditorSourceRecoveryCapture stale(fixture.input(2), limits, fixture.budget());
    stale.advance(epoch + 1);
    check(stale.state() == SourceRecoveryCaptureState::Failed);
    EditorSourceRecoveryCapture cancelled(fixture.input(2), limits, fixture.budget());
    cancelled.advance(epoch);
    cancelled.cancel();
    check(cancelled.state() == SourceRecoveryCaptureState::Cancelled);
    unified_io_fixture::wait(fixture.host.queue, [&] {
        return fixture.host.queue.reserved() == 0 && fixture.host.mounts.telemetry().inFlight == 0;
    });
    core::pmr::String diagnostic;
    EditorSceneDocument foreign(false);
    auto wrong = fixture.input(2);
    EditorSourceRecoveryCaptureInput foreignInput{wrong.sources,
        wrong.resources,
        wrong.projectDraft,
        foreign,
        wrong.scripts,
        wrong.alias,
        wrong.sessionId,
        wrong.workspace,
        wrong.sequence,
        wrong.workspaceEpoch,
        wrong.sceneVersion,
        wrong.scenePath,
        wrong.sceneSavedHash,
        wrong.sceneSavedFileBytes};
    EditorSourceRecoveryCapture owner(foreignInput, limits, fixture.budget());
    check(owner.state() == SourceRecoveryCaptureState::Failed);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    bool allocationFailed = false;
    try {
        core::CpuMemoryQuotaScope denied(quota);
        EditorSourceRecoveryCapture exhausted(fixture.input(2), limits, fixture.budget());
    } catch (const std::bad_alloc&) {
        allocationFailed = true;
    }
    check(allocationFailed && quota->liveBytes() == 0
        && fixture.document.scene().name() == "Newer live scene"
        && fixture.scripts.active()->text == "# newer live text\n");
    // The explicit v4 observation path still requires a real prepared cold
    // domain, including when the manifest declares zero script components.
    const auto root = std::filesystem::path("folder");
    const auto policy = EditorSceneScriptPolicy::inputs(root, fixture.draft);
    check(fixture.document.prepareScriptAuthoringSources(
              fixture.host.context, fixture.draft, policy.modules, diagnostic),
        diagnostic);
    auto v4 = fixture.input(3);
    v4.sceneVersion = EditorSceneScriptCodec::contractVersion;
    v4.scenePath = v4.sceneSavedHash = v4.sceneSavedFileBytes = {};
    EditorSourceRecoveryCapture scripted(v4, limits, fixture.budget());
    unified_io_fixture::wait(fixture.host.queue, [&] {
        scripted.advance(epoch);
        return scripted.state() != SourceRecoveryCaptureState::Pending;
    });
    snapshot = scripted.snapshot(epoch, issue);
    check(snapshot && snapshot->scene.yaml.find("contractVersion: 4") != core::pmr::String::npos,
        scripted.diagnostic());
}

void conflictCapture(CaptureFixture& fixture)
{
    auto record = fixture.host.record;
    auto changed = record.scripts[0];
    changed.path = "changed.arp";
    changed.savedText = changed.savedFileBytes = "# historical\n";
    changed.text = "# changed draft\n";
    changed.newline = "\n";
    auto missing = changed;
    missing.path = "missing.arp";
    missing.text = "# missing draft\n";
    record.scripts.push_back(std::move(changed));
    record.scripts.push_back(std::move(missing));
    record.activeScript = 2;
    EditorSourceRecoveryDocuments task(fixture.host.context, record, "workspace", limits);
    fixture.host.finish(task);
    core::pmr::String diagnostic;
    auto prepared = task.prepareForSession(fixture.scripts, epoch, diagnostic);
    check(prepared
            && fixture.scripts.commitRecoveredSourceDocuments(
                   *prepared, fixture.host.context, diagnostic)
                == ScriptDocumentSession::RecoveryCommitResult::Committed,
        diagnostic);
    auto captured = fixture.capture(1);
    const char* issue = nullptr;
    const auto* snapshot = captured->snapshot(epoch, issue);
    check(snapshot && snapshot->scripts.size() == 3 && snapshot->activeScript == 2
        && snapshot->scripts[1].savedFileBytes == "# historical\n"
        && snapshot->scripts[1].text == "# changed draft\n"
        && snapshot->scripts[2].text == "# missing draft\n"
        && !fixture.scripts.documents()[2].sourceSnapshot);
}

void roundtrip(CaptureFixture& fixture)
{
    const auto submitted = fixture.host.files.telemetry().submitted;
    Store store(fixture.host.files, fixture.desc());
    check(fixture.host.files.telemetry().submitted == submitted);
    fixture.finish(store);
    check(store.state() == SourceRecoveryStoreState::Ready, store.diagnostic());
    check(fixture.host.files.telemetry().submitted == submitted + 2);
    const auto now = Store::TimePoint{};
    auto first = fixture.capture(1);
    check(store.submit(*first, epoch, now) == SourceRecoveryStoreSubmit::Accepted);
    check(!store.receipt()->submitted());
    check(store.submit(*first, epoch, now) == SourceRecoveryStoreSubmit::Busy);
    fixture.finish(store);
    check(store.receipt()->terminal() && store.receipt()->published(), store.diagnostic());
    EditorRecoverySnapshot decoded;
    core::pmr::String diagnostic;
    check(store.decodeLatest(epoch, decoded, diagnostic), diagnostic);
    check(decoded.sequence == 1
        && decoded.project->draftYaml.find("Captured project draft") != core::pmr::String::npos
        && decoded.scripts.front().text == "# unsaved\n"
        && decoded.scene.yaml.find("Captured unsaved scene") != core::pmr::String::npos);
    auto second = fixture.capture(2);
    check(store.submit(*second, epoch, now + std::chrono::seconds(1))
        == SourceRecoveryStoreSubmit::NotDue);
    check(store.submit(*second, epoch, now - std::chrono::seconds(1))
        == SourceRecoveryStoreSubmit::Rejected);
    check(store.submit(*second, epoch, now + std::chrono::seconds(10))
        == SourceRecoveryStoreSubmit::Accepted);
    fixture.finish(store);
    check(store.receipt()->published());
    auto third = fixture.capture(3);
    check(store.submit(*third, epoch, now + std::chrono::seconds(20))
        == SourceRecoveryStoreSubmit::Accepted);
    fixture.finish(store);
    check(store.receipt()->published() && store.nextSequence() == 4);
    check(store.decodeLatest(epoch, decoded, diagnostic) && decoded.sequence == 3);
    Store collision(fixture.host.files, fixture.desc());
    fixture.finish(collision);
    check(collision.state() == SourceRecoveryStoreState::Failed);
    Store reopened(fixture.host.files, fixture.desc(SourceRecoveryStoreMode::Resume));
    fixture.finish(reopened);
    check(reopened.state() == SourceRecoveryStoreState::Ready, reopened.diagnostic());
    check(reopened.nextSequence() == 4 && reopened.decodeLatest(epoch, decoded, diagnostic)
        && decoded.sequence == 3 && decoded.workspaceEpoch == 3);
    check(!reopened.decodeLatest(epoch + 1, decoded, diagnostic));
    auto descriptor = fixture.desc();
    descriptor.slots[1] = descriptor.slots[0];
    Store duplicate(fixture.host.files, descriptor);
    check(duplicate.state() == SourceRecoveryStoreState::Failed);
    descriptor = fixture.desc(SourceRecoveryStoreMode::Inspect);
    Store inspector(fixture.host.files, descriptor);
    fixture.finish(inspector);
    check(inspector.state() == SourceRecoveryStoreState::Ready
        && inspector.submit(*third, epoch, now) == SourceRecoveryStoreSubmit::Rejected);
}

void slotConflicts(CaptureFixture& fixture)
{
    Store store(fixture.host.files, fixture.desc());
    fixture.finish(store);
    auto first = fixture.capture(1);
    check(store.submit(*first, epoch, Store::TimePoint{}) == SourceRecoveryStoreSubmit::Accepted);
    fixture.finish(store);
    check(store.receipt()->published());
    auto second = fixture.capture(2);
    check(store.submit(*second, epoch, Store::TimePoint{} + std::chrono::seconds(10))
        == SourceRecoveryStoreSubmit::Accepted);
    fixture.finish(store);
    check(store.receipt()->published());
    // Corrupt the older slot while the newest slot is still valid. Neither
    // preflight nor reopening may silently accept the newer surviving record.
    external(fixture, "recovery-a.yaml", "type: arp.editor-recovery\ncontractVersion: 999\n");
    auto third = fixture.capture(3);
    check(store.submit(*third, epoch, Store::TimePoint{} + std::chrono::seconds(20))
        == SourceRecoveryStoreSubmit::Accepted);
    fixture.finish(store);
    check(store.state() == SourceRecoveryStoreState::Failed && !store.receipt()->submitted()
        && store.receipt()->result().commitState == FileCommitState::NotCommitted
        && store.nextSequence() == 3);
    Store reopened(fixture.host.files, fixture.desc(SourceRecoveryStoreMode::Resume));
    fixture.finish(reopened);
    check(reopened.state() == SourceRecoveryStoreState::Failed);
    const auto intact = read(fixture, "recovery-b.yaml");
    EditorRecoverySnapshot snapshot;
    core::pmr::String diagnostic;
    check(EditorRecoveryCodec::decode(
              {reinterpret_cast<const char*>(intact.bytes.data()), intact.bytes.size()},
              snapshot,
              diagnostic,
              fixture.budget())
        && snapshot.sequence == 2);
}

void dependencyConflict(CaptureFixture& fixture)
{
    Store store(fixture.host.files, fixture.desc());
    fixture.finish(store);
    auto capture = fixture.capture(1);
    external(fixture, "project.arp.yaml", "# changed input\n");
    check(store.submit(*capture, epoch, Store::TimePoint{}) == SourceRecoveryStoreSubmit::Accepted);
    fixture.finish(store);
    check(store.state() == SourceRecoveryStoreState::Failed && !store.receipt()->submitted()
        && store.nextSequence() == 1
        && read(fixture, "recovery-a.yaml").status == FileIoStatus::MissingFile);
    EditorSourceRecoveryCapture stale(fixture.input(1), limits, fixture.budget());
    unified_io_fixture::wait(fixture.host.queue, [&] {
        stale.advance(epoch);
        return stale.state() != SourceRecoveryCaptureState::Pending;
    });
    check(stale.state() == SourceRecoveryCaptureState::Failed);
}

void staleStorage(CaptureFixture& fixture, bool afterAcceptance)
{
    Store store(fixture.host.files, fixture.desc());
    fixture.finish(store);
    auto capture = fixture.capture(1);
    check(store.submit(*capture, epoch, Store::TimePoint{}) == SourceRecoveryStoreSubmit::Accepted);
    auto receipt = store.receipt();
    if (afterAcceptance) {
        unified_io_fixture::wait(fixture.host.queue, [&] {
            store.advance(epoch);
            return receipt->submitted();
        });
        // Deliver the actual successful result before making its publication
        // stale. The store must preserve Committed and refuse to publish it.
        unified_io_fixture::wait(fixture.host.queue, [&] {
            return receipt->terminal();
        });
    }
    store.advance(epoch + 1);
    check(!receipt->published());
    if (afterAcceptance) {
        check(store.state() == SourceRecoveryStoreState::ReconcileRequired
            && receipt->result().commitState == FileCommitState::Committed);
    } else {
        check(store.state() == SourceRecoveryStoreState::Failed && !receipt->submitted()
            && receipt->terminal()
            && receipt->result().commitState == FileCommitState::NotCommitted);
    }
    check(fixture.host.mounts.unmount(fixture.host.mount));
    const char* issue = nullptr;
    check(!capture->snapshot(epoch, issue));
}

void readonlyAndReopen(CaptureFixture& fixture, bool native)
{
    {
        Store store(fixture.host.files, fixture.desc());
        fixture.finish(store);
        auto capture = fixture.capture(1);
        check(store.submit(*capture, epoch, Store::TimePoint{})
            == SourceRecoveryStoreSubmit::Accepted);
        fixture.finish(store);
        check(store.receipt()->published());
    }
    auto saved = read(fixture, "recovery-a.yaml");
    const auto manifest = read(fixture, "project.arp.yaml");
    const auto scene = read(fixture, "scene.yaml");
    const auto script = read(fixture, "rules.arp");
    check(fixture.host.mounts.unmount(fixture.host.mount));
    fixture.host.backend->shutdown();
    if (native) {
        // The new native owner opens the actual previously persisted bytes.
        fixture.host.backend =
            core::makeShared<NativeDirectoryFileBackend>(fixture.host.nativeDescriptor(false));
    } else {
        auto readonly = core::makeShared<MemoryFileBackend>(
            MemoryFileBackendDesc{8, limits.maxTotalBytes, 1, transportBytes, 512, false});
        check(readonly->registerDirectory("folder") == FileIoStatus::Success);
        check(
            readonly->registerFile("folder/recovery-a.yaml", saved.bytes.data(), saved.bytes.size())
            == FileIoStatus::Success);
        for (const auto& entry : {std::make_pair("folder/project.arp.yaml", &manifest),
                 std::make_pair("folder/scene.yaml", &scene),
                 std::make_pair("folder/rules.arp", &script)})
            check(readonly->registerFile(
                      entry.first, entry.second->bytes.data(), entry.second->bytes.size())
                == FileIoStatus::Success);
        fixture.host.backend = readonly;
    }
    fixture.host.mount = fixture.host.mounts.mount("workspace", fixture.host.backend);
    check(bool(fixture.host.mount));
    Store reader(fixture.host.files, fixture.desc(SourceRecoveryStoreMode::Inspect));
    fixture.finish(reader);
    EditorRecoverySnapshot decoded;
    core::pmr::String diagnostic;
    check(reader.decodeLatest(epoch, decoded, diagnostic) && decoded.sequence == 1, diagnostic);
    // Begin the next integration step from actual stored bytes, using new
    // resource/Passive Scene owners and fresh document IDs. This grants no
    // live Shell, history or GPU publication authority.
    EditorSourceRecoveryPreparation recovery(fixture.host.files,
        decoded,
        "workspace",
        {fixture.host.mount, "folder", epoch},
        limits,
        fixture.budget());
    unified_io_fixture::wait(fixture.host.queue, [&] {
        recovery.advance(epoch);
        return recovery.state() != SourceRecoveryState::Pending;
    });
    const char* issue = nullptr;
    const auto* candidate = recovery.candidate(epoch, issue);
    check(candidate && candidate->document->scene().name() == "Captured unsaved scene"
            && candidate->draft.name == "Captured project draft",
        recovery.diagnostic());
    EditorSourceRecoveryDocuments documents(candidate->sources, decoded, "workspace", limits);
    fixture.host.finish(documents);
    ScriptDocumentSession restored(100);
    auto prepared = documents.prepareForSession(restored, epoch, diagnostic);
    check(prepared
            && restored.commitRecoveredSourceDocuments(*prepared, candidate->sources, diagnostic)
                == ScriptDocumentSession::RecoveryCommitResult::Committed,
        diagnostic);
    check(restored.activeId() == 100 && restored.active()->text == "# unsaved\n"
        && restored.active()->sourceSnapshot && !restored.active()->sourceConflict()
        && restored.active()->savedFileBytes == fixture.host.record.scripts[0].savedFileBytes);
    const auto submitted = fixture.host.files.telemetry().submitted;
    Store writer(fixture.host.files, fixture.desc(SourceRecoveryStoreMode::Resume));
    check(writer.state() == SourceRecoveryStoreState::Failed
        && writer.diagnostic() == "EDITOR-RECOVERY-STORE-WRITE-CAPABILITY"
        && fixture.host.files.telemetry().submitted == submitted);
}

void recordMismatch(CaptureFixture& fixture)
{
    auto capture = fixture.capture(1);
    const char* issue = nullptr;
    auto record = *capture->snapshot(epoch, issue);
    record.sessionId = "9305ee93-3897-43ae-93da-ecbb0c569c83";
    core::pmr::String yaml, diagnostic;
    check(EditorRecoveryCodec::encode(record, yaml, diagnostic, fixture.budget()), diagnostic);
    external(fixture, "recovery-a.yaml", yaml);
    Store wrongOwner(fixture.host.files, fixture.desc(SourceRecoveryStoreMode::Resume));
    fixture.finish(wrongOwner);
    check(wrongOwner.state() == SourceRecoveryStoreState::Failed);
    record.sessionId = fixture.host.record.sessionId;
    record.sequence = 2; // Sequence 2 belongs in the other explicit slot.
    check(EditorRecoveryCodec::encode(record, yaml, diagnostic, fixture.budget()), diagnostic);
    external(fixture, "recovery-a.yaml", yaml);
    Store wrongSlot(fixture.host.files, fixture.desc(SourceRecoveryStoreMode::Inspect));
    fixture.finish(wrongSlot);
    check(wrongSlot.state() == SourceRecoveryStoreState::Failed);
    auto desc = fixture.desc();
    desc.maxRawResidentBytes = 4 * desc.records.maxEnvelopeBytes;
    Store insufficient(fixture.host.files, desc);
    check(insufficient.state() == SourceRecoveryStoreState::Failed);
}

void unknown(CaptureFixture& fixture)
{
    Store store(fixture.host.files, fixture.desc());
    fixture.finish(store);
    auto capture = fixture.capture(1);
    check(store.submit(*capture, epoch, Store::TimePoint{}) == SourceRecoveryStoreSubmit::Accepted);
    fixture.finish(store);
    check(store.state() == SourceRecoveryStoreState::ReconcileRequired);
    auto receipt = store.receipt();
    check(receipt->terminal() && !receipt->published()
        && receipt->result().commitState == FileCommitState::OutcomeUnknown);
    check(store.submit(*capture, epoch, Store::TimePoint{}) == SourceRecoveryStoreSubmit::Rejected);
    Store explicitRead(fixture.host.files, fixture.desc(SourceRecoveryStoreMode::Inspect));
    fixture.finish(explicitRead);
    EditorRecoverySnapshot decoded;
    core::pmr::String diagnostic;
    check(explicitRead.decodeLatest(epoch, decoded, diagnostic) && decoded.sequence == 1);
    check(receipt->result().commitState == FileCommitState::OutcomeUnknown);
}

void cancelled(CaptureFixture& fixture)
{
    auto store = core::makeUnique<Store>(fixture.host.files, fixture.desc());
    fixture.finish(*store);
    auto capture = fixture.capture(1);
    check(
        store->submit(*capture, epoch, Store::TimePoint{}) == SourceRecoveryStoreSubmit::Accepted);
    auto receipt = store->receipt();
    unified_io_fixture::wait(fixture.host.queue, [&] {
        store->advance(epoch);
        return receipt->submitted() || receipt->terminal();
    });
    store.reset();
    unified_io_fixture::wait(fixture.host.queue, [&] {
        return receipt->terminal();
    });
    check(!receipt->published());
    auto config = fixture.desc();
    config.slots = {core::pmr::String("cancel-a.yaml"), core::pmr::String("cancel-b.yaml")};
    Store pending(fixture.host.files, config);
    pending.advance(epoch);
    pending.cancel();
    check(pending.state() == SourceRecoveryStoreState::Cancelled);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    Store preparation(fixture.host.files, config);
    fixture.finish(preparation);
    {
        core::CpuMemoryQuotaScope denied(quota);
        check(preparation.submit(*capture, epoch, Store::TimePoint{})
            == SourceRecoveryStoreSubmit::Rejected);
    }
    check(quota->liveBytes() == 0 && preparation.nextSequence() == 1);
}
} // namespace

int main(int argc, char** argv)
{
    bool native = false, smoke = false;
    for (int i = 1; i < argc; ++i) {
        native |= std::string_view(argv[i]) == "--native";
        smoke |= std::string_view(argv[i]) == "--smoke";
    }
    const unsigned loops = smoke ? 20 : 1;
    const auto run = [&] {
        {
            CaptureFixture fixture(native);
            readonlyAndReopen(fixture, native);
        }
        {
            CaptureFixture fixture(native);
            recordMismatch(fixture);
        }
        {
            CaptureFixture fixture(native);
            captureContract(fixture);
        }
        {
            CaptureFixture fixture(native);
            conflictCapture(fixture);
        }
        {
            CaptureFixture fixture(native);
            slotConflicts(fixture);
        }
        {
            CaptureFixture fixture(native);
            dependencyConflict(fixture);
        }
        {
            CaptureFixture fixture(native);
            staleStorage(fixture, false);
        }
        {
            CaptureFixture fixture(native);
            staleStorage(fixture, true);
        }
        {
            CaptureFixture fixture(native);
            roundtrip(fixture);
        }
        {
            CaptureFixture fixture(native, true);
            unknown(fixture);
        }
        {
            CaptureFixture fixture(native);
            cancelled(fixture);
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
    std::printf("Source recovery store %u cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
        loops,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
}
