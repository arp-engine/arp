#include "assets/glb_importer_common.h"
#include "core/encoding/base64.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "resource/mesh_asset_resources.h"
#include "resource/resource_graph_executor.h"
#include "tests/fixtures/static_glb_geometry.h"
#include "tests/fixtures/ui_atlas_pixels.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cstdio>
#include <string_view>

namespace {
using namespace arp;
using namespace core::io;
using namespace resource;
constexpr const char* meshId = "00000000-0000-4000-8000-000000000060";
constexpr std::size_t sourceBudget = 65536;

core::pmr::Vector<std::uint8_t> fixture(bool repeated = false)
{
    auto json = tests::StaticGlbGeometryFixture::document();
    const auto at = json.find("external-slot-zero");
    json.replace(at, 18, "\\u4e2d\\u6587\\ud83d\\udc7d");
    const core::ByteBuffer png(
        std::begin(tests::uiAtlasPngBytes), std::end(tests::uiAtlasPngBytes));
    const auto encoded = core::Base64::encode(png);
    const std::string material = "\"name\":\"\\u4e2d\\u6587\\ud83d\\udc7d\"";
    const auto name = json.find(material);
    assert(name != std::string::npos);
    json.insert(name + material.size(),
        ",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.2,0.4,0.6,1],\"baseColorTexture\":{\"index\":0}}");
    json.insert(json.size() - 1,
        std::string(",\"images\":[{\"name\":\"image\",\"uri\":\"data:image/png;base64,")
            + encoded.c_str() + "\"}],\"textures\":[{\"source\":0}]");
    if (repeated) {
        const std::string roots = "\"nodes\":[0]";
        const auto position = json.rfind(roots);
        assert(position != std::string::npos);
        json.replace(position, roots.size(), "\"nodes\":[0,0]");
    }
    return tests::StaticGlbGeometryFixture::bytes(json);
}

MeshGlbSourceDesc descriptor(ResourceFileOrigin origin = {})
{
    MeshGlbSourceDesc desc;
    desc.path = "model.glb";
    desc.origin = origin;
    desc.limits = {sourceBudget, 3, 3, 1, false, 32, 1024};
    desc.maxImportBytes = 512 * 1024;
    return desc;
}

void check(ResourceManager& manager, bool identity)
{
    const auto handle = manager.find<MeshAssetResource>(meshId);
    assert(handle.ready());
    const auto& resource = *handle.get();
    assert(resource.mesh()->vertices.size() == 3 && resource.mesh()->indices.size() == 3
        && resource.mesh()->vertices[0].position[0] == 1.25f
        && resource.mesh()->vertices[2].position[2] == -.75f);
    assert(resource.materials().size() == 1 && resource.materials()[0].name == u8"中文👽"
        && resource.materials()[0].baseColorFactor[1] == .4f
        && resource.materials()[0].baseColorTexture == 0);
    assert(resource.images().size() == 1
        && resource.images()[0].encodedBytes.size() == sizeof(tests::uiAtlasPngBytes));
    for (std::size_t i = 0; i < sizeof(tests::uiAtlasPngBytes); ++i)
        assert(resource.images()[0].encodedBytes[i] == tests::uiAtlasPngBytes[i]);
    assert(resource.textures().size() == 1 && resource.textureResourceIds().size() == 1
        && resource.textures()[0].sourceImage == 0);
    assert(resource.sourceIdentityVersion() == (identity ? 1u : 0u));
    if (identity)
        assert(resource.sourceIdentities().size() == 1);
}

void jsonContracts()
{
    assets::detail::JsonValue output;
    assert(!assets::detail::parseJson("{}", output, {}));
    assert(
        assets::detail::parseJson(R"({"name":"\u4e2d\u6587\ud83d\udc7d"})", output, {8, 8, false}));
    assert(output.find("name")->stringValue == u8"中文👽");
    assert(assets::detail::parseJson(u8R"({"name":"中文👽"})", output, {8, 8, false}));
    for (const auto* text : {R"({"name":"\ud83d"})",
             R"({"name":"\udc7d"})",
             R"({"name":"\u123"})",
             R"({"name":"\u12xz"})",
             R"({"name":"\ud83d\u1234"})",
             R"({"a":1,"\u0061":2})",
             R"({"a":01})",
             R"({"a":+1})",
             R"({"a":1e})"})
        assert(!assets::detail::parseJson(text, output, {8, 8, false}));
    assert(!assets::detail::parseJson(R"({"a":[[[1]]]})", output, {3, 8, false}));
    assert(!assets::detail::parseJson(R"([1,2,3])", output, {8, 3, false}));
    assert(!assets::detail::parseJson(R"({"name":"\u4e2d"})", output, {8, 8, true}));
}

void contracts()
{
    jsonContracts();
    const auto bytes = fixture();
    auto backend = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{1, sourceBudget, 1, sourceBudget, 128});
    MountedFileBackend mounts({1, 1, sourceBudget, 32, 128});
    const ResourceFileOrigin origin{
        FilePathScope::MountedResource, mounts.mount("source", backend)};
    for (unsigned test = 0; test < 14; ++test) {
        ResourceSourceSet sources;
        ResourceSourceBuffer source;
        source.path = "model.glb";
        source.pathScope = test == 2 ? FilePathScope::HostFilesystem : origin.pathScope;
        source.mount = test == 2 ? core::PoolHandle{} : origin.mount;
        source.bytes = bytes;
        if (test == 3)
            source.bytes.pop_back();
        if (test != 1)
            assert(sources.add(std::move(source)));
        ResourceLoadContext context;
        context.sources = &sources;
        context.requirePreloadedSources = true;
        auto desc = descriptor(origin);
        if (test == 4)
            desc.limits.maxSourceBytes = bytes.size() - 1;
        if (test == 5)
            desc.maxImportBytes = 1;
        if (test == 6)
            desc.limits.maxVertices = 2;
        if (test == 7)
            desc.limits.maxJsonDepth = 2;
        if (test == 8)
            desc.limits.maxJsonValues = 2;
        if (test == 9)
            desc.path = "../model.glb";
        if (test == 10)
            desc.origin.mount = {};
        if (test == 11)
            desc.maxImportBytes = UINT64_MAX;
        if (test == 12)
            context.maxDecodedBytes = 1;
        if (test == 13)
            desc.limits.maxSourceBytes = 0;
        ResourceManager manager;
        auto candidate = core::makeUnique<MeshAssetResource>(meshId, desc);
        candidate->setRetainSourceIdentity(true);
        const auto result = manager.loadResource(std::move(candidate), context);
        assert(result.success == (test == 0));
        if (test == 0)
            check(manager, true);
        else
            assert(!manager.find<MeshAssetResource>(meshId).ready());
    }
    ResourceManager manager;
    ResourceLoadContext context;
    context.requirePreloadedSources = true;
    auto legacy = core::makeUnique<MeshAssetResource>(meshId, std::filesystem::path("missing.glb"));
    core::pmr::Vector<ResourceFileReadIntent> intents;
    core::pmr::String diagnostic;
    assert(!legacy->collectFileReadIntents(manager, intents, diagnostic));
    assert(!manager.loadResource(std::move(legacy), context).success);
    // Missing mounted input has no native fallback even outside a graph.
    assert(!manager.loadResource(core::makeUnique<MeshAssetResource>(meshId, descriptor(origin)))
            .success);
    // Identity opt-in must not change ordinary repeated-occurrence import.
    const auto repeated = fixture(true);
    auto repeatedLimits = descriptor().limits;
    repeatedLimits.maxVertices = repeatedLimits.maxIndices = 6;
    repeatedLimits.maxPrimitives = 2;
    const core::ArrayView<const std::uint8_t> view(repeated.data(), repeated.size());
    const auto ordinary = assets::importStaticMeshFromGlbBytes(view, repeatedLimits);
    assert(ordinary.success && ordinary.mesh.vertices.size() == 6);
    assert(!assets::importStaticMeshWithSourceIdentityFromGlbBytes(view, repeatedLimits)
            .geometry.success);
    ResourceSourceSet sources;
    ResourceSourceBuffer initial;
    initial.path = "model.glb";
    initial.pathScope = origin.pathScope;
    initial.mount = origin.mount;
    initial.bytes = bytes;
    assert(sources.add(std::move(initial)));
    context.sources = &sources;
    core::pmr::Vector<core::UniquePtr<ResourceBase>> candidates;
    candidates.push_back(core::makeUnique<MeshAssetResource>(meshId, descriptor(origin)));
    assert(manager.loadResourcesAtomically(std::move(candidates), context).success);
    const auto prior = manager.find<MeshAssetResource>(meshId);
    const auto generation = prior.generation();
    sources.clear();
    ResourceSourceBuffer invalid;
    invalid.path = "model.glb";
    invalid.pathScope = origin.pathScope;
    invalid.mount = origin.mount;
    invalid.bytes = bytes;
    invalid.bytes.pop_back();
    assert(sources.add(std::move(invalid)));
    const auto failedReload = manager.reloadResource(
        core::makeUnique<MeshAssetResource>(meshId, descriptor(origin)), context);
    assert(!failedReload.success && failedReload.telemetry.rolledBack);
    assert(prior.ready() && manager.generationOf(meshId) == generation
        && manager.find<MeshAssetResource>(meshId).get() == prior.get());
}

