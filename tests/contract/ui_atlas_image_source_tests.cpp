#include "assets/image_decoder.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "resource/resource_graph_executor.h"
#include "tests/fixtures/ui_atlas_pixels.h"
#include "tests/fixtures/ui_metadata_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"
#include "ui/ui_atlas_image_resource.h"
#include "ui/ui_atlas_metadata_resource.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace {

using namespace arp;
using namespace arp::core;
using namespace arp::core::io;
using namespace arp::resource;

constexpr const char* atlasId = "6cf0e0f0-d372-42eb-9e7c-78c5ba8eae1a";
constexpr const char* imageId = "2d844cf4-4902-48ba-b905-839fd7ad6db5";
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

ui::UiAtlasImageResourceLoadDesc imageDescriptor(ResourceFileOrigin origin)
{
    ui::UiAtlasImageResourceLoadDesc desc;
    desc.resourceId = imageId;
    desc.alias = "ui/image";
    desc.status = "Prototype Complete";
    desc.atlasMetadataAlias = "ui/atlas";
    desc.maxEncodedBytes = sourceBudget;
    desc.maxDecodedBytes = tests::uiAtlasRgbaBytes;
    desc.origin = origin;
    return desc;
}

void check(const ResourceManager& manager)
{
    const auto* image = manager.find<ui::UiAtlasImageResource>(imageId).get();
    assert(image && image->data && image->data->width == 32 && image->data->height == 16);
    assert(image->data->srgb && image->data->atlasResourceId == "texture/test.atlas");
    const auto& bytes = image->data->rgba8;
    assert(bytes.size() == tests::uiAtlasRgbaBytes);
    for (std::size_t i = 0; i < bytes.size(); i += 4)
        assert(bytes[i] == 12 && bytes[i + 1] == 34 && bytes[i + 2] == 56 && bytes[i + 3] == 255);
}

void add(
    ResourceSourceSet& sources, const char* path, core::StringView bytes, ResourceFileOrigin origin)
{
    ResourceSourceBuffer source;
    source.path = path;
    source.pathScope = origin.pathScope;
    source.mount = origin.mount;
    source.bytes.assign(bytes.begin(), bytes.end());
    assert(sources.add(std::move(source)));
}

