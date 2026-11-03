#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "resource/resource_graph_executor.h"
#include "tests/fixtures/environment_lod_source_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cstdio>
#include <string_view>
#include <type_traits>

namespace {
using namespace arp;
using namespace resource;
using namespace core::io;
using Fixture = tests::EnvironmentLodSourceFixture;

template <class Resource> constexpr bool isPlant = std::is_same_v<Resource, PlantAssetSet>;

template <class Resource> const char* id()
{
    return isPlant<Resource> ? Fixture::plantId : Fixture::rockId;
}

template <class Resource> core::UniquePtr<Resource> candidate(EnvironmentLodSourceDesc desc)
{
    if constexpr (isPlant<Resource>)
        return core::makeUnique<Resource>(id<Resource>(),
            "plants/source",
            core::pmr::String("plants"),
            std::move(desc),
            core::pmr::String("wood"),
            core::pmr::String("foliage"));
    else
        return core::makeUnique<Resource>(
            id<Resource>(), "rocks/source", core::pmr::String("rocks"), std::move(desc));
}

core::SharedPtr<MemoryFileBackend> memory(const Fixture& fixture)
{
    auto backend = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{8, 6 * Fixture::sourceBudget, 1, Fixture::sourceBudget, 256});
    assert(backend->registerDirectory("plants") == FileIoStatus::Success);
    assert(backend->registerDirectory("rocks") == FileIoStatus::Success);
    for (const auto& file : fixture.files)
        assert(backend->registerFile(file.path, file.bytes.data(), file.bytes.size())
            == FileIoStatus::Success);
    return backend;
}

template <class Resource> void check(const Resource& resource, std::uint32_t levels)
{
    assert(resource.lodCount() == levels && resource.variantCount() == 1
        && resource.transformLayout() == assets::NamedVariantTransformLayout::RowMajor);
    const auto* first = resource.findVariant("leaf\xf0\x9f\x8c\xbf", 0);
    assert(first && first->mesh.vertices[0].position[0] == 2.5f
        && first->hasWindData == isPlant<Resource>);
    assert(resource.materialsForLod(0)->size() == 1 && resource.texturesForLod(0)->size() == 1
        && resource.imagesForLod(0)->size() == 1);
    if (levels > 1) {
        const auto* distant = resource.findVariant("leaf\xf0\x9f\x8c\xbf", 1);
        assert(distant && distant->mesh.vertices[0].position[0] == 1.25f);
    }
    if (levels == 3) {
        assert(resource.findVariant("leaf\xf0\x9f\x8c\xbf", 2) == first
            && resource.materialsForLod(2) == resource.materialsForLod(0)
            && resource.imagesForLod(2) == resource.imagesForLod(0));
    }
}

