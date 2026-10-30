#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "resource/resource_graph_executor.h"
#include "tests/fixtures/ui_metadata_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"
#include "ui/ui_theme_resource.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace {

using namespace arp;
using namespace arp::core;
using namespace arp::core::io;
using namespace arp::resource;

constexpr const char* atlasId = "6cf0e0f0-d372-42eb-9e7c-78c5ba8eae1a";
constexpr const char* themeId = "8d958aa4-f406-4bc7-9eb4-4cc6cc15c8fe";
constexpr std::size_t sourceBudget = 16 * 1024;

ui::UiAtlasMetadataResourceLoadDesc atlasDescriptor(ResourceFileOrigin origin)
{
    ui::UiAtlasMetadataResourceLoadDesc desc;
    desc.resourceId = atlasId;
    desc.alias = "ui/atlas";
    desc.status = "Contract Complete";
    desc.atlasMetadataPath = "meta/atlas.arpui";
    desc.expectedAtlasResourceId = "texture/test.atlas";
    desc.maxMetadataBytes = sourceBudget;
    desc.origin = origin;
    return desc;
}

ui::UiThemeResourceLoadDesc themeDescriptor(ResourceFileOrigin origin)
{
    ui::UiThemeResourceLoadDesc desc;
    desc.resourceId = themeId;
    desc.alias = "ui/theme";
    desc.status = "Contract Complete";
    desc.themePath = "theme.arpui";
    desc.atlasMetadataPath = "meta/atlas.arpui";
    desc.atlasMetadataAlias = "ui/atlas";
    desc.maxThemeBytes = sourceBudget;
    desc.origin = desc.atlasMetadataOrigin = origin;
    return desc;
}

bool admit(ResourceManager& manager,
    core::UniquePtr<ResourceBase> resource,
    const ResourceSourceSet& sources,
    const char* diagnostic = nullptr)
{
    const auto id = resource->reference().id;
    if (!manager.recordSnapshot(id))
        assert(manager.declareResource(id, resource->reference().type).success);
    const auto* prior = manager.recordSnapshot(id);
    const auto generation = manager.generationOf(id);
    ResourceLoadContext context;
    context.sources = &sources;
    context.requirePreloadedSources = true;
    core::pmr::Vector<core::UniquePtr<ResourceBase>> candidates;
    candidates.push_back(std::move(resource));
    auto result = manager.loadResourcesAtomically(std::move(candidates), context);
    assert(manager.recordSnapshot(id) == prior);
    if (!result.success) {
        assert(manager.generationOf(id) == generation && prior->state == ResourceState::Declared);
        if (diagnostic)
            assert(result.diagnostics.find(diagnostic) != core::pmr::String::npos);
    }
    return result.success;
}

ResourceSourceSet source(std::string_view path, std::string_view bytes, ResourceFileOrigin origin)
{
    ResourceSourceSet sources;
    ResourceSourceBuffer buffer;
    buffer.path = path;
    buffer.pathScope = origin.pathScope;
    buffer.mount = origin.mount;
    buffer.bytes.assign(bytes.begin(), bytes.end());
    assert(sources.add(std::move(buffer)));
    return sources;
}

void check(const ResourceManager& manager)
{
    const auto* atlas = manager.find<ui::UiAtlasMetadataResource>(atlasId).get();
    const auto* theme = manager.find<ui::UiThemeResource>(themeId).get();
    assert(atlas && atlas->data && theme && theme->data);
    assert(atlas->data->width == 32 && atlas->data->height == 16 && atlas->data->srgb);
    assert(atlas->data->sourcePath == "images/atlas.png");
    assert(atlas->data->fontFaces.size() == 1 && atlas->data->slices.size() == 1);
    assert(theme->data->theme.themeId() == "test.theme");
    assert(theme->data->atlasResourceId == atlas->data->resourceId);
    assert(theme->data->styleColors.at("style.panel").g == 0.5f);
    assert(theme->data->styleFonts.at("style.panel") == "font.test");
}

