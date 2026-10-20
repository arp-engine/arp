#pragma once

#include "core/api.h"
#include "core/io/file_request.h"
#include "core/io/file_result.h"
#include "core/runtime/completion_queue.h"

namespace arp::core::io {

struct FileBackendCapability {
    FileBackendKind kind = FileBackendKind::NullFileBackend;
    FileBackendStatus status = FileBackendStatus::ContractComplete;
    bool nativeAsync = false;
    bool supportsCancellation = false;
    bool completesThroughCompletionQueue = false;
};

class ARP_API FileBackend {
public:
    virtual ~FileBackend() = default;

    virtual FileBackendCapability capability() const noexcept = 0;
    virtual FileReadResult validateRead(const FileReadRequest& request) const = 0;
    virtual FileIoStatus readAsync(const FileReadRequest& request,
        FileReadCompletion completion,
        runtime::CompletionQueue& completions) noexcept = 0;
};

class ARP_API NullFileBackend final : public FileBackend {
public:
    FileBackendCapability capability() const noexcept override;
    FileReadResult validateRead(const FileReadRequest& request) const override;
    FileIoStatus readAsync(const FileReadRequest& request,
        FileReadCompletion completion,
        runtime::CompletionQueue& completions) noexcept override;
};

} // namespace arp::core::io
