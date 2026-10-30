#include "core/encoding/yaml_contract.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "resource/prefab_asset_resources.h"
#include "resource/resource_graph_executor.h"
#include "resource/resource_source_io.h"
#include "resource/terrain_asset_resource.h"
#include "resource/vegetation_catalog_cooked.h"
#include "tests/fixtures/cooked_vegetation_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string_view>

using namespace arp::core;
using namespace arp::core::io;
using namespace arp::resource;

namespace {

constexpr const char* terrainId = "c31d9042-8cb2-4942-9969-104109c37090";
constexpr const char* manifestName = "terrain.asset.yaml";
constexpr std::size_t sourceBudget = 64 * 1024;
using CatalogFixture = arp::tests::CookedVegetationFixture;
constexpr const char* catalogManifest = "catalog/catalog.cooked.yaml";

ResourceReference reference()
{
    return {terrainId, TerrainAssetResource::resourceTypeName(), {}, "Contract Complete", {}};
}

void relocateBlock(const std::filesystem::path& manifest, bool catalog)
{
    const auto original = readTextFile(manifest, sourceBudget);
    assert(original.success);
    auto document = YamlContract::parseSingleDocument(original.text);
    auto& binding = catalog ? document["geometry"] : document["tiles"][0]["height"];
    const auto before = YamlContract::text(binding, "path");
    const std::string_view after = catalog ? u8"几何.bin" : u8"tiles/高度.bin";
    std::filesystem::rename(
        manifest.parent_path() / std::filesystem::u8path(before.begin(), before.end()),
        manifest.parent_path() / std::filesystem::u8path(after.begin(), after.end()));
    binding["path"] = pmr::String(after);
    std::ofstream output(manifest, std::ios::binary | std::ios::trunc);
    const auto text = YamlContract::serialize(document);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    assert(output.good());
}

void writeFixture(const std::filesystem::path& root)
{
    TerrainAssetBuildDesc desc;
    desc.version = TerrainAssetVersion::V2;
    desc.hasSurfaceWeights = false;
    desc.hasVegetation = false;
    desc.resourceId = terrainId;
    desc.bounds = {0, 2, 0, 2};
    desc.tileCountX = desc.tileCountZ = 1;
    desc.samplesPerEdge = 2;
    desc.tileWorldSize = 2;
    TerrainAssetBuildTile tile;
    tile.heights = {1, 3, 5, 7};
    desc.tiles.push_back(std::move(tile));
    assert(TerrainAssetWriter().write(desc, root).success);
    assert(VegetationCatalogCookedCodec()
            .write({CatalogFixture::catalogId,
                       "VegetationCatalogResource",
                       {},
                       "Contract Complete",
                       {}},
                CatalogFixture::catalog(),
                root / "catalog",
                CatalogFixture::geometryBytes)
            .success);
    relocateBlock(root / manifestName, false);
    relocateBlock(root / catalogManifest, true);
}

UniquePtr<ResourceBase> resourceFor(
    bool catalog, const std::filesystem::path& path, ResourceFileOrigin origin)
{
    if (!catalog)
        return makeUnique<TerrainAssetResource>(reference(), path, origin);
    VegetationCatalogResourceLoadDesc desc;
    desc.catalogPath = path;
    desc.resourceId = CatalogFixture::catalogId;
    desc.status = "Contract Complete";
    desc.input = VegetationCatalogInput::Cooked;
    desc.sourceOrigin = origin;
    return makeUnique<VegetationCatalogResource>(desc);
}

void prepareDependencies(ResourceManager& manager, bool catalog)
{
    if (catalog) {
        auto material = makeUnique<MaterialResource>();
        material->configureMetadata(
            {CatalogFixture::materialId, "MaterialResource", {}, "Contract Complete", {}});
        assert(manager.loadResource(std::move(material)).success);
    }
}

std::uint64_t contentIdentity(ResourceManager& manager, bool catalog)
{
    if (catalog) {
        auto loaded = manager.find<VegetationCatalogResource>(CatalogFixture::catalogId);
        assert(
            loaded.ready() && loaded.get()->processedByteCount() == CatalogFixture::geometryBytes);
        assert(loaded.get()->data->templates.front().vertices.front().position[0] == 1.25f);
        StableHash64 hash;
        loaded.get()->appendContentFingerprint(hash);
        return hash.value();
    }
    auto loaded = manager.find<TerrainAssetResource>(terrainId);
    assert(loaded.ready() && loaded.get()->sample(1, 1).sample.height == 4);
    return loaded.get()->generation();
}

struct Observation {
    ResourceGraphExecutionResult result;
    std::uint64_t generation = 0;
    unsigned calls = 0;

