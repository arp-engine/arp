#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/io/thread_pool_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "resource/resource_graph_executor.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <optional>
#include <stdexcept>

namespace {
using namespace arp;
using namespace core;
using namespace core::io;
using namespace resource;
constexpr const char* id = "932adff7-c6cb-4aaf-a4c2-a54e1a299f12";
constexpr const char* type = "GraphAdmissionResource";
constexpr std::size_t bytesBudget = 64;

enum class Entry { Executor, ManagerGraph, ManagerTyped };

enum class Action {
    Good,
    NoPlaceholder,
    Unloaded,
    AlreadyReady,
    CancelBeforeProcess,
    RemountBeforeProcess,
    CloseFiles,
    CloseQueue,
    CloseRouter,
    CloseBackend,
    CancelDuringLoad,
    RemountDuringLoad,
    CloseDuringLoad,
    ThrowDuringLoad,
    CallerReject,
    CallerCancelFinal,
    QueueFull,
    AllocationBefore,
    AllocationAfterAdmit,
    LegacyDisabled,
    DecodeRestricted
};

struct Observation {
    std::thread::id owner = std::this_thread::get_id();
    core::SharedPtr<ManagedFileBackend> backend;
    core::SharedPtr<MountedFileBackend> router;
    PoolHandle mount;
    runtime::CompletionQueue* queue = nullptr;
    runtime::CancellationSource cancellation;
    core::SharedPtr<CpuMemoryQuota> quota;
    std::optional<CpuMemoryQuotaLease> lease;
    Action action = Action::Good;
    bool lastRead = false;
    unsigned loads = 0, plans = 0, completions = 0, gates = 0;
    ResourceGraphExecutionResult result;

    void remount()
    {
        assert(router->unmount(mount));
        const auto replacement = router->mount("source", backend);
        assert(replacement && !(replacement == mount));
    }
};

class GraphAdmissionResource final : public ResourceBase {
public:
    explicit GraphAdmissionResource(Observation& observation)
        : ResourceBase({id, type, "candidate", "Contract Complete", {}}), observation_(observation)
    {}

    bool planFileReadIntents(ResourceManager&,
        const ResourceLoadContext& context,
        core::pmr::Vector<ResourceFileReadIntent>& intents,
        core::pmr::String&) const override
    {
        assert(std::this_thread::get_id() == observation_.owner);
        ++observation_.plans;
        if (observation_.action == Action::DecodeRestricted)
            assert(context.maxDecodedBytes == bytesBudget / 2);
        if (!context.sources->find("node.yaml")) {
            ResourceFileReadIntent intent;
            intent.path = "node.yaml";
            intent.pathScope = FilePathScope::MountedResource;
            intent.mount = observation_.mount;
            intent.maxBytes = bytesBudget;
            intents.push_back(std::move(intent));
        } else {
            observation_.lastRead = true;
            if (observation_.action == Action::QueueFull)
                for (std::size_t i = 0; i < observation_.queue->capacity(); ++i)
                    assert(observation_.queue->post({[](void*) {}, nullptr}).accepted());
        }
        return true;
    }

    ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context) override
    {
        assert(std::this_thread::get_id() == observation_.owner);
        ++observation_.loads;
        const auto* source = context.sources->find("node.yaml");
        assert(source && source->bytes.size() == 6 && context.requirePreloadedSources);
        assert(source->pathScope == FilePathScope::MountedResource
            && source->mount == observation_.mount);
        if (observation_.action == Action::DecodeRestricted)
            assert(context.maxDecodedBytes == bytesBudget / 2);
        const auto result = ResourceBase::load(manager, context);
        assert(result.success);
        if (observation_.action == Action::CancelDuringLoad)
            observation_.cancellation.cancel();
        if (observation_.action == Action::RemountDuringLoad)
            observation_.remount();
        if (observation_.action == Action::CloseDuringLoad)
            observation_.backend->shutdown();
        if (observation_.action == Action::ThrowDuringLoad)
            throw std::runtime_error("domain preparation failed after admission");
        if (observation_.action == Action::AllocationAfterAdmit)
            observation_.lease.emplace(observation_.quota,
                observation_.quota->limitBytes() - observation_.quota->liveBytes());
        return result;
    }

private:
    Observation& observation_;
};

