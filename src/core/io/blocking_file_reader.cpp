#include "core/io/blocking_file_reader.h"

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
    return {};
}

} // namespace

FileReadResult BlockingFileReader::read(const FileReadRequest& request) const
{
    const auto validation = validate(request);
    if (!validation.diagnostics.empty()) {
        return validation;
    }

    const std::filesystem::path path(core::toStdString(request.path));
    std::error_code fsError;
    const auto fileSize = std::filesystem::file_size(path, fsError);
    if (fsError) {
        return failure(FileIoStatus::MissingFile, "file stat failed or file is missing");
    }

    std::uint64_t offset = 0;
    std::uint64_t size = fileSize;
    if (request.range.mode == FileReadRangeMode::ExactRange) {
        offset = request.range.offset;
        size = request.range.size;
        if (offset > fileSize) {
            return failure(FileIoStatus::InvalidRange, "file read offset is beyond file size");
        }
        if (size > fileSize - offset) {
            if (request.partialPolicy == FilePartialReadPolicy::DenyPartial) {
                return failure(
                    FileIoStatus::PartialReadDenied, "file read range exceeds file size");
            }
            size = fileSize - offset;
        }
    }

    if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return failure(FileIoStatus::InvalidRange, "file read size exceeds addressable memory");
    }

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

} // namespace arp::core::io
