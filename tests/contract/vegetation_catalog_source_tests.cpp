#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "resource/resource_graph_executor.h"
#include "tests/fixtures/unified_io_fixture.h"
#include "tests/fixtures/vegetation_catalog_source_fixture.h"

#include <cstdio>
#include <string_view>

namespace {
using namespace arp;
using namespace resource;
using namespace core::io;
using Fixture = tests::VegetationCatalogSourceFixture;

core::UniquePtr<VegetationCatalogResource> candidate(ResourceFileOrigin origin)
{
    return core::makeUnique<VegetationCatalogResource>(Fixture::descriptor(origin));
}

void check(const VegetationCatalogResource& resource)
{
    assert(
        resource.data && resource.data->assets.size() == 2 && resource.data->templates.size() == 2);
    const auto& data = *resource.data;
    assert(data.assets[0].id == "grass-a" && data.assets[0].templateId == 1
        && data.assets[1].id == "rock-b" && data.assets[1].templateId == 2
        && data.templateIdFor("grass-a") == 1 && data.templateIdFor("rock-b") == 2);
    assert(data.validation.required.size() == 6);
    assert(data.templates[0].vertices.size() == 3 && data.templates[1].vertices.size() == 3
        && data.templates[0].vertices[0].position[0] == 12.5f
        && data.templates[0].baseColor[1] == .4f && data.templates[0].sections.size() == 1);
    assert(resource.processedByteCount() > 0);
}

core::SharedPtr<MemoryFileBackend> memory(const Fixture& fixture)
{
    auto backend = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{5, 3 * Fixture::sourceBudget, 1, Fixture::sourceBudget, 256});
    assert(backend->registerDirectory("runtime") == FileIoStatus::Success);
    assert(backend->registerDirectory("runtime/models") == FileIoStatus::Success);
    const auto sources = fixture.sources({});
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const auto& source = *sources.sourceAt(i);
        assert(backend->registerFile(source.path, source.bytes.data(), source.bytes.size())
            == FileIoStatus::Success);
    }
    return backend;
}

void replace(core::pmr::String& text, std::string_view from, std::string_view to)
{
    const auto at = text.find(from.data(), 0, from.size());
    assert(at != core::pmr::String::npos);
    text.replace(at, from.size(), to.data(), to.size());
}