    static void complete(void* context, ResourceGraphExecutionResult result)
    {
        auto& observation = *static_cast<Observation*>(context);
        ++observation.calls;
        observation.result = std::move(result);
    }
};

// A failed manifest/block must leave the already Ready terrain untouched.
Observation run(SharedPtr<ManagedFileBackend> backend,
    const std::filesystem::path& localRoot,
    bool expectedSuccess,
    std::size_t maxReads = 2,
    bool host = false,
    bool catalog = false)
{
    auto router =
        makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 2, sourceBudget, 32, 1024});
    const auto mount = router->mount("terrain-fixture", backend);
    assert(mount);
    runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = 8;
    desc.completionBudget = 8;
    desc.workerCount = 1;
    desc.workQueueCapacity = 4;
    desc.enableThreadPoolFileBackend = host;
    if (!host)
        desc.fileBackend = router;
    runtime::AsyncRuntime runtime(desc);
    ResourceManager manager;
    ResourceManager baseline;
    const auto* id = catalog ? CatalogFixture::catalogId : terrainId;
    const auto* name = catalog ? catalogManifest : manifestName;
    prepareDependencies(manager, catalog);
    prepareDependencies(baseline, catalog);
    assert(baseline.loadResource(resourceFor(catalog, localRoot / name, {})).success);
    if (!expectedSuccess)
        assert(manager.loadResource(resourceFor(catalog, localRoot / name, {})).success);
    const auto previousGeneration = manager.generationOf(id);
    const auto expectedIdentity = contentIdentity(baseline, catalog);
    ResourceGraphExecutor executor(
        manager, runtime.fileService(), runtime.workService(), runtime.completionQueue());
    ResourceGraphExecutionRequest request;
    ResourceGraphNode node;
    node.resource = resourceFor(catalog,
        host ? localRoot / name : std::filesystem::path(name),
        host ? ResourceFileOrigin{} : ResourceFileOrigin{FilePathScope::MountedResource, mount});
    request.graph.rootResourceId = id;
    request.graph.nodes.push_back(std::move(node));
    request.budget = {1, maxReads, sourceBudget, sourceBudget, 8};
    Observation observation;
    assert(executor.submit(std::move(request), {Observation::complete, &observation})
        == ResourceGraphSubmitStatus::Accepted);
    assert(observation.calls == 0);
    unified_io_fixture::wait(runtime.completionQueue(), [&] {
        return observation.calls != 0;
    });
    assert(observation.calls == 1);
    if (observation.result.resource.success != expectedSuccess)
        std::fprintf(
            stderr, "terrain acquisition: %s\n", observation.result.resource.diagnostics.c_str());
    assert(observation.result.resource.success == expectedSuccess);
    observation.generation = contentIdentity(manager, catalog);
    assert(observation.generation == expectedIdentity);
    if (!expectedSuccess)
        assert(manager.generationOf(id) == previousGeneration);
    runtime.shutdown();
    assert(runtime.completionQueue().reserved() == 0 && runtime.completionQueue().pending() == 0);
    return observation;
}

enum class Mutation { None, BadUtf8, MissingBlock, CorruptBlock, EscapePath, Bom };

