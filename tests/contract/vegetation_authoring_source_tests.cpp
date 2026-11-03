#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "resource/resource_source_io.h"
#include "resource/vegetation_catalog_build_service.h"
#include "tests/fixtures/unified_io_fixture.h"
#include "tests/fixtures/vegetation_authoring_fixture.h"

#include <cstdio>
#include <string_view>

namespace {
using namespace arp;
using namespace core::io;
using namespace resource;
using Fixture = tests::VegetationAuthoringFixture;
using Y = core::YamlContract;
constexpr std::size_t sourceBudget = 65536;

void check(const VegetationCatalogPrepareResult& result)
{
    if (!result.success)
        std::fprintf(stderr, "catalog source: %s\n", result.diagnostics.c_str());
    assert(result.success && result.data && result.decodedBytes == 204
        && result.importPeakBytes > result.decodedBytes);
    const auto* mesh = result.data->findTemplate("grass-a");
    assert(mesh && mesh->vertices.size() == 3 && mesh->indices.size() == 3);
    assert(mesh->vertices[0].position[0] == 2.5f && mesh->vertices[2].position[2] == -1.5f);
    assert(mesh->vertices[0].primaryBend == 0.25f && mesh->vertices[0].flutter == 0.5f
        && mesh->vertices[0].stiffness == 0.75f && mesh->vertices[0].normal[2] == 1.0f);
    assert(mesh->baseColor[0] == 0.125f && mesh->baseColor[3] == 1.0f);
    assert(mesh->sections.size() == 1 && mesh->sections[0].materialSlot == 0);
    assert(
        result.data->assets[0].materials[0].resourceId == "33c5dcdf-4f06-482a-94eb-26baf7c937fb");
}

core::pmr::String authoring()
{
    auto node = Fixture::document();
    node["maxSourceBytes"] = sourceBudget;
    node["maxTotalSourceBytes"] = sourceBudget;
    return Y::serialize(node);
}

ResourceSourceSet input(ResourceFileOrigin origin, const core::pmr::String& path = "plant.glb")
{
    ResourceSourceBuffer file;
    file.path = path;
    file.pathScope = origin.pathScope;
    file.mount = origin.mount;
    file.bytes = tests::StaticGlbGeometryFixture::bytes();
    ResourceSourceSet result;
    assert(result.add(std::move(file)));
    return result;
}

void contracts()
{
    VegetationCatalogBuildService service;
    auto backend = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{2, 2 * sourceBudget, 1, sourceBudget, 256});
    MountedFileBackend mounts({1, 1, sourceBudget, 32, 256});
    const ResourceFileOrigin origin{
        FilePathScope::MountedResource, mounts.mount("source", backend)};
    VegetationCatalogSourceDesc desc{{}, origin};
    auto sources = input(origin);
    ResourceLoadContext context;
    context.sources = &sources;
    const auto text = authoring();
    const auto good = service.prepareFromSources(text, desc, context);
    check(good);
    // Identical data remains valid after the acquired buffers are destroyed.
    sources.clear();
    check(good);
    for (unsigned test = 0; test < 17; ++test) {
        auto node = Fixture::document();
        auto source = desc;
        sources = input(origin);
        if (test == 0)
            sources.clear();
        if (test == 1)
            source.origin.mount.generation += 1;
        if (test == 2)
            source.sourceRoot = "../outside";
        if (test == 3)
            source.sourceRoot.assign(1, static_cast<char>(0xff));
        if (test == 4)
            node["assets"][0]["source"] = "../outside.glb";
        if (test == 5)
            node["contractVersion"] = "unsupported";
        if (test == 6)
            node["maxSourceBytes"] = 1;
        if (test == 7)
            node["maxTotalSourceBytes"] = 1;
        if (test == 8)
            node["maxImportBytes"] = 1;
        if (test == 9)
            node["maxDecodedBytes"] = 1;
        if (test == 10)
            node["maxJsonValues"] = 1;
        if (test == 11)
            node["maxJsonDepth"] = 0;
        if (test == 12)
            source.origin.mount = {};
        if (test == 13) {
            sources.clear();
            auto truncated = tests::StaticGlbGeometryFixture::bytes();
            truncated.pop_back();
            ResourceSourceBuffer invalid;
            invalid.path = "plant.glb";
            invalid.pathScope = origin.pathScope;
            invalid.mount = origin.mount;
            invalid.bytes = std::move(truncated);
            assert(sources.add(std::move(invalid)));
        }
        auto yaml = Y::serialize(node);
        if (test == 14)
            yaml.push_back(0);
        if (test == 15)
            yaml.push_back(static_cast<char>(0xff));
        if (test == 16)
            yaml.assign(VegetationCatalogBuildService::maxAuthoringBytes + 1, 'a');
        const auto failed = service.prepareFromSources(yaml, source, context);
        assert(!failed.success && !failed.data && !failed.diagnostics.empty());
        check(good);
    }
    sources = input(origin);
    core::pmr::String bom("\xef\xbb\xbf");
    bom += text;
    check(service.prepareFromSources(bom, desc, context));
    core::pmr::Vector<ResourceFileReadIntent> reads;
    core::pmr::String diagnostic;
    assert(service.collectSourceReadIntents(text, desc, reads, diagnostic));
    assert(reads.size() == 1 && reads[0].path == "plant.glb" && reads[0].mount == origin.mount
        && reads[0].maxBytes == sourceBudget);
    const auto original = reads[0];
    auto bad = Fixture::document();
    bad["assets"][0]["source"] = "../outside.glb";
    assert(!service.collectSourceReadIntents(Y::serialize(bad), desc, reads, diagnostic));
    assert(reads.size() == 1 && reads[0].path == original.path);
    // This is a planning service, not semantic acceptance of an unfinished catalog.
    auto repeated = Fixture::document();
    auto& entries = repeated["assets"].get_value_ref<Y::Node::sequence_type&>();
    entries.push_back(entries.front());
    entries[1]["assetId"] = "grass-b";
    entries[1]["templateId"] = 2;
    entries[1]["variantIndex"] = 1;
    repeated["maxDecodedBytes"] = 408;
    assert(service.collectSourceReadIntents(Y::serialize(repeated), desc, reads, diagnostic));
    assert(reads.size() == 1);
    const auto sourceBytes = tests::StaticGlbGeometryFixture::bytes().size();
    repeated["maxTotalSourceBytes"] = 2 * sourceBytes - 1;
    const auto totalFailure = service.prepareFromSources(Y::serialize(repeated), desc, context);
    assert(!totalFailure.success && !totalFailure.data
        && totalFailure.diagnostics == "CATALOG-AUTHORING-SOURCE-BUDGET");
    repeated["maxTotalSourceBytes"] = 2 * sourceBytes;
    const auto doubled = service.prepareFromSources(Y::serialize(repeated), desc, context);
    assert(doubled.success && doubled.data->templates.size() == 2
        && doubled.sourceBytes == 2 * sourceBytes && doubled.decodedBytes == 408);
    auto nested = desc;
    nested.sourceRoot = "nested";
    sources = input(origin, "nested/plant.glb");
    check(service.prepareFromSources(text, nested, context));