core::SharedPtr<MemoryFileBackend> memory()
{
    auto backend =
        makeShared<MemoryFileBackend>(MemoryFileBackendDesc{1, bytesBudget, 2, bytesBudget, 64});
    const std::string_view bytes = "abcdef";
    assert(backend->registerFile(
               "node.yaml", reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size())
        == FileIoStatus::Success);
    return backend;
}

void run(core::SharedPtr<ManagedFileBackend> backend,
    Action action = Action::Good,
    Entry entry = Entry::Executor)
{
    Observation observation;
    observation.action = action;
    observation.backend = std::move(backend);
    [&] {
        observation.router =
            makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 2, bytesBudget, 32, 64});
        observation.mount = observation.router->mount("source", observation.backend);
        assert(observation.mount);
        runtime::AsyncRuntimeDesc desc;
        desc.enableWork = desc.enableThreadPoolFileBackend = false;
        desc.fileBackend = observation.router;
        desc.completionQueueCapacity = desc.completionBudget = 4;
        runtime::AsyncRuntime runtime(desc);
        assert(runtime.valid());
        observation.queue = &runtime.completionQueue();
        observation.quota =
            makeShared<CpuMemoryQuota>(action == Action::AllocationBefore ? 1 : 64 * 1024);
        ResourceManager manager;
        ResourceHandle<ResourceBase> old;
        if (action == Action::Unloaded || action == Action::AlreadyReady) {
            assert(manager
                    .loadResource(makeUnique<ResourceBase>(
                        ResourceReference{id, type, "original", "Contract Complete", {}}))
                    .success);
            old = manager.find<ResourceBase>(id);
            if (action == Action::Unloaded)
                assert(manager.unload(id).success);
        } else if (action != Action::NoPlaceholder)
            assert(manager.declareResource(id, type).success);
        const auto* previous = manager.recordSnapshot(id);
        const auto generation = manager.generationOf(id);
        const auto priorState = previous ? previous->state : ResourceState::Unloaded;
        ResourceGraphExecutionRequest request;
        request.graph.rootResourceId = id;
        ResourceGraphNode node;
        node.resource = makeUnique<GraphAdmissionResource>(observation);
        request.graph.nodes.push_back(std::move(node));
        request.budget = {1, 1, bytesBudget, bytesBudget, 4};
        request.cancellation = observation.cancellation.token();
        if (action == Action::DecodeRestricted)
            request.graph.context.maxDecodedBytes = bytesBudget / 2;
        if (action == Action::CallerReject || action == Action::CallerCancelFinal)
            request.graph.context.commitGate = {[](void* context) noexcept {
                                                    auto& o = *static_cast<Observation*>(context);
                                                    ++o.gates;
                                                    if (o.action == Action::CallerReject)
                                                        return false;
                                                    if (o.gates == 3)
                                                        o.cancellation.cancel();
                                                    return true;
                                                },
                &observation};
        auto executor = action == Action::LegacyDisabled
            ? makeUnique<ResourceGraphExecutor>(
                  manager, runtime.fileService(), runtime.workService(), runtime.completionQueue())
            : makeUnique<ResourceGraphExecutor>(
                  manager, runtime.fileService(), runtime.completionQueue());
        ResourceGraphExecutionCompletion completion{
            [](void* context, ResourceGraphExecutionResult result) {
                auto& o = *static_cast<Observation*>(context);
                assert(std::this_thread::get_id() == o.owner);
                ++o.completions;
                o.result = std::move(result);
            },
            &observation};
        const auto submitted = entry == Entry::ManagerGraph ? manager.loadGraphAsync(runtime,
                                                                  std::move(request.graph),
                                                                  request.budget,
                                                                  completion,
                                                                  request.cancellation)
            : entry == Entry::ManagerTyped                  ? manager.loadAsyncResource(runtime,
                                                 std::move(request.graph.nodes[0].resource),
                                                 request.budget,
                                                 completion,
                                                 request.graph.context,
                                                 request.cancellation)
                                           : executor->submit(std::move(request), completion);
        assert(submitted == ResourceGraphSubmitStatus::Accepted);
        assert(observation.loads == 0 && observation.plans == 0);
        assert(manager.recordSnapshot(id) == previous && manager.generationOf(id) == generation);
        assert(observation.completions == 0);
        // Stop exactly after delivery of the last IO, before the queued owner load.
        unified_io_fixture::wait(
            runtime.completionQueue(),
            [&] {
                return observation.lastRead || observation.completions;
            },
            1);
        if (!observation.lastRead)
            std::fprintf(stderr,
                "owner acquisition action %u status %u: %s / %s\n",
                static_cast<unsigned>(action),
                static_cast<unsigned>(observation.result.fileStatus),
                observation.result.diagnosticId ? observation.result.diagnosticId : "none",
                observation.result.resource.diagnostics.c_str());
        assert(observation.lastRead && observation.loads == 0);
        if (action != Action::QueueFull)
            assert(observation.completions == 0);
        if (action == Action::CancelBeforeProcess)
            observation.cancellation.cancel();
        if (action == Action::RemountBeforeProcess)
            observation.remount();
        if (action == Action::CloseFiles)
            runtime.fileService().shutdown();
        if (action == Action::CloseQueue)
            runtime.completionQueue().shutdown();
        if (action == Action::CloseRouter)
            observation.router->shutdown();
        if (action == Action::CloseBackend)
            observation.backend->shutdown();
        if (action == Action::AllocationBefore || action == Action::AllocationAfterAdmit) {
            CpuMemoryQuotaScope scope(observation.quota);
            runtime.dispatch(1);
        }
        unified_io_fixture::wait(runtime.completionQueue(), [&] {
            return observation.completions != 0;
        });
        observation.lease.reset();
        assert(observation.completions == 1 && observation.result.telemetry.completionCount == 1);
        const bool success = action == Action::Good || action == Action::LegacyDisabled
            || action == Action::DecodeRestricted || action == Action::Unloaded
            || action == Action::NoPlaceholder;
        if (observation.result.resource.success != success)
            std::fprintf(stderr,
                "owner action %u: %s / %s\n",
                static_cast<unsigned>(action),
                observation.result.diagnosticId ? observation.result.diagnosticId : "none",
                observation.result.resource.diagnostics.c_str());
        assert(observation.result.resource.success == success);
        if (success) {
            assert(manager.find<GraphAdmissionResource>(id).ready());
            if (previous)
                assert(manager.recordSnapshot(id) == previous);
            assert(manager.generationOf(id) == generation + 1);
            assert(observation.loads == 1 && observation.result.telemetry.completedNodes == 1);
            assert(manager.resolveAlias("candidate").success);
            if (action == Action::Unloaded) {
                assert(old.stale() && !old.ready() && old.get() == nullptr);
                assert(!manager.resolveAlias("original").success);
            }
        } else {
            assert(
                manager.recordSnapshot(id) == previous && manager.generationOf(id) == generation);
            assert(!previous || previous->state == priorState);
            assert(!manager.resolveAlias("candidate").success);
            if (action == Action::AlreadyReady)
                assert(old.ready() && manager.resolveAlias("original").success);
            if (action == Action::RemountBeforeProcess || action == Action::RemountDuringLoad)
                assert(observation.result.fileStatus == FileIoStatus::StaleLocation);
            if (action == Action::CloseFiles || action == Action::CloseQueue
                || action == Action::CloseRouter || action == Action::CloseBackend
                || action == Action::CloseDuringLoad)
                assert(observation.result.fileStatus == FileIoStatus::ServiceClosed);
            if (action == Action::CancelBeforeProcess || action == Action::CancelDuringLoad
                || action == Action::CallerCancelFinal)
                assert(observation.result.fileStatus == FileIoStatus::Cancelled);
            if (action == Action::QueueFull)
                assert(observation.result.fileStatus == FileIoStatus::QueueFull);
        }
        if (action == Action::CallerCancelFinal)
            assert(observation.gates == 3);
        assert(runtime.workService().telemetry().submitted == 0);
        runtime.shutdown();
        assert(
            runtime.completionQueue().pending() == 0 && runtime.completionQueue().reserved() == 0);
        assert(observation.router->telemetry().inFlight == 0);
        if (auto* http = dynamic_cast<HttpFileBackend*>(observation.backend.get()))
            assert(http->telemetry().inFlight == 0 && http->telemetry().activeNativeTransfers == 0);
    }();
    {
        ResourceGraphExecutionResult discard;
        std::swap(discard, observation.result);
    }
    if (observation.quota->liveBytes())
        std::fprintf(stderr,
            "owner quota action %u retained %llu bytes\n",
            static_cast<unsigned>(action),
            static_cast<unsigned long long>(observation.quota->liveBytes()));
    assert(observation.quota->liveBytes() == 0);
}

