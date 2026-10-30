#include "core/io/file_io.h"

#include "core/io/blocking_file_reader.h"
#include "core/io/filesystem_path.h"

namespace arp::core {

namespace {

io::FileReadResult readLocalBytes(const std::filesystem::path& path, std::uint64_t maxBytes)
{
    io::FileReadRequest request;
    request.pathScope = io::FilePathScope::HostFilesystem;
    request.path = FilePath::utf8(path);
    request.maxBytes = maxBytes;
    return io::BlockingFileReader().read(request);
}

// Keep the public synchronous helpers' diagnostic categories while the
// shared reader supplies structured statuses to asynchronous consumers.
pmr::String legacyDiagnostic(const std::filesystem::path& path, io::FileIoStatus status, bool text)
{
    const char* prefix;
    switch (status) {
    case io::FileIoStatus::MissingFile:
    case io::FileIoStatus::PermissionDenied:
        prefix = text ? "failed to open text file: " : "failed to open binary file: ";
        break;
    case io::FileIoStatus::BudgetExceeded:
        prefix = text ? "text file exceeds byte budget: " : "binary file exceeds byte budget: ";
        break;
    case io::FileIoStatus::InvalidRange:
        prefix = text ? "failed to size text file: " : "failed to size binary file: ";
        break;
    default:
        prefix = text ? "failed to read text file: " : "failed to read binary file: ";
        break;
    }
    pmr::String result(prefix, &defaultCpuMemoryResource());
    result += FilePath::utf8(path);
    return result;
}

} // namespace

TextFileReadResult readTextFile(const std::filesystem::path& path, std::uint64_t maxBytes)
{
    TextFileReadResult result;
    auto read = readLocalBytes(path, maxBytes);
    result.success = read.status == io::FileIoStatus::Success;
    if (!result.success)
        result.diagnostics = legacyDiagnostic(path, read.status, true);
    // Preserve the historical byte-exact text API, including BOM/newlines and
    // embedded NUL. Explicit UTF-8 decoding is a separate policy adapter.
    if (result.success && !read.bytes.empty())
        result.text.assign(reinterpret_cast<const char*>(read.bytes.data()), read.bytes.size());
    return result;
}

BinaryFileReadResult readBinaryFile(const std::filesystem::path& path, std::uint64_t maxBytes)
{
    BinaryFileReadResult result;
    auto read = readLocalBytes(path, maxBytes);
    result.success = read.status == io::FileIoStatus::Success;
    result.bytes = std::move(read.bytes);
    if (!result.success)
        result.diagnostics = legacyDiagnostic(path, read.status, false);
    return result;
}

} // namespace arp::core