template <class Resource> void contracts(const Fixture& fixture, ResourceFileOrigin origin)
{
    auto sources = fixture.sources(origin);
    ResourceLoadContext context;
    context.sources = &sources;
    context.maxDecodedBytes = Fixture::decodedBudget;
    ResourceManager manager;
    auto initial = candidate<Resource>(fixture.descriptor(origin, isPlant<Resource>));
    auto* mutableCandidate = initial.get();
    assert(manager.loadResource(std::move(initial), context).success);
    auto prior = manager.find<Resource>(id<Resource>());
    const auto generation = prior.generation();
    const auto* pointer = prior.get();
    check(*pointer, 1);
    assert(!mutableCandidate->loadLod(2).success && prior.get()->lodCount() == 1);
    for (unsigned test = 0; test < 23; ++test) {
        auto desc = fixture.descriptor(origin, isPlant<Resource>, 3);
        sources = fixture.sources(origin);
        if (test == 0)
            desc.levels[1].sharedLod = 2;
        if (test == 1)
            desc.levels[2].version.digest[0] ^= 1;
        if (test == 2)
            desc.levels[1].path = "missing.glb";
        if (test == 3)
            desc.variantNames[0] = "absent";
        if (test == 4)
            desc.residentLevels = 0;
        if (test == 5)
            desc.levels[0].limits.maxJsonDepth = 0;
        if (test == 6)
            desc.maxTotalSourceBytes = 1;
        if (test == 7)
            desc.maxTotalImportBytes = 1;
        if (test == 8)
            desc.maxDecodedBytes = 1;
        if (test == 9)
            context.maxDecodedBytes = 1;
        if (test == 10)
            desc.origin.mount.generation += 1;
        if (test == 11)
            desc.levels[2].limits.maxVertices -= 1;
        if (test == 12) {
            desc.levels[2].sharedLod = UINT32_MAX;
            desc.levels[2].path = desc.levels[0].path;
            desc.levels[2].version = desc.levels[1].version;
        }
        if (test == 13)
            desc.levels[0].version = {};
        if (test == 14)
            desc.levels[0].path = "../outside.glb";
        if (test == 15)
            desc.residentLevels = 4;
        if (test == 16)
            desc.variantNames.clear();
        if (test == 17)
            desc.variantNames.push_back(desc.variantNames[0]);
        if (test == 18)
            sources.clear();
        if (test == 19) {
            auto corrupted = fixture;
            corrupted.files[isPlant<Resource> ? 0 : 3].bytes[0] ^= 1;
            sources = corrupted.sources(origin);
        }
        if (test == 20)
            desc.maxTotalImportBytes = 4 * 1024 * 1024 + 1;
        if (test == 21)
            desc.levels[1].byteCount += 1;
        if (test == 22) {
            desc.levels[0].limits.maxImportBytes = 1;
            desc.levels[2].limits.maxImportBytes = 1;
        }
        const auto failed = manager.reloadResource(candidate<Resource>(std::move(desc)), context);
        if (failed.success)
            std::fprintf(stderr, "unexpected LOD success %u\n", test);
        assert(!failed.success && failed.telemetry.rolledBack && prior.ready()
            && prior.get() == pointer && prior.generation() == generation
            && !failed.diagnostics.empty());
        check(*pointer, 1);
        context.maxDecodedBytes = Fixture::decodedBudget;
    }
    sources = fixture.sources(origin);
    auto next = prior.get()->makeLodCandidate(3);
    assert(next && !prior.get()->makeLodCandidate(1) && !prior.get()->makeLodCandidate(4));
    const auto reloaded = manager.reloadResource(std::move(next), context);
    assert(reloaded.success && reloaded.telemetry.committed && !prior.ready());
    auto current = manager.find<Resource>(id<Resource>());
    check(*current.get(), 3);
    assert(current.generation() == generation + 1 && manager.retainedResourceCount() == 1);
    manager.releaseRetiredResources();
    assert(!manager.retainedResourceCount());

    // Full imported payload is owned, and the declared shared level adds no geometry copy.
    const auto full = EnvironmentLodSourceLoader::prepare(
        fixture.descriptor(origin, isPlant<Resource>, 3), context);
    assert(full.success && full.levels[2].variants.empty()
        && full.importPeakUpperBound <= 8 * 1024 * 1024);
    sources.clear();
    check(*current.get(), 3);
    auto missing =
        EnvironmentLodSourceLoader::prepare(fixture.descriptor(origin, isPlant<Resource>, 3), {});
    assert(!missing.success && missing.levels.empty());
}

void canonicalOrder(ResourceFileOrigin origin)
{
    Fixture pair;
    for (auto& file : pair.files) {
        auto json = tests::NamedGlbSourceFixture::document();
        tests::NamedGlbSourceFixture::replace(
            json, "\"nodes\":[{", "\"nodes\":[{\"children\":[1],");
        tests::NamedGlbSourceFixture::replace(
            json, "\"scale\":[2,3,4]}]", "\"scale\":[2,3,4]},{\"name\":\"second\",\"mesh\":0}]");
        file.bytes = tests::StaticGlbGeometryFixture::bytes(json);
    }
    auto desc = pair.descriptor(origin, true, 3);
    desc.variantNames.insert(desc.variantNames.begin(), "second");
    auto inputs = pair.sources(origin);
    ResourceLoadContext context;
    context.sources = &inputs;
    context.maxDecodedBytes = Fixture::decodedBudget;
    const auto prepared = EnvironmentLodSourceLoader::prepare(desc, context);
    assert(prepared.success && prepared.levels.size() == 3);
    for (unsigned lod = 0; lod < 3; ++lod) {
        const auto* data = EnvironmentLodSourceLoader::resolve(prepared.levels, lod);
        assert(data && data->variants.size() == 2 && data->variants[0].name == "second"
            && data->variants[1].name == "leaf\xf0\x9f\x8c\xbf");
    }
}