core::SharedPtr<HttpFileBackend> http(const char* url)
{
    HttpFileBackendDesc desc;
    desc.baseUrl = url;
    desc.maxInFlight = 2;
    // Includes HTTP headers, URI/parser state and transfer metadata as well
    // as the separately bounded six-byte fixture payload.
    desc.maxBytesInFlight = 64 * 1024;
    desc.maxPathBytes = 64;
    desc.maxHeaderBytes = 4096;
    desc.ioChunkBytes = 64;
    desc.timeoutMs = 3000;
    desc.cancellationPollMs = 2;
    return makeShared<HttpFileBackend>(desc);
}

void metadataOnly(bool close)
{
    runtime::CompletionQueue queue({2});
    NullFileBackend backend;
    FileService files(queue, backend);
    ResourceManager manager;
    ResourceGraphExecutor executor(manager, files, queue);
    ResourceGraphExecutionRequest request;
    request.graph.rootResourceId = id;
    ResourceGraphNode node;
    node.resource = makeUnique<ResourceBase>(
        ResourceReference{id, "MetadataOnly", {}, "Contract Complete", {}});
    request.graph.nodes.push_back(std::move(node));
    request.budget = {1, 1, bytesBudget, bytesBudget, 2};
    Observation probe;
    assert(executor.submit(std::move(request),
               {[](void* context, ResourceGraphExecutionResult result) {
                    auto& o = *static_cast<Observation*>(context);
                    ++o.completions;
                    o.result = std::move(result);
                },
                   &probe})
        == ResourceGraphSubmitStatus::Accepted);
    queue.dispatch(1);
    assert(probe.completions == 0 && !manager.recordSnapshot(id));
    if (close)
        files.shutdown();
    queue.dispatch(1);
    assert(probe.completions == 1 && probe.result.resource.success == !close);
    assert(probe.result.telemetry.submittedFileReads == 0
        && probe.result.telemetry.locationValidations == 0);
    if (close)
        assert(
            probe.result.fileStatus == FileIoStatus::ServiceClosed && !manager.recordSnapshot(id));
    files.shutdown();
    queue.shutdown();
}

