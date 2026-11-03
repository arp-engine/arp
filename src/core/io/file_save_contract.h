#pragma once

#include "core/io/file_read_contract.h"
#include "core/io/file_save.h"
#include "core/io/filesystem_path.h"

namespace arp::core::io {

// Pure admission. A backend additionally bounds its path and worker storage.
class FileSaveContract final {
public:
    static FileReadValidation validate(const FileSaveRequest& request) noexcept
    {
        if (request.contractVersion != 1)
            return {FileIoStatus::InvalidRequest, "IO-SAVE-CONTRACT-VERSION"};
        if (request.pathScope != FilePathScope::MountedResource)
            return {FileIoStatus::InvalidPathScope, "IO-SAVE-MOUNTED-LOCATION-REQUIRED"};
        if (!request.mount || request.path.find('\0') != core::pmr::String::npos
            || !core::FilePath::isSafeRelativePath(request.path, request.path.size())
            || core::Utf8Decoder::validate(request.path))
            return {FileIoStatus::InvalidRequest, "IO-SAVE-LOCATION"};
        if (!request.maxWriteBytes || request.maxWriteBytes == UINT64_MAX
            || request.maxBaselineBytes == UINT64_MAX
            || request.maxWriteBytes > UINT64_MAX - request.maxBaselineBytes)
            return {FileIoStatus::InvalidRequest, "IO-SAVE-FINITE-BUDGET-REQUIRED"};
        if (request.bytes.capacity() > request.maxWriteBytes)
            return {FileIoStatus::BudgetExceeded, "IO-SAVE-PAYLOAD-BUDGET"};
        if (!request.expectedVersion.valid())
            return {FileIoStatus::InvalidRequest, "IO-SAVE-VERSION-TOKEN"};
        if (request.precondition == FileSavePrecondition::MustBeMissing) {
            if (request.expectedVersion.kind != FileVersionKind::None || request.maxBaselineBytes)
                return {FileIoStatus::InvalidRequest, "IO-SAVE-UNUSED-BASELINE"};
        } else if (request.precondition == FileSavePrecondition::ContentSha256) {
            if (request.expectedVersion.kind != FileVersionKind::ContentSha256)
                return {FileIoStatus::Unsupported, "IO-SAVE-CONTENT-BASELINE-REQUIRED"};
        } else {
            return {FileIoStatus::InvalidRequest, "IO-SAVE-PRECONDITION"};
        }
        return {};
    }

    static FileVersionToken contentVersion(const FileSaveRequest& request)
    {
        core::Sha256 hash;
        hash.update({reinterpret_cast<const char*>(request.bytes.data()), request.bytes.size()});
        return FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256);
    }

    static std::uint64_t reservationBytes(const FileSaveRequest& request) noexcept
    {
        return request.maxWriteBytes + request.maxBaselineBytes;
    }
};

} // namespace arp::core::io
