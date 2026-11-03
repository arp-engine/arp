#pragma once

#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "editor/remote/editor_remote_protocol.h"
#include "editor/tests/fixtures/script_scene_shell_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cstdio>

namespace arp::editor::testfixtures::sources {
using namespace arp;
using namespace editor;
using namespace editor::testfixtures;
using namespace core::io;
inline constexpr std::size_t fileBytes = 1024 * 1024;

// One explicit bundle serves Memory/native/HTTP; logical paths never name the
// physical fixture directory. Mutations below are deliberate failure probes.
class FixtureSource final {
public:
    FixtureSource()
    {
        std::filesystem::create_directories(fixture.files.root / "workspace");
        for (const auto* name :
            {"project.arp.yaml", "primary.arp", "secondary.arp", "scene.yaml"}) {
            const auto bytes = fixture.read(name);
            write(name, bytes);
        }
        // The visual fixture must exercise the real viewport, not the explicit
        // CameraMissing static reference preview. Keep the shared fixture's
        // script/native identities unchanged when adding the camera object.
        EditorSceneDocument scripted(false);
        core::pmr::String diagnostic;
        check(scripted.bindServiceSession(Fixture::services(), diagnostic), diagnostic);
        const auto input =
            EditorSceneScriptPolicy::inputs(fixture.files.root, fixture.files.manifest);
        check(EditorSceneScriptCodec::decode(fixture.original, scripted, input, diagnostic),
            diagnostic);
        check(scripted.addComponent("CameraComponent", scripted.createObject("Source Camera"))
            != nullptr);
        check(scripted.select(fixture.child));
        check(EditorSceneScriptCodec::encode(scripted, input, scene, diagnostic), diagnostic);
        write("scene.yaml", scene);
        EditorSceneDocument plain(false);
        plain.scene().setName("Mounted sky scene");
        const auto sky = plain.createObject("Mounted sky");
        check(plain.addComponent("SkyComponent", sky) != nullptr);
        check(
            plain.addComponent("CameraComponent", plain.createObject("Source Camera")) != nullptr);
        check(plain.select(sky));
        check(EditorSceneAuthoringCodec::encode(plain, v3, diagnostic), diagnostic);
        write("plain.yaml", v3);
        write("bom.yaml", core::pmr::String("\xef\xbb\xbf") + scene);
        write("bad.yaml", "type: arp.editor-scene\ncontractVersion: 999\n");
        write("utf8.yaml", std::string_view("\xc0\x80", 2));
        write("nul.yaml", std::string_view("a\0b", 3));
    }

    void write(const char* name, std::string_view bytes)
    {
        fixture.files.write(core::pmr::String("workspace/") + name, bytes);
    }

    core::SharedPtr<ManagedFileBackend> backend(
        unsigned mode, std::string_view url = {}, bool writable = false)
    {
        if (mode == 1) {
            auto desc = workspace.descriptor();
            desc.rootPath = core::FilePath::utf8(fixture.files.root);
            desc.maxBytesInFlight = 8 * fileBytes;
            desc.allowObservedBaselineReplace = writable;
            desc.ioChunkBytes = 4096;
            desc.maxScanEntries = 32;
            return core::makeShared<NativeDirectoryFileBackend>(desc);
        }
        if (mode == 2) {
            HttpFileBackendDesc desc;
            desc.baseUrl = url;
            desc.maxInFlight = 1;
            desc.maxBytesInFlight = 2 * fileBytes;
            desc.maxPathBytes = 512;
            desc.maxHeaderBytes = 8192;
            desc.ioChunkBytes = 4096;
            desc.timeoutMs = 3000;
            desc.cancellationPollMs = 2;
            return core::makeShared<HttpFileBackend>(desc);
        }
        auto memory = core::makeShared<MemoryFileBackend>(
            MemoryFileBackendDesc{16, 8 * fileBytes, 1, 8 * fileBytes, 512, writable});
        check(memory->registerDirectory("workspace") == FileIoStatus::Success);
        for (const auto* name : {"project.arp.yaml",
                 "primary.arp",
                 "secondary.arp",
                 "scene.yaml",
                 "plain.yaml",
                 "bom.yaml",
                 "bad.yaml",
                 "utf8.yaml",
                 "nul.yaml"}) {
            const auto bytes = fixture.read((core::pmr::String("workspace/") + name).c_str());
            check(memory->registerFile(core::pmr::String("workspace/") + name,
                      reinterpret_cast<const std::uint8_t*>(bytes.data()),
                      bytes.size())
                == FileIoStatus::Success);
        }
        core::pmr::String large(fileBytes + 1, 'x');
        check(memory->registerFile("workspace/large.yaml",
                  reinterpret_cast<const std::uint8_t*>(large.data()),
                  large.size())
            == FileIoStatus::Success);
        return memory;
    }