template <class Resource>
void graph(
    const Fixture& fixture, core::SharedPtr<ManagedFileBackend> backend, unsigned failure = 0)
{
    auto mounts = core::makeShared<MountedFileBackend>(
        MountedFileBackendDesc{1, 1, Fixture::sourceBudget, 32, 256});
    ResourceFileOrigin origin{FilePathScope::MountedResource, mounts->mount("source", backend)};
    core::runtime::AsyncRuntimeDesc runtimeDesc;
    runtimeDesc.enableWork = runtimeDesc.enableThreadPoolFileBackend = false;
    runtimeDesc.fileBackend = mounts;
    runtimeDesc.completionQueueCapacity = runtimeDesc.completionBudget = 4;
    core::runtime::AsyncRuntime runtime(runtimeDesc);
    ResourceManager manager;
    ResourceBudgetConfig managerBudget;
    managerBudget.maxReloadGenerations = 8;
    managerBudget.maxRetainedRecoveryBytes = 2 * Fixture::decodedBudget;
    manager.setBudgetConfig(managerBudget);
    ResourceGraphExecutor executor(manager, runtime.fileService(), runtime.completionQueue());

    struct Completion {
        bool done = false;
        ResourceGraphExecutionResult result;
    } completion;

    const ResourceGraphExecutionCompletion finish{
        [](void* pointer, ResourceGraphExecutionResult result) {
            auto& out = *static_cast<Completion*>(pointer);
            out.result = std::move(result);
            out.done = true;
        },
        &completion};
    const auto requestFor = [&](core::UniquePtr<Resource> resource) {
        ResourceGraphExecutionRequest request;
        request.graph.rootResourceId = id<Resource>();
        ResourceGraphNode node;
        node.resource = std::move(resource);
        request.graph.nodes.push_back(std::move(node));
        request.budget = {1, 3, 3 * Fixture::sourceBudget, Fixture::decodedBudget, 4};
        return request;
    };
    assert(
        executor.submit(
            requestFor(candidate<Resource>(fixture.descriptor(origin, isPlant<Resource>))), finish)
        == ResourceGraphSubmitStatus::Accepted);
    unified_io_fixture::wait(runtime.completionQueue(), [&] {
        return completion.done;
    });
    if (!completion.result.resource.success)
        std::fprintf(stderr, "%s\n", completion.result.resource.diagnostics.c_str());
    assert(
        completion.result.resource.success && completion.result.telemetry.submittedFileReads == 1);
    auto prior = manager.find<Resource>(id<Resource>());
    check(*prior.get(), 1);
    for (unsigned invalid = 0; invalid < 6; ++invalid) {
        auto denied = requestFor(prior.get()->makeLodCandidate(3));
        denied.admission = ResourceGraphAdmission::ReloadSingle;
        denied.expectedGeneration = prior.generation();
        if (invalid == 0)
            denied.expectedGeneration = 0;
        if (invalid == 1)
            denied.expectedGeneration += 1;
        if (invalid == 2)
            denied.admission = static_cast<ResourceGraphAdmission>(99);
        if (invalid == 3)
            manager.setBudgetConfig({});
        if (invalid == 4) {
            ResourceGraphNode extra;
            extra.resource = candidate<Resource>(fixture.descriptor(origin, isPlant<Resource>));
            denied.graph.nodes.push_back(std::move(extra));
        }
        if (invalid == 5)
            denied.graph.rootResourceId = "00000000-0000-4000-8000-000000000099";
        completion = {};
        assert(
            executor.submit(std::move(denied), finish) == ResourceGraphSubmitStatus::InvalidRequest
            && !completion.done && prior.ready());
        manager.setBudgetConfig(managerBudget);
    }
    if (failure == 9) {
        assert(mounts->unmount(origin.mount));
        origin.mount = mounts->mount("source", backend);
        assert(origin.mount);
    }
    auto pending = failure == 6 || failure == 9
        ? candidate<Resource>(fixture.descriptor(origin, isPlant<Resource>))
        : prior.get()->makeLodCandidate(3);
    auto request = requestFor(std::move(pending));
    request.admission = ResourceGraphAdmission::ReloadSingle;
    request.expectedGeneration = prior.generation();
    core::runtime::CancellationSource cancellation;
    request.cancellation = cancellation.token();

    struct Gate {
        unsigned calls = 0;
        MountedFileBackend* mounts;
        core::PoolHandle mount;
        unsigned failure;
    } gate{0, mounts.get(), origin.mount, failure};

    if (failure == 3 || failure == 5)
        request.graph.context.commitGate = {[](void* context) noexcept {
                                                auto& value = *static_cast<Gate*>(context);
                                                if (++value.calls == 2) {
                                                    if (value.failure == 5)
                                                        value.mounts->unmount(value.mount);
                                                    else
                                                        return false;
                                                }
                                                return true;
                                            },
            &gate};
    if (failure == 7)
        request.budget.maxFileReads = 1;
    completion = {};
    assert(executor.submit(std::move(request), finish) == ResourceGraphSubmitStatus::Accepted);
    if (failure == 1)
        cancellation.cancel();
    if (failure == 2)
        mounts->unmount(origin.mount);
    if (failure == 8)
        manager.setBudgetConfig({});
    ResourceHandle<Resource> concurrent;
    if (failure == 4) {
        auto inputs = fixture.sources(origin);
        ResourceLoadContext context;
        context.sources = &inputs;
        context.maxDecodedBytes = Fixture::decodedBudget;
        assert(manager.reloadResource(prior.get()->makeLodCandidate(2), context).success);
        concurrent = manager.find<Resource>(id<Resource>());
        check(*concurrent.get(), 2);
    }
    unified_io_fixture::wait(runtime.completionQueue(), [&] {
        return completion.done;
    });
    if (!failure && !completion.result.resource.success)
        std::fprintf(stderr,
            "LOD reload: %s; %s\n",
            completion.result.resource.diagnostics.c_str(),
            completion.result.diagnosticId ? completion.result.diagnosticId : "");
    assert(completion.result.resource.success == (!failure || failure == 6 || failure == 9));
    if (!failure) {
        auto loaded = manager.find<Resource>(id<Resource>());
        assert(!prior.ready() && loaded.generation() == prior.generation() + 1
            && completion.result.telemetry.submittedFileReads == 3);
        check(*loaded.get(), 3);
    } else if (failure == 9) {
        auto rebound = manager.find<Resource>(id<Resource>());
        assert(!prior.ready() && rebound.generation() == prior.generation() + 1
            && completion.result.telemetry.submittedFileReads == 1
            && rebound.get()->sourceDescriptor()->origin.mount == origin.mount);
        check(*rebound.get(), 1);
        // Expansion must use the newly committed mount, without a host fallback.
        auto expansion = requestFor(rebound.get()->makeLodCandidate(3));
        expansion.admission = ResourceGraphAdmission::ReloadSingle;
        expansion.expectedGeneration = rebound.generation();
        completion = {};
        assert(
            executor.submit(std::move(expansion), finish) == ResourceGraphSubmitStatus::Accepted);
        unified_io_fixture::wait(runtime.completionQueue(), [&] {
            return completion.done;
        });
        assert(completion.result.resource.success && !rebound.ready());
        check(*manager.find<Resource>(id<Resource>()).get(), 3);
    } else if (failure == 4) {
        assert(concurrent.ready()
            && manager.find<Resource>(id<Resource>()).get() == concurrent.get()
            && completion.result.resource.diagnostics == "ARP-RESOURCE-GRAPH-RELOAD-GENERATION");
    } else {
        assert(prior.ready() && manager.find<Resource>(id<Resource>()).get() == prior.get());
        check(*prior.get(), 1);
        if (failure == 3 || failure == 5)
            assert(gate.calls == 2);
        if (failure == 8)
            assert(completion.result.diagnosticId
                && std::string_view(completion.result.diagnosticId)
                    == "ARP-RESOURCE-GRAPH-RELOAD-BUDGET");
    }
    manager.releaseRetiredResources();
    runtime.shutdown();
    assert(!manager.retainedResourceCount() && !runtime.completionQueue().pending()
        && !runtime.completionQueue().reserved() && !mounts->telemetry().inFlight
        && !mounts->telemetry().reservedReadBytes);
}

