#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "resource/resource_graph_executor.h"
#include "tests/fixtures/ui_document_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"
#include "ui/ui_document_resource.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string_view>

namespace {

using namespace arp;
using namespace arp::core;
using namespace arp::core::io;
using namespace arp::resource;

constexpr const char* id = "c28d4d95-953b-4c4c-9631-0c54d44db13a";
// Fixture text and one output plan fit this explicit CPU-only test budget.
constexpr std::size_t sourceBudget = 16 * 1024;

ui::UiDocumentResourceLoadDesc descriptor()
{
    ui::UiDocumentResourceLoadDesc desc;
    desc.resourceId = id;
    desc.alias = "ui/source/document";
    desc.status = "Prototype Complete";
    desc.documentContract = "ui.document/v1";
    desc.documentPath = "hud.arpui";
    desc.maxDocumentBytes = sourceBudget;
    return desc;
}

void checkDocument(const ui::UiDocumentResource& document)
{
    assert(document.data);
    assert(document.data->sourcePath == "hud.arpui");
    assert(document.data->drawPlan.recordCount() == 3);
    assert(document.data->drawPlan.textRecordCount() == 1);
    assert(document.data->drawPlan.imageRecordCount() == 1);
    assert(!document.data->bindings.empty());
    bool sourceRect = false;
    for (const auto& record : document.data->drawPlan.records()) {
        const auto binding = std::find_if(document.data->bindings.begin(),
            document.data->bindings.end(),
            [&](const ui::UiDocumentRuntimeBinding& binding) {
                return binding.widgetId == record.widgetId;
            });
        assert(binding != document.data->bindings.end());
        assert(binding->rect.x == record.rect.x && binding->rect.y == record.rect.y);
        assert(
            binding->rect.width == record.rect.width && binding->rect.height == record.rect.height);
        if (record.kind == ui::DrawRecordKind::Image)
            sourceRect = record.hasTextureSourceRect;
    }
    assert(sourceRect);
}

void capturedSource(std::string_view bytes,
    bool success,
    const char* diagnostic = nullptr,
    bool wrongOrigin = false,
    std::size_t budget = sourceBudget,
    bool missing = false)
{
    auto desc = descriptor();
    desc.maxDocumentBytes = budget;
    auto backend =
        makeShared<MemoryFileBackend>(MemoryFileBackendDesc{1, sourceBudget, 1, sourceBudget, 128});
    MountedFileBackend mounts({1, 1, sourceBudget, 32, 128});
    desc.origin = {FilePathScope::MountedResource, mounts.mount("ui", backend)};
    ResourceSourceSet sources;
    ResourceSourceBuffer source;
    source.path = "hud.arpui";
    source.pathScope = wrongOrigin ? FilePathScope::HostFilesystem : desc.origin.pathScope;
    source.mount = wrongOrigin ? PoolHandle{} : desc.origin.mount;
    source.bytes.assign(bytes.begin(), bytes.end());
    if (!missing)
        assert(sources.add(std::move(source)));
    ResourceManager manager;
    assert(manager.declareResource(id, ui::kUiDocumentResourceType).success);
    const auto* prior = manager.recordSnapshot(id);
    const auto generation = manager.generationOf(id);
    ResourceLoadContext context;
    context.sources = &sources;
    context.requirePreloadedSources = true;
    core::pmr::Vector<core::UniquePtr<ResourceBase>> candidates;
    candidates.push_back(makeUnique<ui::UiDocumentResource>(desc));
    auto loaded = manager.loadResourcesAtomically(std::move(candidates), context);
    if (loaded.success != success)
        std::fprintf(stderr, "UI source: %s\n", loaded.diagnostics.c_str());
    assert(loaded.success == success);
    assert(manager.recordSnapshot(id) == prior);
    if (success) {
        checkDocument(*manager.find<ui::UiDocumentResource>(id).get());
        assert(manager.generationOf(id) == generation + 1);
    } else {
        assert(manager.generationOf(id) == generation && prior->state == ResourceState::Declared);
        assert(!manager.resolveAlias(desc.alias).success);
        if (diagnostic)
            assert(loaded.diagnostics.find(diagnostic) != core::pmr::String::npos);
    }
    mounts.shutdown();
}

void sourceContracts()
{
    const std::string_view fixture = tests::uiDocumentFixture();
    capturedSource(fixture, true);
    core::pmr::String bytes = "\xEF\xBB\xBF";
    bytes += fixture;
    capturedSource(bytes, true);
    capturedSource("", false, "ARP-RESOURCE-SOURCE-BUDGET");
    capturedSource("\xEF\xBB\xBF", false, "UI-DOCUMENT-SOURCE-EMPTY");
    bytes = fixture;
    bytes.push_back('\0');
    capturedSource(bytes, false, "NUL");
    bytes = fixture;
    bytes += "\xC0\xAF";
    capturedSource(bytes, false, "UTF8");
    capturedSource(fixture, false, "LOCATION-MISMATCH", true);
    capturedSource(fixture, false, "NOT-PRELOADED", false, sourceBudget, true);
    capturedSource(fixture, false, "SOURCE-BUDGET", false, fixture.size() - 1);
    capturedSource(fixture, false, "SOURCE-CONFIG", false, 0);
    capturedSource(fixture, false, "SOURCE-CONFIG", false, SIZE_MAX);
    capturedSource("type: ui.document\ncontractVersion: unknown\n", false);

    // Prepared mounting must reject an empty result without touching a component.
    ui::DocumentComponentDesc componentDesc;
    componentDesc.documentSource = "hud.arpui";
    componentDesc.inputRoute = "input";
    componentDesc.renderRoute = "render";
    ui::DocumentComponent component(componentDesc);
    ui::UiArpuiRuntimeLoadResult empty;
    assert(!ui::DocumentComponentMounter().mount(component, empty).ok());
    assert(!component.runtimeHandle().valid());
}

void graph(core::SharedPtr<ManagedFileBackend> backend)
{
    auto mounts =
        makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 1, sourceBudget, 32, 128});
    auto desc = descriptor();
    desc.origin = {FilePathScope::MountedResource, mounts->mount("ui", backend)};
    assert(desc.origin.mount);
    runtime::AsyncRuntimeDesc runtimeDesc;
    runtimeDesc.enableWork = runtimeDesc.enableThreadPoolFileBackend = false;
    runtimeDesc.fileBackend = mounts;
    runtimeDesc.completionQueueCapacity = runtimeDesc.completionBudget = 4;
    runtime::AsyncRuntime runtime(runtimeDesc);
    ResourceManager manager;
    assert(manager.declareResource(id, ui::kUiDocumentResourceType).success);
    ResourceGraphExecutor executor(manager, runtime.fileService(), runtime.completionQueue());
    ResourceGraphExecutionRequest request;
    request.graph.rootResourceId = id;
    ResourceGraphNode node;
    node.resource = makeUnique<ui::UiDocumentResource>(desc);
    request.graph.nodes.push_back(std::move(node));
    request.budget = {1, 1, sourceBudget, sourceBudget, 4};

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
    assert(!completion.count);
    unified_io_fixture::wait(runtime.completionQueue(), [&] {
        return completion.count != 0;
    });
    if (!completion.result.resource.success)
        std::fprintf(stderr,
            "UI graph: %s / %s\n",
            completion.result.diagnosticId ? completion.result.diagnosticId : "none",
            completion.result.resource.diagnostics.c_str());
    assert(completion.count == 1 && completion.result.resource.success);
    assert(completion.result.telemetry.submittedFileReads == 1);
    assert(completion.result.telemetry.bytesRead
        == std::string_view(tests::uiDocumentFixture()).size());
    assert(runtime.workService().telemetry().submitted == 0);
    checkDocument(*manager.find<ui::UiDocumentResource>(id).get());
    runtime.shutdown();
    assert(runtime.completionQueue().pending() == 0);
    assert(runtime.completionQueue().reserved() == 0);
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
        assert(backend->telemetry().inFlight == 0);
        assert(backend->telemetry().activeNativeTransfers == 0);
    } else {
        auto backend = makeShared<MemoryFileBackend>(
            MemoryFileBackendDesc{1, sourceBudget, 1, sourceBudget, 128});
        const std::string_view bytes = tests::uiDocumentFixture();
        assert(backend->registerFile(
                   "hud.arpui", reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size())
            == FileIoStatus::Success);
        graph(backend);
    }
}

} // namespace

int main(int argc, char** argv)
{
    const char* url = argc == 3 && std::string_view(argv[1]) == "--http" ? argv[2] : nullptr;
    assert(argc == 1 || url);
    for (unsigned i = 0; i < 20; ++i) {
        if (!url)
            sourceContracts();
        cycle(url);
    }
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i) {
        if (!url)
            sourceContracts();
        cycle(url);
    }
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    std::puts(
        "UI document source: 20 warmup +20 measured cycles; core memory returned to baseline");
}
