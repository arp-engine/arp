#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "resource/resource_graph_executor.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

class FileBackedGraphResource final : public arp::resource::ResourceBase {
public:
    FileBackedGraphResource(arp::resource::ResourceReference reference,
        std::filesystem::path path,
        arp::resource::ResourceGraphStage processingStage,
        arp::core::PoolHandle mount = {},
        arp::core::io::FilePathScope scope = arp::core::io::FilePathScope::HostFilesystem,
        std::size_t sourceBudget = 1024,
        arp::core::io::FileVersionToken expected = {})
        : arp::resource::ResourceBase(std::move(reference)), path_(std::move(path)),
          processingStage_(processingStage), mount_(mount), scope_(scope),
          sourceBudget_(sourceBudget), expected_(expected)
    {}

    bool collectFileReadIntents(arp::resource::ResourceManager&,
        arp::core::pmr::Vector<arp::resource::ResourceFileReadIntent>& intents,
        arp::core::pmr::String&) const override
    {
        arp::resource::ResourceFileReadIntent intent;
        intent.pathScope = scope_;
        intent.mount = mount_;
        intent.expectedVersion = expected_;
        intent.maxVersionBytes =
            expected_.kind == arp::core::io::FileVersionKind::None ? 0 : sourceBudget_;
        intent.path = path_.generic_string().c_str();
        intent.role = text("contract-yaml");
        intent.maxBytes = sourceBudget_;
        intents.push_back(std::move(intent));
        return true;
    }

    arp::resource::ResourceGraphStage processingStage() const noexcept override
    {
        return processingStage_;
    }

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& manager,
        const arp::resource::ResourceLoadContext& context) override
    {
        arp::resource::ResourceLoadResult result;
        if (context.sources == nullptr || !context.requirePreloadedSources) {
            result.diagnostics = text("graph resource requires preloaded source");
            return result;
        }
        const auto* source = context.sources->find(path_.generic_string().c_str());
        if (source == nullptr || source->bytes.empty() || source->pathScope != scope_
            || !(source->mount == mount_)
            || (expected_.kind != arp::core::io::FileVersionKind::None
                && source->version != expected_)) {
            result.diagnostics = text("graph resource preloaded source is missing");
            return result;
        }
        processedBytes_ = source->bytes.size();
        return manager.admitResourceMetadata(*this, context.policy);
    }

    std::size_t processedByteCount() const noexcept override { return processedBytes_; }

private:
    std::filesystem::path path_;
    arp::resource::ResourceGraphStage processingStage_;
    arp::core::PoolHandle mount_;
    arp::core::io::FilePathScope scope_;
    std::size_t sourceBudget_ = 0;
    arp::core::io::FileVersionToken expected_;
    std::size_t processedBytes_ = 0u;
};

class FailingGraphResource final : public arp::resource::ResourceBase {
public:
    explicit FailingGraphResource(arp::resource::ResourceReference reference)
        : arp::resource::ResourceBase(std::move(reference))
    {}

    arp::resource::ResourceLoadResult load(
        arp::resource::ResourceManager&, const arp::resource::ResourceLoadContext&) override
    {
        arp::resource::ResourceLoadResult result;
        result.resourceId = reference().id;
        result.resourceType = reference().type;
        result.diagnostics = text("contract dependency processing failed");
        return result;
    }
};

struct Probe {
    arp::resource::ResourceGraphExecutionResult result;
    std::size_t calls = 0u;
};

void recordResult(void* userData, arp::resource::ResourceGraphExecutionResult result)
{
    auto* probe = static_cast<Probe*>(userData);
    probe->result = std::move(result);
    ++probe->calls;
}

arp::resource::ResourceGraphRequest requestFor(const std::filesystem::path& path,
    const char* id,
    arp::resource::ResourceGraphStage stage,
    arp::core::PoolHandle mount = {},
    arp::core::io::FilePathScope scope = arp::core::io::FilePathScope::HostFilesystem,
    std::size_t sourceBudget = 1024,
    arp::core::io::FileVersionToken expected = {})
{
    arp::resource::ResourceReference reference;
    reference.id = text(id);
    reference.type = text("FileBackedGraphResource");
    reference.alias = text(id);
    reference.status = text("Prototype Complete");
    arp::resource::ResourceGraphNode node;
    node.resource = arp::core::makeUnique<FileBackedGraphResource>(
        reference, path, stage, mount, scope, sourceBudget, expected);
    arp::resource::ResourceGraphRequest request;
    request.rootResourceId = reference.id;
    request.nodes.push_back(std::move(node));
    return request;
}

