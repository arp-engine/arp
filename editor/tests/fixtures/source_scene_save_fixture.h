#pragma once

#include "core/io/file_query_contract.h"
#include "editor/tests/fixtures/source_scene_shell_fixture.h"

namespace arp::editor::testfixtures::sources {
// Shared real-source save operations; these never replace the FileService backend.
inline core::io::FileReadResult read(Host& host, std::string_view relative)
{
    FileReadRequest request;
    request.contractVersion = 1;
    request.mount = host.mount;
    request.pathScope = FilePathScope::MountedResource;
    request.path = "workspace/";
    request.path.append(relative.data(), relative.size());
    request.maxBytes = fileBytes;
    FileQueryRequest query;
    query.pathScope = request.pathScope;
    query.mount = request.mount;
    query.path = request.path;
    query.maxResultBytes = FileQueryContract::resultBytes(FileQueryResult{});
    query.versionPolicy = FileVersionPolicy::ContentSha256;
    query.maxVersionBytes = fileBytes;
    unified_io_fixture::Probe<FileQueryResult> version;
    check(
        host.files.query(query, {decltype(version)::complete, &version}) == FileIoStatus::Accepted);
    unified_io_fixture::wait(host.queue, [&] {
        return version.calls == 1;
    });
    check(version.result.status == FileIoStatus::Success);
    request.expectedVersion = version.result.metadata.version;
    request.maxVersionBytes = fileBytes;
    unified_io_fixture::Probe<FileReadResult> probe;
    check(host.files.read(request, {decltype(probe)::complete, &probe}) == FileIoStatus::Accepted);
    unified_io_fixture::wait(host.queue, [&] {
        return probe.calls == 1;
    });
    return std::move(probe.result);
}

inline std::uint64_t save(EditorShell& shell, const char* path, bool overwrite = false)
{
    SaveSceneCommand command(path);
    command.overwriteExisting = overwrite;
    const auto result = Host::command(shell, command).snapshot;
    check(result.sceneIoPending && !result.sceneSaveReceipt.submitted
        && result.scenePersistenceTaskId != 0);
    return result.scenePersistenceTaskId;
}

inline void rename(EditorShell& shell, const char* name)
{
    SetSceneObjectNameCommand command(name);
    command.objectId = shell.sceneDocument().scene().rootId().value();
    Host::command(shell, command);
}

} // namespace arp::editor::testfixtures::sources