    ScriptSceneShellFixture fixture;
    unified_io_fixture::Workspace workspace;
    core::pmr::String v3, scene;
};

class Host final {
public:
    explicit Host(
        FixtureSource& source, unsigned mode = 0, std::string_view url = {}, bool writable = false)
        : mount(mounts.mount("source", source.backend(mode, url, writable)))
    {
        check(bool(mount));
    }

    ~Host()
    {
        mounts.shutdown();
        while (queue.pending())
            queue.dispatch(8);
        check(mounts.telemetry().inFlight == 0);
    }

    static EditorResponse command(EditorShell& shell, EditorCommandPayload payload)
    {
        auto result = shell.submitCommand(std::move(payload));
        if (result.status != EditorResponseStatus::Completed)
            std::fprintf(stderr, "command rejected: %u\n", unsigned(result.diagnostic));
        check(result.status == EditorResponseStatus::Completed);
        return result;
    }

    static EditorStateSnapshot state(EditorShell& shell)
    {
        return command(shell, GetEditorStateQuery{}).snapshot;
    }

    void open(EditorShell& shell)
    {
        check(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                    temporary.root)
                .ok());
        EditorProjectIoBudget budget;
        budget.inputs = {64, fileBytes, 4 * fileBytes};
        budget.directory = {32, 4, 4096, 8192};
        budget.maxDecodedBytes = 4 * fileBytes;
        check(shell.bindProjectSourceFiles(files, mount, "workspace", budget));
        OpenProjectCommand open{"workspace"};
        open.source = "workspace";
        command(shell, std::move(open));
        unified_io_fixture::wait(queue, [&] {
            shell.pollTasks();
            return state(shell).projectIoTaskStatus != ProjectIoStatus::Pending;
        });
        const auto result = state(shell);
        check(result.projectIoTaskStatus == ProjectIoStatus::Succeeded, result.projectIoDiagnostic);
    }

    static std::uint64_t begin(EditorShell& shell, const char* path)
    {
        LoadSceneCommand load(path);
        load.discardChanges = true;
        const auto result = command(shell, load).snapshot;
        check(result.sceneIoPending && result.scenePersistenceTaskId
            && result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Pending);
        return result.scenePersistenceTaskId;
    }

    EditorStateSnapshot finish(EditorShell& shell, std::uint64_t id)
    {
        unified_io_fixture::wait(queue, [&] {
            shell.pollTasks();
            return command(shell, GetScenePersistenceTaskQuery{id})
                       .snapshot.scenePersistenceTaskStatus
                != ScenePersistenceTaskStatus::Pending;
        });
        return command(shell, GetScenePersistenceTaskQuery{id}).snapshot;
    }

    unified_io_fixture::Workspace temporary;
    core::runtime::CompletionQueue queue{{8}};
    MountedFileBackend mounts{{2, 1, 8 * fileBytes, 128, 512}};
    FileService files{queue, mounts};
    core::PoolHandle mount;
};

} // namespace arp::editor::testfixtures::sources
