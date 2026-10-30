#include "core/io/detail/native_directory_access.h"

#include "core/hash/sha256.h"
#include "core/io/file_query_contract.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstring>
#include <limits>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>
#endif

namespace arp::core::io::detail {
namespace {

FileReadResult readFailure(FileIoStatus status, const char* diagnostic)
{
    FileReadResult result;
    result.status = status;
    result.backendKind = FileBackendKind::NativeDirectoryFileBackend;
    result.diagnosticId = diagnostic;
    return result;
}

#if defined(_WIN32)

struct NativeError {
    FileIoStatus status = FileIoStatus::Success;
    const char* diagnostic = nullptr;
};

NativeError error(DWORD code, const char* diagnostic)
{
    const auto status = code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND
        ? FileIoStatus::MissingFile
        : code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION
            || code == ERROR_CANT_ACCESS_FILE
        ? FileIoStatus::PermissionDenied
        : code == ERROR_INSUFFICIENT_BUFFER || code == ERROR_MORE_DATA
        ? FileIoStatus::BudgetExceeded
        : code == ERROR_NOT_SUPPORTED || code == ERROR_INVALID_FUNCTION
        ? FileIoStatus::Unsupported
        : FileIoStatus::BackendUnavailable;
    return {status, diagnostic};
}

class NativeHandle final {
public:
    NativeHandle() = default;

    NativeHandle(HANDLE value, std::atomic_uint64_t& count) : value_(value), count_(&count)
    {
        if (valid())
            ++*count_;
    }

    NativeHandle(const NativeHandle&) = delete;
    NativeHandle& operator=(const NativeHandle&) = delete;

    NativeHandle(NativeHandle&& other) noexcept
        : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)),
          count_(std::exchange(other.count_, nullptr))
    {}

    NativeHandle& operator=(NativeHandle&& other) noexcept
    {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
            count_ = std::exchange(other.count_, nullptr);
        }
        return *this;
    }

    ~NativeHandle() { reset(); }

    void reset() noexcept
    {
        if (valid() && CloseHandle(value_))
            --*count_;
        value_ = INVALID_HANDLE_VALUE;
        count_ = nullptr;
    }

    bool valid() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }

    HANDLE get() const noexcept { return value_; }

private:
    HANDLE value_ = INVALID_HANDLE_VALUE;
    std::atomic_uint64_t* count_ = nullptr;
};

core::pmr::Vector<wchar_t> wide(std::string_view text)
{
    core::pmr::Vector<wchar_t> result{&core::defaultCpuMemoryResource()};
    if (text.empty() || text.size() > INT_MAX)
        return result;
    const auto count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!count)
        return result;
    result.resize(static_cast<std::size_t>(count) + 1);
    if (MultiByteToWideChar(CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            result.data(),
            count)
        != count)
        result.clear();
    return result;
}

NativeError narrow(
    const wchar_t* text, std::size_t count, core::pmr::String& result, std::size_t budget)
{
    if (!count || count > INT_MAX)
        return {FileIoStatus::InvalidRequest, "IO-LOCAL-NAME-ENCODING"};
    const auto bytes = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, text, static_cast<int>(count), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return {FileIoStatus::InvalidRequest, "IO-LOCAL-NAME-ENCODING"};
    if (static_cast<std::size_t>(bytes) > budget)
        return {FileIoStatus::BudgetExceeded, "IO-LOCAL-NAME-BUDGET"};
    result.resize(static_cast<std::size_t>(bytes));
    const auto converted = WideCharToMultiByte(CP_UTF8,
                               WC_ERR_INVALID_CHARS,
                               text,
                               static_cast<int>(count),
                               result.data(),
                               bytes,
                               nullptr,
                               nullptr)
        == bytes;
    return converted ? NativeError{}
                     : NativeError{FileIoStatus::InvalidRequest, "IO-LOCAL-NAME-ENCODING"};
}

