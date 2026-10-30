#pragma once

#include "core/io/file_request.h"
#include "core/io/file_result.h"
#include "core/text/utf8.h"

#include <limits>

namespace arp::core::io {

struct FileReadValidation {
    FileIoStatus status = FileIoStatus::Success;
    const char* diagnostic = nullptr;
};

struct FileResolvedReadRange {
    FileIoStatus status = FileIoStatus::Success;
    const char* diagnostic = nullptr;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

// Pure request validation. No filesystem, allocation or transport work occurs.
class FileReadContract final {
public:
    static FileResolvedReadRange resolveRange(
        const FileReadRequest& request, std::uint64_t fileSize) noexcept
    {
        const auto offset =
            request.range.mode == FileReadRangeMode::ExactRange ? request.range.offset : 0;
        auto size =
            request.range.mode == FileReadRangeMode::ExactRange ? request.range.size : fileSize;
        if (offset > fileSize)
            return {FileIoStatus::InvalidRange, "IO-READ-OFFSET"};
        if (size > fileSize - offset) {
            if (request.partialPolicy == FilePartialReadPolicy::DenyPartial)
                return {FileIoStatus::PartialReadDenied, "IO-READ-PARTIAL-DENIED"};
            size = fileSize - offset;
        }
        if (size > request.maxBytes)
            return {FileIoStatus::BudgetExceeded, "IO-READ-BYTE-BUDGET"};
        return {FileIoStatus::Success, nullptr, offset, size};
    }

    static FileReadValidation validate(const FileReadRequest& request) noexcept
    {
        if (request.contractVersion > 1)
            return {FileIoStatus::InvalidRequest, "IO-READ-CONTRACT-VERSION"};
        if (!request.expectedVersion.valid())
            return {FileIoStatus::InvalidRequest, "IO-READ-VERSION-TOKEN"};
        if (request.expectedVersion.kind != FileVersionKind::None
            && request.maxVersionBytes == UINT64_MAX)
            return {FileIoStatus::InvalidRequest, "IO-READ-VERSION-BUDGET"};
        if (request.expectedVersion.kind != FileVersionKind::None
            && request.expectedVersion.kind != FileVersionKind::ContentSha256)
            return {FileIoStatus::Unsupported, "IO-READ-STRONG-VERSION-REQUIRED"};
        if (request.expectedVersion.kind == FileVersionKind::None && request.maxVersionBytes)
            return {FileIoStatus::InvalidRequest, "IO-READ-UNUSED-VERSION-BUDGET"};
        if (request.contractVersion == 1 && (!request.maxBytes || request.maxBytes == UINT64_MAX))
            return {FileIoStatus::InvalidRequest, "IO-READ-BUDGET-REQUIRED"};
        if (request.pathScope != FilePathScope::HostFilesystem
            && request.pathScope != FilePathScope::MountedResource
            && request.pathScope != FilePathScope::Package)
            return {FileIoStatus::InvalidPathScope, "IO-READ-PATH-SCOPE"};
        if (request.path.empty() || request.path.find('\0') != core::pmr::String::npos)
            return {FileIoStatus::InvalidRequest, "IO-READ-PATH"};
        if (request.contractVersion == 1 && core::Utf8Decoder::validate(request.path))
            return {FileIoStatus::InvalidRequest, "IO-READ-PATH-ENCODING"};
        if (request.range.mode != FileReadRangeMode::WholeFile
            && request.range.mode != FileReadRangeMode::ExactRange)
            return {FileIoStatus::InvalidRange, "IO-READ-RANGE-MODE"};
        if (request.partialPolicy != FilePartialReadPolicy::DenyPartial
            && request.partialPolicy != FilePartialReadPolicy::AllowPartial)
            return {FileIoStatus::InvalidRequest, "IO-READ-PARTIAL-POLICY"};
        if (request.priority != FileRequestPriority::Normal
            && request.priority != FileRequestPriority::Background
            && request.priority != FileRequestPriority::Interactive)
            return {FileIoStatus::InvalidRequest, "IO-READ-PRIORITY"};
        if (request.range.mode == FileReadRangeMode::WholeFile
            && (request.range.offset || request.range.size))
            return {FileIoStatus::InvalidRange, "IO-READ-WHOLE-RANGE"};
        if (request.range.mode == FileReadRangeMode::ExactRange) {
            if (!request.range.size || request.range.offset > UINT64_MAX - request.range.size)
                return {FileIoStatus::InvalidRange, "IO-READ-RANGE-OVERFLOW"};
            if (request.range.size > request.maxBytes)
                return {FileIoStatus::BudgetExceeded, "IO-READ-BYTE-BUDGET"};
        }
        return {};
    }
};
} // namespace arp::core::io
