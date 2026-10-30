#include "core/io/blocking_file_reader.h"

#include "core/io/file_read_contract.h"

#include <filesystem>
#include <fstream>
#include <limits>

namespace arp::core::io {

namespace {

core::pmr::String diagnostic(const char* text)
{
    return core::pmr::String(text, &core::defaultCpuMemoryResource());
}

FileReadResult failure(FileIoStatus status, const char* text)
{
    FileReadResult result;
    result.backendKind = FileBackendKind::ThreadPoolFallbackFileBackend;
    result.status = status;
    result.diagnostics = diagnostic(text);
    return result;
}

FileReadResult validate(const FileReadRequest& request)
{
    const auto contract = FileReadContract::validate(request);
    if (contract.diagnostic)
        return failure(contract.status, contract.diagnostic);
    if (request.expectedVersion.kind != FileVersionKind::None)
        return failure(FileIoStatus::Unsupported, "IO-READ-HOST-VERSION-UNSUPPORTED");
    if (request.pathScope != FilePathScope::HostFilesystem) {
        return failure(FileIoStatus::InvalidPathScope,
            "blocking file reader requires HostFilesystem path scope");
    }
    if (request.path.empty()) {
        return failure(
            FileIoStatus::InvalidRequest, "blocking file reader requires non-empty path");
    }
    if (request.range.mode == FileReadRangeMode::ExactRange && request.range.size == 0) {
        return failure(
            FileIoStatus::InvalidRange, "exact range file read requires size greater than zero");
    }
    FileReadResult result;
    result.backendKind = FileBackendKind::ThreadPoolFallbackFileBackend;
    return result;
}

} // namespace

FileReadResult BlockingFileReader::read(const FileReadRequest& request) const
{
    const auto validation = validate(request);
    if (!validation.diagnostics.empty()) {
        return validation;
    }

    const auto path = std::filesystem::u8path(request.path.begin(), request.path.end());
    std::error_code fsError;
    const auto fileSize = std::filesystem::file_size(path, fsError);
    if (fsError) {
        return failure(FileIoStatus::MissingFile, "file stat failed or file is missing");
    }

    const auto range = FileReadContract::resolveRange(request, fileSize);
    if (range.diagnostic)
        return failure(range.status, range.diagnostic);
    const auto offset = range.offset;
    const auto size = range.size;

    if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return failure(FileIoStatus::InvalidRange, "file read size exceeds addressable memory");
    }

    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())
        || size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
        return failure(FileIoStatus::InvalidRange, "IO-READ-STREAM-RANGE");

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return failure(FileIoStatus::PermissionDenied, "file open failed");
    }
    if (offset != 0) {
        file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!file) {
            return failure(FileIoStatus::InvalidRange, "file seek failed");
        }
    }

    FileReadResult result;
    result.backendKind = FileBackendKind::ThreadPoolFallbackFileBackend;
    result.status = FileIoStatus::Success;
    result.bytes.resize(static_cast<std::size_t>(size));
    if (size != 0) {
        file.read(reinterpret_cast<char*>(result.bytes.data()),
            static_cast<std::streamsize>(result.bytes.size()));
        if (static_cast<std::size_t>(file.gcount()) != result.bytes.size()) {
            if (request.partialPolicy == FilePartialReadPolicy::DenyPartial) {
                return failure(FileIoStatus::PartialReadDenied,
                    "file read returned fewer bytes than requested");
            }
            result.bytes.resize(static_cast<std::size_t>(file.gcount()));
        }
    }
    return result;
}

FileReadResult BlockingFileReader::validateRead(const FileReadRequest& request) const
{
    return validate(request);
}

} // namespace arp::core::io