void pureClosedBackends()
{
    unified_io_fixture::Workspace workspace;
    core::pmr::Vector<core::SharedPtr<ManagedFileBackend>> backends{memory(),
        makeShared<NativeDirectoryFileBackend>(workspace.descriptor()),
        http("http://127.0.0.1:1/")};
    for (auto& backend : backends) {
        auto router =
            makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 2, bytesBudget, 32, 64});
        FileReadRequest request;
        request.contractVersion = 1;
        request.path = "node.yaml";
        request.pathScope = FilePathScope::MountedResource;
        request.maxBytes = bytesBudget;
        request.mount = router->mount("source", backend);
        assert(router->validateRead(request).status == FileIoStatus::Success);
        backend->shutdown();
        assert(backend->validateRead(request).status == FileIoStatus::ServiceClosed);
        assert(router->validateRead(request).status == FileIoStatus::ServiceClosed);
    }
    runtime::CompletionQueue queue({2});
    runtime::WorkService work({&queue, 1, 1, true, true});
    assert(!work.closed());
    work.shutdown();
    assert(work.closed());
    ThreadPoolFallbackFileBackend legacy(work);
    FileReadRequest request;
    request.path = core::FilePath::utf8(workspace.root / "a.bin");
    request.pathScope = FilePathScope::HostFilesystem;
    request.contractVersion = 1;
    request.maxBytes = bytesBudget;
    assert(legacy.validateRead(request).status == FileIoStatus::ServiceClosed);
}