void graph(core::SharedPtr<ManagedFileBackend> backend,
    bool identity,
    bool stale = false,
    bool cancel = false)
{
    auto mounts =
        core::makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 1, sourceBudget, 32, 128});
    const ResourceFileOrigin origin{
        FilePathScope::MountedResource, mounts->mount("source", backend)};
    core::runtime::AsyncRuntimeDesc desc;
    desc.enableWork = desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = mounts;
    desc.completionQueueCapacity = desc.completionBudget = 4;
    core::runtime::AsyncRuntime runtime(desc);
    ResourceManager manager;
    ResourceGraphExecutor executor(manager, runtime.fileService(), runtime.completionQueue());
    ResourceGraphExecutionRequest request;
    core::runtime::CancellationSource cancellation;
    request.cancellation = cancellation.token();
    request.graph.rootResourceId = meshId;
    ResourceGraphNode node;
    auto mesh = core::makeUnique<MeshAssetResource>(meshId, descriptor(origin));
    mesh->setRetainSourceIdentity(identity);
    mesh->setGroupSectionsByMaterial(true);
    mesh->setRetainSourceRanges(true);
    node.resource = std::move(mesh);
    request.graph.nodes.push_back(std::move(node));
    request.budget = {1, 1, sourceBudget, 65536, 4};

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
    if (stale)
        assert(mounts->unmount(origin.mount));
    if (cancel)
        cancellation.cancel();
    unified_io_fixture::wait(runtime.completionQueue(), [&] {
        return completion.done;
    });
    if (!stale && !cancel && !completion.result.resource.success)
        std::fprintf(
            stderr, "Mesh source graph: %s\n", completion.result.resource.diagnostics.c_str());
    assert(completion.result.resource.success == !(stale || cancel));
    if (!stale && !cancel) {
        check(manager, identity);
        assert(completion.result.telemetry.submittedFileReads == 1);
    }
    runtime.shutdown();
    assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved()
        && !mounts->telemetry().inFlight && !mounts->telemetry().reservedReadBytes);
}

