#pragma once

#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "editor/remote/editor_workspace_protocol.h"
#include "tests/fixtures/unified_io_fixture.h"

namespace arp::editor::tests::workspace {
inline constexpr const char* readToken = "read-01234567890123456789012345678901";
inline constexpr const char* writeToken = "write-0123456789012345678901234567890";
inline constexpr const char* saved = "\xef\xbb\xbf# saved\r\n";

class Host final {
public:
    explicit Host(bool native)
    {
        std::filesystem::create_directories(workspace.root / "folder");
        auto memory = core::makeShared<core::io::MemoryFileBackend>(
            core::io::MemoryFileBackendDesc{8, 131072, 1, 262144, 512, true});
        assert(memory->registerDirectory("folder") == core::io::FileIoStatus::Success);
        for (const auto* path : {"rules.arp", "project.yaml", "scene.yaml", "changed.arp"}) {
            const auto name = core::pmr::String("folder/") + path;
            workspace.write(name, saved);
            assert(memory->registerFile(name,
                       reinterpret_cast<const std::uint8_t*>(saved),
                       std::char_traits<char>::length(saved))
                == core::io::FileIoStatus::Success);
        }
        if (native) {
            auto desc = workspace.descriptor();
            desc.allowObservedBaselineReplace = true;
            backend = core::makeShared<core::io::NativeDirectoryFileBackend>(desc);
        } else
            backend = memory;
        mount = mounts.mount("workspace", backend);
        assert(bool(mount));
    }

    ~Host()
    {
        mounts.shutdown();
        backend->shutdown();
        while (queue.pending())
            queue.dispatch(8);
        assert(queue.reserved() == 0 && mounts.telemetry().inFlight == 0);
        if (const auto native =
                std::dynamic_pointer_cast<core::io::NativeDirectoryFileBackend>(backend)) {
            const auto stats = native->telemetry();
            assert(stats.inFlight == 0 && stats.reservedBytes == 0 && stats.openNativeObjects == 0);
        }
    }

    unified_io_fixture::Workspace workspace;
    core::runtime::CompletionQueue queue{{8}};
    core::io::MountedFileBackend mounts{{2, 1, 262144, 128, 512}};
    core::io::FileService files{queue, mounts};
    core::SharedPtr<core::io::ManagedFileBackend> backend;
    core::PoolHandle mount;
};

inline EditorWorkspaceProtocolDesc config(core::PoolHandle mount = {})
{
    EditorWorkspaceProtocolDesc d;
    d.mount = mount;
    d.workspaceId = "fixture";
    d.sessionId = "session-a";
    d.readToken = readToken;
    d.writeToken = writeToken;
    d.expectedHost = "127.0.0.1:8094";
    d.allowedOrigin = "http://127.0.0.1:8093";
    d.maxFileBytes = 65536;
    d.maxRequestBytes = d.maxResponseBytes = 131072;
    d.preparationBytes = 2 * 1024 * 1024;
    d.maxQueryBytes = 8192;
    d.maxDirectoryEntries = 16;
    d.maxDirectoryNameBytes = 1024;
    d.maxReceipts = 2;
    return d;
}
} // namespace arp::editor::tests::workspace