SharedPtr<MemoryFileBackend> memoryFixture(
    const std::filesystem::path& root, Mutation mutation, bool catalog = false)
{
    auto backend = makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{4, sourceBudget, 4, sourceBudget, 1024});
    assert(backend->registerDirectory("tiles") == FileIoStatus::Success);
    assert(backend->registerDirectory("catalog") == FileIoStatus::Success);
    TerrainAssetResource terrain(reference(), root / manifestName);
    auto files = terrain.inspectManifest().files;
    if (catalog)
        files = VegetationCatalogCookedCodec()
                    .inspect({CatalogFixture::catalogId,
                                 "VegetationCatalogResource",
                                 {},
                                 "Contract Complete",
                                 {}},
                        root / catalogManifest)
                    .files;
    assert(files.size() == 2);
    for (const auto& intent : files) {
        const auto localPath = std::filesystem::u8path(intent.path.begin(), intent.path.end());
        auto file = readBinaryFile(localPath, sourceBudget);
        assert(file.success);
        const auto name = FilePath::utf8(localPath.lexically_relative(root));
        if (name == (catalog ? catalogManifest : manifestName)) {
            if (mutation == Mutation::BadUtf8)
                file.bytes.push_back(0xFF);
            if (mutation == Mutation::EscapePath) {
                auto document = YamlContract::parseSingleDocument(
                    {reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size()});
                if (catalog)
                    document["geometry"]["path"] = "../outside.bin";
                else
                    document["tiles"][0]["height"]["path"] = "../outside.bin";
                const auto serialized = YamlContract::serialize(document);
                file.bytes.assign(serialized.begin(), serialized.end());
            }
            if (mutation == Mutation::Bom)
                file.bytes.insert(file.bytes.begin(), {0xEF, 0xBB, 0xBF});
        } else {
            if (mutation == Mutation::MissingBlock)
                continue;
            if (mutation == Mutation::CorruptBlock)
                file.bytes[0] ^= 1;
        }
        assert(backend->registerFile(name, file.bytes.data(), file.bytes.size())
            == FileIoStatus::Success);
    }
    return backend;
}

// Exercises planner identity conflicts and cancellation between batches without
// requiring terrain parsing to manufacture invalid graph behavior.
class PlanningProbe final : public ResourceBase {
public:
    enum class Mode {
        Normal,
        Conflict,
        Duplicate,
        Cancel,
        AllocateFail,
        InvalidVersion,
        StaleMount
    };

    PlanningProbe(ResourceFileOrigin origin,
        Mode mode,
        runtime::CancellationSource& cancellation,
        MountedFileBackend& router,
        SharedPtr<ManagedFileBackend> backend)
        : ResourceBase({terrainId, "PlanningProbe", {}, "Contract Complete", {}}), origin_(origin),
          mode_(mode), cancellation_(cancellation), router_(router), backend_(std::move(backend))
    {}

    bool planFileReadIntents(ResourceManager&,
        const ResourceLoadContext& context,
        pmr::Vector<ResourceFileReadIntent>& intents,
        pmr::String& diagnostic) const override
    {
        assert(context.requirePreloadedSources && context.sources);
        if (context.sources->size()) {
            if (mode_ == Mode::Cancel) {
                cancellation_.cancel();
                return true;
            }
            if (mode_ == Mode::AllocateFail)
                throw std::bad_alloc();
            auto origin = origin_;
            if (mode_ == Mode::Conflict)
                origin = {};
            if (mode_ == Mode::StaleMount) {
                assert(router_.unmount(origin_.mount));
                const auto replacement = router_.mount("probe", backend_);
                assert(replacement && !(replacement == origin_.mount));
            }
            assert(appendResourceReadIntent("manifest", 64, intents, diagnostic, origin));
            if (mode_ == Mode::InvalidVersion)
                intents.back().expectedVersion.digest[0] =
                    1; // Invalid None token, even when cached.
            assert(appendResourceReadIntent("payload", 64, intents, diagnostic, origin_));
            return true;
        }
        assert(appendResourceReadIntent("manifest", 64, intents, diagnostic, origin_));
        if (mode_ == Mode::Duplicate)
            intents.push_back(intents.front());
        return true;
    }

    ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context) override
    {
        assert(mode_ == Mode::Normal && context.sources->size() == 2);
        return ResourceBase::load(manager, context);
    }