void ownerSubmission()
{
#if !defined(__EMSCRIPTEN__)
    runtime::CompletionQueue queue({2});
    NullFileBackend backend;
    FileService files(queue, backend);
    ResourceManager manager;
    ResourceGraphExecutor executor(manager, files, queue);
    ResourceGraphExecutionRequest request;
    request.graph.rootResourceId = id;
    ResourceGraphNode node;
    node.resource = makeUnique<ResourceBase>(
        ResourceReference{id, "MetadataOnly", {}, "Contract Complete", {}});
    request.graph.nodes.push_back(std::move(node));
    request.budget = {1, 1, bytesBudget, bytesBudget, 2};
    auto status = ResourceGraphSubmitStatus::Accepted;
    unsigned callbacks = 0;
    std::thread other([&] {
        status = executor.submit(std::move(request),
            {[](void* context, ResourceGraphExecutionResult) {
                 ++*static_cast<unsigned*>(context);
             },
                &callbacks});
    });
    other.join();
    assert(status == ResourceGraphSubmitStatus::InvalidRequest && callbacks == 0);
    assert(!manager.recordSnapshot(id) && queue.pending() == 0 && queue.reserved() == 0);
#endif
}

void managerSubmissionContracts()
{
    runtime::AsyncRuntimeDesc desc;
    desc.enableWork = desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = memory();
    desc.completionQueueCapacity = desc.completionBudget = 2;
    runtime::AsyncRuntime runtime(desc);
    assert(runtime.valid());
    ResourceManager manager;
    Observation observation;
    // native_reference(ResourceBase*) registers these immutable DB entries on
    // first use. Warm that known process-owned metadata, then measure rejection.
    (void)reflection::class_of<ResourceBase>();
    (void)reflection::type_of<ResourceBase*>();
    const auto before = defaultCpuAllocator().stats();
    assert(manager.loadAsync<GraphAdmissionResource>(runtime.workService(), {}, {}, observation)
        == runtime::WorkServiceStatus::InvalidWorkItem);
    assert(manager.loadAsyncResource(runtime.workService(), {}, {})
        == runtime::WorkServiceStatus::InvalidWorkItem);
    assert(manager.loadGraphAsync(runtime.workService(), {}, {})
        == runtime::WorkServiceStatus::InvalidWorkItem);
    assert(defaultCpuAllocator().stats().allocationCount == before.allocationCount);
    assert(manager.loadAsyncResource(
               runtime.workService(), makeUnique<GraphAdmissionResource>(observation), {})
        == runtime::WorkServiceStatus::InvalidWorkItem);
    assert(defaultCpuAllocator().stats().liveBytes == before.liveBytes);
    const ResourceGraphExecutionCompletion completion{
        [](void* user, ResourceGraphExecutionResult result) {
            auto& observation = *static_cast<Observation*>(user);
            ++observation.completions;
            observation.result = std::move(result);
        },
        &observation};
    const ResourceGraphExecutionBudget budget{1, 1, bytesBudget, bytesBudget, 2};
    ResourceSourceSet borrowed;
    ResourceLoadContext context;
    context.sources = &borrowed;
    assert(
        manager.loadAsync<GraphAdmissionResource>(runtime, budget, completion, context, observation)
        == ResourceGraphSubmitStatus::InvalidRequest);
    auto invalid = budget;
    invalid.maxSourceBytes = 0;
    assert(manager.loadAsync<GraphAdmissionResource>(runtime, invalid, completion, {}, observation)
        == ResourceGraphSubmitStatus::InvalidRequest);
    auto quota = makeShared<CpuMemoryQuota>(1);
    {
        CpuMemoryQuotaScope scope(quota);
        assert(
            manager.loadAsync<GraphAdmissionResource>(runtime, budget, completion, {}, observation)
            == ResourceGraphSubmitStatus::AllocationFailed);
    }
    assert(quota->liveBytes() == 0);
#if !defined(__EMSCRIPTEN__)
    std::thread other([&] {
        assert(!manager.isOwnerThread());
        assert(
            manager.loadAsync<GraphAdmissionResource>(runtime, budget, completion, {}, observation)
            == ResourceGraphSubmitStatus::InvalidRequest);
        ResourceGraphExecutor wrong(manager, runtime.fileService(), runtime.completionQueue());
        ResourceGraphExecutionRequest request;
        request.budget = budget;
        request.graph.rootResourceId = id;
        ResourceGraphNode node;
        node.resource = makeUnique<ResourceBase>(
            ResourceReference{id, "MetadataOnly", {}, "Contract Complete", {}});
        request.graph.nodes.push_back(std::move(node));
        assert(wrong.submit(std::move(request), completion)
            == ResourceGraphSubmitStatus::InvalidRequest);
    });
    other.join();
#endif
    assert(observation.completions == 0 && !manager.recordSnapshot(id));
    assert(runtime.completionQueue().pending() == 0 && runtime.completionQueue().reserved() == 0);
    assert(runtime.workService().telemetry().submitted == 0);
    for (unsigned i = 0; i < 2; ++i)
        assert(runtime.completionQueue().post({[](void*) {}, nullptr}).accepted());
    assert(manager.loadAsync<GraphAdmissionResource>(runtime, budget, completion, {}, observation)
        == ResourceGraphSubmitStatus::QueueFull);
    runtime.dispatch();
    assert(observation.completions == 0 && !manager.recordSnapshot(id));
    runtime::CancellationSource cancelled;
    cancelled.cancel();
    assert(manager.loadAsyncResource(runtime,
               makeUnique<GraphAdmissionResource>(observation),
               budget,
               completion,
               {},
               cancelled.token())
        == ResourceGraphSubmitStatus::Accepted);
    assert(observation.completions == 0);
    runtime.dispatch();
    assert(observation.completions == 1 && !observation.result.resource.success);
    assert(observation.result.fileStatus == FileIoStatus::Cancelled);
    assert(core::StringView(observation.result.diagnosticId) == "ARP-RESOURCE-GRAPH-CANCELLED");
    assert(observation.result.telemetry.submittedFileReads == 0 && observation.loads == 0);
    assert(!manager.recordSnapshot(id));
    runtime.shutdown();
    assert(manager.loadAsync<GraphAdmissionResource>(runtime, budget, completion, {}, observation)
        == ResourceGraphSubmitStatus::ServiceClosed);
}

