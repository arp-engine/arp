#include "core/io/file_io.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "renderer/ui/ui_atlas_runtime.h"
#include "renderer/ui/ui_text_atlas.h"
#include "renderer/ui/ui_text_layout.h"
#include "resource/resource_graph_executor.h"
#include "tests/fixtures/ui_font_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"
#include "ui/ui_atlas_metadata_resource.h"
#include "ui/ui_font_face_cache.h"
#include "ui/ui_font_set_resource.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace {

using namespace arp;
using namespace arp::core;
using namespace arp::core::io;
using namespace arp::resource;

constexpr const char* atlasId = "6cf0e0f0-d372-42eb-9e7c-78c5ba8eae1a";
constexpr const char* fontId = "77777777-7777-4777-8777-777777777777";
constexpr std::size_t sourceBudget = 2 * 1024 * 1024;
constexpr std::size_t metadataBudget = 16 * 1024;

ui::UiAtlasMetadataResourceLoadDesc atlasDescriptor(ResourceFileOrigin origin)
{
    ui::UiAtlasMetadataResourceLoadDesc desc;
    desc.resourceId = atlasId;
    desc.alias = "ui/font-atlas";
    desc.status = "Contract Complete";
    desc.atlasMetadataPath = "meta/atlas.arpui";
    desc.expectedAtlasResourceId = "texture/test.font.atlas";
    desc.maxMetadataBytes = metadataBudget;
    desc.origin = origin;
    return desc;
}

ui::UiFontSetResourceLoadDesc fontDescriptor(ResourceFileOrigin origin)
{
    ui::UiFontSetResourceLoadDesc desc;
    desc.resourceId = fontId;
    desc.alias = "ui/fonts";
    desc.status = "Contract Complete";
    desc.atlasMetadataAlias = "ui/font-atlas";
    desc.maxFaces = 2;
    desc.maxFontBytes = desc.maxTotalSourceBytes = sourceBudget;
    desc.origin = origin;
    return desc;
}

void add(
    ResourceSourceSet& sources, const char* path, core::StringView bytes, ResourceFileOrigin origin)
{
    ResourceSourceBuffer buffer;
    buffer.path = path;
    buffer.pathScope = origin.pathScope;
    buffer.mount = origin.mount;
    buffer.bytes.assign(bytes.begin(), bytes.end());
    assert(sources.add(std::move(buffer)));
}