void contracts()
{
    Fixture fixture;
    auto backend = memory(fixture);
    MountedFileBackend mounts({1, 1, Fixture::sourceBudget, 32, 256});
    ResourceFileOrigin origin{FilePathScope::MountedResource, mounts.mount("source", backend)};
    auto sources = fixture.sources(origin);
    ResourceLoadContext context;
    context.sources = &sources;
    context.maxDecodedBytes = Fixture::decodedBudget;
    ResourceManager manager;
    assert(manager.loadResource(candidate(origin), context).success);
    auto prior = manager.find<VegetationCatalogResource>(Fixture::id);
    check(*prior.get());
    for (unsigned test = 0; test < 25; ++test) {
        auto bad = fixture;
        auto desc = Fixture::descriptor(origin);
        if (test == 0)
            desc.sourceLimits.reset();
        if (test == 1)
            desc.sourceLimits->maxAssets = 1;
        if (test == 2)
            desc.sourceLimits->maxCatalogBytes = 1;
        if (test == 3) {
            desc.sourceLimits->maxCatalogBytes =
                std::max(fixture.source.size(), fixture.runtime.size());
            desc.sourceLimits->maxTotalSourceBytes = desc.sourceLimits->maxCatalogBytes;
        }
        if (test == 4)
            desc.sourceLimits->maxDecodedBytes = 1;
        if (test == 5)
            desc.sourceLimits->maxImportBytes = 16 * 1024 * 1024 + 1;
        if (test == 6)
            desc.sourceLimits->mesh.maxDecodedBytes = 1;
        if (test == 7)
            desc.sourceLimits->mesh.geometry.maxVertices = 2;
        if (test == 8)
            replace(bad.source, "runtime/catalog.yaml", "../outside.yaml");
        if (test == 9)
            replace(bad.runtime, "models/plant.glb", "../outside.glb");
        if (test == 10)
            replace(bad.runtime, "id: rock-b", "id: different");
        if (test == 11)
            replace(bad.runtime, "templateCategory: rock_mossy_01", "templateCategory: typo");
        if (test == 12) {
            replace(bad.source, "id: rock-b", "id: grass-a");
            replace(bad.runtime, "id: rock-b", "id: grass-a");
        }
        if (test == 13)
            replace(bad.source, "contractVersion: 2", "contractVersion: 1");
        if (test == 14)
            bad.source = "type: VegetationCatalogAssetRecord\nfields: {}\n";
        if (test == 15)
            bad.geometry.pop_back();
        if (test == 16)
            desc.sourceOrigin.mount.generation += 1;
        if (test == 17)
            context.maxDecodedBytes = 1;
        if (test == 18)
            context.maxDecodedBytes = 0;
        if (test == 19)
            bad.source.push_back('\0');
        if (test == 20)
            desc.sourceLimits->mesh.colorPolicy = assets::StaticMeshVertexColorPolicy::Unspecified;
        if (test == 21)
            replace(bad.runtime, "source: models/plant.glb", "source: models/other.glb");
        if (test == 22)
            desc.sourceLimits->maxMetadataDepth = 1;
        if (test == 23)
            desc.sourceLimits->maxMetadataValues = 1;
        if (test == 24)
            desc.sourceLimits->maxMetadataCollectionLength = 4;
        sources = bad.sources(origin);
        const auto failed =
            manager.reloadResource(core::makeUnique<VegetationCatalogResource>(desc), context);
        if (failed.success)
            std::fprintf(stderr, "unexpected catalog success %u\n", test);
        assert(!failed.success && failed.telemetry.rolledBack && prior.ready()
            && manager.find<VegetationCatalogResource>(Fixture::id).get() == prior.get());
        check(*prior.get());
        context.maxDecodedBytes = Fixture::decodedBudget;
    }
    sources = fixture.sources(origin);
    assert(manager.reloadResource(candidate(origin), context).unchangedContent && prior.ready());
    auto bom = fixture;
    bom.source.insert(0, "\xef\xbb\xbf");
    sources = bom.sources(origin);
    const auto reloaded = manager.reloadResource(candidate(origin), context);
    assert(reloaded.success && reloaded.telemetry.committed && !prior.ready());
    check(*manager.find<VegetationCatalogResource>(Fixture::id).get());
    auto current = manager.find<VegetationCatalogResource>(Fixture::id);
    auto profile = Fixture::descriptor(origin);
    profile.sourceLimits->mesh.maxNodeVisits += 1;
    assert(manager.reloadResource(core::makeUnique<VegetationCatalogResource>(profile), context)
               .telemetry.committed
        && !current.ready());
    current = manager.find<VegetationCatalogResource>(Fixture::id);
    assert(mounts.unmount(origin.mount));
    origin.mount = mounts.mount("source", backend);
    profile.sourceOrigin = origin;
    sources = bom.sources(origin);
    assert(manager.reloadResource(core::makeUnique<VegetationCatalogResource>(profile), context)
               .telemetry.committed
        && !current.ready());
    assert(manager.find<VegetationCatalogResource>(Fixture::id).get()->loadDesc.sourceOrigin.mount
        == origin.mount);
    auto malformed = fixture;
    replace(malformed.runtime, "id: rock-b", "id: foreign");
    sources = malformed.sources(origin);
    core::pmr::Vector<ResourceFileReadIntent> reads(1);
    reads[0].path = "retained";
    core::pmr::String diagnostic;
    assert(!candidate(origin)->planFileReadIntents(manager, context, reads, diagnostic)
        && reads.size() == 1 && reads[0].path == "retained" && !diagnostic.empty());
    // A source-only HostFilesystem request cannot reopen real files.
    unified_io_fixture::Workspace workspace;
    workspace.write("source.yaml", fixture.source);
    auto host = Fixture::descriptor({});
    host.catalogPath = workspace.root / "source.yaml";
    ResourceLoadContext absent;
    absent.maxDecodedBytes = Fixture::decodedBudget;
    assert(
        !manager.reloadResource(core::makeUnique<VegetationCatalogResource>(host), absent).success);
}