class BudgetMetadataResource final : public ResourceBase {
public:
    BudgetMetadataResource(
        const char* resourceId, const char* resourceType, bool removeRoot = false)
        : ResourceBase({resourceId, resourceType, {}, "Contract Complete", {}}),
          removeRoot_(removeRoot)
    {}

    ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context) override
    {
        assert(context.maxDecodedBytes == 8);
        auto result = ResourceBase::load(manager, context);
        if (removeRoot_)
            assert(manager.unload(id).success);
        return result;
    }

private:
    bool removeRoot_;
};

void managerGraphContracts()
{
    constexpr const char* otherId = "932adff7-c6cb-4aaf-a4c2-a54e1a299f13";
    runtime::AsyncRuntimeDesc config;
    config.enableWork = config.enableThreadPoolFileBackend = false;
    config.fileBackend = memory();
    config.completionQueueCapacity = config.completionBudget = 4;
    runtime::AsyncRuntime runtime(config);
    assert(runtime.valid());
    ResourceManager manager;
    const auto graph = [otherId](bool removeRoot = false) {
        ResourceGraphRequest graph;
        graph.rootResourceId = id;
        ResourceGraphNode root, other;
        root.resource = makeUnique<BudgetMetadataResource>(id, "RootBudget");
        other.resource = makeUnique<BudgetMetadataResource>(otherId, "OtherBudget", removeRoot);
        graph.nodes.push_back(std::move(root));
        graph.nodes.push_back(std::move(other));
        return graph;
    };
    Observation observation;
    const ResourceGraphExecutionCompletion completion{
        [](void* context, ResourceGraphExecutionResult result) {
            auto& o = *static_cast<Observation*>(context);
            ++o.completions;
            o.result = std::move(result);
        },
        &observation};
    const ResourceGraphExecutionBudget budget{2, 1, bytesBudget, bytesBudget, 4};
    ResourceBudgetConfig limits;
    limits.maxGraphNodes = 1;
    manager.setBudgetConfig(limits);
    assert(manager.loadGraphAsync(runtime, graph(), budget, completion)
        == ResourceGraphSubmitStatus::NodeBudgetExceeded);
    limits.maxGraphNodes = 0;
    limits.maxCompletionDispatchCount = 1;
    manager.setBudgetConfig(limits);
    assert(manager.loadGraphAsync(runtime, graph(), budget, completion)
        == ResourceGraphSubmitStatus::InvalidRequest);
    assert(!manager.recordSnapshot(id) && observation.completions == 0);
    limits.maxCompletionDispatchCount = 0;
    limits.maxDecodedBytes = 8;
    manager.setBudgetConfig(limits);
    assert(manager.loadGraphAsync(runtime, graph(), budget, completion)
        == ResourceGraphSubmitStatus::Accepted);
    unified_io_fixture::wait(runtime.completionQueue(), [&] {
        return observation.completions == 1;
    });
    if (!observation.result.resource.success)
        std::fprintf(stderr,
            "Manager independent root: %s / %s\n",
            observation.result.resource.diagnostics.c_str(),
            observation.result.diagnosticId ? observation.result.diagnosticId : "none");
    assert(observation.result.resource.success);
    assert(observation.result.resource.resourceId == id);
    assert(observation.result.resource.resourceType == "RootBudget");
    assert(manager.stateOf(id) == ResourceState::Ready
        && manager.stateOf(otherId) == ResourceState::Ready);
    assert(manager.unload(id).success && manager.unload(otherId).success);
    assert(manager.loadGraphAsync(runtime, graph(true), budget, completion)
        == ResourceGraphSubmitStatus::Accepted);
    unified_io_fixture::wait(runtime.completionQueue(), [&] {
        return observation.completions == 2;
    });
    assert(!observation.result.resource.success && observation.result.resource.resourceId == id);
    assert(
        core::StringView(observation.result.diagnosticId) == "ARP-RESOURCE-GRAPH-ROOT-NOT-READY");
    assert(manager.stateOf(id) == ResourceState::Unloaded
        && manager.stateOf(otherId) == ResourceState::Ready);
    runtime.shutdown();
}