ui::UiFontFaceDesc checkFonts(ResourceManager& manager, const char* expectedPath = "fonts/test.ttf")
{
    const auto resource = manager.find<ui::UiFontSetResource>(fontId);
    assert(resource.ready() && resource.get()->data);
    const auto& faces = resource.get()->data->fonts;
    assert(faces.size() == 2 && faces[0].source && faces[0].source == faces[1].source);
    assert(faces[0].source->desc().path == expectedPath);
    assert(faces[0].source->contentVersion().kind == FileVersionKind::ContentSha256);
    auto face = faces[0];
    const auto loaded = ui::UiFontFaceLoader().load(face);
    assert(loaded.ok() && loaded.face().validatedByFreeType
        && loaded.face().desc.source == face.source);
    ui::UiFontRasterizeDesc desc;
    desc.face = face;
    desc.text = "AV abc";
    desc.maxX = 128;
    desc.maxY = 64;
    const auto plain = ui::UiFontTextRasterizer().rasterize(desc);
    ui::UiFontFaceCache cache({2, sourceBudget});
    const auto cached = ui::UiFontTextRasterizer(cache).rasterize(desc);
    assert(plain.ok() && cached.ok() && !plain.text().pixels.empty());
    assert(plain.text().glyphCount == 6 && plain.text().glyphCount == cached.text().glyphCount);
    assert(plain.text().advanceX == cached.text().advanceX);
    assert(plain.text().pixels.size() == cached.text().pixels.size());
    for (std::size_t i = 0; i < plain.text().pixels.size(); ++i) {
        const auto& a = plain.text().pixels[i];
        const auto& b = cached.text().pixels[i];
        assert(a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height
            && a.alpha == b.alpha);
    }
    core::pmr::String diagnostic;
    float kerning = 0;
    assert(cache.kerningX(face, 'A', 'V', kerning, diagnostic));
    assert(cache.size() == 1);
    arp::renderer::ui::TextLayoutCache layout(faces);
    layout.setFontFaceCache(&cache);
    arp::renderer::ui::TextLayoutKey key;
    key.text = "AV";
    key.fontToken = face.tokenId;
    key.locale = "en";
    key.sizePx = face.sizePx;
    key.styleId = "test.style";
    key.maxWidthPx = 128;
    key.scaleBucket = 1;
    const auto run = layout.resolve(key);
    if (!run.ok())
        std::fprintf(stderr, "UI font layout: %s\n", run.diagnostics().c_str());
    assert(run.ok() && run.run().glyphQuads.size() == 2);

    // The dynamic renderer retains the same remote/memory byte owners.
    renderer::ui::UnifiedUiAtlasRuntimeBuildDesc atlas;
    atlas.baseAtlas.resourceId = "font-source-test";
    atlas.baseAtlas.extent = {1, 1};
    atlas.baseAtlas.rgba8 = {255, 255, 255, 255};
    atlas.maxWidth = 128;
    atlas.maxHeight = 128;
    atlas.fontFaces = &faces;
    auto runtime = renderer::ui::UnifiedUiAtlasRuntime::build(atlas);
    assert(runtime.ok());
    auto moved = std::move(runtime);
    assert(manager.unload(fontId).success && !resource.ready() && !resource.get());
    assert(manager.unload(atlasId).success);

    core::pmr::Vector<renderer::ui::UiDrawCommand> commands;
    renderer::ui::UiDrawCommand command;
    command.kind = renderer::ui::UiDrawCommandKind::Text;
    command.rect = ui::LayoutRect::fromOriginSize(0, 0, 96, 20);
    command.clipRect = command.rect;
    command.fontToken = face.tokenId;
    command.styleId = "test.style";
    command.text = "AV";
    commands.push_back(command);
    assert(moved.resolve(commands).ok());
    assert(moved.resolveTextFast(commands).ok());
    const auto recovery = moved.makeRecoveryPlan();
    assert(recovery.fontFaces && recovery.fontFaces->front().source == face.source);
    const auto retainedBytes = recovery.baseAtlas.rgba8.size()
        + recovery.seedEntries->size() * sizeof(renderer::ui::GlyphAtlasEntry)
        + recovery.seedPages->size() * sizeof(renderer::ui::GlyphAtlasPage)
        + face.source->bytes().size();
    assert(moved.makeRecoveryPlan({retainedBytes}).ok());
    assert(!moved.makeRecoveryPlan({retainedBytes - 1}).ok());
    auto recovered = renderer::ui::UnifiedUiAtlasRuntime::build(recovery);
    assert(recovered.ok());
    assert(recovered.atlas().rgba8 == moved.atlas().rgba8);
    assert(recovered.resolve(commands).ok());
    commands.front().text = "BC";
    assert(recovered.resolve(commands).ok());
    commands.front().fontToken = "undeclared";
    commands.front().text = " ";
    assert(!recovered.resolveTextFast(commands).ok());
    commands.front().fontToken = face.tokenId;
    commands.front().text = "BC";

    renderer::ui::UiTextAtlasBuildDesc legacy;
    legacy.baseAtlas = atlas.baseAtlas;
    legacy.commands = &commands;
    legacy.fontFace = &face;
    assert(renderer::ui::UiTextAtlasBuilder().build(legacy).ok());
    const auto retained = ui::UiFontTextRasterizer(cache).rasterize(desc);
    assert(retained.ok() && retained.text().advanceX == cached.text().advanceX);
    return face;
}