void contracts()
{
    auto backend =
        makeShared<MemoryFileBackend>(MemoryFileBackendDesc{1, sourceBudget, 1, sourceBudget, 128});
    MountedFileBackend mounts({2, 1, sourceBudget, 32, 128});
    ResourceFileOrigin origin{FilePathScope::MountedResource, mounts.mount("metadata", backend)};
    const auto atlas = atlasDescriptor(origin);
    const auto theme = themeDescriptor(origin);
    auto atlasSources = source("meta/atlas.arpui", tests::uiMetadataAtlasFixture(), origin);
    auto themeSources = source("theme.arpui", tests::uiMetadataThemeFixture(), origin);
    for (bool isTheme : {false, true}) {
        for (const auto bytes : {std::string_view{},
                 std::string_view{"\xEF\xBB\xBF"},
                 std::string_view{"\xC0\xAF"},
                 std::string_view{"\0bad", 4}}) {
            ResourceManager manager;
            if (isTheme)
                assert(
                    admit(manager, makeUnique<ui::UiAtlasMetadataResource>(atlas), atlasSources));
            auto bad = source(isTheme ? "theme.arpui" : "meta/atlas.arpui", bytes, origin);
            assert(
                !(isTheme ? admit(manager, makeUnique<ui::UiThemeResource>(theme), bad)
                          : admit(manager, makeUnique<ui::UiAtlasMetadataResource>(atlas), bad)));
        }
        ResourceManager manager;
        if (isTheme)
            assert(admit(manager, makeUnique<ui::UiAtlasMetadataResource>(atlas), atlasSources));
        core::pmr::String bom = "\xEF\xBB\xBF";
        bom += isTheme ? tests::uiMetadataThemeFixture() : tests::uiMetadataAtlasFixture();
        auto bytes = source(isTheme ? "theme.arpui" : "meta/atlas.arpui", bom, origin);
        assert(isTheme ? admit(manager, makeUnique<ui::UiThemeResource>(theme), bytes)
                       : admit(manager, makeUnique<ui::UiAtlasMetadataResource>(atlas), bytes));
    }
    for (unsigned test = 0; test < 8; ++test) {
        ResourceManager manager;
        assert(admit(manager, makeUnique<ui::UiAtlasMetadataResource>(atlas), atlasSources));
        auto candidate = theme;
        if (test == 0)
            candidate.atlasMetadataPath = "other/atlas.arpui";
        if (test == 1)
            candidate.atlasMetadataOrigin = {};
        if (test == 2)
            candidate.atlasMetadataPath.clear();
        if (test == 3)
            candidate.maxThemeBytes = 0;
        if (test == 4)
            candidate.maxThemeBytes = SIZE_MAX;
        if (test == 5)
            candidate.maxThemeBytes = 1;
        if (test == 6)
            candidate.origin = {};
        if (test == 7)
            candidate.atlasMetadataAlias = "ui/missing";
        const char* diagnostic = test < 2 ? "UI-THEME-ATLAS-LOCATION-MISMATCH"
            : test == 2                   ? "requires theme and atlas metadata paths"
            : test < 5                    ? "SOURCE-CONFIG"
            : test == 5                   ? "SOURCE-BUDGET"
            : test == 6                   ? "LOCATION-MISMATCH"
                                          : "alias not found";
        assert(
            !admit(manager, makeUnique<ui::UiThemeResource>(candidate), themeSources, diagnostic));
        assert(manager.find<ui::UiAtlasMetadataResource>(atlasId).ready());
    }
    for (unsigned test = 0; test < 7; ++test) {
        ResourceManager manager;
        auto candidate = atlas;
        if (test == 0)
            candidate.maxMetadataBytes = 0;
        if (test == 1)
            candidate.maxMetadataBytes = SIZE_MAX;
        if (test == 2)
            candidate.maxMetadataBytes = 1;
        if (test == 3)
            candidate.origin = {};
        if (test == 4)
            candidate.expectedAtlasResourceId = "texture/other";
        if (test == 5)
            candidate.atlasMetadataPath = "../atlas.arpui";
        if (test == 6)
            candidate.atlasMetadataPath = "missing.arpui";
        assert(!admit(manager, makeUnique<ui::UiAtlasMetadataResource>(candidate), atlasSources));
    }
    for (bool isTheme : {false, true}) {
        ResourceManager manager;
        if (isTheme)
            assert(admit(manager, makeUnique<ui::UiAtlasMetadataResource>(atlas), atlasSources));
        auto bad = source(isTheme ? "theme.arpui" : "meta/atlas.arpui",
            isTheme ? "type: ui.theme\ncontractVersion: unknown\n"
                    : "type: ui.atlas\ncontractVersion: unknown\n",
            origin);
        assert(!(isTheme ? admit(manager, makeUnique<ui::UiThemeResource>(theme), bad)
                         : admit(manager, makeUnique<ui::UiAtlasMetadataResource>(atlas), bad)));
    }
    ResourceManager manager;
    assert(admit(manager, makeUnique<ui::UiAtlasMetadataResource>(atlas), atlasSources));
    assert(admit(manager, makeUnique<ui::UiThemeResource>(theme), themeSources));
    check(manager);
    core::pmr::Vector<ResourceFileReadIntent> reads;
    core::pmr::String diagnostic;
    assert(ui::UiThemeResource(theme).collectFileReadIntents(manager, reads, diagnostic));
    assert(reads.size() == 1 && reads.front().path == "theme.arpui");

    // The source mount of a shared atlas is independent of the theme source.
    auto themeBackend =
        makeShared<MemoryFileBackend>(MemoryFileBackendDesc{1, sourceBudget, 1, sourceBudget, 128});
    auto cross = theme;
    cross.origin.mount = mounts.mount("theme", themeBackend);
    assert(cross.origin.mount && !(cross.origin.mount == origin.mount));
    ResourceManager shared;
    assert(admit(shared, makeUnique<ui::UiAtlasMetadataResource>(atlas), atlasSources));
    auto crossSources = source("theme.arpui", tests::uiMetadataThemeFixture(), cross.origin);
    assert(admit(shared, makeUnique<ui::UiThemeResource>(cross), crossSources));
    check(shared);
    mounts.shutdown();
}

