#include "editor/cli/cli_source.h"

#include "core/io/filesystem_path.h"
#include "editor/io/editor_cold_host_read.h"

#include <exception>

namespace arp::editor {

core::io::FileReadResult EditorCliSource::read(
    const std::filesystem::path& path, std::uint64_t maxBytes) noexcept
{
    using namespace core::io;
    FileReadResult failed;
    failed.status = FileIoStatus::InvalidRequest;
    failed.diagnosticId = "EDITOR-CLI-SOURCE-PROFILE";
    if (path.empty() || !maxBytes || maxBytes > maxSourceBytes)
        return failed;
    try {
        FileReadRequest request;
        request.contractVersion = 1;
        request.pathScope = FilePathScope::HostFilesystem;
        request.path = core::FilePath::utf8(path);
        request.maxBytes = maxBytes;
        return EditorColdHostRead::read(request);
    } catch (const std::bad_alloc&) {
        failed.status = FileIoStatus::BudgetExceeded;
        failed.diagnosticId = "EDITOR-CLI-SOURCE-ALLOCATION";
    } catch (const std::exception&) {
        failed.status = FileIoStatus::BackendUnavailable;
        failed.diagnosticId = "EDITOR-CLI-SOURCE-PREPARATION";
    }
    return failed;
}

} // namespace arp::editor
