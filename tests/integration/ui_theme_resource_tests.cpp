#include "core/cpu_allocator.h"
#include "resource/resource_manager.h"
#include "ui/ui_atlas_metadata_resource.h"
#include "ui/ui_theme_resource.h"

#include <cassert>
#include <filesystem>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

std::filesystem::path projectRoot()
{
    auto current = std::filesystem::current_path();
    while (!current.empty()) {
        if (std::filesystem::exists(current / "assets" / "pika" / "ui" / "theme.arpui")) {
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

arp::ui::UiThemeResourceLoadDesc pikaThemeDesc()
{
    const auto root = projectRoot();
    arp::ui::UiThemeResourceLoadDesc desc;
    desc.resourceId = text("8d958aa4-f406-4bc7-9eb4-4cc6cc15c8fe");
    desc.alias = text("ui/pika/theme/default");
    desc.status = text("Contract Complete");
    desc.themePath = root / "assets" / "pika" / "ui" / "theme.arpui";
    desc.atlasMetadataPath = root / "assets" / "pika" / "ui" / "pika_ui_atlas.arpui";
    desc.atlasMetadataAlias = text("ui/pika/atlas/metadata");
    return desc;
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
}

void theme_resource_loads_explicit_yaml_contract_through_resource_manager()
{
    arp::resource::ResourceManager manager;
    const auto atlasLoaded = arp::ui::loadUiAtlasMetadataResource(manager, pikaAtlasDesc());
    assert(atlasLoaded.success);

    const auto loaded = arp::ui::loadUiThemeResource(manager, pikaThemeDesc());
    assert(loaded.success);
    assert(loaded.resource.resourceType == text("UiThemeResource"));

    const auto handle =
        manager.findByAlias<arp::ui::UiThemeResource>(text("ui/pika/theme/default"));
    assert(handle.ready());
    assert(handle.get() != nullptr);
    assert(handle.get()->data != nullptr);

    const auto& data = *handle.get()->data;
    assert(data.theme.themeId() == text("pika.theme.default"));
    assert(data.theme.findToken(text("color.panel.background")) != nullptr);
    assert(data.fonts.size() >= 4u);
    assert(data.styleColors.find(text("style.panel.surface")) != data.styleColors.end());
    assert(data.styleFonts.find(text("style.panel.surface")) != data.styleFonts.end());
    assert(data.atlasResourceId == text("texture/pika_ui_atlas"));
    assert(data.atlasSourcePath.generic_string() == "assets/pika/ui/textures/pika_ui_atlas.png");
    assert(data.atlasWidth == 224.0f);
    assert(data.atlasHeight == 96.0f);
    assert(data.atlasSrgb);
}

void theme_resource_rejects_invalid_status_and_missing_admitted_atlas_metadata()
{
    arp::resource::ResourceManager invalidStatusManager;
    auto invalidStatus = pikaThemeDesc();
    invalidStatus.status = text("Backend Validated");
    const auto rejected = arp::ui::loadUiThemeResource(invalidStatusManager, invalidStatus);
    assert(!rejected.success);
    assert(rejected.diagnostics.find("invalid UI theme resource status")
        != arp::core::pmr::String::npos);

    arp::resource::ResourceManager missingAtlasManager;
    auto missingAtlas = pikaThemeDesc();
    const auto missing = arp::ui::loadUiThemeResource(missingAtlasManager, missingAtlas);
    assert(!missing.success);
    assert(missing.diagnostics.find("alias not found") != arp::core::pmr::String::npos);
}

void atlas_metadata_resource_rejects_contract_mismatch()
{
    arp::resource::ResourceManager manager;
    auto desc = pikaAtlasDesc();
    desc.expectedAtlasResourceId = text("texture/not_declared_by_theme");
    const auto rejected = arp::ui::loadUiAtlasMetadataResource(manager, desc);
    assert(!rejected.success);
    assert(rejected.diagnostics.find("UI atlas metadata resourceId does not match")
        != arp::core::pmr::String::npos);
}

} // namespace

int main()
{
    atlas_metadata_resource_loads_explicit_yaml_contract_through_resource_manager();
    theme_resource_loads_explicit_yaml_contract_through_resource_manager();
    theme_resource_rejects_invalid_status_and_missing_admitted_atlas_metadata();
    atlas_metadata_resource_rejects_contract_mismatch();
    return 0;
}
