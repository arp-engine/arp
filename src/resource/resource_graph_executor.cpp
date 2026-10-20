#include "resource/resource_graph_executor.h"

#include "resource/resource_graph_plan.h"

#include <chrono>
#include <utility>

namespace arp::resource {

namespace {

struct ExecutionState {
    ResourceGraphExecutor* executor = nullptr;
    ResourceManager* manager = nullptr;
    core::io::FileService* fileService = nullptr;
    core::runtime::WorkService* workService = nullptr;
    core::runtime::CompletionQueue* completionQueue = nullptr;
    ResourceGraphExecutionRequest request;
    ResourceGraphExecutionCompletion completion;
    ResourceGraphExecutionResult result;
    core::pmr::Vector<std::size_t> order{&core::defaultCpuMemoryResource()};
    std::size_t orderIndex = 0u;
    core::pmr::Vector<ResourceFileReadIntent> intents{&core::defaultCpuMemoryResource()};
    std::size_t intentIndex = 0u;
    ResourceSourceSet sources;
    std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
    bool completed = false;
};

core::pmr::String text(const char* value)
{
    return core::pmr::String(value, &core::defaultCpuMemoryResource());
}

ResourceLoadResult failure(const ResourceGraphRequest& graph, const char* message)
{
    ResourceLoadResult result;
    result.state = ResourceState::Failed;
    result.resourceId = graph.rootResourceId;
    result.diagnostics = text(message);
    return result;
}

void finishNow(void* userData) noexcept
{
    core::UniquePtr<ExecutionState> state{
        static_cast<ExecutionState*>(userData),
        core::CpuAllocatedDeleter<ExecutionState>{
            userData,
            &core::destroyCpuAllocatedUnique<ExecutionState>,
        },
    };
    if (state->completed) {
        ++state->result.telemetry.lateCompletionSuppressions;
        return;
    }
    state->completed = true;
    state->result.telemetry.totalDurationNs =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - state->startedAt)
                .count());
    state->result.telemetry.stageOrder.push_back(ResourceGraphStage::Completion);
    state->result.telemetry.completionCount = 1u;
    state->completion.complete(state->completion.userData, std::move(state->result));
}

void finishFromDispatch(ExecutionState* state) noexcept
{
    finishNow(state);
}

ResourceGraphSubmitStatus mapWorkStatus(core::runtime::WorkServiceStatus status)
{
    if (status == core::runtime::WorkServiceStatus::QueueFull) {
        return ResourceGraphSubmitStatus::QueueFull;
    }
    if (status == core::runtime::WorkServiceStatus::ServiceClosed) {
        return ResourceGraphSubmitStatus::ServiceClosed;
    }
    return ResourceGraphSubmitStatus::InvalidRequest;
}

ResourceGraphSubmitStatus mapFileStatus(core::io::FileIoStatus status)
{
    if (status == core::io::FileIoStatus::QueueFull) {
        return ResourceGraphSubmitStatus::QueueFull;
    }
    if (status == core::io::FileIoStatus::ServiceClosed) {
        return ResourceGraphSubmitStatus::ServiceClosed;
    }
    return ResourceGraphSubmitStatus::InvalidRequest;
}

ResourceGraphSubmitStatus startNode(ExecutionState* state);

core::runtime::WorkStatus executeNode(void* userData) noexcept
{
    auto* state = static_cast<ExecutionState*>(userData);
    if (state->request.cancellation.cancelled()) {
        state->result.resource =
            failure(state->request.graph, "resource graph execution cancelled before processing");
        ++state->result.telemetry.cancellations;
        return core::runtime::WorkStatus::Cancelled;
    }
    auto& node = state->request.graph.nodes[state->order[state->orderIndex]];
    state->result.telemetry.stageOrder.push_back(node.resource->processingStage());
    for (const auto& dependency : node.dependencies) {
        node.resource->addDependency(ResourceDependency{dependency.id, dependency.usage});
    }
    auto context = state->request.graph.context;
    context.sources = &state->sources;
    context.requirePreloadedSources = true;
    context.maxDecodedBytes = state->request.budget.maxDecodedBytes;
    auto* resource = node.resource.get();
    state->result.resource = state->manager->loadResource(std::move(node.resource), context);
    if (state->result.resource.success
        && resource->processingStage() == ResourceGraphStage::ImageDecode) {
        state->result.telemetry.bytesDecoded += resource->processedByteCount();
    }
    return state->result.resource.success ? core::runtime::WorkStatus::Completed
                                          : core::runtime::WorkStatus::Failed;
}