void managerContracts(bool quick, const char* url = nullptr)
{
    if (!url) {
        managerSubmissionContracts();
        managerGraphContracts();
    }
    for (const auto entry : {Entry::ManagerGraph, Entry::ManagerTyped}) {
        if (url) {
            for (const auto action : {Action::RemountBeforeProcess,
                     Action::CancelBeforeProcess,
                     Action::CloseBackend,
                     Action::RemountDuringLoad,
                     Action::CancelDuringLoad,
                     Action::CloseDuringLoad})
                run(http(url), action, entry);
        } else {
            for (const auto action : {Action::Good,
                     Action::LegacyDisabled,
                     Action::NoPlaceholder,
                     Action::Unloaded,
                     Action::AlreadyReady,
                     Action::CancelBeforeProcess,
                     Action::RemountBeforeProcess,
                     Action::CloseFiles,
                     Action::CloseQueue,
                     Action::CloseRouter,
                     Action::CloseBackend,
                     Action::CancelDuringLoad,
                     Action::RemountDuringLoad,
                     Action::CloseDuringLoad,
                     Action::ThrowDuringLoad,
                     Action::CallerReject,
                     Action::CallerCancelFinal,
                     Action::QueueFull,
                     Action::AllocationBefore,
                     Action::AllocationAfterAdmit})
                run(memory(), action, entry);
        }
        if (!url)
            run(memory(), Action::DecodeRestricted, entry);
        if (quick)
            continue;
        for (unsigned i = 0; i < 20; ++i)
            run(url ? core::SharedPtr<ManagedFileBackend>(http(url)) : memory(),
                Action::Good,
                entry);
        const auto before = defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            run(url ? core::SharedPtr<ManagedFileBackend>(http(url)) : memory(),
                Action::Good,
                entry);
        const auto after = defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::printf(
            "Manager owner admission entry=%u core bytes %llu -> %llu; live count delta 0\n",
            static_cast<unsigned>(entry),
            static_cast<unsigned long long>(before.liveBytes),
            static_cast<unsigned long long>(after.liveBytes));
    }
    std::puts("ResourceAsyncOwnerAdmission/1: graph/typed owner gates PASS");
}
} // namespace

