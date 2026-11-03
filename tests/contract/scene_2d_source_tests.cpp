#include "2d/tile_resources.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "resource/resource_graph_executor.h"
#include "resource/resource_source_io.h"
#include "tests/fixtures/scene_2d_fixture.h"
#include "tests/fixtures/tile_map_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <array>
#include <cstdio>
#include <string_view>

namespace {
using namespace arp;
using namespace core::io;
using namespace resource;
constexpr std::size_t fileBudget = 65536;
constexpr const char* animationId = "00000000-0000-4000-8000-000000000010";

// Real authored files, acquired once by an explicit bounded test-tool boundary.
class SourceFixture final {
public:
    const std::array<const char*, 5> names{
        "atlas.yaml", "atlas_b.yaml", "atlas.png", "tileset.yaml", "tilemap.yaml"};
    std::array<core::pmr::Vector<std::uint8_t>, 5> bytes;

    SourceFixture()
    {
        for (std::size_t i = 0; i < names.size(); ++i) {
            auto input = core::readBinaryFile(
                std::filesystem::path(ARP_2D_SOURCE_FIXTURE_ROOT) / names[i], fileBudget);
            assert(input.success);
            bytes[i] = std::move(input.bytes);
        }
    }

    ResourceSourceSet sources(ResourceFileOrigin origin) const
    {
        ResourceSourceSet result;
        for (std::size_t i = 0; i < names.size(); ++i) {
            ResourceSourceBuffer source;
            source.path = names[i];
            source.pathScope = origin.pathScope;
            source.mount = origin.mount;
            source.bytes = bytes[i];
            assert(result.add(std::move(source)));
        }
        return result;
    }