private:
    ResourceFileOrigin origin_;
    Mode mode_;
    runtime::CancellationSource& cancellation_;
    MountedFileBackend& router_;
    SharedPtr<ManagedFileBackend> backend_;
};

void planningContracts()
{
    for (const auto mode : {PlanningProbe::Mode::Normal,
             PlanningProbe::Mode::Conflict,
             PlanningProbe::Mode::Duplicate,
             PlanningProbe::Mode::Cancel,
             PlanningProbe::Mode::AllocateFail,
             PlanningProbe::Mode::InvalidVersion,
             PlanningProbe::Mode::StaleMount}) {
        auto memory = makeShared<MemoryFileBackend>(MemoryFileBackendDesc{2, 64, 2, 128, 64});
        for (const auto* name : {"manifest", "payload"})
            assert(memory->registerFile(name, reinterpret_cast<const std::uint8_t*>(name), 1)
                == FileIoStatus::Success);
        auto router = makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 2, 128, 8, 64});
        auto mount = router->mount("probe", memory);
        runtime::AsyncRuntimeDesc desc;
        desc.fileBackend = router;
        desc.enableThreadPoolFileBackend = false;
        desc.workerCount = 1;
        desc.workQueueCapacity = 4;
        desc.completionQueueCapacity = desc.completionBudget = 8;
        runtime::AsyncRuntime runtime(desc);
        ResourceManager manager;
        runtime::CancellationSource cancellation;
        ResourceGraphExecutor executor(
            manager, runtime.fileService(), runtime.workService(), runtime.completionQueue());
        ResourceGraphExecutionRequest request;
        ResourceGraphNode node;
        node.resource =
            makeUnique<PlanningProbe>(ResourceFileOrigin{FilePathScope::MountedResource, mount},
                mode,
                cancellation,
                *router,
                memory);
        request.graph.nodes.push_back(std::move(node));
        request.graph.rootResourceId = terrainId;
        request.budget = {1, 2, 128, 128, 8};
        request.cancellation = cancellation.token();
        Observation observation;
        assert(executor.submit(std::move(request), {Observation::complete, &observation})
            == ResourceGraphSubmitStatus::Accepted);
        assert(observation.calls == 0);
        unified_io_fixture::wait(runtime.completionQueue(), [&] {
            return observation.calls != 0;
        });
        assert(observation.calls == 1);
        assert(observation.result.resource.success == (mode == PlanningProbe::Mode::Normal));
        if (mode == PlanningProbe::Mode::Normal)
            assert(observation.result.telemetry.submittedFileReads == 2);
        if (mode == PlanningProbe::Mode::Conflict)
            assert(std::string_view(observation.result.diagnosticId)
                == "ARP-RESOURCE-GRAPH-SOURCE-CONFLICT");
        if (mode == PlanningProbe::Mode::AllocateFail)
            assert(std::string_view(observation.result.diagnosticId)
                == "ARP-RESOURCE-GRAPH-ALLOCATION");
        if (mode == PlanningProbe::Mode::InvalidVersion)
            assert(observation.result.fileStatus == FileIoStatus::InvalidRequest);
        if (mode == PlanningProbe::Mode::StaleMount) {
            assert(observation.result.fileStatus == FileIoStatus::StaleLocation);
            assert(observation.result.telemetry.submittedFileReads == 1);
        }
        runtime.shutdown();
        assert(memory->telemetry().inFlight == 0 && router->telemetry().inFlight == 0);
    }
}

