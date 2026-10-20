#include "core/cpu_allocator.h"
#include "core/runtime/async_runtime.h"
#include "reflection/reflection.h"
#include "resource/prefab_asset_resources.h"
#include "resource/resource_manager.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>

using namespace arp::reflection;

namespace {

constexpr const char* kMaterialId = "7f4b9d5a-1f63-4f4c-9a45-3c1a9e7e92b1";
constexpr const char* kMissingMaterialId = "c8591a23-0f99-4070-a4d0-d79ab87379eb";
constexpr const char* kUnknownMaterialId = "b055987e-d04c-42ef-a3f1-f77721eac8f7";
constexpr const char* kTextureId = "9e3f0b60-93f7-42b8-88e7-32e0c9f0bc40";
constexpr const char* kMetadataOnlyId = "86ccf943-c70e-4ddc-b7c4-f1a6c02ff115";
constexpr const char* kOwnedResourceId = "af3826cf-7f56-406e-8352-c19e56a21019";
constexpr const char* kGraphRootId = "95f7e21e-6508-4b5f-a85d-a0ef870f0860";
constexpr const char* kGraphLeafId = "3904e852-21a9-4fb8-b4ee-eb3e7631c408";
constexpr const char* kGraphOtherId = "82620ef2-8f19-43b6-9714-2ec487824880";
constexpr const char* kBatchFirstId = "165b5ee8-2514-46e7-8472-1bfa17c1e226";
constexpr const char* kBatchSecondId = "94d1a54f-2719-41d1-b37c-5b5d266feb52";

struct TestTextureResource {
    arp::core::pmr::String path{&arp::core::defaultCpuMemoryResource()};
};

struct TestMaterialResource {
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String baseColorTexture{&arp::core::defaultCpuMemoryResource()};
    float roughness = 0.0f;
};

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

class MetadataOnlyResource final : public arp::resource::ResourceBase {
public:
    MetadataOnlyResource() : arp::resource::ResourceBase(makeReference())
    {
        addDependency(arp::resource::ResourceDependency{
            text(kTextureId),
            text("texture/baseColor"),
        });
    }

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& manager,
        const arp::resource::ResourceLoadContext& context) override
    {
        return manager.admitResourceMetadata(*this, context.policy);
    }

private:
    static arp::resource::ResourceReference makeReference()
    {
        arp::resource::ResourceReference reference;
        reference.id = text(kMetadataOnlyId);
        reference.type = text("MetadataOnlyResource");
        reference.alias = text("metadata/material");
        reference.status = text("Contract Complete");
        reference.displayName = text("Metadata Material");
        return reference;
    }
};

class OwnedReadyResource final : public arp::resource::ResourceBase {
public:
    explicit OwnedReadyResource(int value)
        : arp::resource::ResourceBase(makeReference()), value_(value)
    {}

    int value() const noexcept { return value_; }

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& manager,
        const arp::resource::ResourceLoadContext& context) override
    {
        return manager.admitResourceMetadata(*this, context.policy);
    }

private:
    static arp::resource::ResourceReference makeReference()
    {
        arp::resource::ResourceReference reference;
        reference.id = text(kOwnedResourceId);
        reference.type = text("OwnedReadyResource");
        reference.alias = text("metadata/owned-ready");
        reference.status = text("Prototype Complete");
        reference.displayName = text("Owned Ready Resource");
        return reference;
    }

    int value_ = 0;
};

class GraphContractResource final : public arp::resource::ResourceBase {
public:
    GraphContractResource(
        const char* id, const char* alias, const char* status = "Contract Complete")
        : arp::resource::ResourceBase(makeReference(id, alias, status))
    {}

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& manager,
        const arp::resource::ResourceLoadContext& context) override
    {
        return manager.admitResourceMetadata(*this, context.policy);
    }

private:
    static arp::resource::ResourceReference makeReference(
        const char* id, const char* alias, const char* status)
    {
        arp::resource::ResourceReference reference;
        reference.id = text(id);
        reference.type = text("GraphContractResource");
        reference.alias = text(alias);
        reference.status = text(status);
        reference.displayName = text("Graph Contract Resource");
        return reference;
    }
};