void localContracts()
{
    unified_io_fixture::Workspace workspace;
    workspace.write("atlas.arpui", tests::uiMetadataAtlasFixture());
    workspace.write("theme.arpui", tests::uiMetadataThemeFixture());
    auto atlas = atlasDescriptor({});
    auto theme = themeDescriptor({});
    atlas.atlasMetadataPath = workspace.root / "atlas.arpui";
    theme.themePath = workspace.root / "theme.arpui";
    theme.atlasMetadataPath = atlas.atlasMetadataPath;
    ResourceManager manager;
    assert(ui::loadUiAtlasMetadataResource(manager, atlas).success);
    assert(ui::loadUiThemeResource(manager, theme).success);
    check(manager);
}

void reloadContracts()
{
    ResourceManager manager;
    const auto atlas = atlasDescriptor({});
    const auto theme = themeDescriptor({});
    auto atlasSources = source("meta/atlas.arpui", tests::uiMetadataAtlasFixture(), {});
    auto themeSources = source("theme.arpui", tests::uiMetadataThemeFixture(), {});
    assert(admit(manager, makeUnique<ui::UiAtlasMetadataResource>(atlas), atlasSources));
    assert(admit(manager, makeUnique<ui::UiThemeResource>(theme), themeSources));
    const auto oldHandle = manager.find<ui::UiThemeResource>(themeId);
    const auto* record = manager.recordSnapshot(themeId);
    core::pmr::String changed = tests::uiMetadataThemeFixture();
    changed.replace(changed.find("0.25, 0.5, 0.75"), 15, "0.25, 0.125, 0.75");
    auto changedSources = source("theme.arpui", changed, {});
    ResourceLoadContext context;
    context.sources = &changedSources;
    context.requirePreloadedSources = true;
    const auto result = manager.reloadResource(makeUnique<ui::UiThemeResource>(theme), context);
    std::printf("UI theme reload: success=%d unchanged=%d generation=%llu -> %llu\n",
        result.success,
        result.unchangedContent,
        static_cast<unsigned long long>(oldHandle.generation()),
        static_cast<unsigned long long>(manager.generationOf(themeId)));
    std::fflush(stdout);
    assert(result.success && !result.unchangedContent && result.telemetry.committed);
    assert(manager.recordSnapshot(themeId) == record);
    assert(oldHandle.stale() && !oldHandle.get());
    assert(manager.find<ui::UiThemeResource>(themeId).get()->data->styleColors.at("style.panel").g
        == 0.125f);
}

