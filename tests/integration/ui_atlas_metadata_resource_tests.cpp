#include "core/runtime/async_runtime.h"
#include "resource/resource_graph_executor.h"
#include "tests/fixtures/ui_metadata_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"
#include "ui/ui_atlas_metadata_resource.h"

#include <cassert>
#include <cstdio>
#include <thread>

namespace {
using namespace arp;
constexpr const char* id = "6cf0e0f0-d372-42eb-9e7c-78c5ba8eae1a";
constexpr std::size_t bytesBudget = 16u * 1024u;

// The former Pika fixture referenced deleted source assets. This is an explicit
// bounded host fixture, shared with the current metadata source contract tests.
class MetadataFixture final {
public:
    MetadataFixture() { workspace.write("atlas.arpui", tests::uiMetadataAtlasFixture()); }

    ui::UiAtlasMetadataResourceLoadDesc descriptor() const
    {
        ui::UiAtlasMetadataResourceLoadDesc desc;
        desc.resourceId = id;
        desc.alias = "ui/test/atlas";
        desc.status = "Contract Complete";
        desc.atlasMetadataPath = workspace.root / "atlas.arpui";
        desc.expectedAtlasResourceId = "texture/test.atlas";
        desc.maxMetadataBytes = bytesBudget;
        return desc;
    }

    unified_io_fixture::Workspace workspace;
};

void check(const resource::ResourceManager& manager)
{
    const auto handle = manager.findByAlias<ui::UiAtlasMetadataResource>("ui/test/atlas");
    assert(handle.ready() && handle.get()->data);
    const auto& data = *handle.get()->data;
    assert(data.atlasId == "test.atlas" && data.resourceId == "texture/test.atlas");
    assert(data.sourcePath == "images/atlas.png" && data.srgb);
    assert(data.width == 32 && data.height == 16 && data.slices.size() == 1);
    assert(data.fontFaces.size() == 1 && data.fontFaces[0].fallbackChain[0] == "font.test");
    assert(data.fontFaces[0].supportedScripts[0] == "Latin");
    assert(data.glyphPolicy == "hybrid" && data.pagePolicy == "singleTexture");
}

void loadContracts()
{
    MetadataFixture fixture;
    resource::ResourceManager sync;
    const auto loaded = ui::loadUiAtlasMetadataResource(sync, fixture.descriptor());
    if (!loaded.success)
        std::fprintf(stderr, "Host metadata fixture: %s\n", loaded.diagnostics.c_str());
    assert(loaded.success);
    check(sync);

    core::runtime::AsyncRuntimeDesc config;
    config.completionQueueCapacity = config.completionBudget = 4;
    config.workerCount = 1;
    config.workQueueCapacity = 2;
    core::runtime::AsyncRuntime runtime(config);
    resource::ResourceManager async;

    struct Probe {
        std::thread::id owner = std::this_thread::get_id();
        unsigned calls = 0;
        resource::ResourceGraphExecutionResult result;
    } probe;

    assert(async.loadAsync<ui::UiAtlasMetadataResource>(runtime,
               resource::ResourceGraphExecutionBudget{1, 1, bytesBudget, bytesBudget, 4},
               resource::ResourceGraphExecutionCompletion{
                   [](void* context, resource::ResourceGraphExecutionResult result) {
                       auto& p = *static_cast<Probe*>(context);
                       assert(std::this_thread::get_id() == p.owner);
                       ++p.calls;
                       p.result = std::move(result);
                   },
                   &probe},
               {},
               fixture.descriptor())
        == resource::ResourceGraphSubmitStatus::Accepted);
    assert(probe.calls == 0 && !async.recordSnapshot(id));
    unified_io_fixture::wait(runtime.completionQueue(), [&] {
        return probe.calls != 0;
    });
    if (!probe.result.resource.success)
        std::fprintf(stderr,
            "Async host metadata: %s / %s\n",
            probe.result.resource.diagnostics.c_str(),
            probe.result.diagnosticId ? probe.result.diagnosticId : "none");
    assert(probe.calls == 1 && probe.result.resource.success);
    assert(probe.result.telemetry.submittedFileReads == 1);
    check(async);
    assert(async.recordSnapshot(id)->contentHash == sync.recordSnapshot(id)->contentHash);
    assert(async.recordSnapshot(id)->dependencies.empty());
    runtime.shutdown(core::runtime::AsyncRuntimeShutdownPolicy::Drain);
    assert(runtime.completionQueue().pending() == 0 && runtime.completionQueue().reserved() == 0);
}

void rejectedInputs()
{
    MetadataFixture fixture;
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        auto desc = fixture.descriptor();
        if (scenario == 0)
            desc.expectedAtlasResourceId = "texture/undeclared";
        if (scenario == 1)
            desc.atlasMetadataPath = fixture.workspace.root / "missing.arpui";
        if (scenario == 2) {
            core::pmr::String text = tests::uiMetadataAtlasFixture();
            constexpr core::StringView fallback = "    fallbackChain: [font.test]\n";
            const auto at = text.find(fallback);
            assert(at != core::pmr::String::npos);
            text.erase(at, fallback.size());
            fixture.workspace.write("atlas.arpui", text);
        }
        resource::ResourceManager manager;
        const auto rejected = ui::loadUiAtlasMetadataResource(manager, desc);
        assert(!rejected.success && !rejected.diagnostics.empty());
        assert(!manager.find<ui::UiAtlasMetadataResource>(id).ready());
        if (scenario == 2)
            assert(rejected.diagnostics.find("fallbackChain") != core::pmr::String::npos);
    }
}
} // namespace

int main()
{
    loadContracts();
    rejectedInputs();
    std::puts("Native metadata source: sync/async owner admission and content identity PASS");
}