int main(int argc, char** argv)
{
    if ((argc == 2 || argc == 4) && core::StringView(argv[1]) == "--manager") {
        assert(argc == 2 || core::StringView(argv[2]) == "--http");
        managerContracts(false, argc == 4 ? argv[3] : nullptr);
        return 0;
    }
    if (argc == 2 && core::StringView(argv[1]) == "--manager-quick") {
        managerContracts(true);
        return 0;
    }

    if (argc == 3 && core::StringView(argv[1]) == "--http") {
        for (auto action : {Action::RemountBeforeProcess,
                 Action::CancelBeforeProcess,
                 Action::CloseBackend,
                 Action::RemountDuringLoad,
                 Action::CancelDuringLoad,
                 Action::CloseDuringLoad})
            run(http(argv[2]), action);
        for (unsigned i = 0; i < 20; ++i)
            run(http(argv[2]));
        const auto before = defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            run(http(argv[2]));
        const auto after = defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::puts("Owner-admission HTTP 20 warmup + 20 ordinary cycles: core bytes/count delta 0");
        return 0;
    }
    metadataOnly(false);
    metadataOnly(true);
    pureClosedBackends();
    ownerSubmission();
    for (auto action : {Action::Good,
             Action::LegacyDisabled,
             Action::NoPlaceholder,
             Action::Unloaded,
             Action::AlreadyReady,
             Action::CancelBeforeProcess,
             Action::RemountBeforeProcess,
             Action::CloseFiles,
             Action::CloseQueue,
             Action::CloseRouter,
             Action::CloseBackend,
             Action::CancelDuringLoad,
             Action::RemountDuringLoad,
             Action::CloseDuringLoad,
             Action::ThrowDuringLoad,
             Action::CallerReject,
             Action::CallerCancelFinal,
             Action::QueueFull,
             Action::AllocationBefore,
             Action::AllocationAfterAdmit})
        run(memory(), action);
    for (unsigned i = 0; i < 20; ++i)
        run(memory());
    const auto before = defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        run(memory());
    const auto after = defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::puts(
        "Owner-admission contracts PASS; 20 warmup + 20 ordinary cycles core bytes/count delta 0");
}
