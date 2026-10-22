#pragma once

#include "core/memory/cpu_allocator.h"
#include "renderer/render_frame.h"
#include "resource/resource_manager.h"
#include "ui/ui_resource_contracts.h"

#include <memory>
#include <utility>

namespace arp::renderer::ui {

inline constexpr const char* kUiAtlasTextureResourceType =
    arp::ui::contracts::kAtlasTexture.type.data();
inline constexpr const char* kUiAtlasTextureResourceContract =
    arp::ui::contracts::kAtlasTexture.contractVersion.data();

struct UiAtlasTextureUploadIntent {
    arp::core::pmr::String resourceUsage{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String uploadPolicy{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String requiredFormat{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String requiredDescriptorSet{&arp::core::defaultCpuMemoryResource()};
    bool backendOwnedUploadStaging = false;
};

struct UiAtlasTextureResourceData {
    UiAtlasTextureUploadIntent intent;
    renderer::GuiAtlasTextureDesc atlasTexture;
    bool exposesNativeBackendHandles = false;
};

struct UiAtlasTextureResourceLoadDesc {
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String alias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String status{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String atlasImageAlias{&arp::core::defaultCpuMemoryResource()};
    UiAtlasTextureUploadIntent intent;
};

struct UiAtlasTextureResource : public arp::resource::ResourceBase {
    UiAtlasTextureResource() = default;

    explicit UiAtlasTextureResource(UiAtlasTextureResourceLoadDesc desc)
        : arp::resource::ResourceBase(arp::resource::ResourceReference{
              desc.resourceId,
              arp::core::pmr::String(
                  kUiAtlasTextureResourceType, &arp::core::defaultCpuMemoryResource()),
              desc.alias,
              desc.status,
          }),
          loadDesc(std::move(desc))
    {}

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& resourceManager,
        const arp::resource::ResourceLoadContext& context) override;

    std::shared_ptr<UiAtlasTextureResourceData> data;
    UiAtlasTextureResourceLoadDesc loadDesc;
};

struct UiAtlasTextureResourceLoadResult {
    bool success = false;
    arp::resource::ResourceLoadResult resource;
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

UiAtlasTextureResourceLoadResult loadUiAtlasTextureResource(
    arp::resource::ResourceManager& resourceManager, const UiAtlasTextureResourceLoadDesc& desc);

} // namespace arp::renderer::ui
