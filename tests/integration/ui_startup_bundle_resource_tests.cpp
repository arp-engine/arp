#include "core/container/generation_pool.h"
#include "core/io/file_io.h"
#include "core/io/http_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "renderer/ui/ui_atlas_runtime.h"
#include "tests/fixtures/ui_bundle_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace {

using namespace arp;
using Fixture = tests::UiBundleFixture;

void replace(core::pmr::String& text, core::StringView from, core::StringView to)
{
    const auto at = text.find(from);
    assert(at != core::pmr::String::npos);
    text.replace(at, from.size(), to.data(), to.size());
}

void assertRetained(const renderer::ui::UiStartupBundleResourceData& data)
{
    assert(data.documents.size() == 1 && data.documents.front()->document.nodes().size() == 3);
    assert(data.fontSet->fonts.size() == 2);
    assert(data.fontSet->fonts[0].source == data.fontSet->fonts[1].source);
    assert(data.retainedPayloadBytes
        == data.fontSet->fonts.front().source->bytes().size() + 2 * 32 * 16 * 4);
    renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas = data.atlasTexture->atlasTexture;
    desc.fontFaces = &data.fontSet->fonts;
    desc.maxWidth = 128;
    desc.maxHeight = 128;
    auto runtime = renderer::ui::UnifiedUiAtlasRuntime::build(desc);
    assert(runtime.ok());
    renderer::ui::UiDrawCommand text;
    text.kind = renderer::ui::UiDrawCommandKind::Text;
    text.fontToken = "font.test";
    text.styleId = "body";
    text.text = "AV";
    text.rect = ui::LayoutRect::fromOriginSize(0, 0, 96, 20);
    text.clipRect = text.rect;
    core::pmr::Vector<renderer::ui::UiDrawCommand> commands;
    commands.push_back(text);
    const auto resolved = runtime.resolve(commands);
    assert(resolved.ok() && resolved.commands().size() == 2);
    auto recovered = renderer::ui::UnifiedUiAtlasRuntime::build(runtime.makeRecoveryPlan());
    assert(recovered.ok() && recovered.atlas().rgba8 == runtime.atlas().rgba8);
}

void rejected(resource::ResourceManager& manager,
    const renderer::ui::UiStartupBundleResourceLoadDesc& desc,
    const char* diagnostic)
{
    const auto* prior = manager.recordSnapshot(Fixture::bundleId);
    const auto generation = manager.generationOf(Fixture::bundleId);
    const auto* child = manager.recordSnapshot(Fixture::themeId);
    const auto childGeneration = manager.generationOf(Fixture::themeId);
    const auto loaded = renderer::ui::loadUiStartupBundleResource(manager, desc);
    if (loaded.success || loaded.diagnostics.find(diagnostic) == core::pmr::String::npos)
        std::fprintf(stderr, "Bundle rejection (%s): %s\n", diagnostic, loaded.diagnostics.c_str());
    assert(!loaded.success && loaded.diagnostics.find(diagnostic) != core::pmr::String::npos);
    assert(manager.recordSnapshot(Fixture::bundleId) == prior);
    assert(manager.generationOf(Fixture::bundleId) == generation);
    assert(!manager.resolveAlias("ui/bundle").success);
    if (prior)
        assert(prior->state == resource::ResourceState::Declared && !prior->metadataAdmitted);
    assert(manager.recordSnapshot(Fixture::themeId) == child);
    assert(child->state == resource::ResourceState::Ready
        && manager.generationOf(Fixture::themeId) == childGeneration);
    assert(manager.resolveAlias("ui/theme").resourceId == Fixture::themeId);
}