void graph(core::SharedPtr<ManagedFileBackend> backend, unsigned failure = 0)
{
    auto mounts = core::makeShared<MountedFileBackend>(
        MountedFileBackendDesc{1, 1, Fixture::sourceBudget, 32, 256});
    ResourceFileOrigin origin{FilePathScope::MountedResource, mounts->mount("source", backend)};
    core::runtime::AsyncRuntimeDesc desc;
    desc.enableWork = desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = mounts;
    desc.completionQueueCapacity = desc.completionBudget = 4;
    core::runtime::AsyncRuntime runtime(desc);
    ResourceManager manager;
    ResourceGraphExecutor executor(manager, runtime.fileService(), runtime.completionQueue());
    ResourceGraphExecutionRequest request;
    request.graph.rootResourceId = Fixture::id;
    ResourceGraphNode node;
    node.resource = candidate(origin);
    request.graph.nodes.push_back(std::move(node));
    request.budget = {1, 3, 3 * Fixture::sourceBudget, Fixture::decodedBudget, 4};
    core::runtime::CancellationSource cancel;
    request.cancellation = cancel.token();
    if (failure == 3)
        request.budget.maxFileReads = 2;

    struct Completion {
        bool done = false;
        ResourceGraphExecutionResult result;
    } completion;

    assert(executor.submit(std::move(request),
               {[](void* out, ResourceGraphExecutionResult result) {
                    auto& value = *static_cast<Completion*>(out);
                    value.result = std::move(result);
                    value.done = true;
                },
                   &completion})
        == ResourceGraphSubmitStatus::Accepted);
    if (failure == 1)
        cancel.cancel();
    if (failure == 2)
        mounts->unmount(origin.mount);
    unified_io_fixture::wait(runtime.completionQueue(), [&] {
        return completion.done;
    });
    if (!failure && !completion.result.resource.success)
        std::fprintf(stderr,
            "catalog graph: %s; %s; fileStatus=%u; reads=%llu\n",
            completion.result.resource.diagnostics.c_str(),
            completion.result.diagnosticId ? completion.result.diagnosticId : "",
            static_cast<unsigned>(completion.result.fileStatus),
            static_cast<unsigned long long>(completion.result.telemetry.submittedFileReads));
    assert(completion.result.resource.success == !failure);
    if (!failure) {
        check(*manager.find<VegetationCatalogResource>(Fixture::id).get());
        assert(completion.result.telemetry.submittedFileReads == 3);
    } else
        assert(!manager.find<VegetationCatalogResource>(Fixture::id).ready());
    if (failure == 4)
        assert(completion.result.telemetry.submittedFileReads == 2);
    runtime.shutdown();
    assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved()
        && !mounts->telemetry().inFlight && !mounts->telemetry().reservedReadBytes);
}

void write(const Fixture& fixture, const std::filesystem::path& root)
{
    std::filesystem::create_directories(root / "runtime/models");
    const auto sources = fixture.sources({});
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const auto& source = *sources.sourceAt(i);
        std::ofstream stream(root / std::filesystem::u8path(source.path), std::ios::binary);
        stream.write(reinterpret_cast<const char*>(source.bytes.data()), source.bytes.size());
        assert(stream.good());
    }
}

void legacyEquivalence(const Fixture& fixture)
{
    unified_io_fixture::Workspace workspace;
    write(fixture, workspace.root);
    auto desc = Fixture::descriptor({});
    desc.catalogPath = workspace.root / "source.yaml";
    desc.input = VegetationCatalogInput::LegacySourceCatalog;
    desc.sourceLimits.reset();
    ResourceManager legacy, acquired;
    assert(legacy.loadResource(core::makeUnique<VegetationCatalogResource>(desc)).success);
    auto source = Fixture::descriptor({});
    auto sources = fixture.sources({});
    ResourceLoadContext context;
    context.sources = &sources;
    context.maxDecodedBytes = Fixture::decodedBudget;
    assert(acquired.loadResource(core::makeUnique<VegetationCatalogResource>(source), context)
            .success);
    const auto& a = *legacy.find<VegetationCatalogResource>(Fixture::id).get()->data;
    const auto& b = *acquired.find<VegetationCatalogResource>(Fixture::id).get()->data;
    assert(a.assets.size() == b.assets.size() && a.templates.size() == b.templates.size());
    for (std::size_t i = 0; i < a.assets.size(); ++i) {
        assert(a.assets[i].id == b.assets[i].id && a.assets[i].templateId == b.assets[i].templateId
            && a.templates[i].indices == b.templates[i].indices);
        const auto& av = a.templates[i].vertices;
        const auto& bv = b.templates[i].vertices;
        assert(av.size() == bv.size());
        for (std::size_t j = 0; j < av.size(); ++j)
            assert(std::equal(
                std::begin(av[j].position), std::end(av[j].position), std::begin(bv[j].position)));
        assert(std::equal(std::begin(a.templates[i].baseColor),
            std::end(a.templates[i].baseColor),
            std::begin(b.templates[i].baseColor)));
    }
}

