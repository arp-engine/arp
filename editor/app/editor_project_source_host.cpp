#include "editor/app/editor_project_source_host.h"

#include "core/io/filesystem_path.h"
#include "core/io/mounted_file_backend.h"
#include "core/io/native_directory_file_backend.h"
#include "editor/workspace/editor_shell.h"

namespace arp::editor {

bool EditorProjectSourceHost::bind(EditorShell& shell, const std::filesystem::path& root)
{
    if (runtime_)
        return false;
    EditorProjectIoBudget budget;
    core::io::NativeDirectoryFileBackendDesc native;
    native.rootPath = core::FilePath::utf8(root);
    native.maxInFlight = 1;
    native.maxBytesInFlight = 2 * budget.inputs.maxFileBytes;
    native.maxPathBytes = ProjectInputBudget::maxPathBytes;
    native.ioChunkBytes = 64 * 1024;
    native.directoryBufferBytes = 64 * 1024;
    native.maxScanEntries = budget.directory.maxEntries;
    native.maxScanNameBytes = budget.directory.maxNameBytes;
    auto backend = core::makeShared<core::io::NativeDirectoryFileBackend>(native);
    const auto capability = backend->capability();
    if (!capability.completesThroughCompletionQueue || !capability.supportsEnumeration
        || !capability.supportsContentVersion)
        return false;
    auto mounts = core::makeShared<core::io::MountedFileBackend>(core::io::MountedFileBackendDesc{
        1, 1, native.maxBytesInFlight, 128, ProjectInputBudget::maxPathBytes});
    const auto mount = mounts->mount("workspace", std::move(backend));
    if (!mount)
        return false;
    core::runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = 8;
    desc.completionBudget = 8;
    desc.enableWork = false;
    desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = std::move(mounts);
    auto candidate = core::makeUnique<core::runtime::AsyncRuntime>(std::move(desc));
    if (!candidate->valid()
        || !shell.bindProjectSourceFiles(candidate->fileService(), mount, "workspace", budget))
        return false;
    runtime_ = std::move(candidate);
    return true;
}

void EditorProjectSourceHost::dispatch() noexcept
{
    if (runtime_)
        (void)runtime_->dispatch();
}
} // namespace arp::editor
