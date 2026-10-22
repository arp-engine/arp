#pragma once

#include "core/memory/cpu_allocator.h"
#include "resource/resource_manager.h"
#include "ui/ui_atlas_metadata_resource.h"
#include "ui/ui_document.h"
#include "ui/ui_font.h"
#include "ui/ui_resource_contracts.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <utility>

namespace arp::ui {

inline constexpr const char* kUiThemeResourceType = contracts::kTheme.type.data();
inline constexpr const char* kUiThemeResourceContract = contracts::kTheme.contractVersion.data();

struct UiThemeResourceData {
    UiTheme theme;
    arp::core::pmr::Vector<UiFontFaceDesc> fonts{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::UnorderedMap<arp::core::pmr::String, UiControlColorValue> styleColors{
        &arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::UnorderedMap<arp::core::pmr::String, arp::core::pmr::String> styleFonts{
        &arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String atlasResourceId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String atlasMetadataAlias{&arp::core::defaultCpuMemoryResource()};
    std::filesystem::path atlasSourcePath;
    float atlasWidth = 0.0f;
    float atlasHeight = 0.0f;
    bool atlasSrgb = true;
};

struct UiThemeResourceLoadDesc {
    std::filesystem::path themePath;
    std::filesystem::path atlasMetadataPath;
    arp::core::pmr::String atlasMetadataAlias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String alias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String status{&arp::core::defaultCpuMemoryResource()};
};

struct UiThemeResource : public arp::resource::ResourceBase {
    UiThemeResource() = default;

    explicit UiThemeResource(UiThemeResourceLoadDesc desc)
        : arp::resource::ResourceBase(arp::resource::ResourceReference{
              desc.resourceId,
              arp::core::pmr::String(kUiThemeResourceType, &arp::core::defaultCpuMemoryResource()),
              desc.alias,
              desc.status,
          }),
          loadDesc(std::move(desc))
    {}

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& resourceManager,
        const arp::resource::ResourceLoadContext& context) override;
    bool collectFileReadIntents(arp::resource::ResourceManager& resourceManager,
        arp::core::pmr::Vector<arp::resource::ResourceFileReadIntent>& intents,
        arp::core::pmr::String& diagnostics) const override;

    arp::resource::ResourceGraphStage processingStage() const noexcept override
    {
        return arp::resource::ResourceGraphStage::YamlParse;
    }

    std::shared_ptr<UiThemeResourceData> data;
    UiThemeResourceLoadDesc loadDesc;
};

struct UiThemeResourceLoadResult {
    bool success = false;
    arp::resource::ResourceLoadResult resource;
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

UiThemeResourceLoadResult loadUiThemeResource(
    arp::resource::ResourceManager& resourceManager, const UiThemeResourceLoadDesc& desc);

} // namespace arp::ui