void cycle(const Fixture& fixture, const char* url, bool native)
{
    if (url) {
        HttpFileBackendDesc desc;
        desc.baseUrl = url;
        desc.maxInFlight = 1;
        // Explicit fixture allowance for one input plus transport headers,
        // URL/DNS/task storage; the backend checks its actual reservation.
        desc.maxBytesInFlight = 2 * Fixture::sourceBudget;
        desc.maxPathBytes = 256;
        desc.maxHeaderBytes = 8192;
        desc.ioChunkBytes = 1024;
        desc.timeoutMs = 3000;
        desc.cancellationPollMs = 10;
        graph(core::makeShared<HttpFileBackend>(desc));
    } else if (native) {
        unified_io_fixture::Workspace workspace;
        write(fixture, workspace.root);
        auto desc = workspace.descriptor();
        desc.ioChunkBytes = 1024;
        graph(core::makeShared<NativeDirectoryFileBackend>(desc));
    } else
        for (unsigned failure = 0; failure < 4; ++failure)
            graph(memory(fixture), failure);
}

void failedCycle(const Fixture& fixture)
{
    auto sources = fixture.sources({});
    ResourceLoadContext context;
    context.sources = &sources;
    context.maxDecodedBytes = Fixture::decodedBudget;
    ResourceManager manager;
    assert(manager.loadResource(candidate({}), context).success);
    const auto prior = manager.find<VegetationCatalogResource>(Fixture::id);
    for (unsigned failure = 0; failure < 4; ++failure) {
        auto bad = fixture;
        auto desc = Fixture::descriptor({});
        if (failure == 0)
            replace(bad.runtime, "id: rock-b", "id: foreign");
        if (failure == 1)
            bad.geometry.pop_back();
        if (failure == 2)
            desc.sourceLimits->maxImportBytes = 16 * 1024 * 1024 + 1;
        if (failure == 3)
            desc.sourceLimits->maxDecodedBytes = 1;
        sources = bad.sources({});
        const auto rejected =
            manager.reloadResource(core::makeUnique<VegetationCatalogResource>(desc), context);
        assert(!rejected.success && rejected.telemetry.rolledBack && prior.ready());
    }
}
} // namespace

int main(int argc, char** argv)
{
    Fixture fixture;
    if (argc == 3 && std::string_view(argv[1]) == "--emit") {
        write(fixture, std::filesystem::u8path(argv[2]));
        return 0;
    }
    if (argc == 1) {
        contracts();
        legacyEquivalence(fixture);
        auto invalid = fixture;
        replace(invalid.runtime, "id: rock-b", "id: foreign");
        graph(memory(invalid), 4);
        cycle(fixture, nullptr, false);
        return 0;
    }
    const bool native = argc == 2 && std::string_view(argv[1]) == "--native";
    const bool failures = argc == 2 && std::string_view(argv[1]) == "--failure-smoke";
    const char* url = argc == 3 && std::string_view(argv[1]) == "--http" ? argv[2] : nullptr;
    assert(native || failures || url || (argc == 2 && std::string_view(argv[1]) == "--smoke"));
    for (unsigned i = 0; i < 20; ++i)
        if (failures)
            failedCycle(fixture);
        else
            cycle(fixture, url, native);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        if (failures)
            failedCycle(fixture);
        else
            cycle(fixture, url, native);
    const auto after = core::defaultCpuAllocator().stats();
    std::printf("Vegetation catalog %s 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
        failures ? "failures" : "sources",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(before.allocationCount - before.deallocationCount),
        static_cast<unsigned long long>(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