    // Even an existing native file must not rescue missing acquired inputs.
    unified_io_fixture::Workspace workspace;
    const auto bytes = tests::StaticGlbGeometryFixture::bytes();
    workspace.write("plant.glb", {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    check(service.prepare(text, workspace.root));
    VegetationCatalogSourceDesc host;
    host.sourceRoot = core::FilePath::utf8(workspace.root);
    host.origin.pathScope = FilePathScope::HostFilesystem;
    const auto missing = service.prepareFromSources(text, host, {});
    assert(!missing.success && missing.diagnostics == "ARP-RESOURCE-SOURCE-NOT-PRELOADED");
    sources = input(host.origin, core::FilePath::utf8(workspace.root / "plant.glb"));
    check(service.prepareFromSources(text, host, context));
}

void acquired(core::SharedPtr<ManagedFileBackend> backend, unsigned failure = 0)
{
    auto mounts =
        core::makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 1, sourceBudget, 32, 256});
    const ResourceFileOrigin origin{
        FilePathScope::MountedResource, mounts->mount("source", backend)};
    core::runtime::AsyncRuntimeDesc runtimeDesc;
    runtimeDesc.enableWork = runtimeDesc.enableThreadPoolFileBackend = false;
    runtimeDesc.fileBackend = mounts;
    runtimeDesc.completionQueueCapacity = runtimeDesc.completionBudget = 4;
    core::runtime::AsyncRuntime runtime(runtimeDesc);
    ResourceSourceSet sources;
    ResourceLoadContext context;
    context.sources = &sources;
    VegetationCatalogBuildService service;
    VegetationCatalogSourceDesc desc{{}, origin};
    core::runtime::CancellationSource cancellation;
    const auto fetch = [&](const ResourceFileReadIntent& intent) {
        FileReadRequest request;
        request.contractVersion = 1;
        request.path = intent.path;
        request.pathScope = intent.pathScope;
        request.mount = intent.mount;
        request.maxBytes = intent.maxBytes;
        request.cancellation = cancellation.token();
        unified_io_fixture::Probe<FileReadResult> probe;
        assert(runtime.fileService().read(request, {decltype(probe)::complete, &probe})
            == FileIoStatus::Accepted);
        if (failure == 1)
            mounts->unmount(origin.mount);
        if (failure == 2)
            cancellation.cancel();
        unified_io_fixture::wait(runtime.completionQueue(), [&] {
            return probe.calls;
        });
        assert(probe.calls == 1);
        if (failure) {
            assert(probe.result.status != FileIoStatus::Success);
            return false;
        }
        assert(probe.result.status == FileIoStatus::Success);
        ResourceSourceBuffer file;
        file.path = intent.path;
        file.pathScope = origin.pathScope;
        file.mount = origin.mount;
        file.bytes = std::move(probe.result.bytes);
        assert(sources.add(std::move(file)));
        return true;
    };
    ResourceFileReadIntent metadata;
    metadata.path = "authoring.yaml";
    metadata.pathScope = origin.pathScope;
    metadata.mount = origin.mount;
    metadata.maxBytes = sourceBudget;
    if (fetch(metadata)) {
        auto text = readResourceText(metadata.path, context, sourceBudget, &origin);
        assert(text.success);
        core::pmr::Vector<ResourceFileReadIntent> reads;
        core::pmr::String diagnostic;
        assert(service.collectSourceReadIntents(text.text, desc, reads, diagnostic));
        for (const auto& read : reads)
            assert(fetch(read));
        mounts->unmount(origin.mount);
        runtime.shutdown();
        // Preparation consumes only the snapshot after the backend is gone.
        const auto prepared = service.prepareFromSources(text.text, desc, context);
        sources.clear();
        check(prepared);
    }
    runtime.shutdown();
    assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved()
        && !mounts->telemetry().inFlight && !mounts->telemetry().reservedReadBytes);
}

