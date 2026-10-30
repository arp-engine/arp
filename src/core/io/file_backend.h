#pragma once

#include "core/api.h"
#include "core/io/file_query.h"
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
    bool supportsMetadata = false;
    bool supportsEnumeration = false;
    bool supportsContentVersion = false;
};

class ARP_API FileBackend {
public:
    virtual ~FileBackend() = default;

    virtual FileBackendCapability capability() const noexcept = 0;
    // Pure request/location admission; no filesystem or network IO. A closed
    // producer reports ServiceClosed, including when reached through a mount.
    virtual FileReadResult validateRead(const FileReadRequest& request) const = 0;
    virtual FileIoStatus readAsync(const FileReadRequest& request,
        FileReadCompletion completion,
        runtime::CompletionQueue& completions) noexcept = 0;

    virtual FileIoStatus queryAsync(
        const FileQueryRequest&, FileQueryCompletion, runtime::CompletionQueue&) noexcept
    {
        return FileIoStatus::Unsupported;
    }
};

class ARP_API NullFileBackend final : public FileBackend {
public:
    FileBackendCapability capability() const noexcept override;
    FileReadResult validateRead(const FileReadRequest& request) const override;
    FileIoStatus readAsync(const FileReadRequest& request,
        FileReadCompletion completion,
        runtime::CompletionQueue& completions) noexcept override;
};

// Injectable backends own their producers. shutdown rejects new submissions
// and quiesces producer access to CompletionQueue before returning. Already
// queued completions retain independent state and remain dispatchable.
class ARP_API ManagedFileBackend : public FileBackend {
public:
    virtual void shutdown() noexcept = 0;
};

} // namespace arp::core::io
