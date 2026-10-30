#pragma once

#include "core/memory/cpu_allocator.h"
#include "resource/resource_manager.h"
#include "ui/ui_resource_contracts.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>

namespace arp::ui {

inline constexpr const char* kUiAtlasImageResourceType = contracts::kAtlasImage.type.data();
inline constexpr const char* kUiAtlasImageResourceContract =
    contracts::kAtlasImage.contractVersion.data();

struct UiAtlasImageResourceData {
    arp::core::pmr::String atlasResourceId{&arp::core::defaultCpuMemoryResource()};
    std::filesystem::path sourcePath;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    bool srgb = true;
    arp::core::pmr::Vector<std::uint8_t> rgba8{&arp::core::defaultCpuMemoryResource()};
};

struct UiAtlasImageResourceLoadDesc {
    std::filesystem::path sourceRoot;
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String alias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String status{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String atlasMetadataAlias{&arp::core::defaultCpuMemoryResource()};
    std::size_t maxEncodedBytes = 64u * 1024u * 1024u;
    std::size_t maxDecodedBytes = 64u * 1024u * 1024u;
    arp::resource::ResourceFileOrigin origin;
};

struct UiAtlasImageResource : public arp::resource::ResourceBase {
    UiAtlasImageResource() = default;

    explicit UiAtlasImageResource(UiAtlasImageResourceLoadDesc desc)
        : arp::resource::ResourceBase(arp::resource::ResourceReference{
              desc.resourceId,
              arp::core::pmr::String(
                  kUiAtlasImageResourceType, &arp::core::defaultCpuMemoryResource()),
              desc.alias,
              desc.status,
          }),
          loadDesc(std::move(desc))
    {}

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& resourceManager,
        const arp::resource::ResourceLoadContext& context) override;
    void appendContentFingerprint(core::StableHash64& hash) const noexcept override;
    bool collectFileReadIntents(arp::resource::ResourceManager& resourceManager,
        arp::core::pmr::Vector<arp::resource::ResourceFileReadIntent>& intents,
        arp::core::pmr::String& diagnostics) const override;

    arp::resource::ResourceGraphStage processingStage() const noexcept override
    {
        return arp::resource::ResourceGraphStage::ImageDecode;
    }

    std::size_t processedByteCount() const noexcept override
    {
        return data ? data->rgba8.size() : 0u;
    }

    std::shared_ptr<UiAtlasImageResourceData> data;
    UiAtlasImageResourceLoadDesc loadDesc;
};

struct UiAtlasImageResourceLoadResult {
    bool success = false;
    arp::resource::ResourceLoadResult resource;
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

UiAtlasImageResourceLoadResult loadUiAtlasImageResource(
    arp::resource::ResourceManager& resourceManager, const UiAtlasImageResourceLoadDesc& desc);

} // namespace arp::ui