class AtomicBatchResource final : public arp::resource::ResourceBase {
public:
    AtomicBatchResource(const char* id, const char* alias, bool failAfterAdmission)
        : arp::resource::ResourceBase(makeReference(id, alias)),
          failAfterAdmission_(failAfterAdmission)
    {}

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& manager,
        const arp::resource::ResourceLoadContext& context) override
    {
        auto admitted = manager.admitResourceMetadata(*this, context.policy);
        if (!admitted.success || !failAfterAdmission_)
            return admitted;
        admitted.success = false;
        admitted.state = arp::resource::ResourceState::Failed;
        admitted.diagnostics = text("intentional atomic batch failure");
        return admitted;
    }

private:
    static arp::resource::ResourceReference makeReference(const char* id, const char* alias)
    {
        arp::resource::ResourceReference reference;
        reference.id = text(id);
        reference.type = text("AtomicBatchResource");
        reference.alias = text(alias);
        reference.status = text("Contract Complete");
        reference.displayName = text("Atomic Batch Resource");
        return reference;
    }

    bool failAfterAdmission_ = false;
};

struct GraphProbe {
    arp::resource::ResourceLoadResult result;
    bool called = false;
};

void recordGraphLoad(void* userData, arp::resource::ResourceLoadResult result)
{
    auto* probe = static_cast<GraphProbe*>(userData);
    probe->result = result;
    probe->called = true;
}

arp::resource::ResourceDependency graphDependency(const char* id)
{
    arp::resource::ResourceDependency dependency;
    dependency.id = text(id);
    dependency.usage = text("graph.dependency");
    return dependency;
}

arp::resource::ResourceGraphNode graphNode(
    const char* id, const char* alias, const char* status = "Contract Complete")
{
    arp::resource::ResourceGraphNode node;
    node.resource = arp::core::makeUnique<GraphContractResource>(id, alias, status);
    return node;
}

arp::resource::ResourceGraphRequest validGraphRequest()
{
    arp::resource::ResourceGraphRequest request;
    request.rootResourceId = text(kGraphRootId);
    auto root = graphNode(kGraphRootId, "graph/root");
    root.dependencies.push_back(graphDependency(kGraphLeafId));
    request.nodes.push_back(std::move(root));
    request.nodes.push_back(graphNode(kGraphLeafId, "graph/leaf"));
    return request;
}

void registerResourceTypes()
{
    registration::class_<TestTextureResource>("TestTextureResource")
        .construct<>()
        .property("path", &TestTextureResource::path);

    registration::class_<TestMaterialResource>("TestMaterialResource")
        .construct<>()
        .property("name", &TestMaterialResource::name)
        .property("baseColorTexture", &TestMaterialResource::baseColorTexture)
        .property("roughness", &TestMaterialResource::roughness);
}

void assertContains(const arp::core::pmr::String& haystack, const char* needle)
{
    assert(haystack.find(needle) != arp::core::pmr::String::npos);
}

arp::core::pmr::String indentPayload(const arp::core::pmr::String& payload)
{
    arp::core::pmr::String indented(&arp::core::defaultCpuMemoryResource());
    indented += "  ";
    for (const char ch : payload) {
        indented.push_back(ch);
        if (ch == '\n') {
            indented += "  ";
        }
    }
    return indented;
}

arp::core::pmr::String materialPayload()
{
    return text("type: TestMaterialResource\n"
                "fields:\n"
                "  name: MeadowRock\n"
                "  baseColorTexture: texture/meadow_rock_base\n"
                "  roughness: 0.6\n");
}

arp::core::pmr::String makeEnvelope(const char* resourceId,
    const char* resourceType,
    const arp::core::pmr::String& payload,
    std::uint32_t contentHash,
    bool includeDependency = true,
    const char* alias = "material/meadow_rock")
{
    arp::core::pmr::String envelope(&arp::core::defaultCpuMemoryResource());
    envelope += "format: ArpRuntimeResource\n";
    envelope += "version: 1\n";
    envelope += "resourceId: ";
    envelope += resourceId;
    envelope += "\nresourceType: ";
    envelope += resourceType;
    envelope += "\nalias: ";
    envelope += alias;
    envelope += "\ndisplayName: MeadowRock";
    envelope += "\nstatus: Contract Complete\n";
    envelope += "contentHash: ";
    envelope += std::to_string(contentHash).c_str();
    envelope += "\n";
    if (includeDependency) {
        envelope += "dependencies:\n";
        envelope += "  - id: ";
        envelope += kTextureId;
        envelope += "\n";
        envelope += "    usage: texture/baseColor\n";
    } else {
        envelope += "dependencies: []\n";
    }
    envelope += "payload:\n";
    envelope += indentPayload(payload);
    return envelope;
}

