#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "resource/resource_graph_executor.h"
#include "resource/resource_source_io.h"
#include "tests/fixtures/shadow_lod_source_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cstdio>
#include <string_view>

namespace {
using namespace arp;
using namespace resource;
using namespace core::io;
using Fixture = tests::ShadowLodSourceFixture;
using Y = core::YamlContract;
constexpr std::size_t decodedBudget = 128 * 1024;

StaticMeshShadowLodSourceDesc descriptor(ResourceFileOrigin origin, unsigned version)
{
    StaticMeshShadowLodSourceDesc desc;
    desc.manifestPath = version == 1 ? "lod/manifest1.yaml" : "lod/manifest2.yaml";
    desc.origin = origin;
    desc.maxSourceBytes = Fixture::sourceBudget;
    desc.maxImportBytes = 2 * 1024 * 1024;
    desc.maxJsonDepth = 32;
    desc.maxJsonValues = 1024;
    return desc;
}

core::UniquePtr<MeshAssetResource> original(ResourceFileOrigin origin, float uvScale = 1)
{
    MeshGlbSourceDesc desc;
    desc.path = "source.glb";
    desc.origin = origin;
    desc.limits = {Fixture::sourceBudget, 3, 3, 1, true, 32, 1024};
    desc.maxImportBytes = 512 * 1024;
    auto result = core::makeUnique<MeshAssetResource>(Fixture::originalId, std::move(desc));
    result->setGroupSectionsByMaterial(true);
    result->setRetainSourceRanges(true);
    result->setRetainSourceIdentity(true);
    result->setUvScale(uvScale);
    return result;
}

void check(ResourceManager& manager, unsigned version)
{
    const auto handle = manager.find<StaticMeshShadowLodResource>(Fixture::lodId);
    assert(handle.ready());
    const auto& lod = *handle.get();
    assert(lod.sourceGeneration() == manager.generationOf(Fixture::originalId)
        && lod.sources().size() == 1 && lod.indexOrderVersion() == (version == 2 ? 1u : 0u));
    assert(lod.shadowTexelCeiling() == 1 && lod.normalizedDepthCeiling() == 1);
    assert(lod.sources()[0].approximable && lod.sources()[0].surfaceError[1] == 0
        && lod.sources()[0].surfaceError[2] == 0);
    for (unsigned level : {1u, 2u}) {
        const auto& candidate = lod.candidate(level);
        assert(candidate.validForIndexedDraw() && candidate.vertices.size() == 3
            && candidate.indices.size() == 3 && candidate.vertices[0].position[0] == 1.25f);
    }
    assert(lod.reorderedOriginalIndices().size() == (version == 2 ? 3u : 0u));
}

core::SharedPtr<MemoryFileBackend> memory(const Fixture& fixture)
{
    auto backend = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{8, 7 * Fixture::sourceBudget, 1, Fixture::sourceBudget, 256});
    assert(backend->registerDirectory("lod") == FileIoStatus::Success);
    for (const auto& file : fixture.files)
        assert(backend->registerFile(file.path, file.bytes.data(), file.bytes.size())
            == FileIoStatus::Success);
    return backend;
}

