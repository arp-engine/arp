#include "core/memory/cpu_allocator.h"
#include "resource/resource_manager.h"
#include "ui/ui_atlas_metadata_resource.h"
#include "ui/ui_font_set_resource.h"

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

void admitPikaAtlasMetadata(arp::resource::ResourceManager& manager)
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
}

void font_set_resource_loads_explicit_font_metadata()
{
    arp::resource::ResourceManager manager;
    admitPikaAtlasMetadata(manager);

    arp::ui::UiFontSetResourceLoadDesc desc;
    desc.resourceId = text("9e34fd0c-4ac4-4b2a-b29f-6005c7acae0b");
    desc.alias = text("ui/pika/font-set/default");
    desc.status = text("Contract Complete");
    desc.atlasMetadataAlias = text("ui/pika/atlas/metadata");
    desc.requiredGlyphPolicy = text("hybrid");
    desc.requiredPagePolicy = text("singleTexture");

    const auto loaded = arp::ui::loadUiFontSetResource(manager, desc);
    assert(loaded.success);
    assert(loaded.resource.resourceType == text("UiFontSetResource"));

    const auto handle =
        manager.findByAlias<arp::ui::UiFontSetResource>(text("ui/pika/font-set/default"));
    assert(handle.ready());
    assert(handle.get() != nullptr);
    assert(handle.get()->data != nullptr);
    assert(handle.get()->data->fonts.size() == 4u);
    assert(handle.get()->data->glyphPolicy == text("hybrid"));
    assert(handle.get()->data->pagePolicy == text("singleTexture"));
    assert(handle.get()->data->fonts[0].tokenId == text("font.ui.body"));
    assert(!handle.get()->data->fonts[0].sourcePath.empty());
    assert(handle.get()->data->fonts[0].fallbackChain.size() == 3u);
    assert(handle.get()->data->fonts[0].supportedScripts.size() == 1u);
    assert(handle.get()->data->fonts[0].supportedScripts[0] == text("Latin"));
    assert(handle.get()->data->fonts[0].charset == text("pika.visible.ascii"));
}

void font_set_resource_rejects_missing_metadata_and_policy_mismatch()
{
    arp::resource::ResourceManager missingManager;
    arp::ui::UiFontSetResourceLoadDesc missingDesc;
    missingDesc.resourceId = text("9e34fd0c-4ac4-4b2a-b29f-6005c7acae0b");
    missingDesc.alias = text("ui/pika/font-set/default");
    missingDesc.status = text("Contract Complete");
    missingDesc.atlasMetadataAlias = text("ui/pika/atlas/metadata");
    const auto missing = arp::ui::loadUiFontSetResource(missingManager, missingDesc);
    assert(!missing.success);
    assert(missing.diagnostics.find("alias not found") != arp::core::pmr::String::npos);

    arp::resource::ResourceManager mismatchManager;
    admitPikaAtlasMetadata(mismatchManager);
    auto mismatchDesc = missingDesc;
    mismatchDesc.requiredGlyphPolicy = text("static-only");
    mismatchDesc.requiredPagePolicy = text("singleTexture");
    const auto mismatch = arp::ui::loadUiFontSetResource(mismatchManager, mismatchDesc);
    assert(!mismatch.success);
    assert(mismatch.diagnostics.find("glyph policy mismatch") != arp::core::pmr::String::npos);
}

} // namespace

int main()
{
    font_set_resource_loads_explicit_font_metadata();
    font_set_resource_rejects_missing_metadata_and_policy_mismatch();
    return 0;
}
