#pragma once

#include "core/io/file_request.h"
#include "core/io/file_result.h"
#include "core/text/text_decoder.h"

namespace arp::core::io {

struct FileTextReadRequest {
    FileReadRequest file;
    TextDecodeDesc text;
};

struct FileTextReadResult {
    // Original bytes and version remain intact, including BOM/newlines/NUL.
    // Decoded output has a separate explicit budget and never defines identity.
    FileReadResult source;
    TextDecodeResult decoded;

    FileIoStatus status() const noexcept
    {
        if (source.status != FileIoStatus::Success)
            return source.status;
        if (decoded.status == TextDecodeStatus::Success)
            return FileIoStatus::Success;
        if (decoded.status == TextDecodeStatus::ByteBudgetExceeded
            || decoded.status == TextDecodeStatus::AllocationFailed)
            return FileIoStatus::BudgetExceeded;
        return FileIoStatus::IntegrityMismatch;
    }

    const char* diagnosticId() const noexcept
    {
        return source.status != FileIoStatus::Success ? source.diagnosticId : decoded.diagnosticId;
    }
};

using FileTextReadCompleteFn = void (*)(void*, FileTextReadResult);

struct FileTextReadCompletion {
    FileTextReadCompleteFn complete = nullptr;
    void* userData = nullptr;

    bool valid() const noexcept { return complete != nullptr; }
};

} // namespace arp::core::io