// Decode only fixture mutation input; production decoding is tested by load.
Y::Node sourceDocumentForTest(const core::pmr::Vector<std::uint8_t>& bytes)
{
    return Y::parseSingleDocument(
        core::pmr::String(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

void directContracts()
{
    Fixture fixture;
    auto backend = memory(fixture);
    MountedFileBackend mounts({1, 1, Fixture::sourceBudget, 32, 256});
    ResourceFileOrigin origin{FilePathScope::MountedResource, mounts.mount("source", backend)};
    for (unsigned version : {1u, 2u}) {
        ResourceManager manager;
        auto sources = fixture.sources(origin);
        ResourceLoadContext context;
        context.sources = &sources;
        context.requirePreloadedSources = true;
        context.maxDecodedBytes = decodedBudget;
        assert(manager.loadResource(original(origin), context).success);
        assert(manager
                .loadResource(core::makeUnique<StaticMeshShadowLodResource>(
                                  Fixture::lodId, Fixture::originalId, descriptor(origin, version)),
                    context)
                .success);
        check(manager, version);
        const auto prior = manager.find<StaticMeshShadowLodResource>(Fixture::lodId);
        const auto generation = prior.generation();
        for (unsigned test = 0; test < 24; ++test) {
            auto changed = fixture;
            auto& manifestFile =
                changed.file(version == 1 ? "lod/manifest1.yaml" : "lod/manifest2.yaml");
            auto node = sourceDocumentForTest(manifestFile.bytes);
            if (test == 0)
                changed.file("lod/one.glb").bytes[0] ^= 1;
            if (test == 1)
                changed.file("lod/cert.yaml").bytes.pop_back();
            if (test == 2)
                node["levels"][0]["source"] = "../outside.glb";
            if (test == 3)
                node["source"] = "wrong.glb";
            if (test == 4)
                node["sourceSha256"] = std::string(64, '0').c_str();
            if (test == 5)
                node["contractVersion"] = 99;
            if (test == 6)
                node["levels"][0]["bytes"] = 1;
            if (test == 12)
                node["levels"][0]["sha256"] = "invalid";
            if (test == 13)
                node["levels"][0]["source"] = "missing.glb";
            if (test == 14)
                node["type"] = "WrongType";
            const auto text = Y::serialize(node);
            manifestFile.bytes.assign(text.begin(), text.end());
            if (test == 7)
                manifestFile.bytes.push_back(0);
            if (test == 8)
                manifestFile.bytes.push_back(0xff);
            sources = changed.sources(origin);
            auto desc = descriptor(origin, version);
            if (test == 9)
                desc.maxImportBytes = 1;
            if (test == 10)
                desc.maxSourceBytes = 0;
            if (test == 11)
                desc.maxJsonValues = 2;
            if (test == 15)
                desc.projectRoot = "../outside";
            if (test == 16)
                desc.projectRoot.assign(1, static_cast<char>(0xff));
            if (test == 17)
                desc.manifestPath = "../outside.yaml";
            if (test == 18)
                desc.origin.mount = {};
            if (test == 19)
                desc.origin.mount.generation += 1;
            if (test == 20)
                desc.maxJsonDepth = 0;
            if (test == 21)
                desc.maxImportBytes = UINT64_MAX;
            if (test == 22)
                sources.clear();
            if (test == 23)
                context.maxDecodedBytes = 1;
            // Reload must enter the candidate loader. Duplicate batch admission
            // rejection would leave these malformed inputs entirely untested.
            const auto result =
                manager.reloadResource(core::makeUnique<StaticMeshShadowLodResource>(
                                           Fixture::lodId, Fixture::originalId, std::move(desc)),
                    context);
            if (result.success)
                std::fprintf(stderr, "unexpected Shadow success %u\n", test);
            assert(!result.success && result.telemetry.rolledBack && !result.diagnostics.empty()
                && prior.ready() && prior.generation() == generation
                && manager.find<StaticMeshShadowLodResource>(Fixture::lodId).get() == prior.get());
            context.maxDecodedBytes = decodedBudget;
        }
        if (version == 2) {
            for (unsigned test = 0; test < 3; ++test) {
                auto changed = fixture;
                auto order = sourceDocumentForTest(changed.file("lod/order.yaml").bytes);
                if (test == 0)
                    order["contractVersion"] = 99;
                if (test == 1)
                    order["sources"][0]["node"] = 99;
                if (test == 2)
                    order["sources"] = Y::Node::sequence();
                const auto orderText = Y::serialize(order);
                auto& orderBytes = changed.file("lod/order.yaml").bytes;
                orderBytes.assign(orderText.begin(), orderText.end());
                auto manifest = sourceDocumentForTest(changed.file("lod/manifest2.yaml").bytes);
                manifest["indexOrder"] = changed.binding("lod/order.yaml");
                const auto text = Y::serialize(manifest);
                changed.file("lod/manifest2.yaml").bytes.assign(text.begin(), text.end());
                sources = changed.sources(origin);
                const auto result = manager.reloadResource(
                    core::makeUnique<StaticMeshShadowLodResource>(
                        Fixture::lodId, Fixture::originalId, descriptor(origin, version)),
                    context);
                const char* expected[] = {"SHADOW-LOD-INDEX-ORDER-CONTRACT",
                    "SHADOW-LOD-INDEX-ORDER-BIJECTION",
                    "SHADOW-LOD-INDEX-ORDER-COUNT"};
                assert(!result.success && result.telemetry.rolledBack
                    && result.diagnostics == expected[test] && prior.ready()
                    && manager.generationOf(Fixture::lodId) == generation
                    && manager.find<StaticMeshShadowLodResource>(Fixture::lodId).get()
                        == prior.get());
            }
        }
        // Text/1 strips a UTF-8 BOM for parsing, but bindings hash the raw bytes.
        auto bom = fixture;
        for (const auto path : {"lod/cert.yaml", "lod/order.yaml"}) {
            auto& bytes = bom.file(path).bytes;
            bytes.insert(bytes.begin(), {0xef, 0xbb, 0xbf});
        }
        auto& bomManifest = bom.file(version == 1 ? "lod/manifest1.yaml" : "lod/manifest2.yaml");
        auto bomNode = sourceDocumentForTest(bomManifest.bytes);
        bomNode["certificates"] = bom.binding("lod/cert.yaml");
        if (version == 2)
            bomNode["indexOrder"] = bom.binding("lod/order.yaml");
        const auto bomText = Y::serialize(bomNode);
        bomManifest.bytes.assign({0xef, 0xbb, 0xbf});
        bomManifest.bytes.insert(bomManifest.bytes.end(), bomText.begin(), bomText.end());
        sources = bom.sources(origin);
        ResourceManager bomManager;
        assert(bomManager.loadResource(original(origin), context).success);
        assert(bomManager
                .loadResource(core::makeUnique<StaticMeshShadowLodResource>(
                                  Fixture::lodId, Fixture::originalId, descriptor(origin, version)),
                    context)
                .success);
        check(bomManager, version);
        sources = fixture.sources(origin);
        auto pending = core::makeUnique<StaticMeshShadowLodResource>(
            Fixture::lodId, Fixture::originalId, descriptor(origin, version));
        core::pmr::Vector<ResourceFileReadIntent> intents;
        core::pmr::String diagnostic;
        assert(pending->planFileReadIntents(manager, context, intents, diagnostic));
        // Identical generation numbers in another owner do not revalidate a plan.
        const auto wrongOwner = pending->load(bomManager, context);
        assert(!wrongOwner.success
            && wrongOwner.diagnostics == "SHADOW-LOD-ORIGINAL-GENERATION-CHANGED");
        auto missingOriginal = core::makeUnique<StaticMeshShadowLodResource>(
            Fixture::lodId, "00000000-0000-4000-8000-000000000099", descriptor(origin, version));
        const auto missing = missingOriginal->load(manager, context);
        assert(!missing.success && missing.diagnostics == "SHADOW-LOD-ORIGINAL-NOT-READY");
        const auto updated = manager.reloadResource(original(origin, 2), context);
        assert(updated.success && updated.telemetry.committed && !updated.unchangedContent);
        const auto failed = pending->load(manager, context);
        assert(!failed.success && failed.diagnostics == "SHADOW-LOD-ORIGINAL-GENERATION-CHANGED");
    }
}

void graph(core::SharedPtr<ManagedFileBackend> backend, unsigned version, unsigned failure = 0)
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
    request.graph.rootResourceId = Fixture::lodId;
    ResourceGraphNode mesh;
    mesh.resource = original(origin);
    request.graph.nodes.push_back(std::move(mesh));
    ResourceGraphNode lod;
    lod.resource = core::makeUnique<StaticMeshShadowLodResource>(
        Fixture::lodId, Fixture::originalId, descriptor(origin, version));
    lod.dependencies = lod.resource->dependencies();
    request.graph.nodes.push_back(std::move(lod));
    request.budget = {2, 6, 6 * Fixture::sourceBudget, decodedBudget, 4};
    core::runtime::CancellationSource cancellation;
    request.cancellation = cancellation.token();

    struct Completion {
        bool done = false;
        ResourceGraphExecutionResult result;
    } completion;

    assert(executor.submit(std::move(request),
               {[](void* pointer, ResourceGraphExecutionResult result) {
                    auto& state = *static_cast<Completion*>(pointer);
                    state.result = std::move(result);
                    state.done = true;
                },
                   &completion})
        == ResourceGraphSubmitStatus::Accepted);
    if (failure == 1)
        mounts->unmount(origin.mount);
    if (failure == 2)
        cancellation.cancel();
    unified_io_fixture::wait(runtime.completionQueue(), [&] {
        return completion.done;
    });
    if (!failure && !completion.result.resource.success)
        std::fprintf(stderr,
            "Shadow graph: %s; %s\n",
            completion.result.resource.diagnostics.c_str(),
            completion.result.diagnosticId ? completion.result.diagnosticId : "");
    assert(completion.result.resource.success == !failure);
    if (!failure) {
        check(manager, version);
        assert(completion.result.telemetry.submittedFileReads == (version == 1 ? 5 : 6));
    }
    runtime.shutdown();
    assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved()
        && !mounts->telemetry().inFlight && !mounts->telemetry().reservedReadBytes);
}

void cycle(const Fixture& fixture, const char* url, bool native)
{
    for (unsigned version : {1u, 2u}) {
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
            graph(backend, version);
            assert(!backend->telemetry().inFlight && !backend->telemetry().activeNativeTransfers);
        } else if (native) {
            unified_io_fixture::Workspace workspace;
            std::filesystem::create_directories(workspace.root / "lod");
            for (const auto& file : fixture.files)
                workspace.write(file.path,
                    {reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size()});
            auto desc = workspace.descriptor();
            desc.ioChunkBytes = 1024;
            graph(core::makeShared<NativeDirectoryFileBackend>(desc), version);
        } else {
            graph(memory(fixture), version);
            graph(memory(fixture), version, 1);
            graph(memory(fixture), version, 2);
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    Fixture fixture;
    if (argc == 3 && std::string_view(argv[1]) == "--emit") {
        const auto root = std::filesystem::u8path(argv[2]);
        std::filesystem::create_directories(root / "lod");
        for (const auto& file : fixture.files) {
            std::ofstream stream(root / std::filesystem::u8path(file.path), std::ios::binary);
            stream.write(reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size());
            assert(stream.good());
        }
        return 0;
    }
    const bool native = argc == 2 && std::string_view(argv[1]) == "--native";
    const char* url = argc == 3 && std::string_view(argv[1]) == "--http" ? argv[2] : nullptr;
    assert(argc == 1 || native || url || (argc == 2 && std::string_view(argv[1]) == "--smoke"));
    if (argc == 1) {
        directContracts();
        cycle(fixture, nullptr, false);
        return 0;
    }
    for (unsigned i = 0; i < 20; ++i)
        cycle(fixture, url, native);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle(fixture, url, native);
    const auto after = core::defaultCpuAllocator().stats();
    std::printf("Shadow v1/v2 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(before.allocationCount - before.deallocationCount),
        static_cast<unsigned long long>(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
