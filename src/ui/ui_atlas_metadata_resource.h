#pragma once

#include "core/memory/cpu_allocator.h"
#include "resource/resource_manager.h"
#include "ui/ui_font.h"
#include "ui/ui_resource_contracts.h"

#include <filesystem>
#include <memory>
#include <utility>

namespace arp::ui {

inline constexpr const char* kUiAtlasMetadataResourceType = contracts::kAtlasMetadata.type.data();
inline constexpr const char* kUiAtlasResourceContract =
    contracts::kAtlasMetadata.contractVersion.data();

struct UiAtlasSliceMetadata {
    arp::core::pmr::String sliceId{&arp::core::defaultCpuMemoryResource()};
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct UiAtlasMetadataResourceData {
    arp::core::pmr::String atlasId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
    std::filesystem::path sourcePath;
    float width = 0.0f;
    float height = 0.0f;
    bool srgb = true;
    arp::core::pmr::Vector<UiAtlasSliceMetadata> slices{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<UiFontFaceDesc> fontFaces{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String glyphPolicy{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String pagePolicy{&arp::core::defaultCpuMemoryResource()};
};

struct UiAtlasMetadataResourceLoadDesc {
    std::filesystem::path atlasMetadataPath;
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String alias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String status{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String expectedAtlasResourceId{&arp::core::defaultCpuMemoryResource()};
};

struct UiAtlasMetadataResource : public arp::resource::ResourceBase {
    UiAtlasMetadataResource() = default;

    explicit UiAtlasMetadataResource(UiAtlasMetadataResourceLoadDesc desc)
        : arp::resource::ResourceBase(arp::resource::ResourceReference{
              desc.resourceId,
              arp::core::pmr::String(
                  kUiAtlasMetadataResourceType, &arp::core::defaultCpuMemoryResource()),
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

    std::shared_ptr<UiAtlasMetadataResourceData> data;
    UiAtlasMetadataResourceLoadDesc loadDesc;
};

struct UiAtlasMetadataResourceLoadResult {
    bool success = false;
    arp::resource::ResourceLoadResult resource;
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

UiAtlasMetadataResourceLoadResult loadUiAtlasMetadataResource(
    arp::resource::ResourceManager& resourceManager, const UiAtlasMetadataResourceLoadDesc& desc);

} // namespace arp::ui
