#pragma once

#include "core/io/file_query.h"
#include "core/io/file_read_contract.h"
#include "core/io/filesystem_path.h"

#include <limits>

namespace arp::core::io {

class ARP_API FileQueryContract final {
public:
    static FileReadValidation validate(const FileQueryRequest& request) noexcept
    {
        if (request.contractVersion != 1)
            return {FileIoStatus::InvalidRequest, "IO-QUERY-CONTRACT-VERSION"};
        if (request.pathScope != FilePathScope::MountedResource)
            return {FileIoStatus::InvalidPathScope, "IO-QUERY-MOUNTED-SCOPE-REQUIRED"};
        if (request.path.find('\0') != core::pmr::String::npos
            || core::Utf8Decoder::validate(request.path)
            || (!request.path.empty()
                && !core::FilePath::isSafeRelativePath(request.path, request.path.size())))
            return {FileIoStatus::InvalidRequest, "IO-QUERY-RELATIVE-PATH"};
        if (!request.maxResultBytes || request.maxResultBytes == UINT64_MAX)
            return {FileIoStatus::InvalidRequest, "IO-QUERY-RESULT-BUDGET"};
        if (request.versionPolicy != FileVersionPolicy::None
            && request.versionPolicy != FileVersionPolicy::ContentSha256)
            return {FileIoStatus::InvalidRequest, "IO-QUERY-VERSION-POLICY"};
        if (request.versionPolicy == FileVersionPolicy::None && request.maxVersionBytes)
            return {FileIoStatus::InvalidRequest, "IO-QUERY-UNUSED-VERSION-BUDGET"};
        if (!request.cursor.snapshot.valid()
            || (request.versionPolicy == FileVersionPolicy::ContentSha256
                && request.maxVersionBytes == UINT64_MAX))
            return {FileIoStatus::InvalidRequest, "IO-QUERY-VERSION-TOKEN-OR-BUDGET"};
        if (request.operation == FileQueryOperation::Stat) {
            if (request.pageSize || request.maxScanEntries || request.maxScanNameBytes
                || request.cursor.offset || request.cursor.snapshot.kind != FileVersionKind::None)
                return {FileIoStatus::InvalidRequest, "IO-QUERY-STAT-PAGINATION"};
        } else if (request.operation == FileQueryOperation::Enumerate) {
            if (!request.pageSize || !request.maxScanEntries || !request.maxScanNameBytes
                || request.pageSize > request.maxScanEntries
                || request.versionPolicy != FileVersionPolicy::None)
                return {FileIoStatus::InvalidRequest, "IO-QUERY-SCAN-BUDGET"};
            if ((request.cursor.snapshot.kind == FileVersionKind::None && request.cursor.offset)
                || (request.cursor.snapshot.kind != FileVersionKind::None
                    && (request.cursor.snapshot.kind != FileVersionKind::DirectorySnapshotSha256
                        || !request.cursor.offset)))
                return {FileIoStatus::InvalidRequest, "IO-QUERY-CURSOR"};
        } else {
            return {FileIoStatus::InvalidRequest, "IO-QUERY-OPERATION"};
        }
        const auto fixed =
            static_cast<std::uint64_t>(request.maxScanEntries) * sizeof(FileDirectoryEntry);
        if (request.maxScanNameBytes > UINT64_MAX - fixed
            || request.maxResultBytes > UINT64_MAX - fixed - request.maxScanNameBytes)
            return {FileIoStatus::BudgetExceeded, "IO-QUERY-BUDGET-OVERFLOW"};
        return {};
    }

    // Owned result payload; strings/entry arrays are additionally bounded by
    // pageSize and per-backend path limits. Network wire budgets are separate.
    static std::uint64_t resultBytes(const FileQueryResult& result) noexcept;
    static std::uint64_t acquireBackendScope() noexcept;
    static std::uint64_t reservationBytes(const FileQueryRequest& request) noexcept;
    static FileQueryResult page(const FileQueryRequest& request,
        core::pmr::Vector<FileDirectoryEntry> entries,
        std::uint64_t backendScope);
};

} // namespace arp::core::io
