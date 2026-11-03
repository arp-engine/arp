#include "editor/app/editor_project_source_host.h"

#include "core/io/filesystem_path.h"
#include "core/io/mounted_file_backend.h"
#include "core/io/native_directory_file_backend.h"
#include "editor/app/editor_workspace_connection_profile.h"
#include "editor/workspace/editor_shell.h"

namespace arp::editor {

struct EditorProjectSourceHost::Connection {
    ConnectionStatus status = ConnectionStatus::Connecting;
    const char* diagnostic = nullptr;

    static void complete(void* data, core::io::WorkspaceFileConnectionResult result) noexcept
    {
        auto& state = *static_cast<Connection*>(data);
        const auto& caps = result.capability;
        if (result.status == core::io::FileIoStatus::Success && caps.supportsRead
            && caps.supportsMetadata && caps.supportsEnumeration && caps.supportsContentVersion
            && caps.completesThroughCompletionQueue) {
            state.status = ConnectionStatus::Ready;
            state.diagnostic = nullptr;
        } else {
            state.status = ConnectionStatus::Failed;
            state.diagnostic = result.diagnosticId ? result.diagnosticId
                                                   : "EDITOR-WORKSPACE-CONNECTION-CAPABILITY";
        }
    }
};

EditorProjectSourceHost::EditorProjectSourceHost() = default;
EditorProjectSourceHost::~EditorProjectSourceHost() = default;

core::io::FileIoStatus EditorProjectSourceHost::connectRemote(
    core::io::WorkspaceFileBackendDesc desc, EditorProjectIoBudget budget) noexcept
{
    using namespace core::io;
    if (runtime_ || !EditorWorkspaceConnectionProfile::validBudget(desc, budget))
        return FileIoStatus::InvalidRequest;
    try {
        auto remote = core::makeShared<WorkspaceFileBackend>(std::move(desc));
        if (remote->diagnostic())
            return FileIoStatus::InvalidRequest;
        auto mounts = core::makeShared<MountedFileBackend>(MountedFileBackendDesc{
            1, 1, 2 * budget.inputs.maxFileBytes, 128, ProjectInputBudget::maxPathBytes});
        const auto mount = mounts->mount("workspace", remote);
        if (!mount)
            return FileIoStatus::BudgetExceeded;
        core::runtime::AsyncRuntimeDesc runtimeDesc;
        runtimeDesc.completionQueueCapacity = runtimeDesc.completionBudget = 8;
        runtimeDesc.enableWork = runtimeDesc.enableThreadPoolFileBackend = false;
        runtimeDesc.fileBackend = std::move(mounts);
        auto candidate = core::makeUnique<core::runtime::AsyncRuntime>(std::move(runtimeDesc));
        if (!candidate->valid())
            return FileIoStatus::BackendUnavailable;
        auto connection = core::makeUnique<Connection>();
        const auto accepted = remote->connectAsync(
            {Connection::complete, connection.get()}, candidate->completionQueue());
        if (accepted != FileIoStatus::Accepted)
            return accepted;
        // No allocation after Accepted: callback storage stays fixed until runtime shutdown.
        connection_ = std::move(connection);
        remote_ = std::move(remote);
        remoteMount_ = mount;
        remoteBudget_ = budget;
        runtime_ = std::move(candidate);
        return FileIoStatus::Accepted;
    } catch (...) {
        return FileIoStatus::BudgetExceeded;
    }
}

bool EditorProjectSourceHost::bindRemote(EditorShell& shell, std::string_view alias)
{
    if (!connection_ || connection_->status != ConnectionStatus::Ready || !runtime_
        || !remote_->connected())
        return false;
    if (!shell.bindProjectSourceFiles(
            runtime_->fileService(), remoteMount_, alias, remoteBudget_)) {
        connection_->diagnostic = "EDITOR-WORKSPACE-BIND-REJECTED";
        return false;
    }
    connection_->status = ConnectionStatus::Bound;
    connection_->diagnostic = nullptr;
    return true;
}

EditorProjectSourceHost::ConnectionStatus EditorProjectSourceHost::connectionStatus() const noexcept
{
    return connection_ ? connection_->status : ConnectionStatus::Unconfigured;
}

const char* EditorProjectSourceHost::diagnostic() const noexcept
{
    return connection_ ? connection_->diagnostic : nullptr;
}

bool EditorProjectSourceHost::remoteWritable() const noexcept
{
    return remote_ && runtime_
        && runtime_->fileService().saveCapability(remoteMount_).observedBaselineReplace;
}

bool EditorProjectSourceHost::bind(
    EditorShell& shell, const std::filesystem::path& root, bool allowWrites)
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
    native.allowObservedBaselineReplace = allowWrites;
    auto backend = core::makeShared<core::io::NativeDirectoryFileBackend>(native);
    const auto capability = backend->capability();
    if (!capability.completesThroughCompletionQueue || !capability.supportsEnumeration
        || !capability.supportsContentVersion
        || (allowWrites && !backend->saveCapability().observedBaselineReplace))
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
