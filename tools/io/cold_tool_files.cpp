#include "tools/io/cold_tool_files.h"

#include "core/diagnostics/diagnostic_error.h"
#include "core/io/file_read_contract.h"
#include "core/io/filesystem_path.h"
#include "core/io/http_file_backend.h"
#include "core/io/native_directory_file_backend.h"

#include <chrono>
#include <thread>

namespace arp::tools::io {
namespace {
using namespace core::io;

void fail(FileReadResult& result, FileIoStatus status, const char* id)
{
    result.status = status;
    result.diagnosticId = id;
    result.bytes.clear();
}

void fail(FileTextReadResult& result, FileIoStatus status, const char* id)
{
    fail(result.source, status, id);
    result.decoded = {};
}

void fail(FileSaveResult& result, FileIoStatus status, const char* id)
{
    result.fail(status, id);
}

void timedOut(FileReadResult& result, bool)
{
    fail(result, FileIoStatus::Timeout, "TOOL-FILE-DEADLINE");
}

void timedOut(FileTextReadResult& result, bool)
{
    fail(result, FileIoStatus::Timeout, "TOOL-FILE-DEADLINE");
}

void timedOut(FileSaveResult& result, bool delivered)
{
    if (!delivered) {
        result.commitState = FileCommitState::OutcomeUnknown;
        result.fail(FileIoStatus::OutcomeUnknown, "TOOL-FILE-COMMIT-UNKNOWN");
    }
    result.deliveryStatus = FileIoStatus::Timeout;
}

template <class Result> struct Delivery {
    Result result;
    bool done = false;
    using Retained = core::SharedPtr<Delivery>;

    static void complete(void* context, Result result) noexcept
    {
        core::UniquePtr<Retained> owner{static_cast<Retained*>(context),
            core::CpuAllocatedDeleter<Retained>{
                context, &core::destroyCpuAllocatedUnique<Retained>}};
        (**owner).result = std::move(result);
        (**owner).done = true;
    }
};
} // namespace

bool ColdToolFiles::validBudget(std::uint64_t bytes) noexcept
{
    return bytes && bytes <= (SIZE_MAX - ColdToolFileProfile::httpControlBytes) / 2;
}

ColdToolFiles::ColdToolFiles(
    std::uint64_t maxFileBytes, std::uint32_t maxMounts, ColdToolFileScope scope)
    : bytes_(maxFileBytes), scope_(scope)
{
    const bool mounted = scope_ == ColdToolFileScope::MountedResources;
    if (!validBudget(bytes_) || (mounted && !maxMounts)
        || (!mounted && (scope_ != ColdToolFileScope::NativeHostFilesystem || maxMounts)))
        throw core::DiagnosticError("TOOL-FILE-PROFILE");
#if defined(__EMSCRIPTEN__)
    if (!mounted)
        throw core::DiagnosticError("TOOL-FILE-HOST-NATIVE-ONLY");
#endif
    core::runtime::AsyncRuntimeDesc desc;
    desc.enableWork = desc.enableThreadPoolFileBackend = !mounted;
    desc.completionQueueCapacity = desc.completionBudget = 2;
    if (mounted) {
        mounts_ = core::makeShared<core::io::MountedFileBackend>(core::io::MountedFileBackendDesc{
            maxMounts, 1, 2 * bytes_, 32, ColdToolFileProfile::pathBytes});
        desc.fileBackend = mounts_;
    } else {
        desc.workerCount = desc.workQueueCapacity = 1;
    }
    runtime_ = core::makeUnique<core::runtime::AsyncRuntime>(std::move(desc));
    if (!runtime_->valid())
        throw core::DiagnosticError("TOOL-FILE-RUNTIME");
}

ColdToolFiles::~ColdToolFiles()
{
    if (runtime_)
        runtime_->shutdown();
}

core::PoolHandle ColdToolFiles::mountDirectory(
    core::StringView name, const std::filesystem::path& directory, bool writable)
{
    if (!mounts_)
        throw core::DiagnosticError("TOOL-FILE-MOUNTS-UNAVAILABLE");
    if (directory.empty())
        throw core::DiagnosticError("TOOL-FILE-EMPTY-DIRECTORY");
    core::io::NativeDirectoryFileBackendDesc desc;
    desc.rootPath = core::FilePath::utf8(directory);
    desc.maxInFlight = 1;
    desc.maxBytesInFlight =
        (writable ? 2 * bytes_ : bytes_) + ColdToolFileProfile::saveScratchBytes;
    desc.maxPathBytes = ColdToolFileProfile::pathBytes;
    desc.ioChunkBytes = ColdToolFileProfile::ioChunkBytes;
    desc.directoryBufferBytes = 4096;
    desc.maxScanEntries = 16;
    desc.maxScanNameBytes = 4096;
    desc.allowObservedBaselineReplace = writable;
    return mounts_->mount(
        name, core::makeShared<core::io::NativeDirectoryFileBackend>(std::move(desc)));
}

core::PoolHandle ColdToolFiles::mountHttp(core::StringView name, core::StringView baseUrl)
{
    if (!mounts_)
        throw core::DiagnosticError("TOOL-FILE-MOUNTS-UNAVAILABLE");
    if (baseUrl.empty() || baseUrl.size() > ColdToolFileProfile::pathBytes)
        throw core::DiagnosticError("TOOL-FILE-HTTP-BASE-BUDGET");
    core::io::HttpFileBackendDesc desc;
    desc.baseUrl = baseUrl;
    desc.maxInFlight = 1;
    desc.maxBytesInFlight = bytes_ + ColdToolFileProfile::httpControlBytes;
    desc.maxPathBytes = ColdToolFileProfile::pathBytes;
    desc.maxHeaderBytes = 8192;
    desc.ioChunkBytes = ColdToolFileProfile::ioChunkBytes;
    desc.timeoutMs = 5000;
    desc.cancellationPollMs = 10;
    desc.maxDnsAddresses = 16;
    return mounts_->mount(name, core::makeShared<core::io::HttpFileBackend>(std::move(desc)));
}

void ColdToolFiles::retire()
{
    runtime_->shutdown();
    if (runtime_->completionQueue().pending() || runtime_->completionQueue().reserved()
        || (mounts_ && mounts_->telemetry().inFlight))
        throw core::DiagnosticError("TOOL-FILE-NOT-RETIRED");
}

template <class Result, class Submit> Result ColdToolFiles::await(Submit&& submit)
{
    Result failure;
#if defined(__EMSCRIPTEN__)
    fail(failure, core::io::FileIoStatus::Unsupported, "TOOL-FILE-NATIVE-WAIT-UNAVAILABLE");
    return failure;
#else
    using Pending = Delivery<Result>;
    auto state = core::makeShared<Pending>();
    auto retained = core::makeUnique<typename Pending::Retained>(state);
    const auto accepted = submit(retained.get(), &Pending::complete);
    if (accepted != core::io::FileIoStatus::Accepted) {
        fail(failure, accepted, "TOOL-FILE-ADMISSION");
        return failure;
    }
    retained.release();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!state->done) {
        if (std::chrono::steady_clock::now() >= deadline) {
            retire();
            timedOut(state->result, state->done);
            return std::move(state->result);
        }
        if (!dispatch()) {
            retire();
            fail(state->result, core::io::FileIoStatus::ServiceClosed, "TOOL-FILE-DISPATCH");
            return std::move(state->result);
        }
        std::this_thread::yield();
    }
    return std::move(state->result);
#endif
}