void completeNode(void* userData, core::runtime::WorkResult workResult) noexcept
{
    auto* state = static_cast<ExecutionState*>(userData);
    state->result.telemetry.stageOrder.push_back(ResourceGraphStage::Admission);
    if (workResult.status != core::runtime::WorkStatus::Completed
        || !state->result.resource.success) {
        const auto nodeIndex = state->order[state->orderIndex];
        if (!state->request.graph.nodes[nodeIndex].dependencies.empty()) {
            ++state->result.telemetry.dependencyFailures;
        }
        if (state->result.resource.diagnostics.empty()) {
            state->result.resource =
                failure(state->request.graph, "resource graph node processing failed");
        }
        finishFromDispatch(state);
        return;
    }
    ++state->result.telemetry.completedNodes;
    ++state->orderIndex;
    const auto status = startNode(state);
    if (status != ResourceGraphSubmitStatus::Accepted) {
        ++state->result.telemetry.queueRejections;
        state->result.resource =
            failure(state->request.graph, "resource graph node submission failed");
        finishFromDispatch(state);
    }
}

ResourceGraphSubmitStatus submitNodeWork(ExecutionState* state)
{
    const auto submitted = state->workService->submit(core::runtime::WorkItem{
        &executeNode,
        state,
        &completeNode,
        state,
        state->request.cancellation,
    });
    return submitted.accepted() ? ResourceGraphSubmitStatus::Accepted
                                : mapWorkStatus(submitted.status);
}

void completeFileRead(void* userData, core::io::FileReadResult read) noexcept
{
    auto* state = static_cast<ExecutionState*>(userData);
    if (read.status != core::io::FileIoStatus::Success) {
        state->result.resource = failure(state->request.graph, "resource graph file read failed");
        if (!read.diagnostics.empty()) {
            state->result.resource.diagnostics += ": ";
            state->result.resource.diagnostics += read.diagnostics;
        }
        finishFromDispatch(state);
        return;
    }
    const auto& intent = state->intents[state->intentIndex];
    if ((intent.maxBytes != 0u && read.bytes.size() > intent.maxBytes)
        || state->sources.totalBytes() + read.bytes.size() > state->request.budget.maxSourceBytes) {
        state->result.resource =
            failure(state->request.graph, "resource graph source byte budget exceeded");
        finishFromDispatch(state);
        return;
    }
    ResourceSourceBuffer source;
    source.path = intent.path;
    source.role = intent.role;
    source.bytes = std::move(read.bytes);
    state->result.telemetry.bytesRead += source.bytes.size();
    state->result.telemetry.stageOrder.push_back(ResourceGraphStage::FileRead);
    state->sources.add(std::move(source));
    ++state->intentIndex;
    if (state->intentIndex < state->intents.size()) {
        core::io::FileReadRequest request;
        const auto& next = state->intents[state->intentIndex];
        request.pathScope = next.pathScope;
        request.path = next.path;
        request.cancellation = state->request.cancellation;
        const auto status = state->fileService->read(request, {&completeFileRead, state});
        if (status != core::io::FileIoStatus::Accepted) {
            ++state->result.telemetry.queueRejections;
            state->result.resource =
                failure(state->request.graph, "resource graph file read submission failed");
            finishFromDispatch(state);
        }
        return;
    }
    const auto status = submitNodeWork(state);
    if (status != ResourceGraphSubmitStatus::Accepted) {
        ++state->result.telemetry.queueRejections;
        state->result.resource =
            failure(state->request.graph, "resource graph processing queue rejected node");
        finishFromDispatch(state);
    }
}

