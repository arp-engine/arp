#pragma once

#include "core/io/file_service.h"
#include "core/runtime/cancellation_token.h"
#include "core/runtime/completion_queue.h"
#include "core/runtime/work_service.h"
#include "resource/resource_manager.h"

#include <cstddef>
#include <cstdint>

namespace arp::resource {

enum class ResourceGraphSubmitStatus : std::uint8_t {
    Accepted,
    InvalidRequest,
    NodeBudgetExceeded,
    FileReadBudgetExceeded,
    SourceByteBudgetExceeded,
    QueueFull,
    ServiceClosed,
};

struct ResourceGraphExecutionBudget {
    std::size_t maxNodes = 0u;
    std::size_t maxFileReads = 0u;
    std::size_t maxSourceBytes = 0u;
    std::size_t maxDecodedBytes = 0u;
    std::size_t completionDispatchBudget = 0u;
};

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
};

struct ResourceGraphExecutionRequest {
    ResourceGraphRequest graph;
    ResourceGraphExecutionBudget budget;
    core::runtime::CancellationToken cancellation;
};

struct ResourceGraphExecutionResult {
    ResourceLoadResult resource;
    ResourceGraphExecutionTelemetry telemetry;
};

using ResourceGraphExecutionCompleteFn = void (*)(void*, ResourceGraphExecutionResult);

struct ResourceGraphExecutionCompletion {
    ResourceGraphExecutionCompleteFn complete = nullptr;
    void* userData = nullptr;

    bool valid() const noexcept { return complete != nullptr; }
};

class ARP_API ResourceGraphExecutor final {
public:
    ResourceGraphExecutor(ResourceManager& manager,
        core::io::FileService& fileService,
        core::runtime::WorkService& workService,
        core::runtime::CompletionQueue& completionQueue) noexcept;

    ResourceGraphSubmitStatus submit(ResourceGraphExecutionRequest request,
        ResourceGraphExecutionCompletion completion) noexcept;

private:
    ResourceManager& manager_;
    core::io::FileService& fileService_;
    core::runtime::WorkService& workService_;
    core::runtime::CompletionQueue& completionQueue_;
};

} // namespace arp::resource