void sourceAndCacheContracts(const core::pmr::Vector<std::uint8_t>& bytes)
{
    // A real, readable host file is insufficient rendering input. These calls
    // must reject before any decoder/cache can reopen that path.
    ui::UiFontFaceDesc pathOnly;
    pathOnly.sourcePath = ARP_FONT_IO_FIXTURE_PATH;
    pathOnly.tokenId = "font.path-only";
    pathOnly.family = "Droid Sans";
    pathOnly.sizePx = 16;
    pathOnly.lineHeightPx = 20;
    core::pmr::String missingDiagnostic;
    assert(!ui::validatePreparedUiFontFace(pathOnly, missingDiagnostic));
    assert(missingDiagnostic == "UI-FONT-SOURCE-NOT-PREPARED");
    core::pmr::Vector<ui::UiFontFaceDesc> unprepared;
    unprepared.push_back(pathOnly);
    renderer::ui::TextLayoutKey missingKey;
    missingKey.text = "A";
    missingKey.fontToken = pathOnly.tokenId;
    missingKey.styleId = "test.style";
    missingKey.locale = "en";
    missingKey.sizePx = 16;
    missingKey.maxWidthPx = 64;
    missingKey.scaleBucket = 1;
    renderer::ui::TextLayoutCache strictLayout(unprepared);
    const auto missing = strictLayout.resolve(missingKey);
    assert(!missing.ok());
    assert(missing.diagnostics().find("UI-FONT-SOURCE-NOT-PREPARED") != core::pmr::String::npos);
    renderer::ui::TextLayoutCache emptyLayout;
    assert(!emptyLayout.resolve(missingKey).ok());
    renderer::ui::UnifiedUiAtlasRuntimeBuildDesc strictAtlas;
    strictAtlas.baseAtlas.resourceId = "strict-font-test";
    strictAtlas.baseAtlas.extent = {1, 1};
    strictAtlas.baseAtlas.rgba8 = {255, 255, 255, 255};
    strictAtlas.fontFaces = &unprepared;
    const auto rejected = renderer::ui::UnifiedUiAtlasRuntime::build(strictAtlas);
    assert(!rejected.ok() && rejected.diagnostics() == "UI-FONT-SOURCE-NOT-PREPARED");
    core::pmr::Vector<renderer::ui::UiDrawCommand> absentCommands;
    renderer::ui::UiDrawCommand absent;
    absent.kind = renderer::ui::UiDrawCommandKind::Text;
    absent.text = "A";
    absentCommands.push_back(absent);
    renderer::ui::UiTextAtlasBuildDesc absentText;
    absentText.baseAtlas = strictAtlas.baseAtlas;
    absentText.commands = &absentCommands;
    assert(!renderer::ui::UiTextAtlasBuilder().build(absentText).ok());
    auto backend =
        makeShared<MemoryFileBackend>(MemoryFileBackendDesc{1, sourceBudget, 1, sourceBudget, 128});
    MountedFileBackend mounts({2, 1, sourceBudget, 32, 128});
    const auto first = mounts.mount("fonts", backend);
    assert(first);
    ui::UiFontSourceDesc desc;
    desc.path = "fonts/test.ttf";
    desc.pathScope = FilePathScope::MountedResource;
    desc.mount = first;
    desc.maxBytes = sourceBudget;
    core::pmr::String diagnostic;
    auto original = ui::UiFontSource::create(desc, bytes, diagnostic);
    assert(original && diagnostic.empty());
    auto face = ui::UiFontFaceDesc{};
    face.tokenId = "font.test";
    face.family = "Droid Sans";
    face.sourcePath = "fonts/test.ttf";
    face.source = original;
    face.sizePx = 16;
    face.lineHeightPx = 20;
    face.maxSourceBytes = sourceBudget;
    ui::UiFontFaceCache cache({3, 3 * sourceBudget});
    const auto* a = cache.acquire(face, diagnostic);
    assert(a);
    auto same = ui::UiFontSource::create(desc, bytes, diagnostic);
    face.source = same;
    assert(cache.acquire(face, diagnostic) == a);
    assert(mounts.unmount(first));
    const auto second = mounts.mount("fonts", backend);
    assert(second && second.index == first.index && second.generation != first.generation);
    desc.mount = second;
    face.source = ui::UiFontSource::create(desc, bytes, diagnostic);
    const auto* b = cache.acquire(face, diagnostic);
    assert(b && b != a);
    auto changed = bytes;
    changed.push_back(0);
    face.source = ui::UiFontSource::create(desc, std::move(changed), diagnostic);
    const auto* c = cache.acquire(face, diagnostic);
    assert(c && c != b && c != a && cache.size() == 3);
    face.maxSourceBytes = bytes.size() - 1;
    assert(!cache.acquire(face, diagnostic) && cache.size() == 3);
    face.maxSourceBytes = sourceBudget;
    face.sourcePath = "different.ttf";
    assert(!cache.acquire(face, diagnostic) && cache.size() == 3);
    face.sourcePath = "fonts/test.ttf";
    face.source = original;
    ui::UiFontFaceCache bounded({1, bytes.size() - 1});
    assert(!bounded.acquire(face, diagnostic) && !bounded.size());
    bounded = ui::UiFontFaceCache({1, sourceBudget});
    assert(bounded.acquire(face, diagnostic));
    face.source = b->source;
    assert(!bounded.acquire(face, diagnostic) && bounded.size() == 1);
    auto moved = std::move(bounded);
    assert(moved.size() == 1 && !bounded.size() && !bounded.residentSourceBytes());
    moved.clear();
    assert(!moved.size() && !moved.residentSourceBytes());

    for (unsigned test = 0; test < 8; ++test) {
        auto bad = desc;
        if (test == 0)
            bad.maxBytes = 0;
        if (test == 1)
            bad.maxBytes = SIZE_MAX;
        if (test == 2)
            bad.maxBytes = bytes.size() - 1;
        if (test == 3)
            bad.path = "../font.ttf";
        if (test == 4)
            bad.mount = {};
        if (test == 5)
            bad.version.kind = FileVersionKind::MetadataObservation;
        if (test == 6)
            bad.version.kind = FileVersionKind::ContentSha256;
        if (test == 7)
            bad.pathScope = FilePathScope::Package;
        assert(!ui::UiFontSource::create(std::move(bad), bytes, diagnostic));
    }
    desc.mount = first;
    desc.version = original->contentVersion();
    assert(ui::UiFontSource::create(desc, bytes, diagnostic));
    auto longPath = desc;
    longPath.path = "fonts/";
    for (unsigned i = 0; i < 10; ++i) {
        longPath.path.append(64, 'a');
        longPath.path += '/';
    }
    longPath.path += "test.ttf";
    assert(ui::UiFontSource::create(std::move(longPath), bytes, diagnostic));
    core::pmr::Vector<std::uint8_t> foreign(
        bytes.begin(), bytes.end(), std::pmr::new_delete_resource());
    assert(!ui::UiFontSource::create(desc, std::move(foreign), diagnostic));

    // A corrupt supplied source must fail even while its host path is readable.
    auto corruptDesc = desc;
    corruptDesc.path = core::FilePath::utf8(ARP_FONT_IO_FIXTURE_PATH);
    corruptDesc.pathScope = FilePathScope::HostFilesystem;
    corruptDesc.mount = {};
    corruptDesc.version = {};
    core::pmr::Vector<std::uint8_t> corrupt{1, 2, 3, 4};
    face.source = ui::UiFontSource::create(corruptDesc, std::move(corrupt), diagnostic);
    face.sourcePath = ARP_FONT_IO_FIXTURE_PATH;
    assert(face.source && !ui::UiFontFaceLoader().load(face).ok());

    // Fail inside core-owned FreeType work, then release all partial backend state.
    face.source = original;
    face.sourcePath = "fonts/test.ttf";
    for (std::uint64_t limit : {256, 2048, 16384, 65536}) {
        auto quota = makeShared<CpuMemoryQuota>(limit);
        ui::UiFontFaceCache temporary({1, sourceBudget});
        {
            CpuMemoryQuotaScope scope(quota);
            try {
                temporary.acquire(face, diagnostic);
            } catch (const std::bad_alloc&) {
            }
        }
        temporary.clear();
        core::pmr::String discard;
        diagnostic.swap(discard);
        discard.clear();
        // Dispose diagnostic capacity before checking its owning quota.
        {
            core::pmr::String released;
            discard.swap(released);
        }
        assert(quota->liveBytes() == 0);
    }
    mounts.shutdown();
}