void cycle(const char* url, bool native)
{
    if (url) {
        HttpFileBackendDesc desc;
        desc.baseUrl = url;
        desc.maxInFlight = 1;
        desc.maxBytesInFlight = 128 * 1024;
        desc.maxPathBytes = 128;
        desc.maxHeaderBytes = 8192;
        desc.ioChunkBytes = 1024;
        desc.timeoutMs = 3000;
        desc.cancellationPollMs = 10;
        auto backend = core::makeShared<HttpFileBackend>(desc);
        graph(backend, true);
        assert(!backend->telemetry().inFlight && !backend->telemetry().activeNativeTransfers);
    } else {
        const auto bytes = fixture();
        if (native) {
            unified_io_fixture::Workspace workspace;
            workspace.write(
                "model.glb", {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
            auto desc = workspace.descriptor();
            desc.ioChunkBytes = 1024;
            graph(core::makeShared<NativeDirectoryFileBackend>(desc), true);
            ResourceManager direct;
            auto nativeSource = descriptor();
            nativeSource.path = core::FilePath::utf8(workspace.root / "model.glb");
            assert(direct
                    .loadResource(
                        core::makeUnique<MeshAssetResource>(meshId, std::move(nativeSource)))
                    .success);
            check(direct, false);
        } else {
            contracts();
            for (const auto mode : {0, 1, 2, 3}) {
                auto backend = core::makeShared<MemoryFileBackend>(
                    MemoryFileBackendDesc{1, sourceBudget, 1, sourceBudget, 128});
                assert(backend->registerFile("model.glb", bytes.data(), bytes.size())
                    == FileIoStatus::Success);
                graph(backend, mode == 1, mode == 2, mode == 3);
            }
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--emit") {
        const auto bytes = fixture();
        std::ofstream output(std::filesystem::u8path(argv[2]), std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        assert(output.good());
        return 0;
    }
    const bool native = argc == 2 && std::string_view(argv[1]) == "--native";
    const char* url = argc == 3 && std::string_view(argv[1]) == "--http" ? argv[2] : nullptr;
    assert(argc == 1 || native || url || (argc == 2 && std::string_view(argv[1]) == "--smoke"));
    if (argc == 1) {
        cycle(nullptr, false);
        return 0;
    }
    for (unsigned i = 0; i < 20; ++i)
        cycle(url, native);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle(url, native);
    const auto after = core::defaultCpuAllocator().stats();
    std::printf("Mesh GLB 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(before.allocationCount - before.deallocationCount),
        static_cast<unsigned long long>(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
