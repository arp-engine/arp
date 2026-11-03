#pragma once

#include "core/io/file_save.h"

namespace arp::core::io {

enum class FileSaveReceiptOperation : std::uint8_t { Query, Release };
enum class FileSaveReceiptState : std::uint8_t { Unavailable, Pending, Terminal, Released };

struct FileSaveReceiptRequest {
    std::uint32_t contractVersion = 1;
    FilePathScope pathScope = FilePathScope::Unspecified;
    core::PoolHandle mount;
    FileSaveReceipt receipt;
    FileSaveReceiptOperation operation = FileSaveReceiptOperation::Query;
    std::uint64_t maxResultBytes = 0;
    runtime::CancellationToken cancellation;
};

struct FileSaveReceiptResult {
    FileIoStatus status = FileIoStatus::BackendUnavailable;
    FileIoStatus deliveryStatus = FileIoStatus::Success;
    const char* diagnosticId = nullptr;
    FileSaveReceiptState state = FileSaveReceiptState::Unavailable;
    FileSaveReceipt receipt;
    // Unobserved is never evidence of a failed/rolled-back save.
    FileSaveResult outcome = [] {
        FileSaveResult value;
        value.commitState = FileCommitState::OutcomeUnknown;
        return value;
    }();
};

struct FileSaveReceiptCompletion {
    using Complete = void (*)(void*, FileSaveReceiptResult);
    Complete complete = nullptr;
    void* userData = nullptr;

    bool valid() const noexcept { return complete != nullptr; }
};

} // namespace arp::core::io