arp::core::pmr::String validMaterialEnvelope()
{
    const auto payload = materialPayload();
    const auto hash = arp::resource::computeResourcePayloadHash(payload);
    assert(hash.success);
    return makeEnvelope(kMaterialId, "TestMaterialResource", payload, hash.hash);
}

} // namespace

int main()
{
    registerResourceTypes();

    {
        arp::resource::ResourceManager manager;
        auto dependency = manager.declareResource(kTextureId, "TestTextureResource");
        assert(dependency.success);

        MetadataOnlyResource resource;
        auto admitted = resource.load(manager, arp::resource::ResourceLoadContext{});
        assert(admitted.success);
        assert(admitted.state == arp::resource::ResourceState::Declared);

        const arp::resource::ResourceRecord* record = manager.recordSnapshot(kMetadataOnlyId);
        assert(record != nullptr);
        assert(record->id == resource.reference().id);
        assert(record->type == resource.reference().type);
        assert(record->alias == resource.reference().alias);
        assert(record->status == resource.reference().status);
        assert(record->displayName == resource.reference().displayName);
        assert(record->dependencies.size() == resource.dependencies().size());
        assert(record->dependencies[0].id == resource.dependencies()[0].id);
        assert(record->dependencies[0].usage == resource.dependencies()[0].usage);

        auto resolved = manager.resolveAlias("metadata/material");
        assert(resolved.success);
        assert(resolved.resourceId == text(kMetadataOnlyId));

        MetadataOnlyResource duplicateAlias;
        auto duplicateAliasResult =
            duplicateAlias.load(manager, arp::resource::ResourceLoadContext{});
        assert(!duplicateAliasResult.success);
        assertContains(duplicateAliasResult.diagnostics, "duplicate resource id");
    }

    {
        arp::resource::ResourceManager manager;
        auto loaded = manager.loadResource(
            arp::core::makeUnique<OwnedReadyResource>(42), arp::resource::ResourceLoadContext{});
        assert(loaded.success);
        assert(loaded.state == arp::resource::ResourceState::Ready);

        const auto* record = manager.recordSnapshot(kOwnedResourceId);
        assert(record != nullptr);
        assert(record->ownedResource != nullptr);
        assert(record->value.is_undefined());
        assert(record->ownedObjects.empty());

        auto handle = manager.find<OwnedReadyResource>(kOwnedResourceId);
        assert(handle.ready());
        assert(handle.get() != nullptr);
        assert(handle.get()->value() == 42);

        auto aliasHandle = manager.findByAlias<OwnedReadyResource>("metadata/owned-ready");
        assert(aliasHandle.ready());
        assert(aliasHandle.get()->value() == 42);
    }

    {
        arp::resource::ResourceManager manager;
        auto loaded = manager.loadGraph(validGraphRequest());
        assert(loaded.success);
        assert(loaded.resourceId == text(kGraphRootId));
        auto root = manager.findByAlias<GraphContractResource>("graph/root");
        auto leaf = manager.findByAlias<GraphContractResource>("graph/leaf");
        assert(root.ready());
        assert(leaf.ready());
        const auto* rootRecord = manager.recordSnapshot(kGraphRootId);
        assert(rootRecord != nullptr);
        assert(rootRecord->dependencies.size() == 1u);
        assert(rootRecord->dependencies[0].id == text(kGraphLeafId));
    }

    {
        arp::resource::ResourceManager manager;
        arp::core::pmr::Vector<arp::core::UniquePtr<arp::resource::ResourceBase>> batch{
            &arp::core::defaultCpuMemoryResource()};
        batch.push_back(
            arp::core::makeUnique<AtomicBatchResource>(kBatchFirstId, "batch/first", false));
        batch.push_back(
            arp::core::makeUnique<AtomicBatchResource>(kBatchSecondId, "batch/second", true));
        const auto result = manager.loadResourcesAtomically(std::move(batch));
        assert(!result.success && result.rolledBack);
        assert(result.loadedCount == 0u);
        assert(result.failedResourceId == text(kBatchSecondId));
        assert(manager.stateOf(kBatchFirstId) == arp::resource::ResourceState::Unloaded);
        assert(manager.stateOf(kBatchSecondId) == arp::resource::ResourceState::Unloaded);
        assert(manager.recordSnapshot(kBatchFirstId) == nullptr);
        assert(manager.recordSnapshot(kBatchSecondId) == nullptr);
        assert(!manager.resolveAlias("batch/first").success);
        assert(!manager.resolveAlias("batch/second").success);

        auto seed = manager.loadResource(
            arp::core::makeUnique<AtomicBatchResource>(kBatchFirstId, "batch/first", false));
        assert(seed.success);
        assert(manager.unload(kBatchFirstId).success);
        const auto priorGeneration = manager.generationOf(kBatchFirstId);
        auto retry = arp::core::pmr::Vector<arp::core::UniquePtr<arp::resource::ResourceBase>>{
            &arp::core::defaultCpuMemoryResource()};
        retry.push_back(
            arp::core::makeUnique<AtomicBatchResource>(kBatchFirstId, "batch/first", false));
        retry.push_back(
            arp::core::makeUnique<AtomicBatchResource>(kBatchSecondId, "batch/second", true));
        const auto retried = manager.loadResourcesAtomically(std::move(retry));
        assert(!retried.success && retried.rolledBack);
        const auto* restored = manager.recordSnapshot(kBatchFirstId);
        assert(restored != nullptr);
        assert(restored->state == arp::resource::ResourceState::Unloaded);
        assert(restored->generation == priorGeneration);
        const auto restoredAlias = manager.resolveAlias("batch/first");
        assert(restoredAlias.success && restoredAlias.resourceId == text(kBatchFirstId));
        assert(!manager.resolveAlias("batch/second").success);
    }

    {
        arp::resource::ResourceManager manager;
        auto request = validGraphRequest();
        request.nodes.pop_back();
        auto missing = manager.loadGraph(std::move(request));
        assert(!missing.success);
        assertContains(missing.diagnostics, "resource graph missing dependency node");
    }

    {
        arp::resource::ResourceManager manager;
        arp::resource::ResourceGraphRequest request;
        request.rootResourceId = text(kGraphRootId);
        request.nodes.push_back(graphNode(kGraphRootId, "graph/duplicate"));
        request.nodes.push_back(graphNode(kGraphOtherId, "graph/duplicate"));
        auto duplicate = manager.loadGraph(std::move(request));
        assert(!duplicate.success);
        assertContains(duplicate.diagnostics, "resource graph duplicate alias");
    }

    {
        arp::resource::ResourceManager manager;
        arp::resource::ResourceGraphRequest request;
        request.rootResourceId = text(kGraphRootId);
        auto root = graphNode(kGraphRootId, "graph/root");
        root.dependencies.push_back(graphDependency(kGraphLeafId));
        auto leaf = graphNode(kGraphLeafId, "graph/leaf");
        leaf.dependencies.push_back(graphDependency(kGraphRootId));
        request.nodes.push_back(std::move(root));
        request.nodes.push_back(std::move(leaf));
        auto cycle = manager.loadGraph(std::move(request));
        assert(!cycle.success);
        assertContains(cycle.diagnostics, "resource graph dependency cycle detected");
    }

    {
        arp::resource::ResourceManager manager;
        arp::resource::ResourceGraphRequest request;
        request.rootResourceId = text(kGraphRootId);
        request.nodes.push_back(graphNode(kGraphRootId, "graph/root", "Legacy fallback"));
        auto legacy = manager.loadGraph(std::move(request));
        assert(!legacy.success);
        assertContains(legacy.diagnostics, "resource graph rejects legacy dependency status");
    }

    {
        arp::core::runtime::AsyncRuntimeDesc runtimeDesc;
        runtimeDesc.completionQueueCapacity = 4u;
        runtimeDesc.workerCount = 1u;
        runtimeDesc.workQueueCapacity = 2u;
        runtimeDesc.completionBudget = 2u;
        arp::core::runtime::AsyncRuntime runtime(runtimeDesc);
        arp::resource::ResourceManager manager;
        GraphProbe probe;
        const auto submitted = manager.loadGraphAsync(runtime.workService(),
            validGraphRequest(),
            arp::resource::ResourceAsyncLoadCompletion{&recordGraphLoad, &probe});
        assert(submitted == arp::core::runtime::WorkServiceStatus::Accepted);
        for (int i = 0; i < 100 && !probe.called; ++i) {
            runtime.dispatch();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(probe.called);
        assert(probe.result.success);
        assert(manager.findByAlias<GraphContractResource>("graph/root").ready());
        runtime.shutdown(arp::core::runtime::AsyncRuntimeShutdownPolicy::Drain);
    }

    {
        arp::resource::ResourceManager manager;
        auto dependency = manager.declareResource(kTextureId, "TestTextureResource");
        assert(dependency.success);

        auto load =
            arp::resource::compatibility::loadFromEnvelope(manager, validMaterialEnvelope());
        assert(load.success);
        assert(load.state == arp::resource::ResourceState::Ready);
        assert(load.resourceId == text(kMaterialId));

        // Reflection fallback removed from ResourceHandle<T>; legacy envelope load
        // no longer supports find<T>() for reflected payload ownership.
        auto handle = manager.find<TestMaterialResource>(kMaterialId);
        assert(!handle.ready());
        assert(handle.get() == nullptr);

        auto wrongType = manager.find<TestTextureResource>(kMaterialId);
        assert(!wrongType.ready());
        assert(wrongType.get() == nullptr);
        assertContains(wrongType.diagnostics(), "type mismatch");

        assert(manager.unload(kMaterialId).success);
        assert(manager.stateOf(kMaterialId) == arp::resource::ResourceState::Unloaded);
        assert(!manager.find<TestMaterialResource>(kMaterialId).ready());
    }

    {
        arp::resource::ResourceManager manager;
        auto first = manager.declareResource(kMaterialId, "TestMaterialResource");
        assert(first.success);
        auto duplicate = manager.declareResource(kMaterialId, "TestMaterialResource");
        assert(!duplicate.success);
        assertContains(duplicate.diagnostics, "duplicate resource id");
    }

    {
        arp::resource::ResourceManager manager;
        auto missingType = arp::resource::compatibility::loadFromEnvelope(manager,
            "format: ArpRuntimeResource\n"
            "version: 1\n"
            "resourceId: c8591a23-0f99-4070-a4d0-d79ab87379eb\n"
            "payload:\n"
            "  type: TestMaterialResource\n"
            "  fields: {}\n");
        assert(!missingType.success);
        assertContains(missingType.diagnostics, "missing required resourceType");
    }

    {
        arp::resource::ResourceManager manager;
        manager.declareResource(kTextureId, "TestTextureResource");
        const auto payload = materialPayload();
        const auto hash = arp::resource::computeResourcePayloadHash(payload);
        auto mismatch = arp::resource::compatibility::loadFromEnvelope(
            manager, makeEnvelope(kMaterialId, "TestTextureResource", payload, hash.hash));
        assert(!mismatch.success);
        assertContains(mismatch.diagnostics, "payload type does not match resourceType");
    }

    {
        arp::resource::ResourceManager manager;
        manager.declareResource(kTextureId, "TestTextureResource");
        auto badHash = arp::resource::compatibility::loadFromEnvelope(
            manager, makeEnvelope(kMaterialId, "TestMaterialResource", materialPayload(), 1u));
        assert(!badHash.success);
        assertContains(badHash.diagnostics, "contentHash mismatch");
    }

    {
        arp::resource::ResourceManager manager;
        auto loaded = manager.loadGraph(validGraphRequest());
        assert(loaded.success);

        const auto snapshot = manager.snapshotReferences("GraphContractResource", 2u);
        assert(snapshot.success && snapshot.entries.size() == 2u);
        assert(snapshot.entries[0].reference.id < snapshot.entries[1].reference.id);
        for (const auto& entry : snapshot.entries) {
            assert(entry.reference.type == "GraphContractResource");
            assert(entry.state == arp::resource::ResourceState::Ready);
            assert(entry.generation != 0u);
        }

        const auto overLimit = manager.snapshotReferences("GraphContractResource", 1u);
        assert(!overLimit.success && overLimit.entries.empty());
        assertContains(overLimit.diagnostics, "caller entry limit");

        const auto unfiltered = manager.snapshotReferences("", 2u);
        assert(!unfiltered.success && unfiltered.entries.empty());
        assertContains(unfiltered.diagnostics, "exact resource type");
    }

    {
        arp::resource::ResourceManager manager;
        auto unresolved =
            arp::resource::compatibility::loadFromEnvelope(manager, validMaterialEnvelope());
        assert(!unresolved.success);
        assertContains(unresolved.diagnostics, "unresolved dependency");
    }

    {
        arp::resource::ResourceManager manager;
        const auto missingPayload = text("type: MissingResource\n"
                                         "fields: {}\n");
        const auto missingHash = arp::resource::computeResourcePayloadHash(missingPayload);
        assert(missingHash.success);
        auto unknown = arp::resource::compatibility::loadFromEnvelope(manager,
            makeEnvelope(kUnknownMaterialId,
                "MissingResource",
                missingPayload,
                missingHash.hash,
                false,
                "material/unknown"));
        assert(!unknown.success);
        assertContains(unknown.diagnostics, "unregistered type");
    }

    return 0;
}