    core::SharedPtr<MemoryFileBackend> memory() const
    {
        auto backend = core::makeShared<MemoryFileBackend>(
            MemoryFileBackendDesc{5, 5 * fileBudget, 2, 2 * fileBudget, 256});
        for (std::size_t i = 0; i < names.size(); ++i)
            assert(backend->registerFile(names[i], bytes[i].data(), bytes[i].size())
                == FileIoStatus::Success);
        return backend;
    }
};

void visuals(ResourceManager& manager, ResourceLoadContext context, ResourceFileOrigin origin)
{
    for (const auto& entry :
        {std::pair{tests::atlasA, "atlas.yaml"}, std::pair{tests::atlasB, "atlas_b.yaml"}})
        assert(manager
                .load<scene2d::SpriteAtlasResource>(context,
                    core::pmr::String(entry.first),
                    core::pmr::String(entry.second),
                    core::pmr::String("atlas.png"),
                    fileBudget,
                    origin)
                .success);
    assert(manager
            .load<scene2d::SpriteResource>(context,
                core::pmr::String(tests::spriteA),
                core::pmr::String(tests::atlasA),
                core::pmr::String("left"))
            .success);
    assert(manager
            .load<scene2d::SpriteResource>(context,
                core::pmr::String(tests::spriteB),
                core::pmr::String(tests::atlasB),
                core::pmr::String("right"))
            .success);
    core::pmr::Vector<scene2d::SpriteAnimationFrame> frames;
    frames.push_back({core::pmr::String(tests::spriteA), .125, 1});
    frames.push_back({core::pmr::String(tests::spriteB), .125, 2});
    assert(manager
            .load<scene2d::SpriteAnimationResource>(
                context, core::pmr::String(animationId), frames, true)
            .success);
}

void contracts(const SourceFixture& fixture)
{
    auto backend = fixture.memory();
    MountedFileBackend mounts({1, 1, fileBudget, 32, 256});
    const ResourceFileOrigin origin{
        FilePathScope::MountedResource, mounts.mount("source", backend)};
    {
        const auto sources = fixture.sources(origin);
        ResourceLoadContext context;
        context.sources = &sources;
        context.requirePreloadedSources = true;
        context.maxDecodedBytes = 511;
        ResourceManager manager;
        assert(!manager
                .load<scene2d::SpriteAtlasResource>(context,
                    core::pmr::String(tests::atlasA),
                    core::pmr::String("atlas.yaml"),
                    core::pmr::String("atlas.png"),
                    fileBudget,
                    origin)
                .success);
    }
    // Both collector and direct loads must reject invalid origins and budgets.
    for (const auto budget : {std::size_t(0), SIZE_MAX}) {
        scene2d::SpriteAtlasResource atlas(
            tests::atlasA, "atlas.yaml", "atlas.png", budget, origin);
        ResourceManager manager;
        core::pmr::Vector<ResourceFileReadIntent> intents;
        core::pmr::String diagnostic;
        assert(!atlas.collectFileReadIntents(manager, intents, diagnostic));
        assert(!atlas.load(manager, {}).success);
    }
    for (unsigned test = 0; test < 13; ++test) {
        auto changed = fixture;
        // BOM is metadata only; the supplied source bytes stay byte-identical.
        if (test == 0)
            for (std::size_t i : {0u, 1u, 3u, 4u})
                changed.bytes[i].insert(changed.bytes[i].begin(), {0xef, 0xbb, 0xbf});
        auto sources = changed.sources(test == 1 ? ResourceFileOrigin{} : origin);
        if (test == 2) {
            sources.clear();
            // A real host file exists here, but mounted cold input cannot reopen it.
            scene2d::SpriteAtlasResource mounted(tests::atlasA,
                "tests/fixtures/scene_2d/atlas.yaml",
                "tests/fixtures/scene_2d/atlas.png",
                fileBudget,
                origin);
            ResourceManager manager;
            assert(!mounted.load(manager, {}).success);
            continue;
        }
        if (test == 3) {
            sources.clear();
            for (std::size_t i = 0; i < changed.names.size(); ++i) {
                ResourceSourceBuffer source;
                source.path = changed.names[i];
                source.pathScope = i == 2 ? FilePathScope::HostFilesystem : origin.pathScope;
                source.mount = i == 2 ? core::PoolHandle{} : origin.mount;
                source.bytes = changed.bytes[i];
                assert(sources.add(std::move(source)));
            }
        }
        const unsigned malformed = test >= 4 ? (test - 4) % 3 : 0;
        const unsigned index = test < 7 ? 0 : test < 10 ? 3 : 4;
        if (test >= 4) {
            auto& input = changed.bytes[index];
            if (malformed == 0)
                input.push_back(0);
            else if (malformed == 1)
                input.push_back(0xff);
            else {
                const std::string_view old(
                    reinterpret_cast<const char*>(input.data()), input.size());
                const auto position = old.find("contractVersion: 1");
                assert(position != std::string_view::npos);
                input[position + std::string_view("contractVersion: ").size()] = '9';
            }
            sources = changed.sources(origin);
        }
        ResourceLoadContext context;
        context.requirePreloadedSources = true;
        context.sources = &sources;
        ResourceManager manager;
        if (test >= 7 || test == 0) {
            visuals(manager, context, origin);
            const auto set = manager.load<scene2d::TileSetResource>(context,
                core::pmr::String(tests::tileSetId),
                core::pmr::String("tileset.yaml"),
                fileBudget,
                origin);
            if (test >= 7 && test < 10) {
                assert(!set.success);
                continue;
            }
            assert(set.success);
            const auto map = manager.load<scene2d::TileMapResource>(context,
                core::pmr::String(tests::tileMapId),
                core::pmr::String(tests::tileSetId),
                core::pmr::String("tilemap.yaml"),
                fileBudget,
                origin);
            assert(map.success == (test == 0));
            if (test == 0) {
                const auto* raw = sources.find("atlas.yaml");
                assert(raw && raw->bytes == changed.bytes[0] && raw->bytes[0] == 0xef);
            }
        } else {
            assert(!manager
                    .load<scene2d::SpriteAtlasResource>(context,
                        core::pmr::String(tests::atlasA),
                        core::pmr::String("atlas.yaml"),
                        core::pmr::String("atlas.png"),
                        fileBudget,
                        origin)
                    .success);
        }
    }
}

void graph(core::SharedPtr<ManagedFileBackend> backend, bool stale = false)
{
    auto mounts =
        core::makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 2, 2 * fileBudget, 32, 256});
    ResourceFileOrigin origin{FilePathScope::MountedResource, mounts->mount("source", backend)};
    core::runtime::AsyncRuntimeDesc desc;
    desc.enableWork = desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = mounts;
    desc.completionQueueCapacity = desc.completionBudget = 4;
    core::runtime::AsyncRuntime runtime(desc);
    ResourceManager manager;
    ResourceGraphExecutor executor(manager, runtime.fileService(), runtime.completionQueue());
    ResourceGraphExecutionRequest request;
    request.graph.rootResourceId = tests::tileMapId;
    auto add = [&](core::UniquePtr<ResourceBase> resource) -> ResourceGraphNode& {
        ResourceGraphNode node;
        node.dependencies = resource->dependencies();
        node.resource = std::move(resource);
        request.graph.nodes.push_back(std::move(node));
        return request.graph.nodes.back();
    };
    add(core::makeUnique<scene2d::TileMapResource>(
        tests::tileMapId, tests::tileSetId, "tilemap.yaml", fileBudget, origin));
    auto& set = add(core::makeUnique<scene2d::TileSetResource>(
        tests::tileSetId, "tileset.yaml", fileBudget, origin));
    set.dependencies.push_back({tests::spriteA, "tile-visual"});
    set.dependencies.push_back({animationId, "tile-animation"});
    core::pmr::Vector<scene2d::SpriteAnimationFrame> frames;
    frames.push_back({core::pmr::String(tests::spriteA), .125, 1});
    frames.push_back({core::pmr::String(tests::spriteB), .125, 2});
    add(core::makeUnique<scene2d::SpriteAnimationResource>(animationId, frames, true));
    add(core::makeUnique<scene2d::SpriteResource>(tests::spriteA, tests::atlasA, "left"));
    add(core::makeUnique<scene2d::SpriteResource>(tests::spriteB, tests::atlasB, "right"));
    add(core::makeUnique<scene2d::SpriteAtlasResource>(
        tests::atlasA, "atlas.yaml", "atlas.png", fileBudget, origin));
    add(core::makeUnique<scene2d::SpriteAtlasResource>(
        tests::atlasB, "atlas_b.yaml", "atlas.png", fileBudget, origin));
    request.budget = {7, 6, 6 * fileBudget, 2 * 16 * 8 * 4, 4};

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
    unified_io_fixture::wait(runtime.completionQueue(), [&] {
        return completion.done;
    });
    if (!stale && !completion.result.resource.success)
        std::fprintf(stderr,
            "2D source graph: %s; id=%s; file=%u; reads=%llu; nodes=%llu\n",
            completion.result.resource.diagnostics.c_str(),
            completion.result.diagnosticId ? completion.result.diagnosticId : "none",
            static_cast<unsigned>(completion.result.fileStatus),
            static_cast<unsigned long long>(completion.result.telemetry.submittedFileReads),
            static_cast<unsigned long long>(completion.result.telemetry.completedNodes));
    assert(completion.result.resource.success != stale);
    if (!stale) {
        assert(completion.result.telemetry.submittedFileReads == 6);
        auto* map = manager.find<scene2d::TileMapResource>(tests::tileMapId).get();
        auto* set = manager.find<scene2d::TileSetResource>(tests::tileSetId).get();
        auto* atlas = manager.find<scene2d::SpriteAtlasResource>(tests::atlasA).get();
        assert(map && map->chunkTiles() == 2 && map->chunks().size() == 2);
        assert(set && set->tileWorldSize() == 1 && set->tile(1)->solid && set->tile(2)->animated);
        assert(atlas && atlas->image().width == 16 && atlas->image().height == 8
            && atlas->image().rgba8.size() == 512 && atlas->slice("left")->pixelsPerUnit == 8);
    } else
        assert(!manager.find<scene2d::TileMapResource>(tests::tileMapId).ready());
    runtime.shutdown();
    assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved()
        && !mounts->telemetry().inFlight && !mounts->telemetry().reservedReadBytes);
}

