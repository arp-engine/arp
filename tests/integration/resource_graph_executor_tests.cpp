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
        arp::resource::ResourceGraphStage processingStage)
        : arp::resource::ResourceBase(std::move(reference)), path_(std::move(path)),
          processingStage_(processingStage)
    {}

    bool collectFileReadIntents(arp::resource::ResourceManager&,
        arp::core::pmr::Vector<arp::resource::ResourceFileReadIntent>& intents,
        arp::core::pmr::String&) const override
    {
        arp::resource::ResourceFileReadIntent intent;
        intent.pathScope = arp::core::io::FilePathScope::HostFilesystem;
        intent.path = path_.generic_string().c_str();
        intent.role = text("contract-yaml");
        intent.maxBytes = 1024u;
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
        if (source == nullptr || source->bytes.empty()) {
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

arp::resource::ResourceGraphRequest requestFor(
    const std::filesystem::path& path, const char* id, arp::resource::ResourceGraphStage stage)
{
    arp::resource::ResourceReference reference;
    reference.id = text(id);
    reference.type = text("FileBackedGraphResource");
    reference.alias = text(id);
    reference.status = text("Prototype Complete");
    arp::resource::ResourceGraphNode node;
    node.resource = arp::core::makeUnique<FileBackedGraphResource>(reference, path, stage);
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
    return 0;
}
