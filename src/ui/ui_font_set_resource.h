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
    // Empty mounted root denotes the mount root. Empty host root preserves the
    // declared relative path; core IO opens that literal path without discovery.
    std::filesystem::path sourceRoot;
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String alias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String status{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String atlasMetadataAlias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String requiredGlyphPolicy{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String requiredPagePolicy{&arp::core::defaultCpuMemoryResource()};
    std::size_t maxFaces = 32;
    std::size_t maxFontBytes = 64u * 1024u * 1024u;
    std::size_t maxTotalSourceBytes = 64u * 1024u * 1024u;
    arp::resource::ResourceFileOrigin origin;
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
    void appendContentFingerprint(core::StableHash64& hash) const noexcept override;
    bool collectFileReadIntents(arp::resource::ResourceManager& resourceManager,
        core::pmr::Vector<arp::resource::ResourceFileReadIntent>& intents,
        core::pmr::String& diagnostic) const override;

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
