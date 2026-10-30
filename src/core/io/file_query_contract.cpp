#include "core/io/file_query_contract.h"

#include "core/hash/sha256.h"
#include "core/ownership/monotonic_id_sequence.h"

#include <algorithm>
#include <utility>

namespace arp::core::io {

std::uint64_t FileQueryContract::acquireBackendScope() noexcept
{
    static core::MonotonicIdSequence sequence;
    return sequence.tryAcquire();
}

std::uint64_t FileQueryContract::resultBytes(const FileQueryResult& result) noexcept
{
    std::uint64_t size =
        sizeof(FileMetadata) + sizeof(FileDirectoryCursor) + sizeof(result.totalEntries);
    size += result.entries.size() * sizeof(FileDirectoryEntry);
    for (const auto& entry : result.entries)
        size += entry.name.size();
    return size;
}

std::uint64_t FileQueryContract::reservationBytes(const FileQueryRequest& request) noexcept
{
    return request.maxResultBytes + request.maxScanNameBytes
        + static_cast<std::uint64_t>(request.maxScanEntries) * sizeof(FileDirectoryEntry);
}

FileQueryResult FileQueryContract::page(const FileQueryRequest& request,
    core::pmr::Vector<FileDirectoryEntry> entries,
    std::uint64_t backendScope)
{
    FileQueryResult result;
    const auto validation = validate(request);
    if (validation.diagnostic) {
        result.fail(validation.status, validation.diagnostic);
        return result;
    }
    if (request.operation != FileQueryOperation::Enumerate) {
        result.fail(FileIoStatus::InvalidRequest, "IO-QUERY-PAGE-REQUIRES-ENUMERATE");
        return result;
    }
    std::uint64_t nameBytes = 0;
    for (const auto& entry : entries) {
        if (!core::FilePath::isSafeRelativePath(entry.name, entry.name.size())
            || entry.name.find('/') != core::pmr::String::npos
            || entry.name.find('\0') != core::pmr::String::npos
            || core::Utf8Decoder::validate(entry.name) || !entry.metadata.version.valid()
            || (entry.metadata.kind != FileEntryKind::RegularFile
                && entry.metadata.kind != FileEntryKind::Directory
                && entry.metadata.kind != FileEntryKind::Link)) {
            result.fail(FileIoStatus::InvalidRequest, "IO-QUERY-INVALID-ENTRY");
            return result;
        }
        if (entry.name.size() > request.maxScanNameBytes - nameBytes) {
            result.fail(FileIoStatus::BudgetExceeded, "IO-QUERY-SCAN-NAME-BUDGET");
            return result;
        }
        nameBytes += entry.name.size();
    }
    if (entries.size() > request.maxScanEntries) {
        result.fail(FileIoStatus::BudgetExceeded, "IO-QUERY-SCAN-ENTRY-BUDGET");
        return result;
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });
    core::Sha256 hash;
    hash.update("ArpDirectorySnapshot/1");
    FileVersionEncoding::appendNumber(hash, backendScope);
    FileVersionEncoding::appendNumber(hash, request.mount.generation);
    FileVersionEncoding::appendNumber(hash, request.path.size());
    hash.update(request.path);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        if (i && entries[i - 1].name == entry.name) {
            result.fail(FileIoStatus::InvalidRequest, "IO-QUERY-DUPLICATE-NAME");
            return result;
        }
        FileVersionEncoding::appendNumber(hash, entry.name.size());
        hash.update(entry.name);
        FileVersionEncoding::appendNumber(hash, static_cast<std::uint64_t>(entry.metadata.kind));
        FileVersionEncoding::appendNumber(hash, entry.metadata.sizeBytes);
        FileVersionEncoding::appendNumber(
            hash, static_cast<std::uint64_t>(entry.metadata.version.kind));
        hash.update({reinterpret_cast<const char*>(entry.metadata.version.digest.data()),
            entry.metadata.version.digest.size()});
    }
    const auto snapshot =
        FileVersionEncoding::finish(hash, FileVersionKind::DirectorySnapshotSha256);
    if (request.cursor.snapshot.kind != FileVersionKind::None
        && request.cursor.snapshot != snapshot) {
        result.fail(FileIoStatus::VersionMismatch, "IO-QUERY-DIRECTORY-CHANGED");
        return result;
    }
    if (request.cursor.offset > entries.size()) {
        result.fail(FileIoStatus::InvalidRange, "IO-QUERY-CURSOR-OFFSET");
        return result;
    }
    const auto count =
        std::min<std::uint64_t>(request.pageSize, entries.size() - request.cursor.offset);
    result.entries.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i)
        result.entries.push_back(
            std::move(entries[static_cast<std::size_t>(request.cursor.offset + i)]));
    result.totalEntries = entries.size();
    result.metadata.kind = FileEntryKind::Directory;
    result.metadata.version = snapshot;
    if (request.cursor.offset + count < entries.size())
        result.next = {snapshot, request.cursor.offset + count};
    if (resultBytes(result) > request.maxResultBytes)
        result.fail(FileIoStatus::BudgetExceeded, "IO-QUERY-RESULT-BUDGET");
    return result;
}

} // namespace arp::core::io
