#include "editor/io/editor_cold_host_read.h"

#include "core/io/file_read_contract.h"
#include "core/io/file_service.h"
#include "core/io/filesystem_path.h"
#include "core/io/thread_pool_file_backend.h"
#include "core/runtime/work_service.h"

#include <algorithm>
#include <new>

namespace arp::editor {

core::BinaryFileReadResult EditorColdHostRead::readBinaryBytes(
    const std::filesystem::path& path, std::uint64_t maxBytes)
{
    core::io::FileReadRequest request;
    request.contractVersion = 1;
    request.pathScope = core::io::FilePathScope::HostFilesystem;
    request.path = core::FilePath::utf8(path);
    // FileRead/1 needs positive transport admission, even for a declared empty
    // file. Check the exact logical limit before returning any acquired bytes.
    request.maxBytes = std::max<std::uint64_t>(1, maxBytes);
    auto bytes = read(request);
    core::BinaryFileReadResult result;
    result.success = bytes.status == core::io::FileIoStatus::Success;
    if (!result.success) {
        result.diagnostics = bytes.diagnosticId ? bytes.diagnosticId : bytes.diagnostics;
    } else if (bytes.bytes.size() > maxBytes) {
        result.success = false;
        result.diagnostics = "EDITOR-COLD-READ-LOGICAL-BYTE-BUDGET";
    } else {
        result.bytes = std::move(bytes.bytes);
    }
    return result;
}

core::TextFileReadResult EditorColdHostRead::readTextBytes(
    const std::filesystem::path& path, std::uint64_t maxBytes)
{
    auto bytes = readBinaryBytes(path, maxBytes);
    core::TextFileReadResult result;
    result.success = bytes.success;
    result.diagnostics = std::move(bytes.diagnostics);
    if (result.success && !bytes.bytes.empty())
        result.text.assign(reinterpret_cast<const char*>(bytes.bytes.data()), bytes.bytes.size());
    return result;
}

core::io::FileReadResult EditorColdHostRead::read(const core::io::FileReadRequest& request) noexcept
{
    using namespace core::io;
    using namespace core::runtime;
    FileReadResult failed;
    failed.backendKind = FileBackendKind::ThreadPoolFallbackFileBackend;
    failed.status = FileIoStatus::InvalidRequest;
    failed.diagnosticId = "EDITOR-COLD-READ-HOST-PROFILE";
    if (request.contractVersion != 1 || request.pathScope != FilePathScope::HostFilesystem
        || request.mount)
        return failed;
    const auto validation = FileReadContract::validate(request);
    if (validation.status != FileIoStatus::Success) {
        failed.status = validation.status;
        failed.diagnosticId = validation.diagnostic;
        return failed;
    }
    try {
        // One request and one completion; no worker threads or wait/poll loop.
        CompletionQueue completions({1});
        WorkService work({&completions, 0, 1, true, true});
        ThreadPoolFallbackFileBackend backend(work);
        FileService files(completions, backend);

        struct Delivery {
            FileReadResult result;
            unsigned count = 0;
        } delivery;

        const auto complete = [](void* context, FileReadResult result) noexcept {
            auto& state = *static_cast<Delivery*>(context);
            state.result = std::move(result);
            ++state.count;
        };
        const auto accepted = files.read(request, {complete, &delivery});
        if (accepted == FileIoStatus::Accepted)
            completions.dispatch(1);
        files.shutdown();
        work.shutdown(WorkShutdownPolicy::Drain);
        if (completions.pending())
            completions.dispatch(1);
        const bool retired = !completions.pending() && !completions.reserved();
        completions.shutdown();
        if (accepted != FileIoStatus::Accepted) {
            failed.status = accepted;
            failed.diagnosticId = "EDITOR-COLD-READ-ADMISSION";
            return failed;
        }
        if (delivery.count != 1 || !retired) {
            failed.status = FileIoStatus::BackendUnavailable;
            failed.diagnosticId = "EDITOR-COLD-READ-COMPLETION";
            return failed;
        }
        return std::move(delivery.result);
    } catch (const std::bad_alloc&) {
        failed.status = FileIoStatus::BudgetExceeded;
        failed.diagnosticId = "EDITOR-COLD-READ-ALLOCATION";
    } catch (...) {
        failed.status = FileIoStatus::BackendUnavailable;
        failed.diagnosticId = "EDITOR-COLD-READ-PREPARATION";
    }
    return failed;
}

} // namespace arp::editor
