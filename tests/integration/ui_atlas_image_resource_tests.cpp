#include "core/cpu_allocator.h"
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

arp::ui::UiAtlasMetadataResourceLoadDesc pikaAtlasMetadataDesc()
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

arp::ui::UiAtlasImageResourceLoadDesc pikaAtlasImageDesc()
{
    arp::ui::UiAtlasImageResourceLoadDesc desc;
    desc.resourceId = text("2d844cf4-4902-48ba-b905-839fd7ad6db5");
    desc.alias = text("ui/pika/atlas/image");
    desc.status = text("Prototype Complete");
    desc.atlasMetadataAlias = text("ui/pika/atlas/metadata");
    desc.sourceRoot = projectRoot();
    return desc;
}

void atlas_image_resource_decodes_declared_metadata_image()
{
    arp::resource::ResourceManager manager;
    const auto metadataLoaded =
        arp::ui::loadUiAtlasMetadataResource(manager, pikaAtlasMetadataDesc());
    assert(metadataLoaded.success);

    const auto imageLoaded = arp::ui::loadUiAtlasImageResource(manager, pikaAtlasImageDesc());
    assert(imageLoaded.success);
    assert(imageLoaded.resource.resourceType == text("UiAtlasImageResource"));

    const auto handle =
        manager.findByAlias<arp::ui::UiAtlasImageResource>(text("ui/pika/atlas/image"));
    assert(handle.ready());
    assert(handle.get() != nullptr);
    assert(handle.get()->data != nullptr);

    const auto& data = *handle.get()->data;
    assert(data.atlasResourceId == text("texture/pika_ui_atlas"));
    assert(data.width == 224u);
    assert(data.height == 96u);
    assert(data.srgb);
    assert(data.rgba8.size() == data.width * data.height * 4u);
}

void atlas_image_resource_rejects_missing_metadata_and_image()
{
    arp::resource::ResourceManager missingMetadataManager;
    const auto missingMetadata =
        arp::ui::loadUiAtlasImageResource(missingMetadataManager, pikaAtlasImageDesc());
    assert(!missingMetadata.success);
    assert(missingMetadata.diagnostics.find("alias not found") != arp::core::pmr::String::npos);

    arp::resource::ResourceManager missingImageManager;
    const auto metadataLoaded =
        arp::ui::loadUiAtlasMetadataResource(missingImageManager, pikaAtlasMetadataDesc());
    assert(metadataLoaded.success);
    auto imageDesc = pikaAtlasImageDesc();
    imageDesc.sourceRoot = projectRoot() / "assets" / "pika" / "missing-root";
    const auto missingImage = arp::ui::loadUiAtlasImageResource(missingImageManager, imageDesc);
    assert(!missingImage.success);
    assert(missingImage.diagnostics.find("UI atlas image source file is missing")
        != arp::core::pmr::String::npos);
}

} // namespace

int main()
{
    atlas_image_resource_decodes_declared_metadata_image();
    atlas_image_resource_rejects_missing_metadata_and_image();
    return 0;
}