void contracts()
{
    auto backend =
        makeShared<MemoryFileBackend>(MemoryFileBackendDesc{1, sourceBudget, 1, sourceBudget, 128});
    MountedFileBackend mounts({1, 1, sourceBudget, 32, 128});
    ResourceFileOrigin origin{FilePathScope::MountedResource, mounts.mount("source", backend)};
    const auto tga = tests::uiAtlasTgaBytes();
    for (unsigned test = 0; test < 22; ++test) {
        auto desc = imageDescriptor(origin);
        core::pmr::String metadata = tests::uiMetadataAtlasFixture();
        const bool tgaPath = test == 1 || test == 16;
        if (tgaPath)
            metadata.replace(metadata.find("atlas.png"), 9, "atlas.tga");
        if (test == 13)
            metadata.replace(metadata.find("width: 32"), 9, "width: 32.5");
        if (test == 14)
            metadata.replace(metadata.find("width: 32"), 9, "width: 64");
        if (test == 15)
            metadata.replace(metadata.find("images/atlas.png"), 16, "../atlas.png");
        if (test == 19)
            metadata.replace(metadata.find("images/atlas.png"), 16, "/atlas.png");
        if (test == 20)
            metadata.replace(metadata.find("width: 32"), 9, "width: 4294967296");
        if (test == 21)
            metadata.replace(metadata.find("images/atlas.png"), 16, u8"images/图集.png");
        ResourceSourceSet sources;
        add(sources, "meta/atlas.arpui", metadata, origin);
        const auto* image = test == 1 ? tga.data() : tests::uiAtlasPngBytes;
        auto size = test == 1 ? tga.size() : sizeof(tests::uiAtlasPngBytes);
        if (test == 9)
            size = 0;
        if (test == 10)
            size = 4;
        if (test == 2)
            desc.maxEncodedBytes = sizeof(tests::uiAtlasPngBytes) - 1;
        if (test == 3)
            desc.maxEncodedBytes = 0;
        if (test == 4)
            desc.maxEncodedBytes = SIZE_MAX;
        if (test == 5)
            desc.maxDecodedBytes = 0;
        if (test == 6)
            desc.maxDecodedBytes = SIZE_MAX;
        if (test == 7)
            desc.maxDecodedBytes = tests::uiAtlasRgbaBytes - 1;
        if (test == 17)
            desc.maxEncodedBytes = sizeof(tests::uiAtlasPngBytes);
        if (test == 18)
            desc.atlasMetadataAlias = "ui/missing";
        if (test != 11)
            add(sources,
                test == 21    ? u8"images/图集.png"
                    : tgaPath ? "images/atlas.tga"
                              : "images/atlas.png",
                {reinterpret_cast<const char*>(image), size},
                test == 8 ? ResourceFileOrigin{} : origin);
        ResourceLoadContext context;
        context.sources = &sources;
        context.requirePreloadedSources = true;
        if (test == 12)
            context.maxDecodedBytes = tests::uiAtlasRgbaBytes - 1;
        ResourceManager manager;
        assert(manager
                .loadResource(
                    makeUnique<ui::UiAtlasMetadataResource>(atlasDescriptor(origin)), context)
                .success);
        assert(manager.declareResource(imageId, ui::kUiAtlasImageResourceType).success);
        const auto* prior = manager.recordSnapshot(imageId);
        const auto generation = prior->generation;
        core::pmr::Vector<core::UniquePtr<ResourceBase>> candidates;
        candidates.push_back(makeUnique<ui::UiAtlasImageResource>(desc));
        auto result = manager.loadResourcesAtomically(std::move(candidates), context);
        const bool success = test == 0 || test == 1 || test == 17 || test == 21;
        if (result.success != success)
            std::fprintf(stderr, "UI image case %u: %s\n", test, result.diagnostics.c_str());
        assert(result.success == success);
        assert(manager.recordSnapshot(imageId) == prior);
        if (success)
            check(manager);
        else {
            assert(prior->generation == generation && prior->state == ResourceState::Declared);
            assert(!manager.resolveAlias("ui/image").success);
            assert(manager.find<ui::UiAtlasMetadataResource>(atlasId).ready());
        }
    }
    mounts.shutdown();
}

void localContract()
{
    unified_io_fixture::Workspace workspace;
    core::pmr::String metadata = tests::uiMetadataAtlasFixture();
    metadata.replace(metadata.find("images/atlas.png"), 16, "nested/atlas.png");
    workspace.write("atlas.arpui", metadata);
    workspace.write("nested/atlas.png",
        {reinterpret_cast<const char*>(tests::uiAtlasPngBytes), sizeof(tests::uiAtlasPngBytes)});
    auto atlas = atlasDescriptor({});
    atlas.atlasMetadataPath = workspace.root / "atlas.arpui";
    auto image = imageDescriptor({});
    image.sourceRoot = workspace.root;
    ResourceManager manager;
    assert(ui::loadUiAtlasMetadataResource(manager, atlas).success);
    assert(ui::loadUiAtlasImageResource(manager, image).success);
    check(manager);
}