void resourceContracts(const core::pmr::Vector<std::uint8_t>& bytes)
{
    auto backend =
        makeShared<MemoryFileBackend>(MemoryFileBackendDesc{1, sourceBudget, 1, sourceBudget, 128});
    MountedFileBackend mounts({1, 1, sourceBudget, 32, 128});
    ResourceFileOrigin origin{FilePathScope::MountedResource, mounts.mount("fonts", backend)};
    for (unsigned test = 0; test < 11; ++test) {
        ResourceSourceSet sources;
        core::pmr::String metadata = tests::uiFontMetadataFixture();
        const char* fontPath = test == 10 ? u8"fonts/字体.ttf" : "fonts/test.ttf";
        if (test == 10)
            for (unsigned i = 0; i < 2; ++i)
                metadata.replace(metadata.find("fonts/test.ttf"),
                    std::string_view("fonts/test.ttf").size(),
                    fontPath);
        add(sources, "meta/atlas.arpui", metadata, origin);
        if (test != 7)
            add(sources,
                fontPath,
                {reinterpret_cast<const char*>(bytes.data()), bytes.size()},
                test == 8 ? ResourceFileOrigin{} : origin);
        ResourceLoadContext context;
        context.sources = &sources;
        context.requirePreloadedSources = true;
        ResourceManager manager;
        assert(manager
                .loadResource(
                    makeUnique<ui::UiAtlasMetadataResource>(atlasDescriptor(origin)), context)
                .success);
        auto desc = fontDescriptor(origin);
        if (test == 1)
            desc.maxFaces = 1;
        if (test == 2)
            desc.maxFontBytes = bytes.size() - 1;
        if (test == 3)
            desc.maxTotalSourceBytes = bytes.size() - 1;
        if (test == 4)
            desc.maxFontBytes = 0;
        if (test == 5)
            desc.maxFontBytes = SIZE_MAX;
        if (test == 6)
            desc.maxTotalSourceBytes = 0;
        if (test == 9)
            desc.atlasMetadataAlias = "ui/missing";
        assert(manager.declareResource(fontId, ui::kUiFontSetResourceType).success);
        const auto* prior = manager.recordSnapshot(fontId);
        const auto generation = prior->generation;
        core::pmr::Vector<core::UniquePtr<ResourceBase>> candidates;
        candidates.push_back(makeUnique<ui::UiFontSetResource>(desc));
        const auto loaded = manager.loadResourcesAtomically(std::move(candidates), context);
        assert(loaded.success == (test == 0 || test == 10));
        if (!loaded.success)
            assert(manager.recordSnapshot(fontId) == prior && prior->generation == generation
                && prior->state == ResourceState::Declared);
        else
            checkFonts(manager, fontPath);
    }
    mounts.shutdown();
}

