#include "core/memory/cpu_allocator.h"
#include "renderer/ui/ui_atlas_texture_resource.h"
#include "resource/resource_manager.h"
#include "ui/ui_atlas_image_resource.h"
#include "ui/ui_atlas_metadata_resource.h"

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

void admitPikaAtlasImage(arp::resource::ResourceManager& manager)
{
    const auto root = projectRoot();
    arp::ui::UiAtlasMetadataResourceLoadDesc metadataDesc;
    metadataDesc.resourceId = text("6cf0e0f0-d372-42eb-9e7c-78c5ba8eae1a");
    metadataDesc.alias = text("ui/pika/atlas/metadata");
    metadataDesc.status = text("Contract Complete");
    metadataDesc.atlasMetadataPath = root / "assets" / "pika" / "ui" / "pika_ui_atlas.arpui";
    metadataDesc.expectedAtlasResourceId = text("texture/pika_ui_atlas");
    const auto metadataLoaded = arp::ui::loadUiAtlasMetadataResource(manager, metadataDesc);
    assert(metadataLoaded.success);

    arp::ui::UiAtlasImageResourceLoadDesc imageDesc;
    imageDesc.resourceId = text("2d844cf4-4902-48ba-b905-839fd7ad6db5");
    imageDesc.alias = text("ui/pika/atlas/image");
    imageDesc.status = text("Prototype Complete");
    imageDesc.atlasMetadataAlias = text("ui/pika/atlas/metadata");
    imageDesc.sourceRoot = root;
    const auto imageLoaded = arp::ui::loadUiAtlasImageResource(manager, imageDesc);
    assert(imageLoaded.success);
}

void atlas_texture_resource_lowers_image_payload_to_renderer_facing_upload_intent()
{
    arp::resource::ResourceManager manager;
    admitPikaAtlasImage(manager);

    arp::renderer::ui::UiAtlasTextureResourceLoadDesc textureDesc;
    textureDesc.resourceId = text("7e3a24b0-222b-4cbb-a51b-b9a5e132da3b");
    textureDesc.alias = text("ui/pika/atlas/texture");
    textureDesc.status = text("Prototype Complete");
    textureDesc.atlasImageAlias = text("ui/pika/atlas/image");
    textureDesc.intent.resourceUsage = text("ui.atlas.sampled");
    textureDesc.intent.uploadPolicy = text("renderer-owned-staging");
    textureDesc.intent.requiredFormat = text("Rgba8Unorm");
    textureDesc.intent.requiredDescriptorSet = text("UiAtlasSet");

    const auto loaded = arp::renderer::ui::loadUiAtlasTextureResource(manager, textureDesc);
    assert(loaded.success);
    assert(loaded.resource.resourceType == text("UiAtlasTextureResource"));

    const auto handle = manager.findByAlias<arp::renderer::ui::UiAtlasTextureResource>(
        text("ui/pika/atlas/texture"));
    assert(handle.ready());
    assert(handle.get() != nullptr);
    assert(handle.get()->data != nullptr);
    assert(handle.get()->data->intent.backendOwnedUploadStaging);
    assert(!handle.get()->data->exposesNativeBackendHandles);
    assert(handle.get()->data->atlasTexture.valid());
    assert(
        handle.get()->data->atlasTexture.resourceId == std::pmr::string("texture/pika_ui_atlas"));
}

void atlas_texture_resource_rejects_missing_image_alias()
{
    arp::resource::ResourceManager manager;
    arp::renderer::ui::UiAtlasTextureResourceLoadDesc textureDesc;
    textureDesc.resourceId = text("7e3a24b0-222b-4cbb-a51b-b9a5e132da3b");
    textureDesc.alias = text("ui/pika/atlas/texture");
    textureDesc.status = text("Prototype Complete");
    textureDesc.atlasImageAlias = text("ui/pika/atlas/image");

    const auto loaded = arp::renderer::ui::loadUiAtlasTextureResource(manager, textureDesc);
    assert(!loaded.success);
    assert(loaded.diagnostics.find("alias not found") != arp::core::pmr::String::npos);
}

} // namespace

int main()
{
    atlas_texture_resource_lowers_image_payload_to_renderer_facing_upload_intent();
    atlas_texture_resource_rejects_missing_image_alias();
    return 0;
}