void cycle(const Fixture& fixture, const char* url, bool native)
{
    if (url) {
        const auto backend = [&] {
            HttpFileBackendDesc desc;
            desc.baseUrl = url;
            desc.maxInFlight = 1;
            desc.maxBytesInFlight = 128 * 1024;
            desc.maxPathBytes = 256;
            desc.maxHeaderBytes = 8192;
            desc.ioChunkBytes = 1024;
            desc.timeoutMs = 3000;
            desc.cancellationPollMs = 10;
            return core::makeShared<HttpFileBackend>(desc);
        };
        graph<PlantAssetSet>(fixture, backend());
        graph<RockAssetSet>(fixture, backend());
    } else if (native) {
        unified_io_fixture::Workspace workspace;
        std::filesystem::create_directories(workspace.root / "plants");
        std::filesystem::create_directories(workspace.root / "rocks");
        for (const auto& file : fixture.files)
            workspace.write(
                file.path, {reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size()});
        auto desc = workspace.descriptor();
        desc.ioChunkBytes = 1024;
        graph<PlantAssetSet>(fixture, core::makeShared<NativeDirectoryFileBackend>(desc));
        graph<RockAssetSet>(fixture, core::makeShared<NativeDirectoryFileBackend>(desc));
    } else
        for (unsigned failure = 0; failure < 10; ++failure) {
            graph<PlantAssetSet>(fixture, memory(fixture), failure);
            graph<RockAssetSet>(fixture, memory(fixture), failure);
        }
}
} // namespace