SharedPtr<HttpFileBackend> httpBackend(const char* url)
{
    HttpFileBackendDesc desc;
    desc.baseUrl = url;
    desc.maxInFlight = 2;
    // Transport budget includes output plus bounded headers, URL and scratch.
    desc.maxBytesInFlight = 4 * sourceBudget;
    desc.maxPathBytes = 1024;
    desc.maxHeaderBytes = 4096;
    desc.ioChunkBytes = 1024;
    desc.timeoutMs = 3000;
    desc.cancellationPollMs = 2;
    return makeShared<HttpFileBackend>(desc);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--write-fixture") {
        writeFixture(std::filesystem::u8path(argv[2]));
        return 0;
    }
    if (argc == 4 && std::string_view(argv[1]) == "--http") {
        const auto root = std::filesystem::u8path(argv[3]);
        for (unsigned i = 0; i < 20; ++i)
            for (bool catalog : {false, true})
                assert(run(httpBackend(argv[2]), root, true, 2, false, catalog)
                           .result.telemetry.submittedFileReads
                    == 2);
        const auto before = defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            for (bool catalog : {false, true})
                assert(run(httpBackend(argv[2]), root, true, 2, false, catalog)
                           .result.telemetry.submittedFileReads
                    == 2);
        const auto after = defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes);
        assert(before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
        for (const auto* scenario : {"bad-utf8/", "missing-block/", "corrupt-block/"}) {
            auto url = pmr::String(argv[2]);
            url += scenario;
            for (bool catalog : {false, true})
                run(httpBackend(url.c_str()), root, false, 2, false, catalog);
        }
        std::puts(
            "Terrain/catalog manifest/block real engine HTTP: 20 ordinary cycles, core live bytes/count delta 0 PASS");
        return 0;
    }
    assert(argc == 1);
    planningContracts();
    unified_io_fixture::Workspace workspace;
    writeFixture(workspace.root);
    const auto unicodeRoot = workspace.root / std::filesystem::u8path(u8"路径");
    writeFixture(unicodeRoot);
    for (bool catalog : {false, true}) {
        run(memoryFixture(unicodeRoot, Mutation::None, catalog),
            unicodeRoot,
            true,
            2,
            true,
            catalog);
#if defined(_WIN32)
        const auto native = run(makeShared<NativeDirectoryFileBackend>(workspace.descriptor()),
            workspace.root,
            true,
            2,
            false,
            catalog);
#endif
        const auto host = run(memoryFixture(workspace.root, Mutation::None, catalog),
            workspace.root,
            true,
            2,
            true,
            catalog);
        const auto memory = run(memoryFixture(workspace.root, Mutation::None, catalog),
            workspace.root,
            true,
            2,
            false,
            catalog);
        assert(host.generation == memory.generation);
#if defined(_WIN32)
        assert(native.generation == memory.generation);
#endif
        assert(memory.result.telemetry.submittedFileReads == 2);
        assert(run(memoryFixture(workspace.root, Mutation::Bom, catalog),
                   workspace.root,
                   true,
                   2,
                   false,
                   catalog)
                   .generation
            == memory.generation);
        for (const auto mutation : {Mutation::BadUtf8,
                 Mutation::MissingBlock,
                 Mutation::CorruptBlock,
                 Mutation::EscapePath})
            run(memoryFixture(workspace.root, mutation, catalog),
                workspace.root,
                false,
                2,
                false,
                catalog);
        const auto limited = run(memoryFixture(workspace.root, Mutation::None, catalog),
            workspace.root,
            false,
            1,
            false,
            catalog);
        assert(limited.result.telemetry.submittedFileReads == 1);
        assert(std::string_view(limited.result.diagnosticId) == "ARP-RESOURCE-GRAPH-READ-BUDGET");
    }
    std::puts(
        "Staged source planning and terrain/catalog native/memory acquisition contracts PASS");
}
