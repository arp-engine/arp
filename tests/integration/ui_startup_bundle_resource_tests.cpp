#include "core/memory/cpu_allocator.h"
#include "renderer/ui/ui_atlas_texture_resource.h"
#include "renderer/ui/ui_startup_bundle_resource.h"
#include "resource/resource_manager.h"
#include "ui/ui_atlas_image_resource.h"
#include "ui/ui_atlas_metadata_resource.h"
#include "ui/ui_document_resource.h"
#include "ui/ui_font_set_resource.h"
#include "ui/ui_theme_resource.h"

#include <cassert>
#include <filesystem>
#include <type_traits>

static_assert(std::is_base_of_v<arp::resource::ResourceBase, arp::ui::UiAtlasMetadataResource>);
static_assert(std::is_base_of_v<arp::resource::ResourceBase, arp::ui::UiThemeResource>);
static_assert(std::is_base_of_v<arp::resource::ResourceBase, arp::ui::UiAtlasImageResource>);
static_assert(std::is_base_of_v<arp::resource::ResourceBase, arp::ui::UiFontSetResource>);
static_assert(std::is_base_of_v<arp::resource::ResourceBase, arp::ui::UiDocumentResource>);
static_assert(
    std::is_base_of_v<arp::resource::ResourceBase, arp::renderer::ui::UiAtlasTextureResource>);
static_assert(
    std::is_base_of_v<arp::resource::ResourceBase, arp::renderer::ui::UiStartupBundleResource>);

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

std::filesystem::path projectRoot()
{
    auto current = std::filesystem::current_path();
    while (!current.empty()) {
        if (std::filesystem::exists(current / "assets" / "pika" / "ui" / "hud.arpui")) {
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

void admitBundleChildren(arp::resource::ResourceManager& manager)
{
    const auto root = projectRoot();
    const auto ui = root / "assets" / "pika" / "ui";

    arp::ui::UiAtlasMetadataResourceLoadDesc metadata;
    metadata.resourceId = text("6cf0e0f0-d372-42eb-9e7c-78c5ba8eae1a");
    metadata.alias = text("ui/pika/atlas/metadata");
    metadata.status = text("Contract Complete");
    metadata.atlasMetadataPath = ui / "pika_ui_atlas.arpui";
    metadata.expectedAtlasResourceId = text("texture/pika_ui_atlas");
    assert(arp::ui::loadUiAtlasMetadataResource(manager, metadata).success);

    arp::ui::UiThemeResourceLoadDesc theme;
    theme.resourceId = text("8d958aa4-f406-4bc7-9eb4-4cc6cc15c8fe");
    theme.alias = text("ui/pika/theme/default");
    theme.status = text("Contract Complete");
    theme.themePath = ui / "theme.arpui";
    theme.atlasMetadataPath = ui / "pika_ui_atlas.arpui";
    theme.atlasMetadataAlias = text("ui/pika/atlas/metadata");
    assert(arp::ui::loadUiThemeResource(manager, theme).success);

    arp::ui::UiAtlasImageResourceLoadDesc image;
    image.resourceId = text("2d844cf4-4902-48ba-b905-839fd7ad6db5");
    image.alias = text("ui/pika/atlas/image");
    image.status = text("Prototype Complete");
    image.atlasMetadataAlias = text("ui/pika/atlas/metadata");
    image.sourceRoot = root;
    assert(arp::ui::loadUiAtlasImageResource(manager, image).success);

    arp::renderer::ui::UiAtlasTextureResourceLoadDesc texture;
    texture.resourceId = text("7e3a24b0-222b-4cbb-a51b-b9a5e132da3b");
    texture.alias = text("ui/pika/atlas/texture");
    texture.status = text("Prototype Complete");
    texture.atlasImageAlias = text("ui/pika/atlas/image");
    texture.intent.resourceUsage = text("ui.atlas.sampled");
    texture.intent.uploadPolicy = text("renderer-owned-staging");
    texture.intent.requiredFormat = text("Rgba8Unorm");
    texture.intent.requiredDescriptorSet = text("UiAtlasSet");
    assert(arp::renderer::ui::loadUiAtlasTextureResource(manager, texture).success);

    arp::ui::UiFontSetResourceLoadDesc fontSet;
    fontSet.resourceId = text("9e34fd0c-4ac4-4b2a-b29f-6005c7acae0b");
    fontSet.alias = text("ui/pika/font-set/default");
    fontSet.status = text("Contract Complete");
    fontSet.atlasMetadataAlias = text("ui/pika/atlas/metadata");
    fontSet.requiredGlyphPolicy = text("hybrid");
    fontSet.requiredPagePolicy = text("singleTexture");
    assert(arp::ui::loadUiFontSetResource(manager, fontSet).success);

    arp::ui::UiDocumentResourceLoadDesc hud;
    hud.resourceId = text("c28d4d95-953b-4c4c-9631-0c54d44db13a");
    hud.alias = text("ui/pika/document/hud");
    hud.status = text("Prototype Complete");
    hud.documentPath = ui / "hud.arpui";
    hud.documentContract = text("ui.document/v1");
    assert(arp::ui::loadUiDocumentResource(manager, hud).success);
}

void startup_bundle_requires_explicit_dependency_graph()
{
    arp::resource::ResourceManager manager;
    admitBundleChildren(manager);

    arp::renderer::ui::UiStartupBundleResourceLoadDesc desc;
    desc.resourceId = text("b5895b6e-3cd3-49cb-aa1d-cf640b3156ce");
    desc.alias = text("ui/pika/startup/visible");
    desc.status = text("Contract Complete");
    desc.themeAlias = text("ui/pika/theme/default");
    desc.atlasMetadataAlias = text("ui/pika/atlas/metadata");
    desc.atlasImageAlias = text("ui/pika/atlas/image");
    desc.atlasTextureAlias = text("ui/pika/atlas/texture");
    desc.fontSetAlias = text("ui/pika/font-set/default");
    desc.documentAliases.push_back(text("ui/pika/document/hud"));

    const auto loaded = arp::renderer::ui::loadUiStartupBundleResource(manager, desc);
    assert(loaded.success);
    assert(loaded.resource.resourceType == text("UiStartupBundleResource"));

    const auto handle = manager.findByAlias<arp::renderer::ui::UiStartupBundleResource>(
        text("ui/pika/startup/visible"));
    assert(handle.ready());
    assert(handle.get()->data->documents.size() == 1u);
    assert(handle.get()->data->theme != nullptr);
    assert(handle.get()->data->atlasTexture != nullptr);
    assert(handle.get()->data->fontSet != nullptr);
}

void startup_bundle_fails_on_missing_child_alias()
{
    arp::resource::ResourceManager manager;
    arp::renderer::ui::UiStartupBundleResourceLoadDesc desc;
    desc.resourceId = text("b5895b6e-3cd3-49cb-aa1d-cf640b3156ce");
    desc.alias = text("ui/pika/startup/visible");
    desc.status = text("Contract Complete");
    desc.themeAlias = text("ui/pika/theme/default");
    const auto loaded = arp::renderer::ui::loadUiStartupBundleResource(manager, desc);
    assert(!loaded.success);
    assert(loaded.diagnostics.find("alias not found") != arp::core::pmr::String::npos);
}

} // namespace

int main()
{
    startup_bundle_requires_explicit_dependency_graph();
    startup_bundle_fails_on_missing_child_alias();
    return 0;
}
