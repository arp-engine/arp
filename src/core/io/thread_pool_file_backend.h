#pragma once

#include "core/io/blocking_file_reader.h"
#include "core/io/file_backend.h"
#include "core/runtime/work_service.h"

namespace arp::core::io {

class ARP_API ThreadPoolFallbackFileBackend final : public FileBackend {
public:
    explicit ThreadPoolFallbackFileBackend(runtime::WorkService& workService) noexcept;
    ~ThreadPoolFallbackFileBackend() override = default;

    FileBackendCapability capability() const noexcept override;
    FileReadResult validateRead(const FileReadRequest& request) const override;
    FileIoStatus readAsync(const FileReadRequest& request,
        FileReadCompletion completion,
        runtime::CompletionQueue& completions) noexcept override;

private:
    struct WorkState {
        BlockingFileReader reader;
        FileReadRequest request;
        FileReadCompletion completion;
        FileReadResult result;
    };

    static runtime::WorkStatus execute(void* userData);
    static void complete(void* userData, runtime::WorkResult result);

    runtime::WorkService& workService_;
};

} // namespace arp::core::io
