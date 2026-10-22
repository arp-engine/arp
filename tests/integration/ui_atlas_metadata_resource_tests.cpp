#include "core/memory/cpu_allocator.h"
#include "core/runtime/async_runtime.h"
#include "resource/resource_manager.h"
#include "ui/ui_atlas_metadata_resource.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

std::filesystem::path projectRoot()
{
    auto current = std::filesystem::current_path();
    while (!current.empty()) {
        if (std::filesystem::exists(current / "assets" / "pika" / "ui" / "pika_ui_atlas.arpui")) {
            return current;
        }
        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return std::filesystem::current_path();
}

arp::ui::UiAtlasMetadataResourceLoadDesc pikaAtlasDesc()
{
    const auto root = projectRoot();
    arp::ui::UiAtlasMetadataResourceLoadDesc desc;
    desc.resourceId = text("6cf0e0f0-d372-42eb-9e7c-78c5ba8eae1a");
    desc.alias = text("ui/pika/atlas/metadata");
    desc.status = text("Contract Complete");
    desc.atlasMetadataPath = root / "assets" / "pika" / "ui" / "pika_ui_atlas.arpui";
    desc.expectedAtlasResourceId = text("texture/pika_ui_atlas");
    return desc;
}

struct AsyncProbe {
    arp::resource::ResourceLoadResult result;
    bool called = false;
};

void recordAsyncLoad(void* userData, arp::resource::ResourceLoadResult result)
{
    auto* probe = static_cast<AsyncProbe*>(userData);
    probe->result = result;
    probe->called = true;
}

void atlas_metadata_resource_loads_explicit_yaml_contract_through_resource_manager()
{
    arp::resource::ResourceManager manager;
    const auto loaded = arp::ui::loadUiAtlasMetadataResource(manager, pikaAtlasDesc());
    assert(loaded.success);
    assert(loaded.resource.resourceType == text("UiAtlasMetadataResource"));

    const auto handle =
        manager.findByAlias<arp::ui::UiAtlasMetadataResource>(text("ui/pika/atlas/metadata"));
    assert(handle.ready());
    assert(handle.get() != nullptr);
    assert(handle.get()->data != nullptr);

    const auto& data = *handle.get()->data;
    assert(data.atlasId == text("pika.ui.atlas"));
    assert(data.resourceId == text("texture/pika_ui_atlas"));
    assert(data.sourcePath.generic_string() == "assets/pika/ui/textures/pika_ui_atlas.png");
    assert(data.width == 224.0f);
    assert(data.height == 96.0f);
    assert(data.srgb);
    assert(data.slices.size() == 3u);
    assert(data.fontFaces.size() == 4u);
    assert(data.glyphPolicy == text("hybrid"));
    assert(data.pagePolicy == text("singleTexture"));
    assert(data.fontFaces[0].fallbackChain.size() == 3u);
    assert(data.fontFaces[0].fallbackChain[0] == text("font.ui.hud.title"));
    assert(data.fontFaces[0].supportedScripts.size() == 1u);
    assert(data.fontFaces[0].supportedScripts[0] == text("Latin"));
    assert(data.fontFaces[0].charset == text("pika.visible.ascii"));
}

void atlas_metadata_resource_loads_through_generic_async_resource_interface()
{
    arp::core::runtime::AsyncRuntimeDesc runtimeDesc;
    runtimeDesc.completionQueueCapacity = 4u;
    runtimeDesc.workerCount = 1u;
    runtimeDesc.workQueueCapacity = 2u;
    runtimeDesc.completionBudget = 2u;
    arp::core::runtime::AsyncRuntime runtime(runtimeDesc);
    arp::resource::ResourceManager manager;

    AsyncProbe probe;
    const auto submitted =
        manager.loadAsync<arp::ui::UiAtlasMetadataResource>(runtime.workService(),
            arp::resource::ResourceAsyncLoadCompletion{&recordAsyncLoad, &probe},
            arp::resource::ResourceLoadContext{},
            pikaAtlasDesc());
    assert(submitted == arp::core::runtime::WorkServiceStatus::Accepted);
    for (int i = 0; i < 100 && !probe.called; ++i) {
        runtime.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(probe.called);
    assert(probe.result.success);
    assert(manager.findByAlias<arp::ui::UiAtlasMetadataResource>(text("ui/pika/atlas/metadata"))
            .ready());
    runtime.shutdown(arp::core::runtime::AsyncRuntimeShutdownPolicy::Drain);
}

void atlas_metadata_sync_and_async_typed_load_paths_match()
{
    arp::resource::ResourceManager syncManager;
    const auto syncLoaded = syncManager.load<arp::ui::UiAtlasMetadataResource>(
        arp::resource::ResourceLoadContext{}, pikaAtlasDesc());
    assert(syncLoaded.success);
    const auto* syncRecord = syncManager.recordSnapshot(syncLoaded.resourceId);
    assert(syncRecord != nullptr);
    assert(syncRecord->alias == text("ui/pika/atlas/metadata"));
    assert(syncRecord->type == text("UiAtlasMetadataResource"));
    assert(syncRecord->dependencies.empty());
    assert(syncManager.findByAlias<arp::ui::UiAtlasMetadataResource>(text("ui/pika/atlas/metadata"))
            .ready());

    arp::core::runtime::AsyncRuntimeDesc runtimeDesc;
    runtimeDesc.completionQueueCapacity = 4u;
    runtimeDesc.workerCount = 1u;
    runtimeDesc.workQueueCapacity = 2u;
    runtimeDesc.completionBudget = 2u;
    arp::core::runtime::AsyncRuntime runtime(runtimeDesc);
    arp::resource::ResourceManager asyncManager;

    AsyncProbe probe;
    const auto submitted =
        asyncManager.loadAsync<arp::ui::UiAtlasMetadataResource>(runtime.workService(),
            arp::resource::ResourceAsyncLoadCompletion{&recordAsyncLoad, &probe},
            arp::resource::ResourceLoadContext{},
            pikaAtlasDesc());
    assert(submitted == arp::core::runtime::WorkServiceStatus::Accepted);
    for (int i = 0; i < 100 && !probe.called; ++i) {
        runtime.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(probe.called);
    assert(probe.result.success);
    const auto* asyncRecord = asyncManager.recordSnapshot(probe.result.resourceId);
    assert(asyncRecord != nullptr);
    assert(asyncRecord->alias == syncRecord->alias);
    assert(asyncRecord->type == syncRecord->type);
    assert(asyncRecord->dependencies.size() == syncRecord->dependencies.size());
    assert(
        asyncManager.findByAlias<arp::ui::UiAtlasMetadataResource>(text("ui/pika/atlas/metadata"))
            .ready());
    runtime.shutdown(arp::core::runtime::AsyncRuntimeShutdownPolicy::Drain);
}

void atlas_metadata_resource_rejects_contract_mismatch_and_missing_file()
{
    arp::resource::ResourceManager mismatchManager;
    auto mismatch = pikaAtlasDesc();
    mismatch.expectedAtlasResourceId = text("texture/not_declared_by_theme");
    const auto rejected = arp::ui::loadUiAtlasMetadataResource(mismatchManager, mismatch);
    assert(!rejected.success);
    assert(rejected.diagnostics.find("UI atlas metadata resourceId does not match")
        != arp::core::pmr::String::npos);

    arp::resource::ResourceManager missingManager;
    auto missing = pikaAtlasDesc();
    missing.atlasMetadataPath = projectRoot() / "assets" / "pika" / "ui" / "missing_atlas.arpui";
    const auto missingResult = arp::ui::loadUiAtlasMetadataResource(missingManager, missing);
    assert(!missingResult.success);
    assert(missingResult.diagnostics.find("UI atlas metadata file is missing")
        != arp::core::pmr::String::npos);
}

void atlas_metadata_resource_rejects_missing_font_fallback_metadata()
{
    const auto root = projectRoot();
    const auto tempPath = root / "artifacts" / "tmp" / "ui_atlas_missing_fallback_test.arpui";
    std::filesystem::create_directories(tempPath.parent_path());
    std::ofstream output(tempPath, std::ios::binary);
    output << "type: \"ui.atlas\"\n"
           << "contractVersion: \"ui.atlas/v2\"\n"
           << "status: \"Contract Complete\"\n"
           << "atlasId: \"test.ui.atlas\"\n"
           << "resourceId: \"texture/test_ui_atlas\"\n"
           << "baseImageAtlas:\n"
           << "  sourcePath: \"assets/pika/ui/textures/pika_ui_atlas.png\"\n"
           << "  width: 64\n"
           << "  height: 64\n"
           << "  colorSpace: \"srgb\"\n"
           << "fontFaces:\n"
           << "  - tokenId: \"font.ui.body\"\n"
           << "    family: \"Arial\"\n"
           << "    sourcePath: \"C:/Windows/Fonts/arial.ttf\"\n"
           << "    weight: 500\n"
           << "    lineHeightPx: 22\n"
           << "    supportedScripts:\n"
           << "      - \"Latin\"\n"
           << "    charset: \"ascii\"\n"
           << "glyphPolicy:\n"
           << "  mode: \"hybrid\"\n"
           << "pagePolicy:\n"
           << "  mode: \"singleTexture\"\n"
           << "slices:\n"
           << "  - sliceId: \"panel\"\n"
           << "    rect:\n"
           << "      x: 0\n"
           << "      y: 0\n"
           << "      width: 16\n"
           << "      height: 16\n";
    output.close();

    arp::resource::ResourceManager manager;
    auto desc = pikaAtlasDesc();
    desc.atlasMetadataPath = tempPath;
    desc.expectedAtlasResourceId = text("texture/test_ui_atlas");
    const auto loaded = arp::ui::loadUiAtlasMetadataResource(manager, desc);
    assert(!loaded.success);
    assert(loaded.diagnostics.find("fallbackChain") != arp::core::pmr::String::npos);
}

} // namespace

int main()
{
    atlas_metadata_resource_loads_explicit_yaml_contract_through_resource_manager();
    atlas_metadata_resource_loads_through_generic_async_resource_interface();
    atlas_metadata_sync_and_async_typed_load_paths_match();
    atlas_metadata_resource_rejects_contract_mismatch_and_missing_file();
    atlas_metadata_resource_rejects_missing_font_fallback_metadata();
    return 0;
}