int main(int argc, char** argv)
{
    Fixture fixture;
    if (argc == 3 && std::string_view(argv[1]) == "--emit") {
        const auto root = std::filesystem::u8path(argv[2]);
        std::filesystem::create_directories(root / "plants");
        std::filesystem::create_directories(root / "rocks");
        for (const auto& file : fixture.files) {
            std::ofstream stream(root / std::filesystem::u8path(file.path), std::ios::binary);
            stream.write(reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size());
            assert(stream.good());
        }
        return 0;
    }
    if (argc == 1) {
        auto backend = memory(fixture);
        MountedFileBackend mounts({1, 1, Fixture::sourceBudget, 32, 256});
        ResourceFileOrigin origin{FilePathScope::MountedResource, mounts.mount("source", backend)};
        contracts<PlantAssetSet>(fixture, origin);
        contracts<RockAssetSet>(fixture, origin);
        canonicalOrder(origin);
        cycle(fixture, nullptr, false);
        return 0;
    }
    const bool native = argc == 2 && std::string_view(argv[1]) == "--native";
    const char* url = argc == 3 && std::string_view(argv[1]) == "--http" ? argv[2] : nullptr;
    assert(native || url || (argc == 2 && std::string_view(argv[1]) == "--smoke"));
    for (unsigned i = 0; i < 20; ++i)
        cycle(fixture, url, native);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle(fixture, url, native);
    const auto after = core::defaultCpuAllocator().stats();
    std::printf("Plant/Rock LOD 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(before.allocationCount - before.deallocationCount),
        static_cast<unsigned long long>(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