void graph(core::SharedPtr<ManagedFileBackend> backend)
{
    auto mounts =
        makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 1, sourceBudget, 32, 128});
    ResourceFileOrigin origin{FilePathScope::MountedResource, mounts->mount("fonts", backend)};
    runtime::AsyncRuntimeDesc desc;
    desc.enableWork = desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = mounts;
    desc.completionQueueCapacity = desc.completionBudget = 4;
    runtime::AsyncRuntime runtime(desc);
    ResourceManager manager;
    ResourceGraphExecutor executor(manager, runtime.fileService(), runtime.completionQueue());
    ResourceGraphExecutionRequest request;
    request.graph.rootResourceId = fontId;
    ResourceGraphNode fonts;
    fonts.resource = makeUnique<ui::UiFontSetResource>(fontDescriptor(origin));
    fonts.dependencies.push_back({atlasId, "ui.atlas.metadata.fonts"});
    ResourceGraphNode atlas;
    atlas.resource = makeUnique<ui::UiAtlasMetadataResource>(atlasDescriptor(origin));
    request.graph.nodes.push_back(std::move(fonts));
    request.graph.nodes.push_back(std::move(atlas));
    request.budget = {2, 2, sourceBudget + metadataBudget, sourceBudget, 4};

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
        std::fprintf(stderr, "UI font graph: %s\n", completion.result.resource.diagnostics.c_str());
    assert(
        completion.result.resource.success && completion.result.telemetry.submittedFileReads == 2);
    checkFonts(manager);
    runtime.shutdown();
    assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved());
    assert(!mounts->telemetry().inFlight);
}