core::io::FileReadResult ColdToolFiles::read(const core::io::FileReadRequest& request)
{
    core::io::FileReadResult rejected;
    if (request.contractVersion != 1 || request.maxBytes > bytes_
        || request.maxVersionBytes > bytes_) {
        fail(rejected, core::io::FileIoStatus::InvalidRequest, "TOOL-FILE-READ-BUDGET");
        return rejected;
    }
    return await<core::io::FileReadResult>([&](void* context, auto callback) {
        return service().read(request, {callback, context});
    });
}

core::io::FileTextReadResult ColdToolFiles::readText(const core::io::FileTextReadRequest& request)
{
    core::io::FileTextReadResult rejected;
    if (request.file.contractVersion != 1 || request.file.maxBytes > bytes_
        || request.file.maxVersionBytes > bytes_ || request.text.maxOutputBytes > bytes_) {
        fail(rejected, core::io::FileIoStatus::InvalidRequest, "TOOL-FILE-TEXT-BUDGET");
        return rejected;
    }
    return await<core::io::FileTextReadResult>([&](void* context, auto callback) {
        return service().readText(request, {callback, context});
    });
}

core::io::FileSaveResult ColdToolFiles::save(core::io::FileSaveRequest request)
{
    core::io::FileSaveResult rejected;
    if (scope_ == ColdToolFileScope::NativeHostFilesystem) {
        fail(rejected, core::io::FileIoStatus::Unsupported, "TOOL-FILE-HOST-SAVE-UNSUPPORTED");
        return rejected;
    }
    if (request.maxWriteBytes > bytes_ || request.maxBaselineBytes > bytes_) {
        fail(rejected, core::io::FileIoStatus::InvalidRequest, "TOOL-FILE-SAVE-BUDGET");
        return rejected;
    }
    return await<core::io::FileSaveResult>([&](void* context, auto callback) {
        return service().save(std::move(request), {callback, context});
    });
}
} // namespace arp::tools::io
