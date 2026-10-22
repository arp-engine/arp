#include "core/io/atomic_file_service.h"

#include "core/io/file_io.h"
#include "core/ownership/monotonic_id_sequence.h"

#include <cstdio>
#include <filesystem>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace arp::core::io {
namespace {
MonotonicIdSequence nextTemporary;

bool atomicReplace(
    const pmr::String& path, const pmr::Vector<std::uint8_t>& bytes, AtomicFileResult& result)
{
    const std::filesystem::path destination(path.c_str());
    char suffix[96];
#ifdef _WIN32
    const auto process = GetCurrentProcessId();
#else
    const auto process = getpid();
#endif
    const auto serial = nextTemporary.tryAcquire();
    if (!serial) {
        result.diagnostic = "ARP-FILE-TEMP-ID-EXHAUSTED";
        return false;
    }
    std::snprintf(suffix,
        sizeof(suffix),
        ".arp-tmp-%llu-%llu",
        static_cast<unsigned long long>(process),
        static_cast<unsigned long long>(serial));
    auto temporary = destination;
    temporary += suffix;
#ifdef _WIN32
    HANDLE file = CreateFileW(
        temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        result.diagnostic = "ARP-FILE-TEMP-CREATE";
        return false;
    }
    bool success = true;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        const auto remaining = bytes.size() - offset;
        const DWORD amount = remaining > MAXDWORD ? MAXDWORD : static_cast<DWORD>(remaining);
        if (!WriteFile(file, bytes.data() + offset, amount, &written, nullptr) || !written) {
            success = false;
            break;
        }
        offset += written;
    }
    if (success)
        success = FlushFileBuffers(file) != 0;
    if (!CloseHandle(file))
        success = false;
    if (success)
        success = MoveFileExW(temporary.c_str(),
                      destination.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
            != 0;
    if (!success) {
        DeleteFileW(temporary.c_str());
        result.diagnostic = "ARP-FILE-ATOMIC-REPLACE";
        return false;
    }
#else
    int file = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file < 0) {
        result.diagnostic = "ARP-FILE-TEMP-CREATE";
        return false;
    }
    bool success = true;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto written = ::write(file, bytes.data() + offset, bytes.size() - offset);
        if (written <= 0) {
            success = false;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (success)
        success = ::fsync(file) == 0;
    if (::close(file) != 0)
        success = false;
    if (success)
        success = ::rename(temporary.c_str(), destination.c_str()) == 0;
    if (!success) {
        ::unlink(temporary.c_str());
        result.diagnostic = "ARP-FILE-ATOMIC-REPLACE";
        return false;
    }
#endif
    result.committed = true;
    return true;
}
} // namespace

AtomicFileResult replaceFileAtomically(
    const pmr::String& path, const pmr::Vector<std::uint8_t>& bytes)
{
    AtomicFileResult result;
    result.success = atomicReplace(path, bytes, result);
    return result;
}

AtomicFileService::AtomicFileService(std::uint32_t capacity, std::size_t maxBytes)
    : maxBytes_(maxBytes), requests_(capacity), completions_({capacity}),
      worker_({&completions_, 1, capacity, false})
{}

AtomicFileService::~AtomicFileService()
{
    worker_.shutdown(runtime::WorkShutdownPolicy::Drain);
    poll();
}

PoolHandle AtomicFileService::read(pmr::String path, FileContentValidator validator)
{
    return submit(AtomicFileOperation::Read, std::move(path), {}, validator);
}

PoolHandle AtomicFileService::replace(
    pmr::String path, pmr::Vector<std::uint8_t> bytes, FileContentValidator validator)
{
    return submit(AtomicFileOperation::Replace, std::move(path), std::move(bytes), validator);
}

PoolHandle AtomicFileService::submit(AtomicFileOperation operation,
    pmr::String path,
    pmr::Vector<std::uint8_t> bytes,
    FileContentValidator validator)
{
    diagnostic_ = nullptr;
    if (path.empty() || path.find('\0') != pmr::String::npos || !maxBytes_
        || bytes.size() > maxBytes_ || !worker_.valid()) {
        diagnostic_ = "ARP-FILE-REQUEST-CONFIG";
        return {};
    }
    Request request;
    request.operation = operation;
    request.path = std::move(path);
    request.maxBytes = maxBytes_;
    request.validator = validator;
    request.result.bytes = std::move(bytes);
    auto handle = requests_.emplace(std::move(request));
    if (!handle) {
        diagnostic_ = "ARP-FILE-REQUEST-CAPACITY";
        return {};
    }
    auto* pending = requests_.get(handle);
    auto submission = worker_.submit({execute, pending, complete, pending, {}});
    if (!submission.accepted()) {
        requests_.erase(handle);
        diagnostic_ = "ARP-FILE-WORK-REJECTED";
        return {};
    }
    return handle;
}

runtime::WorkStatus AtomicFileService::execute(void* data)
{
    auto& request = *static_cast<Request*>(data);
    auto& result = request.result;
    try {
        std::error_code error;
        const std::filesystem::path path(request.path.c_str());
        const bool exists = std::filesystem::exists(path, error);
        if (error) {
            result.diagnostic = "ARP-FILE-STAT";
            return runtime::WorkStatus::Failed;
        }
        if (request.operation == AtomicFileOperation::Read) {
            if (!exists) {
                result.missing = true;
                result.diagnostic = "ARP-FILE-MISSING";
                return runtime::WorkStatus::Failed;
            }
            auto read = readBinaryFile(path, request.maxBytes);
            if (!read.success) {
                result.diagnostic = read.diagnostics;
                return runtime::WorkStatus::Failed;
            }
            result.bytes = std::move(read.bytes);
            result.success =
                !request.validator || request.validator(result.bytes, result.diagnostic);
        } else {
            if (request.validator && !request.validator(result.bytes, result.diagnostic))
                return runtime::WorkStatus::Failed;
            if (exists && request.validator) {
                auto previous = readBinaryFile(path, request.maxBytes);
                if (!previous.success) {
                    result.diagnostic = previous.diagnostics;
                    return runtime::WorkStatus::Failed;
                }
                if (!request.validator(previous.bytes, result.diagnostic))
                    return runtime::WorkStatus::Failed;
            }
            result.success = atomicReplace(request.path, result.bytes, result);
        }
        return result.success ? runtime::WorkStatus::Completed : runtime::WorkStatus::Failed;
    } catch (...) {
        result.diagnostic = "ARP-FILE-WORK-EXCEPTION";
        return runtime::WorkStatus::Failed;
    }
}

void AtomicFileService::complete(void* data, runtime::WorkResult result)
{
    auto& request = *static_cast<Request*>(data);
    if (result.status == runtime::WorkStatus::Cancelled) {
        request.result.success = false;
        request.result.diagnostic = "ARP-FILE-CANCELLED";
    }
    request.complete = true;
}

void AtomicFileService::poll()
{
    if (completions_.capacity())
        completions_.dispatch(completions_.capacity());
}

bool AtomicFileService::take(PoolHandle handle, AtomicFileResult& result)
{
    poll();
    auto* request = requests_.get(handle);
    if (!request) {
        diagnostic_ = "ARP-FILE-REQUEST-STALE";
        return false;
    }
    if (!request->complete)
        return false;
    result = std::move(request->result);
    requests_.erase(handle);
    return true;
}
} // namespace arp::core::io