void graph(core::SharedPtr<ManagedFileBackend> backend)
{
    auto mounts =
        makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 1, sourceBudget, 32, 128});
    ResourceFileOrigin origin{FilePathScope::MountedResource, mounts->mount("metadata", backend)};
    runtime::AsyncRuntimeDesc desc;
    desc.enableWork = desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = mounts;
    desc.completionQueueCapacity = desc.completionBudget = 4;
    runtime::AsyncRuntime runtime(desc);
    ResourceManager manager;
    ResourceGraphExecutor executor(manager, runtime.fileService(), runtime.completionQueue());
    ResourceGraphExecutionRequest request;
    request.graph.rootResourceId = themeId;
    ResourceGraphNode theme;
    theme.resource = makeUnique<ui::UiThemeResource>(themeDescriptor(origin));
    theme.dependencies.push_back({atlasId, "ui.atlas.metadata"});
    ResourceGraphNode atlas;
    atlas.resource = makeUnique<ui::UiAtlasMetadataResource>(atlasDescriptor(origin));
    request.graph.nodes.push_back(std::move(theme));
    request.graph.nodes.push_back(std::move(atlas));
    request.budget = {2, 2, 2 * sourceBudget, sourceBudget, 4};

    struct Completion {
        unsigned count = 0;
        ResourceGraphExecutionResult result;
    } completion;

    assert(executor.submit(std::move(request),
               {[](void* context, ResourceGraphExecutionResult result) {
                    auto& completion = *static_cast<Completion*>(context);
                    ++completion.count;
                    completion.result = std::move(result);
                },
                   &completion})
        == ResourceGraphSubmitStatus::Accepted);
    unified_io_fixture::wait(runtime.completionQueue(), [&] {
        return completion.count != 0;
    });
    if (!completion.result.resource.success)
        std::fprintf(stderr, "UI metadata: %s\n", completion.result.resource.diagnostics.c_str());
    assert(completion.count == 1 && completion.result.resource.success);
    assert(completion.result.telemetry.submittedFileReads == 2);
    assert(completion.result.telemetry.bytesRead
        == std::string_view(tests::uiMetadataAtlasFixture()).size()
            + std::string_view(tests::uiMetadataThemeFixture()).size());
    check(manager);
    runtime.shutdown();
    assert(runtime.completionQueue().pending() == 0 && runtime.completionQueue().reserved() == 0);
    assert(mounts->telemetry().inFlight == 0);
}

void cycle(const char* url)
{
    if (url) {
        HttpFileBackendDesc desc;
        desc.baseUrl = url;
        desc.maxInFlight = 1;
        desc.maxBytesInFlight = 128 * 1024;
        desc.maxPathBytes = 128;
        desc.maxHeaderBytes = 8 * 1024;
        desc.ioChunkBytes = 1024;
        desc.timeoutMs = 3000;
        desc.cancellationPollMs = 10;
        auto backend = makeShared<HttpFileBackend>(desc);
        graph(backend);
        assert(!backend->telemetry().inFlight && !backend->telemetry().activeNativeTransfers);
    } else {
        contracts();
        localContracts();
        reloadContracts();
        auto backend = makeShared<MemoryFileBackend>(
            MemoryFileBackendDesc{3, 2 * sourceBudget, 1, sourceBudget, 128});
        assert(backend->registerDirectory("meta") == FileIoStatus::Success);
        for (const auto& entry : {std::pair{"meta/atlas.arpui", tests::uiMetadataAtlasFixture()},
                 std::pair{"theme.arpui", tests::uiMetadataThemeFixture()}})
            assert(backend->registerFile(entry.first,
                       reinterpret_cast<const std::uint8_t*>(entry.second),
                       std::string_view(entry.second).size())
                == FileIoStatus::Success);
        graph(backend);
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--reload") {
        reloadContracts();
        return 0;
    }
    const char* url = argc == 3 && std::string_view(argv[1]) == "--http" ? argv[2] : nullptr;
    assert(argc == 1 || url);
    for (unsigned i = 0; i < 20; ++i)
        cycle(url);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle(url);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    std::puts(
        "UI metadata source: 20 warmup +20 measured cycles; core memory returned to baseline");
}
