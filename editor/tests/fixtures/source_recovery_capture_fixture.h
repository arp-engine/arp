#pragma once

#include "core/hash/sha256.h"
#include "editor/tests/fixtures/lost_receipt_backend.h"
#include "editor/tests/fixtures/source_recovery_file_fixture.h"
#include "editor/workspace/editor_scene_service_session.h"
#include "editor/workspace/editor_source_recovery_capture.h"
#include "editor/workspace/editor_source_recovery_store.h"

namespace arp::editor::testfixtures::source_recovery {

class CaptureFixture final {
public:
    explicit CaptureFixture(bool native, bool loseReceipt = false) : host(native, true)
    {
        if (loseReceipt) {
            auto delegate = host.backend;
            assert(host.mounts.unmount(host.mount));
            host.backend = core::makeShared<LostReceiptBackend>(std::move(delegate));
            host.mount = host.mounts.mount("workspace", host.backend);
            reacquire();
        }
        const auto loaded = resources.loadSources(host.context.snapshot, host.files, epoch);
        assert(loaded.success);
        core::pmr::String diagnostic;
        assert(
            document.bindServiceSession(core::makeShared<EditorSceneServiceSession>(
                                            resources.retainManager(), resources.physicsWorld2D()),
                diagnostic));
        assert(EditorSceneAuthoringCodec::decode(host.record.scene.savedFileBytes,
            document,
            &EditorSceneAuthoringCodec::isResourceReady,
            &resources.manager(),
            diagnostic));
        document.scene().setName("Captured unsaved scene");
        EditorSourceRecoveryDocuments tabs(host.context, host.record, "workspace", limits);
        host.finish(tabs);
        assert(tabs.state() == RecoverySourceDocumentsState::Ready);
        auto prepared = tabs.prepareForSession(scripts, epoch, diagnostic);
        assert(prepared
            && scripts.commitRecoveredSourceDocuments(*prepared, host.context, diagnostic)
                == ScriptDocumentSession::RecoveryCommitResult::Committed);
        draft = *resources.manifest();
        draft.name = "Captured project draft";
    }

    void reacquire()
    {
        ProjectSourceSession read(host.files, {host.mount, "folder", epoch}, limits);
        assert(read.read("project.arp.yaml", limits.maxFileBytes) == FileIoStatus::Accepted);
        unified_io_fixture::wait(host.queue, [&] {
            return read.state() != ProjectSourceState::Pending;
        });
        host.context = {read.finish(epoch), &host.files, epoch};
        assert(host.context.snapshot);
    }

    EditorSourceRecoveryCaptureInput input(std::uint64_t sequence) const
    {
        return {host.context,
            resources,
            draft,
            document,
            scripts,
            "workspace",
            host.record.sessionId,
            "Script",
            sequence,
            sequence,
            EditorSceneAuthoringCodec::contractVersion,
            host.record.scene.path,
            host.record.scene.savedContentHash,
            host.record.scene.savedFileBytes};
    }

    static EditorRecoveryBudget budget()
    {
        EditorRecoveryBudget value;
        value.maxEnvelopeBytes = 65536;
        value.maxPayloadBytes = 32768;
        value.maxScripts = 4;
        return value;
    }

    EditorSourceRecoveryStoreDesc desc(
        SourceRecoveryStoreMode mode = SourceRecoveryStoreMode::Fresh) const
    {
        EditorSourceRecoveryStoreDesc value;
        value.storage = {host.mount, "folder", epoch};
        value.slots = {core::pmr::String("recovery-a.yaml"), core::pmr::String("recovery-b.yaml")};
        value.sessionId = host.record.sessionId;
        value.projectId = host.record.project->id;
        value.sourceAlias = "workspace";
        value.sourceRoot = "folder";
        value.mode = mode;
        value.records = budget();
        value.inputs = limits;
        value.maxRawResidentBytes = 5 * value.records.maxEnvelopeBytes;
        return value;
    }

    core::UniquePtr<EditorSourceRecoveryCapture> capture(std::uint64_t sequence)
    {
        auto result =
            core::makeUnique<EditorSourceRecoveryCapture>(input(sequence), limits, budget());
        unified_io_fixture::wait(host.queue, [&] {
            result->advance(epoch);
            return result->state() != SourceRecoveryCaptureState::Pending;
        });
        assert(result->state() == SourceRecoveryCaptureState::Ready);
        return result;
    }

    void finish(EditorSourceRecoveryStore& store)
    {
        unified_io_fixture::wait(host.queue, [&] {
            store.advance(epoch);
            return store.state() != SourceRecoveryStoreState::Opening
                && store.state() != SourceRecoveryStoreState::Pending;
        });
    }

    core::io::FileReadResult readFile(std::string_view path)
    {
        ProjectFileReadTask task(
            host.files, {host.mount, "folder", epoch}, path, budget().maxEnvelopeBytes);
        unified_io_fixture::wait(host.queue, [&] {
            task.advance(epoch);
            return task.state() != ProjectFileReadState::Pending;
        });
        assert(task.state() == ProjectFileReadState::Complete);
        return task.takeResult();
    }

    void replaceFile(std::string_view path, std::string_view text)
    {
        auto observed = readFile(path);
        core::io::FileSaveRequest request;
        request.pathScope = core::io::FilePathScope::MountedResource;
        request.mount = host.mount;
        request.path = core::pmr::String("folder/") + core::pmr::String(path);
        request.bytes.assign(text.begin(), text.end());
        request.maxWriteBytes = budget().maxEnvelopeBytes;
        if (observed.status == core::io::FileIoStatus::Success) {
            core::Sha256 hash;
            hash.update(
                {reinterpret_cast<const char*>(observed.bytes.data()), observed.bytes.size()});
            request.expectedVersion = core::io::FileVersionEncoding::finish(
                hash, core::io::FileVersionKind::ContentSha256);
            request.precondition = core::io::FileSavePrecondition::ContentSha256;
            request.maxBaselineBytes = request.maxWriteBytes;
        }
        ProjectFileSaveTask task(host.files, std::move(request));
        task.advance();
        unified_io_fixture::wait(host.queue, [&] {
            return task.terminal();
        });
        assert(task.result().status == core::io::FileIoStatus::Success);
    }

    Host host;
    EditorProjectResources resources;
    EditorSceneDocument document{false};
    ScriptDocumentSession scripts;
    ProjectManifest draft;
};
} // namespace arp::editor::testfixtures::source_recovery