arp::resource::ResourceGraphRequest dependencyFailureRequest(const std::filesystem::path& path)
{
    constexpr const char* leafId = "db99277a-73c1-4e55-8466-8a4669dc2cd4";
    constexpr const char* rootId = "4f7871ef-198f-42e5-bf97-6b7553fd8f16";
    auto request = requestFor(path, leafId, arp::resource::ResourceGraphStage::YamlParse);
    request.rootResourceId = text(rootId);

    arp::resource::ResourceReference rootReference;
    rootReference.id = text(rootId);
    rootReference.type = text("FailingGraphResource");
    rootReference.alias = text(rootId);
    rootReference.status = text("Prototype Complete");
    arp::resource::ResourceGraphNode root;
    root.resource = arp::core::makeUnique<FailingGraphResource>(rootReference);
    root.dependencies.push_back({text(leafId), text("contract.leaf")});
    request.nodes.push_back(std::move(root));
    return request;
}

bool hasStage(const arp::resource::ResourceGraphExecutionTelemetry& telemetry,
    arp::resource::ResourceGraphStage stage)
{
    for (const auto observed : telemetry.stageOrder) {
        if (observed == stage) {
            return true;
        }
    }
    return false;
}

void dispatchUntil(arp::core::runtime::AsyncRuntime& runtime, Probe& probe)
{
    for (int index = 0; index < 200 && probe.calls == 0u; ++index) {
        runtime.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void noopCompletion(void*) noexcept
{}

void mountedContracts()
{
    using namespace arp::core;
    using namespace arp::core::io;
    using namespace arp::resource;
    auto memory = makeShared<MemoryFileBackend>(MemoryFileBackendDesc{1, 64, 4, 256, 64});
    const auto bytes = std::string_view("abcdef");
    assert(memory->registerFile(
               "node.yaml", reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size())
        == FileIoStatus::Success);
    auto router = makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 4, 256, 16, 64});
    const auto mount = router->mount("workspace", memory);
    runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = 8;
    desc.completionBudget = 8;
    desc.workerCount = 1;
    desc.workQueueCapacity = 4;
    desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = router;
    runtime::AsyncRuntime runtime(desc);
    ResourceManager manager;
    ResourceGraphExecutor executor(
        manager, runtime.fileService(), runtime.workService(), runtime.completionQueue());
    const auto run = [&](ResourceGraphExecutionRequest request, bool success) {
        Probe probe;
        assert(executor.submit(std::move(request), {recordResult, &probe})
            == ResourceGraphSubmitStatus::Accepted);
        assert(probe.calls == 0);
        dispatchUntil(runtime, probe);
        assert(probe.calls == 1 && probe.result.resource.success == success);
        assert(probe.result.telemetry.completionCount == 1);
        return std::move(probe.result);
    };
    ResourceGraphExecutionRequest good;
    Sha256 hash;
    hash.update(bytes);
    const auto expected = FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256);
    good.graph = requestFor("node.yaml",
        "147091c0-2269-493c-a511-55d876838f5f",
        ResourceGraphStage::TypedProcessing,
        mount,
        FilePathScope::MountedResource,
        64,
        expected);
    good.budget = {1, 1, 64, 64, 8};
    auto loaded = run(std::move(good), true);
    assert(loaded.telemetry.submittedFileReads == 1 && loaded.telemetry.bytesRead == 6);
    const auto* rejectedId = "b1b17037-66e4-4bbb-8b31-86c22f1b5f6a";
    ResourceGraphExecutionRequest noMemory;
    noMemory.graph = requestFor("node.yaml",
        rejectedId,
        ResourceGraphStage::TypedProcessing,
        mount,
        FilePathScope::MountedResource,
        64);
    noMemory.budget = {1, 1, 64, 64, 8};
    Probe rejected;
    auto quota = makeShared<CpuMemoryQuota>(1);
    {
        CpuMemoryQuotaScope scope(quota);
        assert(executor.submit(std::move(noMemory), {recordResult, &rejected})
            == ResourceGraphSubmitStatus::AllocationFailed);
    }
    assert(rejected.calls == 0 && quota->liveBytes() == 0);
    assert(runtime.completionQueue().pending() == 0 && runtime.completionQueue().reserved() == 0);
    ResourceGraphExecutionRequest acceptedFailure;
    acceptedFailure.graph = requestFor("node.yaml",
        rejectedId,
        ResourceGraphStage::TypedProcessing,
        mount,
        FilePathScope::MountedResource,
        64);
    acceptedFailure.budget = {1, 1, 64, 64, 8};
    Probe allocation;
    assert(executor.submit(std::move(acceptedFailure), {recordResult, &allocation})
        == ResourceGraphSubmitStatus::Accepted);
    assert(allocation.calls == 0);
    {
        CpuMemoryQuotaScope scope(quota);
        runtime.dispatch();
    }
    assert(allocation.calls == 1 && !allocation.result.resource.success);
    assert(allocation.result.diagnosticId
        && std::string_view(allocation.result.diagnosticId) == "ARP-RESOURCE-GRAPH-ALLOCATION");
    assert(quota->liveBytes() == 0 && manager.recordSnapshot(rejectedId) == nullptr);
    ResourceSourceSet sources;
    ResourceSourceBuffer first;
    first.path = "source";
    first.bytes.assign(bytes.begin(), bytes.end());
    bool refused = false;
    {
        CpuMemoryQuotaScope scope(quota);
        try {
            sources.add(std::move(first));
        } catch (const std::bad_alloc&) {
            refused = true;
        }
    }
    assert(refused && sources.totalBytes() == 0 && !sources.find("source"));
    auto wrong = expected;
    wrong.digest[0] ^= 1;
    ResourceGraphExecutionRequest changed;
    changed.graph = requestFor("node.yaml",
        "2706fdd9-340c-4350-b8c0-c43ae7b986cc",
        ResourceGraphStage::TypedProcessing,
        mount,
        FilePathScope::MountedResource,
        64,
        wrong);
    changed.budget = {1, 1, 64, 64, 8};
    auto mismatch = run(std::move(changed), false);
    assert(mismatch.fileStatus == FileIoStatus::VersionMismatch);
    assert(manager.recordSnapshot("2706fdd9-340c-4350-b8c0-c43ae7b986cc") == nullptr);

    ResourceGraphExecutionRequest tooSmall;
    tooSmall.graph = requestFor("node.yaml",
        "269090d6-25ee-40bd-bafa-d7501e12762e",
        ResourceGraphStage::TypedProcessing,
        mount,
        FilePathScope::MountedResource,
        1);
    tooSmall.budget = {1, 1, 64, 64, 8};
    auto failed = run(std::move(tooSmall), false);
    assert(failed.fileStatus == FileIoStatus::BudgetExceeded);
    assert(failed.telemetry.bytesRead == 0
        && manager.recordSnapshot("269090d6-25ee-40bd-bafa-d7501e12762e") == nullptr);

    for (const bool readCountBudget : {false, true}) {
        const auto* leaf = readCountBudget ? "3e46480c-58cc-41a7-815f-42e9884d6c93"
                                           : "9d94b82d-2c02-4bd0-9511-f9c889c7523f";
        const auto* root = readCountBudget ? "987d2215-af7a-4484-a7f3-e7b2e3b0c98e"
                                           : "3d2668f4-65f6-4ef2-8cce-942d6c530494";
        ResourceGraphExecutionRequest graph;
        graph.graph = requestFor("node.yaml",
            leaf,
            ResourceGraphStage::TypedProcessing,
            mount,
            FilePathScope::MountedResource,
            64);
        auto parent = requestFor("node.yaml",
            root,
            ResourceGraphStage::TypedProcessing,
            mount,
            FilePathScope::MountedResource,
            64);
        parent.nodes.front().dependencies.push_back({text(leaf), text("source")});
        graph.graph.nodes.push_back(std::move(parent.nodes.front()));
        graph.graph.rootResourceId = text(root);
        graph.budget = {2, readCountBudget ? 1u : 2u, readCountBudget ? 64u : 6u, 64, 8};
        auto bounded = run(std::move(graph), false);
        assert(bounded.telemetry.submittedFileReads == 1 && bounded.telemetry.bytesRead == 6);
    }

    ResourceGraphExecutionRequest stale;
    stale.graph = requestFor("node.yaml",
        "f007c4f4-de2b-4e2b-b7ec-e57973cd09a6",
        ResourceGraphStage::TypedProcessing,
        mount,
        FilePathScope::MountedResource,
        64);
    stale.budget = {1, 1, 64, 64, 8};
    Probe staleProbe;
    assert(executor.submit(std::move(stale), {recordResult, &staleProbe})
        == ResourceGraphSubmitStatus::Accepted);
    assert(router->unmount(mount));
    const auto replacement = router->mount("workspace", memory);
    assert(replacement && !(replacement == mount));
    dispatchUntil(runtime, staleProbe);
    assert(staleProbe.calls == 1 && !staleProbe.result.resource.success);
    assert(staleProbe.result.fileStatus == FileIoStatus::StaleLocation);
    assert(staleProbe.result.telemetry.submittedFileReads == 0);
    runtime.shutdown();
    assert(router->telemetry().inFlight == 0 && runtime.completionQueue().reserved() == 0);
}

} // namespace

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "arp_resource_graph_executor";
    std::filesystem::create_directories(root);
    const auto yamlPath = root / "node.yaml";
    {
        std::ofstream output(yamlPath, std::ios::binary);
        output << "type: contract\ncontractVersion: contract/v1\n";
    }

    arp::core::runtime::AsyncRuntimeDesc runtimeDesc;
    runtimeDesc.completionQueueCapacity = 16u;
    runtimeDesc.workerCount = 1u;
    runtimeDesc.workQueueCapacity = 4u;
    runtimeDesc.completionBudget = 4u;
    arp::core::runtime::AsyncRuntime runtime(runtimeDesc);
    arp::resource::ResourceManager manager;
    arp::resource::ResourceGraphExecutor executor(
        manager, runtime.fileService(), runtime.workService(), runtime.completionQueue());

    arp::resource::ResourceGraphExecutionRequest request;
    request.graph = requestFor(yamlPath,
        "b54af9b4-c2e6-4fc1-968d-0ac387a63da1",
        arp::resource::ResourceGraphStage::YamlParse);
    request.budget = {2u, 2u, 1024u, 1024u, 4u};

    Probe probe;
    assert(executor.submit(std::move(request), {&recordResult, &probe})
        == arp::resource::ResourceGraphSubmitStatus::Accepted);
    assert(probe.calls == 0u);
    dispatchUntil(runtime, probe);
    assert(probe.calls == 1u);
    assert(probe.result.resource.success);
    assert(hasStage(probe.result.telemetry, arp::resource::ResourceGraphStage::Validation));
    assert(hasStage(probe.result.telemetry, arp::resource::ResourceGraphStage::FileRead));
    assert(hasStage(probe.result.telemetry, arp::resource::ResourceGraphStage::YamlParse));
    assert(hasStage(probe.result.telemetry, arp::resource::ResourceGraphStage::Admission));
    assert(hasStage(probe.result.telemetry, arp::resource::ResourceGraphStage::Completion));
    assert(probe.result.telemetry.bytesRead > 0u);
    assert(probe.result.telemetry.completedNodes == 1u);
    assert(probe.result.telemetry.completionCount == 1u);
    assert(probe.result.telemetry.totalDurationNs > 0u);

    arp::resource::ResourceGraphExecutionRequest decode;
    decode.graph = requestFor(yamlPath,
        "315b472b-85d8-4888-89db-6340b9c4528d",
        arp::resource::ResourceGraphStage::ImageDecode);
    decode.budget = {2u, 2u, 1024u, 1024u, 4u};
    Probe decodeProbe;
    assert(executor.submit(std::move(decode), {&recordResult, &decodeProbe})
        == arp::resource::ResourceGraphSubmitStatus::Accepted);
    dispatchUntil(runtime, decodeProbe);
    assert(decodeProbe.calls == 1u);
    assert(decodeProbe.result.resource.success);
    assert(hasStage(decodeProbe.result.telemetry, arp::resource::ResourceGraphStage::ImageDecode));
    assert(decodeProbe.result.telemetry.bytesDecoded > 0u);

    arp::resource::ResourceGraphExecutionRequest dependencyFailure;
    dependencyFailure.graph = dependencyFailureRequest(yamlPath);
    dependencyFailure.budget = {4u, 2u, 1024u, 1024u, 4u};
    Probe dependencyProbe;
    assert(executor.submit(std::move(dependencyFailure), {&recordResult, &dependencyProbe})
        == arp::resource::ResourceGraphSubmitStatus::Accepted);
    dispatchUntil(runtime, dependencyProbe);
    assert(dependencyProbe.calls == 1u);
    assert(!dependencyProbe.result.resource.success);
    assert(dependencyProbe.result.resource.diagnostics == "contract dependency processing failed");
    assert(dependencyProbe.result.telemetry.dependencyFailures == 1u);

    arp::resource::ResourceGraphExecutionRequest overBudget;
    overBudget.graph = requestFor(yamlPath,
        "1c94db4e-4707-4d9a-b9eb-b5a67fd3301e",
        arp::resource::ResourceGraphStage::ImageDecode);
    overBudget.budget.maxNodes = 0u;
    Probe budgetProbe;
    assert(executor.submit(std::move(overBudget), {&recordResult, &budgetProbe})
        == arp::resource::ResourceGraphSubmitStatus::NodeBudgetExceeded);
    assert(budgetProbe.calls == 0u);

    arp::core::runtime::CancellationSource cancellation;
    cancellation.cancel();
    arp::resource::ResourceGraphExecutionRequest cancelled;
    cancelled.graph = requestFor(yamlPath,
        "0bf5931e-32f8-486a-a825-25aa5ad217ef",
        arp::resource::ResourceGraphStage::YamlParse);
    cancelled.budget = {2u, 2u, 1024u, 1024u, 4u};
    cancelled.cancellation = cancellation.token();
    Probe cancelledProbe;
    assert(executor.submit(std::move(cancelled), {&recordResult, &cancelledProbe})
        == arp::resource::ResourceGraphSubmitStatus::Accepted);
    assert(cancelledProbe.calls == 0u);
    dispatchUntil(runtime, cancelledProbe);
    assert(cancelledProbe.calls == 1u);
    assert(!cancelledProbe.result.resource.success);
    assert(cancelledProbe.result.resource.diagnostics
        == "resource graph execution cancelled before file acquisition");

    runtime.shutdown(arp::core::runtime::AsyncRuntimeShutdownPolicy::Drain);

    arp::resource::ResourceGraphExecutionRequest closed;
    closed.graph = requestFor(yamlPath,
        "f0130fb1-b67e-4371-9db0-67f9dd2c0f80",
        arp::resource::ResourceGraphStage::YamlParse);
    closed.budget = {2u, 2u, 1024u, 1024u, 4u};
    Probe closedProbe;
    assert(executor.submit(std::move(closed), {&recordResult, &closedProbe})
        == arp::resource::ResourceGraphSubmitStatus::ServiceClosed);
    assert(closedProbe.calls == 0u);

    arp::core::runtime::AsyncRuntimeDesc fullDesc;
    fullDesc.completionQueueCapacity = 1u;
    fullDesc.workerCount = 1u;
    fullDesc.workQueueCapacity = 1u;
    fullDesc.completionBudget = 1u;
    arp::core::runtime::AsyncRuntime fullRuntime(fullDesc);
    assert(fullRuntime.completionQueue().post({&noopCompletion, nullptr}).accepted());
    arp::resource::ResourceManager fullManager;
    arp::resource::ResourceGraphExecutor fullExecutor(fullManager,
        fullRuntime.fileService(),
        fullRuntime.workService(),
        fullRuntime.completionQueue());
    arp::core::runtime::CancellationSource fullCancellation;
    fullCancellation.cancel();
    arp::resource::ResourceGraphExecutionRequest queueFull;
    queueFull.graph = requestFor(yamlPath,
        "2e4012d0-bdea-4388-b4a2-73927ad606a5",
        arp::resource::ResourceGraphStage::YamlParse);
    queueFull.budget = {2u, 2u, 1024u, 1024u, 1u};
    queueFull.cancellation = fullCancellation.token();
    Probe queueProbe;
    assert(fullExecutor.submit(std::move(queueFull), {&recordResult, &queueProbe})
        == arp::resource::ResourceGraphSubmitStatus::QueueFull);
    assert(queueProbe.calls == 0u);
    fullRuntime.dispatch();
    fullRuntime.shutdown(arp::core::runtime::AsyncRuntimeShutdownPolicy::Drain);
    mountedContracts();
    return 0;
}