ResourceGraphSubmitStatus startNode(ExecutionState* state)
{
    if (state->orderIndex >= state->order.size()) {
        finishFromDispatch(state);
        return ResourceGraphSubmitStatus::Accepted;
    }
    if (state->request.cancellation.cancelled()) {
        ++state->result.telemetry.cancellations;
        state->result.resource = failure(
            state->request.graph, "resource graph execution cancelled before file acquisition");
        finishFromDispatch(state);
        return ResourceGraphSubmitStatus::Accepted;
    }
    state->intents.clear();
    state->intentIndex = 0u;
    state->sources.clear();
    auto& node = state->request.graph.nodes[state->order[state->orderIndex]];
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    if (!node.resource->collectFileReadIntents(*state->manager, state->intents, diagnostics)) {
        state->result.resource =
            failure(state->request.graph, "resource graph file intent collection failed");
        if (!diagnostics.empty()) {
            state->result.resource.diagnostics += ": ";
            state->result.resource.diagnostics += diagnostics;
        }
        finishFromDispatch(state);
        return ResourceGraphSubmitStatus::Accepted;
    }
    if (state->intents.size() > state->request.budget.maxFileReads) {
        return ResourceGraphSubmitStatus::FileReadBudgetExceeded;
    }
    if (state->intents.empty()) {
        return submitNodeWork(state);
    }
    core::io::FileReadRequest request;
    request.pathScope = state->intents.front().pathScope;
    request.path = state->intents.front().path;
    request.cancellation = state->request.cancellation;
    const auto status = state->fileService->read(request, {&completeFileRead, state});
    return status == core::io::FileIoStatus::Accepted ? ResourceGraphSubmitStatus::Accepted
                                                      : mapFileStatus(status);
}

} // namespace

ResourceGraphExecutor::ResourceGraphExecutor(ResourceManager& manager,
    core::io::FileService& fileService,
    core::runtime::WorkService& workService,
    core::runtime::CompletionQueue& completionQueue) noexcept
    : manager_(manager), fileService_(fileService), workService_(workService),
      completionQueue_(completionQueue)
{}

ResourceGraphSubmitStatus ResourceGraphExecutor::submit(
    ResourceGraphExecutionRequest request, ResourceGraphExecutionCompletion completion) noexcept
{
    if (!completion.valid() || request.budget.maxNodes == 0u || request.budget.maxFileReads == 0u
        || request.budget.maxSourceBytes == 0u || request.budget.maxDecodedBytes == 0u
        || request.budget.completionDispatchBudget == 0u) {
        return request.budget.maxNodes == 0u ? ResourceGraphSubmitStatus::NodeBudgetExceeded
                                             : ResourceGraphSubmitStatus::InvalidRequest;
    }
    if (completionQueue_.closed()) {
        return ResourceGraphSubmitStatus::ServiceClosed;
    }
    if (request.graph.nodes.size() > request.budget.maxNodes) {
        return ResourceGraphSubmitStatus::NodeBudgetExceeded;
    }

    auto state = core::makeUnique<ExecutionState>();
    state->executor = this;
    state->manager = &manager_;
    state->fileService = &fileService_;
    state->workService = &workService_;
    state->completionQueue = &completionQueue_;
    state->request = std::move(request);
    state->completion = completion;
    state->result.telemetry.stageOrder.push_back(ResourceGraphStage::Validation);
    auto plan = buildResourceGraphPlan(state->request.graph);
    if (!plan.success) {
        state->result.resource.resourceId = plan.resourceId;
        state->result.resource.resourceType = plan.resourceType;
        state->result.resource.diagnostics = std::move(plan.diagnostics);
        return ResourceGraphSubmitStatus::InvalidRequest;
    }
    state->order = std::move(plan.order);

    auto* raw = state.get();
    if (raw->request.cancellation.cancelled()) {
        ++raw->result.telemetry.cancellations;
        raw->result.resource = failure(
            raw->request.graph, "resource graph execution cancelled before file acquisition");
        const auto posted = completionQueue_.post({&finishNow, raw});
        if (!posted.accepted()) {
            return posted.status == core::runtime::CompletionStatus::ServiceClosed
                ? ResourceGraphSubmitStatus::ServiceClosed
                : ResourceGraphSubmitStatus::QueueFull;
        }
        state.release();
        return ResourceGraphSubmitStatus::Accepted;
    }

    state.release();
    const auto started = startNode(raw);
    if (started != ResourceGraphSubmitStatus::Accepted) {
        core::UniquePtr<ExecutionState> cleanup{
            raw,
            core::CpuAllocatedDeleter<ExecutionState>{
                raw,
                &core::destroyCpuAllocatedUnique<ExecutionState>,
            },
        };
    }
    return started;
}

} // namespace arp::resource