void contracts(const core::pmr::Vector<std::uint8_t>& bytes)
{
    core::GenerationPool<unsigned> provenance(1);
    resource::ResourceFileOrigin origin{
        core::io::FilePathScope::MountedResource, provenance.emplace(1)};
    Fixture fixture(origin);
    auto sources = fixture.sources(bytes);
    resource::ResourceManager manager;
    fixture.loadChildren(manager, sources);
    assert(manager.declareResource(Fixture::bundleId, renderer::ui::kUiStartupBundleResourceType)
            .success);
    auto desc = fixture.bundle();
    const auto payloadBytes = bytes.size() + 2 * 32 * 16 * 4;
    for (unsigned test = 0; test < 13; ++test) {
        auto bad = desc;
        const char* diagnostic = "UI-BUNDLE-BUDGET";
        if (test == 0)
            bad.budget.maxDocuments = 0;
        if (test == 1)
            bad.budget.maxDocuments = SIZE_MAX;
        if (test == 2)
            bad.budget.maxFontFaces = 0;
        if (test == 3)
            bad.budget.maxRetainedPayloadBytes = 0;
        if (test == 4)
            bad.budget.maxRetainedPayloadBytes = SIZE_MAX;
        if (test == 5) {
            bad.budget.maxFontFaces = 1;
            diagnostic = "UI-BUNDLE-FONT-BUDGET";
        }
        if (test == 6) {
            bad.budget.maxRetainedPayloadBytes = payloadBytes - 1;
            diagnostic = "UI-BUNDLE-PAYLOAD-BUDGET";
        }
        if (test == 7)
            bad.documentAliases.clear();
        if (test == 8) {
            bad.documentAliases.push_back("ui/document");
            bad.budget.maxDocuments = 2;
            diagnostic = "UI-BUNDLE-DOCUMENT-DUPLICATE";
        }
        if (test == 9) {
            bad.fontSetAlias = "ui/missing";
            diagnostic = "UI-BUNDLE-CHILD-NOT-READY";
        }
        if (test == 10) {
            bad.fontSetAlias = "ui/theme";
            diagnostic = "UI-BUNDLE-CHILD-NOT-READY";
        }
        if (test == 11) {
            bad.documentAliases[0] = "ui/missing";
            diagnostic = "UI-BUNDLE-CHILD-NOT-READY";
        }
        if (test == 12) {
            bad.alias = "ui/theme";
            diagnostic = "duplicate alias";
        }
        rejected(manager, bad, diagnostic);
    }

    // Direct member preparation must preserve existing data and dependencies.
    renderer::ui::UiStartupBundleResource local(desc);
    local.data = core::makeShared<renderer::ui::UiStartupBundleResourceData>();
    const auto original = local.data;
    local.addDependency({Fixture::documentId, "test.original"});
    local.loadDesc.alias = "ui/changed-identity";
    const auto identity = local.load(manager, {});
    assert(!identity.success && identity.diagnostics == "UI-BUNDLE-IDENTITY-MISMATCH");
    assert(local.data == original && local.dependencies().size() == 1);
    local.loadDesc.alias = desc.alias;
    local.loadDesc.budget.maxRetainedPayloadBytes = payloadBytes - 1;
    assert(!local.load(manager, {}).success && local.data == original);
    assert(local.dependencies().size() == 1 && local.dependencies()[0].usage == "test.original");
    unsigned allocationFailures = 0, allocationSuccesses = 0;
    for (std::uint64_t limit : {1, 1024, 4096, 65536}) {
        auto quota = core::makeShared<core::CpuMemoryQuota>(limit);
        {
            resource::ResourceManager isolated;
            fixture.loadChildren(isolated, sources);
            assert(isolated
                    .declareResource(Fixture::bundleId, renderer::ui::kUiStartupBundleResourceType)
                    .success);
            const auto* before = isolated.recordSnapshot(Fixture::bundleId);
            const auto generation = isolated.generationOf(Fixture::bundleId);
            renderer::ui::UiStartupBundleResourceLoadResult loaded;
            {
                core::CpuMemoryQuotaScope scope(quota);
                loaded = renderer::ui::loadUiStartupBundleResource(isolated, desc);
            }
            {
                if (loaded.success) {
                    ++allocationSuccesses;
                    assert(isolated.find<renderer::ui::UiStartupBundleResource>(Fixture::bundleId)
                            .ready());
                } else {
                    ++allocationFailures;
                    assert(loaded.diagnosticId);
                    if (limit == 1) {
                        assert(std::string_view(loaded.diagnosticId) == "UI-BUNDLE-ALLOCATION");
                        assert(loaded.diagnostics.empty() && loaded.resource.diagnostics.empty());
                    }
                    assert(isolated.recordSnapshot(Fixture::bundleId) == before);
                    assert(before->state == resource::ResourceState::Declared
                        && !before->metadataAdmitted);
                    assert(isolated.generationOf(Fixture::bundleId) == generation);
                    assert(!isolated.resolveAlias("ui/bundle").success);
                }
            }
        }
        assert(quota->liveBytes() == 0);
    }
    assert(allocationFailures && allocationSuccesses);
    assert(!manager.recordSnapshot(Fixture::bundleId)->metadataAdmitted);
    assert(local.data == original && local.dependencies().size() == 1);

    // A caller's explicit additional dependency remains part of admission.
    {
        resource::ResourceManager directManager;
        fixture.loadChildren(directManager, sources);
        renderer::ui::UiStartupBundleResource direct(desc);
        direct.addDependency({Fixture::documentId, "test.additional"});
        const auto loaded = direct.load(directManager, {});
        assert(loaded.success && direct.dependencies().size() == 7);
        assert(direct.dependencies().front().usage == "test.additional");
    }

    // Admission binds managed identity, not just same-looking metadata bytes.
    auto otherAtlas = fixture.atlas();
    otherAtlas.resourceId = "88888888-8888-4888-8888-888888888888";
    otherAtlas.alias = "ui/atlas-other";
    resource::ResourceLoadContext context;
    context.sources = &sources;
    context.requirePreloadedSources = true;
    assert(manager.loadResource(core::makeUnique<ui::UiAtlasMetadataResource>(otherAtlas), context)
            .success);
    auto otherFonts = fixture.fonts();
    otherFonts.resourceId = "99999999-9999-4999-8999-999999999999";
    otherFonts.alias = "ui/fonts-other";
    otherFonts.atlasMetadataAlias = otherAtlas.alias;
    assert(
        manager.loadResource(core::makeUnique<ui::UiFontSetResource>(otherFonts), context).success);
    auto wrongBinding = desc;
    wrongBinding.fontSetAlias = otherFonts.alias;
    rejected(manager, wrongBinding, "UI-BUNDLE-DEPENDENCY-MISMATCH");
    auto& admittedFace =
        manager.find<ui::UiFontSetResource>(Fixture::fontId).get()->data->fonts.front();
    const auto originalSource = admittedFace.source;
    auto foreignOrigin = originalSource->desc();
    ++foreignOrigin.mount.generation;
    core::pmr::String sourceDiagnostic;
    admittedFace.source = ui::UiFontSource::create(foreignOrigin, bytes, sourceDiagnostic);
    assert(admittedFace.source);
    rejected(manager, desc, "UI-BUNDLE-FONT-METADATA");
    admittedFace.source = originalSource;

    desc.budget.maxRetainedPayloadBytes = payloadBytes;
    const auto* record = manager.recordSnapshot(Fixture::bundleId);
    const auto generation = manager.generationOf(Fixture::bundleId);
    const auto loaded = renderer::ui::loadUiStartupBundleResource(manager, desc);
    if (!loaded.success)
        std::fprintf(stderr, "Bundle: %s\n", loaded.diagnostics.c_str());
    assert(loaded.success);
    auto handle = manager.find<renderer::ui::UiStartupBundleResource>(Fixture::bundleId);
    auto retained = handle.get()->data;
    assert(manager.recordSnapshot(Fixture::bundleId) == record
        && manager.generationOf(Fixture::bundleId) == generation + 1);
    assert(handle.get()->dependencies().size() == 6);
    const auto duplicate = renderer::ui::loadUiStartupBundleResource(manager, desc);
    assert(!duplicate.success && handle.ready() && handle.get()->data == retained);
    assert(manager.recordSnapshot(Fixture::bundleId) == record);
    assert(manager.unload(Fixture::bundleId).success);
    assert(renderer::ui::loadUiStartupBundleResource(manager, desc).success);
    assert(!handle.get());
    for (const auto* id : {Fixture::bundleId,
             Fixture::documentId,
             Fixture::textureId,
             Fixture::imageId,
             Fixture::fontId,
             Fixture::themeId,
             Fixture::atlasId})
        assert(manager.unload(id).success);
    assertRetained(*retained);
}