void cycle(const char* url, bool native)
{
    const auto text = authoring();
    const auto bytes = tests::StaticGlbGeometryFixture::bytes();
    if (url) {
        HttpFileBackendDesc desc;
        desc.baseUrl = url;
        desc.maxInFlight = 1;
        desc.maxBytesInFlight = 128 * 1024;
        desc.maxPathBytes = 256;
        desc.maxHeaderBytes = 8192;
        desc.ioChunkBytes = 1024;
        desc.timeoutMs = 3000;
        desc.cancellationPollMs = 10;
        auto backend = core::makeShared<HttpFileBackend>(desc);
        acquired(backend);
        assert(!backend->telemetry().inFlight && !backend->telemetry().activeNativeTransfers);
    } else if (native) {
        unified_io_fixture::Workspace workspace;
        workspace.write("authoring.yaml", text);
        workspace.write("plant.glb", {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
        auto desc = workspace.descriptor();
        desc.ioChunkBytes = 1024;
        acquired(core::makeShared<NativeDirectoryFileBackend>(desc));
    } else {
        for (unsigned failure = 0; failure < 3; ++failure) {
            auto backend = core::makeShared<MemoryFileBackend>(
                MemoryFileBackendDesc{2, 2 * sourceBudget, 1, sourceBudget, 256});
            assert(backend->registerFile("authoring.yaml",
                       reinterpret_cast<const std::uint8_t*>(text.data()),
                       text.size())
                == FileIoStatus::Success);
            assert(backend->registerFile("plant.glb", bytes.data(), bytes.size())
                == FileIoStatus::Success);
            acquired(backend, failure);
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--emit") {
        const auto root = std::filesystem::u8path(argv[2]);
        const auto text = authoring();
        std::ofstream metadata(root / "authoring.yaml", std::ios::binary);
        metadata.write(text.data(), text.size());
        const auto bytes = tests::StaticGlbGeometryFixture::bytes();
        std::ofstream mesh(root / "plant.glb", std::ios::binary);
        mesh.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        assert(metadata.good() && mesh.good());
        return 0;
    }
    if (argc == 1) {
        contracts();
        cycle(nullptr, false);
        return 0;
    }
    const bool native = argc == 2 && std::string_view(argv[1]) == "--native";
    const char* url = argc == 3 && std::string_view(argv[1]) == "--http" ? argv[2] : nullptr;
    assert(native || url || (argc == 2 && std::string_view(argv[1]) == "--smoke"));
    for (unsigned i = 0; i < 20; ++i)
        cycle(url, native);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle(url, native);
    const auto after = core::defaultCpuAllocator().stats();
    std::printf("Vegetation authoring 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(before.allocationCount - before.deallocationCount),
        static_cast<unsigned long long>(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
