#pragma once

#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "editor/project/project_manifest.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_source_recovery_documents.h"
#include "tests/fixtures/unified_io_fixture.h"

namespace arp::editor::testfixtures::source_recovery {
using namespace arp;
using namespace editor;
using namespace core::io;
inline constexpr std::uint64_t epoch = 9;
inline constexpr ProjectInputBudget limits{4, 65536, 131072};
// Three explicit fixture slots: write limit, observed-baseline limit and a
// bounded private transport workspace (including native rename/path scratch).
inline constexpr std::size_t transportBytes = 3 * ScriptDocumentSession::maxScriptBytes;

class Host final {
public:
    explicit Host(bool native, bool writable = false)
    {
        record.sessionId = "4305ee93-3897-43ae-93da-ecbb0c569c83";
        record.sequence = record.workspaceEpoch = 1;
        record.workspace = "Script";
        record.activeScript = 0;
        record.source.emplace();
        record.source->alias = "workspace";
        record.source->root = "folder";
        record.project.emplace();
        ProjectManifest manifest;
        manifest.id = "00000000-0000-4000-8000-000000000094";
        manifest.name = "Recovery observations";
        manifest.engineContract = "arp.scene-asset/2";
        ProjectDiagnostic codec;
        assert(ProjectManifestCodec::encode(manifest, record.project->savedFileBytes, codec));
        record.project->id = manifest.id;
        record.project->draftYaml = record.project->savedFileBytes;
        record.source->manifestFileBytes = record.project->savedFileBytes;
        EditorSceneDocument document(false);
        document.scene().setName("Observed scene");
        core::pmr::String diagnostic;
        assert(EditorSceneAuthoringCodec::encode(document, record.scene.yaml, diagnostic));
        assert(EditorSceneAuthoringCodec::contentHash(
            document, record.scene.savedContentHash, diagnostic));
        record.scene.savedFileBytes = record.scene.yaml;
        record.scene.path = record.scene.boundTarget = "scene.yaml";
        EditorRecoveryScript script;
        script.path = "rules.arp";
        script.savedFileBytes = "\xef\xbb\xbf# saved\r\n";
        script.savedText = ScriptDocumentSession::normalizeFileBytes(script.savedFileBytes);
        script.text = "# unsaved\n";
        script.newline = "\r\n";
        record.scripts.push_back(std::move(script));
        std::filesystem::create_directories(workspace.root / "folder");
        auto memory = core::makeShared<MemoryFileBackend>(
            MemoryFileBackendDesc{8, limits.maxTotalBytes, 1, transportBytes, 512, writable});
        assert(memory->registerDirectory("folder") == FileIoStatus::Success);
        const auto add = [&](const char* relative, std::string_view bytes) {
            const auto path = core::pmr::String("folder/") + relative;
            workspace.write(path, bytes);
            assert(memory->registerFile(
                       path, reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size())
                == FileIoStatus::Success);
        };
        add("project.arp.yaml", record.source->manifestFileBytes);
        add("scene.yaml", record.scene.savedFileBytes);
        add("rules.arp", record.scripts.front().savedFileBytes);
        add("changed.arp", "# current external\n");
        if (native)
            backend = core::makeShared<NativeDirectoryFileBackend>(nativeDescriptor(writable));
        else
            backend = memory;
        mount = mounts.mount("workspace", backend);
        assert(bool(mount));
        ProjectSourceSession acquire(files, {mount, "folder", epoch}, limits);
        assert(acquire.read("project.arp.yaml", limits.maxFileBytes) == FileIoStatus::Accepted);
        unified_io_fixture::wait(queue, [&] {
            return acquire.state() != ProjectSourceState::Pending;
        });
        context = {acquire.finish(epoch), &files, epoch};
        assert(bool(context.snapshot));
        record.source->inputs = context.snapshot->identities();
        assert(EditorRecoveryCodec::validate(record, diagnostic));
    }

    core::io::NativeDirectoryFileBackendDesc nativeDescriptor(bool writable) const
    {
        auto descriptor = workspace.descriptor();
        descriptor.allowObservedBaselineReplace = writable;
        descriptor.maxBytesInFlight = transportBytes;
        return descriptor;
    }

    ~Host()
    {
        mounts.shutdown();
        // The stale-mount probe deliberately removes its registry owner. The
        // host still owns that transport and explicitly quiesces it here.
        backend->shutdown();
        while (queue.pending())
            queue.dispatch(8);
        assert(queue.reserved() == 0 && mounts.telemetry().inFlight == 0);
        if (const auto native = std::dynamic_pointer_cast<NativeDirectoryFileBackend>(backend)) {
            const auto telemetry = native->telemetry();
            assert(telemetry.inFlight == 0 && telemetry.reservedBytes == 0
                && telemetry.openNativeObjects == 0);
        }
    }

    void finish(EditorSourceRecoveryFiles& task)
    {
        unified_io_fixture::wait(queue, [&] {
            task.advance(epoch);
            return task.state() != RecoverySourceFilesState::Pending;
        });
    }

    void finish(EditorSourceRecoveryDocuments& task)
    {
        unified_io_fixture::wait(queue, [&] {
            task.advance(epoch);
            return task.state() != RecoverySourceDocumentsState::Pending;
        });
    }

    unified_io_fixture::Workspace workspace;
    core::runtime::CompletionQueue queue{{8}};
    MountedFileBackend mounts{{2, 1, transportBytes, 32, 1024}};
    FileService files{queue, mounts};
    core::SharedPtr<ManagedFileBackend> backend;
    core::PoolHandle mount;
    ProjectSourceContext context;
    EditorRecoverySnapshot record;
};

} // namespace arp::editor::testfixtures::source_recovery