FileMetadata metadata(const BY_HANDLE_FILE_INFORMATION& info)
{
    FileMetadata value;
    value.kind = info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ? FileEntryKind::Link
        : info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY            ? FileEntryKind::Directory
                                                                      : FileEntryKind::RegularFile;
    value.sizeBytes = value.kind == FileEntryKind::RegularFile
        ? (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow
        : 0;
    core::Sha256 hash;
    hash.update("ArpWindowsMetadataObservation/1");
    FileVersionEncoding::appendNumber(hash, info.dwVolumeSerialNumber);
    FileVersionEncoding::appendNumber(
        hash, (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow);
    FileVersionEncoding::appendNumber(hash, info.dwFileAttributes);
    FileVersionEncoding::appendNumber(hash, value.sizeBytes);
    FileVersionEncoding::appendNumber(hash,
        (static_cast<std::uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32)
            | info.ftLastWriteTime.dwLowDateTime);
    value.version = FileVersionEncoding::finish(hash, FileVersionKind::MetadataObservation);
    return value;
}

NativeError information(HANDLE handle, BY_HANDLE_FILE_INFORMATION& info)
{
    if (GetFileType(handle) != FILE_TYPE_DISK)
        return {FileIoStatus::Unsupported, "IO-LOCAL-NON-DISK-OBJECT"};
    if (!GetFileInformationByHandle(handle, &info))
        return error(GetLastError(), "IO-LOCAL-HANDLE-INFORMATION");
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        return {FileIoStatus::PermissionDenied, "IO-LOCAL-REPARSE-DENIED"};
    return {};
}

NativeError readAt(HANDLE file, std::uint64_t offset, std::uint8_t* output, std::size_t size)
{
    if (offset > static_cast<std::uint64_t>(INT64_MAX))
        return {FileIoStatus::InvalidRange, "IO-LOCAL-OFFSET"};
    LARGE_INTEGER position;
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN))
        return error(GetLastError(), "IO-LOCAL-SEEK");
    while (size) {
        const auto count = static_cast<DWORD>(std::min<std::size_t>(size, MAXDWORD));
        DWORD received = 0;
        if (!ReadFile(file, output, count, &received, nullptr))
            return error(GetLastError(), "IO-LOCAL-READ");
        if (!received)
            return {FileIoStatus::PartialReadDenied, "IO-LOCAL-TRUNCATED"};
        output += received;
        size -= received;
    }
    return {};
}

// The range, when present, is extracted from the very stream being hashed.
NativeError scan(HANDLE file,
    std::uint64_t size,
    std::uint64_t maxBytes,
    std::size_t chunkBytes,
    const runtime::CancellationToken& cancel,
    FileVersionToken& version,
    std::uint64_t rangeOffset = 0,
    core::pmr::Vector<std::uint8_t>* output = nullptr)
{
    if (size > maxBytes)
        return {FileIoStatus::BudgetExceeded, "IO-LOCAL-VERSION-BUDGET"};
    core::pmr::Vector<std::uint8_t> chunk{&core::defaultCpuMemoryResource()};
    chunk.resize(static_cast<std::size_t>(std::min<std::uint64_t>(size, chunkBytes)));
    core::Sha256 hash;
    for (std::uint64_t offset = 0; offset < size;) {
        if (cancel.cancelled())
            return {FileIoStatus::Cancelled, "IO-LOCAL-CANCELLED"};
        const auto count =
            static_cast<std::size_t>(std::min<std::uint64_t>(chunk.size(), size - offset));
        const auto read = readAt(file, offset, chunk.data(), count);
        if (read.diagnostic)
            return read;
        hash.update({reinterpret_cast<const char*>(chunk.data()), count});
        if (output && !output->empty()) {
            const auto begin = std::max(offset, rangeOffset);
            const auto end = std::min(offset + count, rangeOffset + output->size());
            if (begin < end)
                std::memcpy(output->data() + (begin - rangeOffset),
                    chunk.data() + (begin - offset),
                    static_cast<std::size_t>(end - begin));
        }
        offset += count;
    }
    version = FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256);
    return {};
}

#endif

} // namespace

struct NativeDirectoryAccess::Impl {
    explicit Impl(const NativeDirectoryFileBackendDesc& config)
        : desc(config), scope(FileQueryContract::acquireBackendScope())
    {
#if defined(_WIN32)
        const auto module = GetModuleHandleW(L"ntdll.dll");
        if (module) {
            open = reinterpret_cast<decltype(open)>(GetProcAddress(module, "NtCreateFile"));
            dosError = reinterpret_cast<decltype(dosError)>(
                GetProcAddress(module, "RtlNtStatusToDosError"));
        }
#endif
    }

    const NativeDirectoryFileBackendDesc& desc;
    std::uint64_t scope;
    std::atomic_uint64_t objects{0};
#if defined(_WIN32)
    decltype(&NtCreateFile) open = nullptr;
    ULONG(WINAPI* dosError)(NTSTATUS) = nullptr;
    NativeHandle root;
    bool initialized = false;
    NativeError rootError;