void metadataRejections(const core::pmr::Vector<std::uint8_t>& bytes)
{
    core::GenerationPool<unsigned> provenance(1);
    resource::ResourceFileOrigin origin{
        core::io::FilePathScope::MountedResource, provenance.emplace(1)};
    Fixture fixture(origin);
    for (unsigned test = 0; test < 8; ++test) {
        core::pmr::String theme = tests::uiBundleThemeFixture();
        core::pmr::String document = tests::uiBundleDocumentFixture();
        const char* diagnostic = "UI-BUNDLE-DOCUMENT-TEXTURE";
        if (test == 0)
            replace(document, "atlas: test.font.atlas", "atlas: other.atlas");
        if (test == 1)
            replace(document, "width: 32, height: 16", "width: 33, height: 16");
        if (test == 2)
            replace(document, "srgb: true", "srgb: false");
        if (test == 3)
            replace(document, "sampler: linear-clamp", "sampler: nearest-clamp");
        if (test == 4)
            replace(document, "alpha: straight", "alpha: premultiplied");
        if (test == 5) {
            replace(theme, "family: Droid Sans", "family: Unbound");
            diagnostic = "UI-BUNDLE-THEME-FONT";
        }
        if (test == 6) {
            replace(document, "style: body\n      state", "style: missing\n      state");
            diagnostic = "UI-BUNDLE-DOCUMENT-FONT";
        }
        if (test == 7) {
            replace(document,
                "33333333-3333-4333-8333-333333333333",
                "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
            replace(document,
                "33333333-3333-4333-8333-333333333333",
                "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
        }
        auto sources = fixture.sources(bytes, theme, document);
        resource::ResourceManager manager;
        fixture.loadChildren(manager, sources);
        rejected(manager, fixture.bundle(), diagnostic);
    }
}

void graph(core::SharedPtr<core::io::ManagedFileBackend> backend)
{
    auto mounts = core::makeShared<core::io::MountedFileBackend>(core::io::MountedFileBackendDesc{
        1, 5, Fixture::sourceBudget + 4 * Fixture::textBudget, 32, 128});
    resource::ResourceFileOrigin origin{
        core::io::FilePathScope::MountedResource, mounts->mount("bundle", backend)};
    Fixture fixture(origin);
    core::runtime::AsyncRuntimeDesc desc;
    desc.enableWork = desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = mounts;
    desc.completionQueueCapacity = desc.completionBudget = 8;
    core::runtime::AsyncRuntime runtime(desc);
    resource::ResourceManager manager;
    resource::ResourceGraphExecutor executor(
        manager, runtime.fileService(), runtime.completionQueue());

    struct Completion {
        unsigned count = 0;
        resource::ResourceGraphExecutionResult result;
    } completion;

    assert(executor.submit(fixture.graph(),
               {[](void* context, resource::ResourceGraphExecutionResult result) {
                    auto& state = *static_cast<Completion*>(context);
                    ++state.count;
                    state.result = std::move(result);
                },
                   &completion})
        == resource::ResourceGraphSubmitStatus::Accepted);
    unified_io_fixture::wait(runtime.completionQueue(), [&] {
        return completion.count != 0;
    });
    if (!completion.result.resource.success)
        std::fprintf(stderr,
            "Bundle graph: %s / %s\n",
            completion.result.diagnosticId ? completion.result.diagnosticId : "none",
            completion.result.resource.diagnostics.c_str());
    assert(completion.count == 1 && completion.result.resource.success);
    assert(completion.result.telemetry.submittedFileReads == 5
        && completion.result.telemetry.completedNodes == 7);
    assert(runtime.workService().telemetry().submitted == 0);
    const auto handle = manager.find<renderer::ui::UiStartupBundleResource>(Fixture::bundleId);
    const auto retained = handle.get()->data;
    runtime.shutdown();
    assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved());
    assert(!mounts->telemetry().inFlight);
    for (const auto* id : {Fixture::bundleId,
             Fixture::documentId,
             Fixture::textureId,
             Fixture::imageId,
             Fixture::fontId,
             Fixture::themeId,
             Fixture::atlasId})
        assert(manager.unload(id).success);
    assertRetained(*retained);
}

void cycle(const char* url, const core::pmr::Vector<std::uint8_t>& bytes)
{
    if (url) {
        core::io::HttpFileBackendDesc desc;
        desc.baseUrl = url;
        desc.maxInFlight = 5;
        desc.maxBytesInFlight = Fixture::sourceBudget + 4 * Fixture::textBudget;
        desc.maxPathBytes = 128;
        desc.maxHeaderBytes = 8 * 1024;
        desc.ioChunkBytes = 4096;
        desc.timeoutMs = 3000;
        desc.cancellationPollMs = 10;
        auto backend = core::makeShared<core::io::HttpFileBackend>(desc);
        graph(backend);
        assert(!backend->telemetry().inFlight && !backend->telemetry().activeNativeTransfers);
    } else {
        contracts(bytes);
        metadataRejections(bytes);
        graph(Fixture::memory(bytes));
    }
}

} // namespace

int main(int argc, char** argv)
{
    const char* url = argc == 3 && std::string_view(argv[1]) == "--http" ? argv[2] : nullptr;
    assert(argc == 1 || url);
    core::BinaryFileReadResult fixture;
    if (!url) {
        fixture = core::readBinaryFile(ARP_FONT_IO_FIXTURE_PATH, Fixture::sourceBudget);
        assert(fixture.success);
    }
    for (unsigned i = 0; i < 20; ++i)
        cycle(url, fixture.bytes);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle(url, fixture.bytes);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    std::printf(
        "UI bundle: 20 warmup +20 measured cycles; baseline=%llu end=%llu peak=%llu; core live bytes/count returned to baseline\n",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(after.peakBytes));
}