void decoderAllocationFailure()
{
    const auto bytes = tests::uiAtlasTgaBytes();
    auto quota = makeShared<CpuMemoryQuota>(tests::uiAtlasRgbaBytes + tests::uiAtlasRgbaBytes / 2);
    const auto before = core::defaultCpuAllocator().stats();
    bool failed = false;
    {
        CpuMemoryQuotaScope scope(quota);
        try {
            const auto result = assets::decodeImageToRgba8(
                {bytes.data(), bytes.size()}, "image/tga", true, tests::uiAtlasRgbaBytes);
            assert(false && "Decoded output copy must exhaust the explicit quota");
        } catch (const std::bad_alloc&) {
            failed = true;
        }
    }
    const auto after = core::defaultCpuAllocator().stats();
    assert(failed && quota->liveBytes() == 0 && before.liveBytes == after.liveBytes);
    assert(after.byIntent(CpuAllocationIntent::ImageDecode).allocationCount
        > before.byIntent(CpuAllocationIntent::ImageDecode).allocationCount);
}

void graph(core::SharedPtr<ManagedFileBackend> backend)
{
    auto mounts =
        makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 1, sourceBudget, 32, 128});
    ResourceFileOrigin origin{FilePathScope::MountedResource, mounts->mount("source", backend)};
    runtime::AsyncRuntimeDesc desc;
    desc.enableWork = desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = mounts;
    desc.completionQueueCapacity = desc.completionBudget = 4;
    runtime::AsyncRuntime runtime(desc);
    ResourceManager manager;
    ResourceGraphExecutor executor(manager, runtime.fileService(), runtime.completionQueue());
    ResourceGraphExecutionRequest request;
    request.graph.rootResourceId = imageId;
    ResourceGraphNode image;
    image.resource = makeUnique<ui::UiAtlasImageResource>(imageDescriptor(origin));
    image.dependencies.push_back({atlasId, "ui.atlas.metadata"});
    ResourceGraphNode atlas;
    atlas.resource = makeUnique<ui::UiAtlasMetadataResource>(atlasDescriptor(origin));
    request.graph.nodes.push_back(std::move(image));
    request.graph.nodes.push_back(std::move(atlas));
    request.budget = {2, 2, 2 * sourceBudget, tests::uiAtlasRgbaBytes, 4};

    struct Completion {
        bool done = false;
        ResourceGraphExecutionResult result;
    } completion;

    assert(executor.submit(std::move(request),
               {[](void* context, ResourceGraphExecutionResult result) {
                    auto& completion = *static_cast<Completion*>(context);
                    assert(!completion.done);
                    completion.done = true;
                    completion.result = std::move(result);
                },
                   &completion})
        == ResourceGraphSubmitStatus::Accepted);
    unified_io_fixture::wait(runtime.completionQueue(), [&] {
        return completion.done;
    });
    if (!completion.result.resource.success)
        std::fprintf(
            stderr, "UI image graph: %s\n", completion.result.resource.diagnostics.c_str());
    assert(completion.result.resource.success);
    assert(completion.result.telemetry.submittedFileReads == 2);
    assert(completion.result.telemetry.bytesDecoded == tests::uiAtlasRgbaBytes);
    check(manager);
    runtime.shutdown();
    assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved());
    assert(!mounts->telemetry().inFlight);
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
        localContract();
        decoderAllocationFailure();
        auto backend = makeShared<MemoryFileBackend>(
            MemoryFileBackendDesc{4, 2 * sourceBudget, 1, sourceBudget, 128});
        assert(backend->registerDirectory("meta") == FileIoStatus::Success);
        assert(backend->registerDirectory("images") == FileIoStatus::Success);
        const std::string_view metadata = tests::uiMetadataAtlasFixture();
        assert(backend->registerFile("meta/atlas.arpui",
                   reinterpret_cast<const std::uint8_t*>(metadata.data()),
                   metadata.size())
            == FileIoStatus::Success);
        assert(backend->registerFile(
                   "images/atlas.png", tests::uiAtlasPngBytes, sizeof(tests::uiAtlasPngBytes))
            == FileIoStatus::Success);
        graph(backend);
    }
}

} // namespace

int main(int argc, char** argv)
{
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
    std::puts("UI image source: 20 warmup +20 measured cycles; core memory returned to baseline");
}