void cycle(const char* url, const core::pmr::Vector<std::uint8_t>& bytes)
{
    if (url) {
        HttpFileBackendDesc desc;
        desc.baseUrl = url;
        desc.maxInFlight = 1;
        desc.maxBytesInFlight = sourceBudget + metadataBudget * 4;
        desc.maxPathBytes = 128;
        desc.maxHeaderBytes = 8 * 1024;
        desc.ioChunkBytes = 4096;
        desc.timeoutMs = 3000;
        desc.cancellationPollMs = 10;
        auto backend = makeShared<HttpFileBackend>(desc);
        graph(backend);
        assert(!backend->telemetry().inFlight && !backend->telemetry().activeNativeTransfers);
    } else {
        sourceAndCacheContracts(bytes);
        resourceContracts(bytes);
        auto backend = makeShared<MemoryFileBackend>(
            MemoryFileBackendDesc{4, sourceBudget + metadataBudget, 1, sourceBudget, 128});
        assert(backend->registerDirectory("meta") == FileIoStatus::Success);
        assert(backend->registerDirectory("fonts") == FileIoStatus::Success);
        const std::string_view metadata = tests::uiFontMetadataFixture();
        assert(backend->registerFile("meta/atlas.arpui",
                   reinterpret_cast<const std::uint8_t*>(metadata.data()),
                   metadata.size())
            == FileIoStatus::Success);
        assert(backend->registerFile("fonts/test.ttf", bytes.data(), bytes.size())
            == FileIoStatus::Success);
        graph(backend);
    }
}

} // namespace

int main(int argc, char** argv)
{
    const char* url = argc == 3 && std::string_view(argv[1]) == "--http" ? argv[2] : nullptr;
    assert(argc == 1 || url);
    core::BinaryFileReadResult fixture;
    if (!url) {
        fixture = core::readBinaryFile(ARP_FONT_IO_FIXTURE_PATH, sourceBudget);
        assert(fixture.success);
    }
    assert(ui::uiFreeTypeBackendAvailable());
    for (unsigned i = 0; i < 20; ++i)
        cycle(url, fixture.bytes);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle(url, fixture.bytes);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    std::puts(
        "UI font source: 20 warmup +20 measured cycles; source and FreeType core memory returned to baseline");
    std::printf(
        "UI font source core peak bytes: %llu; baseline live bytes: %llu; measured end live bytes: %llu\n",
        static_cast<unsigned long long>(after.peakBytes),
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes));
}
