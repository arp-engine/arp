#pragma once

#include "core/memory/cpu_allocator.h"
#include "resource/resource_manager.h"
#include "ui/ui_font.h"
#include "ui/ui_resource_contracts.h"

#include <filesystem>
#include <memory>
#include <utility>

namespace arp::ui {

inline constexpr const char* kUiFontSetResourceType = contracts::kFontSet.type.data();
inline constexpr const char* kUiFontSetResourceContract =
    contracts::kFontSet.contractVersion.data();

struct UiFontSetResourceData {
    arp::core::pmr::Vector<UiFontFaceDesc> fonts{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String glyphPolicy{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String pagePolicy{&arp::core::defaultCpuMemoryResource()};
};

struct UiFontSetResourceLoadDesc {
    // Optional explicit root for relative font source paths declared by the
    // atlas metadata. When set, the resolved face paths are owned by the
    // resource and no cwd/project-root discovery is performed downstream.
    std::filesystem::path sourceRoot;
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String alias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String status{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String atlasMetadataAlias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String requiredGlyphPolicy{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String requiredPagePolicy{&arp::core::defaultCpuMemoryResource()};
};

struct UiFontSetResource : public arp::resource::ResourceBase {
    UiFontSetResource() = default;

    explicit UiFontSetResource(UiFontSetResourceLoadDesc desc)
        : arp::resource::ResourceBase(arp::resource::ResourceReference{
              desc.resourceId,
              arp::core::pmr::String(
                  kUiFontSetResourceType, &arp::core::defaultCpuMemoryResource()),
              desc.alias,
              desc.status,
          }),
          loadDesc(std::move(desc))
    {}

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& resourceManager,
        const arp::resource::ResourceLoadContext& context) override;

    std::shared_ptr<UiFontSetResourceData> data;
    UiFontSetResourceLoadDesc loadDesc;
};

struct UiFontSetResourceLoadResult {
    bool success = false;
    arp::resource::ResourceLoadResult resource;
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

UiFontSetResourceLoadResult loadUiFontSetResource(
    arp::resource::ResourceManager& resourceManager, const UiFontSetResourceLoadDesc& desc);

} // namespace arp::ui
