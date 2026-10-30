#pragma once

#include "core/io/file_service.h"
#include "core/runtime/cancellation_token.h"
#include "core/runtime/completion_queue.h"
#include "core/runtime/work_service.h"
#include "resource/resource_manager.h"

#include <cstddef>
#include <cstdint>
#include <thread>

namespace arp::resource {

struct ResourceGraphExecutionTelemetry {
    core::pmr::Vector<ResourceGraphStage> stageOrder{&core::defaultCpuMemoryResource()};
    std::uint64_t bytesRead = 0u;
    std::uint64_t bytesDecoded = 0u;
    std::uint64_t completedNodes = 0u;
    std::uint64_t queueRejections = 0u;
    std::uint64_t cancellations = 0u;
    std::uint64_t dependencyFailures = 0u;
    std::uint64_t lateCompletionSuppressions = 0u;
    std::uint64_t completionCount = 0u;
    std::uint64_t totalDurationNs = 0u;
    std::uint64_t submittedFileReads = 0;
    std::uint64_t processingDispatches = 0;
    std::uint64_t locationValidations = 0;
};

struct ResourceGraphExecutionRequest {
    ResourceGraphRequest graph;
    ResourceGraphExecutionBudget budget;
    core::runtime::CancellationToken cancellation;
};

struct ResourceGraphExecutionResult {
    ResourceLoadResult resource;
    ResourceGraphExecutionTelemetry telemetry;
    // Static terminal diagnostic remains available on allocation failure.
    const char* diagnosticId = nullptr;
    core::io::FileIoStatus fileStatus = core::io::FileIoStatus::Success;
};

using ResourceGraphExecutionCompleteFn = void (*)(void*, ResourceGraphExecutionResult);

struct ResourceGraphExecutionCompletion {
    ResourceGraphExecutionCompleteFn complete = nullptr;
    void* userData = nullptr;

    bool valid() const noexcept { return complete != nullptr; }
};

class ARP_API ResourceGraphExecutor final {
public:
    // All calls and CompletionQueue dispatch use this constructor's thread.
    // Parsing/admission are owner work; FileService still owns asynchronous IO.
    ResourceGraphExecutor(ResourceManager& manager,
        core::io::FileService& fileService,
        core::runtime::CompletionQueue& completionQueue) noexcept;
    // Compatibility overload. WorkService is no longer used for registry work.
    ResourceGraphExecutor(ResourceManager& manager,
        core::io::FileService& fileService,
        core::runtime::WorkService& workService,
        core::runtime::CompletionQueue& completionQueue) noexcept;

    ResourceGraphSubmitStatus submit(ResourceGraphExecutionRequest request,
        ResourceGraphExecutionCompletion completion) noexcept;

private:
    ResourceManager& manager_;
    core::io::FileService& fileService_;
    core::runtime::CompletionQueue& completionQueue_;
    std::thread::id ownerThread_ = std::this_thread::get_id();
};

} // namespace arp::resource
