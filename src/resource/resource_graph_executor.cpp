#include "resource/resource_graph_executor.h"

#include "core/io/file_read_contract.h"
#include "resource/resource_graph_plan.h"
#include "resource/resource_source_io.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace arp::resource {

namespace {

struct ExecutionState {
    ResourceManager* manager = nullptr;
    core::io::FileService* fileService = nullptr;
    std::thread::id ownerThread;
    core::io::FileReadRequest locationCheck;
    core::runtime::CompletionQueue* completionQueue = nullptr;
    ResourceGraphExecutionRequest request;
    ResourceGraphExecutionCompletion completion;
    ResourceGraphExecutionResult result;
    ResourceLoadResult rootResult;
    bool rootAdmitted = false;
    core::pmr::Vector<std::size_t> order{&core::defaultCpuMemoryResource()};
    std::size_t orderIndex = 0u;
    core::pmr::Vector<ResourceFileReadIntent> intents{&core::defaultCpuMemoryResource()};
    std::size_t intentIndex = 0u;
    ResourceSourceSet sources;
    std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
    bool completed = false;
    bool finishing = false;
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
    state->finishing = true;
}

template <class Function> void dispatchGuard(ExecutionState* state, Function&& function) noexcept
{
    const auto fail = [&](const char* diagnostic) {
        state->result.resource.success = false;
        state->result.resource.state = ResourceState::Failed;
        state->result.resource.resourceId = std::move(state->request.graph.rootResourceId);
        state->result.resource.diagnostics.clear();
        state->result.diagnosticId = diagnostic;
        state->finishing = true;
    };
    try {
        if (std::this_thread::get_id() != state->ownerThread) {
            state->result.fileStatus = core::io::FileIoStatus::InvalidRequest;
            fail("ARP-RESOURCE-GRAPH-OWNER-THREAD");
        } else
            function();
    } catch (const std::bad_alloc&) {
        state->result.fileStatus = core::io::FileIoStatus::BudgetExceeded;
        fail("ARP-RESOURCE-GRAPH-ALLOCATION");
    } catch (...) {
        fail("ARP-RESOURCE-GRAPH-EXECUTION");
    }
    if (state->finishing)
        finishNow(state);
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
void completeFileRead(void*, core::io::FileReadResult) noexcept;
ResourceGraphSubmitStatus startFileRead(ExecutionState* state);
ResourceGraphSubmitStatus planNodeSources(ExecutionState* state);

bool activeSources(ExecutionState* state) noexcept
{
    const auto refuse = [&](core::io::FileIoStatus status, const char* diagnostic) {
        if (!state->result.diagnosticId) {
            state->result.fileStatus = status;
            state->result.diagnosticId = diagnostic;
            if (status == core::io::FileIoStatus::Cancelled)
                ++state->result.telemetry.cancellations;
        }
        return false;
    };
    if (state->request.cancellation.cancelled())
        return refuse(core::io::FileIoStatus::Cancelled, "ARP-RESOURCE-GRAPH-CANCELLED");
    if (state->fileService->closed() || state->completionQueue->closed())
        return refuse(core::io::FileIoStatus::ServiceClosed, "ARP-RESOURCE-GRAPH-CLOSED");
    try {
        for (std::size_t i = 0; i < state->sources.size(); ++i) {
            const auto& source = *state->sources.sourceAt(i);
            state->locationCheck.path = source.path;
            state->locationCheck.pathScope = source.pathScope;
            state->locationCheck.mount = source.mount;
            ++state->result.telemetry.locationValidations;
            const auto result = state->fileService->validateRead(state->locationCheck);
            if (result.status != core::io::FileIoStatus::Success)
                return refuse(result.status, "ARP-RESOURCE-GRAPH-LOCATION-REJECTED");
        }
    } catch (...) {
        return refuse(
            core::io::FileIoStatus::BudgetExceeded, "ARP-RESOURCE-GRAPH-LOCATION-ALLOCATION");
    }
    return true;
}

bool commitAllowed(void* context) noexcept
{
    auto* state = static_cast<ExecutionState*>(context);
    if (!activeSources(state))
        return false;
    const auto& caller = state->request.graph.context.commitGate;
    if (caller.allow && !caller.allow(caller.userData)) {
        state->result.fileStatus = core::io::FileIoStatus::InvalidRequest;
        state->result.diagnosticId = "ARP-RESOURCE-GRAPH-COMMIT-GATE";
        return false;
    }
    // The caller gate may itself cancel or close a location.
    return activeSources(state);
}

void processNode(void* userData) noexcept
{
    auto* state = static_cast<ExecutionState*>(userData);
    dispatchGuard(state, [&] {
        ++state->result.telemetry.processingDispatches;
        if (!activeSources(state)) {
            state->result.resource = failure(
                state->request.graph, "resource graph admission rejected before processing");
            finishFromDispatch(state);
            return;
        }
        auto& node = state->request.graph.nodes[state->order[state->orderIndex]];
        state->result.telemetry.stageOrder.push_back(node.resource->processingStage());
        for (const auto& dependency : node.dependencies)
            node.resource->addDependency(ResourceDependency{dependency.id, dependency.usage});
        auto context = state->request.graph.context;
        context.sources = &state->sources;
        context.requirePreloadedSources = true;
        context.maxDecodedBytes = context.maxDecodedBytes
            ? std::min(context.maxDecodedBytes, state->request.budget.maxDecodedBytes)
            : state->request.budget.maxDecodedBytes;
        context.commitGate = {commitAllowed, state};
        auto* resource = node.resource.get();
        // Prepare completion strings before any registry mutation. Publishing
        // a successful candidate must not be followed by a fallible copy.
        state->result.resource.resourceId = resource->reference().id;
        state->result.resource.resourceType = resource->reference().type;
        core::pmr::Vector<core::UniquePtr<ResourceBase>> candidates;
        candidates.push_back(std::move(node.resource));
        auto batch = state->manager->loadResourcesAtomically(std::move(candidates), context);
        state->result.telemetry.stageOrder.push_back(ResourceGraphStage::Admission);
        state->result.resource.success = batch.success;
        state->result.resource.state = batch.success ? ResourceState::Ready : ResourceState::Failed;
        state->result.resource.diagnostics.swap(batch.diagnostics);
        if (!batch.success) {
            if (!node.dependencies.empty())
                ++state->result.telemetry.dependencyFailures;
            finishFromDispatch(state);
            return;
        }
        if (resource->reference().id == state->request.graph.rootResourceId) {
            std::swap(state->result.resource, state->rootResult);
            state->rootAdmitted = true;
        }
        if (resource->processingStage() == ResourceGraphStage::ImageDecode)
            state->result.telemetry.bytesDecoded += resource->processedByteCount();
        ++state->result.telemetry.completedNodes;
        ++state->orderIndex;
        const auto status = startNode(state);
        if (status != ResourceGraphSubmitStatus::Accepted) {
            ++state->result.telemetry.queueRejections;
            state->result.resource =
                failure(state->request.graph, "resource graph node submission failed");
            finishFromDispatch(state);
        }
    });
}

ResourceGraphSubmitStatus submitNodeProcessing(ExecutionState* state)
{
    const auto submitted = state->completionQueue->post({processNode, state});
    if (!submitted.accepted()) {
        state->result.fileStatus = submitted.status == core::runtime::CompletionStatus::QueueFull
            ? core::io::FileIoStatus::QueueFull
            : core::io::FileIoStatus::ServiceClosed;
        state->result.diagnosticId = "ARP-RESOURCE-GRAPH-OWNER-QUEUE-REJECTED";
    }
    return submitted.accepted() ? ResourceGraphSubmitStatus::Accepted
        : submitted.status == core::runtime::CompletionStatus::QueueFull
        ? ResourceGraphSubmitStatus::QueueFull
        : ResourceGraphSubmitStatus::ServiceClosed;
}

void completeFileReadImpl(void* userData, core::io::FileReadResult read)
{
    auto* state = static_cast<ExecutionState*>(userData);
    if (read.status != core::io::FileIoStatus::Success) {
        state->result.fileStatus = read.status;
        state->result.diagnosticId = read.diagnosticId;
        state->result.resource = failure(state->request.graph, "resource graph file read failed");
        if (!read.diagnostics.empty()) {
            state->result.resource.diagnostics += ": ";
            state->result.resource.diagnostics += read.diagnostics;
        } else if (read.diagnosticId) {
            state->result.resource.diagnostics += ": ";
            state->result.resource.diagnostics += read.diagnosticId;
        }
        finishFromDispatch(state);
        return;
    }
    const auto& intent = state->intents[state->intentIndex];
    if ((intent.maxBytes != 0u && read.bytes.size() > intent.maxBytes)
        || read.bytes.size()
            > state->request.budget.maxSourceBytes - state->result.telemetry.bytesRead) {
        state->result.resource =
            failure(state->request.graph, "resource graph source byte budget exceeded");
        finishFromDispatch(state);
        return;
    }
    ResourceSourceBuffer source;
    source.path = intent.path;
    source.role = intent.role;
    source.pathScope = intent.pathScope;
    source.mount = intent.mount;
    source.version = read.version;
    source.bytes = std::move(read.bytes);
    state->result.telemetry.bytesRead += source.bytes.size();
    state->result.telemetry.stageOrder.push_back(ResourceGraphStage::FileRead);
    if (!state->sources.add(std::move(source))) {
        state->result.diagnosticId = "ARP-RESOURCE-GRAPH-SOURCE-AMBIGUOUS";
        state->result.resource =
            failure(state->request.graph, "resource graph source path is ambiguous");
        finishFromDispatch(state);
        return;
    }
    ++state->intentIndex;
    if (state->intentIndex < state->intents.size()) {
        const auto status = startFileRead(state);
        if (status != ResourceGraphSubmitStatus::Accepted) {
            ++state->result.telemetry.queueRejections;
            state->result.resource =
                failure(state->request.graph, "resource graph file read submission failed");
            finishFromDispatch(state);
        }
        return;
    }
    const auto status = planNodeSources(state);
    if (status != ResourceGraphSubmitStatus::Accepted) {
        ++state->result.telemetry.queueRejections;
        state->result.resource =
            failure(state->request.graph, "resource graph processing queue rejected node");
        finishFromDispatch(state);
    }
}

void completeFileRead(void* context, core::io::FileReadResult read) noexcept
{
    auto* state = static_cast<ExecutionState*>(context);
    dispatchGuard(state, [&] {
        completeFileReadImpl(context, std::move(read));
    });
}

ResourceGraphSubmitStatus startFileRead(ExecutionState* state)
{
    if (!activeSources(state)) {
        state->result.resource =
            failure(state->request.graph, "resource graph source admission rejected");
        finishFromDispatch(state);
        return ResourceGraphSubmitStatus::Accepted;
    }
    if (state->result.telemetry.submittedFileReads >= state->request.budget.maxFileReads) {
        state->result.diagnosticId = "ARP-RESOURCE-GRAPH-READ-BUDGET";
        return ResourceGraphSubmitStatus::FileReadBudgetExceeded;
    }
    if (state->result.telemetry.bytesRead >= state->request.budget.maxSourceBytes) {
        state->result.diagnosticId = "ARP-RESOURCE-GRAPH-SOURCE-BUDGET";
        return ResourceGraphSubmitStatus::SourceByteBudgetExceeded;
    }
    const auto& intent = state->intents[state->intentIndex];
    core::io::FileReadRequest request;
    request.pathScope = intent.pathScope;
    request.path = intent.path;
    request.mount = intent.mount;
    request.expectedVersion = intent.expectedVersion;
    request.maxVersionBytes = intent.maxVersionBytes;
    request.contractVersion = 1;
    const auto remaining = state->request.budget.maxSourceBytes - state->result.telemetry.bytesRead;
    request.maxBytes =
        intent.maxBytes ? std::min<std::uint64_t>(intent.maxBytes, remaining) : remaining;
    request.cancellation = state->request.cancellation;
    const auto status = state->fileService->read(request, {completeFileRead, state});
    if (status == core::io::FileIoStatus::Accepted)
        ++state->result.telemetry.submittedFileReads;
    else {
        state->result.fileStatus = status;
        state->result.diagnosticId = "ARP-RESOURCE-GRAPH-READ-SUBMISSION";
    }
    return status == core::io::FileIoStatus::Accepted ? ResourceGraphSubmitStatus::Accepted
                                                      : mapFileStatus(status);
}

ResourceGraphSubmitStatus planNodeSources(ExecutionState* state)
{
    if (state->request.cancellation.cancelled()) {
        ++state->result.telemetry.cancellations;
        state->result.fileStatus = core::io::FileIoStatus::Cancelled;
        state->result.diagnosticId = "ARP-RESOURCE-GRAPH-CANCELLED";
        state->result.resource = failure(
            state->request.graph, "resource graph execution cancelled before file acquisition");
        finishFromDispatch(state);
        return ResourceGraphSubmitStatus::Accepted;
    }
    if (!activeSources(state)) {
        state->result.resource =
            failure(state->request.graph, "resource graph source admission rejected");
        finishFromDispatch(state);
        return ResourceGraphSubmitStatus::Accepted;
    }
    state->intents.clear();
    state->intentIndex = 0u;
    auto& node = state->request.graph.nodes[state->order[state->orderIndex]];
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    auto context = state->request.graph.context;
    context.sources = &state->sources;
    context.requirePreloadedSources = true;
    context.maxDecodedBytes = context.maxDecodedBytes
        ? std::min(context.maxDecodedBytes, state->request.budget.maxDecodedBytes)
        : state->request.budget.maxDecodedBytes;
    if (!node.resource->planFileReadIntents(
            *state->manager, context, state->intents, diagnostics)) {
        state->result.diagnosticId = "ARP-RESOURCE-GRAPH-SOURCE-PLAN";
        state->result.resource =
            failure(state->request.graph, "resource graph file intent collection failed");
        if (!diagnostics.empty()) {
            state->result.resource.diagnostics += ": ";
            state->result.resource.diagnostics += diagnostics;
        }
        finishFromDispatch(state);
        return ResourceGraphSubmitStatus::Accepted;
    }
    const auto validation =
        pendingResourceReads(state->intents, state->sources, state->request.budget.maxSourceBytes);
    if (validation.status != ResourceSourcePlanStatus::Valid) {
        state->result.fileStatus = validation.request.status;
        state->result.diagnosticId = validation.status == ResourceSourcePlanStatus::Ambiguous
            ? "ARP-RESOURCE-GRAPH-SOURCE-AMBIGUOUS"
            : validation.status == ResourceSourcePlanStatus::Conflict
            ? "ARP-RESOURCE-GRAPH-SOURCE-CONFLICT"
            : validation.request.diagnostic;
        state->result.resource = failure(state->request.graph,
            "resource graph source plan is invalid or conflicts with acquired data");
        finishFromDispatch(state);
        return ResourceGraphSubmitStatus::Accepted;
    }
    if (state->intents.size()
        > state->request.budget.maxFileReads - state->result.telemetry.submittedFileReads) {
        state->result.diagnosticId = "ARP-RESOURCE-GRAPH-READ-BUDGET";
        return ResourceGraphSubmitStatus::FileReadBudgetExceeded;
    }
    if (state->intents.empty()) {
        return submitNodeProcessing(state);
    }
    return startFileRead(state);
}

ResourceGraphSubmitStatus startNode(ExecutionState* state)
{
    if (state->orderIndex >= state->order.size()) {
        // The root need not be the last topological item: disconnected authored
        // nodes are permitted. Retain its prepared result without a final copy.
        std::swap(state->result.resource, state->rootResult);
        const auto* root = state->manager->recordSnapshot(state->request.graph.rootResourceId);
        if (!state->rootAdmitted || !root || root->state != ResourceState::Ready) {
            state->result.resource.success = false;
            state->result.resource.state = ResourceState::Failed;
            state->result.diagnosticId = "ARP-RESOURCE-GRAPH-ROOT-NOT-READY";
        }
        finishFromDispatch(state);
        return ResourceGraphSubmitStatus::Accepted;
    }
    state->sources.clear();
    return planNodeSources(state);
}

void startFromDispatch(void* context) noexcept
{
    auto* state = static_cast<ExecutionState*>(context);
    dispatchGuard(state, [&] {
        if (startNode(state) != ResourceGraphSubmitStatus::Accepted) {
            ++state->result.telemetry.queueRejections;
            state->result.resource = failure(
                state->request.graph, "resource graph acquisition or processing submission failed");
            finishFromDispatch(state);
        }
    });
}

} // namespace

ResourceGraphExecutor::ResourceGraphExecutor(ResourceManager& manager,
    core::io::FileService& fileService,
    core::runtime::WorkService& workService,
    core::runtime::CompletionQueue& completionQueue) noexcept
    : ResourceGraphExecutor(manager, fileService, completionQueue)
{
    (void)workService;
}

ResourceGraphExecutor::ResourceGraphExecutor(ResourceManager& manager,
    core::io::FileService& fileService,
    core::runtime::CompletionQueue& completionQueue) noexcept
    : manager_(manager), fileService_(fileService), completionQueue_(completionQueue)
{}

ResourceGraphSubmitStatus ResourceGraphExecutor::submit(
    ResourceGraphExecutionRequest request, ResourceGraphExecutionCompletion completion) noexcept
{
    if (std::this_thread::get_id() != ownerThread_ || !manager_.isOwnerThread()
        || request.graph.context.sources)
        return ResourceGraphSubmitStatus::InvalidRequest;
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

    if (request.budget.maxFileReads > SIZE_MAX - 2 || request.budget.maxNodes == SIZE_MAX
        || request.budget.completionDispatchBudget == SIZE_MAX
        || request.graph.nodes.size() > (SIZE_MAX - request.budget.maxFileReads - 2) / 2
        || request.budget.maxSourceBytes == SIZE_MAX || request.budget.maxDecodedBytes == SIZE_MAX)
        return ResourceGraphSubmitStatus::InvalidRequest;
    try {
        auto state = core::makeUnique<ExecutionState>();
        state->manager = &manager_;
        state->fileService = &fileService_;
        state->ownerThread = ownerThread_;
        state->locationCheck.contractVersion = 1;
        state->locationCheck.maxBytes = request.budget.maxSourceBytes;
        state->completionQueue = &completionQueue_;
        state->request = std::move(request);
        state->completion = completion;
        // Validation + per-node processing/admission + per-read acquisition +
        // completion. Reserve before acceptance so terminal telemetry cannot
        // allocate while handling an execution failure.
        state->result.telemetry.stageOrder.reserve(
            2 * state->request.graph.nodes.size() + state->request.budget.maxFileReads + 2);
        state->result.telemetry.stageOrder.push_back(ResourceGraphStage::Validation);
        auto plan = buildResourceGraphPlan(state->request.graph);
        if (!plan.success)
            return ResourceGraphSubmitStatus::InvalidRequest;
        state->order = std::move(plan.order);
        auto reservation = completionQueue_.reserve();
        if (!reservation.valid())
            return reservation.status() == core::runtime::CompletionStatus::QueueFull
                ? ResourceGraphSubmitStatus::QueueFull
                : ResourceGraphSubmitStatus::ServiceClosed;
        const auto posted =
            completionQueue_.postReserved(std::move(reservation), {startFromDispatch, state.get()});
        if (!posted.accepted())
            return ResourceGraphSubmitStatus::ServiceClosed;
        state.release();
        return ResourceGraphSubmitStatus::Accepted;
    } catch (const std::bad_alloc&) {
        return ResourceGraphSubmitStatus::AllocationFailed;
    } catch (...) {
        return ResourceGraphSubmitStatus::InvalidRequest;
    }
}

} // namespace arp::resource
