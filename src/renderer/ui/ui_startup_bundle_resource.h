#pragma once

#include "core/memory/cpu_allocator.h"
#include "renderer/ui/ui_atlas_texture_resource.h"
#include "resource/resource_manager.h"
#include "ui/ui_atlas_image_resource.h"
#include "ui/ui_atlas_metadata_resource.h"
#include "ui/ui_document_resource.h"
#include "ui/ui_font_set_resource.h"
#include "ui/ui_resource_contracts.h"
#include "ui/ui_theme_resource.h"

#include <memory>
#include <utility>

namespace arp::renderer::ui {

inline constexpr const char* kUiStartupBundleResourceType =
    arp::ui::contracts::kStartupBundle.type.data();
inline constexpr const char* kUiStartupBundleResourceContract =
    arp::ui::contracts::kStartupBundle.contractVersion.data();

struct UiStartupBundleResourceData {
    std::shared_ptr<const arp::ui::UiThemeResourceData> theme;
    std::shared_ptr<const arp::ui::UiAtlasMetadataResourceData> atlasMetadata;
    std::shared_ptr<const arp::ui::UiAtlasImageResourceData> atlasImage;
    std::shared_ptr<const UiAtlasTextureResourceData> atlasTexture;
    std::shared_ptr<const arp::ui::UiFontSetResourceData> fontSet;
    arp::core::pmr::Vector<std::shared_ptr<const arp::ui::UiDocumentResourceData>> documents{
        &arp::core::defaultCpuMemoryResource()};
};

struct UiStartupBundleResourceLoadDesc {
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String alias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String status{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String themeAlias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String atlasMetadataAlias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String atlasImageAlias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String atlasTextureAlias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String fontSetAlias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<arp::core::pmr::String> documentAliases{
        &arp::core::defaultCpuMemoryResource()};
};

struct UiStartupBundleResource : public arp::resource::ResourceBase {
    UiStartupBundleResource() = default;

    explicit UiStartupBundleResource(UiStartupBundleResourceLoadDesc desc)
        : arp::resource::ResourceBase(arp::resource::ResourceReference{
              desc.resourceId,
              arp::core::pmr::String(
                  kUiStartupBundleResourceType, &arp::core::defaultCpuMemoryResource()),
              desc.alias,
              desc.status,
          }),
          loadDesc(std::move(desc))
    {}

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& resourceManager,
        const arp::resource::ResourceLoadContext& context) override;

    std::shared_ptr<UiStartupBundleResourceData> data;
    UiStartupBundleResourceLoadDesc loadDesc;
};

struct UiStartupBundleResourceLoadResult {
    bool success = false;
    arp::resource::ResourceLoadResult resource;
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

UiStartupBundleResourceLoadResult loadUiStartupBundleResource(
    arp::resource::ResourceManager& resourceManager, const UiStartupBundleResourceLoadDesc& desc);

} // namespace arp::renderer::ui