void cycle(const SourceFixture& fixture, const char* url, bool native)
{
    if (url) {
        HttpFileBackendDesc desc;
        desc.baseUrl = url;
        desc.maxInFlight = 2;
        desc.maxBytesInFlight = 256 * 1024;
        desc.maxPathBytes = 256;
        desc.maxHeaderBytes = 8 * 1024;
        desc.ioChunkBytes = 1024;
        desc.timeoutMs = 3000;
        desc.cancellationPollMs = 10;
        auto backend = core::makeShared<HttpFileBackend>(desc);
        graph(backend);
        assert(!backend->telemetry().inFlight && !backend->telemetry().activeNativeTransfers);
    } else if (native) {
        unified_io_fixture::Workspace workspace;
        for (std::size_t i = 0; i < fixture.names.size(); ++i)
            workspace.write(fixture.names[i],
                {reinterpret_cast<const char*>(fixture.bytes[i].data()), fixture.bytes[i].size()});
        auto desc = workspace.descriptor();
        desc.ioChunkBytes = 1024;
        graph(core::makeShared<NativeDirectoryFileBackend>(desc));
    } else {
        contracts(fixture);
        graph(fixture.memory());
        graph(fixture.memory(), true);
    }
}
} // namespace

int main(int argc, char** argv)
{
    SourceFixture fixture;
    const bool smoke = argc > 1;
    const bool native = smoke && std::string_view(argv[1]) == "--native";
    const char* url = argc == 3 && std::string_view(argv[1]) == "--http" ? argv[2] : nullptr;
    assert(argc == 1 || (argc == 2 && (native || std::string_view(argv[1]) == "--smoke")) || url);
    if (!smoke) {
        cycle(fixture, nullptr, false);
        std::puts("PASS 2D source origin/Text/1 and graph contracts");
        return 0;
    }
    for (unsigned i = 0; i < 20; ++i)
        cycle(fixture, url, native);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle(fixture, url, native);
    const auto after = core::defaultCpuAllocator().stats();
    std::printf("2D source 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(before.allocationCount - before.deallocationCount),
        static_cast<unsigned long long>(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