    NativeError ensureRoot()
    {
        if (initialized)
            return rootError;
        const auto name = wide(desc.rootPath);
        if (name.empty()) {
            rootError = {FileIoStatus::InvalidRequest, "IO-LOCAL-ROOT-ENCODING"};
        } else {
            root = NativeHandle(CreateFileW(name.data(),
                                    FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                    nullptr),
                objects);
            if (!root.valid()) {
                rootError = error(GetLastError(), "IO-LOCAL-ROOT-OPEN");
            } else {
                BY_HANDLE_FILE_INFORMATION info{};
                rootError = information(root.get(), info);
                if (!rootError.diagnostic && !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    rootError = {FileIoStatus::InvalidRequest, "IO-LOCAL-ROOT-NOT-DIRECTORY"};
            }
        }
        initialized = true;
        return rootError;
    }

    NativeHandle child(
        HANDLE parent, std::string_view segment, bool directory, NativeError& failure)
    {
        auto name = wide(segment);
        if (name.empty() || (name.size() - 1) * sizeof(wchar_t) > USHRT_MAX) {
            failure = {FileIoStatus::InvalidRequest, "IO-LOCAL-SEGMENT-ENCODING"};
            return {};
        }
        UNICODE_STRING unicode{};
        unicode.Buffer = name.data();
        unicode.Length = static_cast<USHORT>((name.size() - 1) * sizeof(wchar_t));
        unicode.MaximumLength = unicode.Length;
        OBJECT_ATTRIBUTES attributes{};
        attributes.Length = sizeof(attributes);
        attributes.RootDirectory = parent;
        attributes.ObjectName = &unicode;
        IO_STATUS_BLOCK result{};
        HANDLE opened = nullptr;
        const auto status = open(&opened,
            FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            &attributes,
            &result,
            nullptr,
            FILE_ATTRIBUTE_NORMAL,
            directory ? FILE_SHARE_READ | FILE_SHARE_WRITE : FILE_SHARE_READ,
            FILE_OPEN,
            FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT
                | (directory ? FILE_DIRECTORY_FILE : 0),
            nullptr,
            0);
        if (status < 0) {
            failure = error(dosError(status), "IO-LOCAL-RELATIVE-OPEN");
            return {};
        }
        NativeHandle handle(opened, objects);
        BY_HANDLE_FILE_INFORMATION info{};
        failure = information(handle.get(), info);
        if (failure.diagnostic)
            return {};
        return handle;
    }

    NativeHandle resolve(std::string_view path, bool directory, NativeError& failure)
    {
        failure = ensureRoot();
        if (failure.diagnostic)
            return {};
        NativeHandle current;
        auto parent = root.get();
        if (path.empty()) {
            HANDLE duplicate = nullptr;
            // There is exactly one IO worker. Enumeration always restarts and
            // finishes before the next operation, so a duplicated root file
            // object's shared directory cursor cannot overlap another scan.
            if (!DuplicateHandle(GetCurrentProcess(),
                    parent,
                    GetCurrentProcess(),
                    &duplicate,
                    0,
                    FALSE,
                    DUPLICATE_SAME_ACCESS)) {
                failure = error(GetLastError(), "IO-LOCAL-ROOT-DUPLICATE");
                return {};
            }
            return NativeHandle(duplicate, objects);
        }
        std::size_t begin = 0;
        for (;;) {
            const auto separator = path.find('/', begin);
            const bool last = separator == std::string_view::npos;
            const auto segment = path.substr(begin, last ? path.size() - begin : separator - begin);
            auto next = child(parent, segment, !last || directory, failure);
            if (failure.diagnostic)
                return {};
            current = std::move(next);
            parent = current.get();
            if (last)
                return current;
            begin = separator + 1;
        }
    }
#endif
};

NativeDirectoryAccess::NativeDirectoryAccess(const NativeDirectoryFileBackendDesc& desc)
    : impl_(core::makeUnique<Impl>(desc))
{}

NativeDirectoryAccess::~NativeDirectoryAccess() = default;

bool NativeDirectoryAccess::supported() const noexcept
{
#if defined(_WIN32)
    return impl_->open && impl_->dosError && impl_->scope;
#else
    return false;
#endif
}

void NativeDirectoryAccess::shutdown() noexcept
{
#if defined(_WIN32)
    impl_->root.reset();
#endif
}

std::uint64_t NativeDirectoryAccess::openObjectCount() const noexcept
{
    return impl_->objects.load();
}

FileReadResult NativeDirectoryAccess::read(const FileReadRequest& request)
{
#if defined(_WIN32)
    NativeError failure;
    auto file = impl_->resolve(request.path, false, failure);
    if (failure.diagnostic)
        return readFailure(failure.status, failure.diagnostic);
    BY_HANDLE_FILE_INFORMATION info{};
    failure = information(file.get(), info);
    if (failure.diagnostic)
        return readFailure(failure.status, failure.diagnostic);
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        return readFailure(FileIoStatus::InvalidRequest, "IO-LOCAL-READ-DIRECTORY");
    const auto size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    const auto range = FileReadContract::resolveRange(request, size);
    if (range.diagnostic)
        return readFailure(range.status, range.diagnostic);
    if (range.size > std::numeric_limits<std::size_t>::max())
        return readFailure(FileIoStatus::BudgetExceeded, "IO-LOCAL-ADDRESS-BUDGET");
    FileReadResult result;
    result.bytes.resize(static_cast<std::size_t>(range.size));
    if (request.expectedVersion.kind == FileVersionKind::ContentSha256) {
        failure = scan(file.get(),
            size,
            request.maxVersionBytes,
            impl_->desc.ioChunkBytes,
            request.cancellation,
            result.version,
            range.offset,
            &result.bytes);
        if (!failure.diagnostic && result.version != request.expectedVersion)
            failure = {FileIoStatus::VersionMismatch, "IO-LOCAL-CONTENT-VERSION-MISMATCH"};
    } else {
        failure = readAt(file.get(), range.offset, result.bytes.data(), result.bytes.size());
    }
    if (failure.diagnostic)
        return readFailure(failure.status, failure.diagnostic);
    result.backendKind = FileBackendKind::NativeDirectoryFileBackend;
    return result;
#else
    (void)request;
    return readFailure(FileIoStatus::Unsupported, "IO-LOCAL-HOST-UNSUPPORTED");
#endif
}

FileQueryResult NativeDirectoryAccess::query(const FileQueryRequest& request)
{
    FileQueryResult result;
    result.backendKind = FileBackendKind::NativeDirectoryFileBackend;
#if defined(_WIN32)
    NativeError failure;
    auto file =
        impl_->resolve(request.path, request.operation == FileQueryOperation::Enumerate, failure);
    if (failure.diagnostic) {
        result.fail(failure.status, failure.diagnostic);
        return result;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    failure = information(file.get(), info);
    if (failure.diagnostic) {
        result.fail(failure.status, failure.diagnostic);
        return result;
    }
    result.metadata = metadata(info);
    if (request.operation == FileQueryOperation::Stat) {
        if (request.versionPolicy == FileVersionPolicy::ContentSha256) {
            if (result.metadata.kind != FileEntryKind::RegularFile) {
                result.fail(FileIoStatus::Unsupported, "IO-LOCAL-CONTENT-VERSION-REQUIRES-FILE");
                return result;
            }
            failure = scan(file.get(),
                result.metadata.sizeBytes,
                request.maxVersionBytes,
                impl_->desc.ioChunkBytes,
                request.cancellation,
                result.metadata.version);
            if (failure.diagnostic)
                result.fail(failure.status, failure.diagnostic);
        }
        if (result.status == FileIoStatus::Success
            && FileQueryContract::resultBytes(result) > request.maxResultBytes)
            result.fail(FileIoStatus::BudgetExceeded, "IO-LOCAL-METADATA-BUDGET");
        return result;
    }
    if (impl_->desc.directoryBufferBytes > MAXDWORD
        || impl_->desc.directoryBufferBytes % alignof(FILE_ID_BOTH_DIR_INFO)
        || impl_->desc.directoryBufferBytes < sizeof(FILE_ID_BOTH_DIR_INFO)) {
        result.fail(FileIoStatus::BudgetExceeded, "IO-LOCAL-DIRECTORY-BUFFER");
        return result;
    }
    static_assert(alignof(std::uint64_t) >= alignof(FILE_ID_BOTH_DIR_INFO));
    core::pmr::Vector<std::uint64_t> storage{&core::defaultCpuMemoryResource()};
    storage.resize(impl_->desc.directoryBufferBytes / sizeof(std::uint64_t));
    auto* buffer = reinterpret_cast<std::uint8_t*>(storage.data());
    const auto bufferBytes = storage.size() * sizeof(std::uint64_t);
    core::pmr::Vector<FileDirectoryEntry> entries{&core::defaultCpuMemoryResource()};
    entries.reserve(request.maxScanEntries);
    std::uint64_t nameBytes = 0;
    bool restart = true;
    for (;;) {
        if (request.cancellation.cancelled()) {
            result.fail(FileIoStatus::Cancelled, "IO-LOCAL-CANCELLED");
            return result;
        }
        if (!GetFileInformationByHandleEx(file.get(),
                restart ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo,
                buffer,
                static_cast<DWORD>(bufferBytes))) {
            const auto code = GetLastError();
            if (code == ERROR_NO_MORE_FILES)
                break;
            failure = error(code, "IO-LOCAL-ENUMERATE");
            result.fail(failure.status, failure.diagnostic);
            return result;
        }
        restart = false;
        std::size_t offset = 0;
        for (;;) {
            if (offset > bufferBytes - offsetof(FILE_ID_BOTH_DIR_INFO, FileName)) {
                result.fail(FileIoStatus::BackendUnavailable, "IO-LOCAL-DIRECTORY-RECORD");
                return result;
            }
            const auto* entry = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(buffer + offset);
            if (entry->FileNameLength % sizeof(wchar_t)
                || entry->FileNameLength
                    > bufferBytes - offset - offsetof(FILE_ID_BOTH_DIR_INFO, FileName)) {
                result.fail(FileIoStatus::BackendUnavailable, "IO-LOCAL-DIRECTORY-NAME");
                return result;
            }
            const std::wstring_view name(entry->FileName, entry->FileNameLength / sizeof(wchar_t));
            if (name != L"." && name != L"..") {
                if (entries.size() >= request.maxScanEntries) {
                    result.fail(FileIoStatus::BudgetExceeded, "IO-LOCAL-SCAN-ENTRY-BUDGET");
                    return result;
                }
                FileDirectoryEntry value;
                const auto conversion =
                    narrow(name.data(), name.size(), value.name, impl_->desc.maxPathBytes);
                if (conversion.diagnostic) {
                    result.fail(conversion.status, conversion.diagnostic);
                    return result;
                }
                if (value.name.size() > request.maxScanNameBytes - nameBytes) {
                    result.fail(FileIoStatus::BudgetExceeded, "IO-LOCAL-SCAN-NAME-BUDGET");
                    return result;
                }
                nameBytes += value.name.size();
                value.metadata.kind = entry->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT
                    ? FileEntryKind::Link
                    : entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY ? FileEntryKind::Directory
                                                                       : FileEntryKind::RegularFile;
                value.metadata.sizeBytes = value.metadata.kind == FileEntryKind::RegularFile
                    ? static_cast<std::uint64_t>(entry->EndOfFile.QuadPart)
                    : 0;
                core::Sha256 hash;
                hash.update("ArpWindowsDirectoryEntryObservation/1");
                FileVersionEncoding::appendNumber(
                    hash, static_cast<std::uint64_t>(entry->FileId.QuadPart));
                FileVersionEncoding::appendNumber(
                    hash, static_cast<std::uint64_t>(entry->LastWriteTime.QuadPart));
                FileVersionEncoding::appendNumber(
                    hash, static_cast<std::uint64_t>(entry->ChangeTime.QuadPart));
                FileVersionEncoding::appendNumber(hash, entry->FileAttributes);
                value.metadata.version =
                    FileVersionEncoding::finish(hash, FileVersionKind::MetadataObservation);
                entries.push_back(std::move(value));
            }
            if (!entry->NextEntryOffset)
                break;
            if (entry->NextEntryOffset > bufferBytes - offset
                || entry->NextEntryOffset < offsetof(FILE_ID_BOTH_DIR_INFO, FileName)
                || entry->NextEntryOffset % alignof(FILE_ID_BOTH_DIR_INFO)) {
                result.fail(FileIoStatus::BackendUnavailable, "IO-LOCAL-DIRECTORY-OFFSET");
                return result;
            }
            offset += entry->NextEntryOffset;
        }
    }
    BY_HANDLE_FILE_INFORMATION after{};
    failure = information(file.get(), after);
    if (failure.diagnostic
        || after.ftLastWriteTime.dwHighDateTime != info.ftLastWriteTime.dwHighDateTime
        || after.ftLastWriteTime.dwLowDateTime != info.ftLastWriteTime.dwLowDateTime) {
        result.fail(failure.diagnostic ? failure.status : FileIoStatus::VersionMismatch,
            "IO-LOCAL-DIRECTORY-CHANGED-DURING-SCAN");
        return result;
    }
    result = FileQueryContract::page(request, std::move(entries), impl_->scope);
    result.backendKind = FileBackendKind::NativeDirectoryFileBackend;
#else
    (void)request;
    result.fail(FileIoStatus::Unsupported, "IO-LOCAL-HOST-UNSUPPORTED");
#endif
    return result;
}

} // namespace arp::core::io::detail
