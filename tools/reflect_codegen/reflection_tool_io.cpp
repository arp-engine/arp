#include "tools/reflect_codegen/reflection_tool_io.h"

#include "core/diagnostics/diagnostic_error.h"
#include "core/io/filesystem_path.h"

#include <algorithm>

namespace arp::tools::reflect_codegen {

bool ReflectionIoLimits::valid() const noexcept
{
    return maxHeaderBytes && maxHeaderBytes <= headerBytes && maxInputBytes
        && maxInputBytes <= inputBytes && maxOutputBytes && maxOutputBytes <= outputBytes
        && maxHeaders && maxHeaders <= headerCount;
}

ReflectionToolIo::ReflectionToolIo(
    const ReflectionIoLimits& limits, bool http, core::StringView baseUrl)
    : limits_(limits), http_(http)
{
    if (!limits_.valid() || (http_ ? baseUrl.empty() : !baseUrl.empty()))
        throw core::DiagnosticError("REFLECT-IO-OPTIONS");
    files_ = core::makeUnique<io::ColdToolFiles>(limits_.maxHeaderBytes,
        http_ ? 1 : 0,
        http_ ? io::ColdToolFileScope::MountedResources
              : io::ColdToolFileScope::NativeHostFilesystem);
    if (http_) {
        mount_ = files_->mountHttp("reflection-headers", baseUrl);
        if (!mount_)
            throw core::DiagnosticError("REFLECT-IO-HTTP-MOUNT");
    }
}

core::io::FileTextReadResult ReflectionToolIo::readHeader(const std::filesystem::path& path)
{
    using namespace core::io;
    const auto failure = [](FileIoStatus status, const char* id) {
        FileTextReadResult failed;
        failed.source.status = status;
        failed.source.diagnosticId = id;
        return failed;
    };
    if (reads_ == limits_.maxHeaders)
        return failure(FileIoStatus::BudgetExceeded, "REFLECT-IO-HEADER-COUNT");
    ++reads_;
    FileTextReadRequest request;
    request.file.contractVersion = 1;
    request.file.pathScope = http_ ? FilePathScope::MountedResource : FilePathScope::HostFilesystem;
    request.file.mount = mount_;
    request.file.path = core::FilePath::utf8(path);
    request.file.maxBytes = std::min(limits_.maxHeaderBytes, limits_.maxInputBytes - inputBytes_);
    request.text.maxOutputBytes = request.file.maxBytes;
    request.text.bom = core::TextBomPolicy::Preserve;
    request.text.nul = core::TextNulPolicy::Reject;
    auto result = files_->readText(request);
    if (result.status() != FileIoStatus::Success)
        return result;
    inputBytes_ += result.source.bytes.size();
#if defined(_WIN32)
    if (result.decoded.text.find('\x1a') != core::pmr::String::npos)
        return failure(FileIoStatus::InvalidRequest, "REFLECT-IO-TEXT-EOF-UNSUPPORTED");
#endif
    return result;
}
} // namespace arp::tools::reflect_codegen
