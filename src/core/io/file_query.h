#pragma once

#include "core/io/file_request.h"
#include "core/io/file_result.h"

namespace arp::core::io {

enum class FileQueryOperation : std::uint8_t { Stat, Enumerate };
enum class FileEntryKind : std::uint8_t { RegularFile, Directory, Link };
enum class FileVersionPolicy : std::uint8_t { None, ContentSha256 };

struct FileMetadata {
    FileEntryKind kind = FileEntryKind::RegularFile;
    std::uint64_t sizeBytes = 0;
    FileVersionToken version;
};

struct FileDirectoryEntry {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    FileMetadata metadata;
};

struct FileDirectoryCursor {
    FileVersionToken snapshot;
    std::uint64_t offset = 0;
};

struct FileQueryRequest {
    std::uint32_t contractVersion = 1;
    FileQueryOperation operation = FileQueryOperation::Stat;
    FilePathScope pathScope = FilePathScope::Unspecified;
    core::PoolHandle mount;
    // Empty path explicitly denotes the mounted root for queries only.
    core::pmr::String path{&core::defaultCpuMemoryResource()};
    runtime::CancellationToken cancellation;
    std::uint64_t maxResultBytes = 0;
    FileVersionPolicy versionPolicy = FileVersionPolicy::None;
    std::uint64_t maxVersionBytes = 0;
    std::uint32_t pageSize = 0;
    std::uint32_t maxScanEntries = 0;
    std::uint64_t maxScanNameBytes = 0;
    FileDirectoryCursor cursor;
};

struct FileQueryResult {
    FileIoStatus status = FileIoStatus::Success;
    FileBackendKind backendKind = FileBackendKind::NullFileBackend;
    const char* diagnosticId = nullptr;
    FileMetadata metadata;
    core::pmr::Vector<FileDirectoryEntry> entries{&core::defaultCpuMemoryResource()};
    std::uint64_t totalEntries = 0;
    FileDirectoryCursor next;

    void fail(FileIoStatus failure, const char* diagnostic) noexcept
    {
        status = failure;
        diagnosticId = diagnostic;
        metadata = {};
        entries.clear();
        totalEntries = 0;
        next = {};
    }
};

struct FileQueryCompletion {
    using Complete = void (*)(void*, FileQueryResult);
    Complete complete = nullptr;
    void* userData = nullptr;

    bool valid() const noexcept { return complete != nullptr; }
};

} // namespace arp::core::io
